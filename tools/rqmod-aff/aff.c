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
 * rqmod-aff: Rance Quest's Flash animations (.aff) from the command line.
 *
 *   rqmod-aff unpack FILE.aff [OUT.swf]          the SWF inside, uncompressed, for a viewer
 *   rqmod-aff unpack --game DIR NAME [OUT.swf]   one of the game's own, by name
 *   rqmod-aff unpack --game DIR --all OUTDIR     every one of the game's own
 *   rqmod-aff check FILE [--json]                what the game plays of it, and every problem
 *   rqmod-aff check --game DIR (NAME | --all) [--json]
 *   rqmod-aff build FRAMES OUT.aff [--fps N] [--sound FILE.wav[@FRAME]]... [--size WxH]
 *   rqmod-aff list --game DIR                    the game's Flash files by name
 *
 * The rules are the engine's own (src/swf_check.c), the ones rq.image.add_flash uses.
 * Exit codes: 0 the game plays it, 1 it would not (or a command failed), 2 a wrong command
 * line, 3 a file that cannot be read.
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <zlib.h>

#include "system4.h"
#include "system4/afa.h"
#include "system4/archive.h"
#include "system4/file.h"
#include "system4/string.h"
#include "system4/utfsjis.h"

#include "aff.h"

#define FLASH_SYSTEM "Data/Flash/System/"

static const char usage_text[] =
	"rqmod-aff: Rance Quest's Flash animations (.aff)\n"
	"\n"
	"  rqmod-aff unpack FILE.aff [OUT.swf]          the SWF inside, uncompressed\n"
	"  rqmod-aff unpack --game DIR NAME [OUT.swf]   one of the game's, by name\n"
	"  rqmod-aff unpack --game DIR --all OUTDIR     every one of the game's\n"
	"  rqmod-aff check FILE [--json]                what the game plays of a file\n"
	"  rqmod-aff check --game DIR (NAME | --all) [--json]\n"
	"  rqmod-aff build FRAMES OUT.aff [--fps N] [--sound FILE.wav[@FRAME]]... [--size WxH]\n"
	"  rqmod-aff list --game DIR                    the game's Flash files by name\n"
	"\n"
	"FILE is an .aff or a .swf (uncompressed or zlib). DIR is the game's folder. NAME is a\n"
	"file as rq.image names it: \"Fireball\" for Data/Flash/System/Fireball.swf, or the full\n"
	"path, such as \"Data/Flash/Battle/<name>.swf\".\n"
	"FRAMES is a folder of PNG files, taken in name order, or a pattern such as walk_*.png.\n"
	"OUT ends in .aff (the game's form) or .swf (compressed, for a viewer).\n"
	"\n"
	"Exit codes: 0 the game plays it, 1 it would not or the command failed, 2 a wrong\n"
	"command line, 3 a file that cannot be read.\n";

static int usage(void)
{
	fputs(usage_text, stderr);
	return 2;
}

/* The arguments as UTF-8, so a Japanese file name arrives whole. */
static char **utf8_args(int *argc)
{
	LPWSTR *w;
	char **out;
	int n, i;

	w = CommandLineToArgvW(GetCommandLineW(), &n);
	if (!w)
		return NULL;
	out = calloc((size_t)n + 1, sizeof *out);
	for (i = 0; out && i < n; i++) {
		int len = WideCharToMultiByte(CP_UTF8, 0, w[i], -1, NULL, 0, NULL, NULL);
		out[i] = len > 0 ? malloc((size_t)len) : NULL;
		if (!out[i] || !WideCharToMultiByte(CP_UTF8, 0, w[i], -1, out[i], len, NULL, NULL)) {
			LocalFree(w);
			return NULL;
		}
	}
	LocalFree(w);
	if (!out)
		return NULL;
	*argc = n;
	return out;
}

/* ---- output ---- */

int write_file(const char *path, const uint8_t *data, size_t n)
{
	FILE *f = file_open_utf8(path, "wb");

	if (!f)
		return 0;
	if (n && fwrite(data, 1, n, f) != n) {
		fclose(f);
		return 0;
	}
	return fclose(f) == 0;
}

