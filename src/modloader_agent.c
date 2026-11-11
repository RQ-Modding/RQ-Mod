/* Named-pipe input agent. Same protocol as rqmod-ctl (\\.\pipe\rqmod).
 * Uses input.c symbols, not 1.0.0 RVAs.
 */

#ifdef RQ_MODLOADER

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "modloader.h"
#include "modloader_priv.h"

#define VK_NR_KEYCODES    0x92
#define PIPE_NAME         "\\\\.\\pipe\\rqmod"

void mouse_force_pos(int x, int y);
void mouse_release_agent(void);
void key_set_state(int code, int down);
int agent_input_active(void);
void mouse_set_wheel(int dir);

static int pulse_ms = 80;

static char *skip_ws(char *s)
{
	while (*s && isspace((unsigned char)*s))
		s++;
	return s;
}

static char *tok(char **ps)
{
	char *s = skip_ws(*ps), *e;
	if (!*s)
		return s;
	e = s;
	while (*e && !isspace((unsigned char)*e))
		e++;
	if (*e) {
		*e = 0;
		*ps = e + 1;
	} else {
		*ps = e;
	}
	return s;
}

int rq_key_from_name(const char *n)
{
	int hex;
	if (!n || !*n)
		return -1;
	if (!_stricmp(n, "enter") || !_stricmp(n, "return"))
		return VK_RETURN;
	if (!_stricmp(n, "esc") || !_stricmp(n, "escape"))
		return VK_ESCAPE;
	if (!_stricmp(n, "space") || !_stricmp(n, "spc"))
		return VK_SPACE;
	if (!_stricmp(n, "tab"))
		return VK_TAB;
	if (!_stricmp(n, "back") || !_stricmp(n, "backspace"))
		return VK_BACK;
	if (!_stricmp(n, "shift"))
		return VK_SHIFT;
	if (!_stricmp(n, "ctrl") || !_stricmp(n, "control"))
		return VK_CONTROL;
	if (!_stricmp(n, "up"))
		return VK_UP;
	if (!_stricmp(n, "down"))
		return VK_DOWN;
	if (!_stricmp(n, "left"))
		return VK_LEFT;
	if (!_stricmp(n, "right"))
		return VK_RIGHT;
	if (!_stricmp(n, "pgup") || !_stricmp(n, "pageup"))
		return VK_PRIOR;
	if (!_stricmp(n, "pgdn") || !_stricmp(n, "pagedown"))
		return VK_NEXT;
	if (!_stricmp(n, "home"))
		return VK_HOME;
	if (!_stricmp(n, "end"))
		return VK_END;
	if (!_stricmp(n, "ins") || !_stricmp(n, "insert"))
		return VK_INSERT;
	if (!_stricmp(n, "del") || !_stricmp(n, "delete"))
		return VK_DELETE;
	if (!_stricmp(n, "z"))
		return 0x5A;
	if (!_stricmp(n, "x"))
		return 0x58;
	if (!_stricmp(n, "c"))
		return 0x43;
	if (!_stricmp(n, "a"))
		return 0x41;
	if (!_stricmp(n, "s"))
		return 0x53;
	if (!_stricmp(n, "leftclick") || !_stricmp(n, "lbutton") || !_stricmp(n, "lmb"))
		return VK_LBUTTON;
	if (!_stricmp(n, "rightclick") || !_stricmp(n, "rbutton") || !_stricmp(n, "rmb"))
		return VK_RBUTTON;
	if (n[0] == '0' && (n[1] == 'x' || n[1] == 'X')) {
		hex = (int)strtol(n, NULL, 16);
		if (hex > 0 && hex < VK_NR_KEYCODES)
			return hex;
	}
	if (!_stricmp(n, "+") || !_stricmp(n, "plus") || !_stricmp(n, "add"))
		return VK_ADD;
	if (!_stricmp(n, "-") || !_stricmp(n, "minus") || !_stricmp(n, "subtract"))
		return VK_SUBTRACT;
	if ((n[0] == 'f' || n[0] == 'F') && n[1] >= '1' && n[1] <= '9') {
		int fn = 0;
		if (!n[2])
			fn = n[1] - '0';
		else if (n[1] == '1' && n[2] >= '0' && n[2] <= '2' && !n[3])
			fn = 10 + (n[2] - '0');
		if (fn >= 1 && fn <= 12)
			return VK_F1 + (fn - 1);
	}
	if (n[0] && !n[1]) {
		char c = n[0];
		if (c >= 'a' && c <= 'z')
			return 0x41 + (c - 'a');
		if (c >= 'A' && c <= 'Z')
			return 0x41 + (c - 'A');
		if (c >= '0' && c <= '9')
			return 0x30 + (c - '0');
	}
	return -1;
}

