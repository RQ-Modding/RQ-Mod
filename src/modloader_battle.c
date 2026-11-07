/* Copyright (C) 2026 Rance Quest modding fork
 *
 * Ally skill select: wrap T_BattleSelect@select / @checkKey / @exec so Lua can
 * claim Space and finish the choice like clicking a skill (execSkill).
 *
 * Prefer returning a real skill-button id so vanilla execSkill runs. Pending
 * @exec is a fallback that only does uses− / nRet / target-self via safe
 * primitives — no nested T_BattleSkill method calls (those AVd after claim).
 *
 * Space uses rising-edge only (was up → down) after select is ready; a key
 * held into battle is armed as already-down so it cannot claim until release.
 */

#ifdef RQ_MODLOADER

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdint.h>

#include "lua.h"
#include "lauxlib.h"

#include "system4.h"
#include "system4/ain.h"
#include "system4/buffer.h"
#include "system4/instructions.h"
#include "system4/string.h"
#include "system4/utfsjis.h"

#include "modloader.h"
#include "modloader_priv.h"
#include "vm.h"
#include "vm/heap.h"
#include "vm/page.h"
#include "input.h"
#include "parts.h"
#include <SDL.h>

#define BATTLE_SENTINEL_ID 200001

static int g_on;
static int g_in_ally_select;
static int g_actor_no = -1;
static int g_actor_chara = -1;
static int g_select_this = -1;
/* Rising-edge tracker: claim only on was-up → now-down while select is ready. */
static int g_space_down_prev = 1;
static int g_pending_skill;
#define SELECT_CB_MAX 16
static int g_hook_refs[SELECT_CB_MAX];
static int g_nhook;

static int g_memb_rfsSkill = -1;
static int g_memb_nTargetNo = -1;
static int g_fno_getSkillIndex = -1;
static int g_fno_getButtonID = -1;
static int g_fno_getSkillStatus = -1;
static int g_fno_updateSkillStatus = -1;
static int g_fno_iic = -1;
static int g_fno_equip = -1;
static int g_fno_uses = -1;
static int g_fno_uses_dec = -1;
static int g_fno_lock = -1;

static void marks_refresh(void);

static void emit0(struct buffer *out, enum opcode op)
{
	buffer_write_int16(out, (uint16_t)op);
}

static void emit1(struct buffer *out, enum opcode op, int32_t arg)
{
	buffer_write_int16(out, (uint16_t)op);
	buffer_write_int32(out, (uint32_t)arg);
}

static uint32_t emit_ifz(struct buffer *out)
{
	uint32_t at = (uint32_t)out->index;
	emit1(out, IFZ, 0);
	return at;
}

static void patch_j(struct buffer *out, uint32_t at, struct ain *ain)
{
	buffer_write_int32_at(out, at + 2, (uint32_t)(ain->code_size + out->index));
}

static int find_fn(struct ain *ain, const char *utf8)
{
	char *sjis = utf2sjis(utf8, 0);
	int fno;
	if (!sjis)
		return -1;
	fno = ain_get_function(ain, sjis);
	free(sjis);
	return fno;
}

static int find_struct(struct ain *ain, const char *utf8)
{
	char *sjis = utf2sjis(utf8, 0);
	int st;
	if (!sjis)
		return -1;
	st = ain_get_struct(ain, sjis);
	free(sjis);
	return st;
}

static int find_member(struct ain *ain, int st, const char *utf8)
{
	char *sjis;
	int i;

	if (!ain || st < 0 || st >= ain->nr_structures)
		return -1;
	sjis = utf2sjis(utf8, 0);
	if (!sjis)
		return -1;
	for (i = 0; i < ain->structures[st].nr_members; i++) {
		if (ain->structures[st].members[i].name
		    && !strcmp(ain->structures[st].members[i].name, sjis)) {
			free(sjis);
			return i;
		}
	}
	free(sjis);
	return -1;
}

static void commit_wrap(struct ain *ain, int fno, struct buffer *out)
{
	ain->functions[fno].address = (uint32_t)(ain->code_size + 6);
	ain->code = xrealloc(ain->code, ain->code_size + out->index);
	memcpy(ain->code + ain->code_size, out->buf, out->index);
	ain->code_size += out->index;
}

static int call_i(int fno, int a)
{
	int r;
	if (fno < 0)
		return 0;
	stack_push(a);
	vm_call(fno, -1);
	r = stack_pop().i;
	return r;
}

static int call_ii(int fno, int a, int b)
{
	int r;
	if (fno < 0)
		return 0;
	stack_push(a);
	stack_push(b);
	vm_call(fno, -1);
	r = stack_pop().i;
	return r;
}

static int call_method_i(int fno, int this_slot, int arg)
{
	int r;
	if (fno < 0 || this_slot < 0 || !page_index_valid(this_slot))
		return -1;
	/* A method wants [this, arg]. vm_call(fno, slot) would push this on top of
	 * the arg and swap them, so the call ran on heap slot <arg>. */
	stack_push(arg);
	vm_call_method(fno, this_slot);
	r = stack_pop().i;
	return r;
}

static void call_method_0(int fno, int this_slot)
{
	if (fno < 0 || this_slot < 0 || !page_index_valid(this_slot))
		return;
	vm_call(fno, this_slot);
}

static int current_struct_slot(void)
{
	struct page *lp = local_page();
	if (!lp)
		return -1;
	return lp->local.struct_ptr;
}

static int skill_page_slot(void)
{
	struct page *sel;
	int slot;

	if (g_select_this < 0 || !page_index_valid(g_select_this) || g_memb_rfsSkill < 0)
		return -1;
	sel = heap_get_page(g_select_this);
	if (!sel || g_memb_rfsSkill >= sel->nr_vars)
		return -1;
	slot = page_get_var(sel, g_memb_rfsSkill).i;
	if (slot < 0 || !page_index_valid(slot))
		return -1;
	return slot;
}

static int skill_usable_chara(int chara, int skill_no)
{
	int equip, uses, lock;

	if (chara <= 0 || skill_no <= 0)
		return 0;
	equip = call_ii(g_fno_equip, chara, skill_no);
	if (equip <= 0)
		return 0;
	uses = call_ii(g_fno_uses, chara, skill_no);
	if (uses <= 0)
		return 0;
	lock = call_ii(g_fno_lock, chara, skill_no);
	if (lock > 0)
		return 0;
	return 1;
}

/*
 * Real skill-row button id (200001+index). status == 0 is still a listed
 * action skill; vanilla getSkillNoFromIndex only rejects status < 0.
 * Requiring status > 0 left button=-1 and forced the crashing pending path.
 * *status gets the row's status: 0 is a greyed button (no uses left, locked,
 * or a front-row skill from the back), which the game does not let a click use.
 */
static int button_for_skill(int skill_no, int *status_out)
{
	int skill_slot, idx, status, btn;

	if (status_out)
		*status_out = -1;
	rq_crash_crumb("button_for_skill", skill_no, g_select_this);
	skill_slot = skill_page_slot();
	if (skill_slot < 0)
		return -1;
	if (g_fno_updateSkillStatus >= 0)
		call_method_0(g_fno_updateSkillStatus, skill_slot);
	idx = call_method_i(g_fno_getSkillIndex, skill_slot, skill_no);
	if (idx < 0)
		return -1;
	status = call_method_i(g_fno_getSkillStatus, skill_slot, idx);
	if (status < 0)
		return -1;
	if (status_out)
		*status_out = status;
	btn = call_method_i(g_fno_getButtonID, skill_slot, idx);
	return btn > 0 ? btn : -1;
}

static int lua_select_key(int key)
{
	lua_State *L = rq_lua();
	int i;

	if (!L)
		return 0;

	/* The first callback that names a skill takes the key. */
	for (i = 0; i < g_nhook; i++) {
		int skill = 0;
		rq_crash_crumb("lua:on_select_key", g_actor_chara, key);
		lua_rawgeti(L, LUA_REGISTRYINDEX, g_hook_refs[i]);
		lua_newtable(L);
		lua_pushinteger(L, key);
		lua_setfield(L, -2, "key");
		lua_pushinteger(L, g_actor_no);
		lua_setfield(L, -2, "no");
		lua_pushinteger(L, g_actor_chara);
		lua_setfield(L, -2, "chara");

		if (lua_pcall(L, 1, 1, 0) != LUA_OK) {
			rq_log("battle on_select_key error: %s", lua_tostring(L, -1));
			lua_pop(L, 1);
			continue;
		}
		if (lua_isinteger(L, -1) || lua_isnumber(L, -1)) {
			skill = (int)lua_tointeger(L, -1);
		} else if (lua_istable(L, -1)) {
			lua_getfield(L, -1, "skill");
			if (lua_isinteger(L, -1) || lua_isnumber(L, -1))
				skill = (int)lua_tointeger(L, -1);
			lua_pop(L, 1);
		}
		lua_pop(L, 1);
		if (skill > 0)
			return skill;
	}
	return 0;
}

static int select_ready(void)
{
	return g_in_ally_select
		&& g_actor_no > 0
		&& g_actor_chara > 0
		&& g_select_this >= 0
		&& page_index_valid(g_select_this)
		&& skill_page_slot() >= 0;
}

static void battle_select_enter(void)
{
	g_in_ally_select = 1;
	g_actor_no = local_get(0).i;
	g_select_this = current_struct_slot();
	if (g_select_this < 0 || !page_index_valid(g_select_this))
		g_select_this = -1;
	g_actor_chara = call_i(g_fno_iic, g_actor_no);
	/*
	 * Arm as already-down so a Space held into battle never counts as a
	 * press until the player releases and taps again.
	 */
	g_space_down_prev = 1;
	g_pending_skill = 0;
}

static void battle_select_exit(void)
{
	g_in_ally_select = 0;
	g_actor_no = -1;
	g_actor_chara = -1;
	g_select_this = -1;
	g_space_down_prev = 1;
	g_pending_skill = 0;
}