uint8_t *swf_compress(const uint8_t *fws, size_t n, size_t *out_n)
{
	uLongf cap = compressBound((uLong)(n - 8));
	uint8_t *out = malloc(8 + cap);

	if (!out || n < 8)
		return free(out), NULL;
	memcpy(out, fws, 8);
	out[0] = 'C';
	if (compress2(out + 8, &cap, fws + 8, (uLong)(n - 8), 9) != Z_OK) {
		free(out);
		return NULL;
	}
	*out_n = 8 + cap;
	return out;
}

static const uint8_t aff_mask[16] = {
	0xC8, 0xBB, 0x8F, 0xB7, 0xED, 0x43, 0x99, 0x4A,
	0xA2, 0x7E, 0x5B, 0xB0, 0x68, 0x18, 0xF8, 0x88
};

static void put32(uint8_t *p, uint32_t v)
{
	p[0] = v & 0xff;
	p[1] = (v >> 8) & 0xff;
	p[2] = (v >> 16) & 0xff;
	p[3] = (v >> 24) & 0xff;
}

uint8_t *aff_wrap(const uint8_t *fws, size_t n, size_t *out_n)
{
	size_t cn, i;
	uint8_t *cws = swf_compress(fws, n, &cn), *out;

	if (!cws)
		return NULL;
	out = malloc(16 + cn);
	if (!out) {
		free(cws);
		return NULL;
	}
	memcpy(out, "AFF\0", 4);
	put32(out + 4, 1);
	put32(out + 8, (uint32_t)(16 + cn));
	put32(out + 12, 0x4d2);
	memcpy(out + 16, cws, cn);
	for (i = 0; i < 0x40 && i < cn; i++)
		out[16 + i] ^= aff_mask[i & 15];
	free(cws);
	*out_n = 16 + cn;
	return out;
}

/* ---- the report ---- */

static void json_str(const char *s)
{
	putchar('"');
	for (; *s; s++) {
		unsigned char ch = (unsigned char)*s;
		if (ch == '"' || ch == '\\')
			printf("\\%c", ch);
		else if (ch < 0x20)
			printf("\\u%04x", ch);
		else
			putchar(ch);
	}
	putchar('"');
}

static void report_json(const char *name, const struct swfc *c, int status)
{
	int i, first;

	printf("{\"file\":");
	json_str(name);
	printf(",\"plays\":%s,\"status\":%d,\"container\":", status ? "false" : "true", status);
	json_str(c->container);
	printf(",\"inner\":");
	json_str(c->inner);
	printf(",\"version\":%d,\"file_size\":%zu,\"swf_size\":%zu", c->version, c->file_size, c->fws_size);
	printf(",\"width\":%d,\"height\":%d,\"x_min\":%d,\"y_min\":%d", c->width, c->height, (int)c->x_min, (int)c->y_min);
	printf(",\"fps\":%g,\"frames\":%d,\"shown\":%d,\"seconds\":%.3f", c->rate / 256.0, c->frames, c->shown, swfc_seconds(c));
	printf(",\"tags\":%d,\"bitmaps\":%d,\"pixels\":%llu,\"shapes\":%d,\"sprites\":%d", c->tags, c->bitmaps,
	       (unsigned long long)c->pixels, c->shapes, c->sprites);
	printf(",\"places\":%d,\"removes\":%d,\"sounds\":%d,\"starts\":%d,\"sound_seconds\":%.3f,\"doactions\":%d",
	       c->places, c->removes, c->sounds, c->starts, c->sound_ms / 1000.0, c->doactions);
	printf(",\"blends\":{");
	for (i = 0, first = 1; i < 16; i++) {
		if (!c->blends[i])
			continue;
		printf("%s\"%d\":%d", first ? "" : ",", i, c->blends[i]);
		first = 0;
	}
	printf("},\"cx_add\":%d,\"cx_mult\":%d,\"cx_both\":%d,\"cx_alpha_add\":%d", c->cx_add, c->cx_mult, c->cx_both,
	       c->cx_alpha_add);
	printf(",\"soft_bitmaps\":%d,\"premul_bitmaps\":%d,\"fill_off\":%d", c->soft_bitmaps, c->premul_bitmaps,
	       c->fill_off);
	printf(",\"actions\":[");
	for (i = 0, first = 1; i < 256; i++) {
		if (!(c->actions[i >> 3] & (1 << (i & 7))))
			continue;
		printf("%s%d", first ? "" : ",", i);
		first = 0;
	}
	printf("],\"gotos\":[");
	for (i = 0; i < c->ngotos; i++)
		printf("%s{\"frame\":%d,\"target\":%d}", i ? "," : "", c->gotos[i].frame, c->gotos[i].target);
	printf("],\"errors\":%d,\"warnings\":%d,\"more\":%d,\"problems\":[", c->errors, c->warnings, c->more);
	for (i = 0; i < c->nitems; i++) {
		const struct swfc_item *it = &c->items[i];
		const char *tn = it->code >= 0 ? swfc_tag_name(it->code) : NULL;
		printf("%s{\"level\":\"%s\",\"frame\":%d,\"tag\":%d,\"code\":%d,\"name\":", i ? "," : "",
		       it->level == SWFC_ERROR ? "error" : "warning", it->frame, it->tag, it->code);
		json_str(tn ? tn : "");
		printf(",\"count\":%d,\"text\":", it->count);
		json_str(it->text);
		putchar('}');
	}
	printf("]}");
}

