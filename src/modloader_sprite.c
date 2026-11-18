/* Copyright (C) 2026 Rance Quest modding fork
 *
 * rq.sprite: sprites a mod makes once and changes later, drawn in the game's
 * scene list among its own sprites. A game window above one covers it, unlike rq.hud.
 *
 * They are nodes of the loader's own (struct msprite), not Chipmunk sprites or
 * GoatGUIEngine parts: a save writes every part into the save file and a load
 * deletes every Chipmunk sprite (SYS_セーブ実行_ロード後復帰処理), so a mod's sprite
 * there would be saved, deleted, or lose its number to the game. These are in no
 * table the game saves, deletes or numbers from. A load (セーブファイル_読込) and the
 * title remove them all (lua/sprite.lua), and rq.sprite.on_reset tells the mods.
 *
 * rq.sprite.scene and the pipe's "scene" command list the whole scene, back to front.
 *
 * Since 1.11.0 a sprite can follow a battle card, a character's card, another mod sprite
 * or a game sprite (attach): x, y and z are then added to that one's, it is hidden while
 * that one is, and it goes when that one goes (a card's at the end of the fight).
 *
 * Since 1.10.0 a sprite can show a GIF, its frames decoded once into textures
 * (rq_gif_frames), and move with s:to. Both run on the game's clock, so game-speed
 * makes them faster, unless clock = "real".
 */

#ifdef RQ_MODLOADER

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <SDL.h>
#include "lua.h"
#include "lauxlib.h"
#include "cJSON.h"
#include "system4.h"
#include "system4/cg.h"
#include "asset_manager.h"
#include "gfx/gfx.h"
#include "scene.h"
#include "sprite.h"
#include "vm.h"
#include "sact.h"
#include "modloader.h"
#include "modloader_priv.h"

#define SPR_MAX 1024
#define SPR_PER_MOD 256
#define SPR_CG_MAX 256
#define SPR_RESET_CB 64
#define SPR_GIF_MAX 64
#define SPR_ATTACH_DEPTH 8
#define SPR_GIF_PIXELS (64u * 1024u * 1024u)
#define SPR_META "rq.sprite"
/* Named layers, from the z map:
 *   1-22 the hub and quest list pages, 101-262 the fight's field and cards and the dungeon view,
 *   801-823 the bar at the bottom, 8001-8301 messages, 9001-9002 fades, 9101 the System menu,
 *   15000-15010 Config. */
#define SPR_Z_BACK 0
#define SPR_Z_CARDS 300
#define SPR_Z_UNDER_WINDOWS 1000
#define SPR_Z_TOP 20000
#define SPR_Z_DEFAULT SPR_Z_UNDER_WINDOWS

bool rq_parts_describe(struct sprite *sp, int *no, int *type, int *x, int *y, int *w, int *h, int *cg);

extern const char rq_sprite_lua[];
extern const unsigned rq_sprite_lua_len;

enum { SPR_NONE, SPR_CG, SPR_FILL, SPR_TEXT, SPR_GIF };
enum { KEEP_NONE, KEEP_SCREEN };
enum { MV_X, MV_Y, MV_W, MV_H, MV_SX, MV_SY, MV_ROT, MV_ALPHA, MV_TR, MV_TG, MV_TB, MV_AR, MV_AG, MV_AB, MV_N };
enum { EASE_LINEAR, EASE_IN, EASE_OUT, EASE_IN_OUT };
enum { AT_NONE, AT_CARD, AT_CHARA, AT_SPRITE, AT_GAME };

struct spr_gif;

/* s:to: from the values it had to these, over ms after delay. */
struct motion {
	int on;
	unsigned mask; /* 1 << MV_* */
	float from[MV_N], to[MV_N];
	double t0;     /* the clock when it starts to move */
	double ms;
	int ease;
	int end;       /* on_end, a registry ref */
};

/* One texture per CG, shared by every sprite that shows it. */
struct spr_cg {
	int no;
	int refs;
	int stale;
	Texture t;
};

struct msprite {
	struct sprite sp; /* first: the scene calls render with it */
	int id;
	int ref;          /* the Lua object, in the registry */
	char mod[64];
	int kind;
	int cg;           /* SPR_CG */
	struct spr_cg *cgt;
	int src;          /* a part of the CG */
	int sx, sy, sw, sh;
	int fr, fg, fb, fa; /* SPR_FILL */
	char *text;       /* SPR_TEXT */
	float size;
	int tr, tg, tb, edge, bold;
	Texture own;      /* fill and text */
	int nat_w, nat_h; /* the content's own size */
	float x, y, w, h; /* w, h 0: the content's size */
	float scale_x, scale_y, rotate;
	float ox, oy;
	int center;       /* origin in the middle */
	int alpha;
	int mr, mg, mb;   /* tint */
	int ar, ag, ab;   /* add */
	int additive;
	int show;
	int z;
	int keep;
	char screen[24];
	struct spr_gif *gif; /* SPR_GIF */
	char gif_name[128];  /* as the mod gave it */
	int cur;             /* the frame shown, from 0 */
	int playing, loop, from, to;
	double fps;          /* 0: the GIF's own delays */
	double t0;           /* the clock when frame `from` began */
	int real;            /* clock = "real" */
	int gif_end;         /* on_end of s:play, a registry ref */
	struct motion mv;
	int at;              /* attach: AT_* */
	int at_no;           /* card no, iic, mod sprite id or game sprite no */
	float bx, by;        /* the target's top left; x, y are added to it */
	int bz;              /* the target's z; z is added to it */
	int at_hidden;       /* the target is not shown */
	unsigned at_seen;    /* the tick it last followed */
};

static struct msprite *g_spr[SPR_MAX];
static int g_next_id = 1;
static int g_count;
static struct spr_cg g_cgs[SPR_CG_MAX];
static int g_reset_cb[SPR_RESET_CB];
static int g_nreset;
static int g_keepers;
static lua_State *g_L;
static unsigned g_tick = 1;

static struct spr_shader {
	Shader s;
	GLint blend_rate, multiply_color, add_color, bot_left, top_right;
	int loaded;
} g_sh;

/* ---- textures ---- */

static struct spr_cg *cg_ref(int no)
{
	int i, free_i = -1;
	struct cg *cg;
	for (i = 0; i < SPR_CG_MAX; i++) {
		if (g_cgs[i].refs > 0 && g_cgs[i].no == no) {
			g_cgs[i].refs++;
			return &g_cgs[i];
		}
		if (g_cgs[i].refs <= 0 && free_i < 0)
			free_i = i;
	}
	if (free_i < 0 || !(cg = asset_cg_load(no)))
		return NULL;
	memset(&g_cgs[free_i], 0, sizeof g_cgs[free_i]);
	g_cgs[free_i].no = no;
	g_cgs[free_i].refs = 1;
	gfx_init_texture_with_cg(&g_cgs[free_i].t, cg);
	cg_free(cg);
	return &g_cgs[free_i];
}

static void cg_unref(struct spr_cg *c)
{
	if (!c || --c->refs > 0)
		return;
	gfx_delete_texture(&c->t);
	memset(c, 0, sizeof *c);
}

void rq_sprite_cg_changed(int no)
{
	int i;
	for (i = 0; i < SPR_CG_MAX; i++) {
		if (g_cgs[i].refs > 0 && g_cgs[i].no == no)
			g_cgs[i].stale = 1;
	}
}

/* A CG that rq.image changed is loaded again before the next draw. */
static int reload_stale(void)
{
	int i, any = 0;
	for (i = 0; i < SPR_CG_MAX; i++) {
		struct cg *cg;
		if (g_cgs[i].refs <= 0 || !g_cgs[i].stale)
			continue;
		g_cgs[i].stale = 0;
		if (!(cg = asset_cg_load(g_cgs[i].no)))
			continue;
		gfx_delete_texture(&g_cgs[i].t);
		gfx_init_texture_with_cg(&g_cgs[i].t, cg);
		cg_free(cg);
		any = 1;
	}
	if (any) {
		for (i = 0; i < SPR_MAX; i++) {
			struct msprite *s = g_spr[i];
			if (s && s->kind == SPR_CG && s->cgt) {
				s->nat_w = s->cgt->t.w;
				s->nat_h = s->cgt->t.h;
			}
		}
	}
	return any;
}


/* A callback the loader holds for a sprite, let go. */
static void unref_fn(int *ref)
{
	if (*ref != LUA_NOREF && *ref != LUA_REFNIL && g_L)
		luaL_unref(g_L, LUA_REGISTRYINDEX, *ref);
	*ref = LUA_NOREF;
}

/* ---- GIFs ---- */

/* The frames of one GIF file as textures, shared by every sprite that shows it. */
struct spr_gif {
	char path[RQ_PATH_MAX];
	int refs;
	int w, h, n;
	Texture *t;
	int *at;   /* where each frame starts, in ms from the first */
	int total; /* ms for all of them */
};

static struct spr_gif g_gifs[SPR_GIF_MAX];
static uint64_t g_gif_pixels;

struct gif_load {
	struct spr_gif *g;
	int cap;
	uint64_t pixels;
	uint8_t *rgba;
};

static const char *gif_frame(void *ctx, const uint8_t *argb, int w, int h, int delay_cs)
{
	struct gif_load *ld = ctx;
	struct spr_gif *g = ld->g;
	size_t i, cells = (size_t)w * h;

	ld->pixels += cells;
	if (g_gif_pixels + ld->pixels > SPR_GIF_PIXELS)
		return "the GIFs on sprites would pass 64 million pixels in all";
	if (g->n == ld->cap) {
		ld->cap = ld->cap ? ld->cap * 2 : 16;
		g->t = xrealloc(g->t, ld->cap * sizeof *g->t);
		g->at = xrealloc(g->at, ld->cap * sizeof *g->at);
	}
	if (!ld->rgba)
		ld->rgba = xmalloc(cells * 4);
	for (i = 0; i < cells; i++) {
		ld->rgba[i * 4] = argb[i * 4 + 1];
		ld->rgba[i * 4 + 1] = argb[i * 4 + 2];
		ld->rgba[i * 4 + 2] = argb[i * 4 + 3];
		ld->rgba[i * 4 + 3] = argb[i * 4];
	}
	memset(&g->t[g->n], 0, sizeof g->t[g->n]);
	gfx_init_texture_with_pixels(&g->t[g->n], w, h, ld->rgba);
	g->at[g->n] = g->total;
	/* As browsers do: a delay under 20 ms plays as 100 ms. */
	g->total += delay_cs >= 2 ? delay_cs * 10 : 100;
	g->n++;
	return NULL;
}

static void gif_free(struct spr_gif *g)
{
	int i;
	for (i = 0; i < g->n; i++)
		gfx_delete_texture(&g->t[i]);
	free(g->t);
	free(g->at);
	memset(g, 0, sizeof *g);
}

/* The frames of the GIF at path, decoded the first time. NULL with *err on a bad file. */
static struct spr_gif *gif_ref(const char *path, const char **err)
{
	struct gif_load ld = { 0 };
	int i, free_i = -1, count;
	for (i = 0; i < SPR_GIF_MAX; i++) {
		if (g_gifs[i].refs > 0 && !strcmp(g_gifs[i].path, path)) {
			g_gifs[i].refs++;
			return &g_gifs[i];
		}
		if (g_gifs[i].refs <= 0 && free_i < 0)
			free_i = i;
	}
	if (free_i < 0) {
		*err = "too many GIFs on sprites";
		return NULL;
	}
	ld.g = &g_gifs[free_i];
	memset(ld.g, 0, sizeof *ld.g);
	*err = rq_gif_frames(path, gif_frame, &ld, &ld.g->w, &ld.g->h, &count);
	free(ld.rgba);
	if (*err) {
		gif_free(ld.g);
		return NULL;
	}
	snprintf(ld.g->path, sizeof ld.g->path, "%s", path);
	ld.g->refs = 1;
	g_gif_pixels += ld.pixels;
	return ld.g;
}

