/* Copyright (C) 2026 Rance Quest modding fork
 *
 * Frostbuy stock: wrap the tail of Ｔアイテム屋 so Lua can force items into
 * shop stock (existence 6) via Ｔアイテム屋／入荷品. Policy stays in Lua.
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
static int g_stock_ref = LUA_NOREF;
static int g_f_stock = -1; /* Ｔアイテム屋／入荷品 */
static int g_f_has = -1;   /* Ｉ有無 */

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

/* Call original Ｔアイテム屋, then CALLSYS for Lua on_stock. */
static int wrap_tail(struct ain *a, int fno, int sys)
{
	int dup = ain_dup_function(a, fno);
	struct buffer out;

	buffer_init(&out, NULL, 0);
	emit1(&out, FUNC, fno);
	emit1(&out, CALLFUNC, dup);
	emit1(&out, CALLSYS, sys);
	emit0(&out, RETURN);
	emit1(&out, ENDFUNC, fno);
	commit_wrap(a, fno, &out);
	rq_log("shop: wrapped Ｔアイテム屋 tail -> on_stock");
	return 1;
}

static int call_has(int no, int fg)
{
	int slot;
	int r;
	if (g_f_has < 0)
		return 0;
	slot = vm_string_ref(string_ref(&EMPTY_STRING));
	stack_push(no);
	stack_push(fg);
	stack_push(slot);
	vm_call(g_f_has, -1);
	r = stack_pop().i;
	return r;
}

static int call_stock(int no)
{
	int r;
	if (g_f_stock < 0)
		return 0;
	stack_push(no);
	vm_call(g_f_stock, -1);
	r = stack_pop().i;
	return r;
}

static void shop_on_stock(void)
{
	lua_State *L = rq_lua();
	int top;

	if (!L || g_stock_ref == LUA_NOREF)
		return;
	top = lua_gettop(L);
	lua_rawgeti(L, LUA_REGISTRYINDEX, g_stock_ref);
	if (!lua_isfunction(L, -1)) {
		lua_settop(L, top);
		return;
	}
	if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
		rq_log("shop: on_stock error: %s", lua_tostring(L, -1));
		lua_pop(L, 1);
	}
	lua_settop(L, top);
}

int rq_shop_syscall(int code)
{
	switch ((enum vm_extra_syscall)code) {
	case VM_XSYS_SHOP_STOCK:
		shop_on_stock();
		return 1;
	default:
		return 0;
	}
}

int rq_shop_install(struct ain *ain)
{
	int f_shop;

	if (!ain)
		return 0;
	if (g_on)
		return 1;

	f_shop = find_fn(ain, "Ｔアイテム屋");
	g_f_stock = find_fn(ain, "Ｔアイテム屋／入荷品");
	g_f_has = find_fn(ain, "Ｉ有無");

	if (f_shop < 0) {
		rq_log("shop: Ｔアイテム屋 not found");
		return 0;
	}
	if (g_f_stock < 0) {
		rq_log("shop: Ｔアイテム屋／入荷品 not found");
		return 0;
	}
	if (g_f_has < 0) {
		rq_log("shop: Ｉ有無 not found");
		return 0;
	}
	if (!wrap_tail(ain, f_shop, VM_XSYS_SHOP_STOCK))
		return 0;

	g_on = 1;
	rq_log("shop: install ok (stock=%d has=%d)", g_f_stock, g_f_has);
	return 1;
}

int rq_lua_shop_on_stock(struct lua_State *L)
{
	luaL_checktype(L, 1, LUA_TFUNCTION);
	if (g_stock_ref != LUA_NOREF)
		luaL_unref(L, LUA_REGISTRYINDEX, g_stock_ref);
	lua_pushvalue(L, 1);
	g_stock_ref = luaL_ref(L, LUA_REGISTRYINDEX);
	return 0;
}

int rq_lua_shop_in_stock(struct lua_State *L)
{
	int no = (int)luaL_checkinteger(L, 1);
	lua_pushboolean(L, call_has(no, 6) != 0);
	return 1;
}

int rq_lua_shop_stock(struct lua_State *L)
{
	int no = (int)luaL_checkinteger(L, 1);
	int ii;
	if (!g_on)
		return luaL_error(L, "shop.stock: call rq.shop.install first");
	ii = call_stock(no);
	lua_pushinteger(L, ii);
	return 1;
}

#endif /* RQ_MODLOADER */
