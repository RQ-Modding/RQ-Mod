/* Copyright (C) 2026 Rance Quest modding fork
 *
 * rq.chara roster API: define (create) and recruit (provide) characters,
 * edit existing ones, observe skill removal, plus rq.store (per-save state).
 *
 * C adds three small hooks and no game logic:
 *   - tail of Ｃ生成／キャラデータ: builds a Lua-defined character per flag
 *     (基本, スキル, ステータス). Base-game numbers fall through untouched.
 *   - tail of Ｃ生成: runs on_create callbacks.
 *   - head of ＣＳ削除: runs on_forget callbacks (cost is still readable).
 * Edits are applied from the end of Ｃ再計算 (rq_roster_apply).
 */

#ifdef RQ_MODLOADER

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>

#include "lua.h"
#include "lauxlib.h"

#include "cJSON.h"
#include "system4.h"
#include "system4/ain.h"
#include "system4/buffer.h"
#include "system4/instructions.h"
#include "system4/string.h"
#include "system4/utfsjis.h"

#include "xsystem4.h"
#include "modloader.h"
#include "modloader_priv.h"
#include "vm.h"
#include "vm/heap.h"
#include "vm/page.h"

#define TPL_MAX 96
#define EDIT_MAX 192
#define CB_MAX 16
#define ID_MAX 64
#define MOD_MAX 48
#define SKILL_SLOT_RANK 568

struct tpl {
	char id[ID_MAX];
	char mod[MOD_MAX];
	int no;
	int ref;
};

struct edit {
	char mod[MOD_MAX];
	int no;
	int ref;
};

static int g_on;
static struct tpl g_tpl[TPL_MAX];
static int g_ntpl;
static struct edit g_edit[EDIT_MAX];
static int g_nedit;
static int g_create_cb[CB_MAX];
static int g_ncreate;
static int g_forget_cb[CB_MAX];
static int g_nforget;
static int g_in_apply;
static int g_lua_forget;

static int f_create = -1, f_cdata = -1, f_base = -1, f_findno = -1, f_del = -1;
static int f_pta_add = -1, f_pta_del = -1, f_state = -1, f_name = -1, f_short = -1;
static int f_facestr = -1, f_line = -1, f_cs_add = -1, f_cs_del = -1, f_cs_find = -1;
static int f_cs_cost = -1, f_cs_rank = -1, f_stype = -1, f_points = -1, f_fa = -1;
static int f_recalc = -1, f_supply = -1, f_list = -1;
static int gi_c = -2;

static const struct { const char *alias; const char *jp; } k_fields[] = {
	{ "no", "番号" }, { "state", "登録状態" }, { "guest", "ゲスト" },
	{ "locked", "削除禁止" }, { "face", "顔ＣＧ" }, { "portrait", "立ち絵ＣＧ" },
	{ "class", "職業" }, { "sex", "性別" }, { "level", "ＬＶ" },
	{ "level_max", "ＬＶ最大" }, { "exp", "ＥＸＰ" }, { "exp_max", "ＥＸＰ最大" },
	{ "rebirths", "転生回数" }, { "hp", "体力" }, { "hp_max", "体力最大" },
	{ "spd", "素早さ" }, { "atk", "攻撃力" }, { "def", "防御力" },
	{ "hit", "命中率" }, { "eva", "回避率" }, { "crit", "必殺率" },
	{ "impact", "衝撃率" }, { "resist", "耐性率" }, { "mag", "魔法力" },
	{ "mdef", "魔抵力" }, { "fire_res", "炎耐性" }, { "ice_res", "氷耐性" },
	{ "thunder_res", "雷耐性" }, { "light_res", "光耐性" }, { "dark_res", "闇耐性" },
	{ "weak_element", "弱点属性" }, { "weapon", "武器" }, { "armor", "防具" },
	{ "item", "持ち物" }, { "special", "オプション" }, { "skill_points", "スキルポイント" },
	{ "race", "種族" }, { "type", "タイプ" }, { "model", "ポリゴン" },
	{ "boss", "ボス" }, { "weakness_known", "弱点把握" },
	{ "status", "状態変化" }, { "guard", "守護率" }, { "stealth", "隠密率" },
	{ "berserk", "狂戦士" }, { "bless_all", "全体加護" }, { "barrier", "バリア" },
	{ "atk_buff", "攻撃付与" }, { "def_buff", "防御付与" }, { "charge", "溜め" },
	{ "init_hp", "初期／体力最大" }, { "init_spd", "初期／素早さ" },
	{ "init_atk", "初期／攻撃力" }, { "init_def", "初期／防御力" },
	{ "init_hit", "初期／命中率" }, { "init_eva", "初期／回避率" },
	{ "init_crit", "初期／必殺率" }, { "init_impact", "初期／衝撃率" },
	{ "init_resist", "初期／耐性率" }, { "init_mag", "初期／魔法力" },
	{ "init_mdef", "初期／魔抵力" }, { "init_sp", "初期／スキルポイント" },
};
#define NFIELDS ((int)(sizeof k_fields / sizeof k_fields[0]))

/* ---- bytecode wrappers (same shape as modloader_chara.c / modloader_skill.c) ---- */

static void emit0(struct buffer *out, enum opcode op)
{
	buffer_write_int16(out, (uint16_t)op);
}

static void emit1(struct buffer *out, enum opcode op, int32_t arg)
{
	buffer_write_int16(out, (uint16_t)op);
	buffer_write_int32(out, (uint32_t)arg);
}

static void commit_wrap(struct ain *a, int fno, struct buffer *out)
{
	a->functions[fno].address = (uint32_t)(a->code_size + 6);
	a->code = xrealloc(a->code, a->code_size + out->index);
	memcpy(a->code + a->code_size, out->buf, out->index);
	a->code_size += out->index;
}

static int find_fn(struct ain *a, const char *utf8)
{
	char *sjis = utf2sjis(utf8, 0);
	int fno;
	if (!sjis)
		return -1;
	fno = ain_get_function(a, sjis);
	free(sjis);
	return fno;
}

