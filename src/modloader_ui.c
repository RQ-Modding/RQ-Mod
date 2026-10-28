/* Screen dump for rqmod-ctl. Reads live PartsEngine widgets on the game
 * thread at gfx_swap. Lua does not own this; the title menus exist first.
 */

#ifdef RQ_MODLOADER

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "system4.h"
#include "system4/string.h"
#include "system4/utfsjis.h"

#include "cJSON.h"
#include "input.h"
#include "scene.h"
#include "sprite.h"
#include "vm.h"
#include "xsystem4.h"
#include "modloader.h"
#include "parts/parts_internal.h"

__declspec(dllimport) void __stdcall Sleep(unsigned long ms);
__declspec(dllimport) unsigned long long __stdcall GetTickCount64(void);

#define U8_CONFIG       "\xE3\x82\xB3\xE3\x83\xB3\xE3\x83\x95\xE3\x82\xA3\xE3\x82\xB0"
#define U8_SETTINGS     "\xE7\x92\xB0\xE5\xA2\x83\xE8\xA8\xAD\xE5\xAE\x9A"
#define U8_CLOSE        "\xE9\x96\x89\xE3\x81\x98\xE3\x82\x8B"
#define U8_BACK         "\xE6\x88\xBB\xE3\x82\x8B"
#define U8_CLICK        "\xE3\x82\xAF\xE3\x83\xAA\xE3\x83\x83\xE3\x82\xAF"
#define U8_NEXT         "\xE6\xAC\xA1\xE3\x81\xB8"
#define U8_YES          "\xE3\x81\xAF\xE3\x81\x84"
#define U8_AGREE        "\xE5\x90\x8C\xE6\x84\x8F"
#define U8_HAJI         "\xE3\x81\xAF\xE3\x81\x98"
#define U8_UNDER        "\xE6\x9C\xAA\xE6\xBA\x80"
#define U8_AGE          "\xE6\xAD\xB3"
#define U8_WARN         "\xE8\xAD\xA6\xE5\x91\x8A"
#define U8_FICTION      "\xE3\x83\x95\xE3\x82\xA3\xE3\x82\xAF\xE3\x82\xB7\xE3\x83\xA7\xE3\x83\xB3"
#define U8_CAUTION      "\xE3\x81\x94\xE6\xB3\xA8\xE6\x84\x8F"
#define U8_YEARS        "\xE5\xB9\xB4\xE9\xBD\xA2"
#define U8_VOLUME       "\xE9\x9F\xB3\xE9\x87\x8F"
#define U8_WINDOW       "\xE3\x82\xA6\xE3\x82\xA3\xE3\x83\xB3\xE3\x83\x89\xE3\x82\xA6"
#define U8_MESSAGE      "\xE3\x83\xA1\xE3\x83\x83\xE3\x82\xBB\xE3\x83\xBC\xE3\x82\xB8"
#define U8_SLIDER       "\xE3\x82\xB9\xE3\x83\xA9\xE3\x82\xA4\xE3\x83\x80\xE3\x83\xBC"
#define U8_BGM          "\xEF\xBC\xA2\xEF\xBC\xA7\xEF\xBC\xAD"
#define U8_FULLSCREEN   "\xE3\x83\x95\xE3\x83\xAB\xE3\x82\xB9\xE3\x82\xAF\xE3\x83\xAA\xE3\x83\xBC\xE3\x83\xB3"
#define U8_VOL          "\xE3\x83\x9C\xE3\x83\xAA\xE3\x83\xA5\xE3\x83\xBC\xE3\x83\xA0"
#define U8_TITLE        "\xE3\x82\xBF\xE3\x82\xA4\xE3\x83\x88\xE3\x83\xAB"
#define U8_TITLE_SCREEN U8_TITLE "\xE7\x94\xBB\xE9\x9D\xA2"
#define U8_TIMES        "\xC3\x97"
#define U8_XMARK        "\xE2\x9C\x95"
#define U8_XHEAVY       "\xE2\x9C\x96"
#define U8_XSMALL       "\xEF\xBD\x98"
#define U8_XBIG         "\xEF\xBC\xB8"

#define CTRL_MAX 96
#define TEXT_MAX 48
#define TEXT_LEN 200
#define LABEL_LEN 160
#define JSON_MAX (96 * 1024)
#define NOTE_N 48
#define NOTE_LEN 700
#define FRAME_MS 4000

