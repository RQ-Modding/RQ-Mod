/* Copyright (C) 2026 Rance Quest modding fork
 *
 * Skill hover: wrap スキル_説明文１ so Lua can append lines (rq.skill).
 */

#ifdef RQ_MODLOADER

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "lua.h"
#include "lauxlib.h"

#include "system4.h"
#include "system4/ain.h"
#include "system4/buffer.h"
#include "system4/instructions.h"
#include "system4/string.h"
#include "system4/utfsjis.h"

#include "modloader.h"
#include "modloader_priv.h"
#include "vm.h"
#include "vm/heap.h"

static int g_on;
static int g_boot;
static int g_boot_done;
static int g_sort_on;
#define SKILL_SORT_MAX 32
static int g_sort_id[SKILL_SORT_MAX];
static int g_sort_as[SKILL_SORT_MAX];
static int g_sort_n;
static void skill_sort_args(void);
static int g_describe_ref = LUA_NOREF;
#define SKILL_DEF_MAX 32
static int g_def_refs[SKILL_DEF_MAX];
static int g_def_n;
static int g_f_init = -1;
static int g_f_flavor = -1;
static int g_f_type = -1;
static int g_f_act = -1;
static int g_f_panel = -1;
static int g_f_flat = -1;
static int g_f_cap = -1;
static int g_f_basic = -1;
static int g_f_weapon = -1;
static int g_f_ef = -1;
static int g_f_kouka2 = -1;
static int g_f_kouka3 = -1;
static int g_f_kouka4 = -1;
static int g_fno_name = -1;
static int g_fno_kinou = -1;
static int g_fno_bairitsu = -1;
static int g_fno_target = -1;
static int g_fno_desc1 = -1;
static int g_fno_koudou = -1;
static int g_fno_kouka = -1;

static void emit0(struct buffer *out, enum opcode op)
{
	buffer_write_int16(out, (uint16_t)op);
}

static void emit1(struct buffer *out, enum opcode op, int32_t arg)
{
	buffer_write_int16(out, (uint16_t)op);
	buffer_write_int32(out, (uint32_t)arg);
}

static int find_fn(struct ain *ain, const char *utf8)
{
	char *sjis = utf2sjis(utf8, 0);
	int fno;
	if (!sjis)
		return -1;
	fno = ain_get_function(ain, sjis);
	free(sjis);
	return fno;
}

static void commit_wrap(struct ain *ain, int fno, struct buffer *out)
{
	ain->functions[fno].address = (uint32_t)(ain->code_size + 6);
	ain->code = xrealloc(ain->code, ain->code_size + out->index);
	memcpy(ain->code + ain->code_size, out->buf, out->index);
	ain->code_size += out->index;
}

static int call_ii(int fno, int a, int b)
{
	int r;
	if (fno < 0)
		return 0;
	stack_push(a);
	stack_push(b);
	vm_call(fno, -1);
	r = stack_pop().i;
	return r;
}

/* String getter: Ｓ名前(id, "") etc. Returns new UTF-8 (caller frees) or "". */
static char *call_get_utf(int fno, int id)
{
	int empty_slot, ret_slot;
	struct string *sj;
	char *utf;

	if (fno < 0)
		return strdup("");
	empty_slot = vm_string_ref(string_ref(&EMPTY_STRING));
	stack_push(id);
	stack_push(empty_slot);
	vm_call(fno, -1);
	ret_slot = stack_pop().i;
	sj = heap_get_string(ret_slot);
	utf = sjis2utf(sj && sj->text ? sj->text : "", 0);
	heap_unref(ret_slot);
	if (!utf)
		return strdup("");
	return utf;
}

static int wrap_describe(struct ain *ain, int fno)
{
	int dup = ain_dup_function(ain, fno);
	struct buffer out;

	buffer_init(&out, NULL, 0);
	emit1(&out, FUNC, fno);
	emit1(&out, SH_LOCALREF, 0);
	emit1(&out, CALLFUNC, dup);
	emit1(&out, CALLSYS, VM_XSYS_SKILL_DESCRIBE);
	emit0(&out, RETURN);
	emit1(&out, ENDFUNC, fno);
	commit_wrap(ain, fno, &out);
	rq_log("skill: wrapped スキル_説明文１ (dup=%d)", dup);
	return 1;
}

