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

---Reaches into the game's own code (the AIN): call, hook and patch its functions, and read and write
---its globals.
---
---Names are the game's own, in UTF-8, such as `Ｔ戦闘前初期化` or `T_BattleSelect@select`; find them
---with `rq.ain.find`. They can change with the game's data, so a mod that patches by number should
---check the count it gets back.
---
---Since: 0.2.0
---@class rq.ain
local ain = {}

---Replaces float constants in one AIN function.
---
---Since: 0.2.0
---Changed: 1.3.0 Reaches the game's code of a function the loader wraps (such as `Ｔ戦闘前初期化`); before, it patched only the wrapper and found nothing.
---@param name string The function's name.
---@param from number The value to find.
---@param to number The new value.
---@param n? integer Which occurrence, from 0. Default: all of them.
---@return integer count How many constants were replaced.
function ain.patch_float(name, from, to, n) end

---Replaces integer constants in one AIN function, such as a hard-coded skill number.
---
---Occurrences count every constant of that value in the function, so they can differ between
---versions of the game's AIN; to find one constant by what it follows, use `patch_int_after`.
---
---Since: 0.17.0
---Changed: 1.3.0 Reaches the game's code of a function the loader wraps.
---@param name string The function's name.
---@param from integer The value to find.
---@param to integer The new value.
---@param n? integer Which occurrence, from 0. Default: all of them.
---@return integer count How many constants were replaced.
---@see rq.ain.patch_int_after
function ain.patch_int(name, from, to, n) end

---Replaces the first integer constant `from` that follows a read of a global.
---
---It looks at the `window` instructions after the read, as for the 2 in
---`if (◆２倍モード == 1) { x = x * 2; }`. A variable's index is never taken for the constant.
---
---Raises an error for an unknown function or global.
---
---Since: 1.3.0
---@param name string The function's name.
---@param global string The global's name.
---@param from integer The value to find.
---@param to integer The new value.
---@param nth_ref? integer Which read of the global in the function, from 0. Default: after every read.
---@param window? integer How many instructions to look at after the read. Default 8.
---@return integer count How many constants were replaced.
function ain.patch_int_after(name, global, from, to, nth_ref, window) end

---Calls any AIN function by its name.
---
---The arguments must be int, bool, float or string, and all of them are needed. A scene command
---blocks until it ends, so call it from a callback the game runs, not at the top level of a mod. A
---function that reaches a save under this call gets the game's own "save failed" message and nothing
---is written; one that reaches a load closes the game with an error. A bad index passed to a game
---function is a fatal error in the game, and this call cannot check it.
---
---Raises an error for an unknown name, a wrong number of arguments, a reference or struct argument, a
---method (`Class@method`, which needs an object), or `SYS_セーブ実行` and `SYS_ロード実行`.
---
---Since: 0.18.0
---Changed: 0.26.0 Refuses `SYS_セーブ実行` and `SYS_ロード実行`.
---Changed: 0.27.0 A save under the call fails safely.
---
---```lua
---local name = rq.ain.call("クエスト_名前", 126)
---```
---@param name string The function's name.
---@param ... integer|boolean|number|string The arguments.
---@return any result What the function returns; nil for none or a struct.
function ain.call(name, ...) end

---What an `rq.ain.hook` callback gets.
---@class rq.AinHookCtx
---@field name string The function's name.
---@field args table The arguments by position; change one to change what the function gets. References and structs are nil.
---@field skip? boolean `before` only: true keeps the function from running.
---@field result? any In `before`: what a skipped function returns. In `after`: the return value, which may be changed.

---Runs Lua before and after any AIN function.
---
---A method is named `Class@method`. Many hooks may sit on one function. The function does not fire
---its hooks again while one of its callbacks runs. A table with neither side does nothing.
---
---Since: 0.18.0
---Changed: 0.23.0 Returns a remover.
---
---```lua
---rq.ain.hook("message", { before = function(h)
---  h.args[3] = h.args[3]:gsub("Rance", "Ranse")  -- rewrite every line of text
---end })
---```
---@param name string The function's name.
---@param opts {before?: fun(h: rq.AinHookCtx), after?: fun(h: rq.AinHookCtx)} The callbacks.
---@return fun() remove Removes both sides of this hook.
function ain.hook(name, opts) end

---Calls a method of the game on the object a global reaches.
---
---`path` is a global's name, then indexes and field names as `rq.ain.global` takes them, and it must
---end on a struct: `{"g_sSystemMenu"}`, `{"g_sCastle", "m_sList"}`. The method's class must be that
---struct's (`T_SystemMenu@createButton` on a `T_SystemMenu`). The arguments are as `rq.ain.call`'s:
---int, bool, float or string, all of them. The same cautions hold: a method that draws or waits
---runs inside this call, and a bad value passed to the game can be a fatal error there.
---
---Raises an error for a path that is not a table, does not start with a known global or does not
---reach a struct, an unknown function, a function that is not a method or not the struct's, a wrong
---number of arguments, or a reference or struct argument.
---
---Since: 2.16.0
---
---```lua
---local rows = rq.ain.method({ "g_sSystemMenu", "m_sButton" }, "T_ButtonGroup@getButtonCount")
---```
---@param path (string|integer)[] The global's name, then indexes and field names to the object.
---@param name string The method, `Class@method`.
---@param ... integer|boolean|number|string The arguments.
---@return any result What the method returns; nil for none or a struct.
---@see rq.ain.call
function ain.method(path, name, ...) end

---Reads a game global that holds an int, bool, float or string.
---
---Give the game's own index for an array element, and a field name to go into a struct, mixed as the
---game does.
---
---Raises an error for an unknown name or field, an index out of range, a step that does not fit the
---value, a whole array or a struct.
---
---Since: 0.22.0
---Changed: 0.24.0 Field names reach into structs.
---
---```lua
---local fa = rq.ain.global("FA", 87)          -- FA[87]
---local name = rq.ain.global("Ｑ", 126, "名前")  -- Ｑ[126].名前
---```
---@param name string The global's name.
---@param ... integer|string Indexes and field names.
---@return integer|boolean|number|string value The value.
function ain.global(name, ...) end

---Writes a game global, as `rq.ain.global` reads it: `set_global(name, [step, ...,] value)`.
---
---It is the game's own state and goes into the next save, so restore what a test changes.
---
---Raises an error as `rq.ain.global` does.
---
---Since: 0.22.0
---@param name string The global's name.
---@param ... any Indexes and field names, then the value.
function ain.set_global(name, ...) end

---Returns the size of an array global.
---
---Many game arrays start at 1 and leave 0 unused, so the size can be one more than the entries.
---
---Since: 0.22.0
---@param name string The global's name.
---@param ... integer|string Indexes and field names to reach an inner array.
---@return integer size The number of elements.
function ain.global_len(name, ...) end

---Makes an array global longer or shorter, as the game's own `Realloc` does.
---
---Values that fit are kept; new elements are 0, false, an empty string or an empty struct. Only an
---array of one dimension. The array is the game's own state and goes into the next save at its new
---size, and a save made before brings back the old size, so grow it again where it is used (for
---example in a `before` hook). It is there only once the game runs, not while `init.lua` loads.
---
---Raises an error for an unknown name, a value that is not an array, more than one dimension, or a
---size below 0 or above 65536.
---
---Since: 2.18.0
---
---```lua
---if rq.ain.global_len("取得可能スキル") < 145 then rq.ain.set_global_len("取得可能スキル", 145) end
---```
---@param name string The global's name.
---@param ... integer|string Indexes and field names to reach an inner array, then the new size.
function ain.set_global_len(name, ...) end

---Removes the game's message boxes whose text contains `needle`.
---
---Every `system.MsgBox` call that shows a string holding `needle` is taken out of the code.
---
---Raises an error when the AIN is not loaded.
---
---Since: 0.2.0
---@param needle string Part of the message, in UTF-8.
---@return integer sites How many calls were taken out.
function ain.skip_msgbox(needle) end

---Swallows the engine's own error boxes for the TADA and HTTP network checks (`getserverversion`).
---
---Since: 1.0.0
function ain.skip_sdl_msgbox() end

---Writes the AIN functions whose name contains `needle` to the log.
---
---Each line is `find '<needle>' fno=<number> <name>`, and the name is what `rq.ain.call` and
---`rq.ain.hook` take (a method as `Class@method`). The match is on the game's own spelling, so
---case and full-width letters count. At most 40 are listed; a longer needle narrows the list.
---
---Raises an error when the AIN is not loaded.
---
---Since: 0.2.0
---Changed: 1.16.1 Each line also gives the function's name.
---
---```lua
---rq.ain.find("Encount")   -- [rqmod] find 'Encount' fno=... T_Player@checkEncount, ...
---```
---@param needle string Part of a name, in UTF-8.
---@return integer count How many were listed.
function ain.find(needle) end

---The camera range `rq.dungeon.camera` patches.
---@class rq.DungeonCamera
---@field lock? number The distance the camera is locked at on a first visit. Default 35.
---@field min? number The nearest zoom. Default 17.5.
---@field max? number The farthest zoom. Default 120.
---@field fog_far? number Where the fog ends. Default 160.
---@field slider_steps? integer Steps on the zoom slider. Default 25.

---Reads and changes the dungeon: the map, the player's place, objects on it, and the camera.
---
---On a map grid, `x` runs east and `z` north.
---
---Since: 0.2.0
---@class rq.dungeon
local dungeon = {}

---Patches the dungeon camera's zoom range, fog and slider in memory.
---
---Raises an error when `opts` is not a table or the AIN is not loaded.
---
---Since: 0.2.0
---
---```lua
---rq.dungeon.camera({ max = 160, fog_far = 220 })
---```
---@param opts rq.DungeonCamera The range; a field left out keeps its default.
---@return boolean ok false when the game lacks one of the camera functions (the log says which).
function dungeon.camera(opts) end

---Returns whether the dungeon map screen exists.
---
---It is true between `T_MapParts@create` and `@delete`, so also in a fight, a scene or a menu on the
---map. For "the player walks the map", use `rq.dungeon.exploring`.
---
---Since: 0.2.0
---@return boolean active true while the map screen exists.
function dungeon.active() end

---Returns whether the player walks the map.
---
---False when the map is hidden or suspended (a join dialog), and during a fight, a character profile,
---Config, the result and appraisal screens, and a scene or choice on the map.
---
---Since: 0.2.0
---@return boolean exploring true while the player can walk.
function dungeon.exploring() end

---Returns a copy of the current room.
---
---Since: 0.2.0
---@return rq.DungeonInfo info The room, the cell and the party.
function dungeon.info() end

---What `rq.dungeon.on_map` gets.
---@class rq.MapCtx
---@field quest integer The quest being entered (its `Ｑ` index).
---@field cleared boolean The quest was cleared before.
---@field map integer The map number.
---@field party integer[] The party's roster slots.

---Calls `fn` when the dungeon map screen is built.
---
---On a first visit the game locks the camera at distance 35 until the quest is cleared. Return true
---from any callback and the engine reports the quest as cleared for that one check, so the zoom stays
---free. The range is whatever is patched at that moment (the game's, or `rq.dungeon.camera`).
---
---Since: 0.9.0
---Changed: 0.23.0 Returns a remover.
---
---```lua
---rq.dungeon.on_map(function(ctx)
---  return ctx.quest == 126  -- free zoom on quest 126, also the first time
---end)
---```
---@param fn fun(ctx: rq.MapCtx): boolean|nil Returns true to free the zoom.
---@return fun() remove Removes this callback.
function dungeon.on_map(fn) end

---Returns the current map, as `rqmod-ctl grid` shows it.
---
---The table holds `player` `{x, z}`, `mapNo`, `room` `{minX, minZ, maxX, maxZ, no, whole}`, `rows`
---(text, north to south), `legend`, `axes` and `events`, each `{x, z, kind, tt, tz, tv, reach}`. By
---default it holds only what the player reaches over plain floor, so a trap, chest or fight across a
---corridor hides the rest of the room; `opts.whole` holds every cell, with floor the player cannot
---reach shown as `:`. `room.no` is the game's room number (`◆ＢＭＡＰ`). After a portal, `room.no`
---changes a moment before the map: wait for `mapNo` to change before reading the new room.
---
---Since: 0.25.0
---Changed: 1.5.0 Added `opts.whole`, `reach`, `room.no` and `room.whole`.
---@param opts? {whole?: boolean} `whole`: every cell of the map. Default false.
---@return table|nil grid The map, or nil when not on a map.
---@return string? reason Why there is no map.
function dungeon.grid(opts) end

---Puts the player on a floor cell of the current map at once, as the game does after a portal.
---
---Any floor cell works, also one the player cannot walk to. `grid().player` shows the new cell after
---the game's next update. Nothing on the cell opens: walk onto it for that.
---
---Raises an error for a cell that is not floor or not whole numbers, an unknown face, or when not on a
---map.
---
---Since: 1.5.0
---@param x integer The cell, east.
---@param z integer The cell, north.
---@param opts? {face?: "north"|"east"|"south"|"west"} `face`: where the player looks. Default "south".
---@return boolean placed true.
function dungeon.place_player(x, z, opts) end

---Puts an object on a free floor cell of the current map.
---
---Walking onto it works as for the game's own. The game leaves a used "exp" in `grid().events`.
---
---Raises an error for an unknown kind, a cell that is not free floor, or when not on a map.
---
---Since: 0.25.0
---
---```lua
---local g = rq.dungeon.grid()
---if g then rq.dungeon.place("chest", g.player.x + 1, g.player.z, { content = 120 }) end
---```
---@param kind "chest"|"red-chest"|"spring"|"achievement"|"exp"|"gold" The object.
---@param x integer The cell, east.
---@param z integer The cell, north.
---@param opts? {content?: integer, rank?: integer} For a chest: `content`, a fixed item number; `rank`, its rank.
---@return boolean placed true when the object was put down.
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
---The footer's controls (`system`, `vault`, `trivia` in section `system-bar`; the party cards in
---section `party`; the money line) are listed on every screen whose own loop takes them: the town,
---its tabs, Rance Castle (`castle`), the status page, the vault, a quest's map and a fight, and an
---unnamed screen (`other`) that shows them. On `castle` each room is `room-<area>`, with the room's
---name as the label and its floor (`B2F`, with ` new` for the NEW mark) as the value.
---
---A game control a player cannot click has `covered = true`: every point of it is under a kit widget
---of the last update (section `kit`), or a mod's page captures the mouse (`rq.hud.capture_mouse`).
---`rqmod-ctl press` refuses it. A page closed on this update still covers until the next one.
---
---A kit control (section `kit`) under the kit's keys cursor has `focused = true` (`rq.uikit.focused`).
---
---On the screen `select` (the game's choice menu and dialogs), `select.message` holds the dialog's
---text rows, one a line.
---
---It builds the whole list, so do not call it on every `rq.on_frame`.
---
---Since: 0.20.0
---Changed: 0.21.0 A screen with no name says `other`.
---Changed: 2.11.0 `covered`.
---Changed: 2.12.0 `focused` on kit controls.
---Changed: 2.19.0 `select.message`.
---Changed: 2.20.0 `status.chara` is the character shown (it was always 0).
---Changed: 2.20.1 The footer (`system`, `vault`, `trivia`, the party cards and the money line) on
---every screen that shows it; Rance Castle is the screen `castle`, with its rooms as `room-<area>`
---and the town tabs; the town tabs on `quests`; the SYSTEM menu over Quests or the status page is
---`system-menu` (it was the screen under it).
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
---The point is inside the control and not covered by another control. For a game control it is also
---away from the kit's widgets over it. It builds the list when nothing did during this update.
---
---Since: 0.20.0
---Changed: 0.21.0 Builds the list itself; before, it read the last `rq.ui.get`.
---Changed: 2.11.0 Away from kit widgets over a game control.
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

---One row of `rq.ui.choose`.
---
---Since: 2.7.0
---@class rq.UiChoice
---@field id? any What `fn` gets for this row: any value but nil. Default: its place in the list, counting separators.
---@field text string The text.
---@field r? integer Red, 0 to 255. Default: the game's grey.
---@field g? integer Green, 0 to 255. Default: the game's grey.
---@field b? integer Blue, 0 to 255. Default: the game's grey.
---@field comment? string Shown in the game's comment box beside the menu while the row is under the mouse.
---@field sound? integer A sound number the row plays when picked; default the game's.

---Opens the game's own choice menu outside a scene, on the hub or any of the game's screens, and
---calls `fn` with the row the player picks.
---
---The menu opens at the next update, and `fn` runs when it closes. It is modal: the game's screen
---under it waits until the player picks, as under the vault's sort menu. It waits to open while the
---System menu or Config is open, while a scene plays, and while another choice menu shows. The
---menu does not scroll, so it takes at most 20 rows. With fewer than 3 rows it draws the game's tall
---buttons.
---
---Under the menu, the HUD still draws (a mod's page stays on the screen, under the menu), but
---`rq.uikit` widgets take no input and `rq.hud.capture_mouse` does nothing, so the menu gets the
---mouse and the keys. The player cancels with a right click or Space, the game's cancel keys, or
---the Return row; `fn` then gets 0. Esc does not cancel the game's menus. For a menu inside a scene
---use `rq.scene.choice`; for its look as a widget on a page, `rq.uikit.options`.
---
---Raises an error without a function, with a row without text, with no row to pick, or with more
---than 20 rows.
---
---Since: 2.7.0
---
---```lua
---rq.ui.choose({ "Sell all", { id = "keep", text = "Keep the rare ones" }, "-", "Sort first" },
---  function(id)
---    if id == 0 then return end -- cancelled
---    rq.log("picked", id)
---  end, { message = "What now?" })
---```
---@param items (string|rq.UiChoice)[] The rows: a text, `"-"` for a separator, or a table.
---@param fn fun(id: any) Called once with the picked row's id, or 0 when cancelled.
---@param opts? {message?: string, back?: string|boolean, cancel?: boolean, cursor?: any, x?: integer, y?: integer} `message`: text rows over the list ("\n" starts a line). `back`: the Return row's text, or false for none (default "Return"). `cancel`: false turns off the right click and Space (default true). `cursor`: the id of the row the cursor starts on. `x`, `y`: the menu's top left (default the center).
---@return boolean opened true when the menu will open; false when one is already waiting or open, and then `fn` is not called.
---@see rq.ui.choosing
---@see rq.uikit.options
function ui.choose(items, fn, opts) end

---Says whether the game's choice menu shows: one of the game's own, `rq.scene.choice` or
---`rq.ui.choose`.
---
---While it does, the HUD draws under the menu, and `rq.uikit` widgets take no input until the
---mouse buttons are up after it.
---
---Since: 2.7.0
---@return boolean choosing true while the menu shows.
function ui.choosing() end

---Asks the player Yes or No in the game's own dialog, and waits for the answer.
---
---It is the window of the game's `選択_確認`: the text, then Yes and No as the game's tall buttons.
---The call returns when the player picks, as the game's own questions do, so it can answer inside
---the game's flow: from an `rq.ain.hook` callback, a kit widget's click (`rq.on_frame`), an
---`rq.ui.choose` callback or a scene. While it shows, the HUD draws under it and `rq.uikit` widgets
---take no input (`rq.ui.choosing`); every `rq.on_frame` callback still runs, the caller's too.
---
---Only "\n" starts a line in the text; the window wraps it at its width, and gives every 3 lines a row
---of their own. `@` and `$`, where the game's own texts break, are drawn as `＠` and `＄` (the game's
---font draws them half wide, with a space after) unless `opts.game` is true, for a text that comes
---from the game.
---
---With `opts.remember`, the window adds the game's "don't ask again" row. A number 1 to 99 is the
---game's own switch (as `選択_確認_拡張`: 10 purchasing, 14 selling, 31 donating, and the others in
---Config's Confirmations tab), so the player's setting counts. A text is the mod's own question: once
---the player turns it off, the call returns true without asking, in every save. The loader then
---lists it on a Confirmations page in Config's Mods tab, where the player can turn it back on, and
---Resetting in Config's Confirmations tab turns it back on too.
---
---The game has one list of dialog rows, so the call shows nothing and returns nil while another
---dialog is being built or shows (`rq.ui.choosing`, or one of these calls), and logs it once.
---
---Raises an error while `init.lua` loads (the game is not running yet), for an empty text or one that
---is not a string, for more than 9 lines, for an option of the wrong type, and for a `remember` that
---is neither a text nor 1 to 99.
---
---Since: 2.19.0
---
---```lua
---if rq.ui.confirm("Swap the absorbed item?\nThe old one goes back to the vault.",
---    { remember = "swap", remember_label = "Swapping an absorbed item" }) then
---  swap()
---end
---```
---@param text string The question.
---@param opts? {yes?: string, no?: string, cursor?: "yes"|"no", sound_yes?: integer, sound_no?: integer, remember?: string|integer, remember_label?: string, game?: boolean} `yes`, `no`: the buttons' labels (default "Yes", "No"). `cursor`: the button the cursor starts on (default "yes"). `sound_yes`, `sound_no`: sound numbers for the buttons (default the game's). `remember`: a key for the mod's own "don't ask again", or 1 to 99 for the game's own. `remember_label`: how the Confirmations page names the mod's question (default the key). `game`: the text is the game's, where `@` and `$` start a line.
---@return boolean|nil yes true for Yes (or a question turned off), false for No, nil when it was not shown.
---@see rq.ui.notice
---@see rq.ui.ask
function ui.confirm(text, opts) end

---Tells the player something in the game's own dialog, with one OK button, and waits until it is
---pressed.
---
---It is the window of the game's `選択_通知`. The text, where it may be called from, and the one
---dialog at a time are as for `rq.ui.confirm`.
---
---Raises an error while `init.lua` loads, for an empty text or one that is not a string, for more
---than 9 lines, and for an option of the wrong type.
---
---Since: 2.19.0
---
---```lua
---rq.ui.notice("The vault cannot take " .. name .. ".")
---```
---@param text string The message.
---@param opts? {ok?: string, game?: boolean} `ok`: the button's label (default "OK"). `game`: the text is the game's, where `@` and `$` start a line.
---@return boolean|nil shown true once the player pressed OK, nil when it was not shown.
function ui.notice(text, opts) end

---One row of `rq.ui.ask`.
---@class rq.UiAskRow
---@field id? any What `ask` returns for this row: any value but nil. Default: its place in the list, counting separators.
---@field text string The text.
---@field r? integer Red, 0 to 255. Default: the game's grey.
---@field g? integer Green, 0 to 255. Default: the game's grey.
---@field b? integer Blue, 0 to 255. Default: the game's grey.
---@field comment? string Shown in the game's comment box beside the dialog while the row is under the mouse.
---@field sound? integer A sound number the row plays when picked; default the game's.

---Asks the player a question with any choices in the game's own dialog, and waits for the answer.
---
---It is the game's window as `選択_確認` builds it, with the rows given: the text, a separator, then
---the rows. With fewer than 3 choices they are the game's tall buttons. The text, where it may be
---called from, and the one dialog at a time are as for `rq.ui.confirm`. Unlike `rq.ui.choose`, it
---waits for the answer, has a text over the rows, and has no Return row and no cancel unless asked.
---
---The window does not scroll: the rows to pick and the lines of text together are at most 20 (two
---separators count as one), for example 9 lines and 11 rows, or 20 rows and no text.
---
---Raises an error while `init.lua` loads, for a text that is not a string, for more than 9 lines, for
---a row without text, with no row to pick, with more rows and lines than fit, and for an option of the
---wrong type.
---
---Since: 2.19.0
---
---```lua
---local pick = rq.ui.ask("Take Quest@Gem Lady?", {
---  { id = "yes", text = "Yes", sound = 5012 }, { id = "trade", text = "Trade a Jewel" }, { id = "no", text = "No" },
---}, { game = true, cursor = "yes" })
---```
---@param text string|nil The question, or nil for rows only.
---@param rows (string|rq.UiAskRow)[] The rows: a text, `"-"` for a separator, or a table.
---@param opts? {cursor?: any, cancel?: boolean, x?: integer, y?: integer, game?: boolean} `cursor`: the id of the row the cursor starts on. `cancel`: true lets a right click or Space close it, and the call returns 0 (default false, as the game's questions). `x`, `y`: the dialog's top left (default the center). `game`: the text is the game's, where `@` and `$` start a line.
---@return any|nil id The picked row's id, 0 when cancelled, nil when it was not shown.
---@see rq.ui.choose
function ui.ask(text, rows, opts) end

---Draws text, boxes and CGs over the game, and reads clicks on them.
---
---The HUD is immediate mode: draw everything again on every `rq.on_frame`, and it shows for that
---frame. It is drawn on the game's own picture, after the game's scene, in game pixels. Every frame
---can hold up to 480 draw calls; past that, a call draws nothing. While the game's choice menu
---shows (`rq.ui.choosing`), the HUD is drawn under the menu, so a mod's page does not cover it.
---
---Since: 0.2.0
---Changed: 2.7.0 Drawn under the game's choice menu while it shows.
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
---@field bold? boolean The heavier weight the game's own Config labels look like. Default false. Since 1.8.0. With `font = "game"`: the game's bold.
---@field font? "hud"|"game" "game" draws in the game's own font, as its buttons and lists do: no dark edge, and `edge` does not count. Default "hud". Since 2.5.0.
---@field face? 256|257|258 With `font = "game"`: the game's font number. 258 is on most buttons, lists and the castle; 256 on item names; 257 on big marks. Default 258. Since 2.5.0.
---@field outline? integer[]|{r: integer, g: integer, b: integer, w?: integer} With `font = "game"`: an outline in this color, `w` pixels wide (1 to 4, default 1), as the castle's room names. Since 2.5.0.
---@field shadow? integer[]|{x?: integer, y?: integer, r: integer, g: integer, b: integer, a?: integer} With `font = "game"`: a copy of the text in this color, `x` and `y` pixels away (default 1 and 1), drawn under it. `a` is its opacity, 0 to 255, times the text's (default 255); the game's sort labels use 64. Since 2.5.0; `a` since 2.11.0.
---@field space? integer With `font = "game"`: the game's fixed spacing in pixels, for Japanese letters (`SetFixedWidthSpacing`). Default -4. Since 2.5.0.
---@field spacing? number With `font = "game"`: the game's letter spacing for English letters, 0 to 4 (`SetSpacing`); the game's lists use 0.95. Default 1. Since 2.5.0.

---Draws a line of UTF-8 text.
---
---The text is white with a dark edge unless the style says otherwise. The font is VL PGothic, from
---the game's `xsystem4\fonts` folder, each letter at its own width. Only characters the game's font
---can draw show. With `w` and `align` the text is placed in the box `w` wide from `x`, and with `h`
---and `valign` in the box `h` high from `y`; text wider than the box goes past it on both sides.
---
---With `font = "game"` the game draws the text in its own font, letter by letter with its own
---spacing, as its buttons, lists and the castle do; `valign = "center"` then centers it as the game
---centers its button labels. The game draws each different text once; it is kept for the next
---updates. Up to 512 different texts draw on one update; past that, the rest draw nothing and the
---log says so once. It needs the game running: before the title it draws nothing.
---
---Raises an error for an unknown `align`, `valign` or `font`, a `face` other than 256 to 258, a
---`size` outside 6 to 96 with the game's font, or a bad `outline`, `shadow` (`shadow.a` outside 0 to
---255) or `spacing`.
---
---Since: 0.2.0
---Changed: 1.8.0 The font is VL PGothic with letters at their own width (before, VL Gothic with half-width Latin letters).
---Changed: 2.5.0 Added `font = "game"` with `face`, `outline`, `shadow`, `space` and `spacing`.
---Changed: 2.11.0 `shadow.a`.
---
---```lua
---rq.hud.text(0, 40, "Centered", { size = 20, w = 1024, align = "center" })
------ A room name as Rance Castle writes it, on the game's button.
---rq.hud.cg(100, 200, 11153, { src = { 0, 0, 206, 30 } })
---rq.hud.text(100, 200, "My Room", { font = "game", size = 24, r = 25, g = 25, b = 25,
---  outline = { 144, 144, 136 }, spacing = 0.95, w = 206, h = 30, align = "center", valign = "center" })
---```
---@param x integer Left edge, or of the box with `style.w`, in game pixels.
---@param y integer Top edge, or of the box with `style.h`, in game pixels.
---@param str string The text.
---@param style? rq.HudTextStyle Size, color and placement.
---@see rq.hud.text_width
function hud.text(x, y, str, style) end

---Returns how wide `rq.hud.text` draws a text.
---
---Only `size` and `bold` of a style count, and with `font = "game"` also `face`, `outline`,
---`shadow`, `space` and `spacing`. With `font = "game"` it also gives the height of the line the game
---draws, which `valign` centers.
---
---With `font = "game"` and `measure = true` it gives the width the game's own windows reckon instead
---(its `MeasureText`): the letters' advances by its kerning tables, without the spacing after the
---last letter, and the font size as the height. The game sizes its comment window and wraps its texts
---by it, so a window laid out by it matches the game's to the pixel. It can be a pixel or two off the
---line `rq.hud.text` draws.
---
---Since: 0.2.0
---Changed: 2.5.0 Takes `font = "game"`, and then also gives the height.
---Changed: 2.13.0 `measure`.
---
---```lua
---local st = { font = "game", face = 258, size = 20, spacing = 0.95, space = -6, measure = true }
---local w = rq.hud.text_width("Use for organization", st) -- 134, as the game's comment window
---```
---@param str string The text.
---@param size_or_style? number|rq.HudTextStyle|{measure?: boolean} A font size, or a style; `measure` with `font = "game"`. Default size 16.
---@return integer px Width in pixels; 0 for an empty text.
---@return integer|nil h With `font = "game"`: the height in pixels. nil for the HUD font.
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
---narrow it (`rq.hud.clip_push` does). Every frame starts with no clip.
---
---Since: 1.6.0
---Changed: 2.5.0 The box is the one `clip_push` narrows and `clip_pop` goes back to.
---@param x? integer Left edge, in game pixels. Leave all four out to end the clip.
---@param y? integer Top edge, in game pixels.
---@param w? integer Width, in pixels.
---@param h? integer Height, in pixels.
---@see rq.hud.clip_push
function hud.clip(x, y, w, h) end

---Narrows the clip to a box inside the clip before it.
---
---What is drawn next shows only where the box and every clip pushed before it overlap, as for a list
---that scrolls inside a page that is clipped too. `rq.hud.clip_pop` goes back to the clip before.
---`rq.hud.clip(x, y, w, h)` starts over with its box, and `rq.hud.clip()` ends every clip. Every
---frame starts with no clip.
---
---Raises an error past 16 clips inside one another.
---
---Since: 2.5.0
---
---```lua
---rq.hud.clip_push(100, 100, 400, 300)   -- the page
---rq.hud.clip_push(120, 140, 360, 200)   -- a list on it: only the overlap shows
---draw_rows()
---rq.hud.clip_pop()                      -- back to the page
---rq.hud.clip_pop()                      -- no clip
---```
---@param x integer Left edge, in game pixels.
---@param y integer Top edge, in game pixels.
---@param w integer Width, in pixels.
---@param h integer Height, in pixels.
---@see rq.hud.clip_pop
function hud.clip_push(x, y, w, h) end

---Goes back to the clip before the last `rq.hud.clip_push`.
---
---After the first push, it ends the clip. With nothing pushed it does nothing.
---
---Since: 2.5.0
function hud.clip_pop() end