struct ui_ctrl {
	int parts_no;
	char id[24];
	char label[LABEL_LEN];
	int x, y, w, h;
	int enabled;
};

struct ui_snap {
	char screen[24];
	char hover[24];
	int nctrl;
	struct ui_ctrl ctrl[CTRL_MAX];
	char json[JSON_MAX];
};

static atomic_flag g_lock = ATOMIC_FLAG_INIT;
static int g_ready;
static atomic_int g_wants;
static atomic_llong g_present_gen;
static atomic_llong g_published;
static atomic_llong g_input_gen;
static struct ui_snap g_snap;

static char g_blob[12000];
static char g_texts[TEXT_MAX][TEXT_LEN];
static int g_ntexts;
static struct ui_ctrl g_ctrls[CTRL_MAX];
static int g_nctrls;
static int g_hover_no;
static char g_screen[24];
static char g_hover[24];

struct sp_note {
	int sp;
	char utf[NOTE_LEN];
};
static struct sp_note g_notes[NOTE_N];
static int g_notes_ready;

static void ui_lock(void)
{
	while (atomic_flag_test_and_set_explicit(&g_lock, memory_order_acquire))
		;
}

static void ui_unlock(void)
{
	atomic_flag_clear_explicit(&g_lock, memory_order_release);
}

static long long rd_ll(atomic_llong *p)
{
	return atomic_load(p);
}

static void notes_ready(void)
{
	int i;
	if (g_notes_ready)
		return;
	for (i = 0; i < NOTE_N; i++)
		g_notes[i].sp = -1;
	g_notes_ready = 1;
}

static struct sp_note *note_slot(int sp, int create)
{
	int i, free_i = -1;
	notes_ready();
	for (i = 0; i < NOTE_N; i++) {
		if (g_notes[i].sp == sp)
			return &g_notes[i];
		if (free_i < 0 && g_notes[i].sp < 0)
			free_i = i;
	}
	if (!create)
		return NULL;
	if (free_i < 0)
		free_i = 0;
	g_notes[free_i].sp = sp;
	g_notes[free_i].utf[0] = 0;
	return &g_notes[free_i];
}

void rq_ui_note_sprite_text(int sp_no, const char *sjis)
{
	char *u;
	struct sp_note *n;
	int len, add;
	if (sp_no < 0 || !sjis || !*sjis)
		return;
	u = sjis2utf(sjis, strlen(sjis));
	if (!u)
		return;
	n = note_slot(sp_no, 1);
	if (!n) {
		free(u);
		return;
	}
	len = (int)strlen(n->utf);
	add = (int)strlen(u);
	if (len + add >= NOTE_LEN)
		add = NOTE_LEN - len - 1;
	if (add > 0) {
		memcpy(n->utf + len, u, (size_t)add);
		n->utf[len + add] = 0;
	}
	free(u);
}

void rq_ui_clear_sprite_text(int sp_no)
{
	struct sp_note *n = note_slot(sp_no, 0);
	if (!n)
		return;
	n->sp = -1;
	n->utf[0] = 0;
}

static const char *note_text(int sp)
{
	struct sp_note *n = note_slot(sp, 0);
	if (!n || !n->utf[0])
		return NULL;
	return n->utf;
}

void rq_ui_init(void)
{
	if (g_ready)
		return;
	memset(&g_snap, 0, sizeof g_snap);
	strcpy(g_snap.screen, "boot");
	strcpy(g_snap.json, "{\"screen\":\"boot\",\"text\":[],\"controls\":[],\"hover\":null}\n");
	notes_ready();
	g_ready = 1;
}

void rq_ui_on_parts_input(void)
{
	atomic_fetch_add(&g_input_gen, 1);
}

int rq_ui_wants_frame(void)
{
	return atomic_load(&g_wants) > 0;
}

static void clean_copy(char *dst, int n, const char *src)
{
	int j = 0, sp = 1;
	if (!src) {
		if (n)
			dst[0] = 0;
		return;
	}
	for (; *src && j < n - 1; src++) {
		unsigned char c = (unsigned char)*src;
		if (c == '\n' || c == '\r' || c == '\t')
			c = ' ';
		if (c < 0x20)
			continue;
		if (c == ' ') {
			if (sp)
				continue;
			sp = 1;
		} else {
			sp = 0;
		}
		dst[j++] = (char)c;
	}
	while (j > 0 && dst[j - 1] == ' ')
		j--;
	dst[j] = 0;
}

