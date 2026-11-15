/* Setting up a situation without raw game names: rq.party, rq.random, and the internal calls of
 * rq.battle.start (lua/basics.lua since 2.36.0). */

#ifdef RQ_MODLOADER

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "lua.h"
#include "lauxlib.h"

#include "system4.h"
#include "system4/ain.h"
#include "system4/utfsjis.h"

#include "modloader.h"
#include "vm.h"
#include "vm/heap.h"
#include "vm/page.h"

#define RANKS 5

static int g_in_start;

/* A game function that takes and returns plain ints. */
static int call_ints(const char *utf, const int *args, int n)
{
	char *sj = utf2sjis(utf, 0);
	int fno = sj ? ain_get_function(ain, sj) : -1;
	int i, ret = 0;

	free(sj);
	if (fno < 0)
		return -1;
	for (i = 0; i < n; i++)
		stack_push((union vm_value) { .i = args[i] });
	vm_call(fno, -1);
	if (ain->functions[fno].return_type.data != AIN_VOID)
		ret = stack_pop().i;
	return ret;
}

static struct page *party_array(void)
{
	int gi = ain_get_global(ain, "PTA");
	int slot;

	if (gi < 0 || !heap || !heap_size || !heap[0].page)
		return NULL;
	slot = global_get(gi).i;
	if (!page_index_valid(slot))
		return NULL;
	return heap_get_page(slot);
}

/* The game restarted inside rq.battle.start, which will never return. */
void rq_state_forget(void)
{
	g_in_start = 0;
}

static int in_battle(void)
{
	struct rq_qbattle b;

	if (g_in_start)
		return 1;
	memset(&b, 0, sizeof b);
	rq_query_battle(&b);
	return b.up;
}

/* Ranks 1 to 5 from a list. nil, false and 0 leave a rank empty. */
static void read_ranks(lua_State *L, int idx, int *out, const char *who)
{
	int i;

	if (lua_rawlen(L, idx) > RANKS)
		luaL_error(L, "%s: at most %d ranks", who, RANKS);
	for (i = 0; i < RANKS; i++) {
		lua_rawgeti(L, idx, i + 1);
		out[i] = lua_toboolean(L, -1) ? (int)luaL_checkinteger(L, -1) : 0;
		lua_pop(L, 1);
	}
}

int rq_lua_party_get(lua_State *L)
{
	struct page *pta = party_array();
	int i;

	lua_createtable(L, RANKS, 0);
	for (i = 1; i <= RANKS; i++) {
		lua_pushinteger(L, pta && i < pta->nr_vars ? pta->values[i].i : 0);
		lua_rawseti(L, -2, i);
	}
	return 1;
}

int rq_lua_party_set(lua_State *L)
{
	int ranks[RANKS];
	int i, k;

	luaL_checktype(L, 1, LUA_TTABLE);
	read_ranks(L, 1, ranks, "rq.party.set");
	if (in_battle())
		return luaL_error(L, "rq.party.set: not during a battle");
	for (i = 0; i < RANKS; i++) {
		if (!ranks[i])
			continue;
		if (!rq_query_unit_recruited(ranks[i]))
			return luaL_error(L, "rq.party.set: character %d is not recruited", ranks[i]);
		for (k = 0; k < i; k++) {
			if (ranks[k] == ranks[i])
				return luaL_error(L, "rq.party.set: character %d is listed twice", ranks[i]);
		}
	}
	call_ints("ＰＴＡ全解除", NULL, 0);
	for (i = 0; i < RANKS; i++) {
		int args[2] = { ranks[i], i + 1 };
		if (ranks[i] && call_ints("ＰＴＡ登録", args, 2) < 0)
			return luaL_error(L, "rq.party.set: the game refused character %d", ranks[i]);
	}
	return 0;
}

/* rq.battle.start runs in lua/basics.lua since 2.36.0, over rq.ain.call. These two are its
 * internal calls, in the loader's private table (never given to a mod): the fight marked as
 * started (rq.party.set refuses during it; a restart inside it forgets it), and whether a fight
 * runs now. */
static int l_fight_mark(lua_State *L)
{
	g_in_start = lua_toboolean(L, 1);
	return 0;
}

static int l_in_battle(lua_State *L)
{
	lua_pushboolean(L, in_battle());
	return 1;
}

