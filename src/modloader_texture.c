/* Copyright (C) 2026 Rance Quest modding fork
 *
 * rq.image.texture: a mod's pictures on the game's 3D textures. The 3D code (src/3d/model.c)
 * asks RE_mod_texture for each texture a model loads; this file answers it.
 */

#ifdef RQ_MODLOADER

#include <stddef.h>

#include "system4/archive.h"
#include "system4/cg.h"

#include "3d/3d_internal.h"

struct cg *RE_mod_texture(struct archive_data *game, const char *path, const char *name, int kind)
{
	(void)game;
	(void)path;
	(void)name;
	(void)kind;
	return NULL;
}

#endif /* RQ_MODLOADER */