static void gif_unref(struct spr_gif *g)
{
	if (!g || --g->refs > 0)
		return;
	g_gif_pixels -= (uint64_t)g->w * g->h * g->n;
	gif_free(g);
}

static Texture *spr_texture(struct msprite *s)
{
	if (s->kind == SPR_GIF)
		return s->gif ? &s->gif->t[s->cur] : NULL;
	if (s->kind == SPR_CG)
		return s->cgt ? &s->cgt->t : NULL;
	return s->own.handle ? &s->own : NULL;
}

static void drop_content(struct msprite *s)
{
	if (s->gif)
		gif_unref(s->gif);
	s->gif = NULL;
	s->gif_name[0] = 0;
	s->playing = 0;
	unref_fn(&s->gif_end);
	if (s->cgt)
		cg_unref(s->cgt);
	s->cgt = NULL;
	if (s->own.handle)
		gfx_delete_texture(&s->own);
	memset(&s->own, 0, sizeof s->own);
	free(s->text);
	s->text = NULL;
	s->kind = SPR_NONE;
	s->nat_w = s->nat_h = 0;
}

/* ---- drawing ---- */

static void prepare_shader(struct gfx_render_job *job, void *data)
{
	struct spr_shader *sh = (struct spr_shader *)job->shader;
	struct msprite *s = data;
	float a = s->alpha / 255.0f, r = s->mr / 255.0f, g = s->mg / 255.0f, b = s->mb / 255.0f;
	Texture *t = spr_texture(s);
	if (s->kind == SPR_FILL) {
		a *= s->fa / 255.0f;
		r *= s->fr / 255.0f;
		g *= s->fg / 255.0f;
		b *= s->fb / 255.0f;
	}
	glUniform1f(sh->blend_rate, a);
	glUniform3f(sh->multiply_color, r, g, b);
	glUniform3f(sh->add_color, s->ar / 255.0f, s->ag / 255.0f, s->ab / 255.0f);
	if (s->src && s->kind == SPR_CG) {
		glUniform2f(sh->bot_left, (float)s->sx, (float)s->sy);
		glUniform2f(sh->top_right, (float)(s->sx + s->sw), (float)(s->sy + s->sh));
	} else {
		glUniform2f(sh->bot_left, 0, 0);
		glUniform2f(sh->top_right, (float)t->w, (float)t->h);
	}
}

static void load_shader(void)
{
	if (g_sh.loaded)
		return;
	gfx_load_shader(&g_sh.s, "shaders/parts.v.glsl", "shaders/parts.f.glsl");
	g_sh.blend_rate = glGetUniformLocation(g_sh.s.program, "blend_rate");
	g_sh.multiply_color = glGetUniformLocation(g_sh.s.program, "multiply_color");
	g_sh.add_color = glGetUniformLocation(g_sh.s.program, "add_color");
	g_sh.bot_left = glGetUniformLocation(g_sh.s.program, "bot_left");
	g_sh.top_right = glGetUniformLocation(g_sh.s.program, "top_right");
	glUseProgram(g_sh.s.program);
	glUniform1i(glGetUniformLocation(g_sh.s.program, "use_clipper"), 0);
	glUniform1i(glGetUniformLocation(g_sh.s.program, "draw_filter"), 0);
	glUseProgram(0);
	g_sh.s.prepare = prepare_shader;
	g_sh.loaded = 1;
}

static void base_size(const struct msprite *s, float *w, float *h)
{
	*w = s->w > 0 ? s->w : (s->src && s->kind == SPR_CG ? (float)s->sw : (float)s->nat_w);
	*h = s->h > 0 ? s->h : (s->src && s->kind == SPR_CG ? (float)s->sh : (float)s->nat_h);
}

static void origin_of(const struct msprite *s, float bw, float bh, float *ox, float *oy)
{
	*ox = s->center ? bw / 2 : s->ox;
	*oy = s->center ? bh / 2 : s->oy;
}


/* ---- time ---- */

/* The game's clock follows game-speed; clock = "real" does not. */
static double now_of(const struct msprite *s)
{
	return s->real ? (double)SDL_GetTicks64() : (double)vm_game_ticks();
}

/* ms from the start of frame `from` to the start of frame f, while it plays. */
static double frame_start(const struct msprite *s, int f)
{
	if (s->fps > 0)
		return (f - s->from) * 1000.0 / s->fps;
	return s->gif->at[f] - s->gif->at[s->from];
}

static double segment_ms(const struct msprite *s)
{
	double ms = frame_start(s, s->to);
	if (s->fps > 0)
		ms += 1000.0 / s->fps;
	else
		ms += (s->to + 1 < s->gif->n ? s->gif->at[s->to + 1] : s->gif->total) - s->gif->at[s->to];
	return ms > 0 ? ms : 1;
}

static int frame_at(const struct msprite *s, double e)
{
	int f;
	if (s->fps > 0) {
		f = s->from + (int)(e * s->fps / 1000.0);
		return f > s->to ? s->to : f;
	}
	for (f = s->from; f < s->to && frame_start(s, f + 1) <= e; f++)
		;
	return f;
}

/* After fps or the clock changed (old_now on the old clock): the frame shown goes on
 * from its start, and a motion from where it was. */
static void rebase(struct msprite *s, double old_now)
{
	if (s->gif && s->playing)
		s->t0 = now_of(s) - frame_start(s, s->cur);
	if (s->mv.on)
		s->mv.t0 = now_of(s) - (old_now - s->mv.t0);
}

static void play_from(struct msprite *s, int from, int to, int loop)
{
	s->from = from;
	s->to = to;
	s->loop = loop;
	s->cur = from;
	s->playing = 1;
	s->t0 = now_of(s);
}

/* ---- motion ---- */

static float mv_get(const struct msprite *s, int i)
{
	float bw, bh;
	switch (i) {
	case MV_X: return s->x;
	case MV_Y: return s->y;
	case MV_W: base_size(s, &bw, &bh); return bw;
	case MV_H: base_size(s, &bw, &bh); return bh;
	case MV_SX: return s->scale_x;
	case MV_SY: return s->scale_y;
	case MV_ROT: return s->rotate;
	case MV_ALPHA: return (float)s->alpha;
	case MV_TR: return (float)s->mr;
	case MV_TG: return (float)s->mg;
	case MV_TB: return (float)s->mb;
	case MV_AR: return (float)s->ar;
	case MV_AG: return (float)s->ag;
	default: return (float)s->ab;
	}
}

static int byte_round(float v)
{
	int n = (int)lroundf(v);
	return n < 0 ? 0 : n > 255 ? 255 : n;
}

static void mv_put(struct msprite *s, int i, float v)
{
	switch (i) {
	case MV_X: s->x = v; break;
	case MV_Y: s->y = v; break;
	case MV_W: s->w = v > 0 ? v : 0; break;
	case MV_H: s->h = v > 0 ? v : 0; break;
	case MV_SX: s->scale_x = v; break;
	case MV_SY: s->scale_y = v; break;
	case MV_ROT: s->rotate = v; break;
	case MV_ALPHA: s->alpha = byte_round(v); break;
	case MV_TR: s->mr = byte_round(v); break;
	case MV_TG: s->mg = byte_round(v); break;
	case MV_TB: s->mb = byte_round(v); break;
	case MV_AR: s->ar = byte_round(v); break;
	case MV_AG: s->ag = byte_round(v); break;
	default: s->ab = byte_round(v); break;
	}
}

static double ease(int kind, double p)
{
	switch (kind) {
	case EASE_IN: return p * p;
	case EASE_OUT: return 1 - (1 - p) * (1 - p);
	case EASE_IN_OUT: return p < 0.5 ? 2 * p * p : 1 - 2 * (1 - p) * (1 - p);
	default: return p;
	}
}

/* The option names a motion moves, and the values each one stands for. */
static const struct { const char *name; unsigned bits; } mv_names[] = {
	{ "x", 1u << MV_X }, { "y", 1u << MV_Y }, { "w", 1u << MV_W }, { "h", 1u << MV_H },
	{ "scale", (1u << MV_SX) | (1u << MV_SY) }, { "scale_x", 1u << MV_SX }, { "scale_y", 1u << MV_SY },
	{ "rotate", 1u << MV_ROT }, { "alpha", 1u << MV_ALPHA },
	{ "tint", (1u << MV_TR) | (1u << MV_TG) | (1u << MV_TB) },
	{ "add", (1u << MV_AR) | (1u << MV_AG) | (1u << MV_AB) },
};

/* s:set of a value a motion moves takes it out of the motion. */
static void unmove(lua_State *L, struct msprite *s, int t)
{
	size_t i;
	if (!s->mv.on)
		return;
	for (i = 0; i < sizeof mv_names / sizeof mv_names[0]; i++) {
		lua_getfield(L, t, mv_names[i].name);
		if (!lua_isnil(L, -1))
			s->mv.mask &= ~mv_names[i].bits;
		lua_pop(L, 1);
	}
}

static void spr_render(struct sprite *base)
{
	struct msprite *s = (struct msprite *)base;
	Texture *t = spr_texture(s);
	float bw, bh, ox, oy, c, sn, u0, v0, u1, v1;
	struct gfx_vertex v[4];
	int i;

	if (!t || !t->handle || s->alpha <= 0)
		return;
	base_size(s, &bw, &bh);
	if (bw <= 0 || bh <= 0)
		return;
	load_shader();
	origin_of(s, bw, bh, &ox, &oy);
	c = cosf(s->rotate * 0.017453292f);
	sn = sinf(s->rotate * 0.017453292f);
	if (s->src && s->kind == SPR_CG) {
		u0 = (float)s->sx; v0 = (float)s->sy;
		u1 = (float)(s->sx + s->sw); v1 = (float)(s->sy + s->sh);
	} else {
		u0 = 0; v0 = 0; u1 = (float)t->w; v1 = (float)t->h;
	}
	/* Top left, top right, bottom left, bottom right (rect_index_data { 0, 1, 3, 2 }). */
	for (i = 0; i < 4; i++) {
		float lx = (i & 1) ? bw : 0, ly = (i & 2) ? bh : 0;
		float px = (lx - ox) * s->scale_x, py = (ly - oy) * s->scale_y;
		v[i].x = s->bx + s->x + ox + px * c - py * sn;
		v[i].y = s->by + s->y + oy + px * sn + py * c;
		v[i].z = 0;
		v[i].w = 1;
		v[i].u = (i & 1) ? u1 : u0;
		v[i].v = (i & 2) ? v1 : v0;
	}
	if (s->additive)
		glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE, GL_ZERO, GL_ONE);
	gfx_render_quadrilateral_with(&g_sh.s, t, v, s);
	if (s->additive)
		glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ZERO);
}

static cJSON *spr_to_json(struct sprite *base, bool verbose)
{
	struct msprite *s = (struct msprite *)base;
	cJSON *o = cJSON_CreateObject();
	(void)verbose;
	cJSON_AddStringToObject(o, "type", "rq.sprite");
	cJSON_AddNumberToObject(o, "id", s->id);
	cJSON_AddStringToObject(o, "mod", s->mod);
	return o;
}

/* In the scene when it is shown, has something to draw, and its target is shown. */
static void place(struct msprite *s)
{
	int want = s->show && !s->at_hidden && spr_texture(s) != NULL;
	int z = s->at != AT_NONE ? s->bz + s->z : s->z;
	if (s->sp.z != z)
		scene_set_sprite_z(&s->sp, z);
	if (want && !s->sp.in_scene)
		scene_register_sprite(&s->sp);
	else if (!want && s->sp.in_scene)
		scene_unregister_sprite(&s->sp);
	scene_dirty();
}

