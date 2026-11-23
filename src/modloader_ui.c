/* Screen dump for rqmod-ctl. Reads live PartsEngine widgets on the game
 * thread at gfx_swap. Lua does not own this; the title menus exist first.
 */

#ifdef RQ_MODLOADER

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>

#include "system4.h"
#include "system4/ain.h"
#include "system4/archive.h"
#include "system4/cg.h"
#include "system4/file.h"
#include "system4/string.h"
#include "system4/utfsjis.h"

#include "lua.h"
#include "lauxlib.h"

#include "cJSON.h"
#include "gfx/gfx.h"
#include "input.h"
#include "mixer.h"
#include "sact.h"
#include "scene.h"
#include "sprite.h"
#include "vm.h"
#include "vm/heap.h"
#include "vm/page.h"
#include "xsystem4.h"
#include "asset_manager.h"
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

#define CTRL_MAX 256
#define TEXT_MAX 48
#define TEXT_LEN 200
#define LABEL_LEN 280
#define JSON_MAX (320 * 1024)
#define UNIT_MAX 128
#define SKILL_MAX 40
#define NOTE_N 48
#define NOTE_LEN 700
#define FRAME_MS 4000
#define PRESS_FRAMES 3

struct ui_ctrl {
	int parts_no;
	char id[40];
	char label[LABEL_LEN];
	char type[16];
	char section[96];
	char value[160];
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
	int has_visible;
	int visible;
	int covered; /* a game control under a mod's kit widget or a captured mouse (2.11.0) */
	int focused; /* a kit widget under the kit's keys cursor (2.12.0) */
};

struct party_row {
	int slot;
	int chara;
	int job;
	int level;
	int level_max;
	int no_limit;
	int hp;
	int hp_max;
	int back_row;
	char name[80];
};

struct hub_info {
	int ok;
	int world;
	int money;
	int rank;
	int points;
	int points_max;
	int donations;
	int trivia_new;
};

struct unit_row {
	int index;
	int chara;
	int job;
	int level;
	int level_max;
	int no_limit;
	int visible;
	char name[80];
};

struct skill_row {
	int id;
	int rank;
	int owned;
	int active;
	int cost;
	char name[80];
};

struct status_dump {
	int ok;
	int chara;
	int job;
	int level;
	int level_max;
	int no_limit;
	int exp;
	int exp_max;
	int hp;
	int hp_max;
	int atk;
	int def;
	int mag;
	int mdef;
	int hit;
	int eva;
	int crit;
	int impact;
	int resist;
	int spd;
	int points;
	int equip[4];
	int n_active;
	int n_learned;
	struct skill_row active[6];
	struct skill_row learned[SKILL_MAX];
	int active_at[6];
	int known_at[18];
	char name[80];
	char job_name[40];
};

struct party_state {
	int ok;
	int layout; /* 0=10, 1=32 */
	int sort_mode;
	int job_type;
	int filter_on;
	int open_flag;
	int top_index;
	int list_count;
};

#define QUEST_MAX 400

struct quest_row {
	int index;
	int quest_no;
	int visible;
	char name[80];
};

struct quest_state {
	int ok;
	int kind; /* 1=list, 2=storage */
	int top;
	int count;
	int sort_mode;
	int selected;
	int mode;
};

struct dungeon_state {
	int ok;
	int area;
	int map_no;
	int map_x;
	int map_z;
	char name[80];
};

#define BATTLE_UNIT_MAX 10
#define BATTLE_SKILL_MAX 48

struct battle_unit {
	int no;
	int chara;
	int enemy;
	int x, y, w, h;
	char name[80];
};

struct battle_skill {
	int button;
	int skill;
	int uses;
	int panel;
	int target;
	int index;
	int enabled;
	int visible;
	int x, y, w, h;
	char name[80];
	char tone[8];
};

struct battle_state {
	int ok;
	int selecting;
	int auto_flag;
	int skill_mode;
	int actor_no;
	int actor_chara;
	char actor_name[80];
	int n_units;
	int n_skills;
	struct battle_unit units[BATTLE_UNIT_MAX];
	struct battle_skill skills[BATTLE_SKILL_MAX];
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
/* rq.ui.shot. Only the VM thread reads these, so a pending one never clashes with the pipe. */
static int g_lua_shot_wanted;
static int g_lua_shot_result;
static char g_lua_shot_path[512];
/* rq.ui.shot's window (2.67.0): the window as shown, taken before the swap. */
static int g_lua_shot_window;
/* rq.ui.get or aim built the list during this update. */
static int g_lua_built;
/* A named screen was seen, so an empty screen is no longer the boot. */
static int g_seen_screen;
static atomic_llong g_present_gen;
static atomic_llong g_published;
static atomic_llong g_input_gen;
static struct ui_snap g_snap;

static char g_blob[12000];
static char g_texts[TEXT_MAX][TEXT_LEN];
static int g_ntexts;
static struct ui_ctrl g_ctrls[CTRL_MAX];
static int g_nctrls;
static void mark_covered(void);
static int g_hover_no;
static char g_screen[24];
static char g_hover[40];
static int g_has_skillshop, g_skill_page, g_skill_pages, g_skill_points;
static struct party_row g_party[5];
static int g_nparty;
static int g_has_party;
static struct hub_info g_info;
static struct unit_row g_units[UNIT_MAX];
static int g_nunits;
static int g_has_units;
static struct party_state g_pstate;
/* The Party screen's 2nd Army box (g_sPartyStore), while it is open (2.31.0). */
static struct unit_row g_store[UNIT_MAX];
static int g_nstore, g_has_store;
static struct { int ok, top_index, list_count, shown; } g_sstate;
static struct status_dump g_status;
static struct quest_row g_quests[QUEST_MAX];
static int g_nquests;
static struct quest_state g_qstate;
static struct dungeon_state g_dstate;
static struct battle_state g_bstate;

#define VAULT_ITEM_MAX 256
#define VAULT_VISIBLE 6

struct vault_item {
	int index;
	int item_no;
	int locked;
	int visible;
	char name[80];
};

struct vault_state {
	int ok;
	int store_type;
	int item_type;
	int item_type_ex;
	int job_type;
	int equip_type;
	int lock_type;
	int sort_mode;
	int top_index;
	int list_count;
	int stock_count;
	int stock_top;
	int open_flag;
	int donation_flag;
	int sell_flag;
	int inv_count;
	int stock_num;
};

static struct vault_item g_vitems[VAULT_ITEM_MAX];
static int g_nvitems;
static int g_has_vitems;
static struct vault_item g_vstock[VAULT_ITEM_MAX];
static int g_nvstock;
static int g_has_vstock;
static struct vault_state g_vstate;

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
	return atomic_load(&g_wants) > 0 || g_lua_shot_wanted;
}

static void service_cgdump(void);
static void service_flashdump(void);

/* cgdump is served here too (2.5.0): a screen that draws no frame, such as the
 * disclaimer, still runs updates, and the pipe would wait until its timeout. */
