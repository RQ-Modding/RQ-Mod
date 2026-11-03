/* Copyright (C) 2026 Rance Quest modding fork
 *
 * Save profiles. The base profile is the engine's own save folder. Every other
 * profile is a full copy of a save folder under <parent>/Profiles/<id>/save,
 * with profile.json next to it. The active profile is chosen before the game
 * reads any save file, so switching restarts the process.
 *
 * Lua gets rq.saves.*: list / create / clone / import_vanilla / rename /
 * delete / switch. Mods cannot write outside their own folder, so the file
 * work lives here and the UI lives in a mod.
 */

#ifdef RQ_MODLOADER

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>
#include <time.h>
#include <errno.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#endif

#include "lua.h"
#include "lauxlib.h"

#include "system4.h"
#include "system4/ain.h"
#include "system4/buffer.h"
#include "system4/file.h"
#include "system4/instructions.h"
#include "system4/savefile.h"
#include "system4/utfsjis.h"

#include "cJSON.h"
#include "modloader.h"
#include "modloader_priv.h"
#include "vm.h"
#include "xsystem4.h"

#define SAVES_PATH_MAX 2048
#define SAVES_ID_MAX 48
#define SAVES_NAME_MAX 40
#define SAVES_MAX 64
#define BASE_ID "base"
#define MAIN_SAVE "RanceQuestMagnum_030.ASD"
#define SUSPEND_SAVE "RanceQuestMagnum_930.ASD"

static char g_base_dir[SAVES_PATH_MAX];
static char g_root_dir[SAVES_PATH_MAX];
static char g_active[SAVES_ID_MAX];
static int g_ready;
static int g_title;
static int g_installed;

/* ---- paths --------------------------------------------------------------- */

static int id_valid(const char *id)
{
	size_t n, i;
	if (!id || !*id)
		return 0;
	n = strlen(id);
	if (n >= SAVES_ID_MAX)
		return 0;
	for (i = 0; i < n; i++) {
		unsigned char c = (unsigned char)id[i];
		if (!(isalnum(c) || c == '_' || c == '-'))
			return 0;
	}
	return 1;
}

static int is_base(const char *id)
{
	return !id || !*id || !strcmp(id, BASE_ID);
}

static char *path_cat(const char *a, const char *b)
{
	return path_join(a, b);
}

static char *profile_dir(const char *id)
{
	return path_cat(g_root_dir, id);
}

static char *save_dir_of(const char *id)
{
	char *p, *s;
	if (is_base(id))
		return strdup(g_base_dir);
	p = profile_dir(id);
	s = path_cat(p, "save");
	free(p);
	return s;
}

static char *meta_path_of(const char *id)
{
	char *p, *s;
	if (is_base(id))
		return path_cat(g_root_dir, "base.json");
	p = profile_dir(id);
	s = path_cat(p, "profile.json");
	free(p);
	return s;
}

static int profile_exists(const char *id)
{
	char *d;
	int ok;
	if (is_base(id))
		return 1;
	if (!id_valid(id))
		return 0;
	d = save_dir_of(id);
	ok = is_directory(d);
	free(d);
	return ok;
}

/* ---- small file helpers -------------------------------------------------- */

static void parent_of(const char *path, char *out, size_t n)
{
	size_t len = strlen(path);
	snprintf(out, n, "%s", path);
	while (len > 0 && (out[len - 1] == '/' || out[len - 1] == '\\'))
		out[--len] = 0;
	while (len > 0 && out[len - 1] != '/' && out[len - 1] != '\\')
		len--;
	while (len > 0 && (out[len - 1] == '/' || out[len - 1] == '\\'))
		len--;
	out[len] = 0;
}

static char *read_text(const char *path)
{
	size_t len = 0;
	uint8_t *data = file_read(path, &len);
	char *s;
	if (!data)
		return NULL;
	s = malloc(len + 1);
	if (!s) {
		free(data);
		return NULL;
	}
	memcpy(s, data, len);
	s[len] = 0;
	free(data);
	return s;
}

static cJSON *read_json(const char *path)
{
	char *txt = read_text(path);
	cJSON *j;
	if (!txt)
		return NULL;
	j = cJSON_Parse(txt);
	free(txt);
	return j;
}

