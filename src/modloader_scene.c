/* Copyright (C) 2026 Rance Quest modding fork
 *
 * Scenes and dialogue. Three layers:
 *   - rq.ain.call: call any AIN function by name with int, bool, float or
 *     string arguments. A scene command blocks until it ends, because vm_call
 *     runs a nested VM loop.
 *   - rq.ain.hook: run Lua before or after any AIN function, read and change
 *     its arguments and result, or skip it. Needs no frame of its own: the head
 *     stays in the function's frame and jumps to the body, and "after" fires
 *     from the RETURN opcode, so every return path is covered.
 *   - rq.scene and rq.quests.on_event, written in Lua over the two above.
 */

#ifdef RQ_MODLOADER

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include <SDL.h>

#include "lua.h"
#include "lauxlib.h"

#include "cJSON.h"

#include "system4.h"
#include "system4/ain.h"
#include "system4/buffer.h"
#include "system4/instructions.h"
#include "system4/string.h"
#include "system4/utfsjis.h"

#include "modloader.h"
#include "modloader_priv.h"
#define VM_PRIVATE
#include "vm.h"
#include "vm/heap.h"
#include "vm/page.h"

#define HOOK_MAX 256
#define HOOK_CB_MAX 16
#define ARG_MAX 32
#define ONLY_ARGS 4
#define ONLY_VALS 16

/* rq.ain.hook's `only` (2.55.0): the callback runs only when each named argument holds one of its
 * values, checked here before any Lua runs. pos is the argument's place in ctx.args. */
struct only {
	int n;
	int pos[ONLY_ARGS];
	int nv[ONLY_ARGS];
	int v[ONLY_ARGS][ONLY_VALS];
};

struct hook {
	int fno;
	int before[HOOK_CB_MAX], nbefore;
	int after[HOOK_CB_MAX], nafter;
	struct only *before_only[HOOK_CB_MAX], *after_only[HOOK_CB_MAX];
	int running;
};

static struct hook g_hooks[HOOK_MAX];
static int g_nhooks;
/* The code each hook's stub jumps to: an address no function holds any more (2.34.0). */
static uint32_t g_hook_code[HOOK_MAX];
static uint16_t *g_ret_map;
static int g_ret_n;
int g_rq_scene_ret_on;

static int find_fn(const char *utf8)
{
	char *sjis = utf2sjis(utf8, 0);
	int fno;

	if (!sjis)
		return -1;
	fno = ain_get_function(ain, sjis);
	free(sjis);
	return fno;
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
	slot = heap_alloc_slot(VM_STRING);
	heap[slot].s = s;
	return slot;
}

static void push_str_slot(lua_State *L, int slot)
{
	char *u;

	if (slot < 0 || (size_t)slot >= heap_size || !heap[slot].s) {
		lua_pushliteral(L, "");
		return;
	}
	u = sjis2utf(heap[slot].s->text, 0);
	lua_pushstring(L, u ? u : "");
	free(u);
}

static int is_plain(int t)
{
	return t == AIN_INT || t == AIN_LONG_INT || t == AIN_BOOL || t == AIN_FLOAT || t == AIN_STRING;
}

static void push_value(lua_State *L, int t, union vm_value v)
{
	switch (t) {
	case AIN_INT:
	case AIN_LONG_INT:
		lua_pushinteger(L, v.i);
		break;
	case AIN_BOOL:
		lua_pushboolean(L, v.i != 0);
		break;
	case AIN_FLOAT:
		lua_pushnumber(L, v.f);
		break;
	case AIN_STRING:
		push_str_slot(L, v.i);
		break;
	default:
		lua_pushnil(L);
		break;
	}
}

/* A new value from the Lua stack. Strings come back as a new slot the VM owns. */
static union vm_value lua_value(lua_State *L, int idx, int t)
{
	union vm_value v;

	v.i = 0;
	switch (t) {
	case AIN_FLOAT:
		v.f = (float)lua_tonumber(L, idx);
		break;
	case AIN_BOOL:
		v.i = lua_toboolean(L, idx);
		break;
	case AIN_STRING:
		v.i = new_str(lua_tostring(L, idx));
		break;
	default:
		v.i = lua_isboolean(L, idx) ? lua_toboolean(L, idx) : (int)lua_tointeger(L, idx);
		break;
	}
	return v;
}

/* ---- rq.ain.call ---- */

/* Class@method. struct_type is not filled in for this game, and '@' can be the second
 * byte of a Shift-JIS character. */
static int is_method(const char *name)
{
	const char *p;

	for (p = name; *p; p += SJIS_2BYTE(*p) ? 2 : 1) {
		if (*p == '@')
			return 1;
	}
	return 0;
}

static int l_call(lua_State *L)
{
	const char *name = luaL_checkstring(L, 1);
	int fno, i, n, want = 0, rt;
	struct ain_function *fn;

	if (!ain || !heap || !heap_size || !heap[0].page)
		return luaL_error(L, "rq.ain.call: the game is not running yet");
	fno = find_fn(name);
	if (fno < 0)
		return luaL_error(L, "rq.ain.call: no function named %s", name);
	fn = &ain->functions[fno];
	if (is_method(fn->name))
		return luaL_error(L, "rq.ain.call: %s is a method and needs an object", name);
	/* A save image would hold this Lua call on its stack, and a load replaces the stack
	 * under it: loading from here is an access violation. */
	if (!strcmp(name, "SYS_セーブ実行") || !strcmp(name, "SYS_ロード実行"))
		return luaL_error(L, "rq.ain.call: %s cannot run from Lua", name);
	n = lua_gettop(L) - 1;
	for (i = 0; i < fn->nr_args; i++) {
		int t = fn->vars[i].type.data;
		if (t == AIN_VOID)
			continue;
		if (!is_plain(t))
			return luaL_error(L, "rq.ain.call: %s: argument %d is a reference or a struct", name, want + 1);
		want++;
	}
	if (n != want)
		return luaL_error(L, "rq.ain.call: %s takes %d arguments, got %d", name, want, n);
	/* Check first: nothing may be left on the VM stack by a Lua error. */
	for (i = 0, n = 2; i < fn->nr_args; i++) {
		int t = fn->vars[i].type.data;
		if (t == AIN_VOID)
			continue;
		if (t == AIN_STRING)
			luaL_checkstring(L, n);
		else if (t == AIN_FLOAT || t == AIN_INT || t == AIN_LONG_INT) {
			if (!lua_isboolean(L, n))
				luaL_checknumber(L, n);
		}
		n++;
	}
	for (i = 0, n = 2; i < fn->nr_args; i++) {
		int t = fn->vars[i].type.data;
		if (t == AIN_VOID)
			continue;
		stack_push(lua_value(L, n++, t));
	}
	vm_call(fno, -1);
	rt = ain->functions[fno].return_type.data;
	if (rt == AIN_VOID)
		return 0;
	{
		union vm_value r = stack_pop();
		if (!is_plain(rt)) {
			if (rt == AIN_STRUCT)
				heap_unref(r.i);
			return 0;
		}
		push_value(L, rt, r);
		if (rt == AIN_STRING)
			heap_unref(r.i);
	}
	return 1;
}

/* ---- rq.ain.global ---- */

/* A value that is a heap slot of an array or a struct. A plain value is not, even when
 * its number happens to be a valid slot, and a reference to a plain value is a slot and
 * a variable number. */
static int holds_page(int t)
{
	return !is_plain(t) && t != AIN_REF_INT && t != AIN_REF_FLOAT && t != AIN_REF_BOOL
		&& t != AIN_REF_LONG_INT && t != AIN_REF_FUNC_TYPE;
}

static int field_number(lua_State *L, struct page *page, const char *global, const char *field)
{
	struct ain_struct *st;
	char *sj, *utf;
	int i;

	if (page->index < 0 || page->index >= ain->nr_structures)
		luaL_error(L, "rq.ain.global: %s: bad struct at field %s", global, field);
	st = &ain->structures[page->index];
	sj = utf2sjis(field, 0);
	for (i = 0; sj && i < st->nr_members && i < page->nr_vars; i++) {
		if (!strcmp(st->members[i].name, sj)) {
			free(sj);
			return i;
		}
	}
	free(sj);
	utf = sjis2utf(st->name, 0);
	lua_pushfstring(L, "rq.ain.global: %s: struct %s has no field %s", global, utf ? utf : "?", field);
	free(utf);
	lua_error(L);
	return -1;
}