void rq_ui_on_update(void)
{
	g_lua_built = 0;
	service_cgdump();
	service_flashdump();
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

static char g_stackblob[4096];

static void collect_stack(void)
{
	int i, len = 0;
	g_stackblob[0] = 0;
	for (i = 0; i < 40; i++) {
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

/* What the game runs under the screen, `busy` in rq.ui.get (2.56.0): one of a quest's own events
 * (クエスト処理: its messages, scenes and fight, with the map read between them) or the wait for a
 * click after a fight (戦闘後処理, over the map). The inner one when both are on the VM's stack;
 * empty when neither is, as on an idle map. */
static char g_busy[16];

static int busy_fno(const char *utf8)
{
	char *sjis = utf2sjis(utf8, 0);
	int fno;
	if (!sjis)
		return -1;
	fno = ain_get_function(ain, sjis);
	free(sjis);
	return fno;
}

/* vm_frame_name gives the function's own name pointer, so a frame is matched by that pointer. */
static void find_busy(void)
{
	static int looked = 0;
	static const char *event, *fight_end;
	const char *name;
	int i;
	g_busy[0] = 0;
	if (!ain)
		return;
	if (!looked) {
		int fe = busy_fno("クエスト処理"), ff = busy_fno("戦闘後処理");
		looked = 1;
		event = fe >= 0 ? ain->functions[fe].name : NULL;
		fight_end = ff >= 0 ? ain->functions[ff].name : NULL;
	}
	if (!event && !fight_end)
		return;
	for (i = 0; i < 4096 && (name = vm_frame_name(i)) != NULL; i++) {
		if (fight_end && name == fight_end) {
			snprintf(g_busy, sizeof g_busy, "fight-end");
			return;
		}
		if (event && name == event) {
			snprintf(g_busy, sizeof g_busy, "event");
			return;
		}
	}
}

/* Trivia, CG mode, Music mode and the castle's Gal monsters' and Poster rooms run inside the
 * town's or the castle's loop, over its screen. They are `other`, with the footer, not the
 * screen under them (2.22.1). */
static int inner_screen(void)
{
	return stack_has("T_Knowledge@") || stack_has("T_CgMode@") || stack_has("T_MusicMode@")
		|| stack_has("T_GirlsRoom@") || stack_has("T_PosterRoom@");
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
	else if (g_nctrls == 0 && text_len >= 60)
		snprintf(g_screen, sizeof g_screen, "disclaimer");
	else if (g_nctrls == 0 && !g_seen_screen)
		snprintf(g_screen, sizeof g_screen, "boot");
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

/* ï¼£è·æ¥­æ–‡å­—åˆ—(ii, 0) */
static const char *job_name(int job)
{
	static const char *names[] = {
		"-----", "Fighter", "Guard", "Gunner", "Sorcerer",
		"Healer", "Ranger", "Brawler", "Civilian"
	};
	return job >= 1 && job <= 8 ? names[job] : names[0];
}

static void add_party_json(cJSON *root)
{
	cJSON *arr = cJSON_AddArrayToObject(root, "party");
	int i;
	for (i = 0; i < g_nparty; i++) {
		struct party_row *r = &g_party[i];
		cJSON *o = cJSON_CreateObject();
		cJSON_AddNumberToObject(o, "slot", r->slot);
		cJSON_AddStringToObject(o, "row", r->back_row ? "back" : "front");
		cJSON_AddNumberToObject(o, "chara", r->chara);
		cJSON_AddStringToObject(o, "name", r->name);
		cJSON_AddStringToObject(o, "class", job_name(r->job));
		cJSON_AddNumberToObject(o, "classId", r->job);
		cJSON_AddNumberToObject(o, "level", r->level);
		if (r->no_limit)
			cJSON_AddNullToObject(o, "levelLimit");
		else
			cJSON_AddNumberToObject(o, "levelLimit", r->level_max);
		cJSON_AddBoolToObject(o, "noLimit", r->no_limit ? 1 : 0);
		cJSON_AddNumberToObject(o, "hp", r->hp);
		cJSON_AddNumberToObject(o, "hpMax", r->hp_max);
		cJSON_AddItemToArray(arr, o);
	}
}

static void add_info_json(cJSON *root)
{
	cJSON *o = cJSON_AddObjectToObject(root, "info");
	cJSON *ch;
	cJSON_AddStringToObject(o, "title", "Rance VIII");
	cJSON_AddNumberToObject(o, "world", g_info.world);
	cJSON_AddNumberToObject(o, "money", g_info.money);
	ch = cJSON_AddObjectToObject(o, "charisma");
	cJSON_AddNumberToObject(ch, "level", g_info.rank);
	cJSON_AddNumberToObject(ch, "points", g_info.points);
	cJSON_AddNumberToObject(ch, "max", g_info.points_max);
	cJSON_AddNumberToObject(o, "donations", g_info.donations);
	cJSON_AddBoolToObject(o, "triviaNew", g_info.trivia_new ? 1 : 0);
}

static void add_unit_rows_json(cJSON *root, const char *key, const struct unit_row *rows, int n)
{
	cJSON *arr = cJSON_AddArrayToObject(root, key);
	int i;
	for (i = 0; i < n; i++) {
		const struct unit_row *r = &rows[i];
		cJSON *o = cJSON_CreateObject();
		cJSON_AddNumberToObject(o, "index", r->index);
		cJSON_AddNumberToObject(o, "chara", r->chara);
		cJSON_AddStringToObject(o, "name", r->name);
		cJSON_AddStringToObject(o, "class", job_name(r->job));
		cJSON_AddNumberToObject(o, "classId", r->job);
		cJSON_AddNumberToObject(o, "level", r->level);
		if (r->no_limit)
			cJSON_AddNullToObject(o, "levelLimit");
		else
			cJSON_AddNumberToObject(o, "levelLimit", r->level_max);
		cJSON_AddBoolToObject(o, "noLimit", r->no_limit ? 1 : 0);
		cJSON_AddBoolToObject(o, "visible", r->visible ? 1 : 0);
		cJSON_AddItemToArray(arr, o);
	}
}

static void add_store_state_json(cJSON *root)
{
	cJSON *o = cJSON_AddObjectToObject(root, "storeState");
	cJSON_AddNumberToObject(o, "topIndex", g_sstate.top_index);
	cJSON_AddNumberToObject(o, "listCount", g_sstate.list_count);
	cJSON_AddNumberToObject(o, "shown", g_sstate.shown);
}

static void add_party_state_json(cJSON *root)
{
	cJSON *o = cJSON_AddObjectToObject(root, "partyState");
	cJSON_AddNumberToObject(o, "tableSize", g_pstate.layout == 1 ? 32 : 10);
	cJSON_AddNumberToObject(o, "layout", g_pstate.layout);
	cJSON_AddNumberToObject(o, "sortMode", g_pstate.sort_mode);
	cJSON_AddNumberToObject(o, "jobType", g_pstate.job_type);
	cJSON_AddBoolToObject(o, "filterOn", g_pstate.filter_on ? 1 : 0);
	cJSON_AddBoolToObject(o, "openFlag", g_pstate.open_flag ? 1 : 0);
	cJSON_AddNumberToObject(o, "topIndex", g_pstate.top_index);
	cJSON_AddNumberToObject(o, "listCount", g_pstate.list_count);
}

static void add_quest_json(cJSON *root)
{
	cJSON *o, *arr;
	int i;
	if (!g_qstate.ok)
		return;
	if (g_qstate.kind == 2) {
		o = cJSON_AddObjectToObject(root, "storage");
		cJSON_AddNumberToObject(o, "mode", g_qstate.mode);
		return;
	}
	o = cJSON_AddObjectToObject(root, "questState");
	cJSON_AddNumberToObject(o, "topIndex", g_qstate.top);
	cJSON_AddNumberToObject(o, "listCount", g_qstate.count);
	cJSON_AddNumberToObject(o, "sortMode", g_qstate.sort_mode);
	cJSON_AddNumberToObject(o, "selected", g_qstate.selected);
	arr = cJSON_AddArrayToObject(root, "quests");
	for (i = 0; i < g_nquests; i++) {
		cJSON *q = cJSON_CreateObject();
		cJSON_AddNumberToObject(q, "index", g_quests[i].index);
		cJSON_AddNumberToObject(q, "questNo", g_quests[i].quest_no);
		cJSON_AddStringToObject(q, "name", g_quests[i].name);
		cJSON_AddBoolToObject(q, "visible", g_quests[i].visible ? 1 : 0);
		cJSON_AddItemToArray(arr, q);
	}
}

static void add_dungeon_json(cJSON *root)
{
	cJSON *o;
	if (!g_dstate.ok)
		return;
	o = cJSON_AddObjectToObject(root, "dungeon");
	cJSON_AddBoolToObject(o, "areaShown", g_dstate.area ? 1 : 0);
	cJSON_AddNumberToObject(o, "mapNo", g_dstate.map_no);
	cJSON_AddNumberToObject(o, "mapX", g_dstate.map_x);
	cJSON_AddNumberToObject(o, "mapZ", g_dstate.map_z);
	cJSON_AddStringToObject(o, "name", g_dstate.name);
}

static void add_battle_json(cJSON *root)
{
	cJSON *o, *actor, *units, *skills;
	int i;
	if (!g_bstate.ok)
		return;
	o = cJSON_AddObjectToObject(root, "battle");
	cJSON_AddStringToObject(o, "phase", g_bstate.selecting ? "select" : "wait");
	cJSON_AddBoolToObject(o, "auto", g_bstate.auto_flag ? 1 : 0);
	cJSON_AddNumberToObject(o, "skillMode", g_bstate.skill_mode);
	actor = cJSON_AddObjectToObject(o, "actor");
	if (g_bstate.actor_no > 0) {
		cJSON_AddNumberToObject(actor, "no", g_bstate.actor_no);
		cJSON_AddNumberToObject(actor, "chara", g_bstate.actor_chara);
		cJSON_AddStringToObject(actor, "name", g_bstate.actor_name);
	} else {
		cJSON_AddNullToObject(actor, "no");
		cJSON_AddNullToObject(actor, "chara");
		cJSON_AddStringToObject(actor, "name", "");
	}
	units = cJSON_AddArrayToObject(o, "units");
	for (i = 0; i < g_bstate.n_units; i++) {
		cJSON *u = cJSON_CreateObject();
		struct battle_unit *r = &g_bstate.units[i];
		cJSON_AddNumberToObject(u, "no", r->no);
		cJSON_AddNumberToObject(u, "chara", r->chara);
		cJSON_AddStringToObject(u, "side", r->enemy ? "enemy" : "ally");
		cJSON_AddStringToObject(u, "name", r->name);
		cJSON_AddNumberToObject(u, "x", r->x);
		cJSON_AddNumberToObject(u, "y", r->y);
		cJSON_AddNumberToObject(u, "w", r->w);
		cJSON_AddNumberToObject(u, "h", r->h);
		cJSON_AddItemToArray(units, u);
	}
	skills = cJSON_AddArrayToObject(o, "skills");
	for (i = 0; i < g_bstate.n_skills; i++) {
		cJSON *s = cJSON_CreateObject();
		struct battle_skill *r = &g_bstate.skills[i];
		char id[40];
		snprintf(id, sizeof id, "skill-%d", r->button);
		cJSON_AddStringToObject(s, "id", id);
		cJSON_AddNumberToObject(s, "button", r->button);
		cJSON_AddNumberToObject(s, "skill", r->skill);
		cJSON_AddStringToObject(s, "name", r->name);
		cJSON_AddNumberToObject(s, "uses", r->uses);
		cJSON_AddNumberToObject(s, "panel", r->panel);
		cJSON_AddStringToObject(s, "tone", r->tone);
		cJSON_AddNumberToObject(s, "target", r->target);
		cJSON_AddNumberToObject(s, "index", r->index);
		cJSON_AddBoolToObject(s, "enabled", r->enabled ? 1 : 0);
		cJSON_AddBoolToObject(s, "visible", r->visible ? 1 : 0);
		cJSON_AddNumberToObject(s, "x", r->x);
		cJSON_AddNumberToObject(s, "y", r->y);
		cJSON_AddNumberToObject(s, "w", r->w);
		cJSON_AddNumberToObject(s, "h", r->h);
		cJSON_AddItemToArray(skills, s);
	}
}

static void add_skill_array(cJSON *parent, const char *key, struct skill_row *rows, int n)
{
	cJSON *arr = cJSON_AddArrayToObject(parent, key);
	int i;
	for (i = 0; i < n; i++) {
		cJSON *o = cJSON_CreateObject();
		cJSON_AddNumberToObject(o, "id", rows[i].id);
		cJSON_AddStringToObject(o, "name", rows[i].name);
		cJSON_AddNumberToObject(o, "rank", rows[i].rank);
		cJSON_AddNumberToObject(o, "cost", rows[i].cost);
		cJSON_AddBoolToObject(o, "owned", rows[i].owned ? 1 : 0);
		cJSON_AddBoolToObject(o, "active", rows[i].active ? 1 : 0);
		cJSON_AddItemToArray(arr, o);
	}
}

static void add_vault_json(cJSON *root);

/* The choice menu's text rows, one line each (rq.ui.get's select.message, 2.19.0). */
static char g_select_msg[2048];

static int module_int(const char *module, const char *name);

/* rq.knownskills' page of the known skills and the count (2.54.0), once a mod installed it. */
static void add_known_pages(cJSON *o)
{
	int n = module_int("knownskills", "page_count");
	if (n <= 0)
		return;
	cJSON_AddNumberToObject(o, "knownPage", module_int("knownskills", "page"));
	cJSON_AddNumberToObject(o, "knownPages", n);
}

static void add_status_json(cJSON *root)
{
	cJSON *o, *eq, *st;
	int i;
	if (!g_status.ok)
		return;
	o = cJSON_AddObjectToObject(root, "status");
	cJSON_AddNumberToObject(o, "chara", g_status.chara);
	cJSON_AddStringToObject(o, "name", g_status.name);
	cJSON_AddStringToObject(o, "class", g_status.job_name[0] ? g_status.job_name : job_name(g_status.job));
	cJSON_AddNumberToObject(o, "classId", g_status.job);
	cJSON_AddNumberToObject(o, "level", g_status.level);
	if (g_status.no_limit)
		cJSON_AddNullToObject(o, "levelLimit");
	else
		cJSON_AddNumberToObject(o, "levelLimit", g_status.level_max);
	cJSON_AddBoolToObject(o, "noLimit", g_status.no_limit ? 1 : 0);
	cJSON_AddNumberToObject(o, "exp", g_status.exp);
	cJSON_AddNumberToObject(o, "expMax", g_status.exp_max);
	cJSON_AddNumberToObject(o, "storedPoints", g_status.points);
	add_known_pages(o);
	st = cJSON_AddObjectToObject(o, "stats");
	cJSON_AddNumberToObject(st, "hp", g_status.hp);
	cJSON_AddNumberToObject(st, "hpMax", g_status.hp_max);
	cJSON_AddNumberToObject(st, "atk", g_status.atk);
	cJSON_AddNumberToObject(st, "def", g_status.def);
	cJSON_AddNumberToObject(st, "mag", g_status.mag);
	cJSON_AddNumberToObject(st, "mdef", g_status.mdef);
	cJSON_AddNumberToObject(st, "hit", g_status.hit);
	cJSON_AddNumberToObject(st, "eva", g_status.eva);
	cJSON_AddNumberToObject(st, "crit", g_status.crit);
	cJSON_AddNumberToObject(st, "impact", g_status.impact);
	cJSON_AddNumberToObject(st, "resist", g_status.resist);
	cJSON_AddNumberToObject(st, "spd", g_status.spd);
	eq = cJSON_AddObjectToObject(o, "equipment");
	cJSON_AddNumberToObject(eq, "special", g_status.equip[0]);
	cJSON_AddNumberToObject(eq, "option", g_status.equip[0]); /* alias: Special slot */
	cJSON_AddNumberToObject(eq, "weapon", g_status.equip[1]);
	cJSON_AddNumberToObject(eq, "armor", g_status.equip[2]);
	cJSON_AddNumberToObject(eq, "item", g_status.equip[3]);
	add_skill_array(o, "activeSkills", g_status.active, g_status.n_active);
	add_skill_array(o, "learnedSkills", g_status.learned, g_status.n_learned);
	(void)i;
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
		if (g_ctrls[i].has_visible)
			cJSON_AddBoolToObject(c, "visible", g_ctrls[i].visible ? 1 : 0);
		if (g_ctrls[i].has_select)
			cJSON_AddBoolToObject(c, "selected", g_ctrls[i].selected ? 1 : 0);
		if (g_ctrls[i].hover_only)
			cJSON_AddBoolToObject(c, "hoverOnly", 1);
		if (g_ctrls[i].covered)
			cJSON_AddBoolToObject(c, "covered", 1);
		if (g_ctrls[i].focused)
			cJSON_AddBoolToObject(c, "focused", 1);
		if (g_ctrls[i].cursor_hover)
			cJSON_AddStringToObject(c, "cursor", "hover-only");
		cJSON_AddItemToArray(controls, c);
	}
	if (g_hover[0])
		cJSON_AddStringToObject(root, "hover", g_hover);
	else
		cJSON_AddNullToObject(root, "hover");
	if (g_busy[0])
		cJSON_AddStringToObject(root, "busy", g_busy);
	if (g_has_party)
		add_party_json(root);
	if (g_has_units)
		add_unit_rows_json(root, "units", g_units, g_nunits);
	if (g_pstate.ok)
		add_party_state_json(root);
	if (g_has_store)
		add_unit_rows_json(root, "storeUnits", g_store, g_nstore);
	if (g_sstate.ok)
		add_store_state_json(root);
	if (g_qstate.ok)
		add_quest_json(root);
	if (g_dstate.ok)
		add_dungeon_json(root);
	if (g_bstate.ok)
		add_battle_json(root);
	if (g_status.ok)
		add_status_json(root);
	if (g_vstate.ok)
		add_vault_json(root);
	if (g_info.ok)
		add_info_json(root);
	if (!strcmp(g_screen, "select")) {
		cJSON *se = cJSON_AddObjectToObject(root, "select");
		cJSON_AddStringToObject(se, "message", g_select_msg);
	}
	if (g_has_skillshop) {
		cJSON *sk = cJSON_AddObjectToObject(root, "skillShop");
		cJSON_AddNumberToObject(sk, "page", g_skill_page);
		cJSON_AddNumberToObject(sk, "pages", g_skill_pages);
		cJSON_AddNumberToObject(sk, "storedPoints", g_skill_points);
		add_known_pages(sk);
	}
	{
		struct rq_debug_view dv;
		if (rq_debug_snapshot(&dv)) {
			cJSON *dbg = cJSON_AddObjectToObject(root, "debug");
			cJSON *arr = cJSON_AddArrayToObject(dbg, "actions");
			int k;
			cJSON_AddStringToObject(dbg, "page", "Main");
			for (k = 0; k < dv.nactions; k++) {
				cJSON *a = cJSON_CreateObject();
				cJSON_AddStringToObject(a, "id", dv.actions[k].id);
				cJSON_AddStringToObject(a, "key", dv.actions[k].key);
				cJSON_AddStringToObject(a, "label", dv.actions[k].label);
				cJSON_AddStringToObject(a, "page", dv.actions[k].page);
				cJSON_AddBoolToObject(a, "on", dv.actions[k].on ? 1 : 0);
				cJSON_AddItemToArray(arr, a);
			}
			if (dv.stats) {
				cJSON *ovs = cJSON_AddArrayToObject(dbg, "overlays");
				cJSON_AddNumberToObject(dbg, "fps", dv.fps);
				cJSON_AddNumberToObject(dbg, "memoryMb", dv.memory_mb);
				cJSON_AddNumberToObject(dbg, "avgFps", dv.avg_fps);
				cJSON_AddNumberToObject(dbg, "vpf", dv.vpf);
				cJSON_AddStringToObject(dbg, "screen", dv.screen[0] ? dv.screen : "other");
				for (k = 0; k < dv.nov; k++)
					cJSON_AddItemToArray(ovs, cJSON_CreateString(dv.overlays[k]));
			}
		}
	}
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
static int sn_cb, sn_cbwrap, sn_hslider, sn_vslider, sn_text, sn_btnsp;
static int sn_cfgmsg, sn_cfgsnd, sn_cfgwin, sn_cfg3d, sn_cfgcfm, sn_cfgetc, sn_cfgetc2;
static int sn_town, sn_sysbar, sn_sysmenu, sn_mini, sn_sysinfo, sn_pstatus;
static int sn_party, sn_status, sn_storeitem, sn_select, sn_storestock, sn_partystore;
static int sn_shop, sn_equipdrag, sn_skillpoint;
static int sn_quest, sn_storequest, sn_mapparts, sn_player;
static int sn_battlesel, sn_bchara, sn_bmon;
static int sn_castle, sn_resultitem;
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
	sn_cfgsnd = sn_get("T_ConfigSound");
	sn_cfgwin = sn_get("T_ConfigWindow");
	sn_cfg3d = sn_get("T_Config3D");
	sn_cfgcfm = sn_get("T_ConfigConfirm");
	sn_cfgetc = sn_get("T_ConfigEtc");
	sn_cfgetc2 = sn_get("T_ConfigEtc2");
	sn_town = sn_get("T_Town");
	sn_sysbar = sn_get("T_SystemBar");
	sn_sysmenu = sn_get("T_SystemMenu");
	sn_mini = sn_get("T_MiniStatusManager");
	sn_sysinfo = sn_get("T_SystemInfo");
	sn_pstatus = sn_get("T_PartyStatus");
	sn_party = sn_get("T_Party");
	sn_partystore = sn_get("T_PartyStore");
	sn_status = sn_get("T_Status");
	sn_storeitem = sn_get("T_StoreItem");
	sn_select = sn_get("T_Select");
	sn_storestock = sn_get("T_StoreStock");
	sn_shop = sn_get("T_Shop");
	sn_equipdrag = sn_get("T_EquipDrag");
	sn_skillpoint = sn_get("T_SkillPoint");
	sn_quest = sn_get("T_Quest");
	sn_storequest = sn_get("T_StoreQuest");
	sn_mapparts = sn_get("T_MapParts");
	sn_player = sn_get("CPlayer");
	sn_battlesel = sn_get("T_BattleSelect");
	sn_bchara = sn_get("T_BattleCharaManager");
	sn_bmon = sn_get("T_BattleMonsterManager");
	sn_castle = sn_get("T_Castle");
	sn_resultitem = sn_get("T_ResultItem");
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

static void mem_set_i(struct page *p, int m, int v)
{
	if (!p || m < 0 || m >= p->nr_vars)
		return;
	p->values[m].i = v;
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

/* A dialog's text as the window shows it (2.19.0): @, $ and a line break each start a line
 * (WordWrapText); other control characters become spaces. */
static void mem_str_lines(struct page *p, int m, char *dst, int n)
{
	struct string *s;
	char *u;
	int i, o = 0;
	dst[0] = 0;
	if (!p || m < 0 || m >= p->nr_vars || !string_index_valid(p->values[m].i) || n < 1)
		return;
	s = heap_get_string(p->values[m].i);
	if (!s || s->size <= 0)
		return;
	u = sjis2utf(s->text, (size_t)s->size);
	if (!u)
		return;
	for (i = 0; u[i] && o < n - 1; i++) {
		unsigned char c = (unsigned char)u[i];
		if (c == '@' || c == '$' || c == 10)
			dst[o++] = 10;
		else if (c == 13)
			continue;
		else if (c < 32)
			dst[o++] = ' ';
		else
			dst[o++] = (char)c;
	}
	dst[o] = 0;
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

/* T_Rect: inclusive end corner, INT_MIN in any field means empty. */
static int t_rect(struct page *r, int *x, int *y, int *w, int *h)
{
	int sx, sy, ex, ey;
	if (!r)
		return 0;
	sx = mem_i(r, 0);
	sy = mem_i(r, 1);
	ex = mem_i(r, 2);
	ey = mem_i(r, 3);
	if (sx == INT_MIN || sy == INT_MIN || ex == INT_MIN || ey == INT_MIN) {
		*w = *h = 0;
		return 0;
	}
	*x = sx;
	*y = sy;
	*w = ex - sx + 1;
	*h = ey - sy + 1;
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

/* Hub (T_Town), system bar, mini status, system info, system menu and
 * Party Summary. Members are looked up by name from the AIN so a field
 * that moves reads as 0 instead of a neighbour's value. */

#define U8_WORLD "\xE2\x97\x86\xE3\x83\xAF\xE3\x83\xBC\xE3\x83\xAB\xE3\x83\x89"

static int gi_town = -2, gi_bar = -2, gi_menu = -2, gi_mini = -2, gi_info = -2, gi_world = -2;
static int gi_party = -2, gi_status = -2, gi_chara = -2, gi_skill = -2, gi_partystore = -2;
static int gi_storeitem = -2, gi_select = -2, gi_storestock = -2, gi_selectdata = -2;
static int gi_shop = -2, gi_equipdrag = -2, gi_skillpoint = -2;
static int gi_quest = -2, gi_storequest = -2, gi_mapparts = -2, gi_player = -2;
static int gi_msgno = -2;
static int gi_battlesel = -2, gi_battlearea = -2, gi_bchara = -2, gi_bmon = -2;
static int gi_pta = -2, gi_ptb = -2;
static int gi_castle = -2, gi_area_name = -2, gi_area_new = -2;

#define AGENT_MAX 32

struct agent_reg {
	int used;
	char id[40];
	char label[80];
	int button;
	int hover_only;
	int slot;
	int x, y, w, h;
};

static struct agent_reg g_agent[AGENT_MAX];

/* rq.agent.widgets: what the kit (or a mod's own HUD) drew on the last update. */
#define KIT_MAX 96

struct kit_ctrl {
	char id[40];
	char label[80];
	char type[16];
	char value[160];
	int x, y, w, h;
	int enabled;
	int has_select, selected;
	int focused; /* the kit's keys cursor (2.12.0) */
};

static struct kit_ctrl g_kit[KIT_MAX];
static int g_nkit;

static int memb(struct page *p, const char *name)
{
	struct ain_struct *s;
	int i;
	if (!p || p->type != STRUCT_PAGE || !ain || p->index < 0
			|| p->index >= ain->nr_structures)
		return -1;
	s = &ain->structures[p->index];
	for (i = 0; i < s->nr_members && i < p->nr_vars; i++) {
		if (s->members[i].name && !strcmp(s->members[i].name, name))
			return i;
	}
	return -1;
}

static struct page *fpage(struct page *p, const char *name)
{
	return mem_page(p, memb(p, name));
}

static int fint(struct page *p, const char *name)
{
	return mem_i(p, memb(p, name));
}

static void fstr(struct page *p, const char *name, char *dst, int n)
{
	mem_str(p, memb(p, name), dst, n);
}

/* AIN member names for Japanese fields are SJIS; accept UTF-8 too. */
static int memb_utf(struct page *p, const char *utf)
{
	char *sj;
	int m;
	if (!utf)
		return -1;
	m = memb(p, utf);
	if (m >= 0)
		return m;
	sj = utf2sjis(utf, 0);
	if (!sj)
		return -1;
	m = memb(p, sj);
	free(sj);
	return m;
}

static int fint_utf(struct page *p, const char *utf)
{
	return mem_i(p, memb_utf(p, utf));
}

static void fstr_utf(struct page *p, const char *utf, char *dst, int n)
{
	mem_str(p, memb_utf(p, utf), dst, n);
}

static struct page *fpage_utf(struct page *p, const char *utf)
{
	return mem_page(p, memb_utf(p, utf));
}

static struct page *arr_at(struct page *arr, int i)
{
	if (!arr || arr->type != ARRAY_PAGE || i < 0 || i >= arr->nr_vars)
		return NULL;
	return slot_page(arr->values[i].i);
}

static int global_index(int *gi, const char *utf)
{
	if (*gi == -2) {
		char *sj = utf2sjis(utf, 0);
		*gi = sj ? ain_get_global(ain, sj) : -1;
		free(sj);
	}
	if (*gi < 0 || *gi >= ain->nr_globals || !heap || !heap_size || !heap[0].page)
		return -1;
	return *gi;
}

static struct page *global_struct(int *gi, const char *utf, int sn)
{
	struct page *p;
	if (global_index(gi, utf) < 0)
		return NULL;
	p = slot_page(global_get(*gi).i);
	if (!p || p->type != STRUCT_PAGE || (sn >= 0 && p->index != sn))
		return NULL;
	return p;
}

static int global_int(int *gi, const char *utf)
{
	if (global_index(gi, utf) < 0)
		return 0;
	return global_get(*gi).i;
}

static int sprite_live(struct page *sp)
{
	return sp && fint(sp, "m_nSpNo") > 0;
}

static int sprite_shown(struct page *sp)
{
	return sprite_live(sp) && fint(sp, "m_bShow");
}

/* The Party screen's base sprite's z: 1 in town, 201 on a quest's map, 301 for a fight's Switch. */
static int party_base_z(struct page *party)
{
	struct page *base = fpage(party, "m_spBase");
	struct sact_sprite *sp = sprite_live(base) ? sact_get_sprite(fint(base, "m_nSpNo")) : NULL;
	return sp ? sprite_get_z(sp) : 0;
}

struct tbtn {
	int id, key, active, show, select, visible, sprite;
	int x, y, w, h;
};

static int tbtn_read(struct page *btn, struct tbtn *o)
{
	struct page *arr, *bsp, *sp;
	int cur = 0, status;
	memset(o, 0, sizeof *o);
	o->sprite = -1;
	if (!btn || btn->type != STRUCT_PAGE || btn->index != sn_button)
		return 0;
	status = fint(btn, "m_nStatus");
	o->id = fint(btn, "m_nID");
	o->key = fint(btn, "m_nKey");
	o->active = status >= 0 && status != 1;
	o->show = fint(btn, "m_bShow") ? 1 : 0;
	o->select = fint(btn, "m_bSelect") ? 1 : 0;
	/* T_Button::getCurrentIndex */
	if (status == 0)
		cur = fint(btn, "m_bExec") ? 2 : 0;
	else if (status == 1 || status == 2)
		cur = status;
	arr = fpage(btn, "m_aspButton");
	bsp = arr_at(arr, cur);
	sp = fpage(fpage(bsp, "m_spBase"), "m_spBase");
	if (cur && !sprite_live(sp)) {
		bsp = arr_at(arr, 0);
		sp = fpage(fpage(bsp, "m_spBase"), "m_spBase");
	}
	if (sprite_live(sp))
		o->sprite = fint(sp, "m_nSpNo");
	o->visible = o->show && bsp && fint(bsp, "m_bShow");
	if (!t_rect(fpage(btn, "m_rcSelect"), &o->x, &o->y, &o->w, &o->h))
		t_rect(fpage(bsp, "m_rcBase"), &o->x, &o->y, &o->w, &o->h);
	return o->w > 0 && o->h > 0;
}

static struct ui_ctrl *emit_tbtn(struct page *btn, int index, const char *id,
		const char *label, const char *section)
{
	struct tbtn b;
	struct ui_ctrl *c;
	if (!tbtn_read(btn, &b))
		return NULL;
	c = ctrl_alloc();
	if (!c)
		return NULL;
	if (id)
		snprintf(c->id, sizeof c->id, "%s", id);
	else
		snprintf(c->id, sizeof c->id, "b%d", b.id);
	if (label)
		snprintf(c->label, sizeof c->label, "%s", label);
	snprintf(c->type, sizeof c->type, "button");
	if (section)
		snprintf(c->section, sizeof c->section, "%s", section);
	c->button = b.id;
	c->key = b.key;
	c->index = index;
	c->x = b.x;
	c->y = b.y;
	c->w = b.w;
	c->h = b.h;
	c->sprite = b.sprite;
	c->enabled = b.active;
	c->has_select = 1;
	c->selected = b.select;
	c->has_visible = 1;
	c->visible = b.visible;
	/* Hover means the game has selected it; a hidden button never is. */
	c->armed = b.visible && b.select;
	if (b.visible && c->label[0])
		text_add(c->label);
	return c;
}

typedef const char *(*btn_namer)(int id, const char **label);

static const char *town_name(int id, const char **label)
{
	switch (id) {
	case 600011: *label = "Rance Castle"; return "rance-castle";
	case 600012: *label = "Frostbuy"; return "frostbuy";
	case 600013: *label = "Party"; return "party";
	case 600014: *label = "Quests"; return "quests";
	default: *label = NULL; return NULL;
	}
}

static const char *bar_name(int id, const char **label)
{
	switch (id) {
	case 900001: *label = "System"; return "system";
	case 900002: *label = "Vault"; return "vault";
	case 900003: *label = "Trivia"; return "trivia";
	default: *label = NULL; return NULL;
	}
}

/* Labels are the ones drawn on CG 10052, not the createButton strings. */
static const char *menu_name(int id, const char **label)
{
	switch (id) {
	case 910001: *label = "Save"; return "save";
	case 910002: *label = "Load"; return "load";
	case 910009: *label = "Suspend Save"; return "suspend-save";
	case 910011: *label = "Config"; return "config";
	case 910012: *label = "CG Mode"; return "cg-mode";
	case 910013: *label = "Music Mode"; return "music";
	case 910014: *label = "Network"; return "web";
	case 910015: *label = "Backlog"; return "backlog";
	case 910091: *label = "Title Screen"; return "title-screen";
	case 910092: *label = "Exit Game"; return "quit";
	case 910050: *label = "Mods"; return "mods"; /* rq.gamebutton, loader 2.16.0 */
	default: *label = NULL; return NULL;
	}
}

static const char *pstatus_name(int id, const char **label)
{
	switch (id) {
	case 100001: *label = "Party Summary"; return "party-summary";
	case 100009: *label = "Close"; return "close";
	default: *label = NULL; return NULL;
	}
}

static int emit_group(struct page *group, const char *section, btn_namer namer)
{
	struct page *arr = fpage(group, "m_asButton");
	int i, n = 0;
	if (!arr || arr->type != ARRAY_PAGE)
		return 0;
	for (i = 0; i < arr->nr_vars; i++) {
		struct page *btn = arr_at(arr, i);
		const char *label = NULL, *name;
		if (!btn || btn->type != STRUCT_PAGE || btn->index != sn_button)
			continue;
		name = namer(fint(btn, "m_nID"), &label);
		if (emit_tbtn(btn, i, name, label, section))
			n++;
	}
	return n;
}

static void read_party(struct page *mini)
{
	struct page *cards = fpage(fpage(mini, "m_sStatus"), "m_asCard");
	int i;
	g_nparty = 0;
	g_has_party = 1;
	for (i = 0; i < 5; i++) {
		struct page *card = arr_at(cards, i);
		struct page *job = fpage(card, "m_sJob");
		struct page *hp = fpage(card, "m_sHpBar");
		struct party_row *r;
		int chara = fint(fpage(fpage(card, "m_sSkill"), "m_sList"), "m_nCharaNo");
		if (!card || chara <= 0)
			continue;
		r = &g_party[g_nparty++];
		memset(r, 0, sizeof *r);
		r->slot = i + 1;
		r->chara = chara;
		fstr(fpage(card, "m_sName"), "m_szText", r->name, (int)sizeof r->name);
		r->job = fint(fpage(job, "m_sJob"), "m_nType");
		r->level = fint(fpage(job, "m_sLevel"), "m_nValue");
		r->level_max = fint(fpage(job, "m_sMax"), "m_nValue");
		r->no_limit = fint(job, "m_bInfinity") ? 1 : 0;
		r->hp = fint(hp, "m_nValue");
		r->hp_max = fint(hp, "m_nMax");
		r->back_row = i >= 3;
	}
}

static struct party_row *party_slot(int slot)
{
	int i;
	for (i = 0; i < g_nparty; i++) {
		if (g_party[i].slot == slot)
			return &g_party[i];
	}
	return NULL;
}

static void emit_party(struct page *mini)
{
	struct page *arr = fpage(fpage(mini, "m_sButton"), "m_asButton");
	int i;
	read_party(mini);
	if (!arr || arr->type != ARRAY_PAGE)
		return;
	for (i = 0; i < arr->nr_vars; i++) {
		struct page *btn = arr_at(arr, i);
		struct party_row *r;
		struct ui_ctrl *c;
		char id[40], label[LABEL_LEN];
		int bid, slot;
		if (!btn || btn->type != STRUCT_PAGE || btn->index != sn_button)
			continue;
		bid = fint(btn, "m_nID");
		if (bid > 922000 && bid <= 922005) {
			slot = bid - 922000;
			r = party_slot(slot);
			snprintf(id, sizeof id, "member-%d", slot);
			if (r)
				snprintf(label, sizeof label, "%s", r->name[0] ? r->name : "member");
			else
				snprintf(label, sizeof label, "empty slot %d", slot);
			c = emit_tbtn(btn, i, id, label, "party");
			if (!c || !r)
				continue;
			/* ãƒŸãƒ‹ã‚¹ãƒ†ãƒ¼ã‚¿ã‚¹_ä½ç½®ï¼¹: front row 592, back row 613 */
			r->back_row = c->y >= 605;
			if (r->no_limit)
				snprintf(c->value, sizeof c->value, "%s Lv %d (no limit)",
					 job_name(r->job), r->level);
			else
				snprintf(c->value, sizeof c->value, "%s Lv %d/%d",
					 job_name(r->job), r->level, r->level_max);
		} else if (bid > 921000 && bid <= 921005) {
			slot = bid - 921000;
			snprintf(id, sizeof id, "status-%d", slot);
			c = emit_tbtn(btn, i, id, "Status", "party");
			if (!c)
				continue;
			/* checkStatus shows it only while its card is selected. */
			c->hover_only = 1;
			snprintf(c->value, sizeof c->value, "member-%d", slot);
		} else if (bid == 920002) {
			c = emit_tbtn(btn, i, "party-summary", "Party Summary", "party");
			if (!c)
				continue;
			/* Shown while the mouse is over the card strip (CG 10100). */
			c->hover_only = 1;
		}
	}
}

static void emit_info_ctrl(const char *id, const char *label, const char *value,
		int x, int y, int w, int h, int visible)
{
	struct ui_ctrl *c = ctrl_alloc();
	if (!c)
		return;
	snprintf(c->id, sizeof c->id, "%s", id);
	snprintf(c->label, sizeof c->label, "%s", label);
	snprintf(c->type, sizeof c->type, "info");
	snprintf(c->section, sizeof c->section, "system-bar");
	if (value)
		snprintf(c->value, sizeof c->value, "%s", value);
	c->x = x;
	c->y = y;
	c->w = w;
	c->h = h;
	c->armed = 0;
	c->has_visible = 1;
	c->visible = visible;
}

static void emit_number(const char *id, const char *label, struct page *num, const char *value)
{
	struct page *t = fpage(num, "m_sText");
	emit_info_ctrl(id, label, value, fint(t, "m_nX"), fint(t, "m_nY"),
		fint(t, "m_nWidth"), fint(t, "m_nHeight"), fint(t, "m_bShow") ? 1 : 0);
}

static void emit_info(struct page *info, struct page *bar)
{
	struct page *gold, *rank, *point, *pbar, *don, *mark;
	char v[64];
	int x, y, w, h;
	gold = fpage(info, "m_sGold");
	rank = fpage(info, "m_sRank");
	point = fpage(info, "m_sPoint");
	pbar = fpage(info, "m_sBar");
	don = fpage(info, "m_sDonation");
	mark = fpage(info, "m_spMark");
	g_info.ok = 1;
	g_info.world = global_int(&gi_world, U8_WORLD);
	g_info.money = fint(gold, "m_nValue");
	g_info.rank = fint(rank, "m_nValue");
	g_info.points = fint(fpage(point, "m_sValue"), "m_nValue");
	g_info.points_max = fint(fpage(point, "m_sMax"), "m_nValue");
	g_info.donations = fint(don, "m_nValue");
	g_info.trivia_new = sprite_shown(mark);

	/* T_SystemBar::createBase draws the logo at bar (888, 4). */
	snprintf(v, sizeof v, "world %d", g_info.world);
	emit_info_ctrl("title", "Rance VIII", v, 888, 740, 136, 28,
		       sprite_shown(fpage(bar, "m_spBase")));
	snprintf(v, sizeof v, "%d", g_info.money);
	emit_number("money", "Money", gold, v);
	snprintf(v, sizeof v, "%d", g_info.rank);
	emit_number("charisma", "Charisma", rank, v);
	snprintf(v, sizeof v, "%d/%d", g_info.points, g_info.points_max);
	x = fint(point, "m_nX");
	y = fint(point, "m_nY");
	w = fint(point, "m_nWidth");
	h = fint(point, "m_nHeight");
	if (pbar && fint(pbar, "m_nWidth") > 0) {
		int by = fint(pbar, "m_nY") + fint(pbar, "m_nHeight");
		if (w <= 0) {
			x = fint(pbar, "m_nX");
			w = fint(pbar, "m_nWidth");
		}
		if (by > y + h)
			h = by - y;
	}
	emit_info_ctrl("charisma-progress", "Charisma progress", v, x, y, w, h,
		       sprite_live(fpage(fpage(point, "m_sValue"), "m_spNumber")));
	snprintf(v, sizeof v, "%d", g_info.donations);
	emit_number("donations", "Donations", don, v);
	if (g_info.trivia_new)
		emit_info_ctrl("trivia-new", "New trivia", "true",
			       fint(mark, "m_nX"), fint(mark, "m_nY"), 16, 16, 1);
}

static int fill_hub(struct page *town)
{
	struct page *bar = global_struct(&gi_bar, "g_sSystemBar", sn_sysbar);
	struct page *mini = global_struct(&gi_mini, "g_sMiniStatus", sn_mini);
	struct page *info = global_struct(&gi_info, "g_sSystemInfo", sn_sysinfo);
	int n0 = g_nctrls;
	emit_group(fpage(town, "m_sButton"), "hub", town_name);
	if (mini && sprite_live(fpage(mini, "m_spBase")))
		emit_party(mini);
	if (bar && sprite_live(fpage(bar, "m_spBase")))
		emit_group(fpage(bar, "m_sButton"), "system-bar", bar_name);
	if (info)
		emit_info(info, bar);
	if (g_nctrls == n0)
		return 0;
	snprintf(g_screen, sizeof g_screen, "hub");
	hover_from_rects();
	return 1;
}

static int has_ctrl(const char *id, const char *section)
{
	int i;
	for (i = 0; i < g_nctrls; i++) {
		if (!strcmp(g_ctrls[i].id, id) && (!section || !strcmp(g_ctrls[i].section, section)))
			return 1;
	}
	return 0;
}

/* Screens whose own loop takes the footer's input (システムバー_入力確認 in the decompile),
 * plus `other` for unnamed ones that show it (Trivia, CG mode, Music mode). Loader 2.20.1. */
static int screen_takes_footer(const char *s)
{
	static const char *const names[] = {
		"hub", "frostbuy", "party-edit", "quests", "castle", "status", "vault",
		"vault-storage", "shop-buy", "dungeon", "battle", "other", NULL
	};
	int i;
	for (i = 0; names[i]; i++) {
		if (!strcmp(s, names[i]))
			return 1;
	}
	return 0;
}

/* The footer (g_sSystemBar, its party cards and money line) on every screen that shows it:
 * the hub, Frostbuy and the Party screen list it themselves; the rest get it here. */
static int emit_footer(void)
{
	struct page *bar = global_struct(&gi_bar, "g_sSystemBar", sn_sysbar);
	struct page *mini = global_struct(&gi_mini, "g_sMiniStatus", sn_mini);
	struct page *info = global_struct(&gi_info, "g_sSystemInfo", sn_sysinfo);
	struct page *arr;
	struct tbtn b;
	int n0 = g_nctrls, shown = 0, i;
	if (!bar || fint(bar, "m_nMode") < 0 || !sprite_live(fpage(bar, "m_spBase")))
		return 0;
	if (!screen_takes_footer(g_screen))
		return 0;
	arr = fpage(fpage(bar, "m_sButton"), "m_asButton");
	if (arr && arr->type == ARRAY_PAGE) {
		for (i = 0; i < arr->nr_vars; i++) {
			if (tbtn_read(arr_at(arr, i), &b) && b.visible)
				shown = 1;
		}
	}
	if (!shown)
		return 0;
	/* The cards' buttons are hidden in some modes (19, the status page). */
	shown = 0;
	arr = mini ? fpage(fpage(mini, "m_sButton"), "m_asButton") : NULL;
	if (arr && arr->type == ARRAY_PAGE && sprite_live(fpage(mini, "m_spBase"))) {
		for (i = 0; i < arr->nr_vars; i++) {
			if (tbtn_read(arr_at(arr, i), &b) && b.visible)
				shown = 1;
		}
	}
	if (shown && !has_ctrl("member-1", "party") && !has_ctrl("party-summary", "party"))
		emit_party(mini);
	if (!has_ctrl("system", "system-bar"))
		emit_group(fpage(bar, "m_sButton"), "system-bar", bar_name);
	if (!g_info.ok && info && fint(fpage(fpage(info, "m_sGold"), "m_sText"), "m_bShow"))
		emit_info(info, bar);
	return g_nctrls > n0;
}

static int fill_system_menu(struct page *menu)
{
	int n0 = g_nctrls;
	emit_group(fpage(menu, "m_sButton"), "system-menu", menu_name);
	if (g_nctrls == n0)
		return 0;
	snprintf(g_screen, sizeof g_screen, "system-menu");
	hover_from_rects();
	return 1;
}

static int fill_party_summary(struct page *ps)
{
	struct page *mini = global_struct(&gi_mini, "g_sMiniStatus", sn_mini);
	int n0 = g_nctrls;
	emit_group(fpage(ps, "m_sButton"), "party-summary", pstatus_name);
	if (mini)
		read_party(mini);
	if (g_nctrls == n0)
		return 0;
	snprintf(g_screen, sizeof g_screen, "party-summary");
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

enum cfg_panel {
	CFG_NONE = -1,
	CFG_MSG = 0,
	CFG_SOUND,
	CFG_WIN,
	CFG_3D,
	CFG_CONFIRM,
	CFG_ETC,
	CFG_ETC2
};

static const char *cfg_section(int panel)
{
	switch (panel) {
	case CFG_MSG: return "message";
	case CFG_SOUND: return "sound";
	case CFG_WIN: return "window";
	case CFG_3D: return "3d";
	case CFG_CONFIRM: return "confirm";
	case CFG_ETC: return "other";
	case CFG_ETC2: return "other-2";
	default: return "config";
	}
}

static void slot_str(int slot, char *dst, int n)
{
	struct string *s;
	char *u;
	dst[0] = 0;
	if (n <= 0 || !string_index_valid(slot))
		return;
	s = heap_get_string(slot);
	if (!s || s->size <= 0)
		return;
	u = sjis2utf(s->text, (size_t)s->size);
	if (!u)
		return;
	clean_copy(dst, n, u);
	free(u);
}

static void array_str(struct page *arr, int i, char *dst, int n)
{
	dst[0] = 0;
	if (!arr || arr->type != ARRAY_PAGE || i < 0 || i >= arr->nr_vars)
		return;
	slot_str(arr->values[i].i, dst, n);
}

static void join_str_array(struct page *arr, char *dst, int n)
{
	int i, len = 0;
	dst[0] = 0;
	if (!arr || arr->type != ARRAY_PAGE)
		return;
	for (i = 0; i < arr->nr_vars && len < n - 1; i++) {
		char piece[200];
		array_str(arr, i, piece, (int)sizeof piece);
		if (!piece[0])
			continue;
		if (len && len < n - 3) {
			dst[len++] = ' ';
			dst[len++] = '/';
			dst[len++] = ' ';
			dst[len] = 0;
		}
		snprintf(dst + len, (size_t)(n - len), "%s", piece);
		len = (int)strlen(dst);
	}
}

static int check_mark_on(struct page *checks, int index)
{
	struct page *sp;
	if (!checks || checks->type != ARRAY_PAGE || index < 0 || index >= checks->nr_vars)
		return 0;
	sp = slot_page(checks->values[index].i);
	return sp && mem_i(sp, 4) ? 1 : 0;
}

static const char *cfg_check_label(int panel, int index)
{
	if (panel == CFG_MSG) {
		switch (index) {
		case 0: return "Do not start the screensaver during auto mode";
		case 1: return "Leave message mode when the button is clicked";
		case 2: return "Stop message skip when a choice menu opens";
		case 3: return "Stop auto mode when a choice menu opens";
		case 4: return "Skip frames while message skip is running";
		case 5: return "Mouse wheel direction (stored flag 0)";
		case 6: return "Mouse wheel direction (stored flag 1)";
		case 7: return "Show the sample message";
		}
	}
	if (panel == CFG_SOUND) {
		switch (index) {
		case 0: return "Mute master";
		case 1: return "Mute BGM";
		case 2: return "Mute sound effects";
		case 3: return "Mute jingle";
		case 4: return "Mute system sounds";
		case 5: return "Keep playing sound when the window is in the background";
		}
	}
	if (panel == CFG_WIN) {
		switch (index) {
		case 0: return "Windowed";
		case 1: return "Fullscreen";
		case 2: return "Aspect ratio: normal";
		case 3: return "Aspect ratio: fixed";
		case 4: return "Scaling: standard";
		case 5: return "Scaling: bicubic";
		case 6: return "Remember window position and size";
		case 7: return "Minimize when fullscreen loses focus";
		case 8: return "Sleep while the window is inactive";
		case 9: return "Sleep when the frame rate would go too high";
		}
	}
	if (panel == CFG_3D) {
		switch (index) {
		case 0: return "3D preset 1";
		case 1: return "3D preset 2";
		case 2: return "3D preset 3";
		case 3: return "3D preset 4";
		case 4: return "3D preset 5";
		}
	}
	if (panel == CFG_ETC) {
		switch (index) {
		case 0: return "Map move style 0";
		case 1: return "Map move style 1";
		case 2: return "Screen control mode 0";
		case 3: return "Screen control mode 1";
		case 4: return "Move the cursor back automatically";
		case 5: return "Hide the cursor when the mouse stays still";
		case 6: return "Battle speed 0";
		case 7: return "Battle speed 1";
		case 8: return "Battle speed 2";
		}
	}
	if (panel == CFG_ETC2) {
		switch (index) {
		case 0: return "Allow the network connection";
		case 1: return "Send the play log";
		case 2: return "Disable Discord Rich Presence";
		}
	}
	if (panel == CFG_CONFIRM) {
		/* Mark is shown when ç¢ºèªãªã—(dialog) is false: the game still asks.
		   Dialog ids are T_ConfigConfirm@getDialogID switch 757, index order. */
		static const int dialog_ids[] = {
			1, 2, 10, 11, 12, 13, 14, 31, 20, 21, 22, 30, 32, 40, 15
		};
		static char buf[15][72];
		if (index < 0 || index >= 15)
			return "Ask before this action";
		snprintf(buf[index], sizeof buf[index],
			"Ask before this action (dialog %d)", dialog_ids[index]);
		return buf[index];
	}
	return "Checkbox";
}

static int cfg_check_on(int panel, int index, struct page *checks)
{
	int g = 0;
	if (panel != CFG_MSG)
		return check_mark_on(checks, index);
	switch (index) {
	case 0: g = 133; break;
	case 1: g = 130; break;
	case 2: g = 143; break;
	case 3: g = 144; break;
	case 4: g = 129; break;
	case 5: return global_get(138).i == 0;
	case 6: return global_get(138).i == 1;
	case 7: g = 128; break;
	default: return check_mark_on(checks, index);
	}
	return global_get(g).i != 0;
}

static const char *cfg_slider_label(int panel, int index)
{
	if (panel == CFG_SOUND) {
		switch (index) {
		case 0: return "Master volume";
		case 1: return "BGM volume";
		case 2: return "Sound effect volume";
		case 3: return "Jingle volume";
		case 4: return "System sound volume";
		}
	}
	switch (index) {
	case 0: return "Message speed";
	case 1: return "Auto mode wait";
	case 2: return "Skip wait for text you have already read";
	case 3: return "Skip wait for text you have not read";
	default: return "Slider";
	}
}

static void describe_cfg_button(struct ui_ctrl *c, int id, int panel,
		struct page *sliders, struct page *checks)
{
	struct page *sl;
	const char *sec = cfg_section(panel);
	if (id >= 100001 && id <= 100009) {
		static const char *tabs[] = {
			"message", "sound", "window", "3d", "confirm", "other", "other-2", "debug", "mods"
		};
		/* The Mods page is Other 2 made on a blank page (rq.config). */
		int cur = rq_config_mods_page() ? 100009 : 100001 + panel;
		snprintf(c->id, sizeof c->id, "tab-%s", tabs[id - 100001]);
		snprintf(c->label, sizeof c->label, "%s", tabs[id - 100001]);
		snprintf(c->type, sizeof c->type, "tab");
		snprintf(c->section, sizeof c->section, "tabs");
		snprintf(c->value, sizeof c->value, "%s", id == cur ? "true" : "false");
		return;
	}
	if (id == 100011) {
		snprintf(c->id, sizeof c->id, "close");
		snprintf(c->label, sizeof c->label, "Return to game");
		snprintf(c->type, sizeof c->type, "button");
		snprintf(c->section, sizeof c->section, "window");
		return;
	}
	if (id == 100019) {
		snprintf(c->id, sizeof c->id, "cancel");
		snprintf(c->label, sizeof c->label, "X");
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
	if (id >= 220001 && id <= 220008 && (panel == CFG_MSG || panel == CFG_SOUND)) {
		int n = id - 220001;
		snprintf(c->id, sizeof c->id, "check-%d", n);
		snprintf(c->label, sizeof c->label, "%s", cfg_check_label(panel, n));
		snprintf(c->type, sizeof c->type, "checkbox");
		snprintf(c->section, sizeof c->section, "%s", sec);
		snprintf(c->value, sizeof c->value, "%s",
			cfg_check_on(panel, n, checks) ? "true" : "false");
		return;
	}
	if (id >= 230001 && id <= 230008 && (panel == CFG_MSG || panel == CFG_SOUND)) {
		int n = id - 230001;
		float rate = 0;
		sl = NULL;
		if (sliders && sliders->type == ARRAY_PAGE && n < sliders->nr_vars)
			sl = slot_page(sliders->values[n].i);
		if (sl && sl->nr_vars > 5)
			rate = sl->values[5].f;
		snprintf(c->id, sizeof c->id, "slider-%d", n);
		snprintf(c->label, sizeof c->label, "%s", cfg_slider_label(panel, n));
		snprintf(c->type, sizeof c->type, "slider");
		snprintf(c->section, sizeof c->section, "%s", sec);
		snprintf(c->value, sizeof c->value, "%.2f", rate);
		return;
	}
	if (id >= 210001 && id < 220000 && (panel == CFG_MSG || panel == CFG_SOUND)) {
		snprintf(c->id, sizeof c->id, "reset-%d", id - 210001);
		snprintf(c->label, sizeof c->label, "Reset");
		snprintf(c->type, sizeof c->type, "button");
		snprintf(c->section, sizeof c->section, "%s", sec);
		return;
	}
	if (id >= 210001 && id <= 210020 && panel != CFG_MSG && panel != CFG_SOUND) {
		int n = id - 210001;
		snprintf(c->id, sizeof c->id, "check-%d", n);
		snprintf(c->label, sizeof c->label, "%s", cfg_check_label(panel, n));
		snprintf(c->type, sizeof c->type, "checkbox");
		snprintf(c->section, sizeof c->section, "%s", sec);
		snprintf(c->value, sizeof c->value, "%s",
			check_mark_on(checks, n) ? "true" : "false");
		return;
	}
	if (id == 200001) {
		snprintf(c->id, sizeof c->id, "manual");
		snprintf(c->label, sizeof c->label, "Manual");
		snprintf(c->type, sizeof c->type, "button");
		snprintf(c->section, sizeof c->section, "%s", sec);
		return;
	}
	snprintf(c->id, sizeof c->id, "b%d", id);
	snprintf(c->type, sizeof c->type, "button");
	snprintf(c->section, sizeof c->section, "%s", sec);
}

static void emit_text_ctrl(const char *id, const char *type, const char *section,
		const char *label, const char *value)
{
	struct ui_ctrl *c = ctrl_alloc();
	if (!c)
		return;
	snprintf(c->id, sizeof c->id, "%s", id);
	if (label)
		snprintf(c->label, sizeof c->label, "%s", label);
	snprintf(c->type, sizeof c->type, "%s", type);
	if (section)
		snprintf(c->section, sizeof c->section, "%s", section);
	if (value)
		snprintf(c->value, sizeof c->value, "%s", value);
	c->enabled = 1;
	c->armed = 0;
	if (label && label[0])
		text_add(label);
}

static void emit_3d_detail(struct page *d3)
{
	struct page *names, *opts, *comments, *chosen, *shown;
	int i, n, cur;
	names = mem_page(d3, 21);
	opts = mem_page(d3, 23);
	comments = mem_page(d3, 25);
	chosen = mem_page(d3, 34);
	shown = mem_page(d3, 22);
	cur = mem_i(d3, 33);
	n = names && names->type == ARRAY_PAGE ? names->nr_vars : 0;
	if (n <= 0 && shown && shown->type == ARRAY_PAGE)
		n = shown->nr_vars;
	for (i = 0; i < n && i < 24; i++) {
		struct page *wrap, *list, *cwrap, *clist;
		char id[40], name[160], value[160], comment[400];
		int pick = -1;
		name[0] = value[0] = comment[0] = 0;
		array_str(names, i, name, (int)sizeof name);
		if (!name[0] && shown && shown->type == ARRAY_PAGE && i < shown->nr_vars) {
			struct page *sp = slot_page(shown->values[i].i);
			mem_str(sp, 4, name, (int)sizeof name);
		}
		if (chosen && chosen->type == ARRAY_PAGE && i < chosen->nr_vars)
			pick = chosen->values[i].i;
		wrap = opts && opts->type == ARRAY_PAGE && i < opts->nr_vars
			? slot_page(opts->values[i].i) : NULL;
		list = mem_page(wrap, 0);
		if (pick >= 0)
			array_str(list, pick, value, (int)sizeof value);
		cwrap = comments && comments->type == ARRAY_PAGE && i < comments->nr_vars
			? slot_page(comments->values[i].i) : NULL;
		clist = mem_page(cwrap, 0);
		join_str_array(clist, comment, (int)sizeof comment);
		snprintf(id, sizeof id, "cat-%d", i);
		emit_text_ctrl(id, "category", "3d / graphics",
			name[0] ? name : "category", value);
		if (i == cur && list && list->type == ARRAY_PAGE) {
			int k;
			for (k = 0; k < list->nr_vars && k < 12; k++) {
				char cid[40], opt[160];
				array_str(list, k, opt, (int)sizeof opt);
				snprintf(cid, sizeof cid, "choice-%d", k);
				emit_text_ctrl(cid, "choice", "3d / values",
					opt[0] ? opt : "option", k == pick ? "true" : "false");
			}
		}
		if (comment[0]) {
			snprintf(id, sizeof id, i == cur ? "explain" : "explain-%d", i);
			emit_text_ctrl(id, "text", "3d / explanation", comment,
				i == cur ? "selected" : "other");
		}
	}
}

static int fill_cfg_panel(struct page *panel, int kind)
{
	struct page *group, *arr, *sliders, *checks;
	int i, n0, check_member;
	group = mem_page(panel, 1);
	arr = mem_page(group, 0);
	sliders = (kind == CFG_MSG || kind == CFG_SOUND) ? mem_page(panel, 2) : NULL;
	check_member = (kind == CFG_MSG || kind == CFG_SOUND) ? 4 : 3;
	checks = mem_page(panel, check_member);
	if (!arr || arr->type != ARRAY_PAGE)
		return 0;
	n0 = g_nctrls;
	text_add(cfg_section(kind));
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
		describe_cfg_button(c, id, kind, sliders, checks);
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
	if (kind == CFG_3D)
		emit_3d_detail(panel);
	if (g_nctrls == n0)
		return 0;
	snprintf(g_screen, sizeof g_screen, "config");
	hover_from_rects();
	return 1;
}

static int cfg_kind(int index)
{
	if (index == sn_cfgmsg) return CFG_MSG;
	if (index == sn_cfgsnd) return CFG_SOUND;
	if (index == sn_cfgwin) return CFG_WIN;
	if (index == sn_cfg3d) return CFG_3D;
	if (index == sn_cfgcfm) return CFG_CONFIRM;
	if (index == sn_cfgetc) return CFG_ETC;
	if (index == sn_cfgetc2) return CFG_ETC2;
	return CFG_NONE;
}

/* Fullwidth ï¼£ / ï¼³ globals hold character and skill master data. */
#define U8_CHARA_ARR "\xEF\xBC\xA3"
#define U8_SKILL_ARR "\xEF\xBC\xB3"

static struct page *chara_page(int no)
{
	struct page *arr;
	if (no <= 0)
		return NULL;
	if (global_index(&gi_chara, U8_CHARA_ARR) < 0)
		return NULL;
	arr = slot_page(global_get(gi_chara).i);
	if (!arr || arr->type != ARRAY_PAGE || no >= arr->nr_vars)
		return NULL;
	return slot_page(arr->values[no].i);
}

static struct page *skill_def_page(int no)
{
	struct page *arr;
	if (no <= 0)
		return NULL;
	if (global_index(&gi_skill, U8_SKILL_ARR) < 0)
		return NULL;
	arr = slot_page(global_get(gi_skill).i);
	if (!arr || arr->type != ARRAY_PAGE || no >= arr->nr_vars)
		return NULL;
	return slot_page(arr->values[no].i);
}

static void skill_name(int id, char *dst, int n)
{
	struct page *sk;
	dst[0] = 0;
	sk = skill_def_page(id);
	if (sk)
		fstr_utf(sk, "\xE5\x90\x8D\xE5\x89\x8D", dst, n); /* åå‰ */
	if (!dst[0])
		snprintf(dst, n, "skill-%d", id);
}

static void read_unit_from_chara(struct unit_row *r, int chara)
{
	struct page *c = chara_page(chara);
	int lv_max;
	r->chara = chara;
	r->name[0] = 0;
	r->job = 0;
	r->level = 0;
	r->level_max = 0;
	r->no_limit = 0;
	if (!c)
		return;
	fstr_utf(c, "\xE5\x90\x8D\xE5\x89\x8D", r->name, (int)sizeof r->name);
	r->job = fint_utf(c, "\xE8\x81\xB7\xE6\xA5\xAD"); /* è·æ¥­ */
	r->level = fint_utf(c, "\xEF\xBC\xAC\xEF\xBC\xB6"); /* ï¼¬ï¼¶ */
	lv_max = fint_utf(c, "\xEF\xBC\xAC\xEF\xBC\xB6\xE6\x9C\x80\xE5\xA4\xA7"); /* ï¼¬ï¼¶æœ€å¤§ */
	r->level_max = lv_max;
	r->no_limit = (lv_max == 2147483647) ? 1 : 0;
}

static void read_card_job_level(struct page *card, struct unit_row *r)
{
	struct page *job;
	if (!card)
		return;
	job = fpage(card, "m_sJob");
	if (!job)
		return;
	if (!r->job)
		r->job = fint(fpage(job, "m_sJob"), "m_nType");
	if (!r->job)
		r->job = fint(job, "m_nType");
	r->level = fint(fpage(job, "m_sLevel"), "m_nValue");
	r->level_max = fint(fpage(job, "m_sMax"), "m_nValue");
	r->no_limit = fint(job, "m_bInfinity") ? 1 : 0;
}

static const char *party_btn_name(int id, const char **label)
{
	static const char *filters[] = {
		"filter-fighter", "filter-guard", "filter-gunner", "filter-sorcerer",
		"filter-healer", "filter-ranger", "filter-brawler", "filter-civilian"
	};
	*label = NULL;
	switch (id) {
	case 100001: *label = "Reset"; return "reset";
	case 100002: *label = "Remove All"; return "remove-all";
	case 100003: *label = "Open"; return "open";
	case 100013: *label = "Call A"; return "call-a";
	case 100016: *label = "Call B"; return "call-b";
	case 100019: *label = "Call C"; return "call-c";
	case 100031: *label = "10"; return "size-10";
	case 100032: *label = "32"; return "size-32";
	case 109999: *label = "Close"; return "close";
	case 170001: *label = "Join Order"; return "sort-join";
	case 170002: *label = "High LV"; return "sort-high-lv";
	case 170003: *label = "Low LV"; return "sort-low-lv";
	case 170009: *label = "Custom"; return "sort-custom";
	default:
		break;
	}
	if (id > 180000 && id <= 180008) {
		int j = id - 180000;
		*label = job_name(j);
		return filters[j - 1];
	}
	if (id > 130000 && id <= 130005) {
		*label = "Party rank";
		return NULL; /* filled as rank-N */
	}
	if (id > 120000 && id < 130000) {
		*label = "Unit";
		return NULL; /* filled as unit-N */
	}
	if (id > 119000 && id <= 119005) {
		*label = "Status";
		return NULL;
	}
	if (id > 114000 && id < 119000) {
		*label = "Status";
		return NULL;
	}
	return NULL;
}

static void emit_named_btn(struct page *btn, int index, const char *id, const char *label,
		const char *section, const char *value, int hover_only)
{
	struct ui_ctrl *c = emit_tbtn(btn, index, id, label, section);
	if (!c)
		return;
	if (value && value[0])
		snprintf(c->value, sizeof c->value, "%s", value);
	if (hover_only)
		c->hover_only = 1;
}

/* --- vault helpers (T_StoreItem / T_Select / T_StoreStock) --- */
static const char *vault_sort_name(int mode)
{
	switch (mode) {
	case 1: case 11: return "Attack";
	case 12: return "Magic";
	case 13: return "Accuracy";
	case 14: return "Critical";
	case 15: return "Stun";
	case 16: return "Speed";
	case 17: return "Defense";
	case 18: return "Resistance";
	case 19: return "Evasion";
	case 20: return "Resilience";
	case 21: return "HP";
	case 22: return "Fire Damage Reduction";
	case 23: return "Ice Damage Reduction";
	case 24: return "Electric Damage Reduction";
	case 25: return "Light Damage Reduction";
	case 26: return "Darkness Damage Reduction";
	case 27: return "Low Enhancement Rank";
	case 28: return "High Enhancement Rank";
	default: return "Default Sort";
	}
}

static const char *vault_item_type_name(int t)
{
	switch (t) {
	case 21: return "Special";
	case 1: return "Weapon";
	case 11: return "Armor";
	case 17: return "Item Other";
	default: return NULL;
	}
}

static const char *vault_rank_name(int ex)
{
	switch (ex) {
	case 1: return "Rank C";
	case 2: return "Rank B";
	case 3: return "Rank A";
	case 4: return "Rank S";
	case 5: return "Others";
	default: return NULL;
	}
}

static const char *vault_weapon_ex_name(int ex)
{
	switch (ex) {
	case 1: return "Dagger";
	case 2: return "Sword";
	case 3: return "Hammer";
	case 4: return "Spear";
	case 5: return "Bow";
	case 6: return "Glove";
	case 7: return "Staff";
	case 8: return "Other";
	default: return NULL;
	}
}

static const char *vault_armor_ex_name(int ex)
{
	switch (ex) {
	case 1: return "Leather";
	case 2: return "Plate";
	case 3: return "Cloth";
	case 4: return "Gi";
	case 5: return "Other";
	default: return NULL;
	}
}

static void slugify(const char *src, char *dst, int n)
{
	int i = 0, j = 0;
	if (!src || n <= 0) { if (n > 0) dst[0] = 0; return; }
	while (src[i] && j < n - 1) {
		unsigned char c = (unsigned char)src[i++];
		if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))
			dst[j++] = (char)tolower(c);
		else if (c == ' ' || c == '-' || c == '_' || c == '/')
			if (j && dst[j - 1] != '-') dst[j++] = '-';
	}
	while (j > 0 && dst[j - 1] == '-') j--;
	dst[j] = 0;
	if (!dst[0]) snprintf(dst, n, "opt");
}

static void read_store_list_items(struct page *mgr, struct vault_item *dst, int *ndst, int max, int *top_out, int *count_out)
{
	struct page *ilist, *ids, *as_item;
	int i, top, count;
	*ndst = 0;
	if (top_out) *top_out = 0;
	if (count_out) *count_out = 0;
	if (!mgr) return;
	ilist = fpage(mgr, "m_sList");
	top = fint(mgr, "m_nTopIndex");
	ids = fpage(ilist, "m_anItemNo");
	count = ids && ids->type == ARRAY_PAGE ? ids->nr_vars : 0;
	if (top_out) *top_out = top;
	if (count_out) *count_out = count;
	for (i = 0; i < count && *ndst < max; i++) {
		struct vault_item *r = &dst[(*ndst)++];
		memset(r, 0, sizeof *r);
		r->index = i;
		r->item_no = ids->values[i].i;
		r->visible = (i >= top && i < top + VAULT_VISIBLE);
	}
	as_item = fpage(mgr, "m_asItem");
	if (as_item && as_item->type == ARRAY_PAGE) {
		for (i = 0; i < as_item->nr_vars; i++) {
			struct page *row = arr_at(as_item, i);
			struct vault_item *r;
			int list_i = top + i;
			char name[80];
			if (!row || list_i < 0 || list_i >= *ndst) continue;
			r = &dst[list_i];
			name[0] = 0;
			fstr(fpage(row, "m_sName"), "m_szText", name, (int)sizeof name);
			if (name[0]) snprintf(r->name, sizeof r->name, "%s", name);
			r->locked = fint(row, "m_bLock") ? 1 : 0;
			r->visible = 1;
			if (!r->item_no) {
				struct page *icon = fpage(row, "m_sIcon");
				if (icon) r->item_no = fint(icon, "m_nItemNo");
			}
		}
	}
}

static void emit_btn_group(struct page *group, const char *section,
		void (*namer)(int id, char *idbuf, int idn, char *label, int ln, char *value, int vn, int *hover_only))
{
	struct page *arr;
	int i;
	if (!group) return;
	arr = fpage(group, "m_asButton");
	if (!arr || arr->type != ARRAY_PAGE) return;
	for (i = 0; i < arr->nr_vars; i++) {
		struct page *btn = arr_at(arr, i);
		struct tbtn b;
		char id[40], label[80], value[80];
		int hover_only = 0;
		if (!tbtn_read(btn, &b) || !b.show) continue;
		id[0] = label[0] = value[0] = 0;
		if (namer) namer(b.id, id, (int)sizeof id, label, (int)sizeof label, value, (int)sizeof value, &hover_only);
		/* "-": a mod's button (rq.gamebutton), listed as its own agent control instead. */
		if (!strcmp(id, "-")) continue;
		if (!id[0]) snprintf(id, sizeof id, "b%d", b.id);
		if (!label[0]) snprintf(label, sizeof label, "%s", id);
		emit_named_btn(btn, i, id, label, section, value, hover_only);
	}
}

static void vault_main_namer(int id, char *idbuf, int idn, char *label, int ln, char *value, int vn, int *hover_only)
{
	int idx;
	*hover_only = 0;
	value[0] = 0;
	if (id == 509991) { snprintf(idbuf, idn, "storage"); snprintf(label, ln, "STORAGE"); return; }
	if (id == 509999) { snprintf(idbuf, idn, "close"); snprintf(label, ln, "Close"); return; }
	if (id >= 500001 && id < 500001 + VAULT_VISIBLE) {
		idx = id - 500001;
		snprintf(idbuf, idn, "item-%d", idx);
		if (g_vstate.top_index + idx < g_nvitems && g_vitems[g_vstate.top_index + idx].name[0])
			snprintf(label, ln, "%s", g_vitems[g_vstate.top_index + idx].name);
		else snprintf(label, ln, "item %d", idx);
		return;
	}
	if (id >= 551001 && id < 551001 + VAULT_VISIBLE) {
		idx = id - 551001;
		snprintf(idbuf, idn, "donate-%d", idx);
		snprintf(label, ln, "DONATE");
		snprintf(value, vn, "item-%d", idx);
		return;
	}
	if (id >= 552001 && id < 552001 + VAULT_VISIBLE) {
		idx = id - 552001;
		snprintf(idbuf, idn, "sell-%d", idx);
		snprintf(label, ln, "SELL");
		snprintf(value, vn, "item-%d", idx);
		return;
	}
	if (id >= 553001 && id < 553001 + VAULT_VISIBLE) {
		idx = id - 553001;
		snprintf(idbuf, idn, "lock-%d", idx);
		snprintf(label, ln, "Lock");
		snprintf(value, vn, "item-%d", idx);
		*hover_only = 1;
		return;
	}
	if (id >= 554001 && id < 554001 + VAULT_VISIBLE) {
		idx = id - 554001;
		snprintf(idbuf, idn, "unlock-%d", idx);
		snprintf(label, ln, "Unlock");
		snprintf(value, vn, "item-%d", idx);
		*hover_only = 1;
		return;
	}
	if (id >= 555001 && id < 555001 + VAULT_VISIBLE) {
		idx = id - 555001;
		snprintf(idbuf, idn, "item-status-%d", idx);
		snprintf(label, ln, "Status");
		snprintf(value, vn, "item-%d", idx);
		*hover_only = 1;
		return;
	}
	if (id >= 580000 && id < 581000) {
		int off = id - 580000;
		if (off == 1) { snprintf(idbuf, idn, "scroll-up"); snprintf(label, ln, "Scroll up"); return; }
		if (off == 2) { snprintf(idbuf, idn, "scroll-down"); snprintf(label, ln, "Scroll down"); return; }
		snprintf(idbuf, idn, "scroll-%d", off);
		snprintf(label, ln, "Scroll %d", off);
		return;
	}
	snprintf(idbuf, idn, "b%d", id);
	snprintf(label, ln, "b%d", id);
}

static void vault_filter_namer(int id, char *idbuf, int idn, char *label, int ln, char *value, int vn, int *hover_only)
{
	static const char *jobs[] = {
		"filter-fighter", "filter-guard", "filter-gunner", "filter-sorcerer",
		"filter-healer", "filter-ranger", "filter-brawler", "filter-civilian"
	};
	*hover_only = 0;
	value[0] = 0;
	/* LimitItem registers buttons as base(571000) + itemType, not index. */
	if (id == 571021 || id == 571001 || id == 571011 || id == 571017) {
		int typ = id - 571000;
		const char *name = "filter-item-other";
		const char *lab = "Item Other";
		if (typ == 21) { name = "filter-special"; lab = "Special"; }
		else if (typ == 1) { name = "filter-weapon"; lab = "Weapon"; }
		else if (typ == 11) { name = "filter-armor"; lab = "Armor"; }
		snprintf(idbuf, idn, "%s", name);
		snprintf(label, ln, "%s", lab);
		if (g_vstate.item_type == typ)
			snprintf(value, vn, "selected");
		return;
	}
	if (id >= 572001 && id <= 572008) {
		int j = id - 572000;
		snprintf(idbuf, idn, "%s", jobs[j - 1]);
		snprintf(label, ln, "%s", job_name(j));
		if (g_vstate.job_type == j) snprintf(value, vn, "selected");
		return;
	}
	if (id >= 573001 && id <= 573008) {
		int ex = id - 573000;
		snprintf(idbuf, idn, "equip-%d", ex);
		snprintf(label, ln, "Equip %d", ex);
		if (g_vstate.equip_type == ex) snprintf(value, vn, "selected");
		return;
	}
	if (id >= 574001 && id <= 574020) {
		int ex = id - 574000;
		const char *lab = NULL;
		if (g_vstate.item_type == 21) {
			lab = vault_rank_name(ex);
			if (lab) {
				if (ex == 1) snprintf(idbuf, idn, "rank-c");
				else if (ex == 2) snprintf(idbuf, idn, "rank-b");
				else if (ex == 3) snprintf(idbuf, idn, "rank-a");
				else if (ex == 4) snprintf(idbuf, idn, "rank-s");
				else if (ex == 5) snprintf(idbuf, idn, "rank-others");
				else snprintf(idbuf, idn, "sub-%d", ex);
			}
		} else if (g_vstate.item_type == 1) {
			lab = vault_weapon_ex_name(ex);
			if (lab) {
				if (ex == 5) snprintf(idbuf, idn, "weapon-bow");
				else {
					char slug[40];
					slugify(lab, slug, (int)sizeof slug);
					snprintf(idbuf, idn, "weapon-%s", slug);
				}
			}
		} else if (g_vstate.item_type == 11) {
			lab = vault_armor_ex_name(ex);
			if (lab) {
				if (ex == 2) snprintf(idbuf, idn, "armor-plate");
				else {
					char slug[40];
					slugify(lab, slug, (int)sizeof slug);
					snprintf(idbuf, idn, "armor-%s", slug);
				}
			}
		}
		if (!idbuf[0]) snprintf(idbuf, idn, "sub-%d", ex);
		if (lab) snprintf(label, ln, "%s", lab);
		else snprintf(label, ln, "Sub %d", ex);
		if (g_vstate.item_type_ex == ex) snprintf(value, vn, "selected");
		return;
	}
	if (id >= 575001 && id <= 575002) {
		int t = id - 575000;
		snprintf(idbuf, idn, "%s", t == 1 ? "filter-locked" : "filter-unlocked");
		snprintf(label, ln, "%s", t == 1 ? "Locked" : "Unlocked");
		if (g_vstate.lock_type == t) snprintf(value, vn, "selected");
		return;
	}
	if (id >= 560001 && id <= 560020) {
		int off = id - 560000;
		if (off == 1) {
			snprintf(idbuf, idn, "sort-default");
			snprintf(label, ln, "Default Sort");
			if (g_vstate.sort_mode <= 1) snprintf(value, vn, "selected");
		} else if (off == 2) {
			snprintf(idbuf, idn, "sort-current");
			snprintf(label, ln, "%s", vault_sort_name(g_vstate.sort_mode));
			if (g_vstate.sort_mode >= 11) snprintf(value, vn, "selected");
		} else {
			snprintf(idbuf, idn, "sort-%d", off);
			snprintf(label, ln, "Sort %d", off);
		}
		return;
	}
	snprintf(idbuf, idn, "b%d", id);
	snprintf(label, ln, "b%d", id);
}

static void stock_namer(int id, char *idbuf, int idn, char *label, int ln, char *value, int vn, int *hover_only)
{
	int idx;
	*hover_only = 0;
	value[0] = 0;
	if (id == 590999) { snprintf(idbuf, idn, "stock-close"); snprintf(label, ln, "Close"); return; }
	if (id >= 590001 && id < 590001 + VAULT_VISIBLE) {
		idx = id - 590001;
		snprintf(idbuf, idn, "stock-%d", idx);
		if (g_vstate.stock_top + idx < g_nvstock && g_vstock[g_vstate.stock_top + idx].name[0])
			snprintf(label, ln, "%s", g_vstock[g_vstate.stock_top + idx].name);
		else snprintf(label, ln, "stock %d", idx);
		return;
	}
	if (id >= 593000 && id < 594000) {
		int off = id - 593000;
		if (off == 1) { snprintf(idbuf, idn, "stock-scroll-up"); snprintf(label, ln, "Scroll up"); return; }
		if (off == 2) { snprintf(idbuf, idn, "stock-scroll-down"); snprintf(label, ln, "Scroll down"); return; }
		snprintf(idbuf, idn, "stock-scroll-%d", off);
		snprintf(label, ln, "Scroll %d", off);
		return;
	}
	snprintf(idbuf, idn, "b%d", id);
	snprintf(label, ln, "b%d", id);
}

static void add_vault_json(cJSON *root)
{
	cJSON *o, *arr;
	int i;
	if (!g_vstate.ok) return;
	o = cJSON_AddObjectToObject(root, "vaultState");
	cJSON_AddNumberToObject(o, "storeType", g_vstate.store_type);
	cJSON_AddNumberToObject(o, "itemType", g_vstate.item_type);
	cJSON_AddNumberToObject(o, "itemTypeEx", g_vstate.item_type_ex);
	cJSON_AddNumberToObject(o, "jobType", g_vstate.job_type);
	cJSON_AddNumberToObject(o, "equipType", g_vstate.equip_type);
	cJSON_AddNumberToObject(o, "lockType", g_vstate.lock_type);
	cJSON_AddNumberToObject(o, "sortMode", g_vstate.sort_mode);
	cJSON_AddStringToObject(o, "sortName", vault_sort_name(g_vstate.sort_mode));
	cJSON_AddNumberToObject(o, "topIndex", g_vstate.top_index);
	cJSON_AddNumberToObject(o, "listCount", g_vstate.list_count);
	cJSON_AddNumberToObject(o, "stockCount", g_vstate.stock_count);
	cJSON_AddNumberToObject(o, "stockTop", g_vstate.stock_top);
	cJSON_AddBoolToObject(o, "openFlag", g_vstate.open_flag ? 1 : 0);
	cJSON_AddBoolToObject(o, "donationFlag", g_vstate.donation_flag ? 1 : 0);
	cJSON_AddBoolToObject(o, "sellFlag", g_vstate.sell_flag ? 1 : 0);
	cJSON_AddNumberToObject(o, "invCount", g_vstate.inv_count);
	cJSON_AddNumberToObject(o, "stockNum", g_vstate.stock_num);
	if (g_has_vitems) {
		arr = cJSON_AddArrayToObject(root, "items");
		for (i = 0; i < g_nvitems; i++) {
			struct vault_item *r = &g_vitems[i];
			cJSON *it = cJSON_CreateObject();
			cJSON_AddNumberToObject(it, "index", r->index);
			cJSON_AddNumberToObject(it, "itemNo", r->item_no);
			cJSON_AddStringToObject(it, "name", r->name);
			cJSON_AddBoolToObject(it, "locked", r->locked ? 1 : 0);
			cJSON_AddBoolToObject(it, "visible", r->visible ? 1 : 0);
			cJSON_AddItemToArray(arr, it);
		}
	}
	if (g_has_vstock) {
		arr = cJSON_AddArrayToObject(root, "stock");
		for (i = 0; i < g_nvstock; i++) {
			struct vault_item *r = &g_vstock[i];
			cJSON *it = cJSON_CreateObject();
			cJSON_AddNumberToObject(it, "index", r->index);
			cJSON_AddNumberToObject(it, "itemNo", r->item_no);
			cJSON_AddStringToObject(it, "name", r->name);
			cJSON_AddBoolToObject(it, "locked", r->locked ? 1 : 0);
			cJSON_AddBoolToObject(it, "visible", r->visible ? 1 : 0);
			cJSON_AddItemToArray(arr, it);
		}
	}
}

static int fill_select(struct page *sel)
{
	struct page *btns, *arr, *data_mgr, *datas;
	int i, n0 = g_nctrls;
	g_select_msg[0] = 0;
	if (!sel || !sprite_live(fpage(sel, "m_spBase")))
		return 0;
	data_mgr = global_struct(&gi_selectdata, "g_sSelectData", -1);
	datas = data_mgr ? fpage(data_mgr, "m_asData") : NULL;
	if (datas && datas->type == ARRAY_PAGE) {
		for (i = 0; i < datas->nr_vars; i++) {
			struct page *d = arr_at(datas, i);
			char line[512];
			size_t at, n;
			if (!d || fint(d, "m_nID") != 0)
				continue;
			mem_str_lines(d, memb(d, "m_szText"), line, (int)sizeof line);
			if (!line[0])
				continue;
			at = strlen(g_select_msg);
			n = strlen(line);
			if (at + n + 2 >= sizeof g_select_msg)
				break;
			if (at)
				g_select_msg[at++] = 10; /* a line break */
			memcpy(g_select_msg + at, line, n + 1);
		}
	}
	btns = fpage(sel, "m_sButton");
	arr = fpage(btns, "m_asButton");
	if (arr && arr->type == ARRAY_PAGE) {
		for (i = 0; i < arr->nr_vars; i++) {
			struct page *btn = arr_at(arr, i);
			struct tbtn b;
			char id[40], label[80], value[40];
			int bid, di;
			if (!tbtn_read(btn, &b) || !b.show) continue;
			bid = b.id;
			label[0] = 0;
			value[0] = 0;
			if (datas && datas->type == ARRAY_PAGE) {
				for (di = 0; di < datas->nr_vars; di++) {
					struct page *d = arr_at(datas, di);
					if (d && fint(d, "m_nID") == bid) {
						fstr(d, "m_szText", label, (int)sizeof label);
						break;
					}
				}
			}
			if (!label[0]) {
				if (bid == 1) snprintf(label, sizeof label, "Yes");
				else if (bid == 2) snprintf(label, sizeof label, "No");
				else if (bid == 999) snprintf(label, sizeof label, "Return");
				else if ((bid == 11 || bid == 12))
					snprintf(label, sizeof label, "DON'T ASK AGAIN");
				else if (bid >= 11 && bid <= 28)
					snprintf(label, sizeof label, "%s", vault_sort_name(bid));
				else
					snprintf(label, sizeof label, "option-%d", bid);
			}
			/* Donate confirm: 1/2 = Yes/No; empty 11/12 = don't-ask checkbox.
			 * Sort popup: 11=Attack .. 28=..., 999=Return (labels filled). */
			if (bid == 1)
				snprintf(id, sizeof id, "yes");
			else if (bid == 2)
				snprintf(id, sizeof id, "no");
			else if (bid == 999)
				snprintf(id, sizeof id, "return");
			else if (strstr(label, "ASK") || strstr(label, "Ask")
					|| ((bid == 11 || bid == 12) && !strcmp(label, "DON'T ASK AGAIN")))
				snprintf(id, sizeof id, "dont-ask");
			else if (bid == 12)
				snprintf(id, sizeof id, "magic");
			else if (bid == 11)
				snprintf(id, sizeof id, "attack");
			else if (bid >= 13 && bid <= 28) {
				char slug[40];
				slugify(label, slug, (int)sizeof slug);
				snprintf(id, sizeof id, "sort-%s", slug);
			} else {
				char slug[40];
				slugify(label, slug, (int)sizeof slug);
				snprintf(id, sizeof id, "select-%s", slug);
			}
			snprintf(value, sizeof value, "%d", bid);
			emit_named_btn(btn, i, id, label, "select", value, 0);
		}
	}
	if (g_nctrls == n0) return 0;
	snprintf(g_screen, sizeof g_screen, "select");
	hover_from_rects();
	return 1;
}

static void emit_equip_drag_drops(void);

static int fill_vault(struct page *store)
{
	struct page *list_mgr, *ilist, *limit_item, *limit_ex, *limit_job, *limit_equip, *limit_lock, *sort;
	struct page *stock;
	int n0 = g_nctrls;
	int open_flag;

	if (!store || !sprite_live(fpage(fpage(store, "m_sList"), "m_spBase")))
		return 0;

	list_mgr = fpage(store, "m_sList");
	ilist = fpage(list_mgr, "m_sList");
	limit_item = fpage(store, "m_sLimitItem");
	limit_ex = fpage(store, "m_sLimitItemEx");
	limit_job = fpage(store, "m_sLimitJob");
	limit_equip = fpage(store, "m_sLimitEquip");
	limit_lock = fpage(store, "m_sLimitLock");
	sort = fpage(store, "m_sSort");
	open_flag = fint(store, "m_bOpenFlag") ? 1 : 0;

	memset(&g_vstate, 0, sizeof g_vstate);
	g_vstate.ok = 1;
	g_vstate.store_type = fint(store, "m_nStoreType");
	g_vstate.item_type = fint(ilist, "m_nItemType");
	if (!g_vstate.item_type) g_vstate.item_type = fint(limit_item, "m_nItemType");
	g_vstate.item_type_ex = fint(ilist, "m_nItemTypeEx");
	if (!g_vstate.item_type_ex) g_vstate.item_type_ex = fint(limit_ex, "m_nItemTypeEx");
	g_vstate.job_type = fint(ilist, "m_nJobType");
	if (!g_vstate.job_type) g_vstate.job_type = fint(limit_job, "m_nJobType");
	g_vstate.equip_type = fint(ilist, "m_nEquipType");
	if (!g_vstate.equip_type) g_vstate.equip_type = fint(limit_equip, "m_nEquipType");
	g_vstate.lock_type = fint(ilist, "m_nLockType");
	if (!g_vstate.lock_type) g_vstate.lock_type = fint(limit_lock, "m_nLockType");
	g_vstate.sort_mode = fint(ilist, "m_nSortMode");
	g_vstate.open_flag = open_flag;
	g_vstate.donation_flag = fint(store, "m_bDonationFlag") ? 1 : 0;
	g_vstate.sell_flag = fint(store, "m_bSellFlag") ? 1 : 0;
	g_vstate.inv_count = fint(fpage(store, "m_sItemCount"), "m_nValue");
	g_vstate.stock_num = fint(fpage(store, "m_sStockCount"), "m_nValue");

	g_has_vitems = 1;
	read_store_list_items(list_mgr, g_vitems, &g_nvitems, VAULT_ITEM_MAX,
		&g_vstate.top_index, &g_vstate.list_count);

	g_has_vstock = 0;
	g_nvstock = 0;
	stock = global_struct(&gi_storestock, "g_sStoreStock", sn_storestock);
	if (stock && (open_flag || sprite_live(fpage(fpage(stock, "m_sList"), "m_spBase")))) {
		read_store_list_items(fpage(stock, "m_sList"), g_vstock, &g_nvstock, VAULT_ITEM_MAX,
			&g_vstate.stock_top, &g_vstate.stock_count);
		g_has_vstock = 1;
		emit_btn_group(fpage(stock, "m_sButton"), "vault", stock_namer);
		emit_btn_group(fpage(stock, "m_sButtonLock"), "vault", stock_namer);
		emit_btn_group(fpage(fpage(stock, "m_sScroll"), "m_sButton"), "vault", stock_namer);
	}

	emit_btn_group(fpage(store, "m_sButton"), "vault", vault_main_namer);
	emit_btn_group(fpage(fpage(store, "m_sScroll"), "m_sButton"), "vault", vault_main_namer);
	emit_btn_group(fpage(limit_item, "m_sButton"), "vault", vault_filter_namer);
	emit_btn_group(fpage(limit_ex, "m_sButton"), "vault", vault_filter_namer);
	emit_btn_group(fpage(limit_job, "m_sButton"), "vault", vault_filter_namer);
	emit_btn_group(fpage(limit_equip, "m_sButton"), "vault", vault_filter_namer);
	emit_btn_group(fpage(limit_lock, "m_sButton"), "vault", vault_filter_namer);
	emit_btn_group(fpage(sort, "m_sButton"), "vault", vault_filter_namer);
	emit_equip_drag_drops();

	/* Mark selected filters */
	{
		int i;
		for (i = n0; i < g_nctrls; i++) {
			struct ui_ctrl *c = &g_ctrls[i];
			if (strcmp(c->section, "vault") != 0 && strcmp(c->section, "equip-drag") != 0) continue;
			if (c->value[0] && !strcmp(c->value, "selected")) {
				c->has_select = 1;
				c->selected = 1;
			} else if (!strncmp(c->id, "filter-", 7) || !strncmp(c->id, "rank-", 5)
					|| !strncmp(c->id, "weapon-", 7) || !strncmp(c->id, "armor-", 6)
					|| !strcmp(c->id, "sort-default") || !strcmp(c->id, "sort-current")) {
				c->has_select = 1;
				c->selected = (c->value[0] && !strcmp(c->value, "selected"));
			}
		}
	}

	if (g_nctrls == n0) return 0;
	if (g_vstate.store_type == 3)
		snprintf(g_screen, sizeof g_screen, "shop-buy");
	else
		snprintf(g_screen, sizeof g_screen, "%s",
			(open_flag || g_has_vstock) ? "vault-storage" : "vault");
	hover_from_rects();
	return 1;
}

static void equip_drop_namer(int id, char *idbuf, int idn, char *label, int ln,
		char *value, int vn, int *hover_only)
{
	*hover_only = 0;
	value[0] = 0;
	if (id == 549999) {
		snprintf(idbuf, idn, "drop-vault");
		snprintf(label, ln, "Vault");
		return;
	}
	if (id >= 540001 && id <= 540005) {
		int idx = id - 540001;
		snprintf(idbuf, idn, "drop-%d", idx);
		snprintf(label, ln, "Party slot %d", idx + 1);
		return;
	}
	snprintf(idbuf, idn, "b%d", id);
	snprintf(label, ln, "b%d", id);
}

static void emit_equip_drag_drops(void)
{
	struct page *drag, *equip;
	drag = global_struct(&gi_equipdrag, "g_sEquipDrag", sn_equipdrag);
	if (!drag)
		return;
	equip = fpage(drag, "m_sEquip");
	if (!equip || !sprite_live(fpage(equip, "m_spBase")))
		return;
	emit_btn_group(fpage(equip, "m_sButton"), "equip-drag", equip_drop_namer);
}

static const char *shop_rack_name(int id, const char **label)
{
	switch (id) {
	case 110001: *label = "SPECIAL RACK"; return "rack-special";
	case 110002: *label = "WEAPON RACK"; return "rack-weapon";
	case 110003: *label = "ARMOR RACK"; return "rack-armor";
	case 110004: *label = "ITEMS AND OTHER RACK"; return "rack-items";
	default: *label = NULL; return NULL;
	}
}

static const char *shop_btn_name(int id, const char **label)
{
	const char *t = town_name(id, label);
	if (t) return t;
	t = bar_name(id, label);
	if (t) return t;
	return shop_rack_name(id, label);
}

static int fill_shop(struct page *shop)
{
	struct page *bar = global_struct(&gi_bar, "g_sSystemBar", sn_sysbar);
	struct page *mini = global_struct(&gi_mini, "g_sMiniStatus", sn_mini);
	struct page *info = global_struct(&gi_info, "g_sSystemInfo", sn_sysinfo);
	int n0 = g_nctrls;
	if (!shop || !sprite_live(fpage(shop, "m_spBase")))
		return 0;
	emit_group(fpage(shop, "m_sButton"), "frostbuy", shop_btn_name);
	if (mini && sprite_live(fpage(mini, "m_spBase")))
		emit_party(mini);
	if (bar && sprite_live(fpage(bar, "m_spBase")))
		emit_group(fpage(bar, "m_sButton"), "system-bar", bar_name);
	if (info)
		emit_info(info, bar);
	if (g_nctrls == n0)
		return 0;
	snprintf(g_screen, sizeof g_screen, "frostbuy");
	hover_from_rects();
	return 1;
}

/* Rance Castle (g_sCastle, T_Castle): rooms 110000 + area in 27 cells, three a floor, and the
 * town tabs. Loader 2.20.1. */
#define U8_AREA_NAME "\xE3\x83\xA9\xE3\x83\xB3\xE3\x82\xB9\xE5\x9F\x8E\xE3\x82\xA8\xE3\x83\xAA\xE3\x82\xA2\xE5\x90\x8D"
#define U8_AREA_NEW "\xE3\x83\xA9\xE3\x83\xB3\xE3\x82\xB9\xE5\x9F\x8E\xE3\x82\xA8\xE3\x83\xAA\xE3\x82\xA2" "new"

static int save_arr_int(int *gi, const char *name, int index);

static const char *const castle_floors[9] = { "Roof", "6F", "5F", "4F", "3F", "2F", "1F", "B1F", "B2F" };

static int fill_castle(struct page *castle)
{
	struct page *arr, *cells, *names;
	int i, n0 = g_nctrls;
	if (!castle || !sprite_live(fpage(castle, "m_spBase")) || !stack_has("T_Castle@"))
		return 0;
	cells = fpage(fpage(castle, "m_sList"), "m_anAreaNo");
	names = global_index(&gi_area_name, U8_AREA_NAME) >= 0
		? slot_page(global_get(gi_area_name).i) : NULL;
	arr = fpage(fpage(castle, "m_sButton"), "m_asButton");
	if (arr && arr->type == ARRAY_PAGE) {
		for (i = 0; i < arr->nr_vars; i++) {
			struct page *btn = arr_at(arr, i);
			struct ui_ctrl *c;
			const char *label = NULL, *tab;
			char id[40], name[LABEL_LEN];
			int bid, area, cell, k;
			if (!btn || btn->type != STRUCT_PAGE || btn->index != sn_button)
				continue;
			bid = fint(btn, "m_nID");
			tab = town_name(bid, &label);
			if (tab) {
				emit_tbtn(btn, i, tab, label, "castle");
				continue;
			}
			area = bid - 110000;
			if (area <= 0 || area > 99)
				continue;
			array_str(names, area, name, (int)sizeof name);
			snprintf(id, sizeof id, "room-%d", area);
			c = emit_tbtn(btn, i, id, name[0] ? name : "room", "castle");
			if (!c)
				continue;
			cell = -1;
			if (cells && cells->type == ARRAY_PAGE) {
				for (k = 0; k < cells->nr_vars && k < 27; k++) {
					if (cells->values[k].i == area) {
						cell = k;
						break;
					}
				}
			}
			snprintf(c->value, sizeof c->value, "%s%s",
				 cell >= 0 ? castle_floors[cell / 3] : "",
				 save_arr_int(&gi_area_new, U8_AREA_NEW, area) ? " new" : "");
		}
	}
	if (g_nctrls == n0)
		return 0;
	snprintf(g_screen, sizeof g_screen, "castle");
	hover_from_rects();
	return 1;
}

/*
 * The Party screen's 2nd Army box (g_sPartyStore, T_PartyStore.jaf), while it is open (2.31.0).
 * Its cards are buttons 280001 + N and their STATUS 270001 + N (N from 0, after the box's scroll,
 * as unit-N on the grid); 200001 is the box's cancel, which closes it; in m_sButtonLock, 200012 is
 * the box and 200011 the grid, the places a dragged character is dropped on.
 */
static void fill_party_store(struct page *store)
{
	struct page *list_mgr, *ids, *arr, *as_chara;
	int i, top, count, shown;

	if (!store || !sprite_live(fpage(store, "m_spBase")))
		return;
	list_mgr = fpage(store, "m_sList");
	top = fint(list_mgr, "m_nTopIndex");
	ids = fpage(fpage(list_mgr, "m_sList"), "m_anCharaNo");
	count = ids && ids->type == ARRAY_PAGE ? ids->nr_vars : 0;
	as_chara = fpage(list_mgr, "m_asChara");
	shown = as_chara && as_chara->type == ARRAY_PAGE ? as_chara->nr_vars : 0;

	g_sstate.ok = 1;
	g_sstate.top_index = top;
	g_sstate.list_count = count;
	g_sstate.shown = shown;
	g_nstore = 0;
	g_has_store = 1;
	for (i = 0; i < count && g_nstore < UNIT_MAX; i++) {
		struct unit_row *r = &g_store[g_nstore++];
		memset(r, 0, sizeof *r);
		r->index = i;
		r->chara = ids->values[i].i;
		read_unit_from_chara(r, r->chara);
		r->visible = i >= top && i < top + shown;
	}

	arr = fpage(fpage(store, "m_sButton"), "m_asButton");
	if (arr && arr->type == ARRAY_PAGE) {
		for (i = 0; i < arr->nr_vars; i++) {
			struct page *btn = arr_at(arr, i);
			char id[40], value[80];
			const char *label;
			int bid, idx;
			struct tbtn b;
			if (!tbtn_read(btn, &b) || !b.show)
				continue;
			bid = b.id;
			value[0] = 0;
			if (bid > 280000 && bid < 290000) {
				idx = bid - 280001;
				snprintf(id, sizeof id, "store-%d", idx);
				label = top + idx < g_nstore && g_store[top + idx].name[0]
					? g_store[top + idx].name : "unit";
				if (top + idx < g_nstore)
					snprintf(value, sizeof value, "%s Lv %d",
						 job_name(g_store[top + idx].job), g_store[top + idx].level);
				emit_named_btn(btn, i, id, label, "party-store", value, 0);
			} else if (bid > 270000 && bid < 280000) {
				idx = bid - 270001;
				snprintf(id, sizeof id, "store-status-%d", idx);
				snprintf(value, sizeof value, "store-%d", idx);
				emit_named_btn(btn, i, id, "Status", "party-store", value, 1);
			} else if (bid == 200001) {
				emit_named_btn(btn, i, "store-close", "Close", "party-store", "", 0);
			}
		}
	}
	arr = fpage(fpage(store, "m_sButtonLock"), "m_asButton");
	if (arr && arr->type == ARRAY_PAGE) {
		for (i = 0; i < arr->nr_vars; i++) {
			struct page *btn = arr_at(arr, i);
			struct tbtn b;
			if (!tbtn_read(btn, &b))
				continue;
			if (b.id == 200012)
				emit_named_btn(btn, i, "store-drop", "2nd Army", "party-store", "", 0);
			else if (b.id == 200011)
				emit_named_btn(btn, i, "grid-drop", "Party list", "party-store", "", 0);
		}
	}
}

static int fill_party_edit(struct page *party)
{
	struct page *btns, *arr, *list_mgr, *chara_list, *ids, *job, *sort, *chara_mgr;
	struct page *as_chara;
	int i, n0 = g_nctrls;
	int layout, sort_mode, job_type, top, count;

	if (!party || !sprite_live(fpage(party, "m_spBase")))
		return 0;

	list_mgr = fpage(party, "m_sList");
	chara_list = fpage(list_mgr, "m_sList");
	job = fpage(party, "m_sJob");
	sort = fpage(party, "m_sSort");
	chara_mgr = fpage(party, "m_sChara");
	layout = fint(list_mgr, "m_nLayout");
	sort_mode = fint(chara_list, "m_nSortMode");
	job_type = fint(job, "m_nJobType");
	if (!job_type)
		job_type = fint(chara_list, "m_nJobType");
	top = fint(list_mgr, "m_nTopIndex");
	ids = fpage(chara_list, "m_anCharaNo");
	count = ids && ids->type == ARRAY_PAGE ? ids->nr_vars : 0;

	g_pstate.ok = 1;
	g_pstate.layout = layout;
	g_pstate.sort_mode = sort_mode;
	g_pstate.job_type = job_type;
	g_pstate.filter_on = job_type > 0;
	g_pstate.open_flag = fint(party, "m_bOpenFlag") ? 1 : 0;
	g_pstate.top_index = top;
	g_pstate.list_count = count;

	/* Owned units: full filtered/sorted list from T_CharaList. */
	g_nunits = 0;
	g_has_units = 1;
	for (i = 0; i < count && g_nunits < UNIT_MAX; i++) {
		struct unit_row *r = &g_units[g_nunits++];
		memset(r, 0, sizeof *r);
		r->index = i;
		r->chara = ids->values[i].i;
		read_unit_from_chara(r, r->chara);
		r->visible = (i >= top && i < top + (layout == 1 ? 32 : 10));
	}

	/* Enrich visible cards (name text on layout 0). */
	as_chara = fpage(list_mgr, "m_asChara");
	if (as_chara && as_chara->type == ARRAY_PAGE) {
		for (i = 0; i < as_chara->nr_vars; i++) {
			struct page *pl = arr_at(as_chara, i);
			struct page *card;
			struct unit_row *r = NULL;
			int chara, list_i, lay;
			if (!pl)
				continue;
			lay = fint(pl, "m_nLayout");
			card = (lay == 0) ? fpage(pl, "m_sMiniCard") : fpage(pl, "m_sMiniMiniCard");
			chara = 0;
			if (card) {
				chara = fint(fpage(fpage(card, "m_sSkill"), "m_sList"), "m_nCharaNo");
				if (!chara)
					chara = fint(card, "m_nCharaNo");
			}
			list_i = top + i;
			if (list_i >= 0 && list_i < g_nunits)
				r = &g_units[list_i];
			if (r && chara > 0)
				r->chara = chara;
			if (r && card) {
				char name[80];
				name[0] = 0;
				fstr(fpage(card, "m_sName"), "m_szText", name, (int)sizeof name);
				if (name[0])
					snprintf(r->name, sizeof r->name, "%s", name);
				read_card_job_level(card, r);
				r->visible = 1;
			}
		}
	}

	/* Formation ranks from party manager / mini-like cards. */
	g_nparty = 0;
	g_has_party = 1;
	if (chara_mgr) {
		struct page *cards = fpage(chara_mgr, "m_asChara");
		for (i = 0; i < 5; i++) {
			struct page *card = arr_at(cards, i);
			struct party_row *r;
			int chara = 0;
			if (card)
				chara = fint(card, "m_nCharaNo");
			if (!chara && card)
				chara = fint(fpage(fpage(card, "m_sSkill"), "m_sList"), "m_nCharaNo");
			r = &g_party[g_nparty++];
			memset(r, 0, sizeof *r);
			r->slot = i + 1;
			r->chara = chara;
			r->back_row = i >= 3;
			if (chara > 0) {
				struct unit_row tmp;
				memset(&tmp, 0, sizeof tmp);
				read_unit_from_chara(&tmp, chara);
				snprintf(r->name, sizeof r->name, "%s", tmp.name);
				r->job = tmp.job;
				r->level = tmp.level;
				r->level_max = tmp.level_max;
				r->no_limit = tmp.no_limit;
			}
		}
	}

	btns = fpage(party, "m_sButton");
	arr = fpage(btns, "m_asButton");
	if (arr && arr->type == ARRAY_PAGE) {
		for (i = 0; i < arr->nr_vars; i++) {
			struct page *btn = arr_at(arr, i);
			const char *label = NULL, *name;
			char id[40], value[80];
			int bid, slot, idx;
			struct tbtn b;
			if (!tbtn_read(btn, &b) || !b.show)
				continue;
			bid = b.id;
			name = party_btn_name(bid, &label);
			value[0] = 0;
			if (bid > 130000 && bid <= 130005) {
				slot = bid - 130000;
				snprintf(id, sizeof id, "rank-%d", slot);
				if (slot >= 1 && slot <= g_nparty && g_party[slot - 1].name[0])
					label = g_party[slot - 1].name;
				else if (!label)
					label = "empty";
				emit_named_btn(btn, i, id, label, "party-edit", value, 0);
			} else if (bid > 119000 && bid <= 119005) {
				slot = bid - 119000;
				snprintf(id, sizeof id, "rank-status-%d", slot);
				snprintf(value, sizeof value, "rank-%d", slot);
				emit_named_btn(btn, i, id, "Status", "party-edit", value, 1);
			} else if (bid > 120000 && bid < 130000) {
				idx = bid - 120001;
				snprintf(id, sizeof id, "unit-%d", idx);
				if (top + idx < g_nunits && g_units[top + idx].name[0])
					label = g_units[top + idx].name;
				else if (!label)
					label = "unit";
				if (top + idx < g_nunits)
					snprintf(value, sizeof value, "%s Lv %d",
						 job_name(g_units[top + idx].job),
						 g_units[top + idx].level);
				emit_named_btn(btn, i, id, label, "party-edit", value, 0);
			} else if (bid > 114000 && bid < 119000) {
				idx = bid - 114001;
				snprintf(id, sizeof id, "unit-status-%d", idx);
				snprintf(value, sizeof value, "unit-%d", idx);
				emit_named_btn(btn, i, id, "Status", "party-edit", value, 1);
			} else if (town_name(bid, &label)) {
				/* Same tab row as the hub (600011–600014), 256×45 at y=8. */
				emit_named_btn(btn, i, town_name(bid, &label), label, "hub", "", 0);
			} else if (name) {
				if (bid == 100031)
					snprintf(value, sizeof value, "%s", layout == 0 ? "selected" : "");
				else if (bid == 100032)
					snprintf(value, sizeof value, "%s", layout == 1 ? "selected" : "");
				else if (bid >= 170001 && bid <= 170009)
					snprintf(value, sizeof value, "%s",
						 (sort_mode > 0 && bid == 170000 + sort_mode) ? "selected" : "");
				else if (bid > 180000 && bid <= 180008)
					snprintf(value, sizeof value, "%s",
						 (job_type == bid - 180000) ? "selected" : "");
				emit_named_btn(btn, i, name, label, "party-edit", value, 0);
			}
		}
	}

	/* Sort / filter buttons live in their own groups. */
	{
		struct page *groups[2];
		int g;
		groups[0] = sort ? fpage(sort, "m_sButton") : NULL;
		groups[1] = job ? fpage(job, "m_sButton") : NULL;
		for (g = 0; g < 2; g++) {
			struct page *ga = fpage(groups[g], "m_asButton");
			if (!ga || ga->type != ARRAY_PAGE)
				continue;
			for (i = 0; i < ga->nr_vars; i++) {
				struct page *btn = arr_at(ga, i);
				const char *label = NULL, *name;
				int bid;
				if (!btn)
					continue;
				bid = fint(btn, "m_nID");
				name = party_btn_name(bid, &label);
				if (!name)
					continue;
				emit_named_btn(btn, i, name, label, "party-edit", "", 0);
			}
		}
	}

	/* Mark selected sort/filter/size after emit_group (overwrite value). */
	for (i = 0; i < g_nctrls; i++) {
		struct ui_ctrl *c = &g_ctrls[i];
		if (strcmp(c->section, "party-edit") != 0)
			continue;
		if (!strcmp(c->id, "size-10")) {
			c->has_select = 1;
			c->selected = layout == 0;
			snprintf(c->value, sizeof c->value, "%s", layout == 0 ? "selected" : "");
		} else if (!strcmp(c->id, "size-32")) {
			c->has_select = 1;
			c->selected = layout == 1;
			snprintf(c->value, sizeof c->value, "%s", layout == 1 ? "selected" : "");
		} else if (!strncmp(c->id, "sort-", 5)) {
			int want = 0;
			if (!strcmp(c->id, "sort-join")) want = 1;
			else if (!strcmp(c->id, "sort-high-lv")) want = 2;
			else if (!strcmp(c->id, "sort-low-lv")) want = 3;
			else if (!strcmp(c->id, "sort-custom")) want = 9;
			c->has_select = 1;
			c->selected = (sort_mode == want);
			snprintf(c->value, sizeof c->value, "%s", c->selected ? "selected" : "");
		} else if (!strncmp(c->id, "filter-", 7)) {
			int j;
			for (j = 1; j <= 8; j++) {
				if (!strcmp(c->label, job_name(j)) || c->button == 180000 + j) {
					c->has_select = 1;
					c->selected = (job_type == j);
					snprintf(c->value, sizeof c->value, "%s",
						 c->selected ? "selected" : "");
					break;
				}
			}
		}
	}

	fill_party_store(global_struct(&gi_partystore, "g_sPartyStore", sn_partystore));

	/* Hub tabs and the system bar stay on screen under the roster. */
	{
		struct page *town = global_struct(&gi_town, "g_sTown", sn_town);
		struct page *bar = global_struct(&gi_bar, "g_sSystemBar", sn_sysbar);
		/* The roster covers the castle picture, so m_spBase may be hidden
		 * while the tab buttons (Frostbuy, Party, …) stay shown. */
		if (town)
			emit_group(fpage(town, "m_sButton"), "hub", town_name);
		if (bar && sprite_live(fpage(bar, "m_spBase")))
			emit_group(fpage(bar, "m_sButton"), "system-bar", bar_name);
	}

	if (g_nctrls == n0)
		return 0;
	snprintf(g_screen, sizeof g_screen, "party-edit");
	hover_from_rects();
	return 1;
}

static void read_skill_slots(struct page *obj, int *dst, int n)
{
	struct page *arr;
	int i;
	for (i = 0; i < n; i++)
		dst[i] = 0;
	arr = fpage(obj, "m_asSkill");
	if (!arr || arr->type != ARRAY_PAGE)
		return;
	for (i = 0; i < arr->nr_vars && i < n; i++) {
		struct page *btn = arr_at(arr, i);
		int id = fint(btn, "m_nSkillNo");
		if (id > 0)
			dst[i] = id;
	}
}

static void pack_skill_slots(const int *ids, int n, struct skill_row *dst, int *ndst, int max)
{
	int i;
	*ndst = 0;
	for (i = 0; i < n && *ndst < max; i++) {
		struct skill_row *s;
		if (ids[i] <= 0)
			continue;
		s = &dst[(*ndst)++];
		memset(s, 0, sizeof *s);
		s->id = ids[i];
		s->owned = 1;
		s->active = 1;
		skill_name(ids[i], s->name, (int)sizeof s->name);
	}
}

static void fill_status_skills(struct page *card)
{
	struct page *equip = fpage(card, "m_sEquipSkill");
	struct page *hold = fpage(card, "m_sHoldSkill");
	struct page *arr;
	int i;

	read_skill_slots(equip, g_status.active_at, 6);
	read_skill_slots(hold, g_status.known_at, 18);
	g_status.n_active = 0;
	g_status.n_learned = 0;
	pack_skill_slots(g_status.active_at, 6, g_status.active, &g_status.n_active, 6);
	arr = fpage(hold, "m_asSkill");
	if (arr && arr->type == ARRAY_PAGE) {
		for (i = 0; i < arr->nr_vars && g_status.n_learned < SKILL_MAX; i++) {
			struct page *btn = arr_at(arr, i);
			struct skill_row *s;
			int id;
			if (!btn)
				continue;
			id = fint(btn, "m_nSkillNo");
			if (id <= 0)
				continue;
			s = &g_status.learned[g_status.n_learned++];
			memset(s, 0, sizeof *s);
			s->id = id;
			s->owned = 1;
			skill_name(id, s->name, (int)sizeof s->name);
		}
	}
}

static void fill_status_from_chara(int chara)
{
	struct page *c = chara_page(chara);
	struct page *skills, *active;
	int i, lv_max;
	if (!c)
		return;
	g_status.chara = chara;
	fstr_utf(c, "\xE5\x90\x8D\xE5\x89\x8D", g_status.name, (int)sizeof g_status.name);
	g_status.job = fint_utf(c, "\xE8\x81\xB7\xE6\xA5\xAD");
	snprintf(g_status.job_name, sizeof g_status.job_name, "%s", job_name(g_status.job));
	g_status.level = fint_utf(c, "\xEF\xBC\xAC\xEF\xBC\xB6");
	lv_max = fint_utf(c, "\xEF\xBC\xAC\xEF\xBC\xB6\xE6\x9C\x80\xE5\xA4\xA7");
	g_status.level_max = lv_max;
	/* The card's m_bInfinity, read in fill_status, is the game's answer (its no-cap
	 * check by the character's number); a cap in the data is never 2147483647 (2.59.1). */
	if (lv_max == 2147483647)
		g_status.no_limit = 1;
	g_status.exp = fint_utf(c, "\xEF\xBC\xA5\xEF\xBC\xB8\xEF\xBC\xB0"); /* ï¼¥ï¼¸ï¼° */
	g_status.exp_max = fint_utf(c, "\xEF\xBC\xA5\xEF\xBC\xB8\xEF\xBC\xB0\xE6\x9C\x80\xE5\xA4\xA7");
	g_status.hp = fint_utf(c, "\xE4\xBD\x93\xE5\x8A\x9B"); /* ä½“åŠ› */
	g_status.hp_max = fint_utf(c, "\xE4\xBD\x93\xE5\x8A\x9B\xE6\x9C\x80\xE5\xA4\xA7");
	g_status.atk = fint_utf(c, "\xE6\x94\xBB\xE6\x92\x83\xE5\x8A\x9B");
	g_status.def = fint_utf(c, "\xE9\x98\xB2\xE5\xBE\xA1\xE5\x8A\x9B");
	g_status.mag = fint_utf(c, "\xE9\xAD\x94\xE6\xB3\x95\xE5\x8A\x9B");
	g_status.mdef = fint_utf(c, "\xE9\xAD\x94\xE6\x8A\xB5\xE5\x8A\x9B");
	g_status.hit = fint_utf(c, "\xE5\x91\xBD\xE4\xB8\xAD\xE7\x8E\x87");
	g_status.eva = fint_utf(c, "\xE5\x9B\x9E\xE9\x81\xBF\xE7\x8E\x87");
	g_status.crit = fint_utf(c, "\xE5\xBF\x85\xE6\xAE\xBA\xE7\x8E\x87");
	g_status.impact = fint_utf(c, "\xE8\xA1\x9D\xE6\x92\x83\xE7\x8E\x87");
	g_status.resist = fint_utf(c, "\xE8\x80\x90\xE6\x80\xA7\xE7\x8E\x87");
	g_status.spd = fint_utf(c, "\xE7\xB4\xA0\xE6\x97\xA9\xE3\x81\x95");
	g_status.points = fint_utf(c, "\xE3\x82\xB9\xE3\x82\xAD\xE3\x83\xAB\xE3\x83\x9D\xE3\x82\xA4\xE3\x83\xB3\xE3\x83\x88");
	g_status.equip[0] = fint_utf(c, "\xE3\x82\xAA\xE3\x83\x97\xE3\x82\xB7\xE3\x83\xA7\xE3\x83\xB3");
	g_status.equip[1] = fint_utf(c, "\xE6\xAD\xA6\xE5\x99\xA8");
	g_status.equip[2] = fint_utf(c, "\xE9\x98\xB2\xE5\x85\xB7");
	g_status.equip[3] = fint_utf(c, "\xE6\x8C\x81\xE3\x81\xA1\xE7\x89\xA9");

	active = fpage_utf(c, "\xE7\x99\xBB\xE9\x8C\xB2\xE3\x82\xB9\xE3\x82\xAD\xE3\x83\xAB"); /* 登録スキル */
	if (active && active->type == ARRAY_PAGE) {
		int any = 0;
		struct skill_row tmp[6];
		int ntmp = 0;
		for (i = 0; i < active->nr_vars && i < 6; i++) {
			int id = active->values[i].i;
			if (id > 0) {
				any = 1;
				if (!g_status.active_at[i])
					g_status.active_at[i] = id;
			}
		}
		for (i = 0; i < 6 && ntmp < 6; i++) {
			struct skill_row *s;
			int id = (i < active->nr_vars) ? active->values[i].i : 0;
			if (id <= 0)
				id = g_status.active_at[i];
			if (id <= 0)
				continue;
			s = &tmp[ntmp++];
			memset(s, 0, sizeof *s);
			s->id = id;
			s->active = 1;
			s->owned = 1;
			skill_name(id, s->name, (int)sizeof s->name);
		}
		if (any && ntmp > 0) {
			memcpy(g_status.active, tmp, sizeof tmp);
			g_status.n_active = ntmp;
		}
	}
	skills = fpage_utf(c, "\xE3\x82\xB9\xE3\x82\xAD\xE3\x83\xAB"); /* スキル */
	if (skills && skills->type == ARRAY_PAGE) {
		struct skill_row tmp[SKILL_MAX];
		int ntmp = 0;
		for (i = 0; i < skills->nr_vars && ntmp < SKILL_MAX; i++) {
			struct page *sk = slot_page(skills->values[i].i);
			struct skill_row *s;
			int id, owned;
			if (!sk)
				continue;
			id = fint_utf(sk, "\xE7\x95\xAA\xE5\x8F\xB7"); /* 番号 */
			owned = fint_utf(sk, "\xE4\xBF\x9D\xE6\x9C\x89"); /* 保有 */
			if (id <= 0 || !owned)
				continue;
			s = &tmp[ntmp++];
			memset(s, 0, sizeof *s);
			s->id = id;
			s->owned = 1;
			s->rank = fint_utf(sk, "\xE3\x83\xA9\xE3\x83\xB3\xE3\x82\xAF");
			s->cost = fint_utf(sk, "\xE3\x82\xB3\xE3\x82\xB9\xE3\x83\x88");
			s->active = fint_utf(sk, "\xE5\xAE\x9F\xE8\xA3\x85\xE4\xB8\xAD");
			skill_name(id, s->name, (int)sizeof s->name);
		}
		if (ntmp > 0) {
			memcpy(g_status.learned, tmp, sizeof tmp);
			g_status.n_learned = ntmp;
		}
	}
}

static int agent_id_ok(const char *id)
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

static int agent_claims(int button)
{
	int i;
	if (button <= 0)
		return 0;
	for (i = 0; i < AGENT_MAX; i++) {
		if (g_agent[i].used && g_agent[i].button == button)
			return 1;
	}
	return 0;
}

int rq_agent_set(const char *id, const char *label, int button, int hover_only)
{
	int i, free_i = -1;
	if (!agent_id_ok(id) || button <= 0)
		return 0;
	ui_lock();
	for (i = 0; i < AGENT_MAX; i++) {
		if (g_agent[i].used && !strcmp(g_agent[i].id, id)) {
			free_i = i;
			break;
		}
		if (!g_agent[i].used && free_i < 0)
			free_i = i;
	}
	if (free_i < 0) {
		ui_unlock();
		return 0;
	}
	g_agent[free_i].used = 1;
	snprintf(g_agent[free_i].id, sizeof g_agent[free_i].id, "%s", id);
	snprintf(g_agent[free_i].label, sizeof g_agent[free_i].label, "%s",
		(label && label[0]) ? label : id);
	g_agent[free_i].button = button;
	g_agent[free_i].hover_only = hover_only ? 1 : 0;
	g_agent[free_i].slot = 0;
	g_agent[free_i].x = g_agent[free_i].y = 0;
	g_agent[free_i].w = g_agent[free_i].h = 0;
	ui_unlock();
	return 1;
}

int rq_agent_set_rect(const char *id, const char *label, int x, int y, int w, int h)
{
	int i, free_i = -1;
	if (!agent_id_ok(id) || w <= 0 || h <= 0)
		return 0;
	ui_lock();
	for (i = 0; i < AGENT_MAX; i++) {
		if (g_agent[i].used && !strcmp(g_agent[i].id, id)) {
			free_i = i;
			break;
		}
		if (!g_agent[i].used && free_i < 0)
			free_i = i;
	}
	if (free_i < 0) {
		ui_unlock();
		return 0;
	}
	g_agent[free_i].used = 1;
	snprintf(g_agent[free_i].id, sizeof g_agent[free_i].id, "%s", id);
	snprintf(g_agent[free_i].label, sizeof g_agent[free_i].label, "%s",
		(label && label[0]) ? label : id);
	g_agent[free_i].button = 0;
	g_agent[free_i].hover_only = 0;
	g_agent[free_i].slot = 0;
	g_agent[free_i].x = x;
	g_agent[free_i].y = y;
	g_agent[free_i].w = w;
	g_agent[free_i].h = h;
	ui_unlock();
	return 1;
}

void rq_agent_clear(const char *id)
{
	int i;
	if (!id || !id[0])
		return;
	ui_lock();
	for (i = 0; i < AGENT_MAX; i++) {
		if (g_agent[i].used && !strcmp(g_agent[i].id, id))
			g_agent[i].used = 0;
	}
	ui_unlock();
}

static struct page *find_tbutton(int id, int *slot_io)
{
	struct page *p;
	size_t i;
	int slot;
	if (id <= 0)
		return NULL;
	slot = slot_io ? *slot_io : 0;
	if (slot > 0 && page_index_valid(slot)) {
		p = heap_get_page(slot);
		if (p && p->type == STRUCT_PAGE && p->index == sn_button
				&& fint(p, "m_nID") == id)
			return p;
	}
	if (!heap || !heap_size)
		return NULL;
	for (i = 1; i < heap_size; i++) {
		if (!page_index_valid((int)i))
			continue;
		p = heap_get_page((int)i);
		if (!p || p->type != STRUCT_PAGE || p->index != sn_button)
			continue;
		if (fint(p, "m_nID") != id)
			continue;
		if (slot_io)
			*slot_io = (int)i;
		return p;
	}
	if (slot_io)
		*slot_io = 0;
	return NULL;
}

/* Registered buttons are listed only while that T_Button is shown. */
static void emit_agent_controls(void)
{
	struct agent_reg copy[AGENT_MAX];
	int i, n = 0;
	snos_init();
	ui_lock();
	memcpy(copy, g_agent, sizeof copy);
	ui_unlock();
	for (i = 0; i < AGENT_MAX; i++) {
		struct page *btn;
		struct tbtn b;
		struct ui_ctrl *c;
		if (!copy[i].used)
			continue;
		if (copy[i].button <= 0) {
			struct ui_ctrl *rc;
			if (copy[i].w <= 0 || copy[i].h <= 0)
				continue;
			rc = ctrl_alloc();
			if (!rc)
				break;
			snprintf(rc->id, sizeof rc->id, "%s", copy[i].id);
			snprintf(rc->label, sizeof rc->label, "%s", copy[i].label);
			snprintf(rc->type, sizeof rc->type, "button");
			snprintf(rc->section, sizeof rc->section, "agent");
			rc->x = copy[i].x;
			rc->y = copy[i].y;
			rc->w = copy[i].w;
			rc->h = copy[i].h;
			rc->enabled = 1;
			rc->armed = 1;
			rc->has_visible = 1;
			rc->visible = 1;
			n++;
			continue;
		}
		btn = find_tbutton(copy[i].button, &copy[i].slot);
		if (!btn || !tbtn_read(btn, &b) || !b.show)
			continue;
		c = emit_tbtn(btn, n, copy[i].id, copy[i].label, "agent");
		if (!c)
			break;
		if (copy[i].hover_only)
			c->hover_only = 1;
		ui_lock();
		if (g_agent[i].used && g_agent[i].button == copy[i].button)
			g_agent[i].slot = copy[i].slot;
		ui_unlock();
		n++;
	}
}

static void emit_kit_controls(void)
{
	struct kit_ctrl copy[KIT_MAX];
	int i, n;

	ui_lock();
	n = g_nkit;
	memcpy(copy, g_kit, sizeof copy[0] * (size_t)n);
	ui_unlock();
	for (i = 0; i < n; i++) {
		struct ui_ctrl *c = ctrl_alloc();
		if (!c)
			break;
		snprintf(c->id, sizeof c->id, "%s", copy[i].id);
		snprintf(c->label, sizeof c->label, "%s", copy[i].label);
		snprintf(c->type, sizeof c->type, "%s", copy[i].type);
		snprintf(c->section, sizeof c->section, "kit");
		snprintf(c->value, sizeof c->value, "%s", copy[i].value);
		c->x = copy[i].x;
		c->y = copy[i].y;
		c->w = copy[i].w;
		c->h = copy[i].h;
		c->enabled = copy[i].enabled;
		c->armed = copy[i].enabled;
		c->has_select = copy[i].has_select;
		c->selected = copy[i].selected;
		c->has_visible = 1;
		c->visible = 1;
		c->focused = copy[i].focused;
	}
}

static void lua_str(lua_State *L, int t, const char *k, char *dst, int n)
{
	lua_getfield(L, t, k);
	if (lua_isstring(L, -1) || lua_isnumber(L, -1)) {
		/* Cut to the buffer at a character's start, so the JSON stays UTF-8 (2.29.0: a text
		 * field's value can pass it). */
		size_t len;
		const char *s = lua_tolstring(L, -1, &len);
		if (len >= (size_t)n) {
			len = (size_t)n - 1;
			while (len && ((unsigned char)s[len] & 0xC0) == 0x80)
				len--;
		}
		memcpy(dst, s, len);
		dst[len] = 0;
	} else if (lua_isboolean(L, -1))
		snprintf(dst, n, "%s", lua_toboolean(L, -1) ? "true" : "false");
	else
		dst[0] = 0;
	lua_pop(L, 1);
}

static int lua_int(lua_State *L, int t, const char *k)
{
	int v;
	lua_getfield(L, t, k);
	v = (int)lua_tointeger(L, -1);
	lua_pop(L, 1);
	return v;
}

/* rq.agent.widgets(list): the list replaces the last one; {} clears it. */
int rq_lua_agent_widgets(lua_State *L)
{
	struct kit_ctrl tmp[KIT_MAX];
	int i, n, k = 0;

	luaL_checktype(L, 1, LUA_TTABLE);
	n = (int)lua_rawlen(L, 1);
	for (i = 1; i <= n && k < KIT_MAX; i++) {
		struct kit_ctrl *c = &tmp[k];
		lua_rawgeti(L, 1, i);
		if (!lua_istable(L, -1)) {
			lua_pop(L, 1);
			continue;
		}
		memset(c, 0, sizeof *c);
		lua_str(L, -1, "id", c->id, (int)sizeof c->id);
		lua_str(L, -1, "label", c->label, (int)sizeof c->label);
		lua_str(L, -1, "type", c->type, (int)sizeof c->type);
		lua_str(L, -1, "value", c->value, (int)sizeof c->value);
		c->x = lua_int(L, -1, "x");
		c->y = lua_int(L, -1, "y");
		c->w = lua_int(L, -1, "w");
		c->h = lua_int(L, -1, "h");
		lua_getfield(L, -1, "disabled");
		c->enabled = !lua_toboolean(L, -1);
		lua_pop(L, 1);
		lua_getfield(L, -1, "selected");
		c->has_select = !lua_isnil(L, -1);
		c->selected = lua_toboolean(L, -1);
		lua_pop(L, 1);
		lua_getfield(L, -1, "focused");
		c->focused = lua_toboolean(L, -1);
		lua_pop(L, 1);
		lua_pop(L, 1);
		if (!agent_id_ok(c->id) || c->w <= 0 || c->h <= 0)
			continue;
		if (!c->label[0])
			snprintf(c->label, sizeof c->label, "%s", c->id);
		if (!c->type[0])
			snprintf(c->type, sizeof c->type, "button");
		k++;
	}
	ui_lock();
	memcpy(g_kit, tmp, sizeof tmp[0] * (size_t)k);
	g_nkit = k;
	ui_unlock();
	lua_pushinteger(L, k);
	return 1;
}

/* A kit entry that takes no click: a comment window (2.13.0), a bar (2.14.0), an empty skill slot
 * (2.15.0) or an item card (2.21.0). It is neither the hover nor over a game control. */
static int kit_passive(const struct ui_ctrl *c)
{
	return !strcmp(c->type, "comment") || !strcmp(c->type, "bar") || !strcmp(c->type, "slot") ||
	       !strcmp(c->type, "card");
}

static void hover_agent(void)
{
	int i, mx, my, active, best = -1, best_area = 0;
	mouse_agent_pos(&mx, &my, &active);
	if (!active)
		return;
	for (i = 0; i < g_nctrls; i++) {
		struct ui_ctrl *c = &g_ctrls[i];
		int area;
		if (strcmp(c->section, "agent") != 0 && strcmp(c->section, "kit") != 0)
			continue;
		if (!c->enabled || !c->armed || c->w <= 0 || c->h <= 0 || kit_passive(c))
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

static int skill_list_at(struct page *mgr, const char *list_member, int index)
{
	struct page *list, *arr;
	list = fpage(mgr, list_member);
	arr = fpage(list, "m_anSkillNo");
	if (!arr || arr->type != ARRAY_PAGE || index < 0 || index >= arr->nr_vars)
		return 0;
	return arr->values[index].i;
}

static void emit_indexed_skills(struct page *group, struct page *mgr, const char *list_member,
		int base, int count, const char *prefix)
{
	struct page *arr;
	int i;
	if (!group)
		return;
	arr = fpage(group, "m_asButton");
	if (!arr || arr->type != ARRAY_PAGE)
		return;
	for (i = 0; i < arr->nr_vars; i++) {
		struct page *btn = arr_at(arr, i);
		struct tbtn b;
		char id[40], label[80], value[32];
		int idx, skill;
		if (!tbtn_read(btn, &b) || !b.show)
			continue;
		if (agent_claims(b.id))
			continue;
		idx = b.id - base;
		id[0] = label[0] = value[0] = 0;
		if (idx >= 0 && idx < count) {
			skill = skill_list_at(mgr, list_member, idx);
			snprintf(id, sizeof id, "%s-%d", prefix, idx);
			skill_name(skill, label, (int)sizeof label);
			if (!label[0])
				snprintf(label, sizeof label, "%s", id);
			snprintf(value, sizeof value, "%d", skill);
		} else if (b.id == 109999) {
			snprintf(id, sizeof id, "close");
			snprintf(label, sizeof label, "Close");
		} else if (b.id >= 110001 && b.id <= 110017) {
			idx = b.id - 110001;
			skill = skill_list_at(mgr, "m_rfsList", idx);
			snprintf(id, sizeof id, "forget-%d", idx);
			skill_name(skill, label, (int)sizeof label);
			if (!label[0])
				snprintf(label, sizeof label, "Forget %d", idx + 1);
			if (skill > 0)
				snprintf(value, sizeof value, "%d", skill);
		} else {
			snprintf(id, sizeof id, "b%d", b.id);
			snprintf(label, sizeof label, "b%d", b.id);
		}
		emit_named_btn(btn, i, id, label, "skill-shop", value, 0);
	}
}

/* rq.skilllist.page() / page_count() (Lua since 2.18.0), rq.knownskills' (2.54.0); 0 before
 * install or on an error. */
static int module_int(const char *module, const char *name)
{
	lua_State *L = rq_lua();
	int top, v = 0;
	if (!L)
		return 0;
	top = lua_gettop(L);
	if (lua_getglobal(L, "rq") == LUA_TTABLE && lua_getfield(L, -1, module) == LUA_TTABLE
	    && lua_getfield(L, -1, name) == LUA_TFUNCTION && lua_pcall(L, 0, 1, 0) == LUA_OK
	    && lua_isinteger(L, -1))
		v = (int)lua_tointeger(L, -1);
	lua_settop(L, top);
	return v;
}

static int fill_skillshop(struct page *sp)
{
	struct page *neu, *hold;
	if (!sp || !sprite_live(fpage(sp, "m_spBase1")))
		return 0;
	neu = fpage(sp, "m_sNewSkill");
	hold = fpage(sp, "m_sHoldSkill");
	emit_indexed_skills(fpage(neu, "m_sButton"), neu, "m_sList", 200001, 36, "offer");
	emit_indexed_skills(fpage(hold, "m_sButton"), hold, "m_rfsList", 300001, 17, "upgrade");
	emit_indexed_skills(fpage(sp, "m_sButton"), hold, "m_rfsList", 0, 0, "forget");
	g_has_skillshop = 1;
	g_skill_page = module_int("skilllist", "page");
	g_skill_pages = module_int("skilllist", "page_count");
	g_skill_points = fint(fpage(fpage(sp, "m_sCard"), "m_sPoint"), "m_nValue");
	snprintf(g_screen, sizeof g_screen, "skill-shop");
	hover_from_rects();
	return 1;
}

static const char *status_btn_name(int id, const char **label)
{
	static const char *equip_ids[] = {
		"equip-special", "equip-weapon", "equip-armor", "equip-item"
	};
	static const char *equip_labs[] = {
		"Special", "Weapon", "Armor", "Item"
	};
	static const char *remove_ids[] = {
		"remove-special", "remove-weapon", "remove-armor", "remove-item"
	};
	*label = NULL;
	switch (id) {
	case 100002: *label = "Skills"; return "skills";
	case 100009: *label = "Close"; return "close";
	case 100011: *label = "Store profile"; return "store-profile";
	case 100012: *label = "Load profile"; return "load-profile";
	case 100013: *label = "Clear profile"; return "clear-profile";
	default:
		break;
	}
	if (id >= 130001 && id <= 130004) {
		int i = id - 130001;
		*label = equip_labs[i];
		return equip_ids[i];
	}
	if (id >= 139001 && id <= 139004) {
		int i = id - 139001;
		*label = "REMOVE";
		return remove_ids[i];
	}
	return NULL;
}

static void status_equip_namer(int id, char *idbuf, int idn, char *label, int ln,
		char *value, int vn, int *hover_only)
{
	const char *lab = NULL;
	const char *name = status_btn_name(id, &lab);
	*hover_only = 0;
	value[0] = 0;
	if (id >= 100101 && id <= 100109) {
		/* rq.gamebutton's "status" group (2.20.0): the mod's own control, <mod>.<id>. */
		snprintf(idbuf, idn, "-");
		return;
	}
	if (id >= 110001 && id <= 110006) {
		int slot = id - 110001;
		int skill = (slot >= 0 && slot < 6) ? g_status.active_at[slot] : 0;
		snprintf(idbuf, idn, "active-%d", slot);
		if (skill > 0)
			skill_name(skill, label, ln);
		if (!label[0])
			snprintf(label, ln, "Active %d", slot + 1);
		snprintf(value, vn, "%d", skill);
		return;
	}
	if (id >= 120001 && id <= 120018) {
		int slot = id - 120001;
		int skill = (slot >= 0 && slot < 18) ? g_status.known_at[slot] : 0;
		snprintf(idbuf, idn, "known-%d", slot);
		if (skill > 0)
			skill_name(skill, label, ln);
		if (!label[0])
			snprintf(label, ln, "Known %d", slot + 1);
		snprintf(value, vn, "%d", skill);
		return;
	}
	if (!name) {
		snprintf(idbuf, idn, "b%d", id);
		snprintf(label, ln, "b%d", id);
		return;
	}
	snprintf(idbuf, idn, "%s", name);
	snprintf(label, ln, "%s", lab ? lab : name);
	if (id >= 139001 && id <= 139004)
		*hover_only = 1;
}

static int fill_status(struct page *st)
{
	struct page *card, *params, *items, *point, *hp;
	int i, n0 = g_nctrls;
	int chara;

	if (!st || !sprite_live(fpage(st, "m_spBase")))
		return 0;
	card = fpage(st, "m_sCard");
	if (!card)
		return 0;
	/* T_StatusCard::getCharaNo's path (fixed in 2.20.0: m_sEquipSkill has no m_nCharaNo). */
	chara = fint(fpage(fpage(card, "m_sEquipSkill"), "m_sList"), "m_nCharaNo");

	memset(&g_status, 0, sizeof g_status);
	g_status.ok = 1;
	g_status.chara = chara;
	fstr(fpage(card, "m_sName"), "m_szText", g_status.name, (int)sizeof g_status.name);
	fstr(fpage(card, "m_sJobName"), "m_szText", g_status.job_name, (int)sizeof g_status.job_name);
	hp = fpage(card, "m_sHpBar");
	g_status.hp = fint(hp, "m_nValue");
	g_status.hp_max = fint(hp, "m_nMax");
	point = fpage(card, "m_sPoint");
	g_status.points = fint(point, "m_nValue");
	g_status.no_limit = fint(card, "m_bInfinity") ? 1 : 0;

	params = fpage(card, "m_asParam");
	if (params && params->type == ARRAY_PAGE) {
		int vals[16];
		memset(vals, 0, sizeof vals);
		for (i = 0; i < params->nr_vars && i < 16; i++) {
			struct page *dn = arr_at(params, i);
			vals[i] = fint(dn, "m_nValue");
		}
		g_status.level = vals[0];
		g_status.level_max = vals[1];
		g_status.exp = vals[2];
		g_status.exp_max = vals[3];
		if (!g_status.hp)
			g_status.hp = vals[4];
		if (!g_status.hp_max)
			g_status.hp_max = vals[5];
		g_status.atk = vals[6];
		g_status.def = vals[7];
		g_status.mag = vals[8];
		g_status.mdef = vals[9];
		g_status.hit = vals[10];
		g_status.eva = vals[11];
		g_status.impact = vals[12];
		g_status.resist = vals[13];
		g_status.crit = vals[14];
		g_status.spd = vals[15];
	}

	items = fpage(card, "m_asItem");
	if (items && items->type == ARRAY_PAGE) {
		for (i = 0; i < items->nr_vars && i < 4; i++) {
			struct page *it = arr_at(items, i);
			g_status.equip[i] = fint(it, "m_nItemNo");
			if (g_status.equip[i] < 0)
				g_status.equip[i] = 0;
		}
	}

	fill_status_skills(card);
	fill_status_from_chara(chara);
	for (i = 0; i < 4; i++) {
		if (g_status.equip[i] < 0)
			g_status.equip[i] = 0;
	}

	emit_btn_group(fpage(st, "m_sButton"), "status", status_equip_namer);
	/* Empty hotbar slots stay hidden. Drag still needs a rect. */
	{
		struct page *arr = fpage(fpage(st, "m_sButton"), "m_asButton");
		int bi;
		if (arr && arr->type == ARRAY_PAGE) {
			for (bi = 0; bi < arr->nr_vars; bi++) {
				struct page *btn = arr_at(arr, bi);
				struct tbtn b;
				char idbuf[40], label[80], value[32];
				int slot, skill;
				if (!tbtn_read(btn, &b) || b.show)
					continue;
				if (b.id < 110001 || b.id > 110006)
					continue;
				slot = b.id - 110001;
				skill = g_status.active_at[slot];
				snprintf(idbuf, sizeof idbuf, "active-%d", slot);
				label[0] = 0;
				if (skill > 0)
					skill_name(skill, label, (int)sizeof label);
				if (!label[0])
					snprintf(label, sizeof label, "Active %d", slot + 1);
				snprintf(value, sizeof value, "%d", skill);
				emit_named_btn(btn, bi, idbuf, label, "status", value, 0);
			}
		}
	}
	if (g_nctrls == n0 && !g_status.name[0] && chara <= 0)
		return 0;
	snprintf(g_screen, sizeof g_screen, "status");
	hover_from_rects();
	return 1;
}

static void emit_plain(const char *id, const char *label, const char *section,
		int x, int y, int w, int h)
{
	struct ui_ctrl *c = ctrl_alloc();
	if (!c)
		return;
	snprintf(c->id, sizeof c->id, "%s", id);
	snprintf(c->label, sizeof c->label, "%s", label);
	snprintf(c->type, sizeof c->type, "button");
	snprintf(c->section, sizeof c->section, "%s", section);
	c->x = x;
	c->y = y;
	c->w = w;
	c->h = h;
	c->enabled = 1;
	c->armed = 1;
	c->has_visible = 1;
	c->visible = 1;
}

struct msg_hit {
	int x, y, w, h;
};

static struct msg_hit g_msghit[8];
static int g_nmsghit;

static void on_msg_sprite(struct sprite *sp, void *unused)
{
	struct sact_sprite *sact;
	int x, y, w, h;
	(void)unused;
	sact = sprite_as_sact(sp);
	if (!sact || sact->sp.hidden || g_nmsghit >= 8)
		return;
	x = sact->rect.x;
	y = sact->rect.y;
	w = sact->rect.w;
	h = sact->rect.h;
	/* Far right of the message frame (x=216, y=568, 772x160). */
	if (x < 920 || y < 540 || y > 740)
		return;
	if (w < 20 || h < 16 || w > 90 || h > 48)
		return;
	g_msghit[g_nmsghit].x = x;
	g_msghit[g_nmsghit].y = y;
	g_msghit[g_nmsghit].w = w;
	g_msghit[g_nmsghit].h = h;
	g_nmsghit++;
}

static int fill_message(void)
{
	static const char *ids[] = { "auto", "skip", "msg-config" };
	static const char *labs[] = { "AUTO", "SKIP", "CONFIG" };
	int i, j;
	g_nmsghit = 0;
	scene_for_each_sprite(on_msg_sprite, NULL);
	for (i = 1; i < g_nmsghit; i++) {
		struct msg_hit k = g_msghit[i];
		j = i;
		while (j > 0 && g_msghit[j - 1].y > k.y) {
			g_msghit[j] = g_msghit[j - 1];
			j--;
		}
		g_msghit[j] = k;
	}
	if (g_nmsghit >= 3) {
		for (i = 0; i < 3; i++)
			emit_plain(ids[i], labs[i], "message",
				g_msghit[i].x, g_msghit[i].y, g_msghit[i].w, g_msghit[i].h);
	} else {
		/* メッセージボタン_設定 layout 1: x=966, y=600 step 40. */
		emit_plain("auto", "AUTO", "message", 966, 600, 52, 32);
		emit_plain("skip", "SKIP", "message", 966, 640, 52, 32);
		emit_plain("msg-config", "CONFIG", "message", 966, 680, 52, 32);
	}
	snprintf(g_screen, sizeof g_screen, "message");
	hover_from_rects();
	return 1;
}

static void quest_name_of(struct page *mgr, int qno, char *dst, int n)
{
	struct page *items;
	int i;
	dst[0] = 0;
	if (qno <= 0)
		return;
	items = fpage(mgr, "m_asItem");
	if (!items || items->type != ARRAY_PAGE)
		return;
	for (i = 0; i < items->nr_vars; i++) {
		struct page *it = arr_at(items, i);
		if (!it || fint(it, "m_nQuestNo") != qno)
			continue;
		fstr(fpage(it, "m_sText"), "m_szText", dst, n);
		return;
	}
}

static const char *quest_sort_name(int id, const char **label)
{
	switch (id) {
	case 180001: *label = "Pending"; return "sort-pending";
	case 180002: *label = "Quest ID"; return "sort-quest-id";
	case 180003: *label = "Clears"; return "sort-clears";
	case 180004: *label = "Difficulty"; return "sort-difficulty";
	case 180005: *label = "Ease"; return "sort-ease";
	case 180006: *label = "Reputation"; return "sort-reputation";
	case 180007: *label = "Alphabetical"; return "sort-name";
	default: *label = NULL; return NULL;
	}
}

static int fill_quests(struct page *quest)
{
	struct page *mgr, *ids, *arr, *sort_arr;
	int i, n0 = g_nctrls, top;
	if (!quest || !sprite_shown(fpage(quest, "m_spBase")))
		return 0;
	mgr = fpage(quest, "m_sList");
	g_qstate.ok = 1;
	g_qstate.kind = 1;
	g_qstate.top = fint(mgr, "m_nTopIndex");
	g_qstate.sort_mode = fint(mgr, "m_nSortMode");
	g_qstate.selected = fint(mgr, "m_nSelectNo");
	top = g_qstate.top;
	ids = fpage(mgr, "m_anQuestList");
	g_nquests = 0;
	g_qstate.count = (ids && ids->type == ARRAY_PAGE) ? ids->nr_vars : 0;
	for (i = 0; i < g_qstate.count && g_nquests < QUEST_MAX; i++) {
		struct quest_row *r = &g_quests[g_nquests++];
		memset(r, 0, sizeof *r);
		r->index = i;
		r->quest_no = ids->values[i].i;
		r->visible = (i >= top && i < top + 13);
		if (r->visible)
			quest_name_of(mgr, r->quest_no, r->name, (int)sizeof r->name);
	}
	arr = fpage(fpage(quest, "m_sButton2"), "m_asButton");
	if (arr && arr->type == ARRAY_PAGE) {
		for (i = 0; i < arr->nr_vars; i++) {
			struct page *btn = arr_at(arr, i);
			struct tbtn b;
			char id[40], value[32];
			int vis, list_i, qno;
			const char *label;
			if (!tbtn_read(btn, &b) || !b.show)
				continue;
			/* The town tabs share this group (街マップ_タブ作成, loader 2.20.1). */
			if (town_name(b.id, &label)) {
				emit_named_btn(btn, i, town_name(b.id, &label), label, "quests", "", 0);
				continue;
			}
			if (b.id < 160001 || b.id > 160013)
				continue;
			vis = b.id - 160001;
			list_i = top + vis;
			qno = (list_i >= 0 && list_i < g_nquests) ? g_quests[list_i].quest_no : 0;
			label = (list_i >= 0 && list_i < g_nquests && g_quests[list_i].name[0])
				? g_quests[list_i].name : "quest";
			snprintf(id, sizeof id, "quest-%d", vis);
			snprintf(value, sizeof value, "%d", qno);
			emit_named_btn(btn, i, id, label, "quests", value, 0);
		}
	}
	arr = fpage(fpage(quest, "m_sButton"), "m_asButton");
	if (arr && arr->type == ARRAY_PAGE) {
		for (i = 0; i < arr->nr_vars; i++) {
			struct page *btn = arr_at(arr, i);
			struct tbtn b;
			char value[32];
			if (!tbtn_read(btn, &b) || !b.show)
				continue;
			if (b.id != 100001)
				continue;
			snprintf(value, sizeof value, "%d", g_qstate.selected);
			emit_named_btn(btn, i, "take", "Take this Quest", "quests", value, 0);
		}
	}
	sort_arr = fpage(fpage(fpage(quest, "m_sSort"), "m_sButton"), "m_asButton");
	if (sort_arr && sort_arr->type == ARRAY_PAGE) {
		for (i = 0; i < sort_arr->nr_vars; i++) {
			struct page *btn = arr_at(sort_arr, i);
			struct tbtn b;
			const char *label = NULL, *name;
			if (!tbtn_read(btn, &b) || !b.show)
				continue;
			name = quest_sort_name(b.id, &label);
			if (!name)
				continue;
			emit_named_btn(btn, i, name, label, "quests", "", 0);
		}
	}
	for (i = 0; i < g_nctrls; i++) {
		struct ui_ctrl *c = &g_ctrls[i];
		int mode;
		if (strcmp(c->section, "quests") != 0 || strncmp(c->id, "sort-", 5) != 0)
			continue;
		mode = c->button - 180000;
		c->has_select = 1;
		c->selected = (g_qstate.sort_mode == mode);
		snprintf(c->value, sizeof c->value, "%s", c->selected ? "selected" : "");
	}
	/* While a quest is chosen the game reads none of the rows (T_Quest::checkInput skips
	 * m_sButton2 while isLocked) until Cancel (a right click, 100009) lets it go: the rows are
	 * not enabled then (2.28.0; agents took it for a lost hover after a dialog). */
	if (g_qstate.selected > 0) {
		for (i = n0; i < g_nctrls; i++) {
			struct ui_ctrl *c = &g_ctrls[i];
			if (strcmp(c->section, "quests") != 0 || strncmp(c->id, "quest-", 6) != 0)
				continue;
			c->enabled = 0;
			c->armed = 0;
		}
	}
	/* Scroll track. Wheel while this is hovered moves the list. */
	emit_plain("scroll", "Scroll", "quests", 919, 60, 28, 438);
	if (g_nctrls == n0)
		return 0;
	snprintf(g_screen, sizeof g_screen, "quests");
	hover_from_rects();
	return 1;
}

static int fill_storequest(struct page *st)
{
	struct page *arr;
	int i, n0 = g_nctrls;
	if (!st || !sprite_shown(fpage(st, "m_spBase")))
		return 0;
	g_qstate.ok = 1;
	g_qstate.kind = 2;
	g_qstate.mode = fint(st, "m_nMode");
	arr = fpage(fpage(st, "m_sButton"), "m_asButton");
	if (arr && arr->type == ARRAY_PAGE) {
		for (i = 0; i < arr->nr_vars; i++) {
			struct page *btn = arr_at(arr, i);
			struct tbtn b;
			if (!tbtn_read(btn, &b) || !b.show)
				continue;
			if (b.id == 100009)
				emit_named_btn(btn, i, "close", "Close", "quest-storage", "", 0);
			else if (b.id == 100001)
				emit_named_btn(btn, i, "yes", "Yes", "quest-storage", "1", 0);
			else if (b.id == 100002)
				emit_named_btn(btn, i, "no", "No", "quest-storage", "2", 0);
		}
	}
	/* The item tiles (2.47.0): item-1 to item-15 in the window's order, at T_StoreQuest's
	 * getIconX / getIconY, the instance as the value. They take hovers (the item card); a click
	 * on one does nothing in the game. */
	arr = fpage(fpage(st, "m_sList"), "m_anItemNo");
	if (arr && arr->type == ARRAY_PAGE) {
		for (i = 0; i < arr->nr_vars && i < 15; i++) {
			int ii = arr->values[i].i, before = g_nctrls;
			char id[16], label[24];
			if (ii <= 0)
				continue;
			snprintf(id, sizeof id, "item-%d", i + 1);
			snprintf(label, sizeof label, "Item %d", i + 1);
			emit_plain(id, label, "quest-storage", 482 + (i % 5) * 106, 204 + (i / 5) * 106, 100, 100);
			if (g_nctrls > before)
				snprintf(g_ctrls[g_nctrls - 1].value, sizeof g_ctrls[g_nctrls - 1].value, "%d", ii);
		}
	}
	if (g_nctrls == n0)
		return 0;
	snprintf(g_screen, sizeof g_screen, "quest-storage");
	hover_from_rects();
	return 1;
}

/* The appraisal after a quest's success (T_ResultItem, リザルト鑑定, 2.41.0): its Finish button
 * (100001, bottom right) and its close button (100009). The items' buttons are not listed. */
static int fill_appraisal(struct page *ri)
{
	struct page *arr;
	int i, n0 = g_nctrls;
	arr = fpage(fpage(ri, "m_sButton"), "m_asButton");
	if (arr && arr->type == ARRAY_PAGE) {
		for (i = 0; i < arr->nr_vars; i++) {
			struct page *btn = arr_at(arr, i);
			struct tbtn b;
			if (!tbtn_read(btn, &b) || !b.show)
				continue;
			if (b.id == 100001)
				emit_named_btn(btn, i, "finish", "Finish", "appraisal", "", 0);
			else if (b.id == 100009)
				emit_named_btn(btn, i, "close", "Close", "appraisal", "", 0);
		}
	}
	if (g_nctrls == n0)
		return 0;
	snprintf(g_screen, sizeof g_screen, "appraisal");
	hover_from_rects();
	return 1;
}

static const char *map_btn_name(int id, const char **label)
{
	switch (id) {
	case 700001: *label = "Area Display"; return "area-show";
	case 700002: *label = "Area Display"; return "area-hide";
	case 700003: *label = "Abandon"; return "abandon";
	case 700004: *label = "Quest Storage"; return "storage";
	case 700005: *label = "Party"; return "party";
	case 700011: *label = "Run"; return "walk-fast";
	case 700012: *label = "Walk"; return "walk-slow";
	default: *label = NULL; return NULL;
	}
}

/* Panel colors from Ｓパネル色: 100 attack (red), 101 support (blue). */
static int skill_panel(int id)
{
	struct page *sk = skill_def_page(id);
	if (!sk)
		return 0;
	return fint_utf(sk, "\xE3\x83\x91\xE3\x83\x8D\xE3\x83\xAB\xE8\x89\xB2");
}

static const char *skill_tone(int panel)
{
	if (panel == 100)
		return "red";
	if (panel == 101)
		return "blue";
	return "other";
}

static void chara_name_of(int chara, char *dst, int n)
{
	struct page *c;
	dst[0] = 0;
	c = chara_page(chara);
	if (c)
		fstr_utf(c, "\xE5\x90\x8D\xE5\x89\x8D", dst, n);
}

static int save_arr_int(int *gi, const char *name, int index)
{
	struct page *arr;
	if (index < 0 || global_index(gi, name) < 0)
		return 0;
	arr = slot_page(global_get(*gi).i);
	if (!arr || arr->type != ARRAY_PAGE || index >= arr->nr_vars)
		return 0;
	return arr->values[index].i;
}

static int skill_no_at(struct page *battle_skill, int index)
{
	struct page *list, *nos;
	if (!battle_skill || index < 0)
		return 0;
	list = fpage(fpage(battle_skill, "m_rfsSkill"), "m_sList");
	nos = fpage(list, "m_anSkillNo");
	if (!nos || nos->type != ARRAY_PAGE || index >= nos->nr_vars)
		return 0;
	return nos->values[index].i;
}

static int skill_uses_at(struct page *battle_skill, int index)
{
	struct page *num;
	if (!battle_skill || index < 0)
		return -1;
	num = arr_at(fpage(fpage(battle_skill, "m_sCount"), "m_asNumber"), index);
	if (!num)
		return -1;
	return fint(num, "m_nValue");
}

static int skill_status_at(struct page *battle_skill, int index)
{
	struct page *st = battle_skill ? fpage(battle_skill, "m_anStatus") : NULL;
	if (!st || st->type != ARRAY_PAGE || index < 0 || index >= st->nr_vars)
		return -1;
	return st->values[index].i;
}

static void decode_skill_button(int button, int *index, int *target)
{
	*index = -1;
	*target = 0;
	if (button >= 210000 && button < 220000) {
		*target = (button - 210000) / 100;
		*index = (button - 210000) % 100 - 1;
	} else if (button >= 200001 && button < 210000) {
		*index = button - 200001;
	}
}

static void emit_battle_skill_btn(struct page *btn, int index, int button,
		const char *label, const char *value)
{
	int n0 = g_nctrls;
	char id[40];
	struct ui_ctrl *c;
	snprintf(id, sizeof id, "skill-%d", button);
	emit_named_btn(btn, index, id, label, "battle", value, 0);
	if (g_nctrls <= n0)
		return;
	c = &g_ctrls[g_nctrls - 1];
	/* Arm from visibility. Hover should not wait for m_bSelect. */
	c->armed = c->visible && c->enabled ? 1 : 0;
}

static void emit_battle_unit(const struct battle_unit *u)
{
	struct ui_ctrl *c = ctrl_alloc();
	if (!c)
		return;
	snprintf(c->id, sizeof c->id, "unit-%d", u->no);
	snprintf(c->label, sizeof c->label, "%s", u->name[0] ? u->name : (u->enemy ? "enemy" : "ally"));
	snprintf(c->type, sizeof c->type, "unit");
	snprintf(c->section, sizeof c->section, "battle");
	snprintf(c->value, sizeof c->value, "%s", u->enemy ? "enemy" : "ally");
	c->x = u->x;
	c->y = u->y;
	c->w = u->w;
	c->h = u->h;
	c->enabled = 1;
	c->armed = 1;
	c->index = u->no;
	c->has_visible = 1;
	c->visible = 1;
}

static void fill_battle_units(struct battle_state *st)
{
	struct page *area, *mon;
	int forward, i;
	area = global_struct(&gi_battlearea, "g_sBattleArea", -1);
	mon = global_struct(&gi_bmon, "g_sBattleMonster", sn_bmon);
	forward = mon && fint(mon, "m_bForward");
	st->n_units = 0;
	for (i = 0; i < 5; i++) {
		struct battle_unit *u;
		struct page *rect, *card, *list;
		int x, y, w, h, chara = 0;
		if (st->n_units >= BATTLE_UNIT_MAX)
			break;
		rect = arr_at(fpage(area, "m_arcChara"), i);
		if (!t_rect(rect, &x, &y, &w, &h))
			continue;
		card = fpage(arr_at(fpage(global_struct(&gi_bchara, "g_sBattleChara", sn_bchara),
				"m_asChara"), i), "m_sCard");
		list = fpage(fpage(card, "m_sSkill"), "m_sList");
		chara = fint(list, "m_nCharaNo");
		if (chara <= 0)
			chara = save_arr_int(&gi_pta, "PTA", i + 1);
		if (chara <= 0)
			continue;
		u = &st->units[st->n_units++];
		memset(u, 0, sizeof *u);
		u->no = i + 1;
		u->chara = chara;
		u->enemy = 0;
		u->x = x;
		u->y = y;
		u->w = w;
		u->h = h;
		chara_name_of(chara, u->name, (int)sizeof u->name);
		if (!u->name[0])
			fstr(fpage(card, "m_sName"), "m_szText", u->name, (int)sizeof u->name);
	}
	for (i = 0; i < 5; i++) {
		struct battle_unit *u;
		struct page *rect;
		int x, y, w, h, chara;
		const char *field = forward ? "m_arcMonsterForward" : "m_arcMonster";
		if (st->n_units >= BATTLE_UNIT_MAX)
			break;
		rect = arr_at(fpage(area, field), i);
		if (!t_rect(rect, &x, &y, &w, &h)) {
			field = forward ? "m_arcMonster" : "m_arcMonsterForward";
			rect = arr_at(fpage(area, field), i);
			if (!t_rect(rect, &x, &y, &w, &h))
				continue;
		}
		chara = save_arr_int(&gi_ptb, "PTB", i + 1);
		u = &st->units[st->n_units++];
		memset(u, 0, sizeof *u);
		u->no = 11 + i;
		u->chara = chara;
		u->enemy = 1;
		u->x = x;
		u->y = y;
		u->w = w;
		u->h = h;
		if (chara > 0)
			chara_name_of(chara, u->name, (int)sizeof u->name);
		if (!u->name[0])
			snprintf(u->name, sizeof u->name, "enemy");
	}
}

static void fill_battle_skills(struct battle_state *st, struct page *battle_skill)
{
	struct page *arr;
	int i;
	st->n_skills = 0;
	if (!battle_skill)
		return;
	arr = fpage(fpage(battle_skill, "m_sButton"), "m_asButton");
	if (!arr || arr->type != ARRAY_PAGE)
		return;
	for (i = 0; i < arr->nr_vars; i++) {
		struct page *btn = arr_at(arr, i);
		struct tbtn b;
		struct battle_skill *s;
		int index, target, skill, panel, uses, status;
		if (st->n_skills >= BATTLE_SKILL_MAX)
			break;
		if (!tbtn_read(btn, &b))
			continue;
		if (b.id < 200001 || b.id >= 220000)
			continue;
		decode_skill_button(b.id, &index, &target);
		if (index < 0)
			continue;
		status = skill_status_at(battle_skill, index);
		if (status < 0)
			continue;
		skill = skill_no_at(battle_skill, index);
		if (skill <= 0)
			continue;
		panel = skill_panel(skill);
		uses = skill_uses_at(battle_skill, index);
		s = &st->skills[st->n_skills++];
		memset(s, 0, sizeof *s);
		s->button = b.id;
		s->skill = skill;
		s->uses = uses;
		s->panel = panel;
		s->target = target;
		s->index = index;
		s->enabled = (b.active && status > 0) ? 1 : 0;
		s->visible = b.visible ? 1 : 0;
		s->x = b.x;
		s->y = b.y;
		s->w = b.w;
		s->h = b.h;
		skill_name(skill, s->name, (int)sizeof s->name);
		snprintf(s->tone, sizeof s->tone, "%s", skill_tone(panel));
	}
}

static int fill_battle(struct page *sel)
{
	struct page *actor, *chrome;
	int i;
	if (!sel || fint(sel, "m_nZ") < 0)
		return 0;
	g_bstate.ok = 1;
	g_bstate.auto_flag = fint(sel, "m_bAutoFlag") ? 1 : 0;
	g_bstate.skill_mode = fint(sel, "m_nSkillMode");
	actor = fpage(sel, "m_rfsSkill");
	g_bstate.actor_no = actor ? fint(actor, "m_nNo") : 0;
	g_bstate.selecting = g_bstate.actor_no > 0;
	if (g_bstate.selecting) {
		struct page *list = fpage(fpage(actor, "m_rfsSkill"), "m_sList");
		g_bstate.actor_chara = fint(list, "m_nCharaNo");
		chara_name_of(g_bstate.actor_chara, g_bstate.actor_name,
			(int)sizeof g_bstate.actor_name);
	}
	fill_battle_units(&g_bstate);
	if (!g_bstate.actor_name[0] && g_bstate.actor_no > 0) {
		for (i = 0; i < g_bstate.n_units; i++) {
			if (g_bstate.units[i].no == g_bstate.actor_no && g_bstate.units[i].name[0]) {
				snprintf(g_bstate.actor_name, sizeof g_bstate.actor_name,
					"%s", g_bstate.units[i].name);
				break;
			}
		}
	}
	fill_battle_skills(&g_bstate, g_bstate.selecting ? actor : NULL);
	/* Skill buttons first so a same-sized unit rect does not win hover. */
	for (i = 0; i < g_bstate.n_skills; i++) {
		struct battle_skill *s = &g_bstate.skills[i];
		struct page *btn;
		char value[80];
		if (!s->visible)
			continue;
		btn = NULL;
		{
			struct page *arr = fpage(fpage(actor, "m_sButton"), "m_asButton");
			int k;
			if (arr && arr->type == ARRAY_PAGE) {
				for (k = 0; k < arr->nr_vars; k++) {
					struct page *cand = arr_at(arr, k);
					if (cand && fint(cand, "m_nID") == s->button) {
						btn = cand;
						break;
					}
				}
			}
		}
		if (!btn)
			continue;
		snprintf(value, sizeof value, "%s:%d:%d:%d", s->tone, s->uses, s->target, s->skill);
		emit_battle_skill_btn(btn, s->index, s->button, s->name, value);
	}
	for (i = 0; i < g_bstate.n_units; i++)
		emit_battle_unit(&g_bstate.units[i]);
	chrome = fpage(fpage(sel, "m_sButton"), "m_asButton");
	if (chrome && chrome->type == ARRAY_PAGE) {
		for (i = 0; i < chrome->nr_vars; i++) {
			struct page *btn = arr_at(chrome, i);
			struct tbtn b;
			if (!tbtn_read(btn, &b) || !b.visible)
				continue;
			if (b.id == 100021)
				emit_named_btn(btn, i, "auto", "Auto", "battle",
					g_bstate.auto_flag ? "on" : "off", 0);
			else if (b.id == 100003 && b.active)
				emit_named_btn(btn, i, "switch", "Switch", "battle", "", 0);
		}
	}
	/* Loader 1.13.0: the bar at the bottom, whose System menu has Title Screen in a fight.
	 * In a fight the bar has no base sprite, only its System button. */
	{
		struct page *bar = global_struct(&gi_bar, "g_sSystemBar", sn_sysbar);
		if (bar)
			emit_group(fpage(bar, "m_sButton"), "system-bar", bar_name);
	}
	snprintf(g_screen, sizeof g_screen, "battle");
	hover_from_rects();
	return 1;
}

static int fill_dungeon(struct page *map)
{
	struct page *arr, *player;
	int i, n0 = g_nctrls;
	if (!map || !sprite_shown(fpage(map, "m_spBase")))
		return 0;
	g_dstate.ok = 1;
	g_dstate.area = fint(map, "m_bAreaShow") ? 1 : 0;
	g_dstate.map_no = fint(map, "m_nMapNo");
	fstr(fpage(map, "m_sName"), "m_szText", g_dstate.name, (int)sizeof g_dstate.name);
	player = global_struct(&gi_player, "g_sPlayer", sn_player);
	if (player) {
		g_dstate.map_x = fint(player, "m_nMapX");
		g_dstate.map_z = fint(player, "m_nMapZ");
	}
	arr = fpage(fpage(map, "m_sButton"), "m_asButton");
	if (arr && arr->type == ARRAY_PAGE) {
		for (i = 0; i < arr->nr_vars; i++) {
			struct page *btn = arr_at(arr, i);
			const char *label = NULL, *name;
			struct tbtn b;
			if (!tbtn_read(btn, &b) || !b.show)
				continue;
			name = map_btn_name(b.id, &label);
			if (!name)
				continue;
			emit_named_btn(btn, i, name, label, "dungeon", "", 0);
		}
	}
	if (g_nctrls == n0)
		return 0;
	snprintf(g_screen, sizeof g_screen, "dungeon");
	hover_from_rects();
	return 1;
}

static int vm_fill_screen(void);

static int vm_fill(void)
{
	if (!vm_fill_screen())
		return 0;
	if (emit_footer())
		hover_from_rects();
	return 1;
}

static int vm_fill_screen(void)
{
	struct page *view = NULL, *title = NULL, *cfg = NULL, *pstatus = NULL, *menu, *town;
	struct page *party, *status, *appr = NULL;
	int i, view_at = 99, title_at = 99, cfg_at = 99, kind = CFG_NONE;
	snos_init();
	if (!ain)
		return 0;
	for (i = 0; i < 24; i++) {
		struct page *p = get_struct_page(i);
		if (!p || p->type != STRUCT_PAGE)
			continue;
		if (p->index == sn_pstatus && !pstatus && sprite_live(fpage(p, "m_spBase")))
			pstatus = p;
		if (sn_resultitem >= 0 && p->index == sn_resultitem && !appr && sprite_live(fpage(p, "m_spBase")))
			appr = p;
		if (is_config_view(p->index) && i < view_at) {
			view = p;
			view_at = i;
		}
		if (cfg_kind(p->index) != CFG_NONE && i < cfg_at) {
			cfg = p;
			cfg_at = i;
			kind = cfg_kind(p->index);
		}
		if (p->index == sn_title && i < title_at) {
			title = p;
			title_at = i;
		}
	}
	/* A mod's dialog over Config (2.28.0): dialog.lua raised the menu above Config's screen. */
	if ((cfg || view) && rq_hud_under_z() != RQ_HUD_UNDER_Z) {
		struct page *sel = global_struct(&gi_select, "g_sSelect", sn_select);
		if (sel && sprite_live(fpage(sel, "m_spBase")) && fill_select(sel))
			return 1;
	}
	if (cfg)
		return fill_cfg_panel(cfg, kind);
	if (view)
		return fill_config(view);
	{
		struct page *sel = global_struct(&gi_select, "g_sSelect", sn_select);
		if (sel && sprite_live(fpage(sel, "m_spBase")) && fill_select(sel))
			return 1;
	}
	/* The appraisal after a quest's success (2.41.0); its dialogs are `select` above. */
	if (appr && fill_appraisal(appr))
		return 1;
	/* Before the screens under it: since 2.20.1 the footer's SYSTEM opens it from Quests, the
	 * status page and the vault too. */
	menu = global_struct(&gi_menu, "g_sSystemMenu", sn_sysmenu);
	if (menu && fint(menu, "m_nMode") > 0 && sprite_shown(fpage(menu, "m_spBase"))
			&& fill_system_menu(menu))
		return 1;
	{
		struct page *stq = global_struct(&gi_storequest, "g_sStoreQuest", sn_storequest);
		if (stq && fill_storequest(stq))
			return 1;
	}
	if (global_int(&gi_msgno, "g_nMessageNo") > 0 && fill_message())
		return 1;
	/* Over the town, the castle or a quest's map: listed as `other`, with only the footer
	 * (vm_fill adds it). */
	if (inner_screen()) {
		snprintf(g_screen, sizeof g_screen, "other");
		return 1;
	}
	{
		struct page *vault = global_struct(&gi_storeitem, "g_sStoreItem", sn_storeitem);
		if (vault && fill_vault(vault))
			return 1;
	}
	{
		struct page *sk = global_struct(&gi_skillpoint, "g_sSkillPoint", sn_skillpoint);
		if (sk && fill_skillshop(sk))
			return 1;
	}
	status = global_struct(&gi_status, "g_sStatus", sn_status);
	if (status && sprite_live(fpage(status, "m_spBase")) && fill_status(status))
		return 1;
	/* Before the town's screens: a fight a mod starts there (rq.battle.start) runs over them,
	 * and their sprites stay live under it (2.22.1). The fight's own Party screen (Switch) is
	 * over the fight: its base is at z 301, the town's at 1 and the map's at 201
	 * (T_Party::createBase). */
	party = global_struct(&gi_party, "g_sParty", sn_party);
	if (party && !sprite_live(fpage(party, "m_spBase")))
		party = NULL;
	if (party && party_base_z(party) >= 301 && fill_party_edit(party))
		return 1;
	{
		struct page *bsel = global_struct(&gi_battlesel, "g_sBattleSelect", sn_battlesel);
		if (bsel && fill_battle(bsel))
			return 1;
	}
	if (party && fill_party_edit(party))
		return 1;
	{
		struct page *quest = global_struct(&gi_quest, "g_sQuest", sn_quest);
		if (quest && fill_quests(quest))
			return 1;
	}
	if (pstatus && fill_party_summary(pstatus))
		return 1;
	{
		struct page *shop = global_struct(&gi_shop, "g_sShop", sn_shop);
		if (shop && fill_shop(shop))
			return 1;
	}
	{
		struct page *map = global_struct(&gi_mapparts, "g_sMapParts", sn_mapparts);
		if (map && fill_dungeon(map))
			return 1;
	}
	{
		struct page *castle = global_struct(&gi_castle, "g_sCastle", sn_castle);
		if (castle && fill_castle(castle))
			return 1;
	}
	if (title && !stack_has(U8_WARN))
		return fill_title(title);
	town = global_struct(&gi_town, "g_sTown", sn_town);
	if (town && sprite_live(fpage(town, "m_spBase")) && stack_has("T_Town@")
			&& fill_hub(town))
		return 1;
	if (!view && !title && stack_has("T_Title"))
		text_add("ui:title-on-stack-no-page");
	return 0;
}

static struct page *sound_panel(void)
{
	int i;
	snos_init();
	for (i = 0; i < 24; i++) {
		struct page *p = get_struct_page(i);
		if (p && p->type == STRUCT_PAGE && p->index == sn_cfgsnd)
			return p;
	}
	return NULL;
}

static struct page *master_slider_page(void)
{
	struct page *panel, *sliders;
	panel = sound_panel();
	if (!panel)
		return NULL;
	sliders = mem_page(panel, 2);
	if (!sliders || sliders->type != ARRAY_PAGE || sliders->nr_vars < 1)
		return NULL;
	return slot_page(sliders->values[0].i);
}

static struct page *master_check_page(void)
{
	struct page *panel, *checks;
	panel = sound_panel();
	if (!panel)
		return NULL;
	checks = mem_page(panel, 4);
	if (!checks || checks->type != ARRAY_PAGE || checks->nr_vars < 1)
		return NULL;
	return slot_page(checks->values[0].i);
}

/* Thumb x follows the 0–1 rate when this page is a horizontal slider. */
static void move_master_knob(struct page *sl, float rate)
{
	struct sp_box knob;
	struct page *wrap, *child, *imp;
	struct sact_sprite *sp;
	int x, y, w, h, span, nx;
	if (!sl)
		return;
	if (rate < 0)
		rate = 0;
	if (rate > 1)
		rate = 1;
	if (!cas_rect(mem_page(sl, 1), &x, &y, &w, &h))
		return;
	wrap = mem_page(sl, 0);
	versatile_box(wrap, &knob);
	if (!knob.ok || knob.w <= 0)
		return;
	span = w - knob.w;
	if (span < 1)
		return;
	nx = x + (int)(rate * (float)span + 0.5f);
	if (wrap && mem_i(wrap, 0) == 0) {
		child = mem_page(wrap, 1);
		imp = child ? cg_imp(mem_i(child, 0)) : NULL;
		mem_set_i(imp, 1, nx);
	}
	if (knob.sp > 0) {
		sp = sact_get_sprite(knob.sp);
		if (sp)
			sprite_set_x(sp, nx);
	}
}

int rq_sound_master_pct(void)
{
	struct page *sl = master_slider_page();
	int pct = 0;
	float r;
	if (sl && sl->nr_vars > 5) {
		r = sl->values[5].f;
		if (r < 0)
			r = 0;
		if (r > 1)
			r = 1;
		return (int)(r * 100.0f + 0.5f);
	}
	if (!mixer_get_volume(0, &pct))
		return 0;
	if (pct < 0)
		pct = 0;
	if (pct > 100)
		pct = 100;
	return pct;
}

int rq_sound_master_muted(void)
{
	struct page *c = master_check_page();
	int m = 0;
	if (c && c->nr_vars > 4)
		return mem_i(c, 4) ? 1 : 0;
	if (!mixer_get_mute(0, &m))
		return 0;
	return m ? 1 : 0;
}

static void sound_apply_pct(int pct)
{
	struct page *sl;
	float r;
	if (pct < 0)
		pct = 0;
	if (pct > 100)
		pct = 100;
	r = (float)pct / 100.0f;
	sl = master_slider_page();
	if (sl && sl->nr_vars > 5)
		sl->values[5].f = r;
	move_master_knob(sl, r);
	mixer_set_volume(0, pct);
}

static void sound_apply_mute(int on)
{
	struct page *c = master_check_page();
	on = on ? 1 : 0;
	if (c && c->nr_vars > 4)
		mem_set_i(c, 4, on);
	mixer_set_mute(0, on);
}

void rq_sound_master_add(int delta_pct)
{
	sound_apply_pct(rq_sound_master_pct() + delta_pct);
}

void rq_sound_master_toggle_mute(void)
{
	sound_apply_mute(!rq_sound_master_muted());
}

static void layer_add(char ov[][24], int max, int *n, const char *id)
{
	int i;
	if (!id || !id[0] || !n || *n >= max)
		return;
	for (i = 0; i < *n; i++) {
		if (!strcmp(ov[i], id))
			return;
	}
	snprintf(ov[*n], 24, "%s", id);
	(*n)++;
}

static int agent_id_live(const char *id)
{
	int i, found = 0;
	if (!id)
		return 0;
	ui_lock();
	for (i = 0; i < AGENT_MAX; i++) {
		if (g_agent[i].used && !strcmp(g_agent[i].id, id)) {
			found = 1;
			break;
		}
	}
	ui_unlock();
	return found;
}

static int vault_overlay_id(char *id, int n)
{
	struct page *store, *stock;
	int open_flag, store_type, stock_open;
	store = global_struct(&gi_storeitem, "g_sStoreItem", sn_storeitem);
	if (!store || !sprite_live(fpage(fpage(store, "m_sList"), "m_spBase")))
		return 0;
	store_type = fint(store, "m_nStoreType");
	open_flag = fint(store, "m_bOpenFlag") ? 1 : 0;
	stock = global_struct(&gi_storestock, "g_sStoreStock", sn_storestock);
	stock_open = stock && sprite_live(fpage(fpage(stock, "m_sList"), "m_spBase"));
	if (store_type == 3)
		snprintf(id, n, "shop-buy");
	else if (open_flag || stock_open)
		snprintf(id, n, "vault-storage");
	else
		snprintf(id, n, "vault");
	return 1;
}

void rq_ui_layers(char *screen, int screen_n, char overlays[][24], int max_ov, int *n_ov)
{
	struct page *p;
	char vid[24];
	if (!screen || screen_n <= 0 || !n_ov)
		return;
	screen[0] = 0;
	*n_ov = 0;
	snos_init();
	collect_stack();
	if (vault_overlay_id(vid, (int)sizeof vid))
		layer_add(overlays, max_ov, n_ov, vid);
	p = global_struct(&gi_status, "g_sStatus", sn_status);
	if (p && sprite_live(fpage(p, "m_spBase")))
		layer_add(overlays, max_ov, n_ov, "status");
	p = global_struct(&gi_battlesel, "g_sBattleSelect", sn_battlesel);
	if (p && fint(p, "m_nZ") >= 0)
		layer_add(overlays, max_ov, n_ov, "battle");
	if (agent_id_live("preview-close"))
		layer_add(overlays, max_ov, n_ov, "item-previews");

	p = global_struct(&gi_select, "g_sSelect", sn_select);
	if (p && sprite_live(fpage(p, "m_spBase"))) {
		snprintf(screen, screen_n, "select");
		return;
	}
	p = global_struct(&gi_menu, "g_sSystemMenu", sn_sysmenu);
	if (p && fint(p, "m_nMode") > 0 && sprite_shown(fpage(p, "m_spBase"))) {
		snprintf(screen, screen_n, "system-menu");
		return;
	}
	p = global_struct(&gi_storequest, "g_sStoreQuest", sn_storequest);
	if (p && sprite_shown(fpage(p, "m_spBase"))) {
		snprintf(screen, screen_n, "quest-storage");
		return;
	}
	if (global_int(&gi_msgno, "g_nMessageNo") > 0) {
		snprintf(screen, screen_n, "message");
		return;
	}
	if (inner_screen()) {
		snprintf(screen, screen_n, "other");
		return;
	}
	p = global_struct(&gi_skillpoint, "g_sSkillPoint", sn_skillpoint);
	if (p && sprite_live(fpage(p, "m_spBase1"))) {
		snprintf(screen, screen_n, "skill-shop");
		return;
	}
	p = global_struct(&gi_party, "g_sParty", sn_party);
	if (p && sprite_live(fpage(p, "m_spBase"))) {
		snprintf(screen, screen_n, "party-edit");
		return;
	}
	p = global_struct(&gi_quest, "g_sQuest", sn_quest);
	if (p && sprite_shown(fpage(p, "m_spBase"))) {
		snprintf(screen, screen_n, "quests");
		return;
	}
	p = global_struct(&gi_shop, "g_sShop", sn_shop);
	if (p && sprite_live(fpage(p, "m_spBase"))) {
		snprintf(screen, screen_n, "frostbuy");
		return;
	}
	p = global_struct(&gi_mapparts, "g_sMapParts", sn_mapparts);
	if (p && sprite_shown(fpage(p, "m_spBase"))) {
		snprintf(screen, screen_n, "dungeon");
		return;
	}
	p = global_struct(&gi_castle, "g_sCastle", sn_castle);
	if (p && sprite_live(fpage(p, "m_spBase")) && stack_has("T_Castle@")) {
		snprintf(screen, screen_n, "castle");
		return;
	}
	p = global_struct(&gi_town, "g_sTown", sn_town);
	if (p && sprite_live(fpage(p, "m_spBase")) && stack_has("T_Town@")) {
		snprintf(screen, screen_n, "hub");
		return;
	}
	if (stack_has("T_Title") && !stack_has(U8_WARN)) {
		snprintf(screen, screen_n, "title");
		return;
	}
	snprintf(screen, screen_n, "other");
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
	g_nparty = 0;
	g_has_party = 0;
	g_nunits = 0;
	g_has_units = 0;
	g_nstore = 0;
	g_has_store = 0;
	memset(&g_sstate, 0, sizeof g_sstate);
	memset(&g_info, 0, sizeof g_info);
	memset(&g_pstate, 0, sizeof g_pstate);
	memset(&g_status, 0, sizeof g_status);
	memset(&g_qstate, 0, sizeof g_qstate);
	memset(&g_dstate, 0, sizeof g_dstate);
	memset(&g_bstate, 0, sizeof g_bstate);
	g_nquests = 0;
	memset(&g_vstate, 0, sizeof g_vstate);
	g_has_skillshop = 0;
	g_skill_page = 0;
	g_skill_pages = 0;
	g_skill_points = 0;
	g_nvitems = 0;
	g_has_vitems = 0;
	g_nvstock = 0;
	g_has_vstock = 0;
	collect_stack();
	find_busy();

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
	/* An unnamed screen that shows the footer (Trivia, CG mode, Music mode) lists it by name. */
	if (!strcmp(g_screen, "other"))
		emit_footer();
	}
	emit_agent_controls();
	emit_kit_controls();
	mark_covered();
	hover_agent();
	if (strcmp(g_screen, "boot") && strcmp(g_screen, "disclaimer") && strcmp(g_screen, "other"))
		g_seen_screen = 1;
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

static void publish_frame(void)
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

enum { SHOT_IDLE, SHOT_WANTED, SHOT_OK, SHOT_FAILED };
static atomic_int g_paused;
static atomic_int g_shot_state;
static char g_shot_path[512];
/* rqmod-ctl shot PATH window (2.67.0): taken before the swap, not from the game's picture. */
static atomic_int g_shot_window;

/* cgdump: the game's own CGs (past mod replacements) saved as PNG, on the
 * main thread because archive reads are not shared with the pipe thread. */
static atomic_int g_cgdump_state;
static int g_cgdump_from, g_cgdump_to;
static char g_cgdump_dir[400];
static char *g_cgdump_json;

static void service_cgdump(void)
{
	cJSON *root, *list;
	int no, n = 0;
	if (atomic_load(&g_cgdump_state) != SHOT_WANTED)
		return;
	root = cJSON_CreateObject();
	list = cJSON_AddArrayToObject(root, "cgs");
	for (no = g_cgdump_from; no <= g_cgdump_to; no++) {
		struct archive_data *d;
		struct cg *cg;
		char path[512];
		FILE *fp;
		cJSON *e;
		if (!asset_exists(ASSET_CG, no))
			continue;
		d = asset_get(ASSET_CG, no);
		if (!d)
			continue;
		cg = cg_load_data(d);
		if (!cg) {
			archive_free_data(d);
			continue;
		}
		snprintf(path, sizeof path, "%s/%d.png", g_cgdump_dir, no);
		fp = file_open_utf8(path, "wb");
		if (fp) {
			cg_write(cg, ALCG_PNG, fp);
			fclose(fp);
			e = cJSON_CreateObject();
			cJSON_AddNumberToObject(e, "no", no);
			cJSON_AddNumberToObject(e, "w", cg->metrics.w);
			cJSON_AddNumberToObject(e, "h", cg->metrics.h);
			if (d->name) {
				char *u = sjis2utf(d->name, 0);
				cJSON_AddStringToObject(e, "name", u ? u : d->name);
				free(u);
			}
			cJSON_AddItemToArray(list, e);
			n++;
		}
		cg_free(cg);
		archive_free_data(d);
	}
	cJSON_AddNumberToObject(root, "count", n);
	g_cgdump_json = cJSON_PrintUnformatted(root);
	cJSON_Delete(root);
	atomic_store(&g_cgdump_state, SHOT_OK);
}

/* flashdump (2.25.0): the bitmaps of a game Flash file saved as PNG, on the main
 * thread for the same reason as cgdump. */
static atomic_int g_flashdump_state;
static char g_flashdump_name[256];
static char g_flashdump_dir[400];
static char *g_flashdump_json;

struct flashdump_ctx {
	cJSON *list;
	int n;
};

static void flashdump_row(void *vctx, int id, int w, int h, const uint8_t *rgba, int mod, int used)
{
	struct flashdump_ctx *ctx = vctx;
	struct cg cg = { 0 };
	char path[512];
	FILE *fp;
	cJSON *e;
	(void)used;
	snprintf(path, sizeof path, "%s/%d_%dx%d.png", g_flashdump_dir, id, w, h);
	cg.metrics.w = w;
	cg.metrics.h = h;
	cg.metrics.bpp = 32;
	cg.metrics.has_pixel = true;
	cg.metrics.has_alpha = true;
	cg.metrics.pixel_pitch = w * 4;
	cg.pixels = (void *)rgba;
	fp = file_open_utf8(path, "wb");
	if (!fp)
		return;
	cg_write(&cg, ALCG_PNG, fp);
	fclose(fp);
	e = cJSON_CreateObject();
	cJSON_AddNumberToObject(e, "id", id);
	cJSON_AddNumberToObject(e, "w", w);
	cJSON_AddNumberToObject(e, "h", h);
	if (mod)
		cJSON_AddTrueToObject(e, "mod");
	cJSON_AddItemToArray(ctx->list, e);
	ctx->n++;
}

static void service_flashdump(void)
{
	struct flashdump_ctx ctx;
	cJSON *root;
	int all;
	if (atomic_load(&g_flashdump_state) != SHOT_WANTED)
		return;
	root = cJSON_CreateObject();
	ctx.list = cJSON_AddArrayToObject(root, "bitmaps");
	ctx.n = 0;
	all = rq_flash_bitmaps_each(g_flashdump_name, flashdump_row, &ctx);
	if (all < 0) {
		cJSON_Delete(root);
		atomic_store(&g_flashdump_state, SHOT_FAILED);
		return;
	}
	cJSON_AddNumberToObject(root, "count", ctx.n);
	g_flashdump_json = cJSON_PrintUnformatted(root);
	cJSON_Delete(root);
	atomic_store(&g_flashdump_state, ctx.n == all ? SHOT_OK : SHOT_FAILED + 1);
}

int rq_ui_cmd_flashdump(const char *args, char **json_out, char *err, int errsz)
{
	unsigned long long t0;
	const char *p = args ? args : "";
	size_t n;
	int st;
	*json_out = NULL;
	n = strcspn(p, " \t");
	if (!n || n >= sizeof g_flashdump_name || !p[n]) {
		if (err)
			snprintf(err, errsz, "usage: flashdump NAME DIR");
		return 0;
	}
	memcpy(g_flashdump_name, p, n);
	g_flashdump_name[n] = 0;
	p += n;
	while (*p == ' ' || *p == '\t')
		p++;
	if (!*p || strlen(p) >= sizeof g_flashdump_dir) {
		if (err)
			snprintf(err, errsz, "usage: flashdump NAME DIR");
		return 0;
	}
	if (!g_ready) {
		if (err)
			snprintf(err, errsz, "not-ready");
		return 0;
	}
	snprintf(g_flashdump_dir, sizeof g_flashdump_dir, "%s", p);
	g_flashdump_json = NULL;
	atomic_store(&g_flashdump_state, SHOT_WANTED);
	t0 = GetTickCount64();
	while ((st = atomic_load(&g_flashdump_state)) == SHOT_WANTED) {
		if (GetTickCount64() - t0 > 60000) {
			if (err)
				snprintf(err, errsz, "timeout");
			return 0;
		}
		Sleep(5);
	}
	atomic_store(&g_flashdump_state, SHOT_IDLE);
	if (st == SHOT_FAILED) {
		if (err)
			snprintf(err, errsz, "no Flash file %s in the game", g_flashdump_name);
		return 0;
	}
	if (st != SHOT_OK) {
		free(g_flashdump_json);
		g_flashdump_json = NULL;
		if (err)
			snprintf(err, errsz, "write-failed (is DIR there?)");
		return 0;
	}
	*json_out = g_flashdump_json;
	g_flashdump_json = NULL;
	return 1;
}

static void service_shot(void)
{
	int ok;
	service_cgdump();
	service_flashdump();
	if (g_lua_shot_wanted && !g_lua_shot_window) {
		ok = gfx_save_texture(gfx_main_surface(), g_lua_shot_path, ALCG_PNG);
		g_lua_shot_result = ok ? 1 : -1;
		g_lua_shot_wanted = 0;
	}
	if (atomic_load(&g_shot_state) != SHOT_WANTED || atomic_load(&g_shot_window))
		return;
	ok = gfx_save_texture(gfx_main_surface(), g_shot_path, ALCG_PNG);
	atomic_store(&g_shot_state, ok ? SHOT_OK : SHOT_FAILED);
}

/* A shot of the window as shown (2.67.0): its back buffer, drawn and not yet swapped. */
void rq_ui_before_swap(void)
{
	int ok;
	if (g_lua_shot_wanted && g_lua_shot_window) {
		ok = gfx_save_window(g_lua_shot_path);
		g_lua_shot_result = ok ? 1 : -1;
		g_lua_shot_wanted = 0;
		g_lua_shot_window = 0;
	}
	if (atomic_load(&g_shot_state) == SHOT_WANTED && atomic_load(&g_shot_window)) {
		ok = gfx_save_window(g_shot_path);
		atomic_store(&g_shot_window, 0);
		atomic_store(&g_shot_state, ok ? SHOT_OK : SHOT_FAILED);
	}
}

static int window_shot_waiting(void)
{
	return (g_lua_shot_wanted && g_lua_shot_window)
		|| (atomic_load(&g_shot_state) == SHOT_WANTED && atomic_load(&g_shot_window));
}

/* The VM stays inside this swap while paused. The window keeps answering, and
 * ui / shot keep working, because they are served from this loop. An exposed
 * window swaps again from handle_window_events, hence the guard. */
void rq_ui_on_present(void)
{
	static int in_pause;
	publish_frame();
	service_shot();
	if (in_pause || !atomic_load(&g_paused))
		return;
	in_pause = 1;
	while (atomic_load(&g_paused)) {
		handle_window_events();
		publish_frame();
		service_shot();
		/* A shot of the window is taken before a swap: the same frame shown again. */
		if (window_shot_waiting())
			gfx_swap();
		Sleep(5);
	}
	in_pause = 0;
	rq_stall_watch_reset();
}

void rq_ui_set_paused(int on)
{
	atomic_store(&g_paused, on ? 1 : 0);
}

int rq_ui_paused(void)
{
	return atomic_load(&g_paused);
}

int rq_ui_cmd_shot(const char *path, char *err, int errsz)
{
	unsigned long long t0;
	int st;
	char path_buf[512];
	int window = 0;
	size_t n = path ? strlen(path) : 0;
	/* shot PATH window: the window as shown, at its pixels (2.67.0). */
	while (n && (path[n - 1] == ' ' || path[n - 1] == '\t'))
		n--;
	if (n > 7 && !_strnicmp(path + n - 7, " window", 7)) {
		n -= 7;
		while (n && path[n - 1] == ' ')
			n--;
		window = 1;
	}
	if (!path || !n || n >= sizeof path_buf) {
		if (err)
			snprintf(err, errsz, "usage: shot PATH [window]");
		return 0;
	}
	memcpy(path_buf, path, n);
	path_buf[n] = 0;
	path = path_buf;
	if (!g_ready) {
		if (err)
			snprintf(err, errsz, "not-ready");
		return 0;
	}
	if (rq_dialog_open()) {
		if (err)
			snprintf(err, errsz, "native-dialog-open");
		return 0;
	}
	snprintf(g_shot_path, sizeof g_shot_path, "%s", path);
	atomic_store(&g_shot_window, window);
	atomic_store(&g_shot_state, SHOT_WANTED);
	t0 = GetTickCount64();
	while ((st = atomic_load(&g_shot_state)) == SHOT_WANTED) {
		if (GetTickCount64() - t0 > FRAME_MS) {
			if (err)
				snprintf(err, errsz, "timeout");
			return 0;
		}
		Sleep(2);
	}
	atomic_store(&g_shot_state, SHOT_IDLE);
	atomic_store(&g_shot_window, 0);
	if (st != SHOT_OK) {
		if (err)
			snprintf(err, errsz, "write-failed");
		return 0;
	}
	return 1;
}

int rq_ui_cmd_cgdump(const char *args, char **json_out, char *err, int errsz)
{
	int from = 0, to = 0, used = 0;
	unsigned long long t0;
	const char *dir;
	*json_out = NULL;
	if (sscanf(args ? args : "", "%d %d %n", &from, &to, &used) < 2 || used <= 0
	    || from < 1 || to < from || to - from > 20000) {
		if (err)
			snprintf(err, errsz, "usage: cgdump FROM TO DIR");
		return 0;
	}
	dir = args + used;
	if (!*dir || strlen(dir) >= sizeof g_cgdump_dir) {
		if (err)
			snprintf(err, errsz, "usage: cgdump FROM TO DIR");
		return 0;
	}
	if (!g_ready) {
		if (err)
			snprintf(err, errsz, "not-ready");
		return 0;
	}
	g_cgdump_from = from;
	g_cgdump_to = to;
	snprintf(g_cgdump_dir, sizeof g_cgdump_dir, "%s", dir);
	g_cgdump_json = NULL;
	atomic_store(&g_cgdump_state, SHOT_WANTED);
	t0 = GetTickCount64();
	while (atomic_load(&g_cgdump_state) == SHOT_WANTED) {
		if (GetTickCount64() - t0 > 60000) {
			if (err)
				snprintf(err, errsz, "timeout");
			return 0;
		}
		Sleep(5);
	}
	atomic_store(&g_cgdump_state, SHOT_IDLE);
	*json_out = g_cgdump_json;
	g_cgdump_json = NULL;
	return 1;
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

/* Ends early with success when a native dialog opens: it blocks the VM
 * thread, so the frames would never come. */
static int wait_frames_or_dialog(long long start, int frames, unsigned long ms)
{
	unsigned long long t0 = GetTickCount64(), next_dlg = 0;
	while (GetTickCount64() - t0 < ms) {
		unsigned long long now;
		if (rd_ll(&g_published) >= start + frames)
			return 1;
		now = GetTickCount64() - t0;
		if (now >= next_dlg) {
			if (rq_dialog_open())
				return 1;
			next_dlg = now + 50;
		}
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

/* Where a press aims inside a control, in percent of its box, first choice first. */
static const int g_aim_at[][2] = {
	{ 50, 50 }, { 50, 30 }, { 30, 50 }, { 30, 30 }, { 70, 30 },
	{ 50, 70 }, { 70, 50 }, { 20, 20 }, { 80, 20 }, { 20, 80 }, { 80, 80 }
};

/* Whether a point is inside one of the kit widgets listed (section "kit"). A comment window takes
 * no clicks, as the game's (2.13.0), and neither does a bar (2.14.0), so they cover nothing. */
static int kit_at(const struct ui_ctrl *a, int n, int px, int py)
{
	int i;
	for (i = 0; i < n; i++) {
		const struct ui_ctrl *o = &a[i];
		if (strcmp(o->section, "kit") != 0 || o->w <= 0 || o->h <= 0 || kit_passive(o))
			continue;
		if (px >= o->x && py >= o->y && px < o->x + o->w && py < o->y + o->h)
			return 1;
	}
	return 0;
}

/* A game control a player cannot click: every aim point under a kit widget of the last update, or
 * any while a mod page captures the mouse. The dump marks it and press refuses it (2.11.0). */
static void mark_covered(void)
{
	int i, k, capture = rq_hud_captured();
	for (i = 0; i < g_nctrls; i++) {
		struct ui_ctrl *c = &g_ctrls[i];
		c->covered = 0;
		if (!strcmp(c->section, "kit") || !strcmp(c->section, "agent") || c->w <= 0 || c->h <= 0)
			continue;
		c->covered = 1;
		if (capture)
			continue;
		for (k = 0; k < (int)(sizeof g_aim_at / sizeof g_aim_at[0]); k++) {
			if (!kit_at(g_ctrls, g_nctrls, c->x + c->w * g_aim_at[k][0] / 100,
				    c->y + c->h * g_aim_at[k][1] / 100)) {
				c->covered = 0;
				break;
			}
		}
	}
}

static int point_clear(const struct ui_snap *s, int self, int px, int py)
{
	int i, area = s->ctrl[self].w * s->ctrl[self].h;
	for (i = 0; i < s->nctrl; i++) {
		const struct ui_ctrl *o = &s->ctrl[i];
		if (i == self || o->w <= 0 || o->h <= 0 || o->w * o->h >= area)
			continue;
		if (px >= o->x && py >= o->y && px < o->x + o->w && py < o->y + o->h)
			return 0;
	}
	return 1;
}

/* Aim away from smaller controls inside this one, hidden ones included:
 * a member card spawns its status button over part of itself. */
static int ctrl_center(const char *id, int *cx, int *cy)
{
	struct ui_snap snap;
	int i, k;
	if (!copy_snap(&snap))
		return 0;
	for (i = 0; i < snap.nctrl; i++) {
		const struct ui_ctrl *c = &snap.ctrl[i];
		if (strcmp(c->id, id) != 0)
			continue;
		*cx = c->x + c->w / 2;
		*cy = c->y + c->h / 2;
		for (k = 0; k < (int)(sizeof g_aim_at / sizeof g_aim_at[0]); k++) {
			int px = c->x + c->w * g_aim_at[k][0] / 100;
			int py = c->y + c->h * g_aim_at[k][1] / 100;
			/* A game control is also aimed away from kit widgets over it (2.11.0). */
			if (point_clear(&snap, i, px, py) &&
			    (!strcmp(c->section, "kit") || !kit_at(snap.ctrl, snap.nctrl, px, py))) {
				*cx = px;
				*cy = py;
				break;
			}
		}
		return 1;
	}
	return 0;
}

/* Whether the last dump marked this control covered, and whether it is a kit widget (2.11.0). */
static int ctrl_covered(const char *id, int *kit)
{
	struct ui_snap snap;
	int i;
	*kit = 0;
	if (!copy_snap(&snap))
		return 0;
	for (i = 0; i < snap.nctrl; i++) {
		if (!strcmp(snap.ctrl[i].id, id)) {
			*kit = !strcmp(snap.ctrl[i].section, "kit");
			return snap.ctrl[i].covered;
		}
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
	/* A native message box blocks the VM thread: no frame will come. */
	if (rq_dialog_json(json_out))
		return 1;
	wants(1);
	start = rd_ll(&g_present_gen);
	if (!wait_frames_or_dialog(start, 1, FRAME_MS)) {
		wants(0);
		if (rq_dialog_json(json_out))
			return 1;
		if (err)
			snprintf(err, errsz, "timeout");
		return 0;
	}
	wants(0);
	if (rd_ll(&g_published) <= start && rq_dialog_json(json_out))
		return 1;
	copy_snap(&snap);
	if (json_out)
		*json_out = strdup(snap.json);
	return 1;
}

/* The game's choice menu (T_Select) selects a row only when the mouse moves: a mouse that already
 * rests on the row's centre selects nothing (Advance Worlds' "Go to World 3" sits where "Go to World 2"
 * was, and a player's clicks through the scene between do not move it). Such a mouse, and every second
 * try, goes a pixel to the side for a published frame first (2.56.0). */
static void hover_place(int cx, int cy, int nudge)
{
	int ax, ay, active;
	mouse_agent_pos(&ax, &ay, &active);
	if (nudge || (active && ax == cx && ay == cy)) {
		mouse_force_pos(cx + 1, cy);
		wait_published_after(rd_ll(&g_present_gen), FRAME_MS);
	}
	mouse_force_pos(cx, cy);
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
	if (rq_dialog_has(id))
		return rq_dialog_hover(id, err, errsz);
	if (rq_dialog_open()) {
		if (err)
			snprintf(err, errsz, "native-dialog-open");
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
	hover_place(cx, cy, 0);
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
		hover_place(cx, cy, 1);
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
	int ok, kit = 0;
	long long start;
	if (!id || !*id) {
		if (err)
			snprintf(err, errsz, "usage: press ID");
		return 0;
	}
	if (rq_dialog_has(id))
		return rq_dialog_press(id, err, errsz);
	if (rq_dialog_open()) {
		if (err)
			snprintf(err, errsz, "native-dialog-open");
		return 0;
	}
	if (ctrl_covered(id, &kit)) {
		if (err)
			snprintf(err, errsz, "covered by a mod's widget");
		return 0;
	}
	if (!hover_is(id)) {
		if (err)
			snprintf(err, errsz, "not-hovered");
		return 0;
	}
	/* Title buttons arm on mouse-up only after a frame where the
	 * decide key was down while the button was already selected.
	 * T_SystemMenu::select presents twice per checkInput, so each edge
	 * is held across PRESS_FRAMES presented frames. */
	/* ã‚­ãƒ¼_æ±ºå®š is left-click or Enter, and the click lands on release. */
	wants(1);
	key_set_state(VK_LBUTTON, 0);
	key_set_state(VK_RETURN, 0);
	start = rd_ll(&g_present_gen);
	ok = wait_frames_or_dialog(start, PRESS_FRAMES, FRAME_MS);
	if (ok && !hover_is(id))
		ok = 0;
	if (ok) {
		key_set_state(VK_LBUTTON, 1);
		/* A kit widget takes the click only. Enter would reach the game's
		 * control the mouse is over, under the widget (2.11.0). */
		if (!kit)
			key_set_state(VK_RETURN, 1);
		start = rd_ll(&g_present_gen);
		ok = wait_frames_or_dialog(start, PRESS_FRAMES, FRAME_MS);
	}
	key_set_state(VK_LBUTTON, 0);
	key_set_state(VK_RETURN, 0);
	if (ok && !rq_dialog_open()) {
		start = rd_ll(&g_present_gen);
		ok = wait_frames_or_dialog(start, PRESS_FRAMES, FRAME_MS);
	}
	wants(0);
	if (!ok) {
		if (err)
			snprintf(err, errsz, "timeout");
		return 0;
	}
	return 1;
}

static int ctrl_xy(const char *id, int *cx, int *cy)
{
	return ctrl_center(id, cx, cy);
}

/* Hold-drag from one control onto another. Magnum party swap/remove
 * and Frostbuy buy (equip-drag drop list) listen while LBUTTON stays
 * down across presented frames. DST may appear only after the hold
 * starts (shop buy → drop-vault), so resolve it again after press. */
int rq_ui_cmd_drag(const char *src, const char *dst, char *err, int errsz)
{
	long long start;
	int sx, sy, dx, dy, step, ok, wait_i;
	unsigned long long t0;

	if (!src || !*src || !dst || !*dst) {
		if (err)
			snprintf(err, errsz, "usage: drag SRC DST");
		return 0;
	}
	if (!g_ready) {
		if (err)
			snprintf(err, errsz, "not-ready");
		return 0;
	}
	if (rq_dialog_open()) {
		if (err)
			snprintf(err, errsz, "native-dialog-open");
		return 0;
	}
	if (!rq_ui_cmd_hover(src, err, errsz))
		return 0;
	if (!ctrl_xy(src, &sx, &sy)) {
		if (err)
			snprintf(err, errsz, "unknown-control");
		return 0;
	}
	dx = sx;
	dy = sy;
	ctrl_xy(dst, &dx, &dy);

	wants(1);
	key_set_state(VK_LBUTTON, 0);
	start = rd_ll(&g_present_gen);
	ok = wait_frames_or_dialog(start, PRESS_FRAMES, FRAME_MS);
	if (ok) {
		key_set_state(VK_LBUTTON, 1);
		start = rd_ll(&g_present_gen);
		ok = wait_frames_or_dialog(start, PRESS_FRAMES, FRAME_MS);
	}
	/* Shop buy creates the left drop list only after hold; wait for DST. */
	if (ok && !ctrl_xy(dst, &dx, &dy)) {
		for (wait_i = 0; wait_i < 45; wait_i++) {
			start = rd_ll(&g_present_gen);
			if (!wait_frames_or_dialog(start, 1, FRAME_MS)) {
				ok = 0;
				break;
			}
			if (ctrl_xy(dst, &dx, &dy))
				break;
		}
		if (ok && !ctrl_xy(dst, &dx, &dy)) {
			key_set_state(VK_LBUTTON, 0);
			wants(0);
			if (err)
				snprintf(err, errsz, "unknown-control");
			return 0;
		}
	}
	if (ok) {
		for (step = 1; step <= 8; step++) {
			int x = sx + (dx - sx) * step / 8;
			int y = sy + (dy - sy) * step / 8;
			mouse_force_pos(x, y);
			start = rd_ll(&g_present_gen);
			if (!wait_frames_or_dialog(start, 1, FRAME_MS)) {
				ok = 0;
				break;
			}
		}
	}
	if (ok) {
		mouse_force_pos(dx, dy);
		start = rd_ll(&g_present_gen);
		ok = wait_frames_or_dialog(start, PRESS_FRAMES, FRAME_MS);
	}
	key_set_state(VK_LBUTTON, 0);
	if (ok) {
		start = rd_ll(&g_present_gen);
		ok = wait_frames_or_dialog(start, PRESS_FRAMES, FRAME_MS);
	}
	/* Give the party / shop drag callback a moment to settle. */
	t0 = GetTickCount64();
	while (GetTickCount64() - t0 < 200)
		Sleep(10);
	wants(0);
	if (!ok) {
		if (err)
			snprintf(err, errsz, "timeout");
		return 0;
	}
	return 1;
}

static void copy_global_str(int *gi, const char *utf, char *dst, int n)
{
	struct string *s;
	char *u;
	dst[0] = 0;
	if (global_index(gi, utf) < 0 || !string_index_valid(global_get(*gi).i))
		return;
	s = heap_get_string(global_get(*gi).i);
	if (!s || s->size <= 0)
		return;
	u = sjis2utf(s->text, (size_t)s->size);
	if (!u)
		return;
	snprintf(dst, n, "%s", u);
	free(u);
}

static void battle_log_line(struct rq_qbattle *o, const char *text)
{
	if (!o || !text || !text[0] || o->n_log >= 24)
		return;
	snprintf(o->log[o->n_log], sizeof o->log[0], "%s", text);
	o->n_log++;
}

int rq_query_battle(struct rq_qbattle *o)
{
	struct battle_state b;
	struct page *sel, *log, *arr, *actor;
	static int gi_blog = -2;
	static int gi_log1 = -2, gi_log2 = -2, gi_nlog = -2;
	int i;
	char extra[180];

	if (!o)
		return 0;
	memset(o, 0, sizeof *o);
	if (!ain)
		return 0;
	snos_init();
	log = global_struct(&gi_blog, "g_sBattleLog", -1);
	arr = fpage(log, "m_asLog");
	if (arr && arr->type == ARRAY_PAGE) {
		for (i = 0; i < arr->nr_vars && o->n_log < 24; i++) {
			struct page *line = arr_at(arr, i);
			char buf[180];
			buf[0] = 0;
			fstr(line, "m_szText", buf, (int)sizeof buf);
			if (!buf[0])
				fstr(fpage(line, "m_sText"), "m_szText", buf, (int)sizeof buf);
			battle_log_line(o, buf);
		}
	}
	if (o->n_log == 0) {
		copy_global_str(&gi_log1, "◆戦闘ログ１", extra, (int)sizeof extra);
		battle_log_line(o, extra);
		copy_global_str(&gi_log2, "◆戦闘ログ２", extra, (int)sizeof extra);
		battle_log_line(o, extra);
		copy_global_str(&gi_nlog, "g_nLogText", extra, (int)sizeof extra);
		battle_log_line(o, extra);
	}
	sel = global_struct(&gi_battlesel, "g_sBattleSelect", sn_battlesel);
	if (!sel || fint(sel, "m_nZ") < 0)
		return 1;
	memset(&b, 0, sizeof b);
	actor = fpage(sel, "m_rfsSkill");
	b.actor_no = actor ? fint(actor, "m_nNo") : 0;
	b.selecting = b.actor_no > 0;
	fill_battle_units(&b);
	fill_battle_skills(&b, b.selecting ? actor : NULL);
	o->up = 1;
	for (i = 0; i < b.n_units; i++) {
		struct rq_qbattle_unit *d;
		if (!b.units[i].enemy && o->n_allies < 8)
			d = &o->allies[o->n_allies++];
		else if (b.units[i].enemy && o->n_enemies < 8)
			d = &o->enemies[o->n_enemies++];
		else
			continue;
		d->id = b.units[i].chara;
		d->no = b.units[i].no;
		snprintf(d->name, sizeof d->name, "%s", b.units[i].name);
	}
	for (i = 0; i < b.n_skills && o->n_skills < 48; i++) {
		struct rq_qbattle_skill *s = &o->skills[o->n_skills++];
		s->id = b.skills[i].skill;
		s->uses = b.skills[i].uses;
		s->enabled = b.skills[i].enabled;
		snprintf(s->name, sizeof s->name, "%s", b.skills[i].name);
	}
	return 1;
}

/* ---- rq.ui: the rqmod-ctl dump, for Lua on the VM thread. The pipe versions
 * wait for frames, which would never come from inside a frame. ---- */

static void push_json(lua_State *L, const cJSON *j)
{
	const cJSON *c;
	int n = 0;

	if (cJSON_IsObject(j)) {
		lua_newtable(L);
		cJSON_ArrayForEach(c, j) {
			push_json(L, c);
			lua_setfield(L, -2, c->string);
		}
	} else if (cJSON_IsArray(j)) {
		lua_newtable(L);
		cJSON_ArrayForEach(c, j) {
			push_json(L, c);
			lua_rawseti(L, -2, ++n);
		}
	} else if (cJSON_IsString(j)) {
		lua_pushstring(L, j->valuestring);
	} else if (cJSON_IsNumber(j)) {
		if (j->valuedouble == (double)(lua_Integer)j->valuedouble)
			lua_pushinteger(L, (lua_Integer)j->valuedouble);
		else
			lua_pushnumber(L, j->valuedouble);
	} else if (cJSON_IsBool(j)) {
		lua_pushboolean(L, cJSON_IsTrue(j));
	} else {
		lua_pushnil(L);
	}
}

int rq_lua_ui_get(lua_State *L)
{
	static struct ui_snap snap;
	cJSON *root;

	if (!g_ready) {
		lua_pushnil(L);
		return 1;
	}
	publish();
	g_lua_built = 1;
	if (!copy_snap(&snap) || !(root = cJSON_Parse(snap.json))) {
		lua_pushnil(L);
		return 1;
	}
	push_json(L, root);
	cJSON_Delete(root);
	return 1;
}

/* The point a click on this control should land on, or nil. */
int rq_lua_dungeon_grid(lua_State *L)
{
	char err[64] = "";
	char *json = NULL;
	cJSON *root;
	int whole = 0;

	if (lua_istable(L, 1)) {
		lua_getfield(L, 1, "whole");
		whole = lua_toboolean(L, -1);
		lua_pop(L, 1);
	}
	if (!rq_path_grid(&json, whole, err, (int)sizeof err)) {
		lua_pushnil(L);
		lua_pushstring(L, err);
		return 2;
	}
	root = cJSON_Parse(json);
	free(json);
	if (!root) {
		lua_pushnil(L);
		lua_pushliteral(L, "json");
		return 2;
	}
	push_json(L, root);
	cJSON_Delete(root);
	return 1;
}

int rq_lua_ui_aim(lua_State *L)
{
	const char *id = luaL_checkstring(L, 1);
	int x, y;

	if (!g_ready) {
		lua_pushnil(L);
		return 1;
	}
	if (!g_lua_built) {
		publish();
		g_lua_built = 1;
	}
	if (!ctrl_center(id, &x, &y)) {
		lua_pushnil(L);
		return 1;
	}
	lua_pushinteger(L, x);
	lua_pushinteger(L, y);
	return 2;
}

/* Saved at the next drawn frame, because waiting for it here would hang the VM thread. */
int rq_lua_ui_shot(lua_State *L)
{
	const char *rel = luaL_checkstring(L, 1);
	char full[1024];

	if (!g_ready)
		return luaL_error(L, "rq.ui.shot: the game is not running yet");
	if (!rq_sandbox_resolve(L, rel, full, (int)sizeof full))
		return luaL_error(L, "rq.ui.shot: %s is outside the mod folder", rel);
	if (strlen(full) >= sizeof g_lua_shot_path)
		return luaL_error(L, "rq.ui.shot: path too long");
	if (g_lua_shot_wanted)
		return luaL_error(L, "rq.ui.shot: another shot is waiting for a frame");
	int window = 0;
	if (!lua_isnoneornil(L, 2)) {
		luaL_checktype(L, 2, LUA_TTABLE);
		lua_getfield(L, 2, "window");
		window = lua_toboolean(L, -1);
		lua_pop(L, 1);
	}
	snprintf(g_lua_shot_path, sizeof g_lua_shot_path, "%s", full);
	g_lua_shot_result = 0;
	g_lua_shot_window = window;
	g_lua_shot_wanted = 1;
	return 0;
}

/* true or false once, then nil again. nil also means the shot is still waiting. */
int rq_lua_ui_shot_result(lua_State *L)
{
	if (g_lua_shot_wanted || !g_lua_shot_result) {
		lua_pushnil(L);
		return 1;
	}
	lua_pushboolean(L, g_lua_shot_result > 0);
	g_lua_shot_result = 0;
	return 1;
}

/* The window: its size in points and pixels, the game's picture in it, full screen, the 3D. */
int rq_lua_ui_window(lua_State *L)
{
	struct gfx_window_info info;
	gfx_window_info(&info);
	lua_createtable(L, 0, 12);
	lua_pushinteger(L, info.w);
	lua_setfield(L, -2, "w");
	lua_pushinteger(L, info.h);
	lua_setfield(L, -2, "h");
	lua_pushinteger(L, info.px_w);
	lua_setfield(L, -2, "pixels_w");
	lua_pushinteger(L, info.px_h);
	lua_setfield(L, -2, "pixels_h");
	lua_createtable(L, 0, 4);
	lua_pushinteger(L, info.view_x);
	lua_setfield(L, -2, "x");
	lua_pushinteger(L, info.view_y);
	lua_setfield(L, -2, "y");
	lua_pushinteger(L, info.view_w);
	lua_setfield(L, -2, "w");
	lua_pushinteger(L, info.view_h);
	lua_setfield(L, -2, "h");
	lua_setfield(L, -2, "view");
	lua_pushnumber(L, info.view_w / 1024.0);
	lua_setfield(L, -2, "scale");
	lua_pushboolean(L, info.fullscreen);
	lua_setfield(L, -2, "fullscreen");
	lua_pushboolean(L, info.sharp_3d);
	lua_setfield(L, -2, "sharp_3d");
	lua_pushboolean(L, info.sharp_3d_shown);
	lua_setfield(L, -2, "sharp_3d_shown");
	if (info.sharp_3d_shown) {
		lua_pushinteger(L, info.sharp_3d_w);
		lua_setfield(L, -2, "sharp_3d_w");
		lua_pushinteger(L, info.sharp_3d_h);
		lua_setfield(L, -2, "sharp_3d_h");
	}
	return 1;
}

/* The window's size in points (out of full screen). */
int rq_lua_ui_set_window_size(lua_State *L)
{
	lua_Integer w = luaL_checkinteger(L, 1), h = luaL_checkinteger(L, 2);
	if (w < 64 || h < 48 || w > 16384 || h > 16384)
		return luaL_error(L, "rq.ui.set_window_size: %d by %d is not a window's size", (int)w, (int)h);
	lua_pushboolean(L, gfx_set_window_size((int)w, (int)h));
	return 1;
}

#endif /* RQ_MODLOADER */
