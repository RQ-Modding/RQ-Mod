/* Copyright (C) 2026 Rance Quest modding fork
 *
 * rq.uikit: game-styled widgets, written in Lua (src/lua/uikit.lua) on rq.hud,
 * and the hub's widgets (src/lua/uikit_hub.lua, loader 2.6.0) on top of it.
 * The Lua files are built into the exe by gen_lua_embed.py.
 */

#ifdef RQ_MODLOADER

#include "lua.h"
#include "lauxlib.h"

#include "modloader.h"
#include "modloader_priv.h"

extern const char rq_uikit_lua[];
extern const unsigned rq_uikit_lua_len;
extern const char rq_uikit_hub_lua[];
extern const unsigned rq_uikit_hub_lua_len;
extern const char rq_uikit_vault_lua[];
extern const unsigned rq_uikit_vault_lua_len;

/* Stack: ui, kit. Runs one part of the kit with rq, ui and kit; leaves the stack as it was. */
static void run_part(lua_State *L, int rq_index, const char *buf, unsigned len, const char *name)
{
	if (luaL_loadbuffer(L, buf, len, name) != LUA_OK) {
		rq_log("uikit: %s", lua_tostring(L, -1));
		lua_pop(L, 1);
		return;
	}
	lua_pushvalue(L, rq_index);
	lua_pushvalue(L, -4);
	lua_pushvalue(L, -4);
	if (lua_pcall(L, 3, 0, 0) != LUA_OK) {
		rq_log("uikit: %s", lua_tostring(L, -1));
		lua_pop(L, 1);
	}
}

/* Runs uikit.lua with rq, then uikit_hub.lua (2.6.0) and uikit_vault.lua (2.8.0) with rq, the
 * kit and its internals, and sets the kit as rq.uikit. */
void rq_uikit_open(lua_State *L, int rq_index)
{
	if (luaL_loadbuffer(L, rq_uikit_lua, rq_uikit_lua_len, "=uikit") != LUA_OK) {
		rq_log("uikit: %s", lua_tostring(L, -1));
		lua_pop(L, 1);
		return;
	}
	lua_pushvalue(L, rq_index);
	if (lua_pcall(L, 1, 2, 0) != LUA_OK) {
		rq_log("uikit: %s", lua_tostring(L, -1));
		lua_pop(L, 1);
		return;
	}
	/* stack: ui, kit */
	run_part(L, rq_index, rq_uikit_hub_lua, rq_uikit_hub_lua_len, "=uikit_hub");
	run_part(L, rq_index, rq_uikit_vault_lua, rq_uikit_vault_lua_len, "=uikit_vault");
	lua_pop(L, 1);
	lua_setfield(L, rq_index, "uikit");
}

#endif /* RQ_MODLOADER */
