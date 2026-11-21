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

#ifndef RQMOD_AFF_H
#define RQMOD_AFF_H

#include <stddef.h>
#include <stdint.h>
#include "swf_check.h"

/* aff.c */
void report(const char *name, const struct swfc *c, int status, int json);
/* A SWF (FWS) compressed with zlib (CWS); NULL on failure. */
uint8_t *swf_compress(const uint8_t *fws, size_t n, size_t *out_n);
/* A SWF (FWS) as the game's .aff: compressed, its first 0x40 bytes masked, the header in front. */
uint8_t *aff_wrap(const uint8_t *fws, size_t n, size_t *out_n);
int write_file(const char *path, const uint8_t *data, size_t n);

/* build.c */
int cmd_build(int argc, char **argv);

/* wav.c: a sound as a SWF holds it. */
struct pcm {
	int rate;           /* 5512, 11025, 22050 or 44100 */
	int bits;           /* 8 (unsigned) or 16 (signed, little-endian) */
	int chans;          /* 1 or 2 */
	uint32_t samples;   /* per channel */
	uint8_t *data;
	size_t bytes;
	char note[200];     /* what was changed, or "" */
};

/* Reads a WAV file. A sound the SWF format holds as it is (PCM at 5512, 11025, 22050 or
 * 44100 Hz, 8 or 16 bits, mono or stereo) comes as it is; any other WAV is converted to
 * 44100 Hz, 16 bits, and at most two channels. 0 with err for MP3, Ogg and the rest. */
int wav_load(const char *path, struct pcm *out, char *err, size_t errn);
void pcm_free(struct pcm *p);

#endif /* RQMOD_AFF_H */