/* Finds a global, then walks the indexes and field names at stack positions first..last. */
static void walk_global_at(lua_State *L, int name_idx, int first, int last, struct page **page, int *varno, int *type)
{
	const char *name = luaL_checkstring(L, name_idx);
	char *sj;
	int i, gi;

	if (!ain || !heap || !heap_size || !heap[0].page)
		luaL_error(L, "rq.ain.global: the game is not running yet");
	sj = utf2sjis(name, 0);
	gi = sj ? ain_get_global(ain, sj) : -1;
	free(sj);
	if (gi < 0)
		luaL_error(L, "rq.ain.global: no global named %s", name);
	*page = heap[0].page;
	*varno = gi;
	*type = variable_type(*page, gi, NULL, NULL);
	for (i = first; i <= last; i++) {
		int slot = (*page)->values[*varno].i;
		struct page *inner = holds_page(*type) && page_index_valid(slot) ? heap_get_page(slot) : NULL;
		int at;

		if (lua_type(L, i) == LUA_TSTRING) {
			if (!inner || inner->type != STRUCT_PAGE)
				luaL_error(L, "rq.ain.global: %s has no struct at step %d", name, i - first + 1);
			at = field_number(L, inner, name, lua_tostring(L, i));
		} else {
			at = (int)luaL_checkinteger(L, i);
			if (!inner || inner->type != ARRAY_PAGE)
				luaL_error(L, "rq.ain.global: %s has no array at step %d", name, i - first + 1);
			if (at < 0 || at >= inner->nr_vars)
				luaL_error(L, "rq.ain.global: %s: index %d is out of range (size %d)", name, at, inner->nr_vars);
		}
		*page = inner;
		*varno = at;
		*type = variable_type(inner, at, NULL, NULL);
	}
}

static void walk_global(lua_State *L, int first, int last, struct page **page, int *varno, int *type)
{
	walk_global_at(L, 1, first, last, page, varno, type);
}

static int l_global(lua_State *L)
{
	struct page *page;
	int varno, type;

	walk_global(L, 2, lua_gettop(L), &page, &varno, &type);
	if (!is_plain(type))
		return luaL_error(L, "rq.ain.global: %s is an array or a struct: give an index or a field, or read a plain value", lua_tostring(L, 1));
	push_value(L, type, page->values[varno]);
	return 1;
}

static int l_set_global(lua_State *L)
{
	struct page *page;
	int top = lua_gettop(L);
	int varno, type;

	if (top < 2)
		return luaL_error(L, "rq.ain.set_global: expected a name and a value");
	walk_global(L, 2, top - 1, &page, &varno, &type);
	if (!is_plain(type))
		return luaL_error(L, "rq.ain.set_global: %s is an array or a struct: give an index or a field, or set a plain value", lua_tostring(L, 1));
	if (type == AIN_STRING)
		luaL_checkstring(L, top);
	else if (!lua_isboolean(L, top))
		luaL_checknumber(L, top);
	variable_set(page, varno, type, lua_value(L, top, type));
	return 0;
}

/* ---- rq.ain.method ---- */

/* rq.ain.method(path, "Class@method", args...): calls a method of the game on the struct that
 * path ({global, field or index, ...}) reaches, with plain arguments, as rq.ain.call does. */
static int l_method(lua_State *L)
{
	const char *name;
	struct page *page, *obj;
	struct ain_function *fn;
	int fno, i, n, k, base, want = 0, rt, varno, type, slot;
	char *cls;
	size_t cl;

	luaL_checktype(L, 1, LUA_TTABLE);
	name = luaL_checkstring(L, 2);
	if (!ain || !heap || !heap_size || !heap[0].page)
		return luaL_error(L, "rq.ain.method: the game is not running yet");
	fno = find_fn(name);
	if (fno < 0)
		return luaL_error(L, "rq.ain.method: no function named %s", name);
	fn = &ain->functions[fno];
	if (!is_method(fn->name))
		return luaL_error(L, "rq.ain.method: %s is not a method (use rq.ain.call)", name);
	k = (int)lua_rawlen(L, 1);
	if (k < 1)
		return luaL_error(L, "rq.ain.method: the path needs a global's name");
	n = lua_gettop(L) - 2;
	for (i = 0; i < fn->nr_args; i++) {
		int t = fn->vars[i].type.data;
		if (t == AIN_VOID)
			continue;
		if (!is_plain(t))
			return luaL_error(L, "rq.ain.method: %s: argument %d is a reference or a struct", name, want + 1);
		want++;
	}
	if (n != want)
		return luaL_error(L, "rq.ain.method: %s takes %d arguments, got %d", name, want, n);
	for (i = 0, n = 3; i < fn->nr_args; i++) {
		int t = fn->vars[i].type.data;
		if (t == AIN_VOID)
			continue;
		if (t == AIN_STRING)
			luaL_checkstring(L, n);
		else if (!lua_isboolean(L, n))
			luaL_checknumber(L, n);
		n++;
	}
	/* The path onto the stack, then walked as rq.ain.global walks its arguments. */
	base = lua_gettop(L);
	for (i = 1; i <= k; i++)
		lua_rawgeti(L, 1, i);
	walk_global_at(L, base + 1, base + 2, base + k, &page, &varno, &type);
	slot = page->values[varno].i;
	obj = holds_page(type) && page_index_valid(slot) ? heap_get_page(slot) : NULL;
	lua_settop(L, base);
	if (!obj || obj->type != STRUCT_PAGE || obj->index < 0 || obj->index >= ain->nr_structures)
		return luaL_error(L, "rq.ain.method: the path does not reach a struct");
	/* The method's class must be the struct's: "T_Foo@bar" on a T_Foo. */
	cls = sjis2utf(ain->structures[obj->index].name, 0);
	cl = cls ? strlen(cls) : 0;
	if (!cls || strncmp(name, cls, cl) || name[cl] != '@') {
		lua_pushfstring(L, "rq.ain.method: %s is not a method of %s", name, cls ? cls : "?");
		free(cls);
		return lua_error(L);
	}
	free(cls);
	for (i = 0, n = 3; i < fn->nr_args; i++) {
		int t = fn->vars[i].type.data;
		if (t == AIN_VOID)
			continue;
		stack_push(lua_value(L, n++, t));
	}
	vm_call_method(fno, slot);
	rt = fn->return_type.data;
	if (rt == AIN_VOID)
		return 0;
	{
		union vm_value r = stack_pop();
		if (!is_plain(rt)) {
			if (rt == AIN_STRUCT)
				heap_unref(r.i);
			return 0;
		}
		push_value(L, rt, r);
		if (rt == AIN_STRING)
			heap_unref(r.i);
	}
	return 1;
}

static int l_global_len(lua_State *L)
{
	struct page *page, *arr;
	int varno, type, slot;

	walk_global(L, 2, lua_gettop(L), &page, &varno, &type);
	slot = page->values[varno].i;
	arr = holds_page(type) && page_index_valid(slot) ? heap_get_page(slot) : NULL;
	if (!arr || arr->type != ARRAY_PAGE)
		return luaL_error(L, "rq.ain.global_len: %s is not an array", lua_tostring(L, 1));
	lua_pushinteger(L, arr->nr_vars);
	return 1;
}

/* ---- priv.keep (2.45.0) ---- */

/* A value that is a page: a struct, or an array of either kind the AIN uses (array@int as a
 * struct member is AIN_ARRAY_INT, an older type than AIN_ARRAY). */
static int is_page_type(int t)
{
	switch (t) {
	case AIN_STRUCT:
	case AIN_ARRAY_TYPE:
		return 1;
	default:
		return t != AIN_REF_ARRAY && ain_is_array_data_type(t);
	}
}

/* Writes src's values back into dst, a page of the same shape, keeping dst's heap slots: a game
 * function up the stack may hold one of them. Inner arrays and structs are walked the same way;
 * an inner array that changed size gets its page replaced in its own slot. */
static void restore_into(struct page *dst, struct page *src)
{
	int i;

	for (i = 0; i < dst->nr_vars && i < src->nr_vars; i++) {
		int type = variable_type(dst, i, NULL, NULL);

		if (is_page_type(type)) {
			int ds = dst->values[i].i, ss = src->values[i].i;
			struct page *dp = ds > 0 && page_index_valid(ds) ? heap_get_page(ds) : NULL;
			struct page *sp = ss > 0 && page_index_valid(ss) ? heap_get_page(ss) : NULL;

			if (dp && sp && dp->type == sp->type && dp->nr_vars == sp->nr_vars) {
				restore_into(dp, sp);
			} else if (type != AIN_STRUCT && ds > 0 && page_index_valid(ds)) {
				if (dp) {
					delete_page_vars(dp);
					free_page(dp);
				}
				heap_set_page(ds, sp ? copy_page(sp) : NULL);
			}
		} else if (type == AIN_STRING) {
			variable_set(dst, i, type, vm_copy(src->values[i], type));
		} else if (is_plain(type)) {
			dst->values[i] = src->values[i];
		}
	}
}

struct kept {
	int slot;            /* the heap slot of a kept array or struct; -1 for a plain value */
	struct page *copy;   /* its deep copy, or NULL for an empty array */
	struct page *holder; /* a plain value's page, variable and type */
	int varno, type;
	union vm_value val;
};

