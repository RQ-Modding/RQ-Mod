/* Copyright (C) 2019 Nunuhara Cabbage <nunuhara@haniwa.technology>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://gnu.org/licenses/>.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <dirent.h>
#include <limits.h>
#include <assert.h>

#include "system4.h"
#include "system4/ald.h"
#include "system4/afa.h"
#include "system4/cg.h"
#include "system4/file.h"
#include "system4/hashtable.h"
#include "system4/string.h"
#include "system4/utfsjis.h"

#include "xsystem4.h"
#include "modloader.h"
#include "asset_manager.h"
#include "gfx/font.h"

enum archive_type {
	AR_TYPE_ALD,
	AR_TYPE_AFA,
	AR_TYPE_MAX
};

const char *asset_strtype(enum asset_type type)
{
	switch (type) {
	case ASSET_BGM:   return "BGM";
	case ASSET_SOUND: return "Sound";
	case ASSET_VOICE: return "Voice";
	case ASSET_CG:    return "CG";
	case ASSET_FLAT:  return "Flat";
	case ASSET_PACT:  return "Pact";
	case ASSET_DATA:  return "Data";
	case ASSET_FLASH: return "Flash";
	}
	return "Invalid";
}

#define MAX_ARCHIVES 8

struct asset_manager {
	bool (*load_archive)(struct asset_manager *manager, const char *name);
	bool (*exists_by_id)(struct asset_manager *manager, int id);
	bool (*exists_by_name)(struct asset_manager *manager, const char *name, int *id_out);
	struct archive_data *(*get_by_id)(struct asset_manager *manager, int id);
	struct archive_data *(*get_by_name)(struct asset_manager *manager, const char *name, int *id_out);
};

// XXX: also used for AFAv1 in Daiteikoku, Shaman's Sanctuary
struct asset_manager_ald {
	struct asset_manager manager;
	struct archive *archive;
};

struct asset_manager_afa {
	struct asset_manager manager;
	struct afa_archive *archives[MAX_ARCHIVES];
};

static struct asset_manager *assets[ASSET_TYPE_MAX] = {0};

bool asset_manager_load_archive(enum asset_type type, const char *archive_name)
{
	if (!assets[type])
		return false;
	if (!assets[type]->load_archive)
		ERROR("load_archive not supported on this archive type");
	char *path = gamedir_path(archive_name);
	bool r = assets[type]->load_archive(assets[type], path);
	free(path);
	return r;
}

/* CG names that are plain numbers ("12301") address a CG by number. */
static int digits_id(const char *name)
{
	long v = 0;
	if (!name || !*name)
		return 0;
	for (; *name; name++) {
		if (*name < '0' || *name > '9')
			return 0;
		v = v * 10 + (*name - '0');
		if (v > 2000000000L)
			return 0;
	}
	return (int)v;
}

/* rq.music: false until asset_manager_init opened this type's archives (mods load before). */
bool asset_type_ready(enum asset_type type)
{
	return assets[type] != NULL;
}

bool asset_exists(enum asset_type type, int id)
{
	if (type == ASSET_CG && rq_image_has(id))
		return true;
	if (!assets[type])
		return false;
	return assets[type]->exists_by_id(assets[type], id);
}

bool asset_exists_by_name(enum asset_type type, const char *name, int *id_out)
{
	if (type == ASSET_CG) {
		int id = digits_id(name);
		if (id > 0 && rq_image_has(id)) {
			if (id_out)
				*id_out = id;
			return true;
		}
	}
	if (!assets[type])
		return false;
	if (!assets[type]->exists_by_name)
		ERROR("exists_by_name not supported on this archive type");
	return assets[type]->exists_by_name(assets[type], name, id_out);
}

struct archive_data *asset_get(enum asset_type type, int id)
{
	if (!assets[type])
		return NULL;
	return assets[type]->get_by_id(assets[type], id);
}

struct archive_data *asset_get_by_name(enum asset_type type, const char *name, int *id_out)
{
	if (!assets[type])
		return NULL;
	if (!assets[type]->get_by_name)
		ERROR("get_by_name not supported on this archive type");
	return assets[type]->get_by_name(assets[type], name, id_out);
}

/*
 * Decoded CG cache.
 *
 * UI screens set the same small CGs (faces, icons, frames) on hundreds of
 * sprites again every time they are rebuilt, and each call used to read the
 * archive and decode the image again. Small CGs are kept here, decoded, in an
 * LRU capped by bytes. Callers get their own copy since they free it.
 * Set XSYS4_NO_CGCACHE to turn it off.
 */
