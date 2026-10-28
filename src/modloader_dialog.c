/* Native Win32 dialogs for rqmod-ctl. SDL_ShowMessageBox (system.MsgBox,
 * MsgBoxOkCancel, system.Error) blocks the VM thread inside a modal
 * #32770 dialog, so no frame is presented until it closes. These run on
 * the pipe thread and never wait for a frame.
 */

#ifdef RQ_MODLOADER

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "modloader.h"

#define DLG_BTN_MAX  8
#define DLG_LINE_MAX 12
#define DLG_TEXT_LEN 1024
#define DLG_WAIT_MS  4000

struct dlg_btn {
	HWND hwnd;
	int ctrl_id;
	char id[40];
	char label[128];
	RECT rc;
	int enabled;
	int is_default;
};

struct dlg_snap {
	HWND hwnd;
	char title[256];
	char message[DLG_TEXT_LEN];
	struct dlg_btn btn[DLG_BTN_MAX];
	int nbtn;
	HWND focus;
};

static HWND g_hover_btn;

static void w2u(char *dst, int n, const wchar_t *src)
{
	int got;
	dst[0] = 0;
	if (!src || !*src || n <= 0)
		return;
	got = WideCharToMultiByte(CP_UTF8, 0, src, -1, dst, n, NULL, NULL);
	if (got <= 0)
		dst[0] = 0;
	dst[n - 1] = 0;
}

static void strip_amp(char *s)
{
	char *r = s, *w = s;
	for (; *r; r++) {
		if (*r == '&' && r[1] != '&')
			continue;
		*w++ = *r;
	}
	*w = 0;
}

static void slug_id(char *dst, int n, const char *label, int ctrl_id)
{
	int j, dash = 0;
	j = snprintf(dst, n, "dialog-");
	for (; *label && j < n - 1; label++) {
		unsigned char c = (unsigned char)*label;
		if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
			dst[j++] = (char)c;
			dash = 0;
		} else if (c >= 'A' && c <= 'Z') {
			dst[j++] = (char)(c - 'A' + 'a');
			dash = 0;
		} else if (!dash && j > 7) {
			dst[j++] = '-';
			dash = 1;
		}
	}
	while (j > 7 && dst[j - 1] == '-')
		j--;
	dst[j] = 0;
	if (j <= 7)
		snprintf(dst, n, "dialog-b%d", ctrl_id);
}

static BOOL CALLBACK find_top(HWND h, LPARAM lp)
{
	DWORD pid = 0;
	wchar_t cls[64];
	HWND *out = (HWND *)lp;
	GetWindowThreadProcessId(h, &pid);
	if (pid != GetCurrentProcessId() || !IsWindowVisible(h))
		return TRUE;
	if (!GetClassNameW(h, cls, 64) || wcscmp(cls, L"#32770") != 0)
		return TRUE;
	*out = h;
	return FALSE;
}

static BOOL CALLBACK on_child(HWND h, LPARAM lp)
{
	struct dlg_snap *s = (struct dlg_snap *)lp;
	wchar_t cls[64], wtxt[DLG_TEXT_LEN];
	char txt[DLG_TEXT_LEN];
	LONG style;
	if (!IsWindowVisible(h) || !GetClassNameW(h, cls, 64))
		return TRUE;
	style = GetWindowLongW(h, GWL_STYLE);
	wtxt[0] = 0;
	GetWindowTextW(h, wtxt, DLG_TEXT_LEN);
	w2u(txt, (int)sizeof txt, wtxt);
	if (!_wcsicmp(cls, L"Button")) {
		struct dlg_btn *b;
		LONG kind = style & BS_TYPEMASK;
		if (s->nbtn >= DLG_BTN_MAX)
			return TRUE;
		if (kind != BS_PUSHBUTTON && kind != BS_DEFPUSHBUTTON)
			return TRUE;
		b = &s->btn[s->nbtn++];
		memset(b, 0, sizeof *b);
		b->hwnd = h;
		b->ctrl_id = GetDlgCtrlID(h);
		snprintf(b->label, sizeof b->label, "%s", txt);
		strip_amp(b->label);
		slug_id(b->id, (int)sizeof b->id, b->label, b->ctrl_id);
		GetWindowRect(h, &b->rc);
		b->enabled = IsWindowEnabled(h) ? 1 : 0;
		b->is_default = kind == BS_DEFPUSHBUTTON;
		return TRUE;
	}
	if (!_wcsicmp(cls, L"Static")) {
		LONG ss = style & SS_TYPEMASK;
		int len;
		if (ss == SS_ICON || ss == SS_BITMAP || !txt[0])
			return TRUE;
		len = (int)strlen(s->message);
		if (len && len < (int)sizeof s->message - 2) {
			s->message[len++] = '\n';
			s->message[len] = 0;
		}
		snprintf(s->message + len, sizeof s->message - (size_t)len, "%s", txt);
	}
	return TRUE;
}

