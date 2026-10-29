/* Copyright (C) 2026 Rance Quest modding fork
 *
 * Hold F3 for the debug action list. Rendering Stats is an engine toggle
 * (OutFox-style, key 6). Mods only append rows through rq.debug.action.
 */

#ifdef RQ_MODLOADER

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "lua.h"
#include "lauxlib.h"

#include "system4.h"
#include "system4/utfsjis.h"

#include "gfx/gfx.h"
#include "gfx/font.h"
#include "input.h"
#include "modloader.h"
#include "modloader_priv.h"
#include "xsystem4.h"

#define DEBUG_MAX 32
#define DEBUG_BUILTIN_ID "rendering-stats"

struct debug_action {
	int used;
	int builtin;
	int toggle;
	int on;
	int vk;
	int ref;
	int down_prev;
	char id[40];
	char label[80];
	char key[16];
	char page[24];
	char mod[64];
};

static struct debug_action g_act[DEBUG_MAX];
static int g_ready;
static int g_stats_on;
static int g_pending_toggle;
static int g_primed;
static int g_in_press;

static double g_fps;
static double g_avg_fps;
static int g_vpf;
static double g_memory_mb;
static int g_verts;
static uint64_t g_fps_t0;
static uint64_t g_fps_last;
static int g_fps_frames;
static int g_fps_have;

const char *rq_lua_caller_mod(lua_State *L);

static int id_ok(const char *id)
{
	int i, n;
	if (!id || !id[0])
		return 0;
	n = (int)strlen(id);
	if (n <= 0 || n >= 40)
		return 0;
	for (i = 0; i < n; i++) {
		unsigned char c = (unsigned char)id[i];
		if (c <= 32 || c >= 127)
			return 0;
	}
	return 1;
}

static struct debug_action *find_id(const char *id)
{
	int i;
	for (i = 0; i < DEBUG_MAX; i++) {
		if (g_act[i].used && !strcmp(g_act[i].id, id))
			return &g_act[i];
	}
	return NULL;
}

static void clear_action(struct debug_action *a, lua_State *L)
{
	if (a->ref != LUA_NOREF && L)
		luaL_unref(L, LUA_REGISTRYINDEX, a->ref);
	memset(a, 0, sizeof *a);
	a->ref = LUA_NOREF;
}

static void ensure_builtin(void)
{
	struct debug_action *a;
	if (!g_ready)
		return;
	a = find_id(DEBUG_BUILTIN_ID);
	if (a)
		return;
	a = &g_act[0];
	if (a->used)
		return;
	memset(a, 0, sizeof *a);
	a->used = 1;
	a->builtin = 1;
	a->toggle = 1;
	a->vk = VK_6;
	a->ref = LUA_NOREF;
	snprintf(a->id, sizeof a->id, "%s", DEBUG_BUILTIN_ID);
	snprintf(a->label, sizeof a->label, "Rendering Stats");
	snprintf(a->key, sizeof a->key, "6");
	snprintf(a->page, sizeof a->page, "Main");
	snprintf(a->mod, sizeof a->mod, "engine");
}

void rq_debug_init(void)
{
	int i;
	if (g_ready)
		return;
	memset(g_act, 0, sizeof g_act);
	for (i = 0; i < DEBUG_MAX; i++)
		g_act[i].ref = LUA_NOREF;
	g_ready = 1;
	g_pending_toggle = 0;
	g_stats_on = 0;
	ensure_builtin();
}

void rq_debug_unload_mod(const char *mod)
{
	lua_State *L;
	int i;
	if (!g_ready || !mod || !mod[0])
		return;
	L = rq_lua();
	for (i = 0; i < DEBUG_MAX; i++) {
		if (!g_act[i].used || g_act[i].builtin)
			continue;
		if (strcmp(g_act[i].mod, mod) != 0)
			continue;
		clear_action(&g_act[i], L);
	}
}

static int remove_id(const char *id)
{
	struct debug_action *a;
	if (!id_ok(id))
		return 0;
	if (!strcmp(id, DEBUG_BUILTIN_ID))
		return 0;
	a = find_id(id);
	if (!a)
		return 1;
	clear_action(a, rq_lua());
	return 1;
}

int rq_debug_swallows(int code)
{
	int i;
	if (!g_ready)
		return 0;
	if (code == VK_F3)
		return 1;
	if (!key_is_down_raw(VK_F3))
		return 0;
	for (i = 0; i < DEBUG_MAX; i++) {
		if (g_act[i].used && g_act[i].vk == code)
			return 1;
	}
	return 0;
}

int rq_debug_wants_frame(void)
{
	if (!g_ready)
		return 0;
	return key_is_down_raw(VK_F3) || g_stats_on;
}