#define CG_CACHE_BUCKETS 1024
#define CG_CACHE_MAX_BYTES (64u * 1024 * 1024)
#define CG_CACHE_MAX_PIXELS (256 * 256)

struct cg_cache_entry {
	int id;
	struct cg cg;
	size_t bytes;
	struct cg_cache_entry *hnext;
	struct cg_cache_entry *prev, *next; // LRU list, head = most recent
};

static struct cg_cache_entry *cg_cache_hash[CG_CACHE_BUCKETS];
static struct cg_cache_entry *cg_cache_head, *cg_cache_tail;
static size_t cg_cache_bytes;
static int cg_cache_on = -1;

static void cg_cache_unlink(struct cg_cache_entry *e)
{
	if (e->prev)
		e->prev->next = e->next;
	else
		cg_cache_head = e->next;
	if (e->next)
		e->next->prev = e->prev;
	else
		cg_cache_tail = e->prev;
	e->prev = e->next = NULL;
}

static void cg_cache_push_front(struct cg_cache_entry *e)
{
	e->prev = NULL;
	e->next = cg_cache_head;
	if (cg_cache_head)
		cg_cache_head->prev = e;
	cg_cache_head = e;
	if (!cg_cache_tail)
		cg_cache_tail = e;
}

static void cg_cache_evict(struct cg_cache_entry *e)
{
	struct cg_cache_entry **p = &cg_cache_hash[(unsigned)e->id % CG_CACHE_BUCKETS];
	while (*p && *p != e)
		p = &(*p)->hnext;
	if (*p)
		*p = e->hnext;
	cg_cache_unlink(e);
	cg_cache_bytes -= e->bytes;
	free(e->cg.pixels);
	free(e);
}

static struct cg *cg_cache_get(int id)
{
	for (struct cg_cache_entry *e = cg_cache_hash[(unsigned)id % CG_CACHE_BUCKETS]; e; e = e->hnext) {
		if (e->id != id)
			continue;
		struct cg *cg = xcalloc(1, sizeof(struct cg));
		*cg = e->cg;
		cg->pixels = xmalloc(e->bytes);
		memcpy(cg->pixels, e->cg.pixels, e->bytes);
		cg_cache_unlink(e);
		cg_cache_push_front(e);
		return cg;
	}
	return NULL;
}

static void cg_cache_put(int id, struct cg *cg)
{
	size_t bytes = (size_t)cg->metrics.w * cg->metrics.h * 4;
	if (!cg->pixels || cg->metrics.w <= 0 || cg->metrics.h <= 0
	    || cg->metrics.w * cg->metrics.h > CG_CACHE_MAX_PIXELS)
		return;
	while (cg_cache_tail && cg_cache_bytes + bytes > CG_CACHE_MAX_BYTES)
		cg_cache_evict(cg_cache_tail);
	struct cg_cache_entry *e = xcalloc(1, sizeof(struct cg_cache_entry));
	e->id = id;
	e->cg = *cg;
	e->bytes = bytes;
	e->cg.pixels = xmalloc(bytes);
	memcpy(e->cg.pixels, cg->pixels, bytes);
	unsigned b = (unsigned)id % CG_CACHE_BUCKETS;
	e->hnext = cg_cache_hash[b];
	cg_cache_hash[b] = e;
	cg_cache_push_front(e);
	cg_cache_bytes += bytes;
}

void asset_cg_cache_forget(int id)
{
	for (struct cg_cache_entry *e = cg_cache_hash[(unsigned)id % CG_CACHE_BUCKETS]; e; e = e->hnext) {
		if (e->id == id) {
			cg_cache_evict(e);
			return;
		}
	}
}

/* Size of the game's own CG, ignoring any mod image on that number. */
bool asset_cg_get_metrics_original(int id, struct cg_metrics *metrics)
{
	struct archive_data *data = asset_get(ASSET_CG, id);
	if (!data)
		return false;
	bool r = cg_get_metrics_data(data, metrics);
	archive_free_data(data);
	return r;
}

