-- rq.uikit's hub widgets (loader 2.6.0): the hub's grey buttons, castle rows, quest rows and
-- list, town tabs, Frostbuy's rack tiles, the sort column, the game's scroll bar, the close
-- button and a page that covers the hub. Built from the game's own CGs (cgdump 10000 to 11430)
-- and its font (font = "game"), with the numbers from its code (T_Castle, T_QuestList, T_Sort,
-- T_ScrollBar, C_Town). notes/plans/plan-ui-kit-hub.md has the research.
local rq, ui, kit = ...
local hud, input = rq.hud, rq.input
local interact, show, place, play = kit.interact, kit.show, kit.place, kit.play
local with, id_of, inside, pulse, text_w = kit.with, kit.id_of, kit.inside, kit.pulse, kit.text_w
local mouse, frame = kit.mouse, kit.frame
local floor, max, min = math.floor, math.max, math.min

-- ---- The hub theme ----

local function game(t)
  t.font = "game"
  return t
end

local GREY_EDGE = { 5, 0, 5, 0 }

-- Starts as Config's theme, so checkboxes, sliders and lists still have their pieces; with it in
-- use, ui.button is the castle's grey button and ui.list has the game's scroll bar.
local hub = kit.copy(ui.themes.config)
hub.button_style = "large"
hub.styles = {
  -- Buttons. normal and hover are the CG's two states side by side; fit is {max size, min
  -- size, max spacing, min spacing} as the game's FitText2 shrinks a label; pad is kept free
  -- at each side.
  button = {
    -- Rance Castle's rooms (T_Castle::createButton): 11153, two states of 206x30.
    large = {
      normal = { cg = 11153, src = { 0, 0, 206, 30 }, edge = GREY_EDGE },
      hover = { cg = 11153, src = { 206, 0, 206, 30 }, edge = GREY_EDGE },
      w = 206, h = 30, pad = 2,
      text = game { face = 258, size = 24, r = 25, g = 25, b = 25, outline = { 144, 144, 136 } },
      fit = { 24, 12, 0.95, 0.5 },
    },
    -- The choice menu's button (選択ＣＧ_ボタン作成): 10022, two states of 340x27.
    choice = {
      normal = { cg = 10022, src = { 0, 0, 340, 27 }, edge = GREY_EDGE },
      hover = { cg = 10022, src = { 340, 0, 340, 27 }, edge = GREY_EDGE },
      w = 340, h = 27, pad = 6,
      text = game { face = 258, size = 20, r = 196, g = 196, b = 196 },
      fit = { 20, 12, 0.95, 0.5 },
    },
    -- Its tall one: 10029, two states of 340x60.
    choice_tall = {
      normal = { cg = 10029, src = { 0, 0, 340, 60 }, edge = GREY_EDGE },
      hover = { cg = 10029, src = { 340, 0, 340, 60 }, edge = GREY_EDGE },
      w = 340, h = 60, pad = 6,
      text = game { face = 258, size = 24, r = 196, g = 196, b = 196 },
      fit = { 24, 12, 0.95, 0.5 },
    },
    -- The footer's Vault and Trivia (T_SystemBar): 10011 carries its word, so the bevel ends
    -- are kept and an empty stretch beside the word fills the middle. Pressed, it sits 1 lower.
    short = {
      normal = { cg = 10011, src = { 0, 0, 98, 26 }, edge = { 4, 0, 4, 0 }, mid = { 8, 10 } },
      hover = { cg = 10011, src = { 98, 0, 98, 26 }, edge = { 4, 0, 4, 0 }, mid = { 8, 10 } },
      w = 98, h = 26, pad = 5, hover_dy = 1,
      text = game { face = 258, size = 20, bold = true, r = 16, g = 16, b = 16,
        shadow = { x = 1, y = 1, r = 205, g = 205, b = 190 } },
      fit = { 20, 12, 0.95, 0.5 },
    },
  },
  -- The town tabs (街マップ_タブ作成): rect buttons 256 apart, 45 high. 11005 is a tab with its
  -- word painted in; the ornaments at the ends are kept and an empty stretch fills the middle.
  -- The current tab is painted into each screen's background (11011 has Quests).
  tabs = {
    town = {
      normal = { cg = 11005, src = { 0, 0, 248, 45 }, edge = { 32, 0, 32, 0 }, mid = { 36, 4 } },
      current = { cg = 11011, src = { 770, 6, 252, 45 }, edge = { 34, 0, 34, 0 }, mid = { 38, 4 } },
      w = 248, h = 45, gap = 8, pad = 34, cover = true,
      text = game { face = 258, size = 26, bold = true, r = 236, g = 232, b = 176, outline = { 58, 53, 24 } },
      fit = { 26, 14, 0.95, 0.5 },
    },
  },
}

