/* Copyright (C) 2026 Rance Quest modding fork
 *
 * rq.image: images from a mod folder served as game CGs.
 *   add(key, path)        a new CG number (stable, from the key) for a file
 *   replace(cg_no, path)  every use of an existing CG number shows the file
 *   restore(cg_no)        undo replace
 *   info(cg_no)           exists / custom / size
 *   compose(key|cg_no, layers, opts)  a CG built from parts of other CGs, fills and text
 * asset_manager.c asks rq_image_has / rq_image_load / rq_image_metrics before
 * it touches the game archive, so sprites, parts and HLL calls all see it.
 * Formats are whatever libsys4 decodes (PNG, JPEG, WebP, QNT, AJP, PMS).
 * A GIF or an animated WebP (2.25.0) is a moving picture: its first frame is the CG,
 * and every frame, fitted the same way, goes to the moving store in modloader_sprite.c,
 * which draws it wherever the game shows the CG whole. T_Sprite::stretchCg is wrapped so
 * the stretched copy on a character card counts as the CG whole.
 *   flash_bitmap(file, id, path)  a picture on one bitmap of a game Flash file (2.25.0)
 *   flash_bitmaps(file)           that file's bitmaps
 */

#ifdef RQ_MODLOADER

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

#include "lua.h"
#include "lauxlib.h"

#include <webp/demux.h>

#include "system4.h"
#include "system4/ain.h"
#include "system4/archive.h"
#include "system4/buffer.h"
#include "system4/cg.h"
#include "system4/instructions.h"
#include "system4/utfsjis.h"

#include "asset_manager.h"
#include "modloader.h"
#include "modloader_priv.h"
#include "rq_pixels.h"
#include "swf.h"
#include "vm.h"
#include "vm/heap.h"
#include "vm/page.h"

#define IMG_MAX 256
#define IMG_BASE 900000
#define IMG_SPAN 90000
#define PLACEHOLDER_ID (IMG_BASE + IMG_SPAN)
/* The title screen's Discord icon (2.59.2): res/discord.png, built into the exe, at DISCORD_SIDE. */
#define DISCORD_ID (PLACEHOLDER_ID + 1)
#define DISCORD_SIDE 22
extern const char rq_discord_png[];
extern const unsigned rq_discord_png_len;
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
	struct layer *layers; /* compose: built on first use instead of read from path */
	int nlayers;
	int building;
	double fps;       /* a moving picture's speed, 0 = the file's own delays */
	int real;         /* clock = "real" */
	char err[160];    /* why a moving picture could not be read */
};

enum layer_kind { LAYER_CG, LAYER_FILL, LAYER_TEXT };

struct layer {
	int kind;
	int cg, sx, sy, sw, sh;   /* LAYER_CG: the part of the CG (sw 0 = all of it) */
	int x, y, w, h;           /* where it goes; w 0 = the part's own size */
	int copy;                 /* replace what is under it instead of blending */
	int alpha;
	int r, g, b, a;           /* LAYER_FILL color, LAYER_TEXT color */
	char *text;
	float size;
	int align, valign, edge, bold; /* LAYER_TEXT: align 0 start, 1 center, 2 end in the box w x h */
	/* LAYER_TEXT in the game's font (2.5.0): drawn by the game when compose is called. */
	unsigned char *gpix, *spix;
	int game, shadow, sr, sg, sb, sa;
	int gw, gh, sw2, sh2, dx, dy;
	struct rq_gametext gt;
};

static struct img g_img[IMG_MAX];
static int g_nimg;

static void free_layers(struct img *m)
{
	int i;
	for (i = 0; i < m->nlayers; i++) {
		free(m->layers[i].text);
		free(m->layers[i].gpix);
		free(m->layers[i].spix);
	}
	free(m->layers);
	m->layers = NULL;
	m->nlayers = 0;
}

static void cg_changed(int id)
{
	asset_cg_cache_forget(id);
	rq_anim_cg_changed(id);
	rq_sprite_cg_changed(id);
}

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

/* ---- compose ---- */

static struct cg *new_cg(int w, int h)
{
	struct cg *c = xcalloc(1, sizeof(struct cg));
	c->type = ALCG_UNKNOWN;
	c->metrics.w = w;
	c->metrics.h = h;
	c->metrics.bpp = 32;
	c->metrics.has_pixel = true;
	c->metrics.has_alpha = true;
	c->metrics.pixel_pitch = w * 4;
	c->metrics.alpha_pitch = 1;
	c->pixels = xcalloc((size_t)w * h, 4);
	return c;
}

/* Straight alpha. a scales the source's alpha (0..255). */
static void put(unsigned char *d, const unsigned char *s, int a, int copy)
{
	int sa = s[3] * a / 255, da, oa, k;

	if (copy) {
		d[0] = s[0];
		d[1] = s[1];
		d[2] = s[2];
		d[3] = (unsigned char)sa;
		return;
	}
	if (sa <= 0)
		return;
	da = d[3] * (255 - sa) / 255;
	oa = sa + da;
	for (k = 0; k < 3; k++)
		d[k] = (unsigned char)((s[k] * sa + d[k] * da + oa / 2) / oa);
	d[3] = (unsigned char)oa;
}

/* The pixels a CG layer reads: the game's own picture for the number being
 * composed (so a page can be made from itself) or one being built now,
 * else whatever the number shows, mod images included. */
static struct cg *layer_source(struct img *m, int id)
{
	struct archive_data *data;
	struct cg *c;
	struct img *o = find_img(id);

	if (id != m->id && !(o && o->building))
		return asset_cg_load(id);
	data = asset_get(ASSET_CG, id);
	if (!data)
		return NULL;
	c = cg_load_data(data);
	archive_free_data(data);
	return c;
}