struct cg *asset_cg_load(int id)
{
	if (id > 0 && rq_image_has(id)) {
		struct cg *mod = rq_image_load(id);
		if (mod)
			return mod;
	}
	if (cg_cache_on < 0)
		cg_cache_on = getenv("XSYS4_NO_CGCACHE") == NULL;
	if (cg_cache_on && id > 0) {
		struct cg *hit = cg_cache_get(id);
		if (hit)
			return hit;
	}
	struct archive_data *data = asset_get(ASSET_CG, id);
	if (!data)
		return NULL;
	struct cg *cg = cg_load_data(data);
	archive_free_data(data);
	if (cg_cache_on && id > 0 && cg)
		cg_cache_put(id, cg);
	return cg;
}

struct cg *asset_cg_load_by_name(const char *name, int *id_out)
{
	int mod_id = digits_id(name);
	if (mod_id > 0 && rq_image_has(mod_id)) {
		struct cg *mod = rq_image_load(mod_id);
		if (mod) {
			if (id_out)
				*id_out = mod_id;
			return mod;
		}
	}
	struct archive_data *data = asset_get_by_name(ASSET_CG, name, id_out);
	if (!data)
		return NULL;
	struct cg *cg = cg_load_data(data);
	archive_free_data(data);
	return cg;
}

bool asset_cg_get_metrics(int id, struct cg_metrics *metrics)
{
	if (rq_image_metrics(id, metrics))
		return true;
	struct archive_data *data = asset_get(ASSET_CG, id);
	if (!data)
		return false;
	bool r = cg_get_metrics_data(data, metrics);
	archive_free_data(data);
	return r;
}

bool asset_cg_get_metrics_by_name(const char *name, struct cg_metrics *metrics)
{
	int mod_id = digits_id(name);
	if (mod_id > 0 && rq_image_metrics(mod_id, metrics))
		return true;
	struct archive_data *data = asset_get_by_name(ASSET_CG, name, NULL);
	if (!data)
		return false;
	bool r = cg_get_metrics_data(data, metrics);
	archive_free_data(data);
	return r;
}

static bool ald_exists_by_id(struct asset_manager *_manager, int id)
{
	struct asset_manager_ald *manager = (struct asset_manager_ald*)_manager;
	return archive_exists(manager->archive, id - 1);
}

static struct archive_data *ald_get_by_id(struct asset_manager *_manager, int id)
{
	struct asset_manager_ald *manager = (struct asset_manager_ald*)_manager;
	return archive_get(manager->archive, id - 1);
}

static bool afa_load_archive(struct asset_manager *_manager, const char *name)
{
	struct asset_manager_afa *manager = (struct asset_manager_afa*)_manager;
	if (manager->archives[MAX_ARCHIVES-1])
		ERROR("Archive limit exceeded");

	char path[PATH_MAX];
	snprintf(path, PATH_MAX, "%s.afa", name);

	int error;
	struct afa_archive *ar = afa_open(path, MMAP_IF_64BIT, &error);
	if (!ar) {
		snprintf(path, PATH_MAX, "%s.AFA", name);
		ar = afa_open(path, MMAP_IF_64BIT, &error);
		if (!ar) {
			WARNING("Failed to open archive: %s", display_utf0(path));
			return false;
		}
	}

	for (int i = MAX_ARCHIVES - 1; i > 0; i--) {
		manager->archives[i] = manager->archives[i-1];
	}
	manager->archives[0] = ar;
	return true;
}

static bool afa_exists_by_id(struct asset_manager *_manager, int id)
{
	struct asset_manager_afa *manager = (struct asset_manager_afa*)_manager;
	for (int i = 0; i < MAX_ARCHIVES && manager->archives[i]; i++) {
		if (archive_exists(&manager->archives[i]->ar, id - 1))
			return true;
	}
	return false;
}

static struct archive_data *afa_get_by_id(struct asset_manager *_manager, int id)
{
	struct asset_manager_afa *manager = (struct asset_manager_afa*)_manager;
	for (int i = 0; i < MAX_ARCHIVES && manager->archives[i]; i++) {
		struct archive_data *data = archive_get(&manager->archives[i]->ar, id - 1);
		if (data)
			return data;
	}
	return NULL;
}

static struct archive_data *afa_get_by_name(struct asset_manager *_manager, const char *name,
		int *id_out)
{
	struct asset_manager_afa *manager = (struct asset_manager_afa*)_manager;

	for (unsigned i = 0; i < MAX_ARCHIVES && manager->archives[i]; i++) {
		struct archive_data *data;
		data = archive_get_by_basename(&manager->archives[i]->ar, name);
		if (data) {
			if (id_out)
				*id_out = data->no + 1;
			return data;
		}
	}
	return NULL;
}

