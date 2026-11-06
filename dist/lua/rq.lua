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

---Reads the mouse and keyboard, and drives them for the game.
---
---The read calls see the real buttons, also while `rq.hud.block_game_input` or `rq.hud.block_keys`
---hides them from the game, so a modal HUD can still read clicks, Enter and Esc. The `set_*` calls,
---`wheel` and `release` act for the player, as a test or an automation would. Typed text is read
---with `rq.hud.text_begin`.
---
---Since: 0.2.0
---@class rq.input
local input = {}

---Returns the mouse position and buttons on this update.
---
---The buttons are the real ones, also while the game's input is blocked. `pressed` and `released`
---are true only on the update the left button went down or came up, as a slider needs to start and
---end a drag.
---
---Since: 0.2.0
---Changed: 1.6.0 Added the `pressed` and `released` results.
---
---```lua
---local x, y, left, right, pressed = rq.input.mouse()
---if pressed then rq.log("click at", x, y) end
---```
---@return integer x Cursor x, in game pixels.
---@return integer y Cursor y, in game pixels.
---@return boolean left The left button is down.
---@return boolean right The right button is down.
---@return boolean pressed The left button went down on this update.
---@return boolean released The left button came up on this update.
---@see rq.hud.clicked
function input.mouse() end

---Returns whether a key is down, by its raw SACT code.
---
---It reads the real key, also while `rq.hud.block_keys` hides it from the game. Get a code from a
---name with `rq.input.code`.
---
---Since: 0.2.0
---
---```lua
---if rq.input.key(rq.input.code("esc")) then close_menu() end
---```
---@param code integer A SACT key code (`VK_LBUTTON` is 1, Enter is 13).
---@return boolean down true while the key is down.
function input.key(code) end

---Returns the key code for a key name.
---
---The names are those `rqmod-ctl` uses, such as `"enter"`, `"esc"`, `"lmb"`, `"f3"` or `"a"`. A number
---is returned as it is.
---
---Raises an error for an unknown name.
---
---Since: 0.20.0
---@param key string|integer A key name, or a code.
---@return integer code The SACT key code.
function input.code(key) end

---Presses or lets go of a key for the game.
---
---The key stays down until you let go of it. The decide key is the left button or Enter, and a click
---lands when the button comes up, so hold it down for a few updates before letting go.
---
---Raises an error for an unknown key name.
---
---Since: 0.20.0
---
---```lua
---rq.input.set_key("enter", true)
----- a few updates later:
---rq.input.set_key("enter", false)
---```
---@param key string|integer A key name as for `rq.input.code`, or a code.
---@param down boolean true to press it, false to let go.
function input.set_key(key, down) end

---Moves the game's mouse.
---
---The cursor stays there and the real mouse is ignored until `rq.input.release`.
---
---Since: 0.20.0
---@param x integer Cursor x, in game pixels.
---@param y integer Cursor y, in game pixels.
---@see rq.input.release
function input.set_mouse(x, y) end

---Sends one wheel step to the game.
---
---`rq.input.scroll` counts it too.
---
---Since: 0.20.0
---@param dir integer Positive scrolls up, negative scrolls down.
function input.wheel(dir) end

---Gives the mouse back to the player.
---
---Ends `rq.input.set_mouse` and lets go of the mouse buttons. Keys pressed with `set_key` stay as
---they are.
---
---Since: 0.20.0
function input.release() end

---Returns the wheel steps since the last update.
---
---With `eat` true the game does not get them, as for a list that scrolls under the mouse. Steps sent
---with `rq.input.wheel` count too.
---
---Since: 1.7.0
---
---```lua
---if rq.hud.hit(list_x, list_y, list_w, list_h) then
---  top = top - rq.input.scroll(true)
---end
---```
---@param eat? boolean true to keep the steps from the game. Default false.
---@return integer steps Up is positive, down negative; 0 when the wheel did not move.
function input.scroll(eat) end


---Reads what is on the screen and saves screenshots.
---
---`rq.ui.get` returns the same data as `rqmod-ctl ui`. Raises an error at the top level of a mod.
---
---Since: 0.20.0
---@class rq.ui
local ui = {}

---Returns what is on the screen.
---
---The table holds `screen` (the screen's name), `hover`, `controls` (each with `id`, `x`, `y`, `w`,
---`h`, `enabled` and more) and the data of the current screen, such as `battle` or `quests`. A
---screen with no name, such as a black one during a change of scene, says `other`; `boot` is used
---only before the first named screen, and `title` only for the real title screen.
---
---It builds the whole list, so do not call it on every `rq.on_frame`.
---
---Since: 0.20.0
---Changed: 0.21.0 A screen with no name says `other`.
---
---```lua
---local s = rq.ui.get()
---if s and s.screen == "camp" then rq.log(#s.controls, "controls") end
---```
---@return table|nil screen The screen data, or nil before the game is ready.
---@see rq.ui.aim
function ui.get() end

---Returns the point a click on a control should land on.
---
---The point is inside the control and not covered by another control. It builds the list when
---nothing did during this update.
---
---Since: 0.20.0
---Changed: 0.21.0 Builds the list itself; before, it read the last `rq.ui.get`.
---@param id string A control id, as `rq.ui.get` or `rqmod-ctl ui` lists it.
---@return integer|nil x Click x in game pixels, or nil when the control is not on screen.
---@return integer|nil y Click y in game pixels.
function ui.aim(id) end

---Saves what the game draws as a PNG at the next drawn frame.
---
---The shot is taken later, so poll `rq.ui.shot_result` from `rq.on_frame` or a coroutine. The folders
---in the path must already exist.
---
---Raises an error before the game runs, when the path leaves the mod's folder, or while another shot
---is still waiting.
---
---Since: 0.21.0
---
---```lua
---rq.ui.shot("shots/camp.png")
---rq.on_frame(function()
---  local ok = rq.ui.shot_result()
---  if ok ~= nil then rq.log("shot saved:", ok) end
---end)
---```
---@param path string A path inside the mod's folder, in ASCII.
---@see rq.ui.shot_result
function ui.shot(path) end

---Returns how the last `rq.ui.shot` ended, once.
---
---Since: 0.21.0
---@return boolean|nil ok true or false once when the shot ended; nil while it waits, or when no result is left.
function ui.shot_result() end

---Draws text, boxes and CGs over the game, and reads clicks on them.
---
---The HUD is immediate mode: draw everything again on every `rq.on_frame`, and it shows for that
---frame. It is drawn on the game's own picture, after the game's scene, in game pixels. Every frame
---can hold up to 480 draw calls; past that, a call draws nothing.
---
---Since: 0.2.0
---
---```lua
---local function button(x, y, w, h, label)
---  if rq.hud.hit(x, y, w, h) then
---    rq.hud.rect(x, y, w, h, 80, 100, 160, 235)
---  else
---    rq.hud.rect(x, y, w, h, 28, 36, 64, 225)
---  end
---  rq.hud.text(x + 14, y + 6, label, { size = 18 })
---  return rq.hud.clicked(x, y, w, h)
---end
---```
---@class rq.hud
local hud = {}

---Returns the size of the game screen.
---
---Since: 0.2.0
---@return integer w Width in pixels, usually 1024.
---@return integer h Height in pixels, usually 768.
function hud.screen() end

---Draws a filled box.
---
---Since: 0.2.0
---@param x integer Left edge, in game pixels.
---@param y integer Top edge, in game pixels.
---@param w integer Width, in pixels.
---@param h integer Height, in pixels.
---@param r? integer Red, 0 to 255. Default 20.
---@param g? integer Green, 0 to 255. Default 20.
---@param b? integer Blue, 0 to 255. Default 30.
---@param a? integer Opacity, 0 to 255. Default 220.
function hud.rect(x, y, w, h, r, g, b, a) end

---How `rq.hud.text` draws text.
---@class rq.HudTextStyle
---@field size? number Font size, in pixels. Default 16.
---@field r? integer Red, 0 to 255. Default 255.
---@field g? integer Green, 0 to 255. Default 255.
---@field b? integer Blue, 0 to 255. Default 255.
---@field a? integer Opacity, 0 to 255. Default 255. Since 1.6.0.
---@field w? integer Width of the box `align` places the text in, in pixels. Since 1.6.0.
---@field h? integer Height of the box `valign` places the text in, in pixels. Since 1.6.0.
---@field align? "left"|"center"|"right" Where the text sits in the box `w` wide. Default "left". Since 1.6.0.
---@field valign? "top"|"center"|"bottom" Where the text sits in the box `h` high. Default "top". Since 1.6.0.
---@field edge? boolean false leaves out the dark edge, as on Config's tabs. Default true. Since 1.7.0.
---@field bold? boolean The heavier weight the game's own Config labels look like. Default false. Since 1.8.0.

---Draws a line of UTF-8 text.
---
---The text is white with a dark edge unless the style says otherwise. The font is VL PGothic, from
---the game's `xsystem4\fonts` folder, each letter at its own width. Only characters the game's font
---can draw show. With `w` and `align` the text is placed in the box `w` wide from `x`, and with `h`
---and `valign` in the box `h` high from `y`; text wider than the box goes past it on both sides.
---
---Raises an error for an unknown `align` or `valign`.
---
---Since: 0.2.0
---Changed: 1.8.0 The font is VL PGothic with letters at their own width (before, VL Gothic with half-width Latin letters).
---
---```lua
---rq.hud.text(0, 40, "Centered", { size = 20, w = 1024, align = "center" })
---```
---@param x integer Left edge, or of the box with `style.w`, in game pixels.
---@param y integer Top edge, or of the box with `style.h`, in game pixels.
---@param str string The text.
---@param style? rq.HudTextStyle Size, color and placement.
---@see rq.hud.text_width
function hud.text(x, y, str, style) end

---Returns how wide `rq.hud.text` draws a text.
---
---Only `size` and `bold` of a style count.
---
---Since: 0.2.0
---@param str string The text.
---@param size_or_style? number|rq.HudTextStyle A font size, or a style. Default size 16.
---@return integer px Width in pixels; 0 for an empty text.
function hud.text_width(str, size_or_style) end

