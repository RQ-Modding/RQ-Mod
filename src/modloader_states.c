/* States at once: poison with stun, and ▲ with ▼ of one stat, on one character.
 *
 * Each of Ｃ状態変化, Ｃ攻撃付与 and Ｃ防御付与 holds one value, and the save format stays.
 * The field keeps the state that came first, and the second lives here, only during a
 * fight. Every read in the game has the form `Ｃ攻撃付与(iic, -1) == 1`, so a read goes to a
 * function that answers for that one value, and a clear right after such a read takes
 * only that side. */

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

#define KIND_STATUS 0
#define KIND_ATK 1
#define KIND_DEF 2
#define KINDS 3
#define POISON 1
#define STUN 2
#define DEAD 3
#define MAX_IIC 256
#define ASK_NEVER 8
#define ASK_POISON_OR_DEAD 9
#define CLEAR_WINDOW 200
#define WANT_CLEARS_STATUS 3
#define WANT_CLEARS_STAT 7

static int g_on;
static int g_base[KINDS], g_orig[KINDS], g_ask[KINDS][3], g_clear[KINDS][3];
static int g_never, g_poisonless;
static int g_fn_hp, g_fn_hp_max, g_fn_boss, g_gi_poison;
static char g_second[KINDS][MAX_IIC];

static const char *const g_names[KINDS] = { "Ｃ状態変化", "Ｃ攻撃付与", "Ｃ防御付与" };

static uint16_t rd16(const uint8_t *p)
{
	return (uint16_t)(p[0] | (p[1] << 8));
}

static int32_t rd32(const uint8_t *p)
{
	return (int32_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24));
}

