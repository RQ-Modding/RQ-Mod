/* Copyright (C) 2026 Rance Quest modding fork
 *
 * Immediate-mode in-game HUD primitives for Lua mods.
 * Drawn on top of Chipmunk/SACT after scene_render.
 */

#ifdef RQ_MODLOADER

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

#include <SDL.h>

#include "lua.h"
#include "lauxlib.h"

#include "system4.h"
#include "system4/ain.h"
#include "system4/buffer.h"
#include "system4/cg.h"
#include "system4/instructions.h"
#include "system4/string.h"
#include "system4/utfsjis.h"

#include "asset_manager.h"
#include "gfx/gfx.h"
#include "gfx/font.h"
#include "input.h"
#include "modloader.h"
#include "modloader_priv.h"
#include "vm.h"
#include "xsystem4.h"

#define HUD_MAX_CMD 480
#define HUD_MAX_CG 160

enum hud_kind {
	HUD_RECT = 1,
	HUD_CG,
	HUD_TEXT,
	HUD_CLIP
};

/* Text alignment inside the box bw, bh (HUD_TEXT). */
enum { ALIGN_START, ALIGN_CENTER, ALIGN_END };

struct hud_cmd {
	int kind;
	int x, y, w, h;
	int r, g, b, a;
	int cg;
	float size;
	char *text;
	/* HUD_CG: source part of the CG; sw == 0 means the whole CG. */
	int sx, sy, sw, sh;
	/* HUD_TEXT: the box the text is aligned in, and no dark edge. */
	int bw, bh, align, valign, no_edge, bold;
};

/* sprite.f.glsl: the texture times multiply_color, alpha times blend_rate. */
static struct hud_shader {
	Shader s;
	GLint blend_rate;
	GLint multiply_color;
	int loaded;
} g_shader;

struct hud_tint {
	float r, g, b, a;
};

struct hud_cg {
	int no;
	Texture t;
};

#define MODAL_CONFIG 1
#define MODAL_RESULT 2
#define MODAL_RESULT_ITEM 4
#define MODAL_ADV 8
#define MODAL_SELECT 16
#define MODAL_N 5

static int g_dungeon;
static int g_shown;
static int g_suspended;
static int g_battle;
static int g_profile;
static int g_modal_n[MODAL_N];
static int g_block_mouse;
static int g_block_keys;
static int g_text_on;
static int g_text_max;
static char g_text[256];
#define HUD_MAX_FRAME_CB 32
static int g_on_frame_ref[HUD_MAX_FRAME_CB];
static int g_on_frame_n;
static int g_mx, g_my, g_left, g_right, g_left_edge, g_left_up, g_prev_left;
static int g_scroll, g_wheel_seen;
extern int rq_wheel_steps;
void mouse_clear_wheel(void);
static int g_ncmd;
static int g_drew;
static struct hud_cmd g_cmd[HUD_MAX_CMD];
static struct hud_cg g_cg[HUD_MAX_CG];
static int g_ncg;

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

static void commit_wrap(struct ain *ain, int fno, struct buffer *out)
{
	ain->functions[fno].address = (uint32_t)(ain->code_size + 6);
	ain->code = xrealloc(ain->code, ain->code_size + out->index);
	memcpy(ain->code + ain->code_size, out->buf, out->index);
	ain->code_size += out->index;
}

static int wrap_method_flag(struct ain *ain, int fno, int sys, int before)
{
	int dup = ain_dup_function(ain, fno);
	int nargs = ain->functions[fno].nr_args;
	int i;
	struct buffer out;

	buffer_init(&out, NULL, 0);
	emit1(&out, FUNC, fno);
	if (before)
		emit1(&out, CALLSYS, sys);
	/* CALLMETHOD pops nr_args, then the struct page under them. */
	emit0(&out, PUSHSTRUCTPAGE);
	for (i = 0; i < nargs; i++)
		emit1(&out, SH_LOCALREF, i);
	emit1(&out, CALLMETHOD, dup);
	if (!before)
		emit1(&out, CALLSYS, sys);
	emit0(&out, RETURN);
	emit1(&out, ENDFUNC, fno);
	commit_wrap(ain, fno, &out);
	if (g_rq_verbose)
		rq_log("HUD: wrapped method fno=%d (%d args)", fno, nargs);
	return 1;
}

static int wrap_func_flag(struct ain *ain, int fno, int sys, int before)
{
	int dup = ain_dup_function(ain, fno);
	int nargs = ain->functions[fno].nr_args;
	int i;
	struct buffer out;

	buffer_init(&out, NULL, 0);
	emit1(&out, FUNC, fno);
	if (before)
		emit1(&out, CALLSYS, sys);
	for (i = 0; i < nargs; i++)
		emit1(&out, SH_LOCALREF, i);
	emit1(&out, CALLFUNC, dup);
	if (!before)
		emit1(&out, CALLSYS, sys);
	emit0(&out, RETURN);
	emit1(&out, ENDFUNC, fno);
	commit_wrap(ain, fno, &out);
	if (g_rq_verbose)
		rq_log("HUD: wrapped function fno=%d (%d args)", fno, nargs);
	return 1;
}

static int wrap_func_around(struct ain *ain, int fno, int sys_on, int sys_off)
{
	int dup = ain_dup_function(ain, fno);
	int nargs = ain->functions[fno].nr_args;
	int i;
	struct buffer out;

	buffer_init(&out, NULL, 0);
	emit1(&out, FUNC, fno);
	emit1(&out, CALLSYS, sys_on);
	for (i = 0; i < nargs; i++)
		emit1(&out, SH_LOCALREF, i);
	emit1(&out, CALLFUNC, dup);
	emit1(&out, CALLSYS, sys_off);
	emit0(&out, RETURN);
	emit1(&out, ENDFUNC, fno);
	commit_wrap(ain, fno, &out);
	if (g_rq_verbose)
		rq_log("HUD: wrapped function around fno=%d", fno);
	return 1;
}

