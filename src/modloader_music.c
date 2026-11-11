/* Copyright (C) 2026 Rance Quest modding fork
 *
 * rq.music (loader 2.4.0): a mod's file plays where the game plays one of its music tracks.
 *
 * The game plays music by number (SYS_PlayBGM → KiwiSoundEngine.Music_Prepare → bgm_prepare),
 * and reads the number back (迷宮音楽_再生中, saves, the system bar). So a mod keeps the number
 * and changes the file behind it: audio_prepare asks rq_music_open first for a music channel.
 *   - A replacement (rq.music.replace) stays until restored.
 *   - A once file (from rq.music.on_play) is used by the next prepare of that number only.
 * The file streams through libsndfile on the track's own mixer (channel_open_music_file), so
 * Config's music volume and the game's fades apply. Loop points come from the mod, else from
 * LOOPSTART / LOOPLENGTH / LOOPEND tags in the file (samples, as music packs write them), else
 * the whole file loops.
 *
 * This file is the C half: the table, the file probe, and what the channel plays now. The
 * rest of rq.music is Lua (lua/music.lua): the API, on_play, the track names.
 */

#ifdef RQ_MODLOADER

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include <sndfile.h>

#include "lua.h"
#include "lauxlib.h"

#include "system4.h"
#include "system4/file.h"
#include "system4/utfsjis.h"

#include "asset_manager.h"
#include "audio.h"
#include "mixer.h"

#include "modloader.h"
#include "modloader_priv.h"

#define MUSIC_MAX 1024          /* track numbers 1..1023; the game's go up to 213 */
#define PREPARED_MAX 64
#define TAG_SCAN (2u * 1024u * 1024u)

struct music_file {
	char full[RQ_PATH_MAX];
	char rel[256];
	char mod[64];
	int loop_start, loop_end;   /* frames; 0 = the file's start / end */
	int volume;                 /* 0..200 (2.27.0: above 100 raises the gain) */
	int rate;
	long frames;
};

static struct music_file *g_rep[MUSIC_MAX];
static struct music_file g_once;
static int g_once_no = -1;

/* What each music channel id was last prepared with: the number, and the mod file or "". */
static struct {
	int id, no;
	char rel[256];
	char mod[64];
	unsigned seq;
} g_prep[PREPARED_MAX];
static unsigned g_seq;

static SNDFILE *open_sf(const char *path, SF_INFO *info)
{
	memset(info, 0, sizeof *info);
#ifdef _WIN32
	wchar_t *w = utf8_to_wchar(path);
	SNDFILE *f = sf_wchar_open(w, SFM_READ, info);
	free(w);
	return f;
#else
	return sf_open(path, SFM_READ, info);
#endif
}

/* Finds "<key>=<digits>" anywhere in buf, ignoring case; -1 when absent. */
static long find_tag(const unsigned char *buf, size_t n, const char *key)
{
	size_t k = strlen(key);
	for (size_t i = 0; i + k + 1 < n; i++) {
		size_t j = 0;
		while (j < k && toupper(buf[i + j]) == key[j])
			j++;
		if (j < k || buf[i + k] != '=' || !isdigit(buf[i + k + 1]))
			continue;
		long v = 0;
		for (size_t p = i + k + 1; p < n && isdigit(buf[p]) && v < 0x7fffffffL / 10; p++)
			v = v * 10 + (buf[p] - '0');
		return v;
	}
	return -1;
}

/* LOOPSTART with LOOPLENGTH or LOOPEND, in samples, from the file's first 2 MB (the Vorbis
 * or Opus comment header). Leaves *start and *end alone when the file has none. */
static void read_loop_tags(const char *path, long *start, long *end)
{
	FILE *f = file_open_utf8(path, "rb");
	if (!f)
		return;
	unsigned char *buf = xmalloc(TAG_SCAN);
	size_t n = fread(buf, 1, TAG_SCAN, f);
	fclose(f);
	long s = find_tag(buf, n, "LOOPSTART");
	if (s >= 0) {
		long len = find_tag(buf, n, "LOOPLENGTH");
		long e = find_tag(buf, n, "LOOPEND");
		*start = s;
		if (len > 0)
			*end = s + len;
		else if (e > s)
			*end = e;
	}
	free(buf);
}

static int whole_track(lua_State *L, int arg)
{
	lua_Integer no = luaL_checkinteger(L, arg);
	if (no < 1 || no >= MUSIC_MAX)
		luaL_error(L, "music number %d is out of range (1 to %d)", (int)no, MUSIC_MAX - 1);
	return (int)no;
}

static double seconds(struct music_file *m, long frames)
{
	return m->rate > 0 ? (double)frames / m->rate : 0;
}

