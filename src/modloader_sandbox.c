/* Mod sandbox: read/require only the mod folder, plus rq.*.
 * No write outside that folder, no absolute dofile/loadfile, no os.execute.
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

#define RQ_SANDBOX_MODS 64

static char g_mod_dirs[RQ_SANDBOX_MODS][RQ_PATH_MAX];
static int g_nmod_dirs;

static int g_io_open = LUA_NOREF;
static int g_io_lines = LUA_NOREF;
static int g_io_input = LUA_NOREF;
static int g_io_output = LUA_NOREF;
static int g_os_remove = LUA_NOREF;
static int g_os_rename = LUA_NOREF;
static int g_load = LUA_NOREF;

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

void rq_sandbox_add_mod(const char *dir)
{
	char full[RQ_PATH_MAX];
	if (!dir || !dir[0] || g_nmod_dirs >= RQ_SANDBOX_MODS)
		return;
	if (!full_path(dir, full, (int)sizeof full))
		return;
	strip_slash(full);
	snprintf(g_mod_dirs[g_nmod_dirs], sizeof g_mod_dirs[0], "%s", full);
	g_nmod_dirs++;
}

static int caller_dir(lua_State *L, char *out, int n)
{
	lua_Debug ar;
	int level, i, best = -1, bestn = -1;
	for (level = 1; level < 16; level++) {
		char full[RQ_PATH_MAX];
		const char *src;
		if (!lua_getstack(L, level, &ar) || !lua_getinfo(L, "S", &ar))
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
	}
	if (best < 0)
		return 0;
	snprintf(out, n, "%s", g_mod_dirs[best]);
	return 1;
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
 * sandbox allows. So a chunk stays in the mod that loads it, whatever name it asks for. */
static int sand_load(lua_State *L)
{
	char dir[RQ_PATH_MAX];
	const char *name = luaL_optstring(L, 2, "load");
	const char *p;
	const char *base = name;

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
	return call_orig(L, g_load, lua_gettop(L));
}

static int sand_dofile(lua_State *L)
{
	const char *path = luaL_checkstring(L, 1);
	char full[RQ_PATH_MAX];
	int base;
	if (path_is_abs(path))
		return luaL_error(L, "dofile of an absolute path is not allowed");
	if (!resolve_in_mod(L, path, 0, full, (int)sizeof full))
		return luaL_error(L, "dofile outside the mod folder");
	if (luaL_loadfile(L, full) != LUA_OK)
		return lua_error(L);
	base = lua_gettop(L) - 1;
	lua_call(L, 0, LUA_MULTRET);
	return lua_gettop(L) - base;
}

static int sand_loadfile_fix(lua_State *L)
{
	const char *path = luaL_checkstring(L, 1);
	char full[RQ_PATH_MAX];
	const char *mode = NULL;
	int err;
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

static int module_file(lua_State *L, const char *dir, const char *rel)
{
	char joined[RQ_PATH_MAX], full[RQ_PATH_MAX];
	snprintf(joined, sizeof joined, "%s\\%s", dir, rel);
	if (!full_path(joined, full, (int)sizeof full))
		return 0;
	if (!under_dir(dir, full))
		return 0;
	if (luaL_loadfile(L, full) != LUA_OK) {
		lua_pop(L, 1);
		return 0;
	}
	return 1;
}

static int sand_searcher(lua_State *L)
{
	const char *name = luaL_checkstring(L, 1);
	char dir[RQ_PATH_MAX];
	char rel[RQ_PATH_MAX];
	const char *s;
	size_t n = 0;
	if (!strncmp(name, "rq", 2) && (name[2] == 0 || name[2] == '.')) {
		lua_pushcfunction(L, rq_module_loader);
		return 1;
	}
	if (strpbrk(name, ":/\\") || strstr(name, "..")) {
		lua_pushfstring(L, "\n\tmodule '%s' is outside the mod folder", name);
		return 1;
	}
	if (!caller_dir(L, dir, (int)sizeof dir)) {
		lua_pushfstring(L, "\n\tno mod folder for '%s'", name);
		return 1;
	}
	for (s = name; *s && n + 2 < sizeof rel; s++) {
		rel[n++] = (*s == '.') ? '\\' : *s;
	}
	rel[n] = 0;
	{
		char lua_rel[RQ_PATH_MAX], init_rel[RQ_PATH_MAX];
		snprintf(lua_rel, sizeof lua_rel, "%s.lua", rel);
		snprintf(init_rel, sizeof init_rel, "%s\\init.lua", rel);
		if (module_file(L, dir, lua_rel) || module_file(L, dir, init_rel))
			return 1;
	}
	lua_pushfstring(L, "\n\tno file '%s.lua' in the mod folder", rel);
	return 1;
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
void rq_sandbox_add_mod(const char *dir) { (void)dir; }

#endif
