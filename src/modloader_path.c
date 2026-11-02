/* Dungeon grid for rqmod-ctl. Reads the live 2D map (geometry + quest
 * layers) and walks a cell path. Right-click uses the engine pathfinder
 * when that path stays outside event hit radii; arrow keys step one cell
 * when a click would clip an event. Camera zoom is whatever the plugin
 * is using right now. Not a Lua HUD: the ray and the input pulse sit on
 * the pipe side of the engine.
 */

#ifdef RQ_MODLOADER

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "system4.h"
#include "system4/ain.h"
#include "system4/utfsjis.h"

#include "cJSON.h"
#include "reign.h"
#include "3d/3d_internal.h"
#include "vm.h"
#include "vm/heap.h"
#include "vm/page.h"
#include "modloader.h"

__declspec(dllimport) void __stdcall Sleep(unsigned long ms);
__declspec(dllimport) unsigned long long __stdcall GetTickCount64(void);

void mouse_force_pos(int x, int y);
void key_set_state(int code, int down);
void agent_focus_window(void);

#define MAP 50
#define CELL 4.f
#define MAX_HZ 160
#define MAX_PATH 160
#define MAX_IN 48

/* T_Player click rect is (8, 8, 800, 600). Area overlay sits at (695, 11). */
#define CLICK_X0 20
#define CLICK_Y0 20
#define CLICK_X1 (8 + 800 - 20)
#define CLICK_Y1 (8 + 600 - 20)
#define AREA_X0 690
#define AREA_Y1 140

#define VK_LBUTTON 0x01
#define VK_RBUTTON 0x02
#define VK_LEFT    0x25
#define VK_UP      0x26
#define VK_RIGHT   0x27
#define VK_DOWN    0x28

/* Fullwidth Ｍ and ◆ＢＭＡＰ, UTF-8. */
#define U8_M    "\xEF\xBC\xAD"
#define U8_BMAP "\xE2\x97\x86\xEF\xBC\xA2\xEF\xBC\xAD\xEF\xBC\xA1\xEF\xBC\xB0"

enum {
	K_VOID = 0,
	K_WALL,
	K_PIT,
	K_FLOOR,
	K_CHEST,
	K_RED,
	K_TRAP,
	K_FIGHT,
	K_PORTAL,
	K_EVENT
};

struct cell {
	signed char kind;
	unsigned char enter;
	short type;
	int height;
	int ground;
	int event;
	int obj;
	int obj_enter;
	int bb;
	int bb_enter;
	int tt, tz, tv, nn;
	float radius;
};

struct grid {
	int map_no;
	int data_map;
	int bmap;
	int layers;
	int move_type;
	int area;
	int key_mode;
	int px, pz;
	float wx, wy, wz;
	float cam_len, cam_yaw, cam_pitch;
	int view[4];
	int plug;
	int map_inst;
	int aim_x, aim_y, aim_hx, aim_hz, aim_ok;
	int mesh_n;
	int keys_armed;
	int nh;
	struct { int x, z; float r; } hz[MAX_HZ];
	struct cell c[MAP][MAP];
	unsigned char reach[MAP][MAP];
	int minx, minz, maxx, maxz;
};

static struct grid G;

static const int DX[4] = { 1, -1, 0, 0 };
static const int DZ[4] = { 0, 0, 1, -1 };

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

static struct page *as_page(int slot)
{
	struct page *p;
	if (!page_index_valid(slot))
		return NULL;
	p = heap_get_page(slot);
	return p;
}

static struct page *gstruct(const char *name)
{
	int g = glookup(name);
	struct page *p;
	if (g < 0)
		return NULL;
	p = as_page(global_get(g).i);
	if (!p || p->type != STRUCT_PAGE)
		return NULL;
	return p;
}

static struct page *garray(const char *name)
{
	int g = glookup(name);
	struct page *p;
	if (g < 0)
		return NULL;
	p = as_page(global_get(g).i);
	if (!p || p->type != ARRAY_PAGE)
		return NULL;
	return p;
}

static int gint(const char *name)
{
	int g = glookup(name);
	if (g < 0)
		return 0;
	return global_get(g).i;
}

static struct page *fpage(struct page *p, const char *name)
{
	int m = memb(p, name);
	if (m < 0)
		return NULL;
	return as_page(p->values[m].i);
}

static int fint(struct page *p, const char *name)
{
	int m = memb(p, name);
	if (!p || m < 0 || m >= p->nr_vars)
		return 0;
	return p->values[m].i;
}

static float ffloat(struct page *p, const char *name)
{
	int m = memb(p, name);
	if (!p || m < 0 || m >= p->nr_vars)
		return 0;
	return p->values[m].f;
}

static int arr_i(struct page *a, int i)
{
	if (!a || a->type != ARRAY_PAGE || i < 0 || i >= a->nr_vars)
		return 0;
	return a->values[i].i;
}

static struct page *arr_p(struct page *a, int i)
{
	if (!a || a->type != ARRAY_PAGE || i < 0 || i >= a->nr_vars)
		return NULL;
	return as_page(a->values[i].i);
}

static int arr2_i(struct page *a, int x, int y)
{
	struct page *row = arr_p(a, x);
	if (row && row->type == ARRAY_PAGE)
		return arr_i(row, y);
	if (a && y >= 0 && y < 100)
		return arr_i(a, x * 100 + y);
	return 0;
}

static const char *kind_name(int k)
{
	switch (k) {
	case K_VOID: return "void";
	case K_WALL: return "wall";
	case K_PIT: return "pit";
	case K_FLOOR: return "floor";
	case K_CHEST: return "chest";
	case K_RED: return "redchest";
	case K_TRAP: return "trap";
	case K_FIGHT: return "fight";
	case K_PORTAL: return "portal";
	case K_EVENT: return "event";
	default: return "void";
	}
}

static char kind_char(int k)
{
	switch (k) {
	case K_WALL: return '#';
	case K_PIT: return 'o';
	case K_FLOOR: return '.';
	case K_CHEST: return 'C';
	case K_RED: return 'R';
	case K_TRAP: return 't';
	case K_FIGHT: return 'F';
	case K_PORTAL: return 'P';
	case K_EVENT: return 'E';
	default: return ' ';
	}
}

/* 当り距離計算. Enter flag > 0 is the smaller radius. */
static float hit_radius(int no, int enter)
{
	if (no <= 0)
		return 0;
	switch (no) {
	case 205: case 208: case 211: case 215: case 216: case 218:
		return enter > 0 ? 3.f : 5.f;
	default:
		return enter > 0 ? 2.f : 4.f;
	}
}