static int wrap_set_show(struct ain *ain, int fno)
{
	int dup = ain_dup_function(ain, fno);
	struct buffer out;

	buffer_init(&out, NULL, 0);
	emit1(&out, FUNC, fno);
	emit0(&out, PUSHSTRUCTPAGE);
	emit1(&out, SH_LOCALREF, 0);
	emit1(&out, CALLMETHOD, dup);
	emit1(&out, SH_LOCALREF, 0);
	emit1(&out, CALLSYS, VM_XSYS_HUD_SHOW);
	emit0(&out, RETURN);
	emit1(&out, ENDFUNC, fno);
	commit_wrap(ain, fno, &out);
	rq_log("HUD: watching T_MapParts@setShow so Item Previews hide when the map HUD is hidden");
	return 1;
}

static int wrap_method_modal(struct ain *ain, int fno, int mask, int on)
{
	int dup = ain_dup_function(ain, fno);
	int nargs = ain->functions[fno].nr_args;
	int i;
	struct buffer out;
	int sys = on ? VM_XSYS_HUD_MODAL_ON : VM_XSYS_HUD_MODAL_OFF;

	buffer_init(&out, NULL, 0);
	emit1(&out, FUNC, fno);
	emit0(&out, PUSHSTRUCTPAGE);
	for (i = 0; i < nargs; i++)
		emit1(&out, SH_LOCALREF, i);
	emit1(&out, CALLMETHOD, dup);
	emit1(&out, PUSH, mask);
	emit1(&out, CALLSYS, sys);
	emit0(&out, RETURN);
	emit1(&out, ENDFUNC, fno);
	commit_wrap(ain, fno, &out);
	if (g_rq_verbose)
		rq_log("HUD: wrapped create/delete fno=%d mask=%d on=%d", fno, mask, on);
	return 1;
}

/* Blocking @select/@wait: ON for the call, OFF when it returns. Do not wrap
 * create/delete of T_Select / T_MapMessage — those objects stay alive on the
 * map, so a leftover bit kept exploring() false on real map-walk. */
static int wrap_modal_around(struct ain *ain, int fno, int mask, int method)
{
	int dup = ain_dup_function(ain, fno);
	int nargs = ain->functions[fno].nr_args;
	int i;
	struct buffer out;

	buffer_init(&out, NULL, 0);
	emit1(&out, FUNC, fno);
	emit1(&out, PUSH, mask);
	emit1(&out, CALLSYS, VM_XSYS_HUD_MODAL_ON);
	if (method)
		emit0(&out, PUSHSTRUCTPAGE);
	for (i = 0; i < nargs; i++)
		emit1(&out, SH_LOCALREF, i);
	if (method)
		emit1(&out, CALLMETHOD, dup);
	else
		emit1(&out, CALLFUNC, dup);
	emit1(&out, PUSH, mask);
	emit1(&out, CALLSYS, VM_XSYS_HUD_MODAL_OFF);
	emit0(&out, RETURN);
	emit1(&out, ENDFUNC, fno);
	commit_wrap(ain, fno, &out);
	return 1;
}

static void wrap_blocking(struct ain *ain, const char *name, int mask, int important)
{
	int fno = find_fn(ain, name);
	int method;
	if (fno < 0) {
		if (important || g_rq_verbose)
			rq_log("HUD: %s is not in this AIN, so it was not wrapped", name);
		return;
	}
	method = strchr(name, '@') != NULL;
	wrap_modal_around(ain, fno, mask, method);
	rq_log("HUD: wrapped %s — Item Previews hide while that screen runs", name);
}

static void wrap_lifetime(struct ain *ain, const char *cls, int mask)
{
	char buf[160];
	int c, d;

	snprintf(buf, sizeof buf, "%s@create", cls);
	c = find_fn(ain, buf);
	if (c >= 0)
		wrap_method_modal(ain, c, mask, 1);
	snprintf(buf, sizeof buf, "%s@delete", cls);
	d = find_fn(ain, buf);
	if (d >= 0)
		wrap_method_modal(ain, d, mask, 0);
	if (c >= 0 || d >= 0)
		rq_log("HUD: watching %s create/delete so Item Previews hide on the result screen", cls);
	else if (g_rq_verbose)
		rq_log("HUD: %s is not in this AIN (result overlay skipped)", cls);
}

static void hud_make_ts(struct text_style *ts, float size, int r, int g, int b);

/* Without the dark edge, as the game's own tab labels. */
static void hud_no_edge(struct text_style *ts)
{
	ts->edge_left = ts->edge_up = ts->edge_right = ts->edge_down = 0.0f;
}

/* The weight the game's own baked labels look like (as the Discord row on Other 2). */
static void hud_bold(struct text_style *ts)
{
	ts->weight = FW_BOLD;
	ts->bold_width = 0.45f;
}

static void hud_make_ts(struct text_style *ts, float size, int r, int g, int b)
{
	memset(ts, 0, sizeof(*ts));
	ts->face = FONT_HUD;
	ts->size = size > 1.0f ? size : 16.0f;
	ts->weight = FW_NORMAL;
	ts->edge_left = 1.0f;
	ts->edge_up = 1.0f;
	ts->edge_right = 1.0f;
	ts->edge_down = 1.0f;
	ts->color.r = (uint8_t)r;
	ts->color.g = (uint8_t)g;
	ts->color.b = (uint8_t)b;
	ts->color.a = 255;
	ts->edge_color.a = 255;
	ts->scale_x = 1.0f;
	ts->space_scale_x = 1.0f;
}