/* Call the original, then CALLSYS. The original's return value stays on the stack. */
static int wrap_tail(struct ain *a, int fno, int sys, const char *tag)
{
	int dup = ain_dup_function(a, fno);
	int nargs = a->functions[fno].nr_args;
	int i;
	struct buffer out;

	buffer_init(&out, NULL, 0);
	emit1(&out, FUNC, fno);
	if (fno == f_cdata) {
		/* A custom number is not in the game's list. Without this the search ends in a TADA error line. */
		emit1(&out, CALLSYS, VM_XSYS_ROSTER_CUSTOM);
		emit1(&out, IFZ, (int32_t)(a->code_size + 6 + 6 + 6 + 6 + 6 + 2));
		emit1(&out, PUSH, 0);
		emit1(&out, CALLSYS, sys);
		emit0(&out, RETURN);
	}
	for (i = 0; i < nargs; i++) {
		emit1(&out, SH_LOCALREF, i);
		if (a->functions[fno].vars[i].type.data == AIN_STRING) {
			emit0(&out, DUP);
			emit0(&out, SP_INC);
		}
	}
	emit1(&out, CALLFUNC, dup);
	emit1(&out, CALLSYS, sys);
	emit0(&out, RETURN);
	emit1(&out, ENDFUNC, fno);
	commit_wrap(a, fno, &out);
	rq_log("roster: %s", tag);
	return 1;
}

/* CALLSYS first (it pops the two int args and pushes them back), then the original. */
static int wrap_head2(struct ain *a, int fno, int sys, const char *tag)
{
	int dup = ain_dup_function(a, fno);
	struct buffer out;

	buffer_init(&out, NULL, 0);
	emit1(&out, FUNC, fno);
	emit1(&out, SH_LOCALREF, 0);
	emit1(&out, SH_LOCALREF, 1);
	emit1(&out, CALLSYS, sys);
	emit1(&out, CALLFUNC, dup);
	emit0(&out, RETURN);
	emit1(&out, ENDFUNC, fno);
	commit_wrap(a, fno, &out);
	rq_log("roster: %s", tag);
	return 1;
}

/* ---- AIN calls ---- */

static int ci(int fno, int n, ...)
{
	va_list ap;
	int i;

	if (fno < 0)
		return 0;
	va_start(ap, n);
	for (i = 0; i < n; i++) {
		int v = va_arg(ap, int);
		stack_push(v);
	}
	va_end(ap);
	vm_call(fno, -1);
	if (ain->functions[fno].return_type.data == AIN_VOID)
		return 0;
	return stack_pop().i;
}

static int new_str(const char *utf)
{
	char *sj = utf2sjis(utf ? utf : "", 0);
	struct string *s;
	int slot;

	if (!sj)
		sj = strdup("");
	s = make_string(sj, strlen(sj));
	free(sj);
	if (!s)
		return vm_string_ref(string_ref(&EMPTY_STRING));
	slot = heap_alloc_slot(VM_STRING);
	heap[slot].s = s;
	return slot;
}

static void drop_ret(int fno)
{
	int ret;
	if (ain->functions[fno].return_type.data == AIN_VOID)
		return;
	ret = stack_pop().i;
	if (ain->functions[fno].return_type.data == AIN_STRING)
		heap_unref(ret);
}

/* fn(i0, [i1,] "text"): setters that read when the text is empty. */
static void call_text(int fno, int nint, int a, int b, const char *utf)
{
	if (fno < 0 || !utf || !utf[0])
		return;
	if (nint >= 1)
		stack_push(a);
	if (nint >= 2)
		stack_push(b);
	stack_push(new_str(utf));
	vm_call(fno, -1);
	drop_ret(fno);
}

static char *local_utf(int idx)
{
	struct string *s = heap_get_string(local_get(idx).i);
	char *u;
	if (!s)
		return strdup("");
	u = sjis2utf(s->text, 0);
	return u ? u : strdup("");
}

/* ---- direct access to Ｃ[iic] ---- */

static struct page *slot_page(int slot)
{
	if (!page_index_valid(slot))
		return NULL;
	return heap_get_page(slot);
}

static struct page *cpage(int iic);

/* A number past Ｃ is a fatal VM error in the game's getters, so calls check it first. */
int rq_chara_exists(int iic)
{
	return cpage(iic) != NULL;
}

static struct page *cpage(int iic)
{
	struct page *arr;
	if (gi_c == -2) {
		char *sj = utf2sjis("Ｃ", 0);
		gi_c = sj ? ain_get_global(ain, sj) : -1;
		free(sj);
	}
	if (!ain || gi_c < 0 || gi_c >= ain->nr_globals || !heap || !heap_size || !heap[0].page)
		return NULL;
	arr = slot_page(global_get(gi_c).i);
	if (!arr || arr->type != ARRAY_PAGE || iic <= 0 || iic >= arr->nr_vars)
		return NULL;
	return slot_page(arr->values[iic].i);
}

static int memb_utf(struct page *p, const char *utf)
{
	struct ain_struct *s;
	char *sj;
	int i, m = -1;

	if (!p || p->type != STRUCT_PAGE || !ain || p->index < 0 || p->index >= ain->nr_structures)
		return -1;
	sj = utf2sjis(utf, 0);
	if (!sj)
		return -1;
	s = &ain->structures[p->index];
	for (i = 0; i < s->nr_members && i < p->nr_vars; i++) {
		if (s->members[i].name && !strcmp(s->members[i].name, sj)) {
			m = i;
			break;
		}
	}
	free(sj);
	return m;
}

static const char *alias_jp(const char *alias)
{
	int i;
	for (i = 0; i < NFIELDS; i++) {
		if (!strcmp(k_fields[i].alias, alias))
			return k_fields[i].jp;
	}
	return NULL;
}

static int mread(struct page *p, const char *jp, int *out)
{
	int m = memb_utf(p, jp);
	if (m < 0)
		return 0;
	*out = p->values[m].i;
	return 1;
}

static int mwrite(struct page *p, const char *jp, int v)
{
	int m = memb_utf(p, jp);
	if (m < 0)
		return 0;
	page_set_var(p, m, v);
	return 1;
}

