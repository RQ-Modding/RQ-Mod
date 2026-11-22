/* Copyright (C) 2026 Rance Quest modding fork
 *
 * rq.image.add_gif: a GIF from a mod folder served as a game Flash file.
 * The game only plays Flash, so the GIF becomes one in memory: one bitmap, one
 * rectangle shape and one PlaceObject2 per GIF frame, on a 30 fps timeline
 * (T_PartsFlash::update always runs at 30). parts_flash_load asks rq_anim_find
 * before the archive, so a key named like a game file replaces it.
 * A still CG becomes a one-frame file the same way, for the marks on battle cards.
 *
 * Since 2.63.0 rq.image.add_flash puts a mod's own Flash file (.aff or SWF) in the same
 * registry, after the shared checker (swf_check.c) has passed it: nothing of a file it refuses
 * reaches swf.c, which closes the game on what it does not handle. flash_info tells what the
 * game plays under a name, a mod's file or its own.
 */

#ifdef RQ_MODLOADER

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdint.h>
#include <zlib.h>

#include "lua.h"
#include "lauxlib.h"

#include "system4.h"
#include "system4/archive.h"
#include "system4/cg.h"
#include "system4/utfsjis.h"

#include "asset_manager.h"
#include "swf.h"
#include "swf_check.h"

#include "modloader.h"
#include "modloader_priv.h"

/* 128 until 2.63.0, with mods' Flash files beside the GIFs and the marks' pictures. */
#define ANIM_MAX 1024
#define ANIM_SIDE_MAX 1024
#define ANIM_FRAMES_MAX 1024
#define ANIM_PIXELS_MAX (32u * 1024u * 1024u)
/* add_flash: a file as it is in the mod's folder, one uncompressed, and all of them. */
#define ANIM_FLASH_READ (64u * 1024u * 1024u)
#define ANIM_FLASH_FILE (64u * 1024u * 1024u)
#define ANIM_FLASH_BYTES (256u * 1024u * 1024u)

enum { ANIM_GIF, ANIM_CG, ANIM_FLASH };

#define SWF_TAG_END 0
#define SWF_TAG_SHOW_FRAME 1
#define SWF_TAG_DEFINE_SHAPE 2
#define SWF_TAG_PLACE_OBJECT2 26
#define SWF_TAG_BITS_LOSSLESS2 36
#define SWF_PLACE_HAS_CHARACTER 0x02

struct anim {
	char name[256];
	char key[104];
	char path[1024]; /* the GIF or Flash file it was made from */
	uint8_t *swf;    /* always FWS, uncompressed: swf_load reads only that */
	size_t size;
	int w, h, frames, ms;
	int kind;        /* ANIM_* */
	size_t counted;  /* bytes in g_flash_bytes (add_flash) */
};

static struct anim g_anim[ANIM_MAX];
static int g_nanim;
static int g_redirect;
static size_t g_flash_bytes;

/* ---- output ---- */

struct out {
	uint8_t *d;
	size_t n, cap;
};

static void out_bytes(struct out *o, const void *p, size_t n)
{
	if (o->n + n > o->cap) {
		o->cap = (o->n + n) * 2 + 256;
		o->d = xrealloc(o->d, o->cap);
	}
	if (p)
		memcpy(o->d + o->n, p, n);
	o->n += n;
}

static void out_u8(struct out *o, unsigned v)
{
	uint8_t b = (uint8_t)v;
	out_bytes(o, &b, 1);
}

static void out_u16(struct out *o, unsigned v)
{
	out_u8(o, v & 0xff);
	out_u8(o, (v >> 8) & 0xff);
}

static void out_u32(struct out *o, uint32_t v)
{
	out_u16(o, v & 0xffff);
	out_u16(o, v >> 16);
}

/* Most significant bit first, padded to a byte by bits_end. */
struct bits {
	struct out *o;
	unsigned cur;
	int n;
};

static void bits_put(struct bits *b, uint32_t v, int count)
{
	while (count-- > 0) {
		b->cur = (b->cur << 1) | ((v >> count) & 1);
		if (++b->n == 8) {
			out_u8(b->o, b->cur);
			b->cur = 0;
			b->n = 0;
		}
	}
}

static void bits_end(struct bits *b)
{
	if (b->n)
		bits_put(b, 0, 8 - b->n);
}

static void out_rect(struct out *o, int w, int h)
{
	struct bits b = { o, 0, 0 };

	bits_put(&b, 16, 5);
	bits_put(&b, 0, 16);
	bits_put(&b, (uint32_t)w * 20, 16);
	bits_put(&b, 0, 16);
	bits_put(&b, (uint32_t)h * 20, 16);
	bits_end(&b);
}

