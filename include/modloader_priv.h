#ifndef RQ_MODLOADER_PRIV_H
#define RQ_MODLOADER_PRIV_H

#include <stdio.h>

#define RQ_PATH_MAX 1024

/* The loader's paths are UTF-8, as libsys4's and Lua's are (2.64.0: they were in the system's
 * code page, so a folder named outside ASCII broke rq.ui.shot, a mod's music and rq.store, and one
 * outside the code page broke every mod). rq_fopen opens one through Windows' wide calls, with its
 * buffers on the stack (the crash report uses it); a name that is not UTF-8 goes to fopen as it
 * is. */
FILE *rq_fopen(const char *path, const char *mode);
#ifdef _WIN32
#include <wchar.h>
/* path in UTF-8 into out, n wide characters; 0 when it is not UTF-8 (GetLastError gives
 * ERROR_NO_UNICODE_TRANSLATION) or does not fit. */
int rq_wpath(const char *path, wchar_t *out, int n);
/* GetFileAttributesW's answer: 0xFFFFFFFF (INVALID_FILE_ATTRIBUTES) when there is no such path. */
unsigned long rq_file_attrs(const char *path);
/* MoveFileExW with its flags; nonzero when the file moved. */
int rq_move_file(const char *from, const char *to, unsigned long flags);
/* A wide name from Windows (a folder listing's, a module's) in UTF-8; 0 when it does not fit. */
int rq_utf8_name(const wchar_t *name, char *out, int n);
#endif

extern char g_rq_game_dir[RQ_PATH_MAX];
extern char g_rq_mods_dir[RQ_PATH_MAX];
extern char g_rq_logs_dir[RQ_PATH_MAX]; /* "logs" beside mods: rqmod.log, crash reports */
extern char g_rq_exe_dir[RQ_PATH_MAX];
extern FILE *g_rq_log;
extern int g_rq_verbose;
extern int g_rq_terminal;
extern int g_rq_skip_sdl;

void rq_log(const char *fmt, ...);
void rq_log_tag(const char *tag, const char *fmt, ...);

/* rq.ain.stack (2.27.0), in modloader_crash.c beside the crash report's AIN stack. */
struct lua_State;
int rq_lua_ain_stack(struct lua_State *L);

#endif
