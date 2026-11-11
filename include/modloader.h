/* In-engine Lua mods (meson -Dmodloader=true). No RVA inject. */

#ifndef RQ_MODLOADER_H
#define RQ_MODLOADER_H

#include <stdbool.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <setjmp.h>

struct ain;
struct lua_State;
struct cg;
struct cg_metrics;

#ifdef RQ_MODLOADER

void rq_mods_early_init(void);
void rq_mods_init(struct ain *ain);
void rq_mods_on_vm_call(int fno, int struct_page);
/* The crossings between Lua and the game, so the title can close Lua first (modloader_unwind.c). */
int rq_lua_pcall(struct lua_State *L, int nargs, int nresults, int msgh);
bool rq_resetting(void);
extern const char *const rq_title_error;
int rq_cross_vm_enter(void);
jmp_buf *rq_cross_jb(int at);
void rq_cross_vm_leave(int at);
_Noreturn void rq_cross_raise(void);
void rq_cross_reset(void);
void rq_unwind_install(struct lua_State *L);
/* After the title's reset: state the reset left half way, as a call it never ended. */
void rq_scene_after_reset(void);
void rq_hud_after_reset(void);
int rq_hud_menu_open(void);
void rq_events_after_reset(void);
#ifdef lua_pcall
#undef lua_pcall
#define lua_pcall(L, n, r, f) rq_lua_pcall((L), (n), (r), (f))
#endif
void rq_crash_note_log(const char *line);
void rq_crash_note_call(int fno, int struct_page);
void rq_crash_crumb(const char *what, int a, int b);
void rq_crash_vm_error(const char *msg);
void rq_crash_test(void);
/* Samples the game thread's stack (from another thread) into logs\rqmod-where-*.txt. */
int rq_crash_where(char *out, size_t n);
void rq_crash_rotate_logs(const char *dir);
void rq_crash_install(void);
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
/* Mod-folder images served as game CGs (modloader_image.c). */
bool rq_image_has(int id);
struct cg *rq_image_load(int id);
bool rq_image_metrics(int id, struct cg_metrics *out);
void rq_image_resolve_fields(struct lua_State *L, int t);
void rq_image_redirect(int game_id, int custom_id);
int rq_lua_image_add(struct lua_State *L);
int rq_lua_image_replace(struct lua_State *L);
int rq_lua_image_restore(struct lua_State *L);
int rq_lua_image_info(struct lua_State *L);
int rq_lua_image_compose(struct lua_State *L);
/* Bitmaps inside the game's Flash files (modloader_image.c, 2.25.0). */
int rq_lua_image_flash_bitmap(struct lua_State *L);
int rq_lua_image_flash_bitmaps(struct lua_State *L);
typedef void (*rq_flash_bitmap_fn)(void *ctx, int id, int w, int h, const uint8_t *rgba, int mod, int used);
int rq_flash_bitmaps_each(const char *name_utf8, rq_flash_bitmap_fn fn, void *ctx);
/* Text in the HUD font as straight-alpha RGBA (modloader_hud.c). Main thread. */
/* Text in the game's own font (modloader_gametext.c, 2.5.0). */
struct rq_gametext {
	int face, size, r, g, b, weight;
	int edge, er, eg, eb;  /* edge: outline width in pixels, 0 for none */
	int space;             /* the game's fixed spacing (SetFixedWidthSpacing) */
	float spacing;         /* the game's letter spacing (SetSpacing) */
};
struct texture;
int rq_gametext_available(void);
struct texture *rq_gametext_texture(const char *utf8, const struct rq_gametext *st);
int rq_gametext_measure(const char *utf8, const struct rq_gametext *st, int *w, int *h);
int rq_gametext_measure_game(const char *utf8, const struct rq_gametext *st);
unsigned char *rq_gametext_rgba(const char *utf8, const struct rq_gametext *st, int *w, int *h);
int rq_gametext_center_y(const struct rq_gametext *st, int box_h, int line_h);
void rq_gametext_after_draw(void);
struct lua_State;
/* Reads a text style's font = "game" fields (modloader_hud.c); 0 for the HUD font. */
int rq_hud_game_style(struct lua_State *L, int idx, struct rq_gametext *gt, int *has_shadow,
		      int *dx, int *dy, int *sr, int *sg, int *sb, int *sa);
