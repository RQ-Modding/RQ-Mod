/*
** Lua core, libraries, and interpreter in a single file.
** Compiling just this file generates a complete Lua stand-alone
** program:
**
** $ gcc -O2 -std=c99 -o lua onelua.c -lm
**
** or
**
** $ gcc -O2 -std=c89 -DLUA_USE_C89 -o lua onelua.c -lm
**
*/

/* default is to build the full interpreter */
#ifndef MAKE_LIB
#ifndef MAKE_LUAC
#ifndef MAKE_LUA
#define MAKE_LUA
#endif
#endif
#endif


/*
** Choose suitable platform-specific features. Default is no
** platform-specific features. Some of these options may need extra
** libraries such as -ldl -lreadline -lncurses
*/
#if 0
#define LUA_USE_LINUX
#define LUA_USE_MACOSX
#define LUA_USE_POSIX
#define LUA_ANSI
#endif


/* no need to change anything below this line ----------------------------- */

#include "lprefix.h"

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <float.h>
#include <limits.h>
#include <locale.h>
#include <math.h>
#include <setjmp.h>
#include <signal.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>


/* setup for luaconf.h */
#define LUA_CORE
#define LUA_LIB
#define ltable_c
#define lvm_c
#include "luaconf.h"

/* do not export internal symbols */
#undef LUAI_FUNC
#undef LUAI_DDEC
#undef LUAI_DDEF
#define LUAI_FUNC	static
#define LUAI_DDEC(def)	/* empty */
#define LUAI_DDEF	static

/* core -- used by all */
#include "lzio.c"
#include "lctype.c"
#include "lopcodes.c"
#include "lmem.c"
#include "lundump.c"
#include "ldump.c"
#include "lstate.c"
#include "lgc.c"
#include "llex.c"
#include "lcode.c"
#include "lparser.c"
#include "ldebug.c"
#include "lfunc.c"
#include "lobject.c"
#include "ltm.c"
#include "lstring.c"
#include "ltable.c"
#include "ldo.c"
#include "lvm.c"
#include "lapi.c"

/*
** RQ MOD (RQ-PATCHES.md, 2): on Windows, the file names that the libraries
** below open, remove and rename are UTF-8, as the engine's are. The C
** library's own calls take the system's code page, which cannot name every
** folder. A name that is not UTF-8 is read in the code page, as they read it.
*/
#if defined(_WIN32) && !defined(MAKE_LUAC)
#include <wchar.h>
__declspec(dllimport) int __stdcall MultiByteToWideChar(unsigned int, unsigned long,
                                                        const char *, int, wchar_t *, int);

/*
** s in wide characters: read as UTF-8, or in the system's code page (as the
** C library reads a name) when it is not UTF-8. NULL when out of memory.
*/
static wchar_t *rq_wide (const char *s) {
  unsigned int cp = 65001;  /* CP_UTF8 */
  unsigned long flags = 8;  /* MB_ERR_INVALID_CHARS */
  int n = MultiByteToWideChar(cp, flags, s, -1, NULL, 0);
  wchar_t *w;
  if (n <= 0) {
    cp = 0;  /* CP_ACP */
    flags = 0;
    n = MultiByteToWideChar(cp, flags, s, -1, NULL, 0);
    if (n <= 0)
      return NULL;
  }
  w = (wchar_t *)malloc(sizeof(wchar_t) * (size_t)n);
  if (w != NULL && MultiByteToWideChar(cp, flags, s, -1, w, n) <= 0) {
    free(w);
    w = NULL;
  }
  return w;
}

static FILE *rq_fopen (const char *name, const char *mode) {
  wchar_t *w = rq_wide(name), *m = rq_wide(mode);
  FILE *f = NULL;
  int e = ENOMEM;
  if (w != NULL && m != NULL) {
    f = _wfopen(w, m);
    e = errno;
  }
  free(w);
  free(m);
  errno = e;
  return f;
}

static FILE *rq_freopen (const char *name, const char *mode, FILE *old) {
  wchar_t *w = rq_wide(name), *m = rq_wide(mode);
  FILE *f = NULL;
  int e = ENOMEM;
  if (w != NULL && m != NULL) {
    f = _wfreopen(w, m, old);
    e = errno;
  }
  else
    fclose(old);  /* as freopen does when it fails */
  free(w);
  free(m);
  errno = e;
  return f;
}

static int rq_remove (const char *name) {
  wchar_t *w = rq_wide(name);
  int r = -1, e = ENOMEM;
  if (w != NULL) {
    r = _wremove(w);
    e = errno;
  }
  free(w);
  errno = e;
  return r;
}

static int rq_rename (const char *from, const char *to) {
  wchar_t *a = rq_wide(from), *b = rq_wide(to);
  int r = -1, e = ENOMEM;
  if (a != NULL && b != NULL) {
    r = _wrename(a, b);
    e = errno;
  }
  free(a);
  free(b);
  errno = e;
  return r;
}

#undef fopen
#undef freopen
#undef remove
#undef rename
#define fopen rq_fopen
#define freopen rq_freopen
#define remove rq_remove
#define rename rq_rename
#endif

/* auxiliary library -- used by all */
#include "lauxlib.c"

/* standard library  -- not used by luac */
#ifndef MAKE_LUAC
#include "lbaselib.c"
#include "lcorolib.c"
#include "ldblib.c"
#include "liolib.c"
#include "lmathlib.c"
#include "loadlib.c"
#include "loslib.c"
#include "lstrlib.c"
#include "ltablib.c"
#include "lutf8lib.c"
#include "linit.c"
#endif

/* lua */
#ifdef MAKE_LUA
#include "lua.c"
#endif

/* luac */
#ifdef MAKE_LUAC
#include "luac.c"
#endif
