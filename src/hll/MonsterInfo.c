/* Copyright (C) 2022 kichikuou <KichikuouChrome@gmail.com>
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

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "system4/aar.h"
#include "system4/string.h"
#include "system4/utfsjis.h"

#include "hll.h"
#include "xsystem4.h"

#ifdef RQ_MODLOADER
#include "modloader.h"
#endif

enum motion_type {
	MOTION_UNSPECIFIED = 0,
	MOTION_LOOP = 2
};

struct motion {
	char *name;
	char *data_name;
	enum motion_type type;
	int frame_begin;
	int frame_end;
	int loop_frame_begin;
	int loop_frame_end;
	char *effect_name;
};

struct monster {
	char *name;
	char *poly_name;
	float scale;
	float pos_x;
	float pos_y;
	float pos_z;
	char *wear_effect_name;
	bool make_shadow;
	bool draw_shadow;
	float shadow_bone_radius;
	unsigned nr_motions;
	struct motion *motions;
};

static unsigned nr_monsters = 0;
static struct monster *monsters = NULL;

#ifdef RQ_MODLOADER
/* rq.monster.models and rq.monster.model (loader 2.40.0): the table read once, and a mod's models,
 * each a copy of a game model at another scale and offset, appended after the file's entries. The
 * loader may read the table before the game asks for it (a mod's model at load); the game's own
 * Load of the same file then keeps it. After each read the mod's models are put back. */
struct rq_derived {
	int base;
	float scale, x, y, z;
	bool textures; /* textures of its own (2.64.0): its folder is marked, so it loads apart */
};
static struct rq_derived *rq_derived = NULL;
static unsigned rq_nderived = 0;
static unsigned rq_file_count = 0; /* the file's entries: the mod's models start here */
static char *rq_loaded_name = NULL;
static void rq_apply_derived(void);
#endif

static void return_string(struct string **out, const char *s)
{
	if (*out)
		free_string(*out);
	*out = s ? cstr_to_string(s) : string_ref(&EMPTY_STRING);
}

static struct motion *get_motion(unsigned monster, unsigned motion)
{
	if (monster >= nr_monsters)
		return NULL;
	if (motion >= monsters[monster].nr_motions)
		return NULL;
	return &monsters[monster].motions[motion];
}

static void init_monster(struct monster *m)
{
	m->scale = 1.0;
	m->make_shadow = true;
	m->draw_shadow = true;
}