---Breaks a text into lines that fit a width.
---
---Lines break at spaces when they can, else inside a word, and also at each line break in the text.
---
---Since: 0.2.0
---
---```lua
---for i, line in ipairs(rq.hud.wrap(desc, 300, 16)) do
---  rq.hud.text(20, 100 + (i - 1) * 20, line, { size = 16 })
---end
---```
---@param str string The text.
---@param max_px integer The widest a line may be, in pixels (at least 8).
---@param size? number The font size to measure with. Default 14.
---@return string[] lines The lines in order; empty for an empty text.
function hud.wrap(str, max_px, size) end

---How `rq.hud.cg` draws a CG.
---
---Since: 1.6.0
---@class rq.HudCgOpts
---@field src? integer[]|{x: integer, y: integer, w: integer, h: integer} The part of the CG to draw, `{x, y, w, h}` in the CG's pixels; cut at the CG's edge.
---@field w? integer Stretch what is drawn to this width, in pixels.
---@field h? integer Stretch what is drawn to this height, in pixels.
---@field alpha? integer Opacity, 0 to 255. Default 255.
---@field tint? integer[]|{r: integer, g: integer, b: integer} Multiplies the colors: `{128, 128, 128}` is half as bright.

---Draws a game CG, or a mod image.
---
---`cg_no` is a game CG number (item icons and the like) or a number from `rq.image.add`. Up to 160
---different CGs are kept for drawing; past that, a new one draws nothing.
---
---Raises an error for a `src` whose `w` or `h` is under 1.
---
---Since: 0.2.0
---Changed: 1.6.0 Added `opts`.
---
---```lua
---rq.hud.cg(100, 100, icon, { w = 32, h = 32, alpha = 180 })
---```
---@param x integer Left edge, in game pixels.
---@param y integer Top edge, in game pixels.
---@param cg_no integer A CG number.
---@param opts? rq.HudCgOpts The part, size, opacity and tint.
---@return integer w The CG's own width, whatever is drawn; 0 when there is no such CG.
---@return integer h The CG's own height; 0 when there is no such CG.
---@see rq.hud.cg_size
function hud.cg(x, y, cg_no, opts) end

---Returns a CG's size without drawing it.
---
---Since: 1.6.0
---@param cg_no integer A CG number.
---@return integer|nil w Width in pixels, or nil when there is no such CG.
---@return integer h Height in pixels.
function hud.cg_size(cg_no) end

---Shows what is drawn next only inside a box, as for a list that scrolls.
---
---The clip lasts until `rq.hud.clip()` with no arguments. A new clip replaces the box; it does not
---narrow it. Every frame starts with no clip.
---
---Since: 1.6.0
---@param x? integer Left edge, in game pixels. Leave all four out to end the clip.
---@param y? integer Top edge, in game pixels.
---@param w? integer Width, in pixels.
---@param h? integer Height, in pixels.
function hud.clip(x, y, w, h) end

---Returns whether the mouse is inside a box on this update.
---
---Since: 0.2.0
---@param x integer Left edge, in game pixels.
---@param y integer Top edge, in game pixels.
---@param w integer Width, in pixels.
---@param h integer Height, in pixels.
---@return boolean inside true while the cursor is in the box.
function hud.hit(x, y, w, h) end

---Returns whether the left button went down inside a box on this update.
---
---Since: 0.2.0
---@param x integer Left edge, in game pixels.
---@param y integer Top edge, in game pixels.
---@param w integer Width, in pixels.
---@param h integer Height, in pixels.
---@return boolean clicked true on the one update the button went down in the box.
---@see rq.hud.block_game_input
function hud.clicked(x, y, w, h) end

---Hides the mouse buttons from the game, so a click on the HUD does not also click the game.
---
---It lasts for this update and until the buttons come up. `rq.input.mouse` and the HUD calls still
---see the clicks.
---
---Since: 0.2.0
function hud.block_game_input() end

---Hides the keyboard from the game for this update.
---
---Call it on every update while a modal HUD is open. The mouse buttons are not affected (use
---`rq.hud.block_game_input`), and `rq.input.key` still reads the real keys.
---
---Since: 0.2.0
function hud.block_keys() end

---Starts reading typed text.
---
---It uses the system's text input, so input methods and keyboard layouts work. Only characters the
---game's font can draw are kept, and Backspace removes the last character. Read the text with
---`rq.hud.text_value` and end with `rq.hud.text_end`. Calling it again restarts with the new text.
---
---Since: 0.2.0
---
---```lua
---rq.hud.text_begin("Rance", 20)
---rq.on_frame(function()
---  local text, on = rq.hud.text_value()
---  if on then rq.hud.text(20, 20, text .. "_") end
---  if on and rq.input.key(rq.input.code("enter")) then rq.hud.text_end() end
---end)
---```
---@param initial? string The starting text, in UTF-8. Default "".
---@param max_bytes? integer The longest the text may be, in bytes, 1 to 255. Default 40.
---@see rq.hud.text_value
function hud.text_begin(initial, max_bytes) end

---Returns the typed text and whether reading is on.
---
---Since: 0.2.0
---@return string text The text so far, in UTF-8.
---@return boolean active true between `text_begin` and `text_end`.
function hud.text_value() end

---Stops reading typed text.
---
---The text stays readable with `rq.hud.text_value` until the next `text_begin`.
---
---Since: 0.2.0
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

---How `rq.agent.control` finds a control: a game button, or a rectangle a mod draws.
---@class rq.AgentOpts
---@field label? string The text `rqmod-ctl ui` shows for the control.
---@field button? integer The game's `T_Button` id. Leave it out for a HUD rectangle and give `x`, `y`, `w`, `h`.
---@field hoverOnly? boolean The control exists only while the cursor is already on its parent.
---@field x? integer Left edge of a HUD rectangle, in game pixels.
---@field y? integer Top edge of a HUD rectangle, in game pixels.
---@field w? integer Width of a HUD rectangle, in game pixels.
---@field h? integer Height of a HUD rectangle, in game pixels.

---A row on the F3 debug menu.
---@class rq.DebugAction
---@field id string A short token that names the row. "rendering-stats" belongs to the engine.
---@field label string The text on the menu.
---@field key string The key that runs it, as a pipe key name ("7", "f4", "space"). "6", "f3", "+", "-" and "m" are reserved.
---@field page? string The menu page. Default "Main".
---@field onPress fun() Runs when the key goes down while F3 is held.

---Adds rows to the engine's F3 debug menu.
---
---Hold F3 to show the menu. It redraws every frame and does not take normal play keys; F3 and the
---keys listed on it are swallowed only so the game does not also see them. Key 6 toggles Rendering
---Stats, which the engine owns. A menu holds up to 32 rows.
---
---While F3 is held, `rqmod-ctl ui` lists the rows under `debug.actions`, so a test can press them
---with `hold f3`, `key 7`, `release f3`.
---
---Since: 0.2.0
---@class rq.debug
local debug = {}

---Adds a row to the F3 menu, replaces it, or removes it.
---
---Pass a `rq.DebugAction` to add a row; a row with the same `id` is replaced. Pass just the id (a
---string, or `{ id = id }` without label, key or onPress) to remove the row; call this when a mod
---no longer needs it. Loading a mod again drops its old rows first.
---
---Raises an error for a missing label, key or onPress, an unknown or reserved key, an id the engine
---owns, a page name of 24 characters or more, or a 33rd row.
---
---Since: 0.2.0
---
---```lua
---rq.debug.action({
---  id = "my-toggle",
---  label = "My Toggle",
---  key = "7",
---  onPress = function() rq.log("my-toggle") end,
---})
---rq.debug.action("my-toggle")  -- remove it again
---```
---@param action rq.DebugAction|string The row, or the id of a row to remove.
---@return boolean ok true when the row was added, replaced or removed.
function debug.action(action) end

---Lets the test pipe (`rqmod-ctl`) see and press a mod's controls.
---
---Agents and test scripts drive the game through `rqmod-ctl` (`ui`, `hover`, `press`). The game's
---own buttons are listed automatically; a mod lists what it adds with these calls.
---
---Since: 0.2.0
---@class rq.agent
local agent = {}

---Registers a control under a stable id for `rqmod-ctl ui`, `hover` and `press`.
---
---With `opts.button`, the control is a game `T_Button`; it is listed only while that button is
---shown, and drops out when the game deletes it. With `x`, `y`, `w`, `h` it is a rectangle a mod
---draws with `rq.hud`: register it on every update it is on screen and remove it when it is not.
---`press` on a rectangle also sends Enter. Calling again with the same id updates the row. Pass no
---`opts` to remove the registration at once.
---
---Raises an error when the id or the button is rejected, or when `opts` has neither a button nor a
---full rectangle.
---
---Since: 0.2.0
---
---```lua
---rq.on_frame(function()
---  rq.hud.rect(20, 20, 120, 32, 40, 40, 40, 220)
---  rq.agent.control("my-button", { label = "My button", x = 20, y = 20, w = 120, h = 32 })
---end)
---```
---@param id string The pipe id: ASCII, no spaces.
---@param opts? rq.AgentOpts The button or rectangle; nil removes the registration.
---@return boolean|nil ok true when registered; nothing when removed.
function agent.control(id, opts) end

---A control a mod drew on this update, for `rqmod-ctl ui`.
---@class rq.AgentWidget
---@field id string ASCII, no spaces, under 40 characters.
---@field label? string The text the dump shows.
---@field type? string The kind of control. Default "button"; `rq.uikit` uses text, button, check, slider, tab and row.
---@field value? string|number|boolean The control's value, such as "on" for a checkbox.
---@field selected? boolean The control is the chosen one (a tab, a list row, a checked box).
---@field disabled? boolean The control does not take input.
---@field x integer Left edge, in game pixels.
---@field y integer Top edge, in game pixels.
---@field w integer Width, in game pixels.
---@field h integer Height, in game pixels.

---Lists the controls a mod's HUD drew on this update.
---
---They show in `rqmod-ctl ui` in the section `"kit"`, and `press` clicks the middle of a control's
---box. Each call replaces the previous list; `{}` clears it. `rq.uikit` lists its own widgets, so
---call this only for a HUD of your own.
---
---Since: 1.8.0
---@param list rq.AgentWidget[] Everything drawn on this update.
---@return integer listed How many controls were listed.
function agent.widgets(list) end