static int write_json(const char *path, cJSON *j)
{
	char parent[SAVES_PATH_MAX];
	char *s;
	int ok;
	parent_of(path, parent, sizeof parent);
	if (parent[0])
		mkdir_p(parent);
	s = cJSON_Print(j);
	if (!s)
		return 0;
	ok = file_write(path, (uint8_t *)s, strlen(s));
	free(s);
	return ok;
}

static void fmt_time(time_t t, char *buf, size_t n)
{
	struct tm *tm = localtime(&t);
	if (!tm) {
		snprintf(buf, n, "?");
		return;
	}
	strftime(buf, n, "%Y-%m-%d %H:%M", tm);
}

static int file_mtime(const char *path, time_t *out)
{
	ustat st;
	if (stat_utf8(path, &st) != 0)
		return 0;
	*out = (time_t)st.st_mtime;
	return 1;
}

/* file_copy() fails on an empty file (fwrite of 0 bytes), so copy by hand. */
static int copy_file(const char *src, const char *dst)
{
	FILE *in = file_open_utf8(src, "rb");
	FILE *out;
	char buf[65536];
	size_t n;
	int ok = 1;
	if (!in)
		return 0;
	out = file_open_utf8(dst, "wb");
	if (!out) {
		fclose(in);
		return 0;
	}
	while ((n = fread(buf, 1, sizeof buf, in)) > 0) {
		if (fwrite(buf, 1, n, out) != n) {
			ok = 0;
			break;
		}
	}
	if (ferror(in))
		ok = 0;
	fclose(in);
	if (fclose(out) != 0)
		ok = 0;
	return ok;
}

static int copy_tree(const char *src, const char *dst, int top)
{
	UDIR *dir;
	char *name;
	int ok = 1;
	if (mkdir_p(dst) != 0 && !is_directory(dst))
		return 0;
	dir = opendir_utf8(src);
	if (!dir)
		return 0;
	while ((name = readdir_utf8(dir))) {
		char *s, *d;
		if (!strcmp(name, ".") || !strcmp(name, "..")) {
			free(name);
			continue;
		}
		/* Backup\ holds the source profile's dated snapshots. A clone starts its own. */
		if (top && !strcmp(name, "Backup")) {
			free(name);
			continue;
		}
		s = path_cat(src, name);
		d = path_cat(dst, name);
		if (is_directory(s))
			ok &= copy_tree(s, d, 0);
		else
			ok &= copy_file(s, d);
		free(s);
		free(d);
		free(name);
		if (!ok)
			break;
	}
	closedir_utf8(dir);
	return ok;
}

static int remove_tree(const char *path)
{
	UDIR *dir;
	char *name;
	int ok = 1;
	dir = opendir_utf8(path);
	if (!dir)
		return 0;
	while ((name = readdir_utf8(dir))) {
		char *p;
		if (!strcmp(name, ".") || !strcmp(name, "..")) {
			free(name);
			continue;
		}
		p = path_cat(path, name);
		if (is_directory(p))
			ok &= remove_tree(p);
		else
			ok &= (remove_utf8(p) == 0);
		free(p);
		free(name);
	}
	closedir_utf8(dir);
	return ok & (rmdir_utf8(path) == 0);
}

/* ---- names and ids ------------------------------------------------------- */

static void clean_name(const char *in, char *out, size_t n)
{
	size_t o = 0;
	const unsigned char *p = (const unsigned char *)(in ? in : "");
	while (*p && isspace(*p))
		p++;
	for (; *p && o + 1 < n && o < SAVES_NAME_MAX; p++) {
		if (*p < 0x20 || *p == 0x7f)
			continue;
		/* Do not cut a UTF-8 sequence in half. */
		if (*p >= 0xC0) {
			int len = *p >= 0xF0 ? 4 : *p >= 0xE0 ? 3 : 2;
			if (o + len >= n || o + len > SAVES_NAME_MAX)
				break;
		}
		out[o++] = (char)*p;
	}
	while (o > 0 && isspace((unsigned char)out[o - 1]))
		o--;
	out[o] = 0;
}