static bool parse_monster_info(char *text)
{
	struct monster *current_monster = NULL;
	struct motion *current_motion = NULL;

	for (;;) {
		// Skip whitespaces and comments
		for (;;) {
			while (isspace(*text))
				text++;
			if (text[0] == '/' && text[1] == '/') {
				char *newline = strchr(text, '\n');
				if (newline)
					text = newline + 1;
				else
					text += strlen(text);
			} else {
				break;
			}
		}

#define MATCH(fmt, ...) len = 0, sscanf(text, fmt " %n", ##__VA_ARGS__, &len), text += len, len
#define STRING_BUF_SIZE 64
#define QUOTED_STRING "\"%63[^\"]\""

		int len;
		unsigned n;
		char string_buf[STRING_BUF_SIZE];
		if (!current_monster) {
			// Toplevel
			if (!*text) { // EOF
				return true;
			} else if (MATCH("モンスター [ %u ] = {", &n)) {
#ifdef RQ_MODLOADER
				if (n + 1 > rq_file_count)
					rq_file_count = n + 1;
#endif
				if (n >= nr_monsters) {
					monsters = xrealloc_array(monsters, nr_monsters, n + 1, sizeof(struct monster));
					nr_monsters = n + 1;
				}
				current_monster = &monsters[n];
				init_monster(current_monster);
			} else {
				char *eol = strchr(text, '\n');
				*eol = '\0';
				WARNING("Parse error in toplevel, near: \"%s\"", text);
				return false;
			}
		} else if (!current_motion) {
			// Monster context
			float f, x, y, z;
			if (!*text) {
				WARNING("unexpected EOF");
				return false;
			} else if (MATCH("}")) {
				current_monster = NULL;
			} else if (MATCH("モーション [ %u ] = {", &n)) {
				if (n >= current_monster->nr_motions) {
					current_monster->motions = xrealloc_array(current_monster->motions, current_monster->nr_motions, n + 1, sizeof(struct motion));
					current_monster->nr_motions = n + 1;
				}
				current_motion = &current_monster->motions[n];
			} else if (MATCH("名前 = " QUOTED_STRING, string_buf)) {
				current_monster->name = utf2sjis(string_buf, 0);
			} else if (MATCH("データ = " QUOTED_STRING, string_buf)) {
				current_monster->poly_name = utf2sjis(string_buf, 0);
			} else if (MATCH("スケール = %f", &f)) {
				current_monster->scale = f;
			} else if (MATCH("位置 = ( %f , %f , %f )", &x, &y, &z)) {
				current_monster->pos_x = x;
				current_monster->pos_y = y;
				current_monster->pos_z = z;
			} else if (MATCH("影生成 = %u", &n)) {
				current_monster->make_shadow = !!n;
			} else if (MATCH("影描画 = %u", &n)) {
				current_monster->draw_shadow = !!n;
			} else if (MATCH("影ボーン半径 = %f", &f)) {
				current_monster->shadow_bone_radius = f;
			} else if (MATCH("常備エフェクト = " QUOTED_STRING, string_buf)) {
				current_monster->wear_effect_name = utf2sjis(string_buf, 0);
			} else {
				char *eol = strchr(text, '\n');
				*eol = '\0';
				WARNING("Parse error in monster, near: \"%s\"", text);
				return false;
			}
		} else {
			// Motion context
			int begin, end;
			if (!*text) {
				WARNING("unexpected EOF");
				return false;
			} else if (MATCH("}")) {
				current_motion = NULL;
			} else if (MATCH("名前 = " QUOTED_STRING, string_buf)) {
				current_motion->name = utf2sjis(string_buf, 0);
			} else if (MATCH("データ = " QUOTED_STRING, string_buf)) {
				current_motion->data_name = utf2sjis(string_buf, 0);
// XXX: sscanf doesn't match this string on Windows for some reason...
#define LOOP_STR "タイプ\x09\x09= ループ"
			} else if (!strncmp(text, LOOP_STR, sizeof(LOOP_STR)-1)) {
				text += sizeof(LOOP_STR)-1;
				current_motion->type = MOTION_LOOP;
			} else if (MATCH("タイプ = ループ")) {
				current_motion->type = MOTION_LOOP;
			} else if (MATCH("フレーム = %d , %d", &begin, &end)) {
				current_motion->frame_begin = begin;
				current_motion->frame_end = end;
			} else if (MATCH("ループフレーム = %d , %d", &begin, &end)) {
				current_motion->loop_frame_begin = begin;
				current_motion->loop_frame_end = end;
			} else if (MATCH("エフェクト = " QUOTED_STRING, string_buf)) {
				current_motion->effect_name = utf2sjis(string_buf, 0);
			} else {
				char *eol = strchr(text, '\n');
				*eol = '\0';
				WARNING("Parse error in motion, near: \"%s\"", text);
				return false;
			}
		}
	}
}

static bool MonsterInfo_Load(struct string *filename)
{
#ifdef RQ_MODLOADER
	if (rq_loaded_name && !strcmp(rq_loaded_name, filename->text))
		return true;
	rq_file_count = 0;
#endif
	char *path = gamedir_path("Data/ReignData.red");
	int error = ARCHIVE_SUCCESS;
	struct archive *aar = (struct archive *)aar_open(path, MMAP_IF_64BIT, &error);
	if (error == ARCHIVE_FILE_ERROR) {
		WARNING("aar_open(\"%s\"): %s", display_utf0(path), strerror(errno));
	} else if (error == ARCHIVE_BAD_ARCHIVE_ERROR) {
		WARNING("aar_open(\"%s\"): invalid AAR archive", display_utf0(path));
	}
	free(path);
	if (!aar)
		return false;
	struct archive_data *dfile = archive_get_by_name(aar, filename->text);
	if (!dfile) {
		archive_free(aar);
		return false;
	}

	// We could parse the SJIS text directly, but convert it to UTF-8 for code
	// clarity.
	char *sjis_text = xmalloc(dfile->size + 1);
	memcpy(sjis_text, dfile->data, dfile->size);
	sjis_text[dfile->size] = '\0';
	char *text = sjis2utf(sjis_text, dfile->size);
	bool ok = parse_monster_info(text);
	free(text);
	free(sjis_text);
	archive_free_data(dfile);
	archive_free(aar);
#ifdef RQ_MODLOADER
	if (ok) {
		free(rq_loaded_name);
		rq_loaded_name = strdup(filename->text);
		rq_apply_derived();
	}
#endif
	return ok;
}

