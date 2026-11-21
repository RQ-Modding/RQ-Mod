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
 * WAV files for rqmod-aff build. The game plays a Flash file's sound only as uncompressed PCM,
 * at one of the four rates a SWF names (5512, 11025, 22050, 44100 Hz), 8 or 16 bits, mono or
 * stereo. Such a WAV goes in as it is; any other (48000 Hz, 24 bits, float, five channels) is
 * converted to 44100 Hz, 16 bits, at most two channels, with a Lanczos resampler.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "system4.h"
#include "system4/file.h"

#include "aff.h"

#define WAV_FMT_PCM 1
#define WAV_FMT_FLOAT 3
#define WAV_FMT_EXTENSIBLE 0xfffe
#define LANCZOS_A 3

static uint32_t le32(const uint8_t *p)
{
	return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24;
}

static unsigned le16(const uint8_t *p)
{
	return p[0] | p[1] << 8;
}

void pcm_free(struct pcm *p)
{
	free(p->data);
	p->data = NULL;
}

/* What a file that is not a WAV looks like, for the message. */
static const char *not_wav(const uint8_t *d, size_t n)
{
	if (n >= 3 && !memcmp(d, "ID3", 3))
		return "an MP3";
	if (n >= 2 && d[0] == 0xff && (d[1] & 0xe0) == 0xe0)
		return "an MP3";
	if (n >= 4 && !memcmp(d, "OggS", 4))
		return "an Ogg";
	if (n >= 4 && !memcmp(d, "fLaC", 4))
		return "a FLAC";
	if (n >= 4 && !memcmp(d, "FORM", 4))
		return "an AIFF";
	return NULL;
}

/* One sample of channel ch at frame i, from -1 to 1. */
static double sample(const uint8_t *data, int fmt, int bits, int chans, size_t i, int ch)
{
	const uint8_t *p = data + (i * chans + ch) * (bits / 8);

	if (fmt == WAV_FMT_FLOAT) {
		if (bits == 32) {
			float f;
			uint32_t u = le32(p);
			memcpy(&f, &u, 4);
			return f;
		} else {
			double f;
			uint64_t u = le32(p) | (uint64_t)le32(p + 4) << 32;
			memcpy(&f, &u, 8);
			return f;
		}
	}
	switch (bits) {
	case 8:
		return (p[0] - 128) / 128.0;
	case 16:
		return (int16_t)le16(p) / 32768.0;
	case 24:
		return (int32_t)((uint32_t)p[0] << 8 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 24) / 2147483648.0;
	case 32:
		return (int32_t)le32(p) / 2147483648.0;
	}
	return 0;
}

static double sinc(double x)
{
	if (fabs(x) < 1e-9)
		return 1;
	x *= 3.14159265358979323846;
	return sin(x) / x;
}

