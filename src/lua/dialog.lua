-- rq.ui.confirm, rq.ui.notice and rq.ui.ask (loader 2.19.0): the game's own dialogs, which wait for
-- the answer as the game's code does (notes/plans/plan-ui-kit-leftovers.md section 13).
--
-- Every dialog of the game is its choice menu, g_sSelect, filled from one list of rows
-- (g_sSelectData) and opened by 選択_実行. These calls add the rows the game's own 選択_確認,
-- 選択_通知 and 選択_確認_拡張 add, then open it. A text row shows 3 lines at most (CG 10024 to
-- 10026), so a longer text is given a row for each 3 lines, wrapped as the window wraps
-- (WordWrapText at 346 pixels, spacing 0.95 down to 0.47: kit.wrap, the port in uikit_vault.lua).
-- In a mod's text only "\n" starts a line; @ and $, which the game breaks at, become ＠ and ＄.
--
-- "Don't ask again" (remember): 1 to 99 is the game's own switch (確認なし, Config's Confirmations
-- tab); a text is the mod's own, kept with the loader's keys in rqmod-global.json, cleared by
-- Config's Resetting too (ダイアログフラグ_クリア), and listed on a page in Config's Mods tab.
-- confirm has it since 2.19.0, ask since 2.28.0 (with the row it stands for).
--
-- Over Config (2.28.0): Config draws at z 15000 to 15010, the game's menu at 9991 (T_Select::create)
-- and its comment window at 9801 (T_Comment::create), so a dialog opened over Config was under it,
-- out of sight. While Config shows, both are raised above it, and the HUD's split with them
-- (kit.under_z): the game's screen, Config, a mod's page, then the dialog.
local rq, ui, kit = ...
local call = rq.ain.call
local gui = rq.ui

local STYLE = { font = "game", face = 258, size = 24, space = -4, measure = true }
local WRAP, MAXS, MINS = 346, 0.95, 0.47
-- The window is at most 762 high and does not scroll: in the Lab, 9 lines of text took about as
-- much as 8 rows (33 high each), and 9 lines with 20 rows ran 150 pixels past the screen. So the
-- rows to pick and the lines of text together are at most 20 (a separator a half).
local PER_ROW, MOST_LINES, MOST_ROWS, MOST_ALL = 3, 9, 20, 20
local FIRST = 100 -- ask's rows are 101 and up, as rq.ui.choose's
-- The game's line under its "don't ask again" row. 選択_補助テキスト追加 keeps it in the row's comment,
-- but the window draws the row's text, which is empty: the game shows a gap there, and so do we.
local NOTE = "※Use system config to return to [On]"

local open = false -- one of these calls shows its window
local logged = false

local function logf(fmt, ...)
  rq.log("rq.ui: " .. string.format(fmt, ...))
end

-- ---- The loader's own keys in rqmod-global.json ----
-- rq.store names its keys by the mod whose code is on the stack. A coroutine's stack holds only
-- this file, so its keys are the loader's, the same whichever mod asks.

local function loader_store(fn, ...)
  local co = coroutine.create(fn)
  local ok, r = coroutine.resume(co, ...)
  if not ok then error(r, 0) end
  return r
end

local PREFIX = "dialog:"

local function flag_key(mod, key)
  return PREFIX .. mod .. ":" .. key
end

local function mod_off(mod, key)
  return loader_store(function() return rq.store.global.get(flag_key(mod, key)) ~= nil end)
end

local function set_mod_off(mod, key, label, off)
  loader_store(function()
    rq.store.global.set(flag_key(mod, key), off and (label or key) or nil)
  end)
end

