/* Optional Discord Rich Presence for the Rance Quest modding fork.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Local IPC only (Discord's named pipe). No client secret and no public key.
 */

#ifdef RQ_MODLOADER

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <math.h>

#ifdef _WIN32
typedef void *rq_handle;
__declspec(dllimport) rq_handle __stdcall CreateFileA(const char *, unsigned long, unsigned long, void *, unsigned long, unsigned long, rq_handle);
__declspec(dllimport) int __stdcall WriteFile(rq_handle, const void *, unsigned long, unsigned long *, void *);
__declspec(dllimport) int __stdcall ReadFile(rq_handle, void *, unsigned long, unsigned long *, void *);
__declspec(dllimport) int __stdcall PeekNamedPipe(rq_handle, void *, unsigned long, unsigned long *, unsigned long *, unsigned long *);
__declspec(dllimport) int __stdcall CloseHandle(rq_handle);
__declspec(dllimport) int __stdcall MoveFileExA(const char *, const char *, unsigned long);
__declspec(dllimport) unsigned long __stdcall GetCurrentProcessId(void);
__declspec(dllimport) unsigned long long __stdcall GetTickCount64(void);
__declspec(dllimport) unsigned long __stdcall GetEnvironmentVariableA(const char *, char *, unsigned long);
#define RQ_GENERIC_READ 0x80000000u
#define RQ_GENERIC_WRITE 0x40000000u
#define RQ_OPEN_EXISTING 3u
#define RQ_MOVE_REPLACE 0x1u
#define RQ_MOVE_WRITE_THROUGH 0x8u
#define RQ_INVALID_HANDLE ((rq_handle)(intptr_t)-1)
#endif

#include "system4.h"
#include "system4/ain.h"
#include "system4/buffer.h"
#include "system4/ini.h"
#include "system4/instructions.h"
#include "system4/little_endian.h"
#include "system4/string.h"

#include "cJSON.h"
#include "gfx/font.h"
#include "gfx/gfx.h"
#include "sact.h"
#include "sprite.h"
#include "vm.h"
#include "vm/heap.h"
#include "vm/page.h"
#include "modloader.h"
#include "modloader_priv.h"

/* Public application id. Forks override it; this is not a secret. */
#define RQ_DISCORD_APP_DEFAULT "1565476265475964938"
#define RQ_CHECK_INDEX 2
#define RQ_CHECK_X 70
#define RQ_CHECK_Y 275
#define RQ_LABEL_X 112
#define RQ_CHECK_CG 40056
/* Empty box baked into the Other 2 panel (CG 40007) for the second row. */
#define RQ_BOX_X 71
#define RQ_BOX_Y 206
#define RQ_BOX_W 28
#define RQ_BOX_H 28
#define RQ_BOX_DST_Y 276
/* Vanilla row-2 glyphs start 8px below the square's top. */
#define RQ_LABEL_DY 8

static int g_inited;
static int g_disabled;
static int g_ui;
static char g_app_id[32];
static int g_f_check = -1;
static int g_f_show = -1;
static int g_f_group_show = -1;
static int g_f_group_z = -1;

static int g_exploring;
static int64_t g_start;
static int g_sent;
static int g_sent_exploring;
static int64_t g_sent_start;
static int g_sent_after_ready;
static unsigned g_nonce;
static int g_save_warned;

#ifdef _WIN32
static rq_handle g_pipe;
static int g_connected;
static unsigned long long g_last_try;
static char g_in[8192];
static int g_in_len;
#endif

static struct text_style g_label_ts;

static int id_ok(const char *s)
{
	int n = 0;
	if (!s || !s[0])
		return 0;
	for (; s[n]; n++) {
		if (s[n] < '0' || s[n] > '9' || n >= 20)
			return 0;
	}
	return 1;
}

static void set_app_id(const char *s)
{
	if (!id_ok(s))
		return;
	snprintf(g_app_id, sizeof g_app_id, "%s", s);
}

