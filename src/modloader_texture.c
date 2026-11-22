/* Copyright (C) 2026 Rance Quest modding fork
 *
 * rq.image.texture: a mod's pictures on the game's 3D textures (2.64.0).
 *   texture(path, picture)  a PNG (or a list, one per frame) wherever a model uses that texture
 *   textures(model)         a model's textures as the game has them, with what mods put there
 * and a model's own textures for rq.monster.model (lua/monster.lua): entries under the model's
 * number, which its folder's mark carries (3d/rq_textures.h).
 *
 * The 3D code (3d/model.c) asks RE_mod_texture for each texture a model loads, and draws the
 * answer with the game's alpha kind. Nothing is kept but the files' names: each load reads the
 * file again, with the same reader and caps as the call. A model the game keeps in memory is
 * loaded again when one of its textures changes, at its next load with no copy of it on screen
 * (ReignEngine.c, rq_reign_texture_changed).
 */

#ifdef RQ_MODLOADER

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lua.h"
#include "lauxlib.h"

#include "system4.h"
#include "system4/aar.h"
#include "system4/archive.h"
#include "system4/cg.h"
#include "system4/utfsjis.h"

#include "3d/3d_internal.h"
#include "3d/rq_textures.h"
#include "modloader.h"
#include "modloader_priv.h"
#include "rq_pixels.h"
#include "xsystem4.h"

#define MODEL0 100000
/* Changed textures' pictures in all (by path and models' own; a list counts each frame). */
#define TEX_PICTURES_MAX 4096
/* A picture file's bytes. */
#define TEX_FILE_BYTES (256u << 20)

struct tex {
	char *folder;   /* the model's folder in the archive's terms (Shift-JIS, upper case, \), no mark */
	char *name;     /* the texture's name, in the same terms */
	int own;        /* a model's own: its number; 0 for every use */
	char *path;     /* the texture's path as rq.image.textures shows it (UTF-8, /) */
	char *model_id; /* a model's own: its id */
	char mod[64];   /* the mod that made the change */
	bool list;      /* the pictures were given as a list */
	int nfiles;
	char **files;   /* the resolved files, one per frame */
	char **given;   /* each as the mod gave it */
	int used;       /* material loads that drew it */
	bool logged, warned;
};

static struct tex *g_tex;
static int g_ntex;
static int g_npictures;

/* The models' texture lists, read once per folder (NULL: no model there). */
struct folder_list {
	char *folder;
	struct rq_texture_list *list;
};
static struct folder_list *g_lists;
static int g_nlists;

static struct archive *g_aar;

/* Data\ReignData.red, opened at first use and kept (as MonsterInfo opens it), so the calls work at
 * a mod's top level, before the game opens its archives. */
static struct archive *tex_archive(void)
{
	static bool tried;
	if (!g_aar && !tried) {
		char *path = gamedir_path("Data/ReignData.red");
		int error = ARCHIVE_SUCCESS;
		tried = true;
		g_aar = (struct archive *)aar_open(path, MMAP_IF_64BIT, &error);
		if (!g_aar)
			rq_log("texture: cannot open %s", path);
		free(path);
	}
	return g_aar;
}

/* A folder or a name in the archive's terms, in place: ASCII upper case, \, no \ at the end. */
static void norm(char *s)
{
	size_t n;
	sjis_normalize_path(s);
	n = strlen(s);
	while (n > 0 && s[n - 1] == '\\')
		s[--n] = '\0';
}

static const struct rq_texture_list *list_of(const char *folder)
{
	struct archive *aar;
	for (int i = 0; i < g_nlists; i++) {
		if (!strcmp(g_lists[i].folder, folder))
			return g_lists[i].list;
	}
	aar = tex_archive();
	g_lists = xrealloc(g_lists, (g_nlists + 1) * sizeof *g_lists);
	g_lists[g_nlists].folder = xstrdup(folder);
	g_lists[g_nlists].list = aar ? rq_textures_list(aar, folder) : NULL;
	return g_lists[g_nlists++].list;
}

static struct tex *find(const char *folder, const char *name, int own)
{
	for (int i = 0; i < g_ntex; i++) {
		struct tex *e = &g_tex[i];
		if (e->own == own && !strcmp(e->name, name) && !strcmp(e->folder, folder))
			return e;
	}
	return NULL;
}

