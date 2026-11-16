/* Copyright (C) 2019 Nunuhara Cabbage <nunuhara@haniwa.technology>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://gnu.org/licenses/>.
 */

#ifndef SYSTEM4_VM_H
#define SYSTEM4_VM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "system4.h"

struct ain;
enum ain_data_type;
struct page;
struct string;

// xsystem4-specific system calls (used for hacks)
enum vm_extra_syscall {
	VM_XSYS_KEY_IS_DOWN = 0x80000000,
	VM_XSYS_LOOT_LOCK_TILE = 0x80000001,
	VM_XSYS_LOOT_LOCK_SCAN = 0x80000002,
	VM_XSYS_LOOT_OPEN = 0x80000003,
	VM_XSYS_LOOT_CLEAR = 0x80000005,
	VM_XSYS_HUD_ON = 0x80000006,
	VM_XSYS_HUD_OFF = 0x80000007,
	VM_XSYS_HUD_SUSPEND = 0x80000008,
	VM_XSYS_HUD_RESUME = 0x80000009,
	VM_XSYS_HUD_BATTLE_ON = 0x8000000A,
	VM_XSYS_HUD_BATTLE_OFF = 0x8000000B,
	VM_XSYS_HUD_SHOW = 0x8000000C,
	VM_XSYS_HUD_PROFILE_ON = 0x8000000D,
	VM_XSYS_HUD_PROFILE_OFF = 0x8000000E,
	VM_XSYS_HUD_MODAL_ON = 0x8000000F,
	VM_XSYS_HUD_MODAL_OFF = 0x80000010,
	VM_XSYS_CHEST_ATTEMPT = 0x80000011,
	VM_XSYS_CHEST_PREPARE = 0x80000012,
	VM_XSYS_ORB_HEAL = 0x80000013,
	VM_XSYS_SKILL_DESCRIBE = 0x80000014,
	VM_XSYS_COUNTER_GUARD = 0x80000015,
	VM_XSYS_BATTLE_SELECT_ENTER = 0x80000016,
	VM_XSYS_BATTLE_SELECT_EXIT = 0x80000017,
	VM_XSYS_BATTLE_SELECT_KEY = 0x80000018,
	VM_XSYS_BATTLE_SELECT_EXEC = 0x80000019,
	VM_XSYS_SKILL_BOOT = 0x8000001A,
	VM_XSYS_CHARA_RECALC = 0x8000001B,
	VM_XSYS_CHARA_OFFER = 0x8000001C,
	VM_XSYS_COMBAT_PHYSICAL = 0x8000001D,
	VM_XSYS_COMBAT_ACCURACY = 0x8000001E,
	VM_XSYS_COMBAT_FINAL = 0x8000001F,
	VM_XSYS_COUNTER_ACTOR_RESET = 0x80000020,
	VM_XSYS_COUNTER_ACTOR = 0x80000021,
	VM_XSYS_SKILL_SORT = 0x80000022,
	VM_XSYS_SHOP_STOCK = 0x80000023,
	/* 0x80000024 to 0x80000030: the skill list of loaders 0.2.0 to 2.17.0 (now Lua). */
	VM_XSYS_DISCORD_ADD = 0x80000031,
	VM_XSYS_DISCORD_SHOW = 0x80000032,
	VM_XSYS_DISCORD_TOGGLE = 0x80000033,
	VM_XSYS_DISCORD_DEL = 0x80000034,
	VM_XSYS_TITLE_ON = 0x80000035,
	VM_XSYS_TITLE_OFF = 0x80000036,
	VM_XSYS_ROSTER_DATA = 0x80000037,
	VM_XSYS_ROSTER_CREATE = 0x80000038,
	VM_XSYS_ROSTER_FORGET = 0x80000039,
	VM_XSYS_BATTLE_START = 0x8000003A,
	VM_XSYS_BATTLE_END = 0x8000003B,
	VM_XSYS_QUEST_CLEAR = 0x8000003C,
	VM_XSYS_ITEM_DATA = 0x8000003D,
	VM_XSYS_ITEM_CREATE = 0x8000003E,
	VM_XSYS_ITEM_ENSURE = 0x8000003F,
	VM_XSYS_SHOP_BUY = 0x80000040,
	VM_XSYS_LOOT_PICK = 0x80000041,
	VM_XSYS_QUEST_END = 0x80000042,
	VM_XSYS_MAP_ENTER = 0x80000043,
	VM_XSYS_MAP_EXIT = 0x80000044,
	VM_XSYS_MAP_CLEARED = 0x80000045,
	VM_XSYS_COMBAT_CRIT = 0x80000046,
	VM_XSYS_BATTLE_DOWN = 0x80000047,
	VM_XSYS_BATTLE_MARKS = 0x80000048,
	VM_XSYS_BATTLE_SUPPORT = 0x80000049,
	VM_XSYS_SCENE_BEFORE = 0x8000004A,
	VM_XSYS_ROSTER_CUSTOM = 0x8000004B,
	VM_XSYS_BATTLE_SWITCH_BEFORE = 0x8000004C,
	VM_XSYS_BATTLE_SWITCH_AFTER = 0x8000004D,
	VM_XSYS_STATE_SET = 0x8000004E,
	VM_XSYS_STATE_ASK = 0x8000004F,
	VM_XSYS_STATE_CLEAR = 0x80000050,
	VM_XSYS_CONFIG_TAB = 0x80000051,
	VM_XSYS_IMAGE_STRETCH = 0x80000052,
	VM_XSYS_HUD_RESULT_ON = 0x80000053,
	VM_XSYS_HUD_RESULT_OFF = 0x80000054,
};