static void read_ini_id(const char *path)
{
	int n, i;
	struct ini_entry *ini = ini_parse(path, &n);
	if (!ini)
		return;
	for (i = 0; i < n; i++) {
		if (ini[i].name && !strcmp(ini[i].name->text, "DiscordApplicationId")
		    && ini[i].value.type == INI_STRING && ini[i].value.s)
			set_app_id(ini[i].value.s->text);
		ini_free_entry(&ini[i]);
	}
	free(ini);
}

static void config_path(char *dst, size_t n)
{
	snprintf(dst, n, "%s\\config.json", g_rq_exe_dir);
}

static cJSON *load_json(void)
{
	char path[RQ_PATH_MAX];
	FILE *fp;
	long sz;
	char *buf;
	cJSON *root;
	config_path(path, sizeof path);
	fp = fopen(path, "rb");
	if (!fp)
		return NULL;
	if (fseek(fp, 0, SEEK_END) != 0) {
		fclose(fp);
		return NULL;
	}
	sz = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	if (sz < 0 || sz > (1 << 20)) {
		fclose(fp);
		return NULL;
	}
	buf = malloc((size_t)sz + 1);
	if (!buf) {
		fclose(fp);
		return NULL;
	}
	sz = (long)fread(buf, 1, (size_t)sz, fp);
	fclose(fp);
	buf[sz] = 0;
	root = cJSON_Parse(buf);
	free(buf);
	return root;
}

static void read_json_settings(void)
{
	cJSON *root, *id, *dis;
	root = load_json();
	if (!root)
		return;
	id = cJSON_GetObjectItemCaseSensitive(root, "discordApplicationId");
	if (cJSON_IsString(id))
		set_app_id(id->valuestring);
	dis = cJSON_GetObjectItemCaseSensitive(root, "disableDiscordRichPresence");
	if (cJSON_IsBool(dis))
		g_disabled = cJSON_IsTrue(dis);
	else if (cJSON_IsNumber(dis))
		g_disabled = dis->valuedouble != 0;
	cJSON_Delete(root);
}

static void save_disabled(void)
{
	char path[RQ_PATH_MAX], tmp[RQ_PATH_MAX];
	cJSON *root, *cur;
	char *text;
	FILE *fp;
	config_path(path, sizeof path);
	snprintf(tmp, sizeof tmp, "%s.tmp", path);
	root = load_json();
	if (!root)
		root = cJSON_CreateObject();
	if (!root) {
		if (!g_save_warned) {
			g_save_warned = 1;
			rq_log("Discord presence: could not write config.json");
		}
		return;
	}
	if (!cJSON_GetObjectItemCaseSensitive(root, "discordApplicationId"))
		cJSON_AddStringToObject(root, "discordApplicationId", g_app_id);
	cur = cJSON_GetObjectItemCaseSensitive(root, "disableDiscordRichPresence");
	if (cur)
		cJSON_ReplaceItemInObjectCaseSensitive(root, "disableDiscordRichPresence",
						       cJSON_CreateBool(g_disabled));
	else
		cJSON_AddBoolToObject(root, "disableDiscordRichPresence", g_disabled);
	text = cJSON_Print(root);
	cJSON_Delete(root);
	if (!text) {
		if (!g_save_warned) {
			g_save_warned = 1;
			rq_log("Discord presence: could not write config.json");
		}
		return;
	}
	fp = fopen(tmp, "wb");
	if (!fp) {
		free(text);
		if (!g_save_warned) {
			g_save_warned = 1;
			rq_log("Discord presence: could not write config.json");
		}
		return;
	}
	fwrite(text, 1, strlen(text), fp);
	fputc('\n', fp);
	fclose(fp);
	free(text);
#ifdef _WIN32
	if (!MoveFileExA(tmp, path, RQ_MOVE_REPLACE | RQ_MOVE_WRITE_THROUGH)) {
		if (!g_save_warned) {
			g_save_warned = 1;
			rq_log("Discord presence: could not replace config.json");
		}
	}
#else
	if (rename(tmp, path) != 0 && !g_save_warned) {
		g_save_warned = 1;
		rq_log("Discord presence: could not replace config.json");
	}
#endif
}

