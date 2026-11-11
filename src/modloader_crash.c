/* Copyright (C) 2026 Rance Quest modding fork
 *
 * Crash reports. A hard crash used to leave rqmod.log with nothing useful and
 * the next launch overwrote it. This file keeps what is needed to find the
 * cause afterwards:
 *   - a ring of the last log lines and of the last calls the loader made into
 *     the game's AIN code (function and `this` page),
 *   - a handler for a Windows exception, an abort and a VM error that writes
 *     rqmod-crash-<time>.log next to rqmod.log: what happened, where (module
 *     and offset, with the symbol when it can be found), the AIN call stack,
 *     those rings, and a raw scan of the stack for return addresses,
 *   - rqmod.log rotation (the last run stays as rqmod.prev.log) and a flag file
 *     so a run that ended without exiting cleanly is reported on the next start.
 */

#ifdef RQ_MODLOADER

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <time.h>
#include <signal.h>

#ifdef _WIN32
#include <windows.h>
#endif

#define VM_PRIVATE
#include "lua.h"
#include "lauxlib.h"
#include "system4.h"
#include "system4/ain.h"
#include "system4/utfsjis.h"

#include "modloader.h"
#include "modloader_priv.h"
#include "vm.h"

#define RING_LINES 200
#define LINE_LEN 200
#define CALL_RING 128

static char g_lines[RING_LINES][LINE_LEN];
static unsigned g_nlines;

struct call_note {
	int fno;
	int struct_page;
	int depth;
	unsigned tick;
};
static struct call_note g_calls[CALL_RING];
static unsigned g_ncalls;

static char g_crash_dir[RQ_PATH_MAX];
static char g_flag_path[RQ_PATH_MAX];
static volatile int g_in_report;
static int g_installed;
#ifdef _WIN32
static DWORD g_main_thread;
#endif

void rq_crash_note_log(const char *line)
{
	char *dst = g_lines[g_nlines % RING_LINES];
	size_t n = line ? strlen(line) : 0;

	if (n >= LINE_LEN)
		n = LINE_LEN - 1;
	if (n)
		memcpy(dst, line, n);
	dst[n] = 0;
	g_nlines++;
}

/* Every call the loader makes into the game, newest overwrites oldest. */
void rq_crash_note_call(int fno, int struct_page)
{
	struct call_note *c = &g_calls[g_ncalls % CALL_RING];

	c->fno = fno;
	c->struct_page = struct_page;
	c->depth = call_stack_ptr;
	c->tick = (unsigned)clock();
	g_ncalls++;
}

/* A marker in the log ring only (not the log file): "lua:on_physical 20 1". */
void rq_crash_crumb(const char *what, int a, int b)
{
	char buf[LINE_LEN];

	snprintf(buf, sizeof buf, "crumb %s %d %d", what ? what : "?", a, b);
	rq_crash_note_log(buf);
}

static const char *fn_name(int fno, char *buf, size_t n)
{
	char *utf = NULL;

	buf[0] = 0;
	if (ain && fno >= 0 && fno < ain->nr_functions && ain->functions[fno].name)
		utf = sjis2utf(ain->functions[fno].name, 0);
	if (utf) {
		snprintf(buf, n, "%s", utf);
		free(utf);
	}
	return buf;
}

static void write_ain_stack(FILE *f)
{
	int i, shown = 0;
	char nm[256];

	fprintf(f, "AIN call stack (%d frames, innermost first):\n", (int)call_stack_ptr);
	if (call_stack_ptr < 0 || call_stack_ptr > 4096)
		return;
	for (i = call_stack_ptr - 1; i >= 0 && shown < 48; i--, shown++) {
		uint32_t addr = (i == call_stack_ptr - 1) ? (uint32_t)instr_ptr
			: call_stack[i + 1].call_address;
		fprintf(f, "  #%d fno=%d this=%d addr=0x%08X ", call_stack_ptr - 1 - i,
			(int)call_stack[i].fno, (int)call_stack[i].struct_page, addr);
		fflush(f);
		fprintf(f, "%s\n", fn_name(call_stack[i].fno, nm, sizeof nm));
	}
}