static char *mstr(struct page *p, const char *jp)
{
	int m = memb_utf(p, jp);
	struct string *s;
	char *u;

	if (m < 0 || !string_index_valid(p->values[m].i))
		return strdup("");
	s = heap_get_string(p->values[m].i);
	if (!s || s->size <= 0)
		return strdup("");
	u = sjis2utf(s->text, (size_t)s->size);
	return u ? u : strdup("");
}

static int chara_no(int iic)
{
	int v = 0;
	struct page *p = cpage(iic);
	if (p)
		mread(p, "番号", &v);
	return v;
}

/* ---- ids ---- */

static int id_to_no(const char *id)
{
	uint32_t h = 2166136261u;
	for (; *id; id++) {
		h ^= (uint8_t)*id;
		h *= 16777619u;
	}
	return 1000 + (int)(h % 9000u);
}

static struct tpl *tpl_by_no(int no)
{
	int i;
	for (i = 0; i < g_ntpl; i++) {
		if (g_tpl[i].no == no)
			return &g_tpl[i];
	}
	return NULL;
}

static const char *mod_of(lua_State *L)
{
	const char *m = rq_lua_caller_mod(L);
	return m && m[0] ? m : "_";
}

/* Number or text id at stack index idx. */
static int resolve_no(lua_State *L, int idx)
{
	if (lua_type(L, idx) == LUA_TNUMBER)
		return (int)lua_tointeger(L, idx);
	if (lua_type(L, idx) == LUA_TSTRING)
		return id_to_no(lua_tostring(L, idx));
	return luaL_error(L, "character must be a number or a text id");
}

static int ensure(lua_State *L)
{
	if (!ain)
		return luaL_error(L, "AIN not loaded");
	if (!rq_roster_install(ain))
		return luaL_error(L, "roster install failed (see logs/rqmod.log)");
	return 0;
}

/* ---- Lua table helpers (t is an absolute index) ---- */

static int tint(lua_State *L, int t, const char *key, int def)
{
	int v = def;
	lua_getfield(L, t, key);
	if (lua_isnumber(L, -1))
		v = (int)lua_tointeger(L, -1);
	else if (lua_isboolean(L, -1))
		v = lua_toboolean(L, -1);
	lua_pop(L, 1);
	return v;
}

static char *tstr(lua_State *L, int t, const char *key)
{
	char *out = NULL;
	lua_getfield(L, t, key);
	if (lua_isstring(L, -1))
		out = strdup(lua_tostring(L, -1));
	lua_pop(L, 1);
	return out;
}

/* ---- field writes shared by define and edit ---- */

static int set_slots(int iic, int n)
{
	int want = n - 2, row, cur, changed = 0;

	if (want < 1)
		want = 1;
	row = ci(f_cs_find, 3, iic, SKILL_SLOT_RANK, 0);
	if (!row) {
		ci(f_cs_add, 3, iic, SKILL_SLOT_RANK, want);
		row = ci(f_cs_find, 3, iic, SKILL_SLOT_RANK, 0);
		changed = 1;
	}
	if (row) {
		cur = ci(f_cs_rank, 3, iic, row, -1);
		if (cur != want) {
			ci(f_cs_rank, 3, iic, row, want);
			changed = 1;
		}
	}
	if (ci(f_fa, 3, iic, 3, -1) != want) {
		ci(f_fa, 3, iic, 3, want);
		changed = 1;
	}
	return changed;
}

/* Returns 1 if a value changed, 0 if not, -1 for an unknown key. Value at vidx. */
static int set_alias(lua_State *L, int iic, struct page *p, const char *key, int vidx)
{
	const char *jp;
	int cur, v;

	if (!strcmp(key, "name") || !strcmp(key, "short") || !strcmp(key, "face_name")) {
		const char *s = lua_tostring(L, vidx);
		char *now;
		int changed;
		if (!s || !s[0])
			return 0;
		now = mstr(p, !strcmp(key, "name") ? "名前" : !strcmp(key, "short") ? "短縮名前" : "顔ＣＧ文字列");
		changed = strcmp(now, s) != 0;
		free(now);
		if (changed)
			call_text(!strcmp(key, "name") ? f_name : !strcmp(key, "short") ? f_short : f_facestr,
				  1, iic, 0, s);
		return changed;
	}
	if (!strcmp(key, "slots"))
		return set_slots(iic, (int)lua_tointeger(L, vidx));
	jp = alias_jp(key);
	if (!jp)
		return -1;
	if (lua_isboolean(L, vidx))
		v = lua_toboolean(L, vidx);
	else
		v = (int)lua_tointeger(L, vidx);
	if (!mread(p, jp, &cur))
		return -1;
	if (cur == v)
		return 0;
	/* A mod image on a character that already shows a game CG: replace that CG instead
	 * (the screens built at load keep the number they read). */
	if ((!strcmp(key, "face") || !strcmp(key, "portrait")) && v >= 900000 && cur > 0 && cur < 900000) {
		rq_image_redirect(cur, v);
		return 0;
	}
	mwrite(p, jp, v);
	return 1;
}

static int apply_init(lua_State *L, int iic, struct page *p, int t)
{
	int changed = 0;
	lua_pushnil(L);
	while (lua_next(L, t)) {
		if (lua_type(L, -2) == LUA_TSTRING && lua_isnumber(L, -1)) {
			char key[48];
			int r;
			snprintf(key, sizeof key, "init_%s", lua_tostring(L, -2));
			r = set_alias(L, iic, p, key, lua_gettop(L));
			if (r > 0)
				changed = 1;
			else if (r < 0)
				rq_log("roster: unknown init stat '%s'", lua_tostring(L, -2));
		}
		lua_pop(L, 1);
	}
	return changed;
}