static void load_settings(void)
{
	char path[RQ_PATH_MAX];
	snprintf(g_app_id, sizeof g_app_id, "%s", RQ_DISCORD_APP_DEFAULT);
	g_disabled = 0;
	snprintf(path, sizeof path, "%s\\System40.ini", g_rq_game_dir);
	read_ini_id(path);
	snprintf(path, sizeof path, "%s\\System40.ini", g_rq_exe_dir);
	read_ini_id(path);
	read_json_settings();
}

#ifdef _WIN32
static void discord_disconnect(void)
{
	uint32_t hdr[2];
	unsigned long wrote = 0;
	if (g_pipe && g_pipe != RQ_INVALID_HANDLE) {
		hdr[0] = 2;
		hdr[1] = 0;
		WriteFile(g_pipe, hdr, sizeof hdr, &wrote, NULL);
		CloseHandle(g_pipe);
	}
	g_pipe = NULL;
	g_connected = 0;
	g_sent = 0;
	g_sent_after_ready = 0;
	g_in_len = 0;
	g_last_try = GetTickCount64();
}

static int write_full(rq_handle h, const void *buf, unsigned n)
{
	const unsigned char *p = buf;
	while (n) {
		unsigned long w = 0;
		if (!WriteFile(h, p, n, &w, NULL) || w == 0)
			return 0;
		p += w;
		n -= w;
	}
	return 1;
}

static int send_frame(uint32_t op, const char *json)
{
	uint32_t hdr[2];
	uint32_t len = json ? (uint32_t)strlen(json) : 0;
	if (!g_pipe || g_pipe == RQ_INVALID_HANDLE)
		return 0;
	hdr[0] = op;
	hdr[1] = len;
	if (!write_full(g_pipe, hdr, 8))
		return 0;
	if (len && !write_full(g_pipe, json, len))
		return 0;
	return 1;
}

static int send_activity(void)
{
	char body[512];
	const char *details;
	unsigned nonce = ++g_nonce;
	details = g_exploring ? "Exploring a Quest" : "Getting Ready for Quest";
	if (g_exploring && g_start > 0) {
		snprintf(body, sizeof body,
			 "{\"cmd\":\"SET_ACTIVITY\",\"nonce\":\"%u\",\"args\":{\"pid\":%lu,\"activity\":{\"details\":\"%s\",\"timestamps\":{\"start\":%lld}}}}",
			 nonce, (unsigned long)GetCurrentProcessId(), details, (long long)g_start);
	} else {
		snprintf(body, sizeof body,
			 "{\"cmd\":\"SET_ACTIVITY\",\"nonce\":\"%u\",\"args\":{\"pid\":%lu,\"activity\":{\"details\":\"%s\"}}}",
			 nonce, (unsigned long)GetCurrentProcessId(), details);
	}
	return send_frame(1, body);
}

static int connect_pipe(void)
{
	char hello[96];
	char override_name[128];
	int i, ntry;
	unsigned long nenv = GetEnvironmentVariableA("RQ_DISCORD_PIPE", override_name, sizeof override_name);
	ntry = (nenv > 0 && nenv < sizeof override_name) ? 1 : 10;
	for (i = 0; i < ntry; i++) {
		char name[160];
		rq_handle h;
		if (ntry == 1)
			snprintf(name, sizeof name, "%s", override_name);
		else
			snprintf(name, sizeof name, "\\\\.\\pipe\\discord-ipc-%d", i);
		h = CreateFileA(name, RQ_GENERIC_READ | RQ_GENERIC_WRITE, 0, NULL, RQ_OPEN_EXISTING, 0, NULL);
		if (h == RQ_INVALID_HANDLE)
			continue;
		g_pipe = h;
		g_connected = 1;
		g_in_len = 0;
		g_sent = 0;
		g_sent_after_ready = 0;
		snprintf(hello, sizeof hello, "{\"v\":1,\"client_id\":\"%s\"}", g_app_id);
		if (!send_frame(0, hello)) {
			discord_disconnect();
			return 0;
		}
		return 1;
	}
	return 0;
}