/* priv.keep(paths, fn, ...): runs fn(...), then puts back each value a path reaches (a path as
 * rq.ain.global takes it: { "Ｃ", iic }, { "◆取得可能スキル番号" }), as it was before fn: a whole
 * character, a whole array, or a plain value. fn's results are returned; its error is raised
 * again after the values are back. For the loader's own Lua: trying the game's code on a
 * character (its starting skills, a step's price) without changing anything. */
int rq_lua_keep(lua_State *L)
{
	int n, i, top, status;
	struct kept *k;

	luaL_checktype(L, 1, LUA_TTABLE);
	luaL_checktype(L, 2, LUA_TFUNCTION);
	n = (int)lua_rawlen(L, 1);
	k = lua_newuserdatauv(L, sizeof *k * (n > 0 ? n : 1), 0);
	/* Walk every path first: a bad one raises before anything is copied. */
	for (i = 0; i < n; i++) {
		struct page *page;
		int varno, type, base, m, j;

		lua_rawgeti(L, 1, i + 1);
		if (!lua_istable(L, -1))
			return luaL_error(L, "keep: path %d is not a table", i + 1);
		base = lua_gettop(L);
		m = (int)lua_rawlen(L, base);
		if (m < 1)
			return luaL_error(L, "keep: path %d is empty", i + 1);
		for (j = 1; j <= m; j++)
			lua_rawgeti(L, base, j);
		walk_global_at(L, base + 1, base + 2, base + m, &page, &varno, &type);
		lua_settop(L, base - 1);
		k[i].copy = NULL;
		if (is_page_type(type)) {
			int slot = page->values[varno].i;
			if (slot <= 0 || !page_index_valid(slot))
				return luaL_error(L, "keep: path %d reaches no array or struct", i + 1);
			k[i].slot = slot;
		} else if (is_plain(type)) {
			k[i].slot = -1;
			k[i].holder = page;
			k[i].varno = varno;
			k[i].type = type;
		} else {
			return luaL_error(L, "keep: path %d reaches a reference", i + 1);
		}
	}
	for (i = 0; i < n; i++) {
		if (k[i].slot > 0) {
			struct page *p = heap_get_page(k[i].slot);
			k[i].copy = p ? copy_page(p) : NULL;
		} else {
			k[i].val = vm_copy(k[i].holder->values[k[i].varno], k[i].type);
		}
	}
	top = lua_gettop(L);
	lua_pushvalue(L, 2);
	for (i = 3; i < top; i++)
		lua_pushvalue(L, i);
	status = lua_pcall(L, top - 3, LUA_MULTRET, 0);
	for (i = n - 1; i >= 0; i--) {
		if (k[i].slot > 0) {
			struct page *cur = page_index_valid(k[i].slot) ? heap_get_page(k[i].slot) : NULL;
			struct page *copy = k[i].copy;
			if (cur && copy && cur->type == copy->type && cur->nr_vars == copy->nr_vars) {
				restore_into(cur, copy);
				delete_page_vars(copy);
				free_page(copy);
			} else if (page_index_valid(k[i].slot)) {
				if (cur) {
					delete_page_vars(cur);
					free_page(cur);
				}
				heap_set_page(k[i].slot, copy);
			}
		} else {
			variable_set(k[i].holder, k[i].varno, k[i].type, k[i].val);
		}
	}
	if (status != LUA_OK)
		return lua_error(L);
	return lua_gettop(L) - top;
}

/* rq.ain.set_global_len(name, [step, ...,] size): makes a one-dimensional array global (or one a
 * path reaches) `size` long, as the game's own Realloc does: values kept, new elements empty. */
static int l_set_global_len(lua_State *L)
{
	struct page *page, *arr;
	union vm_value dim;
	int top = lua_gettop(L);
	int varno, type, slot, struct_type, size;

	if (top < 2)
		return luaL_error(L, "rq.ain.set_global_len: expected a name and a size");
	size = (int)luaL_checkinteger(L, top);
	if (size < 0 || size > 65536)
		return luaL_error(L, "rq.ain.set_global_len: size must be 0 to 65536");
	walk_global(L, 2, top - 1, &page, &varno, &type);
	slot = page->values[varno].i;
	arr = holds_page(type) && page_index_valid(slot) ? heap_get_page(slot) : NULL;
	if (!arr || arr->type != ARRAY_PAGE)
		return luaL_error(L, "rq.ain.set_global_len: %s is not an array", lua_tostring(L, 1));
	if (arr->array.rank != 1)
		return luaL_error(L, "rq.ain.set_global_len: %s has more than one dimension", lua_tostring(L, 1));
	if (arr->nr_vars == size)
		return 0;
	type = variable_type(page, varno, &struct_type, NULL);
	dim.i = size;
	heap_set_page(slot, realloc_array(arr, 1, &dim, type, struct_type, true));
	return 0;
}

/* ---- rq.ain.hook ---- */

static struct hook *hook_for(int fno)
{
	struct buffer out;
	uint32_t base, target;
	int id, i;

	for (i = 0; i < g_nhooks; i++) {
		if (g_hooks[i].fno == fno)
			return &g_hooks[i];
	}
	if (g_nhooks >= HOOK_MAX)
		return NULL;
	id = g_nhooks++;
	g_hooks[id].fno = fno;

	/* FUNC; PUSH id; CALLSYS; IFZ body; RETURN; body: JUMP original */
	base = ain->code_size;
	target = base + 6 + 6 + 6 + 6 + 2;
	buffer_init(&out, NULL, 0);
	buffer_write_int16(&out, (uint16_t)FUNC);
	buffer_write_int32(&out, (uint32_t)fno);
	buffer_write_int16(&out, (uint16_t)PUSH);
	buffer_write_int32(&out, (uint32_t)id);
	buffer_write_int16(&out, (uint16_t)CALLSYS);
	buffer_write_int32(&out, (uint32_t)VM_XSYS_SCENE_BEFORE);
	buffer_write_int16(&out, (uint16_t)IFZ);
	buffer_write_int32(&out, target);
	buffer_write_int16(&out, (uint16_t)RETURN);
	buffer_write_int16(&out, (uint16_t)JUMP);
	buffer_write_int32(&out, ain->functions[fno].address);
	ain->code = xrealloc(ain->code, ain->code_size + out.index);
	memcpy(ain->code + ain->code_size, out.buf, out.index);
	ain->code_size += out.index;
	g_hook_code[id] = ain->functions[fno].address;
	ain->functions[fno].address = base + 6;
	free(out.buf);
	return &g_hooks[id];
}

/* For the loader's code patches (modloader.c): the code addresses the stubs jump to, so a scan of
 * a function's code stops before a hooked function's code, which no function's address marks. */
int rq_hook_codes(const uint32_t **out)
{
	*out = g_hook_code;
	return g_nhooks;
}

static void ret_map_set(int fno, int id)
{
	if (fno >= g_ret_n) {
		int n = ain->nr_functions + 64;
		g_ret_map = xrealloc(g_ret_map, n * sizeof *g_ret_map);
		memset(g_ret_map + g_ret_n, 0, (n - g_ret_n) * sizeof *g_ret_map);
		g_ret_n = n;
	}
	g_ret_map[fno] = (uint16_t)(id + 1);
	g_rq_scene_ret_on = 1;
}

/* The variable of the argument at ctx.args position pos (non-void arguments counted from 1), or
 * -1. */
static int arg_var(struct ain_function *fn, int pos)
{
	int i, n = 1;

	for (i = 0; i < fn->nr_args; i++) {
		if (fn->vars[i].type.data == AIN_VOID)
			continue;
		if (n == pos)
			return i;
		n++;
	}
	return -1;
}

/* opts.only at idx into *o: { [pos] = value or { values } }, whole numbers for int arguments.
 * Returns 0 when there is none. Raises the errors before anything is registered. */