/* Learn skills from { {id=, n=}, ... }. Idempotent: use-count skills are never stacked twice. */
static int apply_learn(lua_State *L, int iic, int t)
{
	int i, n = (int)lua_rawlen(L, t), changed = 0;

	for (i = 1; i <= n; i++) {
		int id, cnt, row;
		lua_rawgeti(L, t, i);
		if (!lua_istable(L, -1)) {
			lua_pop(L, 1);
			continue;
		}
		id = tint(L, lua_gettop(L), "id", 0);
		cnt = tint(L, lua_gettop(L), "n", 1);
		lua_pop(L, 1);
		if (id <= 0)
			continue;
		row = ci(f_cs_find, 3, iic, id, 0);
		if (!row) {
			ci(f_cs_add, 3, iic, id, cnt);
			changed = 1;
		} else {
			int type = ci(f_stype, 2, id, -1);
			if ((type == 20 || type == 30) && ci(f_cs_rank, 3, iic, row, -1) < cnt) {
				ci(f_cs_add, 3, iic, id, cnt);
				changed = 1;
			}
		}
	}
	return changed;
}

/* ---- creating a Lua-defined character (one call per flag) ---- */

static void build_base(lua_State *L, int t, int iic, int no)
{
	char *name = tstr(L, t, "name");
	char *shortn = tstr(L, t, "short");
	char *facestr = tstr(L, t, "face_name");
	int face = tint(L, t, "face", 2916);
	int job = tint(L, t, "class", 1);
	int level = tint(L, t, "level", 1);
	int level_max = tint(L, t, "level_max", 35);
	int slots = tint(L, t, "slots", 3);
	struct page *p;
	int i, n;

	stack_push(iic);
	stack_push(no);
	stack_push(vm_string_ref(string_ref(&EMPTY_STRING)));
	stack_push(face);
	stack_push(job);
	stack_push(level);
	stack_push(level_max);
	stack_push(slots);
	stack_push(0);
	vm_call(f_base, -1);
	drop_ret(f_base);

	call_text(f_name, 1, iic, 0, name);
	call_text(f_short, 1, iic, 0, shortn ? shortn : name);
	call_text(f_facestr, 1, iic, 0, facestr);
	free(name);
	free(shortn);
	free(facestr);

	p = cpage(iic);
	if (p) {
		mwrite(p, "性別", tint(L, t, "sex", 2));
		lua_pushnil(L);
		while (lua_next(L, t)) {
			if (lua_type(L, -2) == LUA_TSTRING) {
				const char *k = lua_tostring(L, -2);
				if (alias_jp(k) && strcmp(k, "no") && strcmp(k, "face") && strcmp(k, "class")
				    && strcmp(k, "level") && strcmp(k, "level_max") && strcmp(k, "sex")
				    && strncmp(k, "init_", 5) && !rq_item_is_gear_key(k))
					set_alias(L, iic, p, k, lua_gettop(L));
			}
			lua_pop(L, 1);
		}
	}

	lua_getfield(L, t, "lines");
	if (lua_istable(L, -1)) {
		n = (int)lua_rawlen(L, -1);
		for (i = 1; i <= n && i <= 6; i++) {
			lua_rawgeti(L, -1, i);
			if (lua_isstring(L, -1))
				call_text(f_line, 2, iic, i, lua_tostring(L, -1));
			lua_pop(L, 1);
		}
	}
	lua_pop(L, 1);
}

static void apply_template(lua_State *L, struct tpl *t, int iic, int no, const char *flag)
{
	int tab;
	struct page *p;

	if (!L)
		return;
	lua_rawgeti(L, LUA_REGISTRYINDEX, t->ref);
	tab = lua_gettop(L);
	if (!lua_istable(L, tab)) {
		lua_pop(L, 1);
		return;
	}
	if (!strcmp(flag, "基本")) {
		build_base(L, tab, iic, no);
	} else if (!strcmp(flag, "スキル")) {
		lua_getfield(L, tab, "skills");
		if (lua_istable(L, -1))
			apply_learn(L, iic, lua_gettop(L));
		lua_pop(L, 1);
	} else if (!strcmp(flag, "ステータス")) {
		p = cpage(iic);
		lua_getfield(L, tab, "init");
		if (p && lua_istable(L, -1))
			apply_init(L, iic, p, lua_gettop(L));
		lua_pop(L, 1);
	}
	lua_pop(L, 1);
}

static void apply_edit_inits(lua_State *L, int iic, int no)
{
	struct page *p = cpage(iic);
	int i;

	if (!L || !p)
		return;
	for (i = 0; i < g_nedit; i++) {
		if (g_edit[i].no != no)
			continue;
		lua_rawgeti(L, LUA_REGISTRYINDEX, g_edit[i].ref);
		if (lua_istable(L, -1)) {
			lua_getfield(L, -1, "init");
			if (lua_istable(L, -1))
				apply_init(L, iic, p, lua_gettop(L));
			lua_pop(L, 1);
		}
		lua_pop(L, 1);
	}
}

/* ---- syscalls ---- */

static void roster_data(void)
{
	int ret = stack_pop().i;
	int iic = local_get(0).i;
	int no = local_get(1).i;
	struct tpl *t;

	if (no == 0 && iic > 0)
		no = chara_no(iic);
	t = tpl_by_no(no);
	if (iic > 0 && (t || g_nedit > 0)) {
		char *flag = local_utf(2);
		if (t) {
			apply_template(rq_lua(), t, iic, no, flag);
			ret = 1;
		}
		/* Ｃ再計算 re-runs this flag to reset the 初期／* bonuses, so edits must write
		 * them here, before the stats are computed. */
		if (!strcmp(flag, "ステータス"))
			apply_edit_inits(rq_lua(), iic, no);
		free(flag);
	}
	stack_push(ret);
}

static void call_cb_list(lua_State *L, const int *refs, int n, int iic, int no)
{
	int i;
	struct tpl *t = tpl_by_no(no);

	for (i = 0; i < n; i++) {
		lua_rawgeti(L, LUA_REGISTRYINDEX, refs[i]);
		lua_newtable(L);
		lua_pushinteger(L, iic);
		lua_setfield(L, -2, "iic");
		lua_pushinteger(L, no);
		lua_setfield(L, -2, "no");
		if (t) {
			lua_pushstring(L, t->id);
			lua_setfield(L, -2, "id");
		}
		if (lua_pcall(L, 1, 0, 0) != LUA_OK) {
			rq_log("chara on_create error: %s", lua_tostring(L, -1));
			lua_pop(L, 1);
		}
	}
}

