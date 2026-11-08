-- rq.gamebutton: buttons of mods in the game's own button groups (loader 2.16.0).
--
-- "system-menu" (2.16.0): the SYSTEM menu gets one more row, MODS, under Save, made by
-- the game itself. A click closes the menu as any row does, and the game's choice menu
-- (rq.ui.choose) lists every mod's entry. Mods add entries, not rows.
--
-- "skill-window" (2.17.0): buttons under the Potential Skills list, in the game's look (CG 11153,
-- as the castle's buttons), two to a row, up to four. See the skill window's part below.
--
-- Built on public calls only:
--   rq.image.compose   CG 10051 (the menu's rows) and 10052 (the rows under the cursor) one row
--                      taller: the MODS row is the game's own pieces (the SAVE row's plate, "MOD"
--                      from CG MODE, "S" from MUSIC MODE, the key icon 10086 in the icon box)
--   after T_SystemMenu@createButton(Save or Suspend Save)   T_SystemMenu::createButton(910050)
--                      through rq.ain.method: the game cuts the row from 10052 at 30 × count,
--                      the menu's base is the taller 10051, open() places it at 740 − its height,
--                      and the keys reach it as any row (the input key is made after the rows)
--   after システムメニュー_有効確認   the row is active while an entry is enabled
--   before システムメニュー_処理      910050: open the list; the game then does nothing for it
local rq = ...
local hook, method, g = rq.ain.hook, rq.ain.method, rq.ain.global

local gamebutton = {}
rq.gamebutton = gamebutton

local ROW_ID = 910050
local GROUPS = { ["system-menu"] = true, ["skill-window"] = true }

local entries = {}   -- in the order they were added
local hooked, composed = false, false

local function logf(fmt, ...)
  rq.log("rq.gamebutton: " .. string.format(fmt, ...))
end