static void out_tag(struct out *o, int code, const void *body, size_t len)
{
	out_u16(o, (code << 6) | 0x3f);
	out_u32(o, (uint32_t)len);
	out_bytes(o, body, len);
}

/* A shape is one rectangle filled with a bitmap. The engine sizes it from the bounds
 * and skips the edge coordinates (parse_define_shape), so those stay zero. */
static void out_shape(struct out *o, int id, int bitmap, int w, int h)
{
	struct out s = { 0 };
	struct bits b = { &s, 0, 0 };
	int i;

	out_u16(&s, id);
	out_rect(&s, w, h);
	out_u8(&s, 1);
	out_u8(&s, 0x41);
	out_u16(&s, bitmap);
	out_u8(&s, 0);
	out_u8(&s, 0);
	out_u8(&s, 1 << 4);
	bits_put(&b, 4, 6);
	bits_put(&b, 1, 1);
	for (i = 0; i < 4; i++) {
		bits_put(&b, 3, 2);
		bits_put(&b, 0, 4);
		bits_put(&b, 0, 1);
		bits_put(&b, 0, 3);
	}
	bits_put(&b, 0, 6);
	bits_end(&b);
	out_tag(o, SWF_TAG_DEFINE_SHAPE, s.d, s.n);
	free(s.d);
}

static int out_bitmap(struct out *o, int id, const uint8_t *argb, int w, int h)
{
	size_t raw = (size_t)w * h * 4;
	uLongf clen = compressBound((uLong)raw);
	struct out s = { 0 };
	int ok;

	/* A DefineBitsLossless2 holds its colours multiplied by alpha, as the SWF format says
	 * and swf.c reads them back (2.63.0). */
	uint8_t *pm = xmalloc(raw);
	for (size_t i = 0; i < raw; i += 4) {
		unsigned a = argb[i];
		pm[i] = (uint8_t)a;
		for (int k = 1; k < 4; k++)
			pm[i + k] = a == 255 ? argb[i + k] : (uint8_t)((argb[i + k] * a + 127) / 255);
	}
	out_u16(&s, id);
	out_u8(&s, 5);
	out_u16(&s, w);
	out_u16(&s, h);
	out_bytes(&s, NULL, clen);
	ok = compress2(s.d + 7, &clen, pm, (uLong)raw, 6) == Z_OK;
	free(pm);
	if (!ok) {
		free(s.d);
		return 0;
	}
	out_tag(o, SWF_TAG_BITS_LOSSLESS2, s.d, 7 + clen);
	free(s.d);
	return 1;
}

static size_t swf_begin(struct out *o, int w, int h)
{
	size_t frames_at;

	out_bytes(o, "FWS\x08", 4);
	out_u32(o, 0);
	out_rect(o, w, h);
	out_u16(o, 30 << 8);
	frames_at = o->n;
	out_u16(o, 0);
	return frames_at;
}

static void swf_end(struct out *o, size_t frames_at, int frames)
{
	out_tag(o, SWF_TAG_END, NULL, 0);
	o->d[frames_at] = frames & 0xff;
	o->d[frames_at + 1] = (frames >> 8) & 0xff;
	o->d[4] = o->n & 0xff;
	o->d[5] = (o->n >> 8) & 0xff;
	o->d[6] = (o->n >> 16) & 0xff;
	o->d[7] = (o->n >> 24) & 0xff;
}

static void out_place(struct out *o, int shape)
{
	uint8_t place[5] = { SWF_PLACE_HAS_CHARACTER, 1, 0, (uint8_t)shape, (uint8_t)(shape >> 8) };

	out_tag(o, SWF_TAG_PLACE_OBJECT2, place, sizeof place);
}

/* ---- GIF ---- */

struct gif {
	const uint8_t *d;
	size_t n, i;
};

/* Decodes one image's codes into w * h palette indexes. A stream that ends early
 * leaves the rest at index 0, as other decoders do. */