static void roster_create(void)
{
	int iic = stack_pop().i;
	int no = local_get(0).i;
	lua_State *L = rq_lua();

	if (iic > 0 && L) {
		struct tpl *t = tpl_by_no(no);
		int ran = 0, i;
		if (t) {
			lua_rawgeti(L, LUA_REGISTRYINDEX, t->ref);
			rq_item_give_gear(L, lua_gettop(L), iic);
			lua_pop(L, 1);
			ran = 1;
		}
		for (i = 0; i < g_nedit; i++) {
			if (g_edit[i].no != no)
				continue;
			lua_rawgeti(L, LUA_REGISTRYINDEX, g_edit[i].ref);
			if (lua_istable(L, -1))
				rq_item_give_gear(L, lua_gettop(L), iic);
			lua_pop(L, 1);
			ran = 1;
		}
		if (t) {
			lua_rawgeti(L, LUA_REGISTRYINDEX, t->ref);
			lua_getfield(L, -1, "on_create");
			if (lua_isfunction(L, -1)) {
				lua_pushinteger(L, iic);
				if (lua_pcall(L, 1, 0, 0) != LUA_OK) {
					rq_log("roster: on_create error: %s", lua_tostring(L, -1));
					lua_pop(L, 1);
				}
				ran = 1;
			} else {
				lua_pop(L, 1);
			}
			lua_pop(L, 1);
		}
		if (g_ncreate > 0) {
			call_cb_list(L, g_create_cb, g_ncreate, iic, no);
			ran = 1;
		}
		if (ran) {
			ci(f_recalc, 1, iic);
			ci(f_supply, 1, iic);
		}
	}
	stack_push(iic);
}

static void roster_forget(void)
{
	int iis = stack_pop().i;
	int iic = stack_pop().i;
	lua_State *L = rq_lua();

	if (L && g_nforget > 0 && iic > 0 && iis > 0) {
		int row = ci(f_cs_find, 3, iic, iis, 0);
		if (row) {
			int cost = ci(f_cs_cost, 3, iic, row, -1);
			int rank = ci(f_cs_rank, 3, iic, row, -1);
			int total = 0, i, fno;
			char *caller = NULL;

			if (g_lua_forget) {
				caller = strdup("lua");
			} else {
				fno = vm_call_stack_fno(1);
				if (fno >= 0 && fno < ain->nr_functions)
					caller = sjis2utf(ain->functions[fno].name, 0);
			}
			for (i = 0; i < g_nforget; i++) {
				lua_rawgeti(L, LUA_REGISTRYINDEX, g_forget_cb[i]);
				lua_newtable(L);
				lua_pushinteger(L, iic);
				lua_setfield(L, -2, "iic");
				lua_pushinteger(L, iis);
				lua_setfield(L, -2, "skill");
				lua_pushinteger(L, row);
				lua_setfield(L, -2, "slot");
				lua_pushinteger(L, rank);
				lua_setfield(L, -2, "rank");
				lua_pushinteger(L, cost);
				lua_setfield(L, -2, "cost");
				lua_pushstring(L, caller ? caller : "");
				lua_setfield(L, -2, "caller");
				if (lua_pcall(L, 1, 1, 0) != LUA_OK) {
					rq_log("chara on_forget error: %s", lua_tostring(L, -1));
				} else if (lua_isnumber(L, -1) && lua_tointeger(L, -1) > 0) {
					total += (int)lua_tointeger(L, -1);
				}
				lua_pop(L, 1);
			}
			free(caller);
			if (total > 0)
				ci(f_points, 2, iic, ci(f_points, 2, iic, -1) + total);
		}
	}
	stack_push(iic);
	stack_push(iis);
}

int rq_roster_syscall(int code)
{
	switch ((enum vm_extra_syscall)code) {
	case VM_XSYS_ROSTER_DATA:
		roster_data();
		return 1;
	case VM_XSYS_ROSTER_CUSTOM: {
		int no = local_get(1).i;
		if (no == 0 && local_get(0).i > 0)
			no = chara_no(local_get(0).i);
		stack_push(no >= 1000 ? 1 : 0);
		return 1;
	}
	case VM_XSYS_ROSTER_CREATE:
		roster_create();
		return 1;
	case VM_XSYS_ROSTER_FORGET:
		roster_forget();
		return 1;
	default:
		return 0;
	}
}

/* ---- edits: re-applied from the end of Ｃ再計算 ---- */

void rq_roster_apply(int iic)
{
	lua_State *L = rq_lua();
	struct page *p;
	int i, no, state, changed = 0;

	if (!g_on || g_in_apply || g_nedit == 0 || !L)
		return;
	p = cpage(iic);
	if (!p || !mread(p, "登録状態", &state) || state == 0 || !mread(p, "番号", &no))
		return;
	for (i = 0; i < g_nedit; i++) {
		int tab;
		if (g_edit[i].no != no)
			continue;
		lua_rawgeti(L, LUA_REGISTRYINDEX, g_edit[i].ref);
		tab = lua_gettop(L);
		if (lua_istable(L, tab)) {
			lua_pushnil(L);
			while (lua_next(L, tab)) {
				if (lua_type(L, -2) == LUA_TSTRING) {
					const char *k = lua_tostring(L, -2);
					int r = 0;
					if (!strcmp(k, "learn")) {
						if (lua_istable(L, -1))
							r = apply_learn(L, iic, lua_gettop(L));
					} else if (!strcmp(k, "init")) {
						r = 0; /* written during the ステータス flag of Ｃ再計算 */
					} else if (rq_item_is_gear_key(k)) {
						r = 0; /* given once, when the character is created */
					} else if (!strcmp(k, "on_recalc")) {
						if (lua_isfunction(L, -1)) {
							lua_pushvalue(L, -1);
							lua_pushinteger(L, iic);
							if (lua_pcall(L, 1, 1, 0) != LUA_OK) {
								rq_log("roster: edit on_recalc error: %s", lua_tostring(L, -1));
							} else if (lua_toboolean(L, -1)) {
								r = 1;
							}
							lua_pop(L, 1);
						}
					} else {
						r = set_alias(L, iic, p, k, lua_gettop(L));
						if (r < 0) {
							rq_log("roster: edit key '%s' unknown", k);
							r = 0;
						}
					}
					if (r > 0)
						changed = 1;
				}
				lua_pop(L, 1);
			}
		}
		lua_pop(L, 1);
	}
	if (changed) {
		g_in_apply = 1;
		ci(f_recalc, 1, iic);
		g_in_apply = 0;
	}
}