static void report_text(const char *name, const struct swfc *c, int status)
{
	int i, any;

	printf("%s: %s\n", name, status == 0 ? "the game plays it" : status == 1 ? "the game would not play it"
	       : "cannot be read");
	if (c->fws) {
		printf("  %s%s%s, SWF %d, %d x %d, %d frames (%.2f s at the game's 30 frames a second), header %g fps\n",
		       c->container, c->inner[0] ? " holding " : "", c->inner, c->version, c->width, c->height,
		       c->frames, swfc_seconds(c), c->rate / 256.0);
		printf("  %d bitmaps (%.2f million pixels), %d shapes, %d sprites, %d placements, %d sounds (%.2f s)\n",
		       c->bitmaps, c->pixels / 1e6, c->shapes, c->sprites, c->places, c->sounds, c->sound_ms / 1000.0);
		printf("  blend modes:");
		for (i = 0, any = 0; i < 16; i++) {
			if (!c->blends[i])
				continue;
			printf("%s %s %d", any ? "," : "", i < 15 ? swfc_blend_name(i) : "other", c->blends[i]);
			any = 1;
		}
		printf("%s\n", any ? "" : " none");
	}
	for (i = 0; i < c->nitems; i++) {
		const struct swfc_item *it = &c->items[i];
		char where[64] = "";
		int n = 0;
		if (it->frame)
			n += snprintf(where + n, sizeof where - n, ", frame %d", it->frame);
		if (it->tag)
			snprintf(where + n, sizeof where - n, ", tag %d", it->tag);
		printf("  %s%s: %s", it->level == SWFC_ERROR ? "error" : "warning", where, it->text);
		if (it->count > 1)
			printf(" (%d times)", it->count);
		putchar('\n');
	}
	if (c->more)
		printf("  ... and %d more problems\n", c->more);
}

void report(const char *name, const struct swfc *c, int status, int json)
{
	if (json)
		report_json(name, c, status);
	else
		report_text(name, c, status);
}

/* ---- the game's files ---- */

struct game {
	struct afa_archive *ar;
};

