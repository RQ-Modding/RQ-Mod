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
	HUD_CLIP,
	HUD_GTEXT
};

/* Text alignment inside the box bw, bh (HUD_TEXT). */
enum { ALIGN_START, ALIGN_CENTER, ALIGN_END };

struct hud_cmd {
	int kind;
	int layer; /* 0 the HUD, 1 the top layer (rq.hud.top, 2.11.0) */
	int x, y, w, h;
	int r, g, b, a;
	int cg;
	float size;
	char *text;
	/* HUD_CG: source part of the CG; sw == 0 means the whole CG. */
	int sx, sy, sw, sh;
	/* HUD_TEXT: the box the text is aligned in, and no dark edge. */
	int bw, bh, align, valign, no_edge, bold;
	/* HUD_GTEXT: the line the game drew, its shadow and the shadow's offset. */
	Texture *tex, *shadow;
	int dx, dy, sa;
	struct rq_gametext gt;
};

/* The clips pushed on this update (rq.hud.clip_push), each already narrowed to the one under it. */
#define HUD_MAX_CLIP 16
static struct { int x, y, w, h; } g_clip[HUD_MAX_CLIP];
static int g_nclip;

/* The layer new commands go to, and whether the top layer's last clip command turned a clip on. */
static int g_layer, g_top_clip_on;

static int game_style(lua_State *L, int idx, struct rq_gametext *gt, int *has_shadow,
		      int *dx, int *dy, int *sr, int *sg, int *sb, int *sa);

int rq_hud_game_style(lua_State *L, int idx, struct rq_gametext *gt, int *has_shadow,
		      int *dx, int *dy, int *sr, int *sg, int *sb, int *sa)
{
	return game_style(L, idx, gt, has_shadow, dx, dy, sr, sg, sb, sa);
}

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
/* rq.hud.block_keys(list) (2.12.0): only these keys, for this update. */
static unsigned char g_block_key[VK_NR_KEYCODES];
static int g_block_some;
/* rq.hud.capture_mouse (2.6.0): the game sees no mouse at all. Set for one
 * update, and kept while a button that was down under it stays down. */
static int g_capture_mouse;
static int g_capture_last; /* captured on the last whole update (rq_hud_captured, 2.11.0) */
static int g_capture_seen;
static int g_text_on;
static int g_text_max;
static char g_text[256];
static size_t g_caret; /* bytes before the caret (2.29.0) */
/* The editing keys (2.29.0), read from the key state on each update, so a real key, an agent's
 * and rq.input.set_key count alike. Held, they repeat as the kit's keys: 250 ms, then every
 * 50 ms. V counts only with Ctrl down, and does not repeat. */
static const int k_edit_keys[] = { VK_BACK, VK_DELETE, VK_LEFT, VK_RIGHT, VK_HOME, VK_END, VK_V };
#define N_EDIT_KEYS ((int)(sizeof k_edit_keys / sizeof k_edit_keys[0]))
static int g_edit_down[N_EDIT_KEYS];
static uint32_t g_edit_next[N_EDIT_KEYS];
/* rqmod-ctl type while the text input is on (2.29.0): queued by the pipe's thread, put in on
 * the game thread's next update. */
static SDL_SpinLock g_typed_lock;
static char g_typed[1024];
static size_t g_typed_n;
#define HUD_MAX_FRAME_CB 32
static int g_on_frame_ref[HUD_MAX_FRAME_CB];
static int g_on_frame_n;
/* The mod that registered each callback, for the stall watch. */
static char g_on_frame_mod[HUD_MAX_FRAME_CB][48];

/*
 * The stall watch (2.31.0). In four Lab stalls the game's loop ran and the Lab's on_frame callback
 * wrote no heartbeat for 60 s (ideas-later.md, "Test Lab: ..."). rq_hud_tick runs only from
 * sact_Update; handle_events runs from every wait loop of the game. These say when the tick and
 * the callbacks last ran and which callback runs now; the pipe thread reads them for where.
 */
#define STALL_WATCH_MS 10000
static volatile uint32_t g_watch_tick, g_watch_round, g_watch_cb_at;
static volatile int g_watch_cb = -1;
/* The game's call depth (call_stack_ptr) where the innermost running rq.on_frame callback began;
 * -1 when none runs. rq_frame_direct() is true when code runs in that callback itself, not in a
 * game function it called (a fight from rq.battle.start runs its turns deeper). 2.46.0. */