/* For rq.image.compose: the text as rq.hud.text draws it, read back from GL. */
unsigned char *rq_hud_text_rgba(const char *utf8, float size, int r, int g, int b, int edge,
				int bold, int *w, int *h, int *text_w)
{
	struct text_style ts;
	Texture t;
	char *sjis;
	unsigned char *px;
	GLboolean scissor;

	sjis = utf2sjis(utf8 ? utf8 : "", 0);
	if (!sjis)
		return NULL;
	hud_make_ts(&ts, size, r, g, b);
	if (!edge)
		hud_no_edge(&ts);
	if (bold)
		hud_bold(&ts);
	*text_w = (int)ceilf(gfx_size_text(&ts, sjis));
	*w = *text_w + 8 < 32 ? 32 : *text_w + 8;
	*h = (int)ts.size + 20;
	scissor = glIsEnabled(GL_SCISSOR_TEST);
	if (scissor)
		glDisable(GL_SCISSOR_TEST);
	gfx_init_texture_rgba(&t, *w, *h, (SDL_Color){ 0, 0, 0, 0 });
	gfx_render_text(&t, 0.0f, 0, sjis, &ts, false);
	px = gfx_get_pixels(&t);
	gfx_delete_texture(&t);
	if (scissor)
		glEnable(GL_SCISSOR_TEST);
	free(sjis);
	return px;
}

static void clear_cmds(void)
{
	int i;
	for (i = 0; i < g_ncmd; i++)
		free(g_cmd[i].text);
	g_ncmd = 0;
}

static struct hud_cmd *push_cmd(int kind)
{
	struct hud_cmd *c;
	if (g_ncmd >= HUD_MAX_CMD)
		return NULL;
	c = &g_cmd[g_ncmd++];
	memset(c, 0, sizeof(*c));
	c->kind = kind;
	g_drew = 1;
	return c;
}

static Texture *cg_tex(int no)
{
	int i;
	struct cg *cg;
	if (no <= 0)
		return NULL;
	for (i = 0; i < g_ncg; i++) {
		if (g_cg[i].no == no)
			return &g_cg[i].t;
	}
	if (g_ncg >= HUD_MAX_CG)
		return NULL;
	cg = asset_cg_load(no);
	if (!cg)
		return NULL;
	g_cg[g_ncg].no = no;
	gfx_init_texture_with_cg(&g_cg[g_ncg].t, cg);
	cg_free(cg);
	return &g_cg[g_ncg++].t;
}

static int hit(int x, int y, int w, int h)
{
	return g_mx >= x && g_my >= y && g_mx < x + w && g_my < y + h;
}

static int modal_index(int mask)
{
	switch (mask) {
	case MODAL_CONFIG: return 0;
	case MODAL_RESULT: return 1;
	case MODAL_RESULT_ITEM: return 2;
	case MODAL_ADV: return 3;
	case MODAL_SELECT: return 4;
	default: return -1;
	}
}

static int modal_any(void)
{
	int i;
	for (i = 0; i < MODAL_N; i++) {
		if (g_modal_n[i] > 0)
			return 1;
	}
	return 0;
}

static void modal_clear(void)
{
	memset(g_modal_n, 0, sizeof g_modal_n);
}

static const char *modal_reason(void)
{
	if (g_modal_n[0] > 0)
		return "Game Config is open";
	if (g_modal_n[1] > 0)
		return "the dungeon result screen is open";
	if (g_modal_n[2] > 0)
		return "the appraisal / identify screen is open";
	if (g_modal_n[3] > 0)
		return "a dungeon dialog / ADV is running";
	if (g_modal_n[4] > 0)
		return "a choice dialog is open";
	return "an overlay is open";
}

int rq_dungeon_active(void)
{
	return g_dungeon;
}

int rq_dungeon_exploring(void)
{
	return g_dungeon && g_shown && !g_suspended && !g_battle && !g_profile && !modal_any();
}

static void log_exploring_change(void)
{
	static int prev = -1;
	static int prev_d, prev_sh, prev_su, prev_b, prev_p, prev_m[MODAL_N];
	int now, i, changed;

	if (!g_rq_verbose && !g_rq_terminal)
		return;
	now = rq_dungeon_exploring() ? 1 : 0;
	changed = now != prev || prev_d != g_dungeon || prev_sh != g_shown
		|| prev_su != g_suspended || prev_b != g_battle || prev_p != g_profile;
	for (i = 0; i < MODAL_N; i++) {
		if (prev_m[i] != g_modal_n[i])
			changed = 1;
	}
	if (!changed)
		return;
	prev = now;
	prev_d = g_dungeon;
	prev_sh = g_shown;
	prev_su = g_suspended;
	prev_b = g_battle;
	prev_p = g_profile;
	memcpy(prev_m, g_modal_n, sizeof prev_m);
	if (now) {
		rq_log("Item Previews: map-walk (button can show)");
		return;
	}
	if (!g_dungeon)
		rq_log("Item Previews hidden: not in a dungeon");
	else if (!g_shown)
		rq_log("Item Previews hidden: the map HUD is hidden");
	else if (g_suspended)
		rq_log("Item Previews hidden: the map is suspended (join dialog / event)");
	else if (g_battle)
		rq_log("Item Previews hidden: battle");
	else if (g_profile)
		rq_log("Item Previews hidden: character profile");
	else if (modal_any())
		rq_log("Item Previews hidden: %s", modal_reason());
	else
		rq_log("Item Previews hidden: exploring is false");
}

int rq_hud_block_mouse(void)
{
	return g_block_mouse;
}

int rq_hud_block_keys(void)
{
	return g_block_keys;
}

/* Text entry for mod dialogs (rename). Printable text only, and only what the
 * game's Gothic face can draw (JIS X 0208 plus ASCII). */
static void text_on_input(const char *utf8)
{
	char *sj, *back;
	size_t len, cur;
	int ok;
	if (!g_text_on || !utf8 || !*utf8)
		return;
	if ((unsigned char)utf8[0] < 0x20)
		return;
	sj = utf2sjis(utf8, 0);
	if (!sj)
		return;
	back = sjis2utf(sj, 0);
	ok = back && !strcmp(back, utf8);
	free(back);
	free(sj);
	if (!ok)
		return;
	len = strlen(utf8);
	cur = strlen(g_text);
	if (cur + len > (size_t)g_text_max || cur + len >= sizeof g_text)
		return;
	memcpy(g_text + cur, utf8, len + 1);
}