static int kind_tt(int tt)
{
	switch (tt) {
	case 1: return K_CHEST;
	case 2: return K_RED;
	case 3: case 4: return K_PORTAL;
	case 6: return K_TRAP;
	case 16: case 17: return K_FIGHT;
	case 5: case 8: case 9: case 10: case 12: case 13:
	case 15: case 18: case 19: case 20:
		return K_EVENT;
	default:
		return 0;
	}
}

static int kind_obj(int no)
{
	switch (no) {
	case 202: return K_CHEST;
	case 214: return K_RED;
	case 201: case 215: return K_PORTAL;
	case 206: return K_TRAP;
	case 204: case 205: return K_FIGHT;
	case 203: case 207: case 208: case 210: case 211:
	case 212: case 213: case 216: case 217: case 218:
		return K_EVENT;
	default:
		return 0;
	}
}

static int hazard(int k)
{
	return k >= K_CHEST;
}

static void cell_center(int x, int z, float *wx, float *wz)
{
	*wx = x * CELL + 2.f;
	*wz = z * CELL + 2.f;
}

static void world_cell(float x, float z, int *cx, int *cz)
{
	float fx = x - 2.f;
	float fz = z - 2.f;
	int ix = (int)(fx / CELL);
	int iz = (int)(fz / CELL);
	if (fx - ix * CELL > 2.f)
		ix++;
	if (fz - iz * CELL > 2.f)
		iz++;
	*cx = ix;
	*cz = iz;
}

static float seg_dist(float ax, float az, float bx, float bz, float px, float pz)
{
	float abx = bx - ax, abz = bz - az;
	float apx = px - ax, apz = pz - az;
	float ab2 = abx * abx + abz * abz;
	float t = ab2 > 1e-8f ? (apx * abx + apz * abz) / ab2 : 0.f;
	float dx, dz;
	if (t < 0.f)
		t = 0.f;
	if (t > 1.f)
		t = 1.f;
	dx = ax + abx * t - px;
	dz = az + abz * t - pz;
	return sqrtf(dx * dx + dz * dz);
}

static int clips(float dist, float r)
{
	return dist <= r + 0.05f;
}

/* World XZ is clear of every hazard except the goal cell. */
static int world_clear(struct grid *g, float x, float z, int gx, int gz)
{
	int i;
	for (i = 0; i < g->nh; i++) {
		float hx, hz, d;
		if (g->hz[i].x == gx && g->hz[i].z == gz)
			continue;
		cell_center(g->hz[i].x, g->hz[i].z, &hx, &hz);
		d = sqrtf((x - hx) * (x - hx) + (z - hz) * (z - hz));
		if (clips(d, g->hz[i].r))
			return 0;
	}
	return 1;
}

static int seg_clear(struct grid *g, int x0, int z0, int x1, int z1, int gx, int gz)
{
	float ax, az, bx, bz;
	int i;
	cell_center(x0, z0, &ax, &az);
	cell_center(x1, z1, &bx, &bz);
	for (i = 0; i < g->nh; i++) {
		float hx, hz;
		if (g->hz[i].x == gx && g->hz[i].z == gz)
			continue;
		cell_center(g->hz[i].x, g->hz[i].z, &hx, &hz);
		if (clips(seg_dist(ax, az, bx, bz, hx, hz), g->hz[i].r))
			return 0;
	}
	return 1;
}

static int in_map(int x, int z)
{
	return x >= 0 && z >= 0 && x < MAP && z < MAP;
}

/* N=1 S=2 E=4 W=8. Matches マップ_通行可. */
static int edge_open(struct grid *g, int x, int z, int dir)
{
	int x2 = x + DX[dir], z2 = z + DZ[dir];
	unsigned a, b;
	if (!in_map(x2, z2))
		return 0;
	if (dir == 0) { a = 4; b = 8; }       /* +x: E then W */
	else if (dir == 1) { a = 8; b = 4; }  /* -x: W then E */
	else if (dir == 2) { a = 1; b = 2; }  /* +z: N then S */
	else { a = 2; b = 1; }                /* -z: S then N */
	return (g->c[z][x].enter & a) && (g->c[z2][x2].enter & b);
}

static int stand_blocked(struct grid *g, int x, int z, int sx, int sz, int gx, int gz)
{
	struct cell *c;
	float wx, wz;
	if (!in_map(x, z))
		return 1;
	if ((x == sx && z == sz) || (x == gx && z == gz))
		return 0;
	c = &g->c[z][x];
	if (c->kind == K_VOID || c->kind == K_WALL || c->kind == K_PIT)
		return 1;
	if (hazard(c->kind))
		return 1;
	cell_center(x, z, &wx, &wz);
	return !world_clear(g, wx, wz, gx, gz);
}

static int bfs(struct grid *g, int sx, int sz, int gx, int gz, int *ox, int *oz)
{
	short parent[MAP * MAP];
	int queue[MAP * MAP];
	int qh = 0, qt = 0;
	int i, n, cur;
	int rev[MAX_PATH];
	if (!in_map(sx, sz) || !in_map(gx, gz))
		return 0;
	if (stand_blocked(g, gx, gz, sx, sz, gx, gz) && !(gx == sx && gz == sz))
		return 0;
	for (i = 0; i < MAP * MAP; i++)
		parent[i] = -2;
	parent[sz * MAP + sx] = -1;
	queue[qt++] = sz * MAP + sx;
	while (qh < qt) {
		int curi = queue[qh++];
		int x = curi % MAP, z = curi / MAP;
		if (x == gx && z == gz)
			break;
		for (i = 0; i < 4; i++) {
			int x2 = x + DX[i], z2 = z + DZ[i], ni;
			if (!edge_open(g, x, z, i))
				continue;
			if (stand_blocked(g, x2, z2, sx, sz, gx, gz))
				continue;
			if (!seg_clear(g, x, z, x2, z2, gx, gz))
				continue;
			ni = z2 * MAP + x2;
			if (parent[ni] != -2)
				continue;
			parent[ni] = (short)curi;
			queue[qt++] = ni;
		}
	}
	if (parent[gz * MAP + gx] == -2)
		return 0;
	n = 0;
	cur = gz * MAP + gx;
	while (cur >= 0 && n < MAX_PATH) {
		rev[n++] = cur;
		if (cur == sz * MAP + sx)
			break;
		cur = parent[cur];
	}
	if (n < 1 || rev[n - 1] != sz * MAP + sx)
		return 0;
	for (i = 0; i < n; i++) {
		int p = rev[n - 1 - i];
		ox[i] = p % MAP;
		oz[i] = p / MAP;
	}
	return n;
}

static int message_up(void)
{
	return gint("g_nMessageNo") > 0;
}

static int read_bmap(void)
{
	return gint(U8_BMAP);
}

static int read_player(int *x, int *z, float *wx, float *wy, float *wz)
{
	struct page *p = gstruct("g_sPlayer");
	if (!p)
		return 0;
	*x = fint(p, "m_nMapX");
	*z = fint(p, "m_nMapZ");
	*wx = ffloat(p, "m_fX");
	*wy = ffloat(p, "m_fY");
	*wz = ffloat(p, "m_fZ");
	return in_map(*x, *z);
}

