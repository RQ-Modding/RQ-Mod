---@meta rq
--- LuaLS / LuaCats definitions for the in-engine `rq` API (Lua 5.4.7).
---
--- Point LuaLS at this file (or the folder that contains it):
---   .luarc.json next to mods\:
---   {
---     "runtime.version": "Lua 5.4",
---     "workspace.library": [ "lua" ]
---   }
--- Installed copy: <game>/lua/rq.lua (next to mods\)
--- Source: RQ-Mod repository, dist/lua/rq.lua

---@class rq.DungeonCamera
---@field lock? number
---@field min? number
---@field max? number
---@field fog_far? number
---@field slider_steps? integer

---@class rq.HudTextStyle
---@field size? number
---@field r? integer
---@field g? integer
---@field b? integer
---@field a? integer Loader 1.6.0. 0 to 255, default 255
---@field w? integer Loader 1.6.0. Width of the box align places the text in
---@field h? integer Loader 1.6.0. Height of the box valign places the text in
---@field align? "left"|"center"|"right" Loader 1.6.0
---@field valign? "top"|"center"|"bottom" Loader 1.6.0
---@field edge? boolean Loader 1.7.0. false leaves out the dark edge
---@field bold? boolean Loader 1.8.0. The weight the game's own Config labels look like

---Loader 1.6.0. How hud.cg draws a CG.
---@class rq.HudCgOpts
---@field src? integer[]|{x: integer, y: integer, w: integer, h: integer} the part of the CG to draw, {x, y, w, h}; cut at the CG's edge
---@field w? integer stretch to this width
---@field h? integer stretch to this height
---@field alpha? integer 0 to 255, default 255
---@field tint? integer[]|{r: integer, g: integer, b: integer} multiplies the colors; {128, 128, 128} is half as bright

---@class rq.LootItem
---@field id integer item instance
---@field source "magnum"|"chest"
---@field seen boolean collection knowledge (identified twin owned)
---@field cg integer real CG if seen, vanilla ? CG if not
---@field name string
---@field comment string
---@field overlay integer 10403 if unseen, else 0
---@field catalog integer do not show on unseen hover
---@field floor integer
---@field x integer
---@field y integer

---@class rq.ChestInfo
---@field x integer
---@field y integer
---@field id integer locked item id when loot-preview encoded the tile; else 0
---@field base_pct integer this chest's vanilla/skill % (30, or 60/80/100, maybe halved)
---@field kind string "red"
---@field pct? integer on_attempt: percent so far (loader 0.14.0). on_result: chance used for this attempt
---@field rolled? integer 1..100 (vanilla RAND)
---@field success? boolean
---@field exploded? boolean fail always explodes

---@class rq.ChestAttemptResult
---@field pct integer 0..100

---@class rq.ain
local ain = {}

---Rewrite float immediates in one AIN function.
---Loader 1.3.0: reaches the game's code of a function the loader wraps (such as Ｔ戦闘前初期化); before, it patched only the wrapper and found nothing.
---@param name string UTF-8 function name
---@param from number
---@param to number
---@param n? integer 0-based occurrence; omit = all
---@return integer count
function ain.patch_float(name, from, to, n) end

---Loader 0.17.0. Replace an integer constant (PUSH) inside one AIN function, for example a hard-coded skill number.
---Loader 1.3.0: reaches the game's code of a function the loader wraps, as patch_float. Occurrences count every PUSH of that value, so they can differ between versions of the game's AIN; patch_int_after finds a constant by what it follows.
---@param name string AIN function name (UTF-8)
---@param from integer
---@param to integer
---@param n? integer 0-based occurrence; omit = all
---@return integer count
function ain.patch_int(name, from, to, n) end

---Loader 1.3.0. Replace the first integer constant `from` that comes within `window` instructions (default 8) after a read of a global, for example the 2 in `if (◆２倍モード == 1) { x = x * 2; }`. nth_ref picks one read of the global in the function (0-based); omit to patch after every read. A variable's index (a PUSH right after a page push) is never taken for the constant.
---@param name string AIN function name (UTF-8)
---@param global string global name (UTF-8)
---@param from integer
---@param to integer
---@param nth_ref? integer 0-based read of the global; omit = all
---@param window? integer instructions to look at after the read, default 8
---@return integer count
function ain.patch_int_after(name, global, from, to, nth_ref, window) end

---Loader 0.18.0. Call any AIN function by name. Arguments must be int, bool, float or string, and all of them are required. A method (Class@method) raises an error, because it needs an object. So do SYS_セーブ実行 and SYS_ロード実行 (0.26.0). Loader 0.27.0: any other function that reaches a save under this call gets the game's own "save failed" message and nothing is written; one that reaches a load closes the game with an error. A bad index passed to a game function is a fatal error in the game, and this call cannot check it.
---A scene command blocks until it ends. Call it from a callback the game runs, not at the top level of a mod.
---@param name string AIN function name (UTF-8)
---@param ... integer|boolean|number|string
---@return any result nil for void or a struct
function ain.call(name, ...) end

---@class rq.AinHookCtx
---@field name string
---@field args table arguments by position; change one to change what the function gets (references and structs are nil)
---@field skip? boolean before only: do not run the function
---@field result? any before: the value returned when skipped; after: the return value, which you may change

---Loader 0.18.0. Run Lua before and after any AIN function. A method is named Class@method. Many callbacks allowed. The same function does not fire again while its callback runs.
---@param name string AIN function name (UTF-8)
---@param opts {before?: fun(h: rq.AinHookCtx), after?: fun(h: rq.AinHookCtx)}
---@return fun() remove Removes both sides of this hook. Loader 0.23.0.
function ain.hook(name, opts) end

---Loader 0.22.0. Reads a game global that holds an int, bool, float or string. Give the game's own index for an array element: rq.ain.global("FA", 87) is FA[87]. Loader 0.24.0: give a field name to go into a struct: rq.ain.global("Ｑ", 126, "名前") is Ｑ[126].名前. Field names are the game's own. Raises an error for an unknown name or field, an index out of range, a whole array or a struct.
---@param name string
---@param ... integer|string indexes and field names
---@return integer|boolean|number|string
function ain.global(name, ...) end

---Loader 0.22.0. Writes the same kind of global: rq.ain.set_global(name [, i, ...], value). It is the game's own state and goes into the next save, so restore what a test changes.
---@param name string
---@param ... any indexes and field names, then the value
function ain.set_global(name, ...) end

---Loader 0.22.0. Size of an array global, with indexes and field names to reach an inner array. Many game arrays start at 1 and leave 0 unused, so the size can be one more than the entries.
---@param name string
---@param ... integer|string indexes and field names
---@return integer
function ain.global_len(name, ...) end

---NOP matching system.MsgBox sites.
---@param needle string
---@return integer site_count
function ain.skip_msgbox(needle) end

---Loader 1.0.0. Swallow the engine's own error boxes for the TADA and HTTP network checks (getserverversion).
function ain.skip_sdl_msgbox() end

---Log matching function names.
---@param needle string
---@return integer count
function ain.find(needle) end

---@class rq.dungeon
local dungeon = {}

---Camera-zoom. Patches clamp / fog / slider in memory.
---@param opts rq.DungeonCamera
---@return boolean
function dungeon.camera(opts) end

---True between T_MapParts@create and @delete (also battle / ADV / menus).
---@return boolean
function dungeon.active() end

---Map-walk only. False in Config, result, ADV, battle, profile, join dialog.
---@return boolean
function dungeon.exploring() end

---Read-only copy of the current room. Does not write a save.
---@return rq.DungeonInfo
function dungeon.info() end

---@class rq.MapCtx
---@field quest integer Ｑ index of the quest being entered
---@field cleared boolean the quest was cleared before
---@field map integer map number
---@field party integer[] iic of the party

---Runs when the dungeon map screen is built. Return true to unlock the camera zoom even on a first visit (the game locks it at distance 35 until a quest is cleared). The range comes from whatever zoom patch is active (vanilla or camera-zoom). Many callbacks allowed.
---@param fn fun(ctx: rq.MapCtx): boolean|nil
---@return fun() remove Removes this callback. Loader 0.23.0.
function dungeon.on_map(fn) end

---Loader 0.25.0. The current map, as rqmod-ctl grid shows it: player {x, z}, mapNo, room {minX, minZ, maxX, maxZ}, rows (text, north to south, see legend), events (each {x, z, kind, tt, tz, tv}), legend and axes. x runs east and z north. Nil and a reason when not on a map.
---By default it holds only what the player reaches over plain floor, so a trap, chest or fight across a corridor hides the rest of the room.
---Loader 1.5.0: opts.whole = true holds every cell of the map, with floor the player cannot reach shown as ':'. Each event has reach (true when the player can get to it over plain floor). room.no is the game's room number (◆ＢＭＡＰ), room.whole says which kind of grid it is.
---After a portal, room.no changes a moment before the map: wait for mapNo to change before reading the new room.
---@param opts? {whole?: boolean}
---@return table|nil grid
---@return string? reason
function dungeon.grid(opts) end

---Loader 1.5.0. Puts the player on a floor cell of the current map at once, also one they cannot walk to, as the game does after a portal. opts.face is "north", "east", "south" (default) or "west". grid().player shows the new cell after the game's next update. Nothing on the cell opens: walk onto it for that. Raises an error for a cell that is not floor, a cell that is not whole numbers, an unknown face, or when not on a map.
---@param x integer
---@param z integer
---@param opts? {face?: "north"|"east"|"south"|"west"}
---@return boolean placed
function dungeon.place_player(x, z, opts) end

---Loader 0.25.0. Puts an object on a free floor cell of the current map: "chest", "red-chest", "spring", "achievement", "exp" or "gold". opts.content is a fixed item number for a chest, opts.rank its rank. Walking onto it works as for the game's own. The game leaves a used "exp" in grid().events. Raises an error for an unknown kind, a cell that is not free floor, or when not on a map.
---@param kind string
---@param x integer
---@param z integer
---@param opts? {content?: integer, rank?: integer}
---@return boolean placed
function dungeon.place(kind, x, z, opts) end

---@class rq.input
local input = {}

---Game pixels. Raw buttons (HUD still sees clicks while game input is blocked).
---@return integer x
---@return integer y
---@return boolean left
---@return boolean right
---@return boolean pressed Loader 1.6.0. The left button went down on this update
---@return boolean released Loader 1.6.0. The left button came up on this update
function input.mouse() end

---Raw SACT key (VK_LBUTTON = 1, etc.).
---@param code integer
---@return boolean
function input.key(code) end

---Loader 0.20.0. Key code for a name as in rqmod-ctl ("enter", "lmb", "f3"). Raises an error on an unknown name.
---@param key string|integer
---@return integer
function input.code(key) end

---Loader 0.20.0. Press or let go of a key for the game. It stays down until you let go. The decide key is left click or Enter, and a click lands on release, so hold it for a few frames.
---@param key string|integer
---@param down boolean
function input.set_key(key, down) end

---Loader 0.20.0. Move the game's mouse, in game pixels. It stays there and the real mouse is ignored until release.
---@param x integer
---@param y integer
function input.set_mouse(x, y) end

---Loader 0.20.0. Send a wheel step: positive is up, negative is down.
---@param dir integer
function input.wheel(dir) end

---Loader 0.20.0. Give the mouse back to the player and let go of the mouse buttons. Keys set with set_key stay as they are.
function input.release() end

---Loader 1.7.0. Wheel steps since the last update, up positive (steps from input.wheel count too).
---With eat true, the game does not get them.
---@param eat? boolean
---@return integer steps
function input.scroll(eat) end


---@class rq.ui
local ui = {}

---Loader 0.20.0. What is on the screen, the same data as rqmod-ctl ui: screen, hover, controls, and the data of the current screen. It builds the whole list, so do not call it on every rq.on_frame. A screen with no name, such as a black one during a change of scene, says other (0.21.0). boot is only used before the first named screen.
---@return table|nil
function ui.get() end

---Loader 0.20.0. The point a click on this control should land on. Builds the list if nothing did during this update (0.21.0).
---@param id string
---@return integer|nil x
---@return integer|nil y
function ui.aim(id) end