/* ---- the table ---- */

static int mod_count(const char *mod)
{
	int i, n = 0;
	for (i = 0; i < SPR_MAX; i++)
		if (g_spr[i] && !strcmp(g_spr[i]->mod, mod))
			n++;
	return n;
}

static struct msprite *by_id(int id)
{
	int i;
	for (i = 0; i < SPR_MAX; i++)
		if (g_spr[i] && g_spr[i]->id == id)
			return g_spr[i];
	return NULL;
}

static void destroy(struct msprite *s, lua_State *L)
{
	int i;
	for (i = 0; i < SPR_MAX; i++) {
		if (g_spr[i] == s) {
			g_spr[i] = NULL;
			break;
		}
	}
	if (s->sp.in_scene)
		scene_unregister_sprite(&s->sp);
	if (s->keep == KEEP_SCREEN)
		g_keepers--;
	drop_content(s);
	unref_fn(&s->mv.end);
	if (L && s->ref != LUA_NOREF)
		luaL_unref(L, LUA_REGISTRYINDEX, s->ref);
	g_count--;
	free(s);
	scene_dirty();
}


/* ---- options ---- */

static int opt_num(lua_State *L, int t, const char *key, float *out)
{
	int ok;
	lua_getfield(L, t, key);
	ok = !lua_isnil(L, -1);
	if (ok) {
		if (!lua_isnumber(L, -1))
			return luaL_error(L, "rq.sprite: %s must be a number", key);
		*out = (float)lua_tonumber(L, -1);
	}
	lua_pop(L, 1);
	return ok;
}

static int opt_int(lua_State *L, int t, const char *key, int *out)
{
	float f;
	if (!opt_num(L, t, key, &f))
		return 0;
	*out = (int)lroundf(f);
	return 1;
}

/* Field name, else array index idx, of the table at t, pushed. */
static int field_at(lua_State *L, int t, const char *name, int idx)
{
	lua_getfield(L, t, name);
	if (lua_isnil(L, -1)) {
		lua_pop(L, 1);
		lua_rawgeti(L, t, idx);
	}
	return !lua_isnil(L, -1);
}

static int byte_of(lua_State *L, int t, const char *name, int idx, int def, const char *key)
{
	lua_Number v;
	if (!field_at(L, t, name, idx)) {
		lua_pop(L, 1);
		return def;
	}
	if (!lua_isnumber(L, -1))
		return luaL_error(L, "rq.sprite: %s.%s must be a number", key, name);
	v = lua_tonumber(L, -1);
	lua_pop(L, 1);
	return v < 0 ? 0 : v > 255 ? 255 : (int)lround(v);
}

/* {r, g, b [, a]} or {1, 2, 3 [, 4]}, each 0 to 255. */
static int opt_color(lua_State *L, int t, const char *key, int *r, int *g, int *b, int *a)
{
	int c;
	lua_getfield(L, t, key);
	if (lua_isnil(L, -1)) {
		lua_pop(L, 1);
		return 0;
	}
	if (!lua_istable(L, -1))
		return luaL_error(L, "rq.sprite: %s must be a table {r, g, b}", key);
	c = lua_gettop(L);
	*r = byte_of(L, c, "r", 1, 255, key);
	*g = byte_of(L, c, "g", 2, 255, key);
	*b = byte_of(L, c, "b", 3, 255, key);
	if (a)
		*a = byte_of(L, c, "a", 4, 255, key);
	lua_pop(L, 1);
	return 1;
}

static void make_text(lua_State *L, struct msprite *s)
{
	int w = 0, h = 0, tw = 0;
	unsigned char *px;
	if (s->own.handle)
		gfx_delete_texture(&s->own);
	memset(&s->own, 0, sizeof s->own);
	s->nat_w = s->nat_h = 0;
	if (!s->text[0])
		return;
	px = rq_hud_text_rgba(s->text, s->size, s->tr, s->tg, s->tb, s->edge, s->bold, &w, &h, &tw);
	if (!px) {
		luaL_error(L, "rq.sprite: the text could not be drawn");
		return;
	}
	gfx_init_texture_with_pixels(&s->own, w, h, px);
	free(px);
	s->nat_w = w;
	s->nat_h = h;
}

static void current_screen(char *out, int n)
{
	char ov[1][24];
	int nov = 0;
	out[0] = 0;
	rq_ui_layers(out, n, ov, 0, &nov);
}

static void opt_content(lua_State *L, struct msprite *s, int t)
{
	int r, g, b, a, restyle = 0;
	float f;

	lua_getfield(L, t, "gif");
	if (!lua_isnil(L, -1)) {
		const char *name = lua_type(L, -1) == LUA_TSTRING ? lua_tostring(L, -1) : NULL;
		char full[RQ_PATH_MAX];
		const char *err;
		struct spr_gif *g;
		if (!name || !name[0] || strlen(name) >= sizeof s->gif_name)
			luaL_error(L, "rq.sprite: gif is a GIF file in the mod's folder or an rq.image.add_gif key");
		if (s->kind != SPR_GIF || strcmp(s->gif_name, name)) {
			if (!rq_anim_source(name, full, sizeof full) && !rq_sandbox_resolve(L, name, full, (int)sizeof full))
				luaL_error(L, "rq.sprite: GIF '%s' is not inside the mod folder", name);
			if (!(g = gif_ref(full, &err)))
				luaL_error(L, "rq.sprite: GIF '%s': %s", name, err);
			drop_content(s);
			s->kind = SPR_GIF;
			s->gif = g;
			snprintf(s->gif_name, sizeof s->gif_name, "%s", name);
			s->nat_w = g->w;
			s->nat_h = g->h;
			play_from(s, 0, g->n - 1, 1);
		}
	}
	lua_pop(L, 1);

	lua_getfield(L, t, "cg");
	if (!lua_isnil(L, -1)) {
		int no;
		struct spr_cg *c;
		if (!lua_isinteger(L, -1))
			luaL_error(L, "rq.sprite: cg must be a CG number (rq.image.add gives one for a file)");
		no = (int)lua_tointeger(L, -1);
		if (s->kind != SPR_CG || s->cg != no) {
			if (no <= 0 || !(c = cg_ref(no)))
				luaL_error(L, "rq.sprite: no CG %d", no);
			drop_content(s);
			s->kind = SPR_CG;
			s->cg = no;
			s->cgt = c;
			s->nat_w = c->t.w;
			s->nat_h = c->t.h;
		}
	}
	lua_pop(L, 1);

	if (opt_color(L, t, "fill", &r, &g, &b, &a)) {
		if (s->kind != SPR_FILL) {
			drop_content(s);
			s->kind = SPR_FILL;
			gfx_init_texture_rgba(&s->own, 1, 1, (SDL_Color){ 255, 255, 255, 255 });
		}
		s->fr = r; s->fg = g; s->fb = b; s->fa = a;
	}

	lua_getfield(L, t, "text");
	if (!lua_isnil(L, -1)) {
		const char *str = lua_type(L, -1) == LUA_TSTRING || lua_isnumber(L, -1) ? lua_tostring(L, -1) : NULL;
		char *dup;
		if (!str)
			luaL_error(L, "rq.sprite: text must be a string");
		dup = xstrdup(str);
		if (s->kind != SPR_TEXT) {
			drop_content(s);
			s->kind = SPR_TEXT;
			s->size = 16;
			s->tr = s->tg = s->tb = 255;
			s->edge = 1;
		} else {
			free(s->text);
		}
		s->text = dup;
		restyle = 1;
	}
	lua_pop(L, 1);
	if (s->kind != SPR_TEXT)
		return;
	if (opt_num(L, t, "size", &f)) {
		if (f < 4 || f > 200)
			luaL_error(L, "rq.sprite: size must be 4 to 200");
		s->size = f;
		restyle = 1;
	}
	if (opt_color(L, t, "color", &r, &g, &b, NULL)) {
		s->tr = r; s->tg = g; s->tb = b;
		restyle = 1;
	}
	lua_getfield(L, t, "edge");
	if (!lua_isnil(L, -1)) {
		s->edge = lua_toboolean(L, -1);
		restyle = 1;
	}
	lua_pop(L, 1);
	lua_getfield(L, t, "bold");
	if (!lua_isnil(L, -1)) {
		s->bold = lua_toboolean(L, -1);
		restyle = 1;
	}
	lua_pop(L, 1);
	if (restyle)
		make_text(L, s);
}

static void opt_src(lua_State *L, struct msprite *s, int t)
{
	static const char *const names[] = { "x", "y", "w", "h" };
	float v[4];
	int i, st;

	lua_getfield(L, t, "src");
	if (lua_isnil(L, -1)) {
		lua_pop(L, 1);
		return;
	}
	if (lua_isboolean(L, -1) && !lua_toboolean(L, -1)) {
		s->src = 0;
		lua_pop(L, 1);
		return;
	}
	if (!lua_istable(L, -1))
		luaL_error(L, "rq.sprite: src is {x, y, w, h}, or false for the whole CG");
	st = lua_gettop(L);
	for (i = 0; i < 4; i++) {
		if (!field_at(L, st, names[i], i + 1) || !lua_isnumber(L, -1))
			luaL_error(L, "rq.sprite: src needs x, y, w, h");
		v[i] = (float)lua_tonumber(L, -1);
		lua_pop(L, 1);
	}
	lua_pop(L, 1);
	if (v[0] < 0 || v[1] < 0 || v[2] <= 0 || v[3] <= 0)
		luaL_error(L, "rq.sprite: src needs x, y >= 0 and w, h > 0");
	s->src = 1;
	s->sx = (int)v[0]; s->sy = (int)v[1]; s->sw = (int)v[2]; s->sh = (int)v[3];
}

static void opt_origin(lua_State *L, struct msprite *s, int t)
{
	lua_getfield(L, t, "origin");
	if (lua_type(L, -1) == LUA_TSTRING) {
		if (strcmp(lua_tostring(L, -1), "center"))
			luaL_error(L, "rq.sprite: origin is {x, y} or \"center\"");
		s->center = 1;
	} else if (lua_istable(L, -1)) {
		int o = lua_gettop(L);
		field_at(L, o, "x", 1);
		s->ox = (float)lua_tonumber(L, -1);
		lua_pop(L, 1);
		field_at(L, o, "y", 2);
		s->oy = (float)lua_tonumber(L, -1);
		lua_pop(L, 1);
		s->center = 0;
	} else if (!lua_isnil(L, -1)) {
		luaL_error(L, "rq.sprite: origin is {x, y} or \"center\"");
	}
	lua_pop(L, 1);
}

static void opt_keep(lua_State *L, struct msprite *s, int t)
{
	const char *k;
	int want = KEEP_NONE;

	lua_getfield(L, t, "keep");
	if (lua_isnil(L, -1)) {
		lua_pop(L, 1);
		return;
	}
	k = lua_tostring(L, -1);
	if (k && !strcmp(k, "screen"))
		want = KEEP_SCREEN;
	else if (!k || strcmp(k, "removed"))
		luaL_error(L, "rq.sprite: keep is \"screen\" or \"removed\"");
	lua_pop(L, 1);
	if (want == KEEP_SCREEN && s->keep != KEEP_SCREEN) {
		current_screen(s->screen, sizeof s->screen);
		g_keepers++;
	} else if (want != KEEP_SCREEN && s->keep == KEEP_SCREEN) {
		g_keepers--;
	}
	s->keep = want;
}


/* ---- attach ---- */

/* Where the target of an attached sprite is now: its top left, its z, and whether it is
 * shown. 0 when it is gone for good, so the sprite goes too. */
static int target_of(struct msprite *s, int depth, float *x, float *y, int *z, int *shown);