static int game_open(struct game *g, const char *dir)
{
	UDIR *d = opendir_utf8(dir);
	char *name, *found = NULL;
	int error = 0;

	if (!d) {
		fprintf(stderr, "rqmod-aff: cannot open the folder %s\n", dir);
		return 0;
	}
	while ((name = readdir_utf8(d))) {
		size_t l = strlen(name);
		if (!found && l >= 7 && !strcasecmp(name + l - 7, "AFF.afa"))
			found = path_join(dir, name);
		free(name);
	}
	closedir_utf8(d);
	if (!found) {
		fprintf(stderr, "rqmod-aff: no Flash archive (*AFF.afa) in %s: give the game's folder\n", dir);
		return 0;
	}
	g->ar = afa_open(found, 0, &error);
	if (!g->ar) {
		fprintf(stderr, "rqmod-aff: cannot read %s: %s\n", found, archive_strerror(error));
		free(found);
		return 0;
	}
	free(found);
	return 1;
}

/* An entry's name as rq.image takes it: "Fireball" for one in Data/Flash/System, the whole
 * path with slashes and .swf for the others. UTF-8, to free. */
static char *entry_key(const struct afa_entry *e)
{
	char *u = sjis2utf(e->name->text, e->name->size), *p, *dot;
	char *out;
	size_t n;

	for (p = u; *p; p++) {
		if (*p == '\\')
			*p = '/';
	}
	if ((dot = strrchr(u, '.')) && !strchr(dot, '/'))
		*dot = 0;
	n = strlen(FLASH_SYSTEM);
	if (!strncasecmp(u, FLASH_SYSTEM, n) && !strchr(u + n, '/')) {
		out = strdup(u + n);
	} else {
		out = malloc(strlen(u) + 5);
		sprintf(out, "%s.swf", u);
	}
	free(u);
	return out;
}

/* The entry for a name, as the engine looks it up (archive_get_by_basename), then by the
 * file's name alone. */
static struct afa_entry *game_find(struct game *g, const char *key)
{
	char full[1024], *sj, *base, *want;
	struct afa_entry *hit = NULL;
	uint32_t i;
	int hits = 0;

	if (strchr(key, '/') || strchr(key, '\\'))
		snprintf(full, sizeof full, "%s", key);
	else
		snprintf(full, sizeof full, FLASH_SYSTEM "%s.swf", key);
	sj = utf2sjis(full, 0);
	want = archive_basename(sj);
	for (i = 0; i < g->ar->nr_files; i++) {
		base = archive_basename(g->ar->files[i].name->text);
		if (!strcmp(base, want))
			hit = &g->ar->files[i];
		free(base);
		if (hit)
			break;
	}
	free(want);
	free(sj);
	if (hit)
		return hit;
	/* By the file's name alone, when only one entry has it. */
	for (i = 0; i < g->ar->nr_files; i++) {
		char *k = entry_key(&g->ar->files[i]);
		const char *slash = strrchr(k, '/');
		const char *bare = slash ? slash + 1 : k;
		size_t bl = strlen(bare);
		if (slash && bl > 4 && !strcasecmp(bare + bl - 4, ".swf"))
			bl -= 4;
		if ((strlen(key) == bl && !strncasecmp(bare, key, bl)) || !strcasecmp(k, key)) {
			hit = &g->ar->files[i];
			hits++;
		}
		free(k);
	}
	return hits == 1 ? hit : NULL;
}

static uint8_t *game_read(struct game *g, struct afa_entry *e, size_t *n)
{
	struct archive_data *d = afa_entry_to_descriptor(g->ar, e);
	uint8_t *out = NULL;

	if (archive_load_file(d)) {
		out = malloc(d->size ? d->size : 1);
		if (out) {
			memcpy(out, d->data, d->size);
			*n = d->size;
		}
	}
	archive_free_data(d);
	return out;
}

/* ---- commands ---- */

static int cmd_list(int argc, char **argv)
{
	struct game g;
	uint32_t i;

	if (argc != 2 || strcmp(argv[0], "--game"))
		return usage();
	if (!game_open(&g, argv[1]))
		return 3;
	for (i = 0; i < g.ar->nr_files; i++) {
		char *k = entry_key(&g.ar->files[i]);
		printf("%s\t%u\n", k, g.ar->files[i].size);
		free(k);
	}
	archive_free(&g.ar->ar);
	return 0;
}

static int check_one(const char *name, const uint8_t *data, size_t n, int json)
{
	struct swfc c;
	int status = swfc_check(data, n, &c);

	report(name, &c, status, json);
	swfc_free(&c);
	return status;
}