static void skill_describe(void)
{
	int base_slot = stack_pop().i;
	int skill_no = local_get(0).i;
	struct string *base_sj;
	char *base_utf = NULL;
	char *out_utf = NULL;
	char *out_sjis = NULL;
	struct string *out_s;
	lua_State *L = rq_lua();

	base_sj = heap_get_string(base_slot);
	base_utf = sjis2utf(base_sj && base_sj->text ? base_sj->text : "", 0);
	if (!base_utf)
		base_utf = strdup("");

	out_utf = base_utf;
	base_utf = NULL;

	if (L && g_describe_ref != LUA_NOREF) {
		lua_rawgeti(L, LUA_REGISTRYINDEX, g_describe_ref);
		lua_pushinteger(L, skill_no);
		lua_pushstring(L, out_utf);
		if (lua_pcall(L, 2, 1, 0) != LUA_OK) {
			rq_log("skill on_describe error: %s", lua_tostring(L, -1));
			lua_pop(L, 1);
		} else if (lua_isstring(L, -1)) {
			const char *ret = lua_tostring(L, -1);
			free(out_utf);
			out_utf = strdup(ret ? ret : "");
			lua_pop(L, 1);
		} else {
			lua_pop(L, 1);
		}
	}

	out_sjis = utf2sjis(out_utf ? out_utf : "", 0);
	free(out_utf);
	heap_unref(base_slot);

	if (!out_sjis) {
		stack_push(vm_string_ref(string_ref(&EMPTY_STRING)));
		return;
	}
	out_s = make_string(out_sjis, strlen(out_sjis));
	free(out_sjis);
	if (!out_s) {
		stack_push(vm_string_ref(string_ref(&EMPTY_STRING)));
		return;
	}
	{
		int slot = heap_alloc_slot(VM_STRING);
		heap[slot].s = out_s;
		stack_push(slot);
	}
}

static int call_ints(int fno, const int *args, int n)
{
	int i;

	if (fno < 0)
		return 0;
	for (i = 0; i < n; i++)
		stack_push(args[i]);
	vm_call(fno, -1);
	if (ain->functions[fno].return_type.data == AIN_VOID)
		return 0;
	return stack_pop().i;
}

static void call_str(int fno, int id, const char *utf)
{
	char *sj;
	struct string *s;
	int slot, ret;

	if (fno < 0)
		return;
	sj = utf2sjis(utf ? utf : "", 0);
	if (!sj)
		sj = strdup("");
	s = make_string(sj, strlen(sj));
	free(sj);
	if (!s) {
		stack_push(id);
		stack_push(vm_string_ref(string_ref(&EMPTY_STRING)));
	} else {
		slot = heap_alloc_slot(VM_STRING);
		heap[slot].s = s;
		stack_push(id);
		stack_push(slot);
	}
	vm_call(fno, -1);
	if (ain->functions[fno].return_type.data == AIN_VOID)
		return;
	ret = stack_pop().i;
	if (ain->functions[fno].return_type.data == AIN_STRING)
		heap_unref(ret);
}

static int field_int(lua_State *L, const char *key, int def)
{
	int v = def;
	lua_getfield(L, -1, key);
	if (lua_isnumber(L, -1))
		v = (int)lua_tointeger(L, -1);
	lua_pop(L, 1);
	return v;
}

static char *field_strdup(lua_State *L, const char *key)
{
	char *out;
	lua_getfield(L, -1, key);
	if (lua_isstring(L, -1)) {
		const char *s = lua_tostring(L, -1);
		out = strdup(s ? s : "");
	} else {
		out = strdup("");
	}
	lua_pop(L, 1);
	return out ? out : strdup("");
}