static void new_id(char *out, size_t n)
{
	char stamp[32];
	time_t t = time(NULL);
	struct tm *tm = localtime(&t);
	int i;
	if (tm)
		strftime(stamp, sizeof stamp, "p%Y%m%d%H%M%S", tm);
	else
		snprintf(stamp, sizeof stamp, "p%lld", (long long)t);
	for (i = 0; i < 100; i++) {
		char *d;
		int taken;
		if (i == 0)
			snprintf(out, n, "%s", stamp);
		else
			snprintf(out, n, "%s_%d", stamp, i);
		d = profile_dir(out);
		taken = is_directory(d);
		free(d);
		if (!taken)
			return;
	}
}

/* ---- metadata ------------------------------------------------------------ */

static cJSON *load_meta(const char *id)
{
	char *p = meta_path_of(id);
	cJSON *j = read_json(p);
	free(p);
	if (!j || !cJSON_IsObject(j)) {
		cJSON_Delete(j);
		j = cJSON_CreateObject();
	}
	return j;
}

static int store_meta(const char *id, cJSON *j)
{
	char *p = meta_path_of(id);
	int ok = write_json(p, j);
	free(p);
	return ok;
}

static void meta_set_str(cJSON *j, const char *key, const char *val)
{
	cJSON_DeleteItemFromObjectCaseSensitive(j, key);
	cJSON_AddStringToObject(j, key, val ? val : "");
}

static const char *meta_str(cJSON *j, const char *key)
{
	cJSON *v = cJSON_GetObjectItemCaseSensitive(j, key);
	return cJSON_IsString(v) && v->valuestring ? v->valuestring : NULL;
}

static cJSON *current_mods_json(void)
{
	cJSON *arr = cJSON_CreateArray();
	int i, n = rq_mod_count();
	for (i = 0; i < n; i++) {
		const char *id = rq_mod_id(i);
		const char *dir = rq_mod_dir(i);
		cJSON *o, *man = NULL;
		const char *name = NULL, *ver = NULL;
		if (!id || !*id)
			continue;
		if (dir && *dir) {
			char *mj = path_cat(dir, "mod.json");
			man = read_json(mj);
			free(mj);
		}
		if (man) {
			name = meta_str(man, "name");
			ver = meta_str(man, "version");
		}
		o = cJSON_CreateObject();
		cJSON_AddStringToObject(o, "id", id);
		cJSON_AddStringToObject(o, "name", name ? name : id);
		cJSON_AddStringToObject(o, "version", ver ? ver : "");
		cJSON_AddItemToArray(arr, o);
		cJSON_Delete(man);
	}
	return arr;
}

static int ever_has(cJSON *ever, const char *id)
{
	cJSON *el;
	cJSON_ArrayForEach(el, ever) {
		if (cJSON_IsString(el) && el->valuestring && !strcmp(el->valuestring, id))
			return 1;
	}
	return 0;
}

/* Called from save_globals(). Only the game slots count, not settings files. */
static int is_game_slot_file(const char *filename)
{
	size_t n;
	const char *dot, *us;
	if (!filename)
		return 0;
	n = strlen(filename);
	if (n < 8)
		return 0;
	dot = filename + n - 4;
	if (strcasecmp(dot, ".ASD") != 0)
		return 0;
	if (n > 8 && strcasecmp(filename + n - 8, "_DAT.ASD") == 0)
		return 0;
	if (n > 8 && strcasecmp(filename + n - 8, "_INF.ASD") == 0)
		return 0;
	us = dot;
	if (us == filename || !isdigit((unsigned char)us[-1]))
		return 0;
	while (us > filename && isdigit((unsigned char)us[-1]))
		us--;
	return us > filename && us[-1] == '_';
}

void rq_saves_on_game_save(const char *filename)
{
	cJSON *meta, *mods, *ever, *el;
	char stamp[32];
	if (!g_ready || !is_game_slot_file(filename))
		return;
	meta = load_meta(g_active);
	mods = current_mods_json();
	ever = cJSON_GetObjectItemCaseSensitive(meta, "mods_ever");
	if (!cJSON_IsArray(ever)) {
		cJSON_DeleteItemFromObjectCaseSensitive(meta, "mods_ever");
		ever = cJSON_AddArrayToObject(meta, "mods_ever");
	}
	cJSON_ArrayForEach(el, mods) {
		const char *id = meta_str(el, "id");
		if (id && !ever_has(ever, id))
			cJSON_AddItemToArray(ever, cJSON_CreateString(id));
	}
	cJSON_DeleteItemFromObjectCaseSensitive(meta, "mods");
	cJSON_AddItemToObject(meta, "mods", mods);
	fmt_time(time(NULL), stamp, sizeof stamp);
	meta_set_str(meta, "last_saved", stamp);
	if (!store_meta(g_active, meta))
		rq_log("saves: could not write metadata for '%s'", g_active);
	cJSON_Delete(meta);
}