static const char *lzw_decode(const uint8_t *in, size_t n, int min, uint8_t *dst, size_t want)
{
	static uint16_t prefix[4096];
	static uint8_t suffix[4096], stack[4097];
	int clear = 1 << min, end = clear + 1, avail = clear + 2;
	int size = min + 1, mask = (1 << size) - 1, prev = -1, first = 0, i;
	uint32_t acc = 0;
	int have = 0;
	size_t pos = 0, out = 0;

	if (min < 1 || min > 11)
		return "bad LZW code size";
	for (i = 0; i < clear; i++) {
		prefix[i] = 0;
		suffix[i] = (uint8_t)i;
	}
	while (out < want) {
		int code, cur, sp = 0;

		while (have < size) {
			if (pos >= n)
				return NULL;
			acc |= (uint32_t)in[pos++] << have;
			have += 8;
		}
		code = acc & mask;
		acc >>= size;
		have -= size;
		if (code == end)
			return NULL;
		if (code == clear) {
			avail = clear + 2;
			size = min + 1;
			mask = (1 << size) - 1;
			prev = -1;
			continue;
		}
		if (prev < 0) {
			if (code >= clear)
				return "broken LZW data";
			dst[out++] = (uint8_t)code;
			prev = first = code;
			continue;
		}
		if (code > avail)
			return "broken LZW data";
		cur = code;
		if (code == avail) {
			stack[sp++] = (uint8_t)first;
			cur = prev;
		}
		while (cur >= clear) {
			stack[sp++] = suffix[cur];
			cur = prefix[cur];
		}
		first = suffix[cur];
		stack[sp++] = (uint8_t)first;
		if (avail < 4096) {
			prefix[avail] = (uint16_t)prev;
			suffix[avail] = (uint8_t)first;
			avail++;
			if (avail == mask + 1 && size < 12) {
				size++;
				mask = (1 << size) - 1;
			}
		}
		prev = code;
		while (sp > 0 && out < want)
			dst[out++] = stack[--sp];
	}
	return NULL;
}

static int gif_u16(const struct gif *g, size_t at)
{
	return g->d[at] | (g->d[at + 1] << 8);
}

/* Skips data sub-blocks. With o set, also collects them. */
static int gif_blocks(struct gif *g, struct out *o)
{
	for (;;) {
		size_t len;

		if (g->i >= g->n)
			return 0;
		len = g->d[g->i++];
		if (!len)
			return 1;
		if (g->i + len > g->n)
			return 0;
		if (o)
			out_bytes(o, g->d + g->i, len);
		g->i += len;
	}
}

static void image_paint(uint8_t *canvas, int cw, int ch, const uint8_t *idx, int x, int y, int w, int h,
			int interlaced, const uint8_t *pal, int pal_n, int transparent)
{
	static const uint8_t start[4] = { 0, 4, 2, 1 }, step[4] = { 8, 8, 4, 2 };
	int pass, row, col;
	size_t src = 0;

	for (pass = interlaced ? 0 : 3; pass < 4; pass++) {
		int from = interlaced ? start[pass] : 0, by = interlaced ? step[pass] : 1;

		for (row = from; row < h; row += by) {
			for (col = 0; col < w; col++, src++) {
				int px = x + col, py = y + row, c = idx[src];
				uint8_t *p;

				if (c == transparent || c >= pal_n || px >= cw || py >= ch)
					continue;
				p = canvas + ((size_t)py * cw + px) * 4;
				p[0] = 255;
				p[1] = pal[c * 3];
				p[2] = pal[c * 3 + 1];
				p[3] = pal[c * 3 + 2];
			}
		}
	}
}

