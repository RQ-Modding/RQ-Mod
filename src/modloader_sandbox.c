/* Mod sandbox: read/require only the mod folder, plus rq.*.
 * No write outside that folder, no absolute dofile/loadfile, no os.execute.
 *
 * Each mod has its own global table (its _ENV). Reads of a name it never set fall
 * through to the shared _G (rq, string, pairs...), writes stay in the mod's table.
 * Every chunk a mod loads (init.lua, require, load, loadfile, dofile) gets it.
 */

#ifdef RQ_MODLOADER

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include "lua.h"
#include "lauxlib.h"

#include "modloader_priv.h"
#include "modloader_order.h"

#define RQ_SANDBOX_MODS 64

enum { MOD_LOADING, MOD_LOADED, MOD_FAILED };

static char g_mod_dirs[RQ_SANDBOX_MODS][RQ_PATH_MAX];
static int g_mod_env[RQ_SANDBOX_MODS];
static char g_mod_id[RQ_SANDBOX_MODS][RQ_ID_LEN];
static char g_mod_deps[RQ_SANDBOX_MODS][RQ_EDGE_MAX][RQ_ID_LEN];
static int g_mod_ndeps[RQ_SANDBOX_MODS];
static int g_mod_export[RQ_SANDBOX_MODS];   /* what init.lua returned, or LUA_NOREF */
static int g_mod_state[RQ_SANDBOX_MODS];
static int g_nmod_dirs;

static int g_io_open = LUA_NOREF;
static int g_io_lines = LUA_NOREF;
static int g_io_input = LUA_NOREF;
static int g_io_output = LUA_NOREF;
static int g_os_remove = LUA_NOREF;
static int g_os_rename = LUA_NOREF;
static int g_load = LUA_NOREF;
static int g_require = LUA_NOREF;

static int path_is_abs(const char *p)
{
	if (!p || !p[0])
		return 0;
	if (p[0] == '\\' || p[0] == '/' )
		return 1;
	if (((p[0] >= 'A' && p[0] <= 'Z') || (p[0] >= 'a' && p[0] <= 'z')) && p[1] == ':')
		return 1;
	return 0;
}

static void strip_slash(char *p)
{
	size_t n;
	if (!p)
		return;
	n = strlen(p);
	while (n > 0 && (p[n - 1] == '\\' || p[n - 1] == '/')) {
		p[n - 1] = 0;
		n--;
	}
}

static int full_path(const char *in, char *out, int n)
{
#ifdef _WIN32
	DWORD got;
	if (!in || !out || n <= 0)
		return 0;
	got = GetFullPathNameA(in, (DWORD)n, out, NULL);
	if (got == 0 || got >= (DWORD)n)
		return 0;
	return 1;
#else
	(void)in;
	(void)out;
	(void)n;
	return 0;
#endif
}

static int under_dir(const char *dir, const char *full)
{
	size_t n;
	if (!dir || !full)
		return 0;
	n = strlen(dir);
	if (n == 0)
		return 0;
#ifdef _WIN32
	if (_strnicmp(full, dir, n) != 0)
		return 0;
#else
	if (strncmp(full, dir, n) != 0)
		return 0;
#endif
	return full[n] == 0 || full[n] == '\\' || full[n] == '/';
}

/* The mod's global table: its own names, then the shared _G for the rest.
 * _G inside the mod is this table. The metatable is hidden, so a mod cannot
 * reach the shared _G through it, and setmetatable(_G, ...) is an error. */
static const char *const g_std_libs[] = {
	"string", "table", "math", "utf8", "coroutine", "os", "io", NULL
};

/* The mod's own package table: its require cache (loaded), and empty paths.
 * loaded starts with the standard libraries and the mod's own _G and package. */
static void new_mod_package(lua_State *L, int env)
{
	int i;
	lua_createtable(L, 0, 3);
	lua_newtable(L);
	for (i = 0; g_std_libs[i]; i++) {
		lua_getglobal(L, g_std_libs[i]);
		lua_setfield(L, -2, g_std_libs[i]);
	}
	lua_pushvalue(L, env);
	lua_setfield(L, -2, "_G");
	lua_pushvalue(L, -2);
	lua_setfield(L, -2, "package");
	lua_setfield(L, -2, "loaded");
	lua_pushliteral(L, "");
	lua_setfield(L, -2, "path");
	lua_pushliteral(L, "");
	lua_setfield(L, -2, "cpath");
	lua_setfield(L, env, "package");
}