static int game_sprite_at(int no, float *x, float *y, int *z, int *shown)
{
	struct sact_sprite *ss = no > 0 ? sact_try_get_sprite(no) : NULL;
	if (!ss)
		return 0;
	*x = (float)ss->rect.x;
	*y = (float)ss->rect.y;
	*z = ss->sp.z;
	*shown = !ss->sp.hidden && ss->sp.in_scene;
	return 1;
}

/* Brings an attached sprite to its target; 0 if the target is gone. */
static int follow(struct msprite *s, int depth)
{
	float x = 0, y = 0;
	int z = 0, shown = 0;

	if (s->at == AT_NONE)
		return 1;
	if (s->at_seen == g_tick)
		return 1;
	if (!target_of(s, depth, &x, &y, &z, &shown))
		return 0;
	s->at_seen = g_tick;
	s->bx = x;
	s->by = y;
	s->bz = z;
	s->at_hidden = !shown;
	return 1;
}

static int target_of(struct msprite *s, int depth, float *x, float *y, int *z, int *shown)
{
	struct msprite *t;
	int no;

	switch (s->at) {
	case AT_CARD:
	case AT_CHARA:
		/* Out of a fight it goes; a card with no one on it, or between sprites, hides it. */
		if (!rq_battle_live())
			return 0;
		no = s->at == AT_CARD ? s->at_no : rq_battle_chara_card(s->at_no);
		if (!no || !game_sprite_at(rq_battle_card_sprite(no), x, y, z, shown)) {
			*x = s->bx;
			*y = s->by;
			*z = s->bz;
			*shown = 0;
		}
		return 1;
	case AT_GAME:
		return game_sprite_at(s->at_no, x, y, z, shown);
	case AT_SPRITE:
		t = by_id(s->at_no);
		if (!t || depth >= SPR_ATTACH_DEPTH || !follow(t, depth + 1))
			return 0;
		*x = t->bx + t->x;
		*y = t->by + t->y;
		*z = t->sp.z;
		*shown = t->show && !t->at_hidden;
		return 1;
	}
	return 1;
}

static const char *attach_err = "rq.sprite: attach is {card = n}, {chara = iic}, {sprite = s}, {game = n}, or false";

/* Reads attach. Returns whether it changed. */
static int opt_attach(lua_State *L, struct msprite *s, int t)
{
	int at = AT_NONE, no = 0, keys = 0, a;
	lua_Integer n;

	lua_getfield(L, t, "attach");
	if (lua_isnil(L, -1)) {
		lua_pop(L, 1);
		return 0;
	}
	if (lua_isboolean(L, -1) && !lua_toboolean(L, -1)) {
		lua_pop(L, 1);
		a = s->at != AT_NONE;
		s->at = AT_NONE;
		s->bx = s->by = 0;
		s->bz = 0;
		s->at_hidden = 0;
		return a;
	}
	if (!lua_istable(L, -1))
		luaL_error(L, attach_err);
	a = lua_gettop(L);
	lua_getfield(L, a, "card");
	if (!lua_isnil(L, -1)) {
		keys++;
		n = lua_isinteger(L, -1) ? lua_tointeger(L, -1) : 0;
		if (!((n >= 1 && n <= 5) || (n >= 11 && n <= 15)))
			luaL_error(L, "rq.sprite: attach card is 1 to 5 (allies) or 11 to 15 (enemies)");
		if (!rq_battle_live())
			luaL_error(L, "rq.sprite: attach card needs a fight");
		at = AT_CARD;
		no = (int)n;
	}
	lua_pop(L, 1);
	lua_getfield(L, a, "chara");
	if (!lua_isnil(L, -1)) {
		keys++;
		n = lua_isinteger(L, -1) ? lua_tointeger(L, -1) : 0;
		if (n <= 0)
			luaL_error(L, "rq.sprite: attach chara is a character's iic");
		if (!rq_battle_live())
			luaL_error(L, "rq.sprite: attach chara needs a fight");
		at = AT_CHARA;
		no = (int)n;
	}
	lua_pop(L, 1);
	lua_getfield(L, a, "game");
	if (!lua_isnil(L, -1)) {
		keys++;
		n = lua_isinteger(L, -1) ? lua_tointeger(L, -1) : 0;
		if (n <= 0 || !sact_try_get_sprite((int)n))
			luaL_error(L, "rq.sprite: attach game needs a game sprite number that exists now");
		at = AT_GAME;
		no = (int)n;
	}
	lua_pop(L, 1);
	lua_getfield(L, a, "sprite");
	if (!lua_isnil(L, -1)) {
		int *id = luaL_testudata(L, -1, SPR_META);
		struct msprite *t = id ? by_id(*id) : NULL, *c;
		int depth = 0;
		keys++;
		if (!t)
			luaL_error(L, "rq.sprite: attach sprite needs a sprite that is not removed");
		for (c = t; c && depth <= SPR_ATTACH_DEPTH; depth++) {
			if (c == s)
				luaL_error(L, "rq.sprite: a sprite cannot follow itself");
			c = c->at == AT_SPRITE ? by_id(c->at_no) : NULL;
		}
		if (depth > SPR_ATTACH_DEPTH)
			luaL_error(L, "rq.sprite: at most %d sprites can follow one another", SPR_ATTACH_DEPTH);
		at = AT_SPRITE;
		no = t->id;
	}
	lua_pop(L, 1);
	if (keys != 1)
		luaL_error(L, attach_err);
	lua_pop(L, 1);
	s->at = at;
	s->at_no = no;
	s->at_seen = 0;
	return 1;
}

/* Options in the table at t. A bad one raises an error. */
static void apply_opts(lua_State *L, struct msprite *s, int t)
{
	float f;
	int n;
	const char *b;

	opt_content(L, s, t);
	opt_src(L, s, t);
	opt_num(L, t, "x", &s->x);
	opt_num(L, t, "y", &s->y);
	if (opt_num(L, t, "w", &f))
		s->w = f > 0 ? f : 0;
	if (opt_num(L, t, "h", &f))
		s->h = f > 0 ? f : 0;
	if (s->kind == SPR_FILL && (s->w <= 0 || s->h <= 0))
		luaL_error(L, "rq.sprite: a fill needs w and h");
	if (opt_num(L, t, "scale", &f))
		s->scale_x = s->scale_y = f;
	opt_num(L, t, "scale_x", &s->scale_x);
	opt_num(L, t, "scale_y", &s->scale_y);
	opt_num(L, t, "rotate", &s->rotate);
	opt_origin(L, s, t);
	if (opt_int(L, t, "alpha", &n))
		s->alpha = n < 0 ? 0 : n > 255 ? 255 : n;
	opt_color(L, t, "tint", &s->mr, &s->mg, &s->mb, NULL);
	opt_color(L, t, "add", &s->ar, &s->ag, &s->ab, NULL);
	lua_getfield(L, t, "blend");
	if (!lua_isnil(L, -1)) {
		b = lua_tostring(L, -1);
		if (b && !strcmp(b, "add"))
			s->additive = 1;
		else if (b && !strcmp(b, "normal"))
			s->additive = 0;
		else
			luaL_error(L, "rq.sprite: blend is \"normal\" or \"add\"");
	}
	lua_pop(L, 1);
	lua_getfield(L, t, "show");
	if (!lua_isnil(L, -1))
		s->show = lua_toboolean(L, -1);
	lua_pop(L, 1);
	if (opt_attach(L, s, t)) {
		lua_getfield(L, t, "z");
		if (lua_isnil(L, -1))
			s->z = s->at != AT_NONE ? 1 : SPR_Z_DEFAULT;
		lua_pop(L, 1);
	}
	lua_getfield(L, t, "z");
	if (lua_type(L, -1) == LUA_TSTRING) {
		const char *z = lua_tostring(L, -1);
		if (!strcmp(z, "back"))
			s->z = SPR_Z_BACK;
		else if (!strcmp(z, "cards"))
			s->z = SPR_Z_CARDS;
		else if (!strcmp(z, "under_windows"))
			s->z = SPR_Z_UNDER_WINDOWS;
		else if (!strcmp(z, "top"))
			s->z = SPR_Z_TOP;
		else
			luaL_error(L, "rq.sprite: z is a number or \"back\", \"cards\", \"under_windows\", \"top\"");
		lua_pop(L, 1);
	} else {
		lua_pop(L, 1);
		opt_int(L, t, "z", &s->z);
	}
	opt_keep(L, s, t);
	{
		double old_fps = s->fps, old_now = now_of(s);
		int old_real = s->real;
		if (opt_num(L, t, "fps", &f)) {
			if (f < 0 || f > 240)
				luaL_error(L, "rq.sprite: fps is 0 (the GIF's own delays) to 240");
			s->fps = f;
		}
		lua_getfield(L, t, "clock");
		if (!lua_isnil(L, -1)) {
			b = lua_tostring(L, -1);
			if (b && !strcmp(b, "real"))
				s->real = 1;
			else if (b && !strcmp(b, "game"))
				s->real = 0;
			else
				luaL_error(L, "rq.sprite: clock is \"game\" or \"real\"");
		}
		lua_pop(L, 1);
		if (s->fps != old_fps || s->real != old_real)
			rebase(s, old_now);
	}
}

/* lua_pcall target: apply_opts(upvalue sprite, arg 1). */
static int l_apply(lua_State *L)
{
	apply_opts(L, lua_touserdata(L, lua_upvalueindex(1)), 1);
	return 0;
}

static int protected_apply(lua_State *L, struct msprite *s, int t)
{
	lua_pushlightuserdata(L, s);
	lua_pushcclosure(L, l_apply, 1);
	lua_pushvalue(L, t);
	return lua_pcall(L, 1, 0, 0);
}

/* ---- Lua ---- */

static struct msprite *check_sprite(lua_State *L, int idx)
{
	int *id = luaL_checkudata(L, idx, SPR_META);
	struct msprite *s = by_id(*id);
	if (!s)
		luaL_error(L, "rq.sprite: sprite %d was removed", *id);
	return s;
}

static const char *mod_of(lua_State *L)
{
	const char *mod = rq_lua_caller_mod(L);
	return mod ? mod : "_";
}

static int l_new(lua_State *L)
{
	const char *mod = mod_of(L);
	struct msprite *s;
	int i, slot = -1, *ud;

	luaL_checktype(L, 1, LUA_TTABLE);
	if (mod_count(mod) >= SPR_PER_MOD)
		return luaL_error(L, "rq.sprite.new: a mod may have %d sprites", SPR_PER_MOD);
	for (i = 0; i < SPR_MAX && slot < 0; i++)
		if (!g_spr[i])
			slot = i;
	if (slot < 0)
		return luaL_error(L, "rq.sprite.new: %d sprites in all is the most", SPR_MAX);
	s = xcalloc(1, sizeof *s);
	s->id = g_next_id++;
	s->ref = LUA_NOREF;
	s->gif_end = LUA_NOREF;
	s->mv.end = LUA_NOREF;
	snprintf(s->mod, sizeof s->mod, "%s", mod);
	s->scale_x = s->scale_y = 1;
	s->alpha = 255;
	s->mr = s->mg = s->mb = 255;
	s->show = 1;
	s->z = s->sp.z = SPR_Z_DEFAULT;
	s->sp.has_pixel = true;
	s->sp.has_alpha = true;
	s->sp.render = spr_render;
	s->sp.to_json = spr_to_json;
	g_spr[slot] = s;
	g_count++;
	/* A bad option leaves no half-made sprite behind. */
	if (protected_apply(L, s, 1) != LUA_OK) {
		destroy(s, NULL);
		return lua_error(L);
	}
	if (s->kind == SPR_NONE) {
		destroy(s, NULL);
		return luaL_error(L, "rq.sprite.new: give cg, fill or text");
	}
	ud = lua_newuserdata(L, sizeof(int));
	*ud = s->id;
	luaL_setmetatable(L, SPR_META);
	lua_pushvalue(L, -1);
	s->ref = luaL_ref(L, LUA_REGISTRYINDEX);
	follow(s, 0);
	place(s);
	return 1;
}