static void battle_select_key(void)
{
	int skill, btn, status;
	int this_slot;
	int down, edge;

	/* Prefer stable select `this` from ENTER; refresh if still unset. */
	this_slot = current_struct_slot();
	if (page_index_valid(this_slot))
		g_select_this = this_slot;
	if (skill_page_slot() >= 0)
		g_in_ally_select = 1;

	down = key_is_down_raw(VK_SPACE) ? 1 : 0;
	edge = down && !g_space_down_prev;
	g_space_down_prev = down;

	if (g_pending_skill > 0) {
		stack_push(BATTLE_SENTINEL_ID);
		return;
	}
	if (!g_in_ally_select) {
		stack_push(0);
		return;
	}
	/* Half-init select (no skill card yet) or held key — never claim. */
	if (!select_ready() || !edge) {
		stack_push(0);
		return;
	}

	skill = lua_select_key(VK_SPACE);
	if (skill <= 0) {
		stack_push(0);
		return;
	}

	/* Swallow so Cancel/moveCursor do not also see Space this frame. */
	key_swallow(VK_SPACE);
	g_space_down_prev = 1;

	btn = button_for_skill(skill, &status);
	if (btn > 0 && status <= 0) {
		rq_log("battle: Space skill %d is greyed out — Cancel", skill);
		stack_push(0);
		return;
	}
	if (btn > 0) {
		/* Vanilla execSkill path — no pending apply. */
		g_pending_skill = 0;
		rq_log("battle: Space → skill %d via button %d (vanilla execSkill)",
		       skill, btn);
		stack_push(btn);
		return;
	}

	/* Fallback: complete in @exec without T_BattleSkill method calls. */
	if (!skill_usable_chara(g_actor_chara, skill)) {
		rq_log("battle: Space skill %d has no button and not usable — Cancel",
		       skill);
		stack_push(0);
		return;
	}
	g_pending_skill = skill;
	rq_log("battle: Space → skill %d via pending exec (no button id)", skill);
	stack_push(BATTLE_SENTINEL_ID);
}

/*
 * Safe pending apply (no nested BattleSkill methods).
 * Returns 1=done true, 2=fail false, 0=call original.
 */
static void battle_select_exec(void)
{
	int skill = g_pending_skill;
	int ref_page, ref_idx;
	struct page *sel;

	if (skill <= 0) {
		stack_push(0);
		return;
	}
	g_pending_skill = 0;

	rq_log("battle: pending apply skill=%d chara=%d select_this=%d",
	       skill, g_actor_chara, g_select_this);

	if (g_actor_chara <= 0 || g_fno_uses_dec < 0) {
		rq_log("battle: pending apply aborted (chara/uses_dec)");
		stack_push(2);
		return;
	}

	call_ii(g_fno_uses_dec, g_actor_chara, skill);

	if (g_select_this >= 0 && page_index_valid(g_select_this)
	    && g_memb_nTargetNo >= 0) {
		sel = heap_get_page(g_select_this);
		if (sel && g_memb_nTargetNo < sel->nr_vars)
			page_set_var(sel, g_memb_nTargetNo,
				     g_actor_no > 0 ? g_actor_no : 0);
	}

	/* exec(int nID, ref int nRet): locals 1/2 = ref page + index. */
	ref_page = local_get(1).i;
	ref_idx = local_get(2).i;
	if (!page_index_valid(ref_page)) {
		rq_log("battle: pending apply bad nRet ref page=%d idx=%d",
		       ref_page, ref_idx);
		stack_push(2);
		return;
	}
	sel = heap_get_page(ref_page);
	if (!sel || ref_idx < 0 || ref_idx >= sel->nr_vars) {
		rq_log("battle: pending apply bad nRet idx=%d nr=%d",
		       ref_idx, sel ? sel->nr_vars : -1);
		stack_push(2);
		return;
	}
	page_set_var(sel, ref_idx, skill);

	rq_log("battle: pending apply ok skill=%d target_no=%d", skill, g_actor_no);
	stack_push(1);
}

static int wrap_select(struct ain *ain, int fno)
{
	int dup = ain_dup_function(ain, fno);
	int nargs = ain->functions[fno].nr_args;
	int i;
	struct buffer out;

	buffer_init(&out, NULL, 0);
	emit1(&out, FUNC, fno);
	emit1(&out, CALLSYS, VM_XSYS_BATTLE_SELECT_ENTER);
	emit0(&out, PUSHSTRUCTPAGE);
	for (i = 0; i < nargs; i++)
		emit1(&out, SH_LOCALREF, i);
	emit1(&out, CALLMETHOD, dup);
	emit1(&out, CALLSYS, VM_XSYS_BATTLE_SELECT_EXIT);
	emit0(&out, RETURN);
	emit1(&out, ENDFUNC, fno);
	commit_wrap(ain, fno, &out);
	rq_log("battle: wrapped T_BattleSelect@select (enter/exit)");
	return 1;
}

static int wrap_check_key(struct ain *ain, int fno)
{
	int dup = ain_dup_function(ain, fno);
	int nargs = ain->functions[fno].nr_args;
	int i;
	struct buffer out;
	uint32_t ifz_at;

	buffer_init(&out, NULL, 0);
	emit1(&out, FUNC, fno);
	emit1(&out, CALLSYS, VM_XSYS_BATTLE_SELECT_KEY);
	emit0(&out, DUP);
	emit1(&out, PUSH, 0);
	emit0(&out, GT);
	ifz_at = emit_ifz(&out);
	emit0(&out, RETURN);
	patch_j(&out, ifz_at, ain);
	emit0(&out, POP);
	emit0(&out, PUSHSTRUCTPAGE);
	for (i = 0; i < nargs; i++)
		emit1(&out, SH_LOCALREF, i);
	emit1(&out, CALLMETHOD, dup);
	emit0(&out, RETURN);
	emit1(&out, ENDFUNC, fno);
	commit_wrap(ain, fno, &out);
	rq_log("battle: wrapped T_BattleSelect@checkKey (Space → Lua)");
	return 1;
}

static int wrap_exec(struct ain *ain, int fno)
{
	int dup = ain_dup_function(ain, fno);
	int nargs = ain->functions[fno].nr_args;
	int i;
	struct buffer out;
	uint32_t ifz_ok, ifz_fail;

	buffer_init(&out, NULL, 0);
	emit1(&out, FUNC, fno);
	emit1(&out, CALLSYS, VM_XSYS_BATTLE_SELECT_EXEC);
	/* 1 → return true; 2 → return false; 0 → original */
	emit0(&out, DUP);
	emit1(&out, PUSH, 1);
	emit0(&out, EQUALE);
	ifz_ok = emit_ifz(&out);
	emit0(&out, RETURN);
	patch_j(&out, ifz_ok, ain);
	emit0(&out, DUP);
	emit1(&out, PUSH, 2);
	emit0(&out, EQUALE);
	ifz_fail = emit_ifz(&out);
	emit0(&out, POP);
	emit1(&out, PUSH, 0);
	emit0(&out, RETURN);
	patch_j(&out, ifz_fail, ain);
	emit0(&out, POP);
	emit0(&out, PUSHSTRUCTPAGE);
	for (i = 0; i < nargs; i++)
		emit1(&out, SH_LOCALREF, i);
	emit1(&out, CALLMETHOD, dup);
	emit0(&out, RETURN);
	emit1(&out, ENDFUNC, fno);
	commit_wrap(ain, fno, &out);
	rq_log("battle: wrapped T_BattleSelect@exec (safe pending apply)");
	return 1;
}

int rq_battle_syscall(int code)
{
	switch ((enum vm_extra_syscall)code) {
	case VM_XSYS_BATTLE_SELECT_ENTER:
		battle_select_enter();
		return 1;
	case VM_XSYS_BATTLE_SELECT_EXIT:
		battle_select_exit();
		return 1;
	case VM_XSYS_BATTLE_SELECT_KEY:
		battle_select_key();
		return 1;
	case VM_XSYS_BATTLE_SELECT_EXEC:
		battle_select_exec();
		return 1;
	case VM_XSYS_BATTLE_MARKS:
		marks_refresh();
		return 1;
	default:
		return 0;
	}
}

int rq_battle_install(struct ain *ain)
{
	int f_select, f_key, f_exec, st;

	if (g_on)
		return 1;
	if (!ain)
		return 0;

	f_select = find_fn(ain, "T_BattleSelect@select");
	f_key = find_fn(ain, "T_BattleSelect@checkKey");
	f_exec = find_fn(ain, "T_BattleSelect@exec");
	st = find_struct(ain, "T_BattleSelect");
	g_memb_rfsSkill = find_member(ain, st, "m_rfsSkill");
	g_memb_nTargetNo = find_member(ain, st, "m_nTargetNo");
	g_fno_getSkillIndex = find_fn(ain, "T_BattleSkill@getSkillIndex");
	g_fno_getButtonID = find_fn(ain, "T_BattleSkill@getButtonID");
	g_fno_getSkillStatus = find_fn(ain, "T_BattleSkill@getSkillStatus");
	g_fno_updateSkillStatus = find_fn(ain, "T_BattleSkill@updateSkillStatus");
	g_fno_iic = find_fn(ain, "戦闘番号_ＩＩＣ");
	g_fno_equip = find_fn(ain, "キャラスキル_実装中");
	g_fno_uses = find_fn(ain, "キャラスキル_使用回数");
	g_fno_uses_dec = find_fn(ain, "キャラスキル_使用回数－");
	g_fno_lock = find_fn(ain, "キャラスキル_使用禁止");

	if (f_select < 0 || f_key < 0 || f_exec < 0 || g_memb_rfsSkill < 0
	    || g_memb_nTargetNo < 0 || g_fno_getSkillIndex < 0
	    || g_fno_getButtonID < 0 || g_fno_iic < 0 || g_fno_equip < 0
	    || g_fno_uses < 0 || g_fno_uses_dec < 0 || g_fno_lock < 0) {
		rq_log("battle: missing AIN (select=%d checkKey=%d exec=%d "
		       "m_rfsSkill=%d m_nTargetNo=%d getSkillIndex=%d "
		       "getButtonID=%d iic=%d equip=%d uses=%d uses_dec=%d lock=%d)",
		       f_select, f_key, f_exec, g_memb_rfsSkill, g_memb_nTargetNo,
		       g_fno_getSkillIndex, g_fno_getButtonID, g_fno_iic,
		       g_fno_equip, g_fno_uses, g_fno_uses_dec, g_fno_lock);
		return 0;
	}
	if (!wrap_select(ain, f_select) || !wrap_check_key(ain, f_key)
	    || !wrap_exec(ain, f_exec))
		return 0;

	g_on = 1;
	rq_log("battle: ally select hooked (prefer vanilla button; safe pending fallback)");
	return 1;
}