static int read_only(lua_State *L, int idx, struct ain_function *fn, const char *name, struct only *o)
{
	int k, j, len;

	memset(o, 0, sizeof *o);
	lua_getfield(L, idx, "only");
	if (lua_isnil(L, -1)) {
		lua_pop(L, 1);
		return 0;
	}
	if (!lua_istable(L, -1))
		return luaL_error(L, "rq.ain.hook: only is a table of argument positions");
	lua_pushnil(L);
	while (lua_next(L, -2)) {
		int pos, var, t;
		if (!lua_isinteger(L, -2))
			return luaL_error(L, "rq.ain.hook: only's keys are argument positions");
		pos = (int)lua_tointeger(L, -2);
		var = arg_var(fn, pos);
		if (pos < 1 || var < 0)
			return luaL_error(L, "rq.ain.hook: %s has no argument %d", name, pos);
		t = fn->vars[var].type.data;
		if (t != AIN_INT && t != AIN_LONG_INT)
			return luaL_error(L, "rq.ain.hook: only[%d]: %s's argument %d is not an int", pos, name, pos);
		if (o->n >= ONLY_ARGS)
			return luaL_error(L, "rq.ain.hook: only names at most %d arguments", ONLY_ARGS);
		k = o->n++;
		o->pos[k] = pos;
		if (lua_isinteger(L, -1)) {
			o->v[k][0] = (int)lua_tointeger(L, -1);
			o->nv[k] = 1;
		} else if (lua_istable(L, -1)) {
			len = (int)lua_rawlen(L, -1);
			if (len < 1 || len > ONLY_VALS)
				return luaL_error(L, "rq.ain.hook: only[%d] lists 1 to %d values", pos, ONLY_VALS);
			for (j = 1; j <= len; j++) {
				lua_rawgeti(L, -1, j);
				if (!lua_isinteger(L, -1))
					return luaL_error(L, "rq.ain.hook: only[%d] lists whole numbers", pos);
				o->v[k][j - 1] = (int)lua_tointeger(L, -1);
				lua_pop(L, 1);
			}
			o->nv[k] = len;
		} else {
			return luaL_error(L, "rq.ain.hook: only[%d] is a whole number or a list of them", pos);
		}
		lua_pop(L, 1);
	}
	lua_pop(L, 1);
	return o->n > 0;
}

static struct only *only_copy(const struct only *o)
{
	struct only *c = xmalloc(sizeof *c);
	memcpy(c, o, sizeof *c);
	return c;
}

/* Whether the current frame's arguments pass a callback's `only` (NULL: none, always). */
static int only_pass(const struct only *o, struct ain_function *fn)
{
	int k, j;

	if (!o)
		return 1;
	for (k = 0; k < o->n; k++) {
		int var = arg_var(fn, o->pos[k]), v, hit = 0;
		if (var < 0)
			return 0;
		v = local_get(var).i;
		for (j = 0; j < o->nv[k] && !hit; j++)
			hit = o->v[k][j] == v;
		if (!hit)
			return 0;
	}
	return 1;
}

/* The callbacks of one side whose `only` the current frame passes, into out. */
static int passing(int *cbs, struct only **only, int n, struct ain_function *fn, int *out)
{
	int i, m = 0;

	for (i = 0; i < n; i++) {
		if (only_pass(only[i], fn))
			out[m++] = cbs[i];
	}
	return m;
}

/* rq.ain.hook(name, { before = fn, after = fn, only = { [pos] = values } }) */
static int l_hook(lua_State *L)
{
	const char *name = luaL_checkstring(L, 1);
	struct hook *h;
	struct only only;
	int fno, has_only;

	luaL_checktype(L, 2, LUA_TTABLE);
	if (!ain)
		return luaL_error(L, "rq.ain.hook: no game loaded");
	fno = find_fn(name);
	if (fno < 0)
		return luaL_error(L, "rq.ain.hook: no function named %s", name);
	has_only = read_only(L, 2, &ain->functions[fno], name, &only);
	h = hook_for(fno);
	if (!h)
		return luaL_error(L, "rq.ain.hook: too many hooked functions");
	lua_getfield(L, 2, "before");
	if (lua_isfunction(L, -1)) {
		if (h->nbefore >= HOOK_CB_MAX)
			return luaL_error(L, "rq.ain.hook: too many callbacks on %s", name);
		h->before_only[h->nbefore] = has_only ? only_copy(&only) : NULL;
		h->before[h->nbefore++] = luaL_ref(L, LUA_REGISTRYINDEX);
	} else {
		lua_pop(L, 1);
	}
	lua_getfield(L, 2, "after");
	if (lua_isfunction(L, -1)) {
		if (h->nafter >= HOOK_CB_MAX)
			return luaL_error(L, "rq.ain.hook: too many callbacks on %s", name);
		h->after_only[h->nafter] = has_only ? only_copy(&only) : NULL;
		h->after[h->nafter++] = luaL_ref(L, LUA_REGISTRYINDEX);
		ret_map_set(fno, (int)(h - g_hooks));
	} else {
		lua_pop(L, 1);
	}
	return 0;
}

/* The table a callback gets: { name, args = {...} }. The current frame is the hooked function. */
static void push_ctx(lua_State *L, struct ain_function *fn, char *name_utf)
{
	int i, n = 1;

	lua_newtable(L);
	lua_pushstring(L, name_utf ? name_utf : "");
	lua_setfield(L, -2, "name");
	lua_newtable(L);
	for (i = 0; i < fn->nr_args; i++) {
		int t = fn->vars[i].type.data;
		if (t == AIN_VOID)
			continue;
		if (is_plain(t)) {
			push_value(L, t, local_get(i));
			lua_rawseti(L, -2, n);
		}
		n++;
	}
	lua_setfield(L, -2, "args");
}

/* Copy ctx.args back into the frame. Only what the callback changed. */
static void pull_args(lua_State *L, int ctx, struct ain_function *fn)
{
	struct page *page = heap_get_page(call_stack[call_stack_ptr - 1].page_slot);
	int i, n = 1;

	lua_getfield(L, ctx, "args");
	for (i = 0; i < fn->nr_args; i++) {
		int t = fn->vars[i].type.data;
		if (t == AIN_VOID)
			continue;
		if (is_plain(t)) {
			int same;
			lua_rawgeti(L, -1, n);
			push_value(L, t, local_get(i));
			same = lua_rawequal(L, -1, -2);
			lua_pop(L, 1);
			if (!same && !lua_isnil(L, -1)) {
				if (t == AIN_STRING)
					heap_unref(page->values[i].i);
				page->values[i] = lua_value(L, -1, t);
			}
			lua_pop(L, 1);
		}
		n++;
	}
	lua_pop(L, 1);
}

static void run_cbs(lua_State *L, int *cbs, int n, int ctx, const char *tag)
{
	int i;

	for (i = 0; i < n; i++) {
		lua_rawgeti(L, LUA_REGISTRYINDEX, cbs[i]);
		lua_pushvalue(L, ctx);
		if (lua_pcall(L, 1, 0, 0) != LUA_OK) {
			rq_log("ain.hook %s error: %s", tag, lua_tostring(L, -1));
			lua_pop(L, 1);
		}
	}
}

/* Stack: id. Leaves [result] flag; flag != 0 returns from the function. */
static void hook_before(void)
{
	int id = stack_pop().i;
	struct hook *h = &g_hooks[id];
	struct ain_function *fn = &ain->functions[h->fno];
	lua_State *L = rq_lua();
	int top, ctx, skip = 0, rt = fn->return_type.data, cbs[HOOK_CB_MAX], ncbs;
	char *utf;

	if (!L || !h->nbefore || h->running) {
		stack_push(0);
		return;
	}
	ncbs = passing(h->before, h->before_only, h->nbefore, fn, cbs);
	if (!ncbs) {
		stack_push(0);
		return;
	}
	top = lua_gettop(L);
	utf = sjis2utf(fn->name, 0);
	push_ctx(L, fn, utf);
	ctx = lua_gettop(L);
	h->running = 1;
	run_cbs(L, cbs, ncbs, ctx, "before");
	h->running = 0;
	pull_args(L, ctx, fn);
	lua_getfield(L, ctx, "skip");
	skip = lua_toboolean(L, -1);
	lua_pop(L, 1);
	if (skip && rt != AIN_VOID && !is_plain(rt)) {
		rq_log("ain.hook: cannot skip %s, it returns a struct", utf ? utf : "?");
		skip = 0;
	}
	if (skip && rt != AIN_VOID) {
		lua_getfield(L, ctx, "result");
		if (lua_isnil(L, -1) && rt == AIN_STRING) {
			lua_pop(L, 1);
			lua_pushliteral(L, "");
		}
		stack_push(lua_value(L, -1, rt));
		lua_pop(L, 1);
	}
	free(utf);
	lua_settop(L, top);
	stack_push(skip ? 1 : 0);
}

/* A hook whose callbacks the title cut off would stay "running", and never run again. */
void rq_scene_after_reset(void)
{
	int i;
	for (i = 0; i < g_nhooks; i++)
		g_hooks[i].running = 0;
}

/* Called by RETURN with the return value on top of the stack. */
void rq_scene_on_return(int fno)
{
	struct hook *h;
	struct ain_function *fn;
	lua_State *L;
	int top, ctx, rt, cbs[HOOK_CB_MAX], ncbs;
	char *utf;

	if (fno < 0 || fno >= g_ret_n || !g_ret_map[fno])
		return;
	h = &g_hooks[g_ret_map[fno] - 1];
	L = rq_lua();
	if (!L || h->running)
		return;
	fn = &ain->functions[fno];
	ncbs = passing(h->after, h->after_only, h->nafter, fn, cbs);
	if (!ncbs)
		return;
	rt = fn->return_type.data;
	top = lua_gettop(L);
	utf = sjis2utf(fn->name, 0);
	push_ctx(L, fn, utf);
	ctx = lua_gettop(L);
	if (is_plain(rt)) {
		push_value(L, rt, stack_peek(0));
		lua_setfield(L, ctx, "result");
	}
	h->running = 1;
	run_cbs(L, cbs, ncbs, ctx, "after");
	h->running = 0;
	if (is_plain(rt)) {
		int same;
		lua_getfield(L, ctx, "result");
		push_value(L, rt, stack_peek(0));
		same = lua_rawequal(L, -1, -2);
		lua_pop(L, 1);
		if (!same && !lua_isnil(L, -1)) {
			union vm_value old = stack_pop();
			if (rt == AIN_STRING)
				heap_unref(old.i);
			stack_push(lua_value(L, -1, rt));
		}
	}
	free(utf);
	lua_settop(L, top);
}