---Runs a function now and draws what it draws on top of the whole HUD.
---
---The HUD draws in call order, so a later draw covers an earlier one, also another mod's. What `fn`
---draws goes to the top layer instead: it is drawn after every other HUD draw of the update, from
---every mod, in the order it was called. Use it for what floats over other widgets: a dragged icon, a
---comment box, a tooltip.
---
---Inside `fn` the clip starts at the whole screen, whatever clip was set outside; `rq.hud.clip`,
---`clip_push` and `clip_pop` work inside it. After `fn` the clip from outside is back, also when `fn`
---raises an error, which goes on to the caller. `top` inside `top` works the same way. The top layer
---shares the 480 draws an update has. While the game's choice menu shows, the top layer is drawn
---under the menu with the rest of the HUD.
---
---It only draws on top: the kit still decides clicks in call order, so a widget drawn after it under
---the same pixels still gets the mouse.
---
---Raises an error when `fn` is not a function, and the error `fn` raises.
---
---Since: 2.11.0
---
---```lua
---rq.on_frame(function()
---  if dragging then
---    local m = rq.input.mouse()
---    rq.hud.top(function()
---      rq.hud.cg(m.x - 50, m.y - 50, icon, { w = 100, h = 100 })
---    end)
---  end
---  draw_my_page()   -- drawn later, still under the icon
---end)
---```
---@param fn fun() The draws for the top layer.
---@return any ... What `fn` returns.
function hud.top(fn) end

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
---see the clicks. It also hides the clicks `rqmod-ctl` drives, as a player's, and the kit's widgets
---mark the game's controls under them `covered` in `rq.ui.get`.
---
---Since: 0.2.0
---Changed: 2.11.0 It hides the clicks `rqmod-ctl` drives too (before, they reached the game).
function hud.block_game_input() end

---Hides the keyboard, or some keys, from the game for this update.
---
---With no argument every key is hidden. With a list, only those keys are, by name (as
---`rq.input.code` takes them) or by code; calls on one update add up. Call it on every update while
---a modal HUD is open. The mouse buttons are not affected (use `rq.hud.block_game_input`; a mouse
---button in the list raises an error), and `rq.input.key` still reads the real keys.
---
---Since: 0.3.0
---Changed: 2.12.0 `keys`: only some keys.
---
---```lua
---rq.hud.block_keys({ "up", "down", "enter" }) -- the game still gets Esc and the rest
---```
---@param keys? (string|integer)[] The keys to hide, by name or code. Default: every key.
function hud.block_keys(keys) end

---Hides the whole mouse from the game for this update: where it is, its buttons and the wheel.
---
---The game sees the cursor off the screen, so its buttons under a mod's page are not hovered, and
---no click or wheel step reaches it. Unlike `rq.hud.block_game_input`, it also hides the mouse that
---`rqmod-ctl` drives, so an agent gets what a player gets. A button that was down stays hidden until
---it comes up, also when the call stops. `rq.input.mouse`, `rq.input.scroll` and the HUD calls still
---see everything. Call it on every update while the page shows; `rq.uikit.page` does.
---
---While the game's choice menu shows (`rq.ui.choosing`), the call does nothing: the menu, drawn over
---the page, needs the mouse.
---
---Since: 2.6.0
---Changed: 2.7.0 Does nothing while the game's choice menu shows.
---
---```lua
---rq.on_frame(function()
---  if not open then return end
---  rq.hud.capture_mouse()
---  rq.hud.block_keys()
---  rq.hud.rect(0, 56, 1024, 536, 0, 0, 0, 200)
---end)
---```
function hud.capture_mouse() end

---Starts reading typed text.
---
---It uses the system's text input, so input methods and keyboard layouts work. Only characters the
---game's font can draw are kept, and Backspace removes the last character. Read the text with
---`rq.hud.text_value` and end with `rq.hud.text_end`. Calling it again restarts with the new text.
---
---Since: 0.3.0
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
---Since: 0.3.0
---@return string text The text so far, in UTF-8.
---@return boolean active true between `text_begin` and `text_end`.
function hud.text_value() end

---Stops reading typed text.
---
---The text stays readable with `rq.hud.text_value` until the next `text_begin`.
---
---Since: 0.3.0
function hud.text_end() end

---Chest loot: what the pools roll, and the items the player will find, known in advance.
---
---A chest's loot comes from five pool functions: `item` (`持ち物を選ぶ`), `special` (`特殊を選ぶ`),
---`rare` (`レアアイテムを選ぶ`), `ultra` (`ウルトラレアを選ぶ`) and `gear` (`武器防具を選ぶ`, weapons and
---armor). `rq.loot.install` rolls each chest's item when it is placed, so `rq.loot.list` can show it.
---
---Since: 0.2.0
---@class rq.loot
local loot = {}

---An item for `rq.loot.add`.
---@class rq.LootAdd
---@field item string|integer An item number or text id.
---@field pool? "item"|"special"|"rare"|"ultra"|"gear" The pool. Default: every pool. "gear" since 0.15.0.
---@field chance? number The percent chance on each roll of the pool; may be a fraction. Default 1.
---@field unique? boolean Skip it when the player already owns one. Default false.

---Adds an item to the chest pools' rolls.
---
---Each roll of the pool has `chance` percent to give this item instead of the game's.
---
---Since: 0.9.0
---Changed: 0.15.0 Added the "gear" pool; no `pool` includes it.
---
---```lua
---rq.loot.add({ item = "mymod:charm", pool = "rare", chance = 2.5, unique = true })
---```
---@param spec rq.LootAdd The item and its chance.
function loot.add(spec) end

---Keeps an item from ever coming out of a chest.
---
---The pool rolls 0 for it, so the game tries the next pool.
---
---Since: 0.9.0
---@param who string|integer An item number or text id.
---@param opts? {pool?: "item"|"special"|"rare"|"ultra"} `pool`: only this pool. Default: every pool.
function loot.remove(who, opts) end

---Calls `fn` after each roll of a chest pool, to replace the item.
---
---Since: 0.9.0
---Changed: 0.23.0 Returns a remover.
---@param fn fun(ctx: {pool: string, item: integer}): integer|string|nil Returns another item number or text id, 0 for nothing, or nil to keep it.
---@return fun() remove Removes this callback.
function loot.on_pick(fn) end

---Rolls each chest's item when the chest is placed, so it is known in advance.
---
---It skips the game's roll at opening and sets the map's chest flags. Red chests keep the game's
---chance and still explode on a fail, which loses the item. It opens no screen.
---
---Since: 0.2.0
---@return boolean ok true when the patch is in place.
function loot.install() end

---An item the player will find, from `rq.loot.list`.
---@class rq.LootItem
---@field id integer The item instance.
---@field source "magnum"|"chest" Held by the Magnum, or locked in a chest on the map.
---@field seen boolean The player knows the item: another instance with the same catalog number is identified.
---@field cg integer The item's CG when seen, else the game's "?" CG.
---@field name string The real name when seen, else "Unidentified item".
---@field comment string The game's item text when seen, else a line saying it is not identified.
---@field overlay integer 10403 when not seen, else 0.
---@field catalog integer The catalog number. Do not show it for an item not seen.
---@field floor integer The floor.
---@field x integer The chest's cell.
---@field y integer The chest's cell.

---Lists the items waiting for the player: the Magnum's (when it holds one), then the locked chests'.
---
---Needs `rq.loot.install`.
---
---Since: 0.2.0
---@return rq.LootItem[] items The items.
function loot.list() end

---What `rq.chest.on_attempt` and `on_result` get for a red chest.
---@class rq.ChestInfo
---@field x integer The chest's cell.
---@field y integer The chest's cell.
---@field id integer The item locked in the chest by `rq.loot.install`; 0 when none.
---@field base_pct integer This chest's chance from the game and the party's skills: 30, or 60, 80 or 100, maybe halved.
---@field kind string "red".
---@field pct? integer `on_attempt`: the percent so far. `on_result`: the chance used. Since 0.14.0.
---@field rolled? integer `on_result`: the roll, 1 to 100.
---@field success? boolean `on_result`: it opened.
---@field exploded? boolean `on_result`: it exploded; a fail always does.

---What `rq.chest.on_attempt` may return instead of a number.
---@class rq.ChestAttemptResult
---@field pct integer The chance, 0 to 100.

---The chance of opening a red chest.
---
---Green chests always open and are not touched. A red chest rolls the game's `成功確率` (30, which a
---thief or Chest Opening can raise), and a fail explodes (12% damage) and loses the item. The engine
---patches only that roll, so Lua can change the chance, for instance to add pity.
---
---Since: 0.2.0
---
---```lua
---rq.chest.install()
---rq.chest.on_attempt(function(info)
---  return math.min(100, info.base_pct + (rq.chest.get("pity") or 0))
---end)
---rq.chest.on_result(function(info)
---  if info.success then rq.chest.set("pity", 0)
---  else rq.chest.set("pity", (rq.chest.get("pity") or 0) + info.base_pct) end
---end)
---```
---@class rq.chest
local chest = {}

---Patches the red chest's roll.
---
---Calling it again, or after `rq.loot.install`, does nothing more.
---
---Since: 0.2.0
---@return boolean ok true when the patch is in place.
function chest.install() end

---Calls `fn` just before a red chest rolls, to set the chance.
---
---Each callback sees the percent the one before left in `info.pct`; nil keeps it. With no callback the
---chance is `info.base_pct`, not 100. The chance is written to `成功確率` and to the "(N% chance to
---disarm)" line before that choice shows. Up to 16 callbacks.
---
---Since: 0.2.0
---Changed: 0.14.0 Every mod's callback runs; added `info.pct`.
---Changed: 0.23.0 Returns a remover.
---@param fn fun(info: rq.ChestInfo): integer|rq.ChestAttemptResult|nil Returns the percent.
---@return fun() remove Removes this callback.
function chest.on_attempt(fn) end

---Calls `fn` after a red chest rolled.
---
---Up to 16 callbacks.
---
---Since: 0.2.0
---Changed: 0.14.0 Every mod's callback runs.
---Changed: 0.23.0 Returns a remover.
---@param fn fun(info: rq.ChestInfo) Gets the chest and the roll.
---@return fun() remove Removes this callback.
function chest.on_result(fn) end

---Reads a number kept for this session, such as a pity count.
---
---It is not in the save; use `rq.store` for that.
---
---Since: 0.2.0
---@param key string The key.
---@return integer|nil value The number, or nil when not set.
function chest.get(key) end

---Keeps a number for this session.
---
---Since: 0.2.0
---@param key string The key.
---@param val integer The number.
function chest.set(key, val) end

---A roster member, from `rq.orb.roster`.
---@class rq.OrbMember
---@field iic integer The roster slot.
---@field state integer Where the character is (`登録状態`): 1 party, 2 reserve, 3 spent.
---@field hp integer HP.
---@field max_hp integer Most HP.

---The blue Recovery Spring on the map (`回復の泉`, object 203).
---
---The engine patches the party heal inside `回復の泉処理`; Lua decides who heals and how with
---`on_heal`. The rest of the spring (Carolie, the flash, clearing the cell) stays the game's. The calls
---that write raise an error for a slot with no character.
---
---Since: 0.2.0
---
---```lua
---rq.orb.install()
---rq.orb.on_heal(function()
---  for _, m in ipairs(rq.orb.roster()) do
---    if m.state == 1 or m.state == 2 then
---      rq.orb.full_heal(m.iic)
---    elseif m.state == 3 then  -- revive the spent at 30%
---      rq.orb.set_state(m.iic, 2)
---      rq.orb.set_hp(m.iic, math.max(1, math.floor(m.max_hp * 3 / 10)))
---    end
---  end
---end)
---```
---@class rq.orb
local orb = {}

---Patches the spring's party heal so it calls `on_heal`.
---
---Since: 0.2.0
---@return boolean ok true when the patch is in place.
function orb.install() end

---Calls `fn` when the spring heals, in place of the game's party heal.
---
---Every callback runs, so two mods that heal both heal. The game's heal runs only with no callback, or
---when all of them failed. Up to 16 callbacks.
---
---Since: 0.2.0
---Changed: 0.14.0 Every mod's callback runs.
---Changed: 0.23.0 Returns a remover.
---@param fn fun() Heals.
---@return fun() remove Removes this callback.
function orb.on_heal(fn) end

---Lists the roster members, slots 20 to 200 with a state other than 0.
---
---Since: 0.2.0
---@param state? integer Only members with this state.
---@return rq.OrbMember[] members The members.
function orb.roster(state) end

---Returns where a character is (`登録状態`).
---
---Since: 0.2.0
---@param iic integer The roster slot.
---@return integer state 1 party, 2 reserve, 3 spent.
function orb.get_state(iic) end

---Sets where a character is (`登録状態`).
---
---Since: 0.2.0
---@param iic integer The roster slot.
---@param state integer 1 party, 2 reserve, 3 spent.
function orb.set_state(iic, state) end

---Sets a character's status (`状態変化`).
---
---Since: 0.2.0
---@param iic integer The roster slot.
---@param status integer The status: 0 none, 1 poison, 3 downed.
function orb.set_status(iic, status) end

---Returns a character's HP.
---
---Since: 0.2.0
---@param iic integer The roster slot.
---@return integer hp HP.
function orb.get_hp(iic) end

---Returns a character's most HP.
---
---Since: 0.2.0
---@param iic integer The roster slot.
---@return integer max Most HP.
function orb.get_max_hp(iic) end

---Sets a character's HP.
---
---Since: 0.2.0
---@param iic integer The roster slot.
---@param hp integer HP.
function orb.set_hp(iic, hp) end

---Fills a character's HP (`Ｃ補給／体力`).
---
---Since: 0.2.0
---@param iic integer The roster slot.
function orb.full_heal(iic) end

---What `rq.skill.get` returns.
---
---It also holds every field `rq.SkillPatch` lists.
---@class rq.SkillInfo
---@field id integer The skill number.
---@field name string The name.
---@field kinou integer The function code (`機能`).
---@field bairitsu integer The multiplier (`倍率`).
---@field target integer The target code (`対象`); names in `rq.ids.target`.
---@field koudou integer The action kind (`行動種類`): 1 physical, 2 magic, 3 special, 4 support, 0 ability.
---@field kouka integer The effect code (`効果`).
---@field desc1 string The raw description (`説明文１`); it may still hold `$`.

---Reads, defines and patches skills, and rewrites their hover text.
---
---Skill numbers run 1 to 1000; the game's end near 600. A text id such as `"mymod:slash"` becomes a
---stable number from 700 to 999. Strings are UTF-8 in Lua. The calls that write raise an error for a
---number outside 1 to 1000. Up to 128 `define` and `patch` calls in all.
---
---Since: 0.2.0
---@class rq.skill
local skill = {}

---Wraps `スキル_説明文１`, the hover text on the status and fight screens, so `on_describe` can rewrite
---it.
---
---Since: 0.2.0
---@return boolean ok true when the patch is in place.
function skill.install() end

---Calls `fn` for a skill's hover text, to rewrite it.
---
---Each callback gets the text the one before left. Needs `rq.skill.install`. Up to 16 callbacks.
---
---Since: 0.2.0
---Changed: 0.14.0 Every mod's callback runs.
---Changed: 0.23.0 Returns a remover.
---
---```lua
---rq.skill.install()
---rq.skill.on_describe(function(no, text)
---  local s = rq.skill.get(no)
---  return text .. " (x" .. s.bairitsu .. ")"
---end)
---```
---@param fn fun(skillNo: integer, baseText: string): string Returns the new text.
---@return fun() remove Removes this callback.
function skill.on_describe(fn) end

---Returns a skill's fields.
---
---Since: 0.2.0
---Changed: 0.5.0 Every field `rq.skill.patch` takes; text ids.
---@param id integer|string A skill number or text id.
---@return rq.SkillInfo info The fields.
function skill.get(id) end

---A new skill for `rq.skill.define`.
---@class rq.SkillSpec
---@field id integer|string A number 1 to 1000, or a text id.
---@field kind "physical"|"knowledge" "physical": an active attack (type 10, action 1, panel 100). "knowledge": a passive (type 30, action 0, panel 105).
---@field name? string The name.
---@field flavor? string The battle-log verb (`Ｓ表示文`).
---@field desc? string The hover text; `$` is a line break.
---@field target? integer 1 melee (front row), 2 ranged, 3 all enemies, 4 needs two or more front-row enemies (`rq.ids.target`).
---@field bairitsu? integer The multiplier (`倍率`).
---@field flat? integer Added damage (`付与ダメージ`).
---@field kinou? integer The function code (`機能`).
---@field kouka? integer The effect code (`効果`).
---@field kouka2? integer The second effect code.
---@field kouka3? integer The third effect code.
---@field kouka4? integer The fourth effect code.
---@field cap? integer The damage cap (`ダメージ上限`); 0 for none.
---@field basic? integer The basic-skill flag (`基本技`).
---@field weapon? integer The weapon it needs (`発動条件／武器`).
---@field ef? string The game's effect text (`ＳＥＦ`).

---Defines a new skill.
---
---The row is written at the end of every `Ｓ起動時登録`, and at once when boot already ran.
---
---Raises an error for a missing id, a too long id, a text id whose number another has, or past 128
---calls.
---
---Since: 0.2.0
---Changed: 0.5.0 Text ids; returns the number.
---
---```lua
---local SLASH = rq.skill.define({ id = "mymod:slash", kind = "physical", name = "Slash",
---  target = 1, bairitsu = 120, desc = "A quick cut.$Front row." })
---```
---@param spec rq.SkillSpec The skill.
---@return integer id The skill number.
function skill.define(spec) end

---Fields for `rq.skill.patch`, named after the game's.
---@class rq.SkillPatch
---@field name? string The name.
---@field flavor? string The battle-log verb.
---@field desc? string The hover text; `$` is a line break.
---@field ef? string The game's effect text.
---@field basic? integer `基本技`.
---@field big? integer `大技`.
---@field kinou? integer `機能`, the function code.
---@field kouka? integer `効果`, the effect code.
---@field kouka2? integer `効果２`.
---@field kouka3? integer `効果３`.
---@field kouka4? integer `効果４`.
---@field locked? integer `削除不可`: cannot be removed.
---@field type? integer `タイプ`: 10 active, 30 knowledge.
---@field action? integer `行動種類`, the action kind.
---@field panel? integer `パネル色`, the panel color.
---@field target? integer `対象`, the target code (`rq.ids.target`).
---@field bairitsu? integer `倍率`, the multiplier.
---@field cap? integer `ダメージ上限`, the damage cap.
---@field element? integer `属性`, the element (`rq.ids.element`).
---@field flat? integer `付与ダメージ`, added damage.
---@field no_first_turn? integer `初ターン使用禁止技`: not on the first turn.
---@field once_per_battle? integer `同一戦闘一度のみ技`: once per fight.
---@field needs_charge? integer `要溜め`: needs a charge.
---@field efa? integer `ＥＦＡ`.
---@field efb? integer `ＥＦＢ`.
---@field weapon? integer `発動条件／武器`, the weapon it needs.

---Changes an existing skill, field by field.
---
---Fields left out stay as the game made them. Patches apply after `Ｓ起動時登録`, in call order with
---the defines, and at once when boot already ran. An unknown field is logged and skipped.
---
---Since: 0.5.0
---
---```lua
---rq.skill.patch(246, { name = "Quick Start", desc = "Acts first.$Two ranks." })
---```
---@param id integer|string A skill number or text id.
---@param fields rq.SkillPatch The fields to change.
function skill.patch(id, fields) end

---Sorts a skill in the known and potential lists as if it were another.
---
---It wraps `compSkill`.
---
---Since: 0.2.0
---Changed: 0.5.0 Takes text ids.
---@param id integer|string The skill to place.
---@param as_id integer|string The skill it sorts as; 0 clears it.
---@return boolean ok false when the compare hook is missing.
function skill.sort_as(id, as_id) end

---Pages the Potential Skills grid, 36 skills a page.
---
---The game keeps the skills a character can buy in rows 1 to 60 of three arrays (`取得可能スキル`,
---`取得可能スキルポイント`, `取得不可能スキル説明`) and shows the first 36. With `install`, the arrays
---hold rows 1 to 144 and the list shows them 36 at a time. A page is 36 rows of the game's own
---filtered list (no panel color 104, none the character holds) in the order they were added, then
---sorted with `compSkill` as the game sorts. Opening the window goes back to page 1. Known Skills is
---not paged.
---
---It is Lua over the game's own code (`src/lua/skilllist.lua`): the arrays are made longer with
---`rq.ain.set_global_len` whenever the game fills them, `スキルポン` stops at row 145 instead of 54,
---and while the window builds its list `取得可能スキル_数` and `取得可能スキル_番号` answer the page's
---rows. The buy is the game's own. Rows 61 to 144 are emptied when the window closes, so a save holds
---only what the game's 60 rows would.
---
---The page buttons are a mod's own: `install{ buttons = false }`, then `rq.gamebutton` entries in the
---`"skill-window"` group that set the page and call `rq.skilllist.refresh`. `install()` with no
---options adds two such entries for the mod, Previous Page and Next Page (`<mod id>.skilllist-prev`
---and `-next`), for mods made before 2.17.0; `on_page` greys them and `on_buttons` gets their ids.
---
---Since: 0.2.0
---Changed: 2.18.0 Lua over the game's own arrays, made longer; 144 rows (were 143).
---
---```lua
---rq.skilllist.install({ buttons = false })
---local function step(dir)
---  local p = rq.skilllist.page() + dir
---  if p >= 1 and p <= rq.skilllist.page_count() then return p end
---end
---rq.gamebutton.add({ group = "skill-window", id = "next", label = "Next Page",
---  enabled = function() return step(1) ~= nil end,
---  on_click = function() rq.skilllist.set_page(step(1)); rq.skilllist.refresh() end })
---```
---@class rq.skilllist
local skilllist = {}

---Turns the paged list on, and with `buttons` adds Previous Page and Next Page.
---
---Calling it again does nothing more, except that a call with `buttons` adds the two buttons when an
---earlier call left them out.
---
---Raises an error for `opts` that is not a table, or when `スキルポン`'s stop at 54 is not found.
---
---Since: 0.2.0
---Changed: 2.17.0 `opts.buttons`; the buttons are at y 660 (were 652), under the list's picture.
---Changed: 2.18.0 The buttons are the calling mod's `"skill-window"` entries, placed in the order
---entries were added (before, they always took the first row).
---@param opts? {buttons?: boolean} `buttons = false` leaves out Previous Page and Next Page, for a mod that makes its own with `rq.gamebutton`. Default true.
---@return boolean ok true when the patch is in place.
function skilllist.install(opts) end

---Calls `fn` when the Potential Skills menu opens, after the page is set to 1 and before the grid is
---built.
---
---Since: 0.2.0
---Changed: 2.18.0 No limit on the number of callbacks (was 16).
---Changed: 0.14.0 Every mod's callback runs.
---Changed: 0.23.0 Returns a remover.
---@param fn fun() Runs on open.
---@return fun() remove Removes this callback.
function skilllist.on_open(fn) end

---What `rq.skilllist.on_page` gets.
---@class rq.SkillListCtx
---@field dir integer -1 previous, +1 next, 0 asks for the current page.
---@field page integer The current page, from 1.
---@field pages integer How many pages, at least 1.

---Calls `fn` to decide the page.
---
---It is also called once per arrow while the buttons are built: false or nil greys that arrow out, so
---its click plays the game's deny sound and does not call Lua. An enabled click calls it again. The
---last callback that answers (not nil) decides; all nil denies. With no callback, the page moves while it
---stays between 1 and the page count.
---
---Since: 0.2.0
---Changed: 0.14.0 Every mod's callback runs; the last answer decides.
---Changed: 2.18.0 No limit on the number of callbacks (was 16).
---Changed: 0.23.0 Returns a remover.
---@param fn fun(ctx: rq.SkillListCtx): integer|false|nil Returns the page to show, or false to deny.
---@return fun() remove Removes this callback.
function skilllist.on_page(fn) end

---Calls `fn` after each build of the list, with the ids of the Previous and Next that `install()` added.
---
---Pass the ids to `rq.agent.control` so tests can press them. Only with those buttons (`install()`
---without `buttons = false`); they are also agent controls already, `<mod id>.skilllist-prev` and
---`-next`, as every `rq.gamebutton` entry.
---
---Since: 0.2.0
---Changed: 0.14.0 Every mod's callback runs.
---Changed: 2.18.0 The ids are those of `"skill-window"` cells, 200101 to 200104 (were 200037 and
---200038); no limit on the number of callbacks (was 16).
---Changed: 0.23.0 Returns a remover.
---@param fn fun(prev_id: integer, next_id: integer) Gets the `T_Button` ids; 0 for an arrow that was not made.
---@return fun() remove Removes this callback.
function skilllist.on_buttons(fn) end

---Sets the page used the next time the grid is cut.
---
---Raises an error before `rq.skilllist.install`.
---
---Since: 0.2.0
---@param page integer The page; below 1 becomes 1.
function skilllist.set_page(page) end

---Returns the page the grid shows, or will show when it is next cut.
---
---Raises an error before `rq.skilllist.install`.
---
---Since: 2.17.0
---@return integer page The page, from 1.
function skilllist.page() end

---Builds the Potential Skills list again for the character shown, as the game does after a buy.
---
---Use it after `set_page` to show the new page at once, for example from a button's `on_click`. The
---page's buttons, the window's and every mod's, are made again with it. Does nothing while the window
---is closed.
---
---Since: 2.17.0
---@return boolean ok true when the list was built; false while the window is closed.
function skilllist.refresh() end

---Returns how many pages the current filtered buy list has.
---
---Raises an error before `rq.skilllist.install`.
---
---Since: 0.2.0
---@return integer pages The page count; 1 before the menu first opens.
function skilllist.page_count() end

---What a profile's save holds, read from its file.
---@class rq.SaveSummary
---@field has_save boolean The profile holds a game save.
---@field saved_at? string When the game save was written, "YYYY-MM-DD HH:MM" (the file's time).
---@field world? integer The world.
---@field fame_rank? integer The fame rank.
---@field fame? integer Fame.
---@field gold? integer Gold.
---@field quests? integer Quests cleared.
---@field suspend boolean A suspend save exists.
---@field read_error? string Why the save could not be read.

---A mod, as a profile records it.
---@class rq.SaveMod
---@field id string The mod's id.
---@field name string The mod's name.
---@field version string The mod's version.

---A save profile, from `rq.saves.list`.
---@class rq.SaveProfile
---@field id string "base", or an id the engine made. Show `name` instead.
---@field name string The name.
---@field is_base boolean The base profile, which can never be deleted.
---@field active boolean The game runs on this profile.
---@field created? string When it was made.
---@field last_saved? string When it was last saved.
---@field source? string Where it came from, such as "clone of X" or "imported from the vanilla game".
---@field mods? rq.SaveMod[] The mods loaded when the game last wrote a slot of this save; nil when it never did under the loader.
---@field mods_ever? string[] The ids of every mod loaded for any save of this profile.
---@field summary rq.SaveSummary What the save holds.

---What `rq.saves.vanilla` returns.
---@class rq.VanillaSave
---@field exists boolean A game save was found.
---@field path? string Its folder.
---@field summary? rq.SaveSummary What it holds.

---Save profiles: whole save folders the player can switch between.
---
---The base profile is the engine's own `SaveData` folder and can never be deleted. Every other profile
---is a full copy under `<parent of SaveData>\Profiles\<id>\save`, with a `profile.json`;
---`Profiles\active.txt` holds the active id. The engine picks the folder at boot, so switching
---restarts the game. Calls that fail return nil and a message. The vanilla game's own save is only
---ever read.
---
---Since: 0.3.0
---
---```lua
---if rq.saves.title() then
---  for _, p in ipairs(rq.saves.list()) do
---    rq.log(p.name, p.active and "(active)" or "", p.summary.gold)
---  end
---end
---local id = rq.saves.clone("base", "Friend test")
---if id then rq.saves.switch(id) end
---```
---@class rq.saves
local saves = {}

---Wraps `T_Title@checkSelect` so `rq.saves.title` works.
---
---The engine already calls it at boot; calling it again is harmless.
---
---Since: 0.3.0
---@return boolean ok true when the hook is in place.
function saves.install() end

---Returns whether the title screen waits for a choice.
---
---It is false inside the title's sub-menus (load, config).
---
---Since: 0.3.0
---@return boolean waiting true on the title's main choice.
function saves.title() end

---Returns the active profile's id.
---
---Since: 0.3.0
---@return string id "base" for the original save.
function saves.active() end

---Lists the profiles: the base first, then the others by creation time.
---
---It reads every profile's save file, so call it when a window opens, not every frame.
---
---Since: 0.3.0
---@return rq.SaveProfile[] profiles The profiles.
function saves.list() end

---Lists the mods loaded in this session.
---
---Since: 0.3.0
---@return rq.SaveMod[] mods The mods.
function saves.current_mods() end

---Reads the vanilla game's save in `Documents\AliceSoft\<game>\SaveData`.
---
---Since: 0.3.0
---@return rq.VanillaSave save What was found.
function saves.vanilla() end

---Makes a new, empty profile.
---
---Since: 0.3.0
---@param name string The name.
---@return string|nil id The new profile's id, or nil.
---@return string? err Why it failed.
function saves.create(name) end

---Copies a whole profile into a new one.
---
---The game's own `Backup` folder is skipped.
---
---Since: 0.3.0
---@param src_id string The profile to copy.
---@param name string The new profile's name.
---@return string|nil id The new profile's id, or nil.
---@return string? err Why it failed.
function saves.clone(src_id, name) end

---Copies the vanilla game's save folder into a new profile, byte for byte.
---
---It never writes to the source.
---
---Since: 0.3.0
---@param name string The new profile's name.
---@return string|nil id The new profile's id, or nil.
---@return string? err Why it failed.
function saves.import_vanilla(name) end

---Renames a profile.
---
---Since: 0.3.0
---@param id string The profile.
---@param name string The new name, up to 40 bytes.
---@return boolean|nil ok true, or nil.
---@return string? err Why it failed.
function saves.rename(id, name) end

---Deletes a profile.
---
---It refuses "base" and the active profile.
---
---Since: 0.3.0
---@param id string The profile.
---@return boolean|nil ok true, or nil.
---@return string? err Why it failed.
function saves.delete(id) end

---Makes a profile active and restarts the game on it.
---
---It records the choice, starts a new process (which waits for this one to exit) and exits this one.
---
---Since: 0.3.0
---@param id string The profile.
---@return nil ok Nothing: on success it does not return.
---@return string? err Why the choice could not be recorded.
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
---They are the game's `取得可能スキル`: rows 1 to 60, or 1 to 144 with `rq.skilllist` on.
---
---Since: 0.2.0
---Changed: 2.18.0 With `rq.skilllist`, the game's own arrays (made longer) instead of the engine's copy.
---@return rq.OfferRow[] rows The rows in order; empty when there is no buy list.
function chara.offers() end

---Replaces the skill in a buy-list row.
---
---It writes the game's list. The row's level text stays as it is.
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
---@field type? integer The game's type value (`タイプ`, `rq.ids.chara_type`). Also `race`, `model`, `boss` and any other `rq.CharaState` name.
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
---@field weak_element integer The weak element (`rq.ids.element`).
---@field weapon integer The equipped weapon's item instance.
---@field armor integer The equipped armor's item instance.
---@field item integer The held item's instance.
---@field special integer The option slot's item instance.
---@field skill_points integer Free skill points.
---@field race integer The race (`rq.ids.race`).
---@field type integer The type value (`rq.ids.chara_type`).
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

---How an image is fitted to its size: "stretch" fills it, "contain" keeps the proportions and pads
---with transparency, "cover" keeps the proportions and crops the middle, "none" keeps the file's size.
---@alias rq.ImageFit "stretch"|"contain"|"cover"|"none"

---Options for `rq.image.add` and `rq.image.replace`.
---@class rq.ImageOpts
---@field fit? rq.ImageFit How the file fits `w` by `h`. Default "none" for `add`, "stretch" for `replace`.
---@field w? integer Target width. Default: the file's (for `replace`, the game CG's).
---@field h? integer Target height. Default: the file's (for `replace`, the game CG's).
---@field keep_size? boolean `replace` only: keep the file's size instead of the game CG's.