static struct RE_instance *map_instance(struct grid *g)
{
	struct RE_plugin *p;
	if (g->plug < 0 || g->map_inst < 0)
		return NULL;
	p = RE_get_plugin(g->plug);
	if (!p || g->map_inst >= p->nr_instances || !p->instances)
		return NULL;
	return p->instances[g->map_inst];
}

static int in_click(struct grid *g, int x, int y)
{
	if (x < CLICK_X0 || y < CLICK_Y0 || x >= CLICK_X1 || y >= CLICK_Y1)
		return 0;
	if (g->view[2] > 0 && g->view[3] > 0) {
		if (x < g->view[0] || y < g->view[1])
			return 0;
		if (x >= g->view[0] + g->view[2] || y >= g->view[1] + g->view[3])
			return 0;
	}
	if (g->area && x >= AREA_X0 && y <= AREA_Y1)
		return 0;
	return 1;
}

/* Project a cell center through the live camera. Zoom is the orbit length. */
static int project_cell(struct grid *g, int x, int z, int *sx, int *sy, float *hy)
{
	struct RE_instance *inst = map_instance(g);
	struct RE_plugin *plug;
	float wx, wz, y;
	if (!inst || !inst->plugin)
		return 0;
	plug = inst->plugin;
	cell_center(x, z, &wx, &wz);
	y = RE_instance_calc_2d_detection_height(inst, wx, wz);
	if (y == 0.f)
		y = g->wy;
	if (hy)
		*hy = y;
	if (!RE_plugin_transform_pos_to_view_pos(plug, wx, y, -wz, sx, sy))
		return 0;
	return 1;
}

static int ray_cell(struct RE_instance *inst, int sx, int sy, float *hx, float *hy, float *hz, int *cx, int *cz)
{
	vec3 hit;
	if (!RE_instance_calc_path_finder_intersect_eye_vec(inst, sx, sy, hit))
		return 0;
	*hx = hit[0];
	*hy = hit[1];
	*hz = -hit[2];
	world_cell(*hx, *hz, cx, cz);
	return 1;
}

/* 1 if a right-click (or left, if move type is swapped) on this cell
 * stays off non-goal events. The engine path is what the click will walk. */
static int click_safe(struct grid *g, int x, int z, int gx, int gz, int *sx, int *sy)
{
	struct RE_instance *inst = map_instance(g);
	float hx, hiy, hz, px, py, pz;
	int cx, cz, n = 0, i, pix, piz;
	const vec3 *pts;
	vec3 start, goal;
	float ax[128], az[128];
	if (!inst || !inst->model)
		return 0;
	if (!read_player(&pix, &piz, &px, &py, &pz))
		return 0;
	if (!project_cell(g, x, z, sx, sy, NULL))
		return 0;
	if (!in_click(g, *sx, *sy))
		return 0;
	if (!ray_cell(inst, *sx, *sy, &hx, &hiy, &hz, &cx, &cz))
		return 0;
	if (cx != x || cz != z)
		return 0;
	if (!world_clear(g, hx, hz, gx, gz))
		return 0;
	start[0] = px;
	start[1] = py;
	start[2] = -pz;
	goal[0] = hx;
	goal[1] = hiy;
	goal[2] = -hz;
	if (!RE_instance_find_path(inst, start, goal))
		return 0;
	RE_instance_optimize_path_line(inst);
	pts = RE_instance_get_path_line(inst, &n);
	if (!pts || n < 2 || n > 128)
		return 0;
	for (i = 0; i < n; i++) {
		ax[i] = pts[i][0];
		az[i] = -pts[i][2];
	}
	for (i = 0; i < n; i++) {
		if (!world_clear(g, ax[i], az[i], gx, gz))
			return 0;
	}
	for (i = 0; i + 1 < n; i++) {
		int h;
		for (h = 0; h < g->nh; h++) {
			float qx, qz;
			if (g->hz[h].x == gx && g->hz[h].z == gz)
				continue;
			cell_center(g->hz[h].x, g->hz[h].z, &qx, &qz);
			if (clips(seg_dist(ax[i], az[i], ax[i + 1], az[i + 1], qx, qz), g->hz[h].r))
				return 0;
		}
	}
	return 1;
}

static void release_keys(void)
{
	key_set_state(VK_LEFT, 0);
	key_set_state(VK_UP, 0);
	key_set_state(VK_RIGHT, 0);
	key_set_state(VK_DOWN, 0);
	key_set_state(VK_LBUTTON, 0);
	key_set_state(VK_RBUTTON, 0);
}

static int cheb(int x0, int z0, int x1, int z1)
{
	int dx = x0 - x1, dz = z0 - z1;
	if (dx < 0)
		dx = -dx;
	if (dz < 0)
		dz = -dz;
	return dx > dz ? dx : dz;
}

