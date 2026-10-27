/* Copyright (C) 2026 Rance Quest modding fork
 *
 * rq.chara / rq.combat: Lua owns Spellblade (and any later skill mod).
 * C registers nothing by itself. It reads and writes slots, and calls Lua
 * from the end of Ｃ再計算, the end of Ｔ購入スキル登録, the Ｃ防御力
 * call inside 物理ダメージ計算 (attack value is already final, defense is
 * not yet subtracted), the aa-bb site in 命中計算, and the tail of
 * 最終ダメージ計算 (optional final-damage percent).
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

static int g_on;
static int g_recalc_ref = LUA_NOREF;
static int g_offer_ref = LUA_NOREF;
static int g_phys_ref = LUA_NOREF;
static int g_acc_ref = LUA_NOREF;

static int g_f_add = -1;
static int g_f_rank = -1;
static int g_f_slot = -1;
static int g_f_owned = -1;
static int g_f_cflavor = -1;
static int g_f_sflavor = -1;
static int g_f_weapon = -1;
static int g_f_detail = -1;
static int g_f_mod = -1;
static int g_f_offer = -1;
static int g_f_no = -1;
static int g_f_lv = -1;
static int g_f_class = -1;
static int g_f_def = -1;
static int g_f_magic = -1;
static int g_f_race = -1;

static int g_loc_paa = -1;
static int g_loc_pi1 = -1;
static int g_loc_pi2 = -1;
static int g_loc_pis = -1;
static int g_loc_haa = -1;
static int g_loc_hi1 = -1;
static int g_loc_hi2 = -1;
static int g_loc_his = -1;
static int g_gi_dm = -1;
static int g_gi_offers = -1;
static int g_gi_offer_pts = -1;
static int g_f_max_uses = -1;
static int g_f_uses = -1;
static int g_f_uses_max = -1;
static int g_f_hotbar = -1;
static int g_f_clear = -1;
static int g_final_pct = 100;

static uint16_t rd16(const uint8_t *p)
{
	return (uint16_t)(p[0] | (p[1] << 8));
}

static int32_t rd32(const uint8_t *p)
{
	return (int32_t)(p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24));
}

static void wr16(uint8_t *p, uint16_t v)
{
	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
}

static void wr32(uint8_t *p, int32_t v)
{
	uint32_t u = (uint32_t)v;
	p[0] = (uint8_t)u;
	p[1] = (uint8_t)(u >> 8);
	p[2] = (uint8_t)(u >> 16);
	p[3] = (uint8_t)(u >> 24);
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

static int find_global(struct ain *a, const char *utf8)
{
	char *sjis = utf2sjis(utf8, 0);
	int g;
	if (!sjis)
		return -1;
	g = ain_get_global(a, sjis);
	free(sjis);
	return g;
}

static int find_local(struct ain *a, int fno, const char *ascii)
{
	struct ain_function *f = &a->functions[fno];
	int i;
	for (i = 0; i < f->nr_vars; i++) {
		if (f->vars[i].name && !strcmp(f->vars[i].name, ascii))
			return i;
	}
	return -1;
}

static uint32_t func_end(struct ain *a, int fno)
{
	uint32_t start = a->functions[fno].address;
	uint32_t end = (uint32_t)a->code_size;
	int i;
	for (i = 0; i < a->nr_functions; i++) {
		uint32_t ad = a->functions[i].address;
		if (ad > start && ad < end)
			end = ad;
	}
	return end;
}

static int insn_len(const uint8_t *p)
{
	uint16_t op = rd16(p) & (uint16_t)~OPTYPE_MASK;
	int w;
	if (op >= NR_OPCODES)
		return 2;
	w = instruction_width((enum opcode)op);
	return w < 2 ? 2 : w;
}

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

/* Call original, then CALLSYS. String args get DUP/SP_INC so the copy's
 * page destructor does not free the wrapper's string. */