static void draw_cg_layer(struct cg *dst, struct img *m, const struct layer *l)
{
	struct cg *src = layer_source(m, l->cg);
	int sx, sy, sw, sh, dw, dh, x, y;

	if (!src || !src->pixels) {
		rq_log("image.compose: CG %d not found", l->cg);
		if (src)
			cg_free(src);
		return;
	}
	sx = l->sx;
	sy = l->sy;
	sw = l->sw > 0 ? l->sw : src->metrics.w - sx;
	sh = l->sh > 0 ? l->sh : src->metrics.h - sy;
	if (sx < 0 || sy < 0 || sw <= 0 || sh <= 0 || sx + sw > src->metrics.w || sy + sh > src->metrics.h) {
		rq_log("image.compose: the part of CG %d is outside it", l->cg);
		cg_free(src);
		return;
	}
	dw = l->w > 0 ? l->w : sw;
	dh = l->h > 0 ? l->h : sh;
	for (y = 0; y < dh; y++) {
		int ty = l->y + y;
		if (ty < 0 || ty >= dst->metrics.h)
			continue;
		for (x = 0; x < dw; x++) {
			int tx = l->x + x;
			unsigned char p[4];
			if (tx < 0 || tx >= dst->metrics.w)
				continue;
			if (dw == sw && dh == sh) {
				memcpy(p, px(src, sx + x, sy + y), 4);
			} else {
				double u0 = sx + (double)x * sw / dw, u1 = sx + (double)(x + 1) * sw / dw;
				double v0 = sy + (double)y * sh / dh, v1 = sy + (double)(y + 1) * sh / dh;
				sample(src, u0, v0, u1, v1, p);
			}
			put((unsigned char *)dst->pixels + ((size_t)ty * dst->metrics.w + tx) * 4, p, l->alpha, l->copy);
		}
	}
	cg_free(src);
}

static void draw_fill_layer(struct cg *dst, const struct layer *l)
{
	int w = l->w > 0 ? l->w : dst->metrics.w - l->x;
	int h = l->h > 0 ? l->h : dst->metrics.h - l->y;
	unsigned char p[4] = { (unsigned char)l->r, (unsigned char)l->g, (unsigned char)l->b, (unsigned char)l->a };
	int x, y;

	for (y = l->y; y < l->y + h; y++) {
		if (y < 0 || y >= dst->metrics.h)
			continue;
		for (x = l->x; x < l->x + w; x++) {
			if (x < 0 || x >= dst->metrics.w)
				continue;
			put((unsigned char *)dst->pixels + ((size_t)y * dst->metrics.w + x) * 4, p, l->alpha, l->copy);
		}
	}
}

static void blit_rgba(struct cg *dst, const unsigned char *t, int tw, int th, int ox, int oy,
		      const struct layer *l)
{
	int x, y;
	for (y = 0; y < th; y++) {
		if (oy + y < 0 || oy + y >= dst->metrics.h)
			continue;
		for (x = 0; x < tw; x++) {
			if (ox + x < 0 || ox + x >= dst->metrics.w)
				continue;
			put((unsigned char *)dst->pixels + ((size_t)(oy + y) * dst->metrics.w + ox + x) * 4,
			    t + ((size_t)y * tw + x) * 4, l->alpha, l->copy);
		}
	}
}

/* Draws the layer's text with the game, now if it is running, else when the CG is built. */
static void render_game_text(struct layer *l)
{
	if (l->gpix || !rq_gametext_available())
		return;
	l->gpix = rq_gametext_rgba(l->text, &l->gt, &l->gw, &l->gh);
	if (l->shadow) {
		struct rq_gametext g2 = l->gt;
		g2.r = g2.er = l->sr;
		g2.g = g2.eg = l->sg;
		g2.b = g2.eb = l->sb;
		l->spix = rq_gametext_rgba(l->text, &g2, &l->sw2, &l->sh2);
	}
}

static void draw_game_text_layer(struct cg *dst, const struct layer *l)
{
	int ox = l->x, oy = l->y;
	render_game_text((struct layer *)l);
	if (!l->gpix)
		return;
	if (l->w > 0 && l->align)
		ox += l->align == 1 ? (l->w - l->gw + 1) / 2 : l->w - l->gw;
	if (l->h > 0 && l->valign)
		oy += l->valign == 1 ? rq_gametext_center_y(&l->gt, l->h, l->gh) : l->h - l->gh;
	if (l->spix && l->sa > 0) {
		/* The shadow's own alpha (2.11.0). */
		struct layer sl = *l;
		sl.alpha = l->alpha * l->sa / 255;
		blit_rgba(dst, l->spix, l->sw2, l->sh2, ox + l->dx, oy + l->dy, &sl);
	}
	blit_rgba(dst, l->gpix, l->gw, l->gh, ox, oy, l);
}

static void draw_text_layer(struct cg *dst, const struct layer *l)
{
	int tw, th, text_w, x, y, ox = l->x, oy = l->y;
	unsigned char *t;

	if (l->game) {
		draw_game_text_layer(dst, l);
		return;
	}
	t = rq_hud_text_rgba(l->text, l->size, l->r, l->g, l->b, l->edge, l->bold,
			     &tw, &th, &text_w);

	if (!t)
		return;
	if (l->w > 0 && l->align)
		ox += l->align == 1 ? (l->w - text_w) / 2 : l->w - text_w;
	/* The glyphs fill about the font size from the top, as in rq.hud.text. */
	if (l->h > 0 && l->valign)
		oy += l->valign == 1 ? (l->h - (int)l->size) / 2 : l->h - (int)l->size;
	for (y = 0; y < th; y++) {
		if (oy + y < 0 || oy + y >= dst->metrics.h)
			continue;
		for (x = 0; x < tw; x++) {
			if (ox + x < 0 || ox + x >= dst->metrics.w)
				continue;
			put((unsigned char *)dst->pixels + ((size_t)(oy + y) * dst->metrics.w + ox + x) * 4,
			    t + ((size_t)y * tw + x) * 4, l->alpha, l->copy);
		}
	}
	free(t);
}

static struct cg *build_compose(struct img *m)
{
	struct cg *c;
	int i;

	/* Made before the game's archives were open: the size is known now. */
	if (m->w <= 0 || m->h <= 0) {
		struct cg_metrics om;
		const struct layer *l = &m->layers[0];
		if (m->replace && asset_cg_get_metrics_original(m->id, &om)) {
			m->w = om.w;
			m->h = om.h;
		} else if (!m->replace && l->kind == LAYER_CG && asset_cg_get_metrics(l->cg, &om)) {
			m->w = l->x + (l->w > 0 ? l->w : l->sw > 0 ? l->sw : om.w);
			m->h = l->y + (l->h > 0 ? l->h : l->sh > 0 ? l->sh : om.h);
		}
		if (m->w <= 0 || m->h <= 0 || m->w > 4096 || m->h > 4096) {
			rq_log("image.compose: CG %d has no size; give opts.w and opts.h", m->id);
			m->w = m->h = 0;
			return NULL;
		}
	}
	c = new_cg(m->w, m->h);

