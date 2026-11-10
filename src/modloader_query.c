/* Read-only rq.units / items / quests / castle / dungeon.info / battle.snapshot.
 * Same pages the pipe already reads. Copies only: nothing here writes a save.
 */

#ifdef RQ_MODLOADER

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lua.h"
#include "lauxlib.h"

#include "cJSON.h"
#include "system4.h"
#include "system4/ain.h"
#include "system4/string.h"
#include "system4/utfsjis.h"

#include "modloader.h"
#include "vm.h"
#include "vm/heap.h"
#include "vm/page.h"

#define Q_UNITS 128
#define Q_QUESTS 512
#define Q_SKILL 40
#define Q_ACTIVE 8
#define Q_ROOMS 32
#define Q_PARTY 6
#define Q_TEXT 240
#define Q_DESC 400

static int gi_chara = -2, gi_skill = -2, gi_item = -2, gi_item_max = -2;
static int gi_quest = -2, gi_castle_open = -2;
static int gi_area = -2, gi_area_ero = -2, gi_area_name = -2;
static int gi_map_no = -2, gi_map_name = -2, gi_pta = -2;
static int gi_mapparts = -2, gi_player = -2, gi_girls = -2;

static int global_index(int *gi, const char *utf)
{
	if (*gi == -2) {
		char *sj = utf2sjis(utf, 0);
		*gi = sj ? ain_get_global(ain, sj) : -1;
		free(sj);
	}
	if (!ain || *gi < 0 || *gi >= ain->nr_globals || !heap || !heap_size || !heap[0].page)
		return -1;
	return *gi;
}

static struct page *slot_page(int slot)
{
	if (!page_index_valid(slot))
		return NULL;
	return heap_get_page(slot);
}

static int memb(struct page *p, const char *name)
{
	struct ain_struct *s;
	int i;
	if (!p || p->type != STRUCT_PAGE || !ain || p->index < 0 || p->index >= ain->nr_structures)
		return -1;
	s = &ain->structures[p->index];
	for (i = 0; i < s->nr_members && i < p->nr_vars; i++) {
		if (s->members[i].name && !strcmp(s->members[i].name, name))
			return i;
	}
	return -1;
}

static int memb_utf(struct page *p, const char *utf)
{
	char *sj;
	int m;
	if (!utf)
		return -1;
	m = memb(p, utf);
	if (m >= 0)
		return m;
	sj = utf2sjis(utf, 0);
	if (!sj)
		return -1;
	m = memb(p, sj);
	free(sj);
	return m;
}

static struct page *fpage(struct page *p, const char *name)
{
	int m = memb(p, name);
	if (!p || m < 0 || m >= p->nr_vars)
		return NULL;
	return slot_page(p->values[m].i);
}

static struct page *fpage_utf(struct page *p, const char *utf)
{
	int m = memb_utf(p, utf);
	if (!p || m < 0 || m >= p->nr_vars)
		return NULL;
	return slot_page(p->values[m].i);
}

static int fint(struct page *p, const char *name)
{
	int m = memb(p, name);
	if (!p || m < 0 || m >= p->nr_vars)
		return 0;
	return p->values[m].i;
}

static int fint_utf(struct page *p, const char *utf)
{
	int m = memb_utf(p, utf);
	if (!p || m < 0 || m >= p->nr_vars)
		return 0;
	return p->values[m].i;
}

static void copy_text(char *dst, int n, const char *src)
{
	int i = 0;
	if (!dst || n <= 0)
		return;
	dst[0] = 0;
	if (!src)
		return;
	while (*src && i < n - 1) {
		unsigned char c = (unsigned char)*src++;
		if (c == '$') {
			dst[i++] = '\n';
			continue;
		}
		if (c < 0x20 && c != '\n' && c != '\t')
			continue;
		dst[i++] = (char)c;
	}
	dst[i] = 0;
}

static void page_str_at(struct page *p, int m, char *dst, int n)
{
	struct string *s;
	char *u;
	dst[0] = 0;
	if (!p || m < 0 || m >= p->nr_vars || !string_index_valid(p->values[m].i))
		return;
	s = heap_get_string(p->values[m].i);
	if (!s || s->size <= 0)
		return;
	u = sjis2utf(s->text, (size_t)s->size);
	if (!u)
		return;
	copy_text(dst, n, u);
	free(u);
}

static void fstr(struct page *p, const char *name, char *dst, int n)
{
	page_str_at(p, memb(p, name), dst, n);
}

static void fstr_utf(struct page *p, const char *utf, char *dst, int n)
{
	page_str_at(p, memb_utf(p, utf), dst, n);
}

static struct page *arr_at(struct page *arr, int i)
{
	if (!arr || arr->type != ARRAY_PAGE || i < 0 || i >= arr->nr_vars)
		return NULL;
	return slot_page(arr->values[i].i);
}

static int arr_i(struct page *arr, int i)
{
	if (!arr || arr->type != ARRAY_PAGE || i < 0 || i >= arr->nr_vars)
		return 0;
	return arr->values[i].i;
}