/* Walks the frames of a GIF. Returns an error, or NULL with the canvas size and count. */
static const char *gif_decode(const uint8_t *data, size_t size, gif_frame_fn frame, void *ctx,
			      int *out_w, int *out_h, int *out_count)
{
	struct gif g = { data, size, 0 };
	uint8_t *canvas = NULL, *saved = NULL, *idx = NULL;
	const uint8_t *global_pal = NULL;
	const char *err = NULL;
	int cw, ch, global_n = 0, flags;
	int disposal = 0, transparent = -1, delay = 0;
	int count = 0;

	if (size < 13 || (memcmp(data, "GIF87a", 6) && memcmp(data, "GIF89a", 6)))
		return "not a GIF file";
	cw = gif_u16(&g, 6);
	ch = gif_u16(&g, 8);
	flags = data[10];
	g.i = 13;
	if (cw <= 0 || ch <= 0 || cw > ANIM_SIDE_MAX || ch > ANIM_SIDE_MAX)
		return "GIF size must be 1 to 1024 pixels per side";
	if (flags & 0x80) {
		global_n = 2 << (flags & 7);
		global_pal = data + g.i;
		if (g.i + (size_t)global_n * 3 > size)
			return "truncated GIF";
		g.i += (size_t)global_n * 3;
	}

	canvas = xcalloc((size_t)cw * ch, 4);
	saved = xmalloc((size_t)cw * ch * 4);

	while (!err) {
		int block;

		if (g.i >= g.n) /* a missing trailer is common and harmless */
			break;
		block = g.d[g.i++];
		if (block == 0x3b)
			break;
		if (block == 0x21) {
			int label;

			if (g.i >= g.n) {
				err = "truncated GIF";
				break;
			}
			label = g.d[g.i++];
			if (label == 0xf9) {
				if (g.i + 6 > g.n) {
					err = "truncated GIF";
					break;
				}
				disposal = (g.d[g.i + 1] >> 2) & 7;
				delay = gif_u16(&g, g.i + 2);
				transparent = (g.d[g.i + 1] & 1) ? g.d[g.i + 4] : -1;
			}
			if (!gif_blocks(&g, NULL))
				err = "truncated GIF";
			continue;
		}
		if (block != 0x2c) {
			err = "broken GIF block";
			break;
		}

		{
			struct out codes = { 0 };
			const uint8_t *pal = global_pal;
			int pal_n = global_n, x, y, w, h, fl, min;
			size_t cells;

			if (g.i + 10 > g.n) {
				err = "truncated GIF";
				break;
			}
			x = gif_u16(&g, g.i);
			y = gif_u16(&g, g.i + 2);
			w = gif_u16(&g, g.i + 4);
			h = gif_u16(&g, g.i + 6);
			fl = g.d[g.i + 8];
			g.i += 9;
			if (fl & 0x80) {
				pal_n = 2 << (fl & 7);
				pal = g.d + g.i;
				if (g.i + (size_t)pal_n * 3 > g.n) {
					err = "truncated GIF";
					break;
				}
				g.i += (size_t)pal_n * 3;
			}
			if (!pal || g.i >= g.n) {
				err = "GIF frame has no palette";
				break;
			}
			if (count >= ANIM_FRAMES_MAX
			    || (uint64_t)cw * ch * (count + 1) > ANIM_PIXELS_MAX) {
				err = "GIF is too long; the limit is 1024 frames and 32 million pixels in total";
				break;
			}
			min = g.d[g.i++];
			if (!gif_blocks(&g, &codes)) {
				free(codes.d);
				err = "truncated GIF";
				break;
			}
			cells = (size_t)w * h;
			free(idx);
			idx = xcalloc(cells ? cells : 1, 1);
			err = lzw_decode(codes.d, codes.n, min, idx, cells);
			free(codes.d);
			if (err)
				break;

			if (disposal == 3)
				memcpy(saved, canvas, (size_t)cw * ch * 4);
			image_paint(canvas, cw, ch, idx, x, y, w, h, fl & 0x40, pal, pal_n, transparent);

			if ((err = frame(ctx, canvas, cw, ch, delay)))
				break;

			if (disposal == 2) {
				int row;
				for (row = y; row < y + h && row < ch; row++) {
					if (x < cw) {
						int span = x + w > cw ? cw - x : w;
						memset(canvas + ((size_t)row * cw + x) * 4, 0, (size_t)span * 4);
					}
				}
			} else if (disposal == 3) {
				memcpy(canvas, saved, (size_t)cw * ch * 4);
			}
			disposal = 0;
			transparent = -1;
			delay = 0;
			count++;
		}
	}

	free(canvas);
	free(saved);
	free(idx);
	if (!err && !count)
		err = "GIF has no frames";
	*out_w = cw;
	*out_h = ch;
	*out_count = count;
	return err;
}

/* The SWF that gif_to_swf builds, one frame at a time. */
struct swf_build {
	struct out o;
	int draw_w, draw_h;
	size_t frames_at;
	int count, cum_cs, shown;
};

static const char *swf_frame(void *ctx, const uint8_t *argb, int w, int h, int delay_cs)
{
	struct swf_build *b = ctx;
	int id = b->count * 2 + 1, target, hold;

	if (!b->o.d)
		b->frames_at = swf_begin(&b->o, b->draw_w ? b->draw_w : w, b->draw_h ? b->draw_h : h);
	if (!out_bitmap(&b->o, id, argb, w, h))
		return "could not compress a frame";
	out_shape(&b->o, id + 1, id, b->draw_w ? b->draw_w : w, b->draw_h ? b->draw_h : h);
	out_place(&b->o, id + 1);
	/* Browsers treat a delay under 20 ms as 100 ms. The running total
	 * decides how many 30 fps frames each one holds, so rounding adds up. */
	b->cum_cs += delay_cs >= 2 ? delay_cs : 10;
	target = (b->cum_cs * 30 + 50) / 100;
	hold = target > b->shown ? target - b->shown : 1;
	b->shown += hold;
	if (b->shown > 65000)
		return "GIF plays for too long";
	while (hold-- > 0)
		out_tag(&b->o, SWF_TAG_SHOW_FRAME, NULL, 0);
	b->count++;
	return NULL;
}

/* Returns an error, or NULL with the finished SWF in m. draw_w and draw_h are the size
 * on screen; 0 keeps the GIF's own. */
static const char *gif_to_swf(const uint8_t *data, size_t size, int draw_w, int draw_h, struct anim *m)
{
	struct swf_build b = { { 0 }, draw_w, draw_h, 0, 0, 0, 0 };
	const char *err;
	int cw, ch, count;

	/* The first frame writes the header, once the GIF's size is known. */
	err = gif_decode(data, size, swf_frame, &b, &cw, &ch, &count);
	if (err) {
		free(b.o.d);
		return err;
	}
	swf_end(&b.o, b.frames_at, b.shown);

	free(m->swf);
	m->swf = b.o.d;
	m->size = b.o.n;
	m->w = draw_w ? draw_w : cw;
	m->h = draw_h ? draw_h : ch;
	m->frames = count;
	m->ms = b.cum_cs * 10;
	return NULL;
}

