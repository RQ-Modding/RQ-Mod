/* Copyright (C) 2026 Rance Quest modding fork
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

/*
 * rq_pixels: a mod's pictures as RQ MOD reads them (every kind of PNG, refused with a reason
 * instead of closing the program), and the rules a mod's picture on one of the game's 3D textures
 * follows. No file access: each program reads the bytes its own way. Only libpng, libsys4's
 * struct cg and the C library, so the same file builds in the engine and in Rance Browser, which
 * keeps a copy (src/core/rq_pixels.c and .h): change both together.
 */

#ifndef RQ_PIXELS_H
#define RQ_PIXELS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct cg;

/* The largest side, in pixels, of a mod's picture on a 3D texture. */
#define RQ_PIXELS_TEXTURE_SIDE 4096
/* The largest side of a mod's still picture for rq.image.add, replace and flash_bitmap. */
#define RQ_PIXELS_PICTURE_SIDE 16384

/* Whether the bytes start with PNG's signature. */
bool rq_pixels_is_png(const uint8_t *data, size_t size);

/*
 * Any PNG (1 to 16 bits; RGB, RGBA, grey, grey with alpha or a palette; tRNS; interlaced) as an
 * 8-bit RGBA cg, has_alpha when the file has an alpha channel or tRNS. A side over `max_side` is
 * refused from the header, before any pixel is read. NULL on any failure, never an abort, with
 * the reason in `why` ("not a PNG", "the file is cut off", "5000x16 (max 4096 a side)" or
 * libpng's own words). An 8-bit RGB or RGBA file, not interlaced and without tRNS, gives
 * libsys4's pixels and has_alpha byte for byte. The caller frees the cg with cg_free.
 */
struct cg *rq_pixels_png(const uint8_t *data, size_t size, int max_side, char *why, size_t why_len);

/*
 * A mod's picture drawn with the game's alpha kind, so a material stays as see-through as the game
 * made it. `game_has_alpha` is the game's picture's; `game` is that picture decoded, needed only
 * when the game's has alpha and the mod's has none (it may be NULL otherwise).
 *   the game's has no alpha: the mod's is made solid (alpha 255), has_alpha false;
 *   both have alpha: the mod's alpha is kept;
 *   the game's has alpha, the mod's has none: the game's alpha, scaled to the mod's size (the
 *   point nearest each point's centre), so the game's holes stay.
 * has_alpha is always the game's afterwards.
 */
void rq_pixels_game_alpha(struct cg *mod, bool game_has_alpha, const struct cg *game);

#endif /* RQ_PIXELS_H */