---What `rq.image.info` returns.
---@class rq.ImageInfo
---@field exists boolean The number shows something, from the game or a mod.
---@field mod boolean A mod image is on this number.
---@field replaced? boolean It replaces a game CG.
---@field w? integer Width.
---@field h? integer Height.
---@field game_w? integer The game CG's own width, when replaced.
---@field game_h? integer The game CG's own height, when replaced.

---Images from a mod's folder, shown by the game as CGs.
---
---The engine checks these before the game's archives, so sprites, parts and every game call see the
---mod image. Paths are inside the mod's folder. Formats: PNG (with alpha), JPEG and WebP (still), and
---the game's QNT, AJP and PMS; a GIF goes through `add_gif`. Face cards are 196×232 and standing
---pictures 1024×768. `face` and `portrait` in `rq.chara.define`, `edit` and `set` take a path too: a
---base-game character's own CG is replaced, because screens keep the number they read. A save keeps a
---mod image's number; without the mod it shows no picture there.
---
---Since: 0.6.0
---
---```lua
---rq.chara.define({ id = "mymod:alice", name = "Alice", face = "faces/alice.png", class = 1 })
---rq.chara.edit(52, { face = "faces/kaoru.png" })  -- a new face for Kaoru
---rq.image.replace(2000, "faces/rance.webp", { fit = "cover" })
---```
---@class rq.image
local image = {}

---Registers a file from the mod's folder as a new CG number.
---
---The number comes from the key (900000 and up), so it is the same every run and safe to keep in a
---save. Without `w` and `h` the file keeps its own size.
---
---Raises an error for a file not in the mod's folder, a path outside it, an unknown fit, a too long
---key, a key whose number another key has, or past 256 mod images.
---
---Since: 0.6.0
---
---```lua
---local ICON = rq.image.add("mymod:icon", "icons/star.png", { fit = "contain", w = 56, h = 56 })
---```
---@param key string A key unique to this image, such as "mymod:alice_face".
---@param path string A path inside the mod's folder.
---@param opts? rq.ImageOpts Size and fit.
---@return integer cg The CG number.
function image.add(key, path, opts) end

---Shows a file wherever the game uses one of its CG numbers.
---
---The file is scaled to the game CG's size. Screens already built show it when they next load the CG.
---
---Raises an error as `rq.image.add` does.
---
---Since: 0.6.0
---@param cg integer The game's CG number.
---@param path string A path inside the mod's folder.
---@param opts? rq.ImageOpts Fit (default "stretch") and size.
---@see rq.image.restore
function image.replace(cg, path, opts) end

---Undoes `rq.image.replace` or a `compose` on a game CG.
---
---Since: 0.6.0
---@param cg integer The game's CG number.
---@return boolean removed false when nothing replaced it.
function image.restore(cg) end

---Returns what is on a CG number, and its size.
---
---Use it to read a game CG's size before drawing art for it.
---
---Since: 0.6.0
---@param cg integer The CG number.
---@return rq.ImageInfo info What the number shows.
function image.info(cg) end

---One layer of `rq.image.compose`: a part of a CG, a fill, or text.
---
---Since: 1.8.0
---@class rq.ImageLayer
---@field cg? integer A CG number; the CG being composed reads the game's own picture, so a page can be made from itself.
---@field src? integer[] `{x, y, w, h}` in the CG. Default: all of it.
---@field fill? integer[]|{r: integer, g: integer, b: integer, a?: integer} A color.
---@field text? string Text in the HUD font, placed in the box `w` by `h`.
---@field x? integer Left edge in the new CG. Default 0.
---@field y? integer Top edge in the new CG. Default 0.
---@field w? integer Width; a part is stretched to it. Default: the part's own (a fill: to the right edge).
---@field h? integer Height. Default: the part's own (a fill: to the bottom edge).
---@field alpha? integer Opacity, 0 to 255. Default 255.
---@field mode? "over"|"copy" "copy" replaces what is under it, alpha and all. Default "over".
---@field size? number Text size. Default 16.
---@field color? integer[]|{r: integer, g: integer, b: integer} Text color. Default white.
---@field align? "start"|"center"|"end" Text placement in `w`.
---@field valign? "top"|"center"|"bottom" Text placement in `h`.
---@field edge? boolean false leaves out the text's dark edge.
---@field bold? boolean Bold text.
---@field font? "hud"|"game" "game": the text in the game's own font, as `rq.hud.text` draws it; `face`, `outline`, `shadow`, `space` and `spacing` work as there, `color` gives the color. Since 2.5.0.
---@field face? 256|257|258 With `font = "game"`: the game's font number. Default 258. Since 2.5.0.
---@field outline? integer[]|{r: integer, g: integer, b: integer, w?: integer} With `font = "game"`: an outline. Since 2.5.0.
---@field shadow? integer[]|{x?: integer, y?: integer, r: integer, g: integer, b: integer, a?: integer} With `font = "game"`: a shadow under the text, `a` its opacity (default 255). Since 2.5.0; `a` since 2.11.0.
---@field space? integer With `font = "game"`: the game's fixed spacing. Default -4. Since 2.5.0.
---@field spacing? number With `font = "game"`: the game's letter spacing. Default 1. Since 2.5.0.

---Makes a CG from parts of other CGs, fills and text.
---
---It is built when it is first shown. A key gives a new number as `add` does; a game CG number
---replaces that CG, and `rq.image.restore` undoes it. The size is `opts.w` by `opts.h`, else the game
---CG's own, else the first layer's extent. Other CGs are read as they are when it is built, mod images
---included. Calling it again with the same key or number rebuilds it.
---
---A text layer with `font = "game"` is drawn by the game when `compose` is called, or, before the
---game runs (in `init.lua`), when the CG is first built.
---
---Raises an error for no layers or more than 256, a layer without `cg`, `text` or `fill`, an unknown
---`mode`, or a size it cannot work out (then give `opts.w` and `opts.h`, 1 to 4096), and for a text
---layer's style as `rq.hud.text` does.
---
---Since: 1.8.0
---Changed: 2.5.0 Text layers take `font = "game"`.
---Changed: 2.11.0 A text layer's `shadow.a`.
---
---```lua
---local TAB = rq.image.compose("mymod:tab", {
---  { cg = 40031, src = { 0, 0, 150, 40 } },
---  { text = "Mods", w = 150, h = 40, align = "center", valign = "center" },
---})
---```
---@param target string|integer A key, or a game CG number to replace.
---@param layers rq.ImageLayer[] The layers, back to front.
---@param opts? {w?: integer, h?: integer} The size.
---@return integer cg The CG number.
function image.compose(target, layers, opts) end

---Options for `rq.image.add_gif`.
---@class rq.GifOpts
---@field w? integer Width on screen. Default: the GIF's.
---@field h? integer Height on screen. Default: the GIF's.

---What `rq.image.gif_info` returns.
---@class rq.GifInfo
---@field w integer Width on screen.
---@field h integer Height on screen.
---@field frames integer Frames in the GIF.
---@field ms integer Length of one loop, in ms.

---Serves a GIF from the mod's folder as a game Flash file.
---
---A bare key means `Data/Flash/System/<key>.swf`; a key with a slash is the full file name. A key
---named like a game file replaces it everywhere, such as "タメマーク". It loops at the GIF's own
---speed, rounded to 30 frames a second. GIF has 256 colors and on or off transparency. Up to 1024
---pixels a side, 1024 frames and 32 million pixels in all.
---
---Raises an error for a bad file.
---
---Since: 0.19.0
---@param key string The Flash file's key.
---@param path string A GIF file in the mod's folder.
---@param opts? rq.GifOpts The size on screen.
---@return string key The key.
function image.add_gif(key, path, opts) end

---Undoes `rq.image.add_gif`. New loads use the game's file again, if there is one.
---
---Since: 0.19.0
---@param key string The key.
---@return boolean removed false when no GIF had that key.
function image.remove_gif(key) end

---Returns a GIF's size and length.
---
---Since: 0.19.0
---@param key string The key.
---@return rq.GifInfo|nil info The GIF, or nil when no GIF has that key.
function image.gif_info(key) end

---One of an item's effects: the game's own code and amount.
---@class rq.ItemEffect
---@field kind integer The game's effect code (`機能`), such as 20 for HP percent.
---@field value integer The amount.

---An item template for `rq.item.define`.
---@class rq.ItemDef
---@field id string The text id, such as "mymod:binoculars". It becomes a stable number from 1000 up.
---@field name string The name.
---@field type? "weapon"|"armor"|"item"|"special"|"quest"|"none" The slot: weapon (1), armor (11), item (17), special (21), quest (22), none (23, cannot be equipped). Default "item".
---@field subtype? integer The detailed kind (`種類詳細`, `rq.ids.item_kind`); a familiar is 20.
---@field rank? integer The kind rank (`種類ランク`). Default 10.
---@field icon? integer|string A game CG number or a file in the mod's folder (shown at 100×100, proportions kept). Default: a grey question mark (since 0.15.0).
---@field price? integer The price.
---@field level? integer The level needed to equip it (`装備ＬＶ`).
---@field rarity? integer The rarity (`レアランク`).
---@field classes? integer[] The classes that may equip it, 1 to 8. Default: all.
---@field characters? integer[] The characters that may equip it (`装備キャラ`).
---@field effects? rq.ItemEffect[] Up to five effects.
---@field info? string The description; `$` is a line break.
---@field locked? boolean Cannot be taken off (`外せません`).
---@field vanishes? boolean Vanishes (`消滅します`).
---@field on_create? fun(ii: integer) Runs when an instance is created.

---What `rq.item.get` returns.
---@class rq.ItemInfo
---@field ii integer The instance.
---@field no integer The item number (`固有番号`).
---@field id? string The text id, for an item a mod defined.
---@field name string The name.
---@field info string The description.
---@field type integer The slot, as the game numbers it (see `rq.ItemDef.type`).
---@field type_name string The slot's name.
---@field subtype integer The detailed kind.
---@field rank integer The kind rank.
---@field icon integer The icon CG.
---@field price integer The price.
---@field level integer The level needed to equip it.
---@field rarity integer The rarity.
---@field place integer 6 shop stock, 7 vault, 8 quest storage, or the owner's roster slot while equipped.
---@field locked integer Non-zero when it cannot be taken off.
---@field vanishes integer Non-zero when it vanishes.
---@field effects rq.ItemEffect[] The effects.

---Defines, gives and changes items.
---
---Items work like characters: define a template, then something provides it (a shop, a chest, a drop,
---a quest, a character that starts with it). The game keeps item instances (`ii`, up to 3000) that
---point to an item number (`no`). The calls take a number or a text id where they say `who`. The first
---call grows the game's restock array (`◆アイテム補充数`) so numbers above 348 are safe. Effects are the
---game's own codes; a mod that needs behavior of its own checks for its item with `rq.item.equipped`
---and does the work in Lua.
---
---A familiar is an item in the item slot with `subtype = 20`; its owner needs Familiar Knowledge
---(321). The game reads three effects by place: the monster number, the class (1 to 4) and the
---growth. Its icon must be 6500 to 6516 or 6632, or the familiar never takes a turn.
---
---Since: 0.8.0
---
---```lua
---local broken = rq.item.define({ id = "mymod:broken_lens", name = "Broken Lens", price = 10,
---  info = "Cracked. It no longer magnifies anything." })
---rq.item.define({ id = "mymod:lens", name = "Lens", price = 2000, icon = "icons/lens.png",
---  info = "Zoom out on a dungeon.$Breaks when the dungeon ends." })
---local ii = rq.item.give("mymod:lens", { to = rq.chara.find(1) })  -- equip it on Rance
---rq.item.transform(ii, "mymod:broken_lens")                       -- later, it breaks
---```
---@class rq.item
local item = {}

---Registers an item template.
---
---No instance exists until it is given or stocked.
---
---Raises an error for a text id whose number another id already has.
---
---Since: 0.8.0
---@param def rq.ItemDef The template.
---@return integer no The item number.
function item.define(def) end

---Creates an instance (`Ｉ生成`) and puts it in the vault, in quest storage, or on a character.
---
---Raises an error for an unknown item.
---
---Since: 0.8.0
---@param who string|integer An item number or text id.
---@param opts? {to?: integer, place?: "vault"|"quest"} `to`: a roster slot to equip it on. `place`: "vault" (default) or "quest" storage.
---@return integer|nil ii The new instance, or nil.
---@return string? err Why it failed, or that the character cannot equip it.
function item.give(who, opts) end

---Returns an instance's fields.
---
---Since: 0.8.0
---@param ii integer The instance.
---@return rq.ItemInfo|nil info A copy of the fields, or nil when there is no such instance.
function item.get(ii) end

---Writes an instance's fields.
---
---The fields are `name`, `info`, `effects`, `price`, `rarity`, `level`, `icon`, `rank`, `subtype`,
---`type`, `locked`, `vanishes` and `place`. After changing the effects of an equipped item, call
---`rq.chara.recalc` on its owner.
---
---Raises an error for an unknown field.
---
---Since: 0.8.0
---@param ii integer The instance.
---@param fields table The fields to write.
function item.set(ii, fields) end

---Turns an instance into another item, keeping its place and owner.
---
---Name, price, effects and description change, as for a "broken" version of an item.
---
---Since: 0.8.0
---@param ii integer The instance.
---@param who string|integer The new item number or text id.
---@return boolean ok false when it could not change.
function item.transform(ii, who) end

---Patches an item number, for every new instance and the ones in the save.
---
---The patch takes the same fields as `rq.item.set`. It applies to every new instance, and to the ones
---already in the save after each load, or on the next frame when called later (all fields but `place`
---and `no`; an equipped item recalculates its owner). The save then keeps those values, also when the
---mod is removed. Each mod keeps one patch per item.
---
---Since: 0.8.0
---Changed: 0.15.0 Reaches instances already in the save.
---@param who string|integer An item number or text id.
---@param patch table The fields to set.
function item.edit(who, patch) end

---Equips an existing instance on a character (`キャラ_装備設定`).
---
---The old item of that slot is replaced, and the character is recalculated.
---
---Since: 0.11.0
---@param ii integer The instance.
---@param iic integer The character's roster slot.
---@return boolean ok false when the character cannot wear it.
function item.equip(ii, iic) end

---Deletes an instance (`Ｉ削除`), taking it off its owner first.
---
---Since: 0.10.0
---@param ii integer The instance.
---@return boolean removed false when there is no such instance.
function item.remove(ii) end

---Returns the instances a character has equipped.
---
---Since: 0.8.0
---@param iic integer The character's roster slot.
---@return {weapon?: integer, armor?: integer, item?: integer, special?: integer} gear The instance in each slot; an empty slot is left out.
function item.equipped(iic) end

---Returns the item number of an instance.
---
---Since: 0.8.0
---@param ii integer The instance.
---@return integer|nil no The item number, or nil when there is no such instance.
function item.number(ii) end

---Returns the text id of a defined item number.
---
---Since: 0.8.0
---@param no integer The item number.
---@return string|nil id The text id, or nil when no mod defined that number.
function item.id_of(no) end

---Locks an item in the vault or the storage, or unlocks it, as the vault's LOCK and UNDO LOCK do
---(`アイテム_ＬＯＣＫ`, `アイテム_ＵＮＬＯＣＫ`).
---
---A locked item cannot be sold or donated. `rq.uikit` rows show the change at once.
---
---Raises an error when `ii` is not a whole number.
---
---Since: 2.9.0
---
---```lua
---rq.item.lock(ii)        -- lock
---rq.item.lock(ii, false) -- unlock
---```
---@param ii integer The instance.
---@param on? boolean false unlocks; default true.
---@return boolean done false when there is no such instance, it is not in the vault or the storage, or it already was.
function item.lock(ii, on) end

---Sells an item from the vault, the storage or a character's gear, as the vault's SELL does
---(`アイテム_売却`): the game asks "Sell ...?" first, and on Yes the item goes, the shop restocks it,
---and you get 100 gold for an item priced over 1000, else 10.
---
---The game's question waits inside the call: call it from a callback, not in a loop. While it asks,
---another `sell`, `donate` or `buy` returns false. With `ask = false` it sells at once, with the
---game's sound and the same steps. An item the game will not let go (a quest item) gets the game's
---"cannot be sold" notice, or false without asking. The money on the system bar updates.
---
---An item a character wears comes off first (`キャラ_装備設定` with 0), after the question. One the
---game will not take off gets its "cannot be removed" notice (or false without asking), and one
---that vanishes when taken off is refused. The game never sells worn gear itself; this is the
---vault's steps on an item taken off.
---
---Raises an error when `ii` is not a whole number, or `opts` is not a table.
---
---Since: 2.9.0
---Changed: 2.10.0 Items a character wears.
---
---```lua
---if rq.item.sell(ii, { ask = false }) then rq.log("sold", ii) end
---```
---@param ii integer The instance.
---@param opts? {ask?: boolean} `ask` (default true): the game's question first.
---@return boolean sold false when it was not sold: no such instance, not in the vault, the storage or a character's gear, locked, a quest item, one that cannot come off or vanishes when it does, No, or another question showing.
function item.sell(ii, opts) end

---Donates an item from the vault, the storage or a character's gear, as the vault's DONATE does
---(`アイテム_寄付`): the game asks "Donate ...?" first, and on Yes the item goes, the shop restocks
---it, and the donations count goes up by one.
---
---The game's question waits inside the call, as with `sell`. With `ask = false` it donates at once.
---An item the game will not let go gets its "cannot be donated" notice, or false without asking. An
---item a character wears comes off first, as with `sell`.
---
---Raises an error when `ii` is not a whole number, or `opts` is not a table.
---
---Since: 2.9.0
---Changed: 2.10.0 Items a character wears.
---
---```lua
---rq.item.donate(ii)
---```
---@param ii integer The instance.
---@param opts? {ask?: boolean} `ask` (default true): the game's question first.
---@return boolean donated false when it was not donated, as with `sell`.
function item.donate(ii, opts) end

---Moves an item between the vault and the storage, as dragging a row on the game's storage screen
---does (`アイテム_存在設定` 7 or 11).
---
---Raises an error when `ii` is not a whole number, or `where` is not "vault" or "storage".
---
---Since: 2.9.0
---
---```lua
---rq.item.store(ii, "storage")
---```
---@param ii integer The instance.
---@param where "vault"|"storage" Where it goes.
---@return boolean moved false when there is no such instance or it is in neither.
function item.store(ii, where) end

---Buys an item from the shop's stock, as the shop's drag of a row onto the vault or a party member
---does (`T_EquitItem::setItem`): the game asks "Purchase ...?" first, and on Yes the item goes into
---the vault, its price comes off the money, and the shop's spent total goes up by it.
---
---With `chara` the item is bought onto that character and equipped (`キャラ_装備設定`), as the drag
---onto a party member does: the character must be able to wear it ("Unable to equip."), and the
---slot's old item must come off, with the game's notice or questions for one that cannot or that
---vanishes. Short of money, the game says "Not enough money.". The notices show only when asking;
---with `ask = false` it buys at once and returns false where the game would have said no, and an
---old item that vanishes when taken off also stops it. The question waits inside the call, as with
---`sell`. The money on the system bar updates. Buying does not restock the shop.
---
---Raises an error when `ii` or `chara` is not a whole number, or `opts` is not a table.
---
---Since: 2.10.0
---
---```lua
---local ii = rq.uikit.item_picker({ x = 538, y = 0, id = "shop", where = "shop" })
---if ii and rq.item.buy(ii) then rq.log("bought", ii) end
---```
---@param ii integer The instance, in the shop's stock (place 6).
---@param opts? {ask?: boolean, chara?: integer} `ask` (default true): the game's question and notices. `chara`: a roster slot to buy onto.
---@return boolean bought false when it was not bought: not in the shop's stock, not enough money, a character who cannot wear it, an old item that cannot come off, No, or another question showing.
function item.buy(ii, opts) end

---Calls `fn` after any item instance is created (`Ｉ生成`), game or mod.
---
---It is not called for the temporary instance of `rq.item.info`, which never enters the game.
---
---Since: 0.8.0
---Changed: 0.23.0 Returns a remover.
---Changed: 2.20.0 Not called for `rq.item.info`'s lookup.
---@param fn fun(ctx: {ii: integer, no: integer, id?: string}) Gets the instance, its number, and its text id for a mod's item.
---@return fun() remove Removes this callback.
function item.on_create(fn) end

---What `rq.item.info` returns: an item number's row data, as `rq.uikit.item_data` gives it for an
---instance, without the fields that belong to one instance (`ii`, `locked`, `place`). The kit's item
---lists take it as a row.
---@class rq.ItemInfo
---@field no integer The item number.
---@field name string Its name.
---@field level string The level line, or "".
---@field info string The stats line, with the game's marks.
---@field icon integer Its icon CG.
---@field rank integer Its rarity rank.
---@field kind integer Its kind: 21 special, 1 weapon, 11 armor, 17 item.
---@field price integer Its price.
---@field discard boolean It can be thrown away.
---@field classes integer[] The class numbers that may equip it.