static void text_on_key(int code)
{
	size_t n;
	if (!g_text_on || code != VK_BACK)
		return;
	n = strlen(g_text);
	if (!n)
		return;
	n--;
	while (n > 0 && ((unsigned char)g_text[n] & 0xC0) == 0x80)
		n--;
	g_text[n] = 0;
}

int rq_lua_hud_text_begin(lua_State *L)
{
	const char *init = luaL_optstring(L, 1, "");
	int max = (int)luaL_optinteger(L, 2, 40);
	if (max < 1)
		max = 1;
	if (max > (int)sizeof g_text - 1)
		max = (int)sizeof g_text - 1;
	g_text_max = max;
	snprintf(g_text, sizeof g_text, "%.*s", max, init);
	if (!g_text_on) {
		register_input_handler(text_on_input);
		register_key_handler(text_on_key);
		SDL_StartTextInput();
		g_text_on = 1;
	}
	return 0;
}

int rq_lua_hud_text_value(lua_State *L)
{
	lua_pushstring(L, g_text);
	lua_pushboolean(L, g_text_on);
	return 2;
}

int rq_lua_hud_text_end(lua_State *L)
{
	(void)L;
	if (g_text_on) {
		SDL_StopTextInput();
		clear_input_handler();
		clear_key_handler();
		g_text_on = 0;
	}
	return 0;
}

int rq_lua_hud_block_keys(lua_State *L)
{
	(void)L;
	g_block_keys = 1;
	return 0;
}

/* The title ends the map, a fight and every window the game had open. */
void rq_hud_after_reset(void)
{
	g_dungeon = 0;
	g_shown = 0;
	g_suspended = 0;
	g_battle = 0;
	g_profile = 0;
	modal_clear();
	g_block_mouse = 0;
}

int rq_hud_syscall(int code)
{
	int i;
	switch ((enum vm_extra_syscall)code) {
	case VM_XSYS_HUD_ON:
		g_dungeon = 1;
		g_shown = 1;
		g_suspended = 0;
		g_battle = 0;
		g_profile = 0;
		modal_clear();
		log_exploring_change();
		return 1;
	case VM_XSYS_HUD_OFF:
		g_dungeon = 0;
		g_shown = 0;
		g_suspended = 0;
		g_battle = 0;
		g_profile = 0;
		modal_clear();
		g_block_mouse = 0;
		log_exploring_change();
		return 1;
	case VM_XSYS_HUD_SUSPEND:
		g_suspended = 1;
		log_exploring_change();
		return 1;
	case VM_XSYS_HUD_RESUME:
		g_suspended = 0;
		log_exploring_change();
		return 1;
	case VM_XSYS_HUD_BATTLE_ON:
		g_battle = 1;
		log_exploring_change();
		return 1;
	case VM_XSYS_HUD_BATTLE_OFF:
		g_battle = 0;
		log_exploring_change();
		return 1;
	case VM_XSYS_HUD_SHOW:
		g_shown = stack_pop().i ? 1 : 0;
		log_exploring_change();
		return 1;
	case VM_XSYS_HUD_PROFILE_ON:
		g_profile = 1;
		log_exploring_change();
		return 1;
	case VM_XSYS_HUD_PROFILE_OFF:
		g_profile = 0;
		log_exploring_change();
		return 1;
	case VM_XSYS_HUD_MODAL_ON:
		i = modal_index(stack_pop().i);
		if (i >= 0)
			g_modal_n[i]++;
		log_exploring_change();
		return 1;
	case VM_XSYS_HUD_MODAL_OFF:
		i = modal_index(stack_pop().i);
		if (i >= 0 && g_modal_n[i] > 0)
			g_modal_n[i]--;
		log_exploring_change();
		return 1;
	default:
		return 0;
	}
}

int rq_hud_install(struct ain *ain)
{
	int f_create, f_delete, f_show, f_susp, f_res, f_battle, f_st_c, f_st_d;
	if (!ain)
		return 0;
	f_create = find_fn(ain, "T_MapParts@create");
	f_delete = find_fn(ain, "T_MapParts@delete");
	if (f_create < 0 || f_delete < 0) {
		rq_log("HUD: T_MapParts create/delete missing — exploring cannot turn on");
		return 0;
	}
	wrap_method_flag(ain, f_create, VM_XSYS_HUD_ON, 0);
	wrap_method_flag(ain, f_delete, VM_XSYS_HUD_OFF, 0);
	rq_log("HUD: dungeon map create/delete turns exploring on and off");
	f_show = find_fn(ain, "T_MapParts@setShow");
	if (f_show >= 0)
		wrap_set_show(ain, f_show);
	f_susp = find_fn(ain, "マップ_サスペンド");
	f_res = find_fn(ain, "マップ_レジューム");
	if (f_susp >= 0) {
		wrap_func_flag(ain, f_susp, VM_XSYS_HUD_SUSPEND, 0);
		rq_log("HUD: map suspend (join dialog / events) hides Item Previews");
	}
	if (f_res >= 0)
		wrap_func_flag(ain, f_res, VM_XSYS_HUD_RESUME, 0);
	f_battle = find_fn(ain, "Ｒ８戦闘");
	if (f_battle >= 0) {
		wrap_func_around(ain, f_battle, VM_XSYS_HUD_BATTLE_ON,
				 VM_XSYS_HUD_BATTLE_OFF);
		rq_log("HUD: battle hides Item Previews until it returns");
	}
	f_st_c = find_fn(ain, "T_Status@create");
	f_st_d = find_fn(ain, "T_Status@delete");
	if (f_st_c >= 0)
		wrap_method_flag(ain, f_st_c, VM_XSYS_HUD_PROFILE_ON, 0);
	if (f_st_d >= 0)
		wrap_method_flag(ain, f_st_d, VM_XSYS_HUD_PROFILE_OFF, 0);
	if (f_st_c >= 0)
		rq_log("HUD: character profile (T_Status) hides Item Previews");
	/* Blocking loops only. create/delete of T_Select / T_MapMessage / T_SystemMenu
	 * stays off — those objects live on the map and never deleted, which stuck
	 * exploring() false on real map-walk. */
	wrap_blocking(ain, "T_SystemMenu@select", MODAL_CONFIG, 1);
	wrap_blocking(ain, "T_Result@select", MODAL_RESULT, 0);
	wrap_blocking(ain, "T_ResultItem@select", MODAL_RESULT_ITEM, 0);
	wrap_blocking(ain, "リザルト鑑定", MODAL_RESULT_ITEM, 0);
	wrap_lifetime(ain, "T_Result", MODAL_RESULT);
	wrap_lifetime(ain, "T_ResultItem", MODAL_RESULT_ITEM);
	wrap_blocking(ain, "T_MapMessage@select", MODAL_ADV, 0);
	wrap_blocking(ain, "T_MapMessage@wait", MODAL_ADV, 0);
	wrap_blocking(ain, "T_ADV@select", MODAL_ADV, 0);
	wrap_blocking(ain, "T_ADV@wait", MODAL_ADV, 0);
	wrap_blocking(ain, "T_Select@select", MODAL_SELECT, 0);
	return 1;
}