/* A model's own entry first, then the one for every use. */
static struct tex *lookup(const char *folder, const char *name, int own)
{
	struct tex *e = own ? find(folder, name, own) : NULL;
	return e ? e : find(folder, name, 0);
}

bool rq_texture_has_own(int own, const char *folder, const char *name)
{
	return own && find(folder, name, own) != NULL;
}

static void free_tex(struct tex *e)
{
	for (int i = 0; i < e->nfiles; i++) {
		free(e->files[i]);
		free(e->given[i]);
	}
	free(e->files);
	free(e->given);
	free(e->folder);
	free(e->name);
	free(e->path);
	free(e->model_id);
}

static void remove_tex(struct tex *e)
{
	g_npictures -= e->nfiles;
	free_tex(e);
	*e = g_tex[--g_ntex];
}

/* The whole file, read as the engine's mod paths are made (fopen on the resolved path). */
static uint8_t *read_file(const char *path, size_t *size, char *why, size_t why_len)
{
	FILE *f = fopen(path, "rb");
	long n;
	uint8_t *data;

	if (!f) {
		snprintf(why, why_len, "it cannot be opened");
		return NULL;
	}
	if (fseek(f, 0, SEEK_END) || (n = ftell(f)) < 0 || fseek(f, 0, SEEK_SET)) {
		fclose(f);
		snprintf(why, why_len, "it cannot be read");
		return NULL;
	}
	if ((unsigned long)n > TEX_FILE_BYTES) {
		fclose(f);
		snprintf(why, why_len, "the file is too large");
		return NULL;
	}
	data = malloc(n ? (size_t)n : 1);
	if (!data || fread(data, 1, (size_t)n, f) != (size_t)n) {
		free(data);
		fclose(f);
		snprintf(why, why_len, "it cannot be read");
		return NULL;
	}
	fclose(f);
	*size = (size_t)n;
	return data;
}

/* The log's name for an entry: its path, or a model's own by the model's id. */
static void entry_label(const struct tex *e, char *out, size_t n)
{
	if (e->own) {
		const char *leaf = strrchr(e->path, '/');
		snprintf(out, n, "%s %s", e->model_id ? e->model_id : "?", leaf ? leaf + 1 : e->path);
	} else {
		snprintf(out, n, "%s", e->path);
	}
}

static void entry_what(const struct tex *e, char *out, size_t n)
{
	if (e->nfiles == 1)
		snprintf(out, n, "%s's %s", e->mod, e->given[0]);
	else
		snprintf(out, n, "%s's %d frames (%s...)", e->mod, e->nfiles, e->given[0]);
}

struct cg *RE_mod_texture(struct archive_data *game, const char *path, const char *name, int kind)
{
	char folder[RQ_PATH_MAX], nm[256], base[256], why[200];
	struct tex *e;
	struct cg *cg;
	uint8_t *data;
	size_t size = 0;
	int own, frame = 0;

	(void)game;
	(void)kind;
	if (!g_ntex || !path || !name || !name[0] || strlen(path) >= sizeof folder || strlen(name) >= sizeof nm)
		return NULL;
	snprintf(folder, sizeof folder, "%s", path);
	own = rq_textures_mark(folder);
	rq_textures_strip_mark(folder);
	norm(folder);
	snprintf(nm, sizeof nm, "%s", name);
	sjis_normalize_path(nm);
	e = lookup(folder, nm, own);
	if (!e && (frame = rq_textures_frame(nm, base, sizeof base)) > 0)
		e = lookup(folder, base, own);
	if (!e || frame >= e->nfiles)
		return NULL;

	data = read_file(e->files[frame], &size, why, sizeof why);
	cg = data ? rq_pixels_png(data, size, RQ_PIXELS_TEXTURE_SIDE, why, sizeof why) : NULL;
	free(data);
	if (!cg) {
		if (!e->warned) {
			char label[RQ_PATH_MAX];
			entry_label(e, label, sizeof label);
			rq_log("texture: %s: %s's %s cannot be read now (%s); the game's picture is drawn",
			       label, e->mod, e->given[frame], why);
			e->warned = true;
		}
		return NULL;
	}
	if (!frame)
		e->used++;
	if (!e->logged) {
		char label[RQ_PATH_MAX], what[RQ_PATH_MAX];
		entry_label(e, label, sizeof label);
		entry_what(e, what, sizeof what);
		rq_log("texture: %s drawn with %s", label, what);
		e->logged = true;
	}
	return cg;
}

