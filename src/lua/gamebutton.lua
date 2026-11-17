-- rq.gamebutton: buttons of mods in the game's own button groups (loader 2.16.0).
--
-- "system-menu" (2.16.0): the SYSTEM menu gets one more row, MODS, under Save, made by
-- the game itself. A click closes the menu as any row does, and the game's choice menu
-- (rq.ui.choose) lists every mod's entry. Mods add entries, not rows.
--
-- "skill-window" (2.17.0): buttons under the Potential Skills list, in the game's look (CG 11153,
-- as the castle's buttons), two to a row, up to four. See the skill window's part below.
--
-- "status" (2.20.0): icon buttons in a character's status page's title row, up to two, and (2.58.0)
-- icons that are not buttons at the left end of the LV row, up to two. See the status page's part
-- below.
--
-- "map" (2.32.0): tabs on a quest's map, attached to the panel's left edge under the area map, up to
-- four. See the map panel's part below.
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
local GROUPS = { ["system-menu"] = true, ["skill-window"] = true, ["status"] = true, ["map"] = true }

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

local function is_enabled(e, ...)
  if e.removed then return false end
  local v = e.enabled
  if v == nil then return true end
  if type(v) == "function" then
    local ok, r = pcall(v, ...)
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

-- ---- The status page ("status", 2.20.0) ----
--
-- The page is g_sStatus (T_Status); its buttons are one group, m_sButton, made by T_Status::create.
-- The free room is the title row (the name is drawn from x 245 and the widest name ends at 435, the
-- X is at 510, 65): two button cells, right to left from the X, spot "title". And (2.58.0) the left
-- end of the LV row, plain grey from x 10 to 91 and y 91 to 116 under the class icon (47 to 79, 63
-- to 90) and left of "LV." (from 92): two icon cells, spot "left" (notes/plans/
-- plan-rocky-chunk-indicator.md in the workspace).
-- Built on public calls only:
--   rq.statuspage.on_open      the cells as buttons of that group, ids 100101 to 100104
--                              (T_ButtonGroup::addButtonRect, its three sprites set to faces composed
--                              from the page's own background, CG 11501, under the cell and the icon;
--                              a left cell also from the window's left strip, CG 11500 at 8, 59, which
--                              covers 11501 there and is the grey the screen shows)
--   rq.statuspage.on_update    shown, enabled, icon and tip asked again on every draw of the card
--   after T_Status@checkSelect a click: the page stays, on_click runs there
--   after ステータス_上/下/左/右矢印  the keys: Up from the special slot reaches the title cells
--   after T_Status@showComment the tip, in the game's comment window
-- Inactive (status 1) is the face darkened, as T_Button::createSp darkens its base; pressed is the
-- icon moved 1 right and 1 down, as the game's two-cell CG buttons (10007, 11505, 11506).
-- A left cell is an icon: always inactive (the game's keys skip it, and a click does nothing), its
-- three faces alike (darkened only when `enabled` says false). T_Button shows the frame of the face
-- under the mouse whether the button is active or not, so the inactive face's frame is the title
-- buttons' highlight (yellow-green, 102) instead of the game's red: the mark lights up under the
-- mouse as they do. The page shows its comment under the mouse, as for any greyed button.
local ST_GRID = { "g_sStatus", "m_sButton" }
local ST_WIN = { "g_sStatus" }
local ST_BASE = 100100
-- x, y, w, h, spot. Ids ST_BASE + the index.
local ST_CELLS = {
  { 472, 61, 32, 29, "title" }, { 436, 61, 32, 29, "title" },
  { 20, 90, 32, 26, "left" }, { 56, 90, 32, 26, "left" },
}
local ST_SPOTS = { title = { 1, 2 }, left = { 3, 4 } }
local ST_BACK = 11501
-- The window's left strip (the scroll bar's column and the grey corner), drawn over ST_BACK.
local ST_STRIP, ST_STRIP_X, ST_STRIP_Y = 11500, 8, 59

local st = {
  hooked = false,
  made = false,    -- the cells exist in the group (since the page opened)
  by_id = {},      -- button id -> entry, for this draw
  faces = {},      -- icon .. ":" .. cell -> { normal, inactive, pressed }
  nfaces = 0,
  shown_face = {}, -- button id -> the face key set on it
}