---Loader 0.21.0. Saves what the game draws as a PNG at the next drawn frame. path is relative to the mod's folder, which must already hold the folders in it; use an ASCII path. Raises an error if the path leaves the mod's folder or another shot is still waiting.
---@param path string
function ui.shot(path) end

---Loader 0.21.0. true or false once, when the shot ended. nil while it waits, or when no result is left to give.
---@return boolean|nil
function ui.shot_result() end

---@class rq.hud
local hud = {}

---Typically 1024×768.
---@return integer w
---@return integer h
function hud.screen() end

---Filled rect. a default 220.
---@param x integer
---@param y integer
---@param w integer
---@param h integer
---@param r integer
---@param g integer
---@param b integer
---@param a? integer
function hud.rect(x, y, w, h, r, g, b, a) end

---UTF-8. Default size 16, white, dark edge. COPY glyphs. Font: VL PGothic, each letter at its own width (1.8.0).
---@param x integer
---@param y integer
---@param str string
---@param style? rq.HudTextStyle
function hud.text(x, y, str, style) end

---The width hud.text takes for the same text and style (VL PGothic since 1.8.0).
---@param str string
---@param size_or_style? number|rq.HudTextStyle
---@return integer px
function hud.text_width(str, size_or_style) end

---Word-wrap to pixel width, not a character count.
---@param str string
---@param max_px integer
---@param size? number
---@return string[] lines
function hud.wrap(str, max_px, size) end

---Game CG by number (item icons, ? CGs), or a mod image from rq.image.add. Returns the CG's own
---size (0, 0 for no CG), whatever is drawn. Loader 1.6.0: opts draws a part, stretched, faded or
---tinted. At most 160 different CGs are kept for drawing; past that, a new one draws nothing.
---@param x integer
---@param y integer
---@param cg_no integer
---@param opts? rq.HudCgOpts
---@return integer w
---@return integer h
function hud.cg(x, y, cg_no, opts) end

---Loader 1.6.0. The CG's size without drawing it; nil when the number is no CG.
---@param cg_no integer
---@return integer|nil w
---@return integer h
function hud.cg_size(cg_no) end

---Loader 1.6.0. Everything drawn after it shows only inside the box, until hud.clip() with no
---arguments. A new clip replaces the box. Every frame starts with no clip.
---@param x? integer
---@param y? integer
---@param w? integer
---@param h? integer
function hud.clip(x, y, w, h) end

---Mouse inside rect this frame.
---@param x integer
---@param y integer
---@param w integer
---@param h integer
---@return boolean
function hud.hit(x, y, w, h) end

---Left press edge inside rect.
---@param x integer
---@param y integer
---@param w integer
---@param h integer
---@return boolean
function hud.clicked(x, y, w, h) end

---This frame + until mouse-up: game does not see L/R. HUD still reads raw mouse.
function hud.block_game_input() end

---Block game keyboard input this frame (the HUD still reads raw keys with rq.input.key).
---Call every frame while a modal HUD is open. Mouse is blocked by block_game_input.
function hud.block_keys() end

---Start capturing typed text (UTF-8). Only characters the game font can draw are kept.
---Backspace removes the last character. Read it with text_value, finish with text_end.
---@param initial? string
---@param max_bytes? integer default 40, at most 255
function hud.text_begin(initial, max_bytes) end

---Current captured text and whether capture is on.
---@return string text
---@return boolean active
function hud.text_value() end

---Stop capturing text.
function hud.text_end() end

---@class rq.loot
local loot = {}

---@class rq.LootAdd
---@field item string|integer item number or text id
---@field pool? "item"|"special"|"rare"|"ultra"|"gear" omit for every pool. gear (weapons and armor) since loader 0.15.0
---@field chance? number percent chance per roll (default 1)
---@field unique? boolean skip when the player already owns one

---Chest pools (loot table). Add an item to a pool's roll with a percent chance.
---@param spec rq.LootAdd
function loot.add(spec) end

---Never lets this item come out of a chest (rolls 0, so the game tries the next pool).
---@param who string|integer
---@param opts? {pool?: "item"|"special"|"rare"|"ultra"}
function loot.remove(who, opts) end

---Called after a chest pool rolled. Return another item number or text id to replace it, 0 for nothing. Many callbacks allowed.
---@param fn fun(ctx: {pool: string, item: integer}): integer|string|nil
---@return fun() remove Removes this callback. Loader 0.23.0.
function loot.on_pick(fn) end

---Lock-roll at place, skip open generator, map HUD flags. Reds keep vanilla explode.
---Does not open UI. No on_attempt hook = vanilla % (not 100%).
---@return boolean
function loot.install() end

---Magnum (if holding) then locked chests.
---@return rq.LootItem[]
function loot.list() end

---@class rq.chest
local chest = {}

---Patch the red RAND site. Fail still explodes. Idempotent with rq.loot.install.
---@return boolean
function chest.install() end

---Called just before the fail/success roll. Return a percent (or {pct=}).
---Loader 0.14.0: every mod's callback runs. Each sees the percent the one before left in info.pct; nil keeps it.
---No hook → info.base_pct (vanilla/skill), not 100%.
---@param fn fun(info: rq.ChestInfo): integer|rq.ChestAttemptResult
---@return fun() remove Removes this callback. Loader 0.23.0.
function chest.on_attempt(fn) end

---Called after the roll. Fail always exploded. rolled is 1..100. Loader 0.14.0: every mod's callback runs.
---@param fn fun(info: rq.ChestInfo)
---@return fun() remove Removes this callback. Loader 0.23.0.
function chest.on_result(fn) end

---Session integer store for pity (not a save file).
---@param key string
---@return integer|nil
function chest.get(key) end

---@param key string
---@param val integer
function chest.set(key, val) end

---@class rq.SkillInfo
---@field id integer
---@field name string
---@field kinou integer 機能
---@field bairitsu integer 倍率
---@field target integer 対象
---@field koudou integer 行動種類 (1 physical, 2 magic, 3 special, 4 support, 0 ability)
---@field kouka integer 効果
---@field desc1 string raw 説明文１ (may still contain $)

---@class rq.OrbMember
---@field iic integer character slot
---@field state integer 登録状態 (1 PTA, 2 控え, 3 spent, …)
---@field hp integer
---@field max_hp integer

---@class rq.orb
local orb = {}

---Patch 回復の泉処理 so the PTA-heal site calls Lua on_heal (or vanilla if none).
---Carolie / flash / tile clear stay in the vanilla function after the hook.
---@return boolean
function orb.install() end

---Spring heal policy. Loader 0.14.0: every mod's callback runs, so two healers both heal. No hook, or all failed → vanilla PTA heal.
---@param fn fun()
---@return fun() remove Removes this callback. Loader 0.23.0.
function orb.on_heal(fn) end

---Roster members with 登録状態 ~= 0 (iic 20..200). Optional state filter.
---@param state? integer
---@return rq.OrbMember[]
function orb.roster(state) end

---@param iic integer
---@return integer
function orb.get_state(iic) end

---@param iic integer
---@param state integer
function orb.set_state(iic, state) end

---@param iic integer
---@param status integer 状態変化
function orb.set_status(iic, status) end

---@param iic integer
---@return integer
function orb.get_hp(iic) end

---@param iic integer
---@return integer
function orb.get_max_hp(iic) end

---@param iic integer
---@param hp integer
function orb.set_hp(iic, hp) end

---Ｃ補給／体力(iic, "") — set HP to max.
---@param iic integer
function orb.full_heal(iic) end

---@class rq.skill
local skill = {}

---Wrap スキル_説明文１ so on_describe can rewrite hover text.
---@return boolean
function skill.install() end

---fn(skillNo, text) → text (UTF-8). Loader 0.14.0: every mod's callback runs, each gets the text the one before left. No hook → base text.
---@param fn fun(skillNo: integer, baseText: string): string
---@return fun() remove Removes this callback. Loader 0.23.0.
function skill.on_describe(fn) end

---Skill fields from AIN getters (Ｓ名前 / Ｓ機能 / Ｓ倍率 / Ｓ対象 / Ｓ説明文１).
---Also every field rq.skill.patch accepts (type, action, element, kouka2, ...). id may be a text id.
---@param id integer|string
---@return rq.SkillInfo
function skill.get(id) end

---@class rq.SkillSpec
---@field id integer|string 1..1000, or a text id
---@field kind "physical"|"knowledge"
---@field name? string
---@field flavor? string battle-log verb (Ｓ表示文)
---@field desc? string hover; $ becomes a newline
---@field target? integer
---@field bairitsu? integer
---@field flat? integer
---@field kinou? integer
---@field kouka? integer
---@field kouka2? integer
---@field kouka3? integer
---@field kouka4? integer
---@field cap? integer 0 = no cap
---@field basic? integer
---@field weapon? integer Ｓ発動条件／武器
---@field ef? string

---Queue a skill row. Flushed at the end of Ｓ起動時登録.
---id may be a text id ("mymod:slash"): it becomes a stable number in 700..999 (collisions are an error).
---Target codes: 1 melee (front row), 2 ranged, 3 all enemies, 4 needs two or more front-row enemies.
---@param spec rq.SkillSpec
---@return integer id numeric id
function skill.define(spec) end

---@class rq.SkillPatch
---@field name? string
---@field flavor? string
---@field desc? string
---@field ef? string
---@field basic? integer
---@field big? integer
---@field kinou? integer
---@field kouka? integer
---@field kouka2? integer
---@field kouka3? integer
---@field kouka4? integer
---@field locked? integer
---@field type? integer
---@field action? integer
---@field panel? integer
---@field target? integer
---@field bairitsu? integer
---@field cap? integer
---@field element? integer
---@field flat? integer
---@field no_first_turn? integer
---@field once_per_battle? integer
---@field needs_charge? integer
---@field efa? integer
---@field efb? integer
---@field weapon? integer

---Change an existing skill field by field. Fields left out stay as the game made them.
---Applied in call order after Ｓ起動時登録, with the defines.
---@param id integer|string
---@param fields rq.SkillPatch
function skill.patch(id, fields) end

---Known list and potential list compare id as if it were as_id.
---False if the compSkill hook is not installed. as_id 0 clears.
---@param id integer
---@param as_id integer
---@return boolean
function skill.sort_as(id, as_id) end

---@class rq.skilllist
local skilllist = {}

---Keep the full potential-skill buy list (144 rows) and draw Previous/Next.
---Known Skills slots stay as they are. Idempotent.
---@return boolean
function skilllist.install() end

---Called when the potential-skill menu opens, before the grid is built.
---The page is already 1. Use this to reset policy. Loader 0.14.0: every mod's callback runs.
---@param fn fun()
---@return fun() remove Removes this callback. Loader 0.23.0.
function skilllist.on_open(fn) end

---Decide the page. Also called once per arrow while the buttons are built,
---so a false/nil return greys that arrow (vanilla deny sound, no second call).
---An enabled click calls this again. dir is -1 or +1 (0 asks for the current page).
---Return a page number to show that page, or false to deny.
---Loader 0.14.0: every mod's callback runs and the last one that answers (not nil) decides. All nil denies.
---@param fn fun(ctx: rq.SkillListCtx): integer|false
---@return fun() remove Removes this callback. Loader 0.23.0.
function skilllist.on_page(fn) end

---Called after Previous / Next exist. prev_id and next_id are T_Button ids, or 0.
---Fires again whenever those buttons are rebuilt. Loader 0.14.0: every mod's callback runs.
---@param fn fun(prev_id: integer, next_id: integer)
---@return fun() remove Removes this callback. Loader 0.23.0.
function skilllist.on_buttons(fn) end

---@class rq.SkillListCtx
---@field dir integer -1 previous, +1 next, 0 query
---@field page integer current page, 1-based
---@field pages integer page count (at least 1)

---Set the page Lua will use the next time the grid is sliced. Clamped to >= 1.
---@param page integer
function skilllist.set_page(page) end

