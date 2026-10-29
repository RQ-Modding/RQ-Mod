/* In-engine Lua mods (meson -Dmodloader=true). No RVA inject. */

#ifndef RQ_MODLOADER_H
#define RQ_MODLOADER_H

#include <stdbool.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>

struct ain;
struct lua_State;

#ifdef RQ_MODLOADER

void rq_mods_early_init(void);
void rq_mods_init(struct ain *ain);
void rq_mods_on_vm_call(int fno, int struct_page);
bool rq_mods_swallow_msgbox(const char *title, const char *msg);
void rq_mods_agent_start(void);
void rq_log(const char *fmt, ...);
const char *rq_build_stamp(void);
void rq_format_sp_now(char *buf, size_t n);
void rq_window_title(char *buf, size_t n, const char *game_utf);
int rq_chest_install(struct ain *ain);
int rq_lua_chest_on_attempt(struct lua_State *L);
int rq_lua_chest_on_result(struct lua_State *L);
int rq_lua_chest_get(struct lua_State *L);
int rq_lua_chest_set(struct lua_State *L);
int rq_chest_syscall(int code);
int rq_orb_install(struct ain *ain);
int rq_orb_syscall(int code);
int rq_lua_orb_on_heal(struct lua_State *L);
int rq_lua_orb_roster(struct lua_State *L);
int rq_lua_orb_get_state(struct lua_State *L);
int rq_lua_orb_set_state(struct lua_State *L);
int rq_lua_orb_set_status(struct lua_State *L);
int rq_lua_orb_get_hp(struct lua_State *L);
int rq_lua_orb_get_max_hp(struct lua_State *L);
int rq_lua_orb_set_hp(struct lua_State *L);
int rq_lua_orb_full_heal(struct lua_State *L);
int rq_skill_install(struct ain *ain);
int rq_skill_syscall(int code);
int rq_lua_skill_on_describe(struct lua_State *L);
int rq_lua_skill_get(struct lua_State *L);
int rq_lua_skill_define(struct lua_State *L);
int rq_lua_skill_sort_as(struct lua_State *L);
int rq_chara_install(struct ain *ain);
int rq_chara_syscall(int code);
int rq_lua_chara_grant(struct lua_State *L);
int rq_lua_chara_rank(struct lua_State *L);
int rq_lua_chara_slots(struct lua_State *L);
int rq_lua_chara_set_slot(struct lua_State *L);
int rq_lua_chara_set_flavor(struct lua_State *L);
int rq_lua_chara_weapon_detail(struct lua_State *L);
int rq_lua_chara_apply_mod(struct lua_State *L);
int rq_lua_chara_on_recalc(struct lua_State *L);
int rq_lua_chara_on_offer(struct lua_State *L);
int rq_lua_chara_offers(struct lua_State *L);
int rq_lua_chara_set_offer(struct lua_State *L);
int rq_lua_chara_max_uses(struct lua_State *L);
int rq_lua_chara_set_uses(struct lua_State *L);
int rq_lua_chara_set_bar(struct lua_State *L);
int rq_lua_chara_clear_slot(struct lua_State *L);
int rq_lua_combat_on_physical(struct lua_State *L);
int rq_lua_combat_on_accuracy(struct lua_State *L);
int rq_counter_install(struct ain *ain);
int rq_counter_syscall(int code);
int rq_lua_counter_on_guard_counter(struct lua_State *L);
int rq_battle_install(struct ain *ain);
int rq_battle_syscall(int code);
int rq_lua_battle_on_select_key(struct lua_State *L);
int rq_lua_battle_in_ally_select(struct lua_State *L);
int rq_lua_battle_actor(struct lua_State *L);
int rq_lua_battle_skill_usable(struct lua_State *L);
int rq_shop_install(struct ain *ain);
int rq_shop_syscall(int code);
int rq_lua_shop_on_stock(struct lua_State *L);
int rq_lua_shop_in_stock(struct lua_State *L);
int rq_lua_shop_stock(struct lua_State *L);
int rq_skilllist_install(struct ain *ain);
int rq_skilllist_syscall(int code);
int rq_lua_skilllist_on_open(struct lua_State *L);
int rq_lua_skilllist_on_page(struct lua_State *L);
int rq_lua_skilllist_on_buttons(struct lua_State *L);
int rq_lua_skilllist_set_page(struct lua_State *L);
int rq_lua_skilllist_page_count(struct lua_State *L);
int rq_skilllist_active(void);
int rq_skilllist_page(void);
int rq_skilllist_pages(void);
int rq_skilllist_note_offer(int index, int skill, int has_points, int points);
int rq_skilllist_offer_at(int index, int *skill, int *points);
int rq_loot_install(struct ain *ain);
int rq_loot_syscall(int code);
int rq_loot_list(struct lua_State *L);
int rq_hud_syscall(int code);
int rq_hud_install(struct ain *ain);
int rq_hud_tick(void);
void rq_hud_draw(void);
int rq_hud_block_mouse(void);
int rq_dungeon_active(void);
int rq_dungeon_exploring(void);
void rq_hud_bind_lua(struct lua_State *L);
struct lua_State *rq_lua(void);