static int g_frame_depth = -1;
extern int32_t call_stack_ptr; /* vm.c; vm.h declares it only for VM_PRIVATE */
static int g_watch_logged;
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
	g_nclip = 0;
	g_layer = 0;
	g_top_clip_on = 0;
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
	c->layer = g_layer;
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

int rq_hud_key_blocked(int code)
{
	if (g_block_keys)
		return 1;
	return g_block_some && code >= 0 && code < VK_NR_KEYCODES && g_block_key[code];
}

int rq_hud_block_keys(void)
{
	return g_block_keys;
}

/* Whether a page captured the mouse on this update or the last whole one (2.11.0): a dump built
 * in the middle of an update sees the capture before the page's callback has run. */
int rq_hud_captured(void)
{
	return g_capture_seen || g_capture_last;
}

int rq_hud_capture_mouse(void)
{
	return g_capture_mouse;
}

int rq_hud_choosing(void)
{
	return g_modal_n[4] > 0;
}

static int g_under_z = RQ_HUD_UNDER_Z;

int rq_hud_under_z(void)
{
	return g_under_z;
}

/* dialog.lua's (2.28.0): the choice menu's z, nil for the game's 9991. Not part of rq. */
int rq_lua_hud_set_under_z(lua_State *L)
{
	g_under_z = (int)luaL_optinteger(L, 1, RQ_HUD_UNDER_Z);
	return 0;
}

int rq_lua_ui_choosing(lua_State *L)
{
	lua_pushboolean(L, rq_hud_choosing());
	return 1;
}

/* Text entry for mod dialogs (rename) and rq.uikit.input. Printable text only, and only what
 * the game's Gothic face can draw (JIS X 0208 plus ASCII), at the caret (2.29.0). */

/* The length of the UTF-8 character at s, or 0 when it is not one. */
static size_t u8_len(const char *s)
{
	unsigned char c = (unsigned char)s[0];
	size_t n, i;
	if (c < 0x80)
		return c ? 1 : 0;
	if ((c & 0xE0) == 0xC0)
		n = 2;
	else if ((c & 0xF0) == 0xE0)
		n = 3;
	else if ((c & 0xF8) == 0xF0)
		n = 4;
	else
		return 0;
	for (i = 1; i < n; i++)
		if (((unsigned char)s[i] & 0xC0) != 0x80)
			return 0;
	return n;
}

/* Whether the game's font draws this one character: it comes back the same through Shift JIS. */
static int text_drawable(const char *ch)
{
	char *sj = utf2sjis(ch, 0), *back;
	int ok;
	if (!sj)
		return 0;
	back = sjis2utf(sj, 0);
	ok = back && !strcmp(back, ch);
	free(back);
	free(sj);
	return ok;
}

/* Puts utf8 in at the caret, character by character: control characters and what the font
 * cannot draw are dropped, and it stops at the first one past the limit. Returns how many
 * characters went in. */
static int text_insert(const char *utf8)
{
	int n = 0;
	const char *p = utf8;
	if (!g_text_on || !utf8)
		return 0;
	while (*p) {
		char ch[5];
		size_t len = u8_len(p), cur;
		if (!len) {
			p++;
			continue;
		}
		memcpy(ch, p, len);
		ch[len] = 0;
		p += len;
		if ((unsigned char)ch[0] < 0x20 || (unsigned char)ch[0] == 0x7F || !text_drawable(ch))
			continue;
		cur = strlen(g_text);
		if (cur + len > (size_t)g_text_max || cur + len >= sizeof g_text)
			break;
		if (g_caret > cur)
			g_caret = cur;
		memmove(g_text + g_caret + len, g_text + g_caret, cur - g_caret + 1);
		memcpy(g_text + g_caret, ch, len);
		g_caret += len;
		n++;
	}
	return n;
}

static void text_on_input(const char *utf8)
{
	text_insert(utf8);
}

static size_t text_prev(size_t at)
{
	if (!at)
		return 0;
	at--;
	while (at > 0 && ((unsigned char)g_text[at] & 0xC0) == 0x80)
		at--;
	return at;
}

static size_t text_next(size_t at)
{
	size_t len = strlen(g_text), n;
	if (at >= len)
		return len;
	n = u8_len(g_text + at);
	return at + (n ? n : 1);
}