static int wrap_tail(struct ain *a, int fno, int sys, const char *tag)
{
	int dup = ain_dup_function(a, fno);
	int nargs = a->functions[fno].nr_args;
	int i;
	struct buffer out;

	buffer_init(&out, NULL, 0);
	emit1(&out, FUNC, fno);
	for (i = 0; i < nargs; i++) {
		emit1(&out, SH_LOCALREF, i);
		if (a->functions[fno].vars[i].type.data == AIN_STRING) {
			emit0(&out, DUP);
			emit0(&out, SP_INC);
		}
	}
	emit1(&out, CALLFUNC, dup);
	emit1(&out, CALLSYS, sys);
	emit0(&out, RETURN);
	emit1(&out, ENDFUNC, fno);
	commit_wrap(a, fno, &out);
	rq_log("%s", tag);
	return 1;
}

static int call_ints(int fno, const int *args, int n)
{
	int i;
	if (fno < 0)
		return 0;
	for (i = 0; i < n; i++)
		stack_push(args[i]);
	vm_call(fno, -1);
	if (ain->functions[fno].return_type.data == AIN_VOID)
		return 0;
	return stack_pop().i;
}

/* Ｓ表示文(id, "") — empty string reads, non-empty writes. */
static char *call_str_read(int fno, int id)
{
	int ret;
	struct string *sj;
	char *utf;
	if (fno < 0)
		return strdup("");
	stack_push(id);
	stack_push(vm_string_ref(string_ref(&EMPTY_STRING)));
	vm_call(fno, -1);
	ret = stack_pop().i;
	sj = heap_get_string(ret);
	utf = sjis2utf(sj && sj->text ? sj->text : "", 0);
	heap_unref(ret);
	if (!utf)
		return strdup("");
	return utf;
}

static void call_str_set(int fno, int a, int b, const char *utf)
{
	char *sj;
	struct string *s;
	int slot, ret;
	if (fno < 0)
		return;
	sj = utf2sjis(utf ? utf : "", 0);
	if (!sj)
		sj = strdup("");
	s = make_string(sj, strlen(sj));
	free(sj);
	stack_push(a);
	stack_push(b);
	if (!s) {
		stack_push(vm_string_ref(string_ref(&EMPTY_STRING)));
	} else {
		slot = heap_alloc_slot(VM_STRING);
		heap[slot].s = s;
		stack_push(slot);
	}
	vm_call(fno, -1);
	if (ain->functions[fno].return_type.data == AIN_VOID)
		return;
	ret = stack_pop().i;
	if (ain->functions[fno].return_type.data == AIN_STRING)
		heap_unref(ret);
}

static void set_local(int idx, int v)
{
	struct page *lp = local_page();
	if (lp)
		page_set_var(lp, idx, v);
}

static void chara_recalc(void)
{
	lua_State *L = rq_lua();
	int iic = local_get(0).i;
	if (!L || g_recalc_ref == LUA_NOREF)
		return;
	lua_rawgeti(L, LUA_REGISTRYINDEX, g_recalc_ref);
	lua_pushinteger(L, iic);
	if (lua_pcall(L, 1, 0, 0) != LUA_OK) {
		rq_log("chara on_recalc error: %s", lua_tostring(L, -1));
		lua_pop(L, 1);
	}
}

static void chara_offer(void)
{
	lua_State *L = rq_lua();
	int iic = local_get(0).i;
	int args[4];
	int skill, cost, level;

	if (!L || g_offer_ref == LUA_NOREF)
		return;
	lua_rawgeti(L, LUA_REGISTRYINDEX, g_offer_ref);
	lua_newtable(L);
	lua_pushinteger(L, iic);
	lua_setfield(L, -2, "iic");
	args[0] = iic;
	args[1] = -1;
	lua_pushinteger(L, call_ints(g_f_no, args, 2));
	lua_setfield(L, -2, "no");
	lua_pushinteger(L, call_ints(g_f_lv, args, 2));
	lua_setfield(L, -2, "level");
	lua_pushinteger(L, call_ints(g_f_class, args, 2));
	lua_setfield(L, -2, "class");
	if (lua_pcall(L, 1, 1, 0) != LUA_OK) {
		rq_log("chara on_offer error: %s", lua_tostring(L, -1));
		lua_pop(L, 1);
		return;
	}
	if (!lua_istable(L, -1)) {
		lua_pop(L, 1);
		return;
	}
	lua_getfield(L, -1, "skill");
	skill = lua_isnumber(L, -1) ? (int)lua_tointeger(L, -1) : 0;
	lua_pop(L, 1);
	lua_getfield(L, -1, "cost");
	cost = lua_isnumber(L, -1) ? (int)lua_tointeger(L, -1) : 0;
	lua_pop(L, 1);
	lua_getfield(L, -1, "level");
	level = lua_isnumber(L, -1) ? (int)lua_tointeger(L, -1) : 0;
	lua_pop(L, 1);
	lua_pop(L, 1);
	if (skill <= 0)
		return;
	args[0] = iic;
	args[1] = skill;
	args[2] = cost;
	args[3] = level;
	call_ints(g_f_offer, args, 4);
}