static int new_mod_env(lua_State *L)
{
	lua_newtable(L);
	lua_pushvalue(L, -1);
	lua_setfield(L, -2, "_G");
	new_mod_package(L, lua_gettop(L));
	lua_createtable(L, 0, 2);
	lua_pushglobaltable(L);
	lua_setfield(L, -2, "__index");
	lua_pushboolean(L, 0);
	lua_setfield(L, -2, "__metatable");
	lua_setmetatable(L, -2);
	return luaL_ref(L, LUA_REGISTRYINDEX);
}

int rq_sandbox_add_mod(lua_State *L, const char *dir, const struct rq_mod_node *node)
{
	char full[RQ_PATH_MAX];
	int i = g_nmod_dirs, k;
	if (!dir || !dir[0] || !node || i >= RQ_SANDBOX_MODS)
		return -1;
	if (!full_path(dir, full, (int)sizeof full))
		return -1;
	strip_slash(full);
	snprintf(g_mod_dirs[i], sizeof g_mod_dirs[0], "%s", full);
	snprintf(g_mod_id[i], sizeof g_mod_id[0], "%s", node->id);
	g_mod_ndeps[i] = node->ndeps < RQ_EDGE_MAX ? node->ndeps : RQ_EDGE_MAX;
	for (k = 0; k < g_mod_ndeps[i]; k++)
		snprintf(g_mod_deps[i][k], RQ_ID_LEN, "%s", node->deps[k]);
	g_mod_export[i] = LUA_NOREF;
	g_mod_state[i] = MOD_LOADING;
	g_mod_env[i] = new_mod_env(L);
	g_nmod_dirs = i + 1;
	return i;
}

/* After the mod's init.lua: ok with its return value on top (popped), or failed. */
void rq_sandbox_mod_done(lua_State *L, int mod, int ok)
{
	if (mod < 0 || mod >= g_nmod_dirs)
		return;
	g_mod_state[mod] = ok ? MOD_LOADED : MOD_FAILED;
	if (!ok)
		return;
	if (lua_isnil(L, -1))
		lua_pop(L, 1);
	else
		g_mod_export[mod] = luaL_ref(L, LUA_REGISTRYINDEX);
}

static int mod_by_id(const char *id)
{
	int i;
	for (i = 0; i < g_nmod_dirs; i++)
		if (strcmp(g_mod_id[i], id) == 0)
			return i;
	return -1;
}

static int is_dep(int mod, const char *id)
{
	int k;
	for (k = 0; k < g_mod_ndeps[mod]; k++)
		if (strcmp(g_mod_deps[mod][k], id) == 0)
			return 1;
	return 0;
}

/* The mod of the nearest stack level that runs a file in a mod folder.
 * Nearest, so a library mod's code called by another mod counts as the library. */
static int caller_mod(lua_State *L)
{
	lua_Debug ar;
	int level, i;
	for (level = 1; lua_getstack(L, level, &ar); level++) {
		char full[RQ_PATH_MAX];
		const char *src;
		int best = -1, bestn = -1;
		if (!lua_getinfo(L, "S", &ar))
			break;
		src = ar.source;
		if (!src || src[0] != '@')
			continue;
		if (!full_path(src + 1, full, (int)sizeof full))
			continue;
		for (i = 0; i < g_nmod_dirs; i++) {
			int len = (int)strlen(g_mod_dirs[i]);
			if (under_dir(g_mod_dirs[i], full) && len > bestn) {
				best = i;
				bestn = len;
			}
		}
		if (best >= 0)
			return best;
	}
	return -1;
}

static int caller_dir(lua_State *L, char *out, int n)
{
	int m = caller_mod(L);
	if (m < 0)
		return 0;
	snprintf(out, n, "%s", g_mod_dirs[m]);
	return 1;
}

/* Gives the function at fidx the mod's globals as its _ENV (its first upvalue,
 * as Lua's own load does). No mod: the chunk keeps the shared _G. */