static void text_edit(int code)
{
	size_t len = strlen(g_text), to;
	if (g_caret > len)
		g_caret = len;
	switch (code) {
	case VK_BACK:
		to = text_prev(g_caret);
		memmove(g_text + to, g_text + g_caret, len - g_caret + 1);
		g_caret = to;
		break;
	case VK_DELETE:
		to = text_next(g_caret);
		memmove(g_text + g_caret, g_text + to, len - to + 1);
		break;
	case VK_LEFT:
		g_caret = text_prev(g_caret);
		break;
	case VK_RIGHT:
		g_caret = text_next(g_caret);
		break;
	case VK_HOME:
		g_caret = 0;
		break;
	case VK_END:
		g_caret = len;
		break;
	case VK_V: {
		char *clip = SDL_HasClipboardText() ? SDL_GetClipboardText() : NULL;
		if (clip) {
			text_insert(clip);
			SDL_free(clip);
		}
		break;
	}
	}
}

/* On each update while the text input is on: the queued text, then the editing keys. */
static void text_tick(void)
{
	char typed[sizeof g_typed];
	uint32_t now;
	int i;
	if (!g_text_on)
		return;
	SDL_AtomicLock(&g_typed_lock);
	memcpy(typed, g_typed, g_typed_n);
	typed[g_typed_n] = 0;
	g_typed_n = 0;
	SDL_AtomicUnlock(&g_typed_lock);
	if (typed[0])
		text_insert(typed);
	now = SDL_GetTicks();
	for (i = 0; i < N_EDIT_KEYS; i++) {
		int code = k_edit_keys[i];
		int down = key_is_down_raw(code) ? 1 : 0, fire = 0;
		if (code == VK_V && !key_is_down_raw(VK_CONTROL))
			down = 0;
		if (down && !g_edit_down[i]) {
			fire = 1;
			g_edit_next[i] = code == VK_V ? UINT32_MAX : now + 250;
		} else if (down && g_edit_next[i] != UINT32_MAX && (int32_t)(now - g_edit_next[i]) >= 0) {
			fire = 1;
			g_edit_next[i] = now + 50;
		}
		g_edit_down[i] = down;
		if (fire)
			text_edit(code);
	}
}

int rq_hud_text_on(void)
{
	return g_text_on;
}

/* From the pipe's thread (rqmod-ctl type). */
void rq_hud_text_queue(const char *utf8)
{
	size_t len = strlen(utf8);
	SDL_AtomicLock(&g_typed_lock);
	if (g_typed_n + len >= sizeof g_typed)
		len = sizeof g_typed - 1 - g_typed_n;
	memcpy(g_typed + g_typed_n, utf8, len);
	g_typed_n += len;
	SDL_AtomicUnlock(&g_typed_lock);
}

int rq_lua_hud_text_begin(lua_State *L)
{
	const char *init = luaL_optstring(L, 1, "");
	int max = (int)luaL_optinteger(L, 2, 40);
	int i;
	if (max < 1)
		max = 1;
	if (max > (int)sizeof g_text - 1)
		max = (int)sizeof g_text - 1;
	g_text_max = max;
	snprintf(g_text, sizeof g_text, "%.*s", max, init);
	/* Not in the middle of a character. */
	g_caret = strlen(g_text);
	while (g_caret > 0 && ((unsigned char)g_text[g_caret] & 0xC0) == 0x80)
		g_caret--;
	g_text[g_caret] = 0;
	/* A key already down (the Enter or the click that started it) waits to be let go. */
	for (i = 0; i < N_EDIT_KEYS; i++) {
		g_edit_down[i] = 1;
		g_edit_next[i] = UINT32_MAX;
	}
	SDL_AtomicLock(&g_typed_lock);
	g_typed_n = 0;
	SDL_AtomicUnlock(&g_typed_lock);
	if (!g_text_on) {
		register_input_handler(text_on_input);
		SDL_StartTextInput();
		g_text_on = 1;
	}
	return 0;
}

int rq_lua_hud_text_value(lua_State *L)
{
	lua_pushstring(L, g_text);
	lua_pushboolean(L, g_text_on);
	lua_pushinteger(L, (lua_Integer)g_caret);
	return 3;
}

int rq_lua_hud_text_end(lua_State *L)
{
	(void)L;
	if (g_text_on) {
		SDL_StopTextInput();
		clear_input_handler();
		g_text_on = 0;
	}
	return 0;
}

/* rq.input.type(text) (2.29.0): text into the HUD's text input as if typed. */
int rq_lua_input_type(lua_State *L)
{
	const char *s = luaL_checkstring(L, 1);
	if (!g_text_on) {
		lua_pushboolean(L, 0);
		return 1;
	}
	lua_pushinteger(L, text_insert(s));
	return 1;
}