static int sjis_clean(char *dst, int n, const char *sjis)
{
	char *u;
	if (!sjis || !*sjis || n <= 0) {
		if (n)
			dst[0] = 0;
		return 0;
	}
	u = sjis2utf(sjis, strlen(sjis));
	if (!u) {
		dst[0] = 0;
		return 0;
	}
	clean_copy(dst, n, u);
	free(u);
	return dst[0] != 0;
}

static void blob_add(const char *s)
{
	int n;
	if (!s || !*s)
		return;
	n = (int)strlen(s);
	if ((int)strlen(g_blob) + n + 2 >= (int)sizeof g_blob)
		return;
	if (g_blob[0])
		strcat(g_blob, "\n");
	strcat(g_blob, s);
}

static void text_add(const char *s)
{
	if (!s || !*s)
		return;
	blob_add(s);
	if (g_ntexts >= TEXT_MAX)
		return;
	if (g_ntexts > 0 && !strcmp(g_texts[g_ntexts - 1], s))
		return;
	snprintf(g_texts[g_ntexts], TEXT_LEN, "%s", s);
	g_ntexts++;
}

static void append_raw(char *dst, int n, const char *sjis, int gap)
{
	int len, add;
	if (!sjis || !*sjis || n <= 0)
		return;
	len = (int)strlen(dst);
	if (gap && len > 0 && len < n - 1) {
		dst[len++] = ' ';
		dst[len] = 0;
	}
	add = (int)strlen(sjis);
	if (len + add >= n)
		add = n - len - 1;
	if (add <= 0)
		return;
	memcpy(dst + len, sjis, (size_t)add);
	dst[len + add] = 0;
}

static void state_text_sjis(struct parts_state *st, char *dst, int n)
{
	if (st->type == PARTS_TEXT) {
		unsigned i;
		for (i = 0; i < st->text.nr_lines; i++) {
			int k;
			struct parts_text_line *line = &st->text.lines[i];
			if (i > 0)
				append_raw(dst, n, " ", 0);
			for (k = 0; k < line->nr_chars; k++)
				append_raw(dst, n, line->chars[k].ch, 0);
		}
	} else if (st->type == PARTS_CONSTRUCTION_PROCESS) {
		struct parts_cp_op *op;
		TAILQ_FOREACH(op, &st->cproc.ops, entry) {
			if ((op->type == PARTS_CP_DRAW_TEXT || op->type == PARTS_CP_COPY_TEXT)
					&& op->text.text && op->text.text->size > 0)
				append_raw(dst, n, op->text.text->text, 1);
		}
	}
}

static int part_drawn_text(struct parts *parts, char *dst, int n)
{
	char sjis[1024];
	int i;
	sjis[0] = 0;
	dst[0] = 0;
	for (i = 0; i < PARTS_NR_STATES; i++)
		state_text_sjis(&parts->states[i], sjis, (int)sizeof sjis);
	return sjis_clean(dst, n, sjis);
}

static void basename_utf(char *dst, int n, const char *sjis)
{
	char full[300];
	char tmp[200];
	const char *base, *q;
	char *dot;
	dst[0] = 0;
	if (!sjis_clean(full, (int)sizeof full, sjis))
		return;
	base = full;
	for (q = full; *q; q++) {
		if (*q == '/' || *q == '\\')
			base = q + 1;
	}
	snprintf(tmp, sizeof tmp, "%s", base);
	dot = strrchr(tmp, '.');
	if (dot && dot != tmp)
		*dot = 0;
	snprintf(dst, n, "%s", tmp);
}

static void part_asset_name(struct parts *parts, char *dst, int n)
{
	int i;
	dst[0] = 0;
	for (i = 0; i < PARTS_NR_STATES && !dst[0]; i++) {
		struct parts_state *st = &parts->states[i];
		const char *name = NULL;
		if (st->type == PARTS_CG && st->cg.name)
			name = st->cg.name->text;
		else if (st->type == PARTS_FLAT && st->flat.name)
			name = st->flat.name->text;
		else if (st->type == PARTS_FLASH && st->flash.name)
			name = st->flash.name->text;
		else if (st->type == PARTS_ANIMATION && st->anim.cg_name)
			name = st->anim.cg_name->text;
		if (name)
			basename_utf(dst, n, name);
	}
	if (dst[0])
		blob_add(dst);
}