/* Reads a GIF file and calls frame for each of its frames (see gif_frame_fn). */
const char *rq_gif_frames(const char *path, gif_frame_fn frame, void *ctx, int *w, int *h, int *count)
{
	const char *err;
	uint8_t *data;
	long len;
	FILE *f = rq_fopen(path, "rb");

	if (!f)
		return "not found";
	fseek(f, 0, SEEK_END);
	len = ftell(f);
	fseek(f, 0, SEEK_SET);
	if (len <= 0) {
		fclose(f);
		return "empty";
	}
	data = xmalloc((size_t)len);
	if (fread(data, 1, (size_t)len, f) != (size_t)len) {
		free(data);
		fclose(f);
		return "could not be read";
	}
	fclose(f);
	err = gif_decode(data, (size_t)len, frame, ctx, w, h, count);
	free(data);
	return err;
}

/* ---- registry ---- */

static struct anim *anim_by_name(const char *name)
{
	int i;

	for (i = 0; i < g_nanim; i++) {
		if (g_anim[i].swf && !strcasecmp(g_anim[i].name, name))
			return &g_anim[i];
	}
	return NULL;
}

/* A bare key is a file in Data/Flash/System, where the game looks for its marks and
 * effects. Slashes are checked here because Shift-JIS trail bytes can look like one. */
static void anim_full_name(const char *key, char *out, size_t n)
{
	char *sj = utf2sjis(key, 0);

	if (strchr(key, '/') || strchr(key, '\\'))
		snprintf(out, n, "%s", sj);
	else
		snprintf(out, n, "Data/Flash/System/%s.swf", sj);
	free(sj);
}

void rq_anim_full_name(const char *key, char *out, size_t n)
{
	anim_full_name(key, out, n);
}

static struct anim *anim_by_key(const char *key)
{
	char name[256];

	anim_full_name(key, name, sizeof name);
	return anim_by_name(name);
}

int rq_anim_find(const char *name, uint8_t **data, size_t *size)
{
	int handle = g_redirect ? g_redirect : rq_battle_mark_file(name);
	struct anim *m;

	/* -1: the game's own file, past any mod's replacement. */
	if (handle < 0)
		return 0;
	m = handle ? &g_anim[handle - 1] : anim_by_name(name);
	if (!m || !m->swf)
		return 0;
	if (data) {
		*data = m->swf;
		*size = m->size;
	}
	return 1;
}

void rq_anim_redirect(int handle)
{
	g_redirect = handle;
}

/* The GIF file an rq.image.add_gif key was made from, for rq.sprite. */
int rq_anim_source(const char *key, char *path, size_t n)
{
	struct anim *m = anim_by_key(key);

	if (!m || m->kind != ANIM_GIF || !m->path[0])
		return 0;
	snprintf(path, n, "%s", m->path);
	return 1;
}

/* An add_flash file by its full name (Shift-JIS, as the game asks). */
int rq_anim_added_flash(const char *name, uint8_t **data, size_t *size)
{
	struct anim *m = anim_by_name(name);

	if (!m || m->kind != ANIM_FLASH)
		return 0;
	*data = m->swf;
	*size = m->size;
	return 1;
}

/* An entry's file goes, and its bytes leave add_flash's count. */
static void anim_drop(struct anim *m)
{
	g_flash_bytes -= m->counted;
	m->counted = 0;
	free(m->swf);
	m->swf = NULL;
}

int rq_anim_handle(const char *key)
{
	struct anim *m = anim_by_key(key);

	return m ? (int)(m - g_anim) + 1 : 0;
}

static struct anim *anim_free_slot(void)
{
	int i;

	for (i = 0; i < g_nanim; i++) {
		if (!g_anim[i].swf)
			break;
	}
	if (i == ANIM_MAX)
		return NULL;
	memset(&g_anim[i], 0, sizeof g_anim[i]);
	if (i == g_nanim)
		g_nanim = i + 1;
	return &g_anim[i];
}

static void cg_name(int no, char *out, size_t n)
{
	snprintf(out, n, "rq/cg/%d", no);
}