void rq_ain_stack_line(char *buf, size_t n, int max)
{
	char nm[256];
	size_t used = 0;
	int shown = 0;
	if (!n)
		return;
	buf[0] = 0;
	if (!ain || call_stack_ptr <= 0 || call_stack_ptr > 4096) {
		snprintf(buf, n, "(none)");
		return;
	}
	for (int i = call_stack_ptr - 1; i >= 0 && shown < max && used + 1 < n; i--, shown++) {
		int w = snprintf(buf + used, n - used, "%s%s", shown ? " < " : "",
				 fn_name(call_stack[i].fno, nm, sizeof nm));
		if (w < 0)
			break;
		used += (size_t)w;
	}
	if (call_stack_ptr > max && used + 6 < n)
		snprintf(buf + used, n - used, " < ...");
}

/* rq.ain.stack(max) (2.27.0): the AIN functions on the game's call stack, innermost first, as
 * names ("T_Result@success" for a method). The game thread only: hooks, rq.on_frame, mod code. */
int rq_lua_ain_stack(lua_State *L)
{
	int max = (int)luaL_optinteger(L, 1, 32);
	int n = 0;
	if (max < 1)
		max = 1;
	lua_newtable(L);
	if (!ain || call_stack_ptr <= 0 || call_stack_ptr > 4096)
		return 1;
	for (int i = call_stack_ptr - 1; i >= 0 && n < max; i--) {
		int fno = call_stack[i].fno;
		if (fno < 0 || fno >= ain->nr_functions || !ain->functions[fno].name)
			continue;
		char *utf = sjis2utf(ain->functions[fno].name, 0);
		if (!utf)
			continue;
		lua_pushstring(L, utf);
		free(utf);
		lua_rawseti(L, -2, ++n);
	}
	return 1;
}

static void write_rings(FILE *f)
{
	unsigned i, n, start;
	char nm[256];

	fprintf(f, "Last calls the loader made into AIN (oldest first):\n");
	n = g_ncalls < CALL_RING ? g_ncalls : CALL_RING;
	start = g_ncalls - n;
	for (i = 0; i < n; i++) {
		struct call_note *c = &g_calls[(start + i) % CALL_RING];
		fprintf(f, "  fno=%d this=%d depth=%d %s\n", c->fno, c->struct_page,
			c->depth, fn_name(c->fno, nm, sizeof nm));
	}
	fprintf(f, "Last log lines (oldest first):\n");
	n = g_nlines < RING_LINES ? g_nlines : RING_LINES;
	start = g_nlines - n;
	for (i = 0; i < n; i++)
		fprintf(f, "  %s\n", g_lines[(start + i) % RING_LINES]);
}

static void write_header(FILE *f, const char *why)
{
	char now[32];

	rq_format_sp_now(now, sizeof now);
	fprintf(f, "==== rqmod crash report ====\n");
	fprintf(f, "when: %s (America/Sao_Paulo)\n", now);
	fprintf(f, "build: %s   loader %s\n", rq_build_stamp(), RQ_VERSION);
	fprintf(f, "what: %s\n", why);
#ifdef _WIN32
	fprintf(f, "thread: %lu (game thread %lu)\n", (unsigned long)GetCurrentThreadId(),
		(unsigned long)g_main_thread);
#endif
	if (ain)
		fprintf(f, "instr_ptr=0x%zX call_stack_ptr=%d\n", instr_ptr, (int)call_stack_ptr);
	fflush(f);
}

#ifdef _WIN32