---Returns an item number's row data, with no instance of it in the game.
---
---Read as the game's own lookups by number read it (`Ｉ固有名前`): a temporary instance, read and
---deleted. The item's own definition runs (a mod item's `on_create` and `rq.item.edit` changes), but
---`rq.item.on_create` listeners do not see that instance. An item whose data the game makes from the
---moment (a few read Rance's level) gives the data of the moment. Kept for a second per number.
---
---Raises an error when `no` is not an integer.
---
---Since: 2.20.0
---
---```lua
---local it = rq.item.info(274)
---if it then rq.log(it.name, it.info) end
---```
---@param no integer The item number.
---@return rq.ItemInfo? info The data, or nil for 0 or a number the game does not have.
function item.info(no) end

---Small key and value storage for each mod and each save.
---
---It lives in `rqmod-store.json` in the active save folder, so it follows the `rq.saves` profile. Use
---it for facts such as "this character was already recruited". Values are numbers, strings and
---booleans. Each mod sees only its own keys. The file is written on every `set` and `delete`.
---
---Since: 0.4.0
---
---```lua
---if not rq.store.get("alice_joined", false) then
---  if rq.chara.recruit("mymod:alice") then rq.store.set("alice_joined", true) end
---end
---```
---@class rq.store
local store = {}

---Reads a key.
---
---Since: 0.4.0
---@param key string The key.
---@param default? any What to return when the key is not set.
---@return number|string|boolean|nil value The value, or `default`.
function store.get(key, default) end

---Writes a key.
---
---Raises an error for a value that is not a number, string, boolean or nil, or when the file cannot be
---opened.
---
---Since: 0.4.0
---@param key string The key.
---@param value number|string|boolean|nil The value; nil deletes the key.
function store.set(key, value) end

---Deletes a key.
---
---Since: 0.4.0
---@param key string The key.
function store.delete(key) end

---Lists this mod's keys.
---
---Since: 0.4.0
---@return string[] keys The keys.
function store.keys() end

---The same calls as `rq.store`, in one file for every save profile.
---
---The file is `rqmod-global.json` in the base profile's save folder. Use it for settings, such as what
---a mod's Config page changes. Its keys are apart from `rq.store`'s.
---
---Since: 1.8.0
---
---```lua
---local on = rq.store.global.get("on", true)
---rq.store.global.set("on", not on)
---```
---@class rq.store_global
---@field get fun(key: string, default?: any): number|string|boolean|nil Reads a key, or returns `default`.
---@field set fun(key: string, value: number|string|boolean|nil) Writes a key; nil deletes it.
---@field delete fun(key: string) Deletes a key.
---@field keys fun(): string[] Lists this mod's keys.
store.global = {}

---What `rq.combat.on_physical` gets for one physical hit.
---@class rq.PhysicalCtx
---@field attacker integer The attacker's roster slot.
---@field defender integer The defender's roster slot.
---@field skill integer The skill number.
---@field attack integer The attack before defense, as the callback before left it.
---@field magic integer The attacker's magic.
---@field race integer The defender's race (`rq.ids.race`); 8 is a hanny.
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
---The returned numbers of every callback add up; nil counts as 0. The game's own limits come after
---them: the chance is at most 100; an ally's attack hits at least 20% of the time; an enemy's at
---least 40% (20% or 60% by the target's class, more after the target dodged several times in a
---row) and at most 80% to 100% by the target's evasion.
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

---What `rq.combat.on_magic` gets for one spell.
---
---Since: 2.1.0
---@class rq.MagicCtx
---@field attacker integer The attacker's roster slot.
---@field defender integer The defender's roster slot.
---@field skill integer The skill number.
---@field damage integer The final damage the game worked out, before any percent.
---@field magic integer The attacker's magic.
---@field race integer The defender's race (`rq.ids.race`); 8 is a hanny.
---@field defender_type integer The defender's type (`Ｃタイプ`); 22 is humanoid.

---Calls `fn` for each magic hit, to change its final damage by a percent.
---
---`fn` returns a percent of the final damage (100 is unchanged, 50 half, 0 none) or nil to keep it.
---The percents of every callback multiply, and the damage is rounded down. It runs after the game's
---last damage step (`最終ダメージ計算`), so the barrier, immunities and the game's magic resistance
---are already in `damage`; a spell that does 0 is not reported. The battle forecast is not changed.
---It is the magic twin of the final percent of `rq.combat.on_physical`.
---
---Raises an error, logged, for a return that is not a whole number of 0 or more.
---
---Since: 2.1.0
---
---```lua
---rq.combat.on_magic(function(ctx)
---  if weakened[ctx.attacker] then return 70 end  -- a weakened caster does 70%
---end)
---```
---@param fn fun(ctx: rq.MagicCtx): integer|nil Returns the percent, or nil.
---@return fun() remove Removes this callback.
---@see rq.combat.on_physical
function combat.on_magic(fn) end

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

---Adds stock to Frostbuy, the item shop, and reports purchases.
---
---The game refills the shop's stock in `Ｔアイテム屋` every time the shop opens. `rq.shop.install` wraps
---the end of that function so `on_stock` callbacks run right after the game's own refill. Without a
---hook the shop keeps only the game's stock. The calls take an item number or a text id.
---
---Since: 0.2.0
---
---```lua
---rq.shop.install()
---rq.shop.on_stock(function()
---  if not rq.shop.in_stock(306) then rq.shop.stock(306) end -- Skill Again
---end)
---```
---@class rq.shop
local shop = {}

---Wraps the end of `Ｔアイテム屋` so `on_stock` callbacks run and `stock` works.
---
---Calling it again is harmless.
---
---Since: 0.2.0
---@return boolean ok true when the hook is in place; false when a game function is missing (see the log).
function shop.install() end

---Registers a function that runs after the game refills the shop's stock.
---
---It runs on every shop open, once `rq.shop.install` was called; before that it never runs. Up to 16
---callbacks. An error in a callback is written to the log and the others still run.
---
---Since: 0.2.0
---Changed: 0.9.0 Keeps every mod's callback (before, the last one replaced the others).
---Changed: 0.23.0 Returns a remover.
---@param fn fun() Adds stock, usually with `rq.shop.stock`.
---@return fun() remove Removes this callback.
---@see rq.shop.stock
function shop.on_stock(fn) end

---What an `rq.shop.on_buy` callback gets.
---@class rq.BuyCtx
---@field ii integer The item instance that was bought.
---@field no integer Its item number.
---@field id? string Its text id, when a mod defined the item.
---@field price integer The price paid, in gold.
---@field name string The item's name.

---Registers a function that runs after each Frostbuy purchase.
---
---It wraps the end of `T_Shop@buyItem` the first time it is used, so it needs no `install`. Use it to
---count purchases, and raise the next price in `on_stock`. Up to 16 callbacks; an error in one is
---written to the log.
---
---Since: 0.9.0
---Changed: 0.23.0 Returns a remover.
---
---```lua
---rq.shop.on_buy(function(ctx)
---  rq.log("bought", ctx.name, "for", ctx.price)
---end)
---```
---@param fn fun(ctx: rq.BuyCtx) Gets the purchase.
---@return fun() remove Removes this callback.
function shop.on_buy(fn) end

---Returns whether an item is already on sale in the shop.
---
---It asks the game whether an instance of that item has the shop's existence value, 6.
---
---Since: 0.2.0
---Changed: 0.9.0 Accepts a text id.
---@param no integer|string An item number or text id.
---@return boolean stocked true when the shop already sells it.
function shop.in_stock(no) end

---Puts one item on sale, through the game's `Ｔアイテム屋／入荷品`.
---
---Raises an error before `rq.shop.install`, for an unknown item, or for a value that is not a number
---or a text id.
---
---Since: 0.2.0
---Changed: 0.9.0 Accepts a text id.
---Changed: 0.26.0 An unknown item raises an error instead of closing the game.
---@param no integer|string An item number or text id.
---@return integer ii The new instance, or 0 when the shop already had the item or the game refused it.
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

---What `rq.battle.on_turn` gets: the character whose turn starts, and what can change it.
---
---Since: 2.1.0
---@class rq.BattleTurnCtx
---@field iic integer The character's roster slot.
---@field unit integer Its battle number: 1 to 5 allies, 11 to 15 enemies.
---@field side "ally"|"enemy"|"summon" Whose turn it is.
---@field turn integer The fight's action count (`◆戦闘ターン`), which every actor's turn raises.
---@field passing boolean true when this turn passes: the turn a stun ends, or after `ctx:skip()`.
---@field downed boolean true once a lethal `ctx:damage` brought the HP to 0 in this turn start. Since 2.3.0.
local BattleTurnCtx = {}

---Takes HP at this turn start and shows it as the game's damage number.
---
---The HP stops at 1, as with the game's poison, unless `opts.lethal` is true. Damage from several
---callbacks, and the game's poison, show as one number.
---
---**Lethal.** With `lethal`, the HP may reach 0. Then the character goes down, as the game shows
---a death: the enemy fades or the ally's card turns red, and `rq.battle.on_down` runs. The turn
---passes (`ctx.downed` and `ctx.passing` are true), and later `ctx:damage` and `ctx:heal` calls in
---this turn start do nothing. The dying character's skills that act at a death run: Hanny Zippo
---brings it back at full HP once a fight, and Rich Enemy (438) doubles the gold. Kalar's curse
---needs a killer: with a `source` that is in the fight and not down, the game's own death
---handling (`死亡計算`) runs with that killer. When the action ends, the game checks for the end
---of the fight, as after an attack: the last enemy's death wins it and the last ally's loses it.
---A familiar never goes down this way: its damage stops at 1 HP. Skills that save a character
---from a hit (Persevere) do not act, as with poison.
---
---Raises an error for a number that is not whole or is below 0, or an option of the wrong type.
---
---Since: 2.1.0
---Changed: 2.3.0 `opts`, with `lethal` and `source`.
---
---```lua
---rq.battle.on_turn(function(ctx)
---  if doomed[ctx.iic] then
---    ctx:damage(999999, { lethal = true, source = doomed[ctx.iic] })
---    ctx:log(" " .. rq.chara.get(ctx.iic).short .. "'s time is up.")
---  end
---end)
---```
---@param n integer The HP to take, 0 or more.
---@param opts? {lethal?: boolean, source?: integer} `lethal`: may bring the HP to 0, default false (in an `rq.status` state's `on_turn`, the definition's `lethal`). `source`: the killer's roster slot, default 0 (in a state's `on_turn`, the state's `source`).
---@return integer taken The HP taken, after the 1 HP floor when not lethal.
function BattleTurnCtx:damage(n, opts) end

---Gives HP at this turn start, with the game's regeneration effect and number.
---
---The HP stops at the most HP. A downed character gets nothing, nor one a lethal `ctx:damage`
---downed in this turn start.
---
---Raises an error for a number that is not whole or is below 0.
---
---Since: 2.1.0
---@param n integer The HP to give, 0 or more.
---@return integer healed The HP given, after the cap.
function BattleTurnCtx:heal(n) end

---Makes this turn pass, as a stun does: the character uses no skill.
---
---Since: 2.1.0
function BattleTurnCtx:skip() end

---Adds text to the battle log line of this turn start.
---
---The line is empty on most turns, so write a whole sentence, starting with a space as the game's
---lines do. Text from several callbacks goes on one line, in the order they ran.
---
---Raises an error for a value that is not a string.
---
---Since: 2.1.0
---@param text string The text to add.
function BattleTurnCtx:log(text) end

---Calls `fn` at the start of every turn in a fight, for allies, enemies and familiars.
---
---It runs after the game's own start of a turn (`Ｔ行動開始`): the stun has ended, the poison and
---regeneration have been done, and nothing is shown yet. Damage and healing from `ctx` then show
---with the game's own number and effect, as poison and regeneration do. It also runs on the turn a
---stun ends, and for a petrified Kaloria, with `ctx.passing` true. A familiar's turn has no card,
---so its damage and healing are not shown. Callbacks run in load order; an error in one is logged
---and the next still runs.
---
---This is where a mod state does its work each turn: a burn's damage, a regeneration, a freeze
---that skips the turn. A lethal `ctx:damage` may down the character (see `BattleTurnCtx:damage`).
---
---Since: 2.1.0
---Changed: 2.3.0 Lethal damage, and `ctx.downed`.
---
---```lua
---local burning = {}  -- iic -> turns left
---rq.battle.on_turn(function(ctx)
---  local left = burning[ctx.iic]
---  if not left then return end
---  local hp_max = rq.chara.get(ctx.iic).hp_max
---  ctx:damage(math.max(1, hp_max // 20))
---  ctx:log(" " .. rq.chara.get(ctx.iic).short .. " is burning.")
---  burning[ctx.iic] = left > 1 and left - 1 or nil
---end)
---```
---@param fn fun(ctx: rq.BattleTurnCtx) Gets the turn.
---@return fun() remove Removes this callback.
---@see rq.battle.on_hit
function battle.on_turn(fn) end

---What `rq.battle.on_hit` gets for one hit.
---
---Since: 2.1.0
---@class rq.BattleHitCtx
---@field attacker integer The attacker's roster slot.
---@field defender integer The defender's roster slot.
---@field skill integer The skill number.
---@field kind "physical"|"magic" The kind of attack.
---@field damage integer The HP the hit took; 0 for a miss, a barrier or an immune target.
---@field missed boolean The hit missed (`◆攻撃外れた`).
---@field crit boolean The hit was a critical.
---@field downed boolean The defender is down after the hit.
---@field counter boolean The hit is a counter.
local BattleHitCtx = {}

---Adds text to this hit's battle log line, after the game's own (" X took 120 damage. (Stun)").
---
---Start it with a space, as the game's suffixes do: `" (Burn)"`.
---
---Raises an error for a value that is not a string.
---
---Since: 2.1.0
---@param text string The text to add.
function BattleHitCtx:log(text) end

---Calls `fn` after each physical or magic attack on a target, with its result.
---
---It runs once per target and per hit: each hit of a multi-hit skill, each target of a skill on
---many, and a counter, for allies, enemies and familiars. A miss is reported too, with `missed`
---true. The damage is already taken from the HP, and the hit is not shown yet. The turn a skill
---only starts to charge is not a hit and is not reported. Special and support skills are not
---attacks; use `rq.battle.on_support` or `rq.battle.on_use` for them.
---
---This is where a hit gives a mod state.
---
---Since: 2.1.0
---
---```lua
---rq.battle.on_hit(function(ctx)
---  if ctx.skill == FIRE_SLASH and not ctx.missed and not ctx.downed and math.random(100) <= 40 then
---    burning[ctx.defender] = 3
---    ctx:log(" (Burn)")
---  end
---end)
---```
---@param fn fun(ctx: rq.BattleHitCtx) Gets the hit.
---@return fun() remove Removes this callback.
---@see rq.battle.on_turn
---@see rq.battle.on_use
function battle.on_hit(fn) end

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
---@param opts {enemies: integer[], exp?: integer} `enemies`: up to five monster numbers (`rq.ids.monster`) in rank order, 0 for an empty rank. `exp`: experience for the win. Default 0.
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

---A skill as `rq.units()` lists it.
---@class rq.SkillBrief
---@field id integer The skill number.
---@field name string The skill's name.
---@field rank? integer Its rank; only in `learned` and `potential`.
---@field cost? integer Its skill-point cost; only in `learned` and `potential`.

---A recruited character, from `rq.units()`.
---@class rq.Unit
---@field id integer The roster slot (iic), the number the `rq.chara` calls take.
---@field name string The name.
---@field class string The class's name.
---@field classId integer The class number.
---@field level integer The level.
---@field stats {hp: integer, hpMax: integer, atk: integer, def: integer, mag: integer, mdef: integer, hit: integer, eva: integer, crit: integer, impact: integer, resist: integer, spd: integer} The current stats, as the game stores them.
---@field equipment {special: integer, weapon: integer, armor: integer, item: integer} The item instance in each slot, 0 when it is empty.
---@field active rq.SkillBrief[] The skills set for battle, up to 8.
---@field learned rq.SkillBrief[] The skills the character owns, up to 40.
---@field potential rq.SkillBrief[] The skills the character can still learn, up to 40.

---An owned item instance, from `rq.items()`.
---@class rq.Item
---@field id integer The instance (ii).
---@field name string The name.
---@field type string "weapon", "armor", "special" or "item".
---@field typeId integer The game's item kind: 1 weapon, 11 armor, 21 special; other values are items.
---@field class string The name of the first class that can equip it.
---@field classId integer That class's number, 0 when none is set.
---@field level integer The level needed to equip it.
---@field description string The item's text.

---A quest, from `rq.quests()`.
---@class rq.Quest
---@field id integer The quest number (its index in the game's `Ｑ` array).
---@field name string The name.
---@field description string The quest's text.
---@field map string Where it takes place (`発生場所`).

---A castle room, from `rq.castle()`.
---@class rq.CastleRoom
---@field id integer The room's index.
---@field name string The name.
---@field state integer The game's raw state; above 0 means open.
---@field enabled boolean `state` is above 0.
---@field used boolean The room was already used.
---@field units {id: integer, name: string}[] Always empty for now; the castle's characters are in `rq.Castle.units`.

---The castle's state, from `rq.castle()`.
---@class rq.Castle
---@field unlocked boolean The castle is open.
---@field enabled boolean Open, with a room open (or no rooms listed).
---@field used boolean A room was already used.
---@field open integer The game's raw castle-open flag.
---@field rooms rq.CastleRoom[] The rooms that have a name, an open state or a use, up to 32.
---@field units {id: integer, name: string}[] The characters on the castle roster, up to 32.

---Where the party is in a dungeon, from `rq.dungeon.info()`.
---@class rq.DungeonInfo
---@field roomId integer The map number.
---@field roomName string The map's name.
---@field mapX integer The player's cell, x.
---@field mapZ integer The player's cell, z.
---@field cell {x: integer, z: integer} The same cell as `mapX` and `mapZ`.
---@field party {slot: integer, id: integer, name: string}[] The dungeon party: party slot (1 to 6), roster slot and name.

---What `rq.battle.snapshot` returns: a copy of the fight screen.
---@class rq.BattleSnap
---@field up boolean Ally skill select is open.
---@field allies {id: integer, no: integer, name: string}[] Each ally: `id` the roster slot, `no` the battle number, `name`.
---@field enemies {id: integer, no: integer, name: string}[] Each enemy, as for `allies`.
---@field skills {id: integer, name: string, uses: integer, enabled: boolean}[] The acting ally's skills: number, name, uses left, and whether the button can be clicked.
---@field log string[] The battle log lines, oldest first.

---Lists the quests and follows them: clears, endings, quest moments and state.
---
---`rq.quests()` and `rq.quests.list()` list the quests that have a name, up to 512. `state`,
---`set_state`, `unlock` and `start` raise an error for a quest number the game does not have. Characters recruited and quests unlocked
---from `on_clear` show on the victory screen with the game's own animation, as the game does for
---Tomato. Dialogue inside a quest is quest script and is not part of these hooks.
---
---Since: 0.2.0
---
---```lua
---rq.chara.define{ id = "mymod:alice", name = "Alice", face = "faces/alice.png", class = 5 }
---rq.quests.on_clear(function(ctx)
---  if ctx.quest == 126 and ctx.first then
---    rq.chara.recruit("mymod:alice", { state = 2 }) -- shown on the victory screen
---    rq.quests.unlock(207)
---  end
---end)
---```
---@class rq.QuestsApi
---@overload fun(): rq.Quest[]
---@field list fun(): rq.Quest[] Lists the quests, as `rq.quests()` does.
local quests = {}

---What an `rq.quests.on_clear` callback gets.
---@class rq.QuestClearCtx
---@field quest integer The quest that was just cleared.
---@field clears integer How many times it was cleared, this clear included.
---@field first boolean This is its first clear.

---Registers a function that runs when a quest is cleared.
---
---It runs only for a clean clear, not for an abandon, an escape or a defeat, at the end of the game's
---`クエスト終了処理／クリアフラグ` (wrapped the first time this is used). Characters recruited with
---`rq.chara.recruit` and quests opened with `rq.quests.unlock` in the callback join the game's "new
---this run" lists, so the victory screen shows them. Up to 16 callbacks; an error in one is written to
---the log.
---
---Since: 0.7.0
---Changed: 0.23.0 Returns a remover.
---@param fn fun(ctx: rq.QuestClearCtx) Gets the cleared quest.
---@return fun() remove Removes this callback.
---@see rq.quests.on_end
function quests.on_clear(fn) end

---Returns a quest's state.
---
---Since: 0.7.0
---@param no integer The quest number.
---@return integer state 0 locked, 1 available, 2 cleared.
function quests.state(no) end

---Sets a quest's state, through the game's `Ｑ状態`.
---
---State 2 also records a first clear. Raises an error for a state other than 0, 1 or 2.
---
---Since: 0.7.0
---@param no integer The quest number.
---@param state integer 0 locked, 1 available, 2 cleared.
---@see rq.quests.unlock
function quests.set_state(no, state) end

---Makes a locked quest available.
---
---It sets state 1 only when the quest is locked. A quest opened this way joins the "new quests" list
---on the victory screen.
---
---Since: 0.7.0
---@param no integer The quest number.
---@return boolean unlocked false when the quest was not locked.
function quests.unlock(no) end

---What an `rq.quests.on_end` callback gets.
---@class rq.QuestEndCtx
---@field quest integer The quest that ends.
---@field result integer How it ends: 1 clean clear, 2 party defeat, 3 abandoned, 4 escape device (abandoned, items kept).
---@field cleared boolean `result` is 1.
---@field party integer[] The dungeon party's roster slots (iic).

---Registers a function that runs when a quest ends, however it ends.
---
---It runs at the start of the game's `クエスト終了処理` (wrapped the first time this is used). Up to 16
---callbacks; an error in one is written to the log.
---
---Since: 0.9.0
---Changed: 0.23.0 Returns a remover.
---
---```lua
---rq.quests.on_end(function(ctx)
---  if not ctx.cleared then rq.log("quest", ctx.quest, "ended with", ctx.result) end
---end)
---```
---@param fn fun(ctx: rq.QuestEndCtx) Gets the quest and how it ended.
---@return fun() remove Removes this callback.
---@see rq.quests.on_clear
function quests.on_end(fn) end

---Returns the quest being played.
---
---Since: 0.18.0
---@return integer no The quest number, or 0 in town.
function quests.current() end

---Starts a quest through the game's own flow, with no click on the quest list.
---
---It returns at once; the game moves on its next update, and `rq.quests.current()` gives the number
---once the quest started. Any join dialog or scene of the quest plays as usual, and like a real start
---it autosaves and counts as a try. A locked quest starts too, including the game's debug entries
---(quest 10 goes straight back to town).
---
---Raises an error for a value that is not a whole number, an unknown quest, a quest this party cannot
---take, while a quest runs or another is starting, and on any screen but the hub or the quest list.
---The game can still refuse after that with its own message, at 2500 items or more or when its Alice
---event is due.
---
---Since: 0.23.0
---
---```lua
---if rq.ui.get().screen == "hub" then rq.quests.start(126) end
---```
---@param no integer The quest number.
function quests.start(no) end

---What `rq.quests.on_event` and `on_event_after` callbacks get.
---
---`code` 8 is `start` and 7 is `map_load`; otherwise `fg` names the kind: 4 `map_event`, 5 `warp`,
---6 `warp_cancel`, 7 `before_battle`, 8 `after_battle`, 9 `chest`, 10 `contact`, 11 `before_warp`;
---anything else is `other`.
---@class rq.QuestEvent
---@field quest integer The quest being played.
---@field kind "start"|"map_load"|"map_event"|"warp"|"warp_cancel"|"before_warp"|"before_battle"|"after_battle"|"chest"|"contact"|"other" The quest moment.
---@field no integer The map event number, or the map number for `map_load`.
---@field code integer The first argument of the game's `クエスト処理`.
---@field fg integer The third argument of `クエスト処理`.
---@field result? integer What the game's handling returned; only in `on_event_after`.

---Registers a function that runs before the game handles a quest moment.
---
---It hooks the game's `クエスト処理`. Return nothing to let the game handle it, or a number to skip the
---game's handling: 0 continues the quest, other values end it (1 returns to town). Do not play a scene
---on `map_load`; the map is not ready.
---
---Since: 0.18.0
---Changed: 0.23.0 Returns a remover.
---
---```lua
---rq.quests.on_event(function(ev)
---  if ev.kind == "chest" then rq.log("chest at event", ev.no) end
---end)
---```
---@param fn fun(ev: rq.QuestEvent): integer|nil Returns nil, or the result that replaces the game's handling.
---@return fun() remove Removes this callback.
---@see rq.quests.on_event_after
function quests.on_event(fn) end

---Registers a function that runs after the game handled a quest moment.
---
---`ev.result` is what the game returned. Return a number to change it, or nothing to keep it.
---
---Since: 0.18.0
---Changed: 0.23.0 Returns a remover.
---@param fn fun(ev: rq.QuestEvent): integer|nil Returns nil, or the result to use instead.
---@return fun() remove Removes this callback.
---@see rq.quests.on_event
function quests.on_event_after(fn) end

---What `rq.quests.new_this_run` returns.
---@class rq.NewThisRun
---@field characters integer[] The roster slots (iic) of characters added during the quest, up to 5.
---@field quests integer[] The quests unlocked during the quest, up to 5.

---Returns what the victory screen will show as new.
---
---Recruiting a character and unlocking a quest add to these lists; the game empties both when a quest
---ends.
---
---Since: 0.7.0
---@return rq.NewThisRun new The new characters and quests.
function quests.new_this_run() end

---Plays scenes with the game's own commands, so a mod can do anything a game scene does.
---
---The commands block until they end. Run them inside `rq.scene.play` (or `once`, `start`), from a
---callback the game runs: a quest event, a hook, or `rq.on_frame` in town. From anywhere else, such
---as every frame, use `rq.scene.start`. Not at the top level of a mod: the game is not running yet.
---
---`fn` gets the namespace itself as `s`, so `s.say(...)` and `rq.scene.say(...)` are the same. A CG
---number from `rq.image.add` works in every layer and as a face; it is drawn at its own size from the
---top-left, so draw a portrait on a full 1024×768 canvas with a transparent background. Times are in
---milliseconds. `effect` is the game's transition number; 1 is the default.
---
---Each line of text has its own read flag, by mod and text, so the game's skip mode passes only lines
---the player has read. Every line passes through the AIN function `message`, so a hook on it can read
---or rewrite any line of the game (`h.args[3]`), as a translation would.
---
---When the player goes to the title during a scene (Config has Return to title), every Lua call that
---waits in the game ends with the error "rq: the game went to the title", and `rq.on_reset("title")`
---follows.
---
---Since: 0.18.0
---Changed: 1.3.1 Each line has its own read flag (before, one read line marked every mod line read).
---Changed: 1.13.0 Going to the title ends the scene with an error.
---
---```lua
---rq.quests.on_event(function(ev)
---  if ev.kind == "start" and ev.quest == 126 then
---    rq.scene.once("my_mod_intro", function(s)
---      s.set(0, 5240, 0, 0)  -- a background
---      s.music(72)
---      s.portrait(1020)
---      s.say(2020, "First line.\nSecond line.")
---      s.think(2020, "A thought.")
---      s.narrate("Narration.")
---      s.say(2020, "Pick one.", { wait = false })
---      local pick = s.choice({ "Yes", "-", { id = 42, text = "No" } })
---      s.clear_text()
---      s.say(2020, pick == 42 and "Too bad." or "Good.")
---    end)
---  end
---end)
---```
---@class rq.scene
local scene = {}

---Any game command by its name, called as `rq.ain.call` would.
---
---Since: 0.18.0
---
---```lua
---rq.scene.cmd["地図点滅"](1, "", 0, 0)
---```
---@type table<string, fun(...): any>
scene.cmd = {}

---How `rq.scene.say`, `think` and `narrate` show a line.
---@class rq.SceneTextOpts
---@field name? string The name plate. Default: the face's registered name.
---@field face? boolean false hides the face. Default true.
---@field page? boolean Uses the game's E variant of the frame.
---@field paper? boolean `narrate` only: the paper frame.
---@field wait? boolean false shows the text without waiting for a click, as for a question above a choice. Default true.

---Opens a scene, runs `fn(s, ...)`, then clears the text and closes it.
---
---An error in `fn` is logged and the scene still closes; then it returns nothing. Its end runs the
---game's `ADVOFF`, which clears the scene layer, so inside a game scene (from a hook) that scene's
---background and pictures go too; `rq.scene.start` waits for a game scene to end instead.
---
---Raises the error "rq: the game went to the title" again when the player went to the title.
---
---Since: 0.18.0
---@param fn fun(s: rq.scene, ...): any The scene.
---@param ... any Passed on to `fn`.
---@return any ... What `fn` returns.
function scene.play(fn, ...) end

---Plays a scene once per save.
---
---It marks `id` seen with `rq.scene.seen` before it plays.
---
---Since: 0.18.0
---@param id string The event flag that remembers the scene.
---@param fn fun(s: rq.scene, ...) The scene.
---@param ... any Passed on to `fn`.
---@return boolean played false when it was already seen.
function scene.once(id, fn, ...) end

---Plays a scene at the next frame, in a coroutine of its own, and returns at once.
---
---It returns false, and queues nothing, while a mod scene plays or waits to, so a mod can call it from
---anywhere, also every frame. It waits while the System menu or Config is open, and while a scene of
---the game's own is on screen. The scene draws over the current screen, takes the clicks, and leaves
---the screen as it was. An error that ends it is logged.
---
---Raises an error when `fn` is not a function.
---
---Since: 1.14.0
---
---```lua
---rq.on_frame(function()
---  if want_talk then
---    if rq.scene.start(function(s) s.say(2020, "Hello.") end) then want_talk = false end
---  end
---end)
---```
---@param fn fun(s: rq.scene, ...) The scene.
---@param ... any Passed on to `fn`.
---@return boolean started false when a mod scene plays or waits to.
---@see rq.scene.active
function scene.start(fn, ...) end

---Returns whether a mod scene plays, or `rq.scene.start` waits to play one.
---
---Since: 1.14.0
---@return boolean active true during `play`, `once` and `start`.
function scene.active() end

---Reads or sets one of the game's event flags.
---
---The game's own scene names work too.
---
---Since: 0.18.0
---@param id string The flag's name.
---@param set? boolean true or false writes the flag; leave it out to read.
---@return boolean seen The flag.
function scene.seen(id, set) end

---Shows a character's line and waits for a click.
---
---A newline in `text` breaks the line.
---
---Since: 0.18.0
---@param face integer The face CG number.
---@param text string The line.
---@param opts? rq.SceneTextOpts Name plate, frame and wait.
function scene.say(face, text, opts) end

---Shows a character's thought and waits for a click.
---
---Since: 0.18.0
---@param face integer The face CG number.
---@param text string The thought.
---@param opts? rq.SceneTextOpts Name plate, frame and wait.
function scene.think(face, text, opts) end

---Shows narration and waits for a click.
---
---Since: 0.18.0
---@param text string The narration.
---@param opts? rq.SceneTextOpts Frame and wait.
function scene.narrate(text, opts) end

---Adds text to the open frame without waiting.
---
---With `br`, `wait`, `show`, `clear_text` and `speed` it builds a line step by step.
---
---Since: 0.18.0
---@param text string The text.
function scene.text(text) end

---Breaks the line in the open frame (`R`).
---
---Since: 0.18.0
function scene.br() end

---Waits for a click, then clears the page (`A`).
---
---Since: 0.18.0
function scene.wait() end

---Shows the text in the frame without waiting (`B`).
---
---Since: 0.18.0
function scene.show() end

---Clears the message frame (`枠消去`).
---
---Since: 0.18.0
function scene.clear_text() end

---Sets how fast text appears (`字速度`).
---
---Since: 0.18.0
---@param ms integer Time per character: 0 shows it at once, -1 uses the player's setting.
function scene.speed(ms) end

---Opens the scene mode (`ADVON`). `rq.scene.play` calls it for you.
---
---Since: 0.18.0
function scene.begin() end

---Clears the text and closes the scene mode (`枠消去`, `ADVOFF`). `rq.scene.play` calls it for you.
---
---Since: 0.18.0
function scene.finish() end

---Shows a wallpaper, the layer at the back (`壁紙表示`).
---
---Since: 0.18.0
---@param cg integer The CG number.
---@param effect? integer The transition. Default 1.
---@param time? integer The transition time, in ms. Default 500.
function scene.wallpaper(cg, effect, time) end

---Shows a masked wallpaper (`マスク壁紙表示`).
---
---Since: 0.18.0
---@param cg integer The CG number.
---@param effect? integer The transition. Default 1.
---@param time? integer The transition time, in ms. Default 500.
function scene.mask_wallpaper(cg, effect, time) end

---Shows a background (`背景表示`).
---
---Since: 0.18.0
---@param cg integer The CG number.
---@param effect? integer The transition. Default 1.
---@param time? integer The transition time, in ms. Default 500.
---@param name? string The game's `szName` argument. Default "".
---@param flag? boolean The game's `bFlag` argument. Default false.
function scene.background(cg, effect, time, name, flag) end

---Brings a background in (`背景イン`).
---
---Since: 0.18.0
---@param cg integer The CG number.
---@param time? integer The transition time, in ms. Default 500.
---@param name? string The game's `szName` argument. Default "".
---@param flag? boolean The game's `bFlag` argument. Default false.
function scene.background_in(cg, time, name, flag) end

---Takes the background out (`背景アウト`).
---
---Since: 0.18.0
---@param time? integer The transition time, in ms. Default 500.
function scene.background_out(time) end

---Shows a standing picture (`立ち絵表示`).
---
---Since: 0.18.0
---@param cg integer The CG number.
---@param effect? integer The transition. Default 1.
---@param time? integer The transition time, in ms. Default 200.
function scene.portrait(cg, effect, time) end

---Shows a small event picture (`ミニイベント表示`).
---
---Since: 0.18.0
---@param cg integer The CG number.
---@param effect? integer The transition. Default 1.
---@param time? integer The transition time, in ms. Default 200.
function scene.mini(cg, effect, time) end

---Shows an event CG over the screen (`イベントＣＧ表示`).
---
---Since: 0.18.0
---@param cg integer The CG number.
---@param effect? integer The transition. Default 1.
---@param time? integer The transition time, in ms. Default 500.
function scene.cg(cg, effect, time) end

---Shows several layers at once (`セット表示`).
---
---Since: 0.18.0
---@param wallpaper? integer The wallpaper CG; 0 leaves the layer empty. Default 0.
---@param background? integer The background CG; 0 leaves it empty. Default 0.
---@param portrait? integer The standing picture CG; 0 leaves it empty. Default 0.
---@param mini? integer The small event picture CG; 0 leaves it empty. Default 0.
function scene.set(wallpaper, background, portrait, mini) end

---Shows several layers at once with one transition (`エフェクトセット表示`).
---
---Since: 0.18.0
---@param effect? integer The transition. Default 1.
---@param wallpaper? integer The wallpaper CG; 0 leaves the layer empty. Default 0.
---@param background? integer The background CG. Default 0.
---@param portrait? integer The standing picture CG. Default 0.
---@param mini? integer The small event picture CG. Default 0.
---@param event_cg? integer The event CG. Default 0.
function scene.effect_set(effect, wallpaper, background, portrait, mini, event_cg) end

---Fades the screen to black (`黒画面`).
---
---Since: 0.18.0
---@param effect? integer The transition. Default 1.
---@param time? integer The transition time, in ms. Default 500.
function scene.black(effect, time) end

---Fades the screen to white (`白画面`).
---
---Since: 0.18.0
---@param effect? integer The transition. Default 1.
---@param time? integer The transition time, in ms. Default 500.
function scene.white(effect, time) end

---Flashes the screen (`画面フラッシュ`).
---
---Since: 0.18.0
---@param time? integer How long, in ms. Default 200.
function scene.flash(time) end

---Shakes the whole screen (`全画面揺れ`).
---
---Since: 0.18.0
---@param mode? integer The game's shake mode (`nMode`). Default 1.
---@param flag? boolean The game's `bFlag` argument. Default false.
function scene.shake(mode, flag) end

---Shakes the face in the message frame (`顔揺れ`).
---
---Since: 0.18.0
---@param mode? integer The game's shake mode (`nMode`). Default 1.
---@param flag? boolean The game's `bFlag` argument. Default false.
function scene.shake_face(mode, flag) end

---Shakes the standing picture (`立ち絵揺れ`).
---
---Since: 0.18.0
---@param mode? integer The game's shake mode (`nMode`). Default 1.
---@param flag? boolean The game's `bFlag` argument. Default false.
function scene.shake_portrait(mode, flag) end

---Plays a game music track (`音楽`).
---
---Since: 0.18.0
---@param no integer The music number.
---@param effect? integer The fade type; -1 is the game's default. Default -1.
---@param time? integer The fade time; -1 is the game's default. Default -1.
function scene.music(no, effect, time) end

---Plays a game sound effect by number (`効果音`).
---
---`rq.sound.play` also plays files and returns a handle.
---
---Since: 0.18.0
---@param no integer The sound number.
function scene.sound(no) end

---Plays a jingle (`Ｒ８ジングル`).
---
---Since: 0.18.0
---@param no integer The sound number.
---@param wait? boolean true waits for it to end. Default false.
function scene.jingle(no, wait) end

---Clears the scene's pictures (`画面消去`).
---
---Since: 0.18.0
function scene.clear() end

---Waits for a click (`キー待ち_クリック`).
---
---Since: 0.18.0
---@return any result What the game's command returns.
function scene.wait_click() end

---Sets the name plate for one of the game's face numbers (`顔登録`).
---
---A mod's own face passes `opts.name` to `say` instead.
---
---Since: 0.18.0
---@param cg integer The face CG number, 2000 and up.
---@param name string The name on the plate.
---@param tone? integer The game's tone value (`口調`). Default 1.
function scene.register_face(cg, name, tone) end

---One row of `rq.scene.choice`.
---@class rq.SceneChoice
---@field id? integer What `choice` returns for this row. Default: its place in the list.
---@field text string The text.
---@field r? integer Red, 0 to 255. Default: the game's color.
---@field g? integer Green, 0 to 255. Default: the game's color.
---@field b? integer Blue, 0 to 255. Default: the game's color.
---@field comment? string The game's comment text for the row. Default "".
---@field sound? integer A sound number for the row.

---Shows a list of choices and waits until the player picks one.
---
---Since: 0.18.0
---
---```lua
---local pick = s.choice({ "Yes", "-", { id = 42, text = "No" } })
---```
---@param items (string|rq.SceneChoice)[] The rows: text, `"-"` for a separator, or a table.
---@param opts? {x?: integer, y?: integer, cancel?: boolean} `x`, `y`: where the list goes (y default 360). `cancel`: the game's cancel flag for the list. Default false.
---@return integer id The picked row's id.
function scene.choice(items, opts) end

---Runs `fn` before a game scene or command.
---
---Game scenes are AIN functions named like `カオル／出会い`; find names with `rq.ain.find`.
---
---Since: 0.18.0
---@param name string The AIN function's name.
---@param fn fun(h: rq.AinHookCtx) Gets the call, as `rq.ain.hook` does.
---@see rq.ain.hook
function scene.before(name, fn) end

---Runs `fn` after a game scene or command.
---
---Since: 0.18.0
---@param name string The AIN function's name.
---@param fn fun(h: rq.AinHookCtx) Gets the call, as `rq.ain.hook` does.
function scene.after(name, fn) end

---Replaces a game scene or command: the game's function does not run.
---
---What `fn` returns becomes the call's result.
---
---Since: 0.18.0
---
---```lua
---rq.scene.replace("カオル／出会い", function(h)
---  rq.scene.play(function(s) s.say(2020, "A new first meeting.") end)
---end)
---```
---@param name string The AIN function's name.
---@param fn fun(h: rq.AinHookCtx): any Returns the result.
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

---A mod's room in Rance Castle, for `rq.castle.room`.
---@class rq.CastleRoomSpec
---@field name string The room's name, as the castle shows it on its button.
---@field on_enter fun(room: rq.CastleRoomHandle) Runs when the player clicks the room. The castle stays open (it is not closed for cleaning), and the game goes back to the town's tabs after it, as with the Poster room.
---@field id? string The mod's own name for the room, for `rq.castle.rooms`. Default `name`.
---@field floor? integer|string The floor: 1 (Roof) to 9 (B2F), or "Roof", "6F" to "1F", "B1F", "B2F". Default "B2F", beside the Poster room.
---@field shown? fun(): boolean Asked each time the castle opens; false leaves the room out this time. Default: always shown.
---@field new? boolean|fun(): boolean The game's NEW mark on the room. Default false.
---@field face? integer|fun(): integer A CG shown beside the name, as a character's face in the game's rooms. Default none.
---@field chara? integer A character id: their face beside the name, as the game shows it (`Ｃ顔ＣＧ`). Use this or `face`.

---Where a mod's room is this time, from a room's `where()`.
---@class rq.CastleRoomPlace
---@field floor string "Roof", "6F" to "1F", "B1F" or "B2F".
---@field row integer 1 (Roof) to 9 (B2F).
---@field col integer 1 to 3.
---@field x integer The room's button, left edge in screen pixels.
---@field y integer Top edge.
---@field w integer Width, 207.
---@field h integer Height, 30.

---A mod's room, from `rq.castle.room`.
---@class rq.CastleRoomHandle
---@field area fun(): integer The game's area number of the room, 30 to 50.
---@field floor fun(): string|nil The floor it was placed on the last time the castle opened, or nil.
---@field where fun(): rq.CastleRoomPlace|nil Where it is while the castle shows it; nil when it is not placed.
---@field set fun(opts: {name?: string, shown?: fun(): boolean, new?: boolean|fun(): boolean, floor?: integer|string}) Changes the room from the next time the castle opens.
---@field remove fun() Takes the room out from the next time the castle opens. Its area number is not given again.

---A mod's room, as `rq.castle.rooms` lists it.
---@class rq.CastleRoomInfo
---@field id string The room's id.
---@field mod string The mod that made it.
---@field name string Its name.
---@field area integer Its area number.
---@field shown boolean It was shown the last time the castle opened.
---@field floor? string Where it is now, while the castle shows it.
---@field col? integer Its column there, 1 to 3.

---Adds a room of the mod's own to Rance Castle.
---
---The room is one of the game's own castle areas, numbers 30 to 50, which the game leaves free. The
---castle rolls its rooms each time it opens; a mod's room is made each time as well, as the Gal
---monsters and Poster rooms are, so the game draws its button, its name in the castle's font, the
---face and the NEW mark, and moves the keys to it. It goes in a free cell of its floor; when the floor
---is full that time, in the nearest floor with one (the one below first). The game's own rooms never
---move. When the player clicks it, `on_enter` runs instead of the game's room code. A mod may open
---a page, a choice menu or a scene there.
---
---Make rooms in `init.lua`; 21 at most, from every mod. The area numbers go in load order and are
---kept in the save's castle arrays, which the game reads only for 1 to 29.
---
---Raises an error for a spec that is not a table, no name, no `on_enter`, a bad floor, a `shown`
---that is not a function, a `new` or `face` of the wrong type, or no area left.
---
---Since: 2.16.0
---
---```lua
---rq.castle.room({ id = "workshop", name = "Workshop", floor = "B2F", new = true,
---  on_enter = function(room)
---    rq.ui.choose({ "Craft", "Repair" }, function(id) rq.log("picked", id) end)
---  end })
---```
---@param spec rq.CastleRoomSpec The room.
---@return rq.CastleRoomHandle room The room, to change or remove it.
function castle.room(spec) end

---Lists every mod's room in Rance Castle.
---
---Since: 2.16.0
---@return rq.CastleRoomInfo[] rooms In the order they were made; removed rooms are left out.
function castle.rooms() end

---An entry for `rq.gamebutton.add`.
---@class rq.GameButtonSpec
---@field id string The mod's own name for the entry; one per mod.
---@field label string The text of its row in the list.
---@field on_click fun(entry: rq.GameButtonHandle) Runs when the player picks it: in "system-menu" after the menus close; in "skill-window" and "status" with the window or page still open, after the game's click sound.
---@field group? "system-menu"|"skill-window"|"status" The game's group. Default "system-menu".
---@field enabled? boolean|fun(chara?: integer): boolean "system-menu": false leaves it out of the list. "skill-window" and "status": false greys the button (a click plays the deny sound and runs nothing). A function is asked each time the list or the window's skill list is built, and in "status" on every draw of the card, with the character shown. Default true.
---@field tip? string|fun(chara: integer): string? The game's comment while the entry is under the mouse or the keys: beside the list ("system-menu"), under the button with the label as its title ("skill-window"), or beside the icon with the label as its title ("status", also on a greyed one). A function only in "status", asked with the character shown when the tip opens; nil or "" shows none. In "status", `\n` starts a line.
---@field icon? integer "status" only, and needed there: the CG drawn as the button, such as one from `rq.image.add` or `rq.image.compose`. The cell is 32 × 29: a smaller icon is centered, a larger one shrunk to fit. Since 2.20.0.
---@field shown? boolean|fun(chara: integer, mode: rq.StatusPageMode?): boolean "status" only: false leaves the button out of the title row, and the next entry takes its cell. A function is asked on every draw of the card. Default true. Since 2.20.0.

---An entry, from `rq.gamebutton.add`.
---@class rq.GameButtonHandle
---@field id fun(): string `<mod id>.<id>`.
---@field set fun(opts: {label?: string, enabled?: boolean|fun(chara?: integer): boolean, tip?: string|fun(chara: integer): string?, icon?: integer, shown?: boolean|fun(chara: integer, mode: rq.StatusPageMode?): boolean}) Changes it; `tip = ""` removes the tip. In "skill-window" a new label or `enabled` shows from the next build of the skill list (`rq.skilllist.refresh` builds it at once). In "status" it shows at once while the page is open; `icon` and `shown` are for "status" only (2.20.0).
---@field remove fun() Takes it out. "system-menu": with no entry left, the MODS row goes as well. "skill-window": the button goes at once; the others move up on the next build. "status": the button goes at once, and the next entry takes its cell.

---An entry, as `rq.gamebutton.groups` lists it.
---@class rq.GameButtonInfo
---@field id string `<mod id>.<id>`.
---@field mod string The mod that added it.
---@field label string Its text.
---@field enabled boolean "system-menu": it is in the list now. "skill-window" and "status": it is not greyed now.
---@field placed? boolean "skill-window" and "status": it has a cell on the last build or draw. Since 2.17.0.
---@field shown? boolean "status": `shown` said yes on the last draw. Since 2.20.0.
---@field button? integer "skill-window": its `T_Button` id on the window now, 200101 to 200104; "status": on the page now, 100101 or 100102. Since 2.17.0.
---@field x? integer "skill-window" and "status": its cell's left edge, in game pixels. Since 2.17.0.
---@field y? integer "skill-window" and "status": its cell's top edge, in game pixels. Since 2.17.0.

---A group, from `rq.gamebutton.groups`.
---@class rq.GameButtonGroup
---@field name string "system-menu", "skill-window" or "status".
---@field open boolean The group is on the screen now.
---@field row? boolean "system-menu": the loader's row is in it (some entry exists).
---@field entries rq.GameButtonInfo[] The entries, in the order they were added.

---Buttons of mods in the game's own menus, made by the game itself.
---
---`"system-menu"`: the SYSTEM menu, from the town, a quest's map or anywhere the game shows it. Mods
---add entries, not rows. While any entry exists the menu has one more row, **MODS**, under Save (or
---Suspend Save), in the menu's own painted look; the keys reach it as any row, and it is greyed while
---no entry is enabled. Picking it closes the menu as any row does and lists every enabled entry in the
---game's choice menu (`rq.ui.choose`, 20 rows at most), in the order they were added. Agents see the
---row as the control `mods` of the `system-menu` screen, and the entries as that choice menu's rows.
---
---`"skill-window"` (2.17.0): buttons under the Potential Skills list (status, then Skills), in the
---look of the castle's buttons (CG 11153, 206 × 30, the label in the game's font, shrunk to fit).
---They sit two to a row under the list's picture: x 531 and 745, y 660 and 692, in the order they
---were added; a fifth is not placed (`placed` false, and the log says so). The window makes them again on each build of its list (open, a buy, a
---character change, `rq.skilllist.refresh`), asking `enabled` each time. A greyed button is the
---game's inactive look and plays the deny sound. The arrow keys reach them: Down from a column's last
---skill goes to the button under it (a greyed one is skipped); from a button Up goes to that column's
---last skill, Down to its first, Left and Right to the other button of the row. Agents see each one
---as the control `<mod id>.<id>` of the `skill-shop` screen.
---
---`"status"` (2.20.0): icon buttons in a character's status page's title row, the only free room on
---the page. The cells are right to left from the X: x 472 and 436, y 61, 32 × 29 each, in the order
---entries were added; the widest name in the game ends at x 435. Entries with `shown` false give their
---cell to the next; a third shown entry is not placed (`placed` false, and the log says so). The
---button is the icon on the page's own background, as the game's CG buttons: the yellow frame under
---the mouse, the icon moved 1 pixel right and down while pressed, the whole cell darkened while
---greyed. `shown`, `enabled` and a function `tip` are asked on every draw of the card (open, another
---character, a change, `rq.statuspage.refresh`). The arrow keys reach the buttons: Up from the Special
---slot (or its REMOVE) goes to the first button not greyed; from a button Left and Right go to the
---other, Down to the Special slot, Up to SKILL SCREEN. Agents see each one as the control
---`<mod id>.<id>` of the `status` screen. See `rq.statuspage` for the page itself.
---
---Since: 2.16.0
---Changed: 2.17.0 The group `"skill-window"`.
---Changed: 2.20.0 The group `"status"`; `enabled` gets the character there.
---
---```lua
---rq.gamebutton.add({ id = "settings", label = "My Mod settings", tip = "Opens My Mod's page",
---  on_click = function() rq.log("open my page here") end })
---rq.gamebutton.add({ group = "skill-window", id = "plan", label = "Plan Skills",
---  on_click = function() rq.log("the skill window is still open") end })
---rq.gamebutton.add({ group = "status", id = "notes", label = "Notes", icon = 11158,
---  tip = function(chara) return "Notes on " .. rq.chara.get(chara).name end,
---  on_click = function() rq.log("notes for", rq.statuspage.chara()) end })
---```
---@class rq.gamebutton
local gamebutton = {}

---Adds an entry to a game menu.
---
---Raises an error for a spec that is not a table, an unknown group, no id or label, no `on_click`,
---an `enabled`, `tip` or `shown` of the wrong type, a `"status"` entry with no `icon`, or an id the
---mod already has.
---
---Since: 2.16.0
---Changed: 2.20.0 The group `"status"`, with `icon` and `shown`.
---@param spec rq.GameButtonSpec The entry.
---@return rq.GameButtonHandle entry The entry, to change or remove it.
function gamebutton.add(spec) end

---Lists the groups and their entries.
---
---Since: 2.16.0
---@return rq.GameButtonGroup[] groups One for each group.
function gamebutton.groups() end

---How the status page was opened, from `rq.statuspage.mode`.
---
---- `"party"`: from the footer's party summary, in town or on a quest (the game's mode 0).
---- `"party-edit"`: from the Party screen (11).
---- `"party-store"`: from the Party screen's stored characters (12).
---- `"battle-result"`: the level-up after a battle (20); it has no equip slots and no list to scroll.
---@alias rq.StatusPageMode "party"|"party-edit"|"party-store"|"battle-result"