static int parts_on_screen(struct parts *parts)
{
	if (!parts->global.show)
		return 0;
	if (parts->message_window && !parts_message_window_show)
		return 0;
	if (parts->linked_to >= 0) {
		struct parts *link = parts_try_get(parts->linked_to);
		if (!link || !link->is_hovered)
			return 0;
	}
	return 1;
}

static void abs_hitbox(struct parts *parts, int *x, int *y, int *w, int *h)
{
	Rectangle hit = parts->states[PARTS_STATE_DEFAULT].common.hitbox;
	if (parts->parent) {
		hit.x += parts->parent->global.pos.x;
		hit.y += parts->parent->global.pos.y;
	}
	*x = hit.x;
	*y = hit.y;
	*w = hit.w;
	*h = hit.h;
}

static void fill_label(struct parts *parts, char *dst, int n)
{
	struct parts *child;
	dst[0] = 0;
	if (part_drawn_text(parts, dst, n))
		return;
	PARTS_FOREACH_CHILD(child, parts) {
		char tmp[LABEL_LEN];
		if (part_drawn_text(child, tmp, (int)sizeof tmp)) {
			int len = (int)strlen(dst);
			if (len && len < n - 1) {
				dst[len++] = ' ';
				dst[len] = 0;
			}
			snprintf(dst + strlen(dst), (size_t)(n - (int)strlen(dst)), "%s", tmp);
		}
	}
	if (dst[0])
		return;
	if (parts->linked_to >= 0) {
		struct parts *link = parts_try_get(parts->linked_to);
		if (link && part_drawn_text(link, dst, n))
			return;
	}
	part_asset_name(parts, dst, n);
	if (!dst[0] && parts->linked_to >= 0) {
		struct parts *link = parts_try_get(parts->linked_to);
		if (link)
			part_asset_name(link, dst, n);
	}
}

static void on_scene_sprite(struct sprite *sp, void *unused)
{
	char buf[400];
	(void)unused;
	if (sp->render == parts_sprite_render) {
		struct parts *parts = (struct parts *)sp;
		if (!parts_on_screen(parts))
			return;
		if (part_drawn_text(parts, buf, (int)sizeof buf))
			text_add(buf);
		return;
	}
	{
		struct sact_sprite *sact = sprite_as_sact(sp);
		const char *t;
		if (!sact || sact->sp.hidden)
			return;
		t = note_text(sact->no);
		if (!t)
			return;
		clean_copy(buf, (int)sizeof buf, t);
		text_add(buf);
	}
}

static int label_is_config(const char *s)
{
	if (!s || !*s || strlen(s) > 40)
		return 0;
	if (strstr(s, U8_CONFIG) || strstr(s, U8_SETTINGS))
		return 1;
	if (strstr(s, "CONFIG") || strstr(s, "Config") || strstr(s, "config"))
		return 1;
	return 0;
}

static int label_is_close(const char *s)
{
	if (!s || !*s)
		return 0;
	if (!_stricmp(s, "x") || !_stricmp(s, "close") || !_stricmp(s, "back"))
		return 1;
	if (!strcmp(s, U8_TIMES) || !strcmp(s, U8_XMARK) || !strcmp(s, U8_XHEAVY)
			|| !strcmp(s, U8_XSMALL) || !strcmp(s, U8_XBIG))
		return 1;
	if (strstr(s, U8_CLOSE) || !strcmp(s, U8_BACK))
		return 1;
	return 0;
}

static int label_is_advance(const char *s)
{
	if (!s || !*s || strlen(s) > 24)
		return 0;
	if (strstr(s, U8_CLICK) || strstr(s, U8_NEXT) || strstr(s, U8_AGREE)
			|| strstr(s, U8_HAJI))
		return 1;
	if (!strcmp(s, U8_YES))
		return 1;
	if (!_stricmp(s, "ok") || !_stricmp(s, "click") || strstr(s, "Click")
			|| strstr(s, "NEXT") || strstr(s, "Next"))
		return 1;
	return 0;
}

static int blob_has(const char *s)
{
	return s && g_blob[0] && strstr(g_blob, s) != NULL;
}