unsigned char *rq_hud_text_rgba(const char *utf8, float size, int r, int g, int b, int edge,
				int bold, int *w, int *h, int *text_w);
int rq_lua_image_add_gif(struct lua_State *L);
int rq_lua_image_remove_gif(struct lua_State *L);
int rq_lua_image_gif_info(struct lua_State *L);

/* GIFs served as Flash files (modloader_anim.c). */
int rq_anim_find(const char *name, uint8_t **data, size_t *size);
void rq_anim_full_name(const char *key, char *out, size_t n);
int rq_battle_mark_file(const char *file);
void rq_battle_forget(void);
void rq_state_forget(void);
void rq_states_forget(void);
int rq_states_second_marks(int iic, int out[3]);
int rq_states_syscall(int code);
void rq_battle_marks_changed(void);
void rq_battle_recheck_card(int iic);
int rq_anim_handle(const char *key);
void rq_anim_redirect(int handle);
int rq_anim_of_cg(int no);
void rq_anim_cg_changed(int no);
/* Called with each GIF frame painted on the whole canvas (ARGB, alpha 0 or 255) and its
 * delay in hundredths of a second as the file gives it. Returns an error or NULL. */
typedef const char *(*gif_frame_fn)(void *ctx, const uint8_t *argb, int w, int h, int delay_cs);
const char *rq_gif_frames(const char *path, gif_frame_fn frame, void *ctx, int *w, int *h, int *count);
int rq_anim_source(const char *key, char *path, size_t n);
int rq_sandbox_resolve(struct lua_State *L, const char *rel, char *out, int n);
int rq_lua_skill_define(struct lua_State *L);
int rq_lua_skill_patch(struct lua_State *L);
int rq_events_install(struct ain *ain);
int rq_events_syscall(int code);
/* Scenes: rq.ain.call, rq.ain.hook, rq.scene (modloader_scene.c). */
extern int g_rq_scene_ret_on;
int rq_scene_syscall(int code);
void rq_scene_on_return(int fno);
void rq_scene_open(struct lua_State *L, int rq_index);
void rq_battle_lua_open(struct lua_State *L, int rq_index);
/* rq.music (modloader_music.c, lua/music.lua, loader 2.4.0). */
struct channel;
struct channel *rq_music_open(int id, int no);
void rq_music_open_lua(struct lua_State *L, int rq_index);
void rq_item_open_lua(struct lua_State *L, int rq_index);
void rq_uikit_open(struct lua_State *L, int rq_index);
/* rq.ids: the game's numbers by name (modloader_ids.c, lua/ids.lua). */
void rq_ids_open(struct lua_State *L, int rq_index);
int rq_lua_battle_on_start(struct lua_State *L);
int rq_lua_battle_on_end(struct lua_State *L);
int rq_lua_battle_on_down(struct lua_State *L);
int rq_lua_battle_on_support(struct lua_State *L);
int rq_lua_battle_on_switch(struct lua_State *L);
int rq_lua_battle_set_flame(struct lua_State *L);
int rq_lua_battle_install_flame(struct lua_State *L);
void rq_battle_flames_clear(void);
void rq_battle_marks_live(int on);
int rq_battle_card_sprite(int no);
int rq_battle_chara_card(int iic);
int rq_battle_live(void);
int rq_battle_face_sprite(int no);
int rq_battle_card_iic(int no);
struct sact_sprite;
int rq_face_render(struct sact_sprite *ss);
/* Moving pictures on game CGs (rq.image, 2.25.0): the frames, fitted to the CG's size. */
struct texture;
int rq_moving_begin(int cg, int w, int h, double fps, int real, const char **err);
const char *rq_moving_frame(int cg, const uint8_t *rgba, int delay_ms);
void rq_moving_end(int cg, int ok);
void rq_moving_drop(int cg);
int rq_moving_has(int cg);
int rq_moving_info(int cg, int *frames, int *ms, int *drawn);
int rq_moving_render(struct sact_sprite *ss);
struct texture *rq_moving_part_frame(int cg, int w, int h);
void rq_moving_tag(int spno, int cg);
int rq_image_syscall(int code);
/* Pictures inside the game's Flash files (rq.image.flash_bitmap, 2.25.0). */
struct swf;
void rq_flash_swap(const char *name, struct swf *swf);
int rq_ui_cmd_flashdump(const char *args, char **json_out, char *err, int errsz);
int rq_lua_battle_set_face(struct lua_State *L);
int rq_lua_battle_get_face(struct lua_State *L);
int rq_lua_battle_on_hp(struct lua_State *L);
void rq_battle_marks_tick(void);
int rq_battle_marks_install(struct ain *a);
int rq_lua_battle_set_buff(struct lua_State *L);
int rq_lua_battle_set_rank_buff(struct lua_State *L);
int rq_lua_battle_set_mark(struct lua_State *L);
int rq_lua_battle_install_states(struct lua_State *L);
void rq_speed_tick(void);
int rq_lua_speed_set(struct lua_State *L);
int rq_lua_speed_get(struct lua_State *L);
void rq_sound_tick(void);
int rq_lua_sound_play(struct lua_State *L);
int rq_lua_sound_play_flash(struct lua_State *L);
int rq_lua_sound_flash_info(struct lua_State *L);
int rq_lua_sound_stop(struct lua_State *L);
int rq_lua_sound_playing(struct lua_State *L);
int rq_lua_sound_set_volume(struct lua_State *L);
int rq_lua_battle_states(struct lua_State *L);
int rq_lua_battle_set_state(struct lua_State *L);
int rq_lua_battle_buffs(struct lua_State *L);
void rq_battle_marks_reload(void);
int rq_lua_quests_on_clear(struct lua_State *L);
int rq_lua_quests_state(struct lua_State *L);
int rq_lua_quests_set_state(struct lua_State *L);
int rq_lua_quests_unlock(struct lua_State *L);
/* Items: define, give, edit, transform (modloader_item.c). */
int rq_item_install(struct ain *ain);
int rq_item_syscall(int code);
int rq_lua_item_define(struct lua_State *L);
int rq_lua_item_edit(struct lua_State *L);
int rq_lua_item_number(struct lua_State *L);
int rq_lua_item_id_of(struct lua_State *L);
int rq_lua_item_get(struct lua_State *L);
int rq_lua_item_find(struct lua_State *L);
int rq_lua_item_count(struct lua_State *L);
int rq_lua_item_set(struct lua_State *L);
int rq_lua_item_give(struct lua_State *L);
int rq_lua_item_transform(struct lua_State *L);
int rq_lua_item_equipped(struct lua_State *L);
int rq_lua_item_remove(struct lua_State *L);
int rq_lua_item_equip(struct lua_State *L);
int rq_lua_item_on_create(struct lua_State *L);
int rq_item_no_from_lua(struct lua_State *L, int idx);
void rq_item_mark_stale(void);
void rq_item_refresh(void);
int rq_item_is_gear_key(const char *key);
void rq_item_check_gear(struct lua_State *L, int t);
void rq_item_give_gear(struct lua_State *L, int t, int iic);
int rq_image_placeholder(void);
int rq_lua_shop_on_buy(struct lua_State *L);
int rq_lua_loot_add(struct lua_State *L);
int rq_lua_loot_remove(struct lua_State *L);
int rq_lua_loot_on_pick(struct lua_State *L);
int rq_lua_battle_get_drop(struct lua_State *L);
int rq_lua_battle_set_drop(struct lua_State *L);
int rq_lua_quests_on_end(struct lua_State *L);
int rq_lua_dungeon_on_map(struct lua_State *L);
int rq_lua_quests_debug_clear(struct lua_State *L);
int rq_lua_quests_new_this_run(struct lua_State *L);
int rq_lua_skill_sort_as(struct lua_State *L);
int rq_chara_install(struct ain *ain);
int rq_chara_syscall(int code);
int rq_chara_exists(int iic);
int rq_chara_member(int iic, const char *utf, int fallback);
int rq_chara_hp(int iic, int *hp, int *max);
int rq_item_known(int no);
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
/* Character roster: define / recruit / edit / forget (modloader_roster.c). */
int rq_roster_install(struct ain *ain);
int rq_roster_syscall(int code);
void rq_roster_apply(int iic);
void rq_roster_mark_stale(void);
void rq_roster_refresh(void);
int rq_lua_chara_define(struct lua_State *L);
int rq_lua_chara_recruit(struct lua_State *L);
int rq_lua_chara_find(struct lua_State *L);
int rq_lua_chara_is_recruited(struct lua_State *L);
int rq_lua_chara_dismiss(struct lua_State *L);
int rq_lua_chara_recalc(struct lua_State *L);
int rq_lua_chara_number(struct lua_State *L);
int rq_lua_chara_id_of(struct lua_State *L);
int rq_lua_chara_edit(struct lua_State *L);
int rq_lua_chara_get(struct lua_State *L);
int rq_lua_chara_set(struct lua_State *L);
int rq_lua_chara_skill_points(struct lua_State *L);
int rq_lua_chara_add_skill_points(struct lua_State *L);
int rq_lua_chara_forget(struct lua_State *L);
int rq_lua_chara_on_create(struct lua_State *L);
int rq_lua_chara_on_forget(struct lua_State *L);
int rq_lua_store_get(struct lua_State *L);
int rq_lua_store_set(struct lua_State *L);
int rq_lua_store_delete(struct lua_State *L);
int rq_lua_store_keys(struct lua_State *L);
int rq_lua_agent_widgets(struct lua_State *L);
void rq_store_push_global(struct lua_State *L);
int rq_lua_combat_on_physical(struct lua_State *L);
int rq_lua_combat_on_accuracy(struct lua_State *L);
int rq_lua_combat_on_crit(struct lua_State *L);
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
int rq_loot_install(struct ain *ain);
int rq_loot_syscall(int code);
int rq_loot_list(struct lua_State *L);
int rq_hud_syscall(int code);
int rq_hud_install(struct ain *ain);
void rq_discord_init(struct ain *ain);
void rq_discord_tick(void);
int rq_discord_syscall(int code);
int rq_hud_tick(void);
void rq_hud_draw(void);
int rq_hud_block_mouse(void);
int rq_hud_block_keys(void);
int rq_hud_key_blocked(int code);
int rq_hud_capture_mouse(void);
int rq_hud_captured(void);
/* The game's choice menu shows (T_Select::open runs): the HUD is drawn under it (2.7.0). */
int rq_hud_choosing(void);
/* The menu's z, under which the HUD is drawn while it shows: 9991, the game's, or above Config's
 * screen while the menu shows over Config (2.28.0, set by dialog.lua). */
