/* Copyright (C) 2026 Rance Quest modding fork
 *
 * Potential Skills pages. C keeps a 144-row buy list (index 0–143; the
 * write at index 144 is still dropped) and draws Previous / Next on the
 * potential-skills window. Lua owns the page index and whether a click
 * moves. Known Skills is untouched.
 */

#ifdef RQ_MODLOADER

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "lua.h"
#include "lauxlib.h"

#include "system4.h"
#include "system4/ain.h"
#include "system4/buffer.h"
#include "system4/instructions.h"
#include "system4/string.h"
#include "system4/utfsjis.h"

#include "modloader.h"
#include "modloader_priv.h"
#include "vm.h"
#include "vm/heap.h"
#include "vm/page.h"

#define SLOTS 144
#define PAGE 36
#define BTN_PREV 200037
#define BTN_NEXT 200038
#define CG_SKILL_BTN 11153
/*
 * Skill rows are y = 127 + 26*row and 24px tall, so row 17 ends at 593.
 * The yellow help text and stored-points band on CG 11551 starts on the
 * next line and runs to about y = 634. The town system bar starts at 736.
 * 601 (one row under the grid) sits on that band. 652 is the gap under it.
 */
#define BTN_Y 652

static int g_on;
static int g_page = 1;
static int g_pages = 1;
static int g_chara;
static int g_comp = -1;
#define LIST_CB_MAX 16
struct cb_list {
	int n;
	int refs[LIST_CB_MAX];
};
static struct cb_list g_open_cb, g_page_cb, g_btn_cb;

static int g_id[SLOTS];
static int g_pt[SLOTS];
static char *g_desc[SLOTS];

static int g_gi_n = -1;
static int g_gi_id = -1;
static int g_gi_pt = -1;
static int g_gi_desc = -1;

static int g_f_split = -1;
static int g_f_detach = -1;
static int g_f_attach = -1;
static int g_f_draw = -1;
static int g_f_width = -1;
static int g_f_height = -1;
static int g_f_create_sp = -1;
static int g_f_frame = -1;
static int g_f_add = -1;
static int g_f_pos = -1;
static int g_f_active = -1;
static int g_f_sound = -1;
static int g_f_show = -1;
static int g_f_create_skill = -1;
static int g_f_set_show = -1;
static int g_f_sprite_show = -1;
static int g_f_update = -1;
static int g_f_click_snd = -1;
static int g_f_error_snd = -1;

static int g_st_sprite = -1;
static int g_st_text = -1;
static int g_st_button = -1;
static int g_st_metrics = -1;

static uint16_t rd16(const uint8_t *p)
{
	return (uint16_t)(p[0] | (p[1] << 8));
}

static int32_t rd32(const uint8_t *p)
{
	return (int32_t)(p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24));
}

static void wr16(uint8_t *p, uint16_t v)
{
	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
}

static void wr32(uint8_t *p, int32_t v)
{
	uint32_t u = (uint32_t)v;
	p[0] = (uint8_t)u;
	p[1] = (uint8_t)(u >> 8);
	p[2] = (uint8_t)(u >> 16);
	p[3] = (uint8_t)(u >> 24);
}

static int op_at(const uint8_t *code, uint32_t p, uint32_t end)
{
	uint16_t op;
	if (p + 2 > end)
		return -1;
	op = rd16(code + p) & (uint16_t)~OPTYPE_MASK;
	if (op >= NR_OPCODES)
		return -1;
	return (int)op;
}

static int op_width(int op)
{
	int w;
	if (op < 0 || op >= NR_OPCODES)
		return 0;
	w = instruction_width((enum opcode)op);
	return w < 2 ? 0 : w;
}

static int func_end(struct ain *a, uint32_t ip, uint32_t *end_out)
{
	uint32_t p = ip;
	int guard = 0;
	while (p + 2 <= a->code_size && guard < 20000) {
		int op = op_at(a->code, p, a->code_size);
		int w = op_width(op);
		if (w <= 0)
			return 0;
		if (op == ENDFUNC) {
			*end_out = p;
			return 1;
		}
		p += (uint32_t)w;
		guard++;
	}
	return 0;
}

static int find_fn(struct ain *a, const char *utf8)
{
	char *sjis = utf2sjis(utf8, 0);
	int fno;
	if (!sjis)
		return -1;
	fno = ain_get_function(a, sjis);
	free(sjis);
	return fno;
}

static int find_global(struct ain *a, const char *utf8)
{
	char *sjis = utf2sjis(utf8, 0);
	int g;
	if (!sjis)
		return -1;
	g = ain_get_global(a, sjis);
	free(sjis);
	return g;
}