static void fill_cells(struct grid *g)
{
	struct page *parts = gstruct("g_sMapParts");
	struct page *data_m = gstruct("g_sMapData");
	struct page *data = fpage(data_m, "m_sData");
	struct page *types = fpage(data, "m_anType");
	struct page *heights = fpage(data, "m_anHeight");
	struct page *grounds = fpage(data, "m_anGround");
	struct page *events = fpage(data, "m_anEvent");
	struct page *en = fpage(data, "m_abEnterN");
	struct page *es = fpage(data, "m_abEnterS");
	struct page *ee = fpage(data, "m_abEnterE");
	struct page *ew = fpage(data, "m_abEnterW");
	struct page *objm = gstruct("g_sObjectData");
	struct page *objd = fpage(objm, "m_sData");
	struct page *objno = fpage(objd, "m_anNo");
	struct page *objen = fpage(objd, "m_anEnterFlag");
	struct page *bbm = gstruct("g_sBillboardData");
	struct page *bbd = fpage(bbm, "m_sData");
	struct page *bbno = fpage(bbd, "m_anCgNo");
	struct page *bben = fpage(bbd, "m_anEnterFlag");
	struct page *maps = garray(U8_M);
	struct page *mode = gstruct("g_sMapMode");
	struct page *slot = NULL, *tt = NULL, *tz = NULL, *tv = NULL, *nn = NULL;
	struct page *garr = garray("g_anGlobalData");
	int x, z;

	memset(g, 0, sizeof *g);
	g->plug = -1;
	g->map_inst = -1;
	g->map_no = parts ? fint(parts, "m_nMapNo") : 0;
	g->data_map = data_m ? fint(data_m, "m_nMapNo") : 0;
	g->area = parts ? fint(parts, "m_bAreaShow") : 0;
	g->key_mode = parts ? fint(parts, "m_bInputKey") : 0;
	g->bmap = read_bmap();
	g->move_type = arr_i(garr, 23);
	g->cam_len = arr_i(garr, 21) / 100.f;
	if (mode) {
		float len = ffloat(mode, "m_fCenterCameraLength");
		struct page *cam = fpage(mode, "m_CenterCamera");
		struct page *plug = fpage(mode, "m_Plugin");
		struct page *map = fpage(mode, "m_Map");
		if (len > 0.f)
			g->cam_len = len;
		if (cam) {
			g->cam_yaw = ffloat(cam, "m_fAngle");
			g->cam_pitch = ffloat(cam, "m_fAngleP");
			if (ffloat(cam, "m_fLength") > 0.f)
				g->cam_len = ffloat(cam, "m_fLength");
		}
		g->view[0] = fint(mode, "m_nViewX");
		g->view[1] = fint(mode, "m_nViewY");
		g->view[2] = fint(mode, "m_nViewWidth");
		g->view[3] = fint(mode, "m_nViewHeight");
		if (plug)
			g->plug = fint(plug, "m_nPlugin");
		if (map)
			g->map_inst = fint(map, "m_nInstance");
	}
	if (maps && g->bmap >= 0 && g->bmap < maps->nr_vars) {
		slot = arr_p(maps, g->bmap);
		tt = fpage(slot, "maptt");
		tz = fpage(slot, "maptz");
		tv = fpage(slot, "maptv");
		nn = fpage(slot, "mapnn");
		g->layers = tt != NULL;
	}
	for (z = 0; z < MAP; z++) {
		for (x = 0; x < MAP; x++) {
			struct cell *c = &g->c[z][x];
			int idx = z * MAP + x;
			int tx = x + 1;
			int ty = MAP - z;
			int k;
			c->type = arr_i(types, idx);
			c->height = arr_i(heights, idx);
			c->ground = arr_i(grounds, idx);
			c->event = arr_i(events, idx);
			c->enter = 0;
			if (arr_i(en, idx)) c->enter |= 1;
			if (arr_i(es, idx)) c->enter |= 2;
			if (arr_i(ee, idx)) c->enter |= 4;
			if (arr_i(ew, idx)) c->enter |= 8;
			c->obj = arr_i(objno, idx);
			c->obj_enter = arr_i(objen, idx);
			c->bb = arr_i(bbno, idx);
			c->bb_enter = arr_i(bben, idx);
			if (g->layers && tx >= 0 && ty >= 0) {
				c->tt = arr2_i(tt, tx, ty);
				c->tz = arr2_i(tz, tx, ty);
				c->tv = arr2_i(tv, tx, ty);
				c->nn = arr2_i(nn, tx, ty);
			}
			k = kind_tt(c->tt);
			if (!k)
				k = kind_obj(c->obj);
			if (!k) {
				if (c->type <= 0)
					k = K_VOID;
				else if (c->type < 101)
					k = K_WALL;
				else if (c->enter == 0)
					k = K_PIT;
				else
					k = K_FLOOR;
			}
			c->kind = (signed char)k;
			c->radius = 0;
			if (hazard(k)) {
				float r = 0;
				if (c->obj > 0)
					r = hit_radius(c->obj, c->obj_enter);
				if (c->bb > 0) {
					float rb = hit_radius(c->bb, c->bb_enter);
					if (rb > r)
						r = rb;
				}
				if (r <= 0.f)
					r = 2.f;
				c->radius = r;
				if (g->nh < MAX_HZ) {
					g->hz[g->nh].x = x;
					g->hz[g->nh].z = z;
					g->hz[g->nh].r = r;
					g->nh++;
				}
			}
		}
	}
	read_player(&g->px, &g->pz, &g->wx, &g->wy, &g->wz);
	/* The .dat type/enter arrays are empty on this map. The walk mesh is
	 * the floor; maptt/maptz/maptv/mapnn stay the event layer. */
	{
		int mesh_n = 0, type_n = 0;
		unsigned char mesh[MAP][MAP];
		struct RE_instance *inst = map_instance(g);
		for (z = 0; z < MAP; z++) {
			for (x = 0; x < MAP; x++) {
				float wx, wz, h;
				vec2 xz;
				int hit = 0;
				if (g->c[z][x].type)
					type_n++;
				cell_center(x, z, &wx, &wz);
				xz[0] = wx;
				xz[1] = -wz;
				if (inst && inst->model && inst->model->collider
						&& collider_height(inst->model->collider, xz, &h))
					hit = 1;
				mesh[z][x] = (unsigned char)hit;
				if (hit)
					mesh_n++;
			}
		}
		g->mesh_n = mesh_n;
		if (type_n == 0 && mesh_n > 0) {
			g->nh = 0;
			for (z = 0; z < MAP; z++) {
				for (x = 0; x < MAP; x++) {
					struct cell *c = &g->c[z][x];
					int i, nbor = 0;
					c->enter = 0;
					if (mesh[z][x]) {
						for (i = 0; i < 4; i++) {
							int x2 = x + DX[i], z2 = z + DZ[i];
							unsigned bit = i == 0 ? 4 : i == 1 ? 8 : i == 2 ? 1 : 2;
							if (in_map(x2, z2) && mesh[z2][x2])
								c->enter |= bit;
						}
					}
					for (i = 0; i < 4; i++) {
						int x2 = x + DX[i], z2 = z + DZ[i];
						if (in_map(x2, z2) && mesh[z2][x2])
							nbor++;
					}
					if (!hazard(c->kind)) {
						if (mesh[z][x])
							c->kind = K_FLOOR;
						else if (nbor >= 4)
							c->kind = K_PIT;
						else if (nbor >= 1)
							c->kind = K_WALL;
						else
							c->kind = K_VOID;
					}
					if (hazard(c->kind) && g->nh < MAX_HZ) {
						g->hz[g->nh].x = x;
						g->hz[g->nh].z = z;
						g->hz[g->nh].r = c->radius > 0.f ? c->radius : 2.f;
						g->nh++;
					}
				}
			}
		}
	}
}