static struct page *global_arr(int *gi, const char *utf)
{
	struct page *p;
	if (global_index(gi, utf) < 0)
		return NULL;
	p = slot_page(global_get(*gi).i);
	if (!p || p->type != ARRAY_PAGE)
		return NULL;
	return p;
}

static int global_int(int *gi, const char *utf)
{
	if (global_index(gi, utf) < 0)
		return 0;
	return global_get(*gi).i;
}

static struct page *global_struct(int *gi, const char *utf)
{
	struct page *p;
	if (global_index(gi, utf) < 0)
		return NULL;
	p = slot_page(global_get(*gi).i);
	if (!p || p->type != STRUCT_PAGE)
		return NULL;
	return p;
}

static void global_str(int *gi, const char *utf, char *dst, int n)
{
	struct string *s;
	char *u;
	dst[0] = 0;
	if (global_index(gi, utf) < 0 || !string_index_valid(global_get(*gi).i))
		return;
	s = heap_get_string(global_get(*gi).i);
	if (!s || s->size <= 0)
		return;
	u = sjis2utf(s->text, (size_t)s->size);
	if (!u)
		return;
	copy_text(dst, n, u);
	free(u);
}

static const char *job_name(int job)
{
	static const char *names[] = {
		"-----", "Fighter", "Guard", "Gunner", "Sorcerer",
		"Healer", "Ranger", "Brawler", "Civilian"
	};
	return job >= 1 && job <= 8 ? names[job] : names[0];
}

static const char *item_type_name(int kind)
{
	if (kind == 1)
		return "weapon";
	if (kind == 11)
		return "armor";
	if (kind == 21)
		return "special";
	return "item";
}

static struct page *chara_page(int no)
{
	struct page *arr = global_arr(&gi_chara, "Ｃ");
	if (!arr || no <= 0 || no >= arr->nr_vars)
		return NULL;
	return slot_page(arr->values[no].i);
}

static void skill_name(int id, char *dst, int n)
{
	struct page *arr, *sk;
	dst[0] = 0;
	arr = global_arr(&gi_skill, "Ｓ");
	if (arr && id > 0 && id < arr->nr_vars) {
		sk = slot_page(arr->values[id].i);
		if (sk)
			fstr_utf(sk, "名前", dst, n);
	}
	if (!dst[0])
		snprintf(dst, n, "skill-%d", id);
}

static void push_skill(lua_State *L, int table_abs, int n, int id, const char *name,
		int rank, int cost, int with_rank)
{
	lua_newtable(L);
	lua_pushinteger(L, id);
	lua_setfield(L, -2, "id");
	lua_pushstring(L, name ? name : "");
	lua_setfield(L, -2, "name");
	if (with_rank) {
		lua_pushinteger(L, rank);
		lua_setfield(L, -2, "rank");
		lua_pushinteger(L, cost);
		lua_setfield(L, -2, "cost");
	}
	lua_rawseti(L, table_abs, n);
}