-- Every question turned off: { mod, key, label }, in key order.
local function all_off()
  return loader_store(function()
    local out = {}
    for _, k in ipairs(rq.store.global.keys()) do
      if k:sub(1, #PREFIX) == PREFIX then
        local mod, key = k:sub(#PREFIX + 1):match("^([^:]*):(.*)$")
        if mod then
          out[#out + 1] = { mod = mod, key = key, label = tostring(rq.store.global.get(k, key)) }
        end
      end
    end
    table.sort(out, function(a, b) return a.mod .. ":" .. a.key < b.mod .. ":" .. b.key end)
    return out
  end)
end

local function clear_all()
  for _, f in ipairs(all_off()) do set_mod_off(f.mod, f.key, nil, false) end
end

-- ---- The Confirmations page in Config's Mods tab ----

local page_added, shown = false, nil

local function ensure_page()
  if page_added or not rq.config or not rq.config.page then return end
  page_added = true
  rq.config.page({
    id = "rq.dialogs",
    title = "Confirmations",
    on_open = function() shown = all_off() end,
    draw = function(u, box)
      if not shown then shown = all_off() end
      u.label("Mod questions turned off with \"Don't ask again\". Tick one to ask it again.",
        { w = box.w })
      u.space(0, 12)
      if #shown == 0 then
        u.label("None.", { dim = true })
        return
      end
      for i, f in ipairs(shown) do
        local asks = not mod_off(f.mod, f.key)
        local v, changed = u.checkbox(asks, f.mod .. ": " .. f.label, { id = "rq.dialogs." .. i })
        if changed then set_mod_off(f.mod, f.key, f.label, not v) end
      end
    end,
  })
end

local hooked = false
local function hook_reset(quiet)
  if hooked then return end
  local ok, err = pcall(rq.ain.hook, "ダイアログフラグ_クリア", { after = function()
    clear_all()
    shown = nil
  end })
  if ok then
    hooked = true
  elseif not quiet then
    logf("Resetting will not clear mod questions: %s", tostring(err))
  end
end

hook_reset(true)
local started = false
rq.on_frame(function()
  if started then return end
  started = true
  hook_reset()
  local ok, list = pcall(all_off)
  if ok and #list > 0 then ensure_page() end
end)

-- ---- Over Config ----

local MENU_Z, MENU_Z_CONFIG = 9991, 16000 -- under the game's debug overlays (18000 up)
local NOTE_Z, NOTE_Z_CONFIG = 9801, 16100
local raised = false
local raise_logged = false

local function raise(on)
  if on == raised then return end
  raised = on
  local from, to = MENU_Z, MENU_Z_CONFIG
  local nfrom, nto = NOTE_Z, NOTE_Z_CONFIG
  if not on then from, to, nfrom, nto = to, from, nto, nfrom end
  -- Both are a local's first value (`int nZ = 9991;`), which rq.ain.patch_int does not see.
  local ok, n = pcall(kit.patch_local_int, "T_Select@create", from, to)
  local ok2, n2 = pcall(kit.patch_local_int, "T_Comment@create", nfrom, nto)
  local menu = ok and n == 1
  if on and (not menu or not ok2 or n2 ~= 1) and not raise_logged then
    raise_logged = true
    logf("over Config: the menu's z: %s; the comment window's: %s. A dialog there may not show",
      tostring(n), tostring(n2))
  end
  kit.under_z((on and menu) and MENU_Z_CONFIG or nil)
end

do
  local ok, err = pcall(rq.ain.hook, "T_Config@run", {
    before = function() raise(true) end,
    after = function() raise(false) end,
  })
  if not ok then logf("dialogs over Config will show under it: %s", tostring(err)) end
end

rq.on_reset(function()
  open = false
  raise(false) -- going to the title from Config does not come back to T_Config::run's after
end)

-- ---- Checks ----

local function running(where)
  local ok, n = pcall(call, "選択データ_数")
  if not ok then
    if tostring(n):find("not running", 1, true) then
      error("rq.ui." .. where .. ": the game is not running yet. Call it from a callback, not at the top level of a mod", 3)
    end
    error(n, 0)
  end
  return n
end

-- Another dialog is being built or shows: the game has one list of rows.
local function busy(where, rows)
  if open or gui.choosing() or rows > 0 then
    if not logged then
      logf("%s: another dialog is open or being built; this one was not shown", where)
      logged = true
    end
    return true
  end
  logged = false
  return false
end

local function opt_type(o, k, want, where)
  local v = o[k]
  if v ~= nil and type(v) ~= want then
    error("rq.ui." .. where .. ": " .. k .. " must be a " .. want, 3)
  end
  return v
end

local function int_opt(o, k, where)
  local v = o[k]
  if v ~= nil and math.type(v) ~= "integer" then
    error("rq.ui." .. where .. ": " .. k .. " must be an integer", 3)
  end
  return v or -1
end

-- The text's rows: the window's wrap, 3 lines to a row. nil for no text.
local function text_rows(text, game, where)
  if text == nil then return nil end
  if type(text) ~= "string" then error("rq.ui." .. where .. ": the text must be a string", 3) end
  if text == "" then return nil end
  if not game then text = text:gsub("@", "＠"):gsub("%$", "＄") end
  local lines = kit.wrap(text, STYLE, WRAP, MAXS, MINS)
  if #lines > MOST_LINES then
    error(string.format("rq.ui.%s: the text takes %d lines; at most %d fit", where, #lines, MOST_LINES), 3)
  end
  local rows, cur = {}, {}
  for _, l in ipairs(lines) do
    cur[#cur + 1] = l[1]
    if #cur == PER_ROW then
      rows[#rows + 1] = table.concat(cur, "\n")
      cur = {}
    end
  end
  if #cur > 0 then rows[#rows + 1] = table.concat(cur, "\n") end
  rows.lines = #lines
  return rows
end

local function add_text(rows)
  if not rows then return end
  for _, t in ipairs(rows) do call("選択_テキスト追加", t, -1, -1, -1) end
  call("選択_区切り追加")
end

local function run(cancel)
  open = true
  local ok, r = pcall(call, "選択_実行", cancel, 0, 0, false)
  open = false
  if not ok then error(r, 0) end
  return r
end

-- "Don't ask again": { off = fn, set = fn } for opts.remember, or nil.
local function remember_of(o, where)
  local remember = o.remember
  if remember == nil then return nil end
  local label = opt_type(o, "remember_label", "string", where)
  local game_id, mod
  if math.type(remember) == "integer" then
    if remember < 1 or remember > 99 then error("rq.ui." .. where .. ": remember must be 1 to 99 or a text", 3) end
    game_id = remember
  elseif type(remember) == "string" and remember ~= "" then
    mod = rq.mod.id() or "_"
  else
    error("rq.ui." .. where .. ": remember must be 1 to 99 or a text", 3)
  end
  return {
    off = function()
      if game_id then return call("確認なし", game_id) == true end
      return mod_off(mod, remember)
    end,
    set = function(v)
      if game_id then
        call(v and "確認なし_ＯＮ" or "確認なし_ＯＦＦ", game_id)
      else
        set_mod_off(mod, remember, label, v)
        if v then ensure_page() end
        shown = nil
      end
    end,
  }
end

-- The game's row under the choices: "DON'T ASK AGAIN" (11) with its note, or its "on" look (12).
local function add_remember(flag)
  call("選択_区切り追加")
  if not flag then
    call("選択_拡張項目追加", 11, 10027, "")
    call("選択_補助テキスト追加", NOTE, -1, -1, -1)
  else
    call("選択_拡張項目追加", 12, 10028, "")
  end
end

-- ---- rq.ui.confirm ----

function gui.confirm(text, o)
  if o ~= nil and type(o) ~= "table" then error("rq.ui.confirm: opts must be a table", 2) end
  o = o or {}
  local game = opt_type(o, "game", "boolean", "confirm")
  local rows = text_rows(text, game, "confirm")
  if not rows then error("rq.ui.confirm: the text must not be empty", 2) end
  local yes = opt_type(o, "yes", "string", "confirm") or "Yes"
  local no = opt_type(o, "no", "string", "confirm") or "No"
  local cursor = o.cursor
  if cursor ~= nil and cursor ~= "yes" and cursor ~= "no" then
    error('rq.ui.confirm: cursor must be "yes" or "no"', 2)
  end
  local sy, sn = int_opt(o, "sound_yes", "confirm"), int_opt(o, "sound_no", "confirm")
  local rem = remember_of(o, "confirm")
  local n = running("confirm")

  if rem and rem.off() then return true end
  if busy("confirm", n) then return nil end
  local flag = false
  while true do
    add_text(rows)
    call("選択_項目追加", 1, yes, -1, -1, -1, "", sy)
    call("選択_項目追加", 2, no, -1, -1, -1, "", sn)
    if rem then add_remember(flag) end
    call("選択_カーソル設定", cursor == "no" and 2 or 1)
    local r = run(false)
    if r == 1 then return true end
    if r == 11 and rem then
      rem.set(true)
      flag = true
    elseif r == 12 and rem then
      rem.set(false)
      flag = false
    else
      return false
    end
  end
end

-- ---- rq.ui.notice ----

function gui.notice(text, o)
  if o ~= nil and type(o) ~= "table" then error("rq.ui.notice: opts must be a table", 2) end
  o = o or {}
  local rows = text_rows(text, opt_type(o, "game", "boolean", "notice"), "notice")
  if not rows then error("rq.ui.notice: the text must not be empty", 2) end
  local ok = opt_type(o, "ok", "string", "notice") or "OK"
  if busy("notice", running("notice")) then return nil end
  add_text(rows)
  call("選択_項目追加", 1, ok, -1, -1, -1, "", -1)
  call("選択_カーソル設定", 1)
  run(false)
  return true
end

-- ---- rq.ui.ask ----

local function ask_rows(items)
  if type(items) ~= "table" then error("rq.ui.ask: expected a list of rows", 3) end
  local rows, ids, picks = {}, {}, 0
  for i, it in ipairs(items) do
    if it == "-" then
      rows[#rows + 1] = { sep = true }
    else
      local text, id, r, g, b, comment, sound
      if type(it) == "string" then
        text, id = it, i
      elseif type(it) == "table" then
        text = it.text
        if it.id ~= nil then id = it.id else id = i end
        r, g, b, comment, sound = it.r, it.g, it.b, it.comment, it.sound
      end
      if type(text) ~= "string" then error("rq.ui.ask: row " .. i .. " has no text", 3) end
      picks = picks + 1
      ids[FIRST + picks] = id
      rows[#rows + 1] = { no = FIRST + picks, text = text, r = r or -1, g = g or -1, b = b or -1,
        comment = comment or "", sound = sound or -1, id = id }
    end
  end
  if picks == 0 then error("rq.ui.ask: no row to pick", 3) end
  if picks > MOST_ROWS then error("rq.ui.ask: at most " .. MOST_ROWS .. " rows to pick", 3) end
  return rows, ids, picks
end

function gui.ask(text, items, o)
  if o ~= nil and type(o) ~= "table" then error("rq.ui.ask: opts must be a table", 2) end
  o = o or {}
  local rows_t = text_rows(text, opt_type(o, "game", "boolean", "ask"), "ask")
  local rows, ids, picks = ask_rows(items)
  local rem = remember_of(o, "ask")
  local answer
  if rem then
    -- The row "don't ask again" stands for: the game's is Yes, its first row.
    for _, r in ipairs(rows) do
      if not r.sep and (o.remember_answer == nil or r.id == o.remember_answer) then
        answer = r.id
        break
      end
    end
    if answer == nil then error("rq.ui.ask: remember_answer is not the id of a row", 2) end
  elseif o.remember_answer ~= nil then
    error("rq.ui.ask: remember_answer needs remember", 2)
  end
  local size, seps = picks + (rows_t and rows_t.lines or 0), 0
  for _, r in ipairs(rows) do
    if r.sep then seps = seps + 1 end
  end
  if rem then size, seps = size + 2, seps + 1 end -- the row, its note and a separator
  size = size + seps // 2
  if size > MOST_ALL then
    error(string.format("rq.ui.ask: %d rows and lines of text; at most %d fit on the screen", size, MOST_ALL), 2)
  end
  local cancel = opt_type(o, "cancel", "boolean", "ask") or false
  local x, y = o.x, o.y
  if (x ~= nil and math.type(x) ~= "integer") or (y ~= nil and math.type(y) ~= "integer") then
    error("rq.ui.ask: x and y must be integers", 2)
  end
  local n = running("ask")
  if rem and rem.off() then return answer end
  if busy("ask", n) then return nil end
  local flag = false
  while true do
    if x then call("選択_位置設定", x, y or -1) end
    add_text(rows_t)
    local cursor
    for _, r in ipairs(rows) do
      if r.sep then
        call("選択_区切り追加")
      else
        call("選択_項目追加", r.no, r.text, r.r, r.g, r.b, r.comment, r.sound)
        if o.cursor ~= nil and r.id == o.cursor and not cursor then cursor = r.no end
      end
    end
    if rem then add_remember(flag) end
    if cursor then call("選択_カーソル設定", cursor) end
    local no = run(cancel)
    if rem and no == 11 then
      rem.set(true)
      flag = true
    elseif rem and no == 12 then
      rem.set(false)
      flag = false
    elseif not no or no <= 0 then
      return 0
    else
      return ids[no] or 0
    end
  end
end