static void free_monster(struct monster *m)
{
	free(m->name);
	free(m->poly_name);
	free(m->wear_effect_name);
	for (unsigned j = 0; j < m->nr_motions; j++) {
		struct motion *mot = &m->motions[j];
		free(mot->name);
		free(mot->data_name);
		free(mot->effect_name);
	}
	free(m->motions);
	memset(m, 0, sizeof *m);
}

static void MonsterInfo_Release(void)
{
	for (unsigned i = 0; i < nr_monsters; i++)
		free_monster(&monsters[i]);
	free(monsters);
	monsters = NULL;
	nr_monsters = 0;
#ifdef RQ_MODLOADER
	free(rq_loaded_name);
	rq_loaded_name = NULL;
	rq_file_count = 0;
#endif
}

#ifdef RQ_MODLOADER
static char *dup_or_null(const char *s)
{
	return s ? strdup(s) : NULL;
}

static void copy_monster(struct monster *dst, const struct monster *src)
{
	*dst = *src;
	dst->name = dup_or_null(src->name);
	dst->poly_name = dup_or_null(src->poly_name);
	dst->wear_effect_name = dup_or_null(src->wear_effect_name);
	dst->motions = NULL;
	if (src->nr_motions) {
		dst->motions = xcalloc(src->nr_motions, sizeof(struct motion));
		for (unsigned j = 0; j < src->nr_motions; j++) {
			dst->motions[j] = src->motions[j];
			dst->motions[j].name = dup_or_null(src->motions[j].name);
			dst->motions[j].data_name = dup_or_null(src->motions[j].data_name);
			dst->motions[j].effect_name = dup_or_null(src->motions[j].effect_name);
		}
	}
}

static void rq_apply_one(unsigned k)
{
	struct rq_derived *d = &rq_derived[k];
	unsigned at = rq_file_count + k;
	struct monster *m;

	if (d->base < 0 || (unsigned)d->base >= rq_file_count)
		return;
	if (at >= nr_monsters) {
		monsters = xrealloc_array(monsters, nr_monsters, at + 1, sizeof(struct monster));
		nr_monsters = at + 1;
	}
	m = &monsters[at];
	free_monster(m);
	copy_monster(m, &monsters[d->base]);
	m->scale = d->scale;
	m->pos_x = d->x;
	m->pos_y = d->y;
	m->pos_z = d->z;
	/* A model with textures of its own loads from "*<number>\<base's folder>": the plugins keep it
	 * apart from the base, and the 3D code reads the base's files (3d/rq_textures.h). */
	if (d->textures && m->poly_name && m->poly_name[0]) {
		char *marked = xmalloc(strlen(m->poly_name) + 16);
		sprintf(marked, "*%u\\%s", 100000u + at, m->poly_name);
		free(m->poly_name);
		m->poly_name = marked;
	}
}

static void rq_apply_derived(void)
{
	for (unsigned k = 0; k < rq_nderived; k++)
		rq_apply_one(k);
}

/* The table, read from the game's file when the game has not read it yet. */
static bool rq_models_ensure(void)
{
	if (rq_loaded_name)
		return true;
	struct string *name = cstr_to_string("Data\\MonsterInfo.txt");
	bool ok = MonsterInfo_Load(name);
	free_string(name);
	return ok;
}

int rq_models_count(int *file_count)
{
	if (!rq_models_ensure())
		return -1;
	if (file_count)
		*file_count = rq_file_count;
	return nr_monsters;
}

/* Entry i's fields; the texts are UTF-8 copies the caller frees. */
bool rq_model_info(int i, char **name, char **poly, float *scale, float *x, float *y, float *z, int *motions)
{
	if (!rq_models_ensure() || i < 0 || (unsigned)i >= nr_monsters)
		return false;
	struct monster *m = &monsters[i];
	*name = m->name ? sjis2utf(m->name, 0) : strdup("");
	/* The folder it draws, without a mod's model's mark. */
	const char *p = m->poly_name;
	if (p && p[0] == '*') {
		const char *q = p + 1;
		while (*q >= '0' && *q <= '9')
			q++;
		if (q > p + 1 && *q == '\\')
			p = q + 1;
	}
	*poly = p ? sjis2utf(p, 0) : strdup("");
	*scale = m->scale;
	*x = m->pos_x;
	*y = m->pos_y;
	*z = m->pos_z;
	*motions = m->nr_motions;
	return true;
}