static void set_mod_env(lua_State *L, int fidx, int mod)
{
	fidx = lua_absindex(L, fidx);
	if (mod < 0 || mod >= g_nmod_dirs)
		return;
	lua_rawgeti(L, LUA_REGISTRYINDEX, g_mod_env[mod]);
	if (!lua_setupvalue(L, fidx, 1))
		lua_pop(L, 1);
}

void rq_sandbox_set_env(lua_State *L, int fidx, int mod)
{
	set_mod_env(L, fidx, mod);
}

/* allow_abs: io may use an absolute path that still sits in the mod folder.
 * dofile/loadfile pass 0 and reject every absolute path. */
static int resolve_in_mod(lua_State *L, const char *path, int allow_abs, char *out, int n)
{
	char dir[RQ_PATH_MAX];
	char joined[RQ_PATH_MAX];
	char full[RQ_PATH_MAX];
	if (!path || !path[0])
		return 0;
	if (!caller_dir(L, dir, (int)sizeof dir))
		return 0;
	if (path_is_abs(path)) {
		if (!allow_abs)
			return 0;
		if (!full_path(path, full, (int)sizeof full))
			return 0;
	} else {
		snprintf(joined, sizeof joined, "%s\\%s", dir, path);
		if (!full_path(joined, full, (int)sizeof full))
			return 0;
	}
	if (!under_dir(dir, full))
		return 0;
	snprintf(out, n, "%s", full);
	return 1;
}

/* A path relative to the calling mod's folder. 0 if it leaves the folder. */
int rq_sandbox_resolve(lua_State *L, const char *rel, char *out, int n)
{
	return resolve_in_mod(L, rel, 0, out, n);
}

static int call_orig(lua_State *L, int ref, int narg)
{
	int base = lua_gettop(L);
	int i;
	lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
	for (i = 0; i < narg; i++)
		lua_pushvalue(L, i + 1);
	lua_call(L, narg, LUA_MULTRET);
	return lua_gettop(L) - base;
}

static int sand_deny_exec(lua_State *L)
{
	return luaL_error(L, "os.execute is not available to mods");
}

static int sand_deny_popen(lua_State *L)
{
	return luaL_error(L, "io.popen is not available to mods");
}

static int sand_deny_tmp(lua_State *L)
{
	return luaL_error(L, "temporary files outside the mod folder are not available");
}

static int sand_deny_loadlib(lua_State *L)
{
	return luaL_error(L, "package.loadlib is not available to mods");
}

static int sand_io_open(lua_State *L)
{
	const char *path = luaL_checkstring(L, 1);
	char full[RQ_PATH_MAX];
	int base, nargs = 1;
	if (!resolve_in_mod(L, path, 1, full, (int)sizeof full))
		return luaL_error(L, "io outside the mod folder");
	base = lua_gettop(L);
	lua_rawgeti(L, LUA_REGISTRYINDEX, g_io_open);
	lua_pushstring(L, full);
	if (lua_isstring(L, 2)) {
		lua_pushvalue(L, 2);
		nargs = 2;
	}
	lua_call(L, nargs, LUA_MULTRET);
	return lua_gettop(L) - base;
}

static int sand_io_lines(lua_State *L)
{
	const char *path;
	char full[RQ_PATH_MAX];
	if (lua_gettop(L) < 1 || lua_isnil(L, 1))
		return luaL_error(L, "io.lines needs a file in the mod folder");
	path = luaL_checkstring(L, 1);
	if (!resolve_in_mod(L, path, 1, full, (int)sizeof full))
		return luaL_error(L, "io outside the mod folder");
	lua_pushstring(L, full);
	lua_replace(L, 1);
	return call_orig(L, g_io_lines, lua_gettop(L));
}

static int sand_io_setfile(lua_State *L, int fn, const char *what)
{
	const char *path;
	char full[RQ_PATH_MAX];
	if (lua_gettop(L) < 1 || lua_isnil(L, 1))
		return call_orig(L, fn, 0);
	if (!lua_isstring(L, 1))
		return call_orig(L, fn, lua_gettop(L));
	path = lua_tostring(L, 1);
	if (!resolve_in_mod(L, path, 1, full, (int)sizeof full))
		return luaL_error(L, "%s outside the mod folder", what);
	lua_pushstring(L, full);
	lua_replace(L, 1);
	return call_orig(L, fn, lua_gettop(L));
}