static int find_struct(struct ain *a, const char *name)
{
	int i;
	for (i = 0; i < a->nr_structures; i++) {
		if (a->structures[i].name && !strcmp(a->structures[i].name, name))
			return i;
	}
	return -1;
}

static void emit0(struct buffer *out, enum opcode op)
{
	buffer_write_int16(out, (uint16_t)op);
}

static void emit1(struct buffer *out, enum opcode op, int32_t arg)
{
	buffer_write_int16(out, (uint16_t)op);
	buffer_write_int32(out, (uint32_t)arg);
}

static void commit_wrap(struct ain *a, int fno, struct buffer *out)
{
	a->functions[fno].address = (uint32_t)(a->code_size + 6);
	a->code = xrealloc(a->code, a->code_size + out->index);
	memcpy(a->code + a->code_size, out->buf, out->index);
	a->code_size += out->index;
	free(out->buf);
	out->buf = NULL;
}

static int replace_body(struct ain *a, int fno, int sys)
{
	struct buffer out;
	buffer_init(&out, NULL, 0);
	emit1(&out, FUNC, fno);
	emit1(&out, CALLSYS, sys);
	emit0(&out, RETURN);
	emit1(&out, ENDFUNC, fno);
	commit_wrap(a, fno, &out);
	return 1;
}

static int wrap_func_prefix(struct ain *a, int fno, int sys)
{
	int dup = ain_dup_function(a, fno);
	int n = a->functions[fno].nr_args;
	int i;
	struct buffer out;

	buffer_init(&out, NULL, 0);
	emit1(&out, FUNC, fno);
	emit1(&out, CALLSYS, sys);
	for (i = 0; i < n; i++)
		emit1(&out, SH_LOCALREF, i);
	emit1(&out, CALLFUNC, dup);
	emit0(&out, RETURN);
	emit1(&out, ENDFUNC, fno);
	commit_wrap(a, fno, &out);
	return 1;
}

/* prefix=1 runs CALLSYS before the original method. Otherwise after. */
static int wrap_method(struct ain *a, int fno, int sys, int prefix)
{
	int dup = ain_dup_function(a, fno);
	int n = a->functions[fno].nr_args;
	int i;
	struct buffer out;

	buffer_init(&out, NULL, 0);
	emit1(&out, FUNC, fno);
	if (prefix)
		emit1(&out, CALLSYS, sys);
	emit0(&out, PUSHSTRUCTPAGE);
	for (i = 0; i < n; i++)
		emit1(&out, SH_LOCALREF, i);
	emit1(&out, CALLMETHOD, dup);
	if (!prefix)
		emit1(&out, CALLSYS, sys);
	emit0(&out, RETURN);
	emit1(&out, ENDFUNC, fno);
	commit_wrap(a, fno, &out);
	return 1;
}

static int patch_cap(struct ain *a, int fno, int gi)
{
	uint32_t ip = a->functions[fno].address;
	uint32_t end, p;
	if (!func_end(a, ip, &end))
		return 0;
	for (p = ip; p + 14 <= end; ) {
		int op = op_at(a->code, p, end);
		int w = op_width(op);
		if (w <= 0)
			return 0;
		if (op == SH_GLOBALREF && rd32(a->code + p + 2) == gi
		    && op_at(a->code, p + 6, end) == PUSH
		    && rd32(a->code + p + 8) == 54
		    && op_at(a->code, p + 12, end) == GTE) {
			wr32(a->code + p + 8, SLOTS);
			return 1;
		}
		p += (uint32_t)w;
	}
	return 0;
}

/* Overwrite a store sequence with CALLSYS + JUMP past it. */
static void write_skip(uint8_t *code, uint32_t p, int sys, int nbytes)
{
	wr16(code + p, (uint16_t)CALLSYS);
	wr32(code + p + 2, sys);
	wr16(code + p + 6, (uint16_t)JUMP);
	wr32(code + p + 8, (int32_t)(p + (uint32_t)nbytes));
}