static int mouse_btn(const char *n)
{
	if (!n || !_stricmp(n, "left") || !_stricmp(n, "l") || !_stricmp(n, "lmb"))
		return VK_LBUTTON;
	if (!_stricmp(n, "right") || !_stricmp(n, "r") || !_stricmp(n, "rmb"))
		return VK_RBUTTON;
	if (!_stricmp(n, "middle") || !_stricmp(n, "m") || !_stricmp(n, "mmb"))
		return VK_MBUTTON;
	return -1;
}

static void set_key(int code, int down)
{
	if (code <= 0 || code >= VK_NR_KEYCODES)
		return;
	key_set_state(code, down);
}

static void pulse_key(int code)
{
	set_key(code, 1);
	Sleep((DWORD)pulse_ms);
	set_key(code, 0);
}

/* While paused the VM reads no input, so these would hang or land later. */
static int is_input_cmd(const char *cmd)
{
	static const char *const names[] = {
		"goto", "step", "hover", "press", "drag", "key", "hold", "down",
		"release", "up", "move", "click", "wheel", "type"
	};
	size_t i;
	for (i = 0; i < sizeof names / sizeof names[0]; i++) {
		if (!_stricmp(cmd, names[i]))
			return 1;
	}
	return 0;
}

static int rq_agent_do(const char *line_in, char *err, int errsz, char **extra)
{
	char buf[512], *p, *cmd, *a, *b, *c;
	int code, x, y, btn;

	if (extra)
		*extra = NULL;
	if (err && errsz)
		err[0] = 0;
	if (!line_in)
		return 0;
	strncpy(buf, line_in, sizeof buf - 1);
	buf[sizeof buf - 1] = 0;
	p = buf;
	cmd = tok(&p);
	if (!*cmd || cmd[0] == '#')
		return 1;

	if (!_stricmp(cmd, "ping") || !_stricmp(cmd, "ok"))
		return 1;
	if (!_stricmp(cmd, "crash")) {
		rq_crash_test(); /* only does anything with env RQ_DEBUG_CRASH */
		return 1;
	}
	if (!_stricmp(cmd, "where")) {
		char path[RQ_PATH_MAX];
		if (!rq_crash_where(path, sizeof path)) {
			if (err)
				snprintf(err, errsz, "where: no game thread or logs folder");
			return 0;
		}
		if (extra) {
			*extra = malloc(strlen(path) + 2);
			if (*extra)
				sprintf(*extra, "%s\n", path);
		}
		return 1;
	}
	if (!_stricmp(cmd, "ungrab")) {
		mouse_release_agent();
		return 1;
	}
	if (!_stricmp(cmd, "pause")) {
		rq_ui_set_paused(1);
		return 1;
	}
	if (!_stricmp(cmd, "resume")) {
		rq_ui_set_paused(0);
		return 1;
	}
	if (!_stricmp(cmd, "shot"))
		return rq_ui_cmd_shot(skip_ws(p), err, errsz);
	if (!_stricmp(cmd, "scene")) {
		char *json = rq_sprite_scene_json();
		if (!json) {
			if (err)
				snprintf(err, errsz, "scene: no list");
			return 0;
		}
		if (extra)
			*extra = json;
		else
			free(json);
		return 1;
	}
	if (!_stricmp(cmd, "apidump")) {
		char *json = rq_api_dump_json();
		if (!json) {
			if (err)
				snprintf(err, errsz, "apidump: not built yet");
			return 0;
		}
		if (extra)
			*extra = json;
		else
			free(json);
		return 1;
	}
	if (!_stricmp(cmd, "cgdump")) {
		char *json = NULL;
		if (!rq_ui_cmd_cgdump(skip_ws(p), &json, err, errsz))
			return 0;
		if (extra)
			*extra = json;
		else
			free(json);
		return 1;
	}
	if (!_stricmp(cmd, "flashdump")) {
		char *json = NULL;
		if (!rq_ui_cmd_flashdump(skip_ws(p), &json, err, errsz))
			return 0;
		if (extra)
			*extra = json;
		else
			free(json);
		return 1;
	}
	if (rq_ui_paused() && is_input_cmd(cmd)) {
		if (err)
			snprintf(err, errsz, "paused");
		return 0;
	}
	if (!_stricmp(cmd, "grid")) {
		char *json = NULL;
		if (!rq_path_grid(&json, 0, err, errsz))
			return 0;
		if (extra)
			*extra = json;
		else
			free(json);
		return 1;
	}
	if (!_stricmp(cmd, "goto")) {
		char *json = NULL;
		int gx, gz, pick;
		a = tok(&p);
		if (!_stricmp(a, "portal")) {
			pick = 1;
			gx = gz = 0;
		} else {
			b = tok(&p);
			if (!*a || !*b) {
				if (err)
					snprintf(err, errsz, "usage: goto X Z | goto portal");
				return 0;
			}
			pick = 0;
			gx = atoi(a);
			gz = atoi(b);
		}
		if (!rq_path_goto(gx, gz, pick, &json, err, errsz))
			return 0;
		if (extra)
			*extra = json;
		else
			free(json);
		return 1;
	}
	if (!_stricmp(cmd, "step")) {
		char *json = NULL;
		int dx = 0, dz = 0;
		a = tok(&p);
		if (!_stricmp(a, "up") || !_stricmp(a, "north"))
			dz = 1;
		else if (!_stricmp(a, "down") || !_stricmp(a, "south"))
			dz = -1;
		else if (!_stricmp(a, "right") || !_stricmp(a, "east"))
			dx = 1;
		else if (!_stricmp(a, "left") || !_stricmp(a, "west"))
			dx = -1;
		else {
			if (err)
				snprintf(err, errsz, "usage: step up|down|left|right");
			return 0;
		}
		if (!rq_path_step(dx, dz, &json, err, errsz))
			return 0;
		if (extra)
			*extra = json;
		else
			free(json);
		return 1;
	}

	if (!_stricmp(cmd, "ui") || !_stricmp(cmd, "screen")) {
		char *json = NULL;
		if (!rq_ui_cmd_ui(&json, err, errsz))
			return 0;
		if (extra)
			*extra = json;
		else
			free(json);
		return 1;
	}
	if (!_stricmp(cmd, "facts")) {
		char *json = NULL;
		if (!rq_query_facts(&json)) {
			if (err)
				snprintf(err, errsz, "facts");
			return 0;
		}
		if (extra)
			*extra = json;
		else
			free(json);
		return 1;
	}
	if (!_stricmp(cmd, "agentstate")) {
		/* 1 while pipe input owns the mouse; a player's Space must leave it at 0 */
		if (extra) {
			*extra = strdup(agent_input_active() ? "agent_input=1" : "agent_input=0");
		}
		return 1;
	}
	if (!_stricmp(cmd, "mods")) {
		char *json = NULL;
		if (!rq_mods_json(&json)) {
			if (err)
				snprintf(err, errsz, "mods");
			return 0;
		}
		if (extra)
			*extra = json;
		else
			free(json);
		return 1;
	}
	if (!_stricmp(cmd, "dialog")) {
		char *json = NULL;
		if (!rq_dialog_json(&json)) {
			if (err)
				snprintf(err, errsz, "no-dialog");
			return 0;
		}
		if (extra)
			*extra = json;
		else
			free(json);
		return 1;
	}
	if (!_stricmp(cmd, "hover")) {
		a = tok(&p);
		return rq_ui_cmd_hover(a, err, errsz);
	}
	if (!_stricmp(cmd, "press")) {
		a = tok(&p);
		if (rq_dialog_has(a) || rq_ui_has_control(a))
			return rq_ui_cmd_press(a, err, errsz);
		code = rq_key_from_name(a);
		if (code < 0) {
			if (err)
				snprintf(err, errsz, "unknown-control");
			return 0;
		}
		pulse_key(code);
		return 1;
	}
	if (!_stricmp(cmd, "drag")) {
		a = tok(&p);
		b = tok(&p);
		if (!*a || !*b) {
			if (err)
				snprintf(err, errsz, "usage: drag SRC DST");
			return 0;
		}
		return rq_ui_cmd_drag(a, b, err, errsz);
	}
	if (!_stricmp(cmd, "key")) {
		a = tok(&p);
		code = rq_key_from_name(a);
		if (code < 0) {
			if (err)
				snprintf(err, errsz, "unknown key '%s'", a);
			return 0;
		}
		pulse_key(code);
		return 1;
	}
	if (!_stricmp(cmd, "hold") || !_stricmp(cmd, "down")) {
		a = tok(&p);
		btn = mouse_btn(a);
		if (btn > 0) {
			set_key(btn, 1);
			return 1;
		}
		code = rq_key_from_name(a);
		if (code < 0) {
			if (err)
				snprintf(err, errsz, "unknown key '%s'", a);
			return 0;
		}
		set_key(code, 1);
		return 1;
	}
	if (!_stricmp(cmd, "release") || !_stricmp(cmd, "up")) {
		a = tok(&p);
		btn = mouse_btn(a);
		if (btn > 0) {
			set_key(btn, 0);
			return 1;
		}
		code = rq_key_from_name(a);
		if (code < 0) {
			if (err)
				snprintf(err, errsz, "unknown key '%s'", a);
			return 0;
		}
		set_key(code, 0);
		return 1;
	}
	if (!_stricmp(cmd, "move")) {
		a = tok(&p);
		b = tok(&p);
		if (!*a || !*b) {
			if (err)
				snprintf(err, errsz, "usage: move X Y");
			return 0;
		}
		x = atoi(a);
		y = atoi(b);
		mouse_force_pos(x, y);
		return 1;
	}
	if (!_stricmp(cmd, "click")) {
		a = tok(&p);
		b = tok(&p);
		c = tok(&p);
		btn = VK_LBUTTON;
		if (mouse_btn(a) > 0 && *b && *c) {
			btn = mouse_btn(a);
			x = atoi(b);
			y = atoi(c);
		} else if (*a && *b) {
			x = atoi(a);
			y = atoi(b);
		} else {
			if (err)
				snprintf(err, errsz, "usage: click [left|right] X Y");
			return 0;
		}
		mouse_force_pos(x, y);
		Sleep(16);
		set_key(btn, 1);
		Sleep((DWORD)pulse_ms);
		set_key(btn, 0);
		return 1;
	}
	if (!_stricmp(cmd, "wheel")) {
		a = tok(&p);
		mouse_set_wheel(a && *a ? atoi(a) : 1);
		return 1;
	}
	if (!_stricmp(cmd, "type")) {
		a = skip_ws(p);
		/* While the HUD's text input is on (2.29.0) the text goes to it, any character the
		 * game's font draws; otherwise the keys of letters, digits and space are pulsed. */
		if (rq_hud_text_on()) {
			rq_hud_text_queue(a);
			return 1;
		}
		for (; *a; a++) {
			unsigned char ch = (unsigned char)*a;
			if (ch == ' ')
				code = VK_SPACE;
			else if (ch >= 'a' && ch <= 'z')
				code = 0x41 + (ch - 'a');
			else if (ch >= 'A' && ch <= 'Z')
				code = 0x41 + (ch - 'A');
			else if (ch >= '0' && ch <= '9')
				code = 0x30 + (ch - '0');
			else
				continue;
			pulse_key(code);
			Sleep(20);
		}
		return 1;
	}
	if (!_stricmp(cmd, "pulse")) {
		a = tok(&p);
		if (*a)
			pulse_ms = atoi(a);
		if (pulse_ms < 16)
			pulse_ms = 16;
		if (pulse_ms > 1000)
			pulse_ms = 1000;
		return 1;
	}
	if (err)
		snprintf(err, errsz, "unknown cmd");
	return 0;
}