static int sand_io_input(lua_State *L)
{
	return sand_io_setfile(L, g_io_input, "io.input");
}

static int sand_io_output(lua_State *L)
{
	return sand_io_setfile(L, g_io_output, "io.output");
}

static int sand_os_remove(lua_State *L)
{
	const char *path = luaL_checkstring(L, 1);
	char full[RQ_PATH_MAX];
	if (!resolve_in_mod(L, path, 1, full, (int)sizeof full))
		return luaL_error(L, "os.remove outside the mod folder");
	lua_pushstring(L, full);
	lua_replace(L, 1);
	return call_orig(L, g_os_remove, 1);
}

static int sand_os_rename(lua_State *L)
{
	const char *a = luaL_checkstring(L, 1);
	const char *b = luaL_checkstring(L, 2);
	char fa[RQ_PATH_MAX], fb[RQ_PATH_MAX];
	if (!resolve_in_mod(L, a, 1, fa, (int)sizeof fa)
			|| !resolve_in_mod(L, b, 1, fb, (int)sizeof fb))
		return luaL_error(L, "os.rename outside the mod folder");
	lua_pushstring(L, fa);
	lua_replace(L, 1);
	lua_pushstring(L, fb);
	lua_replace(L, 2);
	return call_orig(L, g_os_rename, 2);
}

/* A chunk's name says which mod it belongs to, for rq.log and for the folder the
 * sandbox allows. So a chunk stays in the mod that loads it, whatever name it asks for.
 * With no env argument, the chunk gets the mod's globals; an env given (even nil) is kept. */
static int sand_load(lua_State *L)
{
	char dir[RQ_PATH_MAX];
	const char *name = luaL_optstring(L, 2, "load");
	const char *p;
	const char *base = name;
	int mod = caller_mod(L);
	int own_env = lua_gettop(L) < 4;

	for (p = name; *p; p++) {
		if (*p == '\\' || *p == '/')
			base = p + 1;
	}
	if (*base == '@' || *base == '=')
		base++;
	if (lua_gettop(L) < 2)
		lua_settop(L, 2);
	if (caller_dir(L, dir, (int)sizeof dir))
		lua_pushfstring(L, "@%s\\%s", dir, base);
	else
		lua_pushfstring(L, "=%s", base);
	lua_replace(L, 2);
	if (own_env && mod >= 0) {
		lua_settop(L, 3);
		lua_rawgeti(L, LUA_REGISTRYINDEX, g_mod_env[mod]);
	}
	return call_orig(L, g_load, lua_gettop(L));
}

static int sand_dofile(lua_State *L)
{
	const char *path = luaL_checkstring(L, 1);
	char full[RQ_PATH_MAX];
	int base, mod = caller_mod(L);
	if (path_is_abs(path))
		return luaL_error(L, "dofile of an absolute path is not allowed");
	if (!resolve_in_mod(L, path, 0, full, (int)sizeof full))
		return luaL_error(L, "dofile outside the mod folder");
	if (luaL_loadfile(L, full) != LUA_OK)
		return lua_error(L);
	set_mod_env(L, -1, mod);
	base = lua_gettop(L) - 1;
	lua_call(L, 0, LUA_MULTRET);
	return lua_gettop(L) - base;
}

/* loadfile(path [, mode [, env]]), as in Lua: with no env, the mod's globals. */
static int sand_loadfile_fix(lua_State *L)
{
	const char *path = luaL_checkstring(L, 1);
	char full[RQ_PATH_MAX];
	const char *mode = NULL;
	int err, mod = caller_mod(L);
	int has_env = !lua_isnone(L, 3);
	if (path_is_abs(path))
		return luaL_error(L, "loadfile of an absolute path is not allowed");
	if (!resolve_in_mod(L, path, 0, full, (int)sizeof full))
		return luaL_error(L, "loadfile outside the mod folder");
	if (lua_isstring(L, 2))
		mode = lua_tostring(L, 2);
	err = mode ? luaL_loadfilex(L, full, mode) : luaL_loadfile(L, full);
	if (err != LUA_OK) {
		lua_pushnil(L);
		lua_insert(L, -2);
		return 2;
	}
	if (has_env) {
		lua_pushvalue(L, 3);
		if (!lua_setupvalue(L, -2, 1))
			lua_pop(L, 1);
	} else {
		set_mod_env(L, -1, mod);
	}
	return 1;
}