int wav_load(const char *path, struct pcm *out, char *err, size_t errn)
{
	size_t n = 0, at = 12, data_at = 0, data_len = 0, frames, i;
	uint8_t *d = file_read(path, &n);
	int fmt = 0, chans = 0, rate = 0, bits = 0, have_fmt = 0, oc, ch;
	const char *other;
	double *mix, ratio;
	int16_t *pcm;

	memset(out, 0, sizeof *out);
	if (!d) {
		snprintf(err, errn, "cannot read %s", path);
		return 0;
	}
	if ((other = not_wav(d, n))) {
		snprintf(err, errn, "%s is %s file: the game plays a Flash file's sound only as PCM. Export the "
			 "sound as WAV (PCM) from your sound program and give that file.", path, other);
		free(d);
		return 0;
	}
	if (n < 12 || memcmp(d, "RIFF", 4) || memcmp(d + 8, "WAVE", 4)) {
		snprintf(err, errn, "%s is not a WAV file: export the sound as WAV (PCM).", path);
		free(d);
		return 0;
	}
	while (at + 8 <= n) {
		uint32_t len = le32(d + at + 4);
		const uint8_t *body = d + at + 8;
		if (len > n - at - 8)
			len = (uint32_t)(n - at - 8);
		if (!memcmp(d + at, "fmt ", 4) && len >= 16) {
			fmt = (int)le16(body);
			chans = (int)le16(body + 2);
			rate = (int)le32(body + 4);
			bits = (int)le16(body + 14);
			if (fmt == WAV_FMT_EXTENSIBLE && len >= 40)
				fmt = (int)le16(body + 24);
			have_fmt = 1;
		} else if (!memcmp(d + at, "data", 4)) {
			data_at = at + 8;
			data_len = len;
		}
		at += 8 + len + (len & 1);
	}
	if (!have_fmt || !data_at) {
		snprintf(err, errn, "%s is a broken WAV file (no %s chunk).", path, have_fmt ? "data" : "fmt");
		free(d);
		return 0;
	}
	if ((fmt != WAV_FMT_PCM && fmt != WAV_FMT_FLOAT) || chans < 1 || chans > 32 || rate < 1000 || rate > 384000
	    || (fmt == WAV_FMT_PCM && bits != 8 && bits != 16 && bits != 24 && bits != 32)
	    || (fmt == WAV_FMT_FLOAT && bits != 32 && bits != 64)) {
		snprintf(err, errn, "%s holds a sound in a form this tool does not read (format %d, %d bits, %d "
			 "channels, %d Hz): export it as WAV, PCM 16-bit.", path, fmt, bits, chans, rate);
		free(d);
		return 0;
	}
	frames = data_len / ((size_t)chans * (bits / 8));
	if (!frames) {
		snprintf(err, errn, "%s holds no sound.", path);
		free(d);
		return 0;
	}

	/* As it is, when a SWF holds it so. */
	if (fmt == WAV_FMT_PCM && (bits == 8 || bits == 16) && chans <= 2
	    && (rate == 5512 || rate == 11025 || rate == 22050 || rate == 44100)) {
		out->rate = rate;
		out->bits = bits;
		out->chans = chans;
		out->samples = (uint32_t)frames;
		out->bytes = frames * chans * (bits / 8);
		out->data = malloc(out->bytes);
		if (!out->data) {
			snprintf(err, errn, "out of memory");
			free(d);
			return 0;
		}
		memcpy(out->data, d + data_at, out->bytes);
		free(d);
		return 1;
	}

	/* Otherwise mixed down to two channels (even ones left, odd ones right), then to 44100 Hz. */
	oc = chans >= 2 ? 2 : 1;
	mix = malloc(frames * oc * sizeof *mix);
	if (!mix) {
		snprintf(err, errn, "out of memory");
		free(d);
		return 0;
	}
	for (i = 0; i < frames; i++) {
		for (ch = 0; ch < oc; ch++) {
			double s = 0;
			int c, used = 0;
			for (c = ch; c < chans; c += oc) {
				s += sample(d + data_at, fmt, bits, chans, i, c);
				used++;
			}
			mix[i * oc + ch] = used ? s / used : 0;
		}
	}
	free(d);
	ratio = 44100.0 / rate;
	out->samples = (uint32_t)floor(frames * ratio);
	if (!out->samples)
		out->samples = 1;
	pcm = malloc((size_t)out->samples * oc * sizeof *pcm);
	if (!pcm) {
		free(mix);
		snprintf(err, errn, "out of memory");
		return 0;
	}
	{
		/* Lanczos: a window of LANCZOS_A lobes, widened when the rate goes down so it does not alias. */
		double scale = ratio < 1 ? ratio : 1, support = LANCZOS_A / scale;
		size_t j;
		for (j = 0; j < out->samples; j++) {
			double t = j / ratio;
			long k0 = (long)floor(t - support) + 1, k1 = (long)floor(t + support), k;
			for (ch = 0; ch < oc; ch++) {
				double acc = 0, wsum = 0, v;
				for (k = k0; k <= k1; k++) {
					double x = (t - k) * scale, w;
					if (k < 0 || (size_t)k >= frames || fabs(x) >= LANCZOS_A)
						continue;
					w = sinc(x) * sinc(x / LANCZOS_A);
					acc += mix[(size_t)k * oc + ch] * w;
					wsum += w;
				}
				v = wsum ? acc / wsum : 0;
				if (v > 1)
					v = 1;
				if (v < -1)
					v = -1;
				pcm[j * oc + ch] = (int16_t)lrint(v * 32767);
			}
		}
	}
	free(mix);
	out->rate = 44100;
	out->bits = 16;
	out->chans = oc;
	out->bytes = (size_t)out->samples * oc * 2;
	out->data = malloc(out->bytes);
	if (!out->data) {
		free(pcm);
		snprintf(err, errn, "out of memory");
		return 0;
	}
	for (i = 0; i < (size_t)out->samples * oc; i++) {
		uint16_t u = (uint16_t)pcm[i];
		out->data[i * 2] = u & 0xff;
		out->data[i * 2 + 1] = u >> 8;
	}
	free(pcm);
	snprintf(out->note, sizeof out->note, "converted from %d Hz, %d bits%s, %d channel%s to 44100 Hz, 16 bits, %s",
		 rate, bits, fmt == WAV_FMT_FLOAT ? " float" : "", chans, chans == 1 ? "" : "s", oc == 1 ? "mono" : "stereo");
	return 1;
}