/* ---- summary of what a save folder holds --------------------------------- */

static int read_global(struct gsave *gs, const char *name_utf8, int *out)
{
	char *sjis = utf2sjis(name_utf8, 0);
	int32_t i;
	int found = 0;
	if (!sjis)
		return 0;
	for (i = 0; i < gs->nr_globals; i++) {
		if (gs->globals[i].name && !strcmp(gs->globals[i].name, sjis)) {
			*out = gs->globals[i].value;
			found = 1;
			break;
		}
	}
	free(sjis);
	return found;
}

static void push_summary(lua_State *L, const char *save_dir)
{
	char *main_path = path_cat(save_dir, MAIN_SAVE);
	char *susp_path = path_cat(save_dir, SUSPEND_SAVE);
	char stamp[32];
	time_t mt;
	int v;

	lua_newtable(L);
	if (!file_exists(main_path)) {
		lua_pushboolean(L, 0);
		lua_setfield(L, -2, "has_save");
	} else {
		enum savefile_error err;
		struct gsave *gs = gsave_read(main_path, &err);
		lua_pushboolean(L, 1);
		lua_setfield(L, -2, "has_save");
		if (file_mtime(main_path, &mt)) {
			fmt_time(mt, stamp, sizeof stamp);
			lua_pushstring(L, stamp);
			lua_setfield(L, -2, "saved_at");
		}
		if (gs) {
			if (read_global(gs, "◆ワールド", &v)) {
				lua_pushinteger(L, v);
				lua_setfield(L, -2, "world");
			}
			if (read_global(gs, "◆名声ランク", &v)) {
				lua_pushinteger(L, v);
				lua_setfield(L, -2, "fame_rank");
			}
			if (read_global(gs, "◆名声", &v)) {
				lua_pushinteger(L, v);
				lua_setfield(L, -2, "fame");
			}
			if (read_global(gs, "◆金", &v)) {
				lua_pushinteger(L, v);
				lua_setfield(L, -2, "gold");
			}
			if (read_global(gs, "◆クエストクリア数", &v)) {
				lua_pushinteger(L, v);
				lua_setfield(L, -2, "quests");
			}
			gsave_free(gs);
		} else {
			lua_pushstring(L, savefile_strerror(err));
			lua_setfield(L, -2, "read_error");
		}
	}
	lua_pushboolean(L, file_exists(susp_path));
	lua_setfield(L, -2, "suspend");
	free(main_path);
	free(susp_path);
}

static void push_json_value(lua_State *L, cJSON *j)
{
	cJSON *el;
	int i = 1;
	if (cJSON_IsString(j)) {
		lua_pushstring(L, j->valuestring ? j->valuestring : "");
	} else if (cJSON_IsNumber(j)) {
		lua_pushnumber(L, j->valuedouble);
	} else if (cJSON_IsBool(j)) {
		lua_pushboolean(L, cJSON_IsTrue(j));
	} else if (cJSON_IsArray(j)) {
		lua_newtable(L);
		cJSON_ArrayForEach(el, j) {
			push_json_value(L, el);
			lua_rawseti(L, -2, i++);
		}
	} else if (cJSON_IsObject(j)) {
		lua_newtable(L);
		cJSON_ArrayForEach(el, j) {
			push_json_value(L, el);
			lua_setfield(L, -2, el->string);
		}
	} else {
		lua_pushnil(L);
	}
}

