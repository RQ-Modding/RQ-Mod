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
#include "modloader.h"
#include "modloader_priv.h"

#define SPR_MAX 1024
#define SPR_PER_MOD 256
#define SPR_CG_MAX 256
#define SPR_RESET_CB 64
#define SPR_META "rq.sprite"
/* Named layers, from the z map (see plan-sprites.md):
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

enum { SPR_NONE, SPR_CG, SPR_FILL, SPR_TEXT };
enum { KEEP_NONE, KEEP_SCREEN };

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
};

static struct msprite *g_spr[SPR_MAX];
static int g_next_id = 1;
static int g_count;
static struct spr_cg g_cgs[SPR_CG_MAX];
static int g_reset_cb[SPR_RESET_CB];
static int g_nreset;
static int g_keepers;
static lua_State *g_L;

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

static Texture *spr_texture(struct msprite *s)
{
	if (s->kind == SPR_CG)
		return s->cgt ? &s->cgt->t : NULL;
	return s->own.handle ? &s->own : NULL;
}

static void drop_content(struct msprite *s)
{
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
		v[i].x = s->x + ox + px * c - py * sn;
		v[i].y = s->y + oy + px * sn + py * c;
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

/* In the scene when it is shown and has something to draw. */
static void place(struct msprite *s)
{
	int want = s->show && spr_texture(s) != NULL;
	if (s->sp.z != s->z)
		scene_set_sprite_z(&s->sp, s->z);
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
	place(s);
	return 1;
}

static int l_set(lua_State *L)
{
	struct msprite *s = check_sprite(L, 1);
	luaL_checktype(L, 2, LUA_TTABLE);
	apply_opts(L, s, 2);
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

static int l_on_reset(lua_State *L)
{
	luaL_checktype(L, 1, LUA_TFUNCTION);
	if (g_nreset >= SPR_RESET_CB)
		return luaL_error(L, "rq.sprite.on_reset: too many callbacks");
	lua_pushvalue(L, 1);
	g_reset_cb[g_nreset++] = luaL_ref(L, LUA_REGISTRYINDEX);
	return 0;
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
		lua_rawgeti(L, LUA_REGISTRYINDEX, g_reset_cb[i]);
		lua_pushstring(L, why);
		if (lua_pcall(L, 1, 0, 0) != LUA_OK) {
			rq_log("rq.sprite.on_reset: %s", lua_tostring(L, -1));
			lua_pop(L, 1);
		}
	}
	return 0;
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
		r->x = (int)s->x; r->y = (int)s->y;
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
	int i, dirty = reload_stale();

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
	return dirty;
}

void rq_sprite_open(lua_State *L, int rq_index)
{
	static const luaL_Reg methods[] = {
		{ "set", l_set },
		{ "get", l_get },
		{ "remove", l_remove },
		{ "alive", l_alive },
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