---One row of a character's skill list, from `rq.chara.slots`.
---@class rq.CharaSlot
---@field slot integer The row, 1 to 20.
---@field skill integer The skill number in the row; 0 when the row is empty.
---@field uses integer Uses left.
---@field max integer Most uses.
---@field bar integer The hotbar position, 1 to 6; 0 when the skill is not on the hotbar.

---What `rq.chara.on_offer` gets for the character whose buy list the game just filled.
---@class rq.OfferCtx
---@field iic integer The character's roster slot.
---@field no integer The character number (`Ｃ番号`).
---@field level integer The character's level.
---@field class integer The character's class (see `rq.CharaFields.class`).

---A row `rq.chara.on_offer` adds to the skill buy list.
---@class rq.Offer
---@field skill integer The skill number.
---@field cost integer The cost the game's price curve starts from.
---@field level integer The level needed to buy it; below it the row shows the game's "Cannot learn yet" text.
---@field points? integer The final price shown. It replaces the game's own price, its rise per use bought and its cap of 9. Since 0.13.0.

---A row of the skill buy list, from `rq.chara.offers`.
---@class rq.OfferRow
---@field index integer The row's index, as `rq.chara.set_offer` takes it.
---@field skill integer The skill number.
---@field points integer The price in skill points (`取得可能スキルポイント`).

---Reads and changes characters: their skills, stats, the skill buy list, and the roster.
---
---The calls work on a roster slot, the `iic` (the `id` of an entry in `rq.units()`), unless they say
---`who`: those take a character number or a mod's text id. Base-game numbers include Rance 1, Kaoru
---52, Onoha 62 and Rebecca 81.
---
---**Skills and the buy list.** `grant`, `rank`, `slots`, `set_slot`, `set_flavor`, `weapon_detail`,
---`apply_mod`, `on_recalc`, `on_offer`, `offers`, `set_offer`, `set_uses`, `set_bar`, `clear_slot` and
---`max_uses` need `rq.chara.install` first.
---
---**Roster.** `define` registers a template; the character exists, and joins the player's roster,
---only when `recruit` runs, so a mod can give it from a quest or an event. `recruit` also works for
---base-game characters. A text id such as `"mymod:alice"` becomes a number from 1000 up, the same
---number every time, so saves stay valid. The roster has 181 slots (20 to 200). The roster calls
---install what they need on first use.
---
---Since: 0.2.0
---Changed: 0.4.0 Added the roster calls.
---
---```lua
----- Create now, provide later
---rq.chara.define({ id = "mymod:alice", name = "Alice", class = 1, level = 5, slots = 4,
---  init = { atk = 10 }, skills = { { id = 100, n = 8 } } })
---if rq.store.get("alice_joined") ~= true then
---  local iic = rq.chara.recruit("mymod:alice", { state = 2 })
---  if iic then rq.store.set("alice_joined", true) end
---end
---
----- Edit a base-game character, also in old saves: Onoha gets Head Start 2 (skill 246)
---rq.chara.edit(62, { init = { eva = 50, spd = 25 }, learn = { { id = 246, n = 2 } } })
---```
---@class rq.chara
local chara = {}