int rq_lua_battle_on_select_key(lua_State *L)
{
	luaL_checktype(L, 1, LUA_TFUNCTION);
	if (g_nhook >= SELECT_CB_MAX)
		return luaL_error(L, "too many on_select_key callbacks registered");
	lua_pushvalue(L, 1);
	g_hook_refs[g_nhook++] = luaL_ref(L, LUA_REGISTRYINDEX);
	return 0;
}

int rq_lua_battle_in_ally_select(lua_State *L)
{
	lua_pushboolean(L, g_in_ally_select != 0);
	return 1;
}

int rq_lua_battle_actor(lua_State *L)
{
	if (!g_in_ally_select || g_actor_no <= 0) {
		lua_pushnil(L);
		return 1;
	}
	lua_newtable(L);
	lua_pushinteger(L, g_actor_no);
	lua_setfield(L, -2, "no");
	lua_pushinteger(L, g_actor_chara);
	lua_setfield(L, -2, "chara");
	return 1;
}

int rq_lua_battle_skill_usable(lua_State *L)
{
	int chara = 0;
	int skill_no;
	int ok, status;

	if (lua_istable(L, 1)) {
		lua_getfield(L, 1, "chara");
		if (lua_isinteger(L, -1) || lua_isnumber(L, -1))
			chara = (int)lua_tointeger(L, -1);
		lua_pop(L, 1);
		if (chara <= 0) {
			lua_getfield(L, 1, "no");
			if (lua_isinteger(L, -1) || lua_isnumber(L, -1)) {
				int no = (int)lua_tointeger(L, -1);
				chara = call_i(g_fno_iic, no);
			}
			lua_pop(L, 1);
		}
	} else {
		chara = (int)luaL_checkinteger(L, 1);
	}
	skill_no = (int)luaL_checkinteger(L, 2);

	if (g_in_ally_select && button_for_skill(skill_no, &status) > 0)
		ok = status > 0;
	else
		ok = skill_usable_chara(chara, skill_no);

	lua_pushboolean(L, ok);
	return 1;
}

/*
 * Ally flame. Allies already own a T_BattleMarkNum (m_sMark2) with the same
 * "preparing" flame the enemies show before a charged attack, but the game
 * never turns it on for them. Lua flags a character; the flame is switched on
 * at once and again at the end of every ally status refresh (the refresh
 * only ever clears a mark whose number matches, so it cannot erase it).
 * Mark2 shows one mark at a time, so the marks of a card take turns (below).
 * A mod's buff is a mark too, so it sits in the card's z order, under the game's windows.
 * Enemy cards have the same Mark2, and there the game does use the flame. A card goes by
 * battle number, as in the game: 1 to 5 are allies, 11 to 15 enemies.
 */
static int g_flame_on;
static int g_flame_iic[5];
static int g_fl_gi_pta = -1;
static int g_fl_gi_ptb = -1;
static int g_fl_gi_monster = -2;
static int g_fl_st_mon_mgr = -1, g_fl_m_monster = -1, g_fl_m_mon_mark2 = -1;
static int g_fl_charging = -1;
static int g_fl_prepare = -1;
static int g_fl_st_mgr = -1;
static int g_fl_m_chara = -1;
static int g_fl_m_mark2 = -1;
static int g_fl_st_mark = -1;
static int g_fl_gi = -2;
static int g_fl_set = -1;
static int g_fl_data[5] = { -1, -1, -1, -1, -1 };
static int g_fl_m_mark = -1, g_fl_m_number = -1, g_fl_m_flash = -1;
static int g_fl_m_partno = -1, g_fl_m_markno = -1;
static int g_fl_set_text = -1;
static int g_fl_set_pos = -1, g_fl_set_z = -1, g_fl_set_show = -1;
static int g_fl_num_create = -1, g_fl_num_update = -1, g_fl_mark_set = -1;
static int g_fl_m_num_show = -1, g_fl_m_x = -1, g_fl_m_y = -1, g_fl_m_z = -1;
static int g_fl_m_stack = -1, g_fl_m_mon_stack = -1, g_fl_m_asmark = -1;

#define CARDS 10
#define BUFF_MAX 8
#define CARD_MARKS (9 + BUFF_MAX * 2) /* the game's marks, second states, a character's buffs, a rank's buffs */
#define MARK_HOLD_MS 1500u
#define MARK_FADE_MS 250u
#define ANIM_MARK_NO 31
#define NOT_SET INT32_MIN
#define ALLY_MARK_X 111
#define ALLY_MARK_Y 80
#define ENEMY_MARK_X 37
#define ENEMY_MARK_Y 22
#define MARK_LAYER 10
#define ALLY_SECOND_X 143
#define ALLY_SECOND_Y 76
#define ENEMY_SECOND_X 72
#define ENEMY_SECOND_Y 22
#define SECOND_STEP 26
#define LOOK_RANGE 4096
#define LAYER_RANGE 100
#define MARK_NOS 37
#define MARK_OVER_MAX 64
/* g_pin anim when the pin must load again; -1 is a real value (the game's own file). */
#define PIN_RELOAD INT32_MIN

/* How a mark is shown. x and y go from the card's corner, layer from the card's z. */
struct mark_look {
	int order, x, y, layer, pinned;
};

struct card_mark {
	int mark, status, cg, anim, rank;
	struct mark_look look;
	char key[32];
	char text[16];
};
struct card {
	struct card_mark e[CARD_MARKS];
	struct card_mark pin[CARD_MARKS];
	int n, npin, cur, shown_anim;
	char shown_text[16];
	uint32_t t0;
};
static struct card g_card[CARDS];
/* A pinned mark needs its own T_BattleMarkNum: a card has one, and it takes turns. They are
 * kept and used again, and only emptied at the end of a fight. */
static struct {
	int slot, mark, anim;
	char text[16];
} g_pin[CARDS][CARD_MARKS];
struct buff_list {
	struct { char key[32]; char text[16]; int cg, anim; struct mark_look look; } e[BUFF_MAX];
	int n;
};
/* What a mod asked for a mark of the game. It lasts until changed, over fights. */
struct mark_cfg {
	int on, hide, cg, anim;
	struct mark_look look;
	char file[256];
};
static struct mark_cfg g_mark_cfg[MARK_NOS];
/* The same for one character (iic) or one card (no), in place of the one for every card. */
static struct {
	int iic, no, mark;
	struct mark_cfg cfg;
} g_mark_over[MARK_OVER_MAX];
static int g_nmark_over;
/* What the loader last loaded on a card's stack mark: its part, and the file it pointed at. */
static struct {
	int part, anim;
} g_stack[CARDS][3];
static const struct {
	int no;
	const char *name;
} g_mark_names[] = {
	{ 1, "攻アップマーク" }, { 2, "攻ダウンマーク" }, { 11, "防アップマーク" }, { 12, "防ダウンマーク" },
	{ 21, "毒マーク" }, { 22, "ぴよりマーク" }, { 23, "瀕死マーク" }, { 31, "タメマーク" },
	{ 32, "守護率マーク" }, { 33, "狂戦士マーク" }, { 34, "隠密マーク" }, { 35, "全体加護マーク" },
	{ 36, "マジックバリアマーク" },
};
/* A buff belongs to a character or to a rank, not to the card. There is one entry for
 * everyone who stood on a card in this fight, so it also says who was in the fight. */
struct chara_buffs {
	int iic;
	struct buff_list list;
};
static struct chara_buffs *g_chara_buff;
static int g_nchara_buff, g_chara_buff_cap;
static struct buff_list g_rank_buff[CARDS];
static int g_marks_live, g_marks_dirty;

static int is_ally_no(int no)
{
	return no >= 1 && no <= 5;
}

static int is_enemy_no(int no)
{
	return no >= 11 && no <= 15;
}

static int card_index(int no)
{
	return is_ally_no(no) ? no - 1 : no - 6;
}

static int card_no(int index)
{
	return index < 5 ? index + 1 : index + 6;
}

/* iic in the card no: PTA and PTB are filled before the battle UI exists. */
static int card_iic(int no)
{
	struct page *arr;
	int slot, gi = is_ally_no(no) ? g_fl_gi_pta : g_fl_gi_ptb;
	int i = is_ally_no(no) ? no : no - 10;

	if (gi < 0 || (!is_ally_no(no) && !is_enemy_no(no)) || !heap || !heap_size || !heap[0].page)
		return 0;
	slot = global_get(gi).i;
	if (!page_index_valid(slot))
		return 0;
	arr = heap_get_page(slot);
	if (!arr || arr->type != ARRAY_PAGE || i >= arr->nr_vars)
		return 0;
	return arr->values[i].i;
}

/* A flame goes with the character when the party changes places, and ends when they leave it. */
static int flame_on(int iic)
{
	int i;

	for (i = 0; iic > 0 && i < 5; i++) {
		if (g_flame_iic[i] == iic)
			return 1;
	}
	return 0;
}

static void flames_tidy(void)
{
	int i, no;

	for (i = 0; i < 5; i++) {
		for (no = 1; no <= 5 && card_iic(no) != g_flame_iic[i]; no++)
			;
		if (no > 5)
			g_flame_iic[i] = 0;
	}
}

