/* Copyright (C) 2026 Rance Quest modding fork
 *
 * rq.config: a Mods tab in the game's Config screen. The tab, its page and its
 * hooks are Lua (src/lua/config.lua, built into the exe). C does what Lua cannot:
 * it adds the tab button to the game's button group (a ref argument), tells
 * the Discord row on Other 2 to stay off while that page stands in for the Mods page,
 * and reads and sets the window's magnification filter for Other 2's radio.
 */

#ifdef RQ_MODLOADER

#include <stdlib.h>
#include <string.h>

#include "lua.h"
#include "lauxlib.h"

#include "system4.h"
#include "system4/ain.h"
#include "system4/buffer.h"
#include "system4/instructions.h"
#include "system4/utfsjis.h"

#include "xsystem4.h"
#include "gfx/gfx.h"
#include "modloader.h"
#include "modloader_priv.h"
#define VM_PRIVATE
#include "vm.h"

extern const char rq_config_lua[];
extern const unsigned rq_config_lua_len;

/* The tab: button 100009 after Other 2 (12 + 120 * 7). No key: F1-F7 are the
 * other tabs and mods already use F8 and up (0 is none to T_Button). */
#define TAB_ID 100009
#define TAB_X 852
#define TAB_Y 40
#define TAB_KEY 0

static int g_f_tab = -1;
static int g_tab_cg[3];
static int g_mods_page;

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

static void emit0(struct buffer *out, enum opcode op)
{
	buffer_write_int16(out, (uint16_t)op);
}

static void emit1(struct buffer *out, enum opcode op, int32_t arg)
{
	buffer_write_int16(out, (uint16_t)op);
	buffer_write_int32(out, (uint32_t)arg);
}

/* コンフィグ共通_作成(ref T_ButtonGroup) runs as before, then CALLSYS adds the tab. */
void rq_config_init(struct ain *a)
{
	int make, dup;
	struct buffer out;

	if (!a)
		return;
	make = find_fn(a, "コンフィグ共通_作成");
	g_f_tab = find_fn(a, "コンフィグ共通_タブ作成");
	if (make < 0 || g_f_tab < 0 || a->functions[make].nr_args != 1) {
		rq_log("config: no Config tabs on this AIN (rq.config pages stay hidden)");
		g_f_tab = -1;
		return;
	}
	dup = ain_dup_function(a, make);
	buffer_init(&out, NULL, 0);
	emit1(&out, FUNC, make);
	emit1(&out, SH_LOCALREF, 0);
	emit1(&out, CALLFUNC, dup);
	emit1(&out, CALLSYS, VM_XSYS_CONFIG_TAB);
	emit0(&out, RETURN);
	emit1(&out, ENDFUNC, make);
	a->functions[make].address = (uint32_t)(a->code_size + 6);
	a->code = xrealloc(a->code, a->code_size + out.index);
	memcpy(a->code + a->code_size, out.buf, out.index);
	a->code_size += out.index;
	free(out.buf);
}

int rq_config_mods_page(void)
{
	return g_mods_page;
}

int rq_config_syscall(int code)
{
	int group;

	if (code != VM_XSYS_CONFIG_TAB)
		return 0;
	/* The original's bool stays on the stack as the wrapper's result. */
	if (g_f_tab < 0 || g_tab_cg[0] <= 0)
		return 1;
	group = local_get(0).i;
	stack_push(group);
	stack_push(TAB_ID);
	stack_push(TAB_X);
	stack_push(TAB_Y);
	stack_push(g_tab_cg[0]);
	stack_push(g_tab_cg[1]);
	stack_push(g_tab_cg[2]);
	stack_push(TAB_KEY);
	vm_call(g_f_tab, -1);
	if (!stack_pop().i)
		rq_log("config: the Mods tab was not made (CG %d)", g_tab_cg[0]);
	return 1;
}

/* tab(normal, hover, current): the tab's CGs, 0 for no tab. */
static int l_tab(lua_State *L)
{
	g_tab_cg[0] = (int)luaL_optinteger(L, 1, 0);
	g_tab_cg[1] = (int)luaL_optinteger(L, 2, g_tab_cg[0]);
	g_tab_cg[2] = (int)luaL_optinteger(L, 3, g_tab_cg[0]);
	lua_pushboolean(L, g_f_tab >= 0);
	return 1;
}

/* mods(on): Other 2 is being made as the Mods page. */
static int l_mods(lua_State *L)
{
	g_mods_page = lua_toboolean(L, 1);
	return 0;
}

/* filter([name]): the window's magnification filter by name ("standard", "bicubic", "nearest",
 * "lanczos", "xbrz"), set first when a name is given, and saved as the title bar's menu saves it. */
static int l_filter(lua_State *L)
{
	if (!lua_isnoneornil(L, 1)) {
		const char *name = luaL_checkstring(L, 1);
		int t = gfx_scaling_from_name(name);
		if (t < 0)
			return luaL_error(L, "no filter named %s", name);
		if (t != rq_get_scaling())
			rq_set_scaling(t);
	}
	lua_pushstring(L, gfx_scaling_name(rq_get_scaling()));
	return 1;
}

/* Runs config.lua with rq, the calls above and as_mod (modloader_roster.c); sets rq.config. */
void rq_config_open(lua_State *L, int rq_index)
{
	if (luaL_loadbuffer(L, rq_config_lua, rq_config_lua_len, "=config") != LUA_OK) {
		rq_log("config: %s", lua_tostring(L, -1));
		lua_pop(L, 1);
		return;
	}
	lua_pushvalue(L, rq_index);
	lua_pushcfunction(L, l_tab);
	lua_pushcfunction(L, l_mods);
	lua_pushcfunction(L, rq_lua_as_mod);
	lua_pushcfunction(L, l_filter);
	if (lua_pcall(L, 5, 1, 0) != LUA_OK) {
		rq_log("config: %s", lua_tostring(L, -1));
		lua_pop(L, 1);
		return;
	}
	lua_setfield(L, rq_index, "config");
}

#endif /* RQ_MODLOADER */