static void push_unit(lua_State *L, int id)
{
	struct page *c = chara_page(id);
	struct page *active, *skills;
	char name[80];
	int job, level, i, n, active_at, learned_at, pot_at;
	if (!c)
		return;
	fstr_utf(c, "名前", name, (int)sizeof name);
	job = fint_utf(c, "職業");
	level = fint_utf(c, "ＬＶ");
	lua_newtable(L);
	lua_pushinteger(L, id);
	lua_setfield(L, -2, "id");
	lua_pushstring(L, name);
	lua_setfield(L, -2, "name");
	lua_pushstring(L, job_name(job));
	lua_setfield(L, -2, "class");
	lua_pushinteger(L, job);
	lua_setfield(L, -2, "classId");
	lua_pushinteger(L, level);
	lua_setfield(L, -2, "level");

	lua_newtable(L);
	lua_pushinteger(L, fint_utf(c, "体力"));
	lua_setfield(L, -2, "hp");
	lua_pushinteger(L, fint_utf(c, "体力最大"));
	lua_setfield(L, -2, "hpMax");
	lua_pushinteger(L, fint_utf(c, "攻撃力"));
	lua_setfield(L, -2, "atk");
	lua_pushinteger(L, fint_utf(c, "防御力"));
	lua_setfield(L, -2, "def");
	lua_pushinteger(L, fint_utf(c, "魔法力"));
	lua_setfield(L, -2, "mag");
	lua_pushinteger(L, fint_utf(c, "魔抵力"));
	lua_setfield(L, -2, "mdef");
	lua_pushinteger(L, fint_utf(c, "命中率"));
	lua_setfield(L, -2, "hit");
	lua_pushinteger(L, fint_utf(c, "回避率"));
	lua_setfield(L, -2, "eva");
	lua_pushinteger(L, fint_utf(c, "必殺率"));
	lua_setfield(L, -2, "crit");
	lua_pushinteger(L, fint_utf(c, "衝撃率"));
	lua_setfield(L, -2, "impact");
	lua_pushinteger(L, fint_utf(c, "耐性率"));
	lua_setfield(L, -2, "resist");
	lua_pushinteger(L, fint_utf(c, "素早さ"));
	lua_setfield(L, -2, "spd");
	lua_setfield(L, -2, "stats");

	lua_newtable(L);
	lua_pushinteger(L, fint_utf(c, "オプション"));
	lua_setfield(L, -2, "special");
	lua_pushinteger(L, fint_utf(c, "武器"));
	lua_setfield(L, -2, "weapon");
	lua_pushinteger(L, fint_utf(c, "防具"));
	lua_setfield(L, -2, "armor");
	lua_pushinteger(L, fint_utf(c, "持ち物"));
	lua_setfield(L, -2, "item");
	lua_setfield(L, -2, "equipment");

	lua_newtable(L);
	active_at = lua_gettop(L);
	active = fpage_utf(c, "登録スキル");
	n = 0;
	if (active && active->type == ARRAY_PAGE) {
		for (i = 0; i < active->nr_vars && n < Q_ACTIVE; i++) {
			int sid = active->values[i].i;
			char sname[80];
			if (sid <= 0)
				continue;
			skill_name(sid, sname, (int)sizeof sname);
			push_skill(L, active_at, ++n, sid, sname, 0, 0, 0);
		}
	}
	lua_setfield(L, -2, "active");

	lua_newtable(L);
	lua_newtable(L);
	learned_at = lua_gettop(L) - 1;
	pot_at = lua_gettop(L);
	skills = fpage_utf(c, "スキル");
	if (skills && skills->type == ARRAY_PAGE) {
		int nl = 0, np = 0;
		for (i = 0; i < skills->nr_vars; i++) {
			struct page *sk = arr_at(skills, i);
			int sid, owned;
			char sname[80];
			if (!sk)
				continue;
			sid = fint_utf(sk, "番号");
			owned = fint_utf(sk, "保有");
			if (sid <= 0)
				continue;
			skill_name(sid, sname, (int)sizeof sname);
			if (owned && nl < Q_SKILL)
				push_skill(L, learned_at, ++nl, sid, sname,
					fint_utf(sk, "ランク"), fint_utf(sk, "コスト"), 1);
			else if (!owned && np < Q_SKILL)
				push_skill(L, pot_at, ++np, sid, sname,
					fint_utf(sk, "ランク"), fint_utf(sk, "コスト"), 1);
		}
	}
	lua_setfield(L, -3, "potential");
	lua_setfield(L, -2, "learned");
}

int rq_query_chara_named(int id)
{
	struct page *c = chara_page(id);
	char name[80];
	if (!c)
		return 0;
	fstr_utf(c, "名前", name, (int)sizeof name);
	return name[0] != 0;
}

static int unit_recruited(int id)
{
	struct page *c = chara_page(id);
	char name[80];
	if (!c || fint_utf(c, "登録状態") == 0)
		return 0;
	fstr_utf(c, "名前", name, (int)sizeof name);
	return name[0] != 0;
}

int rq_query_unit_recruited(int id)
{
	return unit_recruited(id);
}

static int l_units(lua_State *L)
{
	struct page *arr;
	int i, n = 0;
	lua_newtable(L);
	arr = global_arr(&gi_chara, "Ｃ");
	if (!arr)
		return 1;
	for (i = 1; i < arr->nr_vars && n < Q_UNITS; i++) {
		if (!unit_recruited(i))
			continue;
		push_unit(L, i);
		lua_rawseti(L, -2, ++n);
	}
	return 1;
}

static int item_class(struct page *it, int *class_id)
{
	struct page *jobs = fpage_utf(it, "装備職業");
	int i, flags = 0, ids = 0, first = 0;
	*class_id = 0;
	if (!jobs || jobs->type != ARRAY_PAGE)
		return 0;
	for (i = 0; i < jobs->nr_vars; i++) {
		int v = jobs->values[i].i;
		if (v >= 1 && v <= 8) {
			ids++;
			if (!first)
				first = v;
		} else if (v == 1 && i >= 1 && i <= 8) {
			flags++;
			if (!first)
				first = i;
		}
	}
	*class_id = first;
	return ids + flags;
}

static int item_kept(int exist)
{
	return exist != 0 && exist != 6 && exist != 9;
}

