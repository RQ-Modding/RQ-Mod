/* Setting up a situation without raw game names: rq.party, rq.battle.start, rq.random. */

#ifdef RQ_MODLOADER

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

/* Enemy numbers live in a long list of ifs in Ｃ敵登録, with no table to look in. So load
 * this one on enemy slot 1, as a fight would, and see if it got a name. Slot 1 is only
 * used during a battle. */
static int monster_exists(int no)
{
	int slot = 1;
	int args[2] = { slot, no };
	int named;

	call_ints("Ｃ初期化", &slot, 1);
	call_ints("Ｃ敵登録", args, 2);
	named = rq_query_chara_named(slot);
	call_ints("Ｃ初期化", &slot, 1);
	return named;
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

/* Blocks until the fight and the screens after it end, like the commands of rq.scene. */
int rq_lua_battle_start(lua_State *L)
{
	struct page *pta;
	int args[RANKS + 1];
	int i, has_enemy = 0, has_ally = 0, re;

	luaL_checktype(L, 1, LUA_TTABLE);
	lua_getfield(L, 1, "enemies");
	luaL_checktype(L, -1, LUA_TTABLE);
	read_ranks(L, lua_gettop(L), args + 1, "rq.battle.start");
	lua_pop(L, 1);
	lua_getfield(L, 1, "exp");
	args[0] = (int)luaL_optinteger(L, -1, 0);
	lua_pop(L, 1);
	if (in_battle())
		return luaL_error(L, "rq.battle.start: a battle is already running");
	for (i = 1; i <= RANKS; i++) {
		if (!args[i])
			continue;
		if (args[i] < 0 || !monster_exists(args[i]))
			return luaL_error(L, "rq.battle.start: no monster number %d", args[i]);
		has_enemy = 1;
	}
	if (!has_enemy)
		return luaL_error(L, "rq.battle.start: no enemies");
	pta = party_array();
	for (i = 1; pta && i <= RANKS && i < pta->nr_vars; i++) {
		if (pta->values[i].i)
			has_ally = 1;
	}
	/* An empty party never gets a turn and the fight never ends. */
	if (!has_ally)
		return luaL_error(L, "rq.battle.start: the party is empty");
	g_in_start = 1;
	re = call_ints("特定戦闘処理", args, RANKS + 1);
	g_in_start = 0;
	lua_pushstring(L, re == 1 ? "won" : re == 2 ? "lost" : "other");
	return 1;
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