void rq_debug_add_verts(int n)
{
	if (n > 0)
		g_verts += n;
}

#ifdef _WIN32
typedef struct rq_pmc {
	unsigned long cb;
	unsigned long page_faults;
	size_t peak_working_set;
	size_t working_set;
	size_t quota_peak_paged;
	size_t quota_paged;
	size_t quota_peak_nonpaged;
	size_t quota_nonpaged;
	size_t pagefile;
	size_t peak_pagefile;
} rq_pmc;

__declspec(dllimport) int __stdcall GetProcessMemoryInfo(void *process, rq_pmc *pmc, unsigned long cb);
__declspec(dllimport) void *__stdcall GetCurrentProcess(void);
#endif

static void sample_memory(void)
{
#ifdef _WIN32
	rq_pmc pmc;
	memset(&pmc, 0, sizeof pmc);
	pmc.cb = sizeof pmc;
	if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof pmc))
		g_memory_mb = (double)pmc.working_set / (1024.0 * 1024.0);
#endif
}

void rq_debug_on_present(void)
{
	uint64_t now = SDL_GetTicks64();
	g_vpf = g_verts;
	g_verts = 0;
	sample_memory();
	if (!g_fps_have) {
		g_fps_t0 = now;
		g_fps_last = now;
		g_fps_frames = 1;
		g_fps_have = 1;
		g_fps = 0;
		g_avg_fps = 0;
		return;
	}
	g_fps_frames++;
	if (now > g_fps_last) {
		double inst = 1000.0 / (double)(now - g_fps_last);
		if (g_fps <= 0.0)
			g_fps = inst;
		else
			g_fps = g_fps * 0.85 + inst * 0.15;
	}
	g_fps_last = now;
	if (now > g_fps_t0)
		g_avg_fps = (double)g_fps_frames * 1000.0 / (double)(now - g_fps_t0);
}

static void call_press(struct debug_action *a)
{
	lua_State *L;
	if (!a || a->builtin || a->ref == LUA_NOREF || g_in_press)
		return;
	L = rq_lua();
	if (!L)
		return;
	g_in_press = 1;
	lua_rawgeti(L, LUA_REGISTRYINDEX, a->ref);
	if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
		rq_log("debug action %s: %s", a->id, lua_tostring(L, -1));
		lua_pop(L, 1);
	}
	g_in_press = 0;
}

void rq_debug_tick(void)
{
	int i, nfire, fire[DEBUG_MAX];
	int f3;
	if (!g_ready)
		return;
	ensure_builtin();
	if (g_pending_toggle) {
		g_stats_on = !g_stats_on;
		g_pending_toggle = 0;
	}
	for (i = 0; i < DEBUG_MAX; i++) {
		if (g_act[i].used && g_act[i].toggle)
			g_act[i].on = g_stats_on;
	}
	f3 = key_is_down_raw(VK_F3) ? 1 : 0;
	nfire = 0;
	for (i = 0; i < DEBUG_MAX; i++) {
		int down;
		if (!g_act[i].used)
			continue;
		down = key_is_down_raw((enum sact_keycode)g_act[i].vk) ? 1 : 0;
		if (!g_primed) {
			g_act[i].down_prev = down;
			continue;
		}
		if (f3 && down && !g_act[i].down_prev) {
			if (g_act[i].builtin)
				g_pending_toggle = 1;
			else if (nfire < DEBUG_MAX)
				fire[nfire++] = i;
		}
		g_act[i].down_prev = down;
	}
	g_primed = 1;
	for (i = 0; i < nfire; i++) {
		if (g_act[fire[i]].used)
			call_press(&g_act[fire[i]]);
	}
}

static void row_label(const struct debug_action *a, char *dst, int n)
{
	if (a->toggle)
		snprintf(dst, n, "%s: %s", a->label, a->on ? "On" : "Off");
	else
		snprintf(dst, n, "%s", a->label);
}