static int l_set(lua_State *L)
{
	struct msprite *s = check_sprite(L, 1);
	luaL_checktype(L, 2, LUA_TTABLE);
	unmove(L, s, 2);
	apply_opts(L, s, 2);
	s->at_seen = 0;
	if (!follow(s, 0)) {
		destroy(s, L);
		lua_settop(L, 1);
		return 1;
	}
	place(s);
	lua_settop(L, 1);
	return 1;
}

static void set_color(lua_State *L, const char *key, int r, int g, int b, int a)
{
	lua_createtable(L, 0, 4);
	lua_pushinteger(L, r); lua_setfield(L, -2, "r");
	lua_pushinteger(L, g); lua_setfield(L, -2, "g");
	lua_pushinteger(L, b); lua_setfield(L, -2, "b");
	if (a >= 0) {
		lua_pushinteger(L, a);
		lua_setfield(L, -2, "a");
	}
	lua_setfield(L, -2, key);
}

static void set_num(lua_State *L, const char *key, double v)
{
	if (v == (double)(lua_Integer)v)
		lua_pushinteger(L, (lua_Integer)v);
	else
		lua_pushnumber(L, v);
	lua_setfield(L, -2, key);
}

static int l_get(lua_State *L)
{
	struct msprite *s = check_sprite(L, 1);
	float bw, bh, ox, oy;

	base_size(s, &bw, &bh);
	origin_of(s, bw, bh, &ox, &oy);
	lua_newtable(L);
	set_num(L, "id", s->id);
	lua_pushstring(L, s->mod);
	lua_setfield(L, -2, "mod");
	if (s->kind == SPR_CG) {
		set_num(L, "cg", s->cg);
		if (s->src) {
			lua_createtable(L, 0, 4);
			set_num(L, "x", s->sx); set_num(L, "y", s->sy);
			set_num(L, "w", s->sw); set_num(L, "h", s->sh);
			lua_setfield(L, -2, "src");
		}
	} else if (s->kind == SPR_FILL) {
		set_color(L, "fill", s->fr, s->fg, s->fb, s->fa);
	} else if (s->kind == SPR_TEXT) {
		lua_pushstring(L, s->text);
		lua_setfield(L, -2, "text");
		set_num(L, "size", s->size);
		set_color(L, "color", s->tr, s->tg, s->tb, -1);
		lua_pushboolean(L, s->edge);
		lua_setfield(L, -2, "edge");
		lua_pushboolean(L, s->bold);
		lua_setfield(L, -2, "bold");
	} else if (s->kind == SPR_GIF) {
		lua_pushstring(L, s->gif_name);
		lua_setfield(L, -2, "gif");
		set_num(L, "frame", s->cur + 1);
		set_num(L, "frames", s->gif->n);
		set_num(L, "gif_ms", s->gif->total);
		set_num(L, "fps", s->fps);
		lua_pushboolean(L, s->playing);
		lua_setfield(L, -2, "playing");
	}
	set_num(L, "x", s->x);
	set_num(L, "y", s->y);
	set_num(L, "w", bw);
	set_num(L, "h", bh);
	set_num(L, "scale_x", s->scale_x);
	set_num(L, "scale_y", s->scale_y);
	set_num(L, "rotate", s->rotate);
	lua_createtable(L, 0, 2);
	set_num(L, "x", ox);
	set_num(L, "y", oy);
	lua_setfield(L, -2, "origin");
	set_num(L, "alpha", s->alpha);
	set_color(L, "tint", s->mr, s->mg, s->mb, -1);
	set_color(L, "add", s->ar, s->ag, s->ab, -1);
	lua_pushstring(L, s->additive ? "add" : "normal");
	lua_setfield(L, -2, "blend");
	lua_pushboolean(L, s->show);
	lua_setfield(L, -2, "show");
	set_num(L, "z", s->z);
	lua_pushstring(L, s->keep == KEEP_SCREEN ? "screen" : "removed");
	lua_setfield(L, -2, "keep");
	if (s->keep == KEEP_SCREEN) {
		lua_pushstring(L, s->screen);
		lua_setfield(L, -2, "screen");
	}
	if (s->at != AT_NONE) {
		static const char *const keys[] = { "", "card", "chara", "sprite", "game" };
		struct msprite *t = s->at == AT_SPRITE ? by_id(s->at_no) : NULL;
		lua_createtable(L, 0, 1);
		if (s->at == AT_SPRITE && t)
			lua_rawgeti(L, LUA_REGISTRYINDEX, t->ref);
		else
			lua_pushinteger(L, s->at_no);
		lua_setfield(L, -2, keys[s->at]);
		lua_setfield(L, -2, "attach");
	}
	lua_createtable(L, 0, 3);
	set_num(L, "x", s->bx + s->x);
	set_num(L, "y", s->by + s->y);
	set_num(L, "z", s->at != AT_NONE ? s->bz + s->z : s->z);
	lua_setfield(L, -2, "drawn");
	lua_pushstring(L, s->real ? "real" : "game");
	lua_setfield(L, -2, "clock");
	lua_pushboolean(L, s->mv.on);
	lua_setfield(L, -2, "moving");
	lua_pushboolean(L, s->sp.in_scene);
	lua_setfield(L, -2, "in_scene");
	return 1;
}

static int l_remove(lua_State *L)
{
	int *id = luaL_checkudata(L, 1, SPR_META);
	struct msprite *s = by_id(*id);
	if (s)
		destroy(s, L);
	return 0;
}

static int l_alive(lua_State *L)
{
	int *id = luaL_checkudata(L, 1, SPR_META);
	lua_pushboolean(L, by_id(*id) != NULL);
	return 1;
}

static int l_tostring(lua_State *L)
{
	int *id = luaL_checkudata(L, 1, SPR_META);
	lua_pushfstring(L, "rq.sprite %d%s", *id, by_id(*id) ? "" : " (removed)");
	return 1;
}

static int l_eq(lua_State *L)
{
	int *a = luaL_checkudata(L, 1, SPR_META), *b = luaL_checkudata(L, 2, SPR_META);
	lua_pushboolean(L, *a == *b);
	return 1;
}

/* This mod's sprites, oldest first. */
static int l_list(lua_State *L)
{
	const char *mod = mod_of(L);
	static struct msprite *mine[SPR_MAX];
	int i, j, n = 0;

	for (i = 0; i < SPR_MAX; i++)
		if (g_spr[i] && !strcmp(g_spr[i]->mod, mod))
			mine[n++] = g_spr[i];
	for (i = 1; i < n; i++) {
		struct msprite *k = mine[i];
		for (j = i; j > 0 && mine[j - 1]->id > k->id; j--)
			mine[j] = mine[j - 1];
		mine[j] = k;
	}
	lua_createtable(L, n, 0);
	for (i = 0; i < n; i++) {
		lua_rawgeti(L, LUA_REGISTRYINDEX, mine[i]->ref);
		lua_rawseti(L, -2, i + 1);
	}
	return 1;
}

static int l_clear(lua_State *L)
{
	const char *mod = mod_of(L);
	int i, n = 0;
	for (i = 0; i < SPR_MAX; i++) {
		if (g_spr[i] && !strcmp(g_spr[i]->mod, mod)) {
			destroy(g_spr[i], L);
			n++;
		}
	}
	lua_pushinteger(L, n);
	return 1;
}

static int l_off_reset(lua_State *L)
{
	int i = (int)lua_tointeger(L, lua_upvalueindex(1));
	if (g_reset_cb[i] != LUA_NOREF) {
		luaL_unref(L, LUA_REGISTRYINDEX, g_reset_cb[i]);
		g_reset_cb[i] = LUA_NOREF;
	}
	return 0;
}

/* Answers the remove function the docs promise (loader 1.13.0; before, nothing). */
static int l_on_reset(lua_State *L)
{
	luaL_checktype(L, 1, LUA_TFUNCTION);
	if (g_nreset >= SPR_RESET_CB)
		return luaL_error(L, "on_reset: too many callbacks");
	lua_pushvalue(L, 1);
	g_reset_cb[g_nreset] = luaL_ref(L, LUA_REGISTRYINDEX);
	lua_pushinteger(L, g_nreset++);
	lua_pushcclosure(L, l_off_reset, 1);
	return 1;
}

/* A load or the title: every mod sprite goes, then each on_reset gets the reason. */
static int l_reset(lua_State *L)
{
	const char *why = luaL_checkstring(L, 1);
	int i, n = g_count;

	for (i = 0; i < SPR_MAX; i++)
		if (g_spr[i])
			destroy(g_spr[i], L);
	if (n)
		rq_log("rq.sprite: %s removed %d sprite%s", why, n, n == 1 ? "" : "s");
	for (i = 0; i < g_nreset; i++) {
		if (g_reset_cb[i] == LUA_NOREF)
			continue;
		lua_rawgeti(L, LUA_REGISTRYINDEX, g_reset_cb[i]);
		lua_pushstring(L, why);
		if (lua_pcall(L, 1, 0, 0) != LUA_OK) {
			rq_log("rq.sprite.on_reset: %s", lua_tostring(L, -1));
			lua_pop(L, 1);
		}
	}
	return 0;
}


/* ---- GIF and motion ---- */

static struct msprite *check_gif(lua_State *L, int idx)
{
	struct msprite *s = check_sprite(L, idx);
	if (s->kind != SPR_GIF)
		luaL_error(L, "rq.sprite: this sprite shows no gif");
	return s;
}

static int l_play(lua_State *L)
{
	struct msprite *s = check_gif(L, 1);
	int from = 1, to = s->gif->n, loop = 1, end = LUA_NOREF;

	if (!lua_isnoneornil(L, 2)) {
		luaL_checktype(L, 2, LUA_TTABLE);
		opt_int(L, 2, "from", &from);
		opt_int(L, 2, "to", &to);
		lua_getfield(L, 2, "loop");
		if (!lua_isnil(L, -1))
			loop = lua_toboolean(L, -1);
		lua_pop(L, 1);
	}
	if (from < 1 || to > s->gif->n || from > to)
		return luaL_error(L, "rq.sprite: play from 1 to %d, with from no more than to", s->gif->n);
	if (!lua_isnoneornil(L, 2)) {
		lua_getfield(L, 2, "on_end");
		if (lua_isfunction(L, -1))
			end = luaL_ref(L, LUA_REGISTRYINDEX);
		else if (!lua_isnil(L, -1))
			return luaL_error(L, "rq.sprite: on_end must be a function");
		else
			lua_pop(L, 1);
	}
	unref_fn(&s->gif_end);
	s->gif_end = end;
	play_from(s, from - 1, to - 1, loop);
	scene_dirty();
	lua_settop(L, 1);
	return 1;
}

static int l_stop(lua_State *L)
{
	struct msprite *s = check_gif(L, 1);
	s->playing = 0;
	unref_fn(&s->gif_end);
	lua_settop(L, 1);
	return 1;
}

/* s:frame() is the frame shown and how many; s:frame(n) shows frame n and stops. */
static int l_frame(lua_State *L)
{
	struct msprite *s = check_gif(L, 1);
	lua_Integer n;

	if (lua_isnoneornil(L, 2)) {
		lua_pushinteger(L, s->cur + 1);
		lua_pushinteger(L, s->gif->n);
		return 2;
	}
	n = luaL_checkinteger(L, 2);
	if (n < 1 || n > s->gif->n)
		return luaL_error(L, "rq.sprite: frame is 1 to %d", s->gif->n);
	s->cur = (int)n - 1;
	s->playing = 0;
	unref_fn(&s->gif_end);
	scene_dirty();
	lua_settop(L, 1);
	return 1;
}