	for (i = 0; i < m->nlayers; i++) {
		const struct layer *l = &m->layers[i];
		if (l->kind == LAYER_CG)
			draw_cg_layer(c, m, l);
		else if (l->kind == LAYER_FILL)
			draw_fill_layer(c, l);
		else
			draw_text_layer(c, l);
	}
	return c;
}

/* ---- moving pictures (2.25.0) ---- */

enum { KIND_STILL, KIND_GIF, KIND_WEBP };

#define WEBP_SIDE_MAX 4096

/* A GIF, an animated WebP (a VP8X chunk with the animation flag), or anything else. */
static int file_kind(const char *path)
{
	unsigned char h[32];
	size_t n;
	FILE *f = fopen(path, "rb");

	if (!f)
		return KIND_STILL;
	n = fread(h, 1, sizeof h, f);
	fclose(f);
	if (n >= 6 && (!memcmp(h, "GIF87a", 6) || !memcmp(h, "GIF89a", 6)))
		return KIND_GIF;
	if (n >= 21 && !memcmp(h, "RIFF", 4) && !memcmp(h + 8, "WEBPVP8X", 8) && (h[20] & 0x02))
		return KIND_WEBP;
	return KIND_STILL;
}

struct anim_load {
	struct img *m;
	int tw, th;        /* the size to fit to; 0 keeps the file's */
	struct cg *still;  /* the first frame, fitted */
	int started, n;
};

/* One frame, RGBA at the file's size (c is taken): fitted as the still is, then given to
 * the moving store. */
static const char *anim_put(struct anim_load *ld, struct cg *c, int delay_ms)
{
	struct cg *f = fit_cg(c, ld->tw, ld->th, ld->m->fit);
	const char *err = NULL;

	if (!ld->started) {
		if (!rq_moving_begin(ld->m->id, f->metrics.w, f->metrics.h, ld->m->fps, ld->m->real, &err)) {
			cg_free(f);
			return err;
		}
		ld->started = 1;
	}
	err = rq_moving_frame(ld->m->id, f->pixels, delay_ms);
	if (!err && !ld->still) {
		ld->still = f;
		f = NULL;
	}
	if (f)
		cg_free(f);
	ld->n++;
	return err;
}

static const char *gif_cb(void *ctx, const uint8_t *argb, int w, int h, int delay_cs)
{
	struct cg *c = new_cg(w, h);
	unsigned char *o = c->pixels;
	size_t i, cells = (size_t)w * h;

	for (i = 0; i < cells; i++) {
		o[i * 4] = argb[i * 4 + 1];
		o[i * 4 + 1] = argb[i * 4 + 2];
		o[i * 4 + 2] = argb[i * 4 + 3];
		o[i * 4 + 3] = argb[i * 4];
	}
	/* As browsers do: a delay under 20 ms plays as 100 ms. */
	return anim_put(ctx, c, delay_cs >= 2 ? delay_cs * 10 : 100);
}

static const char *webp_frames(const char *path, struct anim_load *ld)
{
	WebPAnimDecoderOptions opt;
	WebPAnimDecoder *dec;
	WebPAnimInfo info;
	WebPData data;
	const char *err = NULL;
	uint8_t *buf;
	long len;
	int prev = 0;
	FILE *f = fopen(path, "rb");

	if (!f)
		return "not found";
	fseek(f, 0, SEEK_END);
	len = ftell(f);
	fseek(f, 0, SEEK_SET);
	if (len <= 0) {
		fclose(f);
		return "empty";
	}
	buf = xmalloc((size_t)len);
	if (fread(buf, 1, (size_t)len, f) != (size_t)len) {
		fclose(f);
		free(buf);
		return "could not be read";
	}
	fclose(f);
	data.bytes = buf;
	data.size = (size_t)len;
	if (!WebPAnimDecoderOptionsInit(&opt)) {
		free(buf);
		return "the WebP decoder did not start";
	}
	opt.color_mode = MODE_RGBA;
	opt.use_threads = 0;
	if (!(dec = WebPAnimDecoderNew(&data, &opt))) {
		free(buf);
		return "not an animated WebP it can read";
	}
	if (!WebPAnimDecoderGetInfo(dec, &info) || info.canvas_width < 1 || info.canvas_height < 1
	    || info.canvas_width > WEBP_SIDE_MAX || info.canvas_height > WEBP_SIDE_MAX)
		err = "WebP size must be 1 to 4096 pixels per side";
	while (!err && WebPAnimDecoderHasMoreFrames(dec)) {
		uint8_t *px;
		int ts;
		struct cg *c;
		if (!WebPAnimDecoderGetNext(dec, &px, &ts)) {
			err = "a broken WebP frame";
			break;
		}
		c = new_cg((int)info.canvas_width, (int)info.canvas_height);
		memcpy(c->pixels, px, (size_t)info.canvas_width * info.canvas_height * 4);
		err = anim_put(ld, c, ts - prev);
		prev = ts;
	}
	WebPAnimDecoderDelete(dec);
	free(buf);
	return err;
}

/* T_Sprite::stretchCg is wrapped once a moving picture exists: after the original, the
 * sprite it made shows the CG whole (VM_XSYS_IMAGE_STRETCH). Only T_CharaFace calls it,
 * for the faces on every character card. */
static int g_stretch_on;
static int g_m_spno = -2;

static void emit0(struct buffer *out, enum opcode op)
{
	buffer_write_int16(out, (uint16_t)op);
}

static void emit1(struct buffer *out, enum opcode op, int32_t arg)
{
	buffer_write_int16(out, (uint16_t)op);
	buffer_write_int32(out, (uint32_t)arg);
}

static void install_stretch_hook(void)
{
	struct buffer out;
	char *sj;
	int fno, dup, nargs, i;

	if (g_stretch_on || !ain)
		return;
	g_stretch_on = 1;
	sj = utf2sjis("T_Sprite@stretchCg", 0);
	fno = sj ? ain_get_function(ain, sj) : -1;
	free(sj);
	if (fno < 0) {
		rq_log("image: T_Sprite@stretchCg not found; card faces show a moving picture's first frame");
		return;
	}
	nargs = ain->functions[fno].nr_args;
	dup = ain_dup_function(ain, fno);
	buffer_init(&out, NULL, 0);
	emit1(&out, FUNC, fno);
	emit0(&out, PUSHSTRUCTPAGE);
	for (i = 0; i < nargs; i++)
		emit1(&out, SH_LOCALREF, i);
	emit1(&out, CALLMETHOD, dup);
	/* The result stays for RETURN; the copy, this and the CG number go to the syscall. */
	emit0(&out, DUP);
	emit0(&out, PUSHSTRUCTPAGE);
	emit1(&out, SH_LOCALREF, 0);
	emit1(&out, CALLSYS, VM_XSYS_IMAGE_STRETCH);
	emit0(&out, RETURN);
	emit1(&out, ENDFUNC, fno);
	ain->functions[fno].address = (uint32_t)(ain->code_size + 6);
	ain->code = xrealloc(ain->code, ain->code_size + out.index);
	memcpy(ain->code + ain->code_size, out.buf, out.index);
	ain->code_size += out.index;
	free(out.buf);
	rq_log("image: wrapped T_Sprite@stretchCg (moving pictures on card faces)");
}

