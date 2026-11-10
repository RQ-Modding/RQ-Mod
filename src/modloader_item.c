/* Copyright (C) 2026 Rance Quest modding fork
 *
 * rq.item: define items, hand them out, read and edit instances, change one
 * item into another. Same shape as the character roster API:
 *   - tail of Ｉ生成／固有データ／Ａ builds a Lua-defined item (the game's own
 *     numbers fall through untouched), then applies rq.item.edit patches.
 *   - tail of Ｉ生成 runs on_create.
 *   - head of Ｉ生成 and Ｔアイテム屋／入荷品 grow ◆アイテム補充数 (348 entries,
 *     indexed by item number) so numbers from 1000 up do not read outside it.
 */

#ifdef RQ_MODLOADER

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#include <time.h>

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
#include "vm/page.h"

#define TPL_MAX 128
#define EDIT_MAX 128
#define CB_MAX 16
#define ID_MAX 64
#define MOD_MAX 48
#define RESTOCK_MIN 11000 /* items 1000..9999 plus headroom */

struct tpl {
	char id[ID_MAX];
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
static int g_create_cb[CB_MAX], g_ncreate;
static int gi_item = -2, gi_restock = -1;
static int f_make = -1, f_data = -1, f_d0 = -1, f_d1 = -1, f_free = -1;
static int f_name = -1, f_info = -1, f_del = -1, f_equip = -1, f_recalc = -1;
static int g_stale;

static const struct { const char *alias; const char *jp; } k_fields[] = {
	{ "place", "存在" }, { "no", "固有番号" }, { "icon", "ＣＧ番号" },
	{ "type", "種類" }, { "subtype", "種類詳細" }, { "rank", "種類ランク" },
	{ "unidentified", "未確定" }, { "price", "価格" }, { "rarity", "レアランク" },
	{ "locked", "外せません" }, { "vanishes", "消滅します" }, { "level", "装備ＬＶ" },
};
#define NFIELDS ((int)(sizeof k_fields / sizeof k_fields[0]))

static const struct { const char *name; int id; } k_types[] = {
	{ "weapon", 1 }, { "armor", 11 }, { "item", 17 }, { "special", 21 },
	{ "quest", 22 }, { "none", 23 },
};

static int type_id(const char *s)
{
	int i;
	for (i = 0; i < 6; i++) {
		if (!strcmp(s, k_types[i].name))
			return k_types[i].id;
	}
	return 0;
}

static const char *type_name(int id)
{
	int i;
	for (i = 0; i < 6; i++) {
		if (k_types[i].id == id)
			return k_types[i].name;
	}
	return "unknown";
}

/* ---- bytecode wrappers ---- */

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

/* Original first, then CALLSYS. Only int arguments. */
static void wrap_tail(struct ain *a, int fno, int sys)
{
	int dup = ain_dup_function(a, fno);
	int nargs = a->functions[fno].nr_args, i;
	struct buffer out;

	buffer_init(&out, NULL, 0);
	emit1(&out, FUNC, fno);
	for (i = 0; i < nargs; i++)
		emit1(&out, SH_LOCALREF, i);
	emit1(&out, CALLFUNC, dup);
	emit1(&out, CALLSYS, sys);
	emit0(&out, RETURN);
	emit1(&out, ENDFUNC, fno);
	commit_wrap(a, fno, &out);
}

/* CALLSYS first (it pops the first int argument and pushes it back), then the original. */
static void wrap_head(struct ain *a, int fno, int sys)
{
	int dup = ain_dup_function(a, fno);
	int nargs = a->functions[fno].nr_args, i;
	struct buffer out;

	buffer_init(&out, NULL, 0);
	emit1(&out, FUNC, fno);
	emit1(&out, SH_LOCALREF, 0);
	emit1(&out, CALLSYS, sys);
	for (i = 1; i < nargs; i++)
		emit1(&out, SH_LOCALREF, i);
	emit1(&out, CALLFUNC, dup);
	emit0(&out, RETURN);
	emit1(&out, ENDFUNC, fno);
	commit_wrap(a, fno, &out);
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

static void call_text(int fno, int a, const char *utf)
{
	int ret;
	if (fno < 0 || !utf || !utf[0])
		return;
	stack_push(a);
	stack_push(new_str(utf));
	vm_call(fno, -1);
	if (ain->functions[fno].return_type.data == AIN_VOID)
		return;
	ret = stack_pop().i;
	if (ain->functions[fno].return_type.data == AIN_STRING)
		heap_unref(ret);
}

/* ---- direct access to Ｉ[ii] ---- */

static struct page *slot_page(int slot)
{
	if (!page_index_valid(slot))
		return NULL;
	return heap_get_page(slot);
}

static struct page *ipage(int ii)
{
	struct page *arr;
	if (gi_item == -2) {
		char *sj = utf2sjis("Ｉ", 0);
		gi_item = sj ? ain_get_global(ain, sj) : -1;
		free(sj);
	}
	if (!ain || gi_item < 0 || gi_item >= ain->nr_globals || !heap || !heap_size || !heap[0].page)
		return NULL;
	arr = slot_page(global_get(gi_item).i);
	if (!arr || arr->type != ARRAY_PAGE || ii <= 0 || ii >= arr->nr_vars)
		return NULL;
	return slot_page(arr->values[ii].i);
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

static struct page *marr(struct page *p, const char *jp)
{
	int m = memb_utf(p, jp);
	struct page *a;
	if (m < 0)
		return NULL;
	a = slot_page(p->values[m].i);
	return a && a->type == ARRAY_PAGE ? a : NULL;
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

/* ◆アイテム補充数 is read at index = item number. Make room for custom numbers. */
static void ensure_restock(void)
{
	int slot;
	struct page *old;
	union vm_value dims[1];

	if (gi_restock < 0 || !heap || !heap_size || !heap[0].page)
		return;
	slot = global_get(gi_restock).i;
	if (!page_index_valid(slot))
		return;
	old = heap_get_page(slot);
	if (!old || old->type != ARRAY_PAGE || old->array.rank != 1 || old->nr_vars >= RESTOCK_MIN)
		return;
	/* One resize: the new entries start at 0. (array_pushback per entry warned 10k times.) */
	dims[0].i = RESTOCK_MIN;
	heap_set_page(slot, realloc_array(old, 1, dims, old->a_type, old->array.struct_type, false));
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

static int resolve_no(lua_State *L, int idx)
{
	if (lua_type(L, idx) == LUA_TNUMBER)
		return (int)lua_tointeger(L, idx);
	if (lua_type(L, idx) == LUA_TSTRING)
		return id_to_no(lua_tostring(L, idx));
	return luaL_error(L, "item must be a number or a text id");
}

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

static int type_at(lua_State *L, int t, int def)
{
	int v = def;
	lua_getfield(L, t, "type");
	if (lua_type(L, -1) == LUA_TSTRING) {
		v = type_id(lua_tostring(L, -1));
		if (!v) {
			lua_pop(L, 1);
			return luaL_error(L, "item type must be weapon, armor, item, special, quest or none");
		}
	} else if (lua_isnumber(L, -1)) {
		v = (int)lua_tointeger(L, -1);
	}
	lua_pop(L, 1);
	return v;
}

/* effects = { {kind=, value=}, ... } up to 5, written to 機能[1..5] / 効果[1..5]. */
static void write_effects(lua_State *L, struct page *p, int t)
{
	struct page *kinds = marr(p, "機能"), *vals = marr(p, "効果");
	int i, n = (int)lua_rawlen(L, t);

	if (!kinds || !vals)
		return;
	for (i = 1; i <= 5; i++) {
		int kind = 0, val = 0;
		if (i <= n) {
			lua_rawgeti(L, t, i);
			if (lua_istable(L, -1)) {
				kind = tint(L, lua_gettop(L), "kind", 0);
				val = tint(L, lua_gettop(L), "value", 0);
			}
			lua_pop(L, 1);
		}
		page_set_var(kinds, i, kind);
		page_set_var(vals, i, val);
	}
}

/* ---- building a Lua-defined item ---- */

static void build_item(lua_State *L, int tab, int ii, int no, int lv)
{
	int jobs[8], i, n, mask_all = 1;
	int icon = tint(L, tab, "icon", 0);
	int price = tint(L, tab, "price", 100);
	int level = tint(L, tab, "level", 0);
	int type = type_at(L, tab, 17);
	int subtype = tint(L, tab, "subtype", 0);
	int rank = tint(L, tab, "rank", 10);
	char *name = tstr(L, tab, "name");
	char *info = tstr(L, tab, "info");
	struct page *p;

	if (!icon)
		icon = rq_image_placeholder();
	memset(jobs, 0, sizeof jobs);
	lua_getfield(L, tab, "classes");
	if (lua_istable(L, -1)) {
		mask_all = 0;
		n = (int)lua_rawlen(L, -1);
		for (i = 1; i <= n; i++) {
			int c;
			lua_rawgeti(L, -1, i);
			c = lua_isnumber(L, -1) ? (int)lua_tointeger(L, -1) : 0;
			lua_pop(L, 1);
			if (c >= 1 && c <= 8)
				jobs[c - 1] = c;
		}
	}
	lua_pop(L, 1);
	if (mask_all) {
		for (i = 0; i < 8; i++)
			jobs[i] = i + 1;
	}

	ensure_restock();
	stack_push(ii);
	stack_push(icon);
	stack_push(price);
	stack_push(level ? level : 1);
	for (i = 0; i < 8; i++)
		stack_push(jobs[i]);
	vm_call(f_d0, -1);

	{
		int kinds[5] = { 0 }, vals[5] = { 0 };
		lua_getfield(L, tab, "effects");
		if (lua_istable(L, -1)) {
			n = (int)lua_rawlen(L, -1);
			for (i = 1; i <= n && i <= 5; i++) {
				lua_rawgeti(L, -1, i);
				if (lua_istable(L, -1)) {
					kinds[i - 1] = tint(L, lua_gettop(L), "kind", 0);
					vals[i - 1] = tint(L, lua_gettop(L), "value", 0);
				}
				lua_pop(L, 1);
			}
		}
		lua_pop(L, 1);
		stack_push(ii);
		stack_push(no);
		stack_push(lv);
		stack_push(new_str(name ? name : "Item"));
		stack_push(type);
		stack_push(subtype);
		stack_push(rank);
		for (i = 0; i < 5; i++) {
			stack_push(kinds[i]);
			stack_push(vals[i]);
		}
		vm_call(f_d1, -1);
	}
	free(name);

	p = ipage(ii);
	if (p) {
		int rarity = tint(L, tab, "rarity", -1);
		if (rarity >= 0)
			mwrite(p, "レアランク", rarity);
		if (tint(L, tab, "locked", 0))
			mwrite(p, "外せません", 1);
		if (tint(L, tab, "vanishes", 0))
			mwrite(p, "消滅します", 1);
		lua_getfield(L, tab, "characters");
		if (lua_istable(L, -1)) {
			struct page *chars = marr(p, "装備キャラ");
			n = (int)lua_rawlen(L, -1);
			for (i = 1; chars && i <= n && i <= 8; i++) {
				lua_rawgeti(L, -1, i);
				page_set_var(chars, i, lua_isnumber(L, -1) ? (int)lua_tointeger(L, -1) : 0);
				lua_pop(L, 1);
			}
		}
		lua_pop(L, 1);
	}
	call_text(f_info, ii, info);
	free(info);
}

/* patch fields shared by edit and set. Value is at vidx. 1 changed, 0 same, -1 unknown. */
static int set_field(lua_State *L, int ii, struct page *p, const char *key, int vidx)
{
	const char *jp;
	int cur, v;

	if (!strcmp(key, "name") || !strcmp(key, "info")) {
		const char *s = lua_tostring(L, vidx);
		char *now;
		int changed;
		if (!s || !s[0])
			return 0;
		now = mstr(p, !strcmp(key, "name") ? "名前" : "情報");
		changed = strcmp(now, s) != 0;
		free(now);
		if (changed)
			call_text(!strcmp(key, "name") ? f_name : f_info, ii, s);
		return changed;
	}
	if (!strcmp(key, "effects")) {
		if (lua_istable(L, vidx))
			write_effects(L, p, vidx);
		return 1;
	}
	jp = alias_jp(key);
	if (!jp)
		return -1;
	if (lua_type(L, vidx) == LUA_TSTRING && !strcmp(key, "type")) {
		v = type_id(lua_tostring(L, vidx));
		if (!v)
			return luaL_error(L, "item type must be weapon, armor, item, special, quest or none");
	} else if (lua_isboolean(L, vidx)) {
		v = lua_toboolean(L, vidx);
	} else {
		v = (int)lua_tointeger(L, vidx);
	}
	if (!mread(p, jp, &cur))
		return -1;
	if (cur == v)
		return 0;
	mwrite(p, jp, v);
	return 1;
}

static void apply_edits(lua_State *L, int ii, int no, int existing)
{
	struct page *p = ipage(ii);
	int i;

	if (!p)
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
					/* A saved instance keeps where it is and what it is. */
					if (!(existing && (!strcmp(k, "place") || !strcmp(k, "no")))
					    && set_field(L, ii, p, k, lua_gettop(L)) < 0)
						rq_log("item.edit: unknown field '%s'", k);
				}
				lua_pop(L, 1);
			}
		}
		lua_pop(L, 1);
	}
}

/* A load brings back instances made before the edits, so they are patched on the next frame. */
void rq_item_mark_stale(void)
{
	g_stale = 1;
}

void rq_item_refresh(void)
{
	lua_State *L = rq_lua();
	int ii, i, count = 0;

	if (!g_stale)
		return;
	g_stale = 0;
	if (!L || !g_on || !g_nedit)
		return;
	for (ii = 1; ii <= 3000; ii++) {
		struct page *p = ipage(ii);
		int no, place, hit = 0;
		if (!p || !mread(p, "固有番号", &no) || !mread(p, "存在", &place) || place == 0)
			continue;
		for (i = 0; i < g_nedit && !hit; i++)
			hit = g_edit[i].no == no;
		if (!hit)
			continue;
		apply_edits(L, ii, no, 1);
		count++;
		if (place >= 1 && place <= 200 && f_recalc >= 0)
			ci(f_recalc, 1, place);
	}
	if (count)
		rq_log("item: edits applied to %d existing items", count);
}

/* ---- syscalls ---- */

static void item_data(void)
{
	lua_State *L = rq_lua();
	int ii = local_get(0).i, no = local_get(1).i, lv = local_get(2).i;
	struct tpl *t = tpl_by_no(no);

	if (!L || ii <= 0)
		return;
	if (t) {
		lua_rawgeti(L, LUA_REGISTRYINDEX, t->ref);
		if (lua_istable(L, -1))
			build_item(L, lua_gettop(L), ii, no, lv);
		lua_pop(L, 1);
	}
	if (g_nedit > 0)
		apply_edits(L, ii, no, 0);
}

/* Set by rq.item.info (lua/item.lua, loader 2.20.0) while it makes its lookup's instance: the
 * rq.item.on_create listeners do not see it, since it never enters the game. The item's own
 * template on_create still runs: it is part of the item's definition. */
static int g_quiet;

static int l_item_quiet(lua_State *L)
{
	g_quiet = lua_toboolean(L, 1);
	return 0;
}

static void item_create(void)
{
	lua_State *L = rq_lua();
	int ii = stack_pop().i;
	int no = local_get(0).i;
	int i;

	if (L && ii > 0) {
		struct tpl *t = tpl_by_no(no);
		if (t) {
			lua_rawgeti(L, LUA_REGISTRYINDEX, t->ref);
			lua_getfield(L, -1, "on_create");
			if (lua_isfunction(L, -1)) {
				lua_pushinteger(L, ii);
				if (lua_pcall(L, 1, 0, 0) != LUA_OK) {
					rq_log("item on_create error: %s", lua_tostring(L, -1));
					lua_pop(L, 1);
				}
			} else {
				lua_pop(L, 1);
			}
			lua_pop(L, 1);
		}
		for (i = 0; !g_quiet && i < g_ncreate; i++) {
			lua_rawgeti(L, LUA_REGISTRYINDEX, g_create_cb[i]);
			lua_newtable(L);
			lua_pushinteger(L, ii);
			lua_setfield(L, -2, "ii");
			lua_pushinteger(L, no);
			lua_setfield(L, -2, "no");
			if (t) {
				lua_pushstring(L, t->id);
				lua_setfield(L, -2, "id");
			}
			if (lua_pcall(L, 1, 0, 0) != LUA_OK) {
				rq_log("item on_create error: %s", lua_tostring(L, -1));
				lua_pop(L, 1);
			}
		}
	}
	stack_push(ii);
}

static void item_ensure(void)
{
	int a = stack_pop().i;
	ensure_restock();
	stack_push(a);
}

int rq_item_syscall2(int code);

int rq_item_syscall(int code)
{
	switch ((enum vm_extra_syscall)code) {
	case VM_XSYS_ITEM_DATA:
		item_data();
		return 1;
	case VM_XSYS_ITEM_CREATE:
		item_create();
		return 1;
	case VM_XSYS_ITEM_ENSURE:
		item_ensure();
		return 1;
	default:
		return rq_item_syscall2(code);
	}
}

int rq_item_install(struct ain *a)
{
	int f_make2, f_stock;
	char *sj;

	if (g_on)
		return 1;
	if (!a)
		return 0;
	f_make = find_fn(a, "Ｉ生成");
	f_data = find_fn(a, "Ｉ生成／固有データ／Ａ");
	f_d0 = find_fn(a, "Ｉ生成／データ０");
	f_d1 = find_fn(a, "Ｉ生成／データ１");
	f_free = find_fn(a, "Ｉ空き番号取得");
	f_name = find_fn(a, "Ｉ名前");
	f_info = find_fn(a, "Ｉ情報");
	f_del = find_fn(a, "Ｉ削除");
	f_equip = find_fn(a, "キャラ_装備設定");
	f_recalc = find_fn(a, "Ｃ再計算");
	f_stock = find_fn(a, "Ｔアイテム屋／入荷品");
	f_make2 = f_make;
	sj = utf2sjis("◆アイテム補充数", 0);
	gi_restock = sj ? ain_get_global(a, sj) : -1;
	free(sj);
	if (f_make < 0 || f_data < 0 || f_d0 < 0 || f_d1 < 0 || f_free < 0 || f_name < 0
	    || f_info < 0 || f_del < 0 || f_equip < 0 || f_stock < 0 || gi_restock < 0) {
		rq_log("item: missing AIN symbol (make=%d data=%d d0=%d d1=%d restock=%d)", f_make, f_data, f_d0, f_d1, gi_restock);
		return 0;
	}
	wrap_tail(a, f_data, VM_XSYS_ITEM_DATA);
	wrap_tail(a, f_make2, VM_XSYS_ITEM_CREATE);
	wrap_head(a, f_make2, VM_XSYS_ITEM_ENSURE);
	wrap_head(a, f_stock, VM_XSYS_ITEM_ENSURE);
	g_on = 1;
	rq_log("item: Ｉ生成／固有データ／Ａ tail -> template, Ｉ生成 -> on_create, restock array grows to %d", RESTOCK_MIN);
	return 1;
}

static int ensure(lua_State *L)
{
	if (!ain)
		return luaL_error(L, "AIN not loaded");
	if (!rq_item_install(ain))
		return luaL_error(L, "item install failed (see logs/rqmod.log)");
	ensure_restock();
	return 0;
}

/* ---- Lua ---- */

int rq_lua_item_define(lua_State *L)
{
	const char *id;
	struct tpl *t;
	int no;

	luaL_checktype(L, 1, LUA_TTABLE);
	rq_image_resolve_fields(L, 1);
	ensure(L);
	lua_getfield(L, 1, "id");
	if (!lua_isstring(L, -1) || !lua_tostring(L, -1)[0])
		return luaL_error(L, "item.define needs a text id");
	id = lua_tostring(L, -1);
	if (strlen(id) >= ID_MAX)
		return luaL_error(L, "item.define id is too long");
	no = id_to_no(id);
	t = tpl_by_no(no);
	if (t && strcmp(t->id, id))
		return luaL_error(L, "item id '%s' collides with '%s' (same number %d); pick another id", id, t->id, no);
	if (!t) {
		if (g_ntpl >= TPL_MAX)
			return luaL_error(L, "too many item.define calls");
		t = &g_tpl[g_ntpl++];
	} else {
		luaL_unref(L, LUA_REGISTRYINDEX, t->ref);
	}
	snprintf(t->id, sizeof t->id, "%s", id);
	t->no = no;
	lua_pop(L, 1);
	lua_pushvalue(L, 1);
	t->ref = luaL_ref(L, LUA_REGISTRYINDEX);
	lua_pushinteger(L, no);
	return 1;
}

int rq_lua_item_edit(lua_State *L)
{
	int no = resolve_no(L, 1);
	const char *mod = rq_lua_caller_mod(L);
	int i;

	luaL_checktype(L, 2, LUA_TTABLE);
	rq_image_resolve_fields(L, 2);
	ensure(L);
	if (!mod || !mod[0])
		mod = "_";
	for (i = 0; i < g_nedit; i++) {
		if (g_edit[i].no == no && !strcmp(g_edit[i].mod, mod)) {
			luaL_unref(L, LUA_REGISTRYINDEX, g_edit[i].ref);
			break;
		}
	}
	if (i == g_nedit) {
		if (g_nedit >= EDIT_MAX)
			return luaL_error(L, "too many item.edit calls");
		g_nedit++;
	}
	g_edit[i].no = no;
	snprintf(g_edit[i].mod, sizeof g_edit[i].mod, "%s", mod);
	lua_pushvalue(L, 2);
	g_edit[i].ref = luaL_ref(L, LUA_REGISTRYINDEX);
	g_stale = 1;
	return 0;
}

int rq_lua_item_number(lua_State *L)
{
	struct page *p = ipage((int)luaL_checkinteger(L, 1));
	int v;
	if (!p || !mread(p, "固有番号", &v) || v <= 0) {
		lua_pushnil(L);
		return 1;
	}
	lua_pushinteger(L, v);
	return 1;
}

int rq_lua_item_id_of(lua_State *L)
{
	struct tpl *t = tpl_by_no((int)luaL_checkinteger(L, 1));
	if (!t) {
		lua_pushnil(L);
		return 1;
	}
	lua_pushstring(L, t->id);
	return 1;
}

int rq_lua_item_get(lua_State *L)
{
	int ii = (int)luaL_checkinteger(L, 1);
	struct page *p = ipage(ii);
	struct page *kinds, *vals;
	struct tpl *t;
	char *s;
	int i, v, n = 0;

	if (!p || !mread(p, "存在", &v) || v == 0) {
		lua_pushnil(L);
		return 1;
	}
	lua_newtable(L);
	lua_pushinteger(L, ii);
	lua_setfield(L, -2, "ii");
	for (i = 0; i < NFIELDS; i++) {
		if (mread(p, k_fields[i].jp, &v)) {
			lua_pushinteger(L, v);
			lua_setfield(L, -2, k_fields[i].alias);
		}
	}
	if (mread(p, "種類", &v)) {
		lua_pushstring(L, type_name(v));
		lua_setfield(L, -2, "type_name");
	}
	s = mstr(p, "名前");
	lua_pushstring(L, s);
	lua_setfield(L, -2, "name");
	free(s);
	s = mstr(p, "情報");
	lua_pushstring(L, s);
	lua_setfield(L, -2, "info");
	free(s);
	if (mread(p, "固有番号", &v) && (t = tpl_by_no(v)) != NULL) {
		lua_pushstring(L, t->id);
		lua_setfield(L, -2, "id");
	}
	kinds = marr(p, "機能");
	vals = marr(p, "効果");
	lua_newtable(L);
	for (i = 1; kinds && vals && i <= 5; i++) {
		if (kinds->values[i].i == 0)
			continue;
		lua_newtable(L);
		lua_pushinteger(L, kinds->values[i].i);
		lua_setfield(L, -2, "kind");
		lua_pushinteger(L, vals->values[i].i);
		lua_setfield(L, -2, "value");
		lua_rawseti(L, -2, ++n);
	}
	lua_setfield(L, -2, "effects");
	return 1;
}

int rq_lua_item_set(lua_State *L)
{
	int ii = (int)luaL_checkinteger(L, 1);
	struct page *p = ipage(ii);

	luaL_checktype(L, 2, LUA_TTABLE);
	rq_image_resolve_fields(L, 2);
	ensure(L);
	if (!p)
		return luaL_error(L, "item.set: no item at %d", ii);
	lua_pushnil(L);
	while (lua_next(L, 2)) {
		if (lua_type(L, -2) == LUA_TSTRING) {
			const char *k = lua_tostring(L, -2);
			if (set_field(L, ii, p, k, lua_gettop(L)) < 0)
				return luaL_error(L, "item.set: unknown field '%s'", k);
		}
		lua_pop(L, 1);
	}
	return 0;
}

/* give(who [, {to = iic, place = "vault"|"quest"}]) -> ii */
int rq_lua_item_give(lua_State *L)
{
	int no = resolve_no(L, 1);
	int ii, to = 0, place = 7;
	struct page *p;

	ensure(L);
	if (no >= 1000 && !tpl_by_no(no))
		return luaL_error(L, "unknown item: define it before giving it");
	if (lua_istable(L, 2)) {
		char *pl = tstr(L, 2, "place");
		to = tint(L, 2, "to", 0);
		if (pl && !strcmp(pl, "quest"))
			place = 8;
		free(pl);
	}
	ii = ci(f_make, 2, no, 0);
	if (ii <= 0) {
		lua_pushnil(L);
		lua_pushstring(L, "the game refused to create it (no such item number or no free slot)");
		return 2;
	}
	mwrite(ipage(ii), "存在", place);
	if (to > 0) {
		int type = 0;
		p = ipage(ii);
		if (p && mread(p, "種類", &type) && !ci(f_equip, 3, to, type, ii)) {
			lua_pushinteger(L, ii);
			lua_pushstring(L, "created in the vault; it cannot be equipped by that character");
			return 2;
		}
	}
	lua_pushinteger(L, ii);
	return 1;
}

/* ---- starting equipment of rq.chara.define / edit ---- */

static const struct { const char *key; int type; } k_gear[] = {
	{ "weapon", 1 }, { "armor", 11 }, { "item", 17 }, { "special", 21 },
};
#define NGEAR ((int)(sizeof k_gear / sizeof k_gear[0]))

int rq_item_is_gear_key(const char *key)
{
	int i;
	for (i = 0; i < NGEAR; i++) {
		if (!strcmp(key, k_gear[i].key))
			return 1;
	}
	return 0;
}

/* At define time, so a typo is an error the mod sees and not a problem in the middle of the game. */
void rq_item_check_gear(lua_State *L, int t)
{
	int i, used = 0;

	for (i = 0; i < NGEAR; i++) {
		int type;
		lua_getfield(L, t, k_gear[i].key);
		type = lua_type(L, -1);
		lua_pop(L, 1);
		if (type == LUA_TNIL)
			continue;
		if (type != LUA_TNUMBER && type != LUA_TSTRING)
			luaL_error(L, "%s must be an item number or a text id", k_gear[i].key);
		used = 1;
	}
	if (used)
		ensure(L);
}

/* Runs inside Ｃ生成, so problems go to the log. Items are looked up here, so the mod may define them later. */
void rq_item_give_gear(lua_State *L, int t, int iic)
{
	int i;

	for (i = 0; i < NGEAR; i++) {
		int no = 0, ii, type = 0;
		lua_getfield(L, t, k_gear[i].key);
		if (lua_type(L, -1) == LUA_TNUMBER || lua_type(L, -1) == LUA_TSTRING)
			no = resolve_no(L, lua_gettop(L));
		lua_pop(L, 1);
		if (!no)
			continue;
		if (no >= 1000 && !tpl_by_no(no)) {
			rq_log("chara %s: item %d is not defined", k_gear[i].key, no);
			continue;
		}
		ii = ci(f_make, 2, no, 0);
		if (ii <= 0) {
			rq_log("chara %s: the game refused to create item %d", k_gear[i].key, no);
			continue;
		}
		mwrite(ipage(ii), "存在", 7);
		if (!mread(ipage(ii), "種類", &type) || type != k_gear[i].type) {
			rq_log("chara %s: item %d has type %d, it does not fit that slot", k_gear[i].key, no, type);
			ci(f_del, 1, ii);
			continue;
		}
		if (!ci(f_equip, 3, iic, type, ii))
			rq_log("chara %s: character %d cannot equip item %d, it stays in the vault", k_gear[i].key, iic, no);
	}
}

/* transform(ii, who): the same instance becomes another item. Keeps place and owner. */
int rq_lua_item_transform(lua_State *L)
{
	int ii = (int)luaL_checkinteger(L, 1);
	int no = resolve_no(L, 2);
	struct page *p = ipage(ii);
	int place = 0;

	ensure(L);
	if (!p || !mread(p, "存在", &place) || place == 0)
		return luaL_error(L, "item.transform: no item at %d", ii);
	if (no >= 1000 && !tpl_by_no(no))
		return luaL_error(L, "unknown item: define it first");
	mwrite(p, "未確定", 0);
	ci(f_data, 3, ii, no, 0);
	mwrite(ipage(ii), "存在", place);
	if (!ipage(ii) || !mread(ipage(ii), "固有番号", &place) || place != no) {
		lua_pushboolean(L, 0);
		return 1;
	}
	lua_pushboolean(L, 1);
	return 1;
}

/* equip(ii, iic): put an existing instance in its slot on a character (キャラ_装備設定, which recalculates). */
int rq_lua_item_equip(lua_State *L)
{
	int ii = (int)luaL_checkinteger(L, 1);
	int iic = (int)luaL_checkinteger(L, 2);
	struct page *p = ipage(ii);
	int type = 0, place = 0;

	ensure(L);
	if (!p || !mread(p, "存在", &place) || place == 0 || !mread(p, "種類", &type)) {
		lua_pushboolean(L, 0);
		lua_pushstring(L, "no such item");
		return 2;
	}
	lua_pushboolean(L, ci(f_equip, 3, iic, type, ii) != 0);
	return 1;
}

/* remove(ii): take it off its owner if equipped, then delete the instance (Ｉ削除). */
int rq_lua_item_remove(lua_State *L)
{
	int ii = (int)luaL_checkinteger(L, 1);
	struct page *p = ipage(ii);
	int place = 0, type = 0;

	ensure(L);
	if (!p || !mread(p, "存在", &place) || place == 0) {
		lua_pushboolean(L, 0);
		return 1;
	}
	if (place >= 1 && place <= 200 && mread(p, "種類", &type))
		ci(f_equip, 3, place, type, 0);
	ci(f_del, 1, ii);
	lua_pushboolean(L, 1);
	return 1;
}

int rq_lua_item_equipped(lua_State *L)
{
	int iic = (int)luaL_checkinteger(L, 1);
	static const struct { const char *key, *jp; } slots[] = {
		{ "weapon", "武器" }, { "armor", "防具" }, { "item", "持ち物" }, { "special", "オプション" },
	};
	struct page *arr, *c;
	int i, slot;
	char *sj = utf2sjis("Ｃ", 0);
	int gi = sj ? ain_get_global(ain, sj) : -1;

	free(sj);
	lua_newtable(L);
	if (gi < 0 || !heap || !heap_size || !heap[0].page)
		return 1;
	slot = global_get(gi).i;
	if (!page_index_valid(slot))
		return 1;
	arr = heap_get_page(slot);
	if (!arr || arr->type != ARRAY_PAGE || iic <= 0 || iic >= arr->nr_vars || !page_index_valid(arr->values[iic].i))
		return 1;
	c = heap_get_page(arr->values[iic].i);
	for (i = 0; i < 4; i++) {
		int v;
		if (mread(c, slots[i].jp, &v) && v > 0) {
			lua_pushinteger(L, v);
			lua_setfield(L, -2, slots[i].key);
		}
	}
	return 1;
}

/* Item number or text id at idx, for modules that do not know the registry. */
int rq_item_known(int no)
{
	return no > 0 && (no < 1000 || tpl_by_no(no));
}

int rq_item_no_from_lua(lua_State *L, int idx)
{
	return resolve_no(L, idx);
}

/* ---- Frostbuy purchase: T_Shop@buyItem(ii) ---- */

static int g_buy_cb[CB_MAX], g_nbuy, g_buy_on;

static void wrap_tail_method1(struct ain *a, int fno, int sys)
{
	int dup = ain_dup_function(a, fno);
	struct buffer out;

	buffer_init(&out, NULL, 0);
	emit1(&out, FUNC, fno);
	emit0(&out, PUSHSTRUCTPAGE);
	emit1(&out, SH_LOCALREF, 0);
	emit1(&out, CALLMETHOD, dup);
	emit1(&out, CALLSYS, sys);
	emit0(&out, RETURN);
	emit1(&out, ENDFUNC, fno);
	commit_wrap(a, fno, &out);
}

static void shop_buy(void)
{
	lua_State *L = rq_lua();
	int ii = local_get(0).i, i;
	struct page *p = ipage(ii);
	int no = 0, price = 0;

	if (!L || !p)
		return;
	mread(p, "固有番号", &no);
	mread(p, "価格", &price);
	for (i = 0; i < g_nbuy; i++) {
		struct tpl *t = tpl_by_no(no);
		char *name = mstr(p, "名前");
		lua_rawgeti(L, LUA_REGISTRYINDEX, g_buy_cb[i]);
		lua_newtable(L);
		lua_pushinteger(L, ii);
		lua_setfield(L, -2, "ii");
		lua_pushinteger(L, no);
		lua_setfield(L, -2, "no");
		lua_pushinteger(L, price);
		lua_setfield(L, -2, "price");
		lua_pushstring(L, name);
		lua_setfield(L, -2, "name");
		free(name);
		if (t) {
			lua_pushstring(L, t->id);
			lua_setfield(L, -2, "id");
		}
		if (lua_pcall(L, 1, 0, 0) != LUA_OK) {
			rq_log("shop on_buy error: %s", lua_tostring(L, -1));
			lua_pop(L, 1);
		}
	}
}

int rq_lua_shop_on_buy(lua_State *L)
{
	int f;

	luaL_checktype(L, 1, LUA_TFUNCTION);
	ensure(L);
	if (!g_buy_on) {
		f = find_fn(ain, "T_Shop@buyItem");
		if (f < 0)
			return luaL_error(L, "T_Shop@buyItem not found");
		wrap_tail_method1(ain, f, VM_XSYS_SHOP_BUY);
		g_buy_on = 1;
		rq_log("item: T_Shop@buyItem tail -> on_buy");
	}
	if (g_nbuy >= CB_MAX)
		return luaL_error(L, "too many shop callbacks registered");
	lua_pushvalue(L, 1);
	g_buy_cb[g_nbuy++] = luaL_ref(L, LUA_REGISTRYINDEX);
	return 0;
}

/* ---- chest pools: rq.loot.add / remove / on_pick ---- */

#define POOLS 5
#define LOOT_MAX 64
static const char *const k_pool_name[POOLS] = { "item", "special", "rare", "ultra", "gear" };
/* 武器防具を選ぶ takes a ref int. wrap_tail passes its two local slots through as they are. */
static const char *const k_pool_fn[POOLS] = { "持ち物を選ぶ", "特殊を選ぶ", "レアアイテムを選ぶ", "ウルトラレアを選ぶ", "武器防具を選ぶ" };
static int g_pool_fno[POOLS];
static int g_loot_on;
static struct { int item, pool, unique; double chance; } g_ladd[LOOT_MAX];
static int g_nladd;
static struct { int item, pool; } g_lrm[LOOT_MAX];
static int g_nlrm;
static int g_pick_cb[CB_MAX], g_npick;

static int pool_at(lua_State *L, int t, const char *key)
{
	char *s = tstr(L, t, key);
	int i, pool = 0;

	if (!s)
		return 0;
	for (i = 0; i < POOLS; i++) {
		if (!strcmp(s, k_pool_name[i]))
			pool = i + 1;
	}
	if (!pool) {
		free(s);
		return luaL_error(L, "loot pool must be item, special, rare, ultra or gear");
	}
	free(s);
	return pool;
}

static int item_owned(int no)
{
	int ii;
	for (ii = 1; ii <= 3000; ii++) {
		struct page *p = ipage(ii);
		int n, place;
		if (!p || !mread(p, "固有番号", &n) || n != no || !mread(p, "存在", &place))
			continue;
		if (place != 0 && place != 6 && place != 9)
			return 1;
	}
	return 0;
}

static void loot_pick(void)
{
	lua_State *L = rq_lua();
	int res = stack_pop().i;
	int fno = vm_call_stack_fno(0), pool = 0, i;

	for (i = 0; i < POOLS; i++) {
		if (g_pool_fno[i] == fno)
			pool = i + 1;
	}
	if (pool) {
		for (i = 0; i < g_nlrm; i++) {
			if (g_lrm[i].item == res && (!g_lrm[i].pool || g_lrm[i].pool == pool))
				res = 0;
		}
		for (i = 0; i < g_nladd; i++) {
			if (g_ladd[i].pool && g_ladd[i].pool != pool)
				continue;
			if (rq_random_float() * 100.0 >= g_ladd[i].chance)
				continue;
			if (g_ladd[i].unique && item_owned(g_ladd[i].item))
				continue;
			res = g_ladd[i].item;
			break;
		}
		for (i = 0; L && i < g_npick; i++) {
			lua_rawgeti(L, LUA_REGISTRYINDEX, g_pick_cb[i]);
			lua_newtable(L);
			lua_pushstring(L, k_pool_name[pool - 1]);
			lua_setfield(L, -2, "pool");
			lua_pushinteger(L, res);
			lua_setfield(L, -2, "item");
			if (lua_pcall(L, 1, 1, 0) != LUA_OK) {
				rq_log("loot on_pick error: %s", lua_tostring(L, -1));
			} else if (lua_type(L, -1) == LUA_TNUMBER) {
				res = (int)lua_tointeger(L, -1);
			} else if (lua_type(L, -1) == LUA_TSTRING) {
				res = id_to_no(lua_tostring(L, -1));
			}
			lua_pop(L, 1);
		}
	}
	stack_push(res);
}

static int install_loot(lua_State *L)
{
	int i;

	if (g_loot_on)
		return 1;
	ensure(L);
	srand((unsigned)time(NULL));
	for (i = 0; i < POOLS; i++) {
		g_pool_fno[i] = find_fn(ain, k_pool_fn[i]);
		if (g_pool_fno[i] < 0)
			return luaL_error(L, "loot pool function %s not found", k_pool_fn[i]);
	}
	for (i = 0; i < POOLS; i++)
		wrap_tail(ain, g_pool_fno[i], VM_XSYS_LOOT_PICK);
	g_loot_on = 1;
	rq_log("item: chest pools (item, special, rare, ultra, gear) wrapped for rq.loot");
	return 1;
}

int rq_lua_loot_add(lua_State *L)
{
	luaL_checktype(L, 1, LUA_TTABLE);
	install_loot(L);
	if (g_nladd >= LOOT_MAX)
		return luaL_error(L, "too many loot.add calls");
	lua_getfield(L, 1, "item");
	if (lua_isnil(L, -1))
		return luaL_error(L, "loot.add needs item");
	g_ladd[g_nladd].item = resolve_no(L, lua_gettop(L));
	lua_pop(L, 1);
	g_ladd[g_nladd].pool = pool_at(L, 1, "pool");
	lua_getfield(L, 1, "chance");
	g_ladd[g_nladd].chance = lua_isnumber(L, -1) ? lua_tonumber(L, -1) : 1.0;
	lua_pop(L, 1);
	g_ladd[g_nladd].unique = tint(L, 1, "unique", 0);
	g_nladd++;
	return 0;
}

int rq_lua_loot_remove(lua_State *L)
{
	install_loot(L);
	if (g_nlrm >= LOOT_MAX)
		return luaL_error(L, "too many loot.remove calls");
	g_lrm[g_nlrm].item = resolve_no(L, 1);
	if (lua_istable(L, 2))
		g_lrm[g_nlrm].pool = pool_at(L, 2, "pool");
	else
		g_lrm[g_nlrm].pool = 0;
	g_nlrm++;
	return 0;
}

int rq_lua_loot_on_pick(lua_State *L)
{
	luaL_checktype(L, 1, LUA_TFUNCTION);
	install_loot(L);
	if (g_npick >= CB_MAX)
		return luaL_error(L, "too many loot callbacks registered");
	lua_pushvalue(L, 1);
	g_pick_cb[g_npick++] = luaL_ref(L, LUA_REGISTRYINDEX);
	return 0;
}

int rq_item_syscall2(int code)
{
	switch ((enum vm_extra_syscall)code) {
	case VM_XSYS_SHOP_BUY:
		shop_buy();
		return 1;
	case VM_XSYS_LOOT_PICK:
		loot_pick();
		return 1;
	default:
		return 0;
	}
}

int rq_lua_item_on_create(lua_State *L)
{
	luaL_checktype(L, 1, LUA_TFUNCTION);
	ensure(L);
	if (g_ncreate >= CB_MAX)
		return luaL_error(L, "too many item callbacks registered");
	lua_pushvalue(L, 1);
	g_create_cb[g_ncreate++] = luaL_ref(L, LUA_REGISTRYINDEX);
	return 0;
}

/* rq.item.lock, sell, donate and store: Lua over the game's own calls (lua/item.lua, loader 2.9.0);
 * rq.item.info (2.20.0). */
extern const char rq_item_lua[];
extern const unsigned rq_item_lua_len;

void rq_item_open_lua(lua_State *L, int rq_index)
{
	if (luaL_loadbuffer(L, rq_item_lua, rq_item_lua_len, "=item") != LUA_OK) {
		rq_log("item.lua: %s", lua_tostring(L, -1));
		lua_pop(L, 1);
		return;
	}
	lua_pushvalue(L, rq_index);
	/* The loader's own switch for rq.item.info (2.20.0); not on rq, so mods do not get it. */
	lua_newtable(L);
	lua_pushcfunction(L, l_item_quiet);
	lua_setfield(L, -2, "quiet");
	if (lua_pcall(L, 2, 0, 0) != LUA_OK) {
		rq_log("item.lua: %s", lua_tostring(L, -1));
		lua_pop(L, 1);
	}
}

#endif /* RQ_MODLOADER */