static char g_stackblob[1600];

static void collect_stack(void)
{
	int i, len = 0;
	g_stackblob[0] = 0;
	for (i = 0; i < 12; i++) {
		const char *sj = vm_frame_name(i);
		char *u;
		if (!sj)
			break;
		u = sjis2utf(sj, 0);
		if (!u)
			continue;
		if (len && len < (int)sizeof g_stackblob - 2) {
			g_stackblob[len++] = '\n';
			g_stackblob[len] = 0;
		}
		snprintf(g_stackblob + len, sizeof g_stackblob - (size_t)len, "%s", u);
		len = (int)strlen(g_stackblob);
		free(u);
	}
}

static int stack_has(const char *s)
{
	return s && g_stackblob[0] && strstr(g_stackblob, s) != NULL;
}

static void set_id(struct ui_ctrl *c, const char *id)
{
	snprintf(c->id, sizeof c->id, "%s", id);
}

static void classify(void)
{
	int i, named_close = -1, pos_close = -1, pos_score = -1000000;
	int has_config = 0, has_close = 0, settings, is_config, is_main;
	int stack_config, stack_title, stack_warn;
	int vw = config.view_width > 0 ? config.view_width : 1024;
	int text_len = 0;

	stack_config = stack_has(U8_CONFIG) || stack_has(U8_SETTINGS);
	stack_title = stack_has("T_Title") || stack_has(U8_TITLE_SCREEN);
	stack_warn = stack_has(U8_WARN);

	for (i = 0; i < g_ntexts; i++)
		text_len += (int)strlen(g_texts[i]);
	for (i = 0; i < g_nctrls; i++) {
		int right, top, score;
		blob_add(g_ctrls[i].label);
		if (label_is_config(g_ctrls[i].label))
			has_config = 1;
		if (label_is_close(g_ctrls[i].label))
			named_close = i;
		right = g_ctrls[i].x + g_ctrls[i].w;
		top = g_ctrls[i].y;
		if (g_ctrls[i].w >= 8 && g_ctrls[i].h >= 8
				&& g_ctrls[i].w <= 120 && g_ctrls[i].h <= 120
				&& right >= vw - 48 && top <= 40) {
			score = right - top * 2;
			if (score > pos_score) {
				pos_score = score;
				pos_close = i;
			}
		}
	}
	settings = blob_has(U8_VOLUME) || blob_has(U8_WINDOW) || blob_has(U8_MESSAGE)
		|| blob_has(U8_SLIDER) || blob_has(U8_BGM) || blob_has("BGM")
		|| blob_has(U8_FULLSCREEN) || blob_has(U8_VOL);
	if (named_close >= 0) {
		set_id(&g_ctrls[named_close], "close");
		has_close = 1;
	} else if (pos_close >= 0 && (g_nctrls >= 8 || settings)) {
		set_id(&g_ctrls[pos_close], "close");
		has_close = 1;
	}
	is_config = stack_config || (has_close && (g_nctrls >= 6 || settings));
	is_main = !is_config && (has_config || (stack_title && !stack_warn));
	if (is_config)
		snprintf(g_screen, sizeof g_screen, "config");
	else if (is_main)
		snprintf(g_screen, sizeof g_screen, "main-menu");
	else if (stack_warn || blob_has(U8_UNDER) || blob_has(U8_WARN) || blob_has(U8_FICTION)
			|| blob_has(U8_CAUTION) || blob_has(U8_YEARS)
			|| blob_has(U8_AGE) || strstr(g_blob, "18"))
		snprintf(g_screen, sizeof g_screen, "disclaimer");
	else if (g_nctrls == 0 && text_len < 8)
		snprintf(g_screen, sizeof g_screen, "boot");
	else if (g_nctrls == 0 && text_len >= 60)
		snprintf(g_screen, sizeof g_screen, "disclaimer");
	else if (g_nctrls == 0)
		snprintf(g_screen, sizeof g_screen, "boot");
	else if (g_nctrls <= 8)
		snprintf(g_screen, sizeof g_screen, "title");
	else
		snprintf(g_screen, sizeof g_screen, "other");

	if (is_main) {
		int best = -1, best_len = 1000;
		for (i = 0; i < g_nctrls; i++) {
			int len;
			if (!label_is_config(g_ctrls[i].label))
				continue;
			if (!strcmp(g_ctrls[i].id, "close"))
				continue;
			len = (int)strlen(g_ctrls[i].label);
			if (len < best_len) {
				best = i;
				best_len = len;
			}
		}
		if (best >= 0)
			set_id(&g_ctrls[best], "config");
	}
	if (strcmp(g_screen, "main-menu") != 0 && strcmp(g_screen, "config") != 0) {
		int named = -1, named_len = 1000, biggest = -1, big_area = -1;
		for (i = 0; i < g_nctrls; i++) {
			int area;
			if (!strcmp(g_ctrls[i].id, "close") || !strcmp(g_ctrls[i].id, "config"))
				continue;
			if (label_is_advance(g_ctrls[i].label)) {
				int len = (int)strlen(g_ctrls[i].label);
				if (len < named_len) {
					named = i;
					named_len = len;
				}
			}
			area = g_ctrls[i].w * g_ctrls[i].h;
			if (area > big_area) {
				big_area = area;
				biggest = i;
			}
		}
		if (named >= 0)
			set_id(&g_ctrls[named], "advance");
		else if (biggest >= 0)
			set_id(&g_ctrls[biggest], "advance");
	}
	g_hover[0] = 0;
	if (g_hover_no != -1) {
		for (i = 0; i < g_nctrls; i++) {
			if (g_ctrls[i].parts_no == g_hover_no) {
				snprintf(g_hover, sizeof g_hover, "%s", g_ctrls[i].id);
				break;
			}
		}
	}
}