/* Fill one S[id] the way Ｓ物理登録 / Ｓ能力Ｚ登録 do. Table is on the stack. */
static void apply_define(lua_State *L)
{
	int id, physical, args[2];
	char kind[32];

	lua_getfield(L, -1, "kind");
	kind[0] = 0;
	if (lua_isstring(L, -1)) {
		const char *s = lua_tostring(L, -1);
		snprintf(kind, sizeof kind, "%s", s ? s : "");
	}
	lua_pop(L, 1);
	physical = !strcmp(kind, "physical");
	if (!physical && strcmp(kind, "knowledge") != 0) {
		rq_log("skill.define: kind must be physical or knowledge");
		return;
	}
	id = field_int(L, "id", 0);
	if (id < 1 || id > 1000) {
		rq_log("skill.define: id %d out of range", id);
		return;
	}

	args[0] = id;
	call_ints(g_f_init, args, 1);
	{
		char *name = field_strdup(L, "name");
		char *flavor = field_strdup(L, "flavor");
		char *desc = field_strdup(L, "desc");
		char *ef = field_strdup(L, "ef");
		call_str(g_fno_name, id, name);
		call_str(g_f_flavor, id, flavor);
		call_str(g_fno_desc1, id, desc);
		free(name);
		free(flavor);
		free(desc);
		/* ef applied after the int fields; keep the pointer alive */
		args[0] = id;
		args[1] = physical ? 10 : 30;
		call_ints(g_f_type, args, 2);
		args[1] = physical ? 1 : 0;
		call_ints(g_f_act, args, 2);
		args[1] = physical ? 100 : 105;
		call_ints(g_f_panel, args, 2);
		args[1] = field_int(L, "target", physical ? 1 : 0);
		call_ints(g_fno_target, args, 2);
		args[1] = field_int(L, "bairitsu", physical ? 100 : 0);
		call_ints(g_fno_bairitsu, args, 2);
		args[1] = field_int(L, "flat", 0);
		call_ints(g_f_flat, args, 2);
		args[1] = field_int(L, "kinou", 0);
		call_ints(g_fno_kinou, args, 2);
		args[1] = field_int(L, "kouka", 0);
		call_ints(g_fno_kouka, args, 2);
		args[1] = field_int(L, "kouka2", 0);
		call_ints(g_f_kouka2, args, 2);
		args[1] = field_int(L, "kouka3", 0);
		call_ints(g_f_kouka3, args, 2);
		args[1] = field_int(L, "kouka4", 0);
		call_ints(g_f_kouka4, args, 2);
		args[1] = field_int(L, "cap", 0);
		call_ints(g_f_cap, args, 2);
		args[1] = field_int(L, "basic", physical ? 1 : 0);
		call_ints(g_f_basic, args, 2);
		args[1] = field_int(L, "weapon", 0);
		call_ints(g_f_weapon, args, 2);
		call_str(g_f_ef, id, ef);
		free(ef);
	}
	rq_log("skill.define: id %d %s", id, physical ? "physical" : "knowledge");
}

static void skill_boot(void)
{
	lua_State *L = rq_lua();
	int i;

	if (!L)
		return;
	for (i = 0; i < g_def_n; i++) {
		lua_rawgeti(L, LUA_REGISTRYINDEX, g_def_refs[i]);
		if (lua_istable(L, -1))
			apply_define(L);
		lua_pop(L, 1);
	}
	g_boot_done = 1;
	rq_log("skill: applied %d define(s) after boot register", g_def_n);
}

int rq_skill_syscall(int code)
{
	switch ((enum vm_extra_syscall)code) {
	case VM_XSYS_SKILL_DESCRIBE:
		skill_describe();
		return 1;
	case VM_XSYS_SKILL_BOOT:
		skill_boot();
		return 1;
	case VM_XSYS_SKILL_SORT:
		skill_sort_args();
		return 1;
	default:
		return 0;
	}
}

static int wrap_boot(struct ain *ain, int fno)
{
	int dup = ain_dup_function(ain, fno);
	struct buffer out;

	buffer_init(&out, NULL, 0);
	emit1(&out, FUNC, fno);
	emit1(&out, CALLFUNC, dup);
	emit1(&out, CALLSYS, VM_XSYS_SKILL_BOOT);
	emit0(&out, RETURN);
	emit1(&out, ENDFUNC, fno);
	commit_wrap(ain, fno, &out);
	rq_log("skill: wrapped Ｓ起動時登録 tail (define flush)");
	return 1;
}

