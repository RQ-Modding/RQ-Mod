/* Copyright (C) 2026 Rance Quest modding fork
 *
 * rq.battle.on_start / on_end. Installed the first time a mod registers one.
 *   - tail of Ｔ戦闘前初期化: every start-of-battle effect of the base game has
 *     run (per-character skills, ambush, poison tea), PTA/PTB are filled.
 *   - head of 戦闘後処理(re): the battle result is known, the cleanup has not run.
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
#include "system4/utfsjis.h"

#include "modloader.h"
#include "modloader_priv.h"
#include "vm.h"
#include "vm/heap.h"
#include "vm/page.h"

#define CB_MAX 16

static int g_on;
static int g_start_cb[CB_MAX], g_nstart;
static int g_end_cb[CB_MAX], g_nend;
static int gi_pta = -2, gi_ptb = -2;

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

static int find_gi(const char *ascii)
{
	return ain_get_global(ain, ascii);
}

/* Call the original (one int arg), then CALLSYS. */
static void wrap_tail1(struct ain *a, int fno, int sys)
{
	int dup = ain_dup_function(a, fno);
	struct buffer out;

	buffer_init(&out, NULL, 0);
	emit1(&out, FUNC, fno);
	emit1(&out, SH_LOCALREF, 0);
	emit1(&out, CALLFUNC, dup);
	emit1(&out, CALLSYS, sys);
	emit0(&out, RETURN);
	emit1(&out, ENDFUNC, fno);
	commit_wrap(a, fno, &out);
}

/* CALLSYS first (pops the arg and pushes it back), then the original. */
static void wrap_head1(struct ain *a, int fno, int sys)
{
	int dup = ain_dup_function(a, fno);
	struct buffer out;

	buffer_init(&out, NULL, 0);
	emit1(&out, FUNC, fno);
	emit1(&out, SH_LOCALREF, 0);
	emit1(&out, CALLSYS, sys);
	emit1(&out, CALLFUNC, dup);
	emit0(&out, RETURN);
	emit1(&out, ENDFUNC, fno);
	commit_wrap(a, fno, &out);
}

int rq_events_install(struct ain *a)
{
	int f_start, f_end;

	if (g_on)
		return 1;
	if (!a)
		return 0;
	f_start = find_fn(a, "Ｔ戦闘前初期化");
	f_end = find_fn(a, "戦闘後処理");
	gi_pta = find_gi("PTA");
	gi_ptb = find_gi("PTB");
	if (f_start < 0 || f_end < 0 || gi_pta < 0 || gi_ptb < 0) {
		rq_log("events: missing AIN symbol (start=%d end=%d PTA=%d PTB=%d)", f_start, f_end, gi_pta, gi_ptb);
		return 0;
	}
	wrap_tail1(a, f_start, VM_XSYS_BATTLE_START);
	wrap_head1(a, f_end, VM_XSYS_BATTLE_END);
	g_on = 1;
	rq_log("events: Ｔ戦闘前初期化 tail -> on_start, 戦闘後処理 head -> on_end");
	return 1;
}

static void push_party(lua_State *L, int gi)
{
	struct page *arr = NULL;
	int slot, i, n = 0;

	lua_newtable(L);
	if (!heap || !heap_size || !heap[0].page)
		return;
	slot = global_get(gi).i;
	if (page_index_valid(slot))
		arr = heap_get_page(slot);
	if (!arr || arr->type != ARRAY_PAGE)
		return;
	for (i = 1; i < arr->nr_vars && i < 6; i++) {
		if (arr->values[i].i > 0) {
			lua_pushinteger(L, arr->values[i].i);
			lua_rawseti(L, -2, ++n);
		}
	}
}

static void run_cbs(const int *refs, int n, int boss, int result, int is_start)
{
	lua_State *L = rq_lua();
	int i;

	if (!L)
		return;
	for (i = 0; i < n; i++) {
		lua_rawgeti(L, LUA_REGISTRYINDEX, refs[i]);
		lua_newtable(L);
		if (is_start) {
			lua_pushboolean(L, boss == 1);
			lua_setfield(L, -2, "boss");
		} else {
			lua_pushinteger(L, result);
			lua_setfield(L, -2, "result");
		}
		push_party(L, gi_pta);
		lua_setfield(L, -2, "allies");
		push_party(L, gi_ptb);
		lua_setfield(L, -2, "enemies");
		if (lua_pcall(L, 1, 0, 0) != LUA_OK) {
			rq_log("battle %s error: %s", is_start ? "on_start" : "on_end", lua_tostring(L, -1));
			lua_pop(L, 1);
		}
	}
}

int rq_events_syscall(int code)
{
	switch ((enum vm_extra_syscall)code) {
	case VM_XSYS_BATTLE_START:
		run_cbs(g_start_cb, g_nstart, local_get(0).i, 0, 1);
		return 1;
	case VM_XSYS_BATTLE_END: {
		int re = stack_pop().i;
		run_cbs(g_end_cb, g_nend, 0, re, 0);
		stack_push(re);
		return 1;
	}
	default:
		return 0;
	}
}

static int add_cb(lua_State *L, int *refs, int *n)
{
	luaL_checktype(L, 1, LUA_TFUNCTION);
	if (!ain || !rq_events_install(ain))
		return luaL_error(L, "battle events install failed (see mods/rqmod.log)");
	if (*n >= CB_MAX)
		return luaL_error(L, "too many battle callbacks registered");
	lua_pushvalue(L, 1);
	refs[(*n)++] = luaL_ref(L, LUA_REGISTRYINDEX);
	return 0;
}

int rq_lua_battle_on_start(lua_State *L)
{
	return add_cb(L, g_start_cb, &g_nstart);
}

int rq_lua_battle_on_end(lua_State *L)
{
	return add_cb(L, g_end_cb, &g_nend);
}

#endif /* RQ_MODLOADER */