static void drain_pipe(void)
{
	unsigned long avail = 0, got = 0;
	if (!g_pipe)
		return;
	if (!PeekNamedPipe(g_pipe, NULL, 0, NULL, &avail, NULL)) {
		discord_disconnect();
		return;
	}
	if (avail > 0) {
		int room = (int)sizeof g_in - g_in_len;
		if (room <= 0) {
			discord_disconnect();
			return;
		}
		if (avail > (unsigned long)room)
			avail = (unsigned long)room;
		if (!ReadFile(g_pipe, g_in + g_in_len, avail, &got, NULL)) {
			discord_disconnect();
			return;
		}
		g_in_len += (int)got;
	}
	while (g_in_len >= 8) {
		uint32_t op, len;
		memcpy(&op, g_in, 4);
		memcpy(&len, g_in + 4, 4);
		if (len > 65536 || g_in_len < 8 + (int)len) {
			if (len > 65536)
				discord_disconnect();
			return;
		}
		if (op == 2) {
			discord_disconnect();
			return;
		}
		if (!g_sent_after_ready) {
			g_sent_after_ready = 1;
			g_sent = 0;
		}
		g_in_len -= 8 + (int)len;
		if (g_in_len > 0)
			memmove(g_in, g_in + 8 + len, (size_t)g_in_len);
	}
}
#else
static void discord_disconnect(void) {}
#endif

static void note_exploring(void)
{
	int ex = rq_dungeon_exploring() ? 1 : 0;
	if (ex && !g_exploring)
		g_start = (int64_t)time(NULL);
	if (!ex)
		g_start = 0;
	g_exploring = ex;
}

void rq_discord_tick(void)
{
#ifdef _WIN32
	unsigned long long now;
#endif
	if (!g_inited)
		return;
	note_exploring();
	if (g_disabled) {
#ifdef _WIN32
		if (g_connected)
			discord_disconnect();
#endif
		return;
	}
#ifdef _WIN32
	if (!g_connected) {
		now = GetTickCount64();
		if (g_last_try && now - g_last_try < 5000)
			return;
		g_last_try = now;
		if (!connect_pipe())
			return;
	} else {
		drain_pipe();
		if (!g_connected)
			return;
	}
	if (g_sent && g_sent_exploring == g_exploring && g_sent_start == g_start)
		return;
	if (!send_activity()) {
		discord_disconnect();
		return;
	}
	g_sent = 1;
	g_sent_exploring = g_exploring;
	g_sent_start = g_start;
#else
	(void)0;
#endif
}

static struct page *safe_page(int slot)
{
	if (!page_index_valid(slot))
		return NULL;
	return heap_get_page(slot);
}

static void show_mark(struct page *cfg, int on)
{
	struct page *arr;
	int slot;
	if (!cfg || cfg->nr_vars < 4 || g_f_show < 0)
		return;
	arr = safe_page(cfg->values[3].i);
	if (!arr || arr->type != ARRAY_PAGE || arr->nr_vars <= RQ_CHECK_INDEX)
		return;
	slot = arr->values[RQ_CHECK_INDEX].i;
	if (!safe_page(slot))
		return;
	stack_push(on ? 1 : 0);
	vm_call_method(g_f_show, slot);
	stack_pop();
}

