/* Text in the game's own font, for rq.hud.text, rq.hud.text_width and
 * rq.image.compose (loader 2.5.0).
 *
 * The English game does not draw its labels with the font alone: its
 * translators place every letter with kerning tables (New/DrawTextCode.jaf,
 * DrawTextNew). So the engine asks the game to draw the line, as its buttons
 * do (T_DrawLine::createFromTextMetrics2), copies the sprite it makes, and
 * deletes the sprite with SP_Delete, as T_Sprite::delete does. The copy is
 * kept in a small cache, so a label drawn on every update is made once.
 *
 * DrawTextNew sets the game's font globals (currentFont, currentFontSize, the
 * spacing); they are put back after each call through the game's own setters.
 *
 * Runs on the VM thread only: from Lua callbacks (rq.on_frame, Config pages)
 * and from rq.image.compose. */

#ifdef RQ_MODLOADER

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "system4.h"
#include "system4/ain.h"
#include "system4/string.h"
#include "system4/utfsjis.h"

#include "gfx/gfx.h"
#include "modloader.h"
#include "modloader_priv.h"
#include "sact.h"
#include "sprite.h"
#include "vm.h"
#include "vm/heap.h"
#include "vm/page.h"

#define GT_MAX 512

struct gt_entry {
	uint32_t hash;
	char *key;
	Texture t;
	unsigned used;
};

static struct gt_entry g_gt[GT_MAX];
static int g_ngt;
static unsigned g_gen = 1;  /* raised after each HUD draw: entries used since are kept */

static int g_fno_draw = -2, g_fno_select, g_fno_size, g_fno_spacing, g_fno_fixed;
static int g_gi_font, g_gi_size, g_gi_spacing, g_gi_fixed;

static int fn(const char *name)
{
	return ain_get_function(ain, (char *)name);
}

static int ready(void)
{
	if (!ain || !heap || !heap_size || !heap[0].page)
		return 0;
	if (g_fno_draw == -2) {
		g_fno_draw = fn("DrawTextNew");
		g_fno_select = fn("SelectFontNumber");
		g_fno_size = fn("SetFontSize");
		g_fno_spacing = fn("SetSpacing");
		g_fno_fixed = fn("SetFixedWidthSpacing");
		g_gi_font = ain_get_global(ain, "currentFont");
		g_gi_size = ain_get_global(ain, "currentFontSize");
		g_gi_spacing = ain_get_global(ain, "currentSpacing");
		g_gi_fixed = ain_get_global(ain, "currentFixedWidthSpacing");
		if (g_fno_draw < 0)
			rq_log("gametext: the game has no DrawTextNew; font = \"game\" draws nothing");
	}
	return g_fno_draw >= 0;
}

int rq_gametext_available(void)
{
	return ready();
}

static int new_str(const char *utf)
{
	char *sj = utf2sjis(utf ? utf : "", 0);
	struct string *s;
	int slot;

	if (!sj)
		sj = strdup("");
	s = make_string(sj, strlen(sj));
	free(sj);
	slot = heap_alloc_slot(VM_STRING);
	heap[slot].s = s;
	return slot;
}

static void call_i(int fno, int a)
{
	if (fno < 0)
		return;
	stack_push(a);
	vm_call(fno, -1);
}

static void call_f(int fno, float a)
{
	union vm_value v;
	if (fno < 0)
		return;
	v.f = a;
	stack_push(v);
	vm_call(fno, -1);
}

/* The game's sprite for the line, or 0. */
static int draw_line(const char *utf8, const struct rq_gametext *st)
{
	union vm_value v;
	int saved_font = 0, saved_size = 0, saved_fixed = 0, have = 0, no;
	float saved_spacing = 1.0f;
	int edge = st->edge > 0 ? st->edge * 100 + st->weight : 0;

	if (g_gi_font >= 0 && g_gi_size >= 0 && g_gi_spacing >= 0 && g_gi_fixed >= 0) {
		saved_font = global_get(g_gi_font).i;
		saved_size = global_get(g_gi_size).i;
		saved_spacing = global_get(g_gi_spacing).f;
		saved_fixed = global_get(g_gi_fixed).i;
		have = 1;
	}
	stack_push(1);  /* nType 1: letter by letter, as the game's buttons */
	stack_push(new_str(utf8));
	stack_push(st->face);
	stack_push(st->size);
	stack_push(st->r);
	stack_push(st->g);
	stack_push(st->b);
	stack_push(st->weight);
	stack_push(st->er);
	stack_push(st->eg);
	stack_push(st->eb);
	stack_push(edge);
	stack_push(st->space);
	v.f = st->spacing;
	stack_push(v);
	vm_call(g_fno_draw, -1);
	no = stack_pop().i;
	if (have && saved_font > 0) {
		call_i(g_fno_select, saved_font);
		call_i(g_fno_size, saved_size);
		call_f(g_fno_spacing, saved_spacing);
		call_i(g_fno_fixed, saved_fixed);
	}
	return no > 0 ? no : 0;
}

