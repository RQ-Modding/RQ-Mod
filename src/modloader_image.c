/* Copyright (C) 2026 Rance Quest modding fork
 *
 * rq.image: images from a mod folder served as game CGs.
 *   add(key, path)        a new CG number (stable, from the key) for a file
 *   replace(cg_no, path)  every use of an existing CG number shows the file
 *   restore(cg_no)        undo replace
 *   info(cg_no)           exists / custom / size
 * asset_manager.c asks rq_image_has / rq_image_load / rq_image_metrics before
 * it touches the game archive, so sprites, parts and HLL calls all see it.
 * Formats are whatever libsys4 decodes (PNG, JPEG, WebP, QNT, AJP, PMS).
 */

#ifdef RQ_MODLOADER

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

#include "lua.h"
#include "lauxlib.h"

#include "system4.h"
#include "system4/archive.h"
#include "system4/cg.h"

#include "asset_manager.h"
#include "modloader.h"
#include "modloader_priv.h"

#define IMG_MAX 256
#define IMG_BASE 900000
#define IMG_SPAN 90000
#define PLACEHOLDER_ID (IMG_BASE + IMG_SPAN)
#define PLACEHOLDER_SIZE_CG 6000 /* a game item icon */

enum fit { FIT_STRETCH, FIT_CONTAIN, FIT_COVER, FIT_NONE };

struct img {
	int id;
	int replace;      /* 1 = overrides a game CG, 0 = new number */
	int fit;
	int w, h;         /* target size, 0 = native (replace: the game CG's size) */
	int keep_size;    /* replace only: do not scale to the game CG's size */
	int failed;
	char key[128];
	char path[RQ_PATH_MAX];
	struct cg *cg;    /* decoded and fitted, kept */
};

static struct img g_img[IMG_MAX];
static int g_nimg;

static struct img *find_img(int id)
{
	int i;
	for (i = 0; i < g_nimg; i++) {
		if (g_img[i].id == id)
			return &g_img[i];
	}
	return NULL;
}

static unsigned char *px(const struct cg *c, int x, int y)
{
	if (x < 0)
		x = 0;
	if (y < 0)
		y = 0;
	if (x >= c->metrics.w)
		x = c->metrics.w - 1;
	if (y >= c->metrics.h)
		y = c->metrics.h - 1;
	return (unsigned char *)c->pixels + ((size_t)y * c->metrics.w + x) * 4;
}

/* Box average over the source footprint when shrinking, bilinear when enlarging. */
static void sample(const struct cg *s, double u0, double v0, double u1, double v1, unsigned char *out)
{
	if (u1 - u0 <= 1.0 && v1 - v0 <= 1.0) {
		double cu = (u0 + u1) * 0.5 - 0.5, cv = (v0 + v1) * 0.5 - 0.5;
		int x0 = (int)floor(cu), y0 = (int)floor(cv);
		double fx = cu - x0, fy = cv - y0;
		const unsigned char *a = px(s, x0, y0), *b = px(s, x0 + 1, y0);
		const unsigned char *c = px(s, x0, y0 + 1), *d = px(s, x0 + 1, y0 + 1);
		int k;
		for (k = 0; k < 4; k++) {
			double top = a[k] * (1 - fx) + b[k] * fx;
			double bot = c[k] * (1 - fx) + d[k] * fx;
			out[k] = (unsigned char)(top * (1 - fy) + bot * fy + 0.5);
		}
	} else {
		int x0 = (int)floor(u0), x1 = (int)ceil(u1), y0 = (int)floor(v0), y1 = (int)ceil(v1);
		double sum[4] = { 0, 0, 0, 0 };
		int x, y, k, n = 0;
		if (x1 <= x0)
			x1 = x0 + 1;
		if (y1 <= y0)
			y1 = y0 + 1;
		for (y = y0; y < y1; y++) {
			for (x = x0; x < x1; x++) {
				const unsigned char *p = px(s, x, y);
				for (k = 0; k < 4; k++)
					sum[k] += p[k];
				n++;
			}
		}
		for (k = 0; k < 4; k++)
			out[k] = (unsigned char)(sum[k] / n + 0.5);
	}
}

/* Returns a new cg of size tw x th. Frees src. */
static struct cg *fit_cg(struct cg *src, int tw, int th, int mode)
{
	int sw = src->metrics.w, sh = src->metrics.h;
	int ox = 0, oy = 0, rw = tw, rh = th, x, y;
	double sx0 = 0, sy0 = 0, sww = sw, shh = sh;
	struct cg *d;