---Installs the hooks the skill and buy-list calls need.
---
---It hooks the end of `Ｃ再計算` (the recalculation, after the game's own spear rewrite) and of
---`Ｔ購入スキル登録` (the buy list), and the combat sites `rq.combat` uses. Calling it again, or after
---`rq.combat.install`, does nothing more. Call it at the top of `init.lua`.
---
---Raises an error when the AIN is not loaded or a hook cannot be placed (the reason is in the log).
---
---Since: 0.2.0
---
---```lua
---rq.chara.install()
---```
---@return boolean ok Always true; a failure raises an error.
function chara.install() end

---Gives a character a skill (`ＣＳ追加`).
---
---Raises an error for a slot with no character or a skill number outside 1 to 1000.
---
---Since: 0.2.0
---@param iic integer The character's roster slot.
---@param skillId integer The skill number.
---@param n integer The rank for a knowledge skill (type 30), or the uses for an active skill (type 10).
---@return integer slot The skill-list row it went to.
function chara.grant(iic, skillId, n) end

---Returns a character's rank in a skill (`ＣＳ有無／ランク`).
---
---Since: 0.2.0
---@param iic integer The character's roster slot.
---@param skillId integer The skill number.
---@return integer rank 1 to 4 (no star through three stars); 0 when the character does not have the skill or there is no character.
function chara.rank(iic, skillId) end

---Lists the rows of a character's skill list that hold a skill.
---
---The game's screens show 6 hotbar positions, so a `slots` value above 6 adds rows but no positions.
---
---Since: 0.2.0
---@param iic integer The character's roster slot.
---@return rq.CharaSlot[] slots The rows in order; empty when there is no character.
function chara.slots(iic) end

---Writes the skill number of a skill-list row (`ＣＳ番号`).
---
---Only the number changes; the uses and the battle-log line stay. See `rq.chara.set_flavor`.
---
---Raises an error for a slot with no character or a skill number outside 0 to 1000.
---
---Since: 0.2.0
---@param iic integer The character's roster slot.
---@param slot integer The skill-list row.
---@param skillId integer The skill number; 0 empties the number.
function chara.set_slot(iic, slot, skillId) end

---Copies the battle-log line (`Ｓ表示文`) of a row's current skill to the character.
---
---The game's own slot swaps skip this, so call it after `rq.chara.set_slot`.
---
---Raises an error for a slot with no character or a row under 1.
---
---Since: 0.2.0
---@param iic integer The character's roster slot.
---@param slot integer The skill-list row.
function chara.set_flavor(iic, slot) end

---Returns the weapon kind of the character's equipped weapon (`Ｉ種類詳細`).
---
---Since: 0.2.0
---@param iic integer The character's roster slot.
---@return integer kind Sword 2, spear or naginata 7, glove 9; 0 when there is no character.
function chara.weapon_detail(iic) end

---Applies a stat change the way the game's own skills do (`Ｃ機能適応`).
---
---Call it from `rq.chara.on_recalc`, so it is applied on every recalculation.
---
---Since: 0.2.0
---@param iic integer The character's roster slot.
---@param kind integer The stat, as the game numbers it: accuracy 8, magic 14.
---@param amount integer How much to add.
---@param scale? integer The percent of `amount` applied. Default 100.
function chara.apply_mod(iic, kind, amount, scale) end

---Calls `fn` after the game recalculates a character (`Ｃ再計算`).
---
---Needs `rq.chara.install`. Up to 16 callbacks in all.
---
---Since: 0.2.0
---Changed: 0.14.0 Every mod's callback runs (before, the last one won).
---Changed: 0.23.0 Returns a remover.
---
---```lua
---rq.chara.on_recalc(function(iic)
---  if rq.chara.weapon_detail(iic) == 7 then rq.chara.apply_mod(iic, 8, 10) end  -- spear: accuracy
---end)
---```
---@param fn fun(iic: integer) Gets the character's roster slot.
---@return fun() remove Removes this callback.
function chara.on_recalc(fn) end

---Calls `fn` after the game filled a character's skill buy list, to add rows.
---
---`fn` returns nil, one `rq.Offer`, or a list of them. Each row goes through the game's own level
---check (`スキルレベル制限表示`). Needs `rq.chara.install`. Up to 16 callbacks in all.
---
---Since: 0.2.0
---Changed: 0.13.0 Every mod's callback runs (before, the last one won); a list adds several rows; added `points`.
---Changed: 0.23.0 Returns a remover.
---
---```lua
---rq.chara.on_offer(function(ctx)
---  if ctx.class == 4 then return { skill = 246, cost = 2, level = 10, points = 3 } end
---end)
---```
---@param fn fun(ctx: rq.OfferCtx): rq.Offer|rq.Offer[]|nil Returns the rows to add.
---@return fun() remove Removes this callback.
function chara.on_offer(fn) end

---Lists the rows of the skill buy list that hold a skill.
---
---With the paged buy list of `rq.skilllist` on, these are the rows its buttons read; otherwise the
---game's `取得可能スキル`.
---
---Since: 0.2.0
---@return rq.OfferRow[] rows The rows in order; empty when there is no buy list.
function chara.offers() end

---Replaces the skill in a buy-list row.
---
---It writes both the game's list and the paged list's copy. The row's level text stays as it is.
---
---Raises an error for an index outside the list, or for `points` when the game has no price list.
---
---Since: 0.2.0
---@param index integer The row's index, as `rq.chara.offers` gives it.
---@param skillId integer The skill number.
---@param points? integer The new price in skill points. Default: the price stays.
function chara.set_offer(index, skillId, points) end

---Sets the uses left and the most uses of a skill-list row.
---
---The most uses is written first; the game caps it at 99.
---
---Raises an error for a slot with no character.
---
---Since: 0.2.0
---@param iic integer The character's roster slot.
---@param slot integer The skill-list row.
---@param uses integer Uses left.
---@param max integer Most uses.
function chara.set_uses(iic, slot, uses, max) end

---Puts a skill-list row on the hotbar, or takes it off (`ＣＳ実装中`).
---
---Raises an error for a slot with no character.
---
---Since: 0.2.0
---@param iic integer The character's roster slot.
---@param slot integer The skill-list row.
---@param pos integer The hotbar position, 1 to 6; 0 takes it off.
function chara.set_bar(iic, slot, pos) end

---Empties a skill-list row (`ＣＳ初期化`).
---
---Raises an error for a slot with no character.
---
---Since: 0.2.0
---@param iic integer The character's roster slot.
---@param slot integer The skill-list row.
function chara.clear_slot(iic, slot) end

---Returns the most uses a character has of a skill (`ＣＳ有無／最大回数`).
---
---This is what the game's price (`スキルポン`) reads when it raises the cost of another use.
---
---Since: 0.2.0
---@param iic integer The character's roster slot.
---@param skillId integer The skill number.
---@return integer max The most uses; 0 when the character does not have the skill or there is no character.
function chara.max_uses(iic, skillId) end

---Stat bonuses that each recalculation adds, in `rq.CharaFields.init`.
---
---`hp`, `atk`, `def`, `mag` and `mdef` are percents (`atk = 10` is +10%); the rest are flat points.
---@class rq.CharaInit
---@field hp? integer Most HP, in percent.
---@field atk? integer Attack, in percent.
---@field def? integer Defense, in percent.
---@field mag? integer Magic, in percent.
---@field mdef? integer Magic defense, in percent.
---@field spd? integer Speed, in points.
---@field hit? integer Accuracy, in points.
---@field eva? integer Evasion, in points.
---@field crit? integer Critical rate, in points.
---@field impact? integer Impact rate, in points.
---@field resist? integer Resistance, in points.
---@field sp? integer Skill points the character starts with.

---A skill a character gets, in `rq.CharaFields.skills` and `rq.CharaEdit.learn`.
---@class rq.CharaSkillGrant
---@field id integer The skill number.
---@field n? integer The rank for a rank skill, or the uses for a use skill. Default 1.

---The fields `rq.chara.define` and `rq.chara.edit` share.
---@class rq.CharaFields
---@field name? string The full name.
---@field short? string The short name.
---@field face? integer|string A game face CG number, or an image path inside the mod's folder (shown at 196×232, proportions kept). Default 2916.
---@field face_name? string The face CG text (`顔ＣＧ文字列`).
---@field portrait? integer|string A standing-picture CG number, or an image path inside the mod's folder (on a 1024×768 canvas, proportions kept).
---@field class? integer 1 Fighter, 2 Guard, 3 Gunner, 4 Sorcerer, 5 Healer, 6 Ranger, 7 Brawler, 8 Civilian.
---@field sex? integer The sex, as the game numbers it.
---@field level? integer The level. `define` only.
---@field level_max? integer The highest level. `define` only.
---@field slots? integer Skill rows the character can equip. The hotbar has 6 positions.
---@field type? integer The game's type value (`タイプ`). Also `race`, `model`, `boss` and any other `rq.CharaState` name.
---@field init? rq.CharaInit Bonuses each recalculation adds.
---@field skills? rq.CharaSkillGrant[] Skills learned when the character is created. `define` only.
---@field weapon? integer|string An item number or text id, created and equipped when the character is created, before `on_create`. An item of the wrong kind is logged and not given. Since 0.15.0.
---@field armor? integer|string As `weapon`, for the armor. Since 0.15.0.
---@field item? integer|string As `weapon`, for the held item. Since 0.15.0.
---@field special? integer|string As `weapon`, for the option slot. Since 0.15.0.

---A character template for `rq.chara.define`.
---@class rq.CharaDef: rq.CharaFields
---@field id string The text id, such as "mymod:alice", under 64 bytes.
---@field lines? string[] Battle lines 1 to 6.
---@field on_create? fun(iic: integer) Runs when this character is created.

---A patch for `rq.chara.edit`. `level`, `level_max`, `skills` and `lines` do not apply.
---@class rq.CharaEdit: rq.CharaFields
---@field learn? rq.CharaSkillGrant[] Makes sure the character has these skills: raises a rank, never lowers it, and does not stack the uses of a use skill twice.
---@field on_recalc? fun(iic: integer): boolean|nil Runs after each recalculation of this character; return true to ask for one more.

---Options for `rq.chara.recruit`.
---@class rq.RecruitOpts
---@field state? integer Where the character goes (`登録状態`): 1 party, 2 reserve, 4 second string.
---@field party? integer A party slot (`ＰＴＡ登録`) to put the character in at once.

---What `rq.chara.on_create` gets.
---@class rq.CreateCtx
---@field iic integer The new character's roster slot.
---@field no integer The character number.
---@field id? string The text id, for a character a mod defined.

---What `rq.chara.on_forget` gets.
---@class rq.ForgetCtx
---@field iic integer The character's roster slot.
---@field skill integer The skill number.
---@field slot integer The skill-list row.
---@field rank integer The skill's rank.
---@field cost integer The skill points the character paid for it; 0 when the game did not record it.
---@field caller string The AIN function that removed it, or "lua" for `rq.chara.forget`.

---Registers a character template.
---
---Nothing joins the roster until `rq.chara.recruit`. Defining the same id again replaces the
---template. Up to 96 templates.
---
---Raises an error for a missing or too long id, an id whose number another id already has, or a
---97th template.
---
---Since: 0.4.0
---@param def rq.CharaDef The template.
---@return integer no The character number the id maps to, 1000 or more.
---@see rq.chara.recruit
function chara.define(def) end

---Creates a character in the roster (`Ｃ生成`), or finds the one already there.
---
---Works for a defined id and for a base-game number. `opts` applies also to a character already in
---the roster. The Party tab's list is refreshed.
---
---Raises an error for a number of 1000 or more that no template has.
---
---Since: 0.4.0
---Changed: 0.6.0 Refreshes the Party tab's list.
---@param who string|integer A text id or a character number.
---@param opts? rq.RecruitOpts Where the new character goes.
---@return integer|nil iic The roster slot, or nil when the game refused.
---@return string? err Why the game refused, such as a full roster.
function chara.recruit(who, opts) end

---Returns the roster slot of a character (`Ｃ番号取得`).
---
---Since: 0.4.0
---@param who string|integer A text id or a character number.
---@return integer|nil iic The roster slot, or nil when the character is not in the roster.
function chara.find(who) end

---Returns whether a character is in the roster.
---
---Since: 0.4.0
---@param who string|integer A text id or a character number.
---@return boolean recruited true when the character is in the roster.
function chara.is_recruited(who) end

---Recalculates a character now (`Ｃ再計算`).
---
---Pending `rq.chara.edit` patches and `init` changes show at once.
---
---Raises an error for a slot with no character.
---
---Since: 0.5.0
---@param iic integer The character's roster slot.
---@param heal? boolean true also refills HP and uses (`Ｃ補給`). Default false.
function chara.recalc(iic, heal) end

---Removes a character from the roster (`Ｃ削除`).
---
---The character leaves the party first. The Party tab's list is refreshed.
---
---Raises an error for an empty slot or a slot under 20.
---
---Since: 0.4.0
---Changed: 0.6.0 Refreshes the Party tab's list.
---@param iic integer The character's roster slot.
function chara.dismiss(iic) end

---Returns the character number in a roster slot.
---
---Since: 0.4.0
---@param iic integer The roster slot.
---@return integer|nil no The character number, or nil when the slot is empty.
function chara.number(iic) end

---Returns the text id of a defined character number.
---
---Since: 0.4.0
---@param no integer The character number.
---@return string|nil id The text id, or nil when no mod defined that number.
function chara.id_of(no) end

---Patches a character, again at the end of every recalculation, so it covers old saves too.
---
---Each mod keeps one patch per character; calling again replaces it. Every edited character is
---recalculated once right after a save loads. A patch made later, with the save already loaded,
---waits for the character's next recalculation; `rq.chara.recalc` applies it now. `weapon`, `armor`,
---`item` and `special` apply only when the character is created. Up to 192 patches.
---
---Since: 0.4.0
---Changed: 1.2.1 Edited characters are recalculated once after a save loads.
---
---```lua
---rq.chara.edit("mymod:alice", { init = { spd = 10 }, weakness_known = true })
---```
---@param who string|integer A text id or a character number (not a roster slot).
---@param patch rq.CharaEdit The fields to set.
function chara.edit(who, patch) end

---Everything `rq.chara.get` returns about a character. `rq.chara.set` takes the same names.
---
---Derived stats are rebuilt by each recalculation. `init_*` are the bonuses the recalculation adds;
---for the game's own characters it writes them again first, so use `rq.chara.edit` to keep a change.
---@class rq.CharaState
---@field iic integer The roster slot.
---@field id? string The text id, for a character a mod defined.
---@field name string The full name.
---@field short string The short name.
---@field face_name string The face CG text.
---@field no integer The character number.
---@field state integer Where the character is (`登録状態`): 1 party, 2 reserve, 4 second string.
---@field guest integer Non-zero for a guest.
---@field locked integer Non-zero when the character cannot be removed.
---@field face integer The face CG number.
---@field portrait integer The standing-picture CG number.
---@field class integer The class (see `rq.CharaFields.class`).
---@field sex integer The sex.
---@field level integer The level.
---@field level_max integer The highest level.
---@field exp integer Experience.
---@field exp_max integer Experience for the next level.
---@field rebirths integer Times reborn.
---@field hp integer HP.
---@field hp_max integer Most HP.
---@field spd integer Speed.
---@field atk integer Attack.
---@field def integer Defense.
---@field hit integer Accuracy.
---@field eva integer Evasion.
---@field crit integer Critical rate.
---@field impact integer Impact rate.
---@field resist integer Resistance.
---@field mag integer Magic.
---@field mdef integer Magic defense.
---@field fire_res integer Fire resistance.
---@field ice_res integer Ice resistance.
---@field thunder_res integer Thunder resistance.
---@field light_res integer Light resistance.
---@field dark_res integer Dark resistance.
---@field weak_element integer The weak element.
---@field weapon integer The equipped weapon's item instance.
---@field armor integer The equipped armor's item instance.
---@field item integer The held item's instance.
---@field special integer The option slot's item instance.
---@field skill_points integer Free skill points.
---@field race integer The race.
---@field type integer The type value.
---@field model integer The 3D model.
---@field boss integer Non-zero for a boss.
---@field weakness_known boolean The player knows the weakness.
---@field status integer Battle state: the status change, 1 poison, 0 none. Since 0.5.0.
---@field guard integer Battle state: guard rate. Since 0.5.0.
---@field stealth integer Battle state: stealth rate. Since 0.5.0.
---@field berserk integer Battle state: berserk. Since 0.5.0.
---@field bless_all integer Battle state: blessing on the whole party. Since 0.5.0.
---@field barrier integer Battle state: barrier. Since 0.5.0.
---@field atk_buff integer Battle state: attack buff. Since 0.5.0.
---@field def_buff integer Battle state: defense buff. Since 0.5.0.
---@field charge integer Battle state: charge. Since 0.5.0.
---@field init_hp integer Bonus to most HP, in percent.
---@field init_spd integer Bonus to speed, in points.
---@field init_atk integer Bonus to attack, in percent.
---@field init_def integer Bonus to defense, in percent.
---@field init_hit integer Bonus to accuracy, in points.
---@field init_eva integer Bonus to evasion, in points.
---@field init_crit integer Bonus to critical rate, in points.
---@field init_impact integer Bonus to impact rate, in points.
---@field init_resist integer Bonus to resistance, in points.
---@field init_mag integer Bonus to magic, in percent.
---@field init_mdef integer Bonus to magic defense, in percent.
---@field init_sp integer Starting skill points.

---Returns everything known about a character.
---
---Since: 0.4.0
---Changed: 0.5.0 Added the battle-state fields.
---@param iic integer The character's roster slot.
---@return rq.CharaState|nil state A copy of the character's fields, or nil when there is no character.
function chara.get(iic) end

---Writes character fields by the names `rq.chara.get` uses.
---
---`slots`, `name`, `short` and `face_name` work too, and `face` and `portrait` take an image path
---inside the mod's folder. A change to a derived stat (`hp_max`, `atk`, ...) lasts only until the
---next recalculation: use `init_*`, or `rq.chara.edit` to keep it.
---
---Raises an error for a slot with no character or an unknown field.
---
---Since: 0.4.0
---
---```lua
---rq.chara.set(iic, { hp = 1, weakness_known = true })
---```
---@param iic integer The character's roster slot.
---@param fields table Field names and new values.
function chara.set(iic, fields) end

---Returns a character's free skill points.
---
---Since: 0.4.0
---@param iic integer The character's roster slot.
---@return integer points The free skill points; 0 when there is no character.
function chara.skill_points(iic) end

---Adds to a character's free skill points.
---
---Raises an error for a slot with no character.
---
---Since: 0.4.0
---@param iic integer The character's roster slot.
---@param delta integer Points to add; negative takes away.
---@return integer total The new total, never below 0.
function chara.add_skill_points(iic, delta) end

---Removes a skill from a character (`ＣＳ削除`).
---
---`rq.chara.on_forget` callbacks run, with `caller` "lua".
---
---Raises an error for a slot with no character.
---
---Since: 0.4.0
---@param iic integer The character's roster slot.
---@param skill integer The skill number.
---@return boolean removed false when the character did not have the skill.
---@return integer? cost The skill points the character had paid for it.
function chara.forget(iic, skill) end

---Calls `fn` after any character is created (`Ｃ生成`), base game or mod.
---
---The engine then recalculates and heals the character. Up to 16 callbacks in all.
---
---Since: 0.4.0
---Changed: 0.23.0 Returns a remover.
---@param fn fun(ctx: rq.CreateCtx) Gets the new character.
---@return fun() remove Removes this callback.
function chara.on_create(fn) end

---Calls `fn` before a skill row is emptied, to give skill points back.
---
---The points every callback returns are added up and given to the character. Use `caller` to skip
---the removals the game makes inside its recalculation. Up to 16 callbacks in all.
---
---Since: 0.4.0
---Changed: 0.23.0 Returns a remover.
---
---```lua
---rq.chara.on_forget(function(ctx)
---  if ctx.caller:find("Ｃ再計算") then return 0 end
---  return ctx.cost > 0 and ctx.cost or 1
---end)
---```
---@param fn fun(ctx: rq.ForgetCtx): integer|nil Returns the points to give back.
---@return fun() remove Removes this callback.
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

---What `rq.combat.on_physical` gets for one physical hit.
---@class rq.PhysicalCtx
---@field attacker integer The attacker's roster slot.
---@field defender integer The defender's roster slot.
---@field skill integer The skill number.
---@field attack integer The attack before defense, as the callback before left it.
---@field magic integer The attacker's magic.
---@field race integer The defender's race; 8 is a hanny.
---@field defender_type integer The defender's type (`Ｃタイプ`); 22 is humanoid.

---What `rq.combat.on_accuracy` gets for one accuracy check.
---@class rq.AccuracyCtx
---@field attacker integer The attacker's roster slot.
---@field defender integer The defender's roster slot.
---@field skill integer The skill number.

---What `rq.combat.on_crit` gets where the game rolls a critical.
---@class rq.CritCtx
---@field attacker integer The attacker's roster slot.
---@field defender integer The defender's roster slot.
---@field skill integer The skill number.
---@field rate integer The critical chance the game is about to roll, in percent.
---@field defender_type integer The defender's type (`Ｃタイプ`); 22 is humanoid.

---Changes physical damage, accuracy and criticals.
---
---Physical attacks keep the game's own path: defense, variance, cap, then the usual multipliers. Lua
---can replace the attack before defense, scale the final damage, add to the hit chance at the same
---place as the game's own -5 and -10 for some skills, and force or deny a critical. The callbacks
---need `rq.combat.install` (or `rq.chara.install`), and hold up to 16 each.
---
---Since: 0.2.0
---
---```lua
---rq.combat.install()
---rq.combat.on_physical(function(ctx)
---  if ctx.race == 8 then return nil, 50 end  -- half damage to a hanny
---end)
---```
---@class rq.combat
local combat = {}

---Installs the hooks the combat callbacks need.
---
---It is the same call as `rq.chara.install`; calling either again does nothing more.
---
---Raises an error when the AIN is not loaded or a hook cannot be placed.
---
---Since: 0.2.0
---@return boolean ok Always true; a failure raises an error.
function combat.install() end

---Calls `fn` for each physical hit, to change the attack and the final damage.
---
---`fn` returns nothing to keep the game's numbers, a new attack before defense, or an attack (nil
---keeps it) and a percent of the final damage, applied after `最終ダメージ計算` (100 is unchanged, 50
---half). Each callback sees the attack the one before left, and the percents multiply.
---
---Since: 0.2.0
---Changed: 0.13.0 Every mod's callback runs (before, the last one won).
---Changed: 0.23.0 Returns a remover.
---@param fn fun(ctx: rq.PhysicalCtx): integer|nil, integer|nil Returns the attack, then the percent.
---@return fun() remove Removes this callback.
function combat.on_physical(fn) end