static int patch_stores(struct ain *a, int fno, int gi_n, int gi_id, int gi_pt, int gi_desc)
{
	uint32_t ip = a->functions[fno].address;
	uint32_t end, p;
	int n_id = 0, n_pt = 0, n_desc = 0;
	if (!func_end(a, ip, &end))
		return 0;
	for (p = ip; p + 24 <= end; ) {
		int op = op_at(a->code, p, end);
		int w = op_width(op);
		if (w <= 0)
			return 0;
		if (op == SH_GLOBALREF && p + 24 <= end
		    && op_at(a->code, p + 6, end) == SH_GLOBALREF
		    && rd32(a->code + p + 8) == gi_n
		    && op_at(a->code, p + 12, end) == SH_LOCALREF
		    && op_at(a->code, p + 18, end) == ITOLI
		    && op_at(a->code, p + 20, end) == LI_ASSIGN
		    && op_at(a->code, p + 22, end) == POP) {
			int which = rd32(a->code + p + 2);
			int local = rd32(a->code + p + 14);
			if (which == gi_id && local == 1) {
				write_skip(a->code, p, VM_XSYS_SKILLLIST_STORE_ID, 24);
				n_id++;
				p += 24;
				continue;
			}
			if (which == gi_pt && local == 2) {
				write_skip(a->code, p, VM_XSYS_SKILLLIST_STORE_PT, 24);
				n_pt++;
				p += 24;
				continue;
			}
		}
		if (op == SH_GLOBALREF && rd32(a->code + p + 2) == gi_desc
		    && p + 20 <= end
		    && op_at(a->code, p + 6, end) == SH_GLOBALREF
		    && rd32(a->code + p + 8) == gi_n
		    && op_at(a->code, p + 12, end) == REF
		    && op_at(a->code, p + 14, end) == SH_SASSIGN_LOCALSREF
		    && rd32(a->code + p + 16) == 3) {
			write_skip(a->code, p, VM_XSYS_SKILLLIST_STORE_DESC, 20);
			n_desc++;
			p += 20;
			continue;
		}
		p += (uint32_t)w;
	}
	if (n_id != 2 || n_pt != 1 || n_desc != 1) {
		rq_log("skilllist: store sites id=%d pt=%d desc=%d (want 2,1,1)",
		       n_id, n_pt, n_desc);
		return 0;
	}
	return 1;
}

/* POP the Numof count, slice, jump past Realloc and Sort. */
static int patch_list(struct ain *a, int fno)
{
	uint32_t ip = a->functions[fno].address;
	uint32_t end, p;
	if (!func_end(a, ip, &end))
		return 0;
	for (p = ip; p + 16 <= end; ) {
		int op = op_at(a->code, p, end);
		int w = op_width(op);
		uint32_t sort;
		if (w <= 0)
			return 0;
		if (op == A_NUMOF
		    && op_at(a->code, p + 2, end) == PUSH
		    && rd32(a->code + p + 4) == PAGE
		    && op_at(a->code, p + 8, end) == GT
		    && op_at(a->code, p + 10, end) == IFZ) {
			sort = (uint32_t)rd32(a->code + p + 12);
			if (op_at(a->code, sort, end) == PUSHLOCALPAGE
			    && op_at(a->code, sort + 14, end) == A_SORT) {
				uint32_t after = sort + 16;
				wr16(a->code + p + 2, (uint16_t)POP);
				wr16(a->code + p + 4, (uint16_t)CALLSYS);
				wr32(a->code + p + 6, VM_XSYS_SKILLLIST_SLICE);
				wr16(a->code + p + 10, (uint16_t)JUMP);
				wr32(a->code + p + 12, (int32_t)after);
				return 1;
			}
		}
		p += (uint32_t)w;
	}
	return 0;
}

static struct page *global_array(int gi)
{
	int slot;
	struct page *arr;
	if (gi < 0)
		return NULL;
	slot = global_get(gi).i;
	if (!page_index_valid(slot))
		return NULL;
	arr = heap_get_page(slot);
	if (!arr || arr->type != ARRAY_PAGE)
		return NULL;
	return arr;
}

static void mirror_int(int gi, int idx, int value)
{
	struct page *arr = global_array(gi);
	if (!arr || idx < 0 || idx >= arr->nr_vars)
		return;
	page_set_var(arr, idx, value);
}

static void mirror_str(int idx, const char *sjis)
{
	struct page *arr = global_array(g_gi_desc);
	struct string *s;
	int slot;
	if (!arr || idx < 0 || idx >= arr->nr_vars)
		return;
	slot = page_get_var(arr, idx).i;
	if (!string_index_valid(slot))
		return;
	s = cstr_to_string(sjis ? sjis : "");
	heap_string_assign(slot, s);
	free_string(s);
}

static void buf_clear(void)
{
	int i;
	for (i = 0; i < SLOTS; i++) {
		g_id[i] = 0;
		g_pt[i] = 0;
		free(g_desc[i]);
		g_desc[i] = NULL;
	}
}

