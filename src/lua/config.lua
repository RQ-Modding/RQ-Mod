-- rq.config (loader 1.8.0): a Mods tab in the game's Config screen. Each mod that calls
-- rq.config.page gets a row in its list; the row's page is drawn with rq.uikit.
--
-- How it fits in the game: Config's seven tabs are buttons 100001-100007 and each tab is a
-- class whose run() returns the next mode. The Mods tab is button 100009 (made by the engine
-- in コンフィグ共通_作成 once a page exists). Clicking it makes コンフィグ共通_処理 return 9;
-- the tab's run() then returns 7 with the Mods flag on, and Other 2 (T_ConfigEtc2) is made on
-- a blank page with no checkboxes, so the game keeps its own tabs, buttons, keys and sounds.
local rq, set_tab, set_mods = ...
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
  for _, cls in ipairs(TABS) do
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

rq.on_frame(function()
  if open and not hidden and #pages > 0 then draw_page() end
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

return config
