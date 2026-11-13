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
extern const char rq_uikit_windows_lua[];
extern const unsigned rq_uikit_windows_lua_len;
extern const char rq_dialog_lua[];
extern const unsigned rq_dialog_lua_len;

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

/* Runs uikit.lua with rq, then uikit_hub.lua (2.6.0), uikit_vault.lua (2.8.0), uikit_windows.lua
 * (2.32.0) and dialog.lua (2.19.0) with rq, the kit and its internals, and sets the kit as rq.uikit. */
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
	/* stack: ui, kit. kit.menu_comment: a choice-menu row's comment as the game shows it (2.45.1),
	 * for rq.uikit.options and rq.ui.ask. */
	lua_pushcfunction(L, rq_lua_menu_comment);
	lua_setfield(L, -2, "menu_comment");
	run_part(L, rq_index, rq_uikit_hub_lua, rq_uikit_hub_lua_len, "=uikit_hub");
	run_part(L, rq_index, rq_uikit_vault_lua, rq_uikit_vault_lua_len, "=uikit_vault");
	/* The game's windows (2.32.0): the Quest Storage window, the quest panel's heading, the
	 * shop's balloon and Trivia's parchment, on the hub's and the vault's parts. */
	run_part(L, rq_index, rq_uikit_windows_lua, rq_uikit_windows_lua_len, "=uikit_windows");
	/* rq.ui.confirm, notice and ask (2.19.0): the game's dialogs, wrapped with the vault's port
	 * of the game's WordWrapText (kit.wrap). kit.under_z sets the menu's z for the HUD's split
	 * and kit.patch_local_int the menu's own (2.28.0: over Config). */
	lua_pushcfunction(L, rq_lua_hud_set_under_z);
	lua_setfield(L, -2, "under_z");
	lua_pushcfunction(L, rq_lua_ain_patch_local_int);
	lua_setfield(L, -2, "patch_local_int");
	run_part(L, rq_index, rq_dialog_lua, rq_dialog_lua_len, "=dialog");
	lua_pop(L, 1);
	lua_setfield(L, rq_index, "uikit");
}

#endif /* RQ_MODLOADER */