static const char *exc_name(DWORD code)
{
	switch (code) {
	case EXCEPTION_ACCESS_VIOLATION: return "ACCESS_VIOLATION";
	case EXCEPTION_STACK_OVERFLOW: return "STACK_OVERFLOW";
	case EXCEPTION_ILLEGAL_INSTRUCTION: return "ILLEGAL_INSTRUCTION";
	case EXCEPTION_INT_DIVIDE_BY_ZERO: return "INT_DIVIDE_BY_ZERO";
	case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: return "ARRAY_BOUNDS_EXCEEDED";
	case EXCEPTION_PRIV_INSTRUCTION: return "PRIV_INSTRUCTION";
	case EXCEPTION_IN_PAGE_ERROR: return "IN_PAGE_ERROR";
	case EXCEPTION_DATATYPE_MISALIGNMENT: return "DATATYPE_MISALIGNMENT";
	default: return "UNKNOWN";
	}
}

/* xsystem4.sym sits next to the exe: "RVA name" per line, ascending, written by
 * build-engine.ps1 from nm. Its first line names the exe size it belongs to, so
 * a stale file is ignored instead of giving wrong names. */
static int sym_lookup(uint64_t rva, char *name, size_t n, uint64_t *off)
{
	char path[MAX_PATH], line[512];
	FILE *f;
	char *slash;
	uint64_t best = 0, cur;
	int found = 0;
	long exe_size = 0, want = 0;
	HANDLE h;

	if (!GetModuleFileNameA(NULL, path, sizeof path))
		return 0;
	h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
			OPEN_EXISTING, 0, NULL);
	if (h != INVALID_HANDLE_VALUE) {
		exe_size = (long)GetFileSize(h, NULL);
		CloseHandle(h);
	}
	slash = strrchr(path, '\\');
	if (!slash)
		return 0;
	snprintf(slash + 1, sizeof path - (size_t)(slash + 1 - path), "xsystem4.sym");
	f = fopen(path, "r");
	if (!f)
		return 0;
	if (!fgets(line, sizeof line, f) || sscanf(line, "# exe-size %ld", &want) != 1
	    || want != exe_size) {
		fclose(f);
		return 0;
	}
	while (fgets(line, sizeof line, f)) {
		char sym[400];
		if (sscanf(line, "%llx %399s", (unsigned long long *)&cur, sym) != 2)
			continue;
		if (cur > rva)
			break;
		best = cur;
		snprintf(name, n, "%s", sym);
		found = 1;
	}
	fclose(f);
	if (found)
		*off = rva - best;
	return found;
}

/* "xsystem4.exe+0x1A2B3C (rq_log+0x12)" */
static void describe_addr(FILE *f, uint64_t addr)
{
	HMODULE mod = NULL;
	char path[MAX_PATH];
	const char *base = "?";

	if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
			       | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			       (LPCSTR)(uintptr_t)addr, &mod) && mod) {
		if (GetModuleFileNameA(mod, path, sizeof path)) {
			base = strrchr(path, '\\');
			base = base ? base + 1 : path;
		}
		uint64_t rva = addr - (uint64_t)(uintptr_t)mod;
		char sym[256];
		uint64_t off = 0;
		fprintf(f, "%s+0x%llX", base, (unsigned long long)rva);
		if (mod == GetModuleHandleA(NULL) && sym_lookup(rva, sym, sizeof sym, &off))
			fprintf(f, " (%s+0x%llX)", sym, (unsigned long long)off);
	} else {
		fprintf(f, "0x%llX", (unsigned long long)addr);
	}
}