#define RQ_HUD_UNDER_Z 9991
int rq_hud_under_z(void);

/* Save profiles (rq.saves.*). rq_saves_init may replace *save_dir. */
void rq_saves_wait_parent(void);
void rq_saves_init(char **save_dir);
/* The Mods tab in Config (modloader_config.c). */
void rq_config_init(struct ain *ain);
int rq_config_syscall(int code);
int rq_config_mods_page(void);
void rq_config_open(struct lua_State *L, int rq_index);
/* rq.sprite (modloader_sprite.c): mod sprites in the scene list. */
void rq_sprite_open(struct lua_State *L, int rq_index);
void rq_sprite_hooks(struct lua_State *L, int rq_index);
int rq_sprite_tick(void);
void rq_sprite_cg_changed(int no);
char *rq_sprite_scene_json(void);
/* The base profile's save folder, the same for every profile (NULL before init). */
const char *rq_saves_base_dir(void);
int rq_saves_install(struct ain *ain);
int rq_saves_title_active(void);
int rq_saves_syscall(int code);
void rq_saves_on_game_save(const char *filename);
int rq_mod_count(void);
const char *rq_mod_id(int i);
const char *rq_mod_dir(int i);
int rq_lua_saves_list(struct lua_State *L);
int rq_lua_saves_active(struct lua_State *L);
int rq_lua_saves_title(struct lua_State *L);
int rq_lua_saves_current_mods(struct lua_State *L);
int rq_lua_saves_vanilla(struct lua_State *L);
int rq_lua_saves_create(struct lua_State *L);
int rq_lua_saves_clone(struct lua_State *L);
int rq_lua_saves_import_vanilla(struct lua_State *L);
int rq_lua_saves_rename(struct lua_State *L);
int rq_lua_saves_delete(struct lua_State *L);
int rq_lua_saves_switch(struct lua_State *L);
int rq_lua_hud_text_begin(struct lua_State *L);
int rq_lua_hud_text_value(struct lua_State *L);
int rq_lua_hud_text_end(struct lua_State *L);
int rq_lua_input_type(struct lua_State *L);
/* The HUD's text input (2.29.0): whether it is on, and text from rqmod-ctl type (any thread). */
int rq_hud_text_on(void);
void rq_hud_text_queue(const char *utf8);
int rq_lua_hud_block_keys(struct lua_State *L);
int rq_dungeon_active(void);
int rq_dungeon_exploring(void);
void rq_hud_bind_lua(struct lua_State *L);
struct lua_State *rq_lua(void);

