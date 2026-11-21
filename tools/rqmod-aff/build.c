/* Copyright (C) 2026 Rance Quest modding fork
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://gnu.org/licenses/>.
 */

/*
 * rqmod-aff build: PNG frames, and WAV sounds, to an animation the game plays.
 *
 * It writes only what the engine plays. Each new picture is a 32-bit bitmap
 * (DefineBitsLossless2) cut to its visible pixels, and one rectangle shape filled with it at
 * its place; one object at depth 1 shows each frame's shape. A frame equal to the one before
 * is held (another ShowFrame), one equal to an earlier one shows that one's shape again. The
 * header says 30 frames a second, as the game plays its files; --fps holds each picture for
 * whole frames of those. A sound is uncompressed PCM (DefineSound) started with its picture.
 * The pictures' colours are stored multiplied by their alpha, as the SWF format says, Rance
 * Quest's own files hold them and the engine reads them back since 2.63.0.
 */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <png.h>
#include <zlib.h>

#include "system4.h"
#include "system4/file.h"

#include "aff.h"

#define SIDE_MAX 4096
#define PICTURES_MAX 10000
#define SOUNDS_MAX 64
#define SHOWN_MAX 65535
#define GAME_FPS 30

/* ---- output ---- */

struct out {
	uint8_t *d;
	size_t n, cap;
	int oom;
};