static void write_context(FILE *f, EXCEPTION_POINTERS *ep)
{
	EXCEPTION_RECORD *er = ep->ExceptionRecord;

	fprintf(f, "exception: 0x%08lX %s\n", (unsigned long)er->ExceptionCode,
		exc_name(er->ExceptionCode));
	if (er->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && er->NumberParameters >= 2) {
		const char *kind = er->ExceptionInformation[0] == 0 ? "reading"
			: er->ExceptionInformation[0] == 1 ? "writing" : "executing";
		fprintf(f, "  %s address 0x%llX\n", kind,
			(unsigned long long)er->ExceptionInformation[1]);
	}
	fprintf(f, "  at ");
	describe_addr(f, (uint64_t)(uintptr_t)er->ExceptionAddress);
	fprintf(f, "\n");
#if defined(__x86_64__) || defined(_M_X64)
	{
		CONTEXT *c = ep->ContextRecord;
		uint64_t *sp = (uint64_t *)(uintptr_t)c->Rsp;
		int i, shown = 0;

		fprintf(f, "  rip=0x%llX rsp=0x%llX rbp=0x%llX rax=0x%llX rcx=0x%llX rdx=0x%llX\n",
			(unsigned long long)c->Rip, (unsigned long long)c->Rsp,
			(unsigned long long)c->Rbp, (unsigned long long)c->Rax,
			(unsigned long long)c->Rcx, (unsigned long long)c->Rdx);
		fprintf(f, "Return addresses found on the stack (raw scan, innermost first; a few may be stale):\n");
		for (i = 0; i < 1024 && shown < 40; i++) {
			uint64_t v;
			HMODULE m = NULL;
			if (IsBadReadPtr(&sp[i], 8))
				break;
			v = sp[i];
			if (v < 0x10000)
				continue;
			if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
						| GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
						(LPCSTR)(uintptr_t)v, &m) || !m)
				continue;
			/* only code in the exe itself (the symbols we can read) */
			if (m != GetModuleHandleA(NULL))
				continue;
			fprintf(f, "  [rsp+0x%X] ", i * 8);
			describe_addr(f, v);
			fprintf(f, "\n");
			shown++;
		}
	}
#endif
	fflush(f);
}

typedef BOOL (WINAPI *minidump_fn)(HANDLE, DWORD, HANDLE, int, void *, void *, void *);

static void write_minidump(const char *path, EXCEPTION_POINTERS *ep)
{
	HMODULE dbg = LoadLibraryA("dbghelp.dll");
	minidump_fn dump;
	HANDLE h;
	struct {
		DWORD ThreadId;
		EXCEPTION_POINTERS *ExceptionPointers;
		BOOL ClientPointers;
	} __attribute__((packed)) mei;

	if (!dbg)
		return;
	dump = (minidump_fn)GetProcAddress(dbg, "MiniDumpWriteDump");
	if (!dump)
		return;
	h = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (h == INVALID_HANDLE_VALUE)
		return;
	mei.ThreadId = GetCurrentThreadId();
	mei.ExceptionPointers = ep;
	mei.ClientPointers = FALSE;
	dump(GetCurrentProcess(), GetCurrentProcessId(), h, 0 /* MiniDumpNormal */,
	     ep ? &mei : NULL, NULL, NULL);
	CloseHandle(h);
}

static void report_path(char *out, size_t n, const char *ext)
{
	SYSTEMTIME t;

	GetLocalTime(&t);
	snprintf(out, n, "%s\\rqmod-crash-%04d%02d%02d-%02d%02d%02d.%s", g_crash_dir,
		 (int)t.wYear, (int)t.wMonth, (int)t.wDay, (int)t.wHour,
		 (int)t.wMinute, (int)t.wSecond, ext);
}

static void write_report(const char *why, EXCEPTION_POINTERS *ep)
{
	char path[RQ_PATH_MAX];
	FILE *f;
	FILE *outs[2];
	int i, n = 0;

	if (g_in_report)
		return; /* the report itself crashed */
	g_in_report = 1;
	report_path(path, sizeof path, "log");
	f = fopen(path, "w");
	if (f)
		outs[n++] = f;
	if (g_rq_log)
		outs[n++] = g_rq_log;
	for (i = 0; i < n; i++) {
		write_header(outs[i], why);
		if (ep)
			write_context(outs[i], ep);
		write_ain_stack(outs[i]);
		write_rings(outs[i]);
		fflush(outs[i]);
	}
	if (f)
		fclose(f);
	if (ep) {
		report_path(path, sizeof path, "dmp");
		write_minidump(path, ep);
	}
	fprintf(stderr, "[rqmod] crash report written next to rqmod.log (rqmod-crash-*.log)\n");
	fflush(stderr);
}