/* The handle of a CG as a one-frame file, made the first time it is asked for. 0 if no such CG. */
int rq_anim_of_cg(int no)
{
	char name[256];
	struct anim *m;
	struct cg *cg;
	struct out o = { 0 };
	uint8_t *argb;
	size_t frames_at, i, cells;
	int w, h, ok;

	cg_name(no, name, sizeof name);
	if ((m = anim_by_name(name)))
		return (int)(m - g_anim) + 1;
	if (no <= 0 || !(cg = asset_cg_load(no)))
		return 0;
	w = cg->metrics.w;
	h = cg->metrics.h;
	if (w <= 0 || h <= 0 || w > ANIM_SIDE_MAX || h > ANIM_SIDE_MAX || !(m = anim_free_slot())) {
		cg_free(cg);
		return 0;
	}
	cells = (size_t)w * h;
	argb = xmalloc(cells * 4);
	for (i = 0; i < cells; i++) {
		const uint8_t *p = (const uint8_t *)cg->pixels + i * 4;
		argb[i * 4] = p[3];
		argb[i * 4 + 1] = p[0];
		argb[i * 4 + 2] = p[1];
		argb[i * 4 + 3] = p[2];
	}
	cg_free(cg);
	frames_at = swf_begin(&o, w, h);
	ok = out_bitmap(&o, 1, argb, w, h);
	free(argb);
	if (!ok) {
		free(o.d);
		return 0;
	}
	out_shape(&o, 2, 1, w, h);
	out_place(&o, 2);
	out_tag(&o, SWF_TAG_SHOW_FRAME, NULL, 0);
	swf_end(&o, frames_at, 1);
	m->swf = o.d;
	m->size = o.n;
	m->w = w;
	m->h = h;
	m->frames = 1;
	m->kind = ANIM_CG;
	snprintf(m->name, sizeof m->name, "%s", name);
	return (int)(m - g_anim) + 1;
}

/* A CG's picture changed, so a card showing it has to load it again. */
void rq_anim_cg_changed(int no)
{
	char name[256];
	struct anim *m;

	cg_name(no, name, sizeof name);
	if (!(m = anim_by_name(name)))
		return;
	anim_drop(m);
	rq_battle_marks_reload();
}

/* ---- Lua ---- */

int rq_lua_image_add_gif(lua_State *L)
{
	const char *key = luaL_checkstring(L, 1);
	const char *rel = luaL_checkstring(L, 2);
	char full[RQ_PATH_MAX], name[256];
	struct anim *m;
	const char *err;
	uint8_t *data;
	long len;
	FILE *f;
	int w = 0, h = 0;

	if (!key[0] || strlen(key) >= sizeof m->key)
		return luaL_error(L, "add_gif key must be 1 to 103 bytes");
	if (lua_istable(L, 3)) {
		lua_getfield(L, 3, "w");
		w = (int)luaL_optinteger(L, -1, 0);
		lua_getfield(L, 3, "h");
		h = (int)luaL_optinteger(L, -1, 0);
		lua_pop(L, 2);
		if (w < 0 || h < 0 || w > ANIM_SIDE_MAX || h > ANIM_SIDE_MAX)
			return luaL_error(L, "add_gif w and h must be 1 to 1024");
	}
	if (!rq_sandbox_resolve(L, rel, full, (int)sizeof full))
		return luaL_error(L, "GIF path '%s' is not inside the mod folder", rel);
	f = rq_fopen(full, "rb");
	if (!f)
		return luaL_error(L, "GIF '%s' not found in the mod folder", rel);
	fseek(f, 0, SEEK_END);
	len = ftell(f);
	fseek(f, 0, SEEK_SET);
	if (len <= 0) {
		fclose(f);
		return luaL_error(L, "GIF '%s' is empty", rel);
	}
	data = xmalloc((size_t)len);
	if (fread(data, 1, (size_t)len, f) != (size_t)len) {
		free(data);
		fclose(f);
		return luaL_error(L, "could not read GIF '%s'", rel);
	}
	fclose(f);

	anim_full_name(key, name, sizeof name);
	m = anim_by_name(name);
	if (!m && !(m = anim_free_slot())) {
		free(data);
		return luaL_error(L, "too many GIFs and Flash files (max %d, with the still icons on battle cards)", ANIM_MAX);
	}
	err = gif_to_swf(data, (size_t)len, w, h, m);
	free(data);
	if (err)
		return luaL_error(L, "GIF '%s': %s", rel, err);
	/* The key's file before, if add_flash made it, is gone. */
	g_flash_bytes -= m->counted;
	m->counted = 0;
	m->kind = ANIM_GIF;
	snprintf(m->name, sizeof m->name, "%s", name);
	snprintf(m->key, sizeof m->key, "%s", key);
	snprintf(m->path, sizeof m->path, "%s", full);
	rq_sound_flash_changed(name);
	lua_pushstring(L, key);
	return 1;
}

int rq_lua_image_remove_gif(lua_State *L)
{
	struct anim *m = anim_by_key(luaL_checkstring(L, 1));

	if (m) {
		anim_drop(m);
		rq_sound_flash_changed(m->name);
	}
	lua_pushboolean(L, m != NULL);
	return 1;
}