---Calls `fn` for each accuracy check, to add to the hit chance.
---
---The returned numbers of every callback add up; nil counts as 0.
---
---Since: 0.2.0
---Changed: 0.13.0 Every mod's callback runs and the numbers add up (before, the last one won).
---Changed: 0.23.0 Returns a remover.
---@param fn fun(ctx: rq.AccuracyCtx): integer|nil Returns points to add to the hit chance; negative lowers it.
---@return fun() remove Removes this callback.
function combat.on_accuracy(fn) end

---Calls `fn` where the game rolls a critical (the `確率演算` call in `クリティカル計算`).
---
---Return true to force the critical, false to deny it, nil to roll as the game does. The first
---callback that returns a boolean decides. A forced critical still adds the game's +30 to the damage
---multiplier, and it also happens for skills whose multiplier is above 150, which never roll one in
---the game.
---
---Since: 0.12.0
---Changed: 0.13.0 Every mod's callback runs until one decides.
---Changed: 0.23.0 Returns a remover.
---
---```lua
---rq.combat.on_crit(function(ctx)
---  if ctx.defender_type == 22 then return true end  -- humanoids always take a critical
---end)
---```
---@param fn fun(ctx: rq.CritCtx): boolean|nil Returns true, false, or nil to roll.
---@return fun() remove Removes this callback.
function combat.on_crit(fn) end