static void store_id(void)
{
	int idx = global_get(g_gi_n).i;
	int id = local_get(1).i;
	if (idx < 0 || idx >= SLOTS)
		return;
	g_id[idx] = id;
	mirror_int(g_gi_id, idx, id);
}

static void store_pt(void)
{
	int idx = global_get(g_gi_n).i;
	int pt = local_get(2).i;
	if (idx < 0 || idx >= SLOTS)
		return;
	g_pt[idx] = pt;
	mirror_int(g_gi_pt, idx, pt);
}

static void store_desc(void)
{
	int idx = global_get(g_gi_n).i;
	int slot = local_get(3).i;
	struct string *src;
	char *copy;
	if (idx < 0 || idx >= SLOTS)
		return;
	free(g_desc[idx]);
	g_desc[idx] = NULL;
	if (!string_index_valid(slot)) {
		mirror_str(idx, "");
		return;
	}
	src = heap_get_string(slot);
	if (src && src->size > 0) {
		copy = malloc((size_t)src->size + 1);
		if (copy) {
			memcpy(copy, src->text, (size_t)src->size);
			copy[src->size] = 0;
			g_desc[idx] = copy;
		}
	}
	mirror_str(idx, g_desc[idx] ? g_desc[idx] : "");
}

static void push_stored_desc(int idx)
{
	const char *t = "";
	struct string *s;
	if (idx >= 1 && idx < SLOTS && g_desc[idx])
		t = g_desc[idx];
	s = cstr_to_string(t);
	stack_push(vm_string_ref(s));
	free_string(s);
}

static void ret_at(void)
{
	int i = local_get(0).i;
	if (i < 1 || i >= SLOTS)
		stack_push(-1);
	else
		stack_push(g_id[i]);
}

static void ret_pt(void)
{
	int i = local_get(0).i;
	if (i < 1 || i >= SLOTS)
		stack_push(-1);
	else
		stack_push(g_pt[i]);
}

static void ret_desc(void)
{
	int i = local_get(0).i;
	if (i < 1 || i >= SLOTS)
		push_stored_desc(-1);
	else
		push_stored_desc(i);
}

static void ret_find(void)
{
	int id = local_get(0).i;
	int i;
	if (id <= 0) {
		stack_push(-1);
		return;
	}
	for (i = 1; i < SLOTS; i++) {
		if (g_id[i] == id) {
			stack_push(i);
			return;
		}
	}
	stack_push(-1);
}

static void skilllist_slice(void)
{
	int slot = local_get(1).i;
	struct page *page;
	int n, pages, page_i, off, len, i;
	int *tmp;

	g_chara = local_get(0).i;
	if (!page_index_valid(slot)) {
		g_pages = 1;
		return;
	}
	page = heap_get_page(slot);
	if (!page || page->type != ARRAY_PAGE) {
		g_pages = 1;
		return;
	}
	n = page->nr_vars;
	pages = n <= 0 ? 1 : (n + PAGE - 1) / PAGE;
	g_pages = pages;
	page_i = g_page < 1 ? 1 : g_page;
	if (page_i > pages)
		page_i = pages;
	g_page = page_i;
	off = (page_i - 1) * PAGE;
	if (off > n)
		off = n;
	len = n - off;
	if (len > PAGE)
		len = PAGE;
	if (off > 0 && len > 0) {
		tmp = calloc((size_t)len, sizeof(int));
		if (!tmp)
			return;
		for (i = 0; i < len; i++)
			tmp[i] = page->values[off + i].i;
		for (i = 0; i < len; i++)
			page->values[i].i = tmp[i];
		free(tmp);
	}
	page->nr_vars = len;
	if (len > 1 && g_comp >= 0)
		array_sort(page, g_comp);
}

static void call_lua_open(void)
{
	lua_State *L = rq_lua();
	int i;
	g_page = 1;
	if (!L)
		return;
	for (i = 0; i < g_open_cb.n; i++) {
		lua_rawgeti(L, LUA_REGISTRYINDEX, g_open_cb.refs[i]);
		if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
			rq_log("skilllist: on_open error: %s", lua_tostring(L, -1));
			lua_pop(L, 1);
		}
	}
}