static void push_file(lua_State *L, struct music_file *m)
{
	lua_newtable(L);
	lua_pushstring(L, m->rel);
	lua_setfield(L, -2, "file");
	lua_pushstring(L, m->mod);
	lua_setfield(L, -2, "mod");
	lua_pushnumber(L, seconds(m, m->loop_start));
	lua_setfield(L, -2, "loop_start");
	lua_pushnumber(L, seconds(m, m->loop_end > 0 ? m->loop_end : m->frames));
	lua_setfield(L, -2, "loop_end");
	lua_pushnumber(L, seconds(m, m->frames));
	lua_setfield(L, -2, "seconds");
	lua_pushinteger(L, m->volume);
	lua_setfield(L, -2, "volume");
}

/* c.resolve(path) → the full path inside the calling mod's folder (2.27.0). */
static int c_resolve(lua_State *L)
{
	const char *rel = luaL_checkstring(L, 1);
	char full[RQ_PATH_MAX];
	if (!rq_sandbox_resolve(L, rel, full, (int)sizeof full))
		return luaL_error(L, "music path '%s' is not inside the mod folder", rel);
	lua_pushstring(L, full);
	return 1;
}

/* c.set(no, path, loop_start_s|nil, loop_end_s|nil, volume, mod, once[, rel]): checks the file
 * (inside the calling mod's folder, a sound libsndfile reads, at most 2 channels) and stores
 * it as the track's replacement, or as the next prepare's file when once. Returns the file's
 * info, as c.get. */
static int c_set(lua_State *L)
{
	int no = whole_track(L, 1);
	const char *rel = luaL_checkstring(L, 2);
	struct music_file m;
	SF_INFO info;

	memset(&m, 0, sizeof m);
	if (lua_type(L, 8) == LUA_TSTRING) {
		/* 2.27.0: a path c.resolve gave earlier, with the mod's own path as arg 8 (the
		 * Config page puts a track back on with no mod code on the stack). */
		snprintf(m.full, sizeof m.full, "%s", rel);
		rel = lua_tostring(L, 8);
	} else if (!rq_sandbox_resolve(L, rel, m.full, (int)sizeof m.full)) {
		return luaL_error(L, "music path '%s' is not inside the mod folder", rel);
	}
	SNDFILE *sf = open_sf(m.full, &info);
	if (!sf)
		return luaL_error(L, "music '%s': %s", rel, sf_strerror(NULL));
	sf_close(sf);
	if (info.channels < 1 || info.channels > 2 || info.samplerate <= 0 || info.frames <= 0)
		return luaL_error(L, "music '%s': not a sound with 1 or 2 channels", rel);
	snprintf(m.rel, sizeof m.rel, "%s", rel);
	snprintf(m.mod, sizeof m.mod, "%s", luaL_optstring(L, 6, "?"));
	m.rate = info.samplerate;
	m.frames = (long)info.frames;
	m.volume = (int)luaL_optinteger(L, 5, 100);
	m.volume = m.volume < 0 ? 0 : m.volume > 200 ? 200 : m.volume;

	long start = 0, end = 0;
	read_loop_tags(m.full, &start, &end);
	if (!lua_isnoneornil(L, 3))
		start = (long)(luaL_checknumber(L, 3) * m.rate + 0.5);
	if (!lua_isnoneornil(L, 4))
		end = (long)(luaL_checknumber(L, 4) * m.rate + 0.5);
	if (end <= 0 || end > m.frames)
		end = 0;
	if (start < 0 || start >= (end > 0 ? end : m.frames))
		start = 0;
	m.loop_start = (int)start;
	m.loop_end = (int)end;

	if (lua_toboolean(L, 7)) {
		g_once = m;
		g_once_no = no;
		push_file(L, &g_once);
		return 1;
	}
	if (!g_rep[no])
		g_rep[no] = xmalloc(sizeof *g_rep[no]);
	*g_rep[no] = m;
	push_file(L, g_rep[no]);
	return 1;
}

/* c.clear(no) → whether a replacement was there. */
static int c_clear(lua_State *L)
{
	int no = whole_track(L, 1);
	int had = g_rep[no] != NULL;
	free(g_rep[no]);
	g_rep[no] = NULL;
	lua_pushboolean(L, had);
	return 1;
}

static int c_clear_once(lua_State *L)
{
	(void)L;
	g_once_no = -1;
	return 0;
}

/* c.get(no) → the replacement's info, or nil. */
static int c_get(lua_State *L)
{
	int no = whole_track(L, 1);
	if (!g_rep[no])
		return 0;
	push_file(L, g_rep[no]);
	return 1;
}

/* c.exists(no) → whether the game has the track; nil before the game opened its archives
 * (a mod's init.lua runs before). */