static void push_profile(lua_State *L, const char *id, int with_summary)
{
	cJSON *meta = load_meta(id);
	const char *name = meta_str(meta, "name");
	const char *key;
	cJSON *v;
	char *dir;

	lua_newtable(L);
	lua_pushstring(L, is_base(id) ? BASE_ID : id);
	lua_setfield(L, -2, "id");
	lua_pushstring(L, name && *name ? name : (is_base(id) ? "Original save" : id));
	lua_setfield(L, -2, "name");
	lua_pushboolean(L, is_base(id));
	lua_setfield(L, -2, "is_base");
	lua_pushboolean(L, is_base(id) ? is_base(g_active) : !strcmp(id, g_active));
	lua_setfield(L, -2, "active");
	key = meta_str(meta, "created");
	if (key) {
		lua_pushstring(L, key);
		lua_setfield(L, -2, "created");
	}
	key = meta_str(meta, "last_saved");
	if (key) {
		lua_pushstring(L, key);
		lua_setfield(L, -2, "last_saved");
	}
	key = meta_str(meta, "source");
	if (key) {
		lua_pushstring(L, key);
		lua_setfield(L, -2, "source");
	}
	v = cJSON_GetObjectItemCaseSensitive(meta, "mods");
	if (cJSON_IsArray(v)) {
		push_json_value(L, v);
		lua_setfield(L, -2, "mods");
	}
	v = cJSON_GetObjectItemCaseSensitive(meta, "mods_ever");
	if (cJSON_IsArray(v)) {
		push_json_value(L, v);
		lua_setfield(L, -2, "mods_ever");
	}
	if (with_summary) {
		dir = save_dir_of(id);
		push_summary(L, dir);
		lua_setfield(L, -2, "summary");
		free(dir);
	}
	cJSON_Delete(meta);
}

/* ---- startup ------------------------------------------------------------- */

static void read_active_file(char *out, size_t n)
{
	char *p = path_cat(g_root_dir, "active.txt");
	char *txt = read_text(p);
	size_t len;
	out[0] = 0;
	free(p);
	if (!txt)
		return;
	len = strlen(txt);
	while (len > 0 && isspace((unsigned char)txt[len - 1]))
		txt[--len] = 0;
	if (id_valid(txt))
		snprintf(out, n, "%s", txt);
	free(txt);
}

static int write_active_file(const char *id)
{
	char *p = path_cat(g_root_dir, "active.txt");
	/* file_write() reports a 0-byte write as a failure, so the base is spelled out. */
	const char *s = is_base(id) ? BASE_ID : id;
	int ok;
	mkdir_p(g_root_dir);
	ok = file_write(p, (uint8_t *)s, strlen(s));
	free(p);
	return ok;
}

void rq_saves_wait_parent(void)
{
#ifdef _WIN32
	wchar_t buf[32];
	DWORD n = GetEnvironmentVariableW(L"RQ_WAIT_PID", buf, 32);
	if (n > 0 && n < 32) {
		DWORD pid = (DWORD)wcstoul(buf, NULL, 10);
		HANDLE h = pid ? OpenProcess(SYNCHRONIZE, FALSE, pid) : NULL;
		if (h) {
			WaitForSingleObject(h, 15000);
			CloseHandle(h);
		}
		SetEnvironmentVariableW(L"RQ_WAIT_PID", NULL);
	}
#endif
}

const char *rq_saves_base_dir(void)
{
	return g_base_dir[0] ? g_base_dir : NULL;
}

/* Call once, after config.save_dir is final and before mkdir_p() on it. */
void rq_saves_init(char **save_dir)
{
	char parent[SAVES_PATH_MAX];
	char active[SAVES_ID_MAX];
	if (!save_dir || !*save_dir)
		return;
	snprintf(g_base_dir, sizeof g_base_dir, "%s", *save_dir);
	parent_of(g_base_dir, parent, sizeof parent);
	{
		char *root = path_cat(parent, "Profiles");
		snprintf(g_root_dir, sizeof g_root_dir, "%s", root);
		free(root);
	}
	g_active[0] = 0;
	read_active_file(active, sizeof active);
	if (active[0] && !is_base(active) && profile_exists(active)) {
		char *dir = save_dir_of(active);
		snprintf(g_active, sizeof g_active, "%s", active);
		free(*save_dir);
		*save_dir = dir;
		rq_log("saves: active profile '%s' -> %s", g_active, *save_dir);
	} else {
		if (active[0])
			rq_log("saves: profile '%s' is missing, using the base save", active);
		rq_log("saves: base profile -> %s", g_base_dir);
	}
	g_ready = 1;
}

/* ---- title screen hook --------------------------------------------------- */