static void flood(struct grid *g)
{
	int queue[MAP * MAP];
	int qh = 0, qt = 0;
	int x, z;
	memset(g->reach, 0, sizeof g->reach);
	g->minx = g->px;
	g->maxx = g->px;
	g->minz = g->pz;
	g->maxz = g->pz;
	if (!in_map(g->px, g->pz))
		return;
	g->reach[g->pz][g->px] = 1;
	queue[qt++] = g->pz * MAP + g->px;
	while (qh < qt) {
		int cur = queue[qh++];
		int cx = cur % MAP, cz = cur / MAP;
		int i;
		for (i = 0; i < 4; i++) {
			int x2, z2, k;
			if (!edge_open(g, cx, cz, i))
				continue;
			x2 = cx + DX[i];
			z2 = cz + DZ[i];
			if (g->reach[z2][x2])
				continue;
			k = g->c[z2][x2].kind;
			if (k == K_VOID)
				continue;
			g->reach[z2][x2] = 1;
			if (k == K_FLOOR)
				queue[qt++] = z2 * MAP + x2;
		}
	}
	for (z = 0; z < MAP; z++) {
		for (x = 0; x < MAP; x++) {
			if (!g->reach[z][x])
				continue;
			if (x < g->minx) g->minx = x;
			if (x > g->maxx) g->maxx = x;
			if (z < g->minz) g->minz = z;
			if (z > g->maxz) g->maxz = z;
		}
	}
	if (g->minx > 0) g->minx--;
	if (g->minz > 0) g->minz--;
	if (g->maxx + 1 < MAP) g->maxx++;
	if (g->maxz + 1 < MAP) g->maxz++;
}

static int aim_self(struct grid *g)
{
	struct RE_instance *inst = map_instance(g);
	float hx, hy, hz;
	int sx, sy, cx, cz;
	g->aim_ok = 0;
	if (!project_cell(g, g->px, g->pz, &sx, &sy, NULL))
		return 0;
	g->aim_x = sx;
	g->aim_y = sy;
	if (!inst || !ray_cell(inst, sx, sy, &hx, &hy, &hz, &cx, &cz))
		return 0;
	g->aim_hx = cx;
	g->aim_hz = cz;
	g->aim_ok = (cx == g->px && cz == g->pz);
	return g->aim_ok;
}

static char *grid_json(struct grid *g)
{
	cJSON *root = cJSON_CreateObject();
	cJSON *player, *cam, *aim, *room, *rows, *events;
	char *printed;
	int x, z, n = 0;
	player = cJSON_AddObjectToObject(root, "player");
	cJSON_AddNumberToObject(root, "mapNo", g->map_no);
	cJSON_AddNumberToObject(root, "dataMap", g->data_map);
	cJSON_AddNumberToObject(root, "bmap", g->bmap);
	cJSON_AddBoolToObject(root, "layers", g->layers);
	cJSON_AddNumberToObject(root, "moveType", g->move_type);
	cJSON_AddBoolToObject(root, "areaShown", g->area);
	cJSON_AddBoolToObject(root, "keyMode", g->key_mode);
	cJSON_AddNumberToObject(root, "meshCells", g->mesh_n);
	cJSON_AddNumberToObject(root, "messageNo", gint("g_nMessageNo"));
	{
		struct page *data = fpage(gstruct("g_sMapData"), "m_sData");
		struct page *types = fpage(data, "m_anType");
		struct page *maps = garray(U8_M);
		struct page *slot = maps ? arr_p(maps, g->bmap) : NULL;
		struct page *tt = fpage(slot, "maptt");
		cJSON *st = cJSON_AddObjectToObject(root, "stats");
		const char *dn = "";
		int nonzero = 0, ttcount = 0, i;
		if (data && data->type == STRUCT_PAGE && data->index >= 0 && data->index < ain->nr_structures)
			dn = ain->structures[data->index].name ? ain->structures[data->index].name : "";
		cJSON_AddStringToObject(st, "dataStruct", dn);
		cJSON_AddNumberToObject(st, "typeN", types ? types->nr_vars : -1);
		cJSON_AddNumberToObject(st, "typeHere", arr_i(types, g->pz * MAP + g->px));
		cJSON_AddNumberToObject(st, "enterHere", g->c[g->pz][g->px].enter);
		cJSON_AddNumberToObject(st, "kindHere", g->c[g->pz][g->px].kind);
		if (types) {
			for (i = 0; i < types->nr_vars; i++) {
				if (arr_i(types, i))
					nonzero++;
			}
		}
		cJSON_AddNumberToObject(st, "typeNonZero", nonzero);
		cJSON_AddNumberToObject(st, "ttN", tt ? tt->nr_vars : -1);
		if (tt) {
			for (i = 0; i < tt->nr_vars && i < 100; i++) {
				struct page *row = arr_p(tt, i);
				int y;
				if (!row || row->type != ARRAY_PAGE)
					continue;
				for (y = 0; y < row->nr_vars && y < 100; y++) {
					if (arr_i(row, y))
						ttcount++;
				}
			}
		}
		cJSON_AddNumberToObject(st, "ttNonZero", ttcount);
		cJSON_AddNumberToObject(st, "objHere", g->c[g->pz][g->px].obj);
		cJSON_AddNumberToObject(st, "hazards", g->nh);
	}
	cJSON_AddNumberToObject(player, "x", g->px);
	cJSON_AddNumberToObject(player, "z", g->pz);
	cJSON_AddNumberToObject(player, "tx", g->px + 1);
	cJSON_AddNumberToObject(player, "ty", MAP - g->pz);
	cam = cJSON_AddObjectToObject(root, "camera");
	cJSON_AddNumberToObject(cam, "length", g->cam_len);
	cJSON_AddNumberToObject(cam, "yaw", g->cam_yaw);
	cJSON_AddNumberToObject(cam, "pitch", g->cam_pitch);
	cJSON_AddNumberToObject(cam, "viewX", g->view[0]);
	cJSON_AddNumberToObject(cam, "viewY", g->view[1]);
	cJSON_AddNumberToObject(cam, "viewW", g->view[2]);
	cJSON_AddNumberToObject(cam, "viewH", g->view[3]);
	aim = cJSON_AddObjectToObject(root, "aim");
	cJSON_AddNumberToObject(aim, "x", g->aim_x);
	cJSON_AddNumberToObject(aim, "y", g->aim_y);
	cJSON_AddNumberToObject(aim, "hitX", g->aim_hx);
	cJSON_AddNumberToObject(aim, "hitZ", g->aim_hz);
	cJSON_AddBoolToObject(aim, "ok", g->aim_ok);
	room = cJSON_AddObjectToObject(root, "room");
	cJSON_AddNumberToObject(room, "minX", g->minx);
	cJSON_AddNumberToObject(room, "minZ", g->minz);
	cJSON_AddNumberToObject(room, "maxX", g->maxx);
	cJSON_AddNumberToObject(room, "maxZ", g->maxz);
	cJSON_AddStringToObject(root, "legend",
		"@ player  . floor  # wall  o pit  C chest  R red  t trap  F fight  P portal  E event");
	cJSON_AddStringToObject(root, "axes",
		"x east (right arrow), z north (up arrow). tx=x+1 ty=50-z. cell is 4 world units. rows run north to south.");
	rows = cJSON_AddArrayToObject(root, "rows");
	for (z = g->maxz; z >= g->minz; z--) {
		char line[MAP + 1];
		int nch = 0;
		for (x = g->minx; x <= g->maxx && nch < MAP; x++) {
			if (x == g->px && z == g->pz)
				line[nch++] = '@';
			else
				line[nch++] = kind_char(g->c[z][x].kind);
		}
		line[nch] = 0;
		cJSON_AddItemToArray(rows, cJSON_CreateString(line));
	}
	events = cJSON_AddArrayToObject(root, "events");
	for (z = g->minz; z <= g->maxz; z++) {
		for (x = g->minx; x <= g->maxx; x++) {
			struct cell *c = &g->c[z][x];
			cJSON *e;
			char ent[8];
			int ep = 0;
			if (!hazard(c->kind) && c->kind != K_PIT)
				continue;
			if (n++ > 300)
				continue;
			e = cJSON_CreateObject();
			cJSON_AddNumberToObject(e, "x", x);
			cJSON_AddNumberToObject(e, "z", z);
			cJSON_AddStringToObject(e, "kind", kind_name(c->kind));
			cJSON_AddNumberToObject(e, "tt", c->tt);
			cJSON_AddNumberToObject(e, "tz", c->tz);
			cJSON_AddNumberToObject(e, "tv", c->tv);
			cJSON_AddNumberToObject(e, "nn", c->nn);
			cJSON_AddNumberToObject(e, "obj", c->obj);
			cJSON_AddNumberToObject(e, "type", c->type);
			cJSON_AddNumberToObject(e, "ground", c->ground);
			cJSON_AddNumberToObject(e, "radius", c->radius);
			if (c->enter & 1) ent[ep++] = 'N';
			if (c->enter & 2) ent[ep++] = 'S';
			if (c->enter & 4) ent[ep++] = 'E';
			if (c->enter & 8) ent[ep++] = 'W';
			ent[ep] = 0;
			cJSON_AddStringToObject(e, "enter", ent);
			cJSON_AddItemToArray(events, e);
		}
	}
	printed = cJSON_PrintUnformatted(root);
	cJSON_Delete(root);
	if (!printed)
		return NULL;
	{
		size_t nlen = strlen(printed);
		char *out = (char *)malloc(nlen + 2);
		if (!out) {
			free(printed);
			return NULL;
		}
		memcpy(out, printed, nlen);
		out[nlen] = '\n';
		out[nlen + 1] = 0;
		free(printed);
		return out;
	}
}