static bool afa_exists_by_name(struct asset_manager *_manager, const char *name, int *id_out)
{
	struct asset_manager_afa *manager = (struct asset_manager_afa*)_manager;

	for (unsigned i = 0; i < MAX_ARCHIVES && manager->archives[i]; i++) {
		if (archive_exists_by_basename(&manager->archives[i]->ar, name, id_out)) {
			if (id_out)
				*id_out += 1;
			return true;
		}
	}
	return false;
}

static void _ald_init(enum asset_type type, struct archive *ar)
{
	struct asset_manager_ald *manager = xcalloc(1, sizeof(struct asset_manager_ald));
	manager->manager.exists_by_id = ald_exists_by_id;
	manager->manager.get_by_id = ald_get_by_id;
	manager->manager.load_archive = NULL;
	manager->manager.exists_by_name = NULL;
	manager->manager.get_by_name = NULL;
	manager->archive = ar;
	assets[type] = &manager->manager;
}

static void ald_init(enum asset_type type, char **files, int count)
{
	if (count < 1)
		return;
	if (assets[type])
		WARNING("Multiple asset archives for type %s", asset_strtype(type));

	int error;
	struct archive *ar = ald_open(files, count, MMAP_IF_64BIT, &error);
	if (!ar)
		ERROR("Failed to open ALD file: %s", archive_strerror(error));

	_ald_init(type, ar);

	for (int i = 0; i < count; i++) {
		free(files[i]);
	}
}

static void afa_init(enum asset_type type, char *file)
{
	if (!file)
		return;
	if (assets[type])
		WARNING("Multiple asset archives for type %s", asset_strtype(type));

	int error;
	struct afa_archive *ar = afa_open(file, MMAP_IF_64BIT, &error);
	if (!ar)
		ERROR("Failed to open AFA file: %s", archive_strerror(error));

	struct asset_manager_afa *manager = xcalloc(1, sizeof(struct asset_manager_afa));
	manager->manager.load_archive = afa_load_archive;
	manager->manager.exists_by_id = afa_exists_by_id;
	manager->manager.get_by_id = afa_get_by_id;
	manager->manager.exists_by_name = afa_exists_by_name;
	manager->manager.get_by_name = afa_get_by_name;
	manager->archives[0] = ar;
	assets[type] = &manager->manager;

	free(file);
}

static char *get_base_name(const char *ain_filename)
{
	char *path = sjis2utf(ain_filename, 0);
	char *dot = strrchr(path, '.');
	if (dot)
		*dot = '\0';
	return path;
}