static int rq_module_loader(lua_State *L)
{
	const char *name = luaL_checkstring(L, 1);
	const char *p;
	lua_getglobal(L, "rq");
	if (!lua_istable(L, -1))
		return luaL_error(L, "rq is not available");
	if (strcmp(name, "rq") == 0)
		return 1;
	p = name + 3;
	while (*p) {
		char part[64];
		const char *dot = strchr(p, '.');
		size_t nlen = dot ? (size_t)(dot - p) : strlen(p);
		if (nlen == 0 || nlen >= sizeof part)
			return luaL_error(L, "bad module name");
		memcpy(part, p, nlen);
		part[nlen] = 0;
		if (!lua_istable(L, -1))
			return luaL_error(L, "no module %s", name);
		lua_getfield(L, -1, part);
		lua_remove(L, -2);
		if (lua_isnil(L, -1))
			return luaL_error(L, "no module %s", name);
		p = dot ? dot + 1 : p + nlen;
	}
	return 1;
}

/* Loads rel from the mod's folder: 1 with the chunk pushed (and its path in path),
 * 0 when there is no such file. A file that does not compile is an error. */
static int module_file(lua_State *L, int mod, const char *rel, char *path, int n)
{
	char joined[RQ_PATH_MAX], full[RQ_PATH_MAX];
	const char *dir = g_mod_dirs[mod];
	int err;
	snprintf(joined, sizeof joined, "%s\\%s", dir, rel);
	if (!full_path(joined, full, (int)sizeof full))
		return 0;
	if (!under_dir(dir, full))
		return 0;
	err = luaL_loadfile(L, full);
	if (err == LUA_ERRFILE) {
		lua_pop(L, 1);
		return 0;
	}
	if (err != LUA_OK)
		luaL_error(L, "error loading module '%s': %s", rel, lua_tostring(L, -1));
	set_mod_env(L, -1, mod);
	snprintf(path, n, "%s", full);
	return 1;
}

/* name.lua, then name\init.lua, in the mod's folder. Dots are folders. */
static int find_module(lua_State *L, int mod, const char *name, char *path, int n)
{
	char rel[RQ_PATH_MAX], lua_rel[RQ_PATH_MAX], init_rel[RQ_PATH_MAX];
	const char *s;
	size_t k = 0;
	for (s = name; *s && k + 2 < sizeof rel; s++)
		rel[k++] = (*s == '.') ? '\\' : *s;
	rel[k] = 0;
	snprintf(lua_rel, sizeof lua_rel, "%s.lua", rel);
	snprintf(init_rel, sizeof init_rel, "%s\\init.lua", rel);
	return module_file(L, mod, lua_rel, path, n) || module_file(L, mod, init_rel, path, n);
}

static int sand_searcher(lua_State *L)
{
	const char *name = luaL_checkstring(L, 1);
	char rel[RQ_PATH_MAX];
	int mod;
	if (!strncmp(name, "rq", 2) && (name[2] == 0 || name[2] == '.')) {
		lua_pushcfunction(L, rq_module_loader);
		return 1;
	}
	if (strpbrk(name, ":/\\") || strstr(name, "..")) {
		lua_pushfstring(L, "\n\tmodule '%s' is outside the mod folder", name);
		return 1;
	}
	mod = caller_mod(L);
	if (mod < 0) {
		lua_pushfstring(L, "\n\tno mod folder for '%s'", name);
		return 1;
	}
	if (find_module(L, mod, name, rel, (int)sizeof rel)) {
		lua_pushstring(L, rel);
		return 2;
	}
	lua_pushfstring(L, "\n\tno file '%s.lua' in the mod folder", name);
	return 1;
}

/* require("<id>") of a mod in the caller's dependencies: what its init.lua returned. */
static int require_dep(lua_State *L, int loaded, const char *name)
{
	int d = mod_by_id(name);
	if (d < 0 || g_mod_state[d] != MOD_LOADED)
		return luaL_error(L, "module '%s': the mod '%s' did not load (see rqmod.log)", name, name);
	if (g_mod_export[d] == LUA_NOREF)
		return luaL_error(L, "module '%s': the mod '%s' shares nothing (its init.lua returns no value)",
				name, name);
	lua_rawgeti(L, LUA_REGISTRYINDEX, g_mod_export[d]);
	lua_pushvalue(L, -1);
	lua_setfield(L, loaded, name);
	return 1;
}