static int sprite_spno_member(void)
{
	char *st, *mem;
	int s, i;

	if (g_m_spno != -2)
		return g_m_spno;
	g_m_spno = -1;
	st = utf2sjis("T_Sprite", 0);
	mem = utf2sjis("m_nSpNo", 0);
	s = st ? ain_get_struct(ain, st) : -1;
	for (i = 0; s >= 0 && mem && i < ain->structures[s].nr_members; i++) {
		if (ain->structures[s].members[i].name && !strcmp(ain->structures[s].members[i].name, mem)) {
			g_m_spno = i;
			break;
		}
	}
	free(st);
	free(mem);
	return g_m_spno;
}

int rq_image_syscall(int code)
{
	int cg, page_no, ok, mem;
	struct page *pg;

	if (code != VM_XSYS_IMAGE_STRETCH)
		return 0;
	cg = stack_pop().i;
	page_no = stack_pop().i;
	ok = stack_pop().i;
	if (!ok || cg <= 0 || (mem = sprite_spno_member()) < 0)
		return 1;
	pg = heap_get_page(page_no);
	if (pg && mem < pg->nr_vars)
		rq_moving_tag(pg->values[mem].i, cg);
	return 1;
}

/* A GIF or an animated WebP: the first frame becomes m->cg, and the frames go to the
 * moving store. NULL with m->err on a bad file. */
static struct cg *anim_decode(struct img *m, int kind)
{
	struct anim_load ld = { 0 };
	const char *err;
	int w, h, count;

	ld.m = m;
	ld.tw = m->w;
	ld.th = m->h;
	if (m->replace && !m->keep_size && ld.tw <= 0) {
		struct cg_metrics om;
		if (asset_cg_get_metrics_original(m->id, &om)) {
			ld.tw = om.w;
			ld.th = om.h;
		}
	}
	if (kind == KIND_GIF)
		err = rq_gif_frames(m->path, gif_cb, &ld, &w, &h, &count);
	else
		err = webp_frames(m->path, &ld);
	if (!err && !ld.still)
		err = "it has no frames";
	rq_moving_end(m->id, !err && ld.n > 1);
	if (err) {
		snprintf(m->err, sizeof m->err, "%s", err);
		rq_log("image: %s: %s", m->path, err);
		if (ld.still)
			cg_free(ld.still);
		m->failed = 1;
		return NULL;
	}
	if (ld.n > 1)
		install_stretch_hook();
	return m->cg = ld.still;
}

/* A still picture file's bytes at most. */
#define STILL_FILE_BYTES (256u << 20)

/* A mod's still picture: the file's bytes read as the mod paths are made (fopen on the resolved
 * path, in the ANSI code page like every mod path), a PNG through rq_pixels (every kind of PNG, and
 * a reason instead of an abort), the rest through libsys4. NULL with the reason in `why`. */
static struct cg *load_still(const char *path, char *why, size_t why_len)
{
	FILE *f = fopen(path, "rb");
	long n;
	uint8_t *data;
	struct cg *cg;

	why[0] = '\0';
	if (!f) {
		snprintf(why, why_len, "it cannot be opened");
		return NULL;
	}
	if (fseek(f, 0, SEEK_END) || (n = ftell(f)) < 0 || fseek(f, 0, SEEK_SET)) {
		fclose(f);
		snprintf(why, why_len, "it cannot be read");
		return NULL;
	}
	if ((unsigned long)n > STILL_FILE_BYTES) {
		fclose(f);
		snprintf(why, why_len, "the file is too large");
		return NULL;
	}
	/* libsys4's format checks read a file's first bytes (up to the 24th) with no size check; no
	 * picture is this small. */
	if (n < 32) {
		fclose(f);
		snprintf(why, why_len, "the file is too small to be a picture");
		return NULL;
	}
	data = malloc((size_t)n);
	if (!data || fread(data, 1, (size_t)n, f) != (size_t)n) {
		free(data);
		fclose(f);
		snprintf(why, why_len, "it cannot be read");
		return NULL;
	}
	fclose(f);
	if (rq_pixels_is_png(data, (size_t)n)) {
		cg = rq_pixels_png(data, (size_t)n, RQ_PIXELS_PICTURE_SIDE, why, why_len);
	} else {
		cg = cg_load_buffer(data, (size_t)n);
		if (cg && (!cg->pixels || cg->metrics.w <= 0 || cg->metrics.h <= 0)) {
			cg_free(cg);
			cg = NULL;
		}
		if (!cg) {
			snprintf(why, why_len, "not a picture the engine reads");
		} else if (cg->metrics.w > RQ_PIXELS_PICTURE_SIDE || cg->metrics.h > RQ_PIXELS_PICTURE_SIDE) {
			snprintf(why, why_len, "%dx%d (max %d a side)", cg->metrics.w, cg->metrics.h,
				 RQ_PIXELS_PICTURE_SIDE);
			cg_free(cg);
			cg = NULL;
		}
	}
	free(data);
	return cg;
}

static struct cg *decode(struct img *m)
{
	struct cg *cg;
	int tw = m->w, th = m->h, kind;

