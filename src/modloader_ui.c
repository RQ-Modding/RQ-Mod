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
#include "vm/heap.h"
#include "vm/page.h"
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

#define CTRL_MAX 128
#define TEXT_MAX 48
#define TEXT_LEN 200
#define LABEL_LEN 160
#define JSON_MAX (96 * 1024)
#define NOTE_N 48
#define NOTE_LEN 700
#define FRAME_MS 4000

struct ui_ctrl {
	int parts_no;
	char id[40];
	char label[LABEL_LEN];
	char type[16];
	char section[96];
	char value[32];
	int x, y, w, h;
	int enabled;
	int button;
	int key;
	int sprite;
	int index;
	int hover_only;
	int cursor_hover;
	int armed;
	int has_select;
	int selected;
};

struct ui_snap {
	char screen[24];
	char hover[40];
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
static char g_hover[40];

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
		if (g_ctrls[i].type[0])
			cJSON_AddStringToObject(c, "type", g_ctrls[i].type);
		if (g_ctrls[i].section[0])
			cJSON_AddStringToObject(c, "section", g_ctrls[i].section);
		if (g_ctrls[i].value[0])
			cJSON_AddStringToObject(c, "value", g_ctrls[i].value);
		if (g_ctrls[i].button)
			cJSON_AddNumberToObject(c, "button", g_ctrls[i].button);
		if (g_ctrls[i].key > 0)
			cJSON_AddNumberToObject(c, "key", g_ctrls[i].key);
		if (g_ctrls[i].sprite >= 0)
			cJSON_AddNumberToObject(c, "sprite", g_ctrls[i].sprite);
		if (g_ctrls[i].index >= 0)
			cJSON_AddNumberToObject(c, "index", g_ctrls[i].index);
		cJSON_AddNumberToObject(c, "x", g_ctrls[i].x);
		cJSON_AddNumberToObject(c, "y", g_ctrls[i].y);
		cJSON_AddNumberToObject(c, "w", g_ctrls[i].w);
		cJSON_AddNumberToObject(c, "h", g_ctrls[i].h);
		cJSON_AddBoolToObject(c, "enabled", g_ctrls[i].enabled ? 1 : 0);
		if (g_ctrls[i].has_select)
			cJSON_AddBoolToObject(c, "selected", g_ctrls[i].selected ? 1 : 0);
		if (g_ctrls[i].hover_only)
			cJSON_AddBoolToObject(c, "hoverOnly", 1);
		if (g_ctrls[i].cursor_hover)
			cJSON_AddStringToObject(c, "cursor", "hover-only");
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
	c->sprite = -1;
	c->index = -1;
	c->armed = 1;
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

static int sn_get(const char *utf)
{
	char *sj;
	int n = -1;
	if (!ain || !utf)
		return -1;
	sj = utf2sjis(utf, 0);
	if (!sj)
		return -1;
	n = ain_get_struct(ain, sj);
	free(sj);
	return n;
}

static int sn_title, sn_button, sn_msg, sn_speed, sn_window, sn_input;
static int sn_volume, sn_debug, sn_tabview, sn_tabbtn, sn_exit;
static int sn_cb, sn_cbwrap, sn_hslider, sn_vslider, sn_text, sn_btnsp, sn_cfgmsg;
static int snos_ready;

static void snos_init(void)
{
	if (snos_ready)
		return;
	sn_title = sn_get("T_Title");
	sn_button = sn_get("T_Button");
	sn_msg = sn_get("CMessageConfigView");
	sn_speed = sn_get("CMessageSpeedConfigView");
	sn_window = sn_get("CWindowConfigView");
	sn_input = sn_get("CInputConfigView");
	sn_volume = sn_get("CVolumeConfigView");
	sn_debug = sn_get("CDebugConfigView");
	sn_tabview = sn_get("CConfigTabView");
	sn_tabbtn = sn_get("CConfigTabButton");
	sn_exit = sn_get("CConfigExitBar");
	sn_cb = sn_get("CASCheckBoxSprite");
	sn_cbwrap = sn_get("CASConfigCheckBoxSprite");
	sn_hslider = sn_get("CASHorizonSlider");
	sn_vslider = sn_get("CASVerticalSlider");
	sn_text = sn_get("CASTextSprite");
	sn_btnsp = sn_get("CASButtonSprite");
	sn_cfgmsg = sn_get("T_ConfigMessage");
	snos_ready = 1;
}

static int is_config_view(int idx)
{
	return idx >= 0 && (idx == sn_msg || idx == sn_speed || idx == sn_window
		|| idx == sn_input || idx == sn_volume || idx == sn_debug);
}

static struct page *slot_page(int slot)
{
	if (!page_index_valid(slot))
		return NULL;
	return heap_get_page(slot);
}

static struct page *mem_page(struct page *p, int m)
{
	if (!p || m < 0 || m >= p->nr_vars)
		return NULL;
	return slot_page(p->values[m].i);
}

static int mem_i(struct page *p, int m)
{
	if (!p || m < 0 || m >= p->nr_vars)
		return 0;
	return p->values[m].i;
}

static void mem_str(struct page *p, int m, char *dst, int n)
{
	struct string *s;
	char *u;
	dst[0] = 0;
	if (!p || m < 0 || m >= p->nr_vars || !string_index_valid(p->values[m].i))
		return;
	s = heap_get_string(p->values[m].i);
	if (!s || s->size <= 0)
		return;
	u = sjis2utf(s->text, (size_t)s->size);
	if (!u)
		return;
	clean_copy(dst, n, u);
	free(u);
}

static struct page *cg_imp(int handle)
{
	struct page *mgr, *list;
	if (handle < 0)
		return NULL;
	mgr = slot_page(global_get(2).i);
	list = mem_page(mgr, 0);
	if (!list || handle >= list->nr_vars)
		return NULL;
	return slot_page(list->values[handle].i);
}

struct sp_box {
	int ok;
	int show;
	int x, y, w, h;
	int sp;
};

static void metrics_imp(struct page *imp, struct sp_box *o, int show_over)
{
	if (!imp)
		return;
	o->ok = 1;
	o->show = show_over >= 0 ? show_over : (mem_i(imp, 5) ? 1 : 0);
	o->x = mem_i(imp, 1);
	o->y = mem_i(imp, 2);
	o->w = mem_i(imp, 6);
	o->h = mem_i(imp, 7);
	o->sp = mem_i(imp, 8);
}

static void versatile_box(struct page *sp, struct sp_box *o)
{
	int type, active;
	struct page *child, *list, *cg;
	memset(o, 0, sizeof *o);
	o->sp = -1;
	if (!sp || sp->type != STRUCT_PAGE)
		return;
	type = mem_i(sp, 0);
	if (type == 0) {
		child = mem_page(sp, 1);
		if (child)
			metrics_imp(cg_imp(mem_i(child, 0)), o, -1);
		return;
	}
	if (type == 2) {
		int show;
		child = mem_page(sp, 3);
		if (!child)
			return;
		show = mem_i(child, 2) ? 1 : 0;
		active = mem_i(child, 1);
		list = mem_page(child, 0);
		o->show = show;
		o->ok = 1;
		if (!list || active < 0 || active >= list->nr_vars)
			return;
		cg = slot_page(list->values[active].i);
		if (cg)
			metrics_imp(cg_imp(mem_i(cg, 0)), o, show);
	}
}

static int cas_rect(struct page *r, int *x, int *y, int *w, int *h)
{
	if (!r)
		return 0;
	*x = mem_i(r, 0);
	*y = mem_i(r, 1);
	*w = mem_i(r, 2);
	*h = mem_i(r, 3);
	return *w > 0 && *h > 0;
}

static int t_rect(struct page *r, int *x, int *y, int *w, int *h)
{
	int sx, sy, ex, ey;
	if (!r)
		return 0;
	sx = mem_i(r, 0);
	sy = mem_i(r, 1);
	ex = mem_i(r, 2);
	ey = mem_i(r, 3);
	*x = sx;
	*y = sy;
	*w = ex - sx;
	*h = ey - sy;
	return *w > 0 && *h > 0;
}

static struct ui_ctrl *ctrl_alloc(void)
{
	struct ui_ctrl *c;
	if (g_nctrls >= CTRL_MAX)
		return NULL;
	c = &g_ctrls[g_nctrls++];
	memset(c, 0, sizeof *c);
	c->sprite = -1;
	c->index = -1;
	c->parts_no = -1;
	c->enabled = 1;
	c->armed = 1;
	return c;
}

static void hover_from_rects(void)
{
	int i, best = -1, best_area = 0, mx, my, active;
	mouse_agent_pos(&mx, &my, &active);
	g_hover[0] = 0;
	if (!active)
		return;
	for (i = 0; i < g_nctrls; i++) {
		struct ui_ctrl *c = &g_ctrls[i];
		int area;
		if (!c->enabled || !c->armed || c->w <= 0 || c->h <= 0)
			continue;
		if (mx < c->x || my < c->y || mx >= c->x + c->w || my >= c->y + c->h)
			continue;
		area = c->w * c->h;
		if (best < 0 || area < best_area) {
			best = i;
			best_area = area;
		}
	}
	if (best >= 0)
		snprintf(g_hover, sizeof g_hover, "%s", g_ctrls[best].id);
}

static const char *title_button_name(int id, const char **label)
{
	*label = NULL;
	switch (id) {
	case 1: *label = "load"; return "load";
	case 2: *label = "restart"; return "restart";
	case 3: *label = "initialize"; return "initialize";
	case 910011:
		*label = "\xE7\x92\xB0\xE5\xA2\x83\xE8\xA8\xAD\xE5\xAE\x9A";
		return "config";
	case 910012: *label = "CG mode"; return "cg-mode";
	case 910013: *label = "music"; return "music";
	case 910014: *label = "web"; return "web";
	case 910092:
		*label = "\xE3\x82\xB2\xE3\x83\xBC\xE3\x83\xA0\xE7\xB5\x82\xE4\xBA\x86";
		return "quit";
	default:
		return NULL;
	}
}

static int button_hit(struct page *btn, int *x, int *y, int *w, int *h,
		int *spno, int *any_show)
{
	struct page *sprites;
	int s;
	*spno = -1;
	*any_show = 0;
	*w = 0;
	if (t_rect(mem_page(btn, 8), x, y, w, h) && *w > 4 && *h > 4)
		;
	else
		*w = 0;
	sprites = mem_page(btn, 0);
	if (!sprites || sprites->type != ARRAY_PAGE)
		return *w > 4 && *h > 4;
	for (s = 0; s < sprites->nr_vars && s < 4; s++) {
		struct page *bsp = slot_page(sprites->values[s].i);
		struct page *text, *tsprite;
		int sx, sy, sw, sh, no;
		if (!bsp)
			continue;
		if (mem_i(bsp, 11))
			*any_show = 1;
		if (*w <= 4 && t_rect(mem_page(bsp, 1), &sx, &sy, &sw, &sh) && sw > 4 && sh > 4) {
			*x = sx;
			*y = sy;
			*w = sw;
			*h = sh;
		}
		text = mem_page(bsp, 0);
		tsprite = mem_page(text, 0);
		no = mem_i(tsprite, 0);
		if (no > 0 && *spno < 0)
			*spno = no;
	}
	return *w > 4 && *h > 4;
}

static int fill_title(struct page *title)
{
	struct page *group, *arr;
	int i, n0;
	group = mem_page(title, 3);
	arr = mem_page(group, 0);
	if (!arr || arr->type != ARRAY_PAGE) {
		text_add("ui:no-button-array");
		return 0;
	}
	n0 = g_nctrls;
	for (i = 0; i < arr->nr_vars; i++) {
		struct page *btn = slot_page(arr->values[i].i);
		struct ui_ctrl *c;
		const char *label = NULL;
		const char *name;
		int id, spno, any_show, x, y, w, h;
		if (!btn || btn->type != STRUCT_PAGE || btn->index != sn_button)
			continue;
		id = mem_i(btn, 1);
		name = title_button_name(id, &label);
		if (!button_hit(btn, &x, &y, &w, &h, &spno, &any_show))
			continue;
		c = ctrl_alloc();
		if (!c)
			break;
		if (name)
			snprintf(c->id, sizeof c->id, "%s", name);
		else
			snprintf(c->id, sizeof c->id, "b%d", id);
		if (label)
			snprintf(c->label, sizeof c->label, "%s", label);
		snprintf(c->type, sizeof c->type, "button");
		c->button = id;
		c->key = mem_i(btn, 2);
		c->index = i;
		c->x = x;
		c->y = y;
		c->w = w;
		c->h = h;
		c->enabled = 1;
		c->sprite = spno;
		c->has_select = 1;
		c->selected = mem_i(btn, 5) ? 1 : 0;
		/* T_Button ignores input until shown, and a decide-key only
		 * clicks a button that is already selected. */
		if (!mem_i(btn, 4) || !any_show)
			c->hover_only = 1;
		c->armed = mem_i(btn, 4) && c->selected && !c->hover_only;
		text_add(c->label);
	}
	if (g_nctrls == n0) {
		char buf[64];
		snprintf(buf, sizeof buf, "ui:title-slots %d", arr->nr_vars);
		text_add(buf);
		return 0;
	}
	snprintf(g_screen, sizeof g_screen, "main-menu");
	hover_from_rects();
	return 1;
}

struct head_note {
	int y;
	char text[LABEL_LEN];
};

static void note_text_label(struct page *text, char *dst, int n, int *y)
{
	dst[0] = 0;
	if (y)
		*y = 0;
	if (!text || text->index != sn_text)
		return;
	mem_str(text, 4, dst, n);
	if (y)
		*y = mem_i(text, 7);
}

static void emit_checkbox(struct page *box, int index, const char *fallback)
{
	struct page *inner, *mark, *cursor;
	struct ui_ctrl *c;
	struct sp_box shown, cur;
	int x, y, w, h;
	char label[LABEL_LEN];
	if (!box)
		return;
	inner = box;
	if (box->index == sn_cbwrap)
		inner = mem_page(box, 0);
	if (!inner || inner->index != sn_cb)
		return;
	if (!cas_rect(mem_page(inner, 4), &x, &y, &w, &h))
		return;
	note_text_label(mem_page(inner, 3), label, (int)sizeof label, NULL);
	c = ctrl_alloc();
	if (!c)
		return;
	snprintf(c->id, sizeof c->id, "check-%d", index);
	if (label[0])
		snprintf(c->label, sizeof c->label, "%s", label);
	else if (fallback)
		snprintf(c->label, sizeof c->label, "%s", fallback);
	snprintf(c->type, sizeof c->type, "checkbox");
	c->index = index;
	c->x = x;
	c->y = y;
	c->w = w;
	c->h = h;
	mark = mem_page(inner, 1);
	versatile_box(mark, &shown);
	if (shown.ok)
		snprintf(c->value, sizeof c->value, "%s", shown.show ? "true" : "false");
	if (shown.sp > 0)
		c->sprite = shown.sp;
	cursor = mem_page(inner, 2);
	versatile_box(cursor, &cur);
	if (cur.ok && !cur.show)
		c->cursor_hover = 1;
	text_add(c->label);
}

static void emit_slider(struct page *sl, int index, const char *label, int vertical)
{
	struct ui_ctrl *c;
	struct sp_box box;
	int x, y, w, h, span, pos;
	double rate;
	if (!sl || !cas_rect(mem_page(sl, 1), &x, &y, &w, &h))
		return;
	versatile_box(mem_page(sl, 0), &box);
	c = ctrl_alloc();
	if (!c)
		return;
	snprintf(c->id, sizeof c->id, "slider-%d", index);
	if (label)
		snprintf(c->label, sizeof c->label, "%s", label);
	snprintf(c->type, sizeof c->type, "slider");
	c->index = index;
	c->x = x;
	c->y = y;
	c->w = w;
	c->h = h;
	if (box.sp > 0)
		c->sprite = box.sp;
	if (box.ok) {
		if (vertical) {
			span = h - box.h;
			pos = box.y - y;
		} else {
			span = w - box.w;
			pos = box.x - x;
		}
		if (span > 0) {
			rate = (double)pos / (double)span;
			if (rate < 0)
				rate = 0;
			if (rate > 1)
				rate = 1;
			snprintf(c->value, sizeof c->value, "%.2f", rate);
		} else {
			snprintf(c->value, sizeof c->value, "%d", vertical ? box.y : box.x);
		}
	}
	text_add(c->label);
}

static void emit_button_sprite(struct page *b, const char *id, const char *label, const char *type)
{
	struct ui_ctrl *c;
	struct sp_box box;
	if (!b)
		return;
	if (b->index == sn_btnsp)
		b = mem_page(b, 0);
	versatile_box(b, &box);
	if (!box.ok || box.w <= 0 || box.h <= 0)
		return;
	c = ctrl_alloc();
	if (!c)
		return;
	snprintf(c->id, sizeof c->id, "%s", id);
	if (label)
		snprintf(c->label, sizeof c->label, "%s", label);
	snprintf(c->type, sizeof c->type, "%s", type);
	c->x = box.x;
	c->y = box.y;
	c->w = box.w;
	c->h = box.h;
	c->sprite = box.sp;
	c->enabled = 1;
}

static void emit_tabs(struct page *viewtabs, char *active, int active_n)
{
	struct page *list;
	int i;
	active[0] = 0;
	if (!viewtabs || viewtabs->index != sn_tabview)
		return;
	list = mem_page(viewtabs, 1);
	if (!list || list->type != ARRAY_PAGE)
		return;
	for (i = 0; i < list->nr_vars; i++) {
		struct page *tab = slot_page(list->values[i].i);
		struct page *base;
		struct ui_ctrl *c;
		struct sp_box box, cur;
		char name[LABEL_LEN];
		int y = 0;
		if (!tab || tab->index != sn_tabbtn)
			continue;
		note_text_label(mem_page(tab, 3), name, (int)sizeof name, &y);
		base = mem_page(tab, 2);
		versatile_box(base, &box);
		c = ctrl_alloc();
		if (!c)
			continue;
		snprintf(c->id, sizeof c->id, "tab-%d", i);
		snprintf(c->label, sizeof c->label, "%s", name);
		snprintf(c->type, sizeof c->type, "tab");
		snprintf(c->section, sizeof c->section, "tabs");
		c->index = i;
		c->value[0] = 0;
		snprintf(c->value, sizeof c->value, "%s", mem_i(tab, 1) ? "true" : "false");
		if (box.ok && box.w > 0) {
			c->x = box.x;
			c->y = box.y;
			c->w = box.w;
			c->h = box.h;
			c->sprite = box.sp;
		}
		versatile_box(mem_page(tab, 5), &cur);
		if (cur.ok && !cur.show)
			c->cursor_hover = 1;
		if (mem_i(tab, 1) && name[0] && !active[0])
			snprintf(active, active_n, "%s", name);
		text_add(name);
	}
}

static void emit_exit(struct page *bar)
{
	if (!bar || bar->index != sn_exit)
		return;
	emit_button_sprite(mem_page(bar, 1), "close-game",
		"\xE3\x82\xB2\xE3\x83\xBC\xE3\x83\xA0\xE7\xB5\x82\xE4\xBA\x86", "button");
	emit_button_sprite(mem_page(bar, 2), "return-title",
		"\xE3\x82\xBF\xE3\x82\xA4\xE3\x83\x88\xE3\x83\xAB\xE3\x81\xB8\xE6\x88\xBB\xE3\x82\x8B", "button");
	emit_button_sprite(mem_page(bar, 3), "close", "X", "button");
}

static void apply_sections(const char *tab, struct head_note *heads, int nheads)
{
	int i, h;
	for (i = 0; i < g_nctrls; i++) {
		int best = -1;
		if (g_ctrls[i].section[0])
			continue;
		if (!strcmp(g_ctrls[i].type, "tab"))
			continue;
		for (h = 0; h < nheads; h++) {
			if (heads[h].y <= g_ctrls[i].y + 2) {
				if (best < 0 || heads[h].y >= heads[best].y)
					best = h;
			}
		}
		if (tab && tab[0] && best >= 0)
			snprintf(g_ctrls[i].section, sizeof g_ctrls[i].section, "%s / %s", tab, heads[best].text);
		else if (best >= 0)
			snprintf(g_ctrls[i].section, sizeof g_ctrls[i].section, "%s", heads[best].text);
		else if (tab)
			snprintf(g_ctrls[i].section, sizeof g_ctrls[i].section, "%s", tab);
	}
}

static int fill_config(struct page *view)
{
	struct head_note heads[24];
	char active[LABEL_LEN];
	char pend[LABEL_LEN];
	int nheads = 0, checks = 0, sliders = 0, i, pend_y = 0, pend_set = 0, pend_used = 0;
	int n0 = g_nctrls;
	active[0] = 0;
	pend[0] = 0;
	for (i = 0; i < view->nr_vars; i++) {
		struct page *child = mem_page(view, i);
		char label[LABEL_LEN];
		if (!child || child->type != STRUCT_PAGE)
			continue;
		if (child->index == sn_tabview) {
			emit_tabs(child, active, (int)sizeof active);
			continue;
		}
		if (child->index == sn_exit) {
			emit_exit(child);
			continue;
		}
		if (child->index == sn_text) {
			if (pend_set && !pend_used && nheads < 24) {
				heads[nheads].y = pend_y;
				snprintf(heads[nheads].text, sizeof heads[nheads].text, "%s", pend);
				nheads++;
			}
			note_text_label(child, pend, (int)sizeof pend, &pend_y);
			pend_set = pend[0] ? 1 : 0;
			pend_used = 0;
			continue;
		}
		if (child->index == sn_cb || child->index == sn_cbwrap) {
			emit_checkbox(child, checks++, NULL);
			continue;
		}
		if (child->index == sn_hslider || child->index == sn_vslider) {
			label[0] = 0;
			if (pend_set && !pend_used) {
				snprintf(label, sizeof label, "%s", pend);
				pend_used = 1;
			}
			emit_slider(child, sliders++, label, child->index == sn_vslider);
			continue;
		}
		if (child->index == sn_btnsp) {
			char bid[40];
			snprintf(bid, sizeof bid, "button-%d", g_nctrls);
			emit_button_sprite(child, bid, pend_set ? pend : "", "button");
			pend_used = 1;
		}
	}
	if (pend_set && !pend_used && nheads < 24) {
		heads[nheads].y = pend_y;
		snprintf(heads[nheads].text, sizeof heads[nheads].text, "%s", pend);
		nheads++;
	}
	if (g_nctrls == n0)
		return 0;
	apply_sections(active, heads, nheads);
	if (active[0])
		text_add(active);
	snprintf(g_screen, sizeof g_screen, "config");
	hover_from_rects();
	return 1;
}

static const char *cfg_check_label(int index)
{
	switch (index) {
	case 0: return "Do not start the screensaver during auto mode";
	case 1: return "Leave message mode when the button is clicked";
	case 2: return "Stop message skip when a choice menu opens";
	case 3: return "Stop auto mode when a choice menu opens";
	case 4: return "Skip frames while message skip is running";
	case 5: return "Mouse wheel direction (stored flag 0)";
	case 6: return "Mouse wheel direction (stored flag 1)";
	case 7: return "Show the sample message";
	default: return "Checkbox";
	}
}

static int cfg_check_on(int index)
{
	int g = 0;
	switch (index) {
	case 0: g = 133; break;
	case 1: g = 130; break;
	case 2: g = 143; break;
	case 3: g = 144; break;
	case 4: g = 129; break;
	case 5: return global_get(138).i == 0;
	case 6: return global_get(138).i == 1;
	case 7: g = 128; break;
	default: return 0;
	}
	return global_get(g).i != 0;
}

static const char *cfg_slider_label(int index)
{
	switch (index) {
	case 0: return "Message speed";
	case 1: return "Auto mode wait";
	case 2: return "Skip wait for text you have already read";
	case 3: return "Skip wait for text you have not read";
	default: return "Slider";
	}
}

static void describe_cfg_button(struct ui_ctrl *c, int id, struct page *sliders)
{
	struct page *sl;
	if (id >= 100001 && id <= 100008) {
		static const char *tabs[] = {
			"message", "sound", "window", "3d", "confirm", "other", "other-2", "debug"
		};
		snprintf(c->id, sizeof c->id, "tab-%s", tabs[id - 100001]);
		snprintf(c->label, sizeof c->label, "%s", tabs[id - 100001]);
		snprintf(c->type, sizeof c->type, "tab");
		snprintf(c->section, sizeof c->section, "tabs");
		snprintf(c->value, sizeof c->value, "%s", id == 100001 ? "true" : "false");
		return;
	}
	if (id == 100011) {
		snprintf(c->id, sizeof c->id, "close");
		snprintf(c->label, sizeof c->label, "X");
		snprintf(c->type, sizeof c->type, "button");
		snprintf(c->section, sizeof c->section, "window");
		return;
	}
	if (id == 100019) {
		snprintf(c->id, sizeof c->id, "cancel");
		snprintf(c->label, sizeof c->label, "Back");
		snprintf(c->type, sizeof c->type, "button");
		snprintf(c->section, sizeof c->section, "window");
		return;
	}
	if (id == 100012) {
		snprintf(c->id, sizeof c->id, "return-title");
		snprintf(c->label, sizeof c->label, "Return to title");
		snprintf(c->type, sizeof c->type, "button");
		snprintf(c->section, sizeof c->section, "window");
		return;
	}
	if (id == 100013) {
		snprintf(c->id, sizeof c->id, "quit");
		snprintf(c->label, sizeof c->label, "Quit game");
		snprintf(c->type, sizeof c->type, "button");
		snprintf(c->section, sizeof c->section, "window");
		return;
	}
	if (id >= 220001 && id <= 220008) {
		int n = id - 220001;
		snprintf(c->id, sizeof c->id, "check-%d", n);
		snprintf(c->label, sizeof c->label, "%s", cfg_check_label(n));
		snprintf(c->type, sizeof c->type, "checkbox");
		snprintf(c->section, sizeof c->section, "message");
		snprintf(c->value, sizeof c->value, "%s", cfg_check_on(n) ? "true" : "false");
		return;
	}
	if (id >= 230001 && id <= 230004) {
		int n = id - 230001;
		float rate = 0;
		sl = NULL;
		if (sliders && sliders->type == ARRAY_PAGE && n < sliders->nr_vars)
			sl = slot_page(sliders->values[n].i);
		if (sl && sl->nr_vars > 5)
			rate = sl->values[5].f;
		snprintf(c->id, sizeof c->id, "slider-%d", n);
		snprintf(c->label, sizeof c->label, "%s", cfg_slider_label(n));
		snprintf(c->type, sizeof c->type, "slider");
		snprintf(c->section, sizeof c->section, "message");
		snprintf(c->value, sizeof c->value, "%.2f", rate);
		return;
	}
	if (id >= 210001 && id < 220000) {
		snprintf(c->id, sizeof c->id, "reset-%d", id - 210001);
		snprintf(c->label, sizeof c->label, "Reset");
		snprintf(c->type, sizeof c->type, "button");
		snprintf(c->section, sizeof c->section, "message");
		return;
	}
	snprintf(c->id, sizeof c->id, "b%d", id);
	snprintf(c->type, sizeof c->type, "button");
}

static int fill_cfgmsg(struct page *msg)
{
	struct page *group, *arr, *sliders;
	int i, n0;
	group = mem_page(msg, 1);
	arr = mem_page(group, 0);
	sliders = mem_page(msg, 2);
	if (!arr || arr->type != ARRAY_PAGE)
		return 0;
	n0 = g_nctrls;
	text_add("message");
	for (i = 0; i < arr->nr_vars; i++) {
		struct page *btn = slot_page(arr->values[i].i);
		struct ui_ctrl *c;
		int id, spno, any_show, x, y, w, h;
		if (!btn || btn->type != STRUCT_PAGE || btn->index != sn_button)
			continue;
		if (!button_hit(btn, &x, &y, &w, &h, &spno, &any_show))
			continue;
		id = mem_i(btn, 1);
		c = ctrl_alloc();
		if (!c)
			break;
		describe_cfg_button(c, id, sliders);
		c->button = id;
		c->index = i;
		c->x = x;
		c->y = y;
		c->w = w;
		c->h = h;
		c->enabled = 1;
		c->sprite = spno;
		c->has_select = 1;
		c->selected = mem_i(btn, 5) ? 1 : 0;
		if (!mem_i(btn, 4) || !any_show)
			c->hover_only = 1;
		c->armed = mem_i(btn, 4) && c->selected && !c->hover_only;
		if (c->label[0])
			text_add(c->label);
	}
	if (g_nctrls == n0)
		return 0;
	snprintf(g_screen, sizeof g_screen, "config");
	hover_from_rects();
	return 1;
}

static int vm_fill(void)
{
	struct page *view = NULL, *title = NULL, *cfg = NULL;
	int i, view_at = 99, title_at = 99, cfg_at = 99;
	snos_init();
	if (!ain)
		return 0;
	for (i = 0; i < 24; i++) {
		struct page *p = get_struct_page(i);
		if (!p || p->type != STRUCT_PAGE)
			continue;
		if (is_config_view(p->index) && i < view_at) {
			view = p;
			view_at = i;
		}
		if (p->index == sn_cfgmsg && i < cfg_at) {
			cfg = p;
			cfg_at = i;
		}
		if (p->index == sn_title && i < title_at) {
			title = p;
			title_at = i;
		}
	}
	if (cfg)
		return fill_cfgmsg(cfg);
	if (view)
		return fill_config(view);
	if (title && !stack_has(U8_WARN))
		return fill_title(title);
	if (!view && !title && stack_has("T_Title"))
		text_add("ui:title-on-stack-no-page");
	return 0;
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
	if (!vm_fill()) {
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
		c->sprite = -1;
		c->index = -1;
		c->armed = 1;
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
	}
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
	long long start;
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
	/* Title buttons arm on mouse-up only after a frame where the
	 * decide key was down while the button was already selected.
	 * Hold each edge across a presented frame. */
	/* キー_決定 is left-click or Enter, and the click lands on release. */
	wants(1);
	key_set_state(VK_LBUTTON, 0);
	key_set_state(VK_RETURN, 0);
	start = rd_ll(&g_present_gen);
	ok = wait_published_after(start, FRAME_MS);
	if (ok && !hover_is(id))
		ok = 0;
	if (ok) {
		key_set_state(VK_LBUTTON, 1);
		key_set_state(VK_RETURN, 1);
		start = rd_ll(&g_present_gen);
		ok = wait_published_after(start, FRAME_MS);
	}
	key_set_state(VK_LBUTTON, 0);
	key_set_state(VK_RETURN, 0);
	if (ok) {
		start = rd_ll(&g_present_gen);
		ok = wait_published_after(start, FRAME_MS);
	}
	wants(0);
	if (!ok) {
		if (err)
			snprintf(err, errsz, "timeout");
		return 0;
	}
	return 1;
}

#endif /* RQ_MODLOADER */