/* A load brings back characters as they were saved, and the game may not recalculate
 * them for a long time (opening the party screen does not), so on the next frame every
 * edited character is recalculated once, which applies the edits. */
static int g_stale;

void rq_roster_mark_stale(void)
{
	g_stale = 1;
}

void rq_roster_refresh(void)
{
	int iic, i, no, state, count = 0;

	if (!g_stale)
		return;
	g_stale = 0;
	if (!g_on || g_nedit == 0 || f_recalc < 0 || !rq_lua())
		return;
	for (iic = 1; iic < 1000; iic++) {
		struct page *p = cpage(iic);
		int hit = 0;
		if (!p || !mread(p, "登録状態", &state) || state == 0 || !mread(p, "番号", &no))
			continue;
		for (i = 0; i < g_nedit && !hit; i++)
			hit = g_edit[i].no == no;
		if (!hit)
			continue;
		ci(f_recalc, 1, iic);
		count++;
	}
	if (count)
		rq_log("roster: edits applied to %d existing characters", count);
}

/* ---- install ---- */

int rq_roster_install(struct ain *a)
{
	if (g_on)
		return 1;
	if (!a)
		return 0;

	f_create = find_fn(a, "Ｃ生成");
	f_cdata = find_fn(a, "Ｃ生成／キャラデータ");
	f_base = find_fn(a, "Ｃ生成／基本データ");
	f_findno = find_fn(a, "Ｃ番号取得");
	f_del = find_fn(a, "Ｃ削除");
	f_pta_add = find_fn(a, "ＰＴＡ登録");
	f_pta_del = find_fn(a, "ＰＴＡ解除");
	f_state = find_fn(a, "Ｃ登録状態");
	f_name = find_fn(a, "Ｃ名前");
	f_short = find_fn(a, "Ｃ短縮名前");
	f_facestr = find_fn(a, "Ｃ顔ＣＧ文字列");
	f_line = find_fn(a, "Ｃ台詞");
	f_cs_add = find_fn(a, "ＣＳ追加");
	f_cs_del = find_fn(a, "ＣＳ削除");
	f_cs_find = find_fn(a, "ＣＳ番号取得");
	f_cs_cost = find_fn(a, "ＣＳコスト");
	f_cs_rank = find_fn(a, "ＣＳランク");
	f_stype = find_fn(a, "Ｓタイプ");
	f_points = find_fn(a, "Ｃスキルポイント");
	f_fa = find_fn(a, "ＣＦＡ");
	f_recalc = find_fn(a, "Ｃ再計算");
	f_supply = find_fn(a, "Ｃ補給");
	f_list = find_fn(a, "キャラデータ一覧_更新"); /* the Party tab's character list; optional */

	if (f_create < 0 || f_cdata < 0 || f_base < 0 || f_findno < 0 || f_del < 0
	    || f_pta_add < 0 || f_pta_del < 0 || f_state < 0 || f_name < 0 || f_short < 0
	    || f_facestr < 0 || f_line < 0 || f_cs_add < 0 || f_cs_del < 0 || f_cs_find < 0
	    || f_cs_cost < 0 || f_cs_rank < 0 || f_stype < 0 || f_points < 0 || f_fa < 0
	    || f_recalc < 0 || f_supply < 0) {
		rq_log("roster: missing AIN symbol (create=%d cdata=%d base=%d del=%d cs_cost=%d points=%d)",
		       f_create, f_cdata, f_base, f_del, f_cs_cost, f_points);
		return 0;
	}
	if (!rq_chara_install(a))
		return 0;
	if (!wrap_tail(a, f_cdata, VM_XSYS_ROSTER_DATA, "Ｃ生成／キャラデータ tail -> template"))
		return 0;
	if (!wrap_tail(a, f_create, VM_XSYS_ROSTER_CREATE, "Ｃ生成 tail -> on_create"))
		return 0;
	if (!wrap_head2(a, f_cs_del, VM_XSYS_ROSTER_FORGET, "ＣＳ削除 head -> on_forget"))
		return 0;
	g_on = 1;
	return 1;
}

/* ---- Lua: define / recruit / lookup ---- */

int rq_lua_chara_define(lua_State *L)
{
	char *id;
	struct tpl *t;
	int no;

	luaL_checktype(L, 1, LUA_TTABLE);
	rq_image_resolve_fields(L, 1);
	rq_item_check_gear(L, 1);
	ensure(L);
	lua_getfield(L, 1, "id");
	if (!lua_isstring(L, -1) || !lua_tostring(L, -1)[0])
		return luaL_error(L, "chara.define needs a text id");
	id = strdup(lua_tostring(L, -1));
	lua_pop(L, 1);
	if (strlen(id) >= ID_MAX) {
		free(id);
		return luaL_error(L, "chara.define id is too long (max %d)", ID_MAX - 1);
	}
	no = id_to_no(id);
	t = tpl_by_no(no);
	if (t && strcmp(t->id, id)) {
		lua_pushfstring(L, "chara id '%s' collides with '%s' (same number %d); pick another id", id, t->id, no);
		free(id);
		return lua_error(L);
	}
	if (!t) {
		if (g_ntpl >= TPL_MAX) {
			free(id);
			return luaL_error(L, "too many chara.define calls");
		}
		t = &g_tpl[g_ntpl++];
	} else {
		luaL_unref(L, LUA_REGISTRYINDEX, t->ref);
	}
	snprintf(t->id, sizeof t->id, "%s", id);
	snprintf(t->mod, sizeof t->mod, "%s", mod_of(L));
	t->no = no;
	lua_pushvalue(L, 1);
	t->ref = luaL_ref(L, LUA_REGISTRYINDEX);
	free(id);
	rq_log("roster: defined '%s' as no %d", t->id, no);
	lua_pushinteger(L, no);
	return 1;
}