int rq_lua_on_frame(struct lua_State *L);
int rq_lua_dungeon_active(struct lua_State *L);
int rq_lua_dungeon_exploring(struct lua_State *L);
int rq_lua_input_mouse(struct lua_State *L);
int rq_lua_input_key(struct lua_State *L);
int rq_lua_input_code(struct lua_State *L);
int rq_lua_input_set_key(struct lua_State *L);
int rq_lua_input_set_mouse(struct lua_State *L);
int rq_lua_input_wheel(struct lua_State *L);
int rq_lua_input_release(struct lua_State *L);
int rq_lua_ui_get(struct lua_State *L);
int rq_lua_ui_aim(struct lua_State *L);
int rq_lua_dungeon_grid(struct lua_State *L);
int rq_lua_ui_shot(struct lua_State *L);
int rq_lua_ui_shot_result(struct lua_State *L);
int rq_lua_hud_rect(struct lua_State *L);
int rq_lua_hud_text(struct lua_State *L);
int rq_lua_hud_cg(struct lua_State *L);
int rq_lua_hud_cg_size(struct lua_State *L);
int rq_lua_input_scroll(struct lua_State *L);
int rq_lua_hud_clip(struct lua_State *L);
int rq_lua_hud_clip_push(struct lua_State *L);
int rq_lua_hud_clip_pop(struct lua_State *L);
int rq_lua_hud_top(struct lua_State *L);
int rq_lua_hud_hit(struct lua_State *L);
int rq_lua_hud_clicked(struct lua_State *L);
int rq_lua_hud_block(struct lua_State *L);
int rq_lua_hud_capture_mouse(struct lua_State *L);
int rq_lua_ui_choosing(struct lua_State *L);
int rq_lua_hud_set_under_z(struct lua_State *L);
int rq_lua_ain_patch_local_int(struct lua_State *L);
int rq_lua_hud_screen(struct lua_State *L);
int rq_lua_hud_text_width(struct lua_State *L);
int rq_lua_hud_wrap(struct lua_State *L);
int rq_lua_loot_list(struct lua_State *L);