/* 1 and *out set when Lua (or the fallback) produced a page. 0 = deny. */
static int call_lua_page(int dir, int *out)
{
	lua_State *L = rq_lua();
	int i, ok = 0;

	if (!L || g_page_cb.n == 0) {
		*out = g_page + dir;
		if (dir == 0)
			*out = g_page;
		if (*out < 1 || *out > g_pages)
			return 0;
		return 1;
	}
	/* Every callback runs. The last one that answers (a page or false) decides. */
	for (i = 0; i < g_page_cb.n; i++) {
		lua_rawgeti(L, LUA_REGISTRYINDEX, g_page_cb.refs[i]);
		lua_newtable(L);
		lua_pushinteger(L, dir);
		lua_setfield(L, -2, "dir");
		lua_pushinteger(L, g_page);
		lua_setfield(L, -2, "page");
		lua_pushinteger(L, g_pages);
		lua_setfield(L, -2, "pages");
		if (lua_pcall(L, 1, 1, 0) != LUA_OK) {
			rq_log("skilllist: on_page error: %s", lua_tostring(L, -1));
			lua_pop(L, 1);
			continue;
		}
		if (lua_type(L, -1) == LUA_TNUMBER) {
			*out = (int)lua_tointeger(L, -1);
			ok = *out >= 1;
		} else if (!lua_isnil(L, -1)) {
			ok = 0;
		}
		lua_pop(L, 1);
	}
	return ok;
}

static void play_snd(int fno)
{
	if (fno < 0)
		return;
	vm_call(fno, -1);
}

static int call_method(int fno, int obj)
{
	int ret = 0;
	vm_call_method(fno, obj);
	if (ain->functions[fno].return_type.data != AIN_VOID)
		ret = stack_pop().i;
	return ret;
}

static int call_func(int fno)
{
	int ret = 0;
	vm_call(fno, -1);
	if (ain->functions[fno].return_type.data != AIN_VOID)
		ret = stack_pop().i;
	return ret;
}

static void push_label(const char *utf8)
{
	struct string *s = cstr_to_string(utf8);
	stack_push(vm_string_ref(s));
	free_string(s);
}

static void unref_slot(int slot)
{
	if (slot >= 0)
		heap_unref(slot);
}

static int make_button(int group, int id, const char *label, int x, int y, int active, int *out_w)
{
	union vm_value dim;
	struct page *arr;
	int arr_slot = -1;
	int sp1 = -1, sp2 = -1, button = -1, metrics = -1;
	int n, w, h, i;
	int frame[10];
	int ok = 0;

	dim.i = 2;
	arr = alloc_array(1, &dim, AIN_ARRAY_STRUCT, g_st_sprite, true);
	if (!arr)
		return 0;
	arr_slot = heap_alloc_page(arr);
	stack_push(CG_SKILL_BTN);
	stack_push(arr_slot);
	n = call_func(g_f_split);
	if (n != 2)
		goto done;

	sp1 = alloc_struct(g_st_text);
	init_struct(g_st_text, sp1);
	sp2 = alloc_struct(g_st_text);
	init_struct(g_st_text, sp2);

	stack_push(call_method(g_f_detach, arr->values[0].i));
	if (!call_method(g_f_attach, sp1))
		goto done;
	stack_push(call_method(g_f_detach, arr->values[1].i));
	if (!call_method(g_f_attach, sp2))
		goto done;

	w = call_method(g_f_width, sp1);
	h = call_method(g_f_height, sp1);
	if (w < 8 || h < 8)
		goto done;
	if (out_w)
		*out_w = w;

	metrics = alloc_struct(g_st_metrics);
	init_struct(g_st_metrics, metrics);
	if (heap_get_page(metrics)->nr_vars < 13)
		goto done;
	{
		struct page *m = heap_get_page(metrics);
		m->values[0].i = 25;
		m->values[1].i = 25;
		m->values[2].i = 25;
		m->values[3].i = 24;
		m->values[5].i = 258;
		m->values[6].i = 1;
		m->values[7].i = 1;
		m->values[8].i = 1;
		m->values[9].i = 1;
		m->values[10].i = 144;
		m->values[11].i = 144;
		m->values[12].i = 136;
	}
	for (i = 0; i < 2; i++) {
		int dest = i == 0 ? sp1 : sp2;
		stack_push(1);
		push_label(label);
		stack_push(metrics);
		stack_push(-4);
		stack_push(0.0f);
		call_method(g_f_draw, dest);
	}

	button = alloc_struct(g_st_button);
	init_struct(g_st_button, button);
	stack_push(sp1);
	stack_push(-1);
	stack_push(sp2);
	if (!call_method(g_f_create_sp, button))
		goto done;

	frame[1] = w;
	frame[2] = h;
	frame[7] = INT32_MIN;
	frame[8] = INT32_MIN;
	frame[9] = 0;
	for (i = 0; i < 3; i++) {
		frame[0] = i;
		if (i == 1) {
			frame[3] = 255;
			frame[4] = 0;
			frame[5] = 0;
			frame[6] = 128;
		} else {
			frame[3] = 216;
			frame[4] = 255;
			frame[5] = 0;
			frame[6] = 102;
		}
		stack_push(frame[0]);
		stack_push(frame[1]);
		stack_push(frame[2]);
		stack_push(frame[3]);
		stack_push(frame[4]);
		stack_push(frame[5]);
		stack_push(frame[6]);
		stack_push(frame[7]);
		stack_push(frame[8]);
		stack_push(frame[9]);
		call_method(g_f_frame, button);
	}

	stack_push(id);
	stack_push(button);
	if (call_method(g_f_add, group) <= 0)
		goto done;
	stack_push(id);
	stack_push(x);
	stack_push(y);
	call_method(g_f_pos, group);
	stack_push(id);
	stack_push(0);
	call_method(g_f_sound, group);
	stack_push(id);
	stack_push(1);
	call_method(g_f_show, group);
	stack_push(id);
	stack_push(active ? 1 : 0);
	call_method(g_f_active, group);
	ok = 1;
done:
	unref_slot(button);
	unref_slot(metrics);
	unref_slot(sp1);
	unref_slot(sp2);
	unref_slot(arr_slot);
	return ok;
}