/* The other two rows already have this square in the panel art. */
static void blit_empty_box(struct sact_sprite *sp)
{
	Texture *tex = sprite_get_texture(sp);
	Texture tmp;
	if (!tex || tex->w < RQ_BOX_X + RQ_BOX_W || tex->h < RQ_BOX_DST_Y + RQ_BOX_H)
		return;
	gfx_init_texture_rgba(&tmp, RQ_BOX_W, RQ_BOX_H, (SDL_Color){ 0, 0, 0, 0 });
	gfx_copy(&tmp, 0, 0, tex, RQ_BOX_X, RQ_BOX_Y, RQ_BOX_W, RQ_BOX_H);
	gfx_copy_amap(&tmp, 0, 0, tex, RQ_BOX_X, RQ_BOX_Y, RQ_BOX_W, RQ_BOX_H);
	gfx_copy(tex, RQ_BOX_X, RQ_BOX_DST_Y, &tmp, 0, 0, RQ_BOX_W, RQ_BOX_H);
	gfx_copy_amap(tex, RQ_BOX_X, RQ_BOX_DST_Y, &tmp, 0, 0, RQ_BOX_W, RQ_BOX_H);
	gfx_delete_texture(&tmp);
	sprite_dirty(sp);
}

static struct sact_sprite *base_sprite(struct page *cfg)
{
	struct page *ts, *inner;
	if (!cfg || cfg->nr_vars < 1)
		return NULL;
	ts = safe_page(cfg->values[0].i);
	if (!ts || ts->nr_vars < 1)
		return NULL;
	inner = safe_page(ts->values[0].i);
	if (!inner || inner->nr_vars < 1)
		return NULL;
	return sact_try_get_sprite(inner->values[0].i);
}

/* Drawing uses the texture size, not the sprite rect, so a wider rect
 * alone leaves the yellow bar at the stock 280px. */
static void stretch_frame(struct sact_sprite *sp, int width)
{
	Texture *tex, neu;
	SDL_Color c;
	tex = sprite_get_texture(sp);
	if (!tex || tex->w < 1 || tex->h < 1 || sp->texture_unowned)
		return;
	if (tex->w >= width && sp->rect.w >= width)
		return;
	c = gfx_get_pixel(tex, tex->w / 2, tex->h / 2);
	if (!c.a)
		return;
	gfx_init_texture_rgba(&neu, width, tex->h, c);
	gfx_delete_texture(tex);
	*tex = neu;
	sp->rect.w = width;
	if (sp->surface_area.w > 0)
		sp->surface_area.w = width;
	sprite_dirty(sp);
}

/* The stock frame is 280px, which ends before this longer label. Stretch
 * the yellow frame and its hit rect so the bar ends just past the text. */
static void widen_row(struct page *cfg, int width)
{
	struct page *group, *arr, *btn, *frames, *fr, *rect, *ts, *inner;
	struct sact_sprite *sp;
	int i, f, spno;
	if (width < 280)
		width = 280;
	group = safe_page(cfg->values[1].i);
	if (!group || group->nr_vars < 1)
		return;
	arr = safe_page(group->values[0].i);
	if (!arr || arr->type != ARRAY_PAGE)
		return;
	for (i = 0; i < arr->nr_vars; i++) {
		btn = safe_page(arr->values[i].i);
		if (!btn || btn->nr_vars < 2)
			continue;
		if (btn->values[1].i != 210001 + RQ_CHECK_INDEX)
			continue;
		frames = safe_page(btn->values[0].i);
		if (!frames || frames->type != ARRAY_PAGE)
			return;
		for (f = 0; f < frames->nr_vars; f++) {
			fr = safe_page(frames->values[f].i);
			if (!fr || fr->nr_vars < 3)
				continue;
			rect = safe_page(fr->values[1].i);
			if (rect && rect->nr_vars >= 4)
				rect->values[2].i = rect->values[0].i + width;
			ts = safe_page(fr->values[2].i);
			if (!ts || ts->nr_vars < 1)
				continue;
			inner = safe_page(ts->values[0].i);
			if (!inner || inner->nr_vars < 1)
				continue;
			spno = inner->values[0].i;
			sp = sact_try_get_sprite(spno);
			if (sp)
				stretch_frame(sp, width);
		}
		return;
	}
}

