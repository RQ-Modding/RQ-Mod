-- rq.config (loader 1.8.0): a Mods tab in the game's Config screen. Each mod that calls
-- rq.config.page gets a row in its list; the row's page is drawn with rq.uikit.
--
-- How it fits in the game: Config's seven tabs are buttons 100001-100007 and each tab is a
-- class whose run() returns the next mode. The Mods tab is button 100009 (made by the engine
-- in コンフィグ共通_作成 once a page exists). Clicking it makes コンフィグ共通_処理 return 9;
-- the tab's run() then returns 7 with the Mods flag on, and Other 2 (T_ConfigEtc2) is made on
-- a blank page with no checkboxes, so the game keeps its own tabs, buttons, keys and sounds.
-- rq.config.settings (2.35.0) builds a page from a list of rows and keeps its values in the
-- mod's store; as_mod (modloader_roster.c) files those store calls under the mod.
local rq, set_tab, set_mods, as_mod = ...
local ui = rq.uikit
local hook = rq.ain.hook

local config = {}

local pages = {} -- in the order they were added
local by_id = {}
local current = 1
local installed = false
local mods = false    -- the next Other 2 is the Mods page
local building = false
local open = false    -- the Mods page is on screen
local hidden = false  -- a game dialog is over it
-- The keys (2.12.0): the tab whose page shows (100009 for Mods), whether the kit has the cursor,
-- and arrows still down after it gave the cursor back.
local cur_tab = nil
local kit_keys = false
local let_go = false
local RING = { 100001, 100002, 100003, 100004, 100005, 100006, 100007, 100009 }
local LIST_ROW = "list:rq.config.pages."
local ARROWS = { "up", "down", "left", "right" }

local TABS = { "T_ConfigMessage", "T_ConfigSound", "T_ConfigWindow", "T_Config3D",
  "T_ConfigConfirm", "T_ConfigEtc", "T_ConfigEtc2" }
local BASES = { 40001, 40002, 40003, 40004, 40005, 40006, 40007 }
local OTHER2 = 40007

-- The page under the tabs: 12..1011 by 85..679, rows of one color each.
local PAGE = { x = 12, y = 85, w = 1000, h = 595 }
local LIST = { x = 24, y = 97, w = 220, h = 571 }
local BODY = { x = 262, y = 97, w = 738, h = 571 }

-- The strip under the tabs ends at 853 (Other 2 ends at 852): carry its frame to 973 for the
-- Mods tab, copying a column from the middle and then the right end.
local function strip(cg)
  return {
    { cg = cg },
    { cg = cg, src = { 500, 38, 1, 44 }, x = 852, y = 38, w = 120, h = 44, mode = "copy" },
    { cg = cg, src = { 852, 38, 2, 44 }, x = 972, y = 38, mode = "copy" },
  }
end

local function blank_page()
  local l = strip(OTHER2)
  l[#l + 1] = { cg = OTHER2, src = { 950, PAGE.y, 1, PAGE.h }, x = PAGE.x, y = PAGE.y,
    w = PAGE.w, h = PAGE.h, mode = "copy" }
  return l
end

-- The tab in its three looks, from Other 2's (its label cut away) and the kit's tab text.
local function tab_cg(state, cg)
  local t = ui.themes.config.tab_text[state]
  local l = ui.layers({ cg = cg, src = { 0, 0, 120, 40 }, edge = { 6, 0, 6, 0 }, mid = { 8, 1 } }, 0, 0, 120, 40)
  l[#l + 1] = { text = "Mods", x = 0, y = 0, w = 120, h = 40, align = "center", valign = "center",
    size = t.size, color = { t.r, t.g, t.b }, edge = false, bold = true }
  return rq.image.compose("rq.config.tab." .. state, l, { w = 120, h = 40 })
end