static int sort_key(int id)
{
	int i;
	for (i = 0; i < g_sort_n; i++) {
		if (g_sort_id[i] == id)
			return g_sort_as[i];
	}
	return id;
}

/* compSkill compares panel color, then skill id. Map stand-in ids so a
 * rewritten attack keeps Melee's place in the known and potential lists. */
static void skill_sort_args(void)
{
	int b = stack_pop().i;
	int a = stack_pop().i;
	stack_push(sort_key(a));
	stack_push(sort_key(b));
}

static int wrap_sort(struct ain *ain, int fno)
{
	int dup = ain_dup_function(ain, fno);
	struct buffer out;

	buffer_init(&out, NULL, 0);
	emit1(&out, FUNC, fno);
	emit1(&out, SH_LOCALREF, 0);
	emit1(&out, SH_LOCALREF, 1);
	emit1(&out, CALLSYS, VM_XSYS_SKILL_SORT);
	emit1(&out, CALLFUNC, dup);
	emit0(&out, RETURN);
	emit1(&out, ENDFUNC, fno);
	commit_wrap(ain, fno, &out);
	rq_log("skill: wrapped compSkill (sort_as)");
	return 1;
}

int rq_skill_install(struct ain *ain)
{
	int f_desc, f_boot;

	if (!ain)
		return 0;
	if (g_on && g_boot)
		return 1;

	if (!g_on) {
	f_desc = find_fn(ain, "スキル_説明文１");
	g_fno_name = find_fn(ain, "Ｓ名前");
	g_fno_kinou = find_fn(ain, "Ｓ機能");
	g_fno_bairitsu = find_fn(ain, "Ｓ倍率");
	g_fno_target = find_fn(ain, "Ｓ対象");
	g_fno_desc1 = find_fn(ain, "Ｓ説明文１");
	g_fno_koudou = find_fn(ain, "Ｓ行動種類");
	g_fno_kouka = find_fn(ain, "Ｓ効果");

	if (f_desc < 0) {
		rq_log("skill: スキル_説明文１ not found");
		return 0;
	}
	if (!wrap_describe(ain, f_desc))
		return 0;

	g_on = 1;
	rq_log("skill: describe wrap on; getters name=%d kinou=%d bairitsu=%d target=%d desc1=%d koudou=%d kouka=%d",
	       g_fno_name, g_fno_kinou, g_fno_bairitsu, g_fno_target, g_fno_desc1,
	       g_fno_koudou, g_fno_kouka);
	}

	if (!g_boot) {
		f_boot = find_fn(ain, "Ｓ起動時登録");
		g_f_init = find_fn(ain, "Ｓ初期化");
		g_f_flavor = find_fn(ain, "Ｓ表示文");
		g_f_type = find_fn(ain, "Ｓタイプ");
		g_f_act = find_fn(ain, "Ｓ行動種類");
		g_f_panel = find_fn(ain, "Ｓパネル色");
		g_f_flat = find_fn(ain, "Ｓ付与ダメージ");
		g_f_cap = find_fn(ain, "Ｓダメージ上限");
		g_f_basic = find_fn(ain, "Ｓ基本技");
		g_f_weapon = find_fn(ain, "Ｓ発動条件／武器");
		g_f_ef = find_fn(ain, "ＳＥＦ");
		g_f_kouka2 = find_fn(ain, "Ｓ効果２");
		g_f_kouka3 = find_fn(ain, "Ｓ効果３");
		g_f_kouka4 = find_fn(ain, "Ｓ効果４");
		if (f_boot < 0 || g_f_init < 0 || g_f_flavor < 0 || g_f_type < 0
		    || g_f_act < 0 || g_f_panel < 0 || g_f_flat < 0 || g_f_cap < 0
		    || g_f_basic < 0 || g_f_weapon < 0 || g_f_ef < 0
		    || g_fno_name < 0 || g_fno_desc1 < 0 || g_fno_target < 0
		    || g_fno_bairitsu < 0 || g_fno_kinou < 0 || g_fno_kouka < 0
		    || g_f_kouka2 < 0 || g_f_kouka3 < 0 || g_f_kouka4 < 0) {
			rq_log("skill: boot/define getters missing (boot=%d init=%d)",
			       f_boot, g_f_init);
			return 0;
		}
		if (!wrap_boot(ain, f_boot))
			return 0;
		g_boot = 1;
	}
	if (!g_sort_on) {
		int f_cmp = find_fn(ain, "compSkill");
		if (f_cmp < 0) {
			rq_log("skill: compSkill not found");
		} else if (wrap_sort(ain, f_cmp)) {
			g_sort_on = 1;
		}
	}
	return 1;
}