/* prev/next are T_Button ids, or 0 when that arrow was not created. */
static void call_lua_buttons(int prev, int next)
{
	lua_State *L = rq_lua();
	int i;

	if (!L)
		return;
	for (i = 0; i < g_btn_cb.n; i++) {
		lua_rawgeti(L, LUA_REGISTRYINDEX, g_btn_cb.refs[i]);
		lua_pushinteger(L, prev);
		lua_pushinteger(L, next);
		if (lua_pcall(L, 2, 0, 0) != LUA_OK) {
			rq_log("skilllist: on_buttons error: %s", lua_tostring(L, -1));
			lua_pop(L, 1);
		}
	}
}

static void skilllist_buttons(void)
{
	int group = member_get(0).i;
	int prev_ok, next_ok, w = 0;
	int x2;
	int prev_id = 0, next_id = 0;
	const int y = BTN_Y;

	if (!page_index_valid(group)) {
		rq_log("skilllist: button group missing");
		call_lua_buttons(0, 0);
		return;
	}
	prev_ok = call_lua_page(-1, &w);
	w = 0;
	if (!make_button(group, BTN_PREV, "Previous Page", 531, y, prev_ok, &w))
		rq_log("skilllist: Previous Page button failed");
	else
		prev_id = BTN_PREV;
	if (prev_id) {
		x2 = 531 + (w > 0 ? w + 8 : 199);
		if (x2 < 730)
			x2 = 730;
		next_ok = call_lua_page(1, &w);
		if (!make_button(group, BTN_NEXT, "Next Page", x2, y, next_ok, NULL))
			rq_log("skilllist: Next Page button failed");
		else
			next_id = BTN_NEXT;
	}
	call_lua_buttons(prev_id, next_id);
}

static void skilllist_click(void)
{
	int id = local_get(0).i;
	int dir = 0;
	int next = 0;
	int obj;

	if (id == BTN_PREV)
		dir = -1;
	else if (id == BTN_NEXT)
		dir = 1;
	else
		return;
	if (!call_lua_page(dir, &next)) {
		play_snd(g_f_error_snd);
		return;
	}
	if (next < 1)
		next = 1;
	g_page = next;
	play_snd(g_f_click_snd);
	obj = vm_frame_struct(0);
	if (obj < 1 || g_f_create_skill < 0)
		return;
	/*
	 * Same sequence as T_SkillPoint::addNewSkill after the purchase.
	 * createSkill deletes the grid and builds the next slice with buttons
	 * hidden (T_Button::clear sets show off, and スキルボタン_登録 never
	 * turns it on). The menu only becomes visible again in setShow, which
	 * create() does once on open and addNewSkill repeats after a buy.
	 * Skipping it left the new 36-slice allocated and invisible.
	 */
	{
		int sp = member_get(2).i;
		int shown = 1;
		if (g_f_sprite_show >= 0 && page_index_valid(sp))
			shown = call_method(g_f_sprite_show, sp) ? 1 : 0;
		stack_push(g_chara);
		stack_push(-1);
		call_method(g_f_create_skill, obj);
		if (g_f_set_show >= 0) {
			stack_push(shown);
			call_method(g_f_set_show, obj);
		}
		if (g_f_update >= 0) {
			stack_push(1);
			call_func(g_f_update);
		}
	}
}