int rq_lua_chara_find(lua_State *L)
{
	int no = resolve_no(L, 1);
	int iic;

	ensure(L);
	iic = ci(f_findno, 1, no);
	if (iic <= 0) {
		lua_pushnil(L);
		return 1;
	}
	lua_pushinteger(L, iic);
	return 1;
}

int rq_lua_chara_recalc(lua_State *L)
{
	int iic = (int)luaL_checkinteger(L, 1);

	ensure(L);
	if (!cpage(iic))
		return luaL_error(L, "chara.recalc: no character at %d", iic);
	ci(f_recalc, 1, iic);
	if (lua_toboolean(L, 2))
		ci(f_supply, 1, iic);
	return 0;
}

int rq_lua_chara_is_recruited(lua_State *L)
{
	int no = resolve_no(L, 1);
	ensure(L);
	lua_pushboolean(L, ci(f_findno, 1, no) > 0);
	return 1;
}

int rq_lua_chara_recruit(lua_State *L)
{
	int no = resolve_no(L, 1);
	int iic;

	ensure(L);
	if (no >= 1000 && !tpl_by_no(no))
		return luaL_error(L, "unknown character: define it before recruiting");
	iic = ci(f_findno, 1, no);
	if (iic <= 0) {
		iic = ci(f_create, 1, no);
		if (iic <= 0) {
			lua_pushnil(L);
			lua_pushstring(L, "the game refused to create it (roster full or bad number)");
			return 2;
		}
	}
	if (lua_istable(L, 2)) {
		int state = tint(L, 2, "state", 0);
		int party = tint(L, 2, "party", 0);
		if (state > 0)
			ci(f_state, 2, iic, state);
		if (party > 0)
			ci(f_pta_add, 2, iic, party);
	}
	ci(f_list, 0);
	lua_pushinteger(L, iic);
	return 1;
}

int rq_lua_chara_dismiss(lua_State *L)
{
	int iic = (int)luaL_checkinteger(L, 1);

	ensure(L);
	if (!cpage(iic) || iic < 20)
		return luaL_error(L, "dismiss: not a roster slot");
	ci(f_pta_del, 1, iic);
	ci(f_del, 1, iic);
	ci(f_list, 0);
	return 0;
}

int rq_lua_chara_number(lua_State *L)
{
	int iic = (int)luaL_checkinteger(L, 1);
	int no = chara_no(iic);
	if (no <= 0) {
		lua_pushnil(L);
		return 1;
	}
	lua_pushinteger(L, no);
	return 1;
}

int rq_lua_chara_id_of(lua_State *L)
{
	struct tpl *t = tpl_by_no((int)luaL_checkinteger(L, 1));
	if (!t) {
		lua_pushnil(L);
		return 1;
	}
	lua_pushstring(L, t->id);
	return 1;
}

/* ---- Lua: edit ---- */

int rq_lua_chara_edit(lua_State *L)
{
	int no = resolve_no(L, 1);
	const char *mod = mod_of(L);
	int i;

	luaL_checktype(L, 2, LUA_TTABLE);
	rq_image_resolve_fields(L, 2);
	rq_item_check_gear(L, 2);
	ensure(L);
	for (i = 0; i < g_nedit; i++) {
		if (g_edit[i].no == no && !strcmp(g_edit[i].mod, mod)) {
			luaL_unref(L, LUA_REGISTRYINDEX, g_edit[i].ref);
			break;
		}
	}
	if (i == g_nedit) {
		if (g_nedit >= EDIT_MAX)
			return luaL_error(L, "too many chara.edit calls");
		g_nedit++;
	}
	g_edit[i].no = no;
	snprintf(g_edit[i].mod, sizeof g_edit[i].mod, "%s", mod);
	lua_pushvalue(L, 2);
	g_edit[i].ref = luaL_ref(L, LUA_REGISTRYINDEX);
	return 0;
}

/* ---- Lua: low level get/set ---- */

int rq_lua_chara_get(lua_State *L)
{
	int iic = (int)luaL_checkinteger(L, 1);
	struct page *p = cpage(iic);
	char *s;
	int i, v;
	struct tpl *t;

	if (!p) {
		lua_pushnil(L);
		return 1;
	}
	lua_newtable(L);
	lua_pushinteger(L, iic);
	lua_setfield(L, -2, "iic");
	for (i = 0; i < NFIELDS; i++) {
		if (mread(p, k_fields[i].jp, &v)) {
			if (!strcmp(k_fields[i].alias, "weakness_known"))
				lua_pushboolean(L, v != 0);
			else
				lua_pushinteger(L, v);
			lua_setfield(L, -2, k_fields[i].alias);
		}
	}
	s = mstr(p, "名前");
	lua_pushstring(L, s);
	lua_setfield(L, -2, "name");
	free(s);
	s = mstr(p, "短縮名前");
	lua_pushstring(L, s);
	lua_setfield(L, -2, "short");
	free(s);
	s = mstr(p, "顔ＣＧ文字列");
	lua_pushstring(L, s);
	lua_setfield(L, -2, "face_name");
	free(s);
	if (mread(p, "番号", &v) && (t = tpl_by_no(v)) != NULL) {
		lua_pushstring(L, t->id);
		lua_setfield(L, -2, "id");
	}
	return 1;
}

int rq_lua_chara_set(lua_State *L)
{
	int iic = (int)luaL_checkinteger(L, 1);
	struct page *p = cpage(iic);

	luaL_checktype(L, 2, LUA_TTABLE);
	rq_image_resolve_fields(L, 2);
	ensure(L);
	if (!p)
		return luaL_error(L, "chara.set: no character at %d", iic);
	lua_pushnil(L);
	while (lua_next(L, 2)) {
		if (lua_type(L, -2) == LUA_TSTRING) {
			const char *k = lua_tostring(L, -2);
			if (set_alias(L, iic, p, k, lua_gettop(L)) < 0)
				return luaL_error(L, "chara.set: unknown field '%s'", k);
		}
		lua_pop(L, 1);
	}
	return 0;
}

int rq_lua_chara_skill_points(lua_State *L)
{
	int iic = (int)luaL_checkinteger(L, 1);
	ensure(L);
	lua_pushinteger(L, cpage(iic) ? ci(f_points, 2, iic, -1) : 0);
	return 1;
}