static void emit0(struct buffer *out, enum opcode op)
{
	buffer_write_int16(out, (uint16_t)op);
}

static void emit1(struct buffer *out, enum opcode op, int32_t arg)
{
	buffer_write_int16(out, (uint16_t)op);
	buffer_write_int32(out, (uint32_t)arg);
}

int rq_saves_install(struct ain *ain)
{
	char *sjis;
	int fno, dup, nargs, i;
	struct buffer out;

	if (g_installed)
		return 1;
	if (!ain)
		return 0;
	sjis = utf2sjis("T_Title@checkSelect", 0);
	fno = sjis ? ain_get_function(ain, sjis) : -1;
	free(sjis);
	if (fno < 0) {
		rq_log("saves: T_Title@checkSelect is not in this AIN; no title hook");
		return 0;
	}
	dup = ain_dup_function(ain, fno);
	nargs = ain->functions[fno].nr_args;
	buffer_init(&out, NULL, 0);
	emit1(&out, FUNC, fno);
	emit1(&out, PUSH, 0);
	emit1(&out, CALLSYS, VM_XSYS_TITLE_ON);
	emit0(&out, PUSHSTRUCTPAGE);
	for (i = 0; i < nargs; i++)
		emit1(&out, SH_LOCALREF, i);
	emit1(&out, CALLMETHOD, dup);
	emit1(&out, PUSH, 0);
	emit1(&out, CALLSYS, VM_XSYS_TITLE_OFF);
	emit0(&out, RETURN);
	emit1(&out, ENDFUNC, fno);
	ain->functions[fno].address = (uint32_t)(ain->code_size + 6);
	ain->code = xrealloc(ain->code, ain->code_size + out.index);
	memcpy(ain->code + ain->code_size, out.buf, out.index);
	ain->code_size += out.index;
	g_installed = 1;
	rq_log("saves: watching T_Title@checkSelect so the Save Manager can show on the title screen");
	return 1;
}

int rq_saves_title_active(void)
{
	return g_title > 0;
}

int rq_saves_syscall(int code)
{
	switch ((enum vm_extra_syscall)code) {
	case VM_XSYS_TITLE_ON:
		stack_pop();
		g_title++;
		return 1;
	case VM_XSYS_TITLE_OFF:
		stack_pop();
		if (g_title > 0)
			g_title--;
		return 1;
	default:
		return 0;
	}
}

/* ---- vanilla install ----------------------------------------------------- */

static int vanilla_dir(char *out, size_t n)
{
#ifdef _WIN32
	wchar_t w[MAX_PATH];
	char *u;
	char game[128];
	if (SHGetFolderPathW(NULL, CSIDL_PERSONAL, NULL, SHGFP_TYPE_CURRENT, w) != S_OK)
		return 0;
	u = wchar_to_utf8(w);
	if (!u)
		return 0;
	{
		char *g = sjis2utf(config.game_name ? config.game_name : "Rance Quest", 0);
		snprintf(game, sizeof game, "%s", g ? g : "Rance Quest");
		free(g);
	}
	snprintf(out, n, "%s/AliceSoft/%s/SaveData", u, game);
	free(u);
	return 1;
#else
	(void)out;
	(void)n;
	return 0;
#endif
}

/* ---- Lua ----------------------------------------------------------------- */

static int fail(lua_State *L, const char *msg)
{
	lua_pushnil(L);
	lua_pushstring(L, msg);
	return 2;
}

static int cmp_ids(const void *a, const void *b)
{
	const char *x = (const char *)a, *y = (const char *)b;
	cJSON *mx = load_meta(x), *my = load_meta(y);
	const char *cx = meta_str(mx, "created"), *cy = meta_str(my, "created");
	int r = strcmp(cx ? cx : "", cy ? cy : "");
	cJSON_Delete(mx);
	cJSON_Delete(my);
	return r ? r : strcmp(x, y);
}