int rq_lua_on_frame(lua_State *L)
{
	luaL_checktype(L, 1, LUA_TFUNCTION);
	/* Every mod keeps its own callback; they run in load order. */
	if (g_on_frame_n >= HUD_MAX_FRAME_CB)
		return luaL_error(L, "on_frame: too many callbacks (max %d)", HUD_MAX_FRAME_CB);
	lua_pushvalue(L, 1);
	g_on_frame_ref[g_on_frame_n++] = luaL_ref(L, LUA_REGISTRYINDEX);
	return 0;
}

int rq_lua_dungeon_active(lua_State *L)
{
	lua_pushboolean(L, g_dungeon);
	return 1;
}

int rq_lua_dungeon_exploring(lua_State *L)
{
	lua_pushboolean(L, rq_dungeon_exploring());
	return 1;
}

int rq_lua_input_mouse(lua_State *L)
{
	lua_pushinteger(L, g_mx);
	lua_pushinteger(L, g_my);
	lua_pushboolean(L, g_left);
	lua_pushboolean(L, g_right);
	lua_pushboolean(L, g_left_edge);
	lua_pushboolean(L, g_left_up);
	return 6;
}

int rq_lua_input_key(lua_State *L)
{
	int code = (int)luaL_checkinteger(L, 1);
	lua_pushboolean(L, key_is_down_raw(code));
	return 1;
}

void key_set_state(int code, int down);
void mouse_force_pos(int x, int y);
void mouse_release_agent(void);
void mouse_set_wheel(int dir);

/* A key name as in rqmod-ctl ("enter", "lmb", "f3") or a raw code. */
static int input_code_at(lua_State *L, int idx)
{
	int code;
	if (lua_type(L, idx) == LUA_TNUMBER)
		return (int)lua_tointeger(L, idx);
	code = rq_key_from_name(luaL_checkstring(L, idx));
	if (code < 0)
		return luaL_error(L, "unknown key '%s'", lua_tostring(L, idx));
	return code;
}

int rq_lua_input_code(lua_State *L)
{
	lua_pushinteger(L, input_code_at(L, 1));
	return 1;
}

int rq_lua_input_set_key(lua_State *L)
{
	key_set_state(input_code_at(L, 1), lua_toboolean(L, 2));
	return 0;
}

int rq_lua_input_set_mouse(lua_State *L)
{
	mouse_force_pos((int)luaL_checkinteger(L, 1), (int)luaL_checkinteger(L, 2));
	return 0;
}

int rq_lua_input_wheel(lua_State *L)
{
	mouse_set_wheel((int)luaL_checkinteger(L, 1));
	return 0;
}

int rq_lua_input_scroll(lua_State *L)
{
	if (g_scroll && lua_toboolean(L, 1))
		mouse_clear_wheel();
	lua_pushinteger(L, g_scroll);
	return 1;
}

int rq_lua_input_release(lua_State *L)
{
	(void)L;
	mouse_release_agent();
	return 0;
}

int rq_lua_hud_screen(lua_State *L)
{
	lua_pushinteger(L, config.view_width);
	lua_pushinteger(L, config.view_height);
	return 2;
}

int rq_lua_hud_text_width(lua_State *L)
{
	const char *utf = luaL_checkstring(L, 1);
	float size = 16.0f;
	int bold = 0;
	struct text_style ts;
	char *sjis;
	if (lua_istable(L, 2)) {
		lua_getfield(L, 2, "size");
		if (lua_isnumber(L, -1))
			size = (float)lua_tonumber(L, -1);
		lua_pop(L, 1);
		lua_getfield(L, 2, "bold");
		bold = lua_toboolean(L, -1);
		lua_pop(L, 1);
	} else if (lua_isnumber(L, 2)) {
		size = (float)lua_tonumber(L, 2);
	}
	sjis = utf2sjis(utf ? utf : "", 0);
	if (!sjis || !*sjis) {
		free(sjis);
		lua_pushinteger(L, 0);
		return 1;
	}
	hud_make_ts(&ts, size, 255, 255, 255);
	if (bold)
		hud_bold(&ts);
	lua_pushinteger(L, (int)ceilf(gfx_size_text(&ts, sjis)));
	free(sjis);
	return 1;
}

static void hud_push_utf_line(lua_State *L, int n, const char *sjis)
{
	char *utf = sjis2utf(sjis ? sjis : "", 0);
	lua_pushinteger(L, n);
	lua_pushstring(L, utf ? utf : "");
	lua_settable(L, -3);
	free(utf);
}