static int manager_global(int *gi, const char *name)
{
	if (*gi == -2) {
		char *sj = utf2sjis(name, 0);
		*gi = sj ? ain_get_global(ain, sj) : -1;
		free(sj);
	}
	return *gi;
}

/* The T_BattleChara or T_BattleMonster of a card. */
static struct page *card_page(int no)
{
	struct page *mgr, *arr, *ch;
	int slot, gi, st_mgr, m_list, i;

	if (is_ally_no(no)) {
		gi = manager_global(&g_fl_gi, "g_sBattleChara");
		st_mgr = g_fl_st_mgr;
		m_list = g_fl_m_chara;
		i = no - 1;
	} else if (is_enemy_no(no)) {
		gi = manager_global(&g_fl_gi_monster, "g_sBattleMonster");
		st_mgr = g_fl_st_mon_mgr;
		m_list = g_fl_m_monster;
		i = no - 11;
	} else {
		return NULL;
	}
	if (m_list < 0 || gi < 0 || gi >= ain->nr_globals || !heap || !heap_size || !heap[0].page)
		return NULL;
	slot = global_get(gi).i;
	if (!page_index_valid(slot))
		return NULL;
	mgr = heap_get_page(slot);
	if (!mgr || mgr->type != STRUCT_PAGE || mgr->index != st_mgr || m_list >= mgr->nr_vars)
		return NULL;
	slot = mgr->values[m_list].i;
	if (!page_index_valid(slot))
		return NULL;
	arr = heap_get_page(slot);
	if (!arr || arr->type != ARRAY_PAGE || i >= arr->nr_vars)
		return NULL;
	slot = arr->values[i].i;
	if (!page_index_valid(slot))
		return NULL;
	ch = heap_get_page(slot);
	return ch && ch->type == STRUCT_PAGE ? ch : NULL;
}

static int flame_mark_slot(int no)
{
	struct page *ch = card_page(no);
	int m_mark2 = is_ally_no(no) ? g_fl_m_mark2 : g_fl_m_mon_mark2;
	int slot;

	if (!ch || m_mark2 < 0 || m_mark2 >= ch->nr_vars)
		return -1;
	slot = ch->values[m_mark2].i;
	if (!page_index_valid(slot))
		return -1;
	ch = heap_get_page(slot);
	return ch && ch->type == STRUCT_PAGE && ch->index == g_fl_st_mark ? slot : -1;
}

/* Every mark a card has right now. Mark2 is one Flash mark with one number and the
 * art cannot be scaled, so several marks take turns in that spot, fading in and out. */
static struct page *mark_child(struct page *p, int member)
{
	if (!p || member < 0 || member >= p->nr_vars || !page_index_valid(p->values[member].i))
		return NULL;
	return heap_get_page(p->values[member].i);
}

static int mark_str(const char *text)
{
	int slot;
	if (!text[0])
		return vm_string_ref(string_ref(&EMPTY_STRING));
	slot = heap_alloc_slot(VM_STRING);
	heap[slot].s = make_string(text, strlen(text));
	return slot;
}

static void mark_call_set(int slot, int mark, int status, const char *text)
{
	if (g_fl_set < 0)
		return;
	stack_push(mark);
	stack_push(status);
	stack_push(mark_str(text));
	vm_call_method(g_fl_set, slot);
	if (ain->functions[g_fl_set].return_type.data != AIN_VOID)
		stack_pop();
}

static void mark_call_text(int slot, const char *text)
{
	struct page *num = heap_get_page(slot);

	if (g_fl_set_text < 0 || g_fl_m_number < 0 || !num || !page_index_valid(num->values[g_fl_m_number].i))
		return;
	stack_push(mark_str(text));
	stack_push(0);
	vm_call_method(g_fl_set_text, num->values[g_fl_m_number].i);
}

static int mark_data(int fno, int no)
{
	if (fno < 0)
		return 0;
	stack_push(no);
	vm_call(fno, -1);
	return stack_pop().i;
}

static const char *mark_name(int mark)
{
	size_t i;

	for (i = 0; i < sizeof g_mark_names / sizeof g_mark_names[0]; i++) {
		if (g_mark_names[i].no == mark)
			return g_mark_names[i].name;
	}
	return NULL;
}

static void card_add(struct card *c, int mark, int status, int cg, const char *key, const char *text)
{
	struct card_mark *e;

	if (c->n >= CARD_MARKS)
		return;
	e = &c->e[c->n++];
	e->mark = mark;
	e->status = status;
	e->cg = cg;
	e->anim = 0;
	e->rank = 0;
	e->look = (struct mark_look){ 0, NOT_SET, NOT_SET, NOT_SET, 0 };
	snprintf(e->key, sizeof e->key, "%s", key);
	snprintf(e->text, sizeof e->text, "%s", text);
}

/* A character's config wins over its card's, which wins over the one for every card.
 * *own says it is not the one for every card. */
static const struct mark_cfg *mark_cfg_for(int no, int mark, int *own)
{
	int iic = card_iic(no), i;

	*own = 1;
	for (i = 0; iic > 0 && i < g_nmark_over; i++) {
		if (g_mark_over[i].mark == mark && g_mark_over[i].iic == iic)
			return &g_mark_over[i].cfg;
	}
	for (i = 0; i < g_nmark_over; i++) {
		if (g_mark_over[i].mark == mark && g_mark_over[i].no == no)
			return &g_mark_over[i].cfg;
	}
	*own = 0;
	return &g_mark_cfg[mark];
}

/* The file a card's mark loads: a handle, -1 for the game's own file when the config for
 * every card replaced it, or 0 to load as usual. */
static int mark_file_for(int no, int mark)
{
	int own;
	const struct mark_cfg *cfg = mark_cfg_for(no, mark, &own);

	if (!own)
		return 0;
	if (cfg->anim)
		return cfg->anim;
	if (cfg->cg)
		return rq_anim_of_cg(cfg->cg);
	return g_mark_cfg[mark].on && (g_mark_cfg[mark].cg || g_mark_cfg[mark].anim) ? -1 : 0;
}

static void card_add_mark(struct card *c, int no, int mark, int status, const char *text)
{
	int own;
	const struct mark_cfg *cfg = mark_cfg_for(no, mark, &own);

	if (cfg->on && cfg->hide)
		return;
	card_add(c, mark, status, 0, "", text);
	if (cfg->on)
		c->e[c->n - 1].look = cfg->look;
	c->e[c->n - 1].anim = mark_file_for(no, mark);
}

/* The second state of a stat has no room in the game's stack, so it stands beside it. */
static void card_add_seconds(struct card *c, int no)
{
	int marks[3], i;

	if (!rq_states_second_marks(card_iic(no), marks))
		return;
	for (i = 0; i < 3; i++) {
		struct mark_look *look;
		int before = c->n;

		if (!marks[i])
			continue;
		card_add_mark(c, no, marks[i], 1, "");
		if (c->n == before)
			continue;
		look = &c->e[c->n - 1].look;
		look->pinned = 1;
		look->order = 0;
		if (look->x == NOT_SET)
			look->x = is_ally_no(no) ? ALLY_SECOND_X : ENEMY_SECOND_X;
		if (look->y == NOT_SET)
			look->y = (is_ally_no(no) ? ALLY_SECOND_Y : ENEMY_SECOND_Y) + i * SECOND_STEP;
	}
}

static struct buff_list *chara_buffs(int iic, int create)
{
	struct chara_buffs *b;
	int i;

	for (i = 0; i < g_nchara_buff; i++) {
		if (g_chara_buff[i].iic == iic)
			return &g_chara_buff[i].list;
	}
	if (!create)
		return NULL;
	if (g_nchara_buff == g_chara_buff_cap) {
		g_chara_buff_cap = g_chara_buff_cap ? g_chara_buff_cap * 2 : 16;
		g_chara_buff = xrealloc(g_chara_buff, sizeof *g_chara_buff * g_chara_buff_cap);
	}
	b = &g_chara_buff[g_nchara_buff++];
	memset(b, 0, sizeof *b);
	b->iic = iic;
	return &b->list;
}

static void card_add_buffs(struct card *c, const struct buff_list *l, int rank)
{
	int i;

	for (i = 0; l && i < l->n; i++) {
		card_add(c, 0, 1, l->e[i].cg, l->e[i].key, l->e[i].text);
		c->e[c->n - 1].anim = l->e[i].anim;
		c->e[c->n - 1].rank = rank;
		c->e[c->n - 1].look = l->e[i].look;
	}
}

/* In turn by order, the same order staying as it was. A pinned mark leaves the turns. */
static void card_sort(struct card *c)
{
	struct card_mark e;
	int i, j, n = 0;

	for (i = 1; i < c->n; i++) {
		e = c->e[i];
		for (j = i; j > 0 && c->e[j - 1].look.order > e.look.order; j--)
			c->e[j] = c->e[j - 1];
		c->e[j] = e;
	}
	c->npin = 0;
	for (i = 0; i < c->n; i++) {
		if (c->e[i].look.pinned)
			c->pin[c->npin++] = c->e[i];
		else
			c->e[n++] = c->e[i];
	}
	c->n = n;
}

/* The list is rebuilt from the battle data, so the turn stays on the mark it was on. */
static void card_fill(int no)
{
	struct card *c = &g_card[card_index(no)];
	struct card_mark old = { 0 };
	char text[16];
	int v, i;

	if (c->n > 0)
		old = c->e[c->cur];
	c->n = 0;
	if (is_ally_no(no) ? flame_on(card_iic(no)) : mark_data(g_fl_charging, no))
		card_add_mark(c, no, 31, 1, "");
	if ((v = mark_data(g_fl_data[0], no)) > 0) {
		snprintf(text, sizeof text, "%d%%", v);
		card_add_mark(c, no, 32, v, text);
	}
	if ((v = mark_data(g_fl_data[1], no)) > 0)
		card_add_mark(c, no, 33, v, "");
	if ((v = mark_data(g_fl_data[2], no)) > 0)
		card_add_mark(c, no, 34, v, "");
	if ((v = mark_data(g_fl_data[3], no)) > 0)
		card_add_mark(c, no, 35, v, "");
	if ((v = mark_data(g_fl_data[4], no)) > 0) {
		snprintf(text, sizeof text, "%dx", v);
		card_add_mark(c, no, 36, v, text);
	}
	card_add_buffs(c, chara_buffs(card_iic(no), 0), 0);
	card_add_buffs(c, &g_rank_buff[card_index(no)], 1);
	card_add_seconds(c, no);
	card_sort(c);
	for (i = 0; i < c->n; i++) {
		if (c->e[i].mark == old.mark && !strcmp(c->e[i].key, old.key)) {
			c->cur = i;
			return;
		}
	}
	c->cur = 0;
	c->t0 = SDL_GetTicks();
}