static int l_items(lua_State *L)
{
	struct page *arr;
	int i, n = 0, limit;
	lua_newtable(L);
	arr = global_arr(&gi_item, "Ｉ");
	if (!arr)
		return 1;
	limit = global_int(&gi_item_max, "◆Ｉ構造体最大使用位置");
	if (limit <= 0 || limit >= arr->nr_vars)
		limit = arr->nr_vars - 1;
	/* No cap on the rows (2.23.0): the loop stops at the game's last used slot. */
	for (i = 1; i <= limit; i++) {
		struct page *it = arr_at(arr, i);
		char name[80], desc[Q_TEXT];
		int kind, class_id;
		if (!it || !item_kept(fint_utf(it, "存在")))
			continue;
		fstr_utf(it, "名前", name, (int)sizeof name);
		if (!name[0])
			continue;
		fstr_utf(it, "情報", desc, (int)sizeof desc);
		kind = fint_utf(it, "種類");
		item_class(it, &class_id);
		lua_newtable(L);
		lua_pushinteger(L, i);
		lua_setfield(L, -2, "id");
		lua_pushstring(L, name);
		lua_setfield(L, -2, "name");
		lua_pushstring(L, item_type_name(kind));
		lua_setfield(L, -2, "type");
		lua_pushinteger(L, kind);
		lua_setfield(L, -2, "typeId");
		lua_pushstring(L, job_name(class_id));
		lua_setfield(L, -2, "class");
		lua_pushinteger(L, class_id);
		lua_setfield(L, -2, "classId");
		lua_pushinteger(L, fint_utf(it, "装備ＬＶ"));
		lua_setfield(L, -2, "level");
		lua_pushstring(L, desc);
		lua_setfield(L, -2, "description");
		/* 2.23.0: the item number, its place and the vault lock (未確定 2, アイテム_ＬＯＣＫ中). */
		lua_pushinteger(L, fint_utf(it, "固有番号"));
		lua_setfield(L, -2, "no");
		lua_pushinteger(L, fint_utf(it, "存在"));
		lua_setfield(L, -2, "place");
		lua_pushboolean(L, fint_utf(it, "未確定") == 2);
		lua_setfield(L, -2, "lock");
		lua_rawseti(L, -2, ++n);
	}
	return 1;
}

static void quest_desc(struct page *q, char *dst, int n)
{
	struct page *arr = fpage_utf(q, "説明文");
	int i, used = 0;
	dst[0] = 0;
	if (arr && arr->type == ARRAY_PAGE) {
		for (i = 0; i < arr->nr_vars && used < n - 1; i++) {
			char line[Q_TEXT];
			page_str_at(arr, i, line, (int)sizeof line);
			if (!line[0])
				continue;
			if (used && used < n - 1)
				dst[used++] = '\n';
			snprintf(dst + used, n - used, "%s", line);
			used = (int)strlen(dst);
		}
	}
	if (!dst[0])
		fstr_utf(q, "説明文", dst, n);
}

static int l_quests(lua_State *L)
{
	struct page *arr;
	int i, n = 0;
	lua_newtable(L);
	arr = global_arr(&gi_quest, "Ｑ");
	if (!arr)
		return 1;
	for (i = 1; i < arr->nr_vars && n < Q_QUESTS; i++) {
		struct page *q = arr_at(arr, i);
		char name[80], desc[Q_DESC], map[80];
		if (!q)
			continue;
		fstr_utf(q, "名前", name, (int)sizeof name);
		if (!name[0])
			continue;
		quest_desc(q, desc, (int)sizeof desc);
		fstr_utf(q, "発生場所", map, (int)sizeof map);
		lua_newtable(L);
		lua_pushinteger(L, i);
		lua_setfield(L, -2, "id");
		lua_pushstring(L, name);
		lua_setfield(L, -2, "name");
		lua_pushstring(L, desc);
		lua_setfield(L, -2, "description");
		lua_pushstring(L, map);
		lua_setfield(L, -2, "map");
		lua_rawseti(L, -2, ++n);
	}
	return 1;
}

static void push_castle_units(lua_State *L)
{
	struct page *room, *list, *ids, *items;
	int i, n = 0;
	lua_newtable(L);
	room = global_struct(&gi_girls, "g_sGirlsRoom");
	list = fpage(room, "m_sList");
	ids = fpage(list, "m_anGirlsRoomList");
	if (ids && ids->type == ARRAY_PAGE) {
		for (i = 0; i < ids->nr_vars && n < 32; i++) {
			int id = ids->values[i].i;
			struct page *c;
			char name[80];
			if (id <= 0)
				continue;
			c = chara_page(id);
			name[0] = 0;
			if (c)
				fstr_utf(c, "名前", name, (int)sizeof name);
			lua_newtable(L);
			lua_pushinteger(L, id);
			lua_setfield(L, -2, "id");
			lua_pushstring(L, name);
			lua_setfield(L, -2, "name");
			lua_rawseti(L, -2, ++n);
		}
	}
	items = fpage(list, "m_asItem");
	if (n == 0 && items && items->type == ARRAY_PAGE) {
		for (i = 0; i < items->nr_vars && n < 32; i++) {
			struct page *it = arr_at(items, i);
			int id = fint(it, "m_nMonsterNo");
			struct page *c;
			char name[80];
			if (id <= 0)
				continue;
			c = chara_page(id);
			name[0] = 0;
			if (c)
				fstr_utf(c, "名前", name, (int)sizeof name);
			if (!name[0])
				fstr(fpage(it, "m_sName"), "m_szText", name, (int)sizeof name);
			lua_newtable(L);
			lua_pushinteger(L, id);
			lua_setfield(L, -2, "id");
			lua_pushstring(L, name);
			lua_setfield(L, -2, "name");
			lua_rawseti(L, -2, ++n);
		}
	}
}