	if (mode == FIT_NONE || tw <= 0 || th <= 0 || (tw == sw && th == sh) || sw <= 0 || sh <= 0)
		return src;
	if (mode == FIT_CONTAIN) {
		double s = fmin((double)tw / sw, (double)th / sh);
		rw = (int)floor(sw * s + 0.5);
		rh = (int)floor(sh * s + 0.5);
		if (rw < 1)
			rw = 1;
		if (rh < 1)
			rh = 1;
		ox = (tw - rw) / 2;
		oy = (th - rh) / 2;
	} else if (mode == FIT_COVER) {
		double s = fmax((double)tw / sw, (double)th / sh);
		sww = tw / s;
		shh = th / s;
		sx0 = (sw - sww) / 2;
		sy0 = (sh - shh) / 2;
	}
	d = xcalloc(1, sizeof(struct cg));
	*d = *src;
	d->metrics.w = tw;
	d->metrics.h = th;
	d->metrics.x = 0;
	d->metrics.y = 0;
	d->metrics.bpp = 32;
	d->metrics.has_pixel = true;
	d->metrics.pixel_pitch = tw * 4;
	if (rw != tw || rh != th)
		d->metrics.has_alpha = true;
	d->pixels = xcalloc((size_t)tw * th, 4);
	for (y = 0; y < rh; y++) {
		for (x = 0; x < rw; x++) {
			double u0 = sx0 + x * sww / rw, u1 = sx0 + (x + 1) * sww / rw;
			double v0 = sy0 + y * shh / rh, v1 = sy0 + (y + 1) * shh / rh;
			sample(src, u0, v0, u1, v1, (unsigned char *)d->pixels + ((size_t)(y + oy) * tw + (x + ox)) * 4);
		}
	}
	cg_free(src);
	return d;
}

/* A grey tile with a question mark, at the size of a game item icon. */
static struct cg *make_placeholder(void)
{
	static const char *const glyph[7] = { ".###.", "#...#", "....#", "..##.", "..#..", ".....", "..#.." };
	struct cg_metrics om;
	struct cg *c = xcalloc(1, sizeof(struct cg));
	int w = 64, h = 64, x, y, cell, gx, gy;

	if (asset_cg_get_metrics_original(PLACEHOLDER_SIZE_CG, &om) && om.w > 0 && om.h > 0) {
		w = om.w;
		h = om.h;
	}
	c->type = ALCG_UNKNOWN;
	c->metrics.w = w;
	c->metrics.h = h;
	c->metrics.bpp = 32;
	c->metrics.has_pixel = true;
	c->metrics.has_alpha = true;
	c->metrics.pixel_pitch = w * 4;
	c->metrics.alpha_pitch = 1;
	c->pixels = xcalloc((size_t)w * h, 4);
	cell = (h < w ? h : w) / 10;
	if (cell < 1)
		cell = 1;
	gx = (w - 5 * cell) / 2;
	gy = (h - 7 * cell) / 2;
	for (y = 0; y < h; y++) {
		for (x = 0; x < w; x++) {
			unsigned char *o = (unsigned char *)c->pixels + ((size_t)y * w + x) * 4;
			int edge = x < 2 || y < 2 || x >= w - 2 || y >= h - 2;
			int mark = 0;
			if (x >= gx && y >= gy && x < gx + 5 * cell && y < gy + 7 * cell)
				mark = glyph[(y - gy) / cell][(x - gx) / cell] == '#';
			o[0] = o[1] = o[2] = mark ? 230 : edge ? 150 : 90;
			o[3] = 255;
		}
	}
	return c;
}

static struct cg *decode(struct img *m)
{
	struct cg *cg;
	int tw = m->w, th = m->h;

	if (m->cg)
		return m->cg;
	if (m->failed)
		return NULL;
	if (m->id == PLACEHOLDER_ID)
		return m->cg = make_placeholder();
	cg = cg_load_file(m->path);
	if (!cg || !cg->pixels) {
		rq_log("image: cannot decode %s", m->path);
		m->failed = 1;
		return NULL;
	}
	if (m->replace && !m->keep_size && tw <= 0) {
		struct cg_metrics om;
		if (asset_cg_get_metrics_original(m->id, &om)) {
			tw = om.w;
			th = om.h;
		}
	}
	m->cg = fit_cg(cg, tw, th, m->fit);
	return m->cg;
}

bool rq_image_has(int id)
{
	struct img *m = find_img(id);
	return m && !m->failed;
}

struct cg *rq_image_load(int id)
{
	struct img *m = find_img(id);
	struct cg *src, *cg;
	size_t bytes;