int rq_lua_on_frame(struct lua_State *L);
int rq_lua_dungeon_active(struct lua_State *L);
int rq_lua_dungeon_exploring(struct lua_State *L);
int rq_lua_input_mouse(struct lua_State *L);
int rq_lua_input_key(struct lua_State *L);
int rq_lua_hud_rect(struct lua_State *L);
int rq_lua_hud_text(struct lua_State *L);
int rq_lua_hud_cg(struct lua_State *L);
int rq_lua_hud_hit(struct lua_State *L);
int rq_lua_hud_clicked(struct lua_State *L);
int rq_lua_hud_block(struct lua_State *L);
int rq_lua_hud_screen(struct lua_State *L);
int rq_lua_hud_text_width(struct lua_State *L);
int rq_lua_hud_wrap(struct lua_State *L);
int rq_lua_loot_list(struct lua_State *L);

void rq_ui_init(void);
void rq_ui_on_present(void);
void rq_ui_on_parts_input(void);
int rq_ui_wants_frame(void);
int rq_ui_cmd_ui(char **json_out, char *err, int errsz);
int rq_ui_cmd_hover(const char *id, char *err, int errsz);
int rq_ui_cmd_press(const char *id, char *err, int errsz);
int rq_ui_cmd_drag(const char *src, const char *dst, char *err, int errsz);
int rq_ui_has_control(const char *id);
int rq_agent_set(const char *id, const char *label, int button, int hover_only);
/* HUD rect. Listed while registered. hover/press click that rectangle. */
int rq_agent_set_rect(const char *id, const char *label, int x, int y, int w, int h);
void rq_agent_clear(const char *id);
int rq_mods_json(char **json_out);
void rq_ui_note_sprite_text(int sp_no, const char *sjis);
void rq_ui_clear_sprite_text(int sp_no);

int rq_dialog_open(void);
int rq_dialog_has(const char *id);
int rq_dialog_json(char **json_out);
int rq_dialog_hover(const char *id, char *err, int errsz);
int rq_dialog_press(const char *id, char *err, int errsz);

#else

static inline void rq_mods_early_init(void) {}
static inline void rq_mods_init(struct ain *ain) { (void)ain; }
static inline void rq_mods_on_vm_call(int fno, int struct_page)
{
	(void)fno;
	(void)struct_page;
}
static inline bool rq_mods_swallow_msgbox(const char *title, const char *msg)
{
	(void)title;
	(void)msg;
	return false;
}
static inline void rq_mods_agent_start(void) {}
static inline void rq_log(const char *fmt, ...) { (void)fmt; }
static inline const char *rq_build_stamp(void) { return ""; }
static inline void rq_format_sp_now(char *buf, size_t n)
{
	if (buf && n)
		buf[0] = 0;
}
static inline void rq_window_title(char *buf, size_t n, const char *game_utf)
{
	if (!buf || !n)
		return;
	snprintf(buf, n, "%s - XSystem4", game_utf ? game_utf : "Rance Quest");
}
static inline int rq_loot_install(struct ain *ain) { (void)ain; return 0; }
static inline int rq_loot_syscall(int code) { (void)code; return 0; }
static inline int rq_chest_install(struct ain *ain) { (void)ain; return 0; }
static inline int rq_chest_syscall(int code) { (void)code; return 0; }
static inline int rq_orb_install(struct ain *ain) { (void)ain; return 0; }
static inline int rq_orb_syscall(int code) { (void)code; return 0; }
static inline int rq_skill_install(struct ain *ain) { (void)ain; return 0; }
static inline int rq_skill_syscall(int code) { (void)code; return 0; }
static inline int rq_counter_install(struct ain *ain) { (void)ain; return 0; }
static inline int rq_counter_syscall(int code) { (void)code; return 0; }
static inline int rq_battle_install(struct ain *ain) { (void)ain; return 0; }
static inline int rq_battle_syscall(int code) { (void)code; return 0; }
static inline int rq_shop_install(struct ain *ain) { (void)ain; return 0; }
static inline int rq_shop_syscall(int code) { (void)code; return 0; }
static inline int rq_skilllist_install(struct ain *ain) { (void)ain; return 0; }
static inline int rq_skilllist_syscall(int code) { (void)code; return 0; }
static inline int rq_chara_syscall(int code) { (void)code; return 0; }
static inline int rq_hud_syscall(int code) { (void)code; return 0; }
static inline int rq_hud_tick(void) { return 0; }
static inline void rq_hud_draw(void) {}
static inline int rq_hud_block_mouse(void) { return 0; }
static inline void rq_ui_init(void) {}
static inline void rq_ui_on_present(void) {}
static inline void rq_ui_on_parts_input(void) {}
static inline int rq_ui_wants_frame(void) { return 0; }
static inline void rq_ui_note_sprite_text(int sp_no, const char *sjis)
{
	(void)sp_no;
	(void)sjis;
}
static inline void rq_ui_clear_sprite_text(int sp_no) { (void)sp_no; }

#endif

void sys_show_simple_message_box(unsigned flags, const char *title, const char *msg);

#endif /* RQ_MODLOADER_H */