int rq_skilllist_syscall(int code)
{
	switch ((enum vm_extra_syscall)code) {
	case VM_XSYS_SKILLLIST_CLEAR:
		buf_clear();
		return 1;
	case VM_XSYS_SKILLLIST_STORE_ID:
		store_id();
		return 1;
	case VM_XSYS_SKILLLIST_STORE_PT:
		store_pt();
		return 1;
	case VM_XSYS_SKILLLIST_STORE_DESC:
		store_desc();
		return 1;
	case VM_XSYS_SKILLLIST_NUM:
		stack_push(SLOTS - 1);
		return 1;
	case VM_XSYS_SKILLLIST_AT:
		ret_at();
		return 1;
	case VM_XSYS_SKILLLIST_PT:
		ret_pt();
		return 1;
	case VM_XSYS_SKILLLIST_DESC:
		ret_desc();
		return 1;
	case VM_XSYS_SKILLLIST_FIND:
		ret_find();
		return 1;
	case VM_XSYS_SKILLLIST_SLICE:
		skilllist_slice();
		return 1;
	case VM_XSYS_SKILLLIST_OPEN:
		call_lua_open();
		return 1;
	case VM_XSYS_SKILLLIST_BUTTONS:
		skilllist_buttons();
		return 1;
	case VM_XSYS_SKILLLIST_CLICK:
		skilllist_click();
		return 1;
	default:
		return 0;
	}
}

static int need_fn(struct ain *a, const char *name, int *out)
{
	*out = find_fn(a, name);
	if (*out < 0)
		rq_log("skilllist: missing %s", name);
	return *out >= 0;
}

int rq_skilllist_install(struct ain *ain)
{
	int f_pon, f_list, f_reg, f_open, f_mgr, f_exec;
	int f_num, f_at, f_pt, f_desc, f_find;

	if (!ain)
		return 0;
	if (g_on)
		return 1;

	g_gi_n = find_global(ain, "◆取得可能スキル番号");
	g_gi_id = find_global(ain, "取得可能スキル");
	g_gi_pt = find_global(ain, "取得可能スキルポイント");
	g_gi_desc = find_global(ain, "取得不可能スキル説明");
	g_comp = find_fn(ain, "compSkill");
	if (g_gi_n < 0 || g_gi_id < 0 || g_gi_pt < 0 || g_gi_desc < 0 || g_comp < 0) {
		rq_log("skilllist: missing store globals or compSkill");
		return 0;
	}

	if (!need_fn(ain, "スキルポン", &f_pon)
	    || !need_fn(ain, "キャラスキル_追加一覧", &f_list)
	    || !need_fn(ain, "Ｔ購入スキル登録", &f_reg)
	    || !need_fn(ain, "取得可能スキル_数", &f_num)
	    || !need_fn(ain, "取得可能スキル_番号", &f_at)
	    || !need_fn(ain, "取得可能スキル_ポイント", &f_pt)
	    || !need_fn(ain, "取得不可能スキル_説明", &f_desc)
	    || !need_fn(ain, "取得可能スキル_検索", &f_find)
	    || !need_fn(ain, "T_SkillPoint@create", &f_open)
	    || !need_fn(ain, "T_NewSkillManager@create", &f_mgr)
	    || !need_fn(ain, "T_SkillPoint@exec", &f_exec)
	    || !need_fn(ain, "T_SkillPoint@createSkill", &g_f_create_skill)
	    || !need_fn(ain, "T_SkillPoint@setShow", &g_f_set_show)
	    || !need_fn(ain, "T_Sprite@getShow", &g_f_sprite_show)
	    || !need_fn(ain, "UPDATE", &g_f_update)
	    || !need_fn(ain, "ＣＧ_分割_横", &g_f_split)
	    || !need_fn(ain, "T_Sprite@detach", &g_f_detach)
	    || !need_fn(ain, "T_TextSprite@attachSp", &g_f_attach)
	    || !need_fn(ain, "T_TextSprite@textDrawCenter2", &g_f_draw)
	    || !need_fn(ain, "T_TextSprite@getWidth", &g_f_width)
	    || !need_fn(ain, "T_TextSprite@getHeight", &g_f_height)
	    || !need_fn(ain, "T_Button@createSp", &g_f_create_sp)
	    || !need_fn(ain, "T_Button@createFrame", &g_f_frame)
	    || !need_fn(ain, "T_ButtonGroup@addButton", &g_f_add)
	    || !need_fn(ain, "T_ButtonGroup@setButtonPos", &g_f_pos)
	    || !need_fn(ain, "T_ButtonGroup@setButtonActive", &g_f_active)
	    || !need_fn(ain, "T_ButtonGroup@setButtonSoundClick", &g_f_sound)
	    || !need_fn(ain, "T_ButtonGroup@setButtonShow", &g_f_show)
	    || !need_fn(ain, "効果音_クリック", &g_f_click_snd)
	    || !need_fn(ain, "効果音_エラー", &g_f_error_snd))
		return 0;

	g_st_sprite = find_struct(ain, "T_Sprite");
	g_st_text = find_struct(ain, "T_TextSprite");
	g_st_button = find_struct(ain, "T_Button");
	g_st_metrics = find_struct(ain, "sact_text_metrics_t");
	if (g_st_sprite < 0 || g_st_text < 0 || g_st_button < 0 || g_st_metrics < 0) {
		rq_log("skilllist: missing button structs");
		return 0;
	}

	if (!patch_cap(ain, f_pon, g_gi_n)) {
		rq_log("skilllist: cap patch failed");
		return 0;
	}
	if (!patch_stores(ain, f_pon, g_gi_n, g_gi_id, g_gi_pt, g_gi_desc))
		return 0;
	if (!patch_list(ain, f_list)) {
		rq_log("skilllist: list cut patch failed");
		return 0;
	}

	replace_body(ain, f_num, VM_XSYS_SKILLLIST_NUM);
	replace_body(ain, f_at, VM_XSYS_SKILLLIST_AT);
	replace_body(ain, f_pt, VM_XSYS_SKILLLIST_PT);
	replace_body(ain, f_desc, VM_XSYS_SKILLLIST_DESC);
	replace_body(ain, f_find, VM_XSYS_SKILLLIST_FIND);
	wrap_func_prefix(ain, f_reg, VM_XSYS_SKILLLIST_CLEAR);
	wrap_method(ain, f_open, VM_XSYS_SKILLLIST_OPEN, 1);
	wrap_method(ain, f_mgr, VM_XSYS_SKILLLIST_BUTTONS, 0);
	wrap_method(ain, f_exec, VM_XSYS_SKILLLIST_CLICK, 0);

	g_on = 1;
	g_page = 1;
	g_pages = 1;
	rq_log("skilllist: install ok (slots %d, %d per page, cap index 0-%d)",
	       SLOTS, PAGE, SLOTS - 1);
	return 1;
}