static void num_call(int fno, int slot)
{
	vm_call_method(fno, slot);
	if (ain->functions[fno].return_type.data != AIN_VOID)
		stack_pop();
}

static void num_call2(int fno, int slot, int a, int b)
{
	stack_push(a);
	stack_push(b);
	num_call(fno, slot);
}

static void num_call1(int fno, int slot, int a)
{
	stack_push(a);
	num_call(fno, slot);
}

/* The corner and z of a card, from the first mark of its stack, which only the game moves. */
static int stack_mark_slot(struct page *ch, int no, int i)
{
	struct page *stack = mark_child(ch, is_ally_no(no) ? g_fl_m_stack : g_fl_m_mon_stack);
	struct page *arr = mark_child(stack, g_fl_m_asmark);

	if (!arr || arr->type != ARRAY_PAGE || i >= arr->nr_vars || !page_index_valid(arr->values[i].i))
		return -1;
	return arr->values[i].i;
}

static struct page *stack_mark(struct page *ch, int no, int i)
{
	int slot = ch ? stack_mark_slot(ch, no, i) : -1;

	return slot >= 0 ? heap_get_page(slot) : NULL;
}

static int card_corner(int no, int *x, int *y, int *z)
{
	struct page *first = stack_mark(card_page(no), no, 0);

	if (!first || g_fl_m_x < 0 || g_fl_m_y < 0 || g_fl_m_z < 0 || first->values[g_fl_m_z].i < 0)
		return 0;
	*x = first->values[g_fl_m_x].i - (is_ally_no(no) ? 143 : 0);
	*y = first->values[g_fl_m_y].i - (is_ally_no(no) ? 24 : 22);
	*z = first->values[g_fl_m_z].i - 1;
	return 1;
}

/* Moves a T_BattleMarkNum where a mark wants it, without a call when it is there already. */
static void num_place(int no, int slot, const struct mark_look *look)
{
	struct page *mark = mark_child(heap_get_page(slot), g_fl_m_mark);
	int x, y, z, tx, ty, tz;

	if (!mark || g_fl_set_pos < 0 || g_fl_set_z < 0 || !card_corner(no, &x, &y, &z))
		return;
	tx = x + (look && look->x != NOT_SET ? look->x : is_ally_no(no) ? ALLY_MARK_X : ENEMY_MARK_X);
	ty = y + (look && look->y != NOT_SET ? look->y : is_ally_no(no) ? ALLY_MARK_Y : ENEMY_MARK_Y);
	tz = z + (look && look->layer != NOT_SET ? look->layer : MARK_LAYER);
	if (mark->values[g_fl_m_x].i != tx || mark->values[g_fl_m_y].i != ty)
		num_call2(g_fl_set_pos, slot, tx, ty);
	if (mark->values[g_fl_m_z].i != tz)
		num_call1(g_fl_set_z, slot, tz);
}

static void pins_apply(int no)
{
	struct card *c = &g_card[card_index(no)];
	int idx = card_index(no), i;

	if (g_fl_num_create < 0 || g_fl_st_mark < 0)
		return;
	for (i = 0; i < CARD_MARKS; i++) {
		int slot = g_pin[idx][i].slot, mark, anim;

		if (i >= c->npin) {
			if (slot > 0 && g_pin[idx][i].mark) {
				mark_call_set(slot, 0, 0, "");
				g_pin[idx][i].mark = 0;
			}
			continue;
		}
		if (slot <= 0) {
			slot = alloc_struct(g_fl_st_mark);
			init_struct(g_fl_st_mark, slot);
			num_call(g_fl_num_create, slot);
			g_pin[idx][i].slot = slot;
			g_pin[idx][i].mark = 0;
		}
		if (c->pin[i].mark > 0 && c->pin[i].anim > 0) {
			mark = ANIM_MARK_NO;
			anim = c->pin[i].anim;
		} else if (c->pin[i].mark > 0) {
			mark = c->pin[i].mark;
			anim = c->pin[i].anim;
		} else {
			mark = ANIM_MARK_NO;
			anim = c->pin[i].anim ? c->pin[i].anim : rq_anim_of_cg(c->pin[i].cg);
		}
		num_place(no, slot, &c->pin[i].look);
		if (g_pin[idx][i].mark != mark || g_pin[idx][i].anim != anim) {
			mark_call_set(slot, 0, 0, "");
			rq_anim_redirect(anim);
			mark_call_set(slot, mark, c->pin[i].status > 0 ? c->pin[i].status : 1, c->pin[i].text);
			rq_anim_redirect(0);
			g_pin[idx][i].mark = mark;
			g_pin[idx][i].anim = anim;
			memcpy(g_pin[idx][i].text, c->pin[i].text, sizeof g_pin[idx][i].text);
		} else if (strcmp(g_pin[idx][i].text, c->pin[i].text)) {
			mark_call_text(slot, c->pin[i].text);
			memcpy(g_pin[idx][i].text, c->pin[i].text, sizeof g_pin[idx][i].text);
		}
	}
}

/* Pinned marks follow the card's own: where it is, and whether its marks show. Their
 * animation only runs when something calls update. */
static void pins_tick(int no)
{
	struct card *c = &g_card[card_index(no)];
	int idx = card_index(no), slot2 = flame_mark_slot(no), i, show;
	struct page *num2 = slot2 >= 0 ? heap_get_page(slot2) : NULL;

	show = num2 && g_fl_m_num_show >= 0 ? num2->values[g_fl_m_num_show].i : 0;
	for (i = 0; i < c->npin; i++) {
		int slot = g_pin[idx][i].slot;
		struct page *num;

		if (slot <= 0 || !g_pin[idx][i].mark || !(num = heap_get_page(slot)))
			continue;
		num_place(no, slot, &c->pin[i].look);
		if (g_fl_set_show >= 0 && num->values[g_fl_m_num_show].i != show)
			num_call1(g_fl_set_show, slot, show);
		if (show && g_fl_num_update >= 0)
			num_call(g_fl_num_update, slot);
	}
}

/* A mark of the stack has its own Flash and the game keeps its place there, so a mod's
 * wishes go straight to the part. The game makes a new part when the stack moves up. */
static int stack_part(struct page *mark)
{
	struct page *flash = mark_child(mark, g_fl_m_flash);

	return flash && g_fl_m_partno >= 0 ? flash->values[g_fl_m_partno].i : 0;
}

static void stack_tick(int no)
{
	struct page *ch = card_page(no);
	int idx = card_index(no), i, x, y, z;

	if (!ch || !card_corner(no, &x, &y, &z))
		return;
	for (i = 0; i < 3; i++) {
		struct page *mark = stack_mark(ch, no, i);
		const struct mark_cfg *cfg;
		int m, part, want, own;

		if (!mark || (m = mark->values[g_fl_m_markno].i) <= 0 || m >= MARK_NOS)
			continue;
		part = stack_part(mark);
		want = mark_file_for(no, m);
		if (g_fl_mark_set >= 0 && part > 0 && (want != g_stack[idx][i].anim
				|| (want && part != g_stack[idx][i].part))) {
			int page = stack_mark_slot(ch, no, i);
			num_call1(g_fl_mark_set, page, 0);
			rq_anim_redirect(want);
			num_call1(g_fl_mark_set, page, m);
			rq_anim_redirect(0);
			if (!(mark = stack_mark(ch, no, i)))
				continue;
			part = stack_part(mark);
			g_stack[idx][i].anim = want;
			g_stack[idx][i].part = part;
		}
		cfg = mark_cfg_for(no, m, &own);
		if (!cfg->on || part <= 0)
			continue;
		if (cfg->hide)
			PE_SetAlpha(part, 0);
		if (cfg->look.x != NOT_SET || cfg->look.y != NOT_SET)
			PE_SetPos(part,
				cfg->look.x != NOT_SET ? x + cfg->look.x : mark->values[g_fl_m_x].i,
				cfg->look.y != NOT_SET ? y + cfg->look.y : mark->values[g_fl_m_y].i);
		if (cfg->look.layer != NOT_SET)
			PE_SetZ(part, z + cfg->look.layer);
	}
}

static void card_apply(int no)
{
	struct card *c = &g_card[card_index(no)];
	struct card_mark *e = c->n > 0 ? &c->e[c->cur] : NULL;
	int slot = flame_mark_slot(no), anim = 0, want;

	pins_apply(no);
	if (slot < 0)
		return;
	num_place(no, slot, e ? &e->look : NULL);
	if (e && e->mark <= 0)
		anim = e->anim ? e->anim : rq_anim_of_cg(e->cg);
	else if (e && e->anim > 0)
		anim = e->anim;
	if (anim) {
		/* T_BattleMark only takes its fixed numbers, so a mod's buff borrows one and
		 * the load is pointed at its file. Marks restart when set, so a mark that
		 * already shows this file only takes the new text. */
		struct page *mark = mark_child(heap_get_page(slot), g_fl_m_mark);

		if (c->shown_anim == anim && mark && g_fl_m_markno >= 0
		    && mark->values[g_fl_m_markno].i == ANIM_MARK_NO) {
			if (strcmp(c->shown_text, e->text)) {
				mark_call_text(slot, e->text);
				memcpy(c->shown_text, e->text, sizeof c->shown_text);
			}
			return;
		}
		mark_call_set(slot, 0, 0, "");
		rq_anim_redirect(anim);
		mark_call_set(slot, ANIM_MARK_NO, 1, e->text);
		rq_anim_redirect(0);
		c->shown_anim = anim;
		memcpy(c->shown_text, e->text, sizeof c->shown_text);
		return;
	}
	/* The game skips a set of the number it already has, and the flame has the one
	 * a mod's buff borrows. -1 is a mark of the game shown with the game's own file on
	 * this card while every other card has a mod's. */
	want = e && e->mark > 0 && e->anim < 0 ? -1 : 0;
	if (c->shown_anim != want)
		mark_call_set(slot, 0, 0, "");
	c->shown_anim = want;
	if (e && e->mark > 0) {
		rq_anim_redirect(want);
		mark_call_set(slot, e->mark, e->status, e->text);
		rq_anim_redirect(0);
	} else {
		mark_call_set(slot, 0, 0, "");
	}
}