static int l_playing(lua_State *L)
{
	struct msprite *s = check_sprite(L, 1);
	lua_pushboolean(L, s->kind == SPR_GIF && s->playing);
	return 1;
}

/* s:to{...}: from where it is to these values over ms, after delay. A new one takes
 * the place of the one moving now, from where that one got to. */
static int l_to(lua_State *L)
{
	struct msprite *s = check_sprite(L, 1);
	struct motion m;
	float f, delay = 0;
	int i, r, g, b;
	const char *e;

	luaL_checktype(L, 2, LUA_TTABLE);
	memset(&m, 0, sizeof m);
	m.end = LUA_NOREF;
	for (i = 0; i < MV_N; i++)
		m.from[i] = m.to[i] = mv_get(s, i);
	if (opt_num(L, 2, "scale", &f)) {
		m.to[MV_SX] = m.to[MV_SY] = f;
		m.mask |= (1u << MV_SX) | (1u << MV_SY);
	}
#define TO(key, idx) \
	if (opt_num(L, 2, key, &f)) { \
		m.to[idx] = f; \
		m.mask |= 1u << (idx); \
	}
	TO("x", MV_X)
	TO("y", MV_Y)
	TO("w", MV_W)
	TO("h", MV_H)
	TO("scale_x", MV_SX)
	TO("scale_y", MV_SY)
	TO("rotate", MV_ROT)
	TO("alpha", MV_ALPHA)
#undef TO
	if (opt_color(L, 2, "tint", &r, &g, &b, NULL)) {
		m.to[MV_TR] = (float)r; m.to[MV_TG] = (float)g; m.to[MV_TB] = (float)b;
		m.mask |= (1u << MV_TR) | (1u << MV_TG) | (1u << MV_TB);
	}
	if (opt_color(L, 2, "add", &r, &g, &b, NULL)) {
		m.to[MV_AR] = (float)r; m.to[MV_AG] = (float)g; m.to[MV_AB] = (float)b;
		m.mask |= (1u << MV_AR) | (1u << MV_AG) | (1u << MV_AB);
	}
	if (opt_num(L, 2, "ms", &f)) {
		if (f < 0)
			return luaL_error(L, "rq.sprite: ms must be 0 or more");
		m.ms = f;
	}
	if (opt_num(L, 2, "delay", &delay) && delay < 0)
		return luaL_error(L, "rq.sprite: delay must be 0 or more");
	lua_getfield(L, 2, "ease");
	if (!lua_isnil(L, -1)) {
		e = lua_tostring(L, -1);
		if (e && !strcmp(e, "linear"))
			m.ease = EASE_LINEAR;
		else if (e && !strcmp(e, "in"))
			m.ease = EASE_IN;
		else if (e && !strcmp(e, "out"))
			m.ease = EASE_OUT;
		else if (e && !strcmp(e, "in_out"))
			m.ease = EASE_IN_OUT;
		else
			return luaL_error(L, "rq.sprite: ease is \"linear\", \"in\", \"out\" or \"in_out\"");
	}
	lua_pop(L, 1);
	lua_getfield(L, 2, "on_end");
	if (lua_isfunction(L, -1))
		m.end = luaL_ref(L, LUA_REGISTRYINDEX);
	else if (!lua_isnil(L, -1))
		return luaL_error(L, "rq.sprite: on_end must be a function");
	else
		lua_pop(L, 1);
	unref_fn(&s->mv.end);
	m.t0 = now_of(s) + delay;
	m.on = 1;
	s->mv = m;
	lua_settop(L, 1);
	return 1;
}

static int l_moving(lua_State *L)
{
	struct msprite *s = check_sprite(L, 1);
	lua_pushboolean(L, s->mv.on);
	return 1;
}

/* Stops the motion where it is, without its on_end. */
static int l_halt(lua_State *L)
{
	struct msprite *s = check_sprite(L, 1);
	s->mv.on = 0;
	unref_fn(&s->mv.end);
	lua_settop(L, 1);
	return 1;
}

/* The frame a playing GIF is on now. Returns whether it changed; an on_end that is due
 * goes into fn and id when they are given. */
static int gif_step(struct msprite *s, int *fn, int *id, int *np)
{
	double e, len;
	int f;

	if (s->kind != SPR_GIF || !s->playing)
		return 0;
	e = now_of(s) - s->t0;
	len = segment_ms(s);
	if (e < 0)
		e = 0;
	if (s->loop) {
		f = frame_at(s, fmod(e, len));
	} else if (e >= len) {
		f = s->to;
		s->playing = 0;
		if (s->gif_end != LUA_NOREF && fn) {
			fn[*np] = s->gif_end;
			id[(*np)++] = s->id;
			s->gif_end = LUA_NOREF;
		}
	} else {
		f = frame_at(s, e);
	}
	if (f == s->cur)
		return 0;
	s->cur = f;
	return 1;
}

/* GIF frames and motions, once a frame. Returns whether anything changed; the on_end
 * callbacks that are due go into fn and id. */
static int advance(int *fn, int *id, int *np)
{
	int i, k, dirty = 0;

	for (i = 0; i < SPR_MAX; i++) {
		struct msprite *s = g_spr[i];
		if (!s)
			continue;
		if (gif_step(s, fn, id, np))
			dirty = 1;
		if (s->mv.on) {
			double e = now_of(s) - s->mv.t0, p;
			if (e < 0)
				continue;
			p = s->mv.ms > 0 ? e / s->mv.ms : 1;
			if (p > 1)
				p = 1;
			for (k = 0; k < MV_N; k++)
				if (s->mv.mask & (1u << k))
					mv_put(s, k, s->mv.from[k] + (s->mv.to[k] - s->mv.from[k]) * (float)ease(s->mv.ease, p));
			if (s->mv.mask)
				dirty = 1;
			if (p >= 1) {
				s->mv.on = 0;
				if (s->mv.end != LUA_NOREF) {
					fn[*np] = s->mv.end;
					id[(*np)++] = s->id;
					s->mv.end = LUA_NOREF;
				}
			}
		}
	}
	return dirty;
}

/* Each attached sprite goes to its target, and goes when that one has gone. */
static int attached(void)
{
	static int gone[SPR_MAX];
	int i, n = 0, any = 0;

	g_tick++;
	for (i = 0; i < SPR_MAX; i++) {
		struct msprite *s = g_spr[i];
		if (!s || s->at == AT_NONE)
			continue;
		any = 1;
		if (follow(s, 0))
			place(s);
		else
			gone[n++] = s->id;
	}
	for (i = 0; i < n; i++) {
		struct msprite *s = by_id(gone[i]);
		if (s)
			destroy(s, g_L);
	}
	return any;
}

/* on_end(s) for each that is due, after every sprite has moved. */
static void fire_ends(const int *fn, const int *id, int n)
{
	int i;
	for (i = 0; i < n && g_L; i++) {
		struct msprite *s = by_id(id[i]);
		lua_rawgeti(g_L, LUA_REGISTRYINDEX, fn[i]);
		if (s)
			lua_rawgeti(g_L, LUA_REGISTRYINDEX, s->ref);
		else
			lua_pushnil(g_L);
		if (lua_pcall(g_L, 1, 0, 0) != LUA_OK) {
			rq_log("rq.sprite on_end: %s", lua_tostring(g_L, -1));
			lua_pop(g_L, 1);
		}
		luaL_unref(g_L, LUA_REGISTRYINDEX, fn[i]);
	}
}


/* ---- battle faces (rq.battle.set_face) ---- */

/* A mod's picture for a character's face on a fight card. The game's face sprite stays as
 * it is: sprite_render asks rq_face_render first, which draws the picture in the sprite's
 * place, with its blend rate, multiply and add colour, so the fight's moves, shakes,
 * flashes and fades still show. The pictures are msprites of their own, not in g_spr or
 * the scene, so a load, rq.sprite.clear and the limits do not see them. */
#define FACE_MAX 64
#define FACE_CARDS 5
#define HP_CARDS 10
#define HP_CB_MAX 64

struct face {
	int iic;
	struct msprite *base; /* lasting; NULL for the game's */
	struct msprite *temp; /* for a while; NULL for the game's */
	int temp_on;
	int temp_real;
	double temp_end;
	uint64_t drawn;       /* real ms of the last draw */
};

static struct face g_face[FACE_MAX];
static struct { int spno, iic; } g_hook[FACE_CARDS];
static int g_hooks;
static struct { int iic, hp; } g_hp_seen[HP_CARDS];
static int g_hp_cb[HP_CB_MAX], g_nhp;

static struct face *face_of(int iic, int create)
{
	int i, free_i = -1;
	for (i = 0; i < FACE_MAX; i++) {
		if (g_face[i].iic == iic && iic > 0)
			return &g_face[i];
		if (!g_face[i].iic && free_i < 0)
			free_i = i;
	}
	if (!create || free_i < 0)
		return NULL;
	memset(&g_face[free_i], 0, sizeof g_face[free_i]);
	g_face[free_i].iic = iic;
	return &g_face[free_i];
}

static void face_free(struct msprite *s)
{
	if (!s)
		return;
	drop_content(s);
	unref_fn(&s->mv.end);
	free(s);
}

static void face_tidy(struct face *f)
{
	if (!f->base && !f->temp_on)
		memset(f, 0, sizeof *f);
}

static double face_now(int real)
{
	return real ? (double)SDL_GetTicks64() : (double)vm_game_ticks();
}

/* The picture for face (a CG number or a GIF), with fps and clock from opts. */
static struct msprite *face_make(lua_State *L, int face, int opts)
{
	struct msprite *s = xcalloc(1, sizeof *s);
	s->ref = LUA_NOREF;
	s->gif_end = LUA_NOREF;
	s->mv.end = LUA_NOREF;
	s->scale_x = s->scale_y = 1;
	s->alpha = 255;
	s->mr = s->mg = s->mb = 255;
	s->show = 1;
	s->sp.render = spr_render;
	lua_newtable(L);
	if (lua_isinteger(L, face)) {
		lua_pushvalue(L, face);
		lua_setfield(L, -2, "cg");
	} else if (lua_type(L, face) == LUA_TSTRING) {
		lua_pushvalue(L, face);
		lua_setfield(L, -2, "gif");
	} else {
		free(s);
		luaL_error(L, "rq.battle.set_face: face is a CG number, a GIF, or nil for the game's");
	}
	if (opts) {
		lua_getfield(L, opts, "fps");
		lua_setfield(L, -2, "fps");
		lua_getfield(L, opts, "clock");
		lua_setfield(L, -2, "clock");
	}
	if (protected_apply(L, s, lua_gettop(L)) != LUA_OK) {
		face_free(s);
		lua_error(L);
	}
	lua_pop(L, 1);
	return s;
}

/* set_face(iic, face [, {ms, fps, clock}]) */
int rq_lua_battle_set_face(lua_State *L)
{
	int iic = (int)luaL_checkinteger(L, 1), opts = 0, real = 0;
	float ms = 0;
	struct msprite *s = NULL;
	struct face *f;

	if (iic <= 0 || !rq_chara_exists(iic))
		return luaL_error(L, "rq.battle.set_face: no character %d", iic);
	if (!lua_isnoneornil(L, 3)) {
		const char *c;
		luaL_checktype(L, 3, LUA_TTABLE);
		opts = 3;
		if (opt_num(L, 3, "ms", &ms) && ms <= 0)
			return luaL_error(L, "rq.battle.set_face: ms must be more than 0");
		lua_getfield(L, 3, "clock");
		c = lua_tostring(L, -1);
		real = c && !strcmp(c, "real");
		lua_pop(L, 1);
	}
	if (!lua_isnoneornil(L, 2))
		s = face_make(L, 2, opts);
	f = face_of(iic, s != NULL || ms > 0);
	if (!f) {
		if (s) {
			face_free(s);
			return luaL_error(L, "rq.battle.set_face: %d characters with a face is the most", FACE_MAX);
		}
		lua_pushboolean(L, 1);
		return 1;
	}
	if (ms > 0) {
		face_free(f->temp);
		f->temp = s;
		f->temp_on = 1;
		f->temp_real = real;
		f->temp_end = face_now(real) + ms;
	} else {
		face_free(f->base);
		f->base = s;
	}
	face_tidy(f);
	scene_dirty();
	lua_pushboolean(L, 1);
	return 1;
}

