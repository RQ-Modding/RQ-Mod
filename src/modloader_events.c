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

/* ---- quests: on_clear and quest state ---- */

static int g_clear_on;
static int g_clear_cb[CB_MAX], g_nclear;
static int gi_iiq = -1, gi_quest = -1;
static int f_qstate = -1, f_qclears = -1;

static int find_gi_utf(const char *utf8)
{
	char *sjis = utf2sjis(utf8, 0);
	int gi;
	if (!sjis)
		return -1;
	gi = ain_get_global(ain, sjis);
	free(sjis);
	return gi;
}

static void wrap_tail0(struct ain *a, int fno, int sys)
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
}

static int quest_syms(struct ain *a)
{
	if (f_qstate >= 0)
		return 1;
	f_qstate = find_fn(a, "Ｑ状態");
	f_qclears = find_fn(a, "Ｑクリア回数");
	gi_iiq = find_gi_utf("◆iiq");
	gi_quest = find_gi_utf("Ｑ");
	if (f_qstate < 0 || f_qclears < 0 || gi_iiq < 0 || gi_quest < 0) {
		rq_log("events: quest symbols missing (state=%d clears=%d iiq=%d Q=%d)", f_qstate, f_qclears, gi_iiq, gi_quest);
		f_qstate = -1;
		return 0;
	}
	return 1;
}

static int qcall(int fno, int a, int b)
{
	stack_push(a);
	stack_push(b);
	vm_call(fno, -1);
	if (ain->functions[fno].return_type.data == AIN_VOID)
		return 0;
	return stack_pop().i;
}

static int quest_valid(int no)
{
	int slot;
	struct page *arr;

	if (!heap || !heap_size || !heap[0].page)
		return 0;
	slot = global_get(gi_quest).i;
	if (!page_index_valid(slot))
		return 0;
	arr = heap_get_page(slot);
	return arr && arr->type == ARRAY_PAGE && no > 0 && no < arr->nr_vars;
}

static int install_clear(struct ain *a)
{
	int f;

	if (g_clear_on)
		return 1;
	if (!a || !quest_syms(a))
		return 0;
	f = find_fn(a, "クエスト終了処理／クリアフラグ");
	if (f < 0) {
		rq_log("events: クエスト終了処理／クリアフラグ not found");
		return 0;
	}
	wrap_tail0(a, f, VM_XSYS_QUEST_CLEAR);
	g_clear_on = 1;
	rq_log("events: クエスト終了処理／クリアフラグ tail -> on_clear");
	return 1;
}

static void run_clear(void)
{
	lua_State *L = rq_lua();
	int iiq = global_get(gi_iiq).i;
	int clears = qcall(f_qclears, iiq, -1);
	int i;

	if (!L)
		return;
	for (i = 0; i < g_nclear; i++) {
		lua_rawgeti(L, LUA_REGISTRYINDEX, g_clear_cb[i]);
		lua_newtable(L);
		lua_pushinteger(L, iiq);
		lua_setfield(L, -2, "quest");
		lua_pushinteger(L, clears);
		lua_setfield(L, -2, "clears");
		lua_pushboolean(L, clears <= 1);
		lua_setfield(L, -2, "first");
		if (lua_pcall(L, 1, 0, 0) != LUA_OK) {
			rq_log("quests on_clear error: %s", lua_tostring(L, -1));
			lua_pop(L, 1);
		}
	}
}

int rq_events_syscall(int code)
{
	switch ((enum vm_extra_syscall)code) {
	case VM_XSYS_QUEST_CLEAR:
		run_clear();
		return 1;
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

int rq_lua_quests_on_clear(lua_State *L)
{
	luaL_checktype(L, 1, LUA_TFUNCTION);
	if (!ain || !install_clear(ain))
		return luaL_error(L, "quest events install failed (see mods/rqmod.log)");
	if (g_nclear >= CB_MAX)
		return luaL_error(L, "too many quest callbacks registered");
	lua_pushvalue(L, 1);
	g_clear_cb[g_nclear++] = luaL_ref(L, LUA_REGISTRYINDEX);
	return 0;
}

static int quest_arg(lua_State *L)
{
	int no = (int)luaL_checkinteger(L, 1);
	if (!ain || !quest_syms(ain))
		return luaL_error(L, "quest symbols missing (see mods/rqmod.log)");
	if (!quest_valid(no))
		return luaL_error(L, "no quest %d", no);
	return no;
}

int rq_lua_quests_state(lua_State *L)
{
	int no = quest_arg(L);
	lua_pushinteger(L, qcall(f_qstate, no, -1));
	return 1;
}

/* Ｑ状態(no, state): 0 locked, 1 available, 2 cleared. State 2 also records a first clear. */
int rq_lua_quests_set_state(lua_State *L)
{
	int no = quest_arg(L);
	int st = (int)luaL_checkinteger(L, 2);

	if (st < 0 || st > 2)
		return luaL_error(L, "quest state must be 0, 1 or 2");
	qcall(f_qstate, no, st);
	return 0;
}

static void push_global_list(lua_State *L, const char *name)
{
	int gi = find_gi_utf(name), slot, i, n = 0;
	struct page *arr = NULL;

	lua_newtable(L);
	if (gi < 0 || !heap || !heap_size || !heap[0].page)
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

/* What the victory screen is going to show: { characters = {iic}, quests = {no} }.
 * Ｃ生成 and Ｑ状態(no, 1) add to these lists; the game empties them when a quest ends. */
int rq_lua_quests_new_this_run(lua_State *L)
{
	lua_newtable(L);
	push_global_list(L, "◆ＮＥＷキャラクター");
	lua_setfield(L, -2, "characters");
	push_global_list(L, "◆ＮＥＷクエスト");
	lua_setfield(L, -2, "quests");
	return 1;
}

/* Test only (needs env RQ_DEBUG_CLEAR): run the game's clean-clear processing for quest no. */
int rq_lua_quests_debug_clear(lua_State *L)
{
	int no = quest_arg(L);
	int f = find_fn(ain, "クエスト終了処理");
	union vm_value v;

	if (!getenv("RQ_DEBUG_CLEAR"))
		return luaL_error(L, "debug_clear is off");
	if (f < 0)
		return luaL_error(L, "クエスト終了処理 not found");
	v.i = no;
	global_set(gi_iiq, v, false);
	stack_push(1);
	vm_call(f, -1);
	return 0;
}

/* Ｑ状態(no, 1): only works on a locked quest. On the victory screen it joins the "new quests" list. */
int rq_lua_quests_unlock(lua_State *L)
{
	int no = quest_arg(L);
	int before = qcall(f_qstate, no, -1);

	if (before == 0)
		qcall(f_qstate, no, 1);
	lua_pushboolean(L, before == 0);
	return 1;
}

#endif /* RQ_MODLOADER */