/* Stack holds Ｃ防御力's args. Pop them, let Lua replace local aa, call
 * defense, leave its return for the vanilla assign. */
static void combat_physical(void)
{
	lua_State *L = rq_lua();
	int n = g_f_def >= 0 ? ain->functions[g_f_def].nr_args : 0;
	int saved[8];
	int i;
	int args[2];

	if (n < 0 || n > 8)
		n = 0;
	for (i = n - 1; i >= 0; i--)
		saved[i] = stack_pop().i;

	g_final_pct = 100;
	if (L && g_phys_ref != LUA_NOREF && g_loc_paa >= 0) {
		args[0] = local_get(g_loc_pi1).i;
		args[1] = -1;
		lua_rawgeti(L, LUA_REGISTRYINDEX, g_phys_ref);
		lua_newtable(L);
		lua_pushinteger(L, args[0]);
		lua_setfield(L, -2, "attacker");
		lua_pushinteger(L, local_get(g_loc_pi2).i);
		lua_setfield(L, -2, "defender");
		lua_pushinteger(L, local_get(g_loc_pis).i);
		lua_setfield(L, -2, "skill");
		lua_pushinteger(L, local_get(g_loc_paa).i);
		lua_setfield(L, -2, "attack");
		lua_pushinteger(L, call_ints(g_f_magic, args, 2));
		lua_setfield(L, -2, "magic");
		args[0] = local_get(g_loc_pi2).i;
		lua_pushinteger(L, call_ints(g_f_race, args, 2));
		lua_setfield(L, -2, "race");
		if (lua_pcall(L, 1, 2, 0) != LUA_OK) {
			rq_log("combat on_physical error: %s", lua_tostring(L, -1));
			lua_pop(L, 1);
		} else {
			if (lua_isnumber(L, -2))
				set_local(g_loc_paa, (int)lua_tointeger(L, -2));
			if (lua_isnumber(L, -1))
				g_final_pct = (int)lua_tointeger(L, -1);
			lua_pop(L, 2);
		}
	}

	for (i = 0; i < n; i++)
		stack_push(saved[i]);
	if (g_f_def >= 0)
		vm_call(g_f_def, -1);
}

static void combat_accuracy(void)
{
	lua_State *L = rq_lua();
	int delta = 0;
	int aa;

	if (L && g_acc_ref != LUA_NOREF && g_loc_haa >= 0) {
		lua_rawgeti(L, LUA_REGISTRYINDEX, g_acc_ref);
		lua_newtable(L);
		lua_pushinteger(L, local_get(g_loc_hi1).i);
		lua_setfield(L, -2, "attacker");
		lua_pushinteger(L, local_get(g_loc_hi2).i);
		lua_setfield(L, -2, "defender");
		lua_pushinteger(L, local_get(g_loc_his).i);
		lua_setfield(L, -2, "skill");
		if (lua_pcall(L, 1, 1, 0) != LUA_OK) {
			rq_log("combat on_accuracy error: %s", lua_tostring(L, -1));
			lua_pop(L, 1);
		} else {
			if (lua_isnumber(L, -1))
				delta = (int)lua_tointeger(L, -1);
			lua_pop(L, 1);
		}
		aa = local_get(g_loc_haa).i + delta;
	} else if (g_loc_haa >= 0) {
		aa = local_get(g_loc_haa).i;
	} else {
		aa = 0;
	}
	stack_push(aa);
}