/* ---- the calls ---- */

/* A texture's path as the mod gave it, in both terms. */
struct tex_path {
	char shown[RQ_PATH_MAX];  /* UTF-8 with / */
	char folder_shown[RQ_PATH_MAX];
	char folder[RQ_PATH_MAX]; /* the archive's terms */
	char name[256];
	char name_sjis[256];      /* the name in Shift-JIS, its case kept */
};

/* false for a path with no folder or too long. */
static bool split_path(const char *given, struct tex_path *tp)
{
	char *sjis, *slash;

	if (strlen(given) >= sizeof tp->shown)
		return false;
	snprintf(tp->shown, sizeof tp->shown, "%s", given);
	for (char *p = tp->shown; *p; p++) {
		if (*p == '\\')
			*p = '/';
	}
	snprintf(tp->folder_shown, sizeof tp->folder_shown, "%s", tp->shown);
	slash = strrchr(tp->folder_shown, '/');
	if (!slash || !slash[1])
		return false;
	*slash = '\0';
	sjis = utf2sjis(tp->shown, 0);
	if (!sjis)
		return false;
	for (char *p = sjis; *p; p = sjis_skip_char(p)) {
		if (*p == '/')
			*p = '\\';
	}
	slash = strrchr(sjis, '\\');
	if (!slash || strlen(slash + 1) >= sizeof tp->name || (size_t)(slash - sjis) >= sizeof tp->folder) {
		free(sjis);
		return false;
	}
	snprintf(tp->name_sjis, sizeof tp->name_sjis, "%s", slash + 1);
	snprintf(tp->name, sizeof tp->name, "%s", slash + 1);
	sjis_normalize_path(tp->name);
	*slash = '\0';
	snprintf(tp->folder, sizeof tp->folder, "%s", sjis);
	norm(tp->folder);
	free(sjis);
	return true;
}

/* The pictures a mod gives, checked and resolved. */
struct pictures {
	int n;
	bool list;
	char **files;
	char **given;
};

static void free_pictures(struct pictures *p)
{
	for (int i = 0; i < p->n; i++) {
		free(p->files[i]);
		free(p->given[i]);
	}
	free(p->files);
	free(p->given);
	memset(p, 0, sizeof *p);
}

/* The PNGs at `idx` (a text, or a list of texts), each resolved in the calling mod's folder,
 * read and checked. NULL, or an error's text (in `err`) for the caller to raise. */