int rq_debug_snapshot(struct rq_debug_view *out)
{
	int i, n, menu;
	if (!out)
		return 0;
	memset(out, 0, sizeof *out);
	if (!g_ready)
		return 0;
	menu = key_is_down_raw(VK_F3) ? 1 : 0;
	if (!menu && !g_stats_on)
		return 0;
	out->menu = menu;
	out->stats = g_stats_on;
	n = 0;
	for (i = 0; i < DEBUG_MAX && n < RQ_DEBUG_ACTIONS; i++) {
		struct rq_debug_row *row;
		if (!g_act[i].used)
			continue;
		row = &out->actions[n++];
		snprintf(row->id, sizeof row->id, "%s", g_act[i].id);
		snprintf(row->key, sizeof row->key, "%s", g_act[i].key);
		snprintf(row->page, sizeof row->page, "%s", g_act[i].page);
		row_label(&g_act[i], row->label, (int)sizeof row->label);
		row->on = g_act[i].on ? 1 : 0;
	}
	out->nactions = n;
	if (!g_stats_on)
		return 1;
	out->fps = g_fps;
	out->avg_fps = g_avg_fps;
	out->memory_mb = g_memory_mb;
	out->vpf = g_vpf;
	rq_ui_layers(out->screen, (int)sizeof out->screen, out->overlays,
		RQ_DEBUG_OV, &out->nov);
	if (menu && out->nov < RQ_DEBUG_OV) {
		int k, have = 0;
		for (k = 0; k < out->nov; k++) {
			if (!strcmp(out->overlays[k], "debug"))
				have = 1;
		}
		if (!have) {
			snprintf(out->overlays[out->nov], sizeof out->overlays[0], "debug");
			out->nov++;
		}
	}
	return 1;
}

static void make_ts(struct text_style *ts)
{
	memset(ts, 0, sizeof *ts);
	ts->face = FONT_GOTHIC;
	ts->size = 16.0f;
	ts->weight = FW_NORMAL;
	ts->edge_left = 1.0f;
	ts->edge_up = 1.0f;
	ts->edge_right = 1.0f;
	ts->edge_down = 1.0f;
	ts->color.r = 245;
	ts->color.g = 245;
	ts->color.b = 245;
	ts->color.a = 255;
	ts->edge_color.a = 255;
	ts->scale_x = 1.0f;
	ts->space_scale_x = 1.0f;
}

static void draw_text(int x, int y, const char *utf)
{
	struct text_style ts;
	Texture t;
	Rectangle r;
	char *sjis;
	int tw, th;
	if (!utf || !utf[0])
		return;
	make_ts(&ts);
	sjis = utf2sjis(utf, 0);
	if (!sjis)
		return;
	tw = (int)ceilf(gfx_size_text(&ts, sjis)) + 8;
	if (tw < 16)
		tw = 16;
	th = 22;
	gfx_init_texture_rgba(&t, tw, th, (SDL_Color){ 0, 0, 0, 0 });
	gfx_render_text(&t, 0.0f, 0, sjis, &ts, false);
	free(sjis);
	r = RECT(x, y, t.w, t.h);
	gfx_render_texture(&t, &r);
	gfx_delete_texture(&t);
}

static void draw_rect(int x, int y, int w, int h, int r, int g, int b, int a)
{
	Texture t;
	Rectangle rect;
	SDL_Color col;
	if (w <= 0 || h <= 0)
		return;
	col = (SDL_Color){ (uint8_t)r, (uint8_t)g, (uint8_t)b, (uint8_t)a };
	gfx_init_texture_rgba(&t, w, h, col);
	rect = RECT(x, y, w, h);
	gfx_render_texture(&t, &rect);
	gfx_delete_texture(&t);
}

static int page_seen(const char pages[][24], int n, const char *page)
{
	int i;
	for (i = 0; i < n; i++) {
		if (!strcmp(pages[i], page))
			return 1;
	}
	return 0;
}

static void draw_rows(const struct rq_debug_view *v, const char *page, int *y, int *widest)
{
	int i;
	for (i = 0; i < v->nactions; i++) {
		char line[128];
		int w;
		if (strcmp(v->actions[i].page, page) != 0)
			continue;
		snprintf(line, sizeof line, "%s  %s", v->actions[i].key, v->actions[i].label);
		w = (int)strlen(line) * 9 + 24;
		if (w > *widest)
			*widest = w;
		draw_text(16, *y, line);
		*y += 18;
	}
}