	if (m->cg)
		return m->cg;
	if (m->failed)
		return NULL;
	if (m->id == PLACEHOLDER_ID)
		return m->cg = make_placeholder();
	if (m->id == DISCORD_ID) {
		cg = cg_load_buffer((uint8_t *)rq_discord_png, rq_discord_png_len);
		if (!cg || !cg->pixels) {
			rq_log("image: cannot decode the built-in Discord icon");
			if (cg)
				cg_free(cg);
			m->failed = 1;
			return NULL;
		}
		return m->cg = fit_cg(cg, DISCORD_SIDE, DISCORD_SIDE, FIT_CONTAIN);
	}
	if (m->layers) {
		m->building = 1;
		m->cg = build_compose(m);
		m->building = 0;
		if (!m->cg)
			m->failed = 1;
		return m->cg;
	}
	if ((kind = file_kind(m->path)) != KIND_STILL)
		return anim_decode(m, kind);
	char why[200];
	cg = load_still(m->path, why, sizeof why);
	if (!cg || !cg->pixels) {
		rq_log("image: cannot decode %s: %s", m->path, why);
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
	free_layers(m);
	rq_moving_drop(game_id);
	m->replace = 1;
	m->fit = src->fit;
	m->w = m->h = 0;
	m->keep_size = 0;
	m->failed = 0;
	m->err[0] = 0;
	m->fps = src->fps;
	m->real = src->real;
	snprintf(m->path, sizeof m->path, "%s", src->path);
	if (file_kind(m->path) != KIND_STILL)
		decode(m);
	cg_changed(game_id);
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

int rq_image_discord(void)
{
	if (!find_img(DISCORD_ID)) {
		if (g_nimg >= IMG_MAX)
			return 0;
		memset(&g_img[g_nimg], 0, sizeof g_img[g_nimg]);
		g_img[g_nimg++].id = DISCORD_ID;
	}
	return DISCORD_ID;
}

/* ---- bitmaps inside the game's Flash files (2.25.0) ----
 * rq.image.flash_bitmap keeps a picture fitted to one bitmap of a game Flash file, and
 * parts_flash_load hands each game file it reads to rq_flash_swap, which puts the picture
 * in the bitmap's place before the file plays. Files are matched as the archive matches
 * them: archive_basename (no extension, upper case, backslashes). */

#define FBMP_MAX 256
#define FBMP_SIDE_MAX 4096
#define FBMP_PIXELS (32u * 1024u * 1024u)

static int parse_fit(lua_State *L, int opts, int def);

struct fbmp {
	char base[256];  /* archive_basename of the file */
	int id, w, h;
	uint8_t *rgba;   /* fitted to w x h */
	int used;        /* loads of the file that got it */
	int mod_file;    /* for a mod's own file (add_flash, 2.63.0), not the game's */
};

static struct fbmp g_fbmp[FBMP_MAX];
static int g_nfbmp;
static size_t g_fbmp_pixels;

static void flash_base(const char *full, char *out, size_t n)
{
	char *b = archive_basename(full);
	snprintf(out, n, "%s", b);
	free(b);
}

static struct fbmp *fbmp_find(const char *base, int id)
{
	int i;
	for (i = 0; i < g_nfbmp; i++) {
		if (g_fbmp[i].id == id && !strcmp(g_fbmp[i].base, base))
			return &g_fbmp[i];
	}
	return NULL;
}

static void fbmp_remove(struct fbmp *b)
{
	g_fbmp_pixels -= (size_t)b->w * b->h;
	free(b->rgba);
	*b = g_fbmp[--g_nfbmp];
}

/* The game's own file, read from the archive as parts_flash_load reads it; since 2.63.0, when
 * the game has none, a mod's own file of that name (rq.image.add_flash), with *mod_file set.
 * A game file a mod replaced is still the game's here, as before. */
static struct swf *game_flash(const char *full, int *mod_file)
{
	struct archive_data *d = asset_get_by_name(ASSET_FLASH, full, NULL);
	struct swf *swf;
	uint8_t *data;
	size_t size;

	*mod_file = 0;
	if (!d) {
		if (!rq_anim_added_flash(full, &data, &size))
			return NULL;
		*mod_file = 1;
		return swf_load(data, size);
	}
	swf = aff_load(d->data, d->size);
	archive_free_data(d);
	return swf;
}

static struct swf_tag_define_bits_lossless *bitmap_tag(struct swf_tag *tag, int id)
{
	for (; tag; tag = tag->next) {
		if (tag->type == TAG_DEFINE_BITS_LOSSLESS || tag->type == TAG_DEFINE_BITS_LOSSLESS2) {
			struct swf_tag_define_bits_lossless *b = (struct swf_tag_define_bits_lossless *)tag;
			if (id < 0 || b->character_id == id)
				return b;
		}
	}
	return NULL;
}

void rq_flash_swap(const char *name, struct swf *swf, int from_mod)
{
	char base[256];
	struct swf_tag *tag;
	int i;

	if (!g_nfbmp || !name || !swf)
		return;
	flash_base(name, base, sizeof base);
	for (i = 0; i < g_nfbmp; i++) {
		struct fbmp *b = &g_fbmp[i];
		struct swf_tag_define_bits_lossless *t;
		size_t bytes;

		/* A game file a mod replaced plays the mod's file, with none of the game's bitmaps;
		 * a mod's own file takes the pictures given for it. */
		if (strcmp(b->base, base) || b->mod_file != (from_mod ? 1 : 0))
			continue;
		for (tag = swf->tags; (t = bitmap_tag(tag, b->id)); tag = t->t.next) {
			if (t->width != b->w || t->height != b->h)
				continue;
			bytes = (size_t)b->w * b->h * 4;
			free(t->data);
			t->data = xmalloc(bytes);
			memcpy(t->data, b->rgba, bytes);
			b->used++;
		}
	}
}

/* Every bitmap of a game Flash file, as the game has it, or of a mod's own file (2.63.0).
 * -1 for a file that is neither. */
int rq_flash_bitmaps_each(const char *name_utf8, rq_flash_bitmap_fn fn, void *ctx)
{
	char full[256], base[256];
	struct swf *swf;
	struct swf_tag_define_bits_lossless *t;
	struct swf_tag *tag;
	int n = 0, mod_file;

	rq_anim_full_name(name_utf8, full, sizeof full);
	if (!(swf = game_flash(full, &mod_file)))
		return -1;
	flash_base(full, base, sizeof base);
	for (tag = swf->tags; (t = bitmap_tag(tag, -1)); tag = t->t.next) {
		struct fbmp *b = fbmp_find(base, t->character_id);
		if (b && b->mod_file != mod_file)
			b = NULL;
		fn(ctx, t->character_id, t->width, t->height, t->data, b ? 1 : 0, b ? b->used : 0);
		n++;
	}
	swf_free(swf);
	return n;
}

static int fit_of(lua_State *L, int opts)
{
	int fit = parse_fit(L, opts, FIT_STRETCH);
	if (fit == FIT_NONE)
		return luaL_error(L, "flash_bitmap fit must be stretch, contain or cover");
	return fit;
}

/* flash_bitmap(file, id, path|nil[, opts]) */
int rq_lua_image_flash_bitmap(lua_State *L)
{
	const char *file = luaL_checkstring(L, 1);
	int id = (int)luaL_checkinteger(L, 2);
	const char *rel = luaL_optstring(L, 3, NULL);
	int opts = lua_istable(L, 4) ? 4 : 0;
	char full[256], base[256], path[RQ_PATH_MAX];
	struct swf_tag_define_bits_lossless *t;
	struct swf *swf;
	struct fbmp *b;
	struct cg *cg;
	int w, h, fit, mod_file;
	FILE *f;

	if (!lua_isnoneornil(L, 4) && !opts)
		return luaL_error(L, "flash_bitmap opts must be a table");
	if (strlen(file) > 200)
		return luaL_error(L, "flash file name is too long");
	rq_anim_full_name(file, full, sizeof full);
	flash_base(full, base, sizeof base);
	if (!rel) {
		if ((b = fbmp_find(base, id))) {
			fbmp_remove(b);
			rq_log("image: %s bitmap %d is the game's again", file, id);
		}
		return 0;
	}
	fit = fit_of(L, opts);
	if (!(swf = game_flash(full, &mod_file)))
		return luaL_error(L, "flash file '%s' is not in the game, nor added with rq.image.add_flash", file);
	t = bitmap_tag(swf->tags, id);
	w = t ? t->width : 0;
	h = t ? t->height : 0;
	swf_free(swf);
	if (!t)
		return luaL_error(L, "flash file '%s' has no bitmap %d (rq.image.flash_bitmaps lists them)", file, id);
	if (w <= 0 || h <= 0 || w > FBMP_SIDE_MAX || h > FBMP_SIDE_MAX)
		return luaL_error(L, "flash file '%s' bitmap %d is %dx%d, too large to replace", file, id, w, h);

	if (!rq_sandbox_resolve(L, rel, path, (int)sizeof path))
		return luaL_error(L, "image path '%s' is not inside the mod folder", rel);
	if (!(f = fopen(path, "rb")))
		return luaL_error(L, "image '%s' not found in the mod folder", rel);
	fclose(f);
	if (file_kind(path) != KIND_STILL)
		return luaL_error(L, "image '%s': a Flash bitmap takes a still picture, not a GIF or an animated WebP", rel);
	char why[200];
	cg = load_still(path, why, sizeof why);
	if (!cg || !cg->pixels) {
		if (cg)
			cg_free(cg);
		return luaL_error(L, "image '%s' cannot be decoded: %s", rel, why);
	}
	cg = fit_cg(cg, w, h, fit);
	if (cg->metrics.w != w || cg->metrics.h != h) {
		cg_free(cg);
		return luaL_error(L, "image '%s' could not be fitted to %dx%d", rel, w, h);
	}

	b = fbmp_find(base, id);
	if (!b && g_nfbmp >= FBMP_MAX) {
		cg_free(cg);
		return luaL_error(L, "too many Flash bitmaps replaced (max %d)", FBMP_MAX);
	}
	if (g_fbmp_pixels - (b ? (size_t)b->w * b->h : 0) + (size_t)w * h > FBMP_PIXELS) {
		cg_free(cg);
		return luaL_error(L, "Flash bitmaps take too much memory (max %u pixels in all)", FBMP_PIXELS);
	}
	if (b) {
		g_fbmp_pixels -= (size_t)b->w * b->h;
		free(b->rgba);
	} else {
		b = &g_fbmp[g_nfbmp++];
		memset(b, 0, sizeof *b);
		snprintf(b->base, sizeof b->base, "%s", base);
		b->id = id;
	}
	b->mod_file = mod_file;
	b->w = w;
	b->h = h;
	b->rgba = xmalloc((size_t)w * h * 4);
	memcpy(b->rgba, cg->pixels, (size_t)w * h * 4);
	g_fbmp_pixels += (size_t)w * h;
	cg_free(cg);
	rq_log("image: %s bitmap %d (%dx%d) shows %s", file, id, w, h, rel);
	return 0;
}

struct bitmaps_list {
	lua_State *L;
	int n;
};

static void bitmaps_row(void *ctx, int id, int w, int h, const uint8_t *rgba, int mod, int used)
{
	struct bitmaps_list *bl = ctx;
	lua_State *L = bl->L;
	(void)rgba;
	lua_newtable(L);
	lua_pushinteger(L, id);
	lua_setfield(L, -2, "id");
	lua_pushinteger(L, w);
	lua_setfield(L, -2, "w");
	lua_pushinteger(L, h);
	lua_setfield(L, -2, "h");
	lua_pushboolean(L, mod);
	lua_setfield(L, -2, "mod");
	if (mod) {
		lua_pushinteger(L, used);
		lua_setfield(L, -2, "used");
	}
	lua_rawseti(L, -2, ++bl->n);
}

/* flash_bitmaps(file) -> { {id, w, h, mod, used?}, ... } */
int rq_lua_image_flash_bitmaps(lua_State *L)
{
	const char *file = luaL_checkstring(L, 1);
	struct bitmaps_list bl = { L, 0 };

	if (strlen(file) > 200)
		return luaL_error(L, "flash file name is too long");
	lua_newtable(L);
	if (rq_flash_bitmaps_each(file, bitmaps_row, &bl) < 0)
		return luaL_error(L, "flash file '%s' is not in the game, nor added with rq.image.add_flash", file);
	return 1;
}

/* ---- Lua ---- */

static int parse_fit(lua_State *L, int opts, int def)
{
	const char *s;
	int v = def;

	if (!opts || !lua_istable(L, opts))
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

/* The full path of a file in the calling mod's folder; raises an error for a path outside it
 * or a file that is not there. */
static void resolve_file(lua_State *L, const char *rel, char *full, int n)
{
	FILE *f;

	if (!rq_sandbox_resolve(L, rel, full, n))
		luaL_error(L, "image path '%s' is not inside the mod folder", rel);
	f = fopen(full, "rb");
	if (!f)
		luaL_error(L, "image '%s' not found in the mod folder", rel);
	fclose(f);
}

static void set_path(struct img *m, const char *full)
{
	snprintf(m->path, sizeof m->path, "%s", full);
	free_layers(m);
	if (m->cg) {
		cg_free(m->cg);
		m->cg = NULL;
	}
	rq_moving_drop(m->id);
	m->failed = 0;
	m->err[0] = 0;
}

/* What add and replace are given, read and checked before an image slot is touched, so a
 * call that raises an error leaves the slot as it was (2.25.0). */
struct img_args {
	int fit, w, h, keep_size;
	double fps;       /* opts.fps and opts.clock, for a moving picture */
	int real;
	char path[RQ_PATH_MAX];
};

static void parse_args(lua_State *L, int opts, const char *rel, int def_fit, int def_w, int def_h,
		       struct img_args *a)
{
	memset(a, 0, sizeof *a);
	if (opts && !lua_istable(L, opts))
		opts = 0;
	a->fit = parse_fit(L, opts, def_fit);
	a->w = opts && opt_int(L, opts, "w") ? opt_int(L, opts, "w") : def_w;
	a->h = opts && opt_int(L, opts, "h") ? opt_int(L, opts, "h") : def_h;
	if (opts) {
		lua_getfield(L, opts, "keep_size");
		a->keep_size = lua_toboolean(L, -1);
		lua_pop(L, 1);
		lua_getfield(L, opts, "fps");
		if (!lua_isnil(L, -1)) {
			if (!lua_isnumber(L, -1) || lua_tonumber(L, -1) <= 0 || lua_tonumber(L, -1) > 240)
				luaL_error(L, "image fps must be a number above 0, up to 240");
			a->fps = lua_tonumber(L, -1);
		}
		lua_pop(L, 1);
		lua_getfield(L, opts, "clock");
		if (!lua_isnil(L, -1)) {
			const char *c = lua_isstring(L, -1) ? lua_tostring(L, -1) : "";
			if (!strcmp(c, "real"))
				a->real = 1;
			else if (strcmp(c, "game"))
				luaL_error(L, "image clock must be \"game\" or \"real\"");
		}
		lua_pop(L, 1);
	}
	resolve_file(L, rel, a->path, (int)sizeof a->path);
}

static void apply_args(struct img *m, const struct img_args *a)
{
	m->fit = a->fit;
	m->w = a->w;
	m->h = a->h;
	m->keep_size = a->keep_size;
	m->fps = a->fps;
	m->real = a->real;
	set_path(m, a->path);
}

static void remove_img(struct img *m)
{
	int id = m->id;
	if (m->cg)
		cg_free(m->cg);
	free_layers(m);
	rq_moving_drop(id);
	*m = g_img[--g_nimg];
	cg_changed(id);
}

/* A moving picture is read when it is set, so a bad file is an error for the mod, not a
 * blank picture later. Raises the error after taking the image away. */
static void check_moving(lua_State *L, struct img *m, const char *rel)
{
	char err[160];
	if (file_kind(m->path) == KIND_STILL || decode(m))
		return;
	snprintf(err, sizeof err, "%s", m->err[0] ? m->err : "it cannot be read");
	remove_img(m);
	luaL_error(L, "image '%s': %s", rel, err);
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
	struct img_args a;

	if (m && (m->replace || strcmp(m->key, key)))
		return luaL_error(L, "image key '%s' collides with '%s'; pick another key", key, m->replace ? "a replaced CG" : m->key);
	parse_args(L, opts, rel, def_fit, def_w, def_h, &a);
	a.keep_size = 0;
	m = slot_for(L, id);
	m->replace = 0;
	snprintf(m->key, sizeof m->key, "%s", key);
	apply_args(m, &a);
	check_moving(L, m, rel);
	cg_changed(id);
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
	struct img_args a;
	struct img *m;

	if (id <= 0 || id >= IMG_BASE)
		return luaL_error(L, "replace needs a game CG number");
	parse_args(L, 3, path, FIT_STRETCH, 0, 0, &a);
	m = slot_for(L, id);
	m->replace = 1;
	m->key[0] = 0;
	apply_args(m, &a);
	check_moving(L, m, path);
	cg_changed(id);
	return 0;
}

int rq_lua_image_restore(lua_State *L)
{
	int id = (int)luaL_checkinteger(L, 1);
	int i;

	for (i = 0; i < g_nimg; i++) {
		if (g_img[i].id == id && g_img[i].replace) {
			remove_img(&g_img[i]);
			lua_pushboolean(L, 1);
			return 1;
		}
	}
	lua_pushboolean(L, 0);
	return 1;
}

static int field_int(lua_State *L, int t, const char *k, int def)
{
	int v = def;
	lua_getfield(L, t, k);
	if (lua_isnumber(L, -1))
		v = (int)lua_tointeger(L, -1);
	lua_pop(L, 1);
	return v;
}

/* {r, g, b, a} by name or by place. */
static void field_color(lua_State *L, int t, const char *k, int *rgba)
{
	static const char *const names[4] = { "r", "g", "b", "a" };
	int i;

	lua_getfield(L, t, k);
	if (lua_istable(L, -1)) {
		for (i = 0; i < 4; i++) {
			lua_getfield(L, -1, names[i]);
			if (!lua_isnumber(L, -1)) {
				lua_pop(L, 1);
				lua_rawgeti(L, -1, i + 1);
			}
			if (lua_isnumber(L, -1))
				rgba[i] = (int)lua_tointeger(L, -1);
			lua_pop(L, 1);
		}
	}
	lua_pop(L, 1);
}

static int field_align(lua_State *L, int t, const char *k)
{
	const char *v;
	int r = 0;

	lua_getfield(L, t, k);
	v = lua_tostring(L, -1);
	if (v && (!strcmp(v, "center") || !strcmp(v, "middle")))
		r = 1;
	else if (v && (!strcmp(v, "end") || !strcmp(v, "right") || !strcmp(v, "bottom")))
		r = 2;
	lua_pop(L, 1);
	return r;
}

static void parse_layer(lua_State *L, int t, struct layer *l)
{
	int rgba[4] = { 255, 255, 255, 255 };
	const char *mode;

	memset(l, 0, sizeof *l);
	l->x = field_int(L, t, "x", 0);
	l->y = field_int(L, t, "y", 0);
	l->w = field_int(L, t, "w", 0);
	l->h = field_int(L, t, "h", 0);
	l->alpha = field_int(L, t, "alpha", 255);
	if (l->alpha < 0)
		l->alpha = 0;
	if (l->alpha > 255)
		l->alpha = 255;
	lua_getfield(L, t, "mode");
	mode = lua_tostring(L, -1);
	if (mode && !strcmp(mode, "copy"))
		l->copy = 1;
	else if (mode && strcmp(mode, "over"))
		luaL_error(L, "image.compose: mode must be over or copy");
	lua_pop(L, 1);

	lua_getfield(L, t, "cg");
	if (lua_isnumber(L, -1)) {
		l->kind = LAYER_CG;
		l->cg = (int)lua_tointeger(L, -1);
		lua_pop(L, 1);
		lua_getfield(L, t, "src");
		if (lua_istable(L, -1)) {
			int s = lua_gettop(L), *f[4] = { &l->sx, &l->sy, &l->sw, &l->sh }, i;
			for (i = 0; i < 4; i++) {
				lua_rawgeti(L, s, i + 1);
				*f[i] = (int)lua_tointeger(L, -1);
				lua_pop(L, 1);
			}
		}
		lua_pop(L, 1);
		return;
	}
	lua_pop(L, 1);

	lua_getfield(L, t, "text");
	if (lua_isstring(L, -1)) {
		l->kind = LAYER_TEXT;
		l->text = strdup(lua_tostring(L, -1));
		lua_pop(L, 1);
		lua_getfield(L, t, "size");
		l->size = lua_isnumber(L, -1) ? (float)lua_tonumber(L, -1) : 16.0f;
		lua_pop(L, 1);
		field_color(L, t, "color", rgba);
		l->r = rgba[0];
		l->g = rgba[1];
		l->b = rgba[2];
		l->align = field_align(L, t, "align");
		l->valign = field_align(L, t, "valign");
		lua_getfield(L, t, "edge");
		l->edge = lua_isnil(L, -1) ? 1 : lua_toboolean(L, -1);
		lua_pop(L, 1);
		lua_getfield(L, t, "bold");
		l->bold = lua_toboolean(L, -1);
		lua_pop(L, 1);
		{
			int has_shadow, dx = 0, dy = 0, sr = 0, sg = 0, sb = 0, sa = 255;
			if (rq_hud_game_style(L, t, &l->gt, &has_shadow, &dx, &dy, &sr, &sg, &sb, &sa)) {
				l->gt.r = l->r;
				l->gt.g = l->g;
				l->gt.b = l->b;
				l->game = 1;
				l->shadow = has_shadow;
				l->sr = sr;
				l->sg = sg;
				l->sb = sb;
				l->sa = sa;
				l->dx = dx;
				l->dy = dy;
				render_game_text(l);
			}
		}
		return;
	}
	lua_pop(L, 1);

	lua_getfield(L, t, "fill");
	if (lua_istable(L, -1)) {
		lua_pop(L, 1);
		l->kind = LAYER_FILL;
		field_color(L, t, "fill", rgba);
		l->r = rgba[0];
		l->g = rgba[1];
		l->b = rgba[2];
		l->a = rgba[3];
		return;
	}
	lua_pop(L, 1);
	luaL_error(L, "image.compose: each layer needs cg, text or fill");
}

/* rq.image.compose(key | cg_no, layers [, {w, h}]) -> cg_no */
int rq_lua_image_compose(lua_State *L)
{
	int id, w, h, n, i, replace = lua_type(L, 1) == LUA_TNUMBER;
	struct layer *layers;
	struct img *m;

	luaL_checktype(L, 2, LUA_TTABLE);
	if (replace) {
		id = (int)lua_tointeger(L, 1);
		if (id <= 0 || id >= IMG_BASE)
			return luaL_error(L, "image.compose needs a key or a game CG number");
	} else {
		const char *key = luaL_checkstring(L, 1);
		if (strlen(key) >= 100)
			return luaL_error(L, "image key is too long");
		id = key_to_id(key);
		m = find_img(id);
		if (m && (m->replace || strcmp(m->key, key)))
			return luaL_error(L, "image key '%s' collides with '%s'; pick another key", key,
					  m->replace ? "a replaced CG" : m->key);
	}
	n = (int)lua_rawlen(L, 2);
	if (n <= 0 || n > 256)
		return luaL_error(L, "image.compose needs 1 to 256 layers");
	layers = xcalloc((size_t)n, sizeof *layers);
	for (i = 0; i < n; i++) {
		lua_rawgeti(L, 2, i + 1);
		if (!lua_istable(L, -1)) {
			free(layers);
			return luaL_error(L, "image.compose: layer %d is not a table", i + 1);
		}
		parse_layer(L, lua_gettop(L), &layers[i]);
		lua_pop(L, 1);
	}
	w = lua_istable(L, 3) ? field_int(L, 3, "w", 0) : 0;
	h = lua_istable(L, 3) ? field_int(L, 3, "h", 0) : 0;
	if (w <= 0 || h <= 0) {
		struct cg_metrics cm;
		/* A replaced CG keeps its size; a new one takes the first picture's. */
		if (replace && asset_cg_get_metrics_original(id, &cm)) {
			w = cm.w;
			h = cm.h;
		} else if (layers[0].kind == LAYER_CG && asset_cg_get_metrics(layers[0].cg, &cm)) {
			w = layers[0].x + (layers[0].w > 0 ? layers[0].w : layers[0].sw > 0 ? layers[0].sw : cm.w);
			h = layers[0].y + (layers[0].h > 0 ? layers[0].h : layers[0].sh > 0 ? layers[0].sh : cm.h);
		}
	}
	if (w <= 0 && h <= 0 && (replace || layers[0].kind == LAYER_CG)) {
		w = h = 0; /* found when it is built */
	} else if (w <= 0 || h <= 0 || w > 4096 || h > 4096) {
		for (i = 0; i < n; i++)
			free(layers[i].text);
		free(layers);
		return luaL_error(L, "image.compose: give opts.w and opts.h (1 to 4096)");
	}
	m = slot_for(L, id);
	free_layers(m);
	if (m->cg) {
		cg_free(m->cg);
		m->cg = NULL;
	}
	m->replace = replace;
	if (!replace)
		snprintf(m->key, sizeof m->key, "%s", lua_tostring(L, 1));
	else
		m->key[0] = 0;
	m->path[0] = 0;
	m->failed = 0;
	m->fit = FIT_NONE;
	m->keep_size = 1;
	m->w = w;
	m->h = h;
	m->layers = layers;
	m->nlayers = n;
	cg_changed(id);
	lua_pushinteger(L, id);
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
	{
		int frames, ms, drawn;
		if (rq_moving_info(id, &frames, &ms, &drawn)) {
			lua_pushinteger(L, frames);
			lua_setfield(L, -2, "frames");
			lua_pushinteger(L, ms);
			lua_setfield(L, -2, "ms");
			lua_pushboolean(L, drawn);
			lua_setfield(L, -2, "drawn");
		}
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