/* A mod's model: slot -1 adds one, else changes that one (0-based among the mod's models).
 * Returns the entry's index, or -1 for a base that is not one of the file's entries. */
int rq_model_derive(int slot, int base, float scale, float x, float y, float z, bool textures)
{
	if (!rq_models_ensure() || base < 0 || (unsigned)base >= rq_file_count)
		return -1;
	if (slot < 0) {
		rq_derived = xrealloc_array(rq_derived, rq_nderived, rq_nderived + 1, sizeof *rq_derived);
		slot = rq_nderived++;
	} else if ((unsigned)slot >= rq_nderived) {
		return -1;
	}
	rq_derived[slot] = (struct rq_derived){ base, scale, x, y, z, textures };
	rq_apply_one(slot);
	return rq_file_count + slot;
}
#endif

static int MonsterInfo_GetNumof(void)
{
	return nr_monsters;
}

static void MonsterInfo_GetViewName(int monster, struct string **name)
{
	if ((unsigned)monster >= nr_monsters)
		return;
	return_string(name, monsters[monster].name);
}

static void MonsterInfo_GetPolyName(int monster, struct string **name)
{
	if ((unsigned)monster >= nr_monsters)
		return;
	return_string(name, monsters[monster].poly_name);
}

static float MonsterInfo_GetSpecular(int monster)
{
	return 0.0;
}

static float MonsterInfo_GetPosX(int monster)
{
	if ((unsigned)monster >= nr_monsters)
		return 0.0;
	return monsters[monster].pos_x;
}

static float MonsterInfo_GetPosY(int monster)
{
	if ((unsigned)monster >= nr_monsters)
		return 0.0;
	return monsters[monster].pos_y;
}

static float MonsterInfo_GetPosZ(int monster)
{
	if ((unsigned)monster >= nr_monsters)
		return 0.0;
	return monsters[monster].pos_z;
}

static float MonsterInfo_GetScale(int monster)
{
	if ((unsigned)monster >= nr_monsters)
		return 0.0;
	return monsters[monster].scale;
}

static float MonsterInfo_GetShadowOffsetX(int monster)
{
	return 0.0;
}

static float MonsterInfo_GetShadowOffsetY(int monster)
{
	return 1.5;
}

static float MonsterInfo_GetShadowOffsetZ(int monster)
{
	return 0.0;
}

static float MonsterInfo_GetShadowBoxSizeX(int monster)
{
	return 3.0;
}

static float MonsterInfo_GetShadowBoxSizeY(int monster)
{
	return 3.0;
}

static float MonsterInfo_GetShadowBoxSizeZ(int monster)
{
	return 3.0;
}

static int MonsterInfo_GetNumofMotion(int monster)
{
	if ((unsigned)monster >= nr_monsters)
		return 0;
	return monsters[monster].nr_motions;
}

static void MonsterInfo_GetWearEffectName(int monster, struct string **name)
{
	if ((unsigned)monster >= nr_monsters)
		return;
	return_string(name, monsters[monster].wear_effect_name);
}

static int MonsterInfo_GetCameraSetting(int monster)
{
	return 0;
}

static void MonsterInfo_GetMotionName(int monster, int motion, struct string **name)
{
	struct motion *m = get_motion(monster, motion);
	return_string(name, m ? m->name : NULL);
}

static void MonsterInfo_GetMotionDataName(int monster, int motion, struct string **name)
{
	struct motion *m = get_motion(monster, motion);
	return_string(name, m ? m->data_name : NULL);
}

static int MonsterInfo_GetMotionType(int monster, int motion)
{
	struct motion *m = get_motion(monster, motion);
	return m ? m->type : 0;
}

static void MonsterInfo_GetMotionEffectName(int monster, int motion, struct string **name)
{
	struct motion *m = get_motion(monster, motion);
	return_string(name, m ? m->effect_name : NULL);
}

static float MonsterInfo_GetBeginFrame(int monster, int motion)
{
	struct motion *m = get_motion(monster, motion);
	return m ? m->frame_begin : 0.0;
}

static float MonsterInfo_GetEndFrame(int monster, int motion)
{
	struct motion *m = get_motion(monster, motion);
	return m ? m->frame_end : 0.0;
}

static float MonsterInfo_GetBeginLoopFrame(int monster, int motion)
{
	struct motion *m = get_motion(monster, motion);
	return m ? m->loop_frame_begin : 0.0;
}

static float MonsterInfo_GetEndLoopFrame(int monster, int motion)
{
	struct motion *m = get_motion(monster, motion);
	return  m ? m->loop_frame_end : 0.0;
}

