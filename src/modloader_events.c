/* Copyright (C) 2026 Rance Quest modding fork
 *
 * rq.battle.on_start / on_end / on_down / on_support. Installed the first
 * time a mod registers one.
 *   - tail of Ｔ戦闘前初期化: every start-of-battle effect of the base game has
 *     run (per-character skills, ambush, poison tea), PTA/PTB are filled.
 *   - head of 戦闘後処理(re): the battle result is known, the cleanup has not run.
 *   - head of Ｃ状態変化(iic, state): on_down fires when the game sets state 3.
 *   - head of Ｔ補助行動(user, target, skill): on_support, before its effects.
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
static int g_down_cb[CB_MAX], g_ndown;
static int g_down_on;
static int g_sup_cb[CB_MAX], g_nsup;
static int g_sup_on;
static int gi_pta = -2, gi_ptb = -2;
static int gint(int *gi, const char *name);
static int find_gi_utf(const char *utf8);

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

/* Both args are pushed, CALLSYS reads them and puts them back, then the original runs. */
static void wrap_head2(struct ain *a, int fno, int sys)
{
	int dup = ain_dup_function(a, fno);
	struct buffer out;

	buffer_init(&out, NULL, 0);
	emit1(&out, FUNC, fno);
	emit1(&out, SH_LOCALREF, 0);
	emit1(&out, SH_LOCALREF, 1);
	emit1(&out, CALLSYS, sys);
	emit1(&out, CALLFUNC, dup);
	emit0(&out, RETURN);
	emit1(&out, ENDFUNC, fno);
	commit_wrap(a, fno, &out);
}

/* Three args: the same idea as wrap_head2. */
static void wrap_head3(struct ain *a, int fno, int sys)
{
	int dup = ain_dup_function(a, fno);
	struct buffer out;

	buffer_init(&out, NULL, 0);
	emit1(&out, FUNC, fno);
	emit1(&out, SH_LOCALREF, 0);
	emit1(&out, SH_LOCALREF, 1);
	emit1(&out, SH_LOCALREF, 2);
	emit1(&out, CALLSYS, sys);
	emit1(&out, CALLFUNC, dup);
	emit0(&out, RETURN);
	emit1(&out, ENDFUNC, fno);
	commit_wrap(a, fno, &out);
}

static int install_support(struct ain *a)
{
	int f;

	if (g_sup_on)
		return 1;
	if (!a)
		return 0;
	f = find_fn(a, "Ｔ補助行動");
	if (f < 0 || a->functions[f].nr_args != 3) {
		rq_log("events: Ｔ補助行動 missing or wrong arity (%d)", f);
		return 0;
	}
	wrap_head3(a, f, VM_XSYS_BATTLE_SUPPORT);
	g_sup_on = 1;
	rq_log("events: Ｔ補助行動 head -> on_support");
	return 1;
}

static void run_support(void)
{
	lua_State *L = rq_lua();
	int skill = stack_pop().i;
	int target = stack_pop().i;
	int user = stack_pop().i;
	int i;

	if (L) {
		rq_crash_crumb("lua:on_support", user, skill);
		for (i = 0; i < g_nsup; i++) {
			lua_rawgeti(L, LUA_REGISTRYINDEX, g_sup_cb[i]);
			lua_newtable(L);
			lua_pushinteger(L, user);
			lua_setfield(L, -2, "user");
			lua_pushinteger(L, target);
			lua_setfield(L, -2, "target");
			lua_pushinteger(L, skill);
			lua_setfield(L, -2, "skill");
			if (lua_pcall(L, 1, 0, 0) != LUA_OK) {
				rq_log("battle on_support error: %s", lua_tostring(L, -1));
				lua_pop(L, 1);
			}
		}
	}
	stack_push(user);
	stack_push(target);
	stack_push(skill);
}

static int install_down(struct ain *a)
{
	int f;

	if (g_down_on)
		return 1;
	if (!a)
		return 0;
	f = find_fn(a, "Ｃ状態変化");
	if (f < 0 || a->functions[f].nr_args != 2) {
		rq_log("events: Ｃ状態変化 missing or wrong arity (%d)", f);
		return 0;
	}
	wrap_head2(a, f, VM_XSYS_BATTLE_DOWN);
	g_down_on = 1;
	rq_log("events: Ｃ状態変化 head -> on_down");
	return 1;
}