int rq_scene_syscall(int code)
{
	if (code == VM_XSYS_SCENE_BEFORE) {
		hook_before();
		return 1;
	}
	return 0;
}

/* ---- rq.quests.current ---- */

static int l_quest_current(lua_State *L)
{
	static int gi = -2;

	if (gi == -2) {
		char *sj = utf2sjis("◆iiq", 0);
		int i;
		gi = -1;
		for (i = 0; sj && i < ain->nr_globals; i++) {
			if (!strcmp(ain->globals[i].name, sj)) {
				gi = i;
				break;
			}
		}
		free(sj);
	}
	if (gi < 0 || !heap || !heap_size || !heap[0].page) {
		lua_pushinteger(L, 0);
		return 1;
	}
	lua_pushinteger(L, global_get(gi).i);
	return 1;
}

/* The read flag of one line of mod text: its own number, by mod and text. */
static int l_msgno(lua_State *L)
{
	lua_pushinteger(L, rq_msgskip_number(rq_lua_caller_mod(L), luaL_checkstring(L, 1)));
	return 1;
}

/* ---- the Lua layer ---- */

static const char lua_layer[] =
"local rq, msgno = ...\n"
"local call, hook = rq.ain.call, rq.ain.hook\n"
"-- The game's S is message 0 for every line, so one read line made them all read.\n"
"local function say_line(s) call('message', msgno(s), 0, s) end\n"
"local scene = {}\n"
"scene.cmd = setmetatable({}, { __index = function(_, name)\n"
"  return function(...) return call(name, ...) end\n"
"end })\n"
"local function text_lines(text, wait)\n"
"  local first = true\n"
"  for line in (tostring(text) .. '\\n'):gmatch('(.-)\\n') do\n"
"    if not first then call('R') end\n"
"    say_line(line)\n"
"    first = false\n"
"  end\n"
"  call(wait == false and 'B' or 'A')\n"
"end\n"
"function scene.begin() call('ADVON') end\n"
"function scene.finish() call('枠消去') call('ADVOFF') end\n"
"function scene.play(fn, ...)\n"
"  scene.begin()\n"
"  local r = table.pack(pcall(fn, scene, ...))\n"
"  if not r[1] and tostring(r[2]):find('rq: the game went to the title', 1, true) then\n"
"    rq.log('scene ended by the title')\n"
"    error(r[2], 0)\n"
"  end\n"
"  scene.finish()\n"
"  if not r[1] then rq.log('scene error: ' .. tostring(r[2])) return nil end\n"
"  return table.unpack(r, 2, r.n)\n"
"end\n"
"function scene.once(id, fn, ...)\n"
"  if scene.seen(id) then return false end\n"
"  scene.seen(id, true)\n"
"  scene.play(fn, ...)\n"
"  return true\n"
"end\n"
"function scene.seen(id, set)\n"
"  if set == nil then return call('EV', id, -2147483648) end\n"
"  return call('EV', id, set and 1 or 0)\n"
"end\n"
"function scene.say(face, text, o)\n"
"  o = o or {}\n"
"  local f = o.page and 'セリフＥ' or (o.face == false and 'セリフ２' or 'セリフ')\n"
"  call(f, face, o.name or '')\n"
"  text_lines(text, o.wait)\n"
"end\n"
"function scene.think(face, text, o)\n"
"  o = o or {}\n"
"  local f = o.page and '思考Ｅ' or (o.face == false and '思考２' or '思考')\n"
"  call(f, face, o.name or '')\n"
"  text_lines(text, o.wait)\n"
"end\n"
"function scene.narrate(text, o)\n"
"  o = o or {}\n"
"  call(o.paper and 'ト書き_paper' or (o.page and 'ト書きＥ' or 'ト書き'))\n"
"  text_lines(text, o.wait)\n"
"end\n"
"function scene.text(s) say_line(tostring(s)) end\n"
"function scene.br() call('R') end\n"
"function scene.wait() call('A') end\n"
"function scene.show() call('B') end\n"
"function scene.clear_text() call('枠消去') end\n"
"function scene.speed(ms) call('字速度', ms) end\n"
"function scene.wallpaper(cg, e, t) call('壁紙表示', cg, e or 1, t or 500) end\n"
"function scene.mask_wallpaper(cg, e, t) call('マスク壁紙表示', cg, e or 1, t or 500) end\n"
"function scene.background(cg, e, t, name, flag) call('背景表示', cg, e or 1, t or 500, name or '', flag or false) end\n"
"function scene.background_in(cg, t, name, flag) call('背景イン', cg, t or 500, name or '', flag or false) end\n"
"function scene.background_out(t) call('背景アウト', t or 500) end\n"
"function scene.portrait(cg, e, t) call('立ち絵表示', cg, e or 1, t or 200) end\n"
"function scene.mini(cg, e, t) call('ミニイベント表示', cg, e or 1, t or 200) end\n"
"function scene.cg(cg, e, t) call('イベントＣＧ表示', cg, e or 1, t or 500) end\n"
"function scene.set(wall, back, portrait, mini) call('セット表示', wall or 0, back or 0, portrait or 0, mini or 0) end\n"
"function scene.effect_set(e, wall, back, portrait, mini, ecg) call('エフェクトセット表示', e or 1, wall or 0, back or 0, portrait or 0, mini or 0, ecg or 0) end\n"
"function scene.black(e, t) call('黒画面', e or 1, t or 500) end\n"
"function scene.white(e, t) call('白画面', e or 1, t or 500) end\n"
"function scene.flash(t) call('画面フラッシュ', t or 200) end\n"
"function scene.shake(mode, flag) call('全画面揺れ', mode or 1, flag or false) end\n"
"function scene.shake_face(mode, flag) call('顔揺れ', mode or 1, flag or false) end\n"
"function scene.shake_portrait(mode, flag) call('立ち絵揺れ', mode or 1, flag or false) end\n"
"function scene.music(no, e, t) call('音楽', no, e or -1, t or -1) end\n"
"function scene.sound(no) call('効果音', no) end\n"
"function scene.jingle(no, wait) call('Ｒ８ジングル', no, wait or false) end\n"
"function scene.clear() call('画面消去') end\n"
"function scene.wait_click() return call('キー待ち_クリック') end\n"
"function scene.register_face(cg, name, tone) call('顔登録', cg, name, tone or 1) end\n"
"function scene.choice(items, o)\n"
"  o = o or {}\n"
"  if o.x then call('選択_位置設定', o.x, o.y or 360) else call('選択初期') end\n"
"  for i, it in ipairs(items) do\n"
"    if it == '-' then\n"
"      call('選択_区切り追加')\n"
"    elseif type(it) == 'string' then\n"
"      call('選択_項目追加', i, it, -1, -1, -1, '', -1)\n"
"    else\n"
"      call('選択_項目追加', it.id or i, it.text, it.r or -1, it.g or -1, it.b or -1, it.comment or '', it.sound or -1)\n"
"    end\n"
"  end\n"
"  return call('選択_実行', o.cancel or false, 0, 0, false)\n"
"end\n"
"function scene.before(name, fn) hook(name, { before = fn }) end\n"
"function scene.after(name, fn) hook(name, { after = fn }) end\n"
"function scene.replace(name, fn)\n"
"  hook(name, { before = function(h)\n"
"    h.skip = true\n"
"    local r = fn(h)\n"
"    if r ~= nil then h.result = r end\n"
"  end })\n"
"end\n"
"rq.scene = scene\n"
"local hit_mode, in_hit, hit_hooked\n"
"function rq.battle.force_hit(mode)\n"
"  if mode ~= nil and mode ~= 'hit' and mode ~= 'miss' then\n"
"    error('rq.battle.force_hit: expected hit, miss or nil', 2)\n"
"  end\n"
"  hit_mode = mode\n"
"  if not mode or hit_hooked then return end\n"
"  hit_hooked = true\n"
"  hook('命中計算', {\n"
"    before = function() in_hit = true end,\n"
"    after = function() in_hit = false end,\n"
"  })\n"
"  hook('確率演算', { before = function(h)\n"
"    if in_hit and hit_mode then\n"
"      h.result = hit_mode == 'hit'\n"
"      h.skip = true\n"
"    end\n"
"  end })\n"
"end\n"
"local kinds = { [4] = 'map_event', [5] = 'warp', [6] = 'warp_cancel', [7] = 'before_battle',\n"
"  [8] = 'after_battle', [9] = 'chest', [10] = 'contact', [11] = 'before_warp' }\n"
"local function event_of(h)\n"
"  local a = h.args\n"
"  local kind = a[1] == 8 and 'start' or (a[1] == 7 and 'map_load' or kinds[a[3]] or 'other')\n"
"  return { quest = rq.quests.current(), kind = kind, no = a[2], code = a[1], fg = a[3], result = h.result }\n"
"end\n"
"function rq.quests.on_event(fn)\n"
"  hook('クエスト処理', { before = function(h)\n"
"    local r = fn(event_of(h))\n"
"    if r ~= nil then h.skip = true h.result = r end\n"
"  end })\n"
"end\n"
"function rq.quests.on_event_after(fn)\n"
"  hook('クエスト処理', { after = function(h)\n"
"    local r = fn(event_of(h))\n"
"    if r ~= nil then h.result = r end\n"
"  end })\n"
"end\n"
"local quest_wanted, quest_hooked\n"
"local function leave_for_quest(h, tab)\n"
"  if quest_wanted then\n"
"    h.skip = true\n"
"    h.result = tab\n"
"  end\n"
"end\n"
"function rq.quests.start(no)\n"
"  if math.type(no) ~= 'integer' then\n"
"    error('rq.quests.start: expected a quest number', 2)\n"
"  end\n"
"  if quest_wanted then\n"
"    error('rq.quests.start: quest ' .. quest_wanted .. ' is already starting', 2)\n"
"  end\n"
"  if rq.quests.current() ~= 0 then\n"
"    error('rq.quests.start: a quest is running', 2)\n"
"  end\n"
"  if not rq.ain.call('クエスト_確認', no) or rq.ain.call('クエスト_名前', no) == '' then\n"
"    error('rq.quests.start: no quest number ' .. no, 2)\n"
"  end\n"
"  if not rq.ain.call('クエスト_有効', no) then\n"
"    error('rq.quests.start: this party cannot take quest ' .. no, 2)\n"
"  end\n"
"  local screen = rq.ui.get().screen\n"
"  if screen ~= 'hub' and screen ~= 'quests' then\n"
"    error('rq.quests.start: only from the hub or the quest list, not ' .. tostring(screen), 2)\n"
"  end\n"
"  quest_wanted = no\n"
"  if quest_hooked then return end\n"
"  quest_hooked = true\n"
"  hook('T_Town@checkKey', { before = function(h) leave_for_quest(h, 600014) end })\n"
"  hook('T_Quest@checkKey', { before = function(h)\n"
"    if h.args[1] == 0 then leave_for_quest(h, 600001) end\n"
"  end })\n"
"  hook('T_Quest@run', { after = function(h)\n"
"    if quest_wanted then\n"
"      h.result = quest_wanted\n"
"      quest_wanted = nil\n"
"    end\n"
"  end })\n"
"end\n"
"function rq.battle.time()\n"
"  return rq.ain.call('戦闘情報_回数')\n"
"end\n"
"function rq.battle.set_time(n)\n"
"  if math.type(n) ~= 'integer' or n < 0 or n > 999 then\n"
"    error('rq.battle.set_time: expected a whole number from 0 to 999', 2)\n"
"  end\n"
"  rq.ain.call('戦闘情報_回数設定', n, false)\n"
"end\n"
"local in_counter, counter_hooked\n"
"function rq.battle.on_use(fn)\n"
"  if type(fn) ~= 'function' then\n"
"    error('rq.battle.on_use: expected a function', 2)\n"
"  end\n"
"  if not counter_hooked then\n"
"    counter_hooked = true\n"
"    hook('戦闘_カウンター実行', {\n"
"      before = function() in_counter = true end,\n"
"      after = function() in_counter = false end,\n"
"    })\n"
"  end\n"
"  hook('戦闘_行動実行', { before = function(h)\n"
"    local skill, unit, target = h.args[1], h.args[2], h.args[3]\n"
"    if skill <= 0 then return end\n"
"    local side = 'summon'\n"
"    if rq.ain.call('戦闘番号_味方確認', unit) then\n"
"      side = 'ally'\n"
"    elseif rq.ain.call('戦闘番号_敵確認', unit) then\n"
"      side = 'enemy'\n"
"    end\n"
"    fn({\n"
"      skill = skill,\n"
"      user = rq.ain.call('戦闘番号_ＩＩＣ', unit),\n"
"      unit = unit,\n"
"      target = target > 0 and rq.ain.call('戦闘番号_ＩＩＣ', target) or 0,\n"
"      target_unit = target > 0 and target or 0,\n"
"      side = side,\n"
"      counter = in_counter == true,\n"
"    })\n"
"  end })\n"
"end\n"
"-- rq.dungeon.place is in src/lua/quests.lua since 2.39.0.\n"
"local FACE = { north = 1, east = 2, south = 3, west = 4 }\n"
"function rq.dungeon.place_player(x, z, opts)\n"
"  if math.type(x) ~= 'integer' or math.type(z) ~= 'integer' then\n"
"    error('rq.dungeon.place_player: expected whole cell numbers', 2)\n"
"  end\n"
"  opts = opts or {}\n"
"  local face = FACE[opts.face or 'south']\n"
"  if not face then\n"
"    error('rq.dungeon.place_player: face is north, east, south or west', 2)\n"
"  end\n"
"  local grid = rq.dungeon.grid({ whole = true })\n"
"  if not grid then\n"
"    error('rq.dungeon.place_player: only on a dungeon map', 2)\n"
"  end\n"
"  local room = grid.room\n"
"  local row = grid.rows[room.maxZ - z + 1]\n"
"  local at = x - room.minX + 1\n"
"  local ch = row and at >= 1 and row:sub(at, at)\n"
"  if ch ~= '.' and ch ~= ':' and ch ~= '@' then\n"
"    error('rq.dungeon.place_player: cell ' .. x .. ',' .. z .. ' is not free floor', 2)\n"
"  end\n"
"  rq.ain.call('Ｒ８キャラ配置', x + 1, 50 - z, face)\n"
"  return true\n"
"end\n"
"local type = type\n"
"local function removable(register)\n"
"  local free = {}\n"
"  return function(fn)\n"
"    if type(fn) ~= 'function' then return register(fn) end\n"
"    local cell = free[#free]\n"
"    if cell then\n"
"      free[#free] = nil\n"
"      cell.fn = fn\n"
"    else\n"
"      cell = { fn = fn }\n"
"      register(function(...)\n"
"        local f = cell.fn\n"
"        if f then return f(...) end\n"
"      end)\n"
"    end\n"
"    local live = true\n"
"    return function()\n"
"      if live then\n"
"        live = false\n"
"        cell.fn = nil\n"
"        free[#free + 1] = cell\n"
"      end\n"
"    end\n"
"  end\n"
"end\n"
"for key, value in pairs(rq) do\n"
"  if type(value) == 'function' and key:sub(1, 3) == 'on_' then\n"
"    rq[key] = removable(value)\n"
"  elseif type(value) == 'table' then\n"
"    for name, fn in pairs(value) do\n"
"      if type(fn) == 'function' and type(name) == 'string' and name:sub(1, 3) == 'on_' then\n"
"        value[name] = removable(fn)\n"
"      end\n"
"    end\n"
"  end\n"
"end\n"
"local raw_hook, hook_slots = rq.ain.hook, {}\n"
"-- A callback's C slot is used again for the next callback with the same `only` (2.55.0).\n"
"local function only_key(only)\n"
"  if type(only) ~= 'table' then return '' end\n"
"  local parts = {}\n"
"  for pos, v in pairs(only) do\n"
"    local vals = {}\n"
"    if type(v) == 'table' then\n"
"      for i, x in ipairs(v) do vals[i] = x end\n"
"      table.sort(vals)\n"
"      for i, x in ipairs(vals) do vals[i] = tostring(x) end\n"
"    else\n"
"      vals[1] = tostring(v)\n"
"    end\n"
"    parts[#parts + 1] = tostring(pos) .. '=' .. table.concat(vals, ',')\n"
"  end\n"
"  table.sort(parts)\n"
"  return ':' .. table.concat(parts, ';')\n"
"end\n"
"function rq.ain.hook(name, spec)\n"
"  if type(spec) ~= 'table' then return raw_hook(name, spec) end\n"
"  local only = spec.only\n"
"  raw_hook(name, { only = only })\n"
"  local removers = {}\n"
"  for _, phase in ipairs({ 'before', 'after' }) do\n"
"    if type(spec[phase]) == 'function' then\n"
"      local key = phase .. ':' .. name .. only_key(only)\n"
"      hook_slots[key] = hook_slots[key] or removable(function(fn)\n"
"        raw_hook(name, { [phase] = fn, only = only })\n"
"      end)\n"
"      removers[#removers + 1] = hook_slots[key](spec[phase])\n"
"    end\n"
"  end\n"
"  return function()\n"
"    for _, remove in ipairs(removers) do remove() end\n"
"  end\n"
"end\n";