local function install()
  installed = true
  if not set_tab(tab_cg("normal", 40061), tab_cg("hover", 40062), tab_cg("current", 40063)) then
    rq.log("config: this game has no Config tabs; pages stay hidden")
    return
  end
  for _, cg in ipairs(BASES) do rq.image.compose(cg, strip(cg)) end

  hook("コンフィグ共通_処理", {
    before = function(h)
      local id = h.args[1]
      if id == 100009 then
        h.skip, h.result = true, 9
      elseif open and (id == 100012 or id == 100013) then
        hidden = true -- the game asks before it goes to the title or quits
      end
    end,
    after = function() hidden = false end,
  })
  for i, cls in ipairs(TABS) do
    hook(cls .. "@create", { before = function() cur_tab = 100000 + i end })
    hook(cls .. "@run", {
      after = function(h)
        if h.result == 9 then
          mods, h.result = true, 7
        else
          mods = false
        end
      end,
    })
  end
  hook("T_ConfigEtc2@create", {
    before = function()
      -- Also when off: going to the title skips delete, so the flag may still be on.
      set_mods(mods)
      if not mods then return end
      cur_tab = 100009
      building = true
      rq.image.compose(OTHER2, blank_page())
    end,
    after = function()
      if not building then return end
      building = false
      rq.image.compose(OTHER2, strip(OTHER2))
      -- Going to the title does not come back to コンフィグ共通_処理's after.
      open, hidden = true, false
      local p = pages[current]
      if p and p.on_open then pcall(p.on_open) end
    end,
  })
  hook("T_ConfigEtc2@delete", {
    before = function()
      if open and not building then
        open = false
        set_mods(false)
        local p = pages[current]
        if p and p.on_close then pcall(p.on_close) end
      end
    end,
  })
  hook("T_ConfigEtc2@createCheckBox", {
    before = function(h)
      if building then h.skip, h.result = true, true end
    end,
  })
  hook("T_ConfigEtc2@setValue", {
    before = function(h)
      if building or open then h.skip = true end
    end,
  })
  -- The tab row's Left and Right reach the Mods tab (2.12.0): the game's go round 100001 to
  -- 100007 only, skipping the open tab (コンフィグ共通_左矢印 / 右矢印).
  local function ring_step(id, d)
    local at
    for i, v in ipairs(RING) do if v == id then at = i end end
    if not at then return nil end
    for k = 1, #RING do
      local v = RING[(at - 1 + d * k) % #RING + 1]
      if v ~= cur_tab then return v end
    end
  end
  hook("コンフィグ共通_右矢印", { after = function(h)
    local r = ring_step(h.args[1], 1)
    if r then h.result = r end
  end })
  hook("コンフィグ共通_左矢印", { after = function(h)
    local r = ring_step(h.args[1], -1)
    if r then h.result = r end
  end })
  -- On the Mods page, Down from a tab or the top buttons gives the cursor to the page's list (the
  -- blank Other 2 page has nothing to go to): the game's cursor stays and is turned off, the kit's
  -- starts. Other 2's checkboxes are made under the blank page; the game's cursor never goes there.
  local function hand_in(h)
    local id = h.args[1]
    h.skip, h.result = true, id
    pcall(rq.ain.call, "コンフィグ_選択設定", id, false)
    ui.focus(LIST_ROW .. current)
    kit_keys = true
  end
  local function on_page() return open and not hidden and #pages > 0 end
  local function checkbox(id) return id > 200000 end
  hook("T_ConfigEtc2@moveDown", { before = function(h)
    local id = h.args[1]
    if on_page() and (id >= 100001 and id <= 100013 or checkbox(id)) then hand_in(h) end
  end })
  hook("T_ConfigEtc2@moveUp", { before = function(h)
    if on_page() and checkbox(h.args[1]) then h.skip, h.result = true, 100009 end
  end })
  for _, m in ipairs({ "moveLeft", "moveRight" }) do
    hook("T_ConfigEtc2@" .. m, { before = function(h)
      if on_page() and checkbox(h.args[1]) then hand_in(h) end
    end })
  end
  -- The page's first place for the game's cursor (T_ConfigEtc2::run starts there, and its arrows
  -- fall back to it) is the Mods tab, not a hidden checkbox.
  for _, m in ipairs({ "getFirstCheckBox", "getLastCheckBox" }) do
    hook("T_ConfigEtc2@" .. m, { before = function(h)
      if (mods or open) and #pages > 0 then h.skip, h.result = true, 100009 end
    end })
  end
  -- Other 2 greys out its own tab; on the Mods page it is the Mods tab that is current.
  hook("T_ButtonGroup@setButtonActive", {
    before = function(h)
      if building and h.args[1] == 100007 and h.args[2] == false then h.args[1] = 100009 end
    end,
  })