/* require for mods. Each mod has its own cache (its package.loaded), so two mods
 * with a util.lua each get their own. Order: rq and rq.*, the mod's cache, a file in
 * the mod's folder, then the id of a mod in its dependencies. Code outside any mod
 * keeps Lua's own require. */
static int sand_require(lua_State *L)
{
	const char *name = luaL_checkstring(L, 1);
	char path[RQ_PATH_MAX];
	int mod, loaded, dep, bad;
	if (!strncmp(name, "rq", 2) && (name[2] == 0 || name[2] == '.')) {
		lua_pushcfunction(L, rq_module_loader);
		lua_pushstring(L, name);
		lua_call(L, 1, 1);
		return 1;
	}
	mod = caller_mod(L);
	if (mod < 0)
		return call_orig(L, g_require, 1);
	lua_settop(L, 1);
	lua_rawgeti(L, LUA_REGISTRYINDEX, g_mod_env[mod]);
	lua_getfield(L, -1, "package");
	lua_remove(L, -2);
	if (!lua_istable(L, -1))
		return luaL_error(L, "require: package is not a table");
	lua_getfield(L, -1, "loaded");
	lua_remove(L, -2);
	if (!lua_istable(L, -1))
		return luaL_error(L, "require: package.loaded is not a table");
	loaded = lua_gettop(L);
	if (lua_getfield(L, loaded, name) != LUA_TNIL && lua_toboolean(L, -1))
		return 1;
	lua_pop(L, 1);
	dep = is_dep(mod, name);
	bad = strpbrk(name, ":/\\") || strstr(name, "..");
	if (bad || !find_module(L, mod, name, path, (int)sizeof path)) {
		if (dep)
			return require_dep(L, loaded, name);
		if (bad)
			return luaL_error(L, "module '%s' is outside the mod folder", name);
		if (mod_by_id(name) >= 0)
			return luaL_error(L, "module '%s' not found:\n\tno file '%s.lua' in the mod folder"
					"\n\t'%s' is a mod: add it to dependencies in mod.json to require it,"
					" or use rq.mod.get", name, name, name);
		return luaL_error(L, "module '%s' not found:\n\tno file '%s.lua' in the mod folder",
				name, name);
	}
	if (dep)
		rq_log("require '%s' in %s: the file %s comes before the dependency of that id",
				name, g_mod_id[mod], path);
	lua_pushstring(L, name);
	lua_pushstring(L, path);
	lua_call(L, 2, 1);
	if (!lua_isnil(L, -1))
		lua_setfield(L, loaded, name);
	else
		lua_pop(L, 1);
	if (lua_getfield(L, loaded, name) == LUA_TNIL) {
		lua_pop(L, 1);
		lua_pushboolean(L, 1);
		lua_pushvalue(L, -1);
		lua_setfield(L, loaded, name);
	}
	lua_pushstring(L, path);
	return 2;
}

/* Shared tables. rq and the standard libraries are one table for every mod, so a mod that
 * writes rq.log = f or string.trim = f changes them for all. After each init.lua the loader
 * compares them with how they were and logs each field the mod added, changed or removed.
 * rq is walked three levels deep, as the API dump is; names starting with _ are skipped. */
#define WATCH_DEPTH 3
#define WATCH_LINES 20

static int g_watch = LUA_NOREF;   /* path -> value, as after the last mod */

static void watch_walk(lua_State *L, int snap, int visited, int t, const char *prefix, int depth)
{
	lua_pushnil(L);
	while (lua_next(L, t)) {
		char path[256];
		const char *key;
		if (lua_type(L, -2) != LUA_TSTRING || (key = lua_tostring(L, -2))[0] == '_') {
			lua_pop(L, 1);
			continue;
		}
		snprintf(path, sizeof path, "%s.%s", prefix, key);
		lua_pushvalue(L, -1);
		lua_setfield(L, snap, path);
		if (lua_type(L, -1) == LUA_TTABLE && depth < WATCH_DEPTH) {
			lua_pushvalue(L, -1);
			if (lua_rawget(L, visited) == LUA_TNIL) {
				lua_pop(L, 1);
				lua_pushvalue(L, -1);
				lua_pushboolean(L, 1);
				lua_rawset(L, visited);
				watch_walk(L, snap, visited, lua_gettop(L), path, depth + 1);
			} else {
				lua_pop(L, 1);
			}
		}
		lua_pop(L, 1);
	}
}