static bool get_pictures(lua_State *L, int idx, const char *shown, struct pictures *out, char *err, size_t err_len)
{
	int n = 0;

	memset(out, 0, sizeof *out);
	if (lua_type(L, idx) == LUA_TSTRING) {
		n = 1;
	} else if (lua_istable(L, idx)) {
		int count = 0;
		lua_rawgeti(L, idx, 1);
		bool listed = !lua_isnil(L, -1);
		lua_pop(L, 1);
		if (!listed) {
			snprintf(err, err_len, "texture '%s': give a PNG or a list of PNGs", shown);
			return false;
		}
		n = (int)lua_rawlen(L, idx);
		lua_pushnil(L);
		while (lua_next(L, idx)) {
			bool ok = lua_isinteger(L, -2) && lua_tointeger(L, -2) >= 1 && lua_tointeger(L, -2) <= n &&
				lua_type(L, -1) == LUA_TSTRING;
			lua_pop(L, 1);
			if (!ok) {
				lua_pop(L, 1);
				snprintf(err, err_len, "texture '%s': a list of frames takes PNG paths only", shown);
				return false;
			}
			count++;
		}
		if (count != n) {
			snprintf(err, err_len, "texture '%s': a list of frames takes PNG paths only", shown);
			return false;
		}
		if (n > TEX_PICTURES_MAX) {
			snprintf(err, err_len, "too many textures (max %d)", TEX_PICTURES_MAX);
			return false;
		}
		out->list = true;
	} else {
		snprintf(err, err_len, "texture '%s': give a PNG or a list of PNGs", shown);
		return false;
	}
	out->files = xcalloc(n, sizeof *out->files);
	out->given = xcalloc(n, sizeof *out->given);
	for (int i = 0; i < n; i++) {
		const char *rel;
		char full[RQ_PATH_MAX], why[200];
		uint8_t *data;
		size_t size = 0;
		int w, h;
		struct cg *cg;

		if (out->list)
			lua_rawgeti(L, idx, i + 1);
		else
			lua_pushvalue(L, idx);
		rel = lua_tostring(L, -1);
		out->given[i] = xstrdup(rel);
		out->n = i + 1;
		lua_pop(L, 1);
		if (!rq_sandbox_resolve(L, out->given[i], full, (int)sizeof full)) {
			snprintf(err, err_len, "texture picture '%s' is not inside the mod folder", out->given[i]);
			goto fail;
		}
		out->files[i] = xstrdup(full);
		if (!(data = read_file(full, &size, why, sizeof why))) {
			FILE *f = fopen(full, "rb");
			if (f) {
				fclose(f);
				snprintf(err, err_len, "texture picture '%s' cannot be read: %s", out->given[i], why);
			} else {
				snprintf(err, err_len, "texture picture '%s' not found in the mod folder", out->given[i]);
			}
			goto fail;
		}
		if (!rq_pixels_is_png(data, size)) {
			free(data);
			snprintf(err, err_len, "texture picture '%s' is not a PNG (textures take PNG only)", out->given[i]);
			goto fail;
		}
		if (rq_pixels_png_size(data, size, &w, &h) &&
		    (w > RQ_PIXELS_TEXTURE_SIDE || h > RQ_PIXELS_TEXTURE_SIDE)) {
			free(data);
			snprintf(err, err_len, "texture picture '%s' is %dx%d (max %d a side)", out->given[i], w, h,
				 RQ_PIXELS_TEXTURE_SIDE);
			goto fail;
		}
		cg = rq_pixels_png(data, size, RQ_PIXELS_TEXTURE_SIDE, why, sizeof why);
		free(data);
		if (!cg) {
			snprintf(err, err_len, "texture picture '%s' cannot be read: %s", out->given[i], why);
			goto fail;
		}
		cg_free(cg);
	}
	return true;
fail:
	free_pictures(out);
	return false;
}

static void set_tex(const struct tex_path *tp, int own, const char *model_id, const char *mod, struct pictures *pics)
{
	struct tex *e = find(tp->folder, tp->name, own);
	char label[RQ_PATH_MAX], what[RQ_PATH_MAX], was[64] = "";

	if (e) {
		snprintf(was, sizeof was, "%s", e->mod);
		g_npictures -= e->nfiles;
		free_tex(e);
	} else {
		g_tex = xrealloc(g_tex, (g_ntex + 1) * sizeof *g_tex);
		e = &g_tex[g_ntex++];
	}
	memset(e, 0, sizeof *e);
	e->folder = xstrdup(tp->folder);
	e->name = xstrdup(tp->name);
	e->own = own;
	e->path = xstrdup(tp->shown);
	e->model_id = model_id ? xstrdup(model_id) : NULL;
	snprintf(e->mod, sizeof e->mod, "%s", mod);
	e->list = pics->list;
	e->nfiles = pics->n;
	e->files = pics->files;
	e->given = pics->given;
	memset(pics, 0, sizeof *pics);
	g_npictures += e->nfiles;

	entry_label(e, label, sizeof label);
	entry_what(e, what, sizeof what);
	if (was[0] && strcmp(was, mod))
		rq_log("texture: %s: %s in the place of %s's", label, what, was);
	else
		rq_log("texture: %s: %s", label, what);
	rq_reign_texture_changed(tp->folder, tp->name, own);
}