static int l_castle(lua_State *L)
{
	struct page *area, *ero, *names;
	int open, i, n = 0, any_enabled = 0, any_used = 0;
	open = global_int(&gi_castle_open, "◆ランス城オープン");
	area = global_arr(&gi_area, "ランス城エリア");
	ero = global_arr(&gi_area_ero, "ランス城エリアero");
	names = global_arr(&gi_area_name, "ランス城エリア名");
	lua_newtable(L);
	lua_pushboolean(L, open != 0);
	lua_setfield(L, -2, "unlocked");
	lua_pushinteger(L, open);
	lua_setfield(L, -2, "open");
	lua_newtable(L);
	if (area) {
		int count = area->nr_vars;
		if (names && names->nr_vars > count)
			count = names->nr_vars;
		for (i = 0; i < count && n < Q_ROOMS; i++) {
			char name[80];
			int state = arr_i(area, i);
			int used = arr_i(ero, i) != 0;
			name[0] = 0;
			if (names)
				page_str_at(names, i, name, (int)sizeof name);
			if (!name[0] && state == 0 && !used)
				continue;
			if (state > 0)
				any_enabled = 1;
			if (used)
				any_used = 1;
			lua_newtable(L);
			lua_pushinteger(L, i);
			lua_setfield(L, -2, "id");
			lua_pushstring(L, name);
			lua_setfield(L, -2, "name");
			lua_pushinteger(L, state);
			lua_setfield(L, -2, "state");
			lua_pushboolean(L, state > 0);
			lua_setfield(L, -2, "enabled");
			lua_pushboolean(L, used);
			lua_setfield(L, -2, "used");
			lua_newtable(L);
			lua_setfield(L, -2, "units");
			lua_rawseti(L, -2, ++n);
		}
	}
	lua_setfield(L, -2, "rooms");
	lua_pushboolean(L, open != 0 && (any_enabled || n == 0));
	lua_setfield(L, -2, "enabled");
	lua_pushboolean(L, any_used);
	lua_setfield(L, -2, "used");
	push_castle_units(L);
	lua_setfield(L, -2, "units");
	return 1;
}

static void push_party(lua_State *L)
{
	struct page *arr = global_arr(&gi_pta, "PTA");
	int i, n = 0;
	lua_newtable(L);
	if (!arr)
		return;
	for (i = 1; i <= Q_PARTY && i < arr->nr_vars; i++) {
		int id = arr->values[i].i;
		struct page *c;
		char name[80];
		if (id <= 0)
			continue;
		c = chara_page(id);
		name[0] = 0;
		if (c)
			fstr_utf(c, "名前", name, (int)sizeof name);
		lua_newtable(L);
		lua_pushinteger(L, i);
		lua_setfield(L, -2, "slot");
		lua_pushinteger(L, id);
		lua_setfield(L, -2, "id");
		lua_pushstring(L, name);
		lua_setfield(L, -2, "name");
		lua_rawseti(L, -2, ++n);
	}
}

static int l_dungeon_info(lua_State *L)
{
	struct page *map = global_struct(&gi_mapparts, "g_sMapParts");
	struct page *player = global_struct(&gi_player, "g_sPlayer");
	char name[80];
	int room = 0, x = 0, z = 0;
	name[0] = 0;
	if (map) {
		room = fint(map, "m_nMapNo");
		fstr(fpage(map, "m_sName"), "m_szText", name, (int)sizeof name);
	}
	if (!room)
		room = global_int(&gi_map_no, "◆ＭＡＰ番号");
	if (!name[0])
		global_str(&gi_map_name, "◆ＭＡＰ名前", name, (int)sizeof name);
	if (player) {
		x = fint(player, "m_nMapX");
		z = fint(player, "m_nMapZ");
	}
	lua_newtable(L);
	lua_pushinteger(L, room);
	lua_setfield(L, -2, "roomId");
	lua_pushstring(L, name);
	lua_setfield(L, -2, "roomName");
	lua_pushinteger(L, x);
	lua_setfield(L, -2, "mapX");
	lua_pushinteger(L, z);
	lua_setfield(L, -2, "mapZ");
	lua_newtable(L);
	lua_pushinteger(L, x);
	lua_setfield(L, -2, "x");
	lua_pushinteger(L, z);
	lua_setfield(L, -2, "z");
	lua_setfield(L, -2, "cell");
	push_party(L);
	lua_setfield(L, -2, "party");
	return 1;
}