static int cmd_check(int argc, char **argv)
{
	const char *game = NULL, *what = NULL;
	int all = 0, json = 0, i, worst = 0;

	for (i = 0; i < argc; i++) {
		if (!strcmp(argv[i], "--game") && i + 1 < argc)
			game = argv[++i];
		else if (!strcmp(argv[i], "--all"))
			all = 1;
		else if (!strcmp(argv[i], "--json"))
			json = 1;
		else if (argv[i][0] == '-' && argv[i][1] == '-')
			return usage();
		else if (!what)
			what = argv[i];
		else
			return usage();
	}
	if (game) {
		struct game g;
		uint32_t k;
		int plays = 0, not = 0, unread = 0, warned = 0;
		if ((all && what) || (!all && !what))
			return usage();
		if (!game_open(&g, game))
			return 3;
		if (all) {
			if (json)
				printf("[");
			for (k = 0; k < g.ar->nr_files; k++) {
				char *key = entry_key(&g.ar->files[k]);
				size_t n = 0;
				uint8_t *data = game_read(&g, &g.ar->files[k], &n);
				struct swfc c;
				int status = 3;
				if (data) {
					status = swfc_check(data, n, &c);
					if (json) {
						if (k)
							printf(",\n");
						report(key, &c, status, 1);
					} else if (status || c.nitems) {
						report(key, &c, status, 0);
					}
					if (c.warnings)
						warned++;
					swfc_free(&c);
					free(data);
				} else {
					fprintf(stderr, "rqmod-aff: cannot read %s from the archive\n", key);
				}
				if (status == 0)
					plays++;
				else if (status == 1)
					not++;
				else
					unread++;
				if (status > worst)
					worst = status;
				free(key);
			}
			if (json)
				printf("]\n");
			else
				printf("%u files: %d play, %d would not, %d cannot be read; %d with warnings\n",
				       g.ar->nr_files, plays, not, unread, warned);
		} else {
			struct afa_entry *e = game_find(&g, what);
			size_t n = 0;
			uint8_t *data;
			if (!e) {
				fprintf(stderr, "rqmod-aff: the game has no Flash file %s (rqmod-aff list names them)\n", what);
				archive_free(&g.ar->ar);
				return 1;
			}
			data = game_read(&g, e, &n);
			if (!data) {
				fprintf(stderr, "rqmod-aff: cannot read %s from the archive\n", what);
				archive_free(&g.ar->ar);
				return 3;
			}
			worst = check_one(what, data, n, json);
			if (json)
				putchar('\n');
			free(data);
		}
		archive_free(&g.ar->ar);
		return worst;
	}
	if (all || !what)
		return usage();
	{
		size_t n = 0;
		uint8_t *data = file_read(what, &n);
		if (!data) {
			fprintf(stderr, "rqmod-aff: cannot read %s\n", what);
			return 3;
		}
		worst = check_one(what, data, n, json);
		if (json)
			putchar('\n');
		free(data);
	}
	return worst;
}

/* The SWF inside data, uncompressed, written to out. */
static int unpack_to(const char *name, const uint8_t *data, size_t n, const char *out)
{
	struct swfc c;
	int status = swfc_check(data, n, &c);

	if (!c.fws) {
		report(name, &c, status, 0);
		swfc_free(&c);
		return 3;
	}
	if (!write_file(out, c.fws, c.fws_size)) {
		fprintf(stderr, "rqmod-aff: cannot write %s\n", out);
		swfc_free(&c);
		return 1;
	}
	printf("%s: %zu bytes%s\n", out, c.fws_size, status ? " (the game would not play it: rqmod-aff check says why)" : "");
	swfc_free(&c);
	return 0;
}

static char *swf_name(const char *path)
{
	const char *dot = strrchr(path, '.');
	const char *slash = strrchr(path, '/'), *bs = strrchr(path, '\\');
	size_t n = dot && (!slash || dot > slash) && (!bs || dot > bs) ? (size_t)(dot - path) : strlen(path);
	char *out = malloc(n + 5);

	memcpy(out, path, n);
	strcpy(out + n, ".swf");
	return out;
}

