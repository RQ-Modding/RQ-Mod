/* Copyright (C) 2026 Rance Quest modding fork
 *
 * rq.sound: sounds a mod plays on the game's sound effect mixer, from three
 * sources. A game sound number, a file in the mod folder (anything libsndfile
 * reads: WAV, OGG, FLAC, Opus, MP3), or a DefineSound inside a game Flash file
 * (Berserk's sound has no number, it lives in Data/Flash/Battle/狂戦士発動).
 *
 * File and Flash sounds are kept in memory after the first play, so a sound
 * played on every hit reads the disk once. Each play hands the audio code its
 * own copy, since a channel frees its data when it closes.
 *
 * Channels come from the game's wav pool. rq_sound_tick releases the ones that
 * ended, as audio_update does for the engine's own anonymous channels.
 */

#ifdef RQ_MODLOADER

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdint.h>

#include "lua.h"
#include "lauxlib.h"

#include "system4.h"
#include "system4/archive.h"
#include "system4/little_endian.h"

#include "asset_manager.h"
#include "audio.h"
#include "swf.h"

#include "modloader.h"
#include "modloader_priv.h"

#define SOUND_CHANNELS 32
#define SOUND_CACHE 128
#define SOUND_FILE_MAX (64u * 1024u * 1024u)

struct cached {
	char key[RQ_PATH_MAX + 8];
	uint8_t *wav;
	size_t size;
};

static struct cached g_cache[SOUND_CACHE];
static int g_ncache;

/* Wav pool ids this module prepared, -1 when free. A handle is the slot plus
 * 1, with a count in the high bits so an old handle cannot stop a new sound. */
static int g_chan[SOUND_CHANNELS];
static int g_gen[SOUND_CHANNELS];
static int g_ready;

static void sound_ready(void)
{
	if (g_ready)
		return;
	audio_init();
	for (int i = 0; i < SOUND_CHANNELS; i++)
		g_chan[i] = -1;
	g_ready = 1;
}

static void mem_free_data(struct archive_data *data)
{
	free(data->data);
	free(data);
}

static struct archive_ops mem_ops = {
	.free_data = mem_free_data
};

static struct archive mem_archive = {
	.mmapped = false,
	.ops = &mem_ops
};

static struct cached *cache_get(const char *key)
{
	for (int i = 0; i < g_ncache; i++) {
		if (!strcmp(g_cache[i].key, key))
			return &g_cache[i];
	}
	return NULL;
}

/* A Flash file's sounds read by play_flash are read again once a mod changed what plays
 * under its full name: add_flash, add_gif, remove_flash or remove_gif (2.63.0). */
void rq_sound_flash_changed(const char *name)
{
	int i, n = 0;

	for (i = 0; i < g_ncache; i++) {
		const char *k = g_cache[i].key, *rest = NULL;
		if (!strncmp(k, "flash:", 6))
			rest = strchr(k + 6, ':');
		if (rest && !strcasecmp(rest + 1, name)) {
			free(g_cache[i].wav);
			continue;
		}
		g_cache[n++] = g_cache[i];
	}
	g_ncache = n;
}

/* Keeps buf (takes ownership). When the cache is full the sound still plays,
 * it is only read again next time. */
static struct cached *cache_put(const char *key, uint8_t *buf, size_t size, struct cached *tmp)
{
	struct cached *c = g_ncache < SOUND_CACHE ? &g_cache[g_ncache++] : tmp;
	snprintf(c->key, sizeof c->key, "%s", key);
	c->wav = buf;
	c->size = size;
	return c;
}