static void add_checkbox(void)
{
	struct page *cfg;
	struct sact_sprite *sp;
	struct string *label;
	int slot, rc;
	static int warned;
	const char *text = "Disable Discord Rich Presence";
	if (g_f_check < 0)
		return;
	slot = vm_frame_struct(0);
	cfg = safe_page(slot);
	if (!cfg)
		return;
	stack_push(RQ_CHECK_INDEX);
	stack_push(RQ_CHECK_X);
	stack_push(RQ_CHECK_Y);
	stack_push(RQ_CHECK_CG);
	stack_push(vm_string_ref(&EMPTY_STRING));
	vm_call_method(g_f_check, slot);
	rc = stack_pop().i;
	if (!rc && !warned) {
		warned = 1;
		rq_log("Discord presence: Other 2 checkbox was not created");
	}
	/* Same Z as the other rows, so the game's own yellow frame shows
	 * only while this button is selected and hides when the cursor leaves. */
	if (page_index_valid(cfg->values[1].i)) {
		if (g_f_group_z >= 0) {
			stack_push(15001);
			vm_call_method(g_f_group_z, cfg->values[1].i);
		}
		if (g_f_group_show >= 0) {
			stack_push(1);
			vm_call_method(g_f_group_show, cfg->values[1].i);
		}
	}
	show_mark(cfg, g_disabled);
	sp = base_sprite(cfg);
	if (!sp || sprite_get_width(sp) < 32 || sprite_get_height(sp) < 32)
		return;
	blit_empty_box(sp);
	label = make_string(text, strlen(text));
	if (!label)
		return;
	sprite_text_clear(sp);
	sprite_set_text_pos(sp, RQ_LABEL_X, RQ_BOX_DST_Y + RQ_LABEL_DY);
	sprite_text_draw(sp, label, &g_label_ts);
	widen_row(cfg, (RQ_LABEL_X + (int)ceilf(gfx_size_text(&g_label_ts, text)) + 22) - RQ_CHECK_X);
	free_string(label);
}

int rq_discord_syscall(int code)
{
	switch (code) {
	case VM_XSYS_DISCORD_ADD:
		add_checkbox();
		return 1;
	case VM_XSYS_DISCORD_SHOW:
		show_mark(get_struct_page(0), g_disabled);
		return 1;
	case VM_XSYS_DISCORD_TOGGLE:
		g_disabled = !g_disabled;
		save_disabled();
		if (g_disabled)
			discord_disconnect();
#ifdef _WIN32
		else
			g_last_try = 0;
#endif
		return 1;
	default:
		return 0;
	}
}

static void emit0(struct buffer *out, enum opcode op)
{
	buffer_write_int16(out, (uint16_t)op);
}

static void emit1(struct buffer *out, enum opcode op, int32_t arg)
{
	buffer_write_int16(out, (uint16_t)op);
	buffer_write_int32(out, (uint32_t)arg);
}

static void commit_wrap(struct ain *ain, int fno, struct buffer *out)
{
	ain->functions[fno].address = (uint32_t)(ain->code_size + 6);
	ain->code = xrealloc(ain->code, ain->code_size + out->index);
	memcpy(ain->code + ain->code_size, out->buf, out->index);
	ain->code_size += out->index;
	free(out->buf);
	out->buf = NULL;
}

static int fn_index(struct ain *ain, const char *name)
{
	return ain_get_function(ain, (char *)name);
}

static int decode(struct ain *ain, uint32_t ip, int *op, int32_t *args, int *width)
{
	unsigned opc;
	int n, i;
	if (ip + 2 > (uint32_t)ain->code_size)
		return 0;
	opc = (unsigned)(uint16_t)LittleEndian_getW(ain->code, (int)ip) & 0x3FFFu;
	if (opc >= NR_OPCODES)
		return 0;
	n = instructions[opc].nr_args;
	if (n < 0 || n > 4 || ip + 2 + (uint32_t)n * 4 > (uint32_t)ain->code_size)
		return 0;
	*op = (int)opc;
	for (i = 0; i < n; i++)
		args[i] = LittleEndian_getDW(ain->code, (int)ip + 2 + 4 * i);
	for (; i < 4; i++)
		args[i] = 0;
	*width = 2 + n * 4;
	return 1;
}