static int snapshot(struct grid *g, char *err, int errsz)
{
	fill_cells(g);
	if (g->map_no <= 0 || !in_map(g->px, g->pz)) {
		if (err)
			snprintf(err, errsz, "not-in-dungeon");
		return 0;
	}
	flood(g);
	aim_self(g);
	return 1;
}

int rq_path_grid(char **json_out, char *err, int errsz)
{
	if (!snapshot(&G, err, errsz))
		return 0;
	*json_out = grid_json(&G);
	if (!*json_out) {
		if (err)
			snprintf(err, errsz, "json");
		return 0;
	}
	return 1;
}

static int pick_portal(struct grid *g, int *gx, int *gz)
{
	int x, z, found = 0;
	int pathx[MAX_PATH], pathz[MAX_PATH];
	for (z = 0; z < MAP; z++) {
		for (x = 0; x < MAP; x++) {
			if (!g->reach[z][x] || g->c[z][x].kind != K_PORTAL)
				continue;
			if (!bfs(g, g->px, g->pz, x, z, pathx, pathz))
				continue;
			if (!found || x > *gx || (x == *gx && z > *gz)) {
				*gx = x;
				*gz = z;
				found = 1;
			}
		}
	}
	return found;
}

static void click_at(struct grid *g, int x, int y)
{
	int btn = g->move_type == 1 ? VK_LBUTTON : VK_RBUTTON;
	mouse_force_pos(x, y);
	Sleep(40);
	key_set_state(btn, 1);
	Sleep(100);
	key_set_state(btn, 0);
}

/* View center. The camera looks at Rance, so this spot is the player,
 * not a chest. Arrow keys are ignored until the window is focused and
 * the cursor has clicked this explorer view and stays on it. */
static void explorer_center(struct grid *g, int *x, int *y)
{
	if (g->view[2] > 0 && g->view[3] > 0) {
		*x = g->view[0] + g->view[2] / 2;
		*y = g->view[1] + g->view[3] / 2;
	} else {
		*x = 408;
		*y = 308;
	}
}

static void arm_keyboard(struct grid *g)
{
	int x, y;
	/* Hold-walk button, not the pathfind button, so the arming click
	 * does not start a route. At the view center the offset is zero. */
	int btn = g->move_type == 1 ? VK_RBUTTON : VK_LBUTTON;
	explorer_center(g, &x, &y);
	agent_focus_window();
	mouse_force_pos(x, y);
	Sleep(60);
	key_set_state(btn, 1);
	Sleep(70);
	key_set_state(btn, 0);
	Sleep(40);
	mouse_force_pos(x, y);
	g->keys_armed = 1;
}

static void arrow_toward(struct grid *g, int dx, int dz)
{
	int vk = 0;
	int x, y;
	if (dx > 0)
		vk = VK_RIGHT;
	else if (dx < 0)
		vk = VK_LEFT;
	else if (dz > 0)
		vk = VK_UP;
	else if (dz < 0)
		vk = VK_DOWN;
	if (!vk)
		return;
	if (!g->keys_armed)
		arm_keyboard(g);
	explorer_center(g, &x, &y);
	agent_focus_window();
	mouse_force_pos(x, y);
	key_set_state(vk, 1);
}

static int wait_move(struct grid *g, int x0, int z0, int tx, int tz, int portal_goal,
		int *ox, int *oz, int *warped, int *msg, unsigned long limit_ms)
{
	/* Game clock and a check every 40 ms of game time, so a faster game
	 * (rq.speed) does not cross three cells between checks and look like a warp. */
	unsigned long long t0 = vm_game_ticks();
	unsigned long long still_at = t0;
	int lastx = x0, lastz = z0;
	double scale = vm_time_scale();
	unsigned long step_ms = scale > 1.0 ? (unsigned long)(40.0 / scale) : 40;
	*warped = 0;
	*msg = 0;
	*ox = x0;
	*oz = z0;
	while (vm_game_ticks() - t0 < limit_ms) {
		int x, z, bmap, mapno;
		float wx, wy, wz;
		Sleep(step_ms ? step_ms : 1);
		if (message_up()) {
			*msg = 1;
			read_player(ox, oz, &wx, &wy, &wz);
			return 0;
		}
		if (!read_player(&x, &z, &wx, &wy, &wz))
			continue;
		*ox = x;
		*oz = z;
		bmap = read_bmap();
		mapno = fint(gstruct("g_sMapParts"), "m_nMapNo");
		if ((mapno > 0 && mapno != g->map_no) || (bmap != g->bmap)
				|| cheb(lastx, lastz, x, z) >= 3) {
			*warped = 1;
			return 1;
		}
		if (x == tx && z == tz)
			return 1;
		if (x != lastx || z != lastz) {
			lastx = x;
			lastz = z;
			still_at = vm_game_ticks();
		} else if (vm_game_ticks() - still_at > 700 && vm_game_ticks() - t0 > 400) {
			return x != x0 || z != z0;
		}
		(void)portal_goal;
	}
	return 0;
}