static void combat_final(void)
{
	union vm_value v;
	int dm;
	if (g_gi_dm < 0 || g_final_pct == 100) {
		g_final_pct = 100;
		return;
	}
	dm = global_get(g_gi_dm).i;
	v.i = (int)((float)dm * ((float)g_final_pct / 100.0f));
	global_set(g_gi_dm, v, false);
	g_final_pct = 100;
}

int rq_chara_syscall(int code)
{
	switch ((enum vm_extra_syscall)code) {
	case VM_XSYS_CHARA_RECALC:
		chara_recalc();
		return 1;
	case VM_XSYS_CHARA_OFFER:
		chara_offer();
		return 1;
	case VM_XSYS_COMBAT_PHYSICAL:
		combat_physical();
		return 1;
	case VM_XSYS_COMBAT_ACCURACY:
		combat_accuracy();
		return 1;
	case VM_XSYS_COMBAT_FINAL:
		combat_final();
		return 1;
	default:
		return 0;
	}
}

static int patch_defense_call(struct ain *a, int fno, int def_fno)
{
	uint32_t start = a->functions[fno].address;
	uint32_t end = func_end(a, fno);
	uint32_t ip, hit = 0;
	int n = 0;

	for (ip = start; ip + 6 <= end; ) {
		int len = insn_len(a->code + ip);
		uint16_t op = rd16(a->code + ip) & (uint16_t)~OPTYPE_MASK;
		if (op == CALLFUNC && rd32(a->code + ip + 2) == def_fno) {
			n++;
			hit = ip;
		}
		ip += (uint32_t)len;
	}
	if (n != 1) {
		rq_log("combat: Ｃ防御力 calls in 物理ダメージ計算 = %d (want 1)", n);
		return 0;
	}
	wr16(a->code + hit, CALLSYS);
	wr32(a->code + hit + 2, VM_XSYS_COMBAT_PHYSICAL);
	rq_log("combat: 物理ダメージ計算 defense call -> on_physical @ %u", hit);
	return 1;
}

static int patch_accuracy_sub(struct ain *a, int fno, int loc_aa, int loc_bb)
{
	uint32_t start = a->functions[fno].address;
	uint32_t end = func_end(a, fno);
	uint32_t ip, hit = 0;
	int n = 0;

	for (ip = start; ip + 14 <= end; ) {
		int len = insn_len(a->code + ip);
		uint16_t op = rd16(a->code + ip) & (uint16_t)~OPTYPE_MASK;
		if (op == SH_LOCALREF && rd32(a->code + ip + 2) == loc_aa
		    && len == 6
		    && (rd16(a->code + ip + 6) & (uint16_t)~OPTYPE_MASK) == SH_LOCALREF
		    && rd32(a->code + ip + 8) == loc_bb
		    && (rd16(a->code + ip + 12) & (uint16_t)~OPTYPE_MASK) == SUB) {
			n++;
			hit = ip;
		}
		ip += (uint32_t)len;
	}
	if (n != 1) {
		rq_log("combat: aa-bb sites in 命中計算 = %d (want 1)", n);
		return 0;
	}
	wr16(a->code + hit, CALLSYS);
	wr32(a->code + hit + 2, VM_XSYS_COMBAT_ACCURACY);
	rq_log("combat: 命中計算 aa-bb -> on_accuracy @ %u", hit);
	return 1;
}