static bool MonsterInfo_GetColumnPos(int monster, float *pos_x, float *pos_y, float *pos_z)
{
	*pos_x = *pos_y = *pos_z = 0.0;
	return true;
}

static float MonsterInfo_GetColumnHeight(int monster)
{
	return 2.0;
}

static float MonsterInfo_GetColumnRadius(int monster)
{
	return 1.0;
}

static int MonsterInfo_GetDrawShadow(int monster)
{
	if ((unsigned)monster >= nr_monsters)
		return 0;
	return monsters[monster].draw_shadow;
}

static int MonsterInfo_GetMakeShadow(int monster)
{
	if ((unsigned)monster >= nr_monsters)
		return 0;
	return monsters[monster].make_shadow;
}

static float MonsterInfo_GetShadowVolumeBoneRadius(int monster)
{
	if ((unsigned)monster >= nr_monsters)
		return 0.0;
	return monsters[monster].shadow_bone_radius;
}

static void MonsterInfo_ModuleFini(void)
{
	MonsterInfo_Release();
}

HLL_LIBRARY(MonsterInfo,
	    HLL_EXPORT(_ModuleFini, MonsterInfo_ModuleFini),
	    HLL_EXPORT(Load, MonsterInfo_Load),
	    HLL_EXPORT(Release, MonsterInfo_Release),
	    HLL_EXPORT(GetNumof, MonsterInfo_GetNumof),
	    HLL_EXPORT(GetViewName, MonsterInfo_GetViewName),
	    HLL_EXPORT(GetPolyName, MonsterInfo_GetPolyName),
	    HLL_EXPORT(GetSpecular, MonsterInfo_GetSpecular),
	    HLL_EXPORT(GetPosX, MonsterInfo_GetPosX),
	    HLL_EXPORT(GetPosY, MonsterInfo_GetPosY),
	    HLL_EXPORT(GetPosZ, MonsterInfo_GetPosZ),
	    HLL_EXPORT(GetScale, MonsterInfo_GetScale),
	    HLL_EXPORT(GetShadowOffsetX, MonsterInfo_GetShadowOffsetX),
	    HLL_EXPORT(GetShadowOffsetY, MonsterInfo_GetShadowOffsetY),
	    HLL_EXPORT(GetShadowOffsetZ, MonsterInfo_GetShadowOffsetZ),
	    HLL_EXPORT(GetShadowBoxSizeX, MonsterInfo_GetShadowBoxSizeX),
	    HLL_EXPORT(GetShadowBoxSizeY, MonsterInfo_GetShadowBoxSizeY),
	    HLL_EXPORT(GetShadowBoxSizeZ, MonsterInfo_GetShadowBoxSizeZ),
	    HLL_EXPORT(GetNumofMotion, MonsterInfo_GetNumofMotion),
	    HLL_EXPORT(GetWearEffectName, MonsterInfo_GetWearEffectName),
	    HLL_EXPORT(GetCameraSetting, MonsterInfo_GetCameraSetting),
	    HLL_EXPORT(GetMotionName, MonsterInfo_GetMotionName),
	    HLL_EXPORT(GetMotionDataName, MonsterInfo_GetMotionDataName),
	    HLL_EXPORT(GetMotionType, MonsterInfo_GetMotionType),
	    HLL_EXPORT(GetMotionEffectName, MonsterInfo_GetMotionEffectName),
	    HLL_EXPORT(GetBeginFrame, MonsterInfo_GetBeginFrame),
	    HLL_EXPORT(GetEndFrame, MonsterInfo_GetEndFrame),
	    HLL_EXPORT(GetBeginLoopFrame, MonsterInfo_GetBeginLoopFrame),
	    HLL_EXPORT(GetEndLoopFrame, MonsterInfo_GetEndLoopFrame),
	    HLL_EXPORT(GetColumnPos, MonsterInfo_GetColumnPos),
	    HLL_EXPORT(GetColumnHeight, MonsterInfo_GetColumnHeight),
	    HLL_EXPORT(GetColumnRadius, MonsterInfo_GetColumnRadius),
	    HLL_EXPORT(GetDrawShadow, MonsterInfo_GetDrawShadow),
	    HLL_EXPORT(GetMakeShadow, MonsterInfo_GetMakeShadow),
	    HLL_EXPORT(GetShadowVolumeBoneRadius, MonsterInfo_GetShadowVolumeBoneRadius)
	    );