/* texture(path, picture|nil) */
int rq_lua_image_texture(lua_State *L)
{
	struct tex_path tp;
	struct pictures pics;
	const char *given;
	char err[RQ_PATH_MAX + 200], why[RQ_PATH_MAX + 200];
	const struct rq_texture_list *l;
	const char *mod = rq_lua_caller_mod(L);
	struct tex *e;
	int old;

	if (lua_type(L, 1) != LUA_TSTRING)
		return luaL_error(L, "texture path must be a text");
	given = lua_tostring(L, 1);
	if (strchr(given, '*'))
		return luaL_error(L, "texture path '%s' has a '*' (a mod's model's textures go through rq.monster.model)", given);
	if (!split_path(given, &tp))
		return luaL_error(L, "no model in '%s' (rq.image.textures lists a model's textures)", given);
	l = list_of(tp.folder);

	if (lua_isnoneornil(L, 2)) {
		if (!rq_textures_check(l, tp.folder_shown, tp.name_sjis, tp.shown, -1, why, sizeof why))
			return luaL_error(L, "%s", why);
		if ((e = find(tp.folder, tp.name, 0))) {
			rq_log("texture: %s is the game's again (it was %s's)", tp.shown, e->mod);
			remove_tex(e);
			rq_reign_texture_changed(tp.folder, tp.name, 0);
		}
		return 0;
	}
	if (!get_pictures(L, 2, tp.shown, &pics, err, sizeof err))
		return luaL_error(L, "%s", err);
	if (!rq_textures_check(l, tp.folder_shown, tp.name_sjis, tp.shown, pics.n, why, sizeof why)) {
		free_pictures(&pics);
		return luaL_error(L, "%s", why);
	}
	e = find(tp.folder, tp.name, 0);
	old = e ? e->nfiles : 0;
	if (g_npictures - old + pics.n > TEX_PICTURES_MAX) {
		free_pictures(&pics);
		return luaL_error(L, "too many textures (max %d)", TEX_PICTURES_MAX);
	}
	set_tex(&tp, 0, NULL, mod, &pics);
	return 0;
}

/* One row of rq.image.textures. */
static void push_row(lua_State *L, const struct rq_texture *t, const char *folder_shown, const char *folder, int own)
{
	char *name = sjis2utf(t->name, 0);
	char nm[256];
	struct tex *e;

	lua_createtable(L, 0, 16);
	lua_pushstring(L, name);
	lua_setfield(L, -2, "name");
	lua_pushfstring(L, "%s/%s", folder_shown, name);
	lua_setfield(L, -2, "path");
	lua_pushstring(L, rq_textures_kind(t->kinds));
	lua_setfield(L, -2, "kind");
	lua_pushboolean(L, t->alpha_map);
	lua_setfield(L, -2, "alpha_map");
	lua_pushinteger(L, t->frames);
	lua_setfield(L, -2, "frames");
	lua_pushboolean(L, t->missing);
	lua_setfield(L, -2, "missing");
	if (!t->missing) {
		lua_pushinteger(L, t->w);
		lua_setfield(L, -2, "w");
		lua_pushinteger(L, t->h);
		lua_setfield(L, -2, "h");
		lua_pushboolean(L, t->alpha);
		lua_setfield(L, -2, "alpha");
	}
	free(name);

	snprintf(nm, sizeof nm, "%s", t->name);
	sjis_normalize_path(nm);
	e = lookup(folder, nm, own);
	if (e) {
		lua_pushstring(L, e->mod);
		lua_setfield(L, -2, "mod");
		if (e->list) {
			lua_createtable(L, e->nfiles, 0);
			for (int i = 0; i < e->nfiles; i++) {
				lua_pushstring(L, e->given[i]);
				lua_rawseti(L, -2, i + 1);
			}
		} else {
			lua_pushstring(L, e->given[0]);
		}
		lua_setfield(L, -2, "picture");
		lua_pushboolean(L, e->own != 0);
		lua_setfield(L, -2, "own");
		lua_pushinteger(L, e->used);
		lua_setfield(L, -2, "used");
	}
}