static int patch_u32(struct ain *ain, uint32_t ip, int arg, int32_t expect, int32_t val)
{
	uint32_t at = ip + 2 + (uint32_t)arg * 4;
	if (LittleEndian_getDW(ain->code, (int)at) != expect)
		return 0;
	LittleEndian_putDW(ain->code, (int)at, (uint32_t)val);
	return 1;
}

static int patch_alloc(struct ain *ain, int fno)
{
	uint32_t ip = ain->functions[fno].address;
	int op, w, i;
	int32_t a[4];
	static const int want_op[] = { PUSHSTRUCTPAGE, PUSH, PUSH, PUSH, A_ALLOC };
	static const int want_push[] = { 0, 3, 2, 1, 0 };
	uint32_t size_ip = 0;
	for (i = 0; i < 5; i++) {
		if (!decode(ain, ip, &op, a, &w) || op != want_op[i])
			return 0;
		if (op == PUSH && a[0] != want_push[i])
			return 0;
		if (i == 2)
			size_ip = ip;
		ip += (uint32_t)w;
	}
	return patch_u32(ain, size_ip, 0, 2, 3);
}

static int patch_limit(struct ain *ain, int fno)
{
	uint32_t ip = ain->functions[fno].address;
	int steps = 0, op, w, n = 0;
	while (steps++ < 48) {
		int32_t a[4];
		if (!decode(ain, ip, &op, a, &w))
			return 0;
		if (op == FUNC || op == ENDFUNC)
			break;
		if (op == SH_LOC_LT_IMM_OR_LOC_GE_IMM && a[2] == 2) {
			if (!patch_u32(ain, ip, 2, 2, 3))
				return 0;
			n++;
		}
		ip += (uint32_t)w;
	}
	return n == 1;
}

static int patch_next(struct ain *ain, int fno)
{
	uint32_t ip = ain->functions[fno].address;
	int steps = 0, op, w, n = 0;
	while (steps++ < 48) {
		int32_t a[4];
		if (!decode(ain, ip, &op, a, &w))
			return 0;
		if (op == FUNC || op == ENDFUNC)
			break;
		if (op == SH_IF_LOC_LT_IMM && a[0] == 1 && a[1] == 2) {
			if (!patch_u32(ain, ip, 1, 2, 3))
				return 0;
			n++;
		}
		ip += (uint32_t)w;
	}
	return n == 1;
}

static int patch_last(struct ain *ain, int fno)
{
	uint32_t ip = ain->functions[fno].address;
	int steps = 0, op, w;
	uint32_t prev = 0;
	int prev_op = -1;
	while (steps++ < 16) {
		int32_t a[4];
		if (!decode(ain, ip, &op, a, &w))
			return 0;
		if (op == CALLMETHOD && prev_op == PUSH)
			return patch_u32(ain, prev, 0, 1, 2);
		prev = ip;
		prev_op = op;
		ip += (uint32_t)w;
	}
	return 0;
}

static void wrap_after(struct ain *ain, int fno, int sys)
{
	int dup = ain_dup_function(ain, fno);
	struct buffer out;
	buffer_init(&out, NULL, 0);
	emit1(&out, FUNC, fno);
	emit0(&out, PUSHSTRUCTPAGE);
	emit1(&out, CALLMETHOD, dup);
	emit1(&out, CALLSYS, sys);
	emit0(&out, RETURN);
	emit1(&out, ENDFUNC, fno);
	commit_wrap(ain, fno, &out);
}