static void make_key(char *buf, size_t n, const char *utf8, const struct rq_gametext *st)
{
	snprintf(buf, n, "%d|%d|%d,%d,%d|%d|%d:%d,%d,%d|%d|%.4f|%s", st->face, st->size,
		 st->r, st->g, st->b, st->weight, st->edge, st->er, st->eg, st->eb,
		 st->space, (double)st->spacing, utf8);
}

static uint32_t fnv(const char *s)
{
	uint32_t h = 2166136261u;
	for (; *s; s++) {
		h ^= (unsigned char)*s;
		h *= 16777619u;
	}
	return h;
}

/* A slot for a new entry: an empty one, else the one used longest ago that was
 * not used since the last draw. NULL when every one is in use on this update. */
static struct gt_entry *free_slot(void)
{
	struct gt_entry *best = NULL;
	int i;
	if (g_ngt < GT_MAX)
		return &g_gt[g_ngt++];
	for (i = 0; i < GT_MAX; i++) {
		if (g_gt[i].used >= g_gen)
			continue;
		if (!best || g_gt[i].used < best->used)
			best = &g_gt[i];
	}
	if (best) {
		free(best->key);
		gfx_delete_texture(&best->t);
		memset(best, 0, sizeof *best);
	}
	return best;
}

static Texture *find_or_make(const char *utf8, const struct rq_gametext *st, int *mw, int *mh,
			     unsigned char **px_out);

Texture *rq_gametext_texture(const char *utf8, const struct rq_gametext *st)
{
	return find_or_make(utf8, st, NULL, NULL, NULL);
}

/* The size of the line; when nothing can be kept on this update it is drawn,
 * measured and dropped. */
int rq_gametext_measure(const char *utf8, const struct rq_gametext *st, int *w, int *h)
{
	Texture *t;
	*w = *h = 0;
	t = find_or_make(utf8, st, w, h, NULL);
	if (t) {
		*w = t->w;
		*h = t->h;
	}
	return *w > 0;
}

static Texture *find_or_make(const char *utf8, const struct rq_gametext *st, int *mw, int *mh,
			     unsigned char **px_out)
{
	char key[1200];
	uint32_t h;
	int i, no;
	struct gt_entry *e;
	struct sact_sprite *sp;
	Texture *src;
	void *px;

	if (!utf8 || !*utf8 || !ready())
		return NULL;
	make_key(key, sizeof key, utf8, st);
	h = fnv(key);
	for (i = 0; i < g_ngt; i++) {
		if (g_gt[i].hash == h && g_gt[i].key && !strcmp(g_gt[i].key, key)) {
			g_gt[i].used = g_gen;
			return &g_gt[i].t;
		}
	}
	no = draw_line(utf8, st);
	if (!no)
		return NULL;
	sp = sact_try_get_sprite(no);
	src = sp ? sprite_get_texture(sp) : NULL;
	if (!src || src->w <= 0 || src->h <= 0) {
		sact_SP_Delete(no);
		return NULL;
	}
	e = free_slot();
	if (!e) {
		static int warned;
		if (mw && mh) {
			*mw = src->w;
			*mh = src->h;
			if (px_out)
				*px_out = gfx_get_pixels(src);
		} else if (!warned++) {
			rq_log("gametext: more than %d different texts drawn on one update; the rest are not drawn", GT_MAX);
		}
		sact_SP_Delete(no);
		return NULL;
	}
	px = gfx_get_pixels(src);
	gfx_init_texture_with_pixels(&e->t, src->w, src->h, px);
	free(px);
	sact_SP_Delete(no);
	e->hash = h;
	e->key = strdup(key);
	e->used = g_gen;
	return &e->t;
}

/* The line as RGBA pixels (free them); it need not be kept, so it works past the
 * 512 texts of one update. */
unsigned char *rq_gametext_rgba(const char *utf8, const struct rq_gametext *st, int *w, int *h)
{
	unsigned char *px = NULL;
	Texture *t;
	*w = *h = 0;
	t = find_or_make(utf8, st, w, h, &px);
	if (!t)
		return px;
	*w = t->w;
	*h = t->h;
	return gfx_get_pixels(t);
}

/* Where the line goes in a box h high: the game's own centering for face 258
 * (T_TextSprite::textDrawCenter2), else the middle. */
int rq_gametext_center_y(const struct rq_gametext *st, int box_h, int line_h)
{
	if (st->face == 258) {
		int size = st->size;
		int corr = (line_h - size) / 2;
		return (box_h - (size * 10) / 18) / 2 - (size * 3) / 18 - corr;
	}
	return (box_h - line_h) / 2;
}

void rq_gametext_after_draw(void)
{
	g_gen++;
}

#endif /* RQ_MODLOADER */