int rq_lua_saves_list(lua_State *L)
{
	char ids[SAVES_MAX][SAVES_ID_MAX];
	int n = 0, i;
	UDIR *dir;
	char *name;

	if (!g_ready)
		return fail(L, "saves not initialised");
	dir = opendir_utf8(g_root_dir);
	if (dir) {
		while ((name = readdir_utf8(dir))) {
			if (n < SAVES_MAX && id_valid(name) && strcmp(name, BASE_ID) != 0
			    && profile_exists(name)) {
				snprintf(ids[n], SAVES_ID_MAX, "%s", name);
				n++;
			}
			free(name);
		}
		closedir_utf8(dir);
	}
	qsort(ids, (size_t)n, SAVES_ID_MAX, cmp_ids);
	lua_newtable(L);
	push_profile(L, BASE_ID, 1);
	lua_rawseti(L, -2, 1);
	for (i = 0; i < n; i++) {
		push_profile(L, ids[i], 1);
		lua_rawseti(L, -2, i + 2);
	}
	return 1;
}

int rq_lua_saves_active(lua_State *L)
{
	lua_pushstring(L, is_base(g_active) ? BASE_ID : g_active);
	return 1;
}

int rq_lua_saves_title(lua_State *L)
{
	lua_pushboolean(L, g_title > 0);
	return 1;
}

int rq_lua_saves_current_mods(lua_State *L)
{
	cJSON *mods = current_mods_json();
	push_json_value(L, mods);
	cJSON_Delete(mods);
	return 1;
}

int rq_lua_saves_vanilla(lua_State *L)
{
	char dir[SAVES_PATH_MAX];
	char *main_path;
	int exists;
	lua_newtable(L);
	if (!vanilla_dir(dir, sizeof dir) || !is_directory(dir)) {
		lua_pushboolean(L, 0);
		lua_setfield(L, -2, "exists");
		return 1;
	}
	main_path = path_cat(dir, MAIN_SAVE);
	exists = file_exists(main_path);
	free(main_path);
	lua_pushstring(L, dir);
	lua_setfield(L, -2, "path");
	lua_pushboolean(L, exists);
	lua_setfield(L, -2, "exists");
	if (exists) {
		push_summary(L, dir);
		lua_setfield(L, -2, "summary");
	}
	return 1;
}

/* Copy src_dir (or nothing) into a new profile. Returns the id on the stack. */
static int make_profile(lua_State *L, const char *name_in, const char *src_dir, const char *source_note)
{
	char id[SAVES_ID_MAX];
	char name[SAVES_NAME_MAX * 4 + 4];
	char stamp[32];
	char *dst, *pdir;
	cJSON *meta;

	if (!g_ready)
		return fail(L, "saves not initialised");
	clean_name(name_in, name, sizeof name);
	if (!name[0])
		return fail(L, "the name is empty");
	new_id(id, sizeof id);
	pdir = profile_dir(id);
	dst = save_dir_of(id);
	if (mkdir_p(dst) != 0 && !is_directory(dst)) {
		free(pdir);
		free(dst);
		return fail(L, "could not create the profile folder");
	}
	if (src_dir && !copy_tree(src_dir, dst, 1)) {
		remove_tree(pdir);
		free(pdir);
		free(dst);
		return fail(L, "copying the save files failed");
	}
	meta = cJSON_CreateObject();
	meta_set_str(meta, "name", name);
	fmt_time(time(NULL), stamp, sizeof stamp);
	meta_set_str(meta, "created", stamp);
	if (source_note)
		meta_set_str(meta, "source", source_note);
	if (!store_meta(id, meta)) {
		cJSON_Delete(meta);
		remove_tree(pdir);
		free(pdir);
		free(dst);
		return fail(L, "could not write profile.json");
	}
	cJSON_Delete(meta);
	free(pdir);
	free(dst);
	rq_log("saves: created profile '%s' (%s)", id, name);
	lua_pushstring(L, id);
	return 1;
}

int rq_lua_saves_create(lua_State *L)
{
	return make_profile(L, luaL_checkstring(L, 1), NULL, "new");
}

int rq_lua_saves_clone(lua_State *L)
{
	const char *src = luaL_checkstring(L, 1);
	const char *name = luaL_checkstring(L, 2);
	char *dir;
	char note[160];
	cJSON *sm;
	const char *sname;
	int r;

	if (!is_base(src) && !profile_exists(src))
		return fail(L, "that save does not exist");
	dir = save_dir_of(src);
	sm = load_meta(src);
	sname = meta_str(sm, "name");
	snprintf(note, sizeof note, "clone of %s", sname && *sname ? sname : (is_base(src) ? "Original save" : src));
	cJSON_Delete(sm);
	r = make_profile(L, name, dir, note);
	free(dir);
	return r;
}