static int add_cb(lua_State *L, struct cb_list *list)
{
	luaL_checktype(L, 1, LUA_TFUNCTION);
	if (list->n >= LIST_CB_MAX)
		return luaL_error(L, "too many skilllist callbacks registered");
	lua_pushvalue(L, 1);
	list->refs[list->n++] = luaL_ref(L, LUA_REGISTRYINDEX);
	return 0;
}

int rq_lua_skilllist_on_open(lua_State *L)
{
	return add_cb(L, &g_open_cb);
}

int rq_lua_skilllist_on_page(lua_State *L)
{
	return add_cb(L, &g_page_cb);
}

int rq_lua_skilllist_on_buttons(lua_State *L)
{
	return add_cb(L, &g_btn_cb);
}

int rq_lua_skilllist_set_page(lua_State *L)
{
	int p = (int)luaL_checkinteger(L, 1);
	if (!g_on)
		return luaL_error(L, "skilllist.set_page: call rq.skilllist.install first");
	if (p < 1)
		p = 1;
	g_page = p;
	return 0;
}

int rq_lua_skilllist_page_count(lua_State *L)
{
	if (!g_on)
		return luaL_error(L, "skilllist.page_count: call rq.skilllist.install first");
	lua_pushinteger(L, g_pages);
	return 1;
}

int rq_skilllist_active(void)
{
	return g_on ? 1 : 0;
}

int rq_skilllist_page(void)
{
	return g_on ? g_page : 0;
}

int rq_skilllist_pages(void)
{
	return g_on ? g_pages : 0;
}

/* The buy list UI reads g_id / g_pt, not the globals スキルポン mirrored.
 * A later set_offer has to update this cache or the buttons stay stale. */
int rq_skilllist_note_offer(int index, int skill, int has_points, int points)
{
	if (!g_on || index < 0 || index >= SLOTS)
		return 0;
	g_id[index] = skill;
	if (has_points)
		g_pt[index] = points;
	if (skill <= 0) {
		free(g_desc[index]);
		g_desc[index] = NULL;
	}
	return 1;
}

int rq_skilllist_offer_at(int index, int *skill, int *points)
{
	if (!g_on || index < 0 || index >= SLOTS)
		return 0;
	if (skill)
		*skill = g_id[index];
	if (points)
		*points = g_pt[index];
	return 1;
}

#endif /* RQ_MODLOADER */