---Pages in the current filtered buy list. 1 before the menu has opened.
---@return integer
function skilllist.page_count() end

---@class rq.SaveSummary
---@field has_save boolean the profile holds a game save
---@field saved_at? string "YYYY-MM-DD HH:MM", mtime of the game save file
---@field world? integer
---@field fame_rank? integer
---@field fame? integer
---@field gold? integer
---@field quests? integer quests cleared
---@field suspend boolean a suspend save exists
---@field read_error? string

---@class rq.SaveMod
---@field id string
---@field name string
---@field version string

---@class rq.SaveProfile
---@field id string "base" or an engine-made id
---@field name string
---@field is_base boolean the base save can never be deleted
---@field active boolean
---@field created? string
---@field last_saved? string
---@field source? string "clone of X", "imported from the vanilla game"
---@field mods? rq.SaveMod[] mods loaded when the game last wrote this save; nil if never
---@field mods_ever? string[] ids of every mod that was loaded for any save
---@field summary rq.SaveSummary

---@class rq.VanillaSave
---@field exists boolean a game save was found
---@field path? string
---@field summary? rq.SaveSummary

---Save profiles. The base profile is the engine's own save folder. Others are full
---copies under <home>/Profiles/<id>/save. Failing calls return nil, message.
---@class rq.saves
local saves = {}

---Wrap T_Title@checkSelect so saves.title() works. Call once from init.lua.
---@return boolean
function saves.install() end

---True while the title screen waits for a choice (not while a sub-menu is open).
---@return boolean
function saves.title() end

---Id of the active profile ("base" for the original save).
---@return string
function saves.active() end

---Base first, then the others by creation time. Reads each profile's save file.
---@return rq.SaveProfile[]
function saves.list() end

---Mods loaded in this session.
---@return rq.SaveMod[]
function saves.current_mods() end

---Vanilla game save in Documents/AliceSoft/<game>/SaveData. Read-only.
---@return rq.VanillaSave
function saves.vanilla() end

---New empty profile. Returns its id.
---@param name string
---@return string|nil id
---@return string? err
function saves.create(name) end

---Copy a whole profile (Backup/ is skipped). Returns the new id.
---@param src_id string
---@param name string
---@return string|nil id
---@return string? err
function saves.clone(src_id, name) end

---Copy the vanilla game's save folder into a new profile. Never writes to the source.
---@param name string
---@return string|nil id
---@return string? err
function saves.import_vanilla(name) end

---@param id string
---@param name string
---@return boolean|nil ok
---@return string? err
function saves.rename(id, name) end

---Refuses "base" and the active profile.
---@param id string
---@return boolean|nil ok
---@return string? err
function saves.delete(id) end

---Make a profile active and restart the game. Does not return on success.
---@param id string
---@return nil
---@return string? err
function saves.switch(id) end

---@class rq.AgentOpts
---@field label? string
---@field button? integer T_Button id. Omit it for a HUD rectangle and give x, y, w, h instead.
---@field hoverOnly? boolean
---@field x? integer HUD rectangle, game pixels
---@field y? integer
---@field w? integer
---@field h? integer

---@class rq.DebugAction
---@field id string short token, not "rendering-stats"
---@field label string menu text
---@field key string pipe key name; "6", "f3", "+", "-", and "m" are reserved
---@field page? string defaults to "Main"
---@field onPress fun() called on the key edge while F3 is held

---@class rq.debug
local debug = {}

---Append or replace a debug-menu row. The engine owns F3 and Rendering Stats (key 6).
---Pass a string id, or `{ id = id }` with no label/key/onPress, to drop the row on unload.
---@param action rq.DebugAction|string
---@return boolean
function debug.action(action) end

---@class rq.agent
local agent = {}

---Register a shown engine button with rqmod-ctl ui / hover / press.
---The row is omitted while that button is not shown. Pass nil to unregister.
---@param id string
---@param opts rq.AgentOpts|nil
---@return boolean|nil
function agent.control(id, opts) end

---@class rq.AgentWidget
---@field id string ASCII, no spaces, under 40 characters
---@field label? string
---@field type? string "button" when left out; the kit uses text, button, check, slider, tab, row
---@field value? string|number|boolean
---@field selected? boolean
---@field disabled? boolean
---@field x integer
---@field y integer
---@field w integer
---@field h integer

---Loader 1.8.0. What a HUD drew on this update, for rqmod-ctl ui (section "kit"). Each call
---replaces the last list; {} clears it. rq.uikit sends its widgets by itself, so call this
---only for a HUD of your own. A press on a row clicks the middle of its box.
---@param list rq.AgentWidget[]
---@return integer listed
function agent.widgets(list) end

---@class rq.CharaSlot
---@field slot integer
---@field skill integer
---@field uses integer remaining uses
---@field max integer max uses
---@field bar integer hotbar position, 0 if unequipped

---@class rq.OfferCtx
---@field iic integer
---@field no integer Ｃ番号
---@field level integer
---@field class integer

---@class rq.Offer
---@field skill integer
---@field cost integer
---@field level integer required level
---@field points? integer final price, replaces the game's own curve (loader 0.13.0)

---@class rq.chara
local chara = {}

---Tail of Ｃ再計算 and Ｔ購入スキル登録, plus the combat sites. Idempotent with rq.combat.install.
---@return boolean
function chara.install() end

---ＣＳ追加. n is rank (type 30) or uses (type 10).
---@param iic integer
---@param skillId integer
---@param n integer
---@return integer slot
function chara.grant(iic, skillId, n) end

---ＣＳ有無／ランク. 0 if the character does not have it.
---@param iic integer
---@param skillId integer
---@return integer
function chara.rank(iic, skillId) end

---@param iic integer
---@return rq.CharaSlot[]
function chara.slots(iic) end

---@param iic integer
---@param slot integer
---@param skillId integer
function chara.set_slot(iic, slot, skillId) end

---Copy Ｓ表示文 of the slot's current skill into the battle-log line.
---@param iic integer
---@param slot integer
function chara.set_flavor(iic, slot) end

---Ｉ種類詳細 of the equipped weapon. Sword 2, spear/naginata 7, glove 9.
---@param iic integer
---@return integer
function chara.weapon_detail(iic) end

---Ｃ機能適応. scale defaults to 100. Magic = 14, accuracy = 8.
---@param iic integer
---@param kind integer
---@param amount integer
---@param scale? integer
function chara.apply_mod(iic, kind, amount, scale) end

---fn(iic) after that character's Ｃ再計算. Loader 0.14.0: every mod's callback runs.
---@param fn fun(iic: integer)
---@return fun() remove Removes this callback. Loader 0.23.0.
function chara.on_recalc(fn) end

---fn(ctx) → nil, one offer {skill, cost, level, points?}, or a list of offers. Called after vanilla filled the buy list.
---Loader 0.13.0: every mod's callback runs (before, the last one won) and a list adds several rows.
---points, if given, is the final price shown: it replaces what スキルポン worked out (no use-count bump, no cap of 9).
---A row whose level gate fails keeps the game's "Cannot learn yet" text.
---@param fn fun(ctx: rq.OfferCtx): rq.Offer|rq.Offer[]|nil
---@return fun() remove Removes this callback. Loader 0.23.0.
function chara.on_offer(fn) end

---@class rq.OfferRow
---@field index integer array index in 取得可能スキル
---@field skill integer
---@field points integer buy cost (取得可能スキルポイント)
---@field points integer 取得可能スキルポイント

---Non-zero buy-list rows.
---@return rq.OfferRow[]
function chara.offers() end

---Replace a buy-list skill id. Leaves the level-lock text.
---Pass points to replace 取得可能スキルポイント (extra-use cost).
---Also updates the paged buy-list cache the buttons actually read.
---@param index integer
---@param skillId integer
---@param points? integer
function chara.set_offer(index, skillId, points) end

---Set remaining uses and max uses. Max is written first (cap 99 in the engine).
---@param iic integer
---@param slot integer
---@param uses integer
---@param max integer
function chara.set_uses(iic, slot, uses, max) end

---ＣＳ実装中. 0 clears the hotbar entry.
---@param iic integer
---@param slot integer
---@param pos integer
function chara.set_bar(iic, slot, pos) end

---ＣＳ初期化. Empties the slot.
---@param iic integer
---@param slot integer
function chara.clear_slot(iic, slot) end

---ＣＳ有無／最大回数. 0 if the character does not have the skill.
---@param iic integer
---@param skillId integer
---@return integer
function chara.max_uses(iic, skillId) end

---@class rq.CharaInit
---@field hp? integer percent bonus (初期／体力最大). Same for atk, def, mag, mdef.
---@field spd? integer flat points. Same for hit, eva, crit, impact, resist.
---@field atk? integer
---@field def? integer
---@field hit? integer
---@field eva? integer
---@field crit? integer
---@field impact? integer
---@field resist? integer
---@field mag? integer
---@field mdef? integer
---@field sp? integer starting skill points

---@class rq.CharaSkillGrant
---@field id integer skill number
---@field n? integer rank (rank skills) or uses (use skills). Default 1.

---@class rq.CharaFields
---@field name? string
---@field short? string
---@field face? integer|string game face CG number, or a path inside the mod folder (shown at 196x232, proportions kept)
---@field face_name? string 顔ＣＧ文字列
---@field portrait? integer|string 立ち絵ＣＧ number, or a path inside the mod folder (1024x768 canvas, proportions kept)
---@field class? integer 1 Fighter 2 Guard 3 Gunner 4 Sorcerer 5 Healer 6 Ranger 7 Brawler 8 Civilian
---@field sex? integer
---@field level? integer
---@field level_max? integer
---@field slots? integer equippable skill slots (rank of skill 568 plus 2). The hotbar has 6 positions.
---@field type? integer ＣＦＡ type for sorcerers, race, model, boss, weakness_known, ... (see rq.chara.get)
---@field init? rq.CharaInit bonuses applied by recalculation (hp/atk/def/mag/mdef are percents, the rest flat points)
---@field skills? rq.CharaSkillGrant[]
---@field weapon? integer|string item number or text id, created and equipped when the character is created (loader 0.15.0)
---@field armor? integer|string same as weapon
---@field item? integer|string same as weapon
---@field special? integer|string same as weapon

---@class rq.CharaDef: rq.CharaFields
---@field id string text id, "mymod:alice". Becomes a stable number from 1000 up.
---@field lines? string[] battle lines 1..6
---@field on_create? fun(iic: integer)

---@class rq.CharaEdit: rq.CharaFields
---@field learn? rq.CharaSkillGrant[] ensure these skills (raises rank, never lowers; use skills are not stacked)
---@field on_recalc? fun(iic: integer): boolean|nil return true to ask for one more recalculation

---@class rq.RecruitOpts
---@field state? integer 登録状態 (1 party, 2 reserve, 4 second string)
---@field party? integer ＰＴＡ slot to put the character in right away

---@class rq.CreateCtx
---@field iic integer
---@field no integer
---@field id? string text id when a mod defined this character

---@class rq.ForgetCtx
---@field iic integer
---@field skill integer
---@field slot integer row in the character's skill list
---@field rank integer
---@field cost integer points paid for it (0 if unknown)
---@field caller string AIN function that removed it, or "lua" for rq.chara.forget

---Define a character (create the template). Nothing joins the roster until recruit.
---Returns the number. Errors if the id collides with another one.
---@param def rq.CharaDef
---@return integer no
function chara.define(def) end

---Provide a character: Ｃ生成 for a defined id or a base-game number. Idempotent.
---@param who string|integer
---@param opts? rq.RecruitOpts
---@return integer|nil iic
---@return string? err
function chara.recruit(who, opts) end

---iic of the character if it exists in the roster.
---@param who string|integer
---@return integer|nil iic
function chara.find(who) end

---@param who string|integer
---@return boolean
function chara.is_recruited(who) end

---Ｃ再計算 now (add heal=true for Ｃ補給 after). Applies pending edits.
---@param iic integer
---@param heal? boolean
function chara.recalc(iic, heal) end

---Ｃ削除 (leaves the party first). Frees the roster slot.
---@param iic integer
function chara.dismiss(iic) end

