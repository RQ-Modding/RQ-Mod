-- rq.gamebutton: buttons of mods in the game's own button groups (loader 2.16.0).
--
-- One group so far, "system-menu": the SYSTEM menu gets one more row, MODS, under Save, made by
-- the game itself. A click closes the menu as any row does, and the game's choice menu
-- (rq.ui.choose) lists every mod's entry. Mods add entries, not rows.
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
local GROUPS = { ["system-menu"] = true }

local entries = {}   -- in the order they were added
local hooked, composed = false, false

local function logf(fmt, ...)
  rq.log("rq.gamebutton: " .. string.format(fmt, ...))
end

local function live()
  local out = {}
  for _, e in ipairs(entries) do
    if not e.removed then out[#out + 1] = e end
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

local function any_enabled()
  for _, e in ipairs(entries) do
    if is_enabled(e) then return true end
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
  for i, e in ipairs(live()) do
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
    if (id ~= 910001 and id ~= 910009) or #live() == 0 then return end
    local ok, err = pcall(method, { "g_sSystemMenu" }, "T_SystemMenu@createButton", ROW_ID, "Mods")
    if not ok then logf("the MODS row: %s", tostring(err)) end
  end })
  hook("システムメニュー_有効確認", { after = function(h)
    if h.args[2] == ROW_ID then h.result = any_enabled() end
  end })
  hook("システムメニュー_処理", { before = function(h)
    if h.args[1] == ROW_ID then open_list() end
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
    if #live() == 0 then uncompose() end
  end
  e.handle = handle
  entries[#entries + 1] = e
  compose()
  if not hooked then install() end
  return handle
end

-- The groups and their entries, for agents and the Lab.
function gamebutton.groups()
  local list = {}
  for _, e in ipairs(live()) do
    list[#list + 1] = { id = e.mod .. "." .. e.id, mod = e.mod, label = e.label, enabled = is_enabled(e) }
  end
  local mode = 0
  pcall(function() mode = g("g_sSystemMenu", "m_nMode") end)
  return {
    { name = "system-menu", open = mode > 0, row = #list > 0, entries = list },
  }
end
