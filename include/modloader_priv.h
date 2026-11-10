#ifndef RQ_MODLOADER_PRIV_H
#define RQ_MODLOADER_PRIV_H

#include <stdio.h>

#define RQ_PATH_MAX 1024

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
