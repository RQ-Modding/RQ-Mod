/* Copyright (C) 2026 Rance Quest modding fork
 *
 * Ally skill select: wrap T_BattleSelect@select / @checkKey so Lua can claim
 * Space (or another key) and complete select like clicking a skill button.
 * Policy (Standby 384, etc.) lives in Lua via rq.battle.on_select_key.
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

static int g_on;
static int g_in_ally_select;
static int g_actor_no = -1;
static int g_actor_chara = -1;
static int g_this_slot = -1;
static int g_space_latched;
static int g_hook_ref = LUA_NOREF;

static int g_memb_rfsSkill = -1;
static int g_fno_getSkillIndex = -1;
static int g_fno_getButtonID = -1;
static int g_fno_getSkillStatus = -1;
static int g_fno_iic = -1;
static int g_fno_equip = -1;
static int g_fno_uses = -1;
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
	if (fno < 0 || this_slot < 0)
		return -1;
	stack_push(arg);
	vm_call(fno, this_slot);
	r = stack_pop().i;
	return r;
}

static int skill_page_slot(void)
{
	struct page *sel;

	if (g_this_slot < 0 || !heap_index_valid(g_this_slot) || g_memb_rfsSkill < 0)
		return -1;
	sel = heap_get_page(g_this_slot);
	if (!sel || g_memb_rfsSkill >= sel->nr_vars)
		return -1;
	return page_get_var(sel, g_memb_rfsSkill).i;
}

static int button_for_skill(int skill_no)
{
	int skill_slot, idx, status, btn;

	skill_slot = skill_page_slot();
	if (skill_slot < 0 || !heap_index_valid(skill_slot))
		return -1;
	idx = call_method_i(g_fno_getSkillIndex, skill_slot, skill_no);
	if (idx < 0)
		return -1;
	status = call_method_i(g_fno_getSkillStatus, skill_slot, idx);
	if (status <= 0)
		return -1;
	btn = call_method_i(g_fno_getButtonID, skill_slot, idx);
	return btn > 0 ? btn : -1;
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
	if (lock != 0)
		return 0;
	return 1;
}

/*
 * Ask Lua for a skill id to complete select with. 0 = vanilla.
 * Accepts integer, or { skill = N }.
 */
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

/* Resolve this-slot from the current method frame. */
static int current_struct_slot(void)
{
	struct page *lp = local_page();
	if (!lp)
		return -1;
	return lp->local.struct_ptr;
}

static void battle_select_enter(void)
{
	g_in_ally_select = 1;
	g_actor_no = local_get(0).i;
	g_this_slot = current_struct_slot();
	g_actor_chara = call_i(g_fno_iic, g_actor_no);
	g_space_latched = 0;
}

static void battle_select_exit(void)
{
	g_in_ally_select = 0;
	g_actor_no = -1;
	g_actor_chara = -1;
	g_this_slot = -1;
	g_space_latched = 0;
}

/*
 * Called at the start of T_BattleSelect@checkKey. Pushes button id (>0) to
 * complete select, or 0 to fall through to vanilla checkKey / checkCancel.
 */
static void battle_select_key(void)
{
	int skill, btn;

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
	btn = button_for_skill(skill);
	if (btn <= 0) {
		stack_push(0);
		return;
	}

	/* Swallow Space so キャンセル_確認 does not also fire this frame. */
	key_set_state(VK_SPACE, 0);
	g_space_latched = 1;
	stack_push(btn);
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
	uint32_t fallthrough;

	buffer_init(&out, NULL, 0);
	emit1(&out, FUNC, fno);
	emit1(&out, CALLSYS, VM_XSYS_BATTLE_SELECT_KEY);
	/* if result > 0: return it */
	emit0(&out, DUP);
	emit1(&out, PUSH, 0);
	emit0(&out, GT);
	ifz_at = out.index;
	emit1(&out, IFZ, 0);
	emit0(&out, RETURN);
	fallthrough = out.index;
	buffer_write_int32_at(&out, ifz_at + 2,
			      (uint32_t)(ain->code_size + fallthrough));
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
	default:
		return 0;
	}
}

int rq_battle_install(struct ain *ain)
{
	int f_select, f_key, st;

	if (g_on)
		return 1;
	if (!ain)
		return 0;

	f_select = find_fn(ain, "T_BattleSelect@select");
	f_key = find_fn(ain, "T_BattleSelect@checkKey");
	st = find_struct(ain, "T_BattleSelect");
	g_memb_rfsSkill = find_member(ain, st, "m_rfsSkill");
	g_fno_getSkillIndex = find_fn(ain, "T_BattleSkill@getSkillIndex");
	g_fno_getButtonID = find_fn(ain, "T_BattleSkill@getButtonID");
	g_fno_getSkillStatus = find_fn(ain, "T_BattleSkill@getSkillStatus");
	g_fno_iic = find_fn(ain, "戦闘番号_ＩＩＣ");
	g_fno_equip = find_fn(ain, "キャラスキル_実装中");
	g_fno_uses = find_fn(ain, "キャラスキル_使用回数");
	g_fno_lock = find_fn(ain, "キャラスキル_使用禁止");

	if (f_select < 0 || f_key < 0 || g_memb_rfsSkill < 0
	    || g_fno_getSkillIndex < 0 || g_fno_getButtonID < 0
	    || g_fno_getSkillStatus < 0 || g_fno_iic < 0
	    || g_fno_equip < 0 || g_fno_uses < 0 || g_fno_lock < 0) {
		rq_log("battle: missing AIN (select=%d checkKey=%d m_rfsSkill=%d "
		       "getSkillIndex=%d getButtonID=%d getSkillStatus=%d iic=%d "
		       "equip=%d uses=%d lock=%d)",
		       f_select, f_key, g_memb_rfsSkill, g_fno_getSkillIndex,
		       g_fno_getButtonID, g_fno_getSkillStatus, g_fno_iic,
		       g_fno_equip, g_fno_uses, g_fno_lock);
		return 0;
	}
	if (!wrap_select(ain, f_select) || !wrap_check_key(ain, f_key))
		return 0;

	g_on = 1;
	rq_log("battle: ally select hooked (Lua on_select_key; Space swallow on claim)");
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

	/* Prefer live battle-skill button when select is open. */
	if (g_in_ally_select && button_for_skill(skill_no) > 0)
		ok = 1;
	else
		ok = skill_usable_chara(chara, skill_no);

	lua_pushboolean(L, ok);
	return 1;
}

#endif /* RQ_MODLOADER */