#define RQ_DEBUG_ACTIONS 32
#define RQ_DEBUG_OV 8

struct rq_debug_row {
	char id[40];
	char key[16];
	char label[96];
	char page[24];
	int on;
};

struct rq_debug_view {
	int menu;
	int stats;
	int nactions;
	struct rq_debug_row actions[RQ_DEBUG_ACTIONS];
	double fps;
	double memory_mb;
	double avg_fps;
	int vpf;
	char screen[24];
	int nov;
	char overlays[RQ_DEBUG_OV][24];
};

void rq_debug_init(void);
void rq_debug_unload_mod(const char *mod);
int rq_debug_swallows(int code);
int rq_debug_wants_frame(void);
void rq_debug_tick(void);
void rq_debug_draw(void);
void rq_debug_add_verts(int n);
void rq_debug_on_present(void);
int rq_debug_snapshot(struct rq_debug_view *out);
int rq_lua_debug_action(struct lua_State *L);
const char *rq_lua_caller_mod(struct lua_State *L);
int rq_key_from_name(const char *name);
/* Base screen id plus open overlay ids (vault, status, battle, item-previews). */
void rq_ui_layers(char *screen, int screen_n, char overlays[][24], int max_ov, int *n_ov);
/* Master volume on the Config Sound tab, 0–100, and its mute box. */
int rq_sound_master_pct(void);
int rq_sound_master_muted(void);
void rq_sound_master_add(int delta_pct);
void rq_sound_master_toggle_mute(void);

