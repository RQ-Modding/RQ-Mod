/* Win32-only extras: the title bar's filter menu, with a mark on the filter in use. Isolated so
 * windows.h never collides with xsystem4 Rectangle / ERROR / VK_* macros. */

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <SDL.h>
#include <SDL_syswm.h>

#include "xsystem4.h"
#include "win32_ui.h"

#define RQ_FILTER_MENU_BASE 0xE100
#define RQ_FILTER_COUNT 5
#define RQ_SHARP_3D_MENU 0xE110

/* The window's system menu once the filters are in it. */
static HMENU filter_menu;

/* Windows sends WM_INITMENU, which SDL's hook never sees (it gets posted messages only), so the
 * mark moves when the filter changes, wherever it is changed: here, Config or the game's Window tab. */
void win32_ui_mark_scaling(int type)
{
	if (!filter_menu || type < 0 || type >= RQ_FILTER_COUNT)
		return;
	CheckMenuRadioItem(filter_menu, RQ_FILTER_MENU_BASE, RQ_FILTER_MENU_BASE + RQ_FILTER_COUNT - 1,
			   RQ_FILTER_MENU_BASE + type, MF_BYCOMMAND);
}

void win32_ui_mark_sharp_3d(int on)
{
	if (filter_menu)
		CheckMenuItem(filter_menu, RQ_SHARP_3D_MENU, MF_BYCOMMAND | (on ? MF_CHECKED : MF_UNCHECKED));
}

static void SDLCALL win_msg_hook(void *userdata, void *hWnd, unsigned int message,
				 Uint64 wParam, Sint64 lParam)
{
	(void)userdata;
	(void)hWnd;
	(void)lParam;
	if (message != WM_SYSCOMMAND)
		return;
	unsigned cmd = (unsigned)wParam;
	if (cmd >= RQ_FILTER_MENU_BASE && cmd < RQ_FILTER_MENU_BASE + RQ_FILTER_COUNT)
		rq_set_scaling((int)(cmd - RQ_FILTER_MENU_BASE));
	else if (cmd == RQ_SHARP_3D_MENU)
		rq_set_sharp_3d(!rq_get_sharp_3d());
}

void win32_ui_init(SDL_Window *window)
{
	if (!window)
		return;
	SDL_SysWMinfo info;
	SDL_VERSION(&info.version);
	if (SDL_GetWindowWMInfo(window, &info)) {
		HMENU sys = GetSystemMenu(info.info.win.window, FALSE);
		if (sys) {
			AppendMenuW(sys, MF_SEPARATOR, 0, NULL);
			AppendMenuW(sys, MF_STRING, RQ_FILTER_MENU_BASE + 0,
				    L"Filter: Standard (bilinear)");
			AppendMenuW(sys, MF_STRING, RQ_FILTER_MENU_BASE + 1,
				    L"Filter: Bicubic");
			AppendMenuW(sys, MF_STRING, RQ_FILTER_MENU_BASE + 2,
				    L"Filter: Nearest (pixel)");
			AppendMenuW(sys, MF_STRING, RQ_FILTER_MENU_BASE + 3,
				    L"Filter: Lanczos");
			AppendMenuW(sys, MF_STRING, RQ_FILTER_MENU_BASE + 4,
				    L"Filter: xBRZ");
			AppendMenuW(sys, MF_STRING, RQ_SHARP_3D_MENU,
				    L"3D at the window's size");
			filter_menu = sys;
			win32_ui_mark_scaling(rq_get_scaling());
			win32_ui_mark_sharp_3d(rq_get_sharp_3d());
		}
	}
	SDL_SetWindowsMessageHook(win_msg_hook, NULL);
}

void rq_focus_sdl_window(SDL_Window *window)
{
	HWND hwnd, fg;
	DWORD fg_tid, our_tid;
	SDL_SysWMinfo info;
	if (!window)
		return;
	SDL_RaiseWindow(window);
	SDL_VERSION(&info.version);
	if (!SDL_GetWindowWMInfo(window, &info))
		return;
	hwnd = info.info.win.window;
	if (!hwnd)
		return;
	fg = GetForegroundWindow();
	fg_tid = GetWindowThreadProcessId(fg, NULL);
	our_tid = GetCurrentThreadId();
	if (fg_tid && fg_tid != our_tid)
		AttachThreadInput(our_tid, fg_tid, TRUE);
	BringWindowToTop(hwnd);
	SetForegroundWindow(hwnd);
	SetFocus(hwnd);
	if (fg_tid && fg_tid != our_tid)
		AttachThreadInput(our_tid, fg_tid, FALSE);
}

#else

#include <SDL.h>

void win32_ui_init(SDL_Window *window)
{
	(void)window;
}

void win32_ui_mark_scaling(int type)
{
	(void)type;
}

void win32_ui_mark_sharp_3d(int on)
{
	(void)on;
}

void rq_focus_sdl_window(SDL_Window *window)
{
	if (window)
		SDL_RaiseWindow(window);
}

#endif