// Non-heap values. Stored in pages and on the stack.
union vm_value {
	int32_t i;
	float f;
	void *ref; // for casting HLL return value
};

struct static_hll_function {
	char *name;
	void *fun;
};

struct static_library {
	char *name;
	struct static_hll_function functions[];
};

extern struct ain *ain;

extern union vm_value *stack;
extern int32_t stack_ptr;

static inline union vm_value _vm_id(union vm_value v)
{
	return v;
}

static inline union vm_value vm_int(int32_t v)
{
	return (union vm_value) { .i = v };
}

static inline union vm_value vm_bool(bool b)
{
	return (union vm_value) { .i = !!b };
}

static inline union vm_value vm_float(float v)
{
	return (union vm_value) { .f = v };
}

#define vm_value_cast(v) _Generic((v),				\
				  union vm_value: _vm_id,	\
				  int32_t: vm_int,		\
				  bool: vm_bool,		\
				  float: vm_float)(v)

static inline void stack_set_value(int n, union vm_value v)
{
	stack[stack_ptr - (1 + n)] = v;
}

static inline void stack_push_value(union vm_value v)
{
	stack[stack_ptr++] = v;
}

// Set the Nth value from the top of the stack to V.
#define stack_set(n, v) (stack_set_value((n), vm_value_cast(v)))
#define stack_push(v) (stack_push_value(vm_value_cast(v)))
union vm_value stack_pop(void);
union vm_value stack_peek(int n);
union vm_value *stack_peek_var(void);

union vm_value local_get(int varno);
union vm_value global_get(int varno);
union vm_value member_get(int varno);
void global_set(int varno, union vm_value val, bool call_dtors);
struct page *local_page(void);
struct page *get_local_page(int frame_no);
struct page *get_struct_page(int frame_no);

int vm_string_ref(struct string *s);
int vm_copy_page(struct page *page);
union vm_value vm_copy(union vm_value v, enum ain_data_type type);

int vm_execute_ain(struct ain *program);
void vm_call(int fno, int struct_page);
// Function number on the AIN call stack. depth 0 is the running function, 1 its caller. -1 if none.
int vm_call_stack_fno(int depth);
/* Args are already on the stack. Inserts struct_page under them, then runs the method. */
void vm_call_method(int fno, int struct_page);
/* Heap slot of the struct page for a call frame. 0 is the current frame. -1 if none. */
int vm_frame_struct(int frame_no);
int vm_time(void);
/* rqmod: the game clock behind vm_time, and its speed factor (1 = real time). */
uint64_t vm_game_ticks(void);
void vm_set_time_scale(double scale);
double vm_time_scale(void);
void vm_sleep(int ms);
#ifdef RQ_PROFILE
void vm_prof_ain_stack(char *buf, size_t n);
#endif

void hll_call(int libno, int fno);
bool library_exists(int libno);
bool library_function_exists(int libno, int fno);
void init_libraries(void);
void exit_libraries(void);

// NOTE: This can probably be merged with ain_data_type, since the values are
// disjoint.
enum string_format_type {
	STRFMT_INT = 2,
	STRFMT_FLOAT = 3,
	STRFMT_STRING = 4,
	STRFMT_BOOL = 48,
	STRFMT_LONG_INT = 56,
};
struct string *string_format(struct string *fmt, union vm_value arg, enum string_format_type type);

void vm_stack_trace(void);
_Noreturn void _vm_error(const char *fmt, ...);
_Noreturn void vm_exit(int code);
_Noreturn void vm_reset(void);

extern bool vm_reset_once;

/* index 0 is the current frame. SJIS name, or NULL. */
const char *vm_frame_name(int index);

#if defined(__ANDROID__) || defined(RELEASE)
// Report the error with a message box and exit.
#define VM_ERROR ERROR
#else
#define VM_ERROR(fmt, ...) \
	_vm_error("*ERROR*(%s:%s:%d): " fmt "\n", __FILE__, __func__, __LINE__, ##__VA_ARGS__)
#endif

#ifdef VM_PRIVATE
#include "system4/little_endian.h"
#include "system4/ain.h"

struct function_call {
	int32_t fno;
	uint32_t call_address;
	uint32_t return_address;
	int32_t page_slot;
	int32_t struct_page;
};

extern struct function_call call_stack[4096];
extern int32_t call_stack_ptr;

extern size_t instr_ptr;

// Read argument N for the current instruction.
static inline int32_t get_argument(int n)
{
	return LittleEndian_getDW(ain->code, instr_ptr + 2 + n*4);
}

// XXX: not strictly portable
static inline float get_argument_float(int n)
{
	union vm_value v;
	v.i = LittleEndian_getDW(ain->code, instr_ptr + 2 + n*4);
	return v.f;
}

uint32_t get_switch_address(int no, int val);
uint32_t get_strswitch_address(int no, struct string *str);

int vm_save_image(const char *key, const char *path);
void vm_load_image(const char *key, const char *path);
struct page *vm_load_image_comments(const char *key, const char *path, int *success);
int vm_write_image_comments(const char *key, const char *path, struct page *comments);

#endif /* VM_PRIVATE */
#endif /* SYSTEM4_VM_H */