int rq_chara_install(struct ain *a)
{
	int f_recalc, f_offer, f_phys, f_hit, f_final;

	if (g_on)
		return 1;
	if (!a)
		return 0;

	f_recalc = find_fn(a, "Ｃ再計算");
	f_offer = find_fn(a, "Ｔ購入スキル登録");
	f_phys = find_fn(a, "物理ダメージ計算");
	f_hit = find_fn(a, "命中計算");
	f_final = find_fn(a, "最終ダメージ計算");
	g_f_add = find_fn(a, "ＣＳ追加");
	g_f_rank = find_fn(a, "ＣＳ有無／ランク");
	g_f_slot = find_fn(a, "ＣＳ番号");
	g_f_owned = find_fn(a, "ＣＳ保有");
	g_f_cflavor = find_fn(a, "Ｃスキル表示文");
	g_f_sflavor = find_fn(a, "Ｓ表示文");
	g_f_weapon = find_fn(a, "Ｃ武器");
	g_f_detail = find_fn(a, "Ｉ種類詳細");
	g_f_mod = find_fn(a, "Ｃ機能適応");
	g_f_offer = find_fn(a, "スキルレベル制限表示");
	g_f_no = find_fn(a, "Ｃ番号");
	g_f_lv = find_fn(a, "ＣＬＶ");
	g_f_class = find_fn(a, "Ｃ職業");
	g_f_def = find_fn(a, "Ｃ防御力");
	g_f_magic = find_fn(a, "Ｃ魔法力");
	g_f_race = find_fn(a, "Ｃ種族");
	g_gi_dm = find_global(a, "◆戦闘／ＤＭ");
	g_gi_offers = find_global(a, "取得可能スキル");
	g_gi_offer_pts = find_global(a, "取得可能スキルポイント");
	g_f_max_uses = find_fn(a, "ＣＳ有無／最大回数");
	g_f_uses = find_fn(a, "ＣＳ使用回数");
	g_f_uses_max = find_fn(a, "ＣＳ使用回数最大");
	g_f_hotbar = find_fn(a, "ＣＳ実装中");
	g_f_clear = find_fn(a, "ＣＳ初期化");

	if (f_recalc < 0 || f_offer < 0 || f_phys < 0 || f_hit < 0 || f_final < 0
	    || g_f_add < 0 || g_f_rank < 0 || g_f_slot < 0 || g_f_owned < 0
	    || g_f_cflavor < 0 || g_f_sflavor < 0 || g_f_weapon < 0
	    || g_f_detail < 0 || g_f_mod < 0 || g_f_offer < 0 || g_f_no < 0
	    || g_f_lv < 0 || g_f_class < 0 || g_f_def < 0 || g_f_magic < 0
	    || g_f_race < 0 || g_gi_dm < 0 || g_gi_offers < 0
	    || g_gi_offer_pts < 0 || g_f_max_uses < 0
	    || g_f_uses < 0 || g_f_uses_max < 0 || g_f_hotbar < 0 || g_f_clear < 0) {
		rq_log("chara: missing AIN symbol (recalc=%d offer=%d phys=%d hit=%d final=%d dm=%d offers=%d pts=%d uses=%d)",
		       f_recalc, f_offer, f_phys, f_hit, f_final, g_gi_dm,
		       g_gi_offers, g_gi_offer_pts, g_f_max_uses);
		return 0;
	}

	g_loc_paa = find_local(a, f_phys, "aa");
	g_loc_pi1 = find_local(a, f_phys, "iic1");
	g_loc_pi2 = find_local(a, f_phys, "iic2");
	g_loc_pis = find_local(a, f_phys, "iis");
	g_loc_haa = find_local(a, f_hit, "aa");
	g_loc_hi1 = find_local(a, f_hit, "iic1");
	g_loc_hi2 = find_local(a, f_hit, "iic2");
	g_loc_his = find_local(a, f_hit, "iis");
	{
		int bb = find_local(a, f_hit, "bb");
		if (g_loc_paa < 0 || g_loc_haa < 0 || bb < 0) {
			rq_log("chara: local aa/bb missing");
			return 0;
		}
		if (!patch_defense_call(a, f_phys, g_f_def))
			return 0;
		if (!patch_accuracy_sub(a, f_hit, g_loc_haa, bb))
			return 0;
	}
	if (!wrap_tail(a, f_recalc, VM_XSYS_CHARA_RECALC,
		       "chara: Ｃ再計算 tail -> on_recalc"))
		return 0;
	if (!wrap_tail(a, f_offer, VM_XSYS_CHARA_OFFER,
		       "chara: Ｔ購入スキル登録 tail -> on_offer"))
		return 0;
	if (!wrap_tail(a, f_final, VM_XSYS_COMBAT_FINAL,
		       "combat: 最終ダメージ計算 tail -> final percent"))
		return 0;

	g_on = 1;
	rq_log("chara: grant/rank/slots/set_slot/set_flavor/weapon_detail/apply_mod ready");
	return 1;
}

static int bind_fn(lua_State *L, int *slot)
{
	luaL_checktype(L, 1, LUA_TFUNCTION);
	if (*slot != LUA_NOREF)
		luaL_unref(L, LUA_REGISTRYINDEX, *slot);
	lua_pushvalue(L, 1);
	*slot = luaL_ref(L, LUA_REGISTRYINDEX);
	return 0;
}