/* A WAV file around the PCM of a DefineSound, as parts/flash.c builds one. */
static uint8_t *wav_of_define_sound(struct swf_tag_define_sound *s, size_t *size)
{
	static const uint8_t header[] = {
		'R' , 'I' , 'F' , 'F' , 0x00, 0x00, 0x00, 0x00,
		'W' , 'A' , 'V' , 'E' , 'f' , 'm' , 't' , ' ' ,
		0x10, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 'd' , 'a' , 't' , 'a' ,
		0x00, 0x00, 0x00, 0x00
	};
	int bytes = s->spec.sample_size / 8;
	size_t n = sizeof header + s->data_len;
	uint8_t *buf = xmalloc(n);

	memcpy(buf, header, sizeof header);
	LittleEndian_putDW(buf, 4, (uint32_t)(n - 8));
	LittleEndian_putW(buf, 22, s->spec.channels);
	LittleEndian_putDW(buf, 24, s->spec.rate);
	LittleEndian_putDW(buf, 28, s->spec.rate * bytes * s->spec.channels);
	LittleEndian_putW(buf, 32, bytes * s->spec.channels);
	LittleEndian_putW(buf, 34, s->spec.sample_size);
	LittleEndian_putDW(buf, 40, s->data_len);
	memcpy(buf + sizeof header, s->data, s->data_len);
	*size = n;
	return buf;
}

/* The Flash file a key names: a mod GIF under that key first, then the game's. */
static struct swf *flash_open(const char *key)
{
	char name[256];
	uint8_t *data;
	size_t size;
	struct archive_data *dfile;
	struct swf *swf;

	rq_anim_full_name(key, name, sizeof name);
	if (rq_anim_find(name, &data, &size))
		return swf_load(data, size);
	dfile = asset_get_by_name(ASSET_FLASH, name, NULL);
	if (!dfile)
		return NULL;
	swf = aff_load(dfile->data, dfile->size);
	archive_free_data(dfile);
	return swf;
}

/* The n-th DefineSound (from 1), or the count of them when n is 0. */
static struct swf_tag_define_sound *flash_sound(struct swf *swf, int n, int *count)
{
	int i = 0;
	for (struct swf_tag *t = swf->tags; t; t = t->next) {
		if (t->type != TAG_DEFINE_SOUND)
			continue;
		if (++i == n)
			return (struct swf_tag_define_sound *)t;
	}
	if (count)
		*count = i;
	return NULL;
}

static int sound_volume(lua_State *L, int opts)
{
	int v = 100;
	if (lua_istable(L, opts)) {
		lua_getfield(L, opts, "volume");
		v = (int)luaL_optinteger(L, -1, 100);
		lua_pop(L, 1);
	}
	return v < 0 ? 0 : v > 100 ? 100 : v;
}

static int sound_loop(lua_State *L, int opts)
{
	int n = 1;
	if (lua_istable(L, opts)) {
		lua_getfield(L, opts, "loop");
		n = (int)luaL_optinteger(L, -1, 1);
		lua_pop(L, 1);
	}
	return n < 0 ? 0 : n;
}

static int free_slot(void)
{
	for (int i = 0; i < SOUND_CHANNELS; i++) {
		if (g_chan[i] < 0)
			return i;
	}
	return -1;
}

/* Starts a prepared channel and pushes its handle, or nil. */
static int start(lua_State *L, int slot, int ch, int opts)
{
	wav_set_loop_count(ch, sound_loop(L, opts));
	wav_fade(ch, 0, sound_volume(L, opts), false);
	if (!wav_play(ch)) {
		wav_unprepare(ch);
		return 0;
	}
	g_chan[slot] = ch;
	g_gen[slot] = (g_gen[slot] + 1) & 0xffff;
	lua_pushinteger(L, (lua_Integer)g_gen[slot] * SOUND_CHANNELS + slot + 1);
	return 1;
}

static int play_bytes(lua_State *L, struct cached *c, int opts)
{
	int slot = free_slot();
	int ch;
	struct archive_data *dfile;

	if (slot < 0)
		return luaL_error(L, "too many mod sounds at once (max %d)", SOUND_CHANNELS);
	dfile = xcalloc(1, sizeof *dfile);
	dfile->data = xmalloc(c->size);
	memcpy(dfile->data, c->wav, c->size);
	dfile->size = c->size;
	dfile->no = -1;
	dfile->archive = &mem_archive;
	ch = wav_get_unused_channel();
	if (!wav_prepare_from_archive_data(ch, AUDIO_NO_METADATA, dfile))
		return 0;
	return start(L, slot, ch, opts);
}