static LONG WINAPI crash_filter(EXCEPTION_POINTERS *ep)
{
	write_report("unhandled Windows exception", ep);
	return EXCEPTION_EXECUTE_HANDLER;
}

/* For a game that hangs: `rqmod-ctl where`, from the pipe thread. The game thread is
 * suspended a few times for a moment and its native stack walked (unwind tables, so
 * no symbols file is needed for the walk itself). Nothing is allocated or written
 * while it is suspended, since it may hold the heap lock. Then the stacks, the AIN
 * call stack and the rings go to logs\rqmod-where-<time>.txt. */
#define WHERE_SAMPLES 5
#define WHERE_FRAMES 40

static int walk_thread(HANDLE th, uint64_t *pcs, int max)
{
	CONTEXT c;
	int n = 0;

	memset(&c, 0, sizeof c);
	c.ContextFlags = CONTEXT_FULL;
	if (!GetThreadContext(th, &c))
		return 0;
#if defined(__x86_64__) || defined(_M_X64)
	while (n < max && c.Rip) {
		DWORD64 base = 0, frame = 0;
		PVOID hdata = NULL;
		PRUNTIME_FUNCTION fe;

		pcs[n++] = c.Rip;
		fe = RtlLookupFunctionEntry(c.Rip, &base, NULL);
		if (!fe) {
			/* a leaf function: the return address is on top of the stack */
			c.Rip = *(DWORD64 *)(uintptr_t)c.Rsp;
			c.Rsp += 8;
		} else {
			RtlVirtualUnwind(UNW_FLAG_NHANDLER, base, c.Rip, fe, &c, &hdata, &frame, NULL);
		}
	}
#endif
	return n;
}

int rq_crash_where(char *out, size_t outn)
{
	static uint64_t pcs[WHERE_SAMPLES][WHERE_FRAMES];
	int np[WHERE_SAMPLES], s, i;
	char path[RQ_PATH_MAX];
	HANDLE th;
	SYSTEMTIME t;
	FILE *f;

	if (!g_main_thread || !g_crash_dir[0])
		return 0;
	th = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION,
			FALSE, g_main_thread);
	if (!th)
		return 0;
	for (s = 0; s < WHERE_SAMPLES; s++) {
		np[s] = 0;
		if (s)
			Sleep(200);
		if (SuspendThread(th) == (DWORD)-1)
			continue;
		np[s] = walk_thread(th, pcs[s], WHERE_FRAMES);
		ResumeThread(th);
	}
	CloseHandle(th);

	GetLocalTime(&t);
	snprintf(path, sizeof path, "%s\\rqmod-where-%04d%02d%02d-%02d%02d%02d.txt", g_crash_dir,
		 (int)t.wYear, (int)t.wMonth, (int)t.wDay, (int)t.wHour, (int)t.wMinute,
		 (int)t.wSecond);
	f = fopen(path, "w");
	if (!f)
		return 0;
	write_header(f, "where the game thread is (rqmod-ctl where)");
	{
		char watch[512];
		rq_stall_watch_text(watch, sizeof watch);
		fprintf(f, "Stall watch: %s\n\n", watch);
	}
	for (s = 0; s < WHERE_SAMPLES; s++) {
		fprintf(f, "Game thread, sample %d of %d, 200 ms apart (innermost first):\n",
			s + 1, WHERE_SAMPLES);
		for (i = 0; i < np[s]; i++) {
			fprintf(f, "  #%d ", i);
			describe_addr(f, pcs[s][i]);
			fprintf(f, "\n");
		}
	}
	write_ain_stack(f);
	write_rings(f);
	fclose(f);
	snprintf(out, outn, "%s", path);
	return 1;
}