static void build_json(char *dst, int dstsz)
{
	cJSON *root, *text, *controls;
	char *printed;
	int i;
	root = cJSON_CreateObject();
	cJSON_AddStringToObject(root, "screen", g_screen);
	text = cJSON_AddArrayToObject(root, "text");
	for (i = 0; i < g_ntexts; i++)
		cJSON_AddItemToArray(text, cJSON_CreateString(g_texts[i]));
	controls = cJSON_AddArrayToObject(root, "controls");
	for (i = 0; i < g_nctrls; i++) {
		cJSON *c = cJSON_CreateObject();
		cJSON_AddStringToObject(c, "id", g_ctrls[i].id);
		cJSON_AddStringToObject(c, "label", g_ctrls[i].label);
		cJSON_AddNumberToObject(c, "x", g_ctrls[i].x);
		cJSON_AddNumberToObject(c, "y", g_ctrls[i].y);
		cJSON_AddNumberToObject(c, "w", g_ctrls[i].w);
		cJSON_AddNumberToObject(c, "h", g_ctrls[i].h);
		cJSON_AddBoolToObject(c, "enabled", g_ctrls[i].enabled ? 1 : 0);
		cJSON_AddItemToArray(controls, c);
	}
	if (g_hover[0])
		cJSON_AddStringToObject(root, "hover", g_hover);
	else
		cJSON_AddNullToObject(root, "hover");
	printed = cJSON_PrintUnformatted(root);
	cJSON_Delete(root);
	if (!printed) {
		snprintf(dst, dstsz,
			 "{\"screen\":\"other\",\"text\":[],\"controls\":[],\"hover\":null}\n");
		return;
	}
	snprintf(dst, dstsz, "%s\n", printed);
	free(printed);
}

static int g_mx, g_my, g_vw, g_vh;
static int g_sprite_hover;
static int g_sprite_hover_area;

static void on_sprite_ctrl(struct sprite *sp, void *unused)
{
	struct sact_sprite *sact;
	struct ui_ctrl *c;
	const char *t;
	int x, y, w, h;
	(void)unused;
	sact = sprite_as_sact(sp);
	if (!sact || sact->sp.hidden || g_nctrls >= CTRL_MAX)
		return;
	x = sact->rect.x;
	y = sact->rect.y;
	w = sact->rect.w;
	h = sact->rect.h;
	if (w < 8 || h < 8)
		return;
	if (w >= g_vw - 32 && h >= g_vh - 48)
		return;
	if (w * h >= (g_vw * g_vh) / 2)
		return;
	c = &g_ctrls[g_nctrls++];
	memset(c, 0, sizeof *c);
	c->parts_no = -sact->no - 1;
	snprintf(c->id, sizeof c->id, "s%d", sact->no);
	t = note_text(sact->no);
	if (t)
		clean_copy(c->label, (int)sizeof c->label, t);
	c->x = x;
	c->y = y;
	c->w = w;
	c->h = h;
	c->enabled = 1;
	if (g_mx >= x && g_my >= y && g_mx < x + w && g_my < y + h) {
		int area = w * h;
		if (g_sprite_hover == -1 || area < g_sprite_hover_area) {
			g_sprite_hover = c->parts_no;
			g_sprite_hover_area = area;
		}
	}
}