static void push_face(lua_State *L, const struct msprite *s, const char *key)
{
	if (!s)
		return;
	if (s->kind == SPR_GIF)
		lua_pushstring(L, s->gif_name);
	else
		lua_pushinteger(L, s->cg);
	lua_setfield(L, -2, key);
}

/* get_face(iic): nil, or {face, temp, temp_ms, card, drawn}. */
int rq_lua_battle_get_face(lua_State *L)
{
	int iic = (int)luaL_checkinteger(L, 1), i;
	struct face *f = face_of(iic, 0);

	if (!f)
		return 0;
	lua_newtable(L);
	push_face(L, f->base, "face");
	if (f->temp_on) {
		double left = f->temp_end - face_now(f->temp_real);
		push_face(L, f->temp, "temp");
		lua_pushinteger(L, left > 0 ? (lua_Integer)left : 0);
		lua_setfield(L, -2, "temp_ms");
	}
	for (i = 0; i < FACE_CARDS; i++) {
		if (g_hook[i].iic == iic && g_hook[i].spno) {
			lua_pushinteger(L, i + 1);
			lua_setfield(L, -2, "card");
		}
	}
	lua_pushboolean(L, f->drawn && SDL_GetTicks64() - f->drawn < 250);
	lua_setfield(L, -2, "drawn");
	return 1;
}

int rq_lua_battle_on_hp(lua_State *L)
{
	luaL_checktype(L, 1, LUA_TFUNCTION);
	if (g_nhp >= HP_CB_MAX)
		return luaL_error(L, "rq.battle.on_hp: too many callbacks");
	lua_pushvalue(L, 1);
	g_hp_cb[g_nhp++] = luaL_ref(L, LUA_REGISTRYINDEX);
	return 0;
}

/* Called by sprite_render for every Chipmunk sprite. */
int rq_face_render(struct sact_sprite *ss)
{
	struct face *f;
	struct msprite *s;
	int i;

	if (!g_hooks)
		return 0;
	for (i = 0; i < FACE_CARDS; i++)
		if (g_hook[i].spno && g_hook[i].spno == ss->no)
			break;
	if (i == FACE_CARDS || !(f = face_of(g_hook[i].iic, 0)))
		return 0;
	s = f->temp_on ? f->temp : f->base;
	if (!s || ss->rect.w <= 0 || ss->rect.h <= 0)
		return 0;
	s->x = (float)ss->rect.x;
	s->y = (float)ss->rect.y;
	s->w = (float)ss->rect.w;
	s->h = (float)ss->rect.h;
	s->alpha = ss->blend_rate;
	s->mr = ss->multiply_color.r;
	s->mg = ss->multiply_color.g;
	s->mb = ss->multiply_color.b;
	s->ar = ss->add_color.r;
	s->ag = ss->add_color.g;
	s->ab = ss->add_color.b;
	spr_render(&s->sp);
	f->drawn = SDL_GetTicks64();
	return 1;
}

struct hp_event {
	int iic, unit, hp, max, before, first;
};

/* Each frame: which face sprites to draw over, faces for a while that ended, GIF frames,
 * and HP changes. */
static int faces_tick(void)
{
	static struct hp_event ev[HP_CARDS];
	int i, n = 0, dirty = 0, hooks = 0;

	if (!rq_battle_live()) {
		for (i = 0; i < FACE_MAX; i++) {
			if (g_face[i].iic && g_face[i].temp_on) {
				face_free(g_face[i].temp);
				g_face[i].temp = NULL;
				g_face[i].temp_on = 0;
				face_tidy(&g_face[i]);
			}
		}
		if (g_hooks)
			dirty = 1;
		memset(g_hook, 0, sizeof g_hook);
		memset(g_hp_seen, 0, sizeof g_hp_seen);
		g_hooks = 0;
		return dirty;
	}
	for (i = 0; i < FACE_MAX; i++) {
		struct face *f = &g_face[i];
		if (!f->iic)
			continue;
		if (f->temp_on && face_now(f->temp_real) >= f->temp_end) {
			face_free(f->temp);
			f->temp = NULL;
			f->temp_on = 0;
			dirty = 1;
			face_tidy(f);
			if (!f->iic)
				continue;
		}
		if (f->base && gif_step(f->base, NULL, NULL, NULL))
			dirty = 1;
		if (f->temp && gif_step(f->temp, NULL, NULL, NULL))
			dirty = 1;
	}
	for (i = 0; i < FACE_CARDS; i++) {
		int iic = rq_battle_card_iic(i + 1);
		int spno = iic > 0 && face_of(iic, 0) ? rq_battle_face_sprite(i + 1) : 0;
		if (g_hook[i].spno != spno || g_hook[i].iic != iic)
			dirty = 1;
		g_hook[i].spno = spno;
		g_hook[i].iic = iic;
		hooks |= spno != 0;
	}
	g_hooks = hooks;

	for (i = 0; i < HP_CARDS; i++) {
		int no = i < 5 ? i + 1 : i + 6, iic = rq_battle_card_iic(no), hp, max;
		if (iic <= 0 || !rq_chara_hp(iic, &hp, &max)) {
			g_hp_seen[i].iic = 0;
			continue;
		}
		if (g_hp_seen[i].iic != iic) {
			ev[n++] = (struct hp_event){ iic, no, hp, max, hp, 1 };
		} else if (g_hp_seen[i].hp != hp) {
			ev[n++] = (struct hp_event){ iic, no, hp, max, g_hp_seen[i].hp, 0 };
		}
		g_hp_seen[i].iic = iic;
		g_hp_seen[i].hp = hp;
	}
	for (i = 0; i < n && g_L; i++) {
		int k;
		for (k = 0; k < g_nhp; k++) {
			lua_State *L = g_L;
			lua_rawgeti(L, LUA_REGISTRYINDEX, g_hp_cb[k]);
			lua_createtable(L, 0, 7);
			lua_pushinteger(L, ev[i].iic);
			lua_setfield(L, -2, "iic");
			lua_pushinteger(L, ev[i].unit);
			lua_setfield(L, -2, "unit");
			lua_pushstring(L, ev[i].unit <= 5 ? "ally" : "enemy");
			lua_setfield(L, -2, "side");
			lua_pushinteger(L, ev[i].hp);
			lua_setfield(L, -2, "hp");
			lua_pushinteger(L, ev[i].max);
			lua_setfield(L, -2, "max");
			lua_pushinteger(L, ev[i].before);
			lua_setfield(L, -2, "before");
			lua_pushboolean(L, ev[i].first);
			lua_setfield(L, -2, "first");
			if (lua_pcall(L, 1, 0, 0) != LUA_OK) {
				rq_log("rq.battle.on_hp: %s", lua_tostring(L, -1));
				lua_pop(L, 1);
			}
		}
	}
	return dirty || hooks;
}

/* ---- moving pictures on game CGs (rq.image, 2.25.0) ---- */

/* A GIF or animated WebP that rq.image put on a CG number. modloader_image.c fits each
 * frame to the CG's size, so wherever the game shows the CG whole (a sprite set from it,
 * a part, a card's stretched face) the frame is drawn in its place, with the sprite's
 * blend rate and colours. One clock per CG, so every place shows the same frame. The
 * frames are not in g_gifs: their size is the CG's, not the file's. */
#define MOV_MAX 64
#define MOV_FRAMES_MAX 1024
#define MOV_PIXELS (64u * 1024u * 1024u)
#define MOV_SEEN_MS 250

struct moving {
	int cg;
	int ready;
	struct spr_gif g;
	int cap;
	struct msprite *s;  /* draws the frames; its gif is g */
	uint64_t pixels;
	uint64_t drawn;     /* real ms of the last draw */
};

static struct moving g_mov[MOV_MAX];
static int g_nmov;
static int g_mov_live;
static uint64_t g_mov_pixels;

static struct moving *mov_of(int cg)
{
	int i;
	if (cg <= 0)
		return NULL;
	for (i = 0; i < g_nmov; i++)
		if (g_mov[i].cg == cg)
			return &g_mov[i];
	return NULL;
}

static void mov_free(struct moving *m)
{
	int i;
	if (!m->cg)
		return;
	if (m->ready)
		g_mov_live--;
	for (i = 0; i < m->g.n; i++)
		gfx_delete_texture(&m->g.t[i]);
	free(m->g.t);
	free(m->g.at);
	g_mov_pixels -= m->pixels;
	free(m->s);
	memset(m, 0, sizeof *m);
	while (g_nmov > 0 && !g_mov[g_nmov - 1].cg)
		g_nmov--;
	scene_dirty();
}

void rq_moving_drop(int cg)
{
	struct moving *m = mov_of(cg);
	if (m)
		mov_free(m);
}

/* Starts the frames of cg, w by h (the CG's size). Frames come with rq_moving_frame and
 * rq_moving_end shows them. */
int rq_moving_begin(int cg, int w, int h, double fps, int real, const char **err)
{
	struct moving *m;
	struct msprite *s;
	int i;

	rq_moving_drop(cg);
	if (w <= 0 || h <= 0) {
		*err = "a moving picture needs a size";
		return 0;
	}
	for (i = 0; i < MOV_MAX && g_mov[i].cg; i++)
		;
	if (i == MOV_MAX) {
		*err = "too many moving pictures (max 64)";
		return 0;
	}
	m = &g_mov[i];
	memset(m, 0, sizeof *m);
	m->cg = cg;
	m->g.w = w;
	m->g.h = h;
	s = m->s = xcalloc(1, sizeof *s);
	s->ref = LUA_NOREF;
	s->gif_end = LUA_NOREF;
	s->mv.end = LUA_NOREF;
	s->scale_x = s->scale_y = 1;
	s->alpha = 255;
	s->mr = s->mg = s->mb = 255;
	s->show = 1;
	s->sp.render = spr_render;
	s->kind = SPR_GIF;
	s->gif = &m->g;
	s->nat_w = w;
	s->nat_h = h;
	s->fps = fps > 0 ? fps : 0;
	s->real = real;
	s->loop = 1;
	if (i >= g_nmov)
		g_nmov = i + 1;
	return 1;
}

/* One frame, RGBA at the CG's size, shown for delay_ms. NULL, or what is wrong. */
const char *rq_moving_frame(int cg, const uint8_t *rgba, int delay_ms)
{
	struct moving *m = mov_of(cg);
	uint64_t px;

	if (!m || m->ready)
		return "no moving picture was begun";
	px = (uint64_t)m->g.w * m->g.h;
	if (m->g.n >= MOV_FRAMES_MAX)
		return "more than 1024 frames";
	if (g_mov_pixels + px > MOV_PIXELS)
		return "the moving pictures would pass 64 million pixels in all";
	if (m->g.n == m->cap) {
		m->cap = m->cap ? m->cap * 2 : 16;
		m->g.t = xrealloc(m->g.t, m->cap * sizeof *m->g.t);
		m->g.at = xrealloc(m->g.at, m->cap * sizeof *m->g.at);
	}
	memset(&m->g.t[m->g.n], 0, sizeof m->g.t[m->g.n]);
	gfx_init_texture_with_pixels(&m->g.t[m->g.n], m->g.w, m->g.h, (void *)rgba);
	m->g.at[m->g.n] = m->g.total;
	m->g.total += delay_ms > 0 ? delay_ms : 100;
	m->g.n++;
	m->pixels += px;
	g_mov_pixels += px;
	return NULL;
}