int rq_lua_image_gif_info(lua_State *L)
{
	struct anim *m = anim_by_key(luaL_checkstring(L, 1));

	if (!m)
		return 0;
	lua_newtable(L);
	lua_pushinteger(L, m->w);
	lua_setfield(L, -2, "w");
	lua_pushinteger(L, m->h);
	lua_setfield(L, -2, "h");
	lua_pushinteger(L, m->frames);
	lua_setfield(L, -2, "frames");
	lua_pushinteger(L, m->ms);
	lua_setfield(L, -2, "ms");
	return 1;
}

/* ---- a mod's Flash files (2.63.0) ---- */

/* The checker's first error, where it is, and how many more: "frame 4, tag 23: ...". */
static void first_problem(const struct swfc *c, int level, char *out, size_t n)
{
	const struct swfc_item *it = NULL;
	char where[64] = "";
	int i, k = 0, total = level == SWFC_ERROR ? c->errors : c->warnings;

	for (i = 0; i < c->nitems && !it; i++)
		if (c->items[i].level == level)
			it = &c->items[i];
	if (!it) {
		snprintf(out, n, "it cannot be read");
		return;
	}
	if (it->frame)
		k += snprintf(where + k, sizeof where - k, "frame %d", it->frame);
	if (it->tag)
		snprintf(where + k, sizeof where - k, "%stag %d", k ? ", " : "", it->tag);
	if (total > 1)
		snprintf(out, n, "%s%s%s (and %d more: rqmod-aff check lists them)", where,
			 where[0] ? ": " : "", it->text, total - 1);
	else
		snprintf(out, n, "%s%s%s", where, where[0] ? ": " : "", it->text);
}

/* The stage of the game's own file under a name, from its archive; 0 when it has none. */
static int game_flash_size(const char *name, int *w, int *h)
{
	struct archive_data *d = asset_get_by_name(ASSET_FLASH, name, NULL);
	struct swf *swf;

	if (!d)
		return 0;
	swf = aff_load(d->data, d->size);
	archive_free_data(d);
	if (!swf)
		return 0;
	*w = swf->frame_size.x_max / 20;
	*h = swf->frame_size.y_max / 20;
	swf_free(swf);
	return 1;
}

int rq_lua_image_add_flash(lua_State *L)
{
	const char *key = luaL_checkstring(L, 1);
	const char *rel = luaL_checkstring(L, 2);
	char full[RQ_PATH_MAX], name[256], why[600];
	struct swfc c;
	struct anim *m;
	uint8_t *data;
	size_t before;
	long len;
	FILE *f;
	int status;

	if (!key[0] || strlen(key) >= sizeof m->key)
		return luaL_error(L, "add_flash key must be 1 to 103 bytes");
	if (!rq_sandbox_resolve(L, rel, full, (int)sizeof full))
		return luaL_error(L, "Flash path '%s' is not inside the mod folder", rel);
	f = rq_fopen(full, "rb");
	if (!f)
		return luaL_error(L, "Flash file '%s' not found in the mod folder", rel);
	fseek(f, 0, SEEK_END);
	len = ftell(f);
	fseek(f, 0, SEEK_SET);
	if (len <= 0 || (unsigned long)len > ANIM_FLASH_READ) {
		fclose(f);
		return luaL_error(L, len <= 0 ? "Flash file '%s' is empty" : "Flash file '%s' is over 64 MB", rel);
	}
	data = xmalloc((size_t)len);
	if (fread(data, 1, (size_t)len, f) != (size_t)len) {
		free(data);
		fclose(f);
		return luaL_error(L, "could not read Flash file '%s'", rel);
	}
	fclose(f);

	/* The engine's rules first: a file they refuse never reaches the game. */
	memset(&c, 0, sizeof c);
	status = swfc_check(data, (size_t)len, &c);
	free(data);
	if (status != 0) {
		first_problem(&c, SWFC_ERROR, why, sizeof why);
		swfc_free(&c);
		return luaL_error(L, "Flash file '%s' %s: %s", rel,
				  status == 3 ? "cannot be read" : "is not played by Rance Quest", why);
	}
	if (c.fws_size > ANIM_FLASH_FILE || c.pixels > ANIM_PIXELS_MAX) {
		unsigned mb = (unsigned)(c.fws_size >> 20);
		unsigned mp = (unsigned)(c.pixels >> 20);
		swfc_free(&c);
		return luaL_error(L, "Flash file '%s' is %u MB uncompressed with %u million pixels in its "
				  "pictures: at most 64 MB and 32 million", rel, mb, mp);
	}
	anim_full_name(key, name, sizeof name);
	m = anim_by_name(name);
	before = m ? m->counted : 0;
	if (g_flash_bytes - before + c.fws_size > ANIM_FLASH_BYTES) {
		swfc_free(&c);
		return luaL_error(L, "Flash file '%s': the files added with add_flash would pass 256 MB "
				  "uncompressed (rq.image.remove_flash frees one)", rel);
	}
	if (!m && !(m = anim_free_slot())) {
		swfc_free(&c);
		return luaL_error(L, "too many GIFs and Flash files (max %d, with the still icons on battle cards)", ANIM_MAX);
	}
	anim_drop(m);
	m->swf = c.fws;
	m->size = c.fws_size;
	c.fws = NULL;
	m->w = c.width;
	m->h = c.height;
	m->frames = c.frames;
	m->ms = (int)(((int64_t)c.frames * 1000 + 15) / 30);
	m->kind = ANIM_FLASH;
	m->counted = m->size;
	g_flash_bytes += m->size;
	snprintf(m->name, sizeof m->name, "%s", name);
	snprintf(m->key, sizeof m->key, "%s", key);
	snprintf(m->path, sizeof m->path, "%s", full);
	rq_sound_flash_changed(name);
	rq_log("image: add_flash %s: %s, %dx%d, %d frames (%.2f s), %d bitmap%s, %d sound%s",
	       key, rel, m->w, m->h, m->frames, swfc_seconds(&c), c.bitmaps, c.bitmaps == 1 ? "" : "s",
	       c.sounds, c.sounds == 1 ? "" : "s");
	if (c.warnings) {
		first_problem(&c, SWFC_WARNING, why, sizeof why);
		rq_log("image: add_flash %s plays, with %s: %s", key,
		       c.warnings == 1 ? "a warning" : "warnings", why);
	}
	{
		/* The game places a file of its own by that file's size: a battle effect's stage is
		 * placed over its target. A replacement of another size is drawn off its place. */
		int gw, gh;
		if (game_flash_size(name, &gw, &gh) && (gw != m->w || gh != m->h))
			rq_log("image: add_flash %s is %d x %d, the game's file %d x %d: the game places it as "
			       "its own, so its picture is not where the game's was (rqmod-aff build --size "
			       "%dx%d puts the frames in the middle of that stage)", key, m->w, m->h, gw, gh, gw, gh);
	}
	swfc_free(&c);
	lua_pushstring(L, key);
	return 1;
}