static void push_battle_side(lua_State *L, const struct rq_qbattle_unit *u, int n)
{
	int i;
	lua_newtable(L);
	for (i = 0; i < n; i++) {
		lua_newtable(L);
		lua_pushinteger(L, u[i].id);
		lua_setfield(L, -2, "id");
		lua_pushinteger(L, u[i].no);
		lua_setfield(L, -2, "no");
		lua_pushstring(L, u[i].name);
		lua_setfield(L, -2, "name");
		lua_rawseti(L, -2, i + 1);
	}
}

static int l_battle_snap(lua_State *L)
{
	struct rq_qbattle b;
	int i;
	memset(&b, 0, sizeof b);
	rq_query_battle(&b);
	lua_newtable(L);
	lua_pushboolean(L, b.up);
	lua_setfield(L, -2, "up");
	push_battle_side(L, b.allies, b.n_allies);
	lua_setfield(L, -2, "allies");
	push_battle_side(L, b.enemies, b.n_enemies);
	lua_setfield(L, -2, "enemies");
	lua_newtable(L);
	for (i = 0; i < b.n_skills; i++) {
		lua_newtable(L);
		lua_pushinteger(L, b.skills[i].id);
		lua_setfield(L, -2, "id");
		lua_pushstring(L, b.skills[i].name);
		lua_setfield(L, -2, "name");
		lua_pushinteger(L, b.skills[i].uses);
		lua_setfield(L, -2, "uses");
		lua_pushboolean(L, b.skills[i].enabled);
		lua_setfield(L, -2, "enabled");
		lua_rawseti(L, -2, i + 1);
	}
	lua_setfield(L, -2, "skills");
	lua_newtable(L);
	for (i = 0; i < b.n_log; i++) {
		lua_pushstring(L, b.log[i]);
		lua_rawseti(L, -2, i + 1);
	}
	lua_setfield(L, -2, "log");
	return 1;
}

static void bind_query(lua_State *L, const char *name, const char *method, lua_CFunction fn)
{
	lua_newtable(L);
	lua_pushcfunction(L, fn);
	lua_setfield(L, -2, method);
	lua_newtable(L);
	lua_pushcfunction(L, fn);
	lua_setfield(L, -2, "__call");
	lua_setmetatable(L, -2);
	lua_setfield(L, -2, name);
}

void rq_query_bind(lua_State *L)
{
	lua_pushstring(L, RQ_VERSION);
	lua_setfield(L, -2, "version");
	bind_query(L, "units", "list", l_units);
	bind_query(L, "items", "list", l_items);
	bind_query(L, "quests", "list", l_quests);
	lua_getfield(L, -1, "quests");
	if (lua_istable(L, -1)) {
		lua_pushcfunction(L, rq_lua_quests_on_clear);
		lua_setfield(L, -2, "on_clear");
		lua_pushcfunction(L, rq_lua_quests_state);
		lua_setfield(L, -2, "state");
		lua_pushcfunction(L, rq_lua_quests_set_state);
		lua_setfield(L, -2, "set_state");
		lua_pushcfunction(L, rq_lua_quests_unlock);
		lua_setfield(L, -2, "unlock");
		lua_pushcfunction(L, rq_lua_quests_on_end);
		lua_setfield(L, -2, "on_end");
		lua_pushcfunction(L, rq_lua_quests_debug_clear);
		lua_setfield(L, -2, "_debug_clear");
		lua_pushcfunction(L, rq_lua_quests_new_this_run);
		lua_setfield(L, -2, "new_this_run");
	}
	lua_pop(L, 1);
	bind_query(L, "castle", "get", l_castle);
	lua_getfield(L, -1, "dungeon");
	if (lua_istable(L, -1)) {
		lua_pushcfunction(L, l_dungeon_info);
		lua_setfield(L, -2, "info");
	}
	lua_pop(L, 1);
	lua_getfield(L, -1, "battle");
	if (lua_istable(L, -1)) {
		lua_pushcfunction(L, l_battle_snap);
		lua_setfield(L, -2, "snapshot");
	}
	lua_pop(L, 1);
}