/* Whether the code runs in an rq.on_frame callback itself, not in a game function it called
 * (modloader_hud.c). rq.battle.reveal_weakness waits for the next turn then (2.46.0). */
static int l_frame_direct(lua_State *L)
{
	lua_pushboolean(L, rq_frame_direct());
	return 1;
}

/* The model table (hll/MonsterInfo.c) for rq.monster.models and model (lua/monster.lua, 2.40.0).
 * models_count() -> count, file count (nil when the table cannot be read). */
static int l_models_count(lua_State *L)
{
	int file = 0, n = rq_models_count(&file);
	if (n < 0)
		return 0;
	lua_pushinteger(L, n);
	lua_pushinteger(L, file);
	return 2;
}

/* model_info(i) -> name, poly, scale, x, y, z, motions; nothing for no entry. */
static int l_model_info(lua_State *L)
{
	char *name = NULL, *poly = NULL;
	float scale, x, y, z;
	int motions;

	if (!rq_model_info((int)luaL_checkinteger(L, 1), &name, &poly, &scale, &x, &y, &z, &motions))
		return 0;
	lua_pushstring(L, name);
	lua_pushstring(L, poly);
	lua_pushnumber(L, scale);
	lua_pushnumber(L, x);
	lua_pushnumber(L, y);
	lua_pushnumber(L, z);
	lua_pushinteger(L, motions);
	free(name);
	free(poly);
	return 7;
}

/* model_derive(slot, base, scale, x, y, z) -> index, or nil. slot -1 adds one. */
static int l_model_derive(lua_State *L)
{
	int at = rq_model_derive((int)luaL_checkinteger(L, 1), (int)luaL_checkinteger(L, 2),
		(float)luaL_checknumber(L, 3), (float)luaL_checknumber(L, 4), (float)luaL_checknumber(L, 5),
		(float)luaL_checknumber(L, 6));
	if (at < 0)
		return 0;
	lua_pushinteger(L, at);
	return 1;
}

void rq_state_private(lua_State *L, int priv)
{
	lua_pushcfunction(L, l_fight_mark);
	lua_setfield(L, priv, "fight_mark");
	lua_pushcfunction(L, l_in_battle);
	lua_setfield(L, priv, "in_battle");
	lua_pushcfunction(L, l_frame_direct);
	lua_setfield(L, priv, "frame_direct");
	lua_pushcfunction(L, rq_lua_battle_on_start_first);
	lua_setfield(L, priv, "on_start_first");
	lua_pushcfunction(L, rq_lua_battle_on_end_first);
	lua_setfield(L, priv, "on_end_first");
	/* 2.40.0: the stores and rq.savedata under a key of the loader's own (lua/monster.lua). */
	lua_pushcfunction(L, rq_lua_as_mod);
	lua_setfield(L, priv, "as_mod");
	lua_pushcfunction(L, l_models_count);
	lua_setfield(L, priv, "models_count");
	lua_pushcfunction(L, l_model_info);
	lua_setfield(L, priv, "model_info");
	lua_pushcfunction(L, l_model_derive);
	lua_setfield(L, priv, "model_derive");
	/* 2.45.0: trying the game's code on a character, then putting it back (lua/skills.lua). */
	lua_pushcfunction(L, rq_lua_keep);
	lua_setfield(L, priv, "keep");
	lua_pushcfunction(L, rq_lua_forget_quiet);
	lua_setfield(L, priv, "forget_quiet");
	/* 2.54.0: the skill rows' numbers in the game's code (lua/knownskills.lua). */
	lua_pushcfunction(L, rq_lua_ain_patch_imm);
	lua_setfield(L, priv, "patch_imm");
}

int rq_lua_random_seed(lua_State *L)
{
	rq_random_seed((int)luaL_checkinteger(L, 1));
	return 0;
}

int rq_lua_random_fix(lua_State *L)
{
	int v;

	if (lua_isnoneornil(L, 1)) {
		rq_random_fix(-1);
		return 0;
	}
	v = (int)luaL_checkinteger(L, 1);
	if (v < 0)
		return luaL_error(L, "rq.random.fix: the value cannot be negative");
	rq_random_fix(v);
	return 0;
}

#endif /* RQ_MODLOADER */