int rq_lua_hud_wrap(lua_State *L)
{
	const char *utf = luaL_checkstring(L, 1);
	int max_w = (int)luaL_checkinteger(L, 2);
	float size = (float)luaL_optnumber(L, 3, 14.0f);
	struct text_style ts;
	char *sjis, *p, *line_start, *last_brk;
	int n = 0;

	lua_newtable(L);
	if (max_w < 8)
		max_w = 8;
	hud_make_ts(&ts, size, 255, 255, 255);
	sjis = utf2sjis(utf ? utf : "", 0);
	if (!sjis || !*sjis) {
		free(sjis);
		return 1;
	}
	p = sjis;
	line_start = p;
	last_brk = NULL;
	while (*p) {
		char *next;
		char saved;
		int w;

		if (*p == '\n' || *p == '\r') {
			saved = *p;
			*p = 0;
			hud_push_utf_line(L, ++n, line_start);
			*p = saved;
			if (*p == '\r' && p[1] == '\n')
				p++;
			p++;
			line_start = p;
			last_brk = NULL;
			continue;
		}
		next = sjis_skip_char(p);
		saved = *next;
		*next = 0;
		w = (int)ceilf(gfx_size_text(&ts, line_start));
		*next = saved;
		if (w > max_w && p != line_start) {
			char *cut = last_brk ? last_brk : p;
			char c2 = *cut;
			*cut = 0;
			hud_push_utf_line(L, ++n, line_start);
			*cut = c2;
			line_start = (c2 == ' ') ? cut + 1 : cut;
			last_brk = NULL;
			p = line_start;
			continue;
		}
		if (*p == ' ')
			last_brk = p;
		p = next;
	}
	if (line_start && *line_start)
		hud_push_utf_line(L, ++n, line_start);
	free(sjis);
	return 1;
}

int rq_lua_hud_rect(lua_State *L)
{
	struct hud_cmd *c = push_cmd(HUD_RECT);
	if (!c)
		return 0;
	c->x = (int)luaL_checkinteger(L, 1);
	c->y = (int)luaL_checkinteger(L, 2);
	c->w = (int)luaL_checkinteger(L, 3);
	c->h = (int)luaL_checkinteger(L, 4);
	c->r = (int)luaL_optinteger(L, 5, 20);
	c->g = (int)luaL_optinteger(L, 6, 20);
	c->b = (int)luaL_optinteger(L, 7, 30);
	c->a = (int)luaL_optinteger(L, 8, 220);
	return 0;
}

static int align_at(lua_State *L, int idx, const char *start, const char *end)
{
	const char *s;
	if (lua_isnoneornil(L, idx))
		return ALIGN_START;
	s = luaL_checkstring(L, idx);
	if (!strcmp(s, start))
		return ALIGN_START;
	if (!strcmp(s, "center"))
		return ALIGN_CENTER;
	if (!strcmp(s, end))
		return ALIGN_END;
	return luaL_error(L, "align must be %s, center or %s, not %s", start, end, s);
}

/* t[key], or t[idx] when key is missing and idx > 0. */
static int int_field(lua_State *L, int t, const char *key, int idx, int def)
{
	int v = def;
	lua_getfield(L, t, key);
	if (lua_isnil(L, -1) && idx > 0) {
		lua_pop(L, 1);
		lua_rawgeti(L, t, idx);
	}
	if (!lua_isnil(L, -1))
		v = (int)luaL_checkinteger(L, -1);
	lua_pop(L, 1);
	return v;
}

int rq_lua_hud_text(lua_State *L)
{
	struct hud_cmd *c = push_cmd(HUD_TEXT);
	const char *s;
	if (!c)
		return 0;
	c->x = (int)luaL_checkinteger(L, 1);
	c->y = (int)luaL_checkinteger(L, 2);
	s = luaL_checkstring(L, 3);
	c->text = strdup(s ? s : "");
	c->size = 16.0f;
	c->r = 255;
	c->g = 255;
	c->b = 255;
	c->a = 255;
	if (lua_istable(L, 4)) {
		lua_getfield(L, 4, "size");
		if (lua_isnumber(L, -1))
			c->size = (float)lua_tonumber(L, -1);
		lua_getfield(L, 4, "r");
		if (lua_isinteger(L, -1))
			c->r = (int)lua_tointeger(L, -1);
		lua_getfield(L, 4, "g");
		if (lua_isinteger(L, -1))
			c->g = (int)lua_tointeger(L, -1);
		lua_getfield(L, 4, "b");
		if (lua_isinteger(L, -1))
			c->b = (int)lua_tointeger(L, -1);
		lua_getfield(L, 4, "a");
		if (lua_isinteger(L, -1))
			c->a = (int)lua_tointeger(L, -1);
		lua_getfield(L, 4, "w");
		if (lua_isinteger(L, -1))
			c->bw = (int)lua_tointeger(L, -1);
		lua_getfield(L, 4, "h");
		if (lua_isinteger(L, -1))
			c->bh = (int)lua_tointeger(L, -1);
		lua_getfield(L, 4, "align");
		c->align = align_at(L, -1, "left", "right");
		lua_getfield(L, 4, "valign");
		c->valign = align_at(L, -1, "top", "bottom");
		lua_getfield(L, 4, "edge");
		c->no_edge = lua_isboolean(L, -1) && !lua_toboolean(L, -1);
		lua_getfield(L, 4, "bold");
		c->bold = lua_toboolean(L, -1);
		lua_settop(L, 4);
		if (c->a < 0)
			c->a = 0;
		if (c->a > 255)
			c->a = 255;
	}
	return 0;
}