	if (!m || !(src = decode(m)))
		return NULL;
	bytes = (size_t)src->metrics.w * src->metrics.h * 4;
	cg = xcalloc(1, sizeof(struct cg));
	*cg = *src;
	cg->pixels = xmalloc(bytes);
	memcpy(cg->pixels, src->pixels, bytes);
	return cg;
}

bool rq_image_metrics(int id, struct cg_metrics *out)
{
	struct img *m = find_img(id);
	struct cg *src;

	if (!m || !(src = decode(m)))
		return false;
	*out = src->metrics;
	return true;
}

/* Make game CG game_id show the mod image custom_id (same file, fitted to the game CG's size).
 * The Party tab and other screens keep the CG number they read when they were built, so
 * a character that already exists is changed by replacing the CG it points to. */
void rq_image_redirect(int game_id, int custom_id)
{
	struct img *src = find_img(custom_id), *m;

	if (!src || game_id <= 0 || game_id >= IMG_BASE)
		return;
	m = find_img(game_id);
	if (m && m->replace && !strcmp(m->path, src->path) && m->fit == src->fit)
		return;
	if (!m) {
		if (g_nimg >= IMG_MAX)
			return;
		m = &g_img[g_nimg++];
		memset(m, 0, sizeof *m);
		m->id = game_id;
	} else if (m->cg) {
		cg_free(m->cg);
		m->cg = NULL;
	}
	m->replace = 1;
	m->fit = src->fit;
	m->w = m->h = 0;
	m->keep_size = 0;
	m->failed = 0;
	snprintf(m->path, sizeof m->path, "%s", src->path);
	asset_cg_cache_forget(game_id);
}

/* The icon of an item whose mod gives none. */
int rq_image_placeholder(void)
{
	if (!find_img(PLACEHOLDER_ID)) {
		if (g_nimg >= IMG_MAX)
			return 0;
		memset(&g_img[g_nimg], 0, sizeof g_img[g_nimg]);
		g_img[g_nimg++].id = PLACEHOLDER_ID;
	}
	return PLACEHOLDER_ID;
}

/* ---- Lua ---- */

static int parse_fit(lua_State *L, int opts, int def)
{
	const char *s;
	int v = def;

	if (!lua_istable(L, opts))
		return def;
	lua_getfield(L, opts, "fit");
	if (lua_isstring(L, -1)) {
		s = lua_tostring(L, -1);
		if (!strcmp(s, "stretch"))
			v = FIT_STRETCH;
		else if (!strcmp(s, "contain"))
			v = FIT_CONTAIN;
		else if (!strcmp(s, "cover"))
			v = FIT_COVER;
		else if (!strcmp(s, "none"))
			v = FIT_NONE;
		else
			return luaL_error(L, "image fit must be stretch, contain, cover or none");
	}
	lua_pop(L, 1);
	return v;
}

static int opt_int(lua_State *L, int opts, const char *key)
{
	int v = 0;
	if (!lua_istable(L, opts))
		return 0;
	lua_getfield(L, opts, key);
	if (lua_isnumber(L, -1))
		v = (int)lua_tointeger(L, -1);
	lua_pop(L, 1);
	return v;
}

static struct img *slot_for(lua_State *L, int id)
{
	struct img *m = find_img(id);
	if (m)
		return m;
	if (g_nimg >= IMG_MAX) {
		luaL_error(L, "too many mod images (max %d)", IMG_MAX);
		return NULL;
	}
	m = &g_img[g_nimg++];
	memset(m, 0, sizeof *m);
	m->id = id;
	return m;
}

static void set_path(lua_State *L, struct img *m, const char *rel)
{
	char full[RQ_PATH_MAX];
	FILE *f;

	if (!rq_sandbox_resolve(L, rel, full, (int)sizeof full))
		luaL_error(L, "image path '%s' is not inside the mod folder", rel);
	f = fopen(full, "rb");
	if (!f)
		luaL_error(L, "image '%s' not found in the mod folder", rel);
	fclose(f);
	snprintf(m->path, sizeof m->path, "%s", full);
	if (m->cg) {
		cg_free(m->cg);
		m->cg = NULL;
	}
	m->failed = 0;
}

static int key_to_id(const char *key)
{
	uint32_t h = 2166136261u;
	for (; *key; key++) {
		h ^= (uint8_t)*key;
		h *= 16777619u;
	}
	return IMG_BASE + (int)(h % IMG_SPAN);
}