void rq_ui_init(void);
void rq_ui_on_present(void);
/* The stall watch (2.31.0): handle_events logs one line when rq.on_frame has not started for 10 s
 * (the game pumps events without its update, or a callback has not returned); its numbers as text
 * for rqmod-ctl where; its clock restarted after a pause. */
void rq_stall_watch(void);
void rq_stall_watch_text(char *buf, size_t n);
void rq_stall_watch_reset(void);
/* The AIN functions on the game's call stack, innermost first, as one line of names. */
void rq_ain_stack_line(char *buf, size_t n, int max);
void rq_ui_on_parts_input(void);
int rq_ui_wants_frame(void);
void rq_ui_on_update(void);
int rq_ui_cmd_ui(char **json_out, char *err, int errsz);
int rq_path_grid(char **json_out, int whole, char *err, int errsz);
int rq_path_goto(int x, int z, int pick_portal, char **json_out, char *err, int errsz);
int rq_path_step(int dx, int dz, char **json_out, char *err, int errsz);
int rq_ui_cmd_hover(const char *id, char *err, int errsz);
int rq_ui_cmd_shot(const char *path, char *err, int errsz);
int rq_ui_cmd_cgdump(const char *args, char **json_out, char *err, int errsz);
void rq_ui_set_paused(int on);
int rq_ui_paused(void);
int rq_ui_cmd_press(const char *id, char *err, int errsz);
int rq_ui_cmd_drag(const char *src, const char *dst, char *err, int errsz);
int rq_ui_has_control(const char *id);
int rq_agent_set(const char *id, const char *label, int button, int hover_only);
/* HUD rect. Listed while registered. hover/press click that rectangle. */
int rq_agent_set_rect(const char *id, const char *label, int x, int y, int w, int h);
void rq_agent_clear(const char *id);
int rq_mods_json(char **json_out);
/* JSON {"version", "entries":[{name, kind}]} of the engine's rq.* before mods load; caller frees. */
char *rq_api_dump_json(void);
void rq_ui_note_sprite_text(int sp_no, const char *sjis);
void rq_ui_clear_sprite_text(int sp_no);

#define RQ_VERSION "2.33.0"

struct rq_qbattle_skill {
	int id;
	int uses;
	int enabled;
	char name[80];
};

struct rq_qbattle_unit {
	int id;
	int no;
	char name[80];
};

struct rq_qbattle {
	int up;
	int n_allies;
	int n_enemies;
	int n_skills;
	int n_log;
	struct rq_qbattle_unit allies[8];
	struct rq_qbattle_unit enemies[8];
	struct rq_qbattle_skill skills[48];
	char log[24][180];
};

int rq_query_battle(struct rq_qbattle *out);
int rq_query_chara_named(int id);
int rq_query_unit_recruited(int id);
int rq_lua_party_get(struct lua_State *L);
int rq_lua_party_set(struct lua_State *L);
int rq_lua_battle_start(struct lua_State *L);
int rq_lua_random_seed(struct lua_State *L);
int rq_lua_random_fix(struct lua_State *L);
/* hll/Math.c: every roll of the game and the loader. A negative value stops fixing. */
void rq_random_seed(int seed);
void rq_random_fix(int value);
int rq_random_int(int max);
float rq_random_float(void);
int rq_query_facts(char **json_out);
void rq_query_bind(struct lua_State *L);
void rq_sandbox_install(struct lua_State *L);
struct rq_mod_node;
/* Registers a mod folder (with its id and dependencies) and makes the mod's own global
 * table. Returns its index, or -1. */