static int input_code_at(lua_State *L, int idx);

/* rq.hud.block_keys([keys]): every key, or only the keys listed (names or codes), for this update. */
int rq_lua_hud_block_keys(lua_State *L)
{
	int i, n;
	if (lua_isnoneornil(L, 1)) {
		g_block_keys = 1;
		return 0;
	}
	luaL_checktype(L, 1, LUA_TTABLE);
	n = (int)lua_rawlen(L, 1);
	for (i = 1; i <= n; i++) {
		int code;
		lua_rawgeti(L, 1, i);
		code = input_code_at(L, lua_gettop(L));
		lua_pop(L, 1);
		if (code < 0 || code >= VK_NR_KEYCODES)
			return luaL_error(L, "hud.block_keys: no key %d", code);
		if (code == VK_LBUTTON || code == VK_RBUTTON || code == VK_MBUTTON)
			return luaL_error(L, "hud.block_keys: the mouse buttons are rq.hud.block_game_input's");
		g_block_key[code] = 1;
		g_block_some = 1;
	}
	return 0;
}

/* The System menu or Config is open, over whatever screen (rq.scene.start waits for them). */
int rq_hud_menu_open(void)
{
	return g_modal_n[0] > 0;
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
	g_capture_mouse = 0;
	g_capture_last = 0;
	g_capture_seen = 0;
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
	/* Config from the title, a message or the System menu (loader 1.14.0, for rq.scene.start). */
	wrap_blocking(ain, "コンフィグ_実行", MODAL_CONFIG, 0);
	wrap_blocking(ain, "T_Result@select", MODAL_RESULT, 0);
	wrap_blocking(ain, "T_ResultItem@select", MODAL_RESULT_ITEM, 0);
	wrap_blocking(ain, "リザルト鑑定", MODAL_RESULT_ITEM, 0);
	wrap_lifetime(ain, "T_Result", MODAL_RESULT);
	wrap_lifetime(ain, "T_ResultItem", MODAL_RESULT_ITEM);
	wrap_blocking(ain, "T_MapMessage@select", MODAL_ADV, 0);
	wrap_blocking(ain, "T_MapMessage@wait", MODAL_ADV, 0);
	wrap_blocking(ain, "T_ADV@select", MODAL_ADV, 0);
	wrap_blocking(ain, "T_ADV@wait", MODAL_ADV, 0);
	/* T_Select has no select method: open is its loop (2.7.0; before, nothing was wrapped). */
	wrap_blocking(ain, "T_Select@open", MODAL_SELECT, 1);
	return 1;
}

int rq_frame_direct(void)
{
	return g_frame_depth >= 0 && call_stack_ptr <= g_frame_depth;
}

int rq_lua_on_frame(lua_State *L)
{
	luaL_checktype(L, 1, LUA_TFUNCTION);
	/* Every mod keeps its own callback; they run in load order. */
	if (g_on_frame_n >= HUD_MAX_FRAME_CB)
		return luaL_error(L, "on_frame: too many callbacks (max %d)", HUD_MAX_FRAME_CB);
	{
		const char *mod = rq_sandbox_caller_id(L);
		snprintf(g_on_frame_mod[g_on_frame_n], sizeof g_on_frame_mod[0], "%s", mod ? mod : "?");
	}
	lua_pushvalue(L, 1);
	g_on_frame_ref[g_on_frame_n++] = luaL_ref(L, LUA_REGISTRYINDEX);
	return 0;
}

void rq_stall_watch_text(char *buf, size_t n)
{
	uint32_t now = SDL_GetTicks();
	uint32_t tick = g_watch_tick, round = g_watch_round, cb_at = g_watch_cb_at;
	int cb = g_watch_cb;
	if (!tick) {
		snprintf(buf, n, "rq.on_frame has not run yet");
		return;
	}
	if (cb >= 0 && cb < HUD_MAX_FRAME_CB)
		snprintf(buf, n, "the HUD tick started %.1f s ago; inside on_frame callback %d (mod %s) "
			 "for %.1f s; the last round ended %.1f s ago",
			 (now - tick) / 1000.0, cb + 1, g_on_frame_mod[cb], (now - cb_at) / 1000.0,
			 round ? (now - round) / 1000.0 : -1.0);
	else
		snprintf(buf, n, "the HUD tick started %.1f s ago; no on_frame callback runs; "
			 "the last round ended %.1f s ago",
			 (now - tick) / 1000.0, round ? (now - round) / 1000.0 : -1.0);
}

