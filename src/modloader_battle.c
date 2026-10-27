/* Copyright (C) 2026 Rance Quest modding fork
 *
 * Ally skill select: wrap T_BattleSelect@select / @checkKey / @exec so Lua can
 * claim Space and finish the choice like clicking a skill (execSkill).
 *
 * Prefer returning a real skill-button id so vanilla execSkill runs. Pending
 * @exec is a fallback that only does uses− / nRet / target-self via safe
 * primitives — no nested T_BattleSkill method calls (those AVd after claim).
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
#include "input.h"

#define BATTLE_SENTINEL_ID 200001

static int g_on;
static int g_in_ally_select;
static int g_actor_no = -1;
static int g_actor_chara = -1;
static int g_select_this = -1;
static int g_space_latched;
static int g_pending_skill;
static int g_hook_ref = LUA_NOREF;

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
	stack_push(arg);
	vm_call(fno, this_slot);
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
	int skill = 0;

	if (!L || g_hook_ref == LUA_NOREF)
		return 0;

	lua_rawgeti(L, LUA_REGISTRYINDEX, g_hook_ref);
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
		return 0;
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
	return skill > 0 ? skill : 0;
}

static void battle_select_enter(void)
{
	g_in_ally_select = 1;
	g_actor_no = local_get(0).i;
	g_select_this = current_struct_slot();
	if (g_select_this < 0 || !page_index_valid(g_select_this))
		g_select_this = -1;
	g_actor_chara = call_i(g_fno_iic, g_actor_no);
	g_space_latched = 0;
	g_pending_skill = 0;
}

static void battle_select_exit(void)
{
	g_in_ally_select = 0;
	g_actor_no = -1;
	g_actor_chara = -1;
	g_select_this = -1;
	g_space_latched = 0;
	g_pending_skill = 0;
}

static void battle_select_key(void)
{
	int skill, btn;
	int this_slot;

	/* Prefer stable select `this` from ENTER; refresh if still unset. */
	this_slot = current_struct_slot();
	if (page_index_valid(this_slot))
		g_select_this = this_slot;
	if (skill_page_slot() >= 0)
		g_in_ally_select = 1;

	if (g_pending_skill > 0) {
		stack_push(BATTLE_SENTINEL_ID);
		return;
	}
	if (!g_in_ally_select) {
		stack_push(0);
		return;
	}
	if (g_space_latched) {
		if (!key_is_down_raw(VK_SPACE))
			g_space_latched = 0;
		stack_push(0);
		return;
	}
	if (!key_is_down_raw(VK_SPACE)) {
		stack_push(0);
		return;
	}

	skill = lua_select_key(VK_SPACE);
	if (skill <= 0) {
		stack_push(0);
		return;
	}

	key_set_state(VK_SPACE, 0);
	g_space_latched = 1;

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
	if (g_hook_ref != LUA_NOREF)
		luaL_unref(L, LUA_REGISTRYINDEX, g_hook_ref);
	lua_pushvalue(L, 1);
	g_hook_ref = luaL_ref(L, LUA_REGISTRYINDEX);
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

#endif /* RQ_MODLOADER */