static void publish(void)
{
	struct parts *parts;
	static struct ui_snap stage;
	int i;

	g_blob[0] = 0;
	g_ntexts = 0;
	g_nctrls = 0;
	g_hover_no = -1;
	g_screen[0] = 0;
	g_hover[0] = 0;
	collect_stack();

	scene_for_each_sprite(on_scene_sprite, NULL);
	PARTS_LIST_FOREACH(parts) {
		char buf[400];
		if (parts->sp.in_scene || !parts_on_screen(parts))
			continue;
		if (part_drawn_text(parts, buf, (int)sizeof buf))
			text_add(buf);
	}
	PARTS_LIST_FOREACH(parts) {
		int x, y, w, h;
		struct ui_ctrl *c;
		if (!parts->clickable || !parts_on_screen(parts))
			continue;
		abs_hitbox(parts, &x, &y, &w, &h);
		if (w <= 0 || h <= 0 || g_nctrls >= CTRL_MAX)
			continue;
		c = &g_ctrls[g_nctrls++];
		memset(c, 0, sizeof *c);
		c->parts_no = parts->no;
		snprintf(c->id, sizeof c->id, "p%d", parts->no);
		fill_label(parts, c->label, (int)sizeof c->label);
		c->x = x;
		c->y = y;
		c->w = w;
		c->h = h;
		c->enabled = 1;
	}
	PARTS_LIST_FOREACH_REVERSE(parts) {
		int x, y, w, h;
		if (!parts_on_screen(parts) || !parts->is_hovered)
			continue;
		if (parts->clickable) {
			abs_hitbox(parts, &x, &y, &w, &h);
			if (w > 0 && h > 0) {
				g_hover_no = parts->no;
				break;
			}
		}
		if (!parts->pass_cursor)
			break;
	}
	g_vw = config.view_width > 0 ? config.view_width : 1024;
	g_vh = config.view_height > 0 ? config.view_height : 768;
	{
		int active = 0;
		mouse_agent_pos(&g_mx, &g_my, &active);
		if (!active) {
			g_mx = -1;
			g_my = -1;
		}
	}
	g_sprite_hover = -1;
	scene_for_each_sprite(on_sprite_ctrl, NULL);
	if (g_hover_no < 0 && g_sprite_hover != -1)
		g_hover_no = g_sprite_hover;
	classify();
	memset(&stage, 0, sizeof stage);
	snprintf(stage.screen, sizeof stage.screen, "%s", g_screen);
	snprintf(stage.hover, sizeof stage.hover, "%s", g_hover);
	stage.nctrl = g_nctrls;
	for (i = 0; i < g_nctrls; i++)
		stage.ctrl[i] = g_ctrls[i];
	build_json(stage.json, (int)sizeof stage.json);
	ui_lock();
	g_snap = stage;
	ui_unlock();
}

void rq_ui_on_present(void)
{
	long long next;
	if (!g_ready)
		return;
	next = rd_ll(&g_present_gen) + 1;
	if (atomic_load(&g_wants) > 0) {
		publish();
		ui_lock();
		atomic_store(&g_published, next);
		ui_unlock();
	}
	atomic_store(&g_present_gen, next);
}

static int wait_published_after(long long start, unsigned long ms)
{
	unsigned long long t0 = GetTickCount64();
	while (GetTickCount64() - t0 < ms) {
		if (rd_ll(&g_published) > start)
			return 1;
		Sleep(2);
	}
	return 0;
}

static int wait_inputs(int n, unsigned long ms)
{
	int i;
	unsigned long long t0 = GetTickCount64();
	for (i = 0; i < n; i++) {
		long long s = rd_ll(&g_input_gen);
		while (rd_ll(&g_input_gen) <= s) {
			if (GetTickCount64() - t0 > ms)
				return 0;
			Sleep(2);
		}
	}
	return 1;
}