void rq_stall_watch_reset(void)
{
	if (g_watch_tick)
		g_watch_tick = SDL_GetTicks();
}

/* From handle_events: one line when rq.on_frame has not started for 10 s, and one when it runs
 * again (rq_hud_tick). A native dialog or a load that pumps no events stops this too, and logs
 * nothing. */
void rq_stall_watch(void)
{
	char text[512], stack[512];
	if (g_watch_logged || !g_watch_tick)
		return;
	if (SDL_GetTicks() - g_watch_tick < STALL_WATCH_MS)
		return;
	g_watch_logged = 1;
	rq_stall_watch_text(text, sizeof text);
	rq_ain_stack_line(stack, sizeof stack, 12);
	rq_log("stall watch: rq.on_frame has not started for %u s, while the game pumps events without "
	       "its update: %s; AIN stack: %s", (unsigned)(STALL_WATCH_MS / 1000), text, stack);
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
		struct rq_gametext gt;
		int has_shadow, dx = 0, dy = 0, sr, sg, sb, sa;
		if (game_style(L, 2, &gt, &has_shadow, &dx, &dy, &sr, &sg, &sb, &sa)) {
			int tw = 0, th = 0, measure;
			lua_getfield(L, 2, "measure");
			measure = lua_toboolean(L, -1);
			lua_pop(L, 1);
			if (measure) {
				/* As the game's windows reckon it, not the drawn line (2.13.0). */
				tw = rq_gametext_measure_game(utf ? utf : "", &gt);
				if (tw < 0)
					return luaL_error(L, "rq.hud.text_width: the game has no MeasureText");
				lua_pushinteger(L, tw);
				lua_pushinteger(L, gt.size);
				return 2;
			}
			if (utf && *utf)
				rq_gametext_measure(utf, &gt, &tw, &th);
			lua_pushinteger(L, tw ? tw + (dx > 0 ? dx : -dx) * has_shadow : 0);
			lua_pushinteger(L, th ? th + (dy > 0 ? dy : -dy) * has_shadow : 0);
			return 2;
		}
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

static int color_part(lua_State *L, int t, const char *key, int idx, int def)
{
	int v = def;
	lua_getfield(L, t, key);
	if (lua_isnil(L, -1)) {
		lua_pop(L, 1);
		lua_rawgeti(L, t, idx);
	}
	if (!lua_isnil(L, -1))
		v = (int)luaL_checkinteger(L, -1);
	lua_pop(L, 1);
	return v < 0 ? 0 : v > 255 ? 255 : v;
}

/* Whether the style at idx asks for the game's font; fills gt from it. Raises an
 * error for a bad face or font. sx, sy, shadow color: the shadow, when it has one. */
static int game_style(lua_State *L, int idx, struct rq_gametext *gt, int *has_shadow,
		      int *dx, int *dy, int *sr, int *sg, int *sb, int *sa)
{
	const char *font;
	int t = lua_absindex(L, idx);
	*has_shadow = 0;
	*sa = 255;
	if (!lua_istable(L, t))
		return 0;
	lua_getfield(L, t, "font");
	font = lua_isnil(L, -1) ? "hud" : luaL_checkstring(L, -1);
	lua_pop(L, 1);
	if (!strcmp(font, "hud"))
		return 0;
	if (strcmp(font, "game"))
		return luaL_error(L, "font must be \"hud\" or \"game\", not %s", font);
	memset(gt, 0, sizeof *gt);
	gt->face = 258;
	gt->size = 16;
	gt->r = gt->g = gt->b = 255;
	gt->space = -4;
	gt->spacing = 1.0f;
	lua_getfield(L, t, "face");
	if (!lua_isnil(L, -1))
		gt->face = (int)luaL_checkinteger(L, -1);
	lua_pop(L, 1);
	if (gt->face < 256 || gt->face > 258)
		return luaL_error(L, "face must be 256, 257 or 258, not %d", gt->face);
	lua_getfield(L, t, "size");
	if (lua_isnumber(L, -1))
		gt->size = (int)(lua_tonumber(L, -1) + 0.5);
	lua_pop(L, 1);
	if (gt->size < 6 || gt->size > 96)
		return luaL_error(L, "size must be 6 to 96 with the game's font, not %d", gt->size);
	gt->r = color_part(L, t, "r", 0, 255);
	gt->g = color_part(L, t, "g", 0, 255);
	gt->b = color_part(L, t, "b", 0, 255);
	lua_getfield(L, t, "bold");
	gt->weight = lua_toboolean(L, -1) ? 40 : 0;
	lua_pop(L, 1);
	/* 2.21.0: the game's own weight number, as its text metrics take it (the stats line's numbers
	 * are 70); it wins over bold. */
	lua_getfield(L, t, "weight");
	if (!lua_isnil(L, -1)) {
		gt->weight = (int)luaL_checkinteger(L, -1);
		if (gt->weight < 0 || gt->weight > 1000)
			return luaL_error(L, "weight must be 0 to 1000, not %d", gt->weight);
	}
	lua_pop(L, 1);
	lua_getfield(L, t, "space");
	if (!lua_isnil(L, -1))
		gt->space = (int)luaL_checkinteger(L, -1);
	lua_pop(L, 1);
	lua_getfield(L, t, "spacing");
	if (!lua_isnil(L, -1))
		gt->spacing = (float)luaL_checknumber(L, -1);
	lua_pop(L, 1);
	if (gt->spacing < 0.0f || gt->spacing > 4.0f)
		return luaL_error(L, "spacing must be 0 to 4");
	lua_getfield(L, t, "outline");
	if (lua_istable(L, -1)) {
		int o = lua_gettop(L);
		gt->er = color_part(L, o, "r", 1, 0);
		gt->eg = color_part(L, o, "g", 2, 0);
		gt->eb = color_part(L, o, "b", 3, 0);
		lua_getfield(L, o, "w");
		gt->edge = lua_isnil(L, -1) ? 1 : (int)luaL_checkinteger(L, -1);
		lua_pop(L, 1);
		if (gt->edge < 1 || gt->edge > 4)
			return luaL_error(L, "outline.w must be 1 to 4");
	} else if (!lua_isnil(L, -1)) {
		return luaL_error(L, "outline must be a table {r, g, b, w = 1}");
	}
	lua_pop(L, 1);
	lua_getfield(L, t, "shadow");
	if (lua_istable(L, -1)) {
		int o = lua_gettop(L);
		lua_getfield(L, o, "x");
		*dx = lua_isnil(L, -1) ? 1 : (int)luaL_checkinteger(L, -1);
		lua_pop(L, 1);
		lua_getfield(L, o, "y");
		*dy = lua_isnil(L, -1) ? 1 : (int)luaL_checkinteger(L, -1);
		lua_pop(L, 1);
		*sr = color_part(L, o, "r", 1, 0);
		*sg = color_part(L, o, "g", 2, 0);
		*sb = color_part(L, o, "b", 3, 0);
		/* The shadow's own alpha, as the game's setShadowRate (2.11.0). */
		lua_getfield(L, o, "a");
		if (!lua_isnil(L, -1)) {
			*sa = (int)luaL_checkinteger(L, -1);
			if (*sa < 0 || *sa > 255)
				return luaL_error(L, "shadow.a must be 0 to 255, not %d", *sa);
		}
		lua_pop(L, 1);
		*has_shadow = 1;
	} else if (!lua_isnil(L, -1)) {
		return luaL_error(L, "shadow must be a table {x = 1, y = 1, r, g, b, a = 255}");
	}
	lua_pop(L, 1);
	return 1;
}

int rq_lua_hud_text(lua_State *L)
{
	struct hud_cmd *c;
	const char *s;
	struct rq_gametext gt;
	int has_shadow, dx = 0, dy = 0, sr = 0, sg = 0, sb = 0, sa = 255;

	if (game_style(L, 4, &gt, &has_shadow, &dx, &dy, &sr, &sg, &sb, &sa)) {
		int x = (int)luaL_checkinteger(L, 1), y = (int)luaL_checkinteger(L, 2);
		Texture *tex, *sh = NULL;
		s = luaL_checkstring(L, 3);
		tex = rq_gametext_texture(s, &gt);
		if (!tex)
			return 0;
		if (has_shadow) {
			struct rq_gametext g2 = gt;
			g2.r = sr;
			g2.g = sg;
			g2.b = sb;
			g2.er = sr;
			g2.eg = sg;
			g2.eb = sb;
			sh = rq_gametext_texture(s, &g2);
		}
		c = push_cmd(HUD_GTEXT);
		if (!c)
			return 0;
		c->x = x;
		c->y = y;
		c->tex = tex;
		c->shadow = sh;
		c->dx = dx;
		c->dy = dy;
		c->sa = sa;
		c->gt = gt;
		c->r = c->g = c->b = 255;
		c->a = 255;
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
		lua_settop(L, 4);
		if (c->a < 0)
			c->a = 0;
		if (c->a > 255)
			c->a = 255;
		return 0;
	}
	c = push_cmd(HUD_TEXT);
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
	/* A clip set with hud.clip is the one clip_push narrows and clip_pop goes back to. */
	g_nclip = 0;
	if (on) {
		g_clip[0].x = c->x;
		g_clip[0].y = c->y;
		g_clip[0].w = c->w;
		g_clip[0].h = c->h;
		g_nclip = 1;
	}
	return 0;
}

static void emit_clip(int on, int x, int y, int w, int h)
{
	struct hud_cmd *c = push_cmd(HUD_CLIP);
	if (!c)
		return;
	c->x = x;
	c->y = y;
	c->w = w < 0 ? 0 : w;
	c->h = h < 0 ? 0 : h;
	c->cg = on;
	if (g_layer == 1)
		g_top_clip_on = on;
}

/* rq.hud.top(fn): runs fn now; what it draws goes on the top layer, drawn after the whole HUD of
 * the update. Inside, the clip starts at the whole screen; the clip outside comes back after it,
 * also when fn raises an error (which goes on to the caller). 2.11.0. */
int rq_lua_hud_top(lua_State *L)
{
	struct { int x, y, w, h; } saved[HUD_MAX_CLIP];
	int saved_layer = g_layer, saved_n = g_nclip, st;

	luaL_checktype(L, 1, LUA_TFUNCTION);
	memcpy(saved, g_clip, sizeof saved);
	g_layer = 1;
	g_nclip = 0;
	if (g_top_clip_on)
		emit_clip(0, 0, 0, 0, 0);
	lua_settop(L, 1);
	lua_pushvalue(L, 1);
	st = lua_pcall(L, 0, LUA_MULTRET, 0);
	memcpy(g_clip, saved, sizeof saved);
	g_nclip = saved_n;
	if (saved_layer == 1) {
		/* Back in an outer top: its clip again. */
		if (g_nclip > 0)
			emit_clip(1, g_clip[g_nclip - 1].x, g_clip[g_nclip - 1].y,
				  g_clip[g_nclip - 1].w, g_clip[g_nclip - 1].h);
		else if (g_top_clip_on)
			emit_clip(0, 0, 0, 0, 0);
	}
	g_layer = saved_layer;
	if (st != LUA_OK)
		return lua_error(L);
	return lua_gettop(L) - 1; /* what fn returned */
}

/* rq.hud.clip_push(x, y, w, h): narrows the clip to this box inside the one before. */
int rq_lua_hud_clip_push(lua_State *L)
{
	int x = (int)luaL_checkinteger(L, 1);
	int y = (int)luaL_checkinteger(L, 2);
	int w = (int)luaL_checkinteger(L, 3);
	int h = (int)luaL_checkinteger(L, 4);
	if (g_nclip >= HUD_MAX_CLIP)
		return luaL_error(L, "hud.clip_push: more than %d clips inside one another", HUD_MAX_CLIP);
	if (w < 0)
		w = 0;
	if (h < 0)
		h = 0;
	if (g_nclip > 0) {
		int x1 = g_clip[g_nclip - 1].x, y1 = g_clip[g_nclip - 1].y;
		int x2 = x1 + g_clip[g_nclip - 1].w, y2 = y1 + g_clip[g_nclip - 1].h;
		int nx1 = x > x1 ? x : x1, ny1 = y > y1 ? y : y1;
		int nx2 = x + w < x2 ? x + w : x2, ny2 = y + h < y2 ? y + h : y2;
		x = nx1;
		y = ny1;
		w = nx2 > nx1 ? nx2 - nx1 : 0;
		h = ny2 > ny1 ? ny2 - ny1 : 0;
	}
	g_clip[g_nclip].x = x;
	g_clip[g_nclip].y = y;
	g_clip[g_nclip].w = w;
	g_clip[g_nclip].h = h;
	g_nclip++;
	emit_clip(1, x, y, w, h);
	return 0;
}

/* rq.hud.clip_pop(): back to the clip before the last clip_push; none after the first. */
int rq_lua_hud_clip_pop(lua_State *L)
{
	(void)L;
	if (g_nclip <= 0)
		return 0;
	g_nclip--;
	if (g_nclip > 0)
		emit_clip(1, g_clip[g_nclip - 1].x, g_clip[g_nclip - 1].y,
			  g_clip[g_nclip - 1].w, g_clip[g_nclip - 1].h);
	else
		emit_clip(0, 0, 0, 0, 0);
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

int rq_lua_hud_capture_mouse(lua_State *L)
{
	(void)L;
	/* The game's choice menu over a page needs the mouse (2.7.0). */
	if (rq_hud_choosing())
		return 0;
	g_capture_mouse = 1;
	g_capture_seen = 1;
	g_block_mouse = 1;
	mouse_clear_wheel();
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
	uint32_t now = SDL_GetTicks();
	if (g_watch_logged) {
		rq_log("stall watch: rq.on_frame runs again after %.1f s", (now - g_watch_tick) / 1000.0);
		g_watch_logged = 0;
	}
	g_watch_tick = now;
	rq_ui_on_update();
	g_block_mouse = 0;
	g_block_keys = 0;
	if (g_block_some) {
		memset(g_block_key, 0, sizeof g_block_key);
		g_block_some = 0;
	}
	g_capture_last = g_capture_seen;
	g_capture_seen = 0;
	g_capture_mouse = g_capture_mouse &&
		(key_is_down_raw(VK_LBUTTON) || key_is_down_raw(VK_RBUTTON));
	clear_cmds();
	g_drew = 0;
	mouse_get_pos_raw(&g_mx, &g_my);
	g_left = key_is_down_raw(VK_LBUTTON);
	g_right = key_is_down_raw(VK_RBUTTON);
	g_left_edge = g_left && !g_prev_left;
	g_left_up = !g_left && g_prev_left;
	g_prev_left = g_left;
	g_scroll = rq_wheel_steps - g_wheel_seen;
	g_wheel_seen = rq_wheel_steps;
	text_tick();
	draw_version_banner();
	rq_item_refresh();
	rq_roster_refresh();
	rq_battle_marks_tick();
	rq_speed_tick();
	rq_sound_tick();
	if (!L || g_on_frame_n == 0) {
		g_watch_round = SDL_GetTicks();
		return g_drew;
	}
	for (int i = 0; i < g_on_frame_n; i++) {
		/* A callback can run the game's loop (a Lab job's rq.ain.call), and so this tick
		 * again inside it: keep the outer callback's place. */
		int prev_cb = g_watch_cb;
		uint32_t prev_at = g_watch_cb_at;
		int prev_depth = g_frame_depth;
		int bad;
		g_watch_cb = i;
		g_watch_cb_at = SDL_GetTicks();
		g_frame_depth = call_stack_ptr;
		lua_rawgeti(L, LUA_REGISTRYINDEX, g_on_frame_ref[i]);
		bad = lua_pcall(L, 0, 0, 0) != LUA_OK;
		g_watch_cb = prev_cb;
		g_watch_cb_at = prev_at;
		g_frame_depth = prev_depth;
		if (bad) {
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
	g_watch_round = SDL_GetTicks();
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

	/* The HUD, then the top layer (rq.hud.top); each with its own clips, off at its start. */
	for (int layer = 0; layer < 2; layer++) {
	if (clipped) {
		glDisable(GL_SCISSOR_TEST);
		clipped = 0;
	}
	for (i = 0; i < g_ncmd; i++) {
		struct hud_cmd *c = &g_cmd[i];
		if (c->layer != layer)
			continue;
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
		} else if (c->kind == HUD_GTEXT && c->tex) {
			int x = c->x, y = c->y, tw = c->tex->w, th = c->tex->h;
			struct hud_cmd white = *c;
			if (c->a <= 0)
				continue;
			white.r = white.g = white.b = 255;
			if (c->align == ALIGN_CENTER)
				x += (c->bw - tw + 1) / 2;
			else if (c->align == ALIGN_END)
				x += c->bw - tw;
			if (c->valign == ALIGN_CENTER)
				y += rq_gametext_center_y(&c->gt, c->bh, th);
			else if (c->valign == ALIGN_END)
				y += c->bh - th;
			if (c->shadow && c->sa > 0) {
				struct hud_cmd sw = white;
				sw.a = c->a * c->sa / 255;
				draw_part(c->shadow, 0, 0, c->shadow->w, c->shadow->h,
					  x + c->dx, y + c->dy, c->shadow->w, c->shadow->h, &sw);
			}
			draw_part(c->tex, 0, 0, tw, th, x, y, tw, th, &white);
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
	}
	if (clipped)
		glDisable(GL_SCISSOR_TEST);
	rq_gametext_after_draw();
}

void rq_hud_bind_lua(lua_State *L)
{
	(void)L;
}

#endif /* RQ_MODLOADER */