static int play_number(lua_State *L, int no, int opts)
{
	int slot = free_slot();
	int ch;

	if (slot < 0)
		return luaL_error(L, "too many mod sounds at once (max %d)", SOUND_CHANNELS);
	if (no <= 0 || !wav_exists(no))
		return 0;
	ch = wav_get_unused_channel();
	if (!wav_prepare(ch, no))
		return 0;
	return start(L, slot, ch, opts);
}

static int play_file(lua_State *L, const char *rel, int opts)
{
	char full[RQ_PATH_MAX], key[RQ_PATH_MAX + 8];
	struct cached tmp, *c;
	uint8_t *data;
	long len;
	FILE *f;
	int r;

	if (!rq_sandbox_resolve(L, rel, full, (int)sizeof full))
		return luaL_error(L, "sound path '%s' is not inside the mod folder", rel);
	snprintf(key, sizeof key, "file:%s", full);
	if ((c = cache_get(key)))
		return play_bytes(L, c, opts);
	f = rq_fopen(full, "rb");
	if (!f)
		return luaL_error(L, "sound '%s' not found in the mod folder", rel);
	fseek(f, 0, SEEK_END);
	len = ftell(f);
	fseek(f, 0, SEEK_SET);
	if (len <= 0 || (unsigned long)len > SOUND_FILE_MAX) {
		fclose(f);
		return luaL_error(L, "sound '%s' is empty or over 64 MB", rel);
	}
	data = xmalloc((size_t)len);
	if (fread(data, 1, (size_t)len, f) != (size_t)len) {
		free(data);
		fclose(f);
		return luaL_error(L, "could not read sound '%s'", rel);
	}
	fclose(f);
	c = cache_put(key, data, (size_t)len, &tmp);
	r = play_bytes(L, c, opts);
	if (c == &tmp)
		free(tmp.wav);
	if (!r)
		rq_log("sound: '%s' is not a format the game can read", rel);
	return r;
}

/* rq.sound.play(src, opts): src is a game sound number or a path in the mod
 * folder. Returns a handle, or nil when the sound did not start. */
int rq_lua_sound_play(lua_State *L)
{
	sound_ready();
	if (lua_type(L, 1) == LUA_TNUMBER)
		return play_number(L, (int)luaL_checkinteger(L, 1), 2);
	return play_file(L, luaL_checkstring(L, 1), 2);
}

/* rq.sound.play_flash(key, opts): opts.index picks the DefineSound (from 1). */
int rq_lua_sound_play_flash(lua_State *L)
{
	const char *key = luaL_checkstring(L, 1);
	char name[256], ckey[300];
	int index = 1, r;
	struct cached tmp, *c;
	struct swf *swf;
	struct swf_tag_define_sound *s;
	uint8_t *wav;
	size_t size;

	sound_ready();
	if (lua_istable(L, 2)) {
		lua_getfield(L, 2, "index");
		index = (int)luaL_optinteger(L, -1, 1);
		lua_pop(L, 1);
	}
	if (index < 1)
		return luaL_error(L, "play_flash index starts at 1");
	/* By the full name, as the registry knows it: a bare name and its path are one file. */
	rq_anim_full_name(key, name, sizeof name);
	snprintf(ckey, sizeof ckey, "flash:%d:%s", index, name);
	if ((c = cache_get(ckey)))
		return play_bytes(L, c, 2);
	if (!(swf = flash_open(key)))
		return luaL_error(L, "Flash file '%s' not found", key);
	s = flash_sound(swf, index, NULL);
	if (!s) {
		swf_free(swf);
		return luaL_error(L, "Flash file '%s' has no sound %d", key, index);
	}
	if ((s->format != SWF_SOUND_UNCOMPRESSED_LE && s->format != 0)
			|| (s->spec.sample_size != 8 && s->spec.sample_size != 16)) {
		swf_free(swf);
		return luaL_error(L, "sound %d of '%s' is compressed (format %d), only PCM plays",
				index, key, s->format);
	}
	wav = wav_of_define_sound(s, &size);
	swf_free(swf);
	c = cache_put(ckey, wav, size, &tmp);
	r = play_bytes(L, c, 2);
	if (c == &tmp)
		free(tmp.wav);
	return r;
}