static void wr32(uint8_t *p, int32_t v)
{
	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
	p[2] = (uint8_t)(v >> 16);
	p[3] = (uint8_t)(v >> 24);
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

static void commit(struct ain *a, int fno, struct buffer *out)
{
	a->functions[fno].address = (uint32_t)(a->code_size + 6);
	a->code = xrealloc(a->code, a->code_size + out->index);
	memcpy(a->code + a->code_size, out->buf, out->index);
	a->code_size += out->index;
}

static int fn_by_name(struct ain *a, const char *utf)
{
	char *sj = utf2sjis(utf, 0);
	int fno = sj ? ain_get_function(a, sj) : -1;

	free(sj);
	return fno;
}

static int call2(int fno, int a, int b)
{
	stack_push(a);
	stack_push(b);
	vm_call(fno, -1);
	return stack_pop().i;
}

static int field(int kind, int iic)
{
	return call2(g_orig[kind], iic, -1);
}

static int has(int kind, int iic, int k)
{
	int f = field(kind, iic);

	if (f == k)
		return 1;
	if (iic <= 0 || iic >= MAX_IIC || !g_second[kind][iic])
		return 0;
	if (kind == KIND_STATUS)
		return k == POISON && f == STUN;
	return (f == 1 || f == 2) && (k == 1 || k == 2);
}

/* What the field becomes when the game sets a state. */
static int on_set(int kind, int iic, int value)
{
	int f;

	if (value == -1 || iic <= 0 || iic >= MAX_IIC)
		return value;
	f = field(kind, iic);
	rq_battle_marks_changed();
	if (value != 1 && value != 2) {
		g_second[kind][iic] = 0;
		return value;
	}
	if (kind == KIND_STATUS) {
		if (f == POISON && value == STUN) {
			g_second[kind][iic] = 1;
			return STUN;
		}
		if (f == STUN && value == POISON) {
			g_second[kind][iic] = 1;
			return STUN;
		}
		return value;
	}
	if (f == 3 - value) {
		g_second[kind][iic] = 1;
		return f;
	}
	return value;
}

/* The turn a character only recovers from stun still does the poison's damage, as the
 * game's own start of a turn would, because the game returns before it gets there. */
static void poison_tick(int iic)
{
	int hp = call2(g_fn_hp, iic, -1);
	int max = call2(g_fn_hp_max, iic, -1);
	int dmg = max / (call2(g_fn_boss, iic, -1) == 1 ? 15 : 10);

	if (dmg > 999)
		dmg = 999;
	if (dmg >= hp)
		dmg = hp - 1;
	if (dmg <= 0)
		return;
	call2(g_fn_hp, iic, hp - dmg);
	if (g_gi_poison >= 0)
		global_set(g_gi_poison, (union vm_value) { .i = dmg }, false);
}

/* What the field becomes when only side k goes. */
static int on_clear(int kind, int iic, int k)
{
	int f = field(kind, iic), both;

	if (iic <= 0 || iic >= MAX_IIC)
		return f == k ? 0 : f;
	both = g_second[kind][iic];
	g_second[kind][iic] = 0;
	rq_battle_marks_changed();
	if (kind == KIND_STATUS) {
		if (k == POISON)
			return f == POISON ? 0 : f;
		if (f != STUN)
			return f;
		if (both) {
			poison_tick(iic);
			return POISON;
		}
		return 0;
	}
	if (f == k)
		return both ? 3 - k : 0;
	return f;
}

int rq_states_syscall(int code)
{
	int kind, k, iic, v;

	switch (code) {
	case VM_XSYS_STATE_SET:
		kind = stack_pop().i;
		v = stack_pop().i;
		iic = stack_pop().i;
		stack_push(on_set(kind, iic, v));
		return 1;
	case VM_XSYS_STATE_ASK:
		k = stack_pop().i;
		kind = stack_pop().i;
		iic = stack_pop().i;
		if (k == ASK_NEVER)
			stack_push(0);
		else if (k == ASK_POISON_OR_DEAD)
			stack_push(has(kind, iic, POISON) || has(kind, iic, DEAD));
		else
			stack_push(has(kind, iic, k) ? k : 0);
		return 1;
	case VM_XSYS_STATE_CLEAR:
		k = stack_pop().i;
		kind = stack_pop().i;
		iic = stack_pop().i;
		stack_push(on_clear(kind, iic, k));
		return 1;
	default:
		return 0;
	}
}

/* A function with the arguments of the accessor that answers for one value. */
static int make_ask(struct ain *a, int kind, int k)
{
	int fno = ain_dup_function(a, g_base[kind]);
	struct buffer out;

	buffer_init(&out, NULL, 0);
	emit1(&out, FUNC, fno);
	emit1(&out, SH_LOCALREF, 0);
	emit1(&out, PUSH, kind);
	emit1(&out, PUSH, k);
	emit1(&out, CALLSYS, VM_XSYS_STATE_ASK);
	emit0(&out, RETURN);
	emit1(&out, ENDFUNC, fno);
	commit(a, fno, &out);
	return fno;
}

static int make_clear(struct ain *a, int kind, int k)
{
	int fno = ain_dup_function(a, g_base[kind]);
	struct buffer out;

	buffer_init(&out, NULL, 0);
	emit1(&out, FUNC, fno);
	emit1(&out, SH_LOCALREF, 0);
	emit1(&out, SH_LOCALREF, 0);
	emit1(&out, PUSH, kind);
	emit1(&out, PUSH, k);
	emit1(&out, CALLSYS, VM_XSYS_STATE_CLEAR);
	emit1(&out, CALLFUNC, g_orig[kind]);
	emit0(&out, RETURN);
	emit1(&out, ENDFUNC, fno);
	commit(a, fno, &out);
	return fno;
}

static void wrap_setter(struct ain *a, int kind)
{
	int fno = g_base[kind];
	struct buffer out;

	buffer_init(&out, NULL, 0);
	emit1(&out, FUNC, fno);
	emit1(&out, SH_LOCALREF, 0);
	emit1(&out, SH_LOCALREF, 0);
	emit1(&out, SH_LOCALREF, 1);
	emit1(&out, PUSH, kind);
	emit1(&out, CALLSYS, VM_XSYS_STATE_SET);
	emit1(&out, CALLFUNC, g_orig[kind]);
	emit0(&out, RETURN);
	emit1(&out, ENDFUNC, fno);
	commit(a, fno, &out);
}

static int kind_of(int fno)
{
	int i;

	for (i = 0; i < KINDS; i++) {
		if (g_base[i] == fno)
			return i;
	}
	return -1;
}

/* Sends the game's reads and its clears of one side to the new functions. */
static void retarget(struct ain *a, int f_poison, int *reads, int *clears)
{
	uint32_t ip, code_end = a->code_size, prev = 0;
	int have_prev = 0, cur = -1, last_k[KINDS] = { 0 }, i;
	uint32_t last_ip[KINDS] = { 0 };

	for (ip = 0; ip + 2 <= code_end; ) {
		uint8_t *p = a->code + ip;
		uint16_t op = rd16(p) & (uint16_t)~OPTYPE_MASK;
		int len = insn_len(p);

		if (op == FUNC) {
			cur = rd32(p + 2);
			for (i = 0; i < KINDS; i++)
				last_k[i] = 0;
		} else if (op == CALLFUNC && have_prev && (rd16(a->code + prev) & (uint16_t)~OPTYPE_MASK) == PUSH) {
			int kind = kind_of(rd32(p + 2));
			int v = rd32(a->code + prev + 2);

			if (kind >= 0 && v == -1) {
				uint8_t *n = p + len;
				uint16_t cmp = rd16(n + insn_len(n)) & (uint16_t)~OPTYPE_MASK;
				int compared = (rd16(n) & (uint16_t)~OPTYPE_MASK) == PUSH && (cmp == EQUALE || cmp == NOTE);
				int k = compared ? rd32(n + 2) : -1;

				last_k[kind] = cmp == EQUALE ? k : 0;
				last_ip[kind] = ip;
				if (compared && kind == KIND_STATUS && cur == f_poison && k == STUN) {
					wr32(p + 2, g_never);
					reads[kind]++;
				} else if (compared && kind == KIND_STATUS && cur == f_poison && k == 0) {
					wr32(p + 2, g_poisonless);
					reads[kind]++;
				} else if (compared && (k == 1 || k == 2) && (kind != KIND_STATUS || k == POISON)) {
					wr32(p + 2, g_ask[kind][k]);
					reads[kind]++;
				}
			} else if (kind >= 0 && v == 0 && (last_k[kind] == 1 || last_k[kind] == 2)
				   && ip - last_ip[kind] < CLEAR_WINDOW) {
				wr32(p + 2, g_clear[kind][last_k[kind]]);
				clears[kind]++;
			}
		}
		prev = ip;
		have_prev = 1;
		ip += (uint32_t)len;
	}
}

static int states_install(struct ain *a)
{
	int i, k, f_poison, reads[KINDS] = { 0 }, clears[KINDS] = { 0 };
	char *sj;

	if (g_on)
		return 1;
	if (!a || !rq_battle_marks_install(a))
		return 0;
	f_poison = fn_by_name(a, "毒計算");
	g_fn_hp = fn_by_name(a, "Ｃ体力");
	g_fn_hp_max = fn_by_name(a, "Ｃ体力最大");
	g_fn_boss = fn_by_name(a, "Ｃボス");
	for (i = 0; i < KINDS; i++) {
		g_base[i] = fn_by_name(a, g_names[i]);
		if (g_base[i] < 0) {
			rq_log("states: no %s", g_names[i]);
			return 0;
		}
	}
	if (f_poison < 0 || g_fn_hp < 0 || g_fn_hp_max < 0 || g_fn_boss < 0) {
		rq_log("states: a game function is missing");
		return 0;
	}
	sj = utf2sjis("◆毒ダメージ有り", 0);
	g_gi_poison = sj ? ain_get_global(a, sj) : -1;
	free(sj);

	for (i = 0; i < KINDS; i++) {
		g_orig[i] = ain_dup_function(a, g_base[i]);
		for (k = 1; k <= 2; k++) {
			g_ask[i][k] = make_ask(a, i, k);
			g_clear[i][k] = make_clear(a, i, k);
		}
	}
	g_never = make_ask(a, KIND_STATUS, ASK_NEVER);
	g_poisonless = make_ask(a, KIND_STATUS, ASK_POISON_OR_DEAD);
	retarget(a, f_poison, reads, clears);
	for (i = 0; i < KINDS; i++)
		wrap_setter(a, i);
	rq_log("states: reads %d %d %d, clears %d %d %d", reads[0], reads[1], reads[2], clears[0], clears[1], clears[2]);
	if (clears[KIND_STATUS] != WANT_CLEARS_STATUS || clears[KIND_ATK] != WANT_CLEARS_STAT
	    || clears[KIND_DEF] != WANT_CLEARS_STAT)
		rq_log("states: WARNING: expected clears %d %d %d", WANT_CLEARS_STATUS, WANT_CLEARS_STAT, WANT_CLEARS_STAT);
	g_on = 1;
	return 1;
}

void rq_states_forget(void)
{
	memset(g_second, 0, sizeof g_second);
}

/* The marks of a card that the game's stack has no room for: one per stat at most. */
int rq_states_second_marks(int iic, int out[KINDS])
{
	int n = 0, f;

	if (!g_on || iic <= 0 || iic >= MAX_IIC)
		return 0;
	out[0] = out[1] = out[2] = 0;
	f = field(KIND_ATK, iic);
	if (g_second[KIND_ATK][iic] && (f == 1 || f == 2))
		out[0] = 3 - f, n++;
	f = field(KIND_DEF, iic);
	if (g_second[KIND_DEF][iic] && (f == 1 || f == 2))
		out[1] = 10 + 3 - f, n++;
	if (g_second[KIND_STATUS][iic] && field(KIND_STATUS, iic) == STUN)
		out[2] = 21, n++;
	return n;
}

int rq_lua_battle_install_states(lua_State *L)
{
	if (!ain || !states_install(ain))
		return luaL_error(L, "battle.install_states failed (see logs/rqmod.log)");
	lua_pushboolean(L, 1);
	return 1;
}

static const struct {
	const char *name;
	int kind, k;
} g_state_names[] = {
	{ "poison", KIND_STATUS, POISON }, { "stun", KIND_STATUS, STUN },
	{ "atk_up", KIND_ATK, 1 }, { "atk_down", KIND_ATK, 2 },
	{ "def_up", KIND_DEF, 1 }, { "def_down", KIND_DEF, 2 },
};

/* states(iic): every state of that character, also without install_states. */
int rq_lua_battle_states(lua_State *L)
{
	int iic = (int)luaL_checkinteger(L, 1);
	size_t i;

	if (!ain)
		return luaL_error(L, "battle.states: no game loaded");
	if (iic <= 0 || iic >= MAX_IIC) {
		lua_pushnil(L);
		return 1;
	}
	if (!g_on) {
		for (i = 0; i < KINDS; i++)
			g_orig[i] = fn_by_name(ain, g_names[i]);
	}
	lua_newtable(L);
	for (i = 0; i < sizeof g_state_names / sizeof g_state_names[0]; i++) {
		lua_pushboolean(L, has(g_state_names[i].kind, iic, g_state_names[i].k));
		lua_setfield(L, -2, g_state_names[i].name);
	}
	lua_pushboolean(L, field(KIND_STATUS, iic) == DEAD);
	lua_setfield(L, -2, "dead");
	return 1;
}

/* set_state(iic, name, on): the same rules as the game's own writes. */
int rq_lua_battle_set_state(lua_State *L)
{
	int iic = (int)luaL_checkinteger(L, 1);
	const char *name = luaL_checkstring(L, 2);
	int on = lua_toboolean(L, 3);
	size_t i;

	if (!g_on)
		return luaL_error(L, "call rq.battle.install_states() first");
	for (i = 0; i < sizeof g_state_names / sizeof g_state_names[0]; i++) {
		int kind = g_state_names[i].kind, k = g_state_names[i].k;

		if (strcmp(g_state_names[i].name, name))
			continue;
		if (iic <= 0 || iic >= MAX_IIC || field(KIND_STATUS, iic) == DEAD) {
			lua_pushboolean(L, 0);
			return 1;
		}
		if (on)
			call2(g_base[kind], iic, k);
		else if (has(kind, iic, k))
			call2(g_clear[kind][k], iic, 0);
		rq_battle_recheck_card(iic);
		lua_pushboolean(L, 1);
		return 1;
	}
	return luaL_error(L, "battle.set_state: unknown state '%s'", name);
}