static int cmd_unpack(int argc, char **argv)
{
	const char *game = NULL;
	const char *pos[2] = { NULL, NULL };
	int all = 0, npos = 0, i, r;

	for (i = 0; i < argc; i++) {
		if (!strcmp(argv[i], "--game") && i + 1 < argc)
			game = argv[++i];
		else if (!strcmp(argv[i], "--all"))
			all = 1;
		else if (argv[i][0] == '-' && argv[i][1] == '-')
			return usage();
		else if (npos < 2)
			pos[npos++] = argv[i];
		else
			return usage();
	}
	if (!game) {
		size_t n = 0;
		uint8_t *data;
		char *out;
		if (all || !npos)
			return usage();
		data = file_read(pos[0], &n);
		if (!data) {
			fprintf(stderr, "rqmod-aff: cannot read %s\n", pos[0]);
			return 3;
		}
		out = pos[1] ? strdup(pos[1]) : swf_name(pos[0]);
		r = unpack_to(pos[0], data, n, out);
		free(out);
		free(data);
		return r;
	}
	{
		struct game g;
		if (!game_open(&g, game))
			return 3;
		if (all) {
			uint32_t k;
			int worst = 0;
			if (npos != 1) {
				archive_free(&g.ar->ar);
				return usage();
			}
			for (k = 0; k < g.ar->nr_files; k++) {
				char *u = sjis2utf(g.ar->files[k].name->text, g.ar->files[k].name->size), *p, *out, *dir;
				size_t n = 0;
				uint8_t *data;
				for (p = u; *p; p++) {
					if (*p == '\\')
						*p = '/';
				}
				p = path_join(pos[0], u);
				out = swf_name(p);
				free(p);
				dir = path_dirname(out);
				mkdir_p(dir);
				free(dir);
				data = game_read(&g, &g.ar->files[k], &n);
				r = data ? unpack_to(u, data, n, out) : 3;
				if (r > worst)
					worst = r;
				free(data);
				free(out);
				free(u);
			}
			archive_free(&g.ar->ar);
			return worst;
		}
		if (!npos) {
			archive_free(&g.ar->ar);
			return usage();
		}
		{
			struct afa_entry *e = game_find(&g, pos[0]);
			size_t n = 0;
			uint8_t *data;
			char *out;
			if (!e) {
				fprintf(stderr, "rqmod-aff: the game has no Flash file %s (rqmod-aff list names them)\n", pos[0]);
				archive_free(&g.ar->ar);
				return 1;
			}
			data = game_read(&g, e, &n);
			if (!data) {
				archive_free(&g.ar->ar);
				return 3;
			}
			if (pos[1]) {
				out = strdup(pos[1]);
			} else {
				const char *slash = strrchr(pos[0], '/');
				out = swf_name(slash ? slash + 1 : pos[0]);
			}
			r = unpack_to(pos[0], data, n, out);
			free(out);
			free(data);
		}
		archive_free(&g.ar->ar);
		return r;
	}
}

int main(int argc, char **argv)
{
	char **args = utf8_args(&argc);

	if (!args)
		args = argv;
	SetConsoleOutputCP(CP_UTF8);
	if (argc < 2 || !strcmp(args[1], "--help") || !strcmp(args[1], "-h") || !strcmp(args[1], "help")) {
		fputs(usage_text, stdout);
		return argc < 2 ? 2 : 0;
	}
	if (!strcmp(args[1], "check"))
		return cmd_check(argc - 2, args + 2);
	if (!strcmp(args[1], "unpack"))
		return cmd_unpack(argc - 2, args + 2);
	if (!strcmp(args[1], "build"))
		return cmd_build(argc - 2, args + 2);
	if (!strcmp(args[1], "list"))
		return cmd_list(argc - 2, args + 2);
	fprintf(stderr, "rqmod-aff: no command %s\n\n", args[1]);
	return usage();
}