int rq_lua_skill_define(lua_State *L)
{
	luaL_checktype(L, 1, LUA_TTABLE);
	lua_getfield(L, 1, "id");
	if (!lua_isnumber(L, -1))
		return luaL_error(L, "skill.define needs id");
	lua_pop(L, 1);
	if (g_def_n >= SKILL_DEF_MAX)
		return luaL_error(L, "too many rq.skill.define calls");
	lua_pushvalue(L, 1);
	g_def_refs[g_def_n++] = luaL_ref(L, LUA_REGISTRYINDEX);
	if (g_boot_done) {
		lua_rawgeti(L, LUA_REGISTRYINDEX, g_def_refs[g_def_n - 1]);
		apply_define(L);
		lua_pop(L, 1);
	}
	return 0;
}

int rq_lua_skill_sort_as(lua_State *L)
{
	int id = (int)luaL_checkinteger(L, 1);
	int as_id = (int)luaL_checkinteger(L, 2);
	int i;

	if (!g_sort_on) {
		lua_pushboolean(L, 0);
		return 1;
	}
	for (i = 0; i < g_sort_n; i++) {
		if (g_sort_id[i] != id)
			continue;
		if (as_id <= 0 || as_id == id) {
			g_sort_id[i] = g_sort_id[g_sort_n - 1];
			g_sort_as[i] = g_sort_as[g_sort_n - 1];
			g_sort_n--;
		} else {
			g_sort_as[i] = as_id;
		}
		lua_pushboolean(L, 1);
		return 1;
	}
	if (as_id <= 0 || as_id == id) {
		lua_pushboolean(L, 1);
		return 1;
	}
	if (g_sort_n >= SKILL_SORT_MAX)
		return luaL_error(L, "too many skill.sort_as");
	g_sort_id[g_sort_n] = id;
	g_sort_as[g_sort_n] = as_id;
	g_sort_n++;
	lua_pushboolean(L, 1);
	return 1;
}

int rq_lua_skill_on_describe(lua_State *L)
{
	luaL_checktype(L, 1, LUA_TFUNCTION);
	if (g_describe_ref != LUA_NOREF)
		luaL_unref(L, LUA_REGISTRYINDEX, g_describe_ref);
	lua_pushvalue(L, 1);
	g_describe_ref = luaL_ref(L, LUA_REGISTRYINDEX);
	return 0;
}

int rq_lua_skill_get(lua_State *L)
{
	int id = (int)luaL_checkinteger(L, 1);
	char *name, *desc1;

	lua_newtable(L);
	lua_pushinteger(L, id);
	lua_setfield(L, -2, "id");

	name = call_get_utf(g_fno_name, id);
	lua_pushstring(L, name);
	lua_setfield(L, -2, "name");
	free(name);

	lua_pushinteger(L, call_ii(g_fno_kinou, id, -1));
	lua_setfield(L, -2, "kinou");

	lua_pushinteger(L, call_ii(g_fno_bairitsu, id, -1));
	lua_setfield(L, -2, "bairitsu");

	lua_pushinteger(L, call_ii(g_fno_target, id, -1));
	lua_setfield(L, -2, "target");

	/* 行動種類: 1 physical, 2 magic, 3 special, 4 support, 0 ability */
	lua_pushinteger(L, call_ii(g_fno_koudou, id, -1));
	lua_setfield(L, -2, "koudou");

	lua_pushinteger(L, call_ii(g_fno_kouka, id, -1));
	lua_setfield(L, -2, "kouka");

	desc1 = call_get_utf(g_fno_desc1, id);
	lua_pushstring(L, desc1);
	lua_setfield(L, -2, "desc1");
	free(desc1);

	return 1;
}

#endif /* RQ_MODLOADER */