int rq_lua_hud_cg(lua_State *L)
{
	struct hud_cmd *c;
	Texture *t;
	int x = (int)luaL_checkinteger(L, 1);
	int y = (int)luaL_checkinteger(L, 2);
	int cg = (int)luaL_checkinteger(L, 3);
	int sx = 0, sy = 0, sw = 0, sh = 0, w = 0, h = 0;
	int r = 255, g = 255, b = 255, a = 255;
	if (!lua_isnoneornil(L, 4)) {
		luaL_checktype(L, 4, LUA_TTABLE);
		lua_getfield(L, 4, "src");
		if (!lua_isnil(L, -1)) {
			int s = lua_gettop(L);
			luaL_checktype(L, s, LUA_TTABLE);
			sx = int_field(L, s, "x", 1, 0);
			sy = int_field(L, s, "y", 2, 0);
			sw = int_field(L, s, "w", 3, 0);
			sh = int_field(L, s, "h", 4, 0);
			if (sw <= 0 || sh <= 0 || sx < 0 || sy < 0)
				return luaL_error(L, "hud.cg: src needs x, y >= 0 and w, h > 0");
		}
		lua_pop(L, 1);
		w = int_field(L, 4, "w", 0, 0);
		h = int_field(L, 4, "h", 0, 0);
		a = int_field(L, 4, "alpha", 0, 255);
		lua_getfield(L, 4, "tint");
		if (!lua_isnil(L, -1)) {
			int s = lua_gettop(L);
			luaL_checktype(L, s, LUA_TTABLE);
			r = int_field(L, s, "r", 1, 255);
			g = int_field(L, s, "g", 2, 255);
			b = int_field(L, s, "b", 3, 255);
		}
		lua_pop(L, 1);
	}
	t = cg_tex(cg);
	c = push_cmd(HUD_CG);
	if (!c)
		goto size;
	c->x = x;
	c->y = y;
	c->cg = cg;
	c->sx = sx;
	c->sy = sy;
	c->sw = sw;
	c->sh = sh;
	c->w = w;
	c->h = h;
	c->r = r < 0 ? 0 : r > 255 ? 255 : r;
	c->g = g < 0 ? 0 : g > 255 ? 255 : g;
	c->b = b < 0 ? 0 : b > 255 ? 255 : b;
	c->a = a < 0 ? 0 : a > 255 ? 255 : a;
size:
	if (t) {
		lua_pushinteger(L, t->w);
		lua_pushinteger(L, t->h);
		return 2;
	}
	lua_pushinteger(L, 0);
	lua_pushinteger(L, 0);
	return 2;
}

int rq_lua_hud_cg_size(lua_State *L)
{
	Texture *t = cg_tex((int)luaL_checkinteger(L, 1));
	if (!t) {
		lua_pushnil(L);
		return 1;
	}
	lua_pushinteger(L, t->w);
	lua_pushinteger(L, t->h);
	return 2;
}

int rq_lua_hud_clip(lua_State *L)
{
	int x = 0, y = 0, w = 0, h = 0, on = !lua_isnoneornil(L, 1);
	struct hud_cmd *c;
	if (on) {
		x = (int)luaL_checkinteger(L, 1);
		y = (int)luaL_checkinteger(L, 2);
		w = (int)luaL_checkinteger(L, 3);
		h = (int)luaL_checkinteger(L, 4);
	}
	c = push_cmd(HUD_CLIP);
	if (!c)
		return 0;
	c->x = x;
	c->y = y;
	c->w = w < 0 ? 0 : w;
	c->h = h < 0 ? 0 : h;
	/* A clip of nothing still clips: cg tells it from "off". */
	c->cg = on;
	return 0;
}

int rq_lua_hud_hit(lua_State *L)
{
	int x = (int)luaL_checkinteger(L, 1);
	int y = (int)luaL_checkinteger(L, 2);
	int w = (int)luaL_checkinteger(L, 3);
	int h = (int)luaL_checkinteger(L, 4);
	lua_pushboolean(L, hit(x, y, w, h));
	return 1;
}

int rq_lua_hud_clicked(lua_State *L)
{
	int x = (int)luaL_checkinteger(L, 1);
	int y = (int)luaL_checkinteger(L, 2);
	int w = (int)luaL_checkinteger(L, 3);
	int h = (int)luaL_checkinteger(L, 4);
	lua_pushboolean(L, g_left_edge && hit(x, y, w, h));
	return 1;
}

int rq_lua_hud_block(lua_State *L)
{
	(void)L;
	g_block_mouse = 1;
	return 0;
}

/* Engine banner, bottom-left of the main menu. Mods cannot remove it. */
static void draw_version_banner(void)
{
	struct hud_cmd *c;
	if (!rq_saves_title_active())
		return;
	c = push_cmd(HUD_TEXT);
	if (!c)
		return;
	c->text = strdup("Xsystem4 - [RQ_Modding " RQ_VERSION "]");
	c->x = 8;
	c->y = config.view_height - 32;
	c->size = 14.0f;
	c->r = c->g = c->b = 230;
	c->a = 255;
}

int rq_hud_tick(void)
{
	lua_State *L = rq_lua();
	rq_ui_on_update();
	g_block_mouse = 0;
	g_block_keys = 0;
	clear_cmds();
	g_drew = 0;
	mouse_get_pos(&g_mx, &g_my);
	g_left = key_is_down_raw(VK_LBUTTON);
	g_right = key_is_down_raw(VK_RBUTTON);
	g_left_edge = g_left && !g_prev_left;
	g_left_up = !g_left && g_prev_left;
	g_prev_left = g_left;
	g_scroll = rq_wheel_steps - g_wheel_seen;
	g_wheel_seen = rq_wheel_steps;
	draw_version_banner();
	rq_item_refresh();
	rq_roster_refresh();
	rq_battle_marks_tick();
	rq_speed_tick();
	rq_sound_tick();
	if (!L || g_on_frame_n == 0)
		return g_drew;
	for (int i = 0; i < g_on_frame_n; i++) {
		lua_rawgeti(L, LUA_REGISTRYINDEX, g_on_frame_ref[i]);
		if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
			/* An error in a callback comes back every frame: log each new text once. */
			static char last[HUD_MAX_FRAME_CB][256];
			const char *msg = lua_tostring(L, -1);
			if (!msg)
				msg = "(not a string)";
			if (strncmp(last[i], msg, sizeof last[i] - 1)) {
				snprintf(last[i], sizeof last[i], "%s", msg);
				rq_log("HUD on_frame error: %s", msg);
				rq_log("HUD on_frame: the same error again will not be logged");
			}
			lua_pop(L, 1);
		}
	}
	log_exploring_change();
	return g_drew;
}