local function live(group)
  local out = {}
  for _, e in ipairs(entries) do
    if not e.removed and (not group or e.group == group) then out[#out + 1] = e end
  end
  return out
end

local function is_enabled(e)
  if e.removed then return false end
  local v = e.enabled
  if v == nil then return true end
  if type(v) == "function" then
    local ok, r = pcall(v)
    if not ok then
      logf("%s %s enabled: %s", e.mod, e.id, tostring(r))
      return false
    end
    v = r
  end
  return v ~= false and v ~= nil
end

local function any_enabled(group)
  for _, e in ipairs(entries) do
    if e.group == group and is_enabled(e) then return true end
  end
  return false
end

-- The MODS row in both CGs. Coordinates are in the game's 164 × 276 CGs: nine rows of 30 from y 0,
-- then a 6 pixel bottom edge; each row's plate is x 31 to 154, its icon box x 4 to 26.
local function row_layers(cg)
  return {
    { cg = cg, src = { 0, 0, 164, 270 }, mode = "copy" },
    { cg = cg, src = { 0, 240, 164, 30 }, y = 270, mode = "copy" },        -- the SAVE row
    { cg = cg, src = { 0, 270, 164, 6 }, y = 300, mode = "copy" },         -- the bottom edge
    { fill = { 0x61, 0xb2, 0x76, 255 }, x = 5, y = 275, w = 21, h = 21, mode = "copy" }, -- the icon box
    { cg = cg, src = { 69, 34, 49, 23 }, x = 35, y = 274, mode = "copy" }, -- "MOD" from CG MODE
    { cg = cg, src = { 63, 64, 11, 23 }, x = 84, y = 274, mode = "copy" }, -- "S" from MUSIC MODE
    { cg = 10086, src = { 3, 3, 26, 22 }, x = 6, y = 277, w = 19, h = 17 }, -- the key icon
  }
end

local function compose()
  if composed then return end
  composed = true
  for _, cg in ipairs({ 10051, 10052 }) do
    local ok, err = pcall(rq.image.compose, cg, row_layers(cg), { w = 164, h = 306 })
    if not ok then logf("CG %d: %s", cg, tostring(err)) end
  end
end

local function uncompose()
  if not composed then return end
  composed = false
  pcall(rq.image.restore, 10051)
  pcall(rq.image.restore, 10052)
end

local function open_list()
  local rows, by = {}, {}
  for i, e in ipairs(live("system-menu")) do
    if is_enabled(e) then
      rows[#rows + 1] = { id = i, text = e.label, comment = e.tip }
      by[i] = e
    end
  end
  if #rows == 0 then return end
  if #rows > 20 then
    logf("%d entries; the list shows the first 20", #rows)
    for i = #rows, 21, -1 do rows[i] = nil end
  end
  rq.ui.choose(rows, function(id)
    local e = by[id]
    if not e or e.removed then return end
    local ok, err = pcall(e.on_click, e.handle)
    if not ok then logf("%s %s on_click: %s", e.mod, e.id, tostring(err)) end
  end, { message = "Mods" })
end

local function install()
  hooked = true
  hook("T_SystemMenu@createButton", { after = function(h)
    local id = h.args[1]
    if (id ~= 910001 and id ~= 910009) or #live("system-menu") == 0 then return end
    local ok, err = pcall(method, { "g_sSystemMenu" }, "T_SystemMenu@createButton", ROW_ID, "Mods")
    if not ok then logf("the MODS row: %s", tostring(err)) end
  end })
  hook("システムメニュー_有効確認", { after = function(h)
    if h.args[2] == ROW_ID then h.result = any_enabled("system-menu") end
  end })
  hook("システムメニュー_処理", { before = function(h)
    if h.args[1] == ROW_ID then open_list() end
  end })
end

-- ---- The skill window ("skill-window", 2.17.0) ----
--
-- The window is g_sSkillPoint (T_SkillPoint, the only one); its list of skills to buy is
-- m_sNewSkill (T_NewSkillManager, the only one), a button group of ids 200001 to 200036 that
-- T_NewSkillManager::create makes again on every build (open, a buy, a character change, a page).
-- Built on public calls only:
--   after T_NewSkillManager@create   each entry as a button of that group, ids 200101 to 200104
--                                    (T_ButtonGroup::addButtonRect, its three sprites set to faces
--                                    composed from CG 11153 with the label, the game's frames).
--                                    The window then sets the group's z and shows it.
--   before T_SkillPoint@exec         a click on an entry: the window stays, on_click runs there
--   after スキルポイント_上/下/左/右矢印  the keys: Down from a column's last skill reaches the entries
--   T_SkillPoint@showComment / hideComment   the tip, in the skill comment's window
-- Inactive (status 1) shows the first face darkened, as T_Button::createSp makes it; pressed
-- (status 2 / exec) shows the CG's second cell.
local SW_GRID = { "g_sSkillPoint", "m_sNewSkill", "m_sButton" }
local SW_MGR = { "g_sSkillPoint", "m_sNewSkill" }
local SW_WIN = { "g_sSkillPoint" }
local SW_BASE = 200100
-- Under the list's image (CG 11551's yellow band ends with its grey border at y 658).
local SW_CELLS = { { 531, 660 }, { 745, 660 }, { 531, 692 }, { 745, 692 } }
local SW_W, SW_H = 206, 30
local MIN_INT = -2147483648

local sw = {
  hooked = false,
  by_id = {},      -- button id -> entry, for the grid on screen
  faces = {},      -- label -> { normal, inactive, pressed }
  nfaces = 0,
  tip = nil,       -- the button id whose tip is shown
}

local function call(name, ...)
  local ok, r = pcall(rq.ain.call, name, ...)
  if not ok then logf("%s: %s", name, tostring(r)) end
  return ok and r or nil
end

local function m(path, name, ...)
  local ok, r = pcall(method, path, name, ...)
  if not ok then logf("%s: %s", name, tostring(r)) return nil end
  return r
end

-- The label as the engine's C page buttons of 2.17.0 drew it (T_TextSprite::textDrawCenter2 with space -4 and
-- spacing 0, size 24, face 258), the size shrunk by 2 down to 12 when it is wider than the button.
local LABEL = { font = "game", face = 258, size = 24, space = -4, spacing = 0 }
local function fit_label(text)
  local size = LABEL.size
  while size > 12 do
    local st = { font = "game", face = LABEL.face, size = size, space = LABEL.space, spacing = LABEL.spacing }
    if rq.hud.text_width(text, st) <= SW_W - 4 then break end
    size = size - 2
  end
  return size, LABEL.spacing
end

local function faces_for(label)
  local f = sw.faces[label]
  if f then return f end
  sw.nfaces = sw.nfaces + 1
  local size, spacing = fit_label(label)
  local text = { text = label, w = SW_W, h = SW_H, align = "center", valign = "center", font = "game",
    face = LABEL.face, size = size, spacing = spacing, space = LABEL.space,
    color = { 25, 25, 25 }, outline = { 144, 144, 136 } }
  local function face(state, cell, dark)
    -- Inactive darkens the plate only (T_Button::createSp blends the base sprite; the label is
    -- its own layer above it).
    local layers = { { cg = 11153, src = { SW_W * cell, 0, SW_W, SW_H }, mode = "copy" } }
    if dark then layers[#layers + 1] = { fill = { 0, 0, 0, 128 } } end
    layers[#layers + 1] = text
    return rq.image.compose(string.format("rq.gamebutton:skill:%d:%d", sw.nfaces, state), layers,
      { w = SW_W, h = SW_H })
  end
  f = { face(0, 0, false), face(1, 0, true), face(2, 1, false) }
  sw.faces[label] = f
  return f
end

local function sw_cell_of(id) return id - SW_BASE end

local function sw_control(e, id)
  local name = e.mod .. "." .. e.id
  if id then
    pcall(rq.agent.control, name, { label = e.label, button = id })
  else
    pcall(rq.agent.control, name)
  end
end

local function sw_make(e, id, x, y, active)
  local f = faces_for(e.label)
  m(SW_GRID, "T_ButtonGroup@addButtonRect", id, SW_W, SW_H, SW_W, SW_H, SW_W, SW_H)
  local idx = m(SW_GRID, "T_ButtonGroup@getButtonIndex", id)
  if not idx or idx < 0 then return false end
  for state = 0, 2 do
    m({ "g_sSkillPoint", "m_sNewSkill", "m_sButton", "m_asButton", idx, "m_aspButton", state, "m_spBase" },
      "T_TextSprite@setCg", f[state + 1])
  end
  -- The game's frames (ＣＧ_システムボタン作成): yellow, red under the cursor while inactive.
  m(SW_GRID, "T_ButtonGroup@setButtonFrame", id, 0, -1, -1, 216, 255, 0, 102, MIN_INT, MIN_INT, 0)
  m(SW_GRID, "T_ButtonGroup@setButtonFrame", id, 1, -1, -1, 255, 0, 0, 128, MIN_INT, MIN_INT, 0)
  m(SW_GRID, "T_ButtonGroup@setButtonFrame", id, 2, -1, -1, 216, 255, 0, 102, MIN_INT, MIN_INT, 0)
  m(SW_GRID, "T_ButtonGroup@setButtonPos", id, x, y)
  m(SW_GRID, "T_ButtonGroup@setButtonSoundClick", id, 0)
  m(SW_GRID, "T_ButtonGroup@setButtonShow", id, true)
  m(SW_GRID, "T_ButtonGroup@setButtonActive", id, active)
  return true
end

local function sw_build()
  for _, e in ipairs(entries) do
    if e.group == "skill-window" then
      if e.button then sw_control(e, nil) end
      e.button, e.cell, e.active = nil, nil, nil
      if not e.removed then e.placed = false end
    end
  end
  sw.by_id = {}
  sw.tip = nil
  local list = live("skill-window")
  if #list == 0 then return end
  local first = 1
  for i, e in ipairs(list) do
    local cell = first + i - 1
    if cell > #SW_CELLS then
      if not e.warned then
        e.warned = true
        logf("%s %s: no cell left under the skill list (up to %d)", e.mod, e.id, #SW_CELLS - first + 1)
      end
    else
      local id = SW_BASE + cell
      local active = is_enabled(e)
      if sw_make(e, id, SW_CELLS[cell][1], SW_CELLS[cell][2], active) then
        e.button, e.cell, e.active, e.placed = id, cell, active, true
        sw.by_id[id] = e
        sw_control(e, id)
      else
        logf("%s %s: the button was not made", e.mod, e.id)
      end
    end
  end
end

local function sw_at(cell)
  local e = sw.by_id[SW_BASE + cell]
  if e and e.active then return e.button end
end

-- The entry under a column: the same column first, then the other, row by row.
local function sw_below(col)
  for row = 0, 1 do
    local id = sw_at(row * 2 + col + 1) or sw_at(row * 2 + (1 - col) + 1)
    if id then return id end
  end
end

local function sw_chara()
  local ok, n = pcall(method, { "g_sSkillPoint", "m_sCard" }, "T_StatusCard@getCharaNo")
  return ok and type(n) == "number" and n > 0 and n or nil
end

local function sw_open() return sw_chara() ~= nil end

local function sw_install()
  sw.hooked = true
  hook("T_NewSkillManager@create", { after = function(h)
    if h.result then sw_build() end
  end })
  hook("T_SkillPoint@exec", { before = function(h)
    local e = sw.by_id[h.args[1]]
    if not e then return end
    h.skip = true
    h.result = false
    m(SW_WIN, "T_SkillPoint@hideComment", -1)
    if e.removed or not e.active then
      call("効果音_エラー")
      return
    end
    call("効果音_クリック")
    local ok, err = pcall(e.on_click, e.handle)
    if not ok then logf("%s %s on_click: %s", e.mod, e.id, tostring(err)) end
  end })
  local function grid_id(id) return type(id) == "number" and id > 200000 and id <= 200036 end
  hook("スキルポイント_下矢印", { after = function(h)
    local id = h.args[1]
    if grid_id(id) then
      local col = m(SW_MGR, "T_NewSkillManager@getCol", id)
      if col and m(SW_MGR, "T_NewSkillManager@getLast", col) == id then
        local to = sw_below(col)
        if to then h.result = to end
      end
    elseif sw.by_id[id] then
      local cell = sw_cell_of(id)
      local col = (cell - 1) % 2
      h.result = (cell <= 2 and sw_at(cell + 2)) or m(SW_WIN, "T_SkillPoint@getFirstNew", col) or id
    end
  end })
  hook("スキルポイント_上矢印", { after = function(h)
    local id = h.args[1]
    if not sw.by_id[id] then return end
    local cell = sw_cell_of(id)
    local col = (cell - 1) % 2
    h.result = (cell > 2 and sw_at(cell - 2)) or m(SW_WIN, "T_SkillPoint@getLastNew", col) or id
  end })
  local function side(h)
    local id = h.args[1]
    if not sw.by_id[id] then return end
    local cell = sw_cell_of(id)
    local other = cell % 2 == 1 and cell + 1 or cell - 1
    h.result = sw_at(other) or id
  end
  hook("スキルポイント_左矢印", { after = side })
  hook("スキルポイント_右矢印", { after = side })
  hook("T_SkillPoint@showComment", { after = function(h)
    local id = h.args[1]
    local e = sw.by_id[id]
    if not e or not e.tip or e.removed then return end
    local cell = sw_cell_of(id)
    call("スキル説明_表示２", id, 0, e.label, e.tip, SW_CELLS[cell][1], SW_CELLS[cell][2] + SW_H, 500)
    pcall(rq.ain.set_global, "g_sSkillPoint", "m_nCommentID", id)
  end })
end

local function check_text(v, what, where)
  if type(v) ~= "string" or v == "" then error(where .. ": " .. what .. " must be a non-empty string", 3) end
end

function gamebutton.add(spec)
  if type(spec) ~= "table" then error("rq.gamebutton.add: spec must be a table", 2) end
  local group = spec.group or "system-menu"
  if not GROUPS[group] then error("rq.gamebutton.add: unknown group " .. tostring(group), 2) end
  check_text(spec.id, "id", "rq.gamebutton.add")
  check_text(spec.label, "label", "rq.gamebutton.add")
  if type(spec.on_click) ~= "function" then error("rq.gamebutton.add: on_click must be a function", 2) end
  if spec.enabled ~= nil and type(spec.enabled) ~= "boolean" and type(spec.enabled) ~= "function" then
    error("rq.gamebutton.add: enabled must be a boolean or a function", 2)
  end
  if spec.tip ~= nil and type(spec.tip) ~= "string" then error("rq.gamebutton.add: tip must be a string", 2) end
  local mod = rq.mod and rq.mod.id and rq.mod.id() or "?"
  for _, e in ipairs(entries) do
    if not e.removed and e.mod == mod and e.id == spec.id then
      error("rq.gamebutton.add: this mod already has an entry " .. spec.id, 2)
    end
  end
  local e = { group = group, mod = mod or "?", id = spec.id, label = spec.label, tip = spec.tip,
    enabled = spec.enabled, on_click = spec.on_click }
  local handle = {}
  function handle.id() return e.mod .. "." .. e.id end
  function handle.set(opts)
    if type(opts) ~= "table" then error("rq.gamebutton handle set: opts must be a table", 2) end
    if opts.label ~= nil then check_text(opts.label, "label", "rq.gamebutton handle set"); e.label = opts.label end
    -- skill-window: a label, a tip or enabled shows from the next build of the list.
    if opts.tip ~= nil then
      if type(opts.tip) ~= "string" then error("rq.gamebutton handle set: tip must be a string", 2) end
      e.tip = opts.tip ~= "" and opts.tip or nil
    end
    if opts.enabled ~= nil then
      if type(opts.enabled) ~= "boolean" and type(opts.enabled) ~= "function" then
        error("rq.gamebutton handle set: enabled must be a boolean or a function", 2)
      end
      e.enabled = opts.enabled
    end
  end
  function handle.remove()
    if e.removed then return end
    e.removed = true
    if group == "system-menu" then
      if #live("system-menu") == 0 then uncompose() end
    else
      if e.button then
        sw_control(e, nil)
        m(SW_GRID, "T_ButtonGroup@setButtonShow", e.button, false)
        m(SW_GRID, "T_ButtonGroup@setButtonActive", e.button, false)
      end
      e.placed = false
    end
  end
  e.handle = handle
  entries[#entries + 1] = e
  if group == "system-menu" then
    compose()
    if not hooked then install() end
  elseif not sw.hooked then
    sw_install()
  end
  return handle
end

-- The groups and their entries, for agents and the Lab.
function gamebutton.groups()
  local list = {}
  for _, e in ipairs(live("system-menu")) do
    list[#list + 1] = { id = e.mod .. "." .. e.id, mod = e.mod, label = e.label, enabled = is_enabled(e) }
  end
  local mode = 0
  pcall(function() mode = g("g_sSystemMenu", "m_nMode") end)
  local skill = {}
  for _, e in ipairs(live("skill-window")) do
    skill[#skill + 1] = { id = e.mod .. "." .. e.id, mod = e.mod, label = e.label, enabled = is_enabled(e),
      placed = e.placed == true, button = e.button, x = e.cell and SW_CELLS[e.cell][1],
      y = e.cell and SW_CELLS[e.cell][2] }
  end
  return {
    { name = "system-menu", open = mode > 0, row = #list > 0, entries = list },
    { name = "skill-window", open = sw_open(), entries = skill },
  }
end
