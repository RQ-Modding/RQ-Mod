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
 */
static int button_for_skill(int skill_no)
{
	int skill_slot, idx, status, btn;

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
	int skill, btn;
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

	btn = button_for_skill(skill);
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
	int ok;

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

	if (g_in_ally_select && button_for_skill(skill_no) > 0)
		ok = 1;
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

#define CARDS 10
#define CARD_MARKS 16
#define BUFF_MAX 8
#define MARK_HOLD_MS 1500u
#define MARK_FADE_MS 250u
#define ANIM_MARK_NO 31

struct card_mark {
	int mark, status, cg, anim;
	char key[32];
	char text[16];
};
struct card {
	struct card_mark e[CARD_MARKS];
	int n, cur, shown_anim;
	char shown_text[16];
	uint32_t t0;
};
static struct card g_card[CARDS];
static struct { char key[32]; char text[16]; int cg, anim; } g_buff[CARDS][BUFF_MAX];
static int g_nbuff[CARDS];
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

static int manager_global(int *gi, const char *name)
{
	if (*gi == -2) {
		char *sj = utf2sjis(name, 0);
		*gi = sj ? ain_get_global(ain, sj) : -1;
		free(sj);
	}
	return *gi;
}

static int flame_mark_slot(int no)
{
	struct page *mgr, *arr, *ch;
	int slot, gi, st_mgr, m_list, m_mark2, i;

	if (is_ally_no(no)) {
		gi = manager_global(&g_fl_gi, "g_sBattleChara");
		st_mgr = g_fl_st_mgr;
		m_list = g_fl_m_chara;
		m_mark2 = g_fl_m_mark2;
		i = no - 1;
	} else if (is_enemy_no(no)) {
		gi = manager_global(&g_fl_gi_monster, "g_sBattleMonster");
		st_mgr = g_fl_st_mon_mgr;
		m_list = g_fl_m_monster;
		m_mark2 = g_fl_m_mon_mark2;
		i = no - 11;
	} else {
		return -1;
	}
	if (m_list < 0 || m_mark2 < 0 || gi < 0 || gi >= ain->nr_globals || !heap || !heap_size || !heap[0].page)
		return -1;
	slot = global_get(gi).i;
	if (!page_index_valid(slot))
		return -1;
	mgr = heap_get_page(slot);
	if (!mgr || mgr->type != STRUCT_PAGE || mgr->index != st_mgr || m_list >= mgr->nr_vars)
		return -1;
	slot = mgr->values[m_list].i;
	if (!page_index_valid(slot))
		return -1;
	arr = heap_get_page(slot);
	if (!arr || arr->type != ARRAY_PAGE || i >= arr->nr_vars)
		return -1;
	slot = arr->values[i].i;
	if (!page_index_valid(slot))
		return -1;
	ch = heap_get_page(slot);
	if (!ch || ch->type != STRUCT_PAGE || m_mark2 >= ch->nr_vars)
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
	snprintf(e->key, sizeof e->key, "%s", key);
	snprintf(e->text, sizeof e->text, "%s", text);
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
	if (is_ally_no(no) ? g_flame_iic[no - 1] : mark_data(g_fl_charging, no))
		card_add(c, 31, 1, 0, "", "");
	if ((v = mark_data(g_fl_data[0], no)) > 0) {
		snprintf(text, sizeof text, "%d%%", v);
		card_add(c, 32, v, 0, "", text);
	}
	if ((v = mark_data(g_fl_data[1], no)) > 0)
		card_add(c, 33, v, 0, "", "");
	if ((v = mark_data(g_fl_data[2], no)) > 0)
		card_add(c, 34, v, 0, "", "");
	if ((v = mark_data(g_fl_data[3], no)) > 0)
		card_add(c, 35, v, 0, "", "");
	if ((v = mark_data(g_fl_data[4], no)) > 0) {
		snprintf(text, sizeof text, "%dx", v);
		card_add(c, 36, v, 0, "", text);
	}
	for (i = 0; i < g_nbuff[card_index(no)]; i++)
	{
		card_add(c, 0, 1, g_buff[card_index(no)][i].cg, g_buff[card_index(no)][i].key, g_buff[card_index(no)][i].text);
		if (c->n > 0 && !strcmp(c->e[c->n - 1].key, g_buff[card_index(no)][i].key))
			c->e[c->n - 1].anim = g_buff[card_index(no)][i].anim;
	}
	for (i = 0; i < c->n; i++) {
		if (c->e[i].mark == old.mark && !strcmp(c->e[i].key, old.key)) {
			c->cur = i;
			return;
		}
	}
	c->cur = 0;
	c->t0 = SDL_GetTicks();
}

static void card_apply(int no)
{
	struct card *c = &g_card[card_index(no)];
	struct card_mark *e = c->n > 0 ? &c->e[c->cur] : NULL;
	int slot = flame_mark_slot(no), anim = 0;

	if (slot < 0)
		return;
	if (e && e->mark <= 0)
		anim = e->anim ? e->anim : rq_anim_of_cg(e->cg);
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
	 * a mod's buff borrows. */
	if (c->shown_anim)
		mark_call_set(slot, 0, 0, "");
	c->shown_anim = 0;
	if (e && e->mark > 0)
		mark_call_set(slot, e->mark, e->status, e->text);
	else
		mark_call_set(slot, 0, 0, "");
}

static void marks_refresh(void)
{
	int i;

	for (i = 0; i < CARDS; i++) {
		int no = card_no(i);

		if (is_ally_no(no) && g_flame_iic[no - 1] && card_iic(no) != g_flame_iic[no - 1])
			g_flame_iic[no - 1] = 0;
		if (card_iic(no) <= 0 || flame_mark_slot(no) < 0) {
			g_card[i].n = 0;
			continue;
		}
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
	int i;

	for (i = 0; i < CARDS; i++)
		g_card[i].shown_anim = 0;
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
	memset(g_flame_iic, 0, sizeof g_flame_iic);
	memset(g_card, 0, sizeof g_card);
	memset(g_nbuff, 0, sizeof g_nbuff);
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
	g_fl_m_partno = find_member(a, find_struct(a, "T_PartsFlash"), "m_nNo");
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
		return luaL_error(L, "battle.install_flame failed (see mods/rqmod.log)");
	lua_pushboolean(L, 1);
	return 1;
}

int rq_lua_battle_set_flame(lua_State *L)
{
	int iic = (int)luaL_checkinteger(L, 1);
	int on = lua_toboolean(L, 2);
	int no;

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
	g_flame_iic[no - 1] = on ? iic : 0;
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

/* set_buff(iic, key, {icon = cg [, text]}) adds or changes; set_buff(iic, key) removes. */
int rq_lua_battle_set_buff(lua_State *L)
{
	int iic = (int)luaL_checkinteger(L, 1);
	const char *key = luaL_checkstring(L, 2);
	int no = card_of_iic(iic), idx = card_index(no), i, n;

	if (strlen(key) >= sizeof g_buff[0][0].key)
		return luaL_error(L, "set_buff key is too long");
	if (!lua_isnoneornil(L, 3))
		luaL_checktype(L, 3, LUA_TTABLE);
	if (!no) {
		lua_pushboolean(L, 0);
		return 1;
	}
	n = g_nbuff[idx];
	for (i = 0; i < n; i++) {
		if (!strcmp(g_buff[idx][i].key, key))
			break;
	}
	if (lua_isnoneornil(L, 3)) {
		if (i < n) {
			memmove(&g_buff[idx][i], &g_buff[idx][i + 1], sizeof g_buff[0][0] * (n - i - 1));
			g_nbuff[idx]--;
		}
	} else {
		const char *text;
		int cg = 0, anim = 0;
		lua_getfield(L, 3, "anim");
		if (!lua_isnil(L, -1)) {
			anim = rq_anim_handle(luaL_checkstring(L, -1));
			if (!anim)
				return luaL_error(L, "set_buff anim '%s' was not added with rq.image.add_gif", lua_tostring(L, -1));
		}
		lua_pop(L, 1);
		lua_getfield(L, 3, "icon");
		if (!lua_isinteger(L, -1) && !anim)
			return luaL_error(L, "set_buff needs icon (a CG number, rq.image.add gives one) or anim (an rq.image.add_gif key)");
		cg = (int)lua_tointeger(L, -1);
		lua_pop(L, 1);
		if (!anim && !rq_anim_of_cg(cg))
			return luaL_error(L, "set_buff icon %d is no CG, is larger than 1024 pixels, or there are too many (max 128 with the GIFs)", cg);
		if (i == n) {
			if (n >= BUFF_MAX)
				return luaL_error(L, "too many buffs on one card (max %d)", BUFF_MAX);
			g_nbuff[idx]++;
		}
		snprintf(g_buff[idx][i].key, sizeof g_buff[0][0].key, "%s", key);
		g_buff[idx][i].cg = cg;
		g_buff[idx][i].anim = anim;
		lua_getfield(L, 3, "text");
		text = lua_tostring(L, -1);
		snprintf(g_buff[idx][i].text, sizeof g_buff[0][0].text, "%s", text ? text : "");
		lua_pop(L, 1);
	}
	card_fill(no);
	card_apply(no);
	lua_pushboolean(L, 1);
	return 1;
}

/* buffs(iic): what the card shows in turn. Vanilla marks have mark, mod buffs have key and icon. */
int rq_lua_battle_buffs(lua_State *L)
{
	int no = card_of_iic((int)luaL_checkinteger(L, 1)), i;

	lua_newtable(L);
	if (!no || flame_mark_slot(no) < 0)
		return 1;
	card_fill(no);
	for (i = 0; i < g_card[card_index(no)].n; i++) {
		struct card_mark *e = &g_card[card_index(no)].e[i];
		lua_newtable(L);
		if (e->mark > 0) {
			lua_pushinteger(L, e->mark);
			lua_setfield(L, -2, "mark");
		} else {
			lua_pushstring(L, e->key);
			lua_setfield(L, -2, "key");
			lua_pushinteger(L, e->cg);
			lua_setfield(L, -2, "icon");
		}
		lua_pushstring(L, e->text);
		lua_setfield(L, -2, "text");
		lua_rawseti(L, -2, i + 1);
	}
	return 1;
}

#endif /* RQ_MODLOADER */