static void marks_refresh(void)
{
	int i;

	flames_tidy();
	for (i = 0; i < CARDS; i++) {
		int no = card_no(i);

		if (card_iic(no) <= 0 || flame_mark_slot(no) < 0) {
			g_card[i].n = 0;
			g_card[i].npin = 0;
			pins_apply(no);
			continue;
		}
		chara_buffs(card_iic(no), 1);
		card_fill(no);
		card_apply(no);
	}
}

void rq_battle_marks_tick(void)
{
	uint32_t now;
	int i;

	if (!g_marks_live)
		return;
	if (g_marks_dirty) {
		g_marks_dirty = 0;
		marks_refresh();
	}
	now = SDL_GetTicks();
	for (i = 0; i < CARDS; i++) {
		struct card *c = &g_card[i];
		struct page *mark, *flash;
		int no = card_no(i), slot, alpha = 255;

		if (card_iic(no) > 0) {
			stack_tick(no);
			pins_tick(no);
		}
		if (c->n <= 0 || (slot = flame_mark_slot(no)) < 0)
			continue;
		if (c->n > 1) {
			uint32_t e = now - c->t0;
			if (e >= MARK_FADE_MS * 2 + MARK_HOLD_MS) {
				c->cur = (c->cur + 1) % c->n;
				c->t0 = now;
				card_apply(no);
				e = 0;
			}
			if (e < MARK_FADE_MS)
				alpha = (int)(255 * e / MARK_FADE_MS);
			else if (e >= MARK_FADE_MS + MARK_HOLD_MS)
				alpha = (int)(255 * (MARK_FADE_MS * 2 + MARK_HOLD_MS - e) / MARK_FADE_MS);
		}
		mark = mark_child(heap_get_page(slot), g_fl_m_mark);
		flash = mark_child(mark, g_fl_m_flash);
		if (flash && g_fl_m_partno >= 0 && flash->values[g_fl_m_partno].i > 0)
			PE_SetAlpha(flash->values[g_fl_m_partno].i, alpha);
	}
}

void rq_battle_marks_reload(void)
{
	int i, k;

	for (i = 0; i < CARDS; i++) {
		g_card[i].shown_anim = 0;
		for (k = 0; k < CARD_MARKS; k++)
			g_pin[i][k].anim = PIN_RELOAD;
	}
	memset(g_stack, 0, sizeof g_stack);
	g_marks_dirty = g_marks_live;
}

void rq_battle_marks_changed(void)
{
	g_marks_dirty = g_marks_live;
}

void rq_battle_marks_live(int on)
{
	g_marks_live = on;
	g_marks_dirty = on;
}

/* Battle start and end: only forget the flags. The battle screen drops its own marks,
 * and calling into it while it is torn down at the end of a fight crashes. */
void rq_battle_flames_clear(void)
{
	int i, k;

	rq_states_forget();
	for (i = 0; i < CARDS; i++) {
		for (k = 0; k < CARD_MARKS; k++) {
			if (g_pin[i][k].slot > 0 && g_pin[i][k].mark) {
				mark_call_set(g_pin[i][k].slot, 0, 0, "");
				g_pin[i][k].mark = 0;
			}
		}
	}
	memset(g_flame_iic, 0, sizeof g_flame_iic);
	memset(g_card, 0, sizeof g_card);
	memset(g_stack, 0, sizeof g_stack);
	g_nchara_buff = 0;
	memset(g_rank_buff, 0, sizeof g_rank_buff);
}

/* The game starts over and its heap goes, with the objects made for pinned marks. */
void rq_battle_forget(void)
{
	rq_state_forget();
	rq_states_forget();
	g_marks_live = 0;
	memset(g_pin, 0, sizeof g_pin);
	memset(g_card, 0, sizeof g_card);
	memset(g_stack, 0, sizeof g_stack);
}

static void wrap_tail_ints(struct ain *a, int fno, int sys)
{
	int dup = ain_dup_function(a, fno);
	int i;
	struct buffer out;

	buffer_init(&out, NULL, 0);
	emit1(&out, FUNC, fno);
	for (i = 0; i < a->functions[fno].nr_args; i++)
		emit1(&out, SH_LOCALREF, i);
	emit1(&out, CALLFUNC, dup);
	emit1(&out, CALLSYS, sys);
	emit0(&out, RETURN);
	emit1(&out, ENDFUNC, fno);
	commit_wrap(a, fno, &out);
}

static int flame_install(struct ain *a)
{
	int f_check, f_init, f_mon_check, f_mon_init, st;

	if (g_flame_on)
		return 1;
	if (!a || !rq_events_install(a))
		return 0;
	f_check = find_fn(a, "戦闘味方_状態確認");
	f_init = find_fn(a, "戦闘味方_初期状態確認");
	f_mon_check = find_fn(a, "戦闘敵_状態確認");
	f_mon_init = find_fn(a, "戦闘敵_初期状態確認");
	g_fl_charging = find_fn(a, "戦闘データ_溜め中");
	g_fl_gi_pta = ain_get_global(a, "PTA");
	g_fl_gi_ptb = ain_get_global(a, "PTB");
	g_fl_st_mon_mgr = find_struct(a, "T_BattleMonsterManager");
	g_fl_m_monster = find_member(a, g_fl_st_mon_mgr, "m_asMonster");
	g_fl_m_mon_mark2 = find_member(a, find_struct(a, "T_BattleMonster"), "m_sMark2");
	g_fl_prepare = find_fn(a, "T_BattleMarkNum@setPrepare");
	g_fl_st_mgr = find_struct(a, "T_BattleCharaManager");
	g_fl_m_chara = find_member(a, g_fl_st_mgr, "m_asChara");
	st = find_struct(a, "T_BattleChara");
	g_fl_m_mark2 = find_member(a, st, "m_sMark2");
	g_fl_st_mark = find_struct(a, "T_BattleMarkNum");
	g_fl_set = find_fn(a, "T_BattleMarkNum@set");
	g_fl_data[0] = find_fn(a, "戦闘データ_守護率");
	g_fl_data[1] = find_fn(a, "戦闘データ_狂戦士");
	g_fl_data[2] = find_fn(a, "戦闘データ_隠密");
	g_fl_data[3] = find_fn(a, "戦闘データ_全体加護");
	g_fl_data[4] = find_fn(a, "戦闘データ_バリア");
	g_fl_m_mark = find_member(a, g_fl_st_mark, "m_sMark");
	g_fl_m_number = find_member(a, g_fl_st_mark, "m_sNumber");
	g_fl_set_text = find_fn(a, "T_DrawText@setText");
	st = find_struct(a, "T_BattleMark");
	g_fl_m_flash = find_member(a, st, "m_sMark");
	g_fl_m_markno = find_member(a, st, "m_nMarkNo");
	g_fl_m_x = find_member(a, st, "m_nX");
	g_fl_m_y = find_member(a, st, "m_nY");
	g_fl_m_z = find_member(a, st, "m_nZ");
	g_fl_m_partno = find_member(a, find_struct(a, "T_PartsFlash"), "m_nNo");
	g_fl_set_pos = find_fn(a, "T_BattleMarkNum@setPos");
	g_fl_set_z = find_fn(a, "T_BattleMarkNum@setZ");
	g_fl_set_show = find_fn(a, "T_BattleMarkNum@setShow");
	g_fl_num_create = find_fn(a, "T_BattleMarkNum@create");
	g_fl_num_update = find_fn(a, "T_BattleMarkNum@update");
	g_fl_mark_set = find_fn(a, "T_BattleMark@set");
	g_fl_m_num_show = find_member(a, g_fl_st_mark, "m_bShow");
	g_fl_m_stack = find_member(a, find_struct(a, "T_BattleChara"), "m_sMark");
	g_fl_m_mon_stack = find_member(a, find_struct(a, "T_BattleMonster"), "m_sMark");
	g_fl_m_asmark = find_member(a, find_struct(a, "T_BattleMarkManager"), "m_asMark");
	if (f_check < 0 || f_init < 0 || g_fl_gi_pta < 0 || g_fl_prepare < 0
	    || g_fl_st_mgr < 0 || g_fl_m_chara < 0 || g_fl_m_mark2 < 0
	    || g_fl_st_mark < 0 || g_fl_set < 0) {
		rq_log("battle: flame missing AIN (check=%d init=%d PTA=%d prepare=%d "
		       "mgr=%d m_asChara=%d m_sMark2=%d)", f_check, f_init, g_fl_gi_pta,
		       g_fl_prepare, g_fl_st_mgr, g_fl_m_chara, g_fl_m_mark2);
		return 0;
	}
	wrap_tail_ints(a, f_check, VM_XSYS_BATTLE_MARKS);
	wrap_tail_ints(a, f_init, VM_XSYS_BATTLE_MARKS);
	if (f_mon_check >= 0 && f_mon_init >= 0 && g_fl_gi_ptb >= 0 && g_fl_charging >= 0
	    && g_fl_st_mon_mgr >= 0 && g_fl_m_monster >= 0 && g_fl_m_mon_mark2 >= 0) {
		wrap_tail_ints(a, f_mon_check, VM_XSYS_BATTLE_MARKS);
		wrap_tail_ints(a, f_mon_init, VM_XSYS_BATTLE_MARKS);
	} else {
		g_fl_gi_ptb = -1;
		rq_log("battle: enemy cards keep one mark, AIN missing (check=%d init=%d PTB=%d charging=%d "
		       "mgr=%d m_asMonster=%d m_sMark2=%d)", f_mon_check, f_mon_init, g_fl_gi_ptb, g_fl_charging,
		       g_fl_st_mon_mgr, g_fl_m_monster, g_fl_m_mon_mark2);
	}
	g_flame_on = 1;
	rq_log("battle: card marks hooked (状態確認 tails rebuild them)");
	return 1;
}