---A character's status page, for mods.
---
---The page is the game's (`T_Status`): a mod reads which character it shows, draws its card again
---after changing that character, and hears when it opens, changes character, is drawn again and
---closes. Buttons on it are `rq.gamebutton`'s `"status"` group. (`rq.status` is the mod states'
---module.)
---
---The card is drawn by the game on open, on a change of character (the scroll bar, the wheel, the
---keys), after every equip, skill or profile change, and when the skill window closes; `on_update`
---follows each of those.
---
---Since: 2.20.0
---
---```lua
---rq.statuspage.on_chara(function(chara, old) rq.log("from", old, "to", chara) end)
---rq.gamebutton.add({ group = "status", id = "heal", label = "Heal", icon = 11158,
---  on_click = function()
---    local c = rq.statuspage.chara()
---    rq.chara.set(c, { hp = rq.chara.get(c).hp_max })
---    rq.statuspage.refresh()
---  end })
---```
---@class rq.statuspage
local statuspage = {}

---Returns the character the status page shows.
---
---Since: 2.20.0
---@return integer? chara The character number (iic), or nil while the page is closed.
function statuspage.chara() end

---Returns how the status page was opened.
---
---Since: 2.20.0
---@return rq.StatusPageMode? mode The mode, or nil while the page is closed.
function statuspage.mode() end

---Draws the status page's card again for the character it shows.
---
---Use it after changing that character from Lua (stats, equipment, skills) so the page shows it. It is
---the game's own redraw (`T_Status::setChara`), with no sound; when the item list is open beside the
---page, the list is built again too, as a scroll does. `on_update` runs after it.
---
---Since: 2.20.0
---@return boolean ok true when the card was drawn; false while the page is closed, hidden under the
---skill window (the game draws the card again when it closes), while a skill is being moved, or when
---called from inside an `on_update`, `on_open` or `on_chara` callback.
function statuspage.refresh() end

---Calls `fn` when the status page opens, once its first character is drawn.
---
---Since: 2.20.0
---@param fn fun(chara: integer, mode: rq.StatusPageMode?) Gets the character and how the page was opened.
---@return fun() remove Removes this callback.
function statuspage.on_open(fn) end

---Calls `fn` when the status page shows another character.
---
---Since: 2.20.0
---@param fn fun(chara: integer, old: integer) Gets the character now shown and the one before.
---@return fun() remove Removes this callback.
function statuspage.on_chara(fn) end

---Calls `fn` after every draw of the status page's card: open, another character, a change of
---equipment, skills or profile, the skill window closing, `refresh`.
---
---Since: 2.20.0
---@param fn fun(chara: integer) Gets the character shown.
---@return fun() remove Removes this callback.
function statuspage.on_update(fn) end

---Calls `fn` after the status page closes.
---
---Since: 2.20.0
---@param fn fun() Runs on close.
---@return fun() remove Removes this callback.
function statuspage.on_close(fn) end

---The game's numbers by name: characters, items, monsters, quests, skills, and the small lists the
---game names (classes, states, races, character types, elements, item kinds, skill targets).
---
---Each kind is a table of constants, so `rq.ids.skill.FORTUNE_448` is 448 and `rq.ids.item.SKILL_AGAIN`
---is 306. Editors complete them from `lua/rq_ids.lua`, with each entry's number and in-game name. A
---key is the English name in upper snake case. A name the game gives to more than one number gets
---the number as a suffix on each of them (`FORTUNE_158`, `FORTUNE_448`), and a key that would start
---with a digit starts with `N_` (`N_10_TON_HAMMER`). Some repeated names also have a key without the
---number, beside the numbered keys: the plain key for the one players know (`FORTUNE` is 448,
---`RICK_ADDISON` is 93), or a key of its own for another version (`CAPTURE_EX`, `USE_IBEPROFUN_EX`,
---`STAMINA_BOOST_1`). The docs' data tables list every entry and mark those keys.
---
---The tables hold the base game only, not what mods define; use a mod's text id for those. Reading
---a key that does not exist raises an error, so a typo stops the mod at once instead of passing
---nil along; use `rq.ids.find` for a name that may not be there. Nothing in `rq.ids` can be
---changed. It works at the top level of `init.lua`.
---
---| Kind | Numbers |
---| --- | --- |
---| `chara` | character numbers (`no`), not roster slots |
---| `chara_type` | character type values (`タイプ`), as in `rq.chara.get(iic).type`: `HUMANOID` is 22 |
---| `class` | class numbers, as in `rq.chara.get(iic).class` |
---| `element` | elements (`属性`): `NOTHING`, `LIGHT`, `DARKNESS`, `ELECTRIC`, `ICE`, `FIRE` |
---| `item` | item numbers, not instances |
---| `item_kind` | item kinds (`種類詳細`), as in an item's `subtype`: `SWORD`, `SHORTSWORD`, `SPEAR`, `CLOTHING`, ... |
---| `monster` | monster numbers, as `rq.battle.start` takes them and `rq.battle.snap` gives them: `COCKROACH` is 358 |
---| `music` | music track numbers, as `rq.music` takes them: `INDOOR_CRAWL_R8` is 1, `BATTLE_2_R8` 52 |
---| `quest` | quest numbers |
---| `race` | races (`種族`), as in `rq.chara.get(iic).race`: `HUMAN`, `HANNY`, ... |
---| `skill` | skill numbers |
---| `state` | status values (`状態変化`): `NONE`, `POISON`, `STUN`, `DOWNED` |
---| `target` | skill target codes (`対象`): `MELEE`, `RANGED`, `ALL_ENEMIES`, `ROW`, `SELF`, `ALLY`, `ALL_ALLIES`, `RANDOM_TARGET` |
---
---Since: 1.15.0
---
---Changed: 1.16.0 The kinds `chara_type`, `element`, `item_kind`, `monster`, `race` and `target`, and keys without the number for some repeated names.
---
---Changed: 2.4.0 The kind `music`.
---
---```lua
---local FORTUNE = rq.ids.skill.FORTUNE  -- 448, also FORTUNE_448
---local iic = rq.chara.find(rq.ids.chara.KAORU_QUINCY)
---if rq.item.number(ii) == rq.ids.item.POISON_MASK then ... end
---if ctx.race == rq.ids.race.HANNY then return nil, 50 end
---local no = rq.ids.find("skill", "Spear Knowledge")  -- from a config file or a player
---```
---@class rq.ids
---@field chara rq.IdsChara Characters by name; each key is in the docs' Characters table (Game data).
---@field chara_type rq.IdsCharaType Character types by name; each key is in the docs' Character types table (Game data). Since 1.16.0.
---@field class rq.IdsClass Classes by name; each key is in the docs' Classes table (Game data).
---@field element rq.IdsElement Elements by name; each key is in the docs' Elements table (Game data). Since 1.16.0.
---@field item rq.IdsItem Items by name; each key is in the docs' Items table (Game data).
---@field item_kind rq.IdsItemKind Item kinds by name; each key is in the docs' Item kinds table (Game data). Since 1.16.0.
---@field monster rq.IdsMonster Monsters by name; each key is in the docs' Monsters table (Game data). Since 1.16.0.
---@field music rq.IdsMusic Music tracks by name; each key is in the docs' Music table (Game data). Since 2.4.0.
---@field quest rq.IdsQuest Quests by name; each key is in the docs' Quests table (Game data).
---@field race rq.IdsRace Races by name; each key is in the docs' Races table (Game data). Since 1.16.0.
---@field skill rq.IdsSkill Skills by name; each key is in the docs' Skills table (Game data).
---@field state rq.IdsState States by name; each key is in the docs' States table (Game data).
---@field target rq.IdsTarget Skill targets by name; each key is in the docs' Skill targets table (Game data). Since 1.16.0.
local ids = {}

---Returns the number of the thing with this name.
---
---The name is compared without case, spaces or punctuation, and é matches e, so "spear knowledge"
---finds Spear Knowledge. A key works too (`"FORTUNE_448"`, `"CAPTURE_EX"`). When the game uses the
---name for more than one number, the number of its plain key is returned ("Fortune" gives 448); a
---repeated name with no plain key returns nothing, and the message lists the numbers.
---`rq.ids.find_all` gives them all.
---
---Raises an error for an unknown kind or a name that is not a string.
---
---Since: 1.15.0
---
---Changed: 1.16.0 Takes the kinds `chara_type`, `element`, `item_kind`, `monster`, `race` and `target`. A repeated name with a plain key gives that key's number.
---
---Changed: 2.4.0 Takes the kind `music`.
---
---```lua
---local no, err = rq.ids.find("item", cfg.item)
---if not no then rq.log(err) end  -- such as: more than one skill is named "Attack": 155, 386, ...
---```
---@param kind "chara"|"chara_type"|"class"|"element"|"item"|"item_kind"|"monster"|"music"|"quest"|"race"|"skill"|"state"|"target" The kind.
---@param name string The in-game English name, or a key.
---@return integer|nil no The number, or nil when no entry or more than one has that name.
---@return string? err Why there is no number.
---@see rq.ids.find_all
function ids.find(kind, name) end

---Returns the numbers of every entry whose name contains a text.
---
---The text is compared as in `rq.ids.find`. An empty text gives every number of the kind.
---
---Raises an error for an unknown kind or a text that is not a string.
---
---Since: 1.15.0
---
---Changed: 1.16.0 Takes the kinds `chara_type`, `element`, `item_kind`, `monster`, `race` and `target`.
---
---Changed: 2.4.0 Takes the kind `music`.
---
---```lua
---rq.ids.find_all("skill", "fortune")  -- {158, 159, 160, 293, 448, 585}: Fortune, Improve Fortune,
---                                     -- 7 Misfortunes, 8 Pains
---```
---@param kind "chara"|"chara_type"|"class"|"element"|"item"|"item_kind"|"monster"|"music"|"quest"|"race"|"skill"|"state"|"target" The kind.
---@param text string What the name must contain.
---@return integer[] numbers The numbers, smallest first; empty when none.
---@see rq.ids.find
function ids.find_all(kind, text) end

---Returns the in-game name of a number.
---
---It is the base game's English name, as `rq.skill.get` and the like give it. Some names keep the
---game's own code for é (`Tomato Pur･e`).
---
---Raises an error for an unknown kind.
---
---Since: 1.15.0
---
---Changed: 1.16.0 Takes the kinds `chara_type`, `element`, `item_kind`, `monster`, `race` and `target`.
---
---Changed: 2.4.0 Takes the kind `music`.
---
---```lua
---rq.log("used " .. (rq.ids.name("skill", ctx.skill) or "a mod skill"))
---```
---@param kind "chara"|"chara_type"|"class"|"element"|"item"|"item_kind"|"monster"|"music"|"quest"|"race"|"skill"|"state"|"target" The kind.
---@param no integer The number.
---@return string|nil name The name, or nil when the base game has none for that number.
function ids.name(kind, no) end

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
---Since: 0.2.0
---
---```lua
---local remove
---remove = rq.battle.on_end(function(ev)
---  rq.log("the first fight ended")
---  remove()
---end)
---```
---@class rq
---@field version string The loader version, such as "1.16.0". A mod whose `mod.json` asks for a newer `loader` is not loaded.
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
---@field mod rq.mod
---@field status rq.status
---@field sound rq.sound
---@field music rq.music
---@field uikit rq.uikit
---@field config rq.config
---@field ids rq.ids
---@field gamebutton rq.gamebutton
---@field statuspage rq.statuspage
rq = {}

---The loader version.
---
---Since: 0.2.0
---@type string
rq.version = "2.20.1"

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

---Finds out about other mods, and reaches what they share.
---
---Every mod has its own global variables and its own `require` cache: a global set in one mod
---is not seen by another, and two mods that each have a `util.lua` each get their own. A mod
---shares only what its `init.lua` returns. A mod that lists that mod's id in `dependencies` in
---`mod.json` gets it with `require("<id>")`; any mod can get it with `rq.mod.get`.
---
---What is shared is the same value, not a copy, so a library mod can keep state in it. The
---library's own modules, globals and files stay its own, also when another mod calls its
---functions.
---
---Since: 2.0.0
---
---```lua
----- mods/ui-lib/init.lua (mod.json: "id": "ui-lib")
---local M = {}
---function M.title(text) rq.log("title: " .. text) end
---return M
---
----- mods/my-mod/init.lua (mod.json: "dependencies": ["ui-lib"])
---local ui = require("ui-lib")
---ui.title("Hello")
---```
---@class rq.mod
local mod = {}

---Returns what a loaded mod's `init.lua` returned.
---
---For optional support of another mod, without listing it in `dependencies`. A mod's value is
---there once its `init.lua` has finished, so during your own `init.lua` you see only the mods
---loaded before yours: list the other mod in `loadAfter` to be sure, or call this later (in a
---callback). To need a mod, list it in `dependencies` and use `require("<id>")` instead.
---
---Since: 2.0.0
---
---```lua
----- mod.json: "loadAfter": ["ui-lib"], so ui-lib loads first when it is installed
---local ui = rq.mod.get("ui-lib")    -- nil when ui-lib is not installed
---if ui then ui.title("Hello") end
---```
---@param id string The mod's `id` in its `mod.json`.
---@return any value What its `init.lua` returned; `nil` when that mod is not loaded, did not finish loading, or returned nothing.
---@see rq.mod.list
function mod.get(id) end

---Returns the id of the mod whose code calls it.
---
---It is the `id` in that mod's `mod.json`. When a mod calls a function of another mod, the code
---running is the other mod's, so that mod's id is returned.
---
---Since: 2.0.0
---@return string|nil id The calling mod's id; `nil` outside any mod's code.
function mod.id() end

---Returns the ids of the mods that finished loading, in load order.
---
---A mod whose `init.lua` raised an error is not in the list, nor is one that is still loading
---(your own mod, during its `init.lua`).
---
---Since: 2.0.0
---
---```lua
---for _, id in ipairs(rq.mod.list()) do rq.log(id) end
---```
---@return string[] ids The mods' `id`s, in the order they loaded.
---@see rq.mod.get
function mod.list() end

rq.mod = mod

---Battle states made by mods, such as a burn, a bleed, a regeneration, a freeze or a weaken, that
---live beside the game's poison, stun and ▲▼, in one registry every mod shares.
---
---A mod defines a state once (`define`, in `init.lua`) and adds it to a character in a fight
---(`add`, often from `rq.battle.on_hit`). Any mod can add, read, cure or remove any mod's state by
---its id. A state lives only in the fight: it goes when the fight ends, when its bearer goes down
---(unless `keep_on_down`), when its turns run out, when it is cured, or when a mod removes it.
---
---**Turns** are the bearer's own: `on_turn` runs at each of the bearer's turn starts, and the
---count goes down at the end of each of the bearer's turns. A state added during its bearer's own
---turn skips that turn's countdown, so a 3-turn burn an enemy puts on ticks three times, and a
---2-turn buff a character gives itself lasts through its next two actions.
---
---**Cures.** The game's cure skills cure mod states too, as the game names them by number: Status
---Restore (227), Encourage (237) and Self-Healing (220), which clear every state of the game,
---cure each state whose `cured_by` has `"all"`; First Aid (218 and 219), which cures only poison,
---cures each state whose `cured_by` has `"poison"`. `cured_by` defaults to `{ "all" }` for a bad
---state and `{}` for a good one. A skill number in `cured_by` makes that support skill cure it. A
---mod's own medicine calls `rq.status.cure`, by id, by tag or by kind. The game's items in a fight
---are skills too, and none of them cures a state.
---
---**On the card** a state with an `icon` shows as a buff (`rq.battle.set_buff`), with the turns
---left as its text, or its stacks when it has no turns. A character holds up to 8 buffs, the
---mods' own and the states' together.
---
---**Lethal.** A state's damage stops at 1 HP, as poison's does, unless its definition has
---`lethal = true`: then `ctx:damage` in its `on_turn` may down the bearer, with the state's
---`source` as the killer (see `BattleTurnCtx:damage`). Once the bearer is down, the states after
---it do not tick that turn.
---
---Since: 2.2.0
---Changed: 2.3.0 `lethal`.
---
---```lua
---local BURN_ICON = rq.image.add("burn", "burn.png", { fit = "contain", w = 56, h = 56 })
---rq.status.define("mymod:burn", {
---  name = "Burn", turns = 3, tags = { "fire" }, icon = { icon = BURN_ICON },
---  on_turn = function(s, ctx)
---    ctx:damage(math.max(1, rq.chara.get(s.iic).hp_max * s.power // 100))
---    ctx:log(" " .. rq.chara.get(s.iic).short .. " burns.")
---  end,
---})
---rq.battle.on_hit(function(ctx)
---  if ctx.skill == FIRE_SLASH and not ctx.missed and not ctx.downed then
---    rq.status.add(ctx.defender, "mymod:burn", { power = 5, source = ctx.attacker })
---    ctx:log(" (Burn)")
---  end
---end)
---```
---@class rq.status
local status = {}

---What `rq.status.define` takes.
---
---The callbacks get the instance `s` first. Their errors are logged with the mod and the id, and the
---next callback still runs.
---
---Since: 2.2.0
---@class rq.StatusDef
---@field name? string The name, for logs and other mods. Default the id.
---@field kind? "bad"|"good" Bad states are cured by the game's cure skills; good ones are not. Default "bad".
---@field turns? integer How many of the bearer's turns it lasts, 1 or more; `add` can give another. nil lasts the whole fight.
---@field stack? "refresh"|"add"|"keep"|integer What adding it again does: "refresh" sets the new turns, "add" adds them, "keep" changes nothing, a number is the most stacks (the turns are set again). Default "refresh".
---@field cured_by? (string|integer)[] What cures it: "all" (Status Restore, Encourage, Self-Healing), "poison" (First Aid), or support skill numbers. Default `{ "all" }` for a bad state, `{}` for a good one.
---@field tags? string[] Names for families of states, for `rq.status.cure(iic, { tag = ... })`.
---@field keep_on_down? boolean Stays when the bearer goes down, until the fight ends. Default false.
---@field lethal? boolean Its `on_turn` damage may bring the HP to 0 and down the bearer, with `source` as the killer; `ctx:damage(n, { lethal = false })` still stops at 1. Default false, as poison. Since 2.3.0.
---@field icon? rq.BuffDef The buff on the card; its `text` is set by `rq.status`.
---@field text? fun(s: rq.StatusInstance): string The icon's text, at most 15 bytes. Default the turns left, else the stacks when more than 1.
---@field on_turn? fun(s: rq.StatusInstance, ctx: rq.BattleTurnCtx) Runs at each of the bearer's turn starts, as `rq.battle.on_turn`: damage, heal, skip, log.
---@field on_add? fun(s: rq.StatusInstance) Runs when it is put on someone who did not have it.
---@field on_remove? fun(s: rq.StatusInstance, reason: "expired"|"cured"|"down"|"removed"|"fight_end") Runs when it goes.
---@field physical? fun(s: rq.StatusInstance, side: "attacker"|"defender", ctx: rq.PhysicalCtx): integer|nil, integer|nil For a physical hit by or on the bearer: returns as `rq.combat.on_physical`, an attack and a percent of the final damage.
---@field magic? fun(s: rq.StatusInstance, side: "attacker"|"defender", ctx: rq.MagicCtx): integer|nil For a spell by or on the bearer: a percent of its damage, as `rq.combat.on_magic`.
---@field accuracy? fun(s: rq.StatusInstance, side: "attacker"|"defender", ctx: rq.AccuracyCtx): integer|nil For an accuracy check by or on the bearer: points added to the hit chance, as `rq.combat.on_accuracy`.

---A state on one character, from `rq.status.add`, `get` and `list`.
---
---It is the live record: a mod may change `turns`, `power` and `data`. The icon shows a change of
---`turns` the next time `rq.status` touches the state.
---
---Since: 2.2.0
---@class rq.StatusInstance
---@field id string The state's id.
---@field iic integer The bearer's roster slot.
---@field kind "bad"|"good" From the definition.
---@field turns? integer The bearer's turns left; nil for the whole fight.
---@field stacks integer How many stacks, 1 or more.
---@field power number What `add` gave in `opts.power`, for the definition's callbacks. Default 0.
---@field source integer Who put it on (`opts.source`); 0 when not given.
---@field data table A table for the mod's own values.

---What `rq.status.info` gives about a definition.
---
---Since: 2.2.0
---@class rq.StatusInfo
---@field id string The id.
---@field name string The name.
---@field kind "bad"|"good" The kind.
---@field mod string The id of the mod that defined it.
---@field turns? integer The default turns; nil for the whole fight.
---@field stack "refresh"|"add"|"keep"|integer What adding it again does.
---@field cured_by (string|integer)[] What cures it.
---@field tags string[] Its tags.
---@field keep_on_down boolean Whether it stays when the bearer goes down.
---@field lethal boolean Whether its damage may down the bearer. Since 2.3.0.

---Defines a state that every mod can then add.
---
---Call it in `init.lua`. The first definition with an `icon` installs the buff marks
---(`rq.battle.install_flame`), and the first with `physical` or `accuracy` the combat hooks
---(`rq.combat.install`).
---
---Raises an error for an id that is not `"<mod>:<name>"` (letters, digits, `_` and `-` on each side),
---an id already defined by any mod, or a field of the wrong type or range.
---
---Since: 2.2.0
---@param id string The state's id, `"<mod>:<name>"`, such as `"mymod:burn"`.
---@param def rq.StatusDef The definition.
---@return string id The id.
function status.define(id, def) end

---Returns what is known of a definition, for any mod.
---
---Raises an error for an id that is not `"<mod>:<name>"`.
---
---Since: 2.2.0
---@param id string The state's id.
---@return rq.StatusInfo|nil info A copy, or nil when no mod defined it.
function status.info(id) end

