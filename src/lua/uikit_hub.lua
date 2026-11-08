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
local focus, focus_group = kit.focus, kit.focus_group
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
-- the current one framed by 10018 at -2,-2. Text 18 px black at 80%, with the game's faint white
-- shadow right and down at 25% (setShadowRate(64), shadow.a since 2.11.0).
hub.sort = {
  -- The column is see-through; on the Quests screen it lies on parchment, cut from there for
  -- under it (opts.back = false leaves it out).
  back = { cg = 11011, src = { 943, 347, 77, 232 }, vfit = "crop" },
  base = { cg = 10042, w = 77, h = 742 },
  normal = { cg = 10043, src = { 0, 0, 77, 26 } },
  hover = { cg = 10043, src = { 77, 0, 77, 26 } },
  current = { cg = 10018 },
  text = game { face = 258, size = 18, r = 0, g = 0, b = 0, a = 204, space = -6,
    shadow = { x = 1, y = 1, r = 255, g = 255, b = 255, a = 64 } },
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
local function styled(st, text, x, y, w, h, id, disabled, noinput, io)
  local over, clicked = false, false
  if not noinput then over, clicked = interact(id, x, y, w, h, disabled, io) end
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
  local _, clicked = styled(st, text, x, y, w, h, id, o.disabled, nil, { nav = o.nav })
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
  local over, clicked = interact(id, x, y, w, h, o.disabled, { nav = o.nav })
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
  local _, clicked = styled(style_of("button", "large"), o.text, x + 3, y + 3, 206, 30, id, o.disabled, nil,
    { nav = o.nav })
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
  local over, clicked = interact(id, x, y, w, h, o.disabled, { nav = o.nav, group = o.group })
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
  -- The arrows and the track move on the press, as T_ScrollBar does; held, they wait 250 ms and
  -- then move on every update while the button stays down, wherever the mouse goes.
  -- The bar's parts take the mouse only: the keys' cursor scrolls the lists by their rows.
  local MO = { stop = false }
  local up_over, _, up_held = interact(id .. ".up", x, y, aw, ah, dis or top <= 0, MO)
  local dn_over, _, dn_held = interact(id .. ".down", x, y + h - ah, aw, ah, dis or top >= max_top, MO)
  local _, _, held = interact(id .. ".bar", x, ty + pos, aw, size, dis, MO)
  local on_bar = inside(x, ty + pos, aw, size)
  local _, _, tr_held = interact(id .. ".track", x, ty, aw, len, dis or (on_bar and not st.paging) or held, MO)
  local now = rq.time_ms()
  local function repeats(on)
    if not on then return false end
    if mouse.pressed then
      st.rep_at = now + 250
      return true
    end
    return st.rep_at ~= nil and now >= st.rep_at
  end
  local up = repeats(up_held)
  local dn = repeats(dn_held)
  local track = repeats(tr_held)
  if not (up_held or dn_held or tr_held) then st.rep_at, st.paging = nil, nil end
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
    -- A page up or down, toward the mouse; held, it stops once the bar reaches the mouse.
    if mouse.pressed then st.paging = mouse.y < ty + pos and -1 or 1 end
    if st.paging == -1 and mouse.y < ty + pos then
      top = top - shown
    elseif st.paging == 1 and mouse.y >= ty + pos + size then
      top = top + shown
    else
      track = false
    end
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
  if (up or dn or track) and mouse.pressed then play("click") end
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
  -- The keys' cursor goes through every row and the list follows it (2.12.0).
  local ids, pre = {}, "quest:" .. key .. "."
  for k, r in ipairs(rows) do
    if not r.disabled then ids[#ids + 1] = pre .. k end
  end
  focus_group(id, ids, { home = index and (pre .. index) or nil, box = { x = x, y = y, w = 228, h = h } })
  local fk = focus.src == "keys" and type(focus.id) == "string" and focus.id:sub(1, #pre) == pre
    and tonumber(focus.id:sub(#pre + 1))
  if fk then
    if fk <= st.top then st.top = fk - 1 elseif fk > st.top + n then st.top = fk - n end
    st.top = min(max(st.top, 0), max_top)
  end
  for i = 1, n do
    local k = st.top + i
    local r = rows[k]
    if r then
      local c = ui.quest_row({ x = x, y = y + (i - 1) * rh, no = r.no, name = r.name, mark = r.mark,
        disabled = r.disabled, current = k == index, id = key .. "." .. k, group = id })
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
  local over, clicked = interact(id, x, y, w, h, o.disabled, { nav = o.nav })
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
---background), again (true, or a button's index: that button can be clicked while it is the
---current one, as a store's second SORT button, which opens the sort menu each time), disabled. Returns the index, whether it changed on this
---update, and the index of the button clicked on this update (nil if none).
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
  local changed, pressed = false, nil
  for i = 1, n do
    local by = y + 21 + 30 * (i - 1)
    local id = id_of({ id = key .. "." .. i }, "sort")
    local current = i == index
    local again = o.again == true or o.again == i
    local over, clicked = interact(id, x, by, 77, 26, (current and not again) or o.disabled)
    ui.draw((over and (again or not current) and not o.disabled) and s.hover or s.normal, x, by, 77, 26)
    local text = tostring(list[i])
    hud.text(x, by, text, with(fitted(text, s.text, s.fit, s.fit_w), { w = 77, h = 26, align = "center",
      valign = "center" }))
    if current then ui.draw(s.current, x - 2, by - 2) end
    show("tab", key .. "-" .. text, text, x, by, 77, 26, { selected = current, disabled = o.disabled })
    if clicked then
      play("click")
      if i ~= index then changed = true end
      index, pressed = i, i
    end
  end
  return index, changed, pressed
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
  -- Under the game's choice menu (rq.ui.choose) the page stays drawn, under it, and leaves the
  -- mouse and the keys to the menu.
  if not frame.blind then
    hud.capture_mouse()
    hud.block_keys()
    ui.keys() -- the arrows, Enter, Page Up and Down move the kit's cursor (2.12.0)
  end
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
  -- A right click or Esc closes when let go, so the game does not get the rest of it. Only a
  -- press the page saw counts: not the one that closed a choice menu over it.
  local esc = input.key(ESC)
  if frame.blind then
    st.esc = nil
  elseif esc and not st.esc_was then
    st.esc = true
  end
  st.esc_was = esc
  if not closed and (mouse.right_released or (st.esc and not esc)) then
    closed = true
    play("click")
  end
  if not esc then st.esc = nil end
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

-- ---- The choice menu's look, inside a page ----

-- The choice menu (T_Select, C_SelectCg): the base 10021 cut to the height, with its last 6 lines
-- under it; buttons 10022 (10029 when fewer than 3) at +18; text rows on 10024 to 10026 (1 to 3
-- lines) at +7; separators 10023 at +4. The space before a row depends on the row above it
-- (T_Select::getSpace).
hub.options = {
  base = { cg = 10021, w = 380, h = 768 },
  sep = { cg = 10023 },
  panels = { { cg = 10024, ph = 35 }, { cg = 10025, ph = 60 }, { cg = 10026, ph = 85 } },
  text = game { face = 258, size = 24, r = 0, g = 0, b = 0, space = -4,
    shadow = { x = 1, y = 1, r = 255, g = 255, b = 255 } },
  text_fit = { 24, 12, 0.95, 0.47 },
  comment = game { face = 258, size = 18, r = 236, g = 232, b = 176, outline = { 0, 0, 0 } },
}

-- ---- Keys ----

local codes = {}
local function code(name)
  if math.type(name) == "integer" then return name end
  local c = codes[name]
  if not c then
    c = input.code(name)
    codes[name] = c
  end
  return c
end

-- A key's presses as the game repeats them: on the press, then after 250 ms every 50 ms
-- (T_InputKeyboard::arrow*; 400 and 80 before 2.12.0, which were not the game's). Kept in
-- the widget's state. A key held while the game's choice menu shows (its Enter, say) counts again
-- only once it is let go.
local function key_hit(st, name)
  local down = input.key(code(name))
  local k = st.keys
  if not k then
    k = {}
    st.keys = k
  end
  if not down then
    k[name] = nil
    return false
  end
  if frame.blind then
    k[name] = math.huge
    return false
  end
  local now, at = rq.time_ms(), k[name]
  if not at then
    -- A key already down when the widget first sees it does not count.
    k[name] = st.keys_seen and now + 250 or math.huge
    return st.keys_seen and true or false
  end
  if now >= at then
    k[name] = now + 50
    return true
  end
  return false
end

local function keys_seen(st) st.keys_seen = true end

local function space(kind, prev)
  if prev == nil then return 8 end
  if prev == "button" then return 6 end
  if prev == "text" then return kind == "text" and 0 or 8 end
  return kind == "sep" and 1 or 6
end

-- Breaks a text into lines of at most maxw in style, at spaces when it can (the game's
-- WordWrapText), and at each line break; keeps at most most lines.
local function wrap_lines(text, style, maxw, most)
  local out = {}
  for para in (tostring(text) .. "\n"):gmatch("(.-)\n") do
    local line = ""
    for word in para:gmatch("%S+") do
      local try = line == "" and word or (line .. " " .. word)
      if line ~= "" and text_w(try, style) > maxw then
        out[#out + 1] = line
        line = word
      else
        line = try
      end
    end
    out[#out + 1] = line
  end
  while #out > most do table.remove(out) end
  return out
end

---The choice menu's look as a widget inside a page: not modal, drawn with the HUD. items: a text,
---"-" for a separator, or {id, text, r, g, b, comment, disabled}; opts: x, y, id, message (text
---rows at the top), back (a Return row, true or its text; picking it gives 0), keys (Up and Down
---move a cursor, wrapping, Enter picks its row and Esc the Return row, as in the game's menu), h
---(the most height, default and at most 762: past it the rows under the message scroll, by the
---wheel, the keys' cursor and the game's scroll bar right of the base).
---Returns the id picked on this update (the row's id, else its index), or nil.
function ui.options(items, o)
  o = o or {}
  if type(items) ~= "table" then error("uikit.options: expected a list of rows", 2) end
  local op = T("options")
  local key = o.id ~= nil and tostring(o.id) or "options"
  -- The rows, as the game lays them out.
  local rows, picks = {}, 0
  if o.message then
    rows[#rows + 1] = { kind = "text", text = o.message }
    rows[#rows + 1] = { kind = "sep" }
  end
  for i, it in ipairs(items) do
    if it == "-" then
      rows[#rows + 1] = { kind = "sep" }
    else
      local r = type(it) == "table" and it or { text = it }
      if type(r.text) ~= "string" then error("uikit.options: row " .. i .. " has no text", 2) end
      local id = r.id
      if id == nil then id = i end
      rows[#rows + 1] = { kind = "button", text = r.text, id = id, r = r.r, g = r.g, b = r.b,
        comment = r.comment, disabled = r.disabled }
      picks = picks + 1
    end
  end
  local back = o.back
  if back == true then back = "Return" end
  if back then
    rows[#rows + 1] = { kind = "sep" }
    rows[#rows + 1] = { kind = "button", text = back, id = 0, back = true }
    picks = picks + 1
  end
  local large = picks < 3
  local bst = style_of("button", large and "choice_tall" or "choice")
  local hh, prev = 0, nil
  for _, r in ipairs(rows) do
    hh = hh + space(r.kind, prev)
    r.y = hh
    if r.kind == "button" then
      hh = hh + bst.h
    elseif r.kind == "text" then
      r.lines = wrap_lines(r.text, with(op.text, { spacing = op.text_fit[3] }), 346, 3)
      hh = hh + op.panels[#r.lines].ph
    else
      hh = hh + 7
    end
    prev = r.kind
  end
  local full = hh + space(nil, prev)
  if o.h ~= nil and math.type(o.h) ~= "integer" then error("uikit.options: h is a whole number of pixels", 2) end
  local cap = min(max(o.h or 762, 1), 762)
  -- The message stays at the top; past cap the rows under it scroll, with the game's bar beside
  -- the base.
  local head = o.message and (rows[2].y + 7) or 0
  local view = cap - head
  local scroll = full > cap and view >= 2 * T("scroll").arrow + T("scroll").min
  hh = min(full, cap)
  local W = 380
  local x, y = place(o, W + (scroll and 22 or 0), hh + 6)
  local ss = ui.state("optionsscroll:" .. key)
  local max_top = scroll and (full - head - view) or 0
  ss.top = min(max(ss.top or 0, 0), max_top)
  -- The base: its top cut to the height, then its last 6 lines.
  hud.cg(x, y, op.base.cg, { src = { 0, 0, W, hh } })
  hud.cg(x, y + hh, op.base.cg, { src = { 0, 762, W, 6 } })
  local picked, over_comment
  -- The rows that can be picked are a list for the keys' cursor, wrapping as the game's menu
  -- (2.12.0: the kit's cursor; before, a cursor of its own).
  local gid, pre = "options:" .. key, "options:" .. key .. "."
  local ids = {}
  for i, r in ipairs(rows) do
    if r.kind == "button" and not r.disabled then ids[#ids + 1] = pre .. i end
  end
  focus_group(gid, ids, { wrap = true, box = { x = x, y = y, w = W, h = hh } })
  local fi = type(focus.id) == "string" and focus.id:sub(1, #pre) == pre and tonumber(focus.id:sub(#pre + 1))
  if fi and focus.src == "keys" and scroll and rows[fi] and rows[fi].y >= head then
    -- The keys' cursor stays in sight.
    local r = rows[fi]
    if r.y - head < ss.top then ss.top = r.y - head end
    if r.y + bst.h - head > ss.top + view then ss.top = r.y + bst.h - head - view end
    ss.top = min(max(ss.top, 0), max_top)
  end
  local ks = o.keys and ui.state("optionskeys:" .. key)
  if ks then
    ui.keys()
    if back and key_hit(ks, "esc") then
      picked = 0
      play("click")
    end
    keys_seen(ks)
    if not frame.blind then hud.block_keys({ "esc" }) end
  end
  if scroll then
    ss.top = ui.scrollbar(ss.top, full - head, view, { x = x + W + 2, y = y + head, h = view,
      id = key .. ".bar", step = bst.h + 6, wheel = { x, y, W, hh + 6 } })
  end
  -- Rows under the message, inside the view: only they take the mouse.
  local in_view = inside(x, y + head, W, view)
  local clipped = false
  for i, r in ipairs(rows) do
    local ry = y + r.y
    local below = r.y >= head
    if below and not clipped then
      hud.clip_push(x, y + head, W, view)
      clipped = true
    end
    if below then ry = ry - ss.top end
    local rh = r.kind == "button" and bst.h or r.kind == "sep" and 7 or 0
    local seen = not below or (ry + rh > y + head and ry < y + cap)
    local whole = not below or (ry >= y + head and ry + rh <= y + cap)
    if not seen then
      -- Out of the view: not drawn.
    elseif r.kind == "sep" then
      ui.draw(op.sep, x + 4, ry)
    elseif r.kind == "text" then
      local n = #r.lines
      ui.draw(op.panels[n], x + 7, ry)
      for li, l in ipairs(r.lines) do
        local s = fitted(l, op.text, op.text_fit, 346)
        hud.text(x + 7, ry + 7 + (li - 1) * 25, l, with(s, { w = 362, align = "center" }))
      end
    else
      local st = bst
      if r.r or r.g or r.b then
        st = kit.copy(bst)
        st.text = with(bst.text, { r = r.r or 196, g = r.g or 196, b = r.b or 196 })
      end
      local id = id_of({ id = key .. "." .. i }, "options")
      local over, clicked = styled(st, r.text, x + 18, ry, bst.w, bst.h, id, r.disabled, false,
        { group = gid, stop = whole, view = below and { x, y + head, W, view } or nil })
      if over and not r.disabled then
        -- The game frames the row under the mouse in yellow (createFrame 216, 255, 0, 102).
        hud.rect(x + 18, ry, bst.w, bst.h, 216, 255, 0, floor(102 * (0.6 + 0.4 * pulse())))
        if r.comment and r.comment ~= "" then over_comment = { r.comment, ry } end
      end
      local slug = r.back and "return" or (key .. "-" .. tostring(r.id))
      if whole then
        show("button", slug, r.text, x + 18, ry, bst.w, bst.h, { disabled = r.disabled, value = tostring(r.id),
          selected = ks and (over and not r.disabled) or nil })
      end
      if clicked then picked = r.id end
    end
  end
  if clipped then hud.clip_pop() end
  if inside(x, y, W + (scroll and 22 or 0), hh + 6) then
    frame.over = true
    hud.block_game_input()
  end
  if over_comment then
    -- The game's comment box sits right of the menu (説明_表示 at 702); here, right of the base.
    local c = op.comment
    local lines = wrap_lines(over_comment[1], c, 260, 8)
    local tw = 0
    for _, l in ipairs(lines) do tw = max(tw, text_w(l, c)) end
    local bx, by = x + W + 6 + (scroll and 22 or 0), over_comment[2]
    -- On the top layer (2.11.0): over the widgets drawn after the list.
    hud.top(function()
      hud.rect(bx, by, tw + 16, #lines * (c.size + 4) + 10, 0, 0, 0, 200)
      box_line(bx, by, tw + 16, #lines * (c.size + 4) + 10, T("hub_page").line)
      for li, l in ipairs(lines) do hud.text(bx + 8, by + 5 + (li - 1) * (c.size + 4), l, c) end
    end)
  end
  return picked
end

-- For the vault's widgets (uikit_vault.lua, loader 2.8.0).
kit.hub = { theme = hub, T = T, style_of = style_of, fitted = fitted, cover = cover, box_line = box_line,
  styled = styled, GREY = GREY, key_hit = key_hit, keys_seen = keys_seen }