---What `rq.counter.on_guard_counter` gets.
---@class rq.CounterGuardCtx
---@field guarded boolean Ally Guard redirected this hit (the game's `FA[77]` is 1).
---@field defender integer The roster slot that took the hit: the guard when redirected.
---@field attacker integer The attacker's roster slot.
---@field skill integer The incoming skill number.
---@field rate integer The Counter rate before the game's guard check clears it.

---Lets the Counter skill (259) work after Ally Guard.
---
---The game sets the Counter rate to 0 inside `ダメージ後計算` when Ally Guard redirected the hit
---(`FA[77]`). `rq.counter.install` patches only that place; the other effects of `FA[77]` and the
---other Counter checks stay as the game has them. Lua decides with `on_guard_counter`.
---
---Since: 0.2.0
---
---```lua
---rq.counter.install()
---rq.counter.on_guard_counter(function(ctx)
---  return ctx.guarded == true
---end)
---```
---@class rq.counter
local counter = {}

---Installs the patch `on_guard_counter` needs.
---
---Raises an error when the AIN is not loaded or the patch cannot be placed.
---
---Since: 0.2.0
---@return boolean ok Always true; a failure raises an error.
function counter.install() end

---Calls `fn` when the game would clear the Counter rate after Ally Guard.
---
---One callback returning true keeps the rate. With no callback, the game's rule stays. Up to 16
---callbacks in all.
---
---Since: 0.2.0
---Changed: 0.14.0 Every mod's callback runs, and one true keeps the rate (before, the last one won).
---Changed: 0.23.0 Returns a remover.
---@param fn fun(ctx: rq.CounterGuardCtx): boolean|nil Returns true to keep the Counter rate.
---@return fun() remove Removes this callback.
function counter.on_guard_counter(fn) end

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

---Hooks into fights: events, ally skill select, marks and buffs on the cards, faces, states, drops,
---and scripted fights.
---
---Each event (`on_*`) installs its hook on first use; most hold up to 16 callbacks. The ally skill
---select calls need `rq.battle.install`, `set_flame` and `set_mark` need `rq.battle.install_flame`,
---and `set_state` needs `rq.battle.install_states`.
---
---A card goes by battle number: 1 to 5 are the ally ranks, 11 to 15 the enemy ranks. Characters are
---roster slots (`iic`); enemy slots are below 20.
---
---Since: 0.2.0
---
---```lua
---rq.battle.on_start(function(ctx)
---  for _, e in ipairs(ctx.enemies) do
---    rq.chara.set(e, { weakness_known = true })  -- reveal every enemy's weakness
---  end
---end)
---```
---@class rq.battle
local battle = {}

---The ally whose turn it is, from `rq.battle.actor`.
---@class rq.BattleActor
---@field no integer The battle number (`戦闘番号`).
---@field chara integer The roster slot.

---What `rq.battle.on_select_key` gets.
---@class rq.BattleSelectKeyCtx
---@field key integer The SACT key code (Space is 32).
---@field no integer The acting ally's battle number.
---@field chara integer The acting ally's roster slot.

---Installs the ally skill select hooks.
---
---It wraps `T_BattleSelect@select`, `@checkKey` and `@exec`, so a key that `on_select_key` claims
---completes the select like a click on that skill: the key is swallowed (Space does not also
---Cancel), and the skill runs on the actor itself.
---
---Raises an error when the AIN is not loaded or a hook cannot be placed.
---
---Since: 0.2.0
---@return boolean ok Always true; a failure raises an error.
function battle.install() end

---Calls `fn` for each key pressed while an ally chooses a skill.
---
---Return a skill number (or `{ skill = n }`) to choose that skill, or nil to let the game handle the
---key. Callbacks run in load order until one names a skill. Needs `rq.battle.install`.
---
---Since: 0.2.0
---Changed: 0.14.0 Every mod's callback runs until one names a skill (before, the last one won).
---Changed: 0.23.0 Returns a remover.
---
---```lua
---rq.battle.install()
---rq.battle.on_select_key(function(ctx)
---  if ctx.key ~= 32 then return nil end
---  local a = rq.battle.actor()
---  if a and rq.battle.skill_usable(a, 384) then return 384 end  -- Space: Standby
---end)
---```
---@param fn fun(ctx: rq.BattleSelectKeyCtx): integer|{skill: integer}|nil Returns the skill to choose.
---@return fun() remove Removes this callback.
function battle.on_select_key(fn) end

---What `rq.battle.on_start` gets.
---@class rq.BattleStartCtx
---@field boss boolean The fight is a boss fight.
---@field allies integer[] The allies' roster slots.
---@field enemies integer[] The enemies' slots, below 20.
---@field group integer The fight group (`◆戦闘グループ`). Since 0.9.0.
---@field rank integer The fight rank (`◆戦闘ランク`). Since 0.9.0.

---What `rq.battle.on_end` gets.
---@class rq.BattleEndCtx
---@field result integer The game's result code.
---@field allies integer[] The allies' roster slots.
---@field enemies integer[] The enemies' slots.
---@field group integer The fight group (`◆戦闘グループ`). Since 0.9.0.
---@field rank integer The fight rank (`◆戦闘ランク`). Since 0.9.0.

---Calls `fn` once per fight, at the end of `Ｔ戦闘前初期化`.
---
---Every start-of-fight effect of the game has already run (skills such as Cough, an ambush, poison
---tea), so `rq.chara.get` and `set` on the lists see the real state.
---
---Since: 0.5.0
---Changed: 0.23.0 Returns a remover.
---
---```lua
---rq.battle.on_start(function(ctx)
---  for _, a in ipairs(ctx.allies) do rq.chara.set(a, { status = 0 }) end  -- clear poison
---end)
---```
---@param fn fun(ctx: rq.BattleStartCtx) Gets the fight.
---@return fun() remove Removes this callback.
function battle.on_start(fn) end

---Calls `fn` at the start of `戦闘後処理`, before the game cleans up the fight.
---
---Set a drop here with `rq.battle.set_drop`.
---
---Since: 0.5.0
---Changed: 0.23.0 Returns a remover.
---@param fn fun(ctx: rq.BattleEndCtx) Gets the fight and its result.
---@return fun() remove Removes this callback.
function battle.on_end(fn) end

---What `rq.battle.on_support` gets.
---@class rq.BattleSupportCtx
---@field user integer The roster slot of who uses the skill.
---@field target integer The roster slot it is aimed at; the user for a self skill.
---@field skill integer The skill number.

---Calls `fn` at the start of every support (blue) skill action (`Ｔ補助行動`).
---
---It runs before the game applies its own effects. A skill a mod defined has no effect of its own,
---so this is where Lua gives it one.
---
---Since: 0.13.0
---Changed: 0.23.0 Returns a remover.
---
---```lua
---rq.battle.on_support(function(ctx)
---  if ctx.skill == MY_SKILL then rq.chara.set(ctx.target, { status = 0 }) end
---end)
---```
---@param fn fun(ctx: rq.BattleSupportCtx) Gets the action.
---@return fun() remove Removes this callback.
function battle.on_support(fn) end

---What `rq.battle.on_down` gets.
---@class rq.BattleDownCtx
---@field iic integer The character the game just set to downed, ally or enemy.

---Calls `fn` when the game sets a character to downed (`Ｃ状態変化` 3).
---
---Any cause counts: damage, poison, capture, instant death. It runs once per fall. A revive is not
---reported.
---
---Since: 0.12.0
---Changed: 0.23.0 Returns a remover.
---Changed: 1.3.1 Once per fall; before, it could run again for a character already down.
---@param fn fun(ctx: rq.BattleDownCtx) Gets the character.
---@return fun() remove Removes this callback.
function battle.on_down(fn) end

---What `rq.battle.on_switch` gets for one rank.
---@class rq.BattleSwitchCtx
---@field left integer The roster slot of who stood on the rank; 0 for nobody.
---@field came_in integer The roster slot of who stands there now; 0 for nobody.
---@field rank integer The ally rank, 1 to 5.

---Calls `fn` for each ally rank that holds someone else after a switch in a fight.
---
---The new cards are already on screen. Someone who only moved shows up as `left` on one rank and
---`came_in` on the other.
---
---Since: 0.29.0
---@param fn fun(ctx: rq.BattleSwitchCtx) Gets one rank.
---@return fun() remove Removes this callback.
function battle.on_switch(fn) end

---What `rq.battle.on_use` gets.
---@class rq.BattleUseEvent
---@field skill integer The skill number.
---@field user integer The roster slot of who uses it.
---@field unit integer The user's battle number.
---@field target integer The target's roster slot; 0 when there is none.
---@field target_unit integer The target's battle number.
---@field side "ally"|"enemy"|"summon" Whose skill it is.
---@field counter boolean true when the skill runs as a counter.

---Calls `fn` each time a skill is carried out, before its effects.
---
---It runs for allies, enemies and summons, once per hit of a multi-hit skill, and for a counter
---too. It only reports: it cannot change or stop the skill.
---
---Since: 0.25.0
---
---```lua
---rq.battle.on_use(function(ev)
---  if ev.side == "enemy" then rq.log("enemy used", ev.skill) end
---end)
---```
---@param fn fun(ev: rq.BattleUseEvent) Gets the action.
---@return fun() remove Removes this callback.
function battle.on_use(fn) end

---Installs the hooks the flame, the buffs and the marks need.
---
---It hooks the ally status refresh so `set_flame` sticks, and installs the fight start and end
---events, which clear the flames and buffs. Call it once at the top of `init.lua`.
---
---Raises an error when a hook cannot be placed.
---
---Since: 0.12.0
---@return boolean ok Always true; a failure raises an error.
function battle.install_flame() end

---Shows or hides the enemies' "charging" flame on an ally.
---
---The flame goes with the character when the party changes places in a switch, and ends when they
---leave the party. It is forgotten at the start and end of a fight. The ally's card shows one mark
---at a time, so the flame takes turns with Protect, Berserk, Stealth and Barrier. Attack and defense
---arrows are separate: `rq.chara.set(iic, { atk_buff = 1 })`.
---
---Raises an error before `rq.battle.install_flame`.
---
---Since: 0.12.0
---Changed: 0.29.0 The flame follows the character in a switch.
---@param iic integer The ally's roster slot.
---@param on boolean true shows it, false hides it.
---@return boolean ok false when the character is not in the party.
function battle.set_flame(iic, on) end

---A mod's buff on a card, for `rq.battle.set_buff` and `set_rank_buff`.
---
---The marks of a card (Protect, Berserk, Stealth, Barrier, Guard, the flame and mods' buffs) take
---turns in one spot, fading in and out, 1.5 seconds each; a single mark stays still.
---@class rq.BuffDef
---@field icon? integer A CG number, such as one from `rq.image.add` with `fit`, `w` and `h`; the spot holds about 56×56. A number that is no CG raises an error. Since 0.27.1 the icon is a game mark, so the game's windows cover it.
---@field anim? string An `rq.image.add_gif` key, played in the game's mark spot (about 35×26, from the top-left corner); wins over `icon`. Since 0.19.0.
---@field text? string A short text over the icon.
---@field order? integer The place in the turns, lowest first. Default 0, as the game's marks; equal ones keep the order they were added. Since 0.30.0.
---@field x? integer Pixels from the card's left edge to the icon's. Default 111 on an ally, 37 on an enemy. Since 0.30.0.
---@field y? integer Pixels from the card's top edge to the icon's. Default 80 on an ally, 22 on an enemy. Since 0.30.0.
---@field layer? integer The z from the card's base, -100 to 100, with the text one above. Default 10. The game's: on an ally 0 base, 1 text, face, skills and arrows, 40 dead, 50 no skill, 55 mask; on an enemy 0 base and name, 1 arrows and states, 2 bar, 15 second line. Since 0.30.0.
---@field pinned? boolean Stays shown, out of the turns; it hides with the card's marks. Since 0.30.0.

---How `rq.battle.set_mark` shows one of the game's marks.
---@class rq.MarkDef
---@field hide? boolean Takes the mark off the screen; the state itself stays.
---@field icon? integer A CG number shown instead of the game's picture.
---@field anim? string An `rq.image.add_gif` key shown instead of the game's picture; wins over `icon`.
---@field x? integer Pixels from the card's left edge. Default: where the game puts it.
---@field y? integer Pixels from the card's top edge. Default: where the game puts it.
---@field layer? integer As in `rq.BuffDef`. Marks 1 to 23 sit at layer 1, under an ally's face.
---@field order? integer Marks 31 to 36 only: as in `rq.BuffDef`.
---@field pinned? boolean Marks 31 to 36 only: as in `rq.BuffDef`.

---Every state of a character, from `rq.battle.states`.
---@class rq.BattleStates
---@field poison boolean Poisoned.
---@field stun boolean Stunned.
---@field atk_up boolean Attack ▲.
---@field atk_down boolean Attack ▼.
---@field def_up boolean Defense ▲.
---@field def_down boolean Defense ▼.
---@field dead boolean Down.

---One mark a card shows, from `rq.battle.buffs`.
---@class rq.BuffInfo
---@field mark? integer A game mark: 31 flame, 32 guard, 33 berserk, 34 stealth, 35 protect, 36 barrier.
---@field key? string A mod buff's key.
---@field icon? integer A mod buff's icon.
---@field rank? boolean true when the buff belongs to the rank. Since 0.29.0.
---@field pinned? boolean true when it stays shown, out of the turns. Since 0.30.0.
---@field text string The text shown with it.

---Adds, changes or removes a mod's buff on a character.
---
---The buff belongs to the character: it leaves the card with them, shows again when they come back,
---and can be changed or removed while they are out. On an enemy the spot is over the card's second
---line, and the game hides the marks of a dead enemy. Up to 8 per character, cleared at the start
---and end of a fight. Up to 128 different icons and GIFs, together.
---
---Raises an error for a def with neither icon nor anim, a number that is no CG, an unknown anim, an
---`x`, `y`, `layer` or `order` out of range or not whole, a too long key, or a 9th buff.
---
---Since: 0.16.0
---Changed: 0.28.0 Enemies too.
---Changed: 0.29.0 The buff follows the character.
---
---```lua
---local ICON = rq.image.add("shield", "shield.png", { fit = "contain", w = 56, h = 56 })
---rq.battle.set_buff(iic, "my-shield", { icon = ICON, text = "3" })
---rq.battle.set_buff(iic, "my-shield")  -- remove it
---```
---@param iic integer The character's roster slot.
---@param key string The buff's name, unique per character.
---@param def? rq.BuffDef The buff; nil removes it.
---@return boolean ok false when the character has not stood on a card in this fight.
---@see rq.battle.set_rank_buff
function battle.set_buff(iic, key, def) end

---Adds, changes or removes a buff that stays on a card, whoever stands there.
---
---It works as `rq.battle.set_buff`, and comes after the character's own buffs. An empty rank can
---hold one; it shows when someone comes in. Up to 8 per rank, cleared at the start and end of a
---fight.
---
---Since: 0.29.0
---@param rank integer The battle number: 1 to 5 for allies, 11 to 15 for enemies.
---@param key string The buff's name, unique per rank.
---@param def? rq.BuffDef The buff; nil removes it.
---@return boolean ok false for any other number.
function battle.set_rank_buff(rank, key, def) end

---Changes how one of the game's marks shows, until changed again, also in later fights.
---
---Without `who` it is for every card. `who = { iic = n }` is for one character, wherever they stand,
---and `who = { no = n }` for one card, whoever stands there. A character's def wins over its card's,
---which wins over the one for every card, and replaces it whole: an empty table `{}` for one
---character shows the game's own mark there while the other cards show a mod's icon. A hidden or
---moved mark keeps its place in the game's stack. Up to 64 defs for characters and cards together.
---Needs `rq.battle.install_flame`.
---
---Raises an error for a bad `who`, a card number that is not 1 to 5 or 11 to 15, a number that is
---no CG, an unknown anim, or a 65th def.
---
---Since: 0.30.0
---Changed: 1.5.0 Added `who`.
---
---```lua
---rq.battle.set_mark(21, { icon = POISON_ICON })   -- poison, on every card
---rq.battle.set_mark(21, {}, { iic = rq.chara.find(1) })  -- but the game's own on Rance
---```
---@param mark integer 1 attack ▲, 2 attack ▼, 11 defense ▲, 12 defense ▼, 21 poison, 22 stun, 23 near death, 31 flame, 32 guard, 33 berserk, 34 stealth, 35 protect, 36 barrier.
---@param def? rq.MarkDef How it shows; nil goes back to the game's (for that `who`).
---@param who? {iic?: integer, no?: integer} One character or one card.
---@return boolean ok false for any other mark number.
function battle.set_mark(mark, def, who) end

---Lets poison and stun be on one character at once, and ▲ and ▼ of one stat, until the game closes.
---
---Without it the game's own rule stays, where a new state replaces the old. With it: poison does its
---damage on the turn a character only recovers from stun; ▲ and ▼ both apply and cancel out;
---recovering from stun takes only stun, First Aid only poison, and a cure of ▼ or a removal of ▲ only
---its own side; Encourage, Self-Healing and Status Restore take both poison and stun, and death takes
---everything. Barrier still blocks poison, a stun still cancels a charge, no two stuns in a row.
---
---The second state of a stat is not in the save and is forgotten when the fight ends. `rq.chara`'s
---`status`, `atk_buff` and `def_buff` still show one value each, the one that came first, and stun
---over poison; the second state shows as a pinned mark, listed by `buffs`. Use
---`rq.battle.states` for all of them.
---
---Raises an error when a hook cannot be placed.
---
---Since: 0.31.0
---@return boolean ok Always true; a failure raises an error.
function battle.install_states() end

---Returns every state of a character, also without `install_states`.
---
---Since: 0.31.0
---@param iic integer The character's slot.
---@return rq.BattleStates|nil states nil for a number that is no character.
function battle.states(iic) end

---Adds or takes one state, by the rules of `install_states`, and the card shows it.
---
---Taking stun from someone also poisoned does the poison's damage, because it counts as the stun
---ending.
---
---Raises an error for an unknown state, or before `rq.battle.install_states`.
---
---Since: 0.31.0
---@param iic integer The character's slot.
---@param state "poison"|"stun"|"atk_up"|"atk_down"|"def_up"|"def_down" The state.
---@param on boolean true adds it, false takes it.
---@return boolean ok false for a number that is no character, or someone dead.
function battle.set_state(iic, state, on) end

---Lists the marks a card shows in turn.
---
---They come by `order`, then the pinned ones. For someone switched out it lists their own buffs that
---wait for their return. A game mark hidden by `set_mark` is not listed.
---
---Since: 0.16.0
---Changed: 0.28.0 Enemies too.
---@param iic integer The character's slot.
---@return rq.BuffInfo[] marks The marks in turn order.
function battle.buffs(iic) end

---Returns the item a won fight hands out (`◆戦闘後アイテム取得`).
---
---Read it inside `rq.battle.on_end`.
---
---Since: 0.9.0
---@return integer no The item number; 0 for none.
function battle.get_drop() end

---Sets the item a won fight hands out.
---
---Set it inside `rq.battle.on_end`. The game then skips its own drop rules and puts the item in quest
---storage, which goes to the vault after the result screen.
---
---Raises an error for a value that is not a number, a text id, nil or false.
---
---Since: 0.9.0
---
---```lua
---rq.battle.on_end(function(ctx)
---  if ctx.boss then rq.battle.set_drop("mymod:crown") end
---end)
---```
---@param who string|integer|nil|false An item number or text id; nil or false clears it.
function battle.set_drop(who) end

---Shows another face for an ally on the cards in a fight.
---
---Only the fight's cards change; the party bar, the status pages and the scenes keep the game's. The
---face lasts until changed, also in later fights. With `opts.ms` it is a face for a while, over the
---lasting one, and it ends with the fight; nil with `ms` shows the game's own for a while. The game's
---face sprite is drawn over, so the fight's moves, shakes, flashes and fades still show on it, and
---the dead mark still covers it. The game's own faces are CGs too (Rance 2000, Lia 2150). Up to 64
---characters.
---
---Raises an error for a character, CG or GIF that is not there, an `ms` of 0 or less, or a 65th
---character.
---
---Since: 1.12.0
---
---```lua
---local HURT = rq.image.add("hurt", "hurt.png")
---rq.battle.on_hp(function(ev)
---  if ev.iic == rq.chara.find("mymod:alice") then  -- half HP or less: another face
---    rq.battle.set_face(ev.iic, ev.hp > 0 and ev.hp * 2 <= ev.max and HURT or nil)
---  end
---end)
---```
---@param iic integer The ally's roster slot.
---@param face integer|string|nil A CG number, a GIF file in the mod's folder or an `rq.image.add_gif` key, stretched to 196×232; nil for the game's face.
---@param opts? {ms?: number, fps?: number, clock?: "game"|"real"} `ms`: how long, for a face for a while. `fps`: GIF speed, as on a sprite. `clock`: "game" (default) or "real" time.
---@return boolean ok true.
---@see rq.battle.get_face
function battle.set_face(iic, face, opts) end

---Returns what `rq.battle.set_face` set for a character.
---
---Since: 1.12.0
---@param iic integer The ally's roster slot.
---@return {face?: integer|string, temp?: integer|string, temp_ms?: integer, card?: integer, drawn: boolean}|nil face `face` the lasting face and `temp` the one for a while (a CG number or the GIF's name; left out for the game's), `temp_ms` the ms left, `card` the card (1 to 5) in a fight, `drawn` whether it was drawn in the last 250 ms; nil when nothing is set.
function battle.get_face(iic) end

---What `rq.battle.on_hp` gets.
---@class rq.HpEvent
---@field iic integer The character's slot.
---@field unit integer The card: 1 to 5 allies, 11 to 15 enemies.
---@field side "ally"|"enemy" Whose card it is.
---@field hp integer HP now.
---@field max integer Most HP.
---@field before integer HP when last seen; `hp` itself the first time.
---@field first boolean The first sight of this character on this card (the start of a fight, a switch).

---Calls `fn` in a fight each time a character's HP changes.
---
---Damage, healing, revival and a mod's `rq.chara.set` all count, for allies and enemies. It also runs
---once with `first = true` when a character is first seen on a card, so one callback can set the
---first face too. HP is read once a frame, so two changes in one frame come as one. Up to 64
---callbacks.
---
---Since: 1.12.0
---@param fn fun(ev: rq.HpEvent) Gets the change.
---@return fun() remove Removes this callback.
function battle.on_hp(fn) end

---Returns whether an ally is choosing a skill.
---
---It is true while `T_BattleSelect@select` is waiting. Needs `rq.battle.install`.
---
---Since: 0.2.0
---@return boolean selecting true during ally skill select.
function battle.in_ally_select() end

---Returns the ally choosing a skill.
---
---Needs `rq.battle.install`.
---
---Since: 0.2.0
---@return rq.BattleActor|nil actor The acting ally, or nil outside skill select.
function battle.actor() end

---Returns whether a character can use a skill now.
---
---The skill must be equipped, have uses left and not be locked. While skill select is open it
---answers like the skill's button: false when the button is greyed out, also for a front-row skill
---from the back.
---
---Since: 0.2.0
---Changed: 1.0.0 Answers like the button during skill select.
---@param actor rq.BattleActor|integer An actor table (`chara` or `no`), or a roster slot.
---@param skill_no integer The skill number.
---@return boolean usable true when the skill can be chosen.
function battle.skill_usable(actor, skill_no) end

---Opens a fight with the current party, using the game's scripted fight.
---
---It blocks until the fight and its result screens end, as `rq.scene` commands do, while
---`rq.on_frame` keeps running; call it from a coroutine if the same mod plays the fight. It counts as
---a fight of the current quest.
---
---Raises an error for no enemies, an unknown monster, an empty party, or during a fight.
---
---Since: 0.22.0
---
---```lua
---coroutine.wrap(function()
---  local result = rq.battle.start({ enemies = { 0, boss_no, 0 } })
---  rq.log("fight:", result)
---end)()
---```
---@param opts {enemies: integer[], exp?: integer} `enemies`: up to five monster numbers in rank order, 0 for an empty rank. `exp`: experience for the win. Default 0.
---@return "won"|"lost"|"other" result How the fight ended.
function battle.start(opts) end

---Decides every accuracy check, of allies and enemies, until `force_hit(nil)`.
---
---The rest of the check runs as usual, so a miss still shows in the log, and a hit can still do 0
---damage. The game keeps the last result in the global `◆攻撃外れた` (1 is a miss).
---
---Raises an error for any other mode.
---
---Since: 0.22.0
---@param mode "hit"|"miss"|nil The result every check gives; nil goes back to rolling.
---@see rq.random.fix
function battle.force_hit(mode) end

---Returns the actions left in this fight, the number on the TIME REMAINING panel.
---
---Since: 0.26.0
---@return integer n The actions left; 999 means no limit, 0 outside a fight.
function battle.time() end

---Sets the actions left in this fight.
---
---The game sets the count again when a fight opens, so a value set outside one is lost.
---
---Raises an error for a number that is not whole or not 0 to 999.
---
---Since: 0.26.0
---@param n integer The actions left, 0 to 999; 999 means no limit.
function battle.set_time(n) end

---Returns a copy of what the fight screen shows.
---
---Since: 0.2.0
---@return rq.BattleSnap snap The allies, enemies, the actor's skills and the log.
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

---What `rq.battle.snapshot` returns: a copy of the fight screen.
---@class rq.BattleSnap
---@field up boolean Ally skill select is open.
---@field allies {id: integer, no: integer, name: string}[] Each ally: `id` the roster slot, `no` the battle number, `name`.
---@field enemies {id: integer, no: integer, name: string}[] Each enemy, as for `allies`.
---@field skills {id: integer, name: string, uses: integer, enabled: boolean}[] The acting ally's skills: number, name, uses left, and whether the button can be clicked.
---@field log string[] The battle log lines, oldest first.

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

---Lists the recruited characters.
---
---`rq.units()` and `rq.units.list()` are the same call. The list is a copy: changing it does not
---change the save. Right after boot it holds only the three default characters the game makes
---before a save loads; wait until it lists more than 10, or use `rq.saves`.
---
---Since: 0.2.0
---
---```lua
---for _, u in ipairs(rq.units()) do
---  rq.log(u.id, u.name, u.level)
---end
---```
---@class rq.units
---@overload fun(): rq.Unit[]
local units = {}

---Returns the recruited characters, as `rq.units()` does.
---
---Since: 0.2.0
---@return rq.Unit[] units A copy of each recruited character.
function units.list() end

---Lists the item instances the player owns.
---
---`rq.items()` and `rq.items.list()` are the same call. Instances whose existence value is 0, 6 or 9
---are left out. The list is a copy.
---
---Since: 0.2.0
---@class rq.items
---@overload fun(): rq.Item[]
local items = {}

---Returns the owned item instances, as `rq.items()` does.
---
---Since: 0.2.0
---@return rq.Item[] items A copy of each owned item.
function items.list() end

---Reads the castle: whether it is open, its rooms and who is in them.
---
---`rq.castle()` and `rq.castle.get()` are the same call.
---
---Since: 0.2.0
---@class rq.castle
---@overload fun(): rq.Castle
local castle = {}

---Returns the castle's state, as `rq.castle()` does.
---
---Since: 0.2.0
---@return rq.Castle castle A copy of the castle's state.
function castle.get() end

---The global table every mod uses to reach the game.
---
---The engine builds `rq` before any mod runs. Mods load before the game starts, so at the top level
---of `init.lua` register callbacks and definitions (`rq.on_frame`, `on_*`, `define`, `edit`,
---`patch`, `rq.image.add`). Calls that read or change game state, such as `rq.skill.get`,
---`rq.chara.get`, `rq.item.give`, `rq.party.*` or `rq.hud.text_width`, raise an error there: make
---them from `rq.on_frame` or a callback. The queries `rq.units()`, `rq.items()`, `rq.quests()` and
---`rq.castle()` return empty results at that point.
---
---Every `on_*` call, `rq.on_frame` and `rq.ain.hook` return a function that removes the callback.
---It works at once, also from inside the callback, and calling it twice is harmless. Definitions
---(`define`, `edit`) are not callbacks and have no remover.
---
---```lua
---local remove
---remove = rq.battle.on_end(function(ev)
---  rq.log("the first fight ended")
---  remove()
---end)
---```
---@class rq
---@field version string The loader version, such as "1.14.1". A mod whose `mod.json` asks for a newer `loader` is not loaded.
---@field game_dir string The game folder (where the AIN is).
---@field mods_dir string The `mods` folder, ending in a backslash.
---@field units rq.units
---@field items rq.items
---@field castle rq.castle
---@field quests rq.QuestsApi
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

---The loader version.
---
---Since: 0.2.0
---@type string
rq.version = "1.14.1"

rq.units = units
rq.items = items
rq.castle = castle

---Writes a line to the console and to the log.
---
---The line goes to `logs\rqmod.log` (the `logs` folder beside `mods`) and, when `config.json` has
---`"terminal": true`, to the console window. It starts with the name of the mod and file that
---called it, such as `[loot-preview] Detected 2 items`. Arguments are turned into text as `tostring`
---does and joined with tabs, as `print` does.
---
---Since: 0.2.0
---Changed: 1.1.0 The log moved from `mods` to the `logs` folder.
---@param ... any The values to write.
function rq.log(...) end

---Calls `fn` once on every game update.
---
---Draw with `rq.hud.*` here, and check input or game state. Every mod may register callbacks; they
---run in load order. An update is not a drawn frame: on a screen that does not redraw it can run
---thousands of times per second, so use `rq.time_ms` for anything that must last a real time. An
---error in `fn` is logged once for each new error text, not on every update.
---
---Since: 0.2.0
---Changed: 0.3.0 Every mod keeps its own callback (before, the last mod replaced the others).
---
---```lua
---rq.on_frame(function()
---  rq.hud.text(10, 10, "Hello from my mod")
---end)
---```
---@param fn fun() Runs on every update.
---@return fun() remove Removes this callback.
function rq.on_frame(fn) end

---Calls `fn` after a load or after going to the title, once the game runs again.
---
---Use it to clear a mod's own flags, such as "a scene is playing". These are the same callbacks as
---`rq.sprite.on_reset`, and they run after every mod sprite is gone. On the title, every Lua call
---that was waiting in the game (a scene, a fight from `rq.battle.start`) has already ended with the
---error "rq: the game went to the title".
---
---Since: 1.13.0
---@param fn fun(why: "load"|"title") `why` says which one happened.
---@return fun() remove Removes this callback.
function rq.on_reset(fn) end

---Returns the time since the game started, in real milliseconds.
---
---It keeps counting at real speed whatever `rq.speed` is set to.
---
---Since: 0.20.0
---@return integer ms Milliseconds since the game started.
function rq.time_ms() end

---Returns a stack trace, of the current code or of a coroutine.
---
---The sandbox has no `debug` library; this gives what `debug.traceback` would.
---
---Since: 0.20.0
---@param co? thread|string The coroutine to trace, or the message when no coroutine is given.
---@param msg? string A message put before the trace.
---@return string trace The stack trace.
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
---Reads and changes who is in the party.
---
---The party has five ranks. Each holds a character id (the `id` of an entry in `rq.units()`) or 0
---when it is empty.
---
---Since: 0.22.0
---@class rq.party
local party = {}

---Returns the character id on each of the five ranks.
---
---Since: 0.22.0
---
---```lua
---for rank, id in ipairs(rq.party.get()) do
---  if id ~= 0 then rq.log(rank, id) end
---end
---```
---@return integer[] ranks Five ids in rank order; 0 for an empty rank.
---@see rq.party.set
function party.get() end

---Replaces the whole party.
---
---Raises an error, and changes nothing, during a battle, for a character that is not recruited,
---for a character listed twice, or for more than five ranks. A screen that is already open, such
---as the Party tab, does not redraw until it is opened again.
---
---Since: 0.22.0
---
---```lua
---local ranks = rq.party.get()
---ranks[1], ranks[2] = ranks[2], ranks[1]   -- swap the first two ranks
---rq.party.set(ranks)
---```
---@param ranks (integer|false)[] Up to five character ids in rank order; 0 or false leaves a rank empty.
---@see rq.party.get
function party.set(ranks) end

rq.party = party

---Controls the random number generator the game and the loader share.
---
---Every roll of the game and of the loader, such as opening a chest or `rq.item` loot, comes from
---this one generator. To force the result of one kind of check, a hook is more exact: see
---`rq.battle.force_hit`.
---
---Since: 0.22.0
---@class rq.random
local random = {}

---Starts the generator from a seed.
---
---The same seed gives the same numbers, but a fight does not replay the same way, because effects
---on screen draw from the same generator.
---
---Since: 0.22.0
---@param n integer The seed.
---@see rq.random.fix
function random.seed(n) end

---Makes every roll give the same value, or goes back to random rolls.
---
---The game rolls with `RAND(n)`, which becomes `value % n + 1`: `fix(0)` makes every roll 1 and
---`fix(99)` makes `RAND(100)` give 100. It is blunt: `fix(0)` also fires every check written as
---`RAND(5) == 1`, good or bad, so it does not guarantee a hit. Use it only for a short time; a
---check that rolls until it gets a new number would never end.
---
---Since: 0.22.0
---
---```lua
---rq.random.fix(0)    -- every roll is 1 for now
---rq.random.fix(nil)  -- random again
---```
---@param value integer|nil The value every roll uses, or nil for random rolls.
---@see rq.battle.force_hit
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