static void run_down(void)
{
	lua_State *L = rq_lua();
	int state = stack_pop().i;
	int iic = stack_pop().i;
	int i;

	if (L && state == 3) {
		rq_crash_crumb("lua:on_down", iic, 0);
		for (i = 0; i < g_ndown; i++) {
			lua_rawgeti(L, LUA_REGISTRYINDEX, g_down_cb[i]);
			lua_newtable(L);
			lua_pushinteger(L, iic);
			lua_setfield(L, -2, "iic");
			if (lua_pcall(L, 1, 0, 0) != LUA_OK) {
				rq_log("battle on_down error: %s", lua_tostring(L, -1));
				lua_pop(L, 1);
			}
		}
	}
	stack_push(iic);
	stack_push(state);
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
	rq_crash_crumb(is_start ? "lua:on_start" : "lua:on_end", n, result);
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
		{
			static int gi_g = -2, gi_r = -2;
			lua_pushinteger(L, gint(&gi_g, "◆戦闘グループ"));
			lua_setfield(L, -2, "group");
			lua_pushinteger(L, gint(&gi_r, "◆戦闘ランク"));
			lua_setfield(L, -2, "rank");
		}
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

/* PTA, the party in the dungeon (iic list). */
static void push_party_now(lua_State *L)
{
	if (gi_pta == -2)
		gi_pta = find_gi("PTA");
	if (gi_pta < 0) {
		lua_newtable(L);
		return;
	}
	push_party(L, gi_pta);
}

/* ---- battle drop ---- */

static int gi_drop = -2;

static int gint(int *gi, const char *name)
{
	if (*gi == -2)
		*gi = find_gi_utf(name);
	if (*gi < 0 || !heap || !heap_size || !heap[0].page)
		return 0;
	return global_get(*gi).i;
}

int rq_lua_battle_get_drop(lua_State *L)
{
	lua_pushinteger(L, gint(&gi_drop, "◆戦闘後アイテム取得"));
	return 1;
}

/* Writes ◆戦闘後アイテム取得, the item number 戦闘後処理 hands out after a won battle (0 = none). */
int rq_lua_battle_set_drop(lua_State *L)
{
	int no = lua_isnil(L, 1) || (lua_isboolean(L, 1) && !lua_toboolean(L, 1)) ? 0 : rq_item_no_from_lua(L, 1);
	union vm_value v;

	gint(&gi_drop, "◆戦闘後アイテム取得");
	if (gi_drop < 0)
		return luaL_error(L, "drop variable not found");
	v.i = no;
	global_set(gi_drop, v, false);
	return 0;
}

/* ---- quest end, every ending ---- */

static int g_qend_on;
static int g_qend_cb[CB_MAX], g_nqend;

static void run_qend(void)
{
	lua_State *L = rq_lua();
	int fg = stack_pop().i, i;

	if (L && quest_syms(ain)) {
		int iiq = global_get(gi_iiq).i;
		for (i = 0; i < g_nqend; i++) {
			lua_rawgeti(L, LUA_REGISTRYINDEX, g_qend_cb[i]);
			lua_newtable(L);
			lua_pushinteger(L, iiq);
			lua_setfield(L, -2, "quest");
			lua_pushinteger(L, fg);
			lua_setfield(L, -2, "result");
			lua_pushboolean(L, fg == 1);
			lua_setfield(L, -2, "cleared");
			push_party_now(L);
			lua_setfield(L, -2, "party");
			if (lua_pcall(L, 1, 0, 0) != LUA_OK) {
				rq_log("quests on_end error: %s", lua_tostring(L, -1));
				lua_pop(L, 1);
			}
		}
	}
	stack_push(fg);
}

int rq_lua_quests_on_end(lua_State *L)
{
	luaL_checktype(L, 1, LUA_TFUNCTION);
	if (!ain || !quest_syms(ain))
		return luaL_error(L, "quest symbols missing (see mods/rqmod.log)");
	if (!g_qend_on) {
		int f = find_fn(ain, "クエスト終了処理");
		if (f < 0)
			return luaL_error(L, "クエスト終了処理 not found");
		wrap_head1(ain, f, VM_XSYS_QUEST_END);
		g_qend_on = 1;
		rq_log("events: クエスト終了処理 head -> on_end");
	}
	if (g_nqend >= CB_MAX)
		return luaL_error(L, "too many quest callbacks registered");
	lua_pushvalue(L, 1);
	g_qend_cb[g_nqend++] = luaL_ref(L, LUA_REGISTRYINDEX);
	return 0;
}

/* ---- dungeon map: unlock the camera on a first visit ---- */

static int g_map_on, g_in_map, g_map_unlock;
static int g_map_cb[CB_MAX], g_nmap;

static void map_enter(void)
{
	lua_State *L = rq_lua();
	int i;

	g_in_map = 1;
	g_map_unlock = 0;
	if (!L || !quest_syms(ain))
		return;
	for (i = 0; i < g_nmap; i++) {
		int iiq = global_get(gi_iiq).i;
		lua_rawgeti(L, LUA_REGISTRYINDEX, g_map_cb[i]);
		lua_newtable(L);
		lua_pushinteger(L, iiq);
		lua_setfield(L, -2, "quest");
		lua_pushboolean(L, iiq > 0 && qcall(f_qstate, iiq, -1) == 2);
		lua_setfield(L, -2, "cleared");
		push_party_now(L);
		lua_setfield(L, -2, "party");
		lua_pushinteger(L, local_get(1).i);
		lua_setfield(L, -2, "map");
		if (lua_pcall(L, 1, 1, 0) != LUA_OK) {
			rq_log("dungeon on_map error: %s", lua_tostring(L, -1));
		} else if (lua_toboolean(L, -1)) {
			g_map_unlock = 1;
		}
		lua_pop(L, 1);
	}
}

static void map_cleared(void)
{
	int res = stack_pop().i;
	if (g_in_map && g_map_unlock)
		res = 1;
	stack_push(res);
}

static void wrap_map_create(struct ain *a, int fno)
{
	int dup = ain_dup_function(a, fno);
	int nargs = a->functions[fno].nr_args, i;
	struct buffer out;

	buffer_init(&out, NULL, 0);
	emit1(&out, FUNC, fno);
	emit1(&out, CALLSYS, VM_XSYS_MAP_ENTER);
	emit0(&out, PUSHSTRUCTPAGE);
	for (i = 0; i < nargs; i++)
		emit1(&out, SH_LOCALREF, i);
	emit1(&out, CALLMETHOD, dup);
	emit1(&out, CALLSYS, VM_XSYS_MAP_EXIT);
	emit0(&out, RETURN);
	emit1(&out, ENDFUNC, fno);
	commit_wrap(a, fno, &out);
}

int rq_lua_dungeon_on_map(lua_State *L)
{
	luaL_checktype(L, 1, LUA_TFUNCTION);
	if (!ain || !quest_syms(ain))
		return luaL_error(L, "quest symbols missing (see mods/rqmod.log)");
	if (!g_map_on) {
		int f_create = find_fn(ain, "T_MapParts@create");
		int f_clear = find_fn(ain, "クエスト_クリア済み");
		if (f_create < 0 || f_clear < 0)
			return luaL_error(L, "map functions not found (create=%d clear=%d)", f_create, f_clear);
		wrap_map_create(ain, f_create);
		wrap_tail1(ain, f_clear, VM_XSYS_MAP_CLEARED);
		g_map_on = 1;
		rq_log("events: T_MapParts@create wrapped; クエスト_クリア済み -> on_map");
	}
	if (g_nmap >= CB_MAX)
		return luaL_error(L, "too many map callbacks registered");
	lua_pushvalue(L, 1);
	g_map_cb[g_nmap++] = luaL_ref(L, LUA_REGISTRYINDEX);
	return 0;
}

int rq_events_syscall(int code)
{
	switch ((enum vm_extra_syscall)code) {
	case VM_XSYS_QUEST_END:
		run_qend();
		return 1;
	case VM_XSYS_MAP_ENTER:
		map_enter();
		return 1;
	case VM_XSYS_MAP_EXIT:
		g_in_map = 0;
		return 1;
	case VM_XSYS_MAP_CLEARED:
		map_cleared();
		return 1;
	case VM_XSYS_QUEST_CLEAR:
		run_clear();
		return 1;
	case VM_XSYS_BATTLE_DOWN:
		run_down();
		return 1;
	case VM_XSYS_BATTLE_SUPPORT:
		run_support();
		return 1;
	case VM_XSYS_BATTLE_START:
		rq_battle_flames_clear();
		run_cbs(g_start_cb, g_nstart, local_get(0).i, 0, 1);
		return 1;
	case VM_XSYS_BATTLE_END: {
		int re = stack_pop().i;
		rq_battle_flames_clear();
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

int rq_lua_battle_on_support(lua_State *L)
{
	luaL_checktype(L, 1, LUA_TFUNCTION);
	if (!ain || !install_support(ain))
		return luaL_error(L, "battle on_support install failed (see mods/rqmod.log)");
	if (g_nsup >= CB_MAX)
		return luaL_error(L, "too many battle callbacks registered");
	lua_pushvalue(L, 1);
	g_sup_cb[g_nsup++] = luaL_ref(L, LUA_REGISTRYINDEX);
	return 0;
}

int rq_lua_battle_on_down(lua_State *L)
{
	luaL_checktype(L, 1, LUA_TFUNCTION);
	if (!ain || !install_down(ain))
		return luaL_error(L, "battle on_down install failed (see mods/rqmod.log)");
	if (g_ndown >= CB_MAX)
		return luaL_error(L, "too many battle callbacks registered");
	lua_pushvalue(L, 1);
	g_down_cb[g_ndown++] = luaL_ref(L, LUA_REGISTRYINDEX);
	return 0;
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