/* rq.sound.flash_info(key): the sounds in a Flash file, or nil if no file. */
int rq_lua_sound_flash_info(lua_State *L)
{
	const char *key = luaL_checkstring(L, 1);
	struct swf *swf = flash_open(key);
	int i = 0;

	if (!swf)
		return 0;
	lua_newtable(L);
	for (struct swf_tag *t = swf->tags; t; t = t->next) {
		struct swf_tag_define_sound *s = (struct swf_tag_define_sound *)t;
		int bytes;
		if (t->type != TAG_DEFINE_SOUND)
			continue;
		bytes = (s->spec.sample_size / 8) * s->spec.channels;
		lua_newtable(L);
		lua_pushinteger(L, s->spec.rate);
		lua_setfield(L, -2, "rate");
		lua_pushinteger(L, s->spec.channels);
		lua_setfield(L, -2, "channels");
		lua_pushinteger(L, s->spec.sample_size);
		lua_setfield(L, -2, "bits");
		lua_pushinteger(L, s->spec.rate && bytes ? (lua_Integer)s->data_len * 1000 / bytes / s->spec.rate : 0);
		lua_setfield(L, -2, "ms");
		lua_pushboolean(L, s->format == SWF_SOUND_UNCOMPRESSED_LE || s->format == 0);
		lua_setfield(L, -2, "playable");
		lua_rawseti(L, -2, ++i);
	}
	swf_free(swf);
	return 1;
}

/* The wav pool id of a live handle, or -1. */
static int handle_channel(lua_Integer h)
{
	int slot;
	if (h <= 0)
		return -1;
	slot = (int)((h - 1) % SOUND_CHANNELS);
	if (g_chan[slot] < 0 || g_gen[slot] != (int)((h - 1) / SOUND_CHANNELS))
		return -1;
	return g_chan[slot];
}

/* rq.sound.stop(handle, fade_ms) */
int rq_lua_sound_stop(lua_State *L)
{
	lua_Integer h;
	int ch, ms;
	sound_ready();
	h = luaL_checkinteger(L, 1);
	ch = handle_channel(h);
	ms = (int)luaL_optinteger(L, 2, 0);
	if (ch >= 0 && ms > 0) {
		wav_fade(ch, ms, 0, true);
	} else if (ch >= 0) {
		/* Free the slot now, so a play right after a stop finds it. */
		wav_unprepare(ch);
		g_chan[(h - 1) % SOUND_CHANNELS] = -1;
	}
	lua_pushboolean(L, ch >= 0);
	return 1;
}

/* rq.sound.playing(handle) */
int rq_lua_sound_playing(lua_State *L)
{
	int ch;
	sound_ready();
	ch = handle_channel(luaL_checkinteger(L, 1));
	lua_pushboolean(L, ch >= 0 && wav_is_playing(ch));
	return 1;
}

/* rq.sound.set_volume(handle, volume, fade_ms) */
int rq_lua_sound_set_volume(lua_State *L)
{
	int ch, v, ms;
	sound_ready();
	ch = handle_channel(luaL_checkinteger(L, 1));
	v = (int)luaL_checkinteger(L, 2);
	ms = (int)luaL_optinteger(L, 3, 0);
	if (ch >= 0)
		wav_fade(ch, ms > 0 ? ms : 0, v < 0 ? 0 : v > 100 ? 100 : v, false);
	lua_pushboolean(L, ch >= 0);
	return 1;
}

void rq_sound_tick(void)
{
	if (!g_ready)
		return;
	for (int i = 0; i < SOUND_CHANNELS; i++) {
		if (g_chan[i] >= 0 && !wav_is_playing(g_chan[i])) {
			wav_unprepare(g_chan[i]);
			g_chan[i] = -1;
		}
	}
}

#endif