void rq_moving_end(int cg, int ok)
{
	struct moving *m = mov_of(cg);

	if (!m || m->ready)
		return;
	if (!ok || m->g.n < 1) {
		mov_free(m);
		return;
	}
	m->ready = 1;
	g_mov_live++;
	m->s->from = 0;
	m->s->to = m->g.n - 1;
	m->s->cur = 0;
	m->s->playing = m->g.n > 1;
	m->s->t0 = now_of(m->s);
	scene_dirty();
}

int rq_moving_has(int cg)
{
	struct moving *m = mov_of(cg);
	return m && m->ready;
}

int rq_moving_info(int cg, int *frames, int *ms, int *drawn)
{
	struct moving *m = mov_of(cg);

	if (!m || !m->ready)
		return 0;
	*frames = m->g.n;
	*ms = m->s->fps > 0 ? (int)(m->g.n * 1000.0 / m->s->fps) : m->g.total;
	*drawn = m->drawn && SDL_GetTicks64() - m->drawn < MOV_SEEN_MS;
	return 1;
}

/* Called by sprite_render for a Chipmunk sprite that shows a CG whole. */
int rq_moving_render(struct sact_sprite *ss)
{
	struct moving *m;
	struct msprite *s;

	if (!g_mov_live || !(m = mov_of(ss->rq_whole_cg)) || !m->ready)
		return 0;
	if (ss->rect.w <= 0 || ss->rect.h <= 0 || ss->draw_method != DRAW_METHOD_NORMAL
	    || ss->surface_area.x || ss->surface_area.y || ss->surface_area.w || ss->surface_area.h)
		return 0;
	s = m->s;
	s->x = (float)ss->rect.x;
	s->y = (float)ss->rect.y;
	s->w = (float)ss->rect.w;
	s->h = (float)ss->rect.h;
	s->alpha = ss->blend_rate;
	s->mr = ss->multiply_color.r;
	s->mg = ss->multiply_color.g;
	s->mb = ss->multiply_color.b;
	s->ar = ss->add_color.r;
	s->ag = ss->add_color.g;
	s->ab = ss->add_color.b;
	spr_render(&s->sp);
	m->drawn = SDL_GetTicks64();
	return 1;
}

/* For parts_render: the frame to draw in place of a part's CG texture of w by h. */
Texture *rq_moving_part_frame(int cg, int w, int h)
{
	struct moving *m;

	if (!g_mov_live || !(m = mov_of(cg)) || !m->ready || m->g.w != w || m->g.h != h)
		return NULL;
	m->drawn = SDL_GetTicks64();
	return &m->g.t[m->s->cur];
}

/* T_Sprite::stretchCg made sprite spno from the whole of cg. */
void rq_moving_tag(int spno, int cg)
{
	struct sact_sprite *ss = spno > 0 ? sact_try_get_sprite(spno) : NULL;
	if (ss)
		ss->rq_whole_cg = cg;
}

/* Each frame: the frame of each moving picture; a redraw only while one is on screen. */
static int moving_tick(void)
{
	uint64_t now;
	int i, dirty = 0;

	if (!g_mov_live)
		return 0;
	now = SDL_GetTicks64();
	for (i = 0; i < g_nmov; i++) {
		struct moving *m = &g_mov[i];
		if (!m->ready || m->g.n < 2)
			continue;
		if (gif_step(m->s, NULL, NULL, NULL) && m->drawn && now - m->drawn < MOV_SEEN_MS)
			dirty = 1;
	}
	return dirty;
}

/* ---- the scene list ---- */

struct scene_row {
	const char *kind;
	int no, z, z2, x, y, w, h, cg;
	const char *mod;
	const char *part;
};

struct scene_walk {
	struct scene_row *rows;
	int n, max;
};

static const char *part_type_name(int type)
{
	static const char *const names[] = { "none", "cg", "text", "animation", "numeral",
		"hgauge", "vgauge", "construction", "flash", "flat", "movie", "rect", "layout", "3d" };
	return type >= 0 && type < (int)(sizeof names / sizeof names[0]) ? names[type] : "other";
}

static void walk_one(struct sprite *sp, void *data)
{
	struct scene_walk *w = data;
	struct scene_row *r;
	struct sact_sprite *ss;
	int type;

	if (w->n >= w->max)
		return;
	r = &w->rows[w->n++];
	memset(r, 0, sizeof *r);
	r->z = sp->z;
	r->z2 = sp->z2;
	if (sp->render == spr_render) {
		struct msprite *s = (struct msprite *)sp;
		float bw, bh;
		base_size(s, &bw, &bh);
		r->kind = "mod";
		r->no = s->id;
		r->mod = s->mod;
		r->x = (int)(s->bx + s->x); r->y = (int)(s->by + s->y);
		r->w = (int)(bw * s->scale_x); r->h = (int)(bh * s->scale_y);
		r->cg = s->kind == SPR_CG ? s->cg : 0;
	} else if ((ss = sprite_as_sact(sp))) {
		r->kind = "sprite";
		r->no = ss->no;
		r->x = ss->rect.x; r->y = ss->rect.y;
		r->w = ss->rect.w; r->h = ss->rect.h;
		r->cg = ss->cg_no;
	} else if (rq_parts_describe(sp, &r->no, &type, &r->x, &r->y, &r->w, &r->h, &r->cg)) {
		r->kind = "part";
		r->part = part_type_name(type);
	} else {
		r->kind = "other";
	}
}

#define SCENE_ROWS 4096

static struct scene_row *walk(int *n)
{
	struct scene_walk w = { xcalloc(SCENE_ROWS, sizeof(struct scene_row)), 0, SCENE_ROWS };
	scene_for_each_sprite(walk_one, &w);
	*n = w.n;
	return w.rows;
}

static int l_scene(lua_State *L)
{
	int i, n;
	struct scene_row *rows = walk(&n);

	lua_createtable(L, n, 0);
	for (i = 0; i < n; i++) {
		struct scene_row *r = &rows[i];
		lua_createtable(L, 0, 12);
		lua_pushstring(L, r->kind);
		lua_setfield(L, -2, "kind");
		set_num(L, "no", r->no);
		set_num(L, "z", r->z);
		set_num(L, "z2", r->z2);
		set_num(L, "x", r->x); set_num(L, "y", r->y);
		set_num(L, "w", r->w); set_num(L, "h", r->h);
		set_num(L, "cg", r->cg);
		if (r->mod) {
			lua_pushstring(L, r->mod);
			lua_setfield(L, -2, "mod");
		}
		if (r->part) {
			lua_pushstring(L, r->part);
			lua_setfield(L, -2, "part");
		}
		lua_rawseti(L, -2, i + 1);
	}
	free(rows);
	return 1;
}

/* The pipe's "scene": the same list as JSON, back to front, with the screen. */
char *rq_sprite_scene_json(void)
{
	int i, n;
	struct scene_row *rows = walk(&n);
	cJSON *o = cJSON_CreateObject(), *a = cJSON_AddArrayToObject(o, "scene");
	char screen[24], *out;

	for (i = 0; i < n; i++) {
		struct scene_row *r = &rows[i];
		cJSON *e = cJSON_CreateObject();
		cJSON_AddStringToObject(e, "kind", r->kind);
		cJSON_AddNumberToObject(e, "no", r->no);
		cJSON_AddNumberToObject(e, "z", r->z);
		cJSON_AddNumberToObject(e, "z2", r->z2);
		cJSON_AddNumberToObject(e, "x", r->x);
		cJSON_AddNumberToObject(e, "y", r->y);
		cJSON_AddNumberToObject(e, "w", r->w);
		cJSON_AddNumberToObject(e, "h", r->h);
		cJSON_AddNumberToObject(e, "cg", r->cg);
		if (r->mod)
			cJSON_AddStringToObject(e, "mod", r->mod);
		if (r->part)
			cJSON_AddStringToObject(e, "part", r->part);
		cJSON_AddItemToArray(a, e);
	}
	cJSON_AddNumberToObject(o, "count", n);
	current_screen(screen, sizeof screen);
	cJSON_AddStringToObject(o, "screen", screen);
	out = cJSON_PrintUnformatted(o);
	cJSON_Delete(o);
	free(rows);
	return out;
}

/* ---- each update ---- */

/* Before the scene is drawn: CGs that changed, and sprites kept to a screen. */
int rq_sprite_tick(void)
{
	static int fn[SPR_MAX * 2], id[SPR_MAX * 2];
	int i, np = 0, dirty = reload_stale();

	if (g_keepers > 0) {
		char screen[24];
		current_screen(screen, sizeof screen);
		for (i = 0; i < SPR_MAX; i++) {
			struct msprite *s = g_spr[i];
			if (s && s->keep == KEEP_SCREEN && strcmp(s->screen, screen)) {
				destroy(s, g_L);
				dirty = 1;
			}
		}
	}
	if (advance(fn, id, &np))
		dirty = 1;
	if (attached())
		dirty = 1;
	if (faces_tick())
		dirty = 1;
	if (moving_tick())
		dirty = 1;
	fire_ends(fn, id, np);
	return dirty;
}

void rq_sprite_open(lua_State *L, int rq_index)
{
	static const luaL_Reg methods[] = {
		{ "set", l_set },
		{ "get", l_get },
		{ "remove", l_remove },
		{ "alive", l_alive },
		{ "play", l_play },
		{ "stop", l_stop },
		{ "frame", l_frame },
		{ "playing", l_playing },
		{ "to", l_to },
		{ "moving", l_moving },
		{ "halt", l_halt },
		{ NULL, NULL }
	};

	g_L = L;
	if (luaL_newmetatable(L, SPR_META)) {
		lua_newtable(L);
		luaL_setfuncs(L, methods, 0);
		lua_setfield(L, -2, "__index");
		lua_pushcfunction(L, l_tostring);
		lua_setfield(L, -2, "__tostring");
		lua_pushcfunction(L, l_eq);
		lua_setfield(L, -2, "__eq");
	}
	lua_pop(L, 1);

	lua_newtable(L);
	lua_pushcfunction(L, l_new);
	lua_setfield(L, -2, "new");
	lua_pushcfunction(L, l_list);
	lua_setfield(L, -2, "list");
	lua_pushcfunction(L, l_clear);
	lua_setfield(L, -2, "clear");
	lua_pushcfunction(L, l_scene);
	lua_setfield(L, -2, "scene");
	lua_pushcfunction(L, l_on_reset);
	lua_setfield(L, -2, "on_reset");
	lua_setfield(L, rq_index, "sprite");
	/* Loader 1.13.0: the same callbacks as rq.on_reset, for mods that keep no sprites. */
	lua_pushcfunction(L, l_on_reset);
	lua_setfield(L, rq_index, "on_reset");
}

/* After rq.ain.hook exists: a load and the title remove every mod sprite. */
void rq_sprite_hooks(lua_State *L, int rq_index)
{
	if (luaL_loadbuffer(L, rq_sprite_lua, rq_sprite_lua_len, "=sprite") != LUA_OK) {
		rq_log("sprite: %s", lua_tostring(L, -1));
		lua_pop(L, 1);
		return;
	}
	lua_pushvalue(L, rq_index);
	lua_pushcfunction(L, l_reset);
	if (lua_pcall(L, 2, 0, 0) != LUA_OK) {
		rq_log("sprite: %s", lua_tostring(L, -1));
		lua_pop(L, 1);
	}
}

#endif /* RQ_MODLOADER */