int rq_lua_chara_on_recalc(lua_State *L)
{
	return bind_fn(L, &g_recalc_ref);
}

int rq_lua_chara_on_offer(lua_State *L)
{
	return bind_fn(L, &g_offer_ref);
}

int rq_lua_combat_on_physical(lua_State *L)
{
	return bind_fn(L, &g_phys_ref);
}

int rq_lua_combat_on_accuracy(lua_State *L)
{
	return bind_fn(L, &g_acc_ref);
}

int rq_lua_chara_grant(lua_State *L)
{
	int args[3];
	args[0] = (int)luaL_checkinteger(L, 1);
	args[1] = (int)luaL_checkinteger(L, 2);
	args[2] = (int)luaL_checkinteger(L, 3);
	lua_pushinteger(L, call_ints(g_f_add, args, 3));
	return 1;
}

int rq_lua_chara_rank(lua_State *L)
{
	int args[3];
	args[0] = (int)luaL_checkinteger(L, 1);
	args[1] = (int)luaL_checkinteger(L, 2);
	args[2] = 0;
	lua_pushinteger(L, call_ints(g_f_rank, args, 3));
	return 1;
}

int rq_lua_chara_slots(lua_State *L)
{
	int iic = (int)luaL_checkinteger(L, 1);
	int slot, n = 0;
	int args[3];
	lua_newtable(L);
	for (slot = 1; slot <= 20; slot++) {
		int owned, skill;
		args[0] = iic;
		args[1] = slot;
		args[2] = -1;
		owned = call_ints(g_f_owned, args, 3);
		skill = call_ints(g_f_slot, args, 3);
		if (owned != 1 && skill == 0)
			continue;
		n++;
		lua_newtable(L);
		lua_pushinteger(L, slot);
		lua_setfield(L, -2, "slot");
		lua_pushinteger(L, skill);
		lua_setfield(L, -2, "skill");
		lua_pushinteger(L, call_ints(g_f_uses, args, 3));
		lua_setfield(L, -2, "uses");
		lua_pushinteger(L, call_ints(g_f_uses_max, args, 3));
		lua_setfield(L, -2, "max");
		lua_pushinteger(L, call_ints(g_f_hotbar, args, 3));
		lua_setfield(L, -2, "bar");
		lua_rawseti(L, -2, n);
	}
	return 1;
}

int rq_lua_chara_set_slot(lua_State *L)
{
	int args[3];
	args[0] = (int)luaL_checkinteger(L, 1);
	args[1] = (int)luaL_checkinteger(L, 2);
	args[2] = (int)luaL_checkinteger(L, 3);
	call_ints(g_f_slot, args, 3);
	return 0;
}

int rq_lua_chara_set_flavor(lua_State *L)
{
	int iic = (int)luaL_checkinteger(L, 1);
	int slot = (int)luaL_checkinteger(L, 2);
	int args[3];
	int skill;
	char *flavor;
	args[0] = iic;
	args[1] = slot;
	args[2] = -1;
	skill = call_ints(g_f_slot, args, 3);
	flavor = call_str_read(g_f_sflavor, skill);
	call_str_set(g_f_cflavor, iic, slot, flavor);
	free(flavor);
	return 0;
}

int rq_lua_chara_weapon_detail(lua_State *L)
{
	int iic = (int)luaL_checkinteger(L, 1);
	int args[2];
	int w;
	args[0] = iic;
	args[1] = -1;
	w = call_ints(g_f_weapon, args, 2);
	args[0] = w;
	args[1] = -1;
	lua_pushinteger(L, call_ints(g_f_detail, args, 2));
	return 1;
}

static struct page *global_array(int gi)
{
	int slot;
	struct page *arr;

	if (gi < 0)
		return NULL;
	slot = global_get(gi).i;
	if (!heap_index_valid(slot))
		return NULL;
	arr = heap_get_page(slot);
	if (!arr || arr->nr_vars <= 0)
		return NULL;
	return arr;
}

static struct page *offer_page(void)
{
	return global_array(g_gi_offers);
}