---Character number (番号) of a roster slot.
---@param iic integer
---@return integer|nil
function chara.number(iic) end

---Text id of a defined character number.
---@param no integer
---@return string|nil
function chara.id_of(no) end

---Declarative edit, re-applied at the end of every recalculation (so it covers old saves).
---Since 1.2.1 each edited character is recalculated once right after a save loads. An edit made
---later waits for the next recalculation; rq.chara.recalc applies it now.
---@param who string|integer character number or text id
---@param patch rq.CharaEdit
function chara.edit(who, patch) end

---All known fields under English names (hp, atk, eva, init_eva, state, weakness_known, ...).
---@param iic integer
---@return table|nil
function chara.get(iic) end

---Write fields by the same names. Derived stats are rebuilt by the next recalculation, and so are the init_* fields of the game's own characters: use chara.edit to keep a change.
---@param iic integer
---@param fields table
function chara.set(iic, fields) end

---@param iic integer
---@return integer
function chara.skill_points(iic) end

---@param iic integer
---@param delta integer
---@return integer new total
function chara.add_skill_points(iic, delta) end

---Remove a skill (ＣＳ削除). Fires on_forget with caller "lua".
---@param iic integer
---@param skill integer
---@return boolean removed
---@return integer? cost points the character had paid for it
function chara.forget(iic, skill) end

---After a character is created (Ｃ生成), base game or mod. May be registered many times.
---@param fn fun(ctx: rq.CreateCtx)
---@return fun() remove Removes this callback. Loader 0.23.0.
function chara.on_create(fn) end

---Before a skill row is emptied. Return points to give back (summed over callbacks). May be registered many times.
---@param fn fun(ctx: rq.ForgetCtx): integer|nil
---@return fun() remove Removes this callback. Loader 0.23.0.
function chara.on_forget(fn) end

---@alias rq.ImageFit "stretch"|"contain"|"cover"|"none"

---@class rq.ImageOpts
---@field fit? rq.ImageFit stretch = fill the target size, contain = keep proportions with transparent bars, cover = keep proportions and crop, none = keep the file's size
---@field w? integer target width (default: the file's size; for replace, the game CG's size)
---@field h? integer target height
---@field keep_size? boolean replace only: do not scale to the game CG's size

---@class rq.ImageInfo
---@field exists boolean the CG number resolves to something (game or mod)
---@field mod boolean a mod image is on this number
---@field replaced? boolean it replaces a game CG
---@field w? integer
---@field h? integer
---@field game_w? integer size of the game's own CG (replaced only)
---@field game_h? integer

---@class rq.image
local image = {}

---Register a file from the mod folder as a new CG number. The number comes from the key
---(900000 and up), so it is the same every run and safe to keep in a save.
---Formats: PNG, JPEG, WebP (still), QNT, AJP, PMS. Not GIF.
---@param key string unique text key, for example "mymod:alice_face"
---@param path string relative to the mod folder
---@param opts? rq.ImageOpts
---@return integer cg
function image.add(key, path, opts) end

---Every use of an existing game CG number shows the mod file instead, scaled to the game CG's size
---(fit "stretch" unless you say otherwise).
---@param cg integer
---@param path string relative to the mod folder
---@param opts? rq.ImageOpts
function image.replace(cg, path, opts) end

---Undo image.replace.
---@param cg integer
---@return boolean removed
function image.restore(cg) end

---@param cg integer
---@return rq.ImageInfo
function image.info(cg) end

---Loader 1.8.0. One layer of image.compose. A layer draws one of: cg (a part of a CG, stretched
---to w x h), fill (a color) or text (in the HUD font, aligned in the box w x h).
---@class rq.ImageLayer
---@field cg? integer any CG number; the CG being composed reads the game's own picture
---@field src? integer[] {x, y, w, h} in the CG (default: all of it)
---@field fill? integer[]|{r: integer, g: integer, b: integer, a?: integer}
---@field text? string
---@field x? integer default 0
---@field y? integer default 0
---@field w? integer default: the part's own width (a fill: to the right edge)
---@field h? integer
---@field alpha? integer 0-255, default 255
---@field mode? "over"|"copy" copy replaces what is under it, alpha and all
---@field size? number text only, default 16
---@field color? integer[]|{r: integer, g: integer, b: integer} text only, default white
---@field align? "start"|"center"|"end" text only, in w
---@field valign? "top"|"center"|"bottom" text only, in h
---@field edge? boolean text only; false leaves out the dark edge
---@field bold? boolean text only

---Loader 1.8.0. A CG made from parts of other CGs, fills and text, built once when it is first
---shown. A key gives a new CG number as image.add does; a game CG number replaces that CG
---(image.restore undoes it). The size is opts.w x opts.h, or else the game CG's own size, or
---else the first layer's extent. Calling it again with the same key or number rebuilds it.
---Layers read other CGs as they are when it is built, mod images included.
---@param target string|integer
---@param layers rq.ImageLayer[]
---@param opts? {w?: integer, h?: integer}
---@return integer cg
function image.compose(target, layers, opts) end

---@class rq.GifOpts
---@field w? integer size on screen; default is the GIF's
---@field h? integer

---@class rq.GifInfo
---@field w integer size on screen
---@field h integer
---@field frames integer frames in the GIF
---@field ms integer length of one loop

---Serve a GIF from the mod folder as a game Flash file (loader 0.19.0). Named like a game file, it replaces that file.
---A bare key means Data/Flash/System/<key>.swf; a key with a slash is the full name. Errors on a bad file.
---@param key string
---@param path string file in the mod folder
---@param opts? rq.GifOpts
---@return string key
function image.add_gif(key, path, opts) end

---Undo add_gif. New loads use the game file again, if there is one.
---@param key string
---@return boolean removed
function image.remove_gif(key) end

---@param key string
---@return rq.GifInfo|nil
function image.gif_info(key) end

---@class rq.ItemEffect
---@field kind integer game effect code (機能), for example 20 = HP percent
---@field value integer

---@class rq.ItemDef
---@field id string text id, "mymod:binoculars". Becomes a stable number from 1000 up.
---@field name string
---@field type? "weapon"|"armor"|"item"|"special"|"quest"|"none" default "item"
---@field subtype? integer 種類詳細
---@field rank? integer 種類ランク (default 10)
---@field icon? integer|string game CG number or a file in the mod folder (100x100, proportions kept). Omitted: a grey question mark (loader 0.15.0)
---@field price? integer
---@field level? integer 装備ＬＶ
---@field rarity? integer レアランク
---@field classes? integer[] which classes may equip it (1..8). Omit for all.
---@field characters? integer[] 装備キャラ list
---@field effects? rq.ItemEffect[] up to 5
---@field info? string description; $ is a line break
---@field locked? boolean 外せません
---@field vanishes? boolean 消滅します
---@field on_create? fun(ii: integer)

---@class rq.ItemInfo
---@field ii integer
---@field no integer item number (固有番号)
---@field id? string text id when a mod defined it
---@field name string
---@field info string
---@field type integer
---@field type_name string
---@field subtype integer
---@field rank integer
---@field icon integer
---@field price integer
---@field level integer
---@field rarity integer
---@field place integer 6 shop, 7 vault, 8 quest storage, a character's iic when equipped
---@field locked integer
---@field vanishes integer
---@field effects rq.ItemEffect[]

---@class rq.item
local item = {}

---Define an item (create the template). No instance exists until it is given or stocked.
---Returns the number. Errors if the id collides with another one.
---@param def rq.ItemDef
---@return integer no
function item.define(def) end

---Create an instance and put it in the vault (or quest storage), or equip it.
---@param who string|integer item number or text id
---@param opts? {to?: integer, place?: "vault"|"quest"} to = a character's iic
---@return integer|nil ii
---@return string? err
function item.give(who, opts) end

---@param ii integer
---@return rq.ItemInfo|nil
function item.get(ii) end

---Write fields of an instance: name, info, effects, price, rarity, level, icon, rank, subtype, type, locked, vanishes, place.
---A familiar is subtype 20, with effects 1 to 3 as monster, class and growth. Its icon must be 6500 to 6516 or 6632, or it never takes a turn.
---@param ii integer
---@param fields table
function item.set(ii, fields) end

---The same instance becomes another item. It keeps its place and its owner.
---@param ii integer
---@param who string|integer
---@return boolean ok
function item.transform(ii, who) end

---Declarative edit of an item number: re-applied to every new instance of it.
---@param who string|integer
---@param patch table same fields as item.set
function item.edit(who, patch) end

---Put an existing instance in its slot on a character (the old item of that slot is replaced). False if it cannot be equipped there.
---@param ii integer
---@param iic integer
---@return boolean ok
function item.equip(ii, iic) end

---Delete an instance: it is taken off its owner first if equipped. False if there is no such item.
---@param ii integer
---@return boolean removed
function item.remove(ii) end

---Instances a character has equipped: { weapon, armor, item, special } (missing = empty).
---@param iic integer
---@return table
function item.equipped(iic) end

---@param ii integer
---@return integer|nil
function item.number(ii) end

---@param no integer
---@return string|nil
function item.id_of(no) end

---After any item is created (Ｉ生成), game or mod. May be registered many times.
---@param fn fun(ctx: {ii: integer, no: integer, id?: string})
---@return fun() remove Removes this callback. Loader 0.23.0.
function item.on_create(fn) end

---@class rq.store
local store = {}

---Per-mod value saved next to the active save profile (rqmod-store.json).
---@param key string
---@param default? any
---@return number|string|boolean|nil
function store.get(key, default) end

---number, string or boolean. nil deletes the key.
---@param key string
---@param value number|string|boolean|nil
function store.set(key, value) end

---@param key string
function store.delete(key) end

---@return string[]
function store.keys() end