int rq_sandbox_add_mod(struct lua_State *L, const char *dir, const struct rq_mod_node *node);
/* After the mod's init.lua: ok with its return value on top of the stack (popped). */
void rq_sandbox_mod_done(struct lua_State *L, int mod, int ok);
/* Snapshots rq and the standard tables; with mod >= 0, first logs what that mod's init.lua
 * added, changed or removed in them since the last snapshot. */
void rq_sandbox_watch(struct lua_State *L, int mod);
/* rq.mod.get, id and list, in the rq table at rq_idx. */
void rq_sandbox_open(struct lua_State *L, int rq_idx);
/* Gives the function at fidx mod's global table as its _ENV. */
void rq_sandbox_set_env(struct lua_State *L, int fidx, int mod);
/* The id of the mod whose code is nearest on the call stack, as rq.mod.id gives it; NULL
 * outside a mod. */
const char *rq_sandbox_caller_id(struct lua_State *L);

/* hll/MsgSkip.c: a read flag of its own for one line of mod text. */
int rq_msgskip_number(const char *mod, const char *text);

int rq_dialog_open(void);
int rq_dialog_has(const char *id);
int rq_dialog_json(char **json_out);
int rq_dialog_hover(const char *id, char *err, int errsz);
int rq_dialog_press(const char *id, char *err, int errsz);

#else

static inline void rq_mods_early_init(void) {}
static inline void rq_mods_init(struct ain *ain) { (void)ain; }
static inline void rq_crash_crumb(const char *what, int a, int b) { (void)what; (void)a; (void)b; }
static inline void rq_crash_vm_error(const char *msg) { (void)msg; }
static inline void rq_crash_test(void) {}
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
static inline int rq_chara_syscall(int code) { (void)code; return 0; }
static inline int rq_roster_syscall(int code) { (void)code; return 0; }
static inline bool rq_image_has(int id) { (void)id; return false; }
static inline struct cg *rq_image_load(int id) { (void)id; return NULL; }
static inline bool rq_image_metrics(int id, struct cg_metrics *out) { (void)id; (void)out; return false; }
static inline int rq_events_syscall(int code) { (void)code; return 0; }
static inline int rq_scene_syscall(int code) { (void)code; return 0; }
static inline int rq_image_syscall(int code) { (void)code; return 0; }
static inline void rq_battle_flames_clear(void) {}
static inline void rq_battle_forget(void) {}
static inline void rq_battle_marks_live(int on) { (void)on; }
static inline int rq_item_syscall(int code) { (void)code; return 0; }
static inline void rq_item_mark_stale(void) {}
static inline void rq_roster_apply(int iic) { (void)iic; }
static inline void rq_roster_mark_stale(void) {}
static inline int rq_hud_syscall(int code) { (void)code; return 0; }
static inline int rq_hud_tick(void) { return 0; }
static inline int rq_sprite_tick(void) { return 0; }
static inline void rq_hud_draw(void) {}
static inline int rq_hud_block_mouse(void) { return 0; }
static inline int rq_hud_block_keys(void) { return 0; }
static inline int rq_hud_key_blocked(int code) { (void)code; return 0; }
static inline int rq_hud_capture_mouse(void) { return 0; }
static inline int rq_hud_captured(void) { return 0; }
static inline int rq_hud_choosing(void) { return 0; }
static inline void rq_saves_wait_parent(void) {}
static inline void rq_saves_init(char **save_dir) { (void)save_dir; }
static inline const char *rq_saves_base_dir(void) { return NULL; }
static inline void rq_saves_on_game_save(const char *filename) { (void)filename; }
static inline int rq_saves_syscall(int code) { (void)code; return 0; }
static inline void rq_ui_init(void) {}
static inline void rq_ui_on_present(void) {}
static inline void rq_stall_watch(void) {}
static inline void rq_ui_on_parts_input(void) {}
static inline int rq_ui_wants_frame(void) { return 0; }
static inline void rq_ui_on_update(void) {}
static inline void rq_ui_note_sprite_text(int sp_no, const char *sjis)
{
	(void)sp_no;
	(void)sjis;
}
static inline void rq_ui_clear_sprite_text(int sp_no) { (void)sp_no; }

#endif

void sys_show_simple_message_box(unsigned flags, const char *title, const char *msg);

#endif /* RQ_MODLOADER_H */