static void wrap_exec(struct ain *ain, int fno, int setvalue)
{
	int dup = ain_dup_function(ain, fno);
	struct buffer out;
	uint32_t base;
	size_t ifz_at, orig_at;
	buffer_init(&out, NULL, 0);
	emit1(&out, FUNC, fno);
	emit1(&out, SH_LOCALREF, 0);
	emit1(&out, PUSH, RQ_CHECK_INDEX);
	emit0(&out, EQUALE);
	ifz_at = out.index;
	emit1(&out, IFZ, 0);
	emit1(&out, CALLSYS, VM_XSYS_DISCORD_TOGGLE);
	emit0(&out, PUSHSTRUCTPAGE);
	emit1(&out, CALLMETHOD, setvalue);
	/* execCheckBox always returns 0. setValue does not. */
	emit1(&out, PUSH, 0);
	emit0(&out, RETURN);
	orig_at = out.index;
	emit0(&out, PUSHSTRUCTPAGE);
	emit1(&out, SH_LOCALREF, 0);
	emit1(&out, CALLMETHOD, dup);
	emit0(&out, RETURN);
	emit1(&out, ENDFUNC, fno);
	base = (uint32_t)ain->code_size;
	buffer_write_int32_at(&out, ifz_at + 2, base + (uint32_t)orig_at);
	commit_wrap(ain, fno, &out);
}

static int install_checkbox(struct ain *ain)
{
	int alloc, gid, gix, next, last, create, setv, exec, show, check;
	alloc = fn_index(ain, "T_ConfigEtc2@2");
	gid = fn_index(ain, "T_ConfigEtc2@getCheckID");
	gix = fn_index(ain, "T_ConfigEtc2@getCheckIndex");
	next = fn_index(ain, "T_ConfigEtc2@getNextCheckBox");
	last = fn_index(ain, "T_ConfigEtc2@getLastCheckBox");
	create = fn_index(ain, "T_ConfigEtc2@create");
	setv = fn_index(ain, "T_ConfigEtc2@setValue");
	exec = fn_index(ain, "T_ConfigEtc2@execCheckBox");
	show = fn_index(ain, "T_Sprite@setShow");
	check = fn_index(ain, "T_ConfigEtc2@createCheckBox");
	g_f_group_show = fn_index(ain, "T_ButtonGroup@setShow");
	g_f_group_z = fn_index(ain, "T_ButtonGroup@setZ");
	if (alloc < 0 || gid < 0 || gix < 0 || next < 0 || last < 0
	    || create < 0 || setv < 0 || exec < 0 || show < 0 || check < 0)
		return 0;
	if (!patch_alloc(ain, alloc) || !patch_limit(ain, gid) || !patch_limit(ain, gix)
	    || !patch_next(ain, next) || !patch_last(ain, last)) {
		rq_log("Discord presence: Config Other 2 checkbox was not patched");
		return 0;
	}
	g_f_show = show;
	g_f_check = check;
	wrap_after(ain, setv, VM_XSYS_DISCORD_SHOW);
	wrap_exec(ain, exec, setv);
	wrap_after(ain, create, VM_XSYS_DISCORD_ADD);
	return 1;
}

void rq_discord_init(struct ain *ain)
{
	memset(&g_label_ts, 0, sizeof g_label_ts);
	g_label_ts.face = FONT_GOTHIC;
	g_label_ts.size = 16.0f;
	g_label_ts.weight = FW_BOLD;
	g_label_ts.bold_width = 0.45f;
	g_label_ts.color = (SDL_Color){ .r = 255, .g = 255, .b = 255, .a = 255 };
	g_label_ts.edge_color = (SDL_Color){ .r = 0, .g = 0, .b = 0, .a = 255 };
	g_label_ts.scale_x = 1.0f;
	g_label_ts.space_scale_x = 1.0f;
	text_style_set_edge_width(&g_label_ts, 1.0f);
	load_settings();
	g_ui = ain && install_checkbox(ain);
	g_inited = 1;
	if (g_ui)
		rq_log("Config Other 2 checkbox: Disable Discord Rich Presence (off until checked)");
	else
		rq_log("Discord presence: no Config Other 2 checkbox on this AIN");
	rq_log("Discord Rich Presence %s, application id %s",
	       g_disabled ? "disabled" : "on", g_app_id);
}

#endif /* RQ_MODLOADER */
