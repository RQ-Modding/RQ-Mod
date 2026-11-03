/* Copyright (C) 2026 Rance Quest modding fork
 *
 * rq.uikit: game-styled widgets, written in Lua (src/lua/uikit.lua) on rq.hud.
 * The Lua file is built into the exe by gen_lua_embed.py.
 */

#ifdef RQ_MODLOADER

#include "lua.h"
#include "lauxlib.h"

#include "modloader.h"
#include "modloader_priv.h"

extern const char rq_uikit_lua[];
extern const unsigned rq_uikit_lua_len;

/* Runs uikit.lua with rq and sets what it returns as rq.uikit. */
void rq_uikit_open(lua_State *L, int rq_index)
{
	if (luaL_loadbuffer(L, rq_uikit_lua, rq_uikit_lua_len, "=uikit") != LUA_OK) {
		rq_log("uikit: %s", lua_tostring(L, -1));
		lua_pop(L, 1);
		return;
	}
	lua_pushvalue(L, rq_index);
	if (lua_pcall(L, 1, 1, 0) != LUA_OK) {
		rq_log("uikit: %s", lua_tostring(L, -1));
		lua_pop(L, 1);
		return;
	}
	lua_setfield(L, rq_index, "uikit");
}

#endif /* RQ_MODLOADER */
