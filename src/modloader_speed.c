/* Copyright (C) 2026 Rance Quest modding fork
 *
 * rq.speed: the speed of the game clock (vm_time). The script's timers, Flash
 * and part animations and the 3D renderers follow it; frame caps, sleeps and
 * audio stay on real time.
 *
 * The play time counter (g_PlayTimer) is a script timer too, so it would count
 * faster. Each update moves its start by what the game clock gained over real
 * time, so the saved play time stays real.
 */

#ifdef RQ_MODLOADER

#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <SDL.h>
#include "lua.h"
#include "lauxlib.h"
#include "system4.h"
#include "system4/ain.h"
#include "system4/utfsjis.h"
#include "modloader.h"
#include "modloader_priv.h"
#include "vm.h"
#include "vm/heap.h"
#include "vm/page.h"

#define SPEED_MIN 0.25
#define SPEED_MAX 8.0

/* Game clock minus real clock at the last update, in ms. */
static int64_t g_last_gain;
static int g_gain_ready;

static int glookup(const char *utf)
{
	int g;
	char *sj;
	if (!ain || !utf)
		return -1;
	g = ain_get_global(ain, utf);
	if (g >= 0)
		return g;
	sj = utf2sjis(utf, 0);
	if (!sj)
		return -1;
	g = ain_get_global(ain, sj);
	free(sj);
	return g;
}

static int memb(struct page *p, const char *name)
{
	struct ain_struct *s;
	int i;
	if (!p || p->type != STRUCT_PAGE || !ain || p->index < 0
			|| p->index >= ain->nr_structures || !name)
		return -1;
	s = &ain->structures[p->index];
	for (i = 0; i < s->nr_members && i < p->nr_vars; i++) {
		if (s->members[i].name && !strcmp(s->members[i].name, name))
			return i;
	}
	return -1;
}

static struct page *slot_page(int slot)
{
	return page_index_valid(slot) ? heap_get_page(slot) : NULL;
}

static struct page *gpage(const char *name)
{
	int g = glookup(name);
	return g < 0 ? NULL : slot_page(global_get(g).i);
}

static struct page *fpage(struct page *p, const char *name)
{
	int m = memb(p, name);
	return m < 0 ? NULL : slot_page(p->values[m].i);
}

/* g_ASTimerManager.m_TimerImpList[g_PlayTimer.m_nHandle], or NULL. */
static struct page *play_timer(void)
{
	struct page *timer = gpage("g_PlayTimer");
	struct page *mgr = gpage("g_ASTimerManager");
	struct page *list = fpage(mgr, "m_TimerImpList");
	int m = memb(timer, "m_nHandle");
	int handle;
	if (!timer || !list || m < 0 || list->type != ARRAY_PAGE)
		return NULL;
	handle = timer->values[m].i;
	if (handle < 0 || handle >= list->nr_vars)
		return NULL;
	return slot_page(list->values[handle].i);
}

void rq_speed_tick(void)
{
	int64_t gain = (int64_t)vm_game_ticks() - (int64_t)SDL_GetTicks64();
	int64_t d = gain - g_last_gain;
	struct page *imp;
	int m;

	g_last_gain = gain;
	if (!g_gain_ready) {
		g_gain_ready = 1;
		return;
	}
	if (!d)
		return;
	imp = play_timer();
	m = memb(imp, "m_nBeginTime");
	if (m >= 0)
		imp->values[m].i += (int)d;
}

int rq_lua_speed_set(lua_State *L)
{
	double f = luaL_checknumber(L, 1);
	if (!(f >= SPEED_MIN))
		f = SPEED_MIN;
	if (f > SPEED_MAX)
		f = SPEED_MAX;
	if (f != vm_time_scale()) {
		vm_set_time_scale(f);
		if (g_rq_verbose)
			rq_log("speed: game clock x%.2f", f);
	}
	lua_pushnumber(L, f);
	return 1;
}

int rq_lua_speed_get(lua_State *L)
{
	lua_pushnumber(L, vm_time_scale());
	return 1;
}

#endif