int rq_lua_image_remove_flash(lua_State *L)
{
	struct anim *m = anim_by_key(luaL_checkstring(L, 1));

	if (m) {
		anim_drop(m);
		rq_sound_flash_changed(m->name);
	}
	lua_pushboolean(L, m != NULL);
	return 1;
}

/* What the game plays under a name: a mod's file (add_flash, add_gif, a mark) or its own. */
int rq_lua_image_flash_info(lua_State *L)
{
	const char *key = luaL_checkstring(L, 1);
	char name[256];
	struct swf *swf;
	struct swf_tag *t;
	uint64_t pixels = 0;
	int from_mod, bitmaps = 0, sounds = 0, starts = 0;

	if (strlen(key) > 200)
		return luaL_error(L, "flash file name is too long");
	anim_full_name(key, name, sizeof name);
	if (!(swf = rq_flash_read(name, &from_mod)))
		return 0;
	for (t = swf->tags; t; t = t->next) {
		if (t->type == TAG_DEFINE_BITS_LOSSLESS || t->type == TAG_DEFINE_BITS_LOSSLESS2) {
			struct swf_tag_define_bits_lossless *b = (struct swf_tag_define_bits_lossless *)t;
			bitmaps++;
			pixels += (uint64_t)b->width * b->height;
		} else if (t->type == TAG_DEFINE_SOUND) {
			sounds++;
		} else if (t->type == TAG_START_SOUND) {
			starts++;
		}
	}
	lua_newtable(L);
	lua_pushinteger(L, swf->frame_size.x_max / 20);
	lua_setfield(L, -2, "w");
	lua_pushinteger(L, swf->frame_size.y_max / 20);
	lua_setfield(L, -2, "h");
	lua_pushinteger(L, swf->frame_count);
	lua_setfield(L, -2, "frames");
	lua_pushinteger(L, ((int64_t)swf->frame_count * 1000 + 15) / 30);
	lua_setfield(L, -2, "ms");
	lua_pushinteger(L, bitmaps);
	lua_setfield(L, -2, "bitmaps");
	lua_pushinteger(L, (lua_Integer)pixels);
	lua_setfield(L, -2, "pixels");
	lua_pushinteger(L, sounds);
	lua_setfield(L, -2, "sounds");
	lua_pushinteger(L, starts);
	lua_setfield(L, -2, "starts");
	lua_pushboolean(L, from_mod);
	lua_setfield(L, -2, "mod");
	swf_free(swf);
	return 1;
}

#endif /* RQ_MODLOADER */