static DWORD WINAPI pipe_thread(LPVOID unused)
{
	(void)unused;
	for (;;) {
		HANDLE h;
		char in[512], out[256], err[128];
		char *extra;
		DWORD n, w;
		h = CreateNamedPipeA(PIPE_NAME, PIPE_ACCESS_DUPLEX,
				     PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
				     PIPE_UNLIMITED_INSTANCES, 65536, 65536, 0, NULL);
		if (h == INVALID_HANDLE_VALUE) {
			Sleep(500);
			continue;
		}
		if (!ConnectNamedPipe(h, NULL) && GetLastError() != ERROR_PIPE_CONNECTED) {
			CloseHandle(h);
			continue;
		}
		n = 0;
		memset(in, 0, sizeof in);
		if (ReadFile(h, in, sizeof in - 1, &n, NULL) && n > 0) {
			char *nl;
			in[n] = 0;
			nl = in;
			while (*nl) {
				char *end = nl;
				while (*end && *end != '\n' && *end != '\r')
					end++;
				if (*end)
					*end++ = 0;
				while (*end == '\n' || *end == '\r')
					end++;
				extra = NULL;
				if (rq_agent_do(nl, err, (int)sizeof err, &extra)) {
					if (extra) {
						DWORD off = 0, len = (DWORD)strlen(extra);
						while (off < len) {
							if (!WriteFile(h, extra + off, len - off, &w, NULL) || !w)
								break;
							off += w;
						}
						free(extra);
					} else {
						snprintf(out, sizeof out, "ok\n");
						WriteFile(h, out, (DWORD)strlen(out), &w, NULL);
					}
				} else {
					snprintf(out, sizeof out, "err %s\n", err[0] ? err : "fail");
					WriteFile(h, out, (DWORD)strlen(out), &w, NULL);
					free(extra);
				}
				nl = end;
			}
		}
		FlushFileBuffers(h);
		DisconnectNamedPipe(h);
		CloseHandle(h);
	}
	return 0;
}

void rq_mods_agent_start(void)
{
	HANDLE th;
	rq_ui_init();
	th = CreateThread(NULL, 0, pipe_thread, NULL, 0, NULL);
	if (!th) {
		rq_log("agent: pipe thread failed");
		return;
	}
	CloseHandle(th);
	rq_log("agent: pipe %s  —  rqmod-ctl ui | mods | hover ID | press ID | drag SRC DST | dialog", PIPE_NAME);
}

#endif /* RQ_MODLOADER */