end

local function draw_page()
  local names = {}
  for i, p in ipairs(pages) do names[i] = p.title end
  local pick, changed = ui.list(names, current, { id = "rq.config.pages", x = LIST.x, y = LIST.y,
    w = LIST.w, h = LIST.h })
  if changed and pick and pick ~= current then
    local old = pages[current]
    if old and old.on_close then pcall(old.on_close) end
    current = pick
    local new = pages[current]
    if new and new.on_open then pcall(new.on_open) end
  end
  local p = pages[current]
  if not p then return end
  ui.label(p.title, { x = BODY.x, y = BODY.y, size = 22 })
  local box = { x = BODY.x, y = BODY.y + 40, w = BODY.w, h = BODY.h - 40 }
  local ok, err = pcall(ui.col, { x = box.x, y = box.y, w = box.w, gap = 12 }, function()
    p.draw(ui, box)
  end)
  if not ok then
    if p.err ~= err then
      p.err = err
      rq.log("config page " .. p.id .. ": " .. tostring(err))
    end
    ui.label(tostring(err), { x = box.x, y = box.y + box.h - 60, w = box.w, size = 14, color = { 255, 140, 120 } })
  end
end

-- Up past the page's top widget gives the cursor back to the tab row, on the Mods tab. The arrow
-- stays hidden from the game until it is let go.
local function hand_back(dir)
  if dir ~= "up" then return end
  kit_keys, let_go = false, true
  ui.focus(nil)
  pcall(rq.ain.call, "コンフィグ_選択設定", 100009, true)
end

rq.on_frame(function()
  if open and not hidden and #pages > 0 then
    draw_page()
    -- The kit has the cursor while one of its widgets is under it: given by Down, or by the mouse.
    if ui.focused() ~= nil then kit_keys = true elseif kit_keys and not let_go then kit_keys = false end
    if kit_keys then ui.keys({ edge = hand_back }) end
  else
    kit_keys = false
  end
  if let_go then
    local down = false
    for _, k in ipairs(ARROWS) do if rq.input.key(rq.input.code(k)) then down = true end end
    if down then rq.hud.block_keys(ARROWS) else let_go = false end
  end
end)