int rq_path_goto(int gx, int gz, int pick, char **json_out, char *err, int errsz)
{
	cJSON *root, *inputs, *trail, *goal;
	char *printed;
	char inbuf[MAX_IN][40];
	int n_in = 0;
	int trailx[MAX_IN], trailz[MAX_IN], n_tr = 0;
	int pathx[MAX_PATH], pathz[MAX_PATH];
	int steps = 0, stalls = 0, prefer_arrow = 0;
	int warped = 0, msg = 0, ok = 0;
	int endx = 0, endz = 0;
	const char *reason = "step-limit";
	int goal_portal = 0;
	int i;

	if (!snapshot(&G, err, errsz))
		return 0;
	if (pick) {
		if (!pick_portal(&G, &gx, &gz)) {
			if (err)
				snprintf(err, errsz, "no-portal");
			return 0;
		}
	}
	if (!in_map(gx, gz)) {
		if (err)
			snprintf(err, errsz, "bad-cell");
		return 0;
	}
	if (hazard(G.c[gz][gx].kind) && G.c[gz][gx].kind != K_PORTAL) {
		if (err)
			snprintf(err, errsz, "target-event");
		return 0;
	}
	if (G.c[gz][gx].kind == K_VOID || G.c[gz][gx].kind == K_WALL
			|| G.c[gz][gx].kind == K_PIT) {
		if (err)
			snprintf(err, errsz, "blocked");
		return 0;
	}
	goal_portal = G.c[gz][gx].kind == K_PORTAL;
	endx = G.px;
	endz = G.pz;
	trailx[n_tr] = endx;
	trailz[n_tr] = endz;
	n_tr++;

	while (steps < MAX_IN) {
		int npath, j, acted = 0, progress;
		int sx = 0, sy = 0;
		int x, z;
		float wx, wy, wz;
		if (message_up()) {
			msg = 1;
			reason = "message";
			break;
		}
		if (!read_player(&x, &z, &wx, &wy, &wz)) {
			reason = "lost-player";
			break;
		}
		G.px = x;
		G.pz = z;
		G.wx = wx;
		G.wy = wy;
		G.wz = wz;
		endx = x;
		endz = z;
		if (warped) {
			ok = goal_portal;
			reason = goal_portal ? "portal" : "unexpected-warp";
			break;
		}
		if (x == gx && z == gz && !goal_portal) {
			ok = 1;
			reason = "arrived";
			break;
		}
		npath = bfs(&G, x, z, gx, gz, pathx, pathz);
		if (npath < 1) {
			reason = "no-path";
			break;
		}
		if (npath == 1) {
			if (!goal_portal) {
				ok = 1;
				reason = "arrived";
				break;
			}
			if (click_safe(&G, gx, gz, gx, gz, &sx, &sy)) {
				click_at(&G, sx, sy);
				snprintf(inbuf[n_in], sizeof inbuf[0], "click %d %d", sx, sy);
				n_in++;
				acted = 1;
			}
			if (!acted) {
				reason = "on-portal";
				break;
			}
		} else if (!prefer_arrow) {
			for (j = npath - 1; j >= 1; j--) {
				if (!click_safe(&G, pathx[j], pathz[j], gx, gz, &sx, &sy))
					continue;
				click_at(&G, sx, sy);
				snprintf(inbuf[n_in], sizeof inbuf[0], "click %d %d", sx, sy);
				n_in++;
				acted = 1;
				break;
			}
		}
		if (!acted && npath >= 2) {
			int dx = pathx[1] - x, dz = pathz[1] - z;
			int holdx, holdy;
			const char *name = dx > 0 ? "right" : dx < 0 ? "left" : dz > 0 ? "up" : "down";
			unsigned long long t0;
			if (!G.keys_armed && n_in < MAX_IN) {
				int ax, ay;
				explorer_center(&G, &ax, &ay);
				snprintf(inbuf[n_in], sizeof inbuf[0], "arm %d %d", ax, ay);
				n_in++;
			}
			arrow_toward(&G, dx, dz);
			explorer_center(&G, &holdx, &holdy);
			t0 = GetTickCount64();
			while (GetTickCount64() - t0 < 1600) {
				int x2, z2, bmap, mapno;
				float a, b, c;
				mouse_force_pos(holdx, holdy);
				Sleep(30);
				if (message_up()) {
					msg = 1;
					break;
				}
				if (!read_player(&x2, &z2, &a, &b, &c))
					continue;
				bmap = read_bmap();
				mapno = fint(gstruct("g_sMapParts"), "m_nMapNo");
				if ((mapno > 0 && mapno != G.map_no) || bmap != G.bmap
						|| cheb(x, z, x2, z2) >= 3) {
					warped = 1;
					endx = x2;
					endz = z2;
					break;
				}
				if (x2 != x || z2 != z)
					break;
			}
			release_keys();
			mouse_force_pos(holdx, holdy);
			if (n_in < MAX_IN) {
				snprintf(inbuf[n_in], sizeof inbuf[0], "%s", name);
				n_in++;
			}
			acted = 1;
			prefer_arrow = 0;
		}
		if (msg) {
			reason = "message";
			break;
		}
		steps++;
		if (!acted) {
			reason = "no-input";
			break;
		}
		if (!warped) {
			int tx = gx, tz = gz;
			progress = wait_move(&G, x, z, tx, tz, goal_portal, &endx, &endz, &warped, &msg, 9000);
			if (msg) {
				reason = "message";
				break;
			}
		} else {
			progress = 1;
		}
		if (n_tr < MAX_IN) {
			trailx[n_tr] = endx;
			trailz[n_tr] = endz;
			n_tr++;
		}
		if (warped) {
			ok = goal_portal;
			reason = goal_portal ? "portal" : "unexpected-warp";
			break;
		}
		if (!progress) {
			stalls++;
			prefer_arrow = 1;
			if (stalls >= 3) {
				reason = "stuck";
				break;
			}
		} else {
			stalls = 0;
			prefer_arrow = 0;
		}
		if (endx == gx && endz == gz && !goal_portal) {
			ok = 1;
			reason = "arrived";
			break;
		}
	}
	release_keys();
	/* The portal script swaps the map a moment after the cell jump. */
	if (warped || read_bmap() != G.bmap) {
		unsigned long long t0 = GetTickCount64();
		while (GetTickCount64() - t0 < 4000) {
			int x, z, mapno;
			float a, b, c;
			Sleep(80);
			if (message_up()) {
				msg = 1;
				ok = 0;
				reason = "message";
				break;
			}
			mapno = fint(gstruct("g_sMapParts"), "m_nMapNo");
			if (!read_player(&x, &z, &a, &b, &c))
				continue;
			endx = x;
			endz = z;
			if ((mapno > 0 && mapno != G.map_no) || cheb(gx, gz, x, z) >= 3)
				break;
		}
	}
	if (message_up()) {
		msg = 1;
		ok = 0;
		reason = "message";
	}

	root = cJSON_CreateObject();
	cJSON_AddBoolToObject(root, "ok", ok);
	cJSON_AddStringToObject(root, "reason", reason);
	cJSON_AddBoolToObject(root, "portal", warped && goal_portal);
	cJSON_AddBoolToObject(root, "message", msg);
	cJSON_AddNumberToObject(root, "mapNo", G.map_no);
	goal = cJSON_AddObjectToObject(root, "goal");
	cJSON_AddNumberToObject(goal, "x", gx);
	cJSON_AddNumberToObject(goal, "z", gz);
	cJSON_AddStringToObject(goal, "kind", kind_name(G.c[gz][gx].kind));
	{
		cJSON *from = cJSON_AddObjectToObject(root, "from");
		cJSON_AddNumberToObject(from, "x", trailx[0]);
		cJSON_AddNumberToObject(from, "z", trailz[0]);
	}
	{
		cJSON *end = cJSON_AddObjectToObject(root, "end");
		int ex = endx, ez = endz;
		float a, b, c;
		int mapno = fint(gstruct("g_sMapParts"), "m_nMapNo");
		read_player(&ex, &ez, &a, &b, &c);
		cJSON_AddNumberToObject(end, "x", ex);
		cJSON_AddNumberToObject(end, "z", ez);
		cJSON_AddNumberToObject(end, "mapNo", mapno);
		cJSON_AddNumberToObject(end, "bmap", read_bmap());
		endx = ex;
		endz = ez;
	}
	inputs = cJSON_AddArrayToObject(root, "inputs");
	for (i = 0; i < n_in; i++)
		cJSON_AddItemToArray(inputs, cJSON_CreateString(inbuf[i]));
	trail = cJSON_AddArrayToObject(root, "trail");
	for (i = 0; i < n_tr; i++) {
		cJSON *p = cJSON_CreateArray();
		cJSON_AddItemToArray(p, cJSON_CreateNumber(trailx[i]));
		cJSON_AddItemToArray(p, cJSON_CreateNumber(trailz[i]));
		cJSON_AddItemToArray(trail, p);
	}
	cJSON_AddNumberToObject(root, "cameraLength", G.cam_len);
	printed = cJSON_PrintUnformatted(root);
	cJSON_Delete(root);
	if (!printed) {
		if (err)
			snprintf(err, errsz, "json");
		return 0;
	}
	{
		size_t nlen = strlen(printed);
		char *out = (char *)malloc(nlen + 2);
		if (!out) {
			free(printed);
			if (err)
				snprintf(err, errsz, "json");
			return 0;
		}
		memcpy(out, printed, nlen);
		out[nlen] = '\n';
		out[nlen + 1] = 0;
		free(printed);
		*json_out = out;
	}
	(void)err;
	(void)errsz;
	return 1;
}