---Loader 1.8.0. The same calls as rq.store, in one file for every save profile
---(rqmod-global.json, in the base profile's save folder). For settings, not for game state.
---@class rq.store_global
---@field get fun(key: string, default?: any): number|string|boolean|nil
---@field set fun(key: string, value: number|string|boolean|nil)
---@field delete fun(key: string)
---@field keys fun(): string[]
store.global = {}

---@class rq.PhysicalCtx
---@field attacker integer
---@field defender integer
---@field skill integer
---@field attack integer pre-defense attack
---@field magic integer
---@field race integer 8 = hanny
---@field defender_type integer Ｃタイプ of the defender (22 = humanoid)

---@class rq.AccuracyCtx
---@field attacker integer
---@field defender integer
---@field skill integer

---@class rq.CritCtx
---@field attacker integer
---@field defender integer
---@field skill integer
---@field rate integer critical chance (percent) the game is about to roll
---@field defender_type integer Ｃタイプ of the defender (22 = humanoid)

---@class rq.combat
local combat = {}

---Same patches as rq.chara.install.
---@return boolean
function combat.install() end

---fn(ctx) → nil, or attack, or attack + final percent (100 = unchanged, 50 = half of final damage).
---Loader 0.13.0: every mod's callback runs. Each one sees the attack the one before left, and the percents multiply.
---@param fn fun(ctx: rq.PhysicalCtx): integer|nil, integer|nil
---@return fun() remove Removes this callback. Loader 0.23.0.
function combat.on_physical(fn) end

---fn(ctx) → accuracy delta added at the aa-bb site. No hook → 0.
---Loader 0.13.0: every mod's callback runs and the deltas add up.
---@param fn fun(ctx: rq.AccuracyCtx): integer
---@return fun() remove Removes this callback. Loader 0.23.0.
function combat.on_accuracy(fn) end

---Loader 0.12.0. Called where the game rolls a critical (the 確率演算 call in クリティカル計算).
---Return true to force it, false to deny it, anything else (nil) to roll as vanilla.
---A forced critical still adds the game's +30 to the damage multiplier.
---Many callbacks: the first one that returns a boolean decides.
---Needs combat.install (or chara.install).
---@param fn fun(ctx: rq.CritCtx): boolean|nil
---@return fun() remove Removes this callback. Loader 0.23.0.
function combat.on_crit(fn) end

---@class rq.CounterGuardCtx
---@field guarded boolean FA[77]==1 (successful Ally Guard redirect this action)
---@field defender integer iic that took the hit (guard when redirected)
---@field attacker integer iic that attacked
---@field skill integer incoming skill id
---@field rate integer Counter rate before FA[77] wipe

---@class rq.counter
local counter = {}

---Patch the Counter FA[77] zero-out inside ダメージ後計算 only.
---Other FA[77] effects and other Counter gates stay vanilla.
---@return boolean
function counter.install() end

---fn(ctx) → true to allow Counter rate despite FA[77]. Loader 0.14.0: every mod's callback runs, and one true allows it. No hook → vanilla block.
---@param fn fun(ctx: rq.CounterGuardCtx): boolean
---@return fun() remove Removes this callback. Loader 0.23.0.
function counter.on_guard_counter(fn) end

---@class rq.BattleActor
---@field no integer battle slot (戦闘番号)
---@field chara integer character iic

---@class rq.BattleSelectKeyCtx
---@field key integer SACT key (Space = 32)
---@field no integer current actor battle slot
---@field chara integer current actor iic

---@class rq.shop
local shop = {}

---Wrap the tail of Ｔアイテム屋 (Frostbuy restock on every open).
---@return boolean
function shop.install() end

---Called after vanilla stock fill. Many callbacks allowed (since 0.9.0). No hook → vanilla only.
---@param fn fun()
---@return fun() remove Removes this callback. Loader 0.23.0.
function shop.on_stock(fn) end

---@class rq.BuyCtx
---@field ii integer the bought instance
---@field no integer item number
---@field id? string text id when a mod defined it
---@field price integer the instance's price
---@field name string

---After a Frostbuy purchase (T_Shop@buyItem). Many callbacks allowed.
---@param fn fun(ctx: rq.BuyCtx)
---@return fun() remove Removes this callback. Loader 0.23.0.
function shop.on_buy(fn) end

---True if item no is already shop stock (Ｉ存在 == 6).
---@param no integer
---@return boolean
function shop.in_stock(no) end

---Ｔアイテム屋／入荷品(no). Returns item instance ii, or 0 if already in shop / failed.
---@param no integer
---@return integer
function shop.stock(no) end

---@class rq.battle
local battle = {}

---Wrap T_BattleSelect@select / @checkKey so Lua can claim a key during ally skill select.
---@return boolean
function battle.install() end

---fn(ctx) → nil/false for vanilla, or skill id / {skill=id} to complete select like clicking that skill.
---When claimed, C swallows the key that frame (Space does not also Cancel).
---Loader 0.14.0: callbacks run in load order until one names a skill.
---@param fn fun(ctx: rq.BattleSelectKeyCtx): integer|table|nil
---@return fun() remove Removes this callback. Loader 0.23.0.
function battle.on_select_key(fn) end

---@class rq.BattleStartCtx
---@field boss boolean
---@field allies integer[] iic list
---@field enemies integer[] iic list (enemy slots are below 20)
---@field group integer ◆戦闘グループ
---@field rank integer ◆戦闘ランク

---@class rq.BattleEndCtx
---@field result integer the game's result code
---@field allies integer[]
---@field enemies integer[]
---@field group integer ◆戦闘グループ
---@field rank integer ◆戦闘ランク

---Once per battle, at the end of Ｔ戦闘前初期化 (all start-of-battle effects already ran). Many allowed.
---@param fn fun(ctx: rq.BattleStartCtx)
---@return fun() remove Removes this callback. Loader 0.23.0.
function battle.on_start(fn) end

---At the start of 戦闘後処理, before cleanup. Many allowed.
---@param fn fun(ctx: rq.BattleEndCtx)
---@return fun() remove Removes this callback. Loader 0.23.0.
function battle.on_end(fn) end

---@class rq.BattleSupportCtx
---@field user integer iic that uses the skill
---@field target integer iic it is aimed at (the user for a self skill)
---@field skill integer

---Loader 0.13.0. Called at the start of every support (blue) skill action (Ｔ補助行動), before the game applies its own
---effects. For a skill a mod defined, this is where Lua gives it its effect. Many callbacks allowed.
---@param fn fun(ctx: rq.BattleSupportCtx)
---@return fun() remove Removes this callback. Loader 0.23.0.
function battle.on_support(fn) end

---@class rq.BattleDownCtx
---@field iic integer the character the game just set to the downed state (Ｃ状態変化 = 3), ally or enemy

---Loader 0.12.0. Called when the game sets a character's state to 3 (downed), for every cause
---(damage, poison, capture, instant death). Loader 1.3.1: once per fall; before, it could fire
---again for a character that was already down.
---A revive is not reported. Many callbacks allowed.
---@param fn fun(ctx: rq.BattleDownCtx)
---@return fun() remove Removes this callback. Loader 0.23.0.
function battle.on_down(fn) end

---@class rq.BattleSwitchCtx
---@field left integer who stood on the rank, 0 for nobody
---@field came_in integer who stands there now, 0 for nobody
---@field rank integer 1 to 5

---Loader 0.29.0. Called for each ally rank that has someone else after a switch in a fight, with the new cards already on screen. Someone who only moved to another rank shows up as left on one rank and came_in on the other. Many callbacks allowed.
---@param fn fun(ctx: rq.BattleSwitchCtx)
---@return fun() remove Removes this callback.
function battle.on_switch(fn) end

---Loader 0.25.0. Runs each time a skill is carried out, before its effects, for allies, enemies and summons: once per hit of a multi-hit skill, and also for a counter. skill is the skill number; user and target are characters, target 0 when there is none; unit and target_unit are the battle numbers; side is "ally", "enemy" or "summon"; counter is true during a counter. It only reports, it cannot change or stop the skill. Many allowed.
---@param fn fun(ev: {skill: integer, user: integer, unit: integer, target: integer, target_unit: integer, side: string, counter: boolean})
---@return fun() remove Removes this callback. Loader 0.23.0.
function battle.on_use(fn) end

---Loader 0.12.0. Hook the ally status refresh so set_flame sticks. Also installs the battle start/end events
---(they clear every flame). Call once from init.lua.
---@return boolean
function battle.install_flame() end

---Loader 0.12.0. Show or hide the "charging" flame on an ally in the current battle. Allies share the enemies'
---mark object, which shows one mark at a time, so Protect, Berserk, Stealth and Barrier marks on that ally are replaced
---while the flame is on. Flames are forgotten at battle start and end (the battle screen drops its own marks).
---Returns false if the character is not in the party. Since loader 0.29.0 the flame goes with the character when the party changes places in a switch, and ends when they leave the party. Needs battle.install_flame.
---@param iic integer
---@param on boolean
---@return boolean
function battle.set_flame(iic, on) end

---@class rq.BuffDef
---@field icon? integer CG number. Use rq.image.add with fit, w and h; the spot holds about 56x56. Since loader 0.27.1 a number that is no CG raises an error, and at most 128 different icons and GIFs fit
---@field anim? string rq.image.add_gif key. Plays in the game's mark spot, about 35x26; wins over icon (loader 0.19.0)
---@field text? string short text over the icon
---@field order? integer place in the turns, lowest first; default 0, and the same order stays as added (loader 0.30.0)
---@field x? integer pixels from the left of the card to the left of the icon; default 111 on an ally, 37 on an enemy (loader 0.30.0)
---@field y? integer pixels from the top of the card to the top of the icon; default 80 on an ally, 22 on an enemy (loader 0.30.0)
---@field layer? integer z from the card's base, -100 to 100, the text one above it; default 10. Ally: 0 base, 1 text, face and skills, 40 dead, 50 no skill, 55 mask. Enemy: 0 base and name, 1 ▲▼ and states, 2 bar, 15 second line (loader 0.30.0)
---@field pinned? boolean stays shown, out of the turns (loader 0.30.0)

---@class rq.MarkDef
---@field hide? boolean does not show on any card
---@field icon? integer CG number shown instead of the game's picture
---@field anim? string rq.image.add_gif key shown instead of the game's picture; wins over icon
---@field x? integer pixels from the left of the card; default where the game puts it
---@field y? integer pixels from the top of the card; default where the game puts it
---@field layer? integer as in rq.BuffDef. The default of marks 1 to 23 is 1, which is under an ally's face
---@field order? integer marks 31 to 36 only: as in rq.BuffDef
---@field pinned? boolean marks 31 to 36 only: as in rq.BuffDef

---@class rq.BattleStates
---@field poison boolean
---@field stun boolean
---@field atk_up boolean
---@field atk_down boolean
---@field def_up boolean
---@field def_down boolean
---@field dead boolean

---@class rq.BuffInfo
---@field mark? integer game mark: 31 flame, 32 guard, 33 berserk, 34 stealth, 35 protect, 36 barrier
---@field key? string mod buff
---@field icon? integer mod buff
---@field rank? boolean true when the buff belongs to the rank (loader 0.29.0)
---@field pinned? boolean true when it stays shown, out of the turns (loader 0.30.0)
---@field text string

---Loader 0.16.0. Add or change a mod's buff on a character, or remove it with no def. Enemies too since loader 0.28.0. It takes turns with
---the other marks of that card, in the card's z order, so the game's windows cover it (loader 0.27.1). At most 8 per character, cleared at battle start and end. Since loader 0.29.0 a buff belongs to the character: it leaves the card with them, shows again if they return, and can be changed or removed while they are out.
---@param iic integer
---@param key string
---@param def? rq.BuffDef
---@return boolean ok false if that character has not stood on a card in this fight
function battle.set_buff(iic, key, def) end

---Loader 0.29.0. The same as set_buff, but the buff stays on the spot, whoever stands there. It comes after the character's own buffs, and an empty rank can hold one. At most 8 per rank, cleared at battle start and end.
---@param rank integer battle number: 1 to 5 for allies, 11 to 15 for enemies
---@param key string
---@param def? rq.BuffDef
---@return boolean ok false for any other number
function battle.set_rank_buff(rank, key, def) end

---Loader 0.30.0. Changes how a mark of the game shows, on every card, until changed again, also in later fights. Without the table it goes back to the game. Call rq.battle.install_flame() first.
---Loader 1.5.0: who = {iic = n} changes it for one character only, wherever they stand, and who = {no = n} for one card (1 to 5, 11 to 15), whoever stands there.
---A character's def wins over its card's, which wins over the one for every card, and replaces it whole: an empty table {} for one character shows the game's own mark there while every other card has a mod's icon.
---set_mark(mark, nil, who) removes that one. At most 64 for characters and cards together.
---@param mark integer 1 attack ▲, 2 attack ▼, 11 defense ▲, 12 defense ▼, 21 poison, 22 stun, 23 near death, 31 flame, 32 guard, 33 berserk, 34 stealth, 35 protect, 36 barrier
---@param def? rq.MarkDef
---@param who? {iic?: integer, no?: integer}
---@return boolean ok false for any other number
function battle.set_mark(mark, def, who) end

---Loader 0.31.0. Lets poison and stun be on one character at once, and ▲ and ▼ of one stat, from now until the game closes. Without this call the game keeps its own rules, where a new state replaces the old. rq.chara status, atk_buff and def_buff still show one value each, the one that came first, and stun over poison; use rq.battle.states for all of them.
---@return boolean ok
function battle.install_states() end

---Loader 0.31.0. Every state of a character, also without install_states.
---@param iic integer
---@return rq.BattleStates|nil states nil for a number that is no character
function battle.states(iic) end

---Loader 0.31.0. Adds or takes one state, by the rules of install_states, and the card shows it. Taking stun does not do poison damage unless poison is on: it counts as the stun ending. Call rq.battle.install_states() first.
---@param iic integer
---@param state "poison"|"stun"|"atk_up"|"atk_down"|"def_up"|"def_down"
---@param on boolean
---@return boolean ok false for a number that is no character, or someone dead
function battle.set_state(iic, state, on) end

---Loader 0.16.0. Everything the card shows in turn, by order, then what is pinned (loader 0.30.0). Enemies too since loader 0.28.0. For someone switched out, their own buffs that wait for their return (loader 0.29.0).
---@param iic integer
---@return rq.BuffInfo[]
function battle.buffs(iic) end

---The item number the battle will hand out after a win (◆戦闘後アイテム取得), 0 for none. Read it inside battle.on_end.
---@return integer
function battle.get_drop() end

---Set the post-battle drop. Item number or text id; nil or false clears it. The game skips its own drop rules when one is set.
---@param who string|integer|nil|false
function battle.set_drop(who) end

---Loader 1.12.0. A face for this character on the cards in a fight only (the party bar and the
---rest of the game keep the game's). face is a CG number or a GIF (a file in the mod's folder or an
---rq.image.add_gif key), stretched to 196x232; nil is the game's face. It lasts until changed, also
---in later fights. With opts.ms it is a face for a while over the lasting one, on the game's clock
---(clock = "real" for real time), ended by the fight's end; nil with ms shows the game's for a while.
---The game's face sprite is drawn over, so its moves, shakes and flashes stay. Allies only.
---@param iic integer
---@param face integer|string|nil
---@param opts? {ms?: number, fps?: number, clock?: "game"|"real"}
---@return boolean
function battle.set_face(iic, face, opts) end

---Loader 1.12.0. What set_face set: face and temp (a CG number or the GIF's name, left out for the
---game's), temp_ms left, the card (1 to 5) in a fight, and drawn in the last 250 ms. nil if nothing.
---@param iic integer
---@return {face?: integer|string, temp?: integer|string, temp_ms?: integer, card?: integer, drawn: boolean}|nil
function battle.get_face(iic) end

---@class rq.HpEvent
---@field iic integer
---@field unit integer the card: 1 to 5 allies, 11 to 15 enemies
---@field side "ally"|"enemy"
---@field hp integer
---@field max integer
---@field before integer the HP last seen; hp itself when first
---@field first boolean the first sight of this character on this card (start of a fight, a switch)

---Loader 1.12.0. In a fight, fn(ev) each time a character's HP changes (damage, healing, revival,
---rq.chara.set), allies and enemies, and once with first = true when one is first seen on a card.
---Read once a frame. Many allowed.
---@param fn fun(ev: rq.HpEvent)
---@return fun() remove
function battle.on_hp(fn) end

---True while T_BattleSelect@select is on the call stack (ally skill choice wait).
---@return boolean
function battle.in_ally_select() end

---Current acting ally during select, or nil.
---@return rq.BattleActor|nil
function battle.actor() end

---Equipped + uses remaining + not locked. Actor table ({chara}/{no}) or chara iic. While select is open it answers like the skill's button: false when the button is greyed out (no uses left, locked, or a front-row skill from the back), since loader 1.0.0.
---@param actor rq.BattleActor|integer
---@param skill_no integer
---@return boolean
function battle.skill_usable(actor, skill_no) end

---Loader 0.22.0. Opens a fight with the current party, using the game's scripted fight. enemies lists up to five monster numbers in rank order, 0 for an empty rank. Blocks until the fight and its result screens end, while rq.on_frame keeps running; call it from a coroutine if the same mod plays the fight. Raises an error for no enemies, an unknown monster, an empty party, or during a battle.
---@param opts {enemies: integer[], exp?: integer}
---@return "won"|"lost"|"other"
function battle.start(opts) end

---Loader 0.22.0. "hit" or "miss" decides every accuracy check, of allies and enemies, until force_hit(nil). A hit can still do 0 damage. The game keeps the last result in the global ◆攻撃外れた (1 is a miss).
---@param mode "hit"|"miss"|nil
function battle.force_hit(mode) end

---Loader 0.26.0. Actions left in this fight, the number on the TIME REMAINING panel. 999 means no limit. 0 outside a fight.
---@return integer
function battle.time() end

---Loader 0.26.0. Sets the actions left in this fight, 0 to 999. 999 means no limit. The game sets the count again when a fight opens, so a value set outside one is lost.
---@param n integer
function battle.set_time(n) end

---Read-only fight snapshot. up is false when ally select is not open.
---@return rq.BattleSnap
function battle.snapshot() end

---@class rq.SkillBrief
---@field id integer
---@field name string
---@field rank? integer
---@field cost? integer

---@class rq.Unit
---@field id integer character number
---@field name string
---@field class string
---@field classId integer
---@field level integer
---@field stats {hp: integer, hpMax: integer, atk: integer, def: integer, mag: integer, mdef: integer, hit: integer, eva: integer, crit: integer, impact: integer, resist: integer, spd: integer}
---@field equipment {special: integer, weapon: integer, armor: integer, item: integer}
---@field active rq.SkillBrief[]
---@field learned rq.SkillBrief[]
---@field potential rq.SkillBrief[]

---@class rq.Item
---@field id integer
---@field name string
---@field type string weapon|armor|special|item
---@field typeId integer
---@field class string
---@field classId integer
---@field level integer
---@field description string

---@class rq.Quest
---@field id integer
---@field name string
---@field description string
---@field map string

---@class rq.CastleRoom
---@field id integer
---@field name string
---@field state integer
---@field enabled boolean
---@field used boolean already used
---@field units {id: integer, name: string}[]

---@class rq.Castle
---@field unlocked boolean
---@field enabled boolean
---@field used boolean already used
---@field open integer raw castle-open flag
---@field rooms rq.CastleRoom[]
---@field units {id: integer, name: string}[]

---@class rq.DungeonInfo
---@field roomId integer
---@field roomName string
---@field mapX integer
---@field mapZ integer
---@field cell {x: integer, z: integer}
---@field party {slot: integer, id: integer, name: string}[]

---@class rq.BattleSnap
---@field up boolean
---@field allies {id: integer, no: integer, name: string}[]
---@field enemies {id: integer, no: integer, name: string}[]
---@field skills {id: integer, name: string, uses: integer, enabled: boolean}[]
---@field log string[]

---@class rq.QuestsApi
---@overload fun(): rq.Quest[]
---@field list fun(): rq.Quest[]
local quests = {}

---@class rq.QuestClearCtx
---@field quest integer Ｑ index of the quest that was just cleared
---@field clears integer clear count including this one
---@field first boolean first clear of this quest

---Runs when a quest is cleared cleanly (not abandoned, not escaped, not failed), at the end of クエスト終了処理／クリアフラグ.
---Characters recruited here (rq.chara.recruit) and quests unlocked here (rq.quests.unlock) show on the victory screen like the game's own. Many callbacks allowed.
---@param fn fun(ctx: rq.QuestClearCtx)
---@return fun() remove Removes this callback. Loader 0.23.0.
function quests.on_clear(fn) end

---0 locked, 1 available, 2 cleared.
---@param no integer
---@return integer
function quests.state(no) end

---Ｑ状態(no, state). State 2 also records a first clear.
---@param no integer
---@param state integer 0, 1 or 2
function quests.set_state(no, state) end

---Make a locked quest available (Ｑ状態(no, 1)). It joins the "new quests" list on the victory screen. False if it was not locked.
---@param no integer
---@return boolean unlocked
function quests.unlock(no) end

---@class rq.QuestEndCtx
---@field quest integer
---@field result integer the game's ending code: 1 clean clear, 2 party defeat, 3 abandoned, 4 escape device (abandoned, items kept)
---@field cleared boolean result == 1
---@field party integer[] iic list

---Runs at the start of クエスト終了処理 for every ending (clear, abandon, defeat). Many callbacks allowed.
---@param fn fun(ctx: rq.QuestEndCtx)
---@return fun() remove Removes this callback. Loader 0.23.0.
function quests.on_end(fn) end

---Loader 0.18.0. The quest being played, 0 in town.
---@return integer
function quests.current() end

---Start a quest through the game's own flow, from the hub or the quest list. Returns at once; the game moves on its next update, and rq.quests.current() gives the number when it started. Raises an error for an unknown number, a party that cannot take the quest, a running quest, or another screen. Loader 0.23.0.
---@param no integer
function quests.start(no) end

---@class rq.QuestEvent
---@field quest integer
---@field kind "start"|"map_load"|"map_event"|"warp"|"warp_cancel"|"before_warp"|"before_battle"|"after_battle"|"chest"|"contact"|"other"
---@field no integer map event number, or map number for map_load
---@field code integer クエスト処理 argument dd
---@field fg integer クエスト処理 argument fg
---@field result? integer on_event_after only

---Loader 0.18.0. Runs before the game handles a quest moment. Return a number to skip the game's handling: 0 continues the quest, other values end it (1 returns to town).
---@param fn fun(ev: rq.QuestEvent): integer|nil
---@return fun() remove Removes this callback. Loader 0.23.0.
function quests.on_event(fn) end

---Loader 0.18.0. Runs after the game handled it. Return a number to change the result.
---@param fn fun(ev: rq.QuestEvent): integer|nil
---@return fun() remove Removes this callback. Loader 0.23.0.
function quests.on_event_after(fn) end

---@class rq.NewThisRun
---@field characters integer[] iic of characters added during the quest, shown on the victory screen
---@field quests integer[] quests unlocked during the quest

---What the victory screen will show. The game empties both lists when a quest ends.
---@return rq.NewThisRun
function quests.new_this_run() end

---@class rq.scene
---Loader 0.18.0. Commands block until they end. Run them inside rq.scene.play.
---Loader 1.3.1: each line of text has its own read flag (by mod and text), so skip mode passes only lines already read.
local scene = {}

---Any game command by its name: rq.scene.cmd["効果_絶頂"](...). Same rules as rq.ain.call.
---@type table<string, fun(...): any>
scene.cmd = {}

---@class rq.SceneTextOpts
---@field name? string name plate; default is the face's registered name
---@field face? boolean false hides the face
---@field page? boolean the game's E variant
---@field paper? boolean narrate only: the paper frame
---@field wait? boolean false shows the text without waiting for a click (use it before a choice)

---ADVON, fn(scene, ...), then clear text and ADVOFF. A Lua error is logged and the scene still closes.
---@param fn fun(s: rq.scene, ...): any
---@return any
function scene.play(fn, ...) end
---Play once per save. False if it was already seen.
---@param id string
---@param fn fun(s: rq.scene, ...)
---@return boolean played
function scene.once(id, fn, ...) end
---Loader 1.14.0. Plays fn(s, ...) as rq.scene.play does, in a coroutine of its own, at the next
---frame, and returns at once. False, and nothing queued, while a mod scene plays or waits to, so it
---is safe from anywhere, every frame of rq.on_frame too. It waits while the System menu or Config
---is open, and while a scene of the game's own is on screen. An error that ends it is logged.
---@param fn fun(s: rq.scene, ...)
---@return boolean started
function scene.start(fn, ...) end
---Loader 1.14.0. True while a mod scene plays (play, once or start), or start waits to play one.
---@return boolean
function scene.active() end
---Read the game's event flag, or set it with a second argument. Game scene names work too.
---@param id string
---@param set? boolean
---@return boolean
function scene.seen(id, set) end
---Character line. A newline in text is a line break.
---@param face integer face CG
---@param text string
---@param opts? rq.SceneTextOpts
function scene.say(face, text, opts) end
---Inner thought.
---@param face integer
---@param text string
---@param opts? rq.SceneTextOpts
function scene.think(face, text, opts) end
---Narration.
---@param text string
---@param opts? rq.SceneTextOpts
function scene.narrate(text, opts) end
---Add text to the open frame without waiting.
---@param text string
function scene.text(text) end
function scene.br() end
---Wait for a click and clear the page.
function scene.wait() end
---Show the text without waiting.
function scene.show() end
function scene.clear_text() end
---@param ms integer 0 instant, -1 the player's setting
function scene.speed(ms) end
function scene.begin() end
function scene.finish() end
---@param cg integer
---@param effect? integer
---@param time? integer
function scene.wallpaper(cg, effect, time) end
function scene.mask_wallpaper(cg, effect, time) end
function scene.background(cg, effect, time, name, flag) end
function scene.background_in(cg, time, name, flag) end
function scene.background_out(time) end
function scene.portrait(cg, effect, time) end
function scene.mini(cg, effect, time) end
function scene.cg(cg, effect, time) end
---Show several layers at once. 0 leaves a layer empty.
function scene.set(wallpaper, background, portrait, mini) end
function scene.effect_set(effect, wallpaper, background, portrait, mini, event_cg) end
function scene.black(effect, time) end
function scene.white(effect, time) end
function scene.flash(time) end
function scene.shake(mode, flag) end
function scene.shake_face(mode, flag) end
function scene.shake_portrait(mode, flag) end
function scene.music(no, effect, time) end
---A game sound number, for scenes. rq.sound.play also plays files and returns a handle.
function scene.sound(no) end
function scene.jingle(no, wait) end
function scene.clear() end
function scene.wait_click() end
---Name plate for a game face number (2000 and up). Custom faces pass opts.name instead.
function scene.register_face(cg, name, tone) end
---@class rq.SceneChoice
---@field id? integer default is the position in the list
---@field text string
---@field r? integer
---@field g? integer
---@field b? integer
---@field comment? string
---@field sound? integer

---Blocks until the player picks. "-" in the list is a separator.
---@param items (string|rq.SceneChoice)[]
---@param opts? {x?: integer, y?: integer, cancel?: boolean}
---@return integer id
function scene.choice(items, opts) end
---@param name string AIN function name, usually a game scene
---@param fn fun(h: rq.AinHookCtx)
function scene.before(name, fn) end
---@param name string
---@param fn fun(h: rq.AinHookCtx)
function scene.after(name, fn) end
---The game's function does not run. What fn returns becomes its result.
---@param name string
---@param fn fun(h: rq.AinHookCtx): any
function scene.replace(name, fn) end

---@class rq
---@field version string loader semver, currently 1.14.1
---@field game_dir string game root (AIN)
---@field mods_dir string mods\
---@field units fun(): rq.Unit[]
---@field items fun(): rq.Item[]
---@field quests rq.QuestsApi
---@field castle fun(): rq.Castle
---@field ain rq.ain
---@field dungeon rq.dungeon
---@field input rq.input
---@field debug rq.debug
---@field hud rq.hud
---@field saves rq.saves
---@field loot rq.loot
---@field chest rq.chest
---@field orb rq.orb
---@field skill rq.skill
---@field chara rq.chara
---@field combat rq.combat
---@field counter rq.counter
---@field shop rq.shop
---@field battle rq.battle
---@field store rq.store
---@field image rq.image
---@field item rq.item
---@field speed rq.speed
---@field sound rq.sound
---@field uikit rq.uikit
---@field config rq.config
rq = {}

---Loader semver. A mod whose manifest loader is newer than this is skipped.
---@type string
rq.version = "1.14.1"

---Recruited units. Copy; writing it does not change the save.
---Right after boot it lists only the three default characters made before the save loads: wait for more than 10 units or use rq.saves.
---@return rq.Unit[]
function rq.units() end

---Owned item instances (existence not 0, 6, or 9). Copy.
---@return rq.Item[]
function rq.items() end


---Castle open flag, rooms, and units listed in the castle roster.
---@return rq.Castle
function rq.castle() end

---Console (if terminal) + logs\rqmod.log (the logs folder beside mods, loader 1.1.0). Prefixed with the current mod folder/file.
---@param ... any
function rq.log(...) end

---fn() once per Chipmunk/SACT Update. Draw with rq.hud.* here. Every mod may register one; they run in load order. Runs once per game update, which is not one per drawn frame: on a screen that does not redraw it can run thousands of times per second. Use rq.time_ms for anything that must last a real time.
---@param fn fun()
---@return fun() remove Removes this callback. Loader 0.23.0.
function rq.on_frame(fn) end

---Loader 1.13.0. fn(why) after a load ("load") or the title ("title"), when the game has started
---again, so a mod can clear its own flags. The same callbacks as rq.sprite.on_reset. On the title,
---every Lua call that waited in the game (a scene, a fight from rq.battle.start) has ended first
---with the error "rq: the game went to the title".
---@param fn fun(why: "load"|"title")
---@return fun() remove
function rq.on_reset(fn) end

---Loader 0.20.0. Milliseconds since the game started, in real time.
---@return integer
function rq.time_ms() end

---Loader 0.20.0. Stack trace, of a coroutine if given. The sandbox has no debug library.
---@param co thread|string|nil
---@param msg? string
---@return string
function rq.traceback(co, msg) end

rq.ain = ain
rq.dungeon = dungeon
rq.input = input
rq.ui = ui
rq.hud = hud
rq.saves = saves
rq.loot = loot
rq.chest = chest
rq.orb = orb
rq.skill = skill
rq.chara = chara
rq.combat = combat
rq.counter = counter
rq.shop = shop
rq.battle = battle
rq.skilllist = skilllist
rq.debug = debug
rq.agent = agent
---@class rq.party
local party = {}

---Loader 0.22.0. Character id on each of the five ranks, 0 for an empty one.
---@return integer[]
function party.get() end

---Loader 0.22.0. Replaces the party. Up to five ids in rank order; 0 or false leaves a rank empty. Raises an error during a battle, for a character not recruited, for one listed twice, or for more than five ranks, and then changes nothing. A screen that is already open does not redraw.
---@param ranks (integer|false)[]
function party.set(ranks) end

rq.party = party

---@class rq.random
local random = {}

---Loader 0.22.0. Starts the generator from n. The same seed gives the same numbers, but a fight does not replay the same way, because effects on screen use the same generator.
---@param n integer
function random.seed(n) end

---Loader 0.22.0. Every roll gives this value until fix(nil). RAND(n) becomes value % n + 1. Blunt: fix(0) also fires every check written as RAND(5) == 1, so it does not guarantee a hit. Use it only for a short time, because a check that rolls until it gets a new number would never end.
---@param value integer|nil
function random.fix(value) end

rq.random = random

---Runs the game clock faster or slower than real time.
---
---The game clock drives the game's timers and waits, Flash and parts animations, the 3D map,
---rain and snow, and battle effects. Real time stays real: sound and music, the frame cap,
---`rq.time_ms`, the loader's own dialogs and the play time written to the save. The factor works
---on top of the game's own battle speed setting.
---
---The factor is 1 on every boot, so a mod that wants another speed sets it each time it loads.
---
---Sounds still play at normal speed, so at 3x a voice can last longer than its animation. Frames
---of a Flash animation that are skipped still start their sounds.
---
---Since: 1.2.0
---@class rq.speed
local speed = {}

---Sets the game clock to `factor` times real time.
---
---The clock does not jump: game time goes on from where it was, at the new rate.
---
---Since: 1.2.0
---
---```lua
---rq.speed.set(2)                -- twice as fast
---local now = rq.speed.set(100)  -- clamped: now == 8
---```
---@param factor number How many times faster than real time. Clamped to 0.25..8.
---@return number factor The factor now in use, after clamping.
---@see rq.speed.get
function speed.set(factor) end

---Returns the factor the game clock runs at.
---
---Since: 1.2.0
---@return number factor 1 unless a mod called `rq.speed.set`.
---@see rq.speed.set
function speed.get() end

rq.speed = speed

---@class rq.sound
local sound = {}

---@class rq.SoundOpts
---@field volume? integer 0..100, default 100
---@field loop? integer times to play, default 1; 0 loops until stopped
---@field index? integer play_flash only: which sound of the file, from 1

---@class rq.FlashSound
---@field rate integer Hz
---@field channels integer 1 or 2
---@field bits integer 8 or 16
---@field ms integer length
---@field playable boolean false for a compressed sound (MP3 or ADPCM), which play_flash refuses

---Loader 1.4.0. Plays on the game's sound effect mixer, so Config's sound volume applies, at real time (rq.speed does not change it).
---src is a game sound number (as rq.scene.sound) or a path in the mod folder: WAV, OGG, FLAC, Opus or MP3. A file is read once and kept in memory.
---Returns a handle, or nil when the number does not exist or the file is not a sound. A missing file or a path out of the mod folder raises an error.
---At most 32 mod sounds at once; one more raises an error.
---@param src integer|string
---@param opts? rq.SoundOpts
---@return integer|nil handle
function sound.play(src, opts) end

---Loader 1.4.0. Plays a sound kept inside a Flash file, such as Berserk's in "Data/Flash/Battle/狂戦士発動.swf". A bare key means Data/Flash/System/<key>.swf, as in rq.image.add_gif, and a GIF added under that key is looked at first.
---Raises an error when the file or that sound is missing, or the sound is compressed.
---@param key string
---@param opts? rq.SoundOpts
---@return integer|nil handle
function sound.play_flash(key, opts) end

---Loader 1.4.0. The sounds in a Flash file, in the order index counts them. nil when there is no such file.
---@param key string
---@return rq.FlashSound[]|nil
function sound.flash_info(key) end

---Loader 1.4.0. Stops at once, or fades out over fade_ms. false when the handle already ended.
---@param handle integer
---@param fade_ms? integer
---@return boolean
function sound.stop(handle, fade_ms) end

---Loader 1.4.0. Whether the sound still plays. An ended handle stays false, even when its slot plays something new.
---@param handle integer
---@return boolean
function sound.playing(handle) end

---Loader 1.4.0. Changes the volume (0..100) at once or over fade_ms. false when the handle already ended.
---@param handle integer
---@param volume integer
---@param fade_ms? integer
---@return boolean
function sound.set_volume(handle, volume, fade_ms) end


---Loader 1.7.0. A part of a CG drawn at any size: the edges stay, the middle stretches.
---@class rq.UiPiece
---@field cg integer
---@field src? integer[] {x, y, w, h} in the CG (default: all of it)
---@field edge? integer[] {left, top, right, bottom} kept as they are
---@field mid? integer[] {x, w}: the middle columns, from x in src (default: what the edges leave)
---@field vmid? integer[] {y, h}: the middle rows, from y in src
---@field vfit? "crop" take the top rows of the middle instead of squashing it

---Loader 1.7.0. A look for the kit: pieces, text styles, colors and sounds. See rq.uikit.themes.config.
---@class rq.UiTheme
---@field label rq.HudTextStyle
---@field dim {r: integer, g: integer, b: integer}
---@field page rq.UiPiece
---@field block rq.UiPiece
---@field button rq.UiPiece
---@field button_text rq.HudTextStyle
---@field button_cover {r: integer, g: integer, b: integer, a: integer}
---@field tab {normal: rq.UiPiece, hover: rq.UiPiece, current: rq.UiPiece}
---@field tab_text {normal: rq.HudTextStyle, hover: rq.HudTextStyle, current: rq.HudTextStyle}
---@field check_frame rq.UiPiece
---@field check_mark rq.UiPiece
---@field hover_bar rq.UiPiece
---@field track rq.UiPiece
---@field knob rq.UiPiece
---@field knob_hot rq.UiPiece
---@field list_text rq.HudTextStyle
---@field list_current {r: integer, g: integer, b: integer}
---@field scrollbar {r: integer, g: integer, b: integer, a: integer}
---@field sound {hover?: integer, click?: integer, error?: integer}

---Where a widget goes. Without x and y it goes where its row or column puts it.
---@class rq.UiPlace
---@field x? integer
---@field y? integer
---@field w? integer|"fill"
---@field h? integer
---@field id? any keeps the widget's state across updates; give one to widgets that show or hide

---@class rq.UiBox: rq.UiPlace
---@field gap? integer
---@field pad? integer
---@field align? "start"|"center"|"end"

---@class rq.UiMenuItem
---@field text string
---@field items? (string|rq.UiMenuItem)[]
---@field disabled? boolean

---Loader 1.7.0. Widgets in the look of the game's Config screen, drawn with rq.hud from the
---game's own pictures. Immediate mode: call them from rq.on_frame on every update they show,
---keep the values, and pass them back in. The game sees no mouse while one is under it.
---@class rq.uikit
---@field theme rq.UiTheme the theme in use
---@field themes {config: rq.UiTheme}
local uikit = {}

---White text with a dark edge. opts: size, color = {r, g, b}, dim, align, valign.
---@param text string
---@param opts? rq.UiPlace|{size?: number, color?: integer[], dim?: boolean, align?: string, valign?: string}
---@return integer x
---@return integer y
---@return integer w
---@return integer h
function uikit.label(text, opts) end

---A Config button with text; the cover pulses when hovered. True on the update it is clicked
---(let go over the button it went down on).
---@param text string
---@param opts? rq.UiPlace|{disabled?: boolean}
---@return boolean clicked
function uikit.button(text, opts) end

---@param value boolean
---@param text? string
---@param opts? rq.UiPlace|{disabled?: boolean}
---@return boolean value
---@return boolean changed
function uikit.checkbox(value, text, opts) end

---One of several checkboxes.
---@param list string[]
---@param index? integer
---@param opts? rq.UiPlace|{dir?: "col"|"row", gap?: integer, disabled?: boolean}
---@return integer|nil index
---@return boolean changed
function uikit.choice(list, index, opts) end

---The Sound tab's slider: drag, click the track, or the wheel over it (the game does not get it).
---opts.w is the track's width (default 200).
---@param value number
---opts.label (1.8.0) names it in rqmod-ctl ui.
---@param opts? rq.UiPlace|{min?: number, max?: number, step?: number, low?: string, high?: string, label?: string, disabled?: boolean}
---@return number value
---@return boolean changed
function uikit.slider(value, opts) end

---Config's tabs, each opts.w wide (default 120).
---@param list string[]
---@param index integer
---@param opts? rq.UiPlace
---@return integer index
---@return boolean changed
function uikit.tabs(list, index, opts) end

---A Config page (style "page") or the 3D tab's dark block ("block"); fn runs in a column inside.
---@param opts rq.UiPlace|{style?: "page"|"block", pad?: integer, gap?: integer, title?: string}
---@param fn? fun(x: integer, y: integer, w: integer, h: integer)
---@return integer x
---@return integer y
---@return integer w
---@return integer h
function uikit.panel(opts, fn) end

---Rows on a dark block that scroll by the wheel. opts.w and opts.h are needed.
---@param items (string|{text: string, disabled?: boolean})[]
---@param index? integer
---@param opts rq.UiPlace|{row_h?: integer, block?: boolean}
---@return integer|nil index
---@return boolean changed
function uikit.list(items, index, opts) end

---Lists side by side: an item with items opens the next list. path is the index at each level;
---it is a new table when it changed. opts.h is needed; opts.w is each list's width (200).
---@param tree rq.UiMenuItem[]
---@param path? integer[]
---@param opts rq.UiPlace|{gap?: integer}
---@return integer[] path
---@return boolean changed
---@return string|rq.UiMenuItem|nil item
function uikit.menu(tree, path, opts) end

---Widgets called in fn go side by side.
---@param opts rq.UiBox|fun()
---@param fn? fun(x: integer, y: integer, w?: integer, h?: integer)
---@return integer x
---@return integer y
---@return integer w
---@return integer h
function uikit.row(opts, fn) end

---Widgets called in fn go one under the other.
---@param opts rq.UiBox|fun()
---@param fn? fun(x: integer, y: integer, w?: integer, h?: integer)
---@return integer x
---@return integer y
---@return integer w
---@return integer h
function uikit.col(opts, fn) end

---Empty space in a row or column.
---@param w integer
---@param h integer
function uikit.space(w, h) end

---Where the last widget went.
---@return integer x
---@return integer y
---@return integer w
---@return integer h
function uikit.last() end

---The mouse was over a kit widget on this update so far.
---@return boolean
function uikit.hovered() end

---What the kit keeps for a widget, by kind and id ("list:options").
---@param id string
---@return table
function uikit.state(id) end

---Draws a piece of the theme (its name, or a piece) at any size.
---@param piece string|rq.UiPiece
---@param x integer
---@param y integer
---@param w? integer
---@param h? integer
---@param opts? rq.HudCgOpts
function uikit.draw(piece, x, y, w, h, opts) end

---A deep copy of a theme (default: Config's), to change and pass to use.
---@param theme? rq.UiTheme
---@return rq.UiTheme
function uikit.copy_theme(theme) end

---Use this theme from now on, for every mod. Set it back when your HUD closes.
---@param theme rq.UiTheme
function uikit.use(theme) end

---Loader 1.8.0. A piece as layers for rq.image.compose, drawn at x, y, w by h in the new CG.
---@param piece string|rq.UiPiece
---@param x? integer
---@param y? integer
---@param w? integer
---@param h? integer
---@return rq.ImageLayer[]
function uikit.layers(piece, x, y, w, h) end

---Loader 1.8.0. A page in the Mods tab of the game's Config screen.
---@class rq.ConfigPage
---@field id? string keeps the page across reloads (default: the title)
---@field title string its row in the list and its heading
---@field draw fun(ui: rq.uikit, box: {x: integer, y: integer, w: integer, h: integer}) runs on every update the page shows, inside a column at box
---@field on_open? fun() the page came on screen
---@field on_close? fun() it went away (another page, another tab, Config closed)

---Loader 1.8.0. The Mods tab in Config: once a mod adds a page, Config gets a tab after Other 2
---with a list of the pages on the left and the chosen page on the right, drawn with rq.uikit.
---Keep values in rq.store or rq.store.global and change them as the widgets say.
---@class rq.config
local config = {}

---Adds a page, or replaces the one with the same id. Returns the id.
---@param page rq.ConfigPage
---@return string id
function config.page(page) end

---The Mods page is on screen (and no game dialog is over it).
---@return boolean
function config.is_open() end

---The page shown, and every page's id in the list's order.
---@return string|nil current
---@return string[] ids
function config.pages() end

---Show this page (now, or the next time the Mods tab opens).
---@param id string
---@return boolean found
function config.show(id) end

---Loader 1.9.0. Options for rq.sprite.new and s:set. new needs one of gif, cg, fill or text.
---@class rq.SpriteOpts
---@field gif? string 1.10.0: a GIF file in the mod's folder, or an rq.image.add_gif key. Plays over and over from the start
---@field fps? number 1.10.0: frames a second for the GIF; 0 (default) for its own delays
---@field clock? "game"|"real" 1.10.0: GIFs and motion on the game's clock (follows rq.speed, the default) or real time
---@field cg? integer a CG number (rq.image.add gives one for a file in the mod)
---@field src? {x: integer, y: integer, w: integer, h: integer}|false a part of the CG; false for the whole
---@field fill? {r: integer, g: integer, b: integer, a?: integer} a colour; needs w and h
---@field text? string text in the HUD font
---@field size? number text size, default 16
---@field color? {r: integer, g: integer, b: integer} text colour, default white
---@field edge? boolean the text's dark edge, default true
---@field bold? boolean
---@field x? number the top left before scale and turn
---@field y? number
---@field w? number stretch to this width (0: its own)
---@field h? number
---@field scale? number both scale_x and scale_y
---@field scale_x? number
---@field scale_y? number
---@field rotate? number degrees, clockwise on screen, around origin
---@field origin? {x: number, y: number}|"center" in the sprite's own pixels from its top left
---@field alpha? integer 0 to 255
---@field tint? {r: integer, g: integer, b: integer} multiplied
---@field add? {r: integer, g: integer, b: integer} added before the tint
---@field blend? "normal"|"add"
---@field show? boolean default true
---@field z? integer|"back"|"cards"|"under_windows"|"top" default "under_windows" (1000)
---@field keep? "screen"|"removed" "screen": removed when rq.ui.get().screen changes
---@field attach? rq.SpriteAttach|false 1.11.0: follow a target; x, y and z are added to its (z 1 by default); false lets it go

---Loader 1.11.0. What an attached sprite follows; give one of these. card and chara work only
---in a fight and go at its end; sprite and game go when their target goes.
---@class rq.SpriteAttach
---@field card? integer the card at this place: 1 to 5 allies, 11 to 15 enemies
---@field chara? integer this character's card, wherever a switch puts it
---@field sprite? rq.Sprite another mod sprite
---@field game? integer a game sprite number from rq.sprite.scene

---Loader 1.9.0. A mod sprite among the game's own, made by rq.sprite.new.
---@class rq.Sprite
local Sprite = {}

---Changes only the options given. Returns the sprite.
---@param opts rq.SpriteOpts
---@return rq.Sprite
function Sprite:set(opts) end

---Its options now, with id, mod, w and h before the scale, origin as {x, y}, screen
---(keep = "screen") and in_scene. 1.11.0: attach, and drawn = {x, y, z} where it is drawn.
---@return table
function Sprite:get() end

---Removes it. set and get raise an error after that.
function Sprite:remove() end

---False once it was removed (by the mod, a load, the title or keep).
---@return boolean
function Sprite:alive() end

---Loader 1.10.0. Plays its GIF from `from` (default 1) to `to` (default the last frame), over
---again unless loop = false. on_end(s) runs when a play without loop reaches its last frame.
---@param opts? {from?: integer, to?: integer, loop?: boolean, on_end?: fun(s: rq.Sprite)}
---@return rq.Sprite
function Sprite:play(opts) end

---Loader 1.10.0. Holds the GIF on the frame it is on.
---@return rq.Sprite
function Sprite:stop() end

---Loader 1.10.0. With no n: the frame shown (from 1) and the count. With n: shows it and stops.
---@param n? integer
---@return integer|rq.Sprite frame
---@return integer? count
function Sprite:frame(n) end

---Loader 1.10.0. Whether its GIF is playing.
---@return boolean
function Sprite:playing() end

---Loader 1.10.0. Options for s:to: the values to move to, and how.
---@class rq.SpriteMotion
---@field x? number
---@field y? number
---@field w? number
---@field h? number
---@field scale? number
---@field scale_x? number
---@field scale_y? number
---@field rotate? number
---@field alpha? integer
---@field tint? {r: integer, g: integer, b: integer}
---@field add? {r: integer, g: integer, b: integer}
---@field ms? number how long, default 0 (a jump)
---@field delay? number ms before it starts
---@field ease? "linear"|"in"|"out"|"in_out" default "linear"
---@field on_end? fun(s: rq.Sprite) once the last values are drawn

---Loader 1.10.0. Moves from the values it has now to these. A new one takes the place of the
---one moving, from where it got to (the old on_end does not run). s:set of a value takes that
---value out of the motion.
---@param opts rq.SpriteMotion
---@return rq.Sprite
function Sprite:to(opts) end

---Loader 1.10.0. Whether a motion is waiting or moving.
---@return boolean
function Sprite:moving() end

---Loader 1.10.0. Stops the motion where it is, without its on_end.
---@return rq.Sprite
function Sprite:halt() end

---Loader 1.9.0. One row of rq.sprite.scene, back to front.
---@class rq.SceneRow
---@field kind "sprite"|"part"|"mod"|"other"
---@field no integer the sprite or part number; a mod sprite's id
---@field z integer
---@field z2 integer
---@field x integer
---@field y integer
---@field w integer
---@field h integer
---@field cg integer 0 when it shows no CG
---@field part? string a part's kind: cg, text, flash and so on
---@field mod? string the mod of a mod sprite

---Loader 1.9.0. Sprites a mod makes once and changes later, drawn among the game's own in z
---order, so its windows cover them (rq.hud is drawn over everything). Not saved; a load and the
---title remove every mod sprite. Call new from rq.on_frame or a callback.
---@class rq.sprite
local sprite = {}

---@param opts rq.SpriteOpts
---@return rq.Sprite
function sprite.new(opts) end

---This mod's sprites, oldest first.
---@return rq.Sprite[]
function sprite.list() end

---Removes all of this mod's sprites.
---@return integer count
function sprite.clear() end

---fn(why) after a load ("load") or the title ("title") removed every mod sprite.
---@param fn fun(why: "load"|"title")
---@return fun() remove
function sprite.on_reset(fn) end

---The game's scene list now, back to front.
---@return rq.SceneRow[]
function sprite.scene() end

rq.sound = sound
rq.uikit = uikit
rq.config = config
rq.sprite = sprite
rq.store = store
rq.quests = quests
rq.scene = scene
rq.image = image
rq.item = item