static int c_exists(lua_State *L)
{
	lua_Integer no = luaL_checkinteger(L, 1);
	if (!asset_type_ready(ASSET_BGM))
		return 0;
	lua_pushboolean(L, no > 0 && no < 100000 && asset_exists(ASSET_BGM, (int)no));
	return 1;
}

/* c.game_seconds(no) → the game track's length in seconds, or nil. Opens the track. */
static int c_game_seconds(lua_State *L)
{
	lua_Integer no = luaL_checkinteger(L, 1);
	if (no <= 0 || no >= 100000 || !asset_exists(ASSET_BGM, (int)no))
		return 0;
	struct channel *ch = channel_open(ASSET_BGM, (int)no);
	if (!ch)
		return 0;
	lua_pushnumber(L, channel_get_time_length(ch) / 1000.0);
	channel_close(ch);
	return 1;
}

/* c.playing(no) → file ("" for the game's), mod, position and length in seconds, of the most
 * recently prepared music channel with that number that plays; nil when none plays. */
static int c_playing(lua_State *L)
{
	lua_Integer no = luaL_checkinteger(L, 1);
	int best = -1;
	for (int i = 0; i < PREPARED_MAX; i++) {
		if (g_prep[i].seq && g_prep[i].no == no && bgm_is_playing(g_prep[i].id)
		    && (best < 0 || g_prep[i].seq > g_prep[best].seq))
			best = i;
	}
	if (best < 0)
		return 0;
	lua_pushstring(L, g_prep[best].rel);
	lua_pushstring(L, g_prep[best].mod);
	lua_pushnumber(L, bgm_get_pos(g_prep[best].id) / 1000.0);
	lua_pushnumber(L, bgm_get_time_length(g_prep[best].id) / 1000.0);
	return 4;
}

static void remember(int id, int no, const struct music_file *m)
{
	int slot = -1, oldest = 0;
	for (int i = 0; i < PREPARED_MAX; i++) {
		if (g_prep[i].seq && g_prep[i].id == id) {
			slot = i;
			break;
		}
		if (!g_prep[i].seq) {
			if (slot < 0)
				slot = i;
		} else if (g_prep[i].seq < g_prep[oldest].seq) {
			oldest = i;
		}
	}
	if (slot < 0)
		slot = oldest;
	g_prep[slot].id = id;
	g_prep[slot].no = no;
	snprintf(g_prep[slot].rel, sizeof g_prep[slot].rel, "%s", m ? m->rel : "");
	snprintf(g_prep[slot].mod, sizeof g_prep[slot].mod, "%s", m ? m->mod : "");
	g_prep[slot].seq = ++g_seq;
}

struct channel *rq_music_open(int id, int no)
{
	struct music_file *m = NULL;
	struct music_file once;

	if (no == g_once_no) {
		once = g_once;
		m = &once;
		g_once_no = -1;
	} else if (no > 0 && no < MUSIC_MAX) {
		m = g_rep[no];
	}
	if (!m) {
		remember(id, no, NULL);
		return NULL;
	}
	struct channel *ch = channel_open_music_file(m->full, no, m->loop_start, m->loop_end, m->volume);
	if (!ch) {
		rq_log("music: %s's '%s' did not open for track %d; the game's plays", m->mod, m->rel, no);
		remember(id, no, NULL);
		return NULL;
	}
	remember(id, no, m);
	return ch;
}

extern const char rq_music_lua[];
extern const unsigned rq_music_lua_len;

/* Runs lua/music.lua with rq and the C half. After rq_battle_lua_open (rq.ain.hook in place). */
void rq_music_open_lua(lua_State *L, int rq_index)
{
	static const luaL_Reg c[] = {
		{ "set", c_set },
		{ "resolve", c_resolve },
		{ "clear", c_clear },
		{ "clear_once", c_clear_once },
		{ "get", c_get },
		{ "exists", c_exists },
		{ "game_seconds", c_game_seconds },
		{ "playing", c_playing },
		{ NULL, NULL }
	};
	if (luaL_loadbuffer(L, rq_music_lua, rq_music_lua_len, "=music.lua") != LUA_OK) {
		rq_log("music.lua: %s", lua_tostring(L, -1));
		lua_pop(L, 1);
		return;
	}
	lua_pushvalue(L, rq_index);
	luaL_newlib(L, c);
	rq_private_push(L); /* 2.36.0: rq.battle.start's music */
	if (lua_pcall(L, 3, 0, 0) != LUA_OK) {
		rq_log("music.lua: %s", lua_tostring(L, -1));
		lua_pop(L, 1);
	}
}

#endif /* RQ_MODLOADER */