static cJSON *unit_json(int id)
{
	struct page *c = chara_page(id);
	struct page *active, *skills;
	cJSON *o, *st, *eq, *act, *learned, *pot;
	char name[80];
	int job, i;
	if (!c)
		return cJSON_CreateNull();
	fstr_utf(c, "名前", name, (int)sizeof name);
	job = fint_utf(c, "職業");
	o = cJSON_CreateObject();
	cJSON_AddNumberToObject(o, "id", id);
	cJSON_AddStringToObject(o, "name", name);
	cJSON_AddStringToObject(o, "class", job_name(job));
	cJSON_AddNumberToObject(o, "classId", job);
	cJSON_AddNumberToObject(o, "level", fint_utf(c, "ＬＶ"));
	st = cJSON_AddObjectToObject(o, "stats");
	cJSON_AddNumberToObject(st, "hp", fint_utf(c, "体力"));
	cJSON_AddNumberToObject(st, "hpMax", fint_utf(c, "体力最大"));
	cJSON_AddNumberToObject(st, "atk", fint_utf(c, "攻撃力"));
	cJSON_AddNumberToObject(st, "def", fint_utf(c, "防御力"));
	cJSON_AddNumberToObject(st, "mag", fint_utf(c, "魔法力"));
	cJSON_AddNumberToObject(st, "mdef", fint_utf(c, "魔抵力"));
	cJSON_AddNumberToObject(st, "hit", fint_utf(c, "命中率"));
	cJSON_AddNumberToObject(st, "eva", fint_utf(c, "回避率"));
	cJSON_AddNumberToObject(st, "crit", fint_utf(c, "必殺率"));
	cJSON_AddNumberToObject(st, "impact", fint_utf(c, "衝撃率"));
	cJSON_AddNumberToObject(st, "resist", fint_utf(c, "耐性率"));
	cJSON_AddNumberToObject(st, "spd", fint_utf(c, "素早さ"));
	eq = cJSON_AddObjectToObject(o, "equipment");
	cJSON_AddNumberToObject(eq, "special", fint_utf(c, "オプション"));
	cJSON_AddNumberToObject(eq, "weapon", fint_utf(c, "武器"));
	cJSON_AddNumberToObject(eq, "armor", fint_utf(c, "防具"));
	cJSON_AddNumberToObject(eq, "item", fint_utf(c, "持ち物"));
	act = cJSON_AddArrayToObject(o, "active");
	active = fpage_utf(c, "登録スキル");
	if (active && active->type == ARRAY_PAGE) {
		int n = 0;
		for (i = 0; i < active->nr_vars && n < Q_ACTIVE; i++) {
			int sid = active->values[i].i;
			char sname[80];
			cJSON *s;
			if (sid <= 0)
				continue;
			skill_name(sid, sname, (int)sizeof sname);
			s = cJSON_CreateObject();
			cJSON_AddNumberToObject(s, "id", sid);
			cJSON_AddStringToObject(s, "name", sname);
			cJSON_AddItemToArray(act, s);
			n++;
		}
	}
	learned = cJSON_AddArrayToObject(o, "learned");
	pot = cJSON_AddArrayToObject(o, "potential");
	skills = fpage_utf(c, "スキル");
	if (skills && skills->type == ARRAY_PAGE) {
		int nl = 0, np = 0;
		for (i = 0; i < skills->nr_vars; i++) {
			struct page *sk = arr_at(skills, i);
			int sid, owned;
			char sname[80];
			cJSON *s;
			if (!sk)
				continue;
			sid = fint_utf(sk, "番号");
			owned = fint_utf(sk, "保有");
			if (sid <= 0)
				continue;
			if (owned && nl >= Q_SKILL)
				continue;
			if (!owned && np >= Q_SKILL)
				continue;
			skill_name(sid, sname, (int)sizeof sname);
			s = cJSON_CreateObject();
			cJSON_AddNumberToObject(s, "id", sid);
			cJSON_AddStringToObject(s, "name", sname);
			cJSON_AddNumberToObject(s, "rank", fint_utf(sk, "ランク"));
			cJSON_AddNumberToObject(s, "cost", fint_utf(sk, "コスト"));
			cJSON_AddItemToArray(owned ? learned : pot, s);
			if (owned)
				nl++;
			else
				np++;
		}
	}
	return o;
}

static cJSON *quest_json(int id)
{
	struct page *arr = global_arr(&gi_quest, "Ｑ");
	struct page *q;
	cJSON *o;
	char name[80], desc[Q_DESC], map[80];
	if (!arr || id <= 0 || id >= arr->nr_vars)
		return cJSON_CreateNull();
	q = arr_at(arr, id);
	if (!q)
		return cJSON_CreateNull();
	fstr_utf(q, "名前", name, (int)sizeof name);
	if (!name[0])
		return cJSON_CreateNull();
	quest_desc(q, desc, (int)sizeof desc);
	fstr_utf(q, "発生場所", map, (int)sizeof map);
	o = cJSON_CreateObject();
	cJSON_AddNumberToObject(o, "id", id);
	cJSON_AddStringToObject(o, "name", name);
	cJSON_AddStringToObject(o, "description", desc);
	cJSON_AddStringToObject(o, "map", map);
	return o;
}