void asset_manager_init(void)
{
	char *ald_filenames[ASSET_TYPE_MAX][ALD_FILEMAX] = {0};
	int ald_count[ASSET_TYPE_MAX] = {0};
	char *afa_filenames[ASSET_TYPE_MAX] = {0};

	UDIR *dir;
	char *d_name;

	if (!(dir = opendir_utf8(config.game_dir))) {
		ERROR("Failed to open directory: %s", display_utf0(config.game_dir));
	}

	char *base = get_base_name(config.ain_filename);
	size_t base_len = strlen(base);

	// get ALD filenames
	while ((d_name = readdir_utf8(dir))) {
		// archive filename must begin with ain filename
		if (strncasecmp(base, d_name, base_len))
			goto loop_next;
		const char *ext = file_extension(d_name);
		if (!ext)
			goto loop_next;
		if (!strcasecmp(ext, "ald")) {
			int dno = toupper(d_name[base_len+1]) - 'A';
			if (dno < 0 || dno >= ALD_FILEMAX) {
				WARNING("Invalid ALD index: %s", display_utf0(d_name));
				goto loop_next;
			}

			switch (d_name[base_len]) {
			case 'b':
			case 'B':
				ald_filenames[ASSET_BGM][dno] = path_join(config.game_dir, d_name);
				ald_count[ASSET_BGM] = max(ald_count[ASSET_BGM], dno+1);
				break;
			case 'g':
			case 'G':
				ald_filenames[ASSET_CG][dno] = path_join(config.game_dir, d_name);
				ald_count[ASSET_CG] = max(ald_count[ASSET_CG], dno+1);
				break;
			case 'w':
			case 'W':
				ald_filenames[ASSET_SOUND][dno] = path_join(config.game_dir, d_name);
				ald_count[ASSET_SOUND] = max(ald_count[ASSET_SOUND], dno+1);
				break;
			case 'd':
			case 'D':
				ald_filenames[ASSET_DATA][dno] = path_join(config.game_dir, d_name);
				ald_count[ASSET_DATA] = max(ald_count[ASSET_DATA], dno+1);
				break;
			default:
				WARNING("Unhandled ALD file: %s", display_utf0(d_name));
				break;
			}
		} else if (!strcasecmp(ext, "bgi")) {
			if (config.bgi_path) {
				WARNING("Multiple bgi files");
				goto loop_next;
			}
			config.bgi_path = path_join(config.game_dir, d_name);
		} else if (!strcasecmp(ext, "wai")) {
			if (config.wai_path) {
				WARNING("Multiple wai files");
				goto loop_next;
			}
			config.wai_path = path_join(config.game_dir, d_name);
		} else if (!strcasecmp(ext, "ex")) {
			if (config.ex_path) {
				WARNING("Multiple ex files");
				goto loop_next;
			}
			config.ex_path = path_join(config.game_dir, d_name);
		} else if (!strcasecmp(ext, "afa")) {
			const char *type = d_name + base_len;
			if (!strcmp(type, "BA.afa")) {
				afa_filenames[ASSET_BGM] = path_join(config.game_dir, d_name);
			} else if (!strcmp(type, "WA.afa") || !strcmp(type, "Sound.afa")) {
				afa_filenames[ASSET_SOUND] = path_join(config.game_dir, d_name);
			} else if (!strcmp(type, "Voice.afa")) {
				afa_filenames[ASSET_VOICE] = path_join(config.game_dir, d_name);
			} else if (!strcmp(type, "CG.afa")) {
				afa_filenames[ASSET_CG] = path_join(config.game_dir, d_name);
			} else if (!strcmp(type, "Flat.afa")) {
				afa_filenames[ASSET_FLAT] = path_join(config.game_dir, d_name);
			} else if (!strcmp(type, "Pact.afa")) {
				afa_filenames[ASSET_PACT] = path_join(config.game_dir, d_name);
			} else if (!strcmp(type, "AFF.afa")) {
				afa_filenames[ASSET_FLASH] = path_join(config.game_dir, d_name);
			}
		} else if (!strcasecmp(ext, "fnl")) {
			if (!config.fnl_path)
				config.fnl_path = path_join(config.game_dir, d_name);
		}
	loop_next:
		free(d_name);
	}
	closedir_utf8(dir);
	free(base);

	// open ALD archives
	ald_init(ASSET_BGM, ald_filenames[ASSET_BGM], ald_count[ASSET_BGM]);
	ald_init(ASSET_SOUND, ald_filenames[ASSET_SOUND], ald_count[ASSET_SOUND]);
	ald_init(ASSET_VOICE, ald_filenames[ASSET_VOICE], ald_count[ASSET_VOICE]);
	ald_init(ASSET_CG, ald_filenames[ASSET_CG], ald_count[ASSET_CG]);
	ald_init(ASSET_FLAT, ald_filenames[ASSET_FLAT], ald_count[ASSET_FLAT]);
	ald_init(ASSET_PACT, ald_filenames[ASSET_PACT], ald_count[ASSET_PACT]);
	ald_init(ASSET_DATA, ald_filenames[ASSET_DATA], ald_count[ASSET_DATA]);
	ald_init(ASSET_FLASH, ald_filenames[ASSET_FLASH], ald_count[ASSET_FLASH]);

	// open AFA archives
	afa_init(ASSET_BGM, afa_filenames[ASSET_BGM]);
	afa_init(ASSET_SOUND, afa_filenames[ASSET_SOUND]);
	afa_init(ASSET_VOICE, afa_filenames[ASSET_VOICE]);
	afa_init(ASSET_CG, afa_filenames[ASSET_CG]);
	afa_init(ASSET_FLAT, afa_filenames[ASSET_FLAT]);
	afa_init(ASSET_PACT, afa_filenames[ASSET_PACT]);
	afa_init(ASSET_DATA, afa_filenames[ASSET_DATA]);
	afa_init(ASSET_FLASH, afa_filenames[ASSET_FLASH]);
}