#else /* !_WIN32 */

static void write_report(const char *why, void *ep)
{
	char path[RQ_PATH_MAX];
	FILE *f;

	(void)ep;
	if (g_in_report)
		return;
	g_in_report = 1;
	snprintf(path, sizeof path, "%s/rqmod-crash-%ld.log", g_crash_dir, (long)time(NULL));
	f = fopen(path, "w");
	if (!f)
		return;
	write_header(f, why);
	write_ain_stack(f);
	write_rings(f);
	fclose(f);
}

#endif

static void on_abort(int sig)
{
	(void)sig;
	write_report("abort() or a failed assertion", NULL);
	_exit(3);
}

static void on_clean_exit(void)
{
	if (g_flag_path[0])
		remove(g_flag_path);
}

/* Called by the VM error path (and usable from anywhere with a message). */
void rq_crash_vm_error(const char *msg)
{
	write_report(msg ? msg : "VM error", NULL);
}

/* Debug aid: a deliberate crash, only with env RQ_DEBUG_CRASH set. */
void rq_crash_test(void)
{
	volatile int *p = NULL;

	if (!getenv("RQ_DEBUG_CRASH"))
		return;
	rq_crash_crumb("crash-test", 1, 2);
	*p = 1;
}

static int exists(const char *p)
{
	FILE *f = fopen(p, "rb");
	if (!f)
		return 0;
	fclose(f);
	return 1;
}

/* Before rqmod.log is opened: keep the last run's log and say if it did not end cleanly. */
void rq_crash_rotate_logs(const char *dir)
{
	char cur[RQ_PATH_MAX], prev[RQ_PATH_MAX], keep[RQ_PATH_MAX];

	snprintf(g_crash_dir, sizeof g_crash_dir, "%s", dir);
	snprintf(cur, sizeof cur, "%s\\rqmod.log", dir);
	snprintf(prev, sizeof prev, "%s\\rqmod.prev.log", dir);
	snprintf(g_flag_path, sizeof g_flag_path, "%s\\rqmod-running.flag", dir);
	if (!exists(cur))
		return;
#ifdef _WIN32
	if (exists(g_flag_path)) {
		/* the last run never reached a clean exit: keep its log for good */
		SYSTEMTIME t;
		GetLocalTime(&t);
		snprintf(keep, sizeof keep, "%s\\rqmod-unclean-%04d%02d%02d-%02d%02d%02d.log", dir,
			 (int)t.wYear, (int)t.wMonth, (int)t.wDay, (int)t.wHour,
			 (int)t.wMinute, (int)t.wSecond);
		CopyFileA(cur, keep, FALSE);
	}
	MoveFileExA(cur, prev, MOVEFILE_REPLACE_EXISTING);
#else
	(void)keep;
	rename(cur, prev);
#endif
}

void rq_crash_install(void)
{
	FILE *f;

	if (g_installed)
		return;
	g_installed = 1;
#ifdef _WIN32
	g_main_thread = GetCurrentThreadId();
#endif
	if (g_flag_path[0]) {
		int unclean = exists(g_flag_path);
		f = fopen(g_flag_path, "w");
		if (f) {
			fprintf(f, "running\n");
			fclose(f);
		}
		if (unclean)
			rq_log("WARNING: the previous run did not exit cleanly (crash, kill or hang). "
			       "Its log was kept as rqmod-unclean-*.log and rqmod.prev.log");
	}
	atexit(on_clean_exit);
#ifdef _WIN32
	SetUnhandledExceptionFilter(crash_filter);
#endif
	signal(SIGABRT, on_abort);
	rq_log("crash reports on: rqmod-crash-*.log (and .dmp) next to rqmod.log");
}

#endif /* RQ_MODLOADER */