static int dlg_snap(struct dlg_snap *s)
{
	HWND top = NULL;
	wchar_t wt[256];
	GUITHREADINFO gti;
	DWORD tid;
	memset(s, 0, sizeof *s);
	EnumWindows(find_top, (LPARAM)&top);
	if (!top)
		return 0;
	s->hwnd = top;
	wt[0] = 0;
	GetWindowTextW(top, wt, 256);
	w2u(s->title, (int)sizeof s->title, wt);
	EnumChildWindows(top, on_child, (LPARAM)s);
	tid = GetWindowThreadProcessId(top, NULL);
	memset(&gti, 0, sizeof gti);
	gti.cbSize = sizeof gti;
	if (GetGUIThreadInfo(tid, &gti))
		s->focus = gti.hwndFocus;
	return 1;
}

static struct dlg_btn *dlg_find(struct dlg_snap *s, const char *id)
{
	int i;
	if (!id)
		return NULL;
	for (i = 0; i < s->nbtn; i++) {
		if (!strcmp(s->btn[i].id, id))
			return &s->btn[i];
	}
	return NULL;
}

static struct dlg_btn *dlg_hovered(struct dlg_snap *s)
{
	int i;
	for (i = 0; i < s->nbtn; i++) {
		if (s->focus && s->btn[i].hwnd == s->focus)
			return &s->btn[i];
	}
	return NULL;
}

int rq_dialog_open(void)
{
	struct dlg_snap s;
	return dlg_snap(&s);
}

int rq_dialog_has(const char *id)
{
	struct dlg_snap s;
	if (!id || !*id || !dlg_snap(&s))
		return 0;
	return dlg_find(&s, id) != NULL;
}

static void add_lines(cJSON *arr, const char *msg)
{
	char line[DLG_TEXT_LEN];
	const char *p = msg;
	while (*p) {
		int n = 0;
		while (p[n] && p[n] != '\n' && p[n] != '\r')
			n++;
		if (n > 0) {
			if (n >= (int)sizeof line)
				n = (int)sizeof line - 1;
			memcpy(line, p, (size_t)n);
			line[n] = 0;
			cJSON_AddItemToArray(arr, cJSON_CreateString(line));
		}
		p += n;
		while (*p == '\n' || *p == '\r')
			p++;
	}
}