-- Rance Castle's room row (T_CastleList): backing 270x36, the button at +3,+3, the face frame at
-- +212,+3 with the face 24x29 inside it, NEW at -4,-3, and the room's mark: a heart at +239,+6
-- or "MORU RUN" at +238,+4 (ランス城_エリアエロ 1, and 2 or 3).
hub.castle = {
  base = { cg = 11152 },
  frame = { cg = 11156 },
  new = { cg = 11155 },
  marks = { heart = { cg = 11157, x = 239, y = 6 }, run = { cg = 11158, x = 238, y = 4 } },
}

-- A quest row (T_QuestList): 11012, two states of 228x30, rows 34 apart. The number is "%03d" in
-- a 38 wide box at +2, the name at +42 in 176, the clear count at -32,+1 ("★" past 9). The
-- current row is covered in see-through cyan (T_QuestListManager's frame). The brown box behind
-- the count is painted into the Quests screen's background (11011), so it is cut from there.
hub.quest = {
  box = { cg = 11011, src = { 638, 130, 32, 30 } },
  normal = { cg = 11012, src = { 0, 0, 228, 30 }, edge = { 44, 0, 5, 0 }, mid = { 60, 1 } },
  hover = { cg = 11012, src = { 228, 0, 228, 30 }, edge = { 44, 0, 5, 0 }, mid = { 60, 1 } },
  no = game { face = 258, size = 24, r = 25, g = 25, b = 25, bold = true, space = -6 },
  name = game { face = 258, size = 20, r = 255, g = 245, b = 100, space = -4 },
  name_fit = { 20, 12, 0.95, 0.5 },
  mark = game { face = 257, size = 26, r = 255, g = 255, b = 255 },
  current = { r = 0, g = 255, b = 255, a = 128 },
  gap = 4,
}

-- Frostbuy's racks (T_Shop): rect buttons whose tiles are painted into the shop's background
-- (11101 at 426,123: black and yellow lines round a dark see-through fill). Drawn from shapes.
hub.rack = {
  w = 104, h = 106,
  outer = { r = 0, g = 0, b = 0, a = 255 },
  line = { r = 214, g = 216, b = 70, a = 255 },
  fill = { r = 24, g = 10, b = 10, a = 170 },
  text = game { face = 258, size = 26, bold = true, r = 248, g = 232, b = 64, outline = { 40, 28, 20 }, space = -6 },
  fit = { 26, 14, 0.95, 0.5 },
}

-- The sort column (T_Sort, vertical): the top of 10042 (77 wide, "SORT" and the row lines) cut
-- to 21 + 30 a row, then its last line; buttons 10043 (two states of 77x26) from +21, 30 apart;
-- the current one framed by 10018 at -2,-2. Text 18 px black at 80% (the game's faint white
-- shadow, at 25%, is left out: a shadow here has no alpha).
hub.sort = {
  -- The column is see-through; on the Quests screen it lies on parchment, cut from there for
  -- under it (opts.back = false leaves it out).
  back = { cg = 11011, src = { 943, 347, 77, 232 }, vfit = "crop" },
  base = { cg = 10042, w = 77, h = 742 },
  normal = { cg = 10043, src = { 0, 0, 77, 26 } },
  hover = { cg = 10043, src = { 77, 0, 77, 26 } },
  current = { cg = 10018 },
  text = game { face = 258, size = 18, r = 0, g = 0, b = 0, a = 204, space = -6 },
  fit = { 18, 12, 0.95, 0.5 },
  fit_w = 73,
}

-- The game's scroll bar (T_ScrollBar, vertical): arrows 10001 and 10002 (two states of 20x40),
-- the bar cut from the top of 10005 with its last 2 lines, at least 10 long. The track is drawn
-- into each screen's background; this one is cut from the Quests screen's (11011).
hub.scroll = {
  up = { cg = 10001, src = { 0, 0, 20, 40 } },
  up_hover = { cg = 10001, src = { 20, 0, 20, 40 } },
  down = { cg = 10002, src = { 0, 0, 20, 40 } },
  down_hover = { cg = 10002, src = { 20, 0, 20, 40 } },
  bar = { cg = 10005, src = { 0, 0, 20, 768 }, edge = { 0, 0, 0, 2 }, vfit = "crop" },
  track = { cg = 11011, src = { 919, 100, 20, 438 }, edge = { 0, 2, 0, 2 } },
  w = 20, arrow = 40, min = 10,
}

-- The close button found on the hub's windows: 10007, two states of 24x20.
hub.close = {
  normal = { cg = 10007, src = { 0, 0, 24, 20 } },
  hover = { cg = 10007, src = { 24, 0, 24, 20 } },
}

-- A page over the hub: the area under the town tabs and above the footer, the game dimmed under
-- it, the hub's yellow-green edge, a title on the current tab's piece, and the close button.
hub.hub_page = {
  x = 0, y = 56, w = 1024, h = 536,
  dim = { r = 0, g = 0, b = 0, a = 215 },
  outer = { r = 0, g = 0, b = 0, a = 255 },
  line = { r = 156, g = 208, b = 16, a = 255 },
  title = game { face = 258, size = 24, bold = true, r = 236, g = 232, b = 176, outline = { 58, 53, 24 } },
  pad = 16,
}

ui.themes.hub = hub

-- A hub piece from the theme in use, else from themes.hub, so a copy of the hub theme can change
-- some of them.
local function T(name)
  local p = ui.theme[name]
  if p == nil then p = hub[name] end
  return p
end

local function style_of(kind, name)
  local own = ui.theme.styles and ui.theme.styles[kind]
  local s = own and own[name] or (hub.styles[kind] and hub.styles[kind][name])
  if not s then error("uikit: no " .. kind .. " style " .. tostring(name), 3) end
  return s
end

-- ---- Text that fits ----

-- The game's FitText2: the biggest size from fit[1] down by 2 to fit[2] whose text fits at the
-- largest spacing, or fits between the two spacings (the largest that does, in steps of 1/20).
-- Results are kept by text, width and style.
local fits, nfits = {}, 0

local function fitted(text, style, fit, maxw)
  if not fit or text == "" then return style end
  local key = text .. "\0" .. maxw .. "\0" .. tostring(style) .. "\0" .. tostring(fit)
  local hit = fits[key]
  if hit then return hit end
  local size, out = fit[1], nil
  while true do
    local s = with(style, { size = size, spacing = fit[3] })
    if text_w(text, s) <= maxw then
      out = s
      break
    end
    s.spacing = fit[4]
    if text_w(text, s) <= maxw then
      local lo, hi = floor(fit[4] * 20 + 0.5), floor(fit[3] * 20 + 0.5)
      while hi - lo > 1 do
        local mid = (lo + hi) // 2
        s.spacing = mid / 20
        if text_w(text, s) <= maxw then lo = mid else hi = mid end
      end
      s.spacing = lo / 20
      out = s
      break
    end
    if size - 2 < fit[2] then
      out = s
      break
    end
    size = size - 2
  end
  if nfits >= 1000 then fits, nfits = {}, 0 end
  fits[key] = out
  nfits = nfits + 1
  return out
end

local GREY = { tint = { 128, 128, 128 } }

local function cover(x, y, w, h)
  local c = T("button_cover")
  hud.rect(x, y, w, h, c.r, c.g, c.b, floor(c.a * (128 + 128 * pulse()) / 255))
end

local function box_line(x, y, w, h, c)
  hud.rect(x, y, w, 1, c.r, c.g, c.b, c.a)
  hud.rect(x, y + h - 1, w, 1, c.r, c.g, c.b, c.a)
  hud.rect(x, y + 1, 1, h - 2, c.r, c.g, c.b, c.a)
  hud.rect(x + w - 1, y + 1, 1, h - 2, c.r, c.g, c.b, c.a)
end

-- A button of a style at a box: draws it, returns hovered and clicked.
local function styled(st, text, x, y, w, h, id, disabled)
  local over, clicked = interact(id, x, y, w, h, disabled)
  local hot = over and not disabled
  ui.draw(hot and st.hover or st.normal, x, y, w, h, disabled and GREY or nil)
  if text and text ~= "" then
    local ts = fitted(text, st.text, st.fit, w - 2 * (st.pad or 4))
    local s = with(ts, { w = w, h = h, align = "center", valign = "center" })
    if disabled then s.r, s.g, s.b = floor(s.r / 2 + 64), floor(s.g / 2 + 64), floor(s.b / 2 + 64) end
    hud.text(x, y + (hot and st.hover_dy or 0), text, s)
  end
  if hot and st.cover then cover(x, y, w, h) end
  if clicked then play("click") end
  return over, clicked
end

-- ---- Buttons ----

local base_button = ui.button

---ui.button with opts.style ("large", "choice", "choice_tall", "short"), or the theme's
---button_style, draws one of the hub's grey buttons. Its size is the style's unless opts.w or h.
function ui.button(text, o)
  local name = o and o.style or ui.theme.button_style
  if not name then return base_button(text, o) end
  o = o or {}
  local st = style_of("button", name)
  local id = id_of(o, "button")
  local x, y, w, h = place(o, st.w, st.h)
  local _, clicked = styled(st, text, x, y, w, h, id, o.disabled)
  show("button", o.id or text, text, x, y, w, h, { disabled = o.disabled })
  return clicked
end

local base_tabs = ui.tabs

---ui.tabs with opts.style = "town" (or the theme's tabs_style) draws the hub's town tabs.
function ui.tabs(list, index, o)
  local name = o and o.style or ui.theme.tabs_style
  if not name then return base_tabs(list, index, o) end
  o = o or {}
  local st = style_of("tabs", name)
  local tabw = o.w or st.w
  local changed = false
  local key = o.id ~= nil and tostring(o.id) or nil
  ui.row({ x = o.x, y = o.y, gap = o.gap or st.gap }, function()
    for i, text in ipairs(list) do
      local id = id_of({ id = key and (key .. "." .. i) }, "tab")
      local x, y, w, h = place(nil, tabw, st.h)
      local current = i == index
      local over, clicked = interact(id, x, y, w, h, current)
      ui.draw(current and st.current or st.normal, x, y, w, h)
      local ts = fitted(text, st.text, st.fit, w - 2 * st.pad)
      hud.text(x, y, text, with(ts, { w = w, h = h, align = "center", valign = "center" }))
      if over and not current and st.cover then cover(x, y, w, h) end
      show("tab", key and (key .. "-" .. i) or text, text, x, y, w, h, { selected = current })
      if clicked then
        play("click")
        index, changed = i, true
      end
    end
  end)
  return index, changed
end

---The close button. opts: x, y, id, disabled. Returns true on the update it is clicked.
function ui.close(o)
  o = o or {}
  local c = T("close")
  local id = id_of(o, "close")
  local x, y, w, h = place(o, 24, 20)
  local over, clicked = interact(id, x, y, w, h, o.disabled)
  ui.draw((over and not o.disabled) and c.hover or c.normal, x, y, w, h, o.disabled and GREY or nil)
  if clicked then play("click") end
  show("button", o.id ~= nil and ("close-" .. tostring(o.id)) or "close", "Close", x, y, w, h, { disabled = o.disabled })
  return clicked
end

-- ---- Rows ----

---A room row of Rance Castle. o: text, face (a CG number, drawn 24x29), new, mark ("heart" or
---"run"), x, y, id, disabled. Returns true on the update its button is clicked.
function ui.castle_row(o)
  o = o or {}
  local c = T("castle")
  local id = id_of(o, "castle")
  local x, y, w, h = place(o, 270, 36)
  ui.draw(c.base, x, y)
  local _, clicked = styled(style_of("button", "large"), o.text, x + 3, y + 3, 206, 30, id, o.disabled)
  ui.draw(c.frame, x + 212, y + 3)
  if o.face then
    hud.cg(x + 213, y + 4, o.face, { w = 24, h = 29 })
  else
    hud.rect(x + 213, y + 4, 24, 29, 0, 0, 0, 255)
  end
  local mark = o.mark ~= nil and c.marks[o.mark]
  if o.mark ~= nil and not mark then error("uikit.castle_row: mark is \"heart\" or \"run\"", 2) end
  if mark then ui.draw(mark, x + mark.x, y + mark.y) end
  if o.new then ui.draw(c.new, x - 4, y - 3) end
  show("button", o.id or o.text, o.text, x + 3, y + 3, 206, 30, { disabled = o.disabled })
  return clicked
end

local function mark_text(m)
  if m == nil or m == false then return nil end
  if type(m) == "number" then
    if m <= 0 then return nil end
    return m > 9 and "★" or tostring(floor(m))
  end
  return tostring(m)
end

---A quest row. o: no (a number is written "%03d"), name, mark (the clear count, or a text),
---box (false leaves out the box for the count, left of the row), current (covered in cyan), x, y,
---w, id, disabled. Returns true on the update it is clicked.
function ui.quest_row(o)
  o = o or {}
  local q = T("quest")
  local id = id_of(o, "quest")
  local x, y, w, h = place(o, 228, 30)
  local over, clicked = interact(id, x, y, w, h, o.disabled)
  ui.draw((over and not o.disabled) and q.hover or q.normal, x, y, w, h, o.disabled and GREY or nil)
  local no = o.no
  if type(no) == "number" then no = string.format("%03d", floor(no)) end
  if no and no ~= "" then
    hud.text(x + 2, y, tostring(no), with(q.no, { w = 38, h = h, align = "center", valign = "center" }))
  end
  if o.name and o.name ~= "" then
    local s = with(fitted(o.name, q.name, q.name_fit, w - 52), { h = h, valign = "center" })
    if o.disabled then s.r, s.g, s.b = 160, 150, 90 end
    hud.text(x + 42, y, o.name, s)
  end
  if o.box ~= false and q.box then ui.draw(q.box, x - 38, y) end
  local m = mark_text(o.mark)
  if m then hud.text(x - 32, y + 1, m, q.mark) end
  if o.current then
    local c = q.current
    hud.rect(x, y, w, h, c.r, c.g, c.b, c.a)
  end
  if clicked then play("click") end
  show("row", o.id or (tostring(no or "") .. "-" .. tostring(o.name or "")), o.name or no, x, y, w, h,
    { selected = o.current and true or false, disabled = o.disabled, value = no and tostring(no) or nil })
  return clicked
end

---The game's scroll bar, up and down. top is the first shown of count, shown at once (any unit:
---rows, or pixels with opts.step). opts: x, y, h (needed, with the arrows), id, step (the arrows
---and the wheel, default 1), wheel = {x, y, w, h} (where the wheel scrolls it; default the bar),
---disabled. Returns the new top and whether it changed on this update.
function ui.scrollbar(top, count, shown, o)
  o = o or {}
  if type(o.h) ~= "number" then error("uikit.scrollbar: h is needed", 2) end
  local s = T("scroll")
  local id = id_of(o, "scrollbar")
  local st = ui.state(id)
  local aw, ah = s.w, s.arrow
  local x, y, w, h = place(o, aw, o.h)
  local len = h - 2 * ah
  if len < s.min then error("uikit.scrollbar: h is too small for the arrows", 2) end
  local step = o.step or 1
  local max_top = max(0, count - shown)
  local old = top
  top = min(max(top or 0, 0), max_top)
  -- The bar as T_ScrollBar::setValue sizes it.
  local size, pos
  if count > 0 and shown > 0 and count > shown then
    size = min(max(floor(len * shown / count), s.min), len)
    pos = top < max_top and floor(len * top / count) or (len - size)
  else
    size, pos = len, 0
  end
  local ty = y + ah
  local dis = o.disabled or max_top == 0
  local up_over, up = interact(id .. ".up", x, y, aw, ah, dis or top <= 0)
  local dn_over, dn = interact(id .. ".down", x, y + h - ah, aw, ah, dis or top >= max_top)
  local _, _, held = interact(id .. ".bar", x, ty + pos, aw, size, dis)
  local on_bar = inside(x, ty + pos, aw, size)
  local _, track = interact(id .. ".track", x, ty, aw, len, dis or on_bar or held)
  if up then top = top - step end
  if dn then top = top + step end
  if held then
    if mouse.pressed or st.grab == nil then st.grab = mouse.y - (ty + pos) end
    local p = min(max(mouse.y - st.grab - ty, 0), len - size)
    top = (p >= len - size) and max_top or floor(p * count / len + 0.5)
  else
    st.grab = nil
  end
  if track then
    -- A page up or down, toward the click.
    if mouse.y < ty + pos then top = top - shown else top = top + shown end
  end
  local wb = o.wheel
  local wheel_on = wb and inside(wb[1], wb[2], wb[3], wb[4]) or inside(x, y, aw, h)
  if wheel_on and not dis and mouse.scroll ~= 0 then
    top = top - mouse.scroll * step
    input.scroll(true)
  end
  top = min(max(top, 0), max_top)
  if top ~= old and not held then
    if count > shown then
      size = min(max(floor(len * shown / count), s.min), len)
      pos = top < max_top and floor(len * top / count) or (len - size)
    end
  elseif held then
    pos = min(max(mouse.y - st.grab - ty, 0), len - size)
  end
  ui.draw(s.track, x, ty, aw, len)
  if max_top > 0 then ui.draw(s.bar, x, ty + pos, aw, size) end
  ui.draw((up_over and top > 0 and not dis) and s.up_hover or s.up, x, y, aw, ah,
    (dis or top <= 0) and GREY or nil)
  ui.draw((dn_over and top < max_top and not dis) and s.down_hover or s.down, x, y + h - ah, aw, ah,
    (dis or top >= max_top) and GREY or nil)
  if (up or dn or track) then play("click") end
  show("scrollbar", o.id or "scrollbar", o.label, x, y, aw, h,
    { value = string.format("%d of %d", top, max_top), disabled = dis })
  return top, top ~= old
end

---The quest list: rows 34 apart (13 by default) and the game's scroll bar beside them, which
---the wheel over the rows moves a row at a time. rows hold {no, name, mark, disabled}. opts: x, y,
---id (keeps the scroll), shown. Returns the chosen index and whether it changed on this update.
function ui.quest_list(rows, index, o)
  o = o or {}
  local q = T("quest")
  local id = id_of(o, "questlist")
  local st = ui.state(id)
  local n = o.shown or 13
  local rh = 30 + q.gap
  local x, y, w, h = place(o, 228 + 15 + 20, n * rh - q.gap)
  local max_top = max(0, #rows - n)
  st.top = min(max(st.top or 0, 0), max_top)
  local key = o.id ~= nil and tostring(o.id) or "quests"
  local changed = false
  for i = 1, n do
    local k = st.top + i
    local r = rows[k]
    if r then
      local c = ui.quest_row({ x = x, y = y + (i - 1) * rh, no = r.no, name = r.name, mark = r.mark,
        disabled = r.disabled, current = k == index, id = key .. "." .. k })
      if c and k ~= index then index, changed = k, true end
    end
  end
  st.top = ui.scrollbar(st.top, #rows, n, { x = x + 243, y = y, h = h, id = key .. ".bar",
    wheel = { x, y, 228, h } })
  if inside(x, y, 228, h) then
    frame.over = true
    hud.block_game_input()
  end
  return index, changed
end

-- ---- Tiles and columns ----

local function lines_of(text)
  local out = {}
  for l in (tostring(text) .. "\n"):gmatch("(.-)\n") do out[#out + 1] = l end
  return out
end

---A Frostbuy rack tile: a dark see-through square with the shop's lines and yellow text, one
---line per "\n". opts: x, y, w, h (default 104 by 106), id, disabled. Returns true when clicked.
function ui.rack(text, o)
  o = o or {}
  local r = T("rack")
  local id = id_of(o, "rack")
  local x, y, w, h = place(o, r.w, r.h)
  local over, clicked = interact(id, x, y, w, h, o.disabled)
  box_line(x, y, w, h, r.outer)
  box_line(x + 1, y + 1, w - 2, h - 2, r.line)
  local f = r.fill
  hud.rect(x + 2, y + 2, w - 4, h - 4, f.r, f.g, f.b, f.a)
  local ls = lines_of(text)
  -- One size for every line: the one that fits the widest.
  local widest, ts = ls[1] or "", r.text
  for _, l in ipairs(ls) do
    if text_w(l, r.text) > text_w(widest, r.text) then widest = l end
  end
  ts = fitted(widest, r.text, r.fit, w - 12)
  local lh = ts.size
  local ty = y + floor((h - lh * #ls) / 2)
  for i, l in ipairs(ls) do
    local s = with(ts, { w = w, h = lh, align = "center", valign = "center" })
    if o.disabled then s.r, s.g, s.b = 140, 130, 70 end
    hud.text(x, ty + (i - 1) * lh, l, s)
  end
  if over and not o.disabled then cover(x, y, w, h) end
  if clicked then play("click") end
  local label = table.concat(ls, " ")
  show("button", o.id or label, label, x, y, w, h, { disabled = o.disabled })
  return clicked
end

---The sort column: "SORT" over up to 16 buttons, the current one framed, on the Quests screen's
---parchment. opts: x, y, id, back (false: see-through, as the game draws it on its own
---background), disabled. Returns the index and whether it changed on this update.
function ui.sort(list, index, o)
  o = o or {}
  local s = T("sort")
  local n = min(#list, 16)
  local bh = 21 + 30 * n
  local x, y, w, h = place(o, 77, bh + 1)
  if o.back ~= false and s.back then ui.draw(s.back, x, y, 77, bh + 1) end
  hud.cg(x, y, s.base.cg, { src = { 0, 0, 77, bh } })
  hud.cg(x, y + bh, s.base.cg, { src = { 0, s.base.h - 1, 77, 1 } })
  local key = o.id ~= nil and tostring(o.id) or "sort"
  local changed = false
  for i = 1, n do
    local by = y + 21 + 30 * (i - 1)
    local id = id_of({ id = key .. "." .. i }, "sort")
    local current = i == index
    local over, clicked = interact(id, x, by, 77, 26, current or o.disabled)
    ui.draw((over and not current and not o.disabled) and s.hover or s.normal, x, by, 77, 26)
    local text = tostring(list[i])
    hud.text(x, by, text, with(fitted(text, s.text, s.fit, s.fit_w), { w = 77, h = 26, align = "center",
      valign = "center" }))
    if current then ui.draw(s.current, x - 2, by - 2) end
    show("tab", key .. "-" .. text, text, x, by, 77, 26, { selected = current, disabled = o.disabled })
    if clicked then
      play("click")
      index, changed = i, true
    end
  end
  return index, changed
end

-- ---- A page over the hub ----

local ESC = input.code("esc")

---A page of a mod's own over the hub, under the town tabs and above the footer. While it shows
---the game gets no mouse and no keys. It closes by its close button, a right click or Esc.
---opts: id, title, x, y, w, h (default 0, 56, 1024, 536), pad, gap. fn runs in a column inside.
---Returns true while it stays open: false on the update it closes.
function ui.page(o, fn)
  if type(o) == "function" then o, fn = nil, o end
  o = o or {}
  local p = T("hub_page")
  local id = id_of(o, "page")
  local st = ui.state(id)
  local x, y, w, h = o.x or p.x, o.y or p.y, o.w or p.w, o.h or p.h
  hud.capture_mouse()
  hud.block_keys()
  frame.over = true
  local d = p.dim
  hud.rect(x, y, w, h, d.r, d.g, d.b, d.a)
  box_line(x, y, w, h, p.outer)
  box_line(x + 1, y + 1, w - 2, h - 2, p.line)
  box_line(x + 2, y + 2, w - 4, h - 4, p.line)
  local pad = o.pad or p.pad
  local top = y + pad
  if o.title then
    local town = style_of("tabs", "town")
    local tw = max(200, text_w(o.title, p.title) + 2 * town.pad + 8)
    ui.draw(town.current, x + pad, y + 8, tw, 45)
    hud.text(x + pad, y + 8, o.title, with(p.title, { w = tw, h = 45, align = "center", valign = "center" }))
    top = y + 8 + 45 + 8
  end
  local closed = ui.close({ x = x + w - 24 - pad, y = y + pad - 4, id = o.id ~= nil and o.id or "page" })
  -- A right click or Esc closes when let go, so the game does not get the rest of it.
  local esc = input.key(ESC)
  if not closed and (mouse.right_released or (st.esc and not esc)) then
    closed = true
    play("click")
  end
  st.esc = esc
  show("page", o.id or o.title or "page", o.title, x, y, w, h)
  if fn then
    local cx, cy, cw, ch = x + pad, top, w - 2 * pad, y + h - pad - top
    hud.clip_push(cx, cy, cw, ch)
    local ok, err = pcall(ui.col, { x = cx, y = cy, w = cw, gap = o.gap or 0 }, function() fn(cx, cy, cw, ch) end)
    hud.clip_pop()
    if not ok then error(err, 0) end
  end
  if closed then st.esc = nil end
  return not closed
end