int rq_battle_marks_install(struct ain *a)
{
	return flame_install(a);
}

int rq_lua_battle_install_flame(lua_State *L)
{
	if (!ain || !flame_install(ain))
		return luaL_error(L, "battle.install_flame failed (see logs/rqmod.log)");
	lua_pushboolean(L, 1);
	return 1;
}

int rq_lua_battle_set_flame(lua_State *L)
{
	int iic = (int)luaL_checkinteger(L, 1);
	int on = lua_toboolean(L, 2);
	int no, i;

	if (!g_flame_on)
		return luaL_error(L, "call rq.battle.install_flame() first");
	for (no = 1; no <= 5; no++) {
		if (iic > 0 && card_iic(no) == iic)
			break;
	}
	if (no > 5) {
		lua_pushboolean(L, 0);
		return 1;
	}
	flames_tidy();
	for (i = 0; i < 5; i++) {
		if (g_flame_iic[i] == iic)
			g_flame_iic[i] = 0;
	}
	for (i = 0; on && i < 5; i++) {
		if (!g_flame_iic[i]) {
			g_flame_iic[i] = iic;
			break;
		}
	}
	card_fill(no);
	card_apply(no);
	lua_pushboolean(L, 1);
	return 1;
}

static int card_of_iic(int iic)
{
	int i;
	for (i = 0; i < CARDS; i++) {
		if (iic > 0 && card_iic(card_no(i)) == iic)
			return card_no(i);
	}
	return 0;
}

/* rq.sprite: the Chipmunk sprite a card is drawn with, 0 out of a fight. An ally's is
 * T_BattleChara.m_sCard.m_spBase, an enemy's T_BattleMonster.m_spBase.m_spBase. */
static int g_sp_m_card = -2, g_sp_m_cbase, g_sp_m_mbase, g_sp_m_tbase, g_sp_m_spno;
static int g_sp_m_face, g_sp_m_fsp;

static void card_members(void)
{
	if (g_sp_m_card != -2)
		return;
	g_sp_m_card = find_member(ain, find_struct(ain, "T_BattleChara"), "m_sCard");
	g_sp_m_cbase = find_member(ain, find_struct(ain, "T_CharaCard"), "m_spBase");
	g_sp_m_mbase = find_member(ain, find_struct(ain, "T_BattleMonster"), "m_spBase");
	g_sp_m_tbase = find_member(ain, find_struct(ain, "T_TextSprite"), "m_spBase");
	g_sp_m_spno = find_member(ain, find_struct(ain, "T_Sprite"), "m_nSpNo");
	g_sp_m_face = find_member(ain, find_struct(ain, "T_CharaCard"), "m_sFace");
	g_sp_m_fsp = find_member(ain, find_struct(ain, "T_CharaFace"), "m_spFace");
}

int rq_battle_card_sprite(int no)
{
	struct page *ch = g_marks_live ? card_page(no) : NULL, *base;

	if (!ch)
		return 0;
	card_members();
	if (is_ally_no(no))
		base = mark_child(mark_child(ch, g_sp_m_card), g_sp_m_cbase);
	else
		base = mark_child(mark_child(ch, g_sp_m_mbase), g_sp_m_tbase);
	if (!base || g_sp_m_spno < 0 || g_sp_m_spno >= base->nr_vars)
		return 0;
	return base->values[g_sp_m_spno].i;
}

/* The sprite of an ally card's face (T_CharaCard.m_sFace.m_spFace), 0 out of a fight. */
int rq_battle_face_sprite(int no)
{
	struct page *ch = g_marks_live && is_ally_no(no) ? card_page(no) : NULL, *base;

	if (!ch)
		return 0;
	card_members();
	base = mark_child(mark_child(mark_child(mark_child(ch, g_sp_m_card), g_sp_m_face), g_sp_m_fsp), g_sp_m_tbase);
	if (!base || g_sp_m_spno < 0 || g_sp_m_spno >= base->nr_vars)
		return 0;
	return base->values[g_sp_m_spno].i;
}

/* Who is on a card, 0 if nobody or out of a fight. */
int rq_battle_card_iic(int no)
{
	return g_marks_live ? card_iic(no) : 0;
}

/* The card a character is on now, 0 if none or out of a fight. */
int rq_battle_chara_card(int iic)
{
	return g_marks_live ? card_of_iic(iic) : 0;
}

int rq_battle_live(void)
{
	return g_marks_live;
}

/* Lets the card show a state a mod has just changed. */
void rq_battle_recheck_card(int iic)
{
	int no = g_marks_live ? card_of_iic(iic) : 0;
	int fno = no ? find_fn(ain, "戦闘表示_状態確認") : -1;

	if (fno < 0)
		return;
	stack_push(no);
	stack_push(0);
	stack_push(0);
	vm_call(fno, -1);
	if (ain->functions[fno].return_type.data != AIN_VOID)
		stack_pop();
}

static int look_int(lua_State *L, int idx, const char *field, int fallback, int range, const char *fn)
{
	lua_Integer v;

	lua_getfield(L, idx, field);
	if (lua_isnil(L, -1)) {
		lua_pop(L, 1);
		return fallback;
	}
	if (!lua_isinteger(L, -1))
		luaL_error(L, "%s %s must be a whole number", fn, field);
	v = lua_tointeger(L, -1);
	lua_pop(L, 1);
	if (v < -range || v > range)
		luaL_error(L, "%s %s must be from %d to %d", fn, field, -range, range);
	return (int)v;
}

static struct mark_look look_read(lua_State *L, int idx, const char *fn)
{
	struct mark_look look;

	look.order = look_int(L, idx, "order", 0, LOOK_RANGE, fn);
	look.x = look_int(L, idx, "x", NOT_SET, LOOK_RANGE, fn);
	look.y = look_int(L, idx, "y", NOT_SET, LOOK_RANGE, fn);
	look.layer = look_int(L, idx, "layer", NOT_SET, LAYER_RANGE, fn);
	lua_getfield(L, idx, "pinned");
	look.pinned = lua_toboolean(L, -1);
	lua_pop(L, 1);
	return look;
}

/* The table at index 3 adds or changes the buff, no table removes it. */
static void buff_list_set(lua_State *L, struct buff_list *l, const char *key, const char *fn)
{
	int i;

	for (i = 0; i < l->n; i++) {
		if (!strcmp(l->e[i].key, key))
			break;
	}
	if (lua_isnoneornil(L, 3)) {
		if (i < l->n) {
			memmove(&l->e[i], &l->e[i + 1], sizeof l->e[0] * (l->n - i - 1));
			l->n--;
		}
	} else {
		const char *text;
		int cg = 0, anim = 0;
		struct mark_look look = look_read(L, 3, fn);
		lua_getfield(L, 3, "anim");
		if (!lua_isnil(L, -1)) {
			anim = rq_anim_handle(luaL_checkstring(L, -1));
			if (!anim)
				luaL_error(L, "%s anim '%s' was not added with rq.image.add_gif", fn, lua_tostring(L, -1));
		}
		lua_pop(L, 1);
		lua_getfield(L, 3, "icon");
		if (!lua_isinteger(L, -1) && !anim)
			luaL_error(L, "%s needs icon (a CG number, rq.image.add gives one) or anim (an rq.image.add_gif key)", fn);
		cg = (int)lua_tointeger(L, -1);
		lua_pop(L, 1);
		if (!anim && !rq_anim_of_cg(cg))
			luaL_error(L, "%s icon %d is no CG, is larger than 1024 pixels, or there are too many (max 128 with the GIFs)", fn, cg);
		if (i == l->n) {
			if (l->n >= BUFF_MAX)
				luaL_error(L, "%s: too many buffs (max %d)", fn, BUFF_MAX);
			l->n++;
		}
		snprintf(l->e[i].key, sizeof l->e[0].key, "%s", key);
		l->e[i].cg = cg;
		l->e[i].anim = anim;
		l->e[i].look = look;
		lua_getfield(L, 3, "text");
		text = lua_tostring(L, -1);
		snprintf(l->e[i].text, sizeof l->e[0].text, "%s", text ? text : "");
		lua_pop(L, 1);
	}
}

static const char *buff_args(lua_State *L, const char *fn)
{
	const char *key = luaL_checkstring(L, 2);

	if (strlen(key) >= sizeof g_rank_buff[0].e[0].key)
		luaL_error(L, "%s key is too long", fn);
	if (!lua_isnoneornil(L, 3))
		luaL_checktype(L, 3, LUA_TTABLE);
	return key;
}

/* set_buff(iic, key, {icon = cg [, text]}) adds or changes; set_buff(iic, key) removes. */
int rq_lua_battle_set_buff(lua_State *L)
{
	int iic = (int)luaL_checkinteger(L, 1);
	const char *key = buff_args(L, "set_buff");
	int no = card_of_iic(iic);
	struct buff_list *l = iic > 0 ? chara_buffs(iic, no != 0) : NULL;

	if (!l) {
		lua_pushboolean(L, 0);
		return 1;
	}
	buff_list_set(L, l, key, "set_buff");
	if (no) {
		card_fill(no);
		card_apply(no);
	}
	lua_pushboolean(L, 1);
	return 1;
}