/* Pushes a new snapshot: every watched path and its value. */
static void watch_snapshot(lua_State *L)
{
	int i, snap, visited;
	lua_newtable(L);
	snap = lua_gettop(L);
	lua_newtable(L);
	visited = lua_gettop(L);
	if (lua_getglobal(L, "rq") == LUA_TTABLE) {
		lua_pushvalue(L, -1);
		lua_pushboolean(L, 1);
		lua_rawset(L, visited);
		watch_walk(L, snap, visited, lua_gettop(L), "rq", 1);
	}
	lua_pop(L, 1);
	for (i = 0; g_std_libs[i]; i++) {
		if (lua_getglobal(L, g_std_libs[i]) == LUA_TTABLE)
			watch_walk(L, snap, visited, lua_gettop(L), g_std_libs[i], WATCH_DEPTH);
		lua_pop(L, 1);
	}
	lua_pop(L, 1);
}

static void watch_line(int *lines, const char *mod, const char *what, const char *path)
{
	if (++*lines <= WATCH_LINES)
		rq_log("%s %s %s, in a table every mod shares", mod, what, path);
}

void rq_sandbox_watch(lua_State *L, int mod)
{
	int now, old, lines = 0;
	const char *id = mod >= 0 && mod < g_nmod_dirs ? g_mod_id[mod] : NULL;
	watch_snapshot(L);
	now = lua_gettop(L);
	if (g_watch != LUA_NOREF && id) {
		lua_rawgeti(L, LUA_REGISTRYINDEX, g_watch);
		old = lua_gettop(L);
		lua_pushnil(L);
		while (lua_next(L, now)) {
			const char *path = lua_tostring(L, -2);
			lua_getfield(L, old, path);
			if (lua_isnil(L, -1))
				watch_line(&lines, id, "added", path);
			else if (!lua_rawequal(L, -1, -2))
				watch_line(&lines, id, "changed", path);
			lua_pop(L, 2);
		}
		lua_pushnil(L);
		while (lua_next(L, old)) {
			const char *path = lua_tostring(L, -2);
			lua_getfield(L, now, path);
			if (lua_isnil(L, -1))
				watch_line(&lines, id, "removed", path);
			lua_pop(L, 2);
		}
		lua_pop(L, 1);
		if (lines > WATCH_LINES)
			rq_log("%s changed %d more fields in shared tables", id, lines - WATCH_LINES);
	}
	if (g_watch != LUA_NOREF)
		luaL_unref(L, LUA_REGISTRYINDEX, g_watch);
	g_watch = luaL_ref(L, LUA_REGISTRYINDEX);
}

/* rq.mod.get(id): what a loaded mod's init.lua returned, or nil. */
static int l_mod_get(lua_State *L)
{
	int d = mod_by_id(luaL_checkstring(L, 1));
	if (d < 0 || g_mod_state[d] != MOD_LOADED || g_mod_export[d] == LUA_NOREF)
		lua_pushnil(L);
	else
		lua_rawgeti(L, LUA_REGISTRYINDEX, g_mod_export[d]);
	return 1;
}

/* rq.mod.id(): the calling mod's id, or nil outside a mod. */
static int l_mod_id(lua_State *L)
{
	int m = caller_mod(L);
	if (m < 0)
		lua_pushnil(L);
	else
		lua_pushstring(L, g_mod_id[m]);
	return 1;
}

/* rq.mod.list(): the ids of the mods whose init.lua finished, in load order. */
static int l_mod_list(lua_State *L)
{
	int i, n = 0;
	lua_newtable(L);
	for (i = 0; i < g_nmod_dirs; i++) {
		if (g_mod_state[i] != MOD_LOADED)
			continue;
		lua_pushstring(L, g_mod_id[i]);
		lua_rawseti(L, -2, ++n);
	}
	return 1;
}