int rq_dialog_json(char **json_out)
{
	struct dlg_snap s;
	struct dlg_btn *hov;
	cJSON *root, *text, *controls, *dlg;
	char *printed, *out;
	char hwnd_s[32];
	int i;
	if (json_out)
		*json_out = NULL;
	if (!dlg_snap(&s))
		return 0;
	hov = dlg_hovered(&s);
	root = cJSON_CreateObject();
	cJSON_AddStringToObject(root, "screen", "native-dialog");
	text = cJSON_AddArrayToObject(root, "text");
	if (s.title[0])
		cJSON_AddItemToArray(text, cJSON_CreateString(s.title));
	add_lines(text, s.message);
	controls = cJSON_AddArrayToObject(root, "controls");
	for (i = 0; i < s.nbtn; i++) {
		struct dlg_btn *b = &s.btn[i];
		cJSON *c = cJSON_CreateObject();
		cJSON_AddStringToObject(c, "id", b->id);
		cJSON_AddStringToObject(c, "label", b->label);
		cJSON_AddStringToObject(c, "type", "button");
		cJSON_AddStringToObject(c, "section", "native-dialog");
		cJSON_AddNumberToObject(c, "button", b->ctrl_id);
		cJSON_AddNumberToObject(c, "x", b->rc.left);
		cJSON_AddNumberToObject(c, "y", b->rc.top);
		cJSON_AddNumberToObject(c, "w", b->rc.right - b->rc.left);
		cJSON_AddNumberToObject(c, "h", b->rc.bottom - b->rc.top);
		cJSON_AddBoolToObject(c, "enabled", b->enabled);
		cJSON_AddBoolToObject(c, "visible", 1);
		cJSON_AddBoolToObject(c, "default", b->is_default);
		cJSON_AddBoolToObject(c, "native", 1);
		cJSON_AddItemToArray(controls, c);
		if (b->label[0])
			cJSON_AddItemToArray(text, cJSON_CreateString(b->label));
	}
	if (hov)
		cJSON_AddStringToObject(root, "hover", hov->id);
	else
		cJSON_AddNullToObject(root, "hover");
	dlg = cJSON_AddObjectToObject(root, "dialog");
	cJSON_AddStringToObject(dlg, "kind", "win32");
	cJSON_AddStringToObject(dlg, "title", s.title);
	cJSON_AddStringToObject(dlg, "message", s.message);
	cJSON_AddStringToObject(dlg, "coords", "screen");
	snprintf(hwnd_s, sizeof hwnd_s, "0x%llx", (unsigned long long)(ULONG_PTR)s.hwnd);
	cJSON_AddStringToObject(dlg, "hwnd", hwnd_s);
	printed = cJSON_PrintUnformatted(root);
	cJSON_Delete(root);
	if (!printed)
		return 0;
	out = (char *)malloc(strlen(printed) + 2);
	if (!out) {
		free(printed);
		return 0;
	}
	sprintf(out, "%s\n", printed);
	free(printed);
	if (json_out)
		*json_out = out;
	else
		free(out);
	return 1;
}

/* Hover on a native dialog is keyboard focus: that is the button Enter
 * would press, and the one the OK/Cancel box gives the default focus. */
int rq_dialog_hover(const char *id, char *err, int errsz)
{
	struct dlg_snap s;
	struct dlg_btn *b;
	ULONGLONG t0;
	if (!dlg_snap(&s)) {
		if (err)
			snprintf(err, errsz, "no-dialog");
		return 0;
	}
	b = dlg_find(&s, id);
	if (!b) {
		if (err)
			snprintf(err, errsz, "unknown-control");
		return 0;
	}
	if (!b->enabled) {
		if (err)
			snprintf(err, errsz, "disabled");
		return 0;
	}
	if (s.focus == b->hwnd) {
		g_hover_btn = b->hwnd;
		return 1;
	}
	SendMessageTimeoutW(s.hwnd, WM_NEXTDLGCTL, (WPARAM)b->hwnd, TRUE,
			    SMTO_ABORTIFHUNG, 1000, NULL);
	t0 = GetTickCount64();
	while (GetTickCount64() - t0 < 1000) {
		struct dlg_snap now;
		if (!dlg_snap(&now) || now.hwnd != s.hwnd)
			break;
		if (now.focus == b->hwnd) {
			g_hover_btn = b->hwnd;
			return 1;
		}
		Sleep(10);
	}
	if (err)
		snprintf(err, errsz, "timeout");
	return 0;
}

int rq_dialog_press(const char *id, char *err, int errsz)
{
	struct dlg_snap s;
	struct dlg_btn *b, *hov;
	ULONGLONG t0;
	if (!dlg_snap(&s)) {
		if (err)
			snprintf(err, errsz, "no-dialog");
		return 0;
	}
	b = dlg_find(&s, id);
	if (!b) {
		if (err)
			snprintf(err, errsz, "unknown-control");
		return 0;
	}
	hov = dlg_hovered(&s);
	if (!hov || hov->hwnd != b->hwnd) {
		if (err)
			snprintf(err, errsz, "not-hovered");
		return 0;
	}
	rq_log("agent: native dialog '%s' press %s (%d)", s.title, b->label, b->ctrl_id);
	PostMessageW(s.hwnd, WM_COMMAND, MAKEWPARAM(b->ctrl_id, BN_CLICKED), (LPARAM)b->hwnd);
	t0 = GetTickCount64();
	while (GetTickCount64() - t0 < DLG_WAIT_MS) {
		if (!IsWindow(s.hwnd) || !IsWindowVisible(s.hwnd)) {
			g_hover_btn = NULL;
			return 1;
		}
		Sleep(10);
	}
	if (err)
		snprintf(err, errsz, "dialog-still-open");
	return 0;
}

#endif /* RQ_MODLOADER */