local function ask(e, field, ...)
  local v = e[field]
  if type(v) ~= "function" then return v end
  local ok, r = pcall(v, ...)
  if not ok then
    logf("%s %s %s: %s", e.mod, e.id, field, tostring(r))
    return nil, true
  end
  return r
end

-- The icon fitted in a w × h cell: centered, shrunk with its proportions when larger.
local function icon_layer(icon, cw, ch, dx, dy)
  local w, h = rq.hud.cg_size(icon)
  if not w or w <= 0 or h <= 0 then return nil end
  local s = math.min(1, cw / w, ch / h)
  local fw, fh = math.max(1, math.floor(w * s + 0.5)), math.max(1, math.floor(h * s + 0.5))
  return { cg = icon, x = (cw - fw) // 2 + dx, y = (ch - fh) // 2 + dy, w = fw, h = fh }
end

-- The three faces (normal, inactive, pressed) of an icon in a cell. A button's: darkened when
-- inactive, moved when pressed. An icon's (a left cell): all alike, darkened when `dark`.
local function st_faces(icon, cell, dark)
  local c = ST_CELLS[cell]
  local is_icon = c[5] == "left"
  local key = icon .. ":" .. cell .. (is_icon and (dark and ":dark" or ":icon") or "")
  local f = st.faces[key]
  if f then return f, key end
  st.nfaces = st.nfaces + 1
  local cx, cy, cw, ch = c[1], c[2], c[3], c[4]
  -- The part of the cell the left strip covers (a left cell only).
  local over = 0
  if is_icon then
    local sw = rq.hud.cg_size(ST_STRIP)
    if sw and cx >= ST_STRIP_X then over = math.min(cw, ST_STRIP_X + sw - cx) end
  end
  local function face(state, dx, darken)
    local layers = { { cg = ST_BACK, src = { cx, cy, cw, ch }, mode = "copy" } }
    if over > 0 then
      layers[#layers + 1] = { cg = ST_STRIP, src = { cx - ST_STRIP_X, cy - ST_STRIP_Y, over, ch }, w = over, h = ch }
    end
    local il = icon_layer(icon, cw, ch, dx, dx)
    if il then layers[#layers + 1] = il end
    if darken then layers[#layers + 1] = { fill = { 0, 0, 0, 128 } } end
    return rq.image.compose(string.format("rq.gamebutton:status:%d:%d", st.nfaces, state), layers,
      { w = cw, h = ch })
  end
  if is_icon then
    local one = face(0, 0, dark)
    f = { one, one, one }
  else
    f = { face(0, 0, false), face(1, 0, true), face(2, 1, false) }
  end
  st.faces[key] = f
  return f, key
end

local function st_make()
  st.by_id, st.shown_face = {}, {}
  for cell, c in ipairs(ST_CELLS) do
    local id = ST_BASE + cell
    -- Frame 1 is the inactive face's: the game's red, but on a left cell (always inactive) the
    -- title buttons' highlight, so the mark lights up under the mouse as they do.
    local r1, g1, a1 = 255, 0, 128
    if c[5] == "left" then r1, g1, a1 = 216, 255, 102 end
    m(ST_GRID, "T_ButtonGroup@addButtonRect", id, c[3], c[4], c[3], c[4], c[3], c[4])
    m(ST_GRID, "T_ButtonGroup@setButtonFrame", id, 0, -1, -1, 216, 255, 0, 102, MIN_INT, MIN_INT, 0)
    m(ST_GRID, "T_ButtonGroup@setButtonFrame", id, 1, -1, -1, r1, g1, 0, a1, MIN_INT, MIN_INT, 0)
    m(ST_GRID, "T_ButtonGroup@setButtonFrame", id, 2, -1, -1, 216, 255, 0, 102, MIN_INT, MIN_INT, 0)
    m(ST_GRID, "T_ButtonGroup@setButtonPos", id, c[1], c[2])
    m(ST_GRID, "T_ButtonGroup@setButtonSoundClick", id, 0)
    -- A left cell is never active: T_Button::checkDown would play the deny sound on its click.
    if c[5] == "left" then m(ST_GRID, "T_ButtonGroup@setButtonSoundError", id, 0) end
    m(ST_GRID, "T_ButtonGroup@setButtonShow", id, false)
    m(ST_GRID, "T_ButtonGroup@setButtonActive", id, false)
  end
  st.made = true
end

local function st_control(e, id)
  local name = e.mod .. "." .. e.id
  if id then
    pcall(rq.agent.control, name, { label = e.label, button = id })
  else
    pcall(rq.agent.control, name)
  end
end

-- An entry's icon for this draw: the CG, or a function's answer (2.58.0). nil for none.
local function st_icon(e, chara, mode)
  local v, bad = ask(e, "icon", chara, mode)
  if bad then return nil end
  if math.type(v) == "integer" and v > 0 then return v end
  if not e.warned_icon then
    e.warned_icon = true
    logf("%s %s icon: expected a CG number, got %s", e.mod, e.id, tostring(v))
  end
  return nil
end

-- Every draw of the card: the entries shown get their spot's cells in load order.
local function st_place(chara)
  if not st.made then return end
  local mode = rq.statuspage.mode()
  local old = st.by_id
  st.by_id = {}
  local used = { title = 0, left = 0 }
  for _, e in ipairs(entries) do
    if e.group == "status" then
      local was = e.button
      e.button, e.cell, e.active, e.placed, e.shown_now, e.icon_now = nil, nil, nil, false, false, nil
      if not e.removed then
        local shown, bad = ask(e, "shown", chara, mode)
        if shown == nil and not bad then shown = true end
        e.shown_now = shown == true
        local icon = e.shown_now and st_icon(e, chara, mode)
        if icon then
          local cells = ST_SPOTS[e.spot]
          used[e.spot] = used[e.spot] + 1
          local cell = cells[used[e.spot]]
          if not cell then
            if not e.warned then
              e.warned = true
              logf("%s %s: no cell left in the status page's %s spot (up to %d)", e.mod, e.id, e.spot, #cells)
            end
          else
            local id = ST_BASE + cell
            e.button, e.cell, e.placed, e.icon_now = id, cell, true, icon
            e.active = is_enabled(e, chara)
            st.by_id[id] = e
          end
        end
      end
      if was and was ~= e.button then st_control(e, nil) end
    end
  end
  for c, cell in ipairs(ST_CELLS) do
    local id = ST_BASE + c
    local e = st.by_id[id]
    if e then
      local f, key = st_faces(e.icon_now, c, not e.active)
      if st.shown_face[id] ~= key then
        local idx = m(ST_GRID, "T_ButtonGroup@getButtonIndex", id)
        if idx and idx >= 0 then
          for state = 0, 2 do
            m({ "g_sStatus", "m_sButton", "m_asButton", idx, "m_aspButton", state, "m_spBase" },
              "T_TextSprite@setCg", f[state + 1])
          end
          -- setCg makes the sprite again at 0,0 with no z: the group's z (T_Status::create's
          -- setZ ran before these cells existed) is the X's, and the place again.
          local xi = m(ST_GRID, "T_ButtonGroup@getButtonIndex", 100009)
          local z = xi and xi >= 0 and m({ "g_sStatus", "m_sButton", "m_asButton", xi }, "T_Button@getZ")
          if z and z > 0 then m(ST_GRID, "T_ButtonGroup@setButtonZ", id, z) end
          m(ST_GRID, "T_ButtonGroup@setButtonPos", id, cell[1], cell[2])
          st.shown_face[id] = key
        end
      end
      m(ST_GRID, "T_ButtonGroup@setButtonShow", id, true)
      -- An icon is always inactive: the keys skip it and a click does nothing.
      m(ST_GRID, "T_ButtonGroup@setButtonActive", id, e.spot == "title" and e.active)
      if old[id] ~= e then st_control(e, id) end
    else
      m(ST_GRID, "T_ButtonGroup@setButtonShow", id, false)
      m(ST_GRID, "T_ButtonGroup@setButtonActive", id, false)
    end
  end
end

-- A title cell's button, when it is active (the keys reach only those).
local function st_at(cell)
  local e = st.by_id[ST_BASE + cell]
  if e and e.active and e.spot == "title" then return e.button end
end

-- The rightmost active cell (the first), else the other.
local function st_first() return st_at(1) or st_at(2) end

local function st_install()
  st.hooked = true
  rq.statuspage.on_open(function() st_make() end)
  rq.statuspage.on_update(function(chara) st_place(chara) end)
  rq.statuspage.on_close(function()
    for _, e in ipairs(entries) do
      if e.group == "status" then
        if e.button then st_control(e, nil) end
        e.button, e.cell, e.active, e.placed, e.shown_now, e.icon_now = nil, nil, nil, false, false, nil
      end
    end
    st.made, st.by_id, st.shown_face = false, {}, {}
  end)
  -- T_Status::exec takes the id by reference, so the click is taken where checkSelect returns it;
  -- exec then does nothing for an id it does not know, and the loop keeps the cursor on it.
  hook("T_Status@checkSelect", { after = function(h)
    local e = st.by_id[h.result]
    if not e then return end
    -- An icon (a left cell) takes no click: no sound, and its tip stays.
    if e.spot == "left" then return end
    m(ST_WIN, "T_Status@hideComment", -1)
    if e.removed or not e.active then
      call("効果音_エラー")
      return
    end
    call("効果音_クリック")
    local ok, err = pcall(e.on_click, e.handle)
    if not ok then logf("%s %s on_click: %s", e.mod, e.id, tostring(err)) end
  end })
  local SPECIAL, REMOVE = 130001, 139001
  hook("ステータス_上矢印", { after = function(h)
    local id = h.args[1]
    if id == SPECIAL or id == REMOVE then
      local to = st_first()
      if to then h.result = to end
    elseif st_at(id - ST_BASE) then
      h.result = 100002
    end
  end })
  hook("ステータス_下矢印", { after = function(h)
    if not st_at(h.args[1] - ST_BASE) then return end
    h.result = rq.statuspage.mode() == "battle-result" and 100002 or SPECIAL
  end })
  -- Left goes to the cell on the left (cell 2), Right to the one on the right (cell 1).
  hook("ステータス_左矢印", { after = function(h)
    local e = st.by_id[h.args[1]]
    if e and e.spot == "title" then h.result = (e.cell == 1 and st_at(2)) or h.args[1] end
  end })
  hook("ステータス_右矢印", { after = function(h)
    local e = st.by_id[h.args[1]]
    if e and e.spot == "title" then h.result = (e.cell == 2 and st_at(1)) or h.args[1] end
  end })
  hook("T_Status@showComment", { after = function(h)
    local id = h.args[1]
    local e = st.by_id[id]
    if not e or e.removed then return end
    local tip = ask(e, "tip", rq.statuspage.chara())
    if type(tip) ~= "string" or tip == "" then return end
    local c = ST_CELLS[e.cell]
    -- The comment window starts a line at "@", as the game's own texts do.
    call("ボタン説明_表示", id, e.label, (tip:gsub("\n", "@")), c[1] + c[3], c[2], 500)
    pcall(rq.ain.set_global, "g_sStatus", "m_nCommentID", id)
  end })
end

-- ---- The map panel ("map", 2.32.0) ----
--
-- A quest's map has the panel g_sMapParts (T_MapParts) at the right; its buttons are one group,
-- m_sButton, made again for each area by T_MapParts::create. The panel has no free room, so a mod's
-- buttons are tabs on its left edge, on the 3D view's right edge between the area map (AREA DISPLAY,
-- CG 11371 at 695,11, 110x110) and the Magnum gauge (CG 11315 at 778,349): up to four of 40x44 at
-- x 768, from y 129, 47 apart (notes/plans/plan-map-panel-windows.md).
-- Built on public calls only:
--   after T_MapParts@create    the shown entries as buttons of that group, ids 700101 to 700104
--                              (T_ButtonGroup::addButtonRect, its three sprites set to faces composed
--                              from the game's plain grey bar, CG 11009, and the icon). A button
--                              exists only while its tab is shown: T_MapParts::setShow shows the
--                              whole group, a hidden button too.
--   before T_MapParts@delete   forget them (the group deletes its buttons)
--   rq.on_frame                shown and enabled asked again every 200 ms, or at once after a set;
--                              a change in the shown tabs makes them again
--   before T_MapParts@exec     a click: T_Map::exec sends ids 700001 to 799999 there, and exec
--                              does nothing for an id it does not know; on_click runs there
--   after T_MapParts@showComment  the tip, in the game's comment window, left of the tabs (the
--                              panel's own buttons have none). The panel calls it on every update
--                              under the mouse and returns at once when that id is already shown:
--                              so does the tip
--   before T_Map@checkInput    the map takes input (every pass of its loop, in key mode too):
--                              agents see the tabs only then, not while the quest storage screen
--                              shows over the panel (the panel stays)
--   after マップ_上/下/左/右矢印   the keys: Left from AREA DISPLAY (which stays today) reaches the
--                              first tab, Up and Down move between the tabs, Right goes back
-- Inactive (status 1) is the face darkened; pressed is the icon moved 1 right and 1 down, as on the
-- status page.
local MP_GRID = { "g_sMapParts", "m_sButton" }
local MP_WIN = { "g_sMapParts" }
local MP_BASE = 700100
local MP_X, MP_Y, MP_W, MP_H, MP_STEP, MP_MAX = 768, 129, 40, 44, 47, 4
local MP_FACE = { cg = 11009, src = { 0, 0, 228, 30 }, edge = { 6, 6, 6, 6 } }
local MP_AREA_ON, MP_AREA_OFF = 700002, 700001

local mp = {
  hooked = false,
  made = false,    -- the panel exists (after create, until delete)
  by_id = {},      -- button id -> entry
  faces = {},      -- icon -> { normal, inactive, pressed }
  nfaces = 0,
  sig = nil,       -- the shown tabs and their icons, as made
  next_ask = 0,    -- rq.time_ms() of the next ask
  dirty = false,   -- ask at the next frame
  input_at = -1000, -- rq.time_ms() of the map's last checkInput
  listed = false,  -- the tabs are agent controls now
  tip_was = nil,   -- the panel's comment id before showComment
}

local function mp_faces(icon)
  local f = mp.faces[icon]
  if f then return f end
  mp.nfaces = mp.nfaces + 1
  local base = rq.uikit.layers(MP_FACE, 0, 0, MP_W, MP_H)
  local w, h = rq.hud.cg_size(icon)
  local il
  if w and w > 0 and h > 0 then
    local s = math.min(1, (MP_W - 8) / w, (MP_H - 10) / h)
    local fw, fh = math.max(1, math.floor(w * s + 0.5)), math.max(1, math.floor(h * s + 0.5))
    il = { x = (MP_W - fw) // 2, y = (MP_H - fh) // 2, w = fw, h = fh }
  end
  local function face(state, d, dark)
    local layers = {}
    for _, l in ipairs(base) do layers[#layers + 1] = l end
    if il then layers[#layers + 1] = { cg = icon, x = il.x + d, y = il.y + d, w = il.w, h = il.h } end
    if dark then layers[#layers + 1] = { fill = { 0, 0, 0, 128 } } end
    return rq.image.compose(string.format("rq.gamebutton:map:%d:%d", mp.nfaces, state), layers,
      { w = MP_W, h = MP_H })
  end
  f = { face(0, 0, false), face(1, 0, true), face(2, 1, false) }
  mp.faces[icon] = f
  return f
end

local function mp_control(e, id)
  local name = e.mod .. "." .. e.id
  if id then
    pcall(rq.agent.control, name, { label = e.label, button = id })
  else
    pcall(rq.agent.control, name)
  end
end

local function mp_erase()
  for id, e in pairs(mp.by_id) do
    mp_control(e, nil)
    if mp.made then m(MP_GRID, "T_ButtonGroup@eraseButton", id) end
    e.button, e.cell = nil, nil
  end
  mp.by_id, mp.sig = {}, nil
end

-- The z of the panel's buttons: AREA DISPLAY's (T_MapParts::create set the group's z before ours).
local function mp_z()
  local idx = m(MP_GRID, "T_ButtonGroup@getButtonIndex", MP_AREA_OFF)
  if not idx or idx < 0 then return nil end
  return m({ "g_sMapParts", "m_sButton", "m_asButton", idx }, "T_Button@getZ")
end

local function mp_add(e, cell)
  local id = MP_BASE + cell
  local f = mp_faces(e.icon)
  m(MP_GRID, "T_ButtonGroup@addButtonRect", id, MP_W, MP_H, MP_W, MP_H, MP_W, MP_H)
  local idx = m(MP_GRID, "T_ButtonGroup@getButtonIndex", id)
  if not idx or idx < 0 then return false end
  for state = 0, 2 do
    m({ "g_sMapParts", "m_sButton", "m_asButton", idx, "m_aspButton", state, "m_spBase" },
      "T_TextSprite@setCg", f[state + 1])
  end
  -- The game's frames (ＣＧ_システムボタン作成): yellow, red under the cursor while inactive.
  m(MP_GRID, "T_ButtonGroup@setButtonFrame", id, 0, -1, -1, 216, 255, 0, 102, MIN_INT, MIN_INT, 0)
  m(MP_GRID, "T_ButtonGroup@setButtonFrame", id, 1, -1, -1, 255, 0, 0, 128, MIN_INT, MIN_INT, 0)
  m(MP_GRID, "T_ButtonGroup@setButtonFrame", id, 2, -1, -1, 216, 255, 0, 102, MIN_INT, MIN_INT, 0)
  local z = mp_z()
  if z and z > 0 then m(MP_GRID, "T_ButtonGroup@setButtonZ", id, z) end
  m(MP_GRID, "T_ButtonGroup@setButtonPos", id, MP_X, MP_Y + MP_STEP * (cell - 1))
  m(MP_GRID, "T_ButtonGroup@setButtonSoundClick", id, 0)
  m(MP_GRID, "T_ButtonGroup@setButtonShow", id, call("マップ部品_表示中") == true)
  m(MP_GRID, "T_ButtonGroup@setButtonActive", id, e.active)
  e.button, e.cell = id, cell
  mp.by_id[id] = e
  if mp.listed then mp_control(e, id) end
  return true
end

-- Asks every entry; makes the tabs again when the shown ones or their icons changed, else sets
-- enabled on the ones there.
local function mp_place()
  if not mp.made then return end
  local list, sig = {}, {}
  for _, e in ipairs(entries) do
    if e.group == "map" then
      e.shown_now = false
      if not e.removed then
        local shown, bad = ask(e, "shown")
        if shown == nil and not bad then shown = true end
        e.shown_now = shown == true
        if e.shown_now then
          if #list < MP_MAX then
            list[#list + 1] = e
            sig[#sig + 1] = e.mod .. "." .. e.id .. ":" .. tostring(e.icon)
          elseif not e.warned then
            e.warned = true
            logf("%s %s: no room left for a tab on the map (up to %d)", e.mod, e.id, MP_MAX)
          end
        end
      end
    end
  end
  sig = table.concat(sig, "\n")
  if sig ~= mp.sig then
    -- Not while the panel is suspended (a fight): a button made then would not be.
    if call("マップ部品_サスペンド状態") == true then return end
    mp_erase()
    for cell, e in ipairs(list) do
      e.active = is_enabled(e)
      if not mp_add(e, cell) then logf("%s %s: the tab was not made", e.mod, e.id) end
    end
    mp.sig = sig
    return
  end
  for id, e in pairs(mp.by_id) do
    local active = is_enabled(e)
    if active ~= e.active then
      e.active = active
      m(MP_GRID, "T_ButtonGroup@setButtonActive", id, active)
    end
  end
end

-- The tabs that take the keys, top to bottom.
local function mp_keys()
  local out = {}
  for cell = 1, MP_MAX do
    local e = mp.by_id[MP_BASE + cell]
    if e and e.active then out[#out + 1] = e.button end
  end
  return out
end

local function mp_install()
  mp.hooked = true
  hook("T_MapParts@delete", { before = function()
    for _, e in pairs(mp.by_id) do
      mp_control(e, nil)
      e.button, e.cell = nil, nil
    end
    mp.made, mp.by_id, mp.sig, mp.listed = false, {}, nil, false
  end })
  hook("T_MapParts@create", { after = function()
    mp.made, mp.by_id, mp.sig = true, {}, nil
    mp_place()
  end })
  hook("T_Map@checkInput", { before = function() mp.input_at = rq.time_ms() end })
  rq.on_frame(function()
    if not mp.made then return end
    local now = rq.time_ms()
    local live = now - mp.input_at < 300
    if live ~= mp.listed then
      mp.listed = live
      for id, e in pairs(mp.by_id) do mp_control(e, live and id or nil) end
    end
    if not mp.dirty and now < mp.next_ask then return end
    mp.dirty, mp.next_ask = false, now + 200
    mp_place()
  end)
  hook("T_MapParts@exec", { before = function(h)
    local e = mp.by_id[h.args[1]]
    if not e then return end
    m(MP_WIN, "T_MapParts@hideComment", -1)
    if e.removed or not e.active then
      call("効果音_エラー")
      return
    end
    call("効果音_クリック")
    local ok, err = pcall(e.on_click, e.handle)
    if not ok then logf("%s %s on_click: %s", e.mod, e.id, tostring(err)) end
  end })
  hook("T_MapParts@showComment", { before = function()
    mp.tip_was = rq.ain.global("g_sMapParts", "m_nCommentID")
  end, after = function(h)
    local id = h.args[1]
    local e = mp.by_id[id]
    if not e or e.removed or mp.tip_was == id then return end
    local tip = ask(e, "tip")
    if type(tip) ~= "string" or tip == "" then return end
    local y = MP_Y + MP_STEP * (e.cell - 1)
    -- The comment window starts a line at "@", as the game's own texts do. It goes left of the
    -- tab: the game puts it at the screen's right edge, over the panel, when it does not fit.
    call("ボタン説明_表示", id, e.label, (tip:gsub("\n", "@")), 0, y, 500)
    local w = call("説明_幅")
    if type(w) == "number" and w > 0 then call("説明_位置設定", math.max(0, MP_X - w - 2), y) end
  end })
  local function area() return call("マップ部品_エリア表示中") == true and MP_AREA_ON or MP_AREA_OFF end
  local function step(from, d)
    local list = mp_keys()
    for i, id in ipairs(list) do
      if id == from then return list[i + d] or from end
    end
    return from
  end
  hook("マップ_左矢印", { after = function(h)
    local id = h.args[1]
    if mp.by_id[id] then
      h.result = id
    elseif id == MP_AREA_ON or id == MP_AREA_OFF then
      local first = mp_keys()[1]
      if first then h.result = first end
    end
  end })
  hook("マップ_右矢印", { after = function(h)
    if mp.by_id[h.args[1]] then h.result = area() end
  end })
  hook("マップ_上矢印", { after = function(h)
    local id = h.args[1]
    if mp.by_id[id] then h.result = step(id, -1) end
  end })
  hook("マップ_下矢印", { after = function(h)
    local id = h.args[1]
    if mp.by_id[id] then h.result = step(id, 1) end
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
  -- "status" (2.58.0): spot "title" (the default, buttons) or "left" (icons, with no on_click).
  local spot = nil
  if group == "status" then
    spot = spec.spot == nil and "title" or spec.spot
    if not ST_SPOTS[spot] then error("rq.gamebutton.add: spot must be \"title\" or \"left\"", 2) end
  elseif spec.spot ~= nil then
    error("rq.gamebutton.add: spot is for the \"status\" group only", 2)
  end
  if spot == "left" then
    if spec.on_click ~= nil then
      error("rq.gamebutton.add: a \"left\" entry is an icon, with no on_click", 2)
    end
  elseif type(spec.on_click) ~= "function" then
    error("rq.gamebutton.add: on_click must be a function", 2)
  end
  if spec.enabled ~= nil and type(spec.enabled) ~= "boolean" and type(spec.enabled) ~= "function" then
    error("rq.gamebutton.add: enabled must be a boolean or a function", 2)
  end
  if group == "status" or group == "map" then
    local icon_ok = math.type(spec.icon) == "integer" and spec.icon > 0
    if group == "status" and type(spec.icon) == "function" then icon_ok = true end
    if not icon_ok then
      error("rq.gamebutton.add: a \"" .. group .. "\" entry needs icon, a CG number"
        .. (group == "status" and " or a function" or ""), 2)
    end
    if spec.tip ~= nil and type(spec.tip) ~= "string" and type(spec.tip) ~= "function" then
      error("rq.gamebutton.add: tip must be a string or a function", 2)
    end
    if spec.shown ~= nil and type(spec.shown) ~= "boolean" and type(spec.shown) ~= "function" then
      error("rq.gamebutton.add: shown must be a boolean or a function", 2)
    end
  elseif spec.tip ~= nil and type(spec.tip) ~= "string" then
    error("rq.gamebutton.add: tip must be a string", 2)
  end
  local mod = rq.mod and rq.mod.id and rq.mod.id() or "?"
  for _, e in ipairs(entries) do
    if not e.removed and e.mod == mod and e.id == spec.id then
      error("rq.gamebutton.add: this mod already has an entry " .. spec.id, 2)
    end
  end
  local e = { group = group, mod = mod or "?", id = spec.id, label = spec.label, tip = spec.tip,
    enabled = spec.enabled, on_click = spec.on_click, icon = spec.icon, shown = spec.shown, spot = spot }
  local handle = {}
  function handle.id() return e.mod .. "." .. e.id end
  function handle.set(opts)
    if type(opts) ~= "table" then error("rq.gamebutton handle set: opts must be a table", 2) end
    if opts.label ~= nil then check_text(opts.label, "label", "rq.gamebutton handle set"); e.label = opts.label end
    -- skill-window: a label, a tip or enabled shows from the next build of the list.
    if opts.tip ~= nil then
      if type(opts.tip) ~= "string" and not ((group == "status" or group == "map") and type(opts.tip) == "function") then
        error("rq.gamebutton handle set: tip must be a string", 2)
      end
      e.tip = opts.tip ~= "" and opts.tip or nil
    end
    if group == "status" or group == "map" then
      if opts.icon ~= nil then
        local icon_ok = math.type(opts.icon) == "integer" and opts.icon > 0
        if group == "status" and type(opts.icon) == "function" then icon_ok = true end
        if not icon_ok then
          error("rq.gamebutton handle set: icon must be a CG number"
            .. (group == "status" and " or a function" or ""), 2)
        end
        e.icon = opts.icon
        e.warned_icon = nil
      end
      if opts.shown ~= nil then
        if type(opts.shown) ~= "boolean" and type(opts.shown) ~= "function" then
          error("rq.gamebutton handle set: shown must be a boolean or a function", 2)
        end
        e.shown = opts.shown
      end
    end
    if opts.enabled ~= nil then
      if type(opts.enabled) ~= "boolean" and type(opts.enabled) ~= "function" then
        error("rq.gamebutton handle set: enabled must be a boolean or a function", 2)
      end
      e.enabled = opts.enabled
    end
    -- status: at once while the page is open. map: at the next frame.
    if group == "status" and st.made then st_place(rq.statuspage.chara()) end
    if group == "map" then mp.dirty = true end
  end
  function handle.remove()
    if e.removed then return end
    e.removed = true
    if group == "system-menu" then
      if #live("system-menu") == 0 then uncompose() end
    elseif group == "status" then
      if e.button then st_control(e, nil) end
      if st.made then st_place(rq.statuspage.chara()) end
    elseif group == "map" then
      mp.dirty = true
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
  elseif group == "status" then
    if not st.hooked then st_install() end
    if st.made then st_place(rq.statuspage.chara()) end
  elseif group == "map" then
    if not mp.hooked then mp_install() end
    mp.dirty = true
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
  local status = {}
  for _, e in ipairs(live("status")) do
    status[#status + 1] = { id = e.mod .. "." .. e.id, mod = e.mod, label = e.label,
      enabled = e.placed and e.active == true or (not e.placed and is_enabled(e, rq.statuspage.chara())),
      shown = e.shown_now == true, placed = e.placed == true, button = e.button,
      x = e.cell and ST_CELLS[e.cell][1], y = e.cell and ST_CELLS[e.cell][2], spot = e.spot,
      icon = e.icon_now }
  end
  local map = {}
  for _, e in ipairs(live("map")) do
    map[#map + 1] = { id = e.mod .. "." .. e.id, mod = e.mod, label = e.label,
      enabled = e.button and e.active == true or (not e.button and is_enabled(e)),
      shown = e.shown_now == true, placed = e.button ~= nil, button = e.button,
      x = e.cell and MP_X, y = e.cell and (MP_Y + MP_STEP * (e.cell - 1)) }
  end
  return {
    { name = "system-menu", open = mode > 0, row = #list > 0, entries = list },
    { name = "skill-window", open = sw_open(), entries = skill },
    { name = "status", open = rq.statuspage.chara() ~= nil, entries = status },
    { name = "map", open = mp.made, entries = map },
  }
end