static void wants(int on)
{
	atomic_store(&g_wants, on ? 1 : 0);
}

static int copy_snap(struct ui_snap *out)
{
	if (!g_ready)
		return 0;
	ui_lock();
	*out = g_snap;
	ui_unlock();
	return 1;
}

int rq_ui_has_control(const char *id)
{
	struct ui_snap snap;
	int i;
	if (!id || !*id || !copy_snap(&snap))
		return 0;
	for (i = 0; i < snap.nctrl; i++) {
		if (!strcmp(snap.ctrl[i].id, id))
			return 1;
	}
	return 0;
}

static int ctrl_center(const char *id, int *cx, int *cy)
{
	struct ui_snap snap;
	int i;
	if (!copy_snap(&snap))
		return 0;
	for (i = 0; i < snap.nctrl; i++) {
		if (strcmp(snap.ctrl[i].id, id) != 0)
			continue;
		*cx = snap.ctrl[i].x + snap.ctrl[i].w / 2;
		*cy = snap.ctrl[i].y + snap.ctrl[i].h / 2;
		return 1;
	}
	return 0;
}

static int hover_is(const char *id)
{
	struct ui_snap snap;
	if (!id || !copy_snap(&snap))
		return 0;
	return snap.hover[0] && !strcmp(snap.hover, id);
}

int rq_ui_cmd_ui(char **json_out, char *err, int errsz)
{
	long long start;
	struct ui_snap snap;
	if (json_out)
		*json_out = NULL;
	if (!g_ready) {
		if (err)
			snprintf(err, errsz, "not-ready");
		return 0;
	}
	wants(1);
	start = rd_ll(&g_present_gen);
	if (!wait_published_after(start, FRAME_MS)) {
		wants(0);
		if (err)
			snprintf(err, errsz, "timeout");
		return 0;
	}
	copy_snap(&snap);
	wants(0);
	if (json_out)
		*json_out = strdup(snap.json);
	return 1;
}

int rq_ui_cmd_hover(const char *id, char *err, int errsz)
{
	long long start;
	int cx, cy;
	unsigned long long t0;
	if (!id || !*id) {
		if (err)
			snprintf(err, errsz, "usage: hover ID");
		return 0;
	}
	if (!g_ready) {
		if (err)
			snprintf(err, errsz, "not-ready");
		return 0;
	}
	wants(1);
	start = rd_ll(&g_present_gen);
	if (!wait_published_after(start, FRAME_MS)) {
		wants(0);
		if (err)
			snprintf(err, errsz, "timeout");
		return 0;
	}
	if (!ctrl_center(id, &cx, &cy)) {
		wants(0);
		if (err)
			snprintf(err, errsz, "unknown-control");
		return 0;
	}
	mouse_force_pos(cx, cy);
	start = rd_ll(&g_present_gen);
	t0 = GetTickCount64();
	while (GetTickCount64() - t0 < FRAME_MS) {
		if (rd_ll(&g_published) > start && hover_is(id)) {
			wants(0);
			return 1;
		}
		Sleep(2);
	}
	if (ctrl_center(id, &cx, &cy)) {
		mouse_force_pos(cx, cy);
		start = rd_ll(&g_present_gen);
		t0 = GetTickCount64();
		while (GetTickCount64() - t0 < FRAME_MS) {
			if (rd_ll(&g_published) > start && hover_is(id)) {
				wants(0);
				return 1;
			}
			Sleep(2);
		}
	}
	wants(0);
	if (err)
		snprintf(err, errsz, "timeout");
	return 0;
}

int rq_ui_cmd_press(const char *id, char *err, int errsz)
{
	int ok;
	if (!id || !*id) {
		if (err)
			snprintf(err, errsz, "usage: press ID");
		return 0;
	}
	if (!hover_is(id)) {
		if (err)
			snprintf(err, errsz, "not-hovered");
		return 0;
	}
	wants(1);
	key_set_state(VK_LBUTTON, 1);
	ok = wait_inputs(2, FRAME_MS);
	key_set_state(VK_LBUTTON, 0);
	if (ok)
		ok = wait_inputs(2, FRAME_MS);
	else
		wait_inputs(1, 500);
	wants(0);
	if (!ok) {
		if (err)
			snprintf(err, errsz, "timeout");
		return 0;
	}
	return 1;
}

#endif /* RQ_MODLOADER */