static void out_bytes(struct out *o, const void *p, size_t n)
{
	if (o->oom)
		return;
	if (o->n + n > o->cap) {
		size_t cap = (o->n + n) * 2 + 4096;
		uint8_t *d = realloc(o->d, cap);
		if (!d) {
			o->oom = 1;
			return;
		}
		o->d = d;
		o->cap = cap;
	}
	if (p)
		memcpy(o->d + o->n, p, n);
	else
		memset(o->d + o->n, 0, n);
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

/* The bits a signed field needs, with its sign; never under 2. */
static int sbits(int32_t v)
{
	uint32_t u = v < 0 ? ~(uint32_t)v : (uint32_t)v;
	int n = 1;

	while (u) {
		n++;
		u >>= 1;
	}
	return n < 2 ? 2 : n;
}

static int max4(int a, int b, int c, int d)
{
	int m = a > b ? a : b;
	m = m > c ? m : c;
	return m > d ? m : d;
}

static void out_rect(struct out *o, int32_t x0, int32_t x1, int32_t y0, int32_t y1)
{
	struct bits b = { o, 0, 0 };
	int n = max4(sbits(x0), sbits(x1), sbits(y0), sbits(y1));

	bits_put(&b, (uint32_t)n, 5);
	bits_put(&b, (uint32_t)x0, n);
	bits_put(&b, (uint32_t)x1, n);
	bits_put(&b, (uint32_t)y0, n);
	bits_put(&b, (uint32_t)y1, n);
	bits_end(&b);
}

/* A bitmap fill's matrix: one pixel of the picture to one pixel on screen (20 twips), its
 * top left at (tx, ty) twips. */
static void out_fill_matrix(struct out *o, int32_t tx, int32_t ty)
{
	struct bits b = { o, 0, 0 };
	int32_t scale = 20 << 16;
	int ns = sbits(scale), nt = sbits(tx) > sbits(ty) ? sbits(tx) : sbits(ty);

	bits_put(&b, 1, 1);
	bits_put(&b, (uint32_t)ns, 5);
	bits_put(&b, (uint32_t)scale, ns);
	bits_put(&b, (uint32_t)scale, ns);
	bits_put(&b, 0, 1);
	bits_put(&b, (uint32_t)nt, 5);
	bits_put(&b, (uint32_t)tx, nt);
	bits_put(&b, (uint32_t)ty, nt);
	bits_end(&b);
}

static void out_tag(struct out *o, int code, const void *body, size_t len, int long_form)
{
	if (len < 0x3f && !long_form) {
		out_u16(o, (unsigned)(code << 6) | (unsigned)len);
	} else {
		out_u16(o, (unsigned)(code << 6) | 0x3f);
		out_u32(o, (uint32_t)len);
	}
	out_bytes(o, body, len);
}

static void out_show_frame(struct out *o)
{
	out_tag(o, 1, NULL, 0, 0);
}

/* A shape: one rectangle (x, y, w, h in pixels) filled with the bitmap. */
static void out_shape(struct out *o, int id, int bitmap, int x, int y, int w, int h)
{
	struct out s = { 0 };
	struct bits b = { &s, 0, 0 };
	int32_t x0 = x * 20, y0 = y * 20, dx = w * 20, dy = h * 20;
	int nm = sbits(x0) > sbits(y0) ? sbits(x0) : sbits(y0);
	int ne = sbits(dx) > sbits(dy) ? sbits(dx) : sbits(dy);
	int i;

	out_u16(&s, (unsigned)id);
	out_rect(&s, x0, x0 + dx, y0, y0 + dy);
	out_u8(&s, 1);              /* one fill */
	out_u8(&s, 0x41);           /* a clipped bitmap */
	out_u16(&s, (unsigned)bitmap);
	out_fill_matrix(&s, x0, y0);
	out_u8(&s, 0);              /* no line */
	out_u8(&s, 1 << 4);         /* one bit for a fill's number, none for a line's */
	/* Move to the top left with fill 1 (on the right of the edges, which go clockwise). */
	bits_put(&b, 0x05, 6);
	bits_put(&b, (uint32_t)nm, 5);
	bits_put(&b, (uint32_t)x0, nm);
	bits_put(&b, (uint32_t)y0, nm);
	bits_put(&b, 1, 1);
	for (i = 0; i < 4; i++) {
		int32_t delta = i == 0 ? dx : i == 1 ? dy : i == 2 ? -dx : -dy;
		bits_put(&b, 3, 2);                 /* a straight edge */
		bits_put(&b, (uint32_t)(ne - 2), 4);
		bits_put(&b, 0, 1);                 /* along one axis */
		bits_put(&b, (uint32_t)(i & 1), 1); /* vertical on the odd ones */
		bits_put(&b, (uint32_t)delta, ne);
	}
	bits_put(&b, 0, 6);                     /* the end */
	bits_end(&b);
	out_tag(o, 2, s.d, s.n, 0);
	if (s.oom)
		o->oom = 1;
	free(s.d);
}

/* A 32-bit picture, A R G B per pixel as the SWF holds it. */
static int out_bitmap(struct out *o, int id, const uint8_t *argb, int w, int h)
{
	size_t raw = (size_t)w * h * 4;
	uLongf clen = compressBound((uLong)raw);
	uint8_t *s = malloc(7 + clen);

	if (!s)
		return 0;
	s[0] = id & 0xff;
	s[1] = (id >> 8) & 0xff;
	s[2] = 5;
	s[3] = w & 0xff;
	s[4] = (w >> 8) & 0xff;
	s[5] = h & 0xff;
	s[6] = (h >> 8) & 0xff;
	if (compress2(s + 7, &clen, argb, (uLong)raw, 9) != Z_OK) {
		free(s);
		return 0;
	}
	out_tag(o, 36, s, 7 + clen, 1);
	free(s);
	return 1;
}

static void out_place(struct out *o, int shape, int replace)
{
	uint8_t p[5] = { (uint8_t)(replace ? 0x03 : 0x02), 1, 0, (uint8_t)(shape & 0xff), (uint8_t)(shape >> 8) };
	out_tag(o, 26, p, sizeof p, 0);
}

static void out_remove(struct out *o)
{
	uint8_t p[2] = { 1, 0 };
	out_tag(o, 28, p, sizeof p, 0);
}

static void out_sound(struct out *o, int id, const struct pcm *s)
{
	int rate = s->rate == 5512 ? 0 : s->rate == 11025 ? 1 : s->rate == 22050 ? 2 : 3;
	uint8_t *body = malloc(7 + s->bytes);

	if (!body) {
		o->oom = 1;
		return;
	}
	body[0] = id & 0xff;
	body[1] = (id >> 8) & 0xff;
	body[2] = (uint8_t)(3 << 4 | rate << 2 | (s->bits == 16) << 1 | (s->chans == 2));
	body[3] = s->samples & 0xff;
	body[4] = (s->samples >> 8) & 0xff;
	body[5] = (s->samples >> 16) & 0xff;
	body[6] = (s->samples >> 24) & 0xff;
	memcpy(body + 7, s->data, s->bytes);
	out_tag(o, 14, body, 7 + s->bytes, 1);
	free(body);
}

static void out_start_sound(struct out *o, int id)
{
	uint8_t p[3] = { (uint8_t)(id & 0xff), (uint8_t)(id >> 8), 0 };
	out_tag(o, 15, p, sizeof p, 0);
}

/* ---- the frames ---- */

static int nat_cmp(const char *a, const char *b)
{
	while (*a && *b) {
		if (isdigit((unsigned char)*a) && isdigit((unsigned char)*b)) {
			const char *ea, *eb;
			size_t la, lb;
			int d;
			while (*a == '0' && isdigit((unsigned char)a[1]))
				a++;
			while (*b == '0' && isdigit((unsigned char)b[1]))
				b++;
			for (ea = a; isdigit((unsigned char)*ea); ea++)
				;
			for (eb = b; isdigit((unsigned char)*eb); eb++)
				;
			la = (size_t)(ea - a);
			lb = (size_t)(eb - b);
			if (la != lb)
				return la < lb ? -1 : 1;
			if ((d = strncmp(a, b, la)))
				return d;
			a = ea;
			b = eb;
			continue;
		}
		if (tolower((unsigned char)*a) != tolower((unsigned char)*b))
			return tolower((unsigned char)*a) - tolower((unsigned char)*b);
		a++;
		b++;
	}
	return (unsigned char)*a - (unsigned char)*b;
}

static int cmp_names(const void *x, const void *y)
{
	return nat_cmp(*(char *const *)x, *(char *const *)y);
}

static int glob_match(const char *p, const char *s)
{
	if (!*p)
		return !*s;
	if (*p == '*') {
		for (;; s++) {
			if (glob_match(p + 1, s))
				return 1;
			if (!*s)
				return 0;
		}
	}
	if (!*s)
		return 0;
	if (*p != '?' && tolower((unsigned char)*p) != tolower((unsigned char)*s))
		return 0;
	return glob_match(p + 1, s + 1);
}

static int is_png_name(const char *n)
{
	size_t l = strlen(n);
	return l > 4 && !strcasecmp(n + l - 4, ".png");
}

/* The PNG files of a folder, or those a pattern names, in name order (numbers by value). */
static char **list_frames(const char *what, int *count)
{
	char *dir, *pattern = NULL, *name, **list = NULL;
	int n = 0, cap = 0;
	UDIR *d;

	if (is_directory(what)) {
		dir = strdup(what);
	} else if (strchr(what, '*') || strchr(what, '?')) {
		const char *slash = strrchr(what, '/'), *bs = strrchr(what, '\\'), *cut = slash;
		if (bs && (!cut || bs > cut))
			cut = bs;
		if (cut) {
			size_t len = (size_t)(cut - what);
			dir = malloc(len + 1);
			memcpy(dir, what, len);
			dir[len] = 0;
		} else {
			dir = strdup(".");
		}
		pattern = strdup(cut ? cut + 1 : what);
	} else {
		return NULL;
	}
	d = opendir_utf8(dir);
	if (!d) {
		free(dir);
		free(pattern);
		return NULL;
	}
	while ((name = readdir_utf8(d))) {
		if (is_png_name(name) && (!pattern || glob_match(pattern, name))) {
			if (n == cap) {
				char **g = realloc(list, (size_t)(cap = cap ? cap * 2 : 64) * sizeof *g);
				if (!g)
					break;
				list = g;
			}
			list[n++] = path_join(dir, name);
		}
		free(name);
	}
	closedir_utf8(d);
	free(dir);
	free(pattern);
	if (n)
		qsort(list, (size_t)n, sizeof *list, cmp_names);
	*count = n;
	return list;
}

/* A PNG of any kind (palette, grey, 16 bits) as 8-bit RGBA, straight alpha. */
static uint8_t *load_png(const char *path, int *w, int *h, char *err, size_t errn)
{
	size_t n = 0;
	uint8_t *data = file_read(path, &n), *px;
	png_image img;

	if (!data) {
		snprintf(err, errn, "cannot read %s", path);
		return NULL;
	}
	memset(&img, 0, sizeof img);
	img.version = PNG_IMAGE_VERSION;
	if (!png_image_begin_read_from_memory(&img, data, n)) {
		snprintf(err, errn, "%s is not a PNG file the tool reads (%s)", path, img.message);
		free(data);
		return NULL;
	}
	img.format = PNG_FORMAT_RGBA;
	if (img.width > SIDE_MAX || img.height > SIDE_MAX || !img.width || !img.height) {
		snprintf(err, errn, "%s is %u x %u pixels: a frame may be 1 to %d pixels a side", path,
			 img.width, img.height, SIDE_MAX);
		png_image_free(&img);
		free(data);
		return NULL;
	}
	px = malloc(PNG_IMAGE_SIZE(img));
	if (!px || !png_image_finish_read(&img, NULL, px, 0, NULL)) {
		snprintf(err, errn, "%s could not be read (%s)", path, img.message);
		free(px);
		png_image_free(&img);
		free(data);
		return NULL;
	}
	*w = (int)img.width;
	*h = (int)img.height;
	png_image_free(&img);
	free(data);
	return px;
}

/* A picture seen before: its shape, by a hash of its place and pixels. */
struct seen {
	uint64_t hash;
	int x, y, w, h;
	int shape;
};

static uint64_t fnv(const uint8_t *p, size_t n, uint64_t h)
{
	size_t i;

	for (i = 0; i < n; i++) {
		h ^= p[i];
		h *= 1099511628211ull;
	}
	return h;
}

struct sound_arg {
	const char *path;
	int at; /* the picture it starts with, from 1 */
	struct pcm pcm;
};

static int parse_size(const char *s, int *w, int *h)
{
	char *end;
	long a = strtol(s, &end, 10), b;

	if (*end != 'x' && *end != 'X')
		return 0;
	b = strtol(end + 1, &end, 10);
	if (*end || a < 1 || b < 1 || a > SIDE_MAX || b > SIDE_MAX)
		return 0;
	*w = (int)a;
	*h = (int)b;
	return 1;
}

int cmd_build(int argc, char **argv)
{
	const char *frames_arg = NULL, *out_path = NULL;
	struct sound_arg sounds[SOUNDS_MAX];
	int nsounds = 0, fps = GAME_FPS, stage_w = 0, stage_h = 0, sized = 0;
	char **paths = NULL, err[600];
	int npaths = 0, i, status = 1;
	struct out o = { 0 };
	struct seen *seen = NULL;
	int nseen = 0, next_id = 1, placed = 0, shown = 0, stored = 0, held = 0, reused = 0;
	uint8_t *prev = NULL;
	int prev_x = 0, prev_y = 0, prev_w = 0, prev_h = 0, prev_empty = 1, prev_shape = 0;
	size_t frames_at;
	int is_aff;
	const char *ext;

	memset(sounds, 0, sizeof sounds);
	for (i = 0; i < argc; i++) {
		if (!strcmp(argv[i], "--fps") && i + 1 < argc) {
			char *end;
			long v = strtol(argv[++i], &end, 10);
			if (*end || v < 1 || v > GAME_FPS) {
				fprintf(stderr, "rqmod-aff: --fps is 1 to 30 (the game plays 30 frames a second)\n");
				return 2;
			}
			fps = (int)v;
		} else if (!strcmp(argv[i], "--sound") && i + 1 < argc) {
			char *arg = argv[++i], *at = strrchr(arg, '@');
			if (nsounds == SOUNDS_MAX) {
				fprintf(stderr, "rqmod-aff: at most %d sounds\n", SOUNDS_MAX);
				return 2;
			}
			sounds[nsounds].at = 1;
			if (at && at[1] && strspn(at + 1, "0123456789") == strlen(at + 1)) {
				sounds[nsounds].at = atoi(at + 1);
				*at = 0;
			}
			if (sounds[nsounds].at < 1) {
				fprintf(stderr, "rqmod-aff: a sound starts with a picture from 1 (FILE.wav@1)\n");
				return 2;
			}
			sounds[nsounds++].path = arg;
		} else if (!strcmp(argv[i], "--size") && i + 1 < argc) {
			if (!parse_size(argv[++i], &stage_w, &stage_h)) {
				fprintf(stderr, "rqmod-aff: --size is WIDTHxHEIGHT, 1 to %d pixels a side\n", SIDE_MAX);
				return 2;
			}
			sized = 1;
		} else if (argv[i][0] == '-' && argv[i][1] == '-') {
			fprintf(stderr, "rqmod-aff: build has no option %s\n", argv[i]);
			return 2;
		} else if (!frames_arg) {
			frames_arg = argv[i];
		} else if (!out_path) {
			out_path = argv[i];
		} else {
			fprintf(stderr, "rqmod-aff: build takes FRAMES and OUT\n");
			return 2;
		}
	}
	if (!frames_arg || !out_path) {
		fprintf(stderr, "rqmod-aff build FRAMES OUT.aff [--fps N] [--sound FILE.wav[@FRAME]]... [--size WxH]\n");
		return 2;
	}
	ext = strrchr(out_path, '.');
	if (!ext || (strcasecmp(ext, ".aff") && strcasecmp(ext, ".swf"))) {
		fprintf(stderr, "rqmod-aff: OUT ends in .aff (the game's form) or .swf (for a viewer)\n");
		return 2;
	}
	is_aff = !strcasecmp(ext, ".aff");

	paths = list_frames(frames_arg, &npaths);
	if (!paths || !npaths) {
		fprintf(stderr, "rqmod-aff: no PNG frames in %s (a folder of PNG files, or a pattern such as walk_*.png)\n",
			frames_arg);
		free(paths);
		return 1;
	}
	if (npaths > PICTURES_MAX) {
		fprintf(stderr, "rqmod-aff: %d frames; at most %d\n", npaths, PICTURES_MAX);
		goto done;
	}
	for (i = 0; i < nsounds; i++) {
		if (sounds[i].at > npaths) {
			fprintf(stderr, "rqmod-aff: %s starts with picture %d, but there are %d\n", sounds[i].path,
				sounds[i].at, npaths);
			goto done;
		}
		if (!wav_load(sounds[i].path, &sounds[i].pcm, err, sizeof err)) {
			fprintf(stderr, "rqmod-aff: %s\n", err);
			goto done;
		}
		if (sounds[i].pcm.note[0])
			printf("%s: %s\n", sounds[i].path, sounds[i].pcm.note);
	}

	/* The header; the size and the frame count are written once known. */
	out_bytes(&o, "FWS\x08", 4);
	out_u32(&o, 0);
	frames_at = 0;

	{
		size_t rect_at = o.n;
		int target = 0;
		for (i = 0; i < npaths; i++) {
			int w, h, x0, y0, x1, y1, x, y, cw, ch, empty, same, shape = 0, hold, k;
			uint8_t *rgba = load_png(paths[i], &w, &h, err, sizeof err), *argb = NULL;

			if (!rgba) {
				fprintf(stderr, "rqmod-aff: %s\n", err);
				goto done;
			}
			if (i == 0) {
				if (!sized) {
					stage_w = w;
					stage_h = h;
				}
				/* The stage, the rate and the frame count, now that the size is known. */
				o.n = rect_at;
				out_rect(&o, 0, stage_w * 20, 0, stage_h * 20);
				out_u16(&o, GAME_FPS << 8);
				frames_at = o.n;
				out_u16(&o, 0);
				{
					uint8_t attrs[4] = { 0, 0, 0, 0 };
					out_tag(&o, 69, attrs, sizeof attrs, 0);
				}
			}
			if (sized ? (w > stage_w || h > stage_h) : (w != stage_w || h != stage_h)) {
				fprintf(stderr, "rqmod-aff: %s is %d x %d, %s %d x %d%s\n", paths[i], w, h,
					sized ? "larger than the stage of" : "not the first frame's", stage_w, stage_h,
					sized ? "" : " (every frame the same size, or give --size)");
				free(rgba);
				goto done;
			}
			/* The visible pixels' box. */
			x0 = w;
			y0 = h;
			x1 = -1;
			y1 = -1;
			for (y = 0; y < h; y++) {
				for (x = 0; x < w; x++) {
					if (rgba[((size_t)y * w + x) * 4 + 3]) {
						if (x < x0) x0 = x;
						if (x > x1) x1 = x;
						if (y < y0) y0 = y;
						if (y > y1) y1 = y;
					}
				}
			}
			empty = x1 < 0;
			cw = empty ? 0 : x1 - x0 + 1;
			ch = empty ? 0 : y1 - y0 + 1;
			if (!empty) {
				argb = malloc((size_t)cw * ch * 4);
				if (!argb) {
					free(rgba);
					fprintf(stderr, "rqmod-aff: out of memory\n");
					goto done;
				}
				for (y = 0; y < ch; y++) {
					for (x = 0; x < cw; x++) {
						const uint8_t *s = rgba + ((size_t)(y0 + y) * w + (x0 + x)) * 4;
						uint8_t *d = argb + ((size_t)y * cw + x) * 4;
						/* Colours multiplied by alpha, as the SWF format holds them;
						 * a clear pixel is all zero. */
						unsigned a = s[3];
						d[0] = (uint8_t)a;
						for (k = 0; k < 3; k++)
							d[1 + k] = a == 255 ? s[k] : (uint8_t)((s[k] * a + 127) / 255);
					}
				}
			}
			free(rgba);

			same = i > 0 && empty == prev_empty && (empty || (x0 == prev_x && y0 == prev_y && cw == prev_w
				&& ch == prev_h && !memcmp(argb, prev, (size_t)cw * ch * 4)));
			if (same) {
				held++;
				shape = prev_shape;
			} else if (empty) {
				if (placed)
					out_remove(&o);
				placed = 0;
			} else {
				uint64_t hash = fnv(argb, (size_t)cw * ch * 4, 14695981039346656037ull);
				hash = fnv((const uint8_t *)&x0, sizeof x0, fnv((const uint8_t *)&y0, sizeof y0, hash));
				for (k = 0; k < nseen; k++) {
					if (seen[k].hash == hash && seen[k].x == x0 && seen[k].y == y0 && seen[k].w == cw
					    && seen[k].h == ch) {
						shape = seen[k].shape;
						reused++;
						break;
					}
				}
				if (!shape) {
					struct seen *g;
					if (next_id + 2 > 65535) {
						fprintf(stderr, "rqmod-aff: too many different pictures for one file\n");
						free(argb);
						goto done;
					}
					if (!out_bitmap(&o, next_id, argb, cw, ch)) {
						fprintf(stderr, "rqmod-aff: could not compress %s\n", paths[i]);
						free(argb);
						goto done;
					}
					out_shape(&o, next_id + 1, next_id, x0, y0, cw, ch);
					shape = next_id + 1;
					next_id += 2;
					stored++;
					g = realloc(seen, (size_t)(nseen + 1) * sizeof *g);
					if (g) {
						seen = g;
						seen[nseen].hash = hash;
						seen[nseen].x = x0;
						seen[nseen].y = y0;
						seen[nseen].w = cw;
						seen[nseen].h = ch;
						seen[nseen].shape = shape;
						nseen++;
					}
				}
				out_place(&o, shape, placed);
				placed = 1;
			}
			/* The sounds that start with this picture. */
			for (k = 0; k < nsounds; k++) {
				if (sounds[k].at == i + 1) {
					int id = next_id++;
					out_sound(&o, id, &sounds[k].pcm);
					out_start_sound(&o, id);
				}
			}
			/* How many of the game's frames this picture holds: the running total decides, so the
			 * rounding does not add up. */
			target = (int)(((long long)(i + 1) * GAME_FPS + fps / 2) / fps);
			hold = target - shown;
			if (hold < 1)
				hold = 1;
			if (shown + hold > SHOWN_MAX) {
				fprintf(stderr, "rqmod-aff: the animation is longer than %d frames\n", SHOWN_MAX);
				free(argb);
				goto done;
			}
			for (k = 0; k < hold; k++)
				out_show_frame(&o);
			shown += hold;

			free(prev);
			prev = argb;
			prev_x = x0;
			prev_y = y0;
			prev_w = cw;
			prev_h = ch;
			prev_empty = empty;
			prev_shape = shape;
		}
	}
	out_tag(&o, 0, NULL, 0, 0);
	if (o.oom) {
		fprintf(stderr, "rqmod-aff: out of memory\n");
		goto done;
	}
	o.d[frames_at] = shown & 0xff;
	o.d[frames_at + 1] = (shown >> 8) & 0xff;
	o.d[4] = o.n & 0xff;
	o.d[5] = (o.n >> 8) & 0xff;
	o.d[6] = (o.n >> 16) & 0xff;
	o.d[7] = (o.n >> 24) & 0xff;

	{
		struct swfc c;
		int check = swfc_check(o.d, o.n, &c);
		size_t fn = 0;
		uint8_t *file = is_aff ? aff_wrap(o.d, o.n, &fn) : swf_compress(o.d, o.n, &fn);

		printf("%d pictures, %d stored (%d held, %d shown again), %d frames at the game's 30 a second "
		       "(%.2f s), %d sound%s\n", npaths, stored, held, reused, shown, shown / 30.0, nsounds,
		       nsounds == 1 ? "" : "s");
		if (check) {
			report(out_path, &c, check, 0);
			fprintf(stderr, "rqmod-aff: the file built does not pass the check: nothing written\n");
		} else if (!file || !write_file(out_path, file, fn)) {
			fprintf(stderr, "rqmod-aff: cannot write %s\n", out_path);
		} else {
			report(out_path, &c, check, 0);
			printf("wrote %s (%zu bytes)\n", out_path, fn);
			status = 0;
		}
		free(file);
		swfc_free(&c);
	}

done:
	for (i = 0; i < nsounds; i++)
		pcm_free(&sounds[i].pcm);
	for (i = 0; i < npaths; i++)
		free(paths[i]);
	free(paths);
	free(seen);
	free(prev);
	free(o.d);
	return status;
}