int rq_lua_saves_import_vanilla(lua_State *L)
{
	const char *name = luaL_checkstring(L, 1);
	char dir[SAVES_PATH_MAX];
	char *main_path;
	int exists;
	if (!vanilla_dir(dir, sizeof dir) || !is_directory(dir))
		return fail(L, "the vanilla save folder was not found");
	main_path = path_cat(dir, MAIN_SAVE);
	exists = file_exists(main_path);
	free(main_path);
	if (!exists)
		return fail(L, "the vanilla folder has no game save");
	return make_profile(L, name, dir, "imported from the vanilla game");
}

int rq_lua_saves_rename(lua_State *L)
{
	const char *id = luaL_checkstring(L, 1);
	char name[SAVES_NAME_MAX * 4 + 4];
	cJSON *meta;
	clean_name(luaL_checkstring(L, 2), name, sizeof name);
	if (!name[0])
		return fail(L, "the name is empty");
	if (!is_base(id) && !profile_exists(id))
		return fail(L, "that save does not exist");
	meta = load_meta(id);
	meta_set_str(meta, "name", name);
	if (!store_meta(id, meta)) {
		cJSON_Delete(meta);
		return fail(L, "could not write the name");
	}
	cJSON_Delete(meta);
	lua_pushboolean(L, 1);
	return 1;
}

int rq_lua_saves_delete(lua_State *L)
{
	const char *id = luaL_checkstring(L, 1);
	char *pdir;
	int ok;
	if (is_base(id))
		return fail(L, "the base save can never be deleted");
	if (!id_valid(id) || !profile_exists(id))
		return fail(L, "that save does not exist");
	if (!strcmp(id, g_active))
		return fail(L, "switch to another save before deleting this one");
	pdir = profile_dir(id);
	ok = remove_tree(pdir);
	free(pdir);
	if (!ok)
		return fail(L, "some files could not be removed");
	rq_log("saves: deleted profile '%s'", id);
	lua_pushboolean(L, 1);
	return 1;
}

#ifdef _WIN32
static int relaunch(void)
{
	wchar_t exe[MAX_PATH * 4];
	wchar_t pid[32];
	wchar_t *cmd;
	STARTUPINFOW si;
	PROCESS_INFORMATION pi;
	BOOL ok;

	if (!GetModuleFileNameW(NULL, exe, (DWORD)(sizeof exe / sizeof exe[0])))
		return 0;
	cmd = _wcsdup(GetCommandLineW());
	if (!cmd)
		return 0;
	swprintf(pid, 32, L"%lu", (unsigned long)GetCurrentProcessId());
	SetEnvironmentVariableW(L"RQ_WAIT_PID", pid);
	memset(&si, 0, sizeof si);
	si.cb = sizeof si;
	memset(&pi, 0, sizeof pi);
	ok = CreateProcessW(exe, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi);
	SetEnvironmentVariableW(L"RQ_WAIT_PID", NULL);
	free(cmd);
	if (!ok)
		return 0;
	CloseHandle(pi.hThread);
	CloseHandle(pi.hProcess);
	return 1;
}
#else
static int relaunch(void)
{
	return 0;
}
#endif

int rq_lua_saves_switch(lua_State *L)
{
	const char *id = luaL_checkstring(L, 1);
	char prev[SAVES_ID_MAX];
	if (!g_ready)
		return fail(L, "saves not initialised");
	if (!is_base(id) && !profile_exists(id))
		return fail(L, "that save does not exist");
	if (is_base(id) ? is_base(g_active) : !strcmp(id, g_active))
		return fail(L, "that save is already active");
	snprintf(prev, sizeof prev, "%s", g_active);
	if (!write_active_file(id))
		return fail(L, "could not record the choice");
	rq_log("saves: switching to '%s', restarting", is_base(id) ? BASE_ID : id);
	if (!relaunch()) {
		write_active_file(prev);
		return fail(L, "could not restart the game");
	}
	vm_exit(0);
}

void rq_saves_bind(lua_State *L)
{
	(void)L;
}

#endif /* RQ_MODLOADER */