extern const char rq_scene_lua[];
extern const unsigned rq_scene_lua_len;
extern const char rq_scene_file_lua[];
extern const unsigned rq_scene_file_lua_len;
extern const char rq_scene_check_lua[];
extern const unsigned rq_scene_check_lua_len;

static int l_menu_open(lua_State *L)
{
	lua_pushboolean(L, rq_hud_menu_open());
	return 1;
}

/* A row's comment for the game's choice menu (2.45.1). T_Select::showComment cuts the comment at
 * each "@" and then into pieces of 28 Shift-JIS bytes (テキスト分割_強制改行(text, 14)) with no
 * regard for words, and only then does the comment window (T_Comment) start a line at "@", "$"
 * and "\n". So a "\n" from a mod did not reset the count, and words were cut where the 28 bytes
 * ended. The loader hands the game lines it never cuts: each line of the mod's text ("\n") is
 * wrapped at a space, or beside a letter that is not ASCII, to at most 28 bytes (an ASCII letter
 * counts 1, half-width katakana 1, any other letter 2, as Shift-JIS stores them); a word longer
 * than a line is cut where it stops fitting. "@" and "$" become "＠" and "＄", as in rq.ui.confirm;
 * "\r" goes and other control letters but the tab become spaces. The lines are joined with sep:
 * "@" for the game, "\n" for the kit's copy of the menu (rq.uikit.options). */