static void prepare_hud_shader(struct gfx_render_job *job, void *data)
{
	struct hud_shader *s = (struct hud_shader *)job->shader;
	struct hud_tint *t = data;
	glUniform1f(s->blend_rate, t->a);
	glUniform3f(s->multiply_color, t->r, t->g, t->b);
}

/* Draws the part sx, sy, sw, sh of t into x, y, w, h, tinted and faded by c. */
static void draw_part(Texture *t, int sx, int sy, int sw, int sh,
		      int x, int y, int w, int h, const struct hud_cmd *c)
{
	struct hud_tint tint = {
		c->r / 255.0f, c->g / 255.0f, c->b / 255.0f, c->a / 255.0f
	};
	/* Top left, top right, bottom left, bottom right: the quad is drawn
	 * by rect_index_data { 0, 1, 3, 2 }. */
	struct gfx_vertex v[4] = {
		{ (float)x,       (float)y,       0, 1, (float)sx,        (float)sy },
		{ (float)(x + w), (float)y,       0, 1, (float)(sx + sw), (float)sy },
		{ (float)x,       (float)(y + h), 0, 1, (float)sx,        (float)(sy + sh) },
		{ (float)(x + w), (float)(y + h), 0, 1, (float)(sx + sw), (float)(sy + sh) },
	};
	if (!g_shader.loaded) {
		gfx_load_shader(&g_shader.s, "shaders/render.v.glsl", "shaders/sprite.f.glsl");
		g_shader.blend_rate = glGetUniformLocation(g_shader.s.program, "blend_rate");
		g_shader.multiply_color = glGetUniformLocation(g_shader.s.program, "multiply_color");
		g_shader.s.prepare = prepare_hud_shader;
		g_shader.loaded = 1;
	}
	gfx_render_quadrilateral_with(&g_shader.s, t, v, &tint);
}

/* One white pixel, tinted by draw_part, draws every rect. */
static Texture *white_tex(void)
{
	static Texture t;
	static int made;
	if (!made) {
		gfx_init_texture_rgba(&t, 1, 1, (SDL_Color){ 255, 255, 255, 255 });
		made = 1;
	}
	return &t;
}

void rq_hud_draw(void)
{
	int i;
	int clipped = 0;
	struct text_style ts;

	white_tex();

	for (i = 0; i < g_ncmd; i++) {
		struct hud_cmd *c = &g_cmd[i];
		if (c->kind == HUD_RECT && c->w > 0 && c->h > 0) {
			draw_part(white_tex(), 0, 0, 1, 1, c->x, c->y, c->w, c->h, c);
		} else if (c->kind == HUD_CLIP) {
			if (c->cg) {
				glEnable(GL_SCISSOR_TEST);
				glScissor(c->x, c->y, c->w, c->h);
				clipped = 1;
			} else if (clipped) {
				glDisable(GL_SCISSOR_TEST);
				clipped = 0;
			}
		} else if (c->kind == HUD_CG) {
			Texture *src = cg_tex(c->cg);
			int sx = c->sx, sy = c->sy, sw = c->sw, sh = c->sh;
			if (!src || c->a <= 0)
				continue;
			if (sw <= 0) {
				sx = sy = 0;
				sw = src->w;
				sh = src->h;
			}
			/* A part past the CG's edge is cut to the CG. */
			if (sx >= src->w || sy >= src->h)
				continue;
			if (sx + sw > src->w)
				sw = src->w - sx;
			if (sy + sh > src->h)
				sh = src->h - sy;
			draw_part(src, sx, sy, sw, sh, c->x, c->y,
				  c->w > 0 ? c->w : sw, c->h > 0 ? c->h : sh, c);
		} else if (c->kind == HUD_TEXT && c->text) {
			Texture t;
			Rectangle r;
			char *sjis;
			int tw, th;
			hud_make_ts(&ts, c->size, c->r, c->g, c->b);
			if (c->no_edge)
				hud_no_edge(&ts);
			if (c->bold)
				hud_bold(&ts);
			sjis = utf2sjis(c->text, 0);
			if (!sjis)
				continue;
			int x = c->x, y = c->y;
			int text_w = *sjis ? (int)ceilf(gfx_size_text(&ts, sjis)) : 0;
			if (c->align == ALIGN_CENTER)
				x += (c->bw - text_w) / 2;
			else if (c->align == ALIGN_END)
				x += c->bw - text_w;
			/* The glyphs fill about the font size from the top. */
			if (c->valign == ALIGN_CENTER)
				y += (c->bh - (int)ts.size) / 2;
			else if (c->valign == ALIGN_END)
				y += c->bh - (int)ts.size;
			tw = text_w + 8;
			if (tw < 32)
				tw = 32;
			th = (int)ts.size + 20;
			/* COPY, not blended: blended glyphs keep dest alpha 0 so
			 * labels were invisible (blank quads / empty Close). */
			/* The clip is for the screen, not for the texture the text is made in. */
			if (clipped)
				glDisable(GL_SCISSOR_TEST);
			gfx_init_texture_rgba(&t, tw, th, (SDL_Color){ 0, 0, 0, 0 });
			gfx_render_text(&t, 0.0f, 0, sjis, &ts, false);
			if (clipped)
				glEnable(GL_SCISSOR_TEST);
			free(sjis);
			if (c->a >= 255) {
				r = RECT(x, y, t.w, t.h);
				gfx_render_texture(&t, &r);
			} else if (c->a > 0) {
				struct hud_cmd white = *c;
				white.r = white.g = white.b = 255;
				draw_part(&t, 0, 0, t.w, t.h, x, y, t.w, t.h, &white);
			}
			gfx_delete_texture(&t);
		}
	}
	if (clipped)
		glDisable(GL_SCISSOR_TEST);
}

void rq_hud_bind_lua(lua_State *L)
{
	(void)L;
}

#endif /* RQ_MODLOADER */