---Puts a state on a character in a fight, or adds to it.
---
---When the character already has it, the definition's `stack` says what changes, and `power` and
---`source` are set again when given. Not in a fight, or on someone down, it does nothing.
---
---Raises an error for an id no mod defined, or an option of the wrong type or range.
---
---Since: 2.2.0
---
---```lua
---local s, new = rq.status.add(target, "mymod:burn", { turns = 2, power = 5, source = user })
---```
---@param iic integer The character's roster slot, ally or enemy.
---@param id string The state's id.
---@param opts? {turns?: integer, stacks?: integer, power?: number, source?: integer, data?: table} `turns`: in place of the definition's, 1 or more. `stacks`: how many to add, default 1. `power`, `source`, `data`: stored on the instance.
---@return rq.StatusInstance|nil s The instance, or nil when nothing was put on.
---@return boolean|nil new true when the character did not have it before.
function status.add(iic, id, opts) end

---Returns a character's instance of a state.
---
---Raises an error for an id no mod defined.
---
---Since: 2.2.0
---@param iic integer The character's roster slot.
---@param id string The state's id.
---@return rq.StatusInstance|nil s The live instance, or nil.
function status.get(iic, id) end

---Returns every state on a character, in the order they came.
---
---Since: 2.2.0
---@param iic integer The character's roster slot.
---@return rq.StatusInstance[] list The live instances; empty when none.
function status.list(iic) end

---Removes a state from a character, with the reason "removed".
---
---Raises an error for an id no mod defined.
---
---Since: 2.2.0
---@param iic integer The character's roster slot.
---@param id string The state's id.
---@return boolean removed false when the character did not have it.
function status.remove(iic, id) end

---Cures states of a character, with the reason "cured": a mod's medicine.
---
---It takes no notice of `cured_by`, which is for the game's skills.
---
---Raises an error for anything else as `what`, or an id no mod defined.
---
---Since: 2.2.0
---
---```lua
---rq.status.cure(target, { tag = "fire" })  -- a burn salve
---rq.status.cure(target, "bad")             -- a cure-all of the mod's own
---```
---@param iic integer The character's roster slot.
---@param what string|{tag: string} A state's id, "bad", "good", "all", or `{ tag = name }`.
---@return integer count How many states went.
function status.cure(iic, what) end

---Calls `fn` each time any mod's state is put on someone who did not have it.
---
---Since: 2.2.0
---@param fn fun(s: rq.StatusInstance) Gets the new instance.
---@return fun() remove Removes this callback.
function status.on_add(fn) end

---Calls `fn` each time any mod's state goes from someone.
---
---Since: 2.2.0
---@param fn fun(s: rq.StatusInstance, reason: "expired"|"cured"|"down"|"removed"|"fight_end") Gets the instance and why it went.
---@return fun() remove Removes this callback.
function status.on_remove(fn) end

rq.status = status

---Plays sounds: a game sound number, a file in the mod folder, or a sound inside a game Flash file.
---
---They play on the game's sound effect mixer, so Config's sound volume applies, and at real time:
---`rq.speed` does not change them. A file or Flash sound is read once and kept in memory, so a sound
---played on every hit reads the disk once. At most 32 mod sounds play at once; one more raises an
---error. These play beside the game's music; `rq.music` puts a file in place of a music track.
---
---Since: 1.4.0
---
---```lua
----- Berserk's sound, from its Flash file
---rq.sound.play_flash("Data/Flash/Battle/狂戦士発動.swf")
----- A sound of your own, looped, then faded out
---local h = rq.sound.play("sfx/rain.ogg", { loop = 0, volume = 60 })
---rq.sound.stop(h, 500)
---```
---@class rq.sound
local sound = {}

---How `rq.sound.play` and `play_flash` play a sound.
---@class rq.SoundOpts
---@field volume? integer The volume, 0 to 100, clamped. Default 100.
---@field loop? integer How many times it plays; 0 loops until stopped. Default 1.
---@field index? integer `play_flash` only: which sound of the file, from 1. Default 1.

---A sound inside a Flash file, from `rq.sound.flash_info`.
---@class rq.FlashSound
---@field rate integer The sample rate, in Hz.
---@field channels integer 1 or 2.
---@field bits integer 8 or 16.
---@field ms integer The length, in milliseconds.
---@field playable boolean false for a compressed sound (MP3 or ADPCM), which `play_flash` refuses.

---Plays a game sound or a sound file from the mod folder.
---
---A file can be WAV, OGG, FLAC, Opus or MP3, up to 64 MB. Raises an error for a missing file, a path
---out of the mod folder, or when 32 mod sounds already play.
---
---Since: 1.4.0
---
---```lua
---local h = rq.sound.play("sfx/hit.wav", { volume = 80 })
---```
---@param src integer|string A game sound number (as for `rq.scene.sound`) or a path in the mod folder.
---@param opts? rq.SoundOpts How to play it.
---@return integer|nil handle The handle, or nil when the number does not exist or the file is not a sound.
---@see rq.sound.stop
function sound.play(src, opts) end

---Plays a sound kept inside a Flash file.
---
---Some game sounds have no number, such as Berserk's in `Data/Flash/Battle/狂戦士発動.swf`. A bare key
---means `Data/Flash/System/<key>.swf`, as in `rq.image.add_gif`, and a GIF added under that key is
---looked at first. Only uncompressed (PCM) sounds play. Raises an error when the file or that sound
---is missing, the sound is compressed, `index` is below 1, or 32 mod sounds already play.
---
---Since: 1.4.0
---@param key string The Flash file's key or path.
---@param opts? rq.SoundOpts How to play it; `index` picks the sound.
---@return integer|nil handle The handle, or nil when the sound did not start.
---@see rq.sound.flash_info
function sound.play_flash(key, opts) end

---Lists the sounds in a Flash file.
---
---Since: 1.4.0
---
---```lua
---for i, s in ipairs(rq.sound.flash_info("Data/Flash/Battle/狂戦士発動.swf") or {}) do
---  rq.log(i, s.ms, s.playable)
---end
---```
---@param key string The Flash file's key or path, as for `play_flash`.
---@return rq.FlashSound[]|nil sounds The sounds, in the order `index` counts them; nil when there is no such file.
function sound.flash_info(key) end

---Stops a sound, at once or with a fade out.
---
---Since: 1.4.0
---@param handle integer A handle from `play` or `play_flash`.
---@param fade_ms? integer The fade out, in milliseconds. Default 0: stop at once.
---@return boolean stopped false when the handle already ended.
function sound.stop(handle, fade_ms) end

---Returns whether a sound still plays.
---
---An ended handle stays false, even when its slot plays something new.
---
---Since: 1.4.0
---@param handle integer A handle from `play` or `play_flash`.
---@return boolean playing true while it plays.
function sound.playing(handle) end

---Changes a sound's volume, at once or over a fade.
---
---Since: 1.4.0
---@param handle integer A handle from `play` or `play_flash`.
---@param volume integer The new volume, 0 to 100, clamped.
---@param fade_ms? integer The fade, in milliseconds. Default 0: at once.
---@return boolean changed false when the handle already ended.
function sound.set_volume(handle, volume, fade_ms) end


---A part of a CG drawn at any size: the edges stay as they are, the middle stretches.
---
---Since: 1.7.0
---@class rq.UiPiece
---@field cg integer The CG number; one from `rq.image.add` works.
---@field src? integer[] `{x, y, w, h}` in the CG. Default: all of it.
---@field edge? integer[] `{left, top, right, bottom}`: the edges kept as they are, in pixels.
---@field mid? integer[] `{x, w}`: the middle columns, from `x` in `src`, such as one empty column between a frame and the text the game drew on it. Default: what the edges leave.
---@field vmid? integer[] `{y, h}`: the middle rows, from `y` in `src`.
---@field vfit? "crop" Takes the top rows of the middle instead of squashing them.

---A look for the kit: pieces, text styles, colors and sounds.
---
---`rq.uikit.themes.config` is the look of the game's Config screen; it holds Config's pieces only.
---`rq.uikit.themes.hub` (2.6.0) is Config's theme with the hub's pieces added: with it in use,
---`rq.uikit.button` is the castle's grey button and `rq.uikit.list` has the game's scroll bar.
---
---Since: 1.7.0
---Changed: 2.6.0 Added the hub's fields.
---@class rq.UiTheme
---@field label rq.HudTextStyle Label text.
---@field dim {r: integer, g: integer, b: integer} The grey-green of Config's Low and High.
---@field page rq.UiPiece A Config page.
---@field block rq.UiPiece The 3D tab's dark block.
---@field button rq.UiPiece A button.
---@field button_text rq.HudTextStyle Button text.
---@field button_cover {r: integer, g: integer, b: integer, a: integer} The cover that pulses over a hovered button.
---@field tab {normal: rq.UiPiece, hover: rq.UiPiece, current: rq.UiPiece} The tabs.
---@field tab_text {normal: rq.HudTextStyle, hover: rq.HudTextStyle, current: rq.HudTextStyle} Tab text.
---@field check_frame rq.UiPiece The checkbox frame.
---@field check_mark rq.UiPiece The checkmark.
---@field hover_bar rq.UiPiece The bar over a hovered row.
---@field track rq.UiPiece The slider track.
---@field knob rq.UiPiece The slider knob.
---@field knob_hot rq.UiPiece The knob while hovered or dragged.
---@field list_text rq.HudTextStyle List row text.
---@field list_current {r: integer, g: integer, b: integer} The color of the chosen list row.
---@field scrollbar {r: integer, g: integer, b: integer, a: integer} A list's scroll bar.
---@field sound {hover?: integer, click?: integer, error?: integer} Sound numbers; nil plays none.
---@field button_style? string The button style `rq.uikit.button` draws without `opts.style`. The hub theme's is "large". Since 2.6.0.
---@field tabs_style? string The tabs style `rq.uikit.tabs` draws without `opts.style`. Since 2.6.0.
---@field styles? {button?: table<string, rq.UiButtonStyle>, tabs?: table<string, rq.UiTabsStyle>} Button and tabs styles by name. A name the theme in use lacks is taken from `themes.hub`. Since 2.6.0.
---@field castle? table Rance Castle's room row: `base`, `frame`, `new` (pieces) and `marks` (`heart`, `run`: pieces with their `x`, `y`). Since 2.6.0.
---@field quest? table A quest row: `normal`, `hover`, `box` (pieces), `no`, `name`, `mark` (text styles), `name_fit`, `current` (a color), `gap`. Since 2.6.0.
---@field rack? table Frostbuy's rack tile: `w`, `h`, `outer`, `line`, `fill` (colors), `text`, `fit`. Since 2.6.0.
---@field sort? table The sort column: `back`, `base`, `normal`, `hover`, `current` (pieces), `text`, `fit`, `fit_w`. Since 2.6.0.
---@field scroll? rq.UiScrollPieces The game's scroll bar. A theme with it gives `rq.uikit.list` that bar. Since 2.6.0.
---@field close? {normal: rq.UiPiece, hover: rq.UiPiece} The close button. Since 2.6.0.
---@field hub_page? table A page over the hub: `x`, `y`, `w`, `h`, `dim`, `outer`, `line` (colors), `title` (a text style), `pad`. Since 2.6.0.
---@field options? table The choice menu's look for `rq.uikit.options`: `base` (the menu's background), `sep`, `panels` (the text row's pieces for 1 to 3 lines, with `ph`, their height), `text`, `text_fit` (text styles). Since 2.7.0; `comment` gone in 2.13.0 (the comment is the game's window).
---@field vault? table The vault's item rows: `rows` (the backgrounds by style: vault, storage, and since 2.9.0 shop and equip), `name`, `info`, `number`, `level_text`, `count` (text styles), `name_fit`, `info_fit`, `level_fit`, `level`, `locked`, `count_box`, `stock_box` (2.9.0) (pieces), `rank` (the first rank badge CG), `kinds` (the kind marks' CGs), `current` (a color), `buttons` (the row buttons' pieces and places), `gold` (the gold digits), `price` (2.9.0: the shop's price place, cells and gap). Since 2.8.0.
---@field filters? table The vault's filter column: `back`, `backs` (2.9.0: the column's grey by style), `cats`, `classes`, `lock`, `equip` (2.9.0) (each with `lit` and `dim` pieces), and `sub` (the sub-filters' panel: `base`, `title`, and by category `title`, `cgs`, `names`). Since 2.8.0.

---A button style of the hub theme: two states of a grey button and its text.
---
---Since: 2.6.0
---@class rq.UiButtonStyle
---@field normal rq.UiPiece The button.
---@field hover rq.UiPiece The button under the mouse.
---@field w integer Its width, in pixels, when `opts.w` is not given.
---@field h integer Its height, in pixels.
---@field text rq.HudTextStyle The text, usually with `font = "game"`.
---@field fit? number[] `{max size, min size, max spacing, min spacing}`: as the game's `FitText2`, a long text gets smaller, 2 pixels at a time, and tighter until it fits.
---@field pad? integer Pixels kept free at each side of the text. Default 4.
---@field hover_dy? integer The text moves down this much under the mouse, as on the footer's buttons.
---@field cover? boolean The pulsing cover over it under the mouse, as on rect buttons.

---A tabs style of the hub theme.
---
---Since: 2.6.0
---@class rq.UiTabsStyle
---@field normal rq.UiPiece A tab.
---@field current rq.UiPiece The current tab.
---@field w integer Each tab's width, in pixels, when `opts.w` is not given.
---@field h integer The height, in pixels.
---@field gap integer Pixels between tabs.
---@field pad integer Pixels kept free at each side of the text.
---@field cover boolean The pulsing cover over a tab under the mouse.
---@field text rq.HudTextStyle The text.
---@field fit? number[] As in `rq.UiButtonStyle`.

---The pieces of the game's scroll bar.
---
---Since: 2.6.0
---@class rq.UiScrollPieces
---@field up rq.UiPiece The up arrow.
---@field up_hover rq.UiPiece The up arrow under the mouse.
---@field down rq.UiPiece The down arrow.
---@field down_hover rq.UiPiece The down arrow under the mouse.
---@field bar rq.UiPiece The bar.
---@field track rq.UiPiece The track the bar runs in.
---@field w integer The width, in pixels (20).
---@field arrow integer Each arrow's height, in pixels (40).
---@field min integer The bar's least length, in pixels (10).

---Where a widget goes.
---
---Without `x` and `y` it goes where its row or column puts it. A widget given both `x` and `y` stands
---where it says and does not move the row on.
---@class rq.UiPlace
---@field x? integer Left edge, in game pixels.
---@field y? integer Top edge, in game pixels.
---@field w? integer|"fill" Width in pixels, or "fill": the width left in the row, or the column's width less its pad. "fill" needs a row or column with a `w`.
---@field h? integer Height in pixels.
---@field id? any Keeps the widget's state across updates (a drag, a list's scroll). Without one a widget is known by the order of the calls, so give one to widgets that show or hide.
---@field nav? false|{up?: any, down?: any, left?: any, right?: any}|fun(dir: "up"|"down"|"left"|"right"): any Where the arrow keys go from this widget, by the other widget's `id`, in place of the nearest one that way; a function returns the id or nil. false: the keys' cursor skips it. Since 2.12.0.
---@field tip? string|rq.UiTip On a widget that takes a click: the game's comment window after the mouse or the keys' cursor stays on it `tip_wait` ms (see `rq.uikit.comment`). A text, or a table with a header and more; false for none where a widget has a tip of its own (`skill_button`). Since 2.13.0.
---@field tip_wait? integer How long the cursor stays on the widget before its tip shows, in ms. Default 500, as the game's. Since 2.13.0.

---A widget's tip: what `rq.uikit.comment` shows, and where.
---
---Since: 2.13.0
---@class rq.UiTip: rq.UiCommentOpts
---@field text? string The text.

---What `rq.uikit.comment` takes besides the text.
---
---Since: 2.13.0
---@class rq.UiCommentOpts
---@field header? string A title over the text, in 22-pixel black (pink in "red").
---@field sub? string Red lines under the text, 3 pixels apart from it, as the game's "cannot use" notes.
---@field style? "normal"|"red" "normal" (default): the paper window with a cyan border. "red": the game's red one, as for the skills of a character who cannot use them.
---@field x? integer Left edge, in game pixels. Default: 11 right of the mouse.
---@field y? integer Top edge, in game pixels. Default: 20 under the mouse.
---@field game? boolean `@` and `$` break a line too, as in the game's own texts (`rq.ain` strings). Default false: only `\n` does.
---@field id? any The window's id in `rqmod-ctl ui` (as `"comment-" .. id`). Default "comment".

---A row or column for `rq.uikit.row` and `col`.
---@class rq.UiBox: rq.UiPlace
---@field gap? integer Pixels between widgets.
---@field pad? integer Pixels inside the box's edge.
---@field align? "start"|"center"|"end" Across the box: up and down in a row (needs `h`), left and right in a column (needs `w`).

---One entry of `rq.uikit.menu`.
---@class rq.UiMenuItem
---@field text string The text.
---@field items? (string|rq.UiMenuItem)[] The next level; picking this entry opens it.
---@field disabled? boolean The entry cannot be picked.

---Widgets in the look of the game's Config screen and, since 2.6.0, of its hub.
---
---They are drawn with `rq.hud` from the game's own pictures: Config draws its labels into its
---pictures, so the kit cuts out the parts that carry no text and writes its own text on them. The kit
---is Lua on the public `rq.hud` calls. The hub's widgets write in the game's own font: grey buttons
---in four sizes, Rance Castle's room rows, quest rows and the quest list, the town tabs, Frostbuy's
---rack tiles, the sort column, the game's scroll bar and close button, a page that covers the
---hub, and (2.7.0) the choice menu's look. They take their pieces from the theme in use, else from
---`themes.hub`.
---
---It works in immediate mode: call the widgets from `rq.on_frame` on every update they show, keep the
---values yourself, and pass them back in. Each widget returns the new value and whether it changed on
---this update. While the mouse is over a widget the game sees no mouse. Hover and click play the
---game's own sounds (1001 and 1002). The widgets are listed for `rqmod-ctl ui` in the section `"kit"`.
---
---The keys (2.12.0): the arrow keys move one cursor between the widgets that take a click, and Enter
---clicks the one under it, as the game's own screens do. The mouse and the keys share it: moving the
---mouse over a widget puts the cursor there, and an arrow then goes on from it; the mouse is never
---moved. An arrow goes to the nearest widget that way, one whose center is past the edge of the
---cursor's widget (the gap along the arrow plus twice the offset across it); with none, it wraps to
---the widget the other way that lines up best, the farthest of those, as a row of tabs wraps. With no
---cursor, Down and Right go to the first widget drawn, Up and Left to the last. A widget's `nav`
---changes where its arrows go. In a list (`list`, `quest_list`, `options`, `item_list`) Up and Down
---go through its rows, also those scrolled out of sight, and the list scrolls to follow; at the end
---of a list that does not wrap they go on to the nearest widget outside it. A focused slider moves
---by Page Up and Page Down. A held key moves again after 250 ms, then every 50 ms, as the game's.
---The kit takes the keys only on an update where `rq.uikit.keys` is called: inside `page`, on
---Config's Mods tab, and where a mod calls it. Elsewhere its widgets over the game's screens are
---for the mouse, and the arrows stay the game's.
---
---Tips (2.13.0): every widget that takes a click has `tip`, the game's comment window
---(`rq.uikit.comment`) shown when the mouse or the keys' cursor has stayed on it 500 ms, with the
---game's sound 5001. Under the mouse it goes at the mouse, as the game's; from the keys, at the
---widget's right edge (left of it when it does not fit). It goes when the cursor leaves. A disabled
---widget shows its tip under the mouse too. `tabs`, `choice` and `sort` take `tips`, one for each
---entry; `list` and `quest_list` rows take `tip`.
---
---Since: 1.7.0
---Changed: 2.12.0 The keys' cursor.
---Changed: 2.13.0 Tips.
---
---```lua
---local ui = rq.uikit
---local vol, mute = 0.8, false
---rq.on_frame(function()
---  ui.panel({ x = 12, y = 85, w = 1000, h = 595 })
---  ui.col({ x = 62, y = 105, gap = 20 }, function()
---    ui.row({ h = 30, align = "center" }, function()
---      ui.label("Master", { w = 218 })
---      vol = ui.slider(vol, { low = "Low", high = "High", id = "vol" })
---      mute = ui.checkbox(mute, "Mute", { x = 640, id = "mute" })
---    end)
---  end)
---end)
---```
---@class rq.uikit
---@field theme rq.UiTheme The theme in use.
---@field themes {config: rq.UiTheme, hub: rq.UiTheme} The built-in themes. `hub` came in 2.6.0.
local uikit = {}