int rq_lua_chara_add_skill_points(lua_State *L)
{
	int iic = (int)luaL_checkinteger(L, 1);
	int delta = (int)luaL_checkinteger(L, 2);
	int now;

	if (!cpage(iic))
		return luaL_error(L, "no character at %d", iic);
	ensure(L);
	now = ci(f_points, 2, iic, -1) + delta;
	if (now < 0)
		now = 0;
	ci(f_points, 2, iic, now);
	lua_pushinteger(L, now);
	return 1;
}

int rq_lua_chara_forget(lua_State *L)
{
	int iic = (int)luaL_checkinteger(L, 1);
	int skill = (int)luaL_checkinteger(L, 2);
	int row, cost = 0;

	if (!cpage(iic))
		return luaL_error(L, "no character at %d", iic);
	ensure(L);
	row = ci(f_cs_find, 3, iic, skill, 0);
	if (!row) {
		lua_pushboolean(L, 0);
		return 1;
	}
	cost = ci(f_cs_cost, 3, iic, row, -1);
	g_lua_forget = 1;
	ci(f_cs_del, 2, iic, skill);
	g_lua_forget = 0;
	lua_pushboolean(L, 1);
	lua_pushinteger(L, cost);
	return 2;
}

static int add_cb(lua_State *L, int *refs, int *n)
{
	luaL_checktype(L, 1, LUA_TFUNCTION);
	ensure(L);
	if (*n >= CB_MAX)
		return luaL_error(L, "too many callbacks registered");
	lua_pushvalue(L, 1);
	refs[(*n)++] = luaL_ref(L, LUA_REGISTRYINDEX);
	return 0;
}

int rq_lua_chara_on_create(lua_State *L)
{
	return add_cb(L, g_create_cb, &g_ncreate);
}

int rq_lua_chara_on_forget(lua_State *L)
{
	return add_cb(L, g_forget_cb, &g_nforget);
}

/* ---- rq.store: per-mod key/value, next to the active save ---- */

static cJSON *g_store;
static char g_store_path[RQ_PATH_MAX];

static void store_open(void)
{
	FILE *f;
	long n;
	char *buf;

	if (g_store)
		return;
	snprintf(g_store_path, sizeof g_store_path, "%s\\rqmod-store.json",
		 config.save_dir && config.save_dir[0] ? config.save_dir : g_rq_game_dir);
	f = fopen(g_store_path, "rb");
	if (f) {
		fseek(f, 0, SEEK_END);
		n = ftell(f);
		fseek(f, 0, SEEK_SET);
		if (n > 0 && n < (16 << 20) && (buf = malloc((size_t)n + 1)) != NULL) {
			if (fread(buf, 1, (size_t)n, f) == (size_t)n) {
				buf[n] = 0;
				g_store = cJSON_Parse(buf);
			}
			free(buf);
		}
		fclose(f);
	}
	if (!g_store || !cJSON_IsObject(g_store)) {
		cJSON_Delete(g_store);
		g_store = cJSON_CreateObject();
	}
}

static void store_flush(void)
{
	FILE *f;
	char *s;

	if (!g_store)
		return;
	s = cJSON_Print(g_store);
	if (!s)
		return;
	f = fopen(g_store_path, "wb");
	if (f) {
		fwrite(s, 1, strlen(s), f);
		fclose(f);
	} else {
		rq_log("store: cannot write %s", g_store_path);
	}
	free(s);
}

static cJSON *store_mod(lua_State *L, int create)
{
	const char *mod = mod_of(L);
	cJSON *m;

	store_open();
	m = cJSON_GetObjectItemCaseSensitive(g_store, mod);
	if (!m && create) {
		m = cJSON_AddObjectToObject(g_store, mod);
	}
	return m;
}

int rq_lua_store_get(lua_State *L)
{
	const char *key = luaL_checkstring(L, 1);
	cJSON *m = store_mod(L, 0);
	cJSON *v = m ? cJSON_GetObjectItemCaseSensitive(m, key) : NULL;

	if (!v) {
		lua_pushvalue(L, 2);
		return 1;
	}
	if (cJSON_IsBool(v))
		lua_pushboolean(L, cJSON_IsTrue(v));
	else if (cJSON_IsNumber(v))
		lua_pushnumber(L, v->valuedouble);
	else if (cJSON_IsString(v))
		lua_pushstring(L, v->valuestring);
	else
		lua_pushvalue(L, 2);
	return 1;
}

int rq_lua_store_set(lua_State *L)
{
	const char *key = luaL_checkstring(L, 1);
	cJSON *m;

	if (lua_isnoneornil(L, 2)) {
		m = store_mod(L, 0);
		if (m && cJSON_GetObjectItemCaseSensitive(m, key)) {
			cJSON_DeleteItemFromObjectCaseSensitive(m, key);
			store_flush();
		}
		return 0;
	}
	m = store_mod(L, 1);
	if (!m)
		return luaL_error(L, "store: cannot open");
	cJSON_DeleteItemFromObjectCaseSensitive(m, key);
	if (lua_isboolean(L, 2))
		cJSON_AddBoolToObject(m, key, lua_toboolean(L, 2));
	else if (lua_type(L, 2) == LUA_TNUMBER)
		cJSON_AddNumberToObject(m, key, lua_tonumber(L, 2));
	else if (lua_type(L, 2) == LUA_TSTRING)
		cJSON_AddStringToObject(m, key, lua_tostring(L, 2));
	else
		return luaL_error(L, "store.set: only numbers, strings and booleans can be stored");
	store_flush();
	return 0;
}

int rq_lua_store_delete(lua_State *L)
{
	lua_settop(L, 1);
	lua_pushnil(L);
	return rq_lua_store_set(L);
}

int rq_lua_store_keys(lua_State *L)
{
	cJSON *m = store_mod(L, 0);
	cJSON *it;
	int n = 0;

	lua_newtable(L);
	if (!m)
		return 1;
	cJSON_ArrayForEach(it, m) {
		lua_pushstring(L, it->string);
		lua_rawseti(L, -2, ++n);
	}
	return 1;
}

#endif /* RQ_MODLOADER */