static cJSON *dungeon_json(void)
{
	struct page *map = global_struct(&gi_mapparts, "g_sMapParts");
	struct page *player = global_struct(&gi_player, "g_sPlayer");
	struct page *pta = global_arr(&gi_pta, "PTA");
	cJSON *o, *cell, *party;
	char name[80];
	int room = 0, x = 0, z = 0, i;
	name[0] = 0;
	if (map) {
		room = fint(map, "m_nMapNo");
		fstr(fpage(map, "m_sName"), "m_szText", name, (int)sizeof name);
	}
	if (!room)
		room = global_int(&gi_map_no, "◆ＭＡＰ番号");
	if (!name[0])
		global_str(&gi_map_name, "◆ＭＡＰ名前", name, (int)sizeof name);
	if (player) {
		x = fint(player, "m_nMapX");
		z = fint(player, "m_nMapZ");
	}
	o = cJSON_CreateObject();
	cJSON_AddNumberToObject(o, "roomId", room);
	cJSON_AddStringToObject(o, "roomName", name);
	cJSON_AddNumberToObject(o, "mapX", x);
	cJSON_AddNumberToObject(o, "mapZ", z);
	cell = cJSON_AddObjectToObject(o, "cell");
	cJSON_AddNumberToObject(cell, "x", x);
	cJSON_AddNumberToObject(cell, "z", z);
	party = cJSON_AddArrayToObject(o, "party");
	if (pta) {
		for (i = 1; i <= Q_PARTY && i < pta->nr_vars; i++) {
			int id = pta->values[i].i;
			struct page *c;
			char pname[80];
			cJSON *row;
			if (id <= 0)
				continue;
			c = chara_page(id);
			pname[0] = 0;
			if (c)
				fstr_utf(c, "名前", pname, (int)sizeof pname);
			row = cJSON_CreateObject();
			cJSON_AddNumberToObject(row, "slot", i);
			cJSON_AddNumberToObject(row, "id", id);
			cJSON_AddStringToObject(row, "name", pname);
			cJSON_AddItemToArray(party, row);
		}
	}
	return o;
}

static cJSON *battle_json(void)
{
	struct rq_qbattle b;
	cJSON *o, *allies, *enemies, *skills, *log;
	int i;
	memset(&b, 0, sizeof b);
	rq_query_battle(&b);
	o = cJSON_CreateObject();
	cJSON_AddBoolToObject(o, "up", b.up);
	allies = cJSON_AddArrayToObject(o, "allies");
	enemies = cJSON_AddArrayToObject(o, "enemies");
	skills = cJSON_AddArrayToObject(o, "skills");
	log = cJSON_AddArrayToObject(o, "log");
	for (i = 0; i < b.n_allies; i++) {
		cJSON *u = cJSON_CreateObject();
		cJSON_AddNumberToObject(u, "id", b.allies[i].id);
		cJSON_AddNumberToObject(u, "no", b.allies[i].no);
		cJSON_AddStringToObject(u, "name", b.allies[i].name);
		cJSON_AddItemToArray(allies, u);
	}
	for (i = 0; i < b.n_enemies; i++) {
		cJSON *u = cJSON_CreateObject();
		cJSON_AddNumberToObject(u, "id", b.enemies[i].id);
		cJSON_AddNumberToObject(u, "no", b.enemies[i].no);
		cJSON_AddStringToObject(u, "name", b.enemies[i].name);
		cJSON_AddItemToArray(enemies, u);
	}
	for (i = 0; i < b.n_skills; i++) {
		cJSON *s = cJSON_CreateObject();
		cJSON_AddNumberToObject(s, "id", b.skills[i].id);
		cJSON_AddStringToObject(s, "name", b.skills[i].name);
		cJSON_AddNumberToObject(s, "uses", b.skills[i].uses);
		cJSON_AddBoolToObject(s, "enabled", b.skills[i].enabled);
		cJSON_AddItemToArray(skills, s);
	}
	for (i = 0; i < b.n_log; i++)
		cJSON_AddItemToArray(log, cJSON_CreateString(b.log[i]));
	return o;
}

int rq_query_facts(char **json_out)
{
	cJSON *root;
	struct page *arr;
	char *printed;
	int i, id = 0;
	size_t len;
	if (!json_out)
		return 0;
	*json_out = NULL;
	root = cJSON_CreateObject();
	if (!root)
		return 0;
	cJSON_AddStringToObject(root, "version", RQ_VERSION);
	arr = global_arr(&gi_chara, "Ｃ");
	if (arr) {
		for (i = 1; i < arr->nr_vars && i < 4000; i++) {
			if (unit_recruited(i)) {
				id = i;
				break;
			}
		}
	}
	if (id)
		cJSON_AddItemToObject(root, "unit", unit_json(id));
	else
		cJSON_AddNullToObject(root, "unit");
	cJSON_AddItemToObject(root, "quest", quest_json(126));
	cJSON_AddItemToObject(root, "dungeon", dungeon_json());
	cJSON_AddItemToObject(root, "battle", battle_json());
	printed = cJSON_PrintUnformatted(root);
	cJSON_Delete(root);
	if (!printed)
		return 0;
	len = strlen(printed);
	*json_out = malloc(len + 2);
	if (!*json_out) {
		free(printed);
		return 0;
	}
	memcpy(*json_out, printed, len);
	(*json_out)[len] = '\n';
	(*json_out)[len + 1] = 0;
	free(printed);
	return 1;
}

#else

void rq_query_bind(struct lua_State *L) { (void)L; }
int rq_query_facts(char **json_out)
{
	if (json_out)
		*json_out = NULL;
	return 0;
}

#endif