---Draws a line of text, white with a dark edge.
---
---Since: 1.7.0
---@param text string The text.
---@param opts? rq.UiPlace|{size?: number, color?: integer[], dim?: boolean, align?: string, valign?: string} `size` (default the theme's, 17), `color` `{r, g, b}`, `dim` for the theme's dim color, `align`, `valign` (default "center").
---@return integer x Left edge.
---@return integer y Top edge.
---@return integer w Width.
---@return integer h Height.
function uikit.label(text, opts) end

---Draws a Config button with text.
---
---The button is at least 150 wide (or the text's width) and 40 high. When hovered, the game's cover
---pulses over it every 2 seconds. A click counts when the button is let go over the same button it
---went down on.
---
---With `opts.style`, or a theme with `button_style`, it is one of the hub's grey buttons instead:
---"large" (Rance Castle's rooms, 206x30), "choice" (the choice menu's, 340x27), "choice_tall"
---(340x60) or "short" (the footer's Vault and Trivia, 98x26). Its size is the style's unless
---`opts.w` or `h` is given, and a long text gets smaller until it fits, as the game's labels do.
---
---Raises an error for a style no theme has.
---
---Since: 1.7.0
---Changed: 2.6.0 Added `opts.style` and the theme's `button_style`.
---
---```lua
---if rq.uikit.button("Reset", { id = "reset" }) then reset() end
---if rq.uikit.button("Open the shop", { style = "large", x = 120, y = 300 }) then open = true end
---```
---@param text string The text.
---@param opts? rq.UiPlace|{disabled?: boolean, style?: "large"|"choice"|"choice_tall"|"short"|string} Place, `disabled` to grey it out, and `style`.
---@return boolean clicked true on the update it is clicked.
function uikit.button(text, opts) end

---Draws a checkbox with its text.
---
---The text is 42 pixels right of the box, and the game's yellow bar covers the row when hovered.
---
---Since: 1.7.0
---@param value boolean Whether it is checked.
---@param text? string The text beside it.
---@param opts? rq.UiPlace|{disabled?: boolean} Place (`w` is the bar's width), and `disabled`.
---@return boolean value The new value.
---@return boolean changed true on the update it was clicked.
function uikit.checkbox(value, text, opts) end

---Draws one of several checkboxes, as the 3D tab's quality.
---
---Since: 1.7.0
---@param list string[] The texts.
---@param index? integer The checked one; nil for none.
---@param opts? rq.UiPlace|{dir?: "col"|"row", gap?: integer, disabled?: boolean, tips?: (string|rq.UiTip)[]} `dir` (default "col"), `gap` in pixels (default 20), `disabled`, `tips` (2.13.0: one for each checkbox).
---@return integer|nil index The checked one.
---@return boolean changed true on the update it changed.
function uikit.choice(list, index, opts) end

---Draws the Sound tab's slider.
---
---Drag the knob or click the track; the drag goes on while the button is down, also off the track.
---The wheel over it moves it by `step`, or by a twentieth, and the game does not get that wheel step.
---Under the keys' cursor, Page Down raises it and Page Up lowers it by `step`, or a twentieth (5%),
---as Config's sliders: on the press, then on every update after 250 ms.
---
---Since: 1.7.0
---Changed: 1.8.0 Added `opts.label`.
---Changed: 2.12.0 Page Up and Page Down.
---@param value number The value.
---@param opts? rq.UiPlace|{min?: number, max?: number, step?: number, low?: string, high?: string, label?: string, disabled?: boolean} `w`: the track's width (default 200). `min` (default 0), `max` (default 1), `step`, `low` and `high` (texts at the ends), `label` (its name in `rqmod-ctl ui`), `disabled`.
---@return number value The new value.
---@return boolean changed true on the update it changed.
function uikit.slider(value, opts) end

---Draws Config's tabs side by side: normal, hovered and current.
---
---With `opts.style = "town"` (or a theme's `tabs_style`) they are the hub's town tabs instead, 248
---by 45 and 8 apart, the current one orange.
---
---Since: 1.7.0
---Changed: 2.6.0 Added `opts.style`.
---@param list string[] The tab texts.
---@param index integer The current tab.
---@param opts? rq.UiPlace|{style?: "town"|string, gap?: integer, tips?: (string|rq.UiTip)[]} Place; `w` is each tab's width (default 120, or the style's). `style`, and `gap` between styled tabs. `tips` (2.13.0): one for each tab.
---@return integer index The current tab.
---@return boolean changed true on the update it changed.
function uikit.tabs(list, index, opts) end

---Draws a Config page or the 3D tab's dark block, and runs `fn` in a column inside it.
---
---A page lower than the game's keeps its top colors; it is not squashed.
---
---Raises an error without `w` and `h`.
---
---Since: 1.7.0
---@param opts rq.UiPlace|{style?: "page"|"block", pad?: integer, gap?: integer, title?: string} `w` and `h` are needed. `style` (default "page"), `pad` (default 12), `gap`, `title`.
---@param fn? fun(x: integer, y: integer, w: integer, h: integer) The widgets inside.
---@return integer x Left edge.
---@return integer y Top edge.
---@return integer w Width.
---@return integer h Height.
function uikit.panel(opts, fn) end

---Draws rows on a dark block that scroll by the wheel.
---
---A bar shows when the rows do not fit. The list stays inside a clip of your own (`rq.hud.clip_push`).
---With a theme that has `scroll` (the hub theme), the bar is the game's scroll bar, with arrows, when
---the list is at least 100 high.
---
---Raises an error without `w` and `h`.
---
---Since: 1.7.0
---Changed: 2.5.0 Stays inside a clip of your own; before, its clip replaced yours.
---Changed: 2.6.0 The game's scroll bar with a theme that has `scroll`.
---@param items (string|{text: string, disabled?: boolean, tip?: string|rq.UiTip})[] The rows; `tip` since 2.13.0.
---@param index? integer The chosen row; nil for none.
---@param opts rq.UiPlace|{row_h?: integer, block?: boolean} `w` and `h` are needed. `row_h` (default 30), `block = false` leaves out the dark block.
---@return integer|nil index The chosen row.
---@return boolean changed true on the update it changed.
function uikit.list(items, index, opts) end

---Draws a menu with levels: lists side by side, where an entry with `items` opens the next list.
---
---A new pick on a level drops the levels after it.
---
---Raises an error without `h`.
---
---Since: 1.7.0
---@param tree rq.UiMenuItem[] The first level.
---@param path? integer[] The index picked at each level.
---@param opts rq.UiPlace|{gap?: integer} `h` is needed. `w` is each list's width (default 200), `gap` (default 10).
---@return integer[] path The path; a new table when it changed.
---@return boolean changed true on the update it changed.
---@return string|rq.UiMenuItem|nil item The entry at the path's end.
function uikit.menu(tree, path, opts) end

---Places the widgets called in `fn` side by side.
---
---Since: 1.7.0
---@param opts rq.UiBox|fun() The box, or `fn` itself.
---@param fn? fun(x: integer, y: integer, w?: integer, h?: integer) The widgets.
---@return integer x Left edge.
---@return integer y Top edge.
---@return integer w The width the widgets cover.
---@return integer h The height they cover.
function uikit.row(opts, fn) end

---Places the widgets called in `fn` one under the other.
---
---Since: 1.7.0
---@param opts rq.UiBox|fun() The box, or `fn` itself.
---@param fn? fun(x: integer, y: integer, w?: integer, h?: integer) The widgets.
---@return integer x Left edge.
---@return integer y Top edge.
---@return integer w The width the widgets cover.
---@return integer h The height they cover.
function uikit.col(opts, fn) end

---Leaves empty space in a row or column.
---
---Since: 1.7.0
---@param w integer Width, in pixels.
---@param h integer Height, in pixels.
function uikit.space(w, h) end

---Returns where the last widget went, as for a tooltip beside it.
---
---Since: 1.7.0
---@return integer x Left edge.
---@return integer y Top edge.
---@return integer w Width.
---@return integer h Height.
function uikit.last() end

---Returns whether the mouse was over a kit widget on this update so far.
---
---Since: 1.7.0
---@return boolean hovered true when a widget drawn so far is under the mouse.
function uikit.hovered() end

---Draws the game's comment window, its tooltip, on this update.
---
---It is the game's own look (`T_Comment`): a paper window with three thin borders, a shadow, a
---header, the text in the game's font and red `sub` lines under it, laid out as the game lays its
---own out, to the pixel. The text wraps at 520 pixels, as the game's, and shows 20 lines at most.
---It is drawn on the top layer (`rq.uikit.top`), over every widget. Call it on every update it
---should show; it does not wait. For a tip that waits for the mouse, give a widget `tip`.
---
---Without `x` and `y` it goes at the mouse, 11 right and 20 under it; past the screen's right edge it
---goes left of the mouse, and past the bottom it ends at 736, as the game's. A given `x` past the
---right edge ends at 1024. It takes no clicks and covers no game control: `rqmod-ctl` lists it in
---the section "kit" with the type "comment" and its text as `value`.
---
---Raises an error when the text, `header` or `sub` is not a string, `style` is unknown or `opts` is
---not a table.
---
---Since: 2.13.0
---
---```lua
---rq.on_frame(function()
---  if hovering_my_icon then
---    rq.uikit.comment("Doubles the gold from quests.", { header = "Golden Touch", sub = "Not in the tutorial." })
---  end
---end)
---```
---@param text string|nil The text. `\n` breaks a line.
---@param opts? rq.UiCommentOpts `header`, `sub`, `style`, `x`, `y`, `game`, `id`.
---@return integer|nil x The window's left edge, or nil when there is nothing to show (no text, header or sub).
---@return integer|nil y Its top edge.
---@return integer|nil w Its width, without the 5-pixel shadow.
---@return integer|nil h Its height, without the shadow.
---@see rq.uikit.comment_size
function uikit.comment(text, opts) end

---Returns the size the comment window would have, without drawing it.
---
---Raises an error as `comment` does.
---
---Since: 2.13.0
---@param text string|nil The text.
---@param opts? rq.UiCommentOpts `header`, `sub`, `style`, `game`.
---@return integer|nil w The width without the 5-pixel shadow; nil when there is nothing to show.
---@return integer|nil h The height without the shadow.
function uikit.comment_size(text, opts) end

---What `rq.uikit.bar` takes.
---
---Since: 2.14.0
---@class rq.UiBarOpts: rq.UiPlace
---@field style? "hp"|"mini"|"monster"|"exp"|"charisma"|"quest"|"magnum" One of the game's bars: `"hp"` (10125, the status and character cards' HP), `"mini"` (10098, the mini cards' HP), `"monster"` (10142, a monster's HP), `"exp"` (10302, EXP on the battle results), `"charisma"` (10013, the hub's), `"quest"` (11303, a quest on the map), `"magnum"` (11316, the Magnum gauge, upright).
---@field cg? integer Any CG as the bar, a mod image included; it is the bar's size.
---@field dir? "right"|"up" "right" (default): it fills from the left. "up": from the bottom. Default "up" for `"magnum"`.
---@field color? integer[]|{r: integer, g: integer, b: integer, a?: integer} A bar of one color, `w` by `h` (default 190 by 4, as the battle's).
---@field anim? integer In ms: a new value is reached in a straight line over this time, as the game's bars move. Default 0, at once. Needs an `id`, or the same call order.

---Draws one of the game's bars, a gauge: its CG cut to `value / max` of its width, as the game's
---`T_Bar`.
---
---With `dir = "up"` it is cut to that part of its height and fills from the bottom. A value above 0
---shows at least 1 pixel. A value is kept between 0 and `max`; a `max` under 1 draws nothing. With
---`anim` the value shown moves from the old value to a new one over that many ms.
---
---Only the bar: the frames round the game's bars are parts of each screen's background. It takes no
---click and is not on the keys' cursor; `tip` shows under the mouse. It is listed for `rqmod-ctl
---ui` with the type "bar" and the value it shows, and never covers a game control.
---
---Raises an error for a `value` or `max` that is not a number, an unknown `style` or `dir`, a CG that
---is not there, no `style`, `cg` or `color`, or an `anim` that is not a whole number of 0 or more.
---
---Since: 2.14.0
---
---```lua
---rq.on_frame(function()
---  local c = rq.chara.get(1)
---  rq.uikit.bar(c.hp, c.hp_max, { style = "hp", x = 74, y = 394, id = "hp", anim = 250 })
---  rq.uikit.bar(fuel, 100, { color = { 255, 200, 0 }, x = 400, y = 600, w = 120, h = 6, tip = "Fuel" })
---end)
---```
---@param value number The value.
---@param max number The value of a full bar.
---@param opts? rq.UiBarOpts `style`, `cg` or `color`; `dir`, `anim`; place, `id`, `tip`.
---@return number shown The value the bar shows on this update (moving with `anim`).
function uikit.bar(value, max, opts) end

---What `rq.uikit.skill_button` takes.
---
---Since: 2.15.0
---@class rq.UiSkillOpts: rq.UiPlace
---@field rank? integer The skill's rank: a mark for each rank above 1, after the name. Default 1, or the character's with `chara`.
---@field chara? integer A character number: the rank, and for a skill that is an action its uses, are the character's (`キャラスキル_ランク`, `キャラスキル_使用回数`), as the status page shows them.
---@field long? boolean The long tile, 167 wide, of a red, blue or item skill (`延長スキルボタン_ＣＧ番号`). The game draws it 25 to the left of the normal tile's place.
---@field count? integer|false The uses, in the level digits at the tile's x + 120, two digits padded with zeros, 99 at most. Default: the character's with `chara`. false: none.
---@field count_max? integer|false On a long tile: the most uses, at x + 145. Default: the character's with `chara`.
---@field disabled? boolean Darkened as the game greys a skill: the tile and its uses at half, the name's color halved. It cannot be clicked; under the mouse it gets the game's red frame, and its tip still shows.
---@field slot? integer With `skill` nil: the empty slot, with this number on it.

---Draws a skill's tile, as the game's status page draws it, and returns whether it was clicked.
---
---The tile is the CG of the skill's panel color, the game's own pick (`スキルボタン_ＣＧ番号`), 142 by
---24. The name is in the game's font, fitted as the game fits it: 18 pixels, else 16, 14 or 12 until
---the name and the rank marks fit, then squeezed. Under the mouse it shows the CG's hovered look
---and the game's yellow frame; Enter and the keys' cursor work as on every widget.
---
---Its `tip` is the game's comment for the skill, its name and `スキル_説明文１`, right of the tile as
---the status page shows it; give a `tip` of your own, or false for none. With `skill` nil and `slot`
---it draws the empty slot (CG 10121) with its number.
---
---It is listed for `rqmod-ctl ui` with the type "skill" and the skill number as `value`; an empty
---slot with the type "slot", which takes no click.
---
---Raises an error for a skill that is not a whole number above 0 or has no tile (no long tile, with
---`long`), no `slot` without a skill, or a `rank`, `chara`, `count` or `count_max` that is not a whole
---number.
---
---Since: 2.15.0
---
---```lua
---rq.on_frame(function()
---  if not open then return end
---  open = rq.uikit.page({ id = "skills", title = "Skills" }, function(x, y)
---    for i, skill in ipairs(my_skills) do
---      if rq.uikit.skill_button(skill, { x = x + 20, y = y + i * 26, chara = chara, id = skill }) then
---        pick(skill)
---      end
---    end
---    rq.uikit.skill_button(nil, { x = x + 200, y = y + 26, slot = 1 })
---  end)
---end)
---```
---@param skill integer|nil A skill number, or nil for an empty slot.
---@param opts? rq.UiSkillOpts `rank` or `chara`, `long`, `count`, `count_max`, `disabled`, `slot`; place, `id`, `tip`, `nav`.
---@return boolean clicked true on the update it is clicked.
function uikit.skill_button(skill, opts) end

---Runs a function now and draws what it draws on top of every widget, as `rq.hud.top` does.
---
---For a widget's floating part: its tooltip, its comment, an icon it drags. Widgets inside `fn` draw
---as usual, but clicks are still decided in call order.
---
---Raises an error when `fn` is not a function, and the error `fn` raises.
---
---Since: 2.11.0
---
---```lua
---if rq.uikit.hovered() then
---  rq.uikit.top(function() rq.uikit.label("Sells for 120 G", { x = mx + 16, y = my }) end)
---end
---```
---@param fn fun() The widgets and draws for the top layer.
---@return any ... What `fn` returns.
---@see rq.hud.top
function uikit.top(fn) end

---Lets the kit take the arrow keys, Enter, Page Up and Page Down on this update.
---
---They move the keys' cursor between widgets (see `rq.uikit`) and are hidden from the game
---(`rq.hud.block_keys`); other keys still reach it. Call it on every update the keys should be the
---kit's; `page` calls it, and so do Config's Mods tab, `options` and `item_list` with `keys`.
---
---With `edge`, an arrow that finds no widget that way calls `edge(dir)` and the cursor does not
---wrap: a mod hands the cursor to something else there, as Config's Mods tab gives it back to the
---game's tab row on Up.
---
---Raises an error when `opts` is not a table or `edge` not a function.
---
---Since: 2.12.0
---
---```lua
---rq.on_frame(function()
---  if not open then return end
---  rq.uikit.keys()
---  if rq.uikit.button("One", { id = "one", x = 100, y = 200 }) then pick(1) end
---  if rq.uikit.button("Two", { id = "two", x = 300, y = 200 }) then pick(2) end
---end)
---```
---@param opts? {edge?: fun(dir: "up"|"down"|"left"|"right")} `edge`: called when an arrow finds nothing that way.
---@see rq.uikit.focus
function uikit.keys(opts) end

---Returns the widget under the keys' cursor.
---
---It is the widget the arrows reached or the mouse last moved over. A list row is its list's `id`
---with the row's number, such as "pages.3" for row 3 of a list with `id = "pages"`.
---
---Since: 2.12.0
---@return any|nil id The widget's `id` (or the kit's own id for one without), or nil for no cursor.
function uikit.focused() end

---Puts the keys' cursor on a widget, as an arrow would, or takes it away.
---
---It also calls `rq.uikit.keys` for this update. A widget not drawn on the last update gets the
---cursor when it shows.
---
---Since: 2.12.0
---
---```lua
---rq.uikit.focus("start")  -- the page opens with the cursor on its Start button
---```
---@param id any|nil The widget's `id`, or a list row's (see `focused`); nil takes the cursor away.
function uikit.focus(id) end

---Returns what the kit keeps for a widget.
---
---Since: 1.7.0
---
---```lua
---local top = rq.uikit.state("list:options").top  -- the list's scroll, in pixels
---```
---@param id string The widget's kind and id, such as "list:options".
---@return table state The kept state, made empty on first use.
function uikit.state(id) end

---Draws a piece of the theme at any size.
---
---Raises an error for a name the theme has no piece for.
---
---Since: 1.7.0
---@param piece string|rq.UiPiece A piece name in the theme, or a piece.
---@param x integer Left edge, in game pixels.
---@param y integer Top edge, in game pixels.
---@param w? integer Width. Default: the piece's own.
---@param h? integer Height. Default: the piece's own.
---@param opts? rq.HudCgOpts `alpha` and `tint`, as in `rq.hud.cg`.
function uikit.draw(piece, x, y, w, h, opts) end

---Returns a deep copy of a theme, to change and pass to `rq.uikit.use`.
---
---A piece in the copy may point at a CG from `rq.image.add`.
---
---Since: 1.7.0
---@param theme? rq.UiTheme The theme to copy. Default: Config's.
---@return rq.UiTheme copy The copy.
function uikit.copy_theme(theme) end

---Uses a theme from now on, for every mod.
---
---Set it back when your HUD closes.
---
---Raises an error when `theme` is not a table.
---
---Since: 1.7.0
---@param theme rq.UiTheme The theme.
function uikit.use(theme) end

---Returns a piece as layers for `rq.image.compose`, to make a CG of it.
---
---The Mods tab of Config is made this way: Other 2's tab with its label cut away and new text on it.
---
---Since: 1.8.0
---@param piece string|rq.UiPiece A piece name in the theme, or a piece.
---@param x? integer Left edge in the new CG. Default 0.
---@param y? integer Top edge in the new CG. Default 0.
---@param w? integer Width. Default: the piece's own.
---@param h? integer Height. Default: the piece's own.
---@return rq.ImageLayer[] layers The layers.
function uikit.layers(piece, x, y, w, h) end

---Draws the close button of the hub's windows (24 by 20).
---
---Since: 2.6.0
---@param opts? rq.UiPlace|{disabled?: boolean} Place, and `disabled`.
---@return boolean clicked true on the update it is clicked.
function uikit.close(opts) end

---Draws a room row of Rance Castle: the grey button with the room's name, the face beside it, and
---the NEW and heart marks.
---
---The row is 270 by 36; the button is at +3,+3 and 206 by 30, and a long name gets smaller until it
---fits. The face is any CG, drawn at 24 by 29 in its frame; without one the frame is black.
---
---Raises an error for a mark that is not "heart" or "run".
---
---Since: 2.6.0
---
---```lua
---if rq.uikit.castle_row({ x = 145, y = 82, text = "Lab", face = 12345, new = true, id = "lab" }) then
---  page_open = true
---end
---```
---@param opts rq.UiPlace|{text?: string, face?: integer, new?: boolean, mark?: "heart"|"run", disabled?: boolean} `text`, `face` (a CG number), `new`, `mark` ("heart", or "run" for MORU RUN), `disabled`.
---@return boolean clicked true on the update its button is clicked.
function uikit.castle_row(opts) end

---Draws a quest row: the number, the yellow name, and the clear count in its box on the left.
---
---The row is 228 by 30. A number is written with three digits ("077"); a long name gets smaller until
---it fits. `mark` past 9 shows "★", as the game does; 0 shows nothing. The box for the count is
---38 pixels left of the row; `box = false` leaves it out.
---
---Since: 2.6.0
---@param opts rq.UiPlace|{no?: integer|string, name?: string, mark?: integer|string, box?: boolean, current?: boolean, disabled?: boolean} `no`, `name`, `mark` (the clear count, or a text), `box`, `current` (covered in see-through cyan, as the game marks a quest it locks), `disabled`.
---@return boolean clicked true on the update it is clicked.
function uikit.quest_row(opts) end

---Draws the quest list: rows 34 apart and the game's scroll bar 15 pixels right of them.
---
---13 rows show by default, as on the Quests screen. The wheel over the rows scrolls a row at a time;
---the bar's arrows, its track and a drag of the bar scroll it too. A click on a row makes it the
---current one. The scroll is kept by `opts.id` (`rq.uikit.state("questlist:<id>").top`).
---
---Since: 2.6.0
---
---```lua
---local rows, pick = {}, nil
---for i, q in ipairs(my_quests) do rows[i] = { no = q.no, name = q.name, mark = q.clears } end
---rq.on_frame(function()
---  pick = rq.uikit.quest_list(rows, pick, { x = 330, y = 130, id = "mine" })
---end)
---```
---@param rows {no?: integer|string, name?: string, mark?: integer|string, disabled?: boolean, tip?: string|rq.UiTip}[] The rows; `tip` since 2.13.0.
---@param index? integer The current row; nil for none.
---@param opts? rq.UiPlace|{shown?: integer} Place, `id`, and `shown` (rows at once, default 13).
---@return integer|nil index The current row.
---@return boolean changed true on the update it changed.
function uikit.quest_list(rows, index, opts) end

---Draws the game's scroll bar, up and down: two arrows, the track and the bar.
---
---`top`, `count` and `shown` are in any unit: rows, or pixels with `opts.step`. The bar is
---`shown / count` of the track, at least 10 pixels, as the game sizes it. The arrows move by `step`,
---a click on the track by `shown` toward the click, and a drag of the bar goes where the bar goes. The
---wheel over the bar, or over `opts.wheel`, moves by `step`, and the game does not get it.
---
---As the game's bar (`T_ScrollBar`), an arrow or the track moves on the press. Held, it waits 250 ms
---and then moves again on every update while the button stays down, wherever the mouse goes; the
---track stops once the bar reaches the mouse.
---
---Raises an error without `h`, or with an `h` that leaves no room between the arrows.
---
---Since: 2.6.0
---Changed: 2.10.0 Moves on the press, and again while held.
---@param top integer The first shown.
---@param count integer How many there are.
---@param shown integer How many show at once.
---@param opts rq.UiPlace|{step?: integer, wheel?: integer[], label?: string, disabled?: boolean} `h` is needed (with the two 40 high arrows). `step` (default 1), `wheel = {x, y, w, h}`, `label`, `disabled`.
---@return integer top The new first shown.
---@return boolean changed true on the update it changed.
function uikit.scrollbar(top, count, shown, opts) end

---Draws a Frostbuy rack tile: a dark see-through square in the shop's black and yellow lines, with
---yellow text.
---
---The shop's own racks are painted into its picture, so the tile is drawn from shapes and the game's
---font; its lettering is close to the painted one, not the same. "\n" starts a new line, and all
---lines take the size that fits the widest. Under the mouse the game's cover pulses over it.
---
---Since: 2.6.0
---
---```lua
---if rq.uikit.rack("MOD\nRACK", { x = 560, y = 123 }) then shop_open = true end
---```
---@param text string The text.
---@param opts? rq.UiPlace|{disabled?: boolean} Place (`w` and `h` default 104 by 106), and `disabled`.
---@return boolean clicked true on the update it is clicked.
function uikit.rack(text, opts) end

---Draws the sort column: "SORT" over up to 16 buttons, 30 apart, the current one framed in yellow.
---
---The game's column is see-through, on the Quests screen's parchment; the kit puts that parchment
---under it unless `opts.back = false`. A long label gets smaller until it fits. The current button
---cannot be clicked, unless `again` names it: a store's SORT is this column with two buttons, and
---its second one opens the sort menu each time.
---
---Since: 2.6.0
---Changed: 2.10.0 `again`, and the button clicked.
---@param list string[] The labels.
---@param index? integer The current one; nil for none.
---@param opts? rq.UiPlace|{back?: boolean, again?: boolean|integer, disabled?: boolean, tips?: (string|rq.UiTip)[]} Place, `back`, `again` (true, or the index of the button that can be clicked while current), `disabled`, `tips` (2.13.0: one for each button).
---@return integer|nil index The current one.
---@return boolean changed true on the update it changed.
---@return integer|nil clicked The index of the button clicked on this update, current or not.
function uikit.sort(list, index, opts) end

---Draws a page of a mod's own over the hub and runs `fn` in a column inside it.
---
---The page covers the hub under the town tabs and above the footer (0, 56, 1024 by 536). The game
---shows dimmed under it and gets no mouse and no keys (`rq.hud.capture_mouse`, `rq.hud.block_keys`),
---so it is modal. With `title` the title sits on the current town tab's piece at the top left. The
---page closes by its close button at the top right, a right click, or Esc; the right click and Esc
---close when let go, so the game does not get them. Keep the page's state yourself: draw it while
---it is open, and stop when it returns false. It does not open by itself; open it from a button of
---your own or a key.
---
---A choice menu from `rq.ui.choose` shows over the page. While it does, the page leaves the mouse and
---the keys to the menu, and the right click, Space or Esc that ends the menu does not close the page.
---
---The arrow keys and Enter move the keys' cursor between the page's widgets (`rq.uikit.keys`).
---
---Since: 2.6.0
---Changed: 2.7.0 Stays open, under it, while the game's choice menu shows over it.
---Changed: 2.12.0 The arrow keys and Enter move between its widgets.
---
---```lua
---local open = false
---rq.on_frame(function()
---  if not open then return end
---  open = rq.uikit.page({ id = "mine", title = "My Page" }, function(x, y, w, h)
---    rq.uikit.button("Do it", { style = "large" })
---  end)
---end)
---```
---@param opts? rq.UiPlace|{title?: string, pad?: integer, gap?: integer} `x`, `y`, `w`, `h` (the hub's area by default), `id` (keeps its state), `title`, `pad` (default 16), `gap` between widgets.
---@param fn? fun(x: integer, y: integer, w: integer, h: integer) The widgets inside, in a column at the box under the title, clipped to it.
---@return boolean open false on the update it closes.
function uikit.page(opts, fn) end

---One row of `rq.uikit.options`.
---
---Since: 2.7.0
---@class rq.UiOptionRow
---@field id? any What `options` returns for this row. Default: its place in the list, counting separators.
---@field text string The text.
---@field r? integer Red, 0 to 255. Default: the game's grey.
---@field g? integer Green, 0 to 255. Default: the game's grey.
---@field b? integer Blue, 0 to 255. Default: the game's grey.
---@field comment? string Shown in the game's comment window right of the list, at the row, while the mouse or the keys' cursor stays on the row (as the game's menu shows it). Before 2.13.0, a plain box at once.
---@field disabled? boolean Greyed out; it cannot be picked.

---Draws the game's choice menu as a widget: its green base, grey rows, separators and a text panel
---at the top, laid out as the game lays out the menu. It is not modal and does not wait: it returns
---the picked row's id on the update it is clicked. Use it inside a page; `rq.ui.choose` opens the
---game's own menu instead.
---
---The list is 380 wide. With fewer than 3 rows to pick it draws the tall buttons, as the game does.
---`message` goes in the text panel at the top, in up to 3 lines of 346 pixels. A long row text gets
---smaller until it fits.
---
---Its rows are on the kit's keys cursor (see `rq.uikit`): Up and Down go through the rows that can be
---picked, wrapping round as the game's menu does, and Enter picks the cursor's row, framed in yellow
---as under the mouse. With `keys` the widget takes the keys itself (`rq.uikit.keys`), and Esc picks
---Return; the arrows, Enter and Esc are hidden from the game while it shows.
---
---The list grows to `h` (default and at most 762 pixels, the game's base). Past it, the rows under
---the message scroll inside the base, with the game's scroll bar right of it (22 pixels more): the
---wheel over the list and the bar move them a row at a time, and the keys' cursor stays in sight.
---The message stays at the top. Only the rows wholly in sight are listed for `rqmod-ctl`.
---
---Raises an error without a list, with a row without text, or with an `h` that is not a whole
---number.
---
---Since: 2.7.0
---Changed: 2.9.0 `keys`.
---Changed: 2.10.0 `h`, and rows past it scroll.
---Changed: 2.11.0 A row's `comment` box is drawn on the top layer, over widgets drawn later.
---Changed: 2.12.0 The rows are on the kit's keys cursor; Left and Right leave the list.
---Changed: 2.13.0 A row's `comment` is the game's comment window, after 500 ms, as the game's menu shows it.
---
---```lua
---local pick = rq.uikit.options({ "Attack", "Defense", "-", { id = "hp", text = "HP" } },
---  { id = "sortby", message = "Sort by", back = true })
---if pick == 0 then open = false elseif pick then sort_by(pick) end
---```
---@param items (string|rq.UiOptionRow)[] The rows: a text, `"-"` for a separator, or a table.
---@param opts? rq.UiPlace|{message?: string, back?: string|boolean, keys?: boolean, h?: integer} Place and `id` (keeps the scroll); `message` (the text panel at the top); `back` (a Return row, true or its text; picking it gives 0); `keys` (the arrow keys, Enter and Esc); `h` (the most height, default 762).
---@return any|nil id The picked row's id on the update it is clicked or Enter picks it (0 for Return); nil otherwise.
---@see rq.ui.choose
function uikit.options(items, opts) end

---An item row's data, as `rq.uikit.item_data` reads it from the game. A table of your own with the
---same fields draws a made-up row.
---
---Since: 2.8.0
---@class rq.UiItemRow
---@field ii? integer The instance; nil for a made-up row.
---@field name? string The name (`アイテム_名前`).
---@field level? string The level line on the red bar (`アイテム_実装条件`), or "" for no bar.
---@field info? string The stats line with the game's marks (`アイテム_情報`): `$` starts a line, numbers draw white, text between two `|` is white and between two backquotes stays yellow.
---@field icon? integer The icon CG (`アイテム_ＣＧ番号`), drawn 100 by 100.
---@field rank? integer The enhancement rank (`アイテム_レアランク`); its badge (CG 11420 + rank) shows on the icon when above 0.
---@field kind? integer The game's item kind (`アイテム_種類`): 1 weapon, 11 armor, 21 special, 17 item. Its mark shows at the top of a row without buttons.
---@field locked? boolean Locked in the vault (`アイテム_ＬＯＣＫ中`): LOCKED shows (not in the shop style), the lock button reads UNDO LOCK, and SELL and DONATE do not show.
---@field price? integer The price (`アイテム_価格`); the shop style shows it in gold digits.
---@field place? integer Where it is (`アイテム_存在`): 7 the vault, 11 the storage, 6 a shop, 8 quest storage, or the owner's roster slot while equipped.
---@field classes? integer[] The classes that may equip it, 1 to 8 (`アイテム_装備職業`); their icons show at the bottom.
---@field discard? boolean The game lets it go: it can be sold or donated (`アイテム_破棄可能`; false for quest items). Since 2.9.0.
---@field disabled? boolean In a list: the row is greyed out and cannot be picked.

---Reads an item instance's row data from the game: its name, level line, stats line, icon, rank,
---kind, lock, price, place, the classes that may equip it, and whether the game lets it go.
---
---The vault's widgets read it for you; call it to read an item once, or to change a field before
---drawing the row.
---
---Raises an error when `ii` is not a whole number.
---
---Since: 2.8.0
---Changed: 2.9.0 `discard`.
---
---```lua
---local row = rq.uikit.item_data(ii)
---if row then rq.log(row.name, row.place) end
---```
---@param ii integer The instance.
---@return rq.UiItemRow|nil row The row data; nil when there is no such instance.
function uikit.item_data(ii) end

---Makes item rows and lists read an instance again on their next update.
---
---Lists keep each instance's data for a second, so a change, such as a lock, shows within a second
---by itself. Call this after your own change to show it at once. `rq.item.lock`, `sell`, `donate`
---and `store` call it for you, and pickers list their items again.
---
---Since: 2.8.0
---Changed: 2.9.0 Pickers and `item_storage` list again too.
---@param ii? integer The instance; nil for all of them.
function uikit.item_refresh(ii) end

---Draws a vault item row, 381 by 128, as the game draws it: the name, the icon with its rank badge,
---the red level bar, the stats line in the game's yellow and white, the icons of the classes that
---may equip it, LOCKED, and the row's buttons.
---
---The background is cut from the vault's panel (`style = "vault"`), the storage's red one
---(`"storage"`), the shop's green one with the price in its GOLD box (`"shop"`), or a character's
---brown equip list (`"equip"`). The name is centered and gets smaller until it fits, down to 12
---pixels; past that it wraps in two lines of 12 pixels, as the game's does. The stats line wraps at
---264 pixels as the game's does, in 3 lines under a level bar and 4 without.
---
---`buttons` adds the vault's LOCK (UNDO LOCK on a locked item), SELL and DONATE buttons at their
---places; a click on one is not a click on the row. A locked row has no SELL or DONATE, as in the
---vault. With `keys` on the current row, L, S and D press its LOCK, SELL and DONATE, as in the vault.
---The buttons only say which was clicked: call `rq.item.lock`, `sell` or `donate` to do it. The row
---is listed for `rqmod-ctl` as `row-<id>`, its buttons as `button-<id>-lock`, `-unlock`, `-sell`,
---`-donate`.
---
---Raises an error without a row, for an `ii` with no instance, or with another style.
---
---Since: 2.8.0
---Changed: 2.9.0 The shop and equip styles, two-line names, `keys`, no SELL or DONATE on a locked row.
---
---```lua
---local clicked, button = rq.uikit.item_row(ii, { x = 538, y = 0, id = "mine", buttons = { lock = true } })
---if button == "lock" then rq.item.lock(ii) elseif button == "unlock" then rq.item.lock(ii, false) end
---```
---@param row integer|rq.UiItemRow An instance, or row data (from `item_data`, or made up).
---@param opts? rq.UiPlace|{style?: "vault"|"storage"|"shop"|"equip", buttons?: {lock?: boolean, sell?: boolean, donate?: boolean}, current?: boolean, disabled?: boolean, keys?: boolean} Place and `id`; `style` (default "vault"); `buttons`; `current` (covered in see-through cyan, as the game marks the row it works on); `disabled` (greyed out, no buttons, no clicks); `keys` (L, S and D press the buttons while the row is current).
---@return boolean clicked true on the update the row is clicked.
---@return "lock"|"unlock"|"sell"|"donate"|nil button The button clicked on this update.
function uikit.item_row(row, opts) end

---Draws the vault's item list: rows 128 apart and the game's scroll bar beside them.
---
---6 rows show by default, as in the vault. The wheel over the rows scrolls a row at a time; the bar's
---arrows, its track and a drag of the bar scroll it too. A click on a row makes it the current one
---and chooses it. Rows given as instances are read from the game and kept for a second
---(`item_refresh`). The scroll is kept by `opts.id` (`rq.uikit.state("itemlist:<id>").top`).
---
---With `keys`, the list takes the keys (`rq.uikit.keys`) and its rows are the kit's keys cursor: Up
---and Down move the current row, also on to a widget outside at the list's ends; Page Up and Page
---Down move six rows, Home and End to the first and last, scrolling to keep it shown; a held key
---repeats after 250 ms, every 50 ms. Enter chooses the current row, and L, S and D press its
---buttons. Those keys are hidden from the game while the list shows (`rq.hud.block_keys`).
---
---With `drag`, a row pressed and moved more than 6 pixels is dragged: its icon follows the mouse on
---the top layer (`rq.hud.top`), over every widget and clip, and a list of the same group under it
---lights up. Let go over another list of the group and
---`rq.uikit.item_dropped(group)` returns the row on that update; nothing moves by itself.
---`item_storage` is the game's storage screen built on this.
---
---Raises an error without a list, or when `drag` is not a name.
---
---Since: 2.8.0
---Changed: 2.9.0 `keys`, `drag`, and the fifth return.
---Changed: 2.11.0 The dragged icon is drawn once, on the top layer.
---Changed: 2.12.0 `keys` uses the kit's keys cursor; it repeats after 250 ms, every 50 ms (was 400 and 80).
---
---```lua
---local list = rq.uikit.item_sort(rq.uikit.item_filter(my_iis, { type = 1 }), 11)
---rq.on_frame(function()
---  pick = rq.uikit.item_list(list, pick, { x = 538, y = 0, id = "weapons" })
---end)
---```
---@param rows (integer|rq.UiItemRow)[] The rows: instances or row data.
---@param index? integer The current row; nil for none.
---@param opts? rq.UiPlace|{shown?: integer, style?: "vault"|"storage"|"shop"|"equip", buttons?: {lock?: boolean, sell?: boolean, donate?: boolean}, bar?: {y: integer, h: integer}, keys?: boolean, drag?: string} Place and `id`; `shown` (rows at once, default 6); `style` and `buttons` for each row; `bar` (where the scroll bar runs, default beside the rows); `keys` (the keys above); `drag` (a group name: rows can be dragged to another list of the group).
---@return integer|nil index The current row.
---@return boolean changed true on the update it changed.
---@return "lock"|"unlock"|"sell"|"donate"|nil button A row button clicked on this update.
---@return integer|nil at The index of that button's row.
---@return boolean chosen true on the update a row is clicked or Enter is pressed (2.9.0).
function uikit.item_list(rows, index, opts) end

---Returns the row dropped on this update on another list of a drag group (`item_list`'s `drag`).
---
---Raises an error when `group` is not a name.
---
---Since: 2.9.0
---
---```lua
---rq.on_frame(function()
---  a_pick = rq.uikit.item_list(a, a_pick, { x = 101, y = 0, id = "a", drag = "mine" })
---  b_pick = rq.uikit.item_list(b, b_pick, { x = 538, y = 0, id = "b", drag = "mine" })
---  local row, from, to = rq.uikit.item_dropped("mine")
---  if row then move(row, from, to) end
---end)
---```
---@param group? string The group; nil for any.
---@return integer|rq.UiItemRow|nil row The row as its list holds it; nil when nothing was dropped.
---@return string|nil from The `id` of the list it came from.
---@return string|nil to The `id` of the list it was dropped on.
---@return integer|nil index Its index in the list it came from.
function uikit.item_dropped(group) end

---Draws the game's storage screen: the storage's red list on the left and the vault's list right of
---it (437 pixels apart, as the game places them), each with its scroll bar, the vault's count and
---the storage's in their boxes, and the vault's filter column and SORT. Drag a row from one list to
---the other to move the item (`rq.item.store`).
---
---As on the game's screen, the filters and the sort apply to both lists, and each count is of its
---list as filtered. While a category's sub-filters show over the vault list's first row, that list
---starts a row lower. The sort starts at the stores' saved mode, as `item_picker`'s. The lists are
---read again after a move, a filter or a sort, and every 2 seconds. The lists are listed for
---`rqmod-ctl` as `row-<id>-storage-<n>` and `row-<id>-vault-<n>`; the filters and SORT as in
---`item_picker`. `rq.uikit.state("storage:<id>")` has `filter` and `sort`.
---
---Since: 2.9.0
---Changed: 2.10.0 The filter column and SORT, for both lists.
---
---```lua
---rq.on_frame(function()
---  if not open then return end
---  rq.hud.capture_mouse()
---  local ii, where = rq.uikit.item_storage({ id = "mine" })
---  if ii then rq.log("moved", ii, "to the", where) end
---end)
---```
---@param opts? rq.UiPlace|{shown?: integer} Place (the storage list's top left, default 101, 0) and `id`; `shown` (rows, default 6).
---@return integer|nil ii The instance moved on this update.
---@return "vault"|"storage"|nil where Where it went.
function uikit.item_storage(opts) end

---Draws a number as the game does: the vault's counters (`font = "count"`: the game's font, 20
---pixels, bold, white), or one of the game's digit fonts.
---
---A digit font is a CG cut across into 10 equal cells, 0 to 9, as the game's `T_NumberCg`. By name:
---
---| `font` | CG | Where the game uses it |
---| --- | --- | --- |
---| `"gold"` | 11103 | The vault's and the shop's gold, 16 by 23 |
---| `"hp"` | 10096 | HP in battle, skill uses on the mini cards |
---| `"level"` | 10122 | Levels and maximums on the cards, skill uses on the status page |
---| `"level_max"` | 10124 | A level at its maximum, on the cards (red) |
---| `"status"` | 10321 | The status page's points, EXP and gold; the level up; the results |
---| `"small"` | 10210 | Skill uses on the mini-mini cards |
---| `"world"` | 10507 | The world level on the title |
---| `"gallery"` | 12252 | The CG mode's page numbers |
---
---Any other CG number works as a font too, a mod image from `rq.image.add` included. With `hyphen` its
---CG has an 11th cell, a "-", and a negative number gets it; without, a negative number draws as 0,
---as the game's.
---
---Raises an error for a number that is not whole, an unknown font name, a CG that is not there, or
---another `align`.
---
---Since: 2.8.0
---Changed: 2.9.0 `align` and `gap`.
---Changed: 2.14.0 The game's digit fonts by name or CG number, `hyphen`, and it returns its place.
---Changed: 2.15.0 `tint`.
---
---```lua
---rq.uikit.number(#list, { x = 957, y = 31, w = 39 })
---rq.uikit.number(price, { x = 758, y = 2, font = "gold", w = 118, align = "right", gap = 1 }) -- the shop's
---rq.uikit.number(level, { x = 300, y = 200, font = level >= max and "level_max" or "level" })
---```
---@param value integer The number.
---@param opts? rq.UiPlace|{font?: "count"|"gold"|"hp"|"level"|"level_max"|"status"|"small"|"world"|"gallery"|integer, digits?: integer, align?: "left"|"center"|"right", gap?: integer, hyphen?: boolean, tint?: integer[]} Place (`w` with `align` places it in that width, default centered); `font` (default "count"; a name or a CG number); `digits` (pads with zeros to that many); `gap` (pixels between digits, default 0); `hyphen` (the CG has an 11th cell, "-"); `tint` (2.15.0: multiplies a digit font's colors, as `rq.hud.cg`'s).
---@return integer x The left edge of what was drawn.
---@return integer y Its top edge.
---@return integer w Its width, in pixels.
---@return integer h Its height.
function uikit.number(value, opts) end

---A filter state for `rq.uikit.filters` and `rq.uikit.item_filter`: what the vault's filter column
---has picked. 0 or nil is "all".
---
---Since: 2.8.0
---@class rq.UiFilterState
---@field type? integer The category: 21 special, 1 weapon, 11 armor, 17 item and other.
---@field sub? integer The category's sub-filter, by its place from the right: weapons 1 daggers to 7 staves and 8 others; armor 1 leather to 4 gi and 5 others; special items 1 rank C to 4 rank S and 5 others.
---@field class? integer The class that may equip it, 1 to 8, in the column's order (Fighter, Guard, Gunner, Sorcerer, Healer, Ranger, Brawler, Civilian).
---@field lock? integer 1 locked, 2 unlocked.
---@field equip? integer 1 equippable, 2 unequippable by `chara`. Since 2.9.0.
---@field chara? integer The character `equip` is about (its roster slot, iic); without it `equip` keeps all. Since 2.9.0.

---Draws the vault's filter column and returns what is picked: the categories (Special, Weapon,
---Armor, Item / Other), the classes, and Locked / Unlocked, on the column's grey with its
---"[FILTERING]" heads; and, while a category with sub-filters is picked, its panel of sub-filters
---over the list's first row.
---
---`style` picks the column as each screen paints it: the vault's (`"vault"`), the shop's without
---Locked / Unlocked (`"shop"`), or a character's equip list with Equippable / Unequippable under
---Locked / Unlocked (`"equip"`; put the character in `state.chara`).
---
---Each group works as the vault's: a click picks one, a second click on it clears it; with none
---picked all are lit, with one picked only it is. Picking another category clears the sub-filter.
---The state table is changed in place. The buttons are listed for `rqmod-ctl` as
---`filter-<id>-special`, `-weapon`, `-armor`, `-item`, `-class-1` to `-class-8`, `-locked`,
---`-unlocked`, `-equippable`, `-unequippable`, and the sub-filters by their names
---(`filter-<id>-swords`, `-rank-a`, `-others`).
---
---Raises an error when the state is not a table of whole numbers, or with another style.
---
---Since: 2.8.0
---Changed: 2.9.0 `style`, and Equippable / Unequippable.
---
---```lua
---local state = {}
---rq.on_frame(function()
---  local _, changed = rq.uikit.filters(state, { x = 945, y = 154, id = "mine" })
---  if changed then list = rq.uikit.item_filter(all, state) end
---end)
---```
---@param state? rq.UiFilterState The state; a new one when nil.
---@param opts? rq.UiPlace|{style?: "vault"|"shop"|"equip", back?: boolean, classes?: boolean, lock?: boolean, equip?: boolean, sub?: false|{x: integer, y: integer}} Place (the categories' top left, 945, 154 in the vault, the shop and the equip list) and `id`; `style` (default "vault"); `back` (the column's grey, default true); `classes`, `lock` and `equip` (false leaves them out); `sub` (where the sub-filters' panel goes, default 407 left and 153 up, over the list's first row; false leaves it out).
---@return rq.UiFilterState state The state.
---@return boolean changed true on the update it changed.
function uikit.filters(state, opts) end

---Returns the items of a list that pass a filter state, tested as the vault tests them
---(`アイテム倉庫_一覧_種類確認`, `_職業確認`, `_装備タイプ確認`, `_ＬＯＣＫタイプ確認`).
---
---Raises an error without a list, or when the state is not a table of whole numbers.
---
---Since: 2.8.0
---Changed: 2.9.0 `equip` and `chara`.
---
---```lua
---local swords = rq.uikit.item_filter(rq.items(), { type = 1, sub = 2 })
---```
---@param list (integer|rq.UiItemRow|rq.Item)[] Instances, row data or `rq.items()` entries.
---@param state? rq.UiFilterState The filter; nil keeps all.
---@return table list A new list of the entries that pass, in their order.
function uikit.item_filter(list, state) end

---Sorts a list of items in place as the vault sorts it, with the game's own comparers
---(`compItem_*`).
---
---Mode 1 is the vault's default, by item number. Modes 11 to 28 are its "Default Sort" menu: 11
---Attack, 12 Magic, 13 Accuracy, 14 Critical, 15 Stun, 16 Speed, 17 Defense, 18 Resistance, 19
---Evasion, 20 Resilience, 21 HP, 22 to 26 the five damage reductions (fire, ice, electric, light,
---darkness), 27 low and 28 high enhancement rank. Ties keep the item-number order. It calls the game
---for each pair, so sort when the list changes, not on every update.
---
---Raises an error without a list or with another mode.
---
---Since: 2.8.0
---@param list (integer|rq.UiItemRow|rq.Item)[] Instances, row data or `rq.items()` entries.
---@param mode? integer The mode; default 1.
---@return table list The same list.
---@see rq.uikit.item_sorts
function uikit.item_sort(list, mode) end

---Lists the vault's sort modes 11 to 28 with the game's names (`アイテム倉庫_ソート名`), for a menu.
---
---Since: 2.8.0
---
---```lua
---local rows = {}
---for _, s in ipairs(rq.uikit.item_sorts()) do rows[#rows + 1] = { id = s.mode, text = s.name } end
---rq.ui.choose(rows, function(mode) if mode ~= 0 then sort_by(mode) end end)
---```
---@return {mode: integer, name: string}[] modes The modes, in the menu's order.
function uikit.item_sorts() end

---Draws an item picker over the player's items, laid out as the vault: the list with its scroll
---bar, the filter column 407 pixels right and 154 down, the count of items listed in the column's
---box, and the store's SORT under the column (405 right and 682 down, as at 943, 682 in the game).
---
---SORT is the game's: "Default Sort" sorts by item number, and the second button, which shows the
---mode's short name, opens the game's sort menu (`rq.ui.choose`, modes 11 to 28) each time it is
---clicked. The mode in use is framed. The sort starts at the stores' saved mode
---(`アイテムソートモード`), and a new one is saved, as the game's stores do, so the vault opens
---sorted the same way. A new sort goes back to the top of the list.
---
---It lists the items in the vault, the storage, both, the shop's stock or the quest storage,
---filtered and sorted, and reads them again when the filters or the sort change, after an
---`rq.item` change, and every 2 seconds. The rows and the filter column take the look of the place:
---the shop's green rows with their prices and its column without Locked, the storage's red rows.
---With `chara` the rows are a character's equip list and the column gets Equippable /
---Unequippable for them. The quest storage keeps the game's order unless a sort is picked. While a
---category's sub-filters show, the list starts a row lower, as the vault's does. Clicking a row (or
---Enter, with `keys`) returns its instance; the picker moves nothing. The filters, the sort, the
---scroll and the pick are kept by `opts.id` (`rq.uikit.state("picker:<id>")` has `filter`, `sort`,
---`list` and `ii`).
---
---Raises an error with another place, or when `chara` is not a whole number.
---
---Since: 2.8.0
---Changed: 2.9.0 The shop and quest places, `chara`, `keys`; a click on the current row picks it again.
---Changed: 2.10.0 The game's SORT at the bottom right, in place of the sort button under the column
---(`button-<id>-sort` is now `tab-<id>-sort-default-sort` and the mode's tab); the saved sort mode.
---
---```lua
---local picking = true
---rq.on_frame(function()
---  if not picking then return end
---  rq.hud.capture_mouse() -- the game under it gets no clicks
---  local ii = rq.uikit.item_picker({ x = 538, y = 0, id = "gift" }) -- where the vault's list is
---  if ii then
---    picking = false
---    give(ii)
---  end
---end)
---```
---@param opts? rq.UiPlace|{where?: "vault"|"storage"|"both"|"shop"|"quest", chara?: integer, shown?: integer, style?: "vault"|"storage"|"shop"|"equip", sort?: integer, keys?: boolean} Place and `id`; `where` (default "vault"); `chara` (a character's roster slot: their equip list); `shown` (rows, default 6); `style` of the rows (default from `where`); `sort` (the first sort mode, default the stores' saved one; a mode under 10 is 1, as in the game); `keys` (the list's keys).
---@return integer|nil picked The instance picked on this update.
---@return integer|nil current The instance picked last.
function uikit.item_picker(opts) end

---A page in the Mods tab of the game's Config screen.
---
---Since: 1.8.0
---@class rq.ConfigPage
---@field id? string Keeps the page across reloads. Default: the title.
---@field title string Its row in the list and its heading.
---@field draw fun(ui: rq.uikit, box: {x: integer, y: integer, w: integer, h: integer}) Runs on every update the page shows, inside a column at `box` (738 by 531, under the title). An error is logged once and shown at the bottom of the page.
---@field on_open? fun() The page came on screen.
---@field on_close? fun() It went away: another page, another tab, Config closed.

---A Mods tab in the game's Config screen.
---
---Once a mod adds a page, Config gets a tab after Other 2, with a list of the pages on the left and the
---chosen page on the right, drawn with `rq.uikit`. Config's own tabs, its buttons, the X and the right
---click work as in the game. The tab has no key (F1 to F7 are the other tabs). Keep settings in
---`rq.store.global` when they hold for every profile, or in `rq.store` for one save. Check that
---`rq.config` is there, so the mod still runs on an older loader.
---
---Since: 1.8.0
---
---```lua
---if rq.config then
---  local on = rq.store.global.get("on", false)
---  rq.config.page({
---    id = "my-mod",
---    title = "My Mod",
---    draw = function(ui, box)
---      local v, changed = ui.checkbox(on, "Turn it on", { id = "on" })
---      if changed then
---        on = v
---        rq.store.global.set("on", on)
---      end
---    end,
---  })
---end
---```
---@class rq.config
local config = {}

---Adds a page, or replaces the one with the same id.
---
---Since: 1.8.0
---@param page rq.ConfigPage The page.
---@return string id The page's id.
function config.page(page) end

---Returns whether the Mods page is on screen.
---
---Since: 1.8.0
---@return boolean open true while the page shows and no game dialog (going to the title, quitting) is over it.
function config.is_open() end

---Returns the page shown and every page's id.
---
---Since: 1.8.0
---@return string|nil current The id of the page shown, or nil.
---@return string[] ids Every page's id, in the list's order.
function config.pages() end

---Shows a page, now or the next time the Mods tab opens.
---
---Since: 1.8.0
---@param id string The page's id.
---@return boolean found false when no page has that id.
function config.show(id) end

---Options for `rq.sprite.new` and `Sprite:set`.
---
---`new` needs one of `gif`, `cg`, `fill` or `text`; `set` changes only the options given. A CG that
---`rq.image.replace` or `compose` changes is shown as it is now.
---
---Since: 1.9.0
---@class rq.SpriteOpts
---@field gif? string A GIF file in the mod's folder, or an `rq.image.add_gif` key. It plays over and over from the start. Since 1.10.0.
---@field fps? number Frames a second for the GIF, 0 to 240; 0 (default) uses the GIF's own delays. Since 1.10.0.
---@field clock? "game"|"real" GIFs and motion run on the game's clock, which follows `rq.speed` (default), or on real time. Since 1.10.0.
---@field cg? integer A CG number; `rq.image.add` gives one for a file in the mod.
---@field src? {x: integer, y: integer, w: integer, h: integer}|false A part of the CG; false for the whole again.
---@field fill? {r: integer, g: integer, b: integer, a?: integer} A color; needs `w` and `h`.
---@field text? string Text in the HUD font.
---@field size? number Text size, 4 to 200. Default 16.
---@field color? {r: integer, g: integer, b: integer} Text color. Default white.
---@field edge? boolean The text's dark edge. Default true.
---@field bold? boolean Bold text. Default false.
---@field x? number Left edge before scale and turn, in game pixels.
---@field y? number Top edge before scale and turn, in game pixels.
---@field w? number Stretch to this width; 0 for its own.
---@field h? number Stretch to this height; 0 for its own.
---@field scale? number Sets both `scale_x` and `scale_y`.
---@field scale_x? number Horizontal scale, around `origin`.
---@field scale_y? number Vertical scale, around `origin`.
---@field rotate? number Degrees, clockwise on screen, around `origin`.
---@field origin? {x: number, y: number}|"center" In the sprite's own pixels from its top left. Default `{0, 0}`.
---@field alpha? integer Opacity, 0 to 255.
---@field tint? {r: integer, g: integer, b: integer} Multiplies the colors.
---@field add? {r: integer, g: integer, b: integer} Added to the colors before the tint.
---@field blend? "normal"|"add" "add" adds it to what is under it, for light and glow. Default "normal".
---@field show? boolean Shown. Default true.
---@field z? integer|"back"|"cards"|"under_windows"|"top" A number, or "back" (0, behind all of the game's), "cards" (300, over a fight's cards and the dungeon view, under the bottom bar), "under_windows" (1000, the default: under messages, menus, fades and Config) or "top" (20000, over all of the game's).
---@field keep? "screen"|"removed" "screen" removes it when `rq.ui.get().screen` is no longer the one it was made on. Default "removed": it stays until the mod, a load or the title removes it.
---@field attach? rq.SpriteAttach|false Follows a target; `x`, `y` and `z` are added to its (z default 1). false lets it go. Since 1.11.0.

---What an attached sprite follows. Give one of these.
---
---An attached sprite follows its target's place, not its scale or turn, is hidden while the target
---is, and goes when the target goes. On a card it follows the card's own sprite; the card's parts
---sit over it (marks +1, the HP bar +2, the flame +10, an enemy's weak point +15), so `z = 20` is over
---all of them. A dead enemy's card stays up until the fight ends, and a sprite on it stays too.
---
---Since: 1.11.0
---@class rq.SpriteAttach
---@field card? integer The card at this place, whoever stands there: 1 to 5 allies, 11 to 15 enemies. Only in a fight; goes at its end.
---@field chara? integer This character's card, wherever a switch puts it; hidden while they are not on a card. Only in a fight; goes at its end.
---@field sprite? rq.Sprite Another mod sprite, with its motion. Up to 8 can follow one another, not in a ring. Goes when that one is removed.
---@field game? integer A game sprite number from `rq.sprite.scene` (kind "sprite"). Goes when the game deletes it.

---A mod sprite among the game's own, made by `rq.sprite.new`.
---
---The methods that return the sprite can be chained.
---
---Since: 1.9.0
---@class rq.Sprite
local Sprite = {}

---Changes only the options given.
---
---Raises an error for a bad option, or after the sprite was removed.
---
---Since: 1.9.0
---@param opts rq.SpriteOpts The options to change.
---@return rq.Sprite self The sprite.
function Sprite:set(opts) end

---Returns the sprite's options now.
---
---Besides the options it has `id`, `mod`, `w` and `h` before the scale (the content's own size when
---not set), `origin` as `{x, y}`, `screen` (with `keep = "screen"`), `in_scene` (shown and drawn),
---`clock` and `moving`. A GIF sprite adds `gif`, `frame`, `frames`, `gif_ms` (one time through),
---`fps` and `playing`. An attached sprite adds `attach` and `drawn = {x, y, z}`, where it is drawn now.
---
---Raises an error after the sprite was removed.
---
---Since: 1.9.0
---Changed: 1.10.0 Added the GIF and motion fields.
---Changed: 1.11.0 Added `attach` and `drawn`.
---@return table opts A copy of the options.
function Sprite:get() end

---Removes the sprite.
---
---`set` and `get` raise an error after that.
---
---Since: 1.9.0
function Sprite:remove() end

---Returns whether the sprite is still there.
---
---Since: 1.9.0
---@return boolean alive false once it was removed: by the mod, a load, the title or `keep`.
function Sprite:alive() end

---Plays the sprite's GIF.
---
---Raises an error for a sprite with no GIF, or a range outside its frames.
---
---Since: 1.10.0
---@param opts? {from?: integer, to?: integer, loop?: boolean, on_end?: fun(s: rq.Sprite)} `from` (default 1) to `to` (default the last frame), over again unless `loop = false`. `on_end` runs when a play without loop reaches its last frame.
---@return rq.Sprite self The sprite.
function Sprite:play(opts) end

---Holds the GIF on the frame it is on.
---
---Since: 1.10.0
---@return rq.Sprite self The sprite.
function Sprite:stop() end

---Returns the GIF frame shown, or shows one and stops.
---
---Raises an error for a frame outside the GIF.
---
---Since: 1.10.0
---@param n? integer The frame to show, from 1; leave it out to read.
---@return integer|rq.Sprite frame Without `n`: the frame shown. With `n`: the sprite.
---@return integer? count Without `n`: how many frames there are.
function Sprite:frame(n) end

---Returns whether the sprite's GIF is playing.
---
---Since: 1.10.0
---@return boolean playing true while it plays.
function Sprite:playing() end

---The values `Sprite:to` moves to, and how.
---
---Since: 1.10.0
---@class rq.SpriteMotion
---@field x? number Left edge to move to.
---@field y? number Top edge to move to.
---@field w? number Width to move to.
---@field h? number Height to move to.
---@field scale? number Scale to move to, both ways.
---@field scale_x? number Horizontal scale to move to.
---@field scale_y? number Vertical scale to move to.
---@field rotate? number Turn to move to, in degrees.
---@field alpha? integer Opacity to move to.
---@field tint? {r: integer, g: integer, b: integer} Tint to move to.
---@field add? {r: integer, g: integer, b: integer} Added color to move to.
---@field ms? number How long, in ms. Default 0, a jump.
---@field delay? number Ms before it starts. Default 0.
---@field ease? "linear"|"in"|"out"|"in_out" "in" starts slow, "out" ends slow. Default "linear".
---@field on_end? fun(s: rq.Sprite) Runs once a frame has drawn the last values; it may call `to` again for the next step.

---Moves the sprite from the values it has now to new ones, over time.
---
---A new `to` takes the place of the one moving, from where it got to, and the old `on_end` does not
---run. `Sprite:set` of a value takes that value out of the motion, and the rest goes on. Removing the
---sprite ends it too.
---
---Raises an error for a negative `ms` or `delay`, an unknown `ease`, or an `on_end` that is not a
---function.
---
---Since: 1.10.0
---
---```lua
---rq.sprite.new({ fill = { 255, 255, 255 }, w = 1024, h = 768, z = "top" })
---  :to({ alpha = 0, ms = 300, on_end = function(s) s:remove() end })  -- a white flash
---```
---@param opts rq.SpriteMotion The values and the timing.
---@return rq.Sprite self The sprite.
function Sprite:to(opts) end

---Returns whether a motion is waiting or moving.
---
---Since: 1.10.0
---@return boolean moving true until the motion ends.
function Sprite:moving() end

---Stops the motion where it is, without its `on_end`.
---
---Since: 1.10.0
---@return rq.Sprite self The sprite.
function Sprite:halt() end

---One row of `rq.sprite.scene`.
---
---Since: 1.9.0
---@class rq.SceneRow
---@field kind "sprite"|"part"|"mod"|"other" What it is: a game sprite, a game part, a mod sprite.
---@field no integer The sprite or part number; a mod sprite's id.
---@field z integer Its z.
---@field z2 integer Its second z.
---@field x integer Left edge, in game pixels.
---@field y integer Top edge, in game pixels.
---@field w integer Width.
---@field h integer Height.
---@field cg integer The CG it shows; 0 for none.
---@field part? string A part's kind: cg, text, flash and so on.
---@field mod? string The mod of a mod sprite.

---Sprites a mod makes once and changes later, drawn among the game's own in z order.
---
---The game's windows, menus and fades cover them where they are above; `rq.hud` is drawn after
---everything and nothing covers it. A sprite shows a CG or a part of one, a GIF, a fill color or text,
---and can be moved, stretched, scaled, turned, faded and tinted, at once or over time, and can follow
---a card or another sprite.
---
---They are the loader's own: the game does not save them or give their numbers out. Every mod sprite
---goes on a load and at the title, and `rq.sprite.on_reset` says so, so a mod can make them again.
---Call `new` from `rq.on_frame` or a callback, not at the top level of `init.lua`. GIF frames are
---textures made once per file and shared: up to 64 GIF files and 64 million pixels in all. A delay
---under 20 ms in a GIF plays as 100 ms, as browsers do.
---
---The game's own z, from `rq.sprite.scene`: 1 to 22 the hub's page and the quest list; 101 to 262 a
---fight's field and cards, the dungeon view; 801 to 823 the bottom bar; 8001 to 8301 messages; 9001
---and 9002 fades; 9101 the System menu; 15000 to 15010 Config.
---
---Since: 1.9.0
---Changed: 1.10.0 GIFs and motion.
---Changed: 1.11.0 Attach.
---
---```lua
---local glow
---rq.on_frame(function()
---  if not glow or not glow:alive() then
---    glow = rq.sprite.new({ cg = rq.image.add("glow", "glow.png"), x = 400, y = 200,
---      origin = "center", blend = "add", alpha = 180 })
---  end
---  glow:set({ rotate = (rq.time_ms() / 20) % 360 })
---end)
---```
---@class rq.sprite
local sprite = {}

---Makes a sprite and shows it.
---
---Up to 256 per mod and 1024 in all.
---
---Raises an error for a bad option (and leaves nothing behind), or past the limits.
---
---Since: 1.9.0
---
---```lua
---rq.battle.on_start(function()
---  -- a star over the card of whoever starts on rank 1, which follows them in a switch
---  rq.sprite.new({ gif = "star.gif", x = 260, y = 4, z = 20, attach = { chara = rq.party.get()[1] } })
---end)
---```
---@param opts rq.SpriteOpts What it shows and where.
---@return rq.Sprite sprite The new sprite.
function sprite.new(opts) end

---Lists this mod's sprites.
---
---Since: 1.9.0
---@return rq.Sprite[] sprites The same objects `new` gave, oldest first.
function sprite.list() end

---Removes all of this mod's sprites.
---
---Since: 1.9.0
---@return integer count How many were removed.
function sprite.clear() end

---Calls `fn` after a load or the title removed every mod sprite.
---
---These are the same callbacks as `rq.on_reset`.
---
---Since: 1.9.0
---@param fn fun(why: "load"|"title") `why` says which one happened.
---@return fun() remove Removes this callback.
function sprite.on_reset(fn) end

---Lists everything in the game's scene list now, back to front.
---
---The pipe's `scene` command gives the same as JSON, with the screen.
---
---Since: 1.9.0
---@return rq.SceneRow[] rows The rows, back to front.
function sprite.scene() end

rq.sound = sound

---Music from a mod's own file, in place of one of the game's tracks.
---
---The game plays music by track number, and reads the number back: whether dungeon music plays,
---what a save plays again on load, what the system bar goes back to. So a mod does not add new
---numbers; it puts its file behind a game track's number (`replace`), everywhere the game plays
---it, or for one time only (`on_play` with `ctx:file`). The game's own fades, crossfades and
---Config's music volume apply to the file as to the game's tracks. `rq.ids.music` names the tracks.
---
---**Files** can be OGG (Vorbis or Opus), FLAC, WAV or MP3, mono or stereo, at any sample rate.
---They stream from the disk while they play. A file loops forever, as the game's tracks do, unless
---the game plays the track once (`音楽１回再生`). It loops between its loop points: `loop_start` /
---`loop_end` in seconds, else `LOOPSTART` with `LOOPLENGTH` or `LOOPEND` tags in the file (in
---samples, as many music packs carry them), else the whole file.
---
---**Two mods** that replace the same track: the later one in load order wins, and the log names
---both. Each mod restores only its own.
---
---The game's looped ambient sounds (rain, wind) are music tracks too: `replace` changes them, and
---`on_play` does not see them.
---
---Since: 2.4.0
---
---```lua
----- A soundtrack pack: the everyday fight track and the indoor dungeon track.
---rq.music.replace(rq.ids.music.BATTLE_2_R8, "music/battle.ogg", { name = "Battle (Remix)" })
---rq.music.replace(rq.ids.music.INDOOR_CRAWL_R8, "music/indoor.ogg", { loop_start = 12.5 })
---```
---@class rq.music
local music = {}

---How `rq.music.replace` and `ctx:file` play a file.
---
---Since: 2.4.0
---@class rq.MusicOpts
---@field loop_start? number Where the loop goes back to after the end, in seconds. Default the file's `LOOPSTART` tag, else 0.
---@field loop_end? number Where the loop ends, in seconds. Default the file's `LOOPLENGTH` or `LOOPEND` tag, else the file's end.
---@field volume? integer The file's own volume, 0 to 100, on top of the game's music volume and fades. Default 100, as loud as the file is.
---@field name? string `replace` only: the track's name in the extras' music room and in `rq.music.info`.

---What `rq.music.info` tells about a track.
---
---Since: 2.4.0
---@class rq.MusicInfo
---@field no integer The track number.
---@field name string The game's name for it (`音楽の名前`), or the mod's `name`; "" when it has none.
---@field exists boolean Whether the game has this track.
---@field file? string The mod's file, as the mod gave it; nil when the game's plays.
---@field mod? string The id of the mod whose file it is.
---@field volume? integer The file's volume, 0 to 100.
---@field loop_start? number The file's loop start, in seconds.
---@field loop_end? number The file's loop end, in seconds.
---@field seconds? number The length, in seconds: the file's, else the game track's.

---What `rq.music.current` gives: the track that plays now.
---
---Since: 2.4.0
---@class rq.MusicNow
---@field no integer The track number.
---@field name string Its name, as in `rq.music.info`.
---@field file? string The mod's file that plays; nil for the game's.
---@field mod? string The id of the mod whose file plays.
---@field pos number Where it plays, in seconds from the file's start.
---@field seconds number The length of what plays, in seconds.

---What `rq.music.on_play` gets: a track the game is about to play.
---
---Since: 2.4.0
---@class rq.MusicPlayCtx
---@field no integer The track the game asks for; `ctx:track` changes it for the callbacks after.
---@field playing integer The track that plays now; 0 for none.
---@field kind "fight"|"dungeon"|"other" "fight": a fight's track (`音楽_戦闘`). "dungeon": a dungeon map's (`迷宮音楽_再生`). "other": the town, scenes, events, the rest.
---@field quest integer The quest in progress (`rq.quests.current()`); 0 for none.
---@field loops integer 0 when it loops until changed, 1 when it plays once.
local MusicPlayCtx = {}

---Plays a mod file for this track this time only.
---
---The path is in the mod folder of the mod whose callback calls it. The file is checked as in
---`replace`. When the track already plays with another file, it starts again with this one.
---
---Raises an error as `rq.music.replace` does.
---
---Since: 2.4.0
---
---```lua
---rq.music.on_play(function(ctx)
---  if ctx.kind == "dungeon" and ctx.quest == rq.ids.quest.MY_QUEST then
---    ctx:file("music/my_dungeon.ogg")
---  end
---end)
---```
---@param path string A path inside the mod's folder.
---@param opts? rq.MusicOpts Loop points and volume; `name` is not used.
function MusicPlayCtx:file(path, opts) end

---Plays another game track in place of this one.
---
---The game then counts that number as the one playing.
---
---Raises an error for a number the game has not.
---
---Since: 2.4.0
---@param no integer The game track to play.
function MusicPlayCtx:track(no) end

---Plays a file wherever the game plays one of its tracks.
---
---It starts the next time the game starts the track, or when a mod plays it (`rq.music.play`). A
---replacement made while the track plays starts the next time the game asks for that track.
---
---Raises an error for a number the game has not, a path out of the mod folder, a missing file, a
---file that is not a sound with 1 or 2 channels, or an option of the wrong type or range. In
---`init.lua` the game has not opened its music yet: the number is checked at the first frame, and a
---wrong one is logged and dropped.
---
---Since: 2.4.0
---
---```lua
---rq.music.replace(rq.ids.music.BATTLE_2_R8, "music/battle.ogg", { loop_start = 8.25, name = "Battle (Remix)" })
---```
---@param no integer The game's track number.
---@param path string A path inside the mod's folder.
---@param opts? rq.MusicOpts Loop points, volume, name.
---@return rq.MusicInfo info What was set, with the loop points the file gave.
---@see rq.music.restore
function music.replace(no, path, opts) end

---Undoes the calling mod's `rq.music.replace` on a track.
---
---It ends the next time the game starts the track. Another mod's replacement stays.
---
---Since: 2.4.0
---@param no integer The track number.
---@return boolean restored false when the mod had not replaced it.
function music.restore(no) end

---Returns what is known of a track: its name, and the mod's file when one replaces it.
---
---A game track's length opens the track, so call it now and then, not every frame.
---
---Since: 2.4.0
---@param no integer The track number.
---@return rq.MusicInfo info The track.
function music.info(no) end

---Returns the track that plays now.
---
---Since: 2.4.0
---@return rq.MusicNow|nil now The track, or nil when no music plays.
function music.current() end

---Plays a game track, as the game does: with its crossfade, and a mod's file when one replaces it.
---
---When it already plays, nothing changes, unless `restart` is set or another file should play now.
---
---Raises an error for a number the game has not.
---
---Since: 2.4.0
---@param no integer The track number.
---@param opts? {restart?: boolean, once?: boolean} `restart`: from the start when it already plays. `once`: play it once, not in a loop.
function music.play(no, opts) end

---Stops the music, with the game's fade.
---
---Since: 2.4.0
function music.stop() end

---Calls `fn` before the game starts a track.
---
---Each time the game changes the music (`SYS_PlayBGM`), with the track it asks for. `ctx:file`
---plays a file of the mod's this time only, and `ctx:track` another game track. Callbacks run in
---load order, and the last `ctx:file` wins. When the track already plays and nothing changes, the
---game goes on with it. An error in a callback is logged and the next still runs.
---
---Since: 2.4.0
---
---```lua
----- Every boss fight of one quest gets the mod's theme.
---rq.music.on_play(function(ctx)
---  if ctx.kind == "fight" and ctx.no == rq.ids.music.BOSS_BATTLE_R8 and ctx.quest == 42 then
---    ctx:file("music/boss42.ogg")
---  end
---end)
---```
---@param fn fun(ctx: rq.MusicPlayCtx) Gets the track.
---@return fun() remove Removes this callback.
function music.on_play(fn) end

rq.music = music
rq.uikit = uikit
rq.config = config
rq.sprite = sprite
rq.store = store
rq.quests = quests
rq.scene = scene
rq.image = image
rq.item = item
rq.ids = ids
rq.gamebutton = gamebutton
rq.statuspage = statuspage