void rq_debug_draw(void)
{
	struct rq_debug_view v;
	char pages[RQ_DEBUG_ACTIONS][24];
	int npages = 0;
	int i, y, widest, sw;
	if (!rq_debug_snapshot(&v))
		return;
	sw = config.view_width > 0 ? config.view_width : 1024;
	if (v.menu) {
		y = 12;
		widest = 160;
		draw_rect(8, 8, 280, 28 + 18 * (v.nactions + 4), 12, 16, 28, 210);
		draw_text(16, y, "Main");
		y += 20;
		draw_rows(&v, "Main", &y, &widest);
		for (i = 0; i < v.nactions; i++) {
			if (!strcmp(v.actions[i].page, "Main"))
				continue;
			if (page_seen(pages, npages, v.actions[i].page))
				continue;
			if (npages < RQ_DEBUG_ACTIONS) {
				snprintf(pages[npages], sizeof pages[0], "%s", v.actions[i].page);
				npages++;
			}
		}
		for (i = 0; i < npages; i++) {
			y += 6;
			draw_text(16, y, pages[i]);
			y += 20;
			draw_rows(&v, pages[i], &y, &widest);
		}
		(void)widest;
	}
	if (!v.stats)
		return;
	{
		char line[64];
		int x = sw - 220;
		int yy = 8;
		int h = 18 * (5 + v.nov) + 16;
		if (x < 8)
			x = 8;
		draw_rect(x, 8, 208, h, 12, 16, 28, 210);
		snprintf(line, sizeof line, "FPS %.1f", v.fps);
		draw_text(x + 8, yy, line);
		yy += 18;
		snprintf(line, sizeof line, "Memory %.1f MB", v.memory_mb);
		draw_text(x + 8, yy, line);
		yy += 18;
		snprintf(line, sizeof line, "Avg FPS %.1f", v.avg_fps);
		draw_text(x + 8, yy, line);
		yy += 18;
		snprintf(line, sizeof line, "VPF %d", v.vpf);
		draw_text(x + 8, yy, line);
		yy += 18;
		snprintf(line, sizeof line, "%s", v.screen[0] ? v.screen : "other");
		draw_text(x + 8, yy, line);
		yy += 18;
		for (i = 0; i < v.nov; i++) {
			draw_text(x + 8, yy, v.overlays[i]);
			yy += 18;
		}
	}
}

int rq_lua_debug_action(lua_State *L)
{
	const char *id, *label, *key, *page, *mod;
	struct debug_action *slot;
	int vk, ref, i;
	if (!g_ready)
		rq_debug_init();
	if (lua_type(L, 1) == LUA_TSTRING) {
		if (!remove_id(lua_tostring(L, 1)))
			return luaL_error(L, "debug.action: cannot remove that id");
		lua_pushboolean(L, 1);
		return 1;
	}
	luaL_checktype(L, 1, LUA_TTABLE);
	lua_getfield(L, 1, "id");
	id = lua_tostring(L, -1);
	lua_getfield(L, 1, "label");
	label = lua_tostring(L, -1);
	lua_getfield(L, 1, "key");
	key = lua_tostring(L, -1);
	lua_getfield(L, 1, "page");
	page = lua_tostring(L, -1);
	lua_getfield(L, 1, "onPress");
	if (!id_ok(id))
		return luaL_error(L, "debug.action: id required");
	if (!label && !key && lua_isnil(L, -1)) {
		if (!remove_id(id))
			return luaL_error(L, "debug.action: cannot remove that id");
		lua_pushboolean(L, 1);
		return 1;
	}
	if (!label || !label[0] || strlen(label) >= 80)
		return luaL_error(L, "debug.action: label required");
	if (!key || !key[0] || strlen(key) >= 16)
		return luaL_error(L, "debug.action: key required");
	if (!lua_isfunction(L, -1))
		return luaL_error(L, "debug.action: onPress function required");
	if (!strcmp(id, DEBUG_BUILTIN_ID))
		return luaL_error(L, "debug.action: rendering-stats is engine-owned");
	vk = rq_key_from_name(key);
	if (vk < 0)
		return luaL_error(L, "debug.action: unknown key");
	if (vk == VK_F3 || vk == VK_6)
		return luaL_error(L, "debug.action: that key is reserved");
	if (!page || !page[0])
		page = "Main";
	if (strlen(page) >= 24)
		return luaL_error(L, "debug.action: page is too long");
	slot = find_id(id);
	if (!slot) {
		for (i = 0; i < DEBUG_MAX; i++) {
			if (!g_act[i].used) {
				slot = &g_act[i];
				break;
			}
		}
	}
	if (!slot)
		return luaL_error(L, "debug.action: too many actions");
	if (slot->builtin)
		return luaL_error(L, "debug.action: rendering-stats is engine-owned");
	lua_pushvalue(L, -1);
	ref = luaL_ref(L, LUA_REGISTRYINDEX);
	if (slot->used && slot->ref != LUA_NOREF)
		luaL_unref(L, LUA_REGISTRYINDEX, slot->ref);
	memset(slot, 0, sizeof *slot);
	slot->used = 1;
	slot->vk = vk;
	slot->ref = ref;
	slot->down_prev = key_is_down_raw((enum sact_keycode)vk) ? 1 : 0;
	snprintf(slot->id, sizeof slot->id, "%s", id);
	snprintf(slot->label, sizeof slot->label, "%s", label);
	snprintf(slot->key, sizeof slot->key, "%s", key);
	snprintf(slot->page, sizeof slot->page, "%s", page);
	mod = rq_lua_caller_mod(L);
	snprintf(slot->mod, sizeof slot->mod, "%s", mod ? mod : "lua");
	lua_pushboolean(L, 1);
	return 1;
}

#endif /* RQ_MODLOADER */