/* Shared by rq.image.add and the character fields. Returns the CG number. */
static int add_image(lua_State *L, const char *key, const char *rel, int opts, int def_fit, int def_w, int def_h)
{
	int id = key_to_id(key);
	struct img *m = find_img(id);

	if (m && (m->replace || strcmp(m->key, key)))
		return luaL_error(L, "image key '%s' collides with '%s'; pick another key", key, m->replace ? "a replaced CG" : m->key);
	m = slot_for(L, id);
	m->replace = 0;
	snprintf(m->key, sizeof m->key, "%s", key);
	m->fit = parse_fit(L, opts, def_fit);
	m->w = opt_int(L, opts, "w") ? opt_int(L, opts, "w") : def_w;
	m->h = opt_int(L, opts, "h") ? opt_int(L, opts, "h") : def_h;
	set_path(L, m, rel);
	asset_cg_cache_forget(id);
	return id;
}

int rq_lua_image_add(lua_State *L)
{
	const char *key = luaL_checkstring(L, 1);
	const char *path = luaL_checkstring(L, 2);
	int id;

	if (strlen(key) >= 100)
		return luaL_error(L, "image key is too long");
	id = add_image(L, key, path, 3, FIT_NONE, 0, 0);
	lua_pushinteger(L, id);
	return 1;
}

int rq_lua_image_replace(lua_State *L)
{
	int id = (int)luaL_checkinteger(L, 1);
	const char *path = luaL_checkstring(L, 2);
	struct img *m;

	if (id <= 0 || id >= IMG_BASE)
		return luaL_error(L, "replace needs a game CG number");
	m = slot_for(L, id);
	m->replace = 1;
	m->key[0] = 0;
	m->fit = parse_fit(L, 3, FIT_STRETCH);
	m->w = opt_int(L, 3, "w");
	m->h = opt_int(L, 3, "h");
	m->keep_size = 0;
	if (lua_istable(L, 3)) {
		lua_getfield(L, 3, "keep_size");
		m->keep_size = lua_toboolean(L, -1);
		lua_pop(L, 1);
	}
	set_path(L, m, path);
	asset_cg_cache_forget(id);
	return 0;
}

int rq_lua_image_restore(lua_State *L)
{
	int id = (int)luaL_checkinteger(L, 1);
	int i;

	for (i = 0; i < g_nimg; i++) {
		if (g_img[i].id == id && g_img[i].replace) {
			if (g_img[i].cg)
				cg_free(g_img[i].cg);
			g_img[i] = g_img[--g_nimg];
			asset_cg_cache_forget(id);
			lua_pushboolean(L, 1);
			return 1;
		}
	}
	lua_pushboolean(L, 0);
	return 1;
}

int rq_lua_image_info(lua_State *L)
{
	int id = (int)luaL_checkinteger(L, 1);
	struct cg_metrics m, om;
	struct img *im = find_img(id);

	lua_newtable(L);
	lua_pushboolean(L, asset_exists(ASSET_CG, id));
	lua_setfield(L, -2, "exists");
	lua_pushboolean(L, im != NULL);
	lua_setfield(L, -2, "mod");
	if (im) {
		lua_pushboolean(L, im->replace);
		lua_setfield(L, -2, "replaced");
	}
	if (asset_cg_get_metrics(id, &m)) {
		lua_pushinteger(L, m.w);
		lua_setfield(L, -2, "w");
		lua_pushinteger(L, m.h);
		lua_setfield(L, -2, "h");
	}
	if (im && im->replace && asset_cg_get_metrics_original(id, &om)) {
		lua_pushinteger(L, om.w);
		lua_setfield(L, -2, "game_w");
		lua_pushinteger(L, om.h);
		lua_setfield(L, -2, "game_h");
	}
	return 1;
}

/* Character fields: a number is a game CG, a string is a file in the mod folder.
 * Rewrites string values of face and portrait in the table at t into CG numbers. */
void rq_image_resolve_fields(lua_State *L, int t)
{
	/* Game sizes: face cards 196x232, standing pictures 1024x768, item icons 100x100. */
	static const char *const keys[] = { "face", "portrait", "icon" };
	static const int dw[] = { 196, 1024, 100 }, dh[] = { 232, 768, 100 };
	int i;

	t = lua_absindex(L, t);
	for (i = 0; i < 3; i++) {
		lua_getfield(L, t, keys[i]);
		if (lua_type(L, -1) == LUA_TSTRING) {
			char key[128];
			int id;
			snprintf(key, sizeof key, "%.60s|%.60s", rq_lua_caller_mod(L) ? rq_lua_caller_mod(L) : "_", lua_tostring(L, -1));
			id = add_image(L, key, lua_tostring(L, -1), 0, FIT_CONTAIN, dw[i], dh[i]);
			lua_pop(L, 1);
			lua_pushinteger(L, id);
			lua_setfield(L, t, keys[i]);
		} else {
			lua_pop(L, 1);
		}
	}
}

#endif /* RQ_MODLOADER */