/* textures(model) -> rows, or nil for a folder or a number with no model */
int rq_lua_image_textures(lua_State *L)
{
	char folder_shown[RQ_PATH_MAX], folder[RQ_PATH_MAX];
	const struct rq_texture_list *l;
	int own = 0;

	if (lua_type(L, 1) == LUA_TNUMBER) {
		char *name = NULL, *poly = NULL;
		float s, x, y, z;
		int motions, file = 0, n;
		if (!lua_isinteger(L, 1))
			return luaL_error(L, "rq.image.textures: a model number is a whole number");
		lua_Integer no = lua_tointeger(L, 1);
		n = rq_models_count(&file);
		if (n < 0 || no < MODEL0 || no >= MODEL0 + n ||
		    !rq_model_info((int)(no - MODEL0), &name, &poly, &s, &x, &y, &z, &motions)) {
			lua_pushnil(L);
			return 1;
		}
		bool none = !poly[0];
		snprintf(folder_shown, sizeof folder_shown, "Data/PolyData/Chara/%s", poly);
		free(name);
		free(poly);
		if (none) {
			lua_pushnil(L);
			return 1;
		}
		if (no >= MODEL0 + file)
			own = (int)no;
	} else if (lua_type(L, 1) == LUA_TSTRING) {
		const char *s = lua_tostring(L, 1);
		size_t len;
		if (strchr(s, '*'))
			return luaL_error(L, "texture path '%s' has a '*' (a mod's model's textures go through rq.monster.model)", s);
		if (strlen(s) >= sizeof folder_shown)
			return luaL_error(L, "rq.image.textures: the folder's path is too long");
		snprintf(folder_shown, sizeof folder_shown, "%s", s);
		for (char *p = folder_shown; *p; p++) {
			if (*p == '\\')
				*p = '/';
		}
		len = strlen(folder_shown);
		while (len > 0 && folder_shown[len - 1] == '/')
			folder_shown[--len] = '\0';
	} else {
		return luaL_error(L, "rq.image.textures: model must be a folder or a model number");
	}
	char *sjis = utf2sjis(folder_shown, 0);
	snprintf(folder, sizeof folder, "%s", sjis ? sjis : "");
	free(sjis);
	norm(folder);
	if (!folder[0] || !(l = list_of(folder))) {
		lua_pushnil(L);
		return 1;
	}
	lua_createtable(L, l->n, 0);
	for (int i = 0; i < l->n; i++) {
		push_row(L, &l->rows[i], folder_shown, folder, own);
		lua_rawseti(L, -2, i + 1);
	}
	return 1;
}

/* ---- a model's own textures (lua/monster.lua) ---- */

/* What rq.monster.model gave, checked and resolved, until the model is made. */
struct staged {
	struct tex_path tp;
	struct pictures pics;
};
static struct staged *g_stage;
static int g_nstage;
static char g_stage_folder[RQ_PATH_MAX];

static void stage_clear(void)
{
	for (int i = 0; i < g_nstage; i++)
		free_pictures(&g_stage[i].pics);
	free(g_stage);
	g_stage = NULL;
	g_nstage = 0;
	g_stage_folder[0] = '\0';
}

/* textures_stage(base_index, textures|nil, who, model_no|0) -> the count staged, or nil and the
 * error's text. Checks a model's own textures against its base's folder, and the cap with what the
 * model (model_no, when it exists) has now, before anything is changed. */
