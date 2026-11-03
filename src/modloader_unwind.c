/* The title with Lua under it (loader 1.13.0).
 *
 * The title is system.Reset, which is vm_reset: it frees the VM and jumps back to the start of
 * vm_execute_ain, over every C frame between, Lua's too. A mod scene waits in a game call, so
 * a player who goes to the title from Config during one left Lua with frames it never closed:
 * the scene's coroutine never ended, and each title cost a level of Lua's C call budget.
 *
 * So the loader keeps a stack of the crossings between Lua and the game: an entry each time C
 * calls into Lua (lua_pcall, which modloader.h sends here, and coroutine.resume), and one each
 * time Lua calls into the game (vm_call). When vm_reset runs with Lua under it, it jumps to the
 * innermost call into the game instead, and that call raises a Lua error, so Lua closes its
 * own frames. When the error comes back to C at the matching call into Lua, the loader goes on
 * outwards, and with no Lua left on the stack vm_reset does the real reset.
 */

#ifdef RQ_MODLOADER

#include <setjmp.h>
#include <string.h>

#include "lua.h"
#include "lauxlib.h"

#include "modloader.h"
#include "vm.h"

#define CROSS_MAX 128

enum { CROSS_LUA, CROSS_VM };

struct crossing {
	int kind;
	lua_State *L;
	jmp_buf jb;
};

static struct crossing g_cross[CROSS_MAX];
static int g_ncross;
static int g_resetting;
static int g_coresume = LUA_NOREF;
static int g_cocreate = LUA_NOREF;

const char *const rq_title_error = "rq: the game went to the title";

bool rq_resetting(void)
{
	return g_resetting;
}

static int push(int kind, lua_State *L)
{
	if (g_ncross >= CROSS_MAX)
		return -1;
	g_cross[g_ncross].kind = kind;
	g_cross[g_ncross].L = L;
	return g_ncross++;
}

/* Back to the depth before an entry, also over entries that a Lua error jumped past. */
static void pop_to(int n)
{
	if (n >= 0 && n < g_ncross)
		g_ncross = n;
}

int rq_lua_pcall(lua_State *L, int nargs, int nresults, int msgh)
{
	int at = push(CROSS_LUA, L);
	int r = lua_pcallk(L, nargs, nresults, msgh, 0, NULL);
	pop_to(at);
	/* Lua under this one has to close too, before the game goes on. */
	if (g_resetting)
		vm_reset();
	return r;
}

int rq_cross_vm_enter(void)
{
	struct crossing *top = g_ncross ? &g_cross[g_ncross - 1] : NULL;
	/* A call from the game into the game, under Lua's call, is one crossing. */
	if (!top || top->kind != CROSS_LUA)
		return -1;
	if (g_resetting)
		luaL_error(top->L, "%s", rq_title_error);
	return push(CROSS_VM, top->L);
}

jmp_buf *rq_cross_jb(int at)
{
	return &g_cross[at].jb;
}

void rq_cross_vm_leave(int at)
{
	pop_to(at);
}

/* vm_reset jumped back into the vm_call at the top of the stack: raise the error in Lua. */
_Noreturn void rq_cross_raise(void)
{
	lua_State *L = g_cross[g_ncross - 1].L;
	g_ncross--;
	luaL_error(L, "%s", rq_title_error);
	for (;;)
		;
}

void rq_cross_reset(void)
{
	struct crossing *top;

	if (!g_ncross) {
		lua_State *L = rq_lua();
		if (g_resetting)
			rq_log("Lua closed its calls before the title");
		g_resetting = 0;
		/* No Lua runs now: what the C code under it had on the stack is left over. */
		if (L)
			lua_settop(L, 0);
		rq_scene_after_reset();
		rq_hud_after_reset();
		rq_events_after_reset();
		return;
	}
	if (!g_resetting)
		rq_log("The title with Lua under it: closing Lua's calls first");
	g_resetting = 1;
	top = &g_cross[g_ncross - 1];
	if (top->kind == CROSS_LUA)
		luaL_error(top->L, "%s", rq_title_error);
	longjmp(top->jb, 1);
}

/* ---- coroutine.resume and coroutine.wrap ----
 * A coroutine is a Lua thread of its own, and a call into the game from it raises its error
 * in that thread. So resume is a crossing too, with the coroutine as its thread. */

static int resume_in(lua_State *L, int ref, int base, int narg)
{
	int at, i, n;

	if (g_resetting)
		return luaL_error(L, "%s", rq_title_error);
	at = push(CROSS_LUA, lua_tothread(L, base));
	lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
	for (i = 0; i < narg; i++)
		lua_pushvalue(L, base + i);
	n = lua_gettop(L) - narg - 1;
	lua_call(L, narg, LUA_MULTRET);
	pop_to(at);
	if (g_resetting)
		return luaL_error(L, "%s", rq_title_error);
	return lua_gettop(L) - n;
}

static int co_resume(lua_State *L)
{
	luaL_checktype(L, 1, LUA_TTHREAD);
	return resume_in(L, g_coresume, 1, lua_gettop(L));
}

/* As Lua's own wrap: the results without the true, and an error goes on to the caller. */
static int co_wrapped(lua_State *L)
{
	int n, narg = lua_gettop(L);

	lua_pushvalue(L, lua_upvalueindex(1));
	lua_insert(L, 1);
	n = resume_in(L, g_coresume, 1, narg + 1);
	if (lua_toboolean(L, -n))
		return n - 1;
	{
		lua_State *co = lua_tothread(L, 1);
		int status = lua_status(co);
		if (status != LUA_OK && status != LUA_YIELD) {
			lua_closethread(co, L);
			lua_xmove(co, L, 1);
		}
		if (lua_type(L, -1) == LUA_TSTRING) {
			luaL_where(L, 1);
			lua_insert(L, -2);
			lua_concat(L, 2);
		}
		return lua_error(L);
	}
}

static int co_wrap(lua_State *L)
{
	luaL_checktype(L, 1, LUA_TFUNCTION);
	lua_rawgeti(L, LUA_REGISTRYINDEX, g_cocreate);
	lua_pushvalue(L, 1);
	lua_call(L, 1, 1);
	lua_pushcclosure(L, co_wrapped, 1);
	return 1;
}

void rq_unwind_install(lua_State *L)
{
	lua_getglobal(L, "coroutine");
	lua_getfield(L, -1, "resume");
	g_coresume = luaL_ref(L, LUA_REGISTRYINDEX);
	lua_getfield(L, -1, "create");
	g_cocreate = luaL_ref(L, LUA_REGISTRYINDEX);
	lua_pushcfunction(L, co_resume);
	lua_setfield(L, -2, "resume");
	lua_pushcfunction(L, co_wrap);
	lua_setfield(L, -2, "wrap");
	lua_pop(L, 1);
}

#endif /* RQ_MODLOADER */