void rq_sandbox_open(lua_State *L, int rq_idx)
{
	rq_idx = lua_absindex(L, rq_idx);
	lua_createtable(L, 0, 3);
	lua_pushcfunction(L, l_mod_get);
	lua_setfield(L, -2, "get");
	lua_pushcfunction(L, l_mod_id);
	lua_setfield(L, -2, "id");
	lua_pushcfunction(L, l_mod_list);
	lua_setfield(L, -2, "list");
	lua_setfield(L, rq_idx, "mod");
}

static int sand_searcher_stop(lua_State *L)
{
	lua_pushstring(L, "\n\tC modules are not available to mods");
	return 1;
}

static int grab(lua_State *L, const char *table, const char *field)
{
	int ref;
	lua_getglobal(L, table);
	lua_getfield(L, -1, field);
	ref = luaL_ref(L, LUA_REGISTRYINDEX);
	lua_pop(L, 1);
	return ref;
}

static void set_fn(lua_State *L, const char *table, const char *field, lua_CFunction fn)
{
	lua_getglobal(L, table);
	lua_pushcfunction(L, fn);
	lua_setfield(L, -2, field);
	lua_pop(L, 1);
}

void rq_sandbox_install(lua_State *L)
{
	g_nmod_dirs = 0;
	g_io_open = grab(L, "io", "open");
	g_io_lines = grab(L, "io", "lines");
	g_io_input = grab(L, "io", "input");
	g_io_output = grab(L, "io", "output");
	g_os_remove = grab(L, "os", "remove");
	g_os_rename = grab(L, "os", "rename");

	set_fn(L, "io", "open", sand_io_open);
	set_fn(L, "io", "lines", sand_io_lines);
	set_fn(L, "io", "input", sand_io_input);
	set_fn(L, "io", "output", sand_io_output);
	set_fn(L, "io", "popen", sand_deny_popen);
	set_fn(L, "io", "tmpfile", sand_deny_tmp);
	set_fn(L, "os", "execute", sand_deny_exec);
	set_fn(L, "os", "remove", sand_os_remove);
	set_fn(L, "os", "rename", sand_os_rename);
	set_fn(L, "os", "tmpname", sand_deny_tmp);
	set_fn(L, "package", "loadlib", sand_deny_loadlib);

	lua_getglobal(L, "load");
	g_load = luaL_ref(L, LUA_REGISTRYINDEX);
	lua_pushcfunction(L, sand_load);
	lua_setglobal(L, "load");
	lua_pushcfunction(L, sand_dofile);
	lua_setglobal(L, "dofile");
	lua_getglobal(L, "require");
	g_require = luaL_ref(L, LUA_REGISTRYINDEX);
	lua_pushcfunction(L, sand_require);
	lua_setglobal(L, "require");
	lua_pushcfunction(L, sand_loadfile_fix);
	lua_setglobal(L, "loadfile");

	lua_getglobal(L, "package");
	lua_getfield(L, -1, "searchers");
	if (lua_istable(L, -1)) {
		lua_pushcfunction(L, sand_searcher);
		lua_rawseti(L, -2, 2);
		lua_pushcfunction(L, sand_searcher_stop);
		lua_rawseti(L, -2, 3);
		lua_pushcfunction(L, sand_searcher_stop);
		lua_rawseti(L, -2, 4);
	}
	lua_pop(L, 2);
	lua_pushnil(L);
	lua_setglobal(L, "debug");
	lua_getglobal(L, "package");
	lua_getfield(L, -1, "loaded");
	if (lua_istable(L, -1)) {
		lua_pushnil(L);
		lua_setfield(L, -2, "debug");
	}
	lua_pop(L, 2);
}

#else

void rq_sandbox_install(struct lua_State *L) { (void)L; }
int rq_sandbox_add_mod(struct lua_State *L, const char *dir, const struct rq_mod_node *node)
{ (void)L; (void)dir; (void)node; return -1; }
void rq_sandbox_mod_done(struct lua_State *L, int mod, int ok) { (void)L; (void)mod; (void)ok; }
void rq_sandbox_open(struct lua_State *L, int rq_idx) { (void)L; (void)rq_idx; }
void rq_sandbox_watch(struct lua_State *L, int mod) { (void)L; (void)mod; }
void rq_sandbox_set_env(struct lua_State *L, int fidx, int mod) { (void)L; (void)fidx; (void)mod; }

#endif