static int l_textures_stage(lua_State *L)
{
	int base = (int)luaL_checkinteger(L, 1);
	const char *who = luaL_checkstring(L, 3);
	int no = (int)luaL_optinteger(L, 4, 0);
	char *name = NULL, *poly = NULL, err[RQ_PATH_MAX + 300], why[RQ_PATH_MAX + 200];
	char folder_shown[RQ_PATH_MAX];
	float s, x, y, z;
	int motions, total = 0;
	const struct rq_texture_list *l;

	stage_clear();
	if (lua_isnoneornil(L, 2)) {
		lua_pushinteger(L, 0);
		return 1;
	}
	luaL_checktype(L, 2, LUA_TTABLE);
	if (!rq_model_info(base, &name, &poly, &s, &x, &y, &z, &motions)) {
		lua_pushnil(L);
		lua_pushfstring(L, "%s: textures: no base model", who);
		return 2;
	}
	snprintf(folder_shown, sizeof folder_shown, "Data/PolyData/Chara/%s", poly);
	free(name);
	free(poly);
	{
		char *sjis = utf2sjis(folder_shown, 0);
		snprintf(g_stage_folder, sizeof g_stage_folder, "%s", sjis ? sjis : "");
		free(sjis);
		norm(g_stage_folder);
	}
	l = list_of(g_stage_folder);
	lua_pushnil(L);
	while (lua_next(L, 2)) {
		struct staged st;
		const char *key;
		char given[RQ_PATH_MAX];
		if (lua_type(L, -2) != LUA_TSTRING) {
			lua_pop(L, 2);
			stage_clear();
			lua_pushnil(L);
			lua_pushfstring(L, "%s: textures: a key must be a texture's name", who);
			return 2;
		}
		key = lua_tostring(L, -2);
		if (!strcmp(key, "*")) {
			/* Colors only (2.64.0's colors by numbers). */
			lua_pop(L, 2);
			stage_clear();
			lua_pushnil(L);
			lua_pushfstring(L, "%s: textures: '*' takes colors", who);
			return 2;
		}
		if (snprintf(given, sizeof given, "%s/%s", folder_shown, key) >= (int)sizeof given ||
		    strchr(key, '/') || strchr(key, '\\') || !split_path(given, &st.tp)) {
			lua_pop(L, 2);
			stage_clear();
			lua_pushnil(L);
			lua_pushfstring(L, "%s: textures: texture '%s' is not a texture of the model in '%s' (rq.image.textures lists them)",
					who, key, folder_shown);
			return 2;
		}
		/* The name as rq.monster.model was given it, in the texts. */
		snprintf(st.tp.shown, sizeof st.tp.shown, "%s", key);
		if (!get_pictures(L, lua_gettop(L), key, &st.pics, err, sizeof err)) {
			lua_pop(L, 2);
			stage_clear();
			lua_pushnil(L);
			lua_pushfstring(L, "%s: textures: %s", who, err);
			return 2;
		}
		if (!rq_textures_check(l, folder_shown, st.tp.name_sjis, key, st.pics.n, why, sizeof why)) {
			free_pictures(&st.pics);
			lua_pop(L, 2);
			stage_clear();
			lua_pushnil(L);
			lua_pushfstring(L, "%s: textures: %s", who, why);
			return 2;
		}
		/* The path as rq.image.textures shows it (`given` fit, so it fits). */
		if (snprintf(st.tp.shown, sizeof st.tp.shown, "%s/%s", folder_shown, key) >= (int)sizeof st.tp.shown)
			st.tp.shown[sizeof st.tp.shown - 1] = '\0';
		total += st.pics.n;
		g_stage = xrealloc(g_stage, (g_nstage + 1) * sizeof *g_stage);
		g_stage[g_nstage++] = st;
		lua_pop(L, 1);
	}
	{
		int had = 0;
		for (int i = 0; no && i < g_ntex; i++) {
			if (g_tex[i].own == no)
				had += g_tex[i].nfiles;
		}
		if (g_npictures - had + total > TEX_PICTURES_MAX) {
			stage_clear();
			lua_pushnil(L);
			lua_pushfstring(L, "%s: textures: too many textures (max %d)", who, TEX_PICTURES_MAX);
			return 2;
		}
	}
	lua_pushinteger(L, g_nstage);
	return 1;
}

/* textures_commit(model_no, id): the staged textures (textures_stage checked them and the cap)
 * become the model's own, in the place of what it had (none staged: it has none). */
static int l_textures_commit(lua_State *L)
{
	int no = (int)luaL_checkinteger(L, 1);
	const char *id = luaL_checkstring(L, 2);
	const char *mod = rq_lua_caller_mod(L);
	char folder[RQ_PATH_MAX] = "";

	for (int i = g_ntex - 1; i >= 0; i--) {
		if (g_tex[i].own == no) {
			snprintf(folder, sizeof folder, "%s", g_tex[i].folder);
			remove_tex(&g_tex[i]);
		}
	}
	for (int i = 0; i < g_nstage; i++)
		set_tex(&g_stage[i].tp, no, id, mod, &g_stage[i].pics);
	if (!g_nstage && folder[0])
		rq_log("texture: %s has the base's textures again", id);
	if (folder[0])
		rq_reign_texture_changed(folder, NULL, no);
	stage_clear();
	return 0;
}

void rq_texture_private(lua_State *L, int priv)
{
	lua_pushcfunction(L, l_textures_stage);
	lua_setfield(L, priv, "textures_stage");
	lua_pushcfunction(L, l_textures_commit);
	lua_setfield(L, priv, "textures_commit");
}

#endif /* RQ_MODLOADER */