---rq.config.page{ id, title, draw = function(ui, box) end, on_open, on_close }
function config.page(t)
  if type(t) ~= "table" or type(t.draw) ~= "function" then
    error("rq.config.page: needs a table with draw = function(ui, box)", 2)
  end
  local id = t.id or t.title
  if id == nil then error("rq.config.page: needs an id or a title", 2) end
  id = tostring(id)
  local p = by_id[id]
  if not p then
    p = { id = id }
    by_id[id] = p
    pages[#pages + 1] = p
  end
  p.title = tostring(t.title or id)
  p.draw, p.on_open, p.on_close, p.err = t.draw, t.on_open, t.on_close, nil
  if not installed then install() end
  return id
end

---The Mods page is on screen.
function config.is_open()
  return open and not hidden
end

---The page shown, and the list of page ids.
function config.pages()
  local ids = {}
  for i, p in ipairs(pages) do ids[i] = p.id end
  return pages[current] and pages[current].id, ids
end

---Show this page next time (or now, when the Mods page is open).
function config.show(id)
  for i, p in ipairs(pages) do
    if p.id == id then
      if i ~= current and open then
        local old = pages[current]
        if old.on_close then pcall(old.on_close) end
        if p.on_open then pcall(p.on_open) end
      end
      current = i
      return true
    end
  end
  return false
end

-- ---- rq.config.settings (2.35.0): a page built from a list of rows ----
-- The values live in the mod's own store (rq.store.global, rq.store or rq.savedata), under each
-- row's key, so a mod that drew its page by hand keeps its players' settings when it switches.
-- The page is drawn here, where no mod is on the stack, so every store call goes through as_mod.

local KINDS = { checkbox = true, stepper = true, slider = true, choice = true, input = true,
  label = true, space = true }
local STORES = {
  global = function() return rq.store.global end,
  profile = function() return rq.store end,
  save = function() return rq.savedata end,
}
local NOTE = { dim = true, size = 15 }

local function fail(what, level)
  error("rq.config.settings: " .. what, (level or 1) + 1)
end

local function whole(v)
  if math.type(v) == "integer" then return v end
  if math.type(v) == "float" and v == math.floor(v) and v >= -2^53 and v <= 2^53 then return math.tointeger(v) end
end

-- The stored value made right for the row, or nil when it is not one the row can take.
local function fit(row, v)
  local k = row.kind
  if k == "checkbox" then
    if type(v) == "boolean" then return v end
  elseif k == "stepper" then
    v = whole(v)
    if v and v >= row.min and v <= row.max then return v end
  elseif k == "slider" then
    if type(v) == "number" and v == v and v >= row.min and v <= row.max then return v end
  elseif k == "choice" then
    for i = 1, #row.options do
      if row.values then
        if row.values[i] == v then return v end
      elseif whole(v) == i then
        return i
      end
    end
  elseif k == "input" then
    if type(v) == "string" and #v <= row.max and not v:find("\0", 1, true) then return v end
  end
end

local function describe(row)
  local k = row.kind
  if k == "checkbox" then return "true or false" end
  if k == "stepper" then return "a whole number from " .. row.min .. " to " .. row.max end
  if k == "slider" then return "a number from " .. row.min .. " to " .. row.max end
  if k == "choice" then
    if row.values then return "one of the row's values" end
    return "a whole number from 1 to " .. #row.options
  end
  return "a text of at most " .. row.max .. " bytes"
end

local function check_rows(rows)
  if type(rows) ~= "table" or #rows == 0 then fail("rows is a list of rows", 3) end
  local out, by_key = {}, {}
  for i, r in ipairs(rows) do
    local at = "row " .. i
    if type(r) ~= "table" then fail(at .. " is not a table", 3) end
    local kind = r.kind
    if not KINDS[kind] then
      fail(at .. ": kind is checkbox, stepper, slider, choice, input, label or space", 3)
    end
    local row = { kind = kind, label = r.label, tip = r.tip, note = r.note, disabled = r.disabled,
      w = r.w, id = r.id }
    if r.label ~= nil and type(r.label) ~= "string" then fail(at .. ": label is a string", 3) end
    if r.disabled ~= nil and type(r.disabled) ~= "boolean" and type(r.disabled) ~= "function" then
      fail(at .. ": disabled is true, false or a function", 3)
    end
    if r.note ~= nil and type(r.note) ~= "string" and type(r.note) ~= "table" then
      fail(at .. ": note is a text or a list of texts", 3)
    end
    if r.w ~= nil and math.type(r.w) ~= "integer" then fail(at .. ": w is a whole number of pixels", 3) end
    if kind == "label" then
      if type(r.text) ~= "string" then fail(at .. ": a label row needs text", 3) end
      row.text, row.dim, row.size, row.color = r.text, r.dim, r.size, r.color
    elseif kind == "space" then
      row.h = r.h or 8
      if math.type(row.h) ~= "integer" then fail(at .. ": h is a whole number of pixels", 3) end
    else
      if type(r.key) ~= "string" or r.key == "" then fail(at .. ": a " .. kind .. " row needs a key", 3) end
      if by_key[r.key] then fail(at .. ": the key " .. r.key .. " is used twice", 3) end
      row.key = r.key
      if kind == "stepper" then
        row.min, row.max = whole(r.min or 0), whole(r.max or 999999)
        row.step, row.big = r.step, r.big
        if not row.min or not row.max or row.max < row.min then
          fail(at .. ": min and max are whole numbers, max not below min", 3)
        end
        row.default = r.default == nil and row.min or r.default
      elseif kind == "slider" then
        row.min, row.max, row.step = r.min or 0, r.max or 1, r.step
        row.low, row.high = r.low, r.high
        if type(row.min) ~= "number" or type(row.max) ~= "number" or row.max < row.min then
          fail(at .. ": min and max are numbers, max not below min", 3)
        end
        if row.step ~= nil and (type(row.step) ~= "number" or row.step <= 0) then
          fail(at .. ": step is a number above 0", 3)
        end
        row.default = r.default == nil and row.min or r.default
      elseif kind == "choice" then
        if type(r.options) ~= "table" or #r.options == 0 then fail(at .. ": options is a list of texts", 3) end
        for j, o in ipairs(r.options) do
          if type(o) ~= "string" then fail(at .. ": option " .. j .. " is not a text", 3) end
        end
        if r.values ~= nil and (type(r.values) ~= "table" or #r.values ~= #r.options) then
          fail(at .. ": values has one value for each option", 3)
        end
        row.options, row.values, row.dir, row.gap, row.tips = r.options, r.values, r.dir or "row", r.gap, r.tips
        row.default = r.default
        if row.default == nil then row.default = row.values and row.values[1] or 1 end
      elseif kind == "input" then
        row.max = r.max or 40
        if math.type(row.max) ~= "integer" or row.max < 1 or row.max > 255 then
          fail(at .. ": max is a whole number from 1 to 255", 3)
        end
        row.placeholder = r.placeholder
        row.default = r.default == nil and "" or r.default
      else
        -- Not `r.default == nil and false or r.default`: with false in the middle that gives nil.
        row.default = r.default
        if row.default == nil then row.default = false end
      end
      local d = fit(row, row.default)
      if d == nil then fail(at .. ": the default of " .. row.key .. " is not " .. describe(row), 3) end
      row.default = d
      by_key[row.key] = row
    end
    out[i] = row
  end
  return out, by_key
end

local function mod_name(id)
  local ok, list = pcall(rq.saves.current_mods)
  if ok and type(list) == "table" then
    for _, m in ipairs(list) do
      if m.id == id and type(m.name) == "string" and m.name ~= "" then return m.name end
    end
  end
  return id
end

local function row_disabled(row, blocked)
  if blocked then return true end
  local d = row.disabled
  if type(d) == "function" then
    local ok, r = pcall(d)
    return ok and r and true or false
  end
  return d and true or false
end

local function notes(row)
  local n = row.note
  if type(n) == "string" then
    ui.label(n, NOTE)
  elseif type(n) == "table" then
    for _, line in ipairs(n) do ui.label(tostring(line), NOTE) end
  end
end

---rq.config.settings{ id, title, keep, rows, reset, label_w, draw, on_open, on_close }: a page
---from a list of rows, and an object with get, set, on_change and reset. 2.35.0.
function config.settings(t)
  if type(t) ~= "table" then fail("expected a table with rows", 2) end
  local mod = rq.mod and rq.mod.id and rq.mod.id()
  if not mod then fail("call it from a mod's code", 2) end
  local keep = t.keep or "global"
  if not STORES[keep] then fail('keep is "global", "profile" or "save"', 2) end
  if t.draw ~= nil and type(t.draw) ~= "function" then fail("draw is a function(ui, box)", 2) end
  if t.title ~= nil and type(t.title) ~= "string" then fail("title is a string", 2) end
  if t.label_w ~= nil and math.type(t.label_w) ~= "integer" then fail("label_w is a whole number of pixels", 2) end
  local rows, by_key = check_rows(t.rows)
  local listeners = {}
  local label_w = t.label_w

  local function store_get(key)
    return as_mod(mod, function() return STORES[keep]().get(key) end)
  end
  local function store_set(key, v)
    as_mod(mod, function() STORES[keep]().set(key, v) end)
  end
  local function row_of(key, level)
    local row = type(key) == "string" and by_key[key]
    if not row then fail("no row has the key " .. tostring(key), level + 1) end
    return row
  end
  local function value(row)
    local v = fit(row, store_get(row.key))
    if v == nil then return row.default end
    return v
  end
  local function changed(key, new, old)
    for _, l in ipairs(listeners) do
      if l.fn then
        local ok, err = pcall(l.fn, key, new, old)
        if not ok then rq.log("config settings " .. mod .. ": on_change: " .. tostring(err)) end
      end
    end
  end
  local function write(row, v)
    local old = value(row)
    store_set(row.key, v)
    if old ~= v then changed(row.key, v, old) end
  end

  local s = {}
  function s.get(key)
    return value(row_of(key, 2))
  end
  function s.set(key, v)
    local row = row_of(key, 2)
    local ok = fit(row, v)
    if ok == nil then fail(key .. " is " .. describe(row), 2) end
    write(row, ok)
  end
  function s.on_change(fn)
    if type(fn) ~= "function" then fail("on_change takes a function", 2) end
    local l = { fn = fn }
    listeners[#listeners + 1] = l
    return function()
      l.fn = nil
      for i, x in ipairs(listeners) do
        if x == l then
          table.remove(listeners, i)
          break
        end
      end
    end
  end
  function s.reset()
    for _, row in ipairs(rows) do
      if row.key then
        local old = value(row)
        store_set(row.key, nil)
        if old ~= row.default then changed(row.key, row.default, old) end
      end
    end
  end

  local function draw_row(row, blocked)
    local kind = row.kind
    if kind == "label" then
      ui.label(row.text, { dim = row.dim, size = row.size, color = row.color })
      return
    elseif kind == "space" then
      ui.space(0, row.h)
      return
    end
    local dis = row_disabled(row, blocked)
    local id = row.id ~= nil and row.id or row.key
    local v = value(row)
    if kind == "checkbox" then
      local nv, c = ui.checkbox(v, row.label, { id = id, disabled = dis, tip = row.tip, w = row.w })
      if c and not dis then write(row, nv) end
    else
      ui.row({ gap = 12, align = "center", h = 34 }, function()
        if row.label then ui.label(row.label, { w = label_w, dim = dis }) end
        local nv, c
        if kind == "stepper" then
          nv, c = ui.stepper(v, { id = id, min = row.min, max = row.max, step = row.step, big = row.big,
            w = row.w, disabled = dis, tip = row.tip })
        elseif kind == "slider" then
          nv, c = ui.slider(v, { id = id, min = row.min, max = row.max, step = row.step, low = row.low,
            high = row.high, label = row.label, w = row.w, disabled = dis, tip = row.tip })
        elseif kind == "choice" then
          local index = v
          if row.values then
            for i, x in ipairs(row.values) do
              if x == v then index = i end
            end
          end
          local i
          i, c = ui.choice(row.options, index, { id = id, dir = row.dir, gap = row.gap, disabled = dis,
            tips = row.tips })
          if c and i then nv = row.values and row.values[i] or i end
        else
          nv, c = ui.input(v, { id = id, max = row.max, w = row.w, placeholder = row.placeholder,
            disabled = dis, tip = row.tip })
        end
        if c and not dis then
          local ok = fit(row, nv)
          if ok ~= nil then write(row, ok) end
        end
      end)
    end
    notes(row)
  end

  local page_id = t.id ~= nil and tostring(t.id) or mod
  config.page({
    id = page_id,
    title = t.title or mod_name(mod),
    on_open = t.on_open,
    on_close = t.on_close,
    draw = function(_, box)
      if not label_w then
        label_w = 0
        for _, row in ipairs(rows) do
          if row.label and row.kind ~= "checkbox" then
            label_w = math.max(label_w, rq.hud.text_width(row.label, ui.theme.label) + 16)
          end
        end
        label_w = math.min(math.max(label_w, 120), 420)
      end
      local blocked = keep == "save" and rq.world.current() == nil
      if blocked then ui.label("Load a save to change these.", { dim = true }) end
      for _, row in ipairs(rows) do draw_row(row, blocked) end
      if t.reset ~= false then
        ui.space(0, 4)
        if ui.button("Reset", { id = "reset", disabled = blocked,
            tip = "Puts every setting on this page back to its default." }) then
          s.reset()
        end
      end
      if t.draw then t.draw(ui, box) end
    end,
  })
  s.id = page_id
  return s
end

return config