static int ally_select_up(void)
{
	struct page *sel = gstruct("g_sBattleSelect");
	struct page *sk;
	if (!sel || fint(sel, "m_nZ") < 0)
		return 0;
	sk = fpage(sel, "m_rfsSkill");
	return sk && fint(sk, "m_nNo") > 0;
}

/* One world-axis step. Holds the arrow until the cell changes, a message
 * opens, or an ally's battle turn starts. */
int rq_path_step(int dx, int dz, char **json_out, char *err, int errsz)
{
	cJSON *root;
	char *printed;
	int x0, z0, x, z, msg = 0, battle = 0, moved = 0;
	float a, b, c;
	int holdx, holdy;
	unsigned long long t0;
	const char *reason = "timeout";

	if (json_out)
		*json_out = NULL;
	if (!dx && !dz) {
		if (err)
			snprintf(err, errsz, "usage: step up|down|left|right");
		return 0;
	}
	if ((dx && dz) || (dx && dx != 1 && dx != -1) || (dz && dz != 1 && dz != -1)) {
		if (err)
			snprintf(err, errsz, "usage: step up|down|left|right");
		return 0;
	}
	if (!snapshot(&G, err, errsz))
		return 0;
	if (!read_player(&x0, &z0, &a, &b, &c)) {
		if (err)
			snprintf(err, errsz, "no-player");
		return 0;
	}
	arrow_toward(&G, dx, dz);
	explorer_center(&G, &holdx, &holdy);
	t0 = GetTickCount64();
	while (GetTickCount64() - t0 < 1800) {
		mouse_force_pos(holdx, holdy);
		Sleep(30);
		if (message_up()) {
			msg = 1;
			reason = "message";
			break;
		}
		if (ally_select_up()) {
			battle = 1;
			reason = "battle";
			break;
		}
		if (!read_player(&x, &z, &a, &b, &c))
			continue;
		if (x != x0 || z != z0) {
			moved = 1;
			x0 = x;
			z0 = z;
			reason = "moved";
			break;
		}
	}
	release_keys();
	mouse_force_pos(holdx, holdy);
	x = x0;
	z = z0;
	read_player(&x, &z, &a, &b, &c);
	root = cJSON_CreateObject();
	cJSON_AddBoolToObject(root, "ok", moved || msg || battle);
	cJSON_AddStringToObject(root, "reason", reason);
	cJSON_AddBoolToObject(root, "moved", moved);
	cJSON_AddBoolToObject(root, "message", msg);
	cJSON_AddBoolToObject(root, "battle", battle);
	cJSON_AddNumberToObject(root, "x", x);
	cJSON_AddNumberToObject(root, "z", z);
	cJSON_AddNumberToObject(root, "dx", dx);
	cJSON_AddNumberToObject(root, "dz", dz);
	printed = cJSON_PrintUnformatted(root);
	cJSON_Delete(root);
	if (!printed) {
		if (err)
			snprintf(err, errsz, "json");
		return 0;
	}
	if (json_out) {
		size_t nlen = strlen(printed);
		char *out = (char *)malloc(nlen + 2);
		if (!out) {
			free(printed);
			if (err)
				snprintf(err, errsz, "json");
			return 0;
		}
		memcpy(out, printed, nlen);
		out[nlen] = '\n';
		out[nlen + 1] = 0;
		free(printed);
		*json_out = out;
	} else {
		free(printed);
	}
	return 1;
}

#endif /* RQ_MODLOADER */