/* set_rank_buff(no, key, def): the same for a battle number, whoever stands there. */
int rq_lua_battle_set_rank_buff(lua_State *L)
{
	int no = (int)luaL_checkinteger(L, 1);
	const char *key = buff_args(L, "set_rank_buff");

	if (!is_ally_no(no) && !is_enemy_no(no)) {
		lua_pushboolean(L, 0);
		return 1;
	}
	buff_list_set(L, &g_rank_buff[card_index(no)], key, "set_rank_buff");
	if (card_iic(no) > 0) {
		card_fill(no);
		card_apply(no);
	}
	lua_pushboolean(L, 1);
	return 1;
}

static void mark_cfg_read(lua_State *L, int idx, const char *name, struct mark_cfg *out)
{
	struct mark_look look = look_read(L, idx, "set_mark");
	int cg = 0, anim = 0;

	lua_getfield(L, idx, "anim");
	if (!lua_isnil(L, -1)) {
		anim = rq_anim_handle(luaL_checkstring(L, -1));
		if (!anim)
			luaL_error(L, "set_mark anim '%s' was not added with rq.image.add_gif", lua_tostring(L, -1));
	}
	lua_pop(L, 1);
	lua_getfield(L, idx, "icon");
	if (!lua_isnil(L, -1)) {
		cg = (int)luaL_checkinteger(L, -1);
		if (!anim && !rq_anim_of_cg(cg))
			luaL_error(L, "set_mark icon %d is no CG, is larger than 1024 pixels, or there are too many (max 128 with the GIFs)", cg);
	}
	lua_pop(L, 1);
	memset(out, 0, sizeof *out);
	lua_getfield(L, idx, "hide");
	out->hide = lua_toboolean(L, -1);
	lua_pop(L, 1);
	out->look = look;
	out->cg = cg;
	out->anim = anim;
	rq_anim_full_name(name, out->file, sizeof out->file);
	out->on = 1;
}

/* set_mark(mark, {hide, icon, anim, order, x, y, layer, pinned} [, who]) changes how a mark of
 * the game shows; set_mark(mark, nil [, who]) gives it back. who is {iic = n} for one character
 * or {no = n} for one card; without it the change is for every card. */
int rq_lua_battle_set_mark(lua_State *L)
{
	int mark = (int)luaL_checkinteger(L, 1), i, s, iic = 0, no = 0;
	const char *name = mark > 0 && mark < MARK_NOS ? mark_name(mark) : NULL;
	struct mark_cfg cfg;

	if (!g_flame_on)
		return luaL_error(L, "call rq.battle.install_flame() first");
	if (!lua_isnoneornil(L, 2))
		luaL_checktype(L, 2, LUA_TTABLE);
	if (!lua_isnoneornil(L, 3)) {
		luaL_checktype(L, 3, LUA_TTABLE);
		lua_getfield(L, 3, "iic");
		iic = (int)luaL_optinteger(L, -1, 0);
		lua_getfield(L, 3, "no");
		no = (int)luaL_optinteger(L, -1, 0);
		lua_pop(L, 2);
		if ((iic > 0) == (no != 0))
			return luaL_error(L, "set_mark who is {iic = n} or {no = n}");
		if (no && !is_ally_no(no) && !is_enemy_no(no))
			return luaL_error(L, "set_mark card number %d is not 1 to 5 or 11 to 15", no);
	}
	if (!name) {
		lua_pushboolean(L, 0);
		return 1;
	}
	if (!lua_isnoneornil(L, 2))
		mark_cfg_read(L, 2, name, &cfg);
	if (!iic && !no) {
		if (lua_isnoneornil(L, 2)) {
			g_mark_cfg[mark].on = 0;
			g_mark_cfg[mark].cg = g_mark_cfg[mark].anim = 0;
		} else {
			g_mark_cfg[mark] = cfg;
		}
	} else {
		for (i = 0; i < g_nmark_over; i++) {
			if (g_mark_over[i].mark == mark && g_mark_over[i].iic == iic && g_mark_over[i].no == no)
				break;
		}
		if (lua_isnoneornil(L, 2)) {
			if (i < g_nmark_over)
				g_mark_over[i] = g_mark_over[--g_nmark_over];
		} else {
			if (i == g_nmark_over) {
				if (g_nmark_over >= MARK_OVER_MAX)
					return luaL_error(L, "set_mark: too many marks for one character or card (max %d)", MARK_OVER_MAX);
				g_nmark_over++;
			}
			g_mark_over[i].iic = iic;
			g_mark_over[i].no = no;
			g_mark_over[i].mark = mark;
			g_mark_over[i].cfg = cfg;
		}
	}
	if (!g_marks_live) {
		lua_pushboolean(L, 1);
		return 1;
	}
	/* What already shows has to load again: the game skips a set of the number it has. */
	for (i = 0; i < CARDS; i++) {
		int no = card_no(i), slot = flame_mark_slot(no), k;
		struct page *ch = card_page(no);

		if (card_iic(no) <= 0 || !ch)
			continue;
		if (slot >= 0) {
			struct page *cur = mark_child(heap_get_page(slot), g_fl_m_mark);
			if (cur && cur->values[g_fl_m_markno].i == mark)
				mark_call_set(slot, 0, 0, "");
		}
		for (s = 0; s < 3; s++) {
			struct page *cur = stack_mark(ch, no, s);
			if (cur && cur->values[g_fl_m_markno].i == mark && g_fl_mark_set >= 0) {
				int page = stack_mark_slot(ch, no, s);
				num_call1(g_fl_mark_set, page, 0);
				num_call1(g_fl_mark_set, page, mark);
				g_stack[i][s].anim = 0;
				g_stack[i][s].part = 0;
			}
		}
		for (k = 0; k < CARD_MARKS; k++)
			g_pin[i][k].anim = PIN_RELOAD;
	}
	g_marks_dirty = 1;
	lua_pushboolean(L, 1);
	return 1;
}

/* A file the game asks for that a mod replaced with set_mark. */
int rq_battle_mark_file(const char *file)
{
	size_t i;

	for (i = 0; i < sizeof g_mark_names / sizeof g_mark_names[0]; i++) {
		int m = g_mark_names[i].no;
		if (g_mark_cfg[m].on && (g_mark_cfg[m].cg || g_mark_cfg[m].anim) && !strcasecmp(g_mark_cfg[m].file, file))
			return g_mark_cfg[m].anim ? g_mark_cfg[m].anim : rq_anim_of_cg(g_mark_cfg[m].cg);
	}
	return 0;
}

static void push_buff(lua_State *L, int index, const char *key, int cg, const char *text, int rank)
{
	lua_newtable(L);
	lua_pushstring(L, key);
	lua_setfield(L, -2, "key");
	lua_pushinteger(L, cg);
	lua_setfield(L, -2, "icon");
	if (rank) {
		lua_pushboolean(L, 1);
		lua_setfield(L, -2, "rank");
	}
	lua_pushstring(L, text);
	lua_setfield(L, -2, "text");
	lua_rawseti(L, -2, index);
}

/* buffs(iic): what the card shows in turn. Vanilla marks have mark, mod buffs have key and icon.
 * Someone off the cards only has the buffs that wait for their return. */
static void push_card(lua_State *L, const struct card *c)
{
	int i;

	for (i = 0; i < c->n + c->npin; i++) {
		const struct card_mark *e = i < c->n ? &c->e[i] : &c->pin[i - c->n];
		if (e->mark > 0) {
			lua_newtable(L);
			lua_pushinteger(L, e->mark);
			lua_setfield(L, -2, "mark");
			lua_pushstring(L, e->text);
			lua_setfield(L, -2, "text");
			lua_rawseti(L, -2, i + 1);
		} else {
			push_buff(L, i + 1, e->key, e->cg, e->text, e->rank);
		}
		if (e->look.pinned) {
			lua_rawgeti(L, -1, i + 1);
			lua_pushboolean(L, 1);
			lua_setfield(L, -2, "pinned");
			lua_pop(L, 1);
		}
	}
}

int rq_lua_battle_buffs(lua_State *L)
{
	int iic = (int)luaL_checkinteger(L, 1);
	int no = card_of_iic(iic);

	lua_newtable(L);
	if (!no) {
		static struct card out;
		const struct buff_list *l = iic > 0 ? chara_buffs(iic, 0) : NULL;

		if (l) {
			out.n = 0;
			card_add_buffs(&out, l, 0);
			card_sort(&out);
			push_card(L, &out);
		}
		return 1;
	}
	if (flame_mark_slot(no) < 0)
		return 1;
	card_fill(no);
	push_card(L, &g_card[card_index(no)]);
	return 1;
}

/* rq.battle.on_turn, on_hit and rq.combat.on_magic: Lua over rq.ain.hook (lua/battle.lua,
 * loader 2.1.0). Runs after rq_scene_open, which puts the removable rq.ain.hook in place. */
extern const char rq_battle_lua[];
extern const unsigned rq_battle_lua_len;
extern const char rq_status_lua[];
extern const unsigned rq_status_lua_len;

static void run_embedded(lua_State *L, int rq_index, const char *code, unsigned len, const char *name)
{
	if (luaL_loadbuffer(L, code, len, name) != LUA_OK) {
		rq_log("%s: %s", name + 1, lua_tostring(L, -1));
		lua_pop(L, 1);
		return;
	}
	lua_pushvalue(L, rq_index);
	if (lua_pcall(L, 1, 0, 0) != LUA_OK) {
		rq_log("%s: %s", name + 1, lua_tostring(L, -1));
		lua_pop(L, 1);
	}
}

/* rq.status (lua/status.lua, loader 2.2.0) is built on the events of battle.lua. */
void rq_battle_lua_open(lua_State *L, int rq_index)
{
	run_embedded(L, rq_index, rq_battle_lua, rq_battle_lua_len, "=battle");
	run_embedded(L, rq_index, rq_status_lua, rq_status_lua_len, "=status");
}

#endif /* RQ_MODLOADER */