static void push_offer_row(lua_State *L, int *n, int index, int skill, int points)
{
	if (skill == 0)
		return;
	(*n)++;
	lua_newtable(L);
	lua_pushinteger(L, index);
	lua_setfield(L, -2, "index");
	lua_pushinteger(L, skill);
	lua_setfield(L, -2, "skill");
	lua_pushinteger(L, points);
	lua_setfield(L, -2, "points");
	lua_rawseti(L, -2, *n);
}

int rq_lua_chara_offers(lua_State *L)
{
	struct page *arr = offer_page();
	struct page *pts = global_array(g_gi_offer_pts);
	int i, n = 0;

	lua_newtable(L);
	/* Paged buy list reads its own cache. That is the list the buttons use. */
	if (rq_skilllist_active()) {
		for (i = 0; i < 144; i++) {
			int skill = 0, points = 0;
			if (!rq_skilllist_offer_at(i, &skill, &points))
				break;
			push_offer_row(L, &n, i, skill, points);
		}
		return 1;
	}
	if (!arr)
		return 1;
	for (i = 0; i < arr->nr_vars; i++) {
		int skill = page_get_var(arr, i).i;
		int points = 0;
		if (pts && i < pts->nr_vars)
			points = page_get_var(pts, i).i;
		push_offer_row(L, &n, i, skill, points);
	}
	return 1;
}

int rq_lua_chara_set_offer(lua_State *L)
{
	struct page *arr = offer_page();
	struct page *pts = global_array(g_gi_offer_pts);
	int index = (int)luaL_checkinteger(L, 1);
	int skill = (int)luaL_checkinteger(L, 2);
	int has_pts = lua_isnumber(L, 3);
	int points = has_pts ? (int)lua_tointeger(L, 3) : 0;
	int wrote = 0;

	if (arr && index >= 0 && index < arr->nr_vars) {
		page_set_var(arr, index, skill);
		wrote = 1;
		if (has_pts) {
			if (!pts || index >= pts->nr_vars)
				return luaL_error(L, "offer points missing");
			page_set_var(pts, index, points);
		}
	}
	if (rq_skilllist_note_offer(index, skill, has_pts, points))
		wrote = 1;
	if (!wrote)
		return luaL_error(L, "offer index out of range");
	return 0;
}

int rq_lua_chara_set_uses(lua_State *L)
{
	int args[3];
	args[0] = (int)luaL_checkinteger(L, 1);
	args[1] = (int)luaL_checkinteger(L, 2);
	args[2] = (int)luaL_checkinteger(L, 4);
	call_ints(g_f_uses_max, args, 3);
	args[2] = (int)luaL_checkinteger(L, 3);
	call_ints(g_f_uses, args, 3);
	return 0;
}

int rq_lua_chara_set_bar(lua_State *L)
{
	int args[3];
	args[0] = (int)luaL_checkinteger(L, 1);
	args[1] = (int)luaL_checkinteger(L, 2);
	args[2] = (int)luaL_checkinteger(L, 3);
	call_ints(g_f_hotbar, args, 3);
	return 0;
}

int rq_lua_chara_clear_slot(lua_State *L)
{
	int args[2];
	args[0] = (int)luaL_checkinteger(L, 1);
	args[1] = (int)luaL_checkinteger(L, 2);
	call_ints(g_f_clear, args, 2);
	return 0;
}

int rq_lua_chara_max_uses(lua_State *L)
{
	int args[3];
	args[0] = (int)luaL_checkinteger(L, 1);
	args[1] = (int)luaL_checkinteger(L, 2);
	args[2] = 0;
	lua_pushinteger(L, call_ints(g_f_max_uses, args, 3));
	return 1;
}

int rq_lua_chara_apply_mod(lua_State *L)
{
	int args[4];
	args[0] = (int)luaL_checkinteger(L, 1);
	args[1] = (int)luaL_checkinteger(L, 2);
	args[2] = (int)luaL_checkinteger(L, 3);
	args[3] = lua_isnumber(L, 4) ? (int)lua_tointeger(L, 4) : 100;
	call_ints(g_f_mod, args, 4);
	return 0;
}

#endif /* RQ_MODLOADER */