#define MENU_COMMENT_BYTES 28

static int mc_width(uint32_t cp)
{
	return (cp < 0x80 || (cp >= 0xFF61 && cp <= 0xFF9F)) ? 1 : 2;
}

static int mc_decode(const unsigned char *s, size_t *i, size_t n, uint32_t *cp)
{
	unsigned char c = s[*i];
	int len = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 1;
	uint32_t v = len == 1 ? c : len == 2 ? (c & 0x1F) : len == 3 ? (c & 0x0F) : (c & 0x07);
	int k;
	if (*i + (size_t)len > n)
		len = 1, v = c;
	for (k = 1; k < len; k++) {
		unsigned char d = s[*i + k];
		if ((d & 0xC0) != 0x80) {
			len = 1;
			v = c;
			break;
		}
		v = (v << 6) | (d & 0x3F);
	}
	if (len == 1 && c >= 0x80)
		v = '?';
	*i += (size_t)len;
	*cp = v;
	return len;
}

static void mc_put(luaL_Buffer *b, uint32_t cp)
{
	char u[4];
	int n;
	if (cp < 0x80) {
		u[0] = (char)cp;
		n = 1;
	} else if (cp < 0x800) {
		u[0] = (char)(0xC0 | (cp >> 6));
		u[1] = (char)(0x80 | (cp & 0x3F));
		n = 2;
	} else if (cp < 0x10000) {
		u[0] = (char)(0xE0 | (cp >> 12));
		u[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
		u[2] = (char)(0x80 | (cp & 0x3F));
		n = 3;
	} else {
		u[0] = (char)(0xF0 | (cp >> 18));
		u[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
		u[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
		u[3] = (char)(0x80 | (cp & 0x3F));
		n = 4;
	}
	luaL_addlstring(b, u, (size_t)n);
}

/* menu_comment(text, sep?) -> the text as lines of at most 28 bytes joined by sep (default "@"). */
int rq_lua_menu_comment(lua_State *L)
{
	size_t n, i = 0, sepn;
	const unsigned char *s = (const unsigned char *)luaL_optlstring(L, 1, "", &n);
	const char *sep = luaL_optlstring(L, 2, "@", &sepn);
	uint32_t *cp = malloc((n + 1) * sizeof *cp);
	size_t cn = 0, p, a, first = 1;
	luaL_Buffer b;

	if (!cp)
		return luaL_error(L, "menu_comment: out of memory");
	while (i < n) {
		uint32_t c;
		mc_decode(s, &i, n, &c);
		if (c == '\r')
			continue;
		if (c == '@')
			c = 0xFF20; /* ＠ */
		else if (c == '$')
			c = 0xFF04; /* ＄ */
		else if (c < 32 && c != '\n' && c != '\t')
			c = ' ';
		cp[cn++] = c;
	}
	luaL_buffinit(L, &b);
	/* each line of the text */
	for (a = 0; a <= cn;) {
		size_t e = a;
		while (e < cn && cp[e] != '\n')
			e++;
		p = a;
		do {
			size_t end = p, cut, k;
			int w = 0;
			while (end < e && w + mc_width(cp[end]) <= MENU_COMMENT_BYTES)
				w += mc_width(cp[end++]);
			if (end >= e) {
				cut = e;
			} else {
				/* the last place a line may end: before a space, or beside a letter not ASCII */
				cut = 0;
				for (k = end; k > p; k--) {
					if (cp[k] == ' ' || cp[k] >= 0x80 || cp[k - 1] >= 0x80) {
						cut = k;
						break;
					}
				}
				if (!cut)
					cut = end;
			}
			if (!first)
				luaL_addlstring(&b, sep, sepn);
			first = 0;
			k = cut;
			while (k > p && cp[k - 1] == ' ')
				k--;
			{
				size_t j;
				for (j = p; j < k; j++)
					mc_put(&b, cp[j]);
			}
			p = cut;
			while (p < e && cp[p] == ' ')
				p++;
		} while (p < e);
		a = e + 1;
	}
	free(cp);
	luaL_pushresult(&b);
	return 1;
}

/* ---- Scene files (2.60.0, lua/scene_file.lua) ---- */

#define SCENE_FILE_MAX (4 * 1024 * 1024)
#define SCENE_JSON_DEPTH 200

/* A cJSON value as Lua: an array gets the metatable at arr_idx (so an empty array and an empty
 * object differ), null the sentinel at null_idx, a whole number an integer. */
static int json_push(lua_State *L, const cJSON *v, int null_idx, int arr_idx, int depth, const char **why)
{
	const cJSON *c;
	lua_Integer i;

	if (depth > SCENE_JSON_DEPTH || !lua_checkstack(L, 4)) {
		*why = "nested too deep";
		return 0;
	}
	if (cJSON_IsNull(v)) {
		lua_pushvalue(L, null_idx);
	} else if (cJSON_IsBool(v)) {
		lua_pushboolean(L, cJSON_IsTrue(v));
	} else if (cJSON_IsNumber(v)) {
		double d = v->valuedouble;
		if (d >= -9007199254740992.0 && d <= 9007199254740992.0 && d == (double)(i = (lua_Integer)d))
			lua_pushinteger(L, i);
		else
			lua_pushnumber(L, d);
	} else if (cJSON_IsString(v)) {
		lua_pushstring(L, v->valuestring ? v->valuestring : "");
	} else if (cJSON_IsArray(v)) {
		lua_Integer n = 0;
		lua_newtable(L);
		lua_pushvalue(L, arr_idx);
		lua_setmetatable(L, -2);
		for (c = v->child; c; c = c->next) {
			if (!json_push(L, c, null_idx, arr_idx, depth + 1, why))
				return 0;
			lua_rawseti(L, -2, ++n);
		}
	} else if (cJSON_IsObject(v)) {
		lua_newtable(L);
		for (c = v->child; c; c = c->next) {
			const char *k = c->string ? c->string : "";
			lua_getfield(L, -1, k);
			if (!lua_isnil(L, -1)) {
				static char msg[160];
				lua_pop(L, 1);
				snprintf(msg, sizeof msg, "the field \"%.100s\" twice in one object", k);
				*why = msg;
				return 0;
			}
			lua_pop(L, 1);
			if (!json_push(L, c, null_idx, arr_idx, depth + 1, why))
				return 0;
			lua_setfield(L, -2, k);
		}
	} else {
		*why = "a value JSON does not have";
		return 0;
	}
	return 1;
}

/* read_json(path, null, array_mt) -> value | nil, why. path is in the calling mod's folder (the mod
 * on the stack, or the one run_as_mod runs as). A byte-order mark is skipped. */
static int l_read_json(lua_State *L)
{
	const char *path = luaL_checkstring(L, 1);
	char full[RQ_PATH_MAX];
	FILE *f;
	long size;
	char *buf, *text;
	const char *end = NULL, *why = NULL;
	cJSON *root;

	luaL_checkany(L, 2);
	luaL_checktype(L, 3, LUA_TTABLE);
	lua_settop(L, 3);
	if (!rq_sandbox_resolve(L, path, full, (int)sizeof full)) {
		lua_pushnil(L);
		lua_pushstring(L, "not a file in the mod's folder");
		return 2;
	}
	f = fopen(full, "rb");
	if (!f) {
		lua_pushnil(L);
		lua_pushstring(L, "no such file");
		return 2;
	}
	fseek(f, 0, SEEK_END);
	size = ftell(f);
	fseek(f, 0, SEEK_SET);
	if (size < 0 || size > SCENE_FILE_MAX) {
		fclose(f);
		lua_pushnil(L);
		lua_pushstring(L, size < 0 ? "cannot be read" : "larger than 4 MB");
		return 2;
	}
	buf = malloc((size_t)size + 1);
	if (!buf || fread(buf, 1, (size_t)size, f) != (size_t)size) {
		fclose(f);
		free(buf);
		lua_pushnil(L);
		lua_pushstring(L, "cannot be read");
		return 2;
	}
	fclose(f);
	buf[size] = 0;
	text = buf;
	if (size >= 3 && (unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB
	    && (unsigned char)text[2] == 0xBF)
		text += 3;
	if ((long)strlen(text) != size - (long)(text - buf)) {
		free(buf);
		lua_pushnil(L);
		lua_pushstring(L, "a zero byte in the file: not a JSON text file");
		return 2;
	}
	root = cJSON_ParseWithOpts(text, &end, 1);
	if (!root) {
		int line = 1, col = 1;
		const char *c;
		if (!end || end < text || end > text + strlen(text))
			end = text;
		for (c = text; c < end; c++) {
			if (*c == '\n') {
				line++;
				col = 1;
			} else if (((unsigned char)*c & 0xC0) != 0x80) {
				col++;
			}
		}
		free(buf);
		lua_pushnil(L);
		lua_pushfstring(L, "not valid JSON at line %d, column %d", line, col);
		return 2;
	}
	free(buf);
	if (!json_push(L, root, 2, 3, 0, &why)) {
		cJSON_Delete(root);
		lua_settop(L, 3);
		lua_pushnil(L);
		lua_pushstring(L, why ? why : "cannot be read");
		return 2;
	}
	cJSON_Delete(root);
	return 1;
}

/* caller_id() -> the calling mod's id, or nil. */
static int l_caller_id(lua_State *L)
{
	const char *id = rq_sandbox_caller_id(L);
	if (id && id[0])
		lua_pushstring(L, id);
	else
		lua_pushnil(L);
	return 1;
}

/* rqmod-ctl playscene and scenefunctions: the pipe thread leaves a request here ("playscene ..."
 * or "scenefunctions ...") and waits; the game thread's frame (scene_file.lua, through ps_poll and
 * ps_answer) takes it and answers. */
static SDL_SpinLock g_ps_lock;
static char g_ps_req[600];
static char g_ps_ans[16 * 1024];
static unsigned g_ps_seq, g_ps_taken, g_ps_answered;
static int g_ps_pending;

static int l_ps_poll(lua_State *L)
{
	char req[sizeof g_ps_req];
	int has = 0;
	SDL_AtomicLock(&g_ps_lock);
	if (g_ps_pending) {
		memcpy(req, g_ps_req, sizeof req);
		g_ps_pending = 0;
		g_ps_taken = g_ps_seq;
		has = 1;
	}
	SDL_AtomicUnlock(&g_ps_lock);
	if (!has)
		return 0;
	lua_pushstring(L, req);
	return 1;
}

static int l_ps_answer(lua_State *L)
{
	const char *s = luaL_checkstring(L, 1);
	SDL_AtomicLock(&g_ps_lock);
	snprintf(g_ps_ans, sizeof g_ps_ans, "%s", s);
	g_ps_answered = g_ps_taken;
	SDL_AtomicUnlock(&g_ps_lock);
	return 0;
}

/* The pipe thread's side: the request (the command's name and its words), then the game's answer
 * (lines starting "ok" or "err", or playscene status's), or a timeout when no frame of mod code ran
 * (a paused game, a native dialog). */
int rq_scene_cmd(const char *cmd, const char *args, char **out, char *err, int errsz)
{
	unsigned seq;
	int waited;

	if (out)
		*out = NULL;
	SDL_AtomicLock(&g_ps_lock);
	seq = ++g_ps_seq;
	snprintf(g_ps_req, sizeof g_ps_req, "%s %s", cmd, args ? args : "");
	g_ps_pending = 1;
	SDL_AtomicUnlock(&g_ps_lock);
	for (waited = 0; waited < 5000; waited += 10) {
		SDL_Delay(10);
		SDL_AtomicLock(&g_ps_lock);
		if (g_ps_answered == seq) {
			size_t n = strlen(g_ps_ans);
			char *s = malloc(n + 2);
			if (s) {
				memcpy(s, g_ps_ans, n);
				s[n] = '\n';
				s[n + 1] = 0;
			}
			SDL_AtomicUnlock(&g_ps_lock);
			if (!s) {
				if (err)
					snprintf(err, errsz, "out of memory");
				return 0;
			}
			if (out)
				*out = s;
			else
				free(s);
			return 1;
		}
		SDL_AtomicUnlock(&g_ps_lock);
	}
	SDL_AtomicLock(&g_ps_lock);
	if (g_ps_seq == seq)
		g_ps_pending = 0;
	SDL_AtomicUnlock(&g_ps_lock);
	if (err)
		snprintf(err, errsz, "timeout: the game ran no frame of mod code in 5 s");
	return 0;
}

void rq_scene_open(lua_State *L, int rq_index)
{
	int check;

	lua_getfield(L, rq_index, "ain");
	lua_pushcfunction(L, l_call);
	lua_setfield(L, -2, "call");
	lua_pushcfunction(L, l_hook);
	lua_setfield(L, -2, "hook");
	lua_pushcfunction(L, l_global);
	lua_setfield(L, -2, "global");
	lua_pushcfunction(L, l_set_global);
	lua_setfield(L, -2, "set_global");
	lua_pushcfunction(L, l_global_len);
	lua_setfield(L, -2, "global_len");
	lua_pushcfunction(L, l_set_global_len);
	lua_setfield(L, -2, "set_global_len");
	lua_pushcfunction(L, l_method);
	lua_setfield(L, -2, "method");
	lua_pop(L, 1);
	lua_getfield(L, rq_index, "quests");
	lua_pushcfunction(L, l_quest_current);
	lua_setfield(L, -2, "current");
	lua_pop(L, 1);
	if (luaL_loadstring(L, lua_layer) != LUA_OK) {
		rq_log("scene layer: %s", lua_tostring(L, -1));
		lua_pop(L, 1);
		return;
	}
	lua_pushvalue(L, rq_index);
	lua_pushcfunction(L, l_msgno);
	if (lua_pcall(L, 2, 0, 0) != LUA_OK) {
		rq_log("scene layer: %s", lua_tostring(L, -1));
		lua_pop(L, 1);
		return;
	}
	/* rq.scene.start and active (lua/scene.lua, loader 1.14.0). */
	if (luaL_loadbuffer(L, rq_scene_lua, rq_scene_lua_len, "=scene") != LUA_OK) {
		rq_log("scene: %s", lua_tostring(L, -1));
		lua_pop(L, 1);
		return;
	}
	lua_pushvalue(L, rq_index);
	lua_pushcfunction(L, l_menu_open);
	lua_pushcfunction(L, rq_lua_menu_comment);
	if (lua_pcall(L, 3, 0, 0) != LUA_OK) {
		rq_log("scene: %s", lua_tostring(L, -1));
		lua_pop(L, 1);
		return;
	}
	/* Scene files and rqmod-ctl playscene (lua/scene_file.lua, loader 2.60.0), with their checker
	 * in a chunk of its own (lua/scene_check.lua, 2.61.0). */
	if (luaL_loadbuffer(L, rq_scene_check_lua, rq_scene_check_lua_len, "=scene_check") != LUA_OK
	    || lua_pcall(L, 0, 1, 0) != LUA_OK) {
		rq_log("scene_check: %s", lua_tostring(L, -1));
		lua_pop(L, 1);
		return;
	}
	check = lua_gettop(L);
	if (luaL_loadbuffer(L, rq_scene_file_lua, rq_scene_file_lua_len, "=scene_file") != LUA_OK) {
		rq_log("scene_file: %s", lua_tostring(L, -1));
		lua_pop(L, 2);
		return;
	}
	lua_pushvalue(L, rq_index);
	lua_pushcfunction(L, l_read_json);
	lua_pushcfunction(L, rq_lua_run_as_mod);
	lua_pushcfunction(L, l_caller_id);
	lua_pushcfunction(L, l_ps_poll);
	lua_pushcfunction(L, l_ps_answer);
	lua_pushvalue(L, check);
	if (lua_pcall(L, 7, 0, 0) != LUA_OK) {
		rq_log("scene_file: %s", lua_tostring(L, -1));
		lua_pop(L, 1);
	}
	lua_pop(L, 1);
}

#endif /* RQ_MODLOADER */
