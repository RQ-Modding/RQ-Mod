-- rq.uikit (loader 1.7.0; 1.8.0 adds ui.layers, bold, and the widgets in rqmod-ctl ui): widgets that look like the game's Config screen, drawn with rq.hud
-- from the game's own pictures. Immediate mode: call the widgets from rq.on_frame on every
-- update they should show, and use what they return. A widget goes where opts.x and opts.y
-- say, or else where the row or column it is called in puts it.
local rq = ...
local hud, input = rq.hud, rq.input

local ui = {}

-- ---- Themes ----

-- A piece is a part of a CG drawn at any size: src = {x, y, w, h} in the CG, edge = {left,
-- top, right, bottom} kept as they are, and the middle stretched. mid = {x, w} (and vmid)
-- picks the middle from elsewhere in src, such as one empty column between a frame and the
-- text the game drew on it. vfit = "crop" takes the top of the middle instead of squashing it.
-- The numbers come from the game's CGs (cgdump 40000 40099): Config's pages carry their labels
-- in the picture, so frames and tracks are cut out of them.
local config = {
  label = { size = 17 },
  dim = { r = 150, g = 190, b = 160 },
  page = { cg = 40007, src = { 950, 85, 1, 595 }, vfit = "crop" },
  block = { cg = 40004, src = { 270, 110, 300, 450 }, edge = { 3, 3, 3, 3 } },
  button = { cg = 40031, src = { 0, 0, 150, 40 }, edge = { 5, 0, 5, 0 }, mid = { 5, 1 } },
  button_text = { size = 18, r = 230, g = 230, b = 215 },
  -- The game covers a hovered button with this color, its alpha going 50% to 100% and back
  -- every 2 s (ボタンアニメ).
  button_cover = { r = 216, g = 255, b = 0, a = 102 },
  tab = {
    normal = { cg = 40011, src = { 0, 0, 120, 40 }, edge = { 6, 0, 6, 0 }, mid = { 8, 1 } },
    hover = { cg = 40012, src = { 0, 0, 120, 40 }, edge = { 6, 0, 6, 0 }, mid = { 8, 1 } },
    current = { cg = 40013, src = { 0, 0, 120, 40 }, edge = { 6, 0, 6, 0 }, mid = { 8, 1 } },
  },
  tab_text = {
    normal = { size = 16, r = 33, g = 65, b = 33, edge = false },
    hover = { size = 16, r = 255, g = 255, b = 255, edge = false },
    current = { size = 16, r = 3, g = 14, b = 64, edge = false },
  },
  check_frame = { cg = 40002, src = { 641, 106, 28, 28 } },
  check_mark = { cg = 40037, src = { 0, 0, 30, 30 } },
  hover_bar = { cg = 40046, src = { 0, 0, 120, 30 } },
  track = { cg = 40002, src = { 319, 114, 202, 17 } },
  knob = { cg = 40035, src = { 0, 0, 24, 35 } },
  knob_hot = { cg = 40036, src = { 0, 0, 24, 35 } },
  list_text = { size = 16 },
  list_current = { r = 255, g = 255, b = 160 },
  scrollbar = { r = 189, g = 195, b = 195, a = 200 },
  sound = { hover = 1001, click = 1002, error = 1003 },
}

ui.themes = { config = config }
ui.theme = config

local function copy(t)
  if type(t) ~= "table" then return t end
  local c = {}
  for k, v in pairs(t) do c[k] = copy(v) end
  return c
end

---A deep copy of a theme, to change some pieces and use it with ui.use.
function ui.copy_theme(t)
  return copy(t or config)
end

function ui.use(theme)
  if type(theme) ~= "table" then error("uikit.use: a theme table", 2) end
  ui.theme = theme
end

local function piece(name)
  if type(name) == "table" then return name end
  local p = ui.theme[name]
  if not p then error("uikit: the theme has no piece " .. tostring(name), 3) end
  return p
end

local NO_EDGE = { 0, 0, 0, 0 }

-- The parts of a piece drawn at x, y, w by h: { src = {x, y, w, h}, x, y, w, h } each.
local function parts(p, x, y, w, h)
  local s = p.src
  local sx, sy, sw, sh
  if s then
    sx, sy, sw, sh = s[1], s[2], s[3], s[4]
  else
    sx, sy = 0, 0
    sw, sh = hud.cg_size(p.cg)
    if not sw then return {} end
  end
  w, h = w or sw, h or sh
  local e = p.edge or NO_EDGE
  local l, t, r, b = e[1], e[2], e[3], e[4]
  local mx, mw = sx + l, sw - l - r
  if p.mid then mx, mw = sx + p.mid[1], p.mid[2] end
  local my, mh = sy + t, sh - t - b
  if p.vmid then my, mh = sy + p.vmid[1], p.vmid[2] end
  local dmh = h - t - b
  if p.vfit == "crop" and dmh < mh then mh = dmh end
  local cols = {
    { sx, l, x, l },
    { mx, mw, x + l, w - l - r },
    { sx + sw - r, r, x + w - r, r },
  }
  local rows = {
    { sy, t, y, t },
    { my, mh, y + t, dmh },
    { sy + sh - b, b, y + h - b, b },
  }
  local out = {}
  for _, row in ipairs(rows) do
    if row[2] > 0 and row[4] > 0 then
      for _, c in ipairs(cols) do
        if c[2] > 0 and c[4] > 0 then
          out[#out + 1] = { src = { c[1], row[1], c[2], row[2] }, x = c[3], y = row[3], w = c[4], h = row[4] }
        end
      end
    end
  end
  return out
end

---Draws a piece (a name in the theme, or a piece table) at x, y, w by h.
---opts: alpha, tint as in rq.hud.cg.
function ui.draw(p, x, y, w, h, opts)
  p = piece(p)
  local alpha, tint = opts and opts.alpha, opts and opts.tint
  for _, q in ipairs(parts(p, x, y, w, h)) do
    hud.cg(q.x, q.y, p.cg, { src = q.src, w = q.w, h = q.h, alpha = alpha, tint = tint })
  end
end

---The same piece as layers for rq.image.compose, to make a CG of it (loader 1.8.0).
function ui.layers(p, x, y, w, h)
  p = piece(p)
  local out = parts(p, x or 0, y or 0, w, h)
  for _, q in ipairs(out) do q.cg = p.cg end
  return out
end

-- ---- Input and state ----

local frame = { auto = 0, hot = nil, was_hot = nil, over = false, last = { 0, 0, 0, 0 } }
local mouse = { x = 0, y = 0, down = false, pressed = false, released = false, scroll = 0 }
local active -- the widget the left button went down on, until it comes up
local state = {} -- kept across updates, by widget id
local stack = {} -- the open rows and columns

-- What was drawn on this update, for rqmod-ctl ui (section "kit"): sent on the next update,
-- when the list is whole.
local shown, shown_ids, published = {}, {}, 0

local function slug(str)
  str = tostring(str or ""):lower():gsub("[^%w]+", "-"):gsub("^%-+", ""):gsub("%-+$", "")
  return str:sub(1, 28)
end

-- kind-key, key being opts.id or the text; the same id twice gets -2, -3.
local function show(kind, key, label, x, y, w, h, extra)
  local id = kind .. "-" .. (slug(key) ~= "" and slug(key) or tostring(#shown + 1))
  local n = shown_ids[id]
  shown_ids[id] = (n or 1) + 1
  if n then id = id .. "-" .. n end
  local row = { id = id, type = kind, label = label and tostring(label) or id, x = x, y = y, w = w, h = h }
  if extra then for k, v in pairs(extra) do row[k] = v end end
  shown[#shown + 1] = row
end

rq.on_frame(function()
  if #shown > 0 or published > 0 then
    if rq.agent and rq.agent.widgets then pcall(rq.agent.widgets, shown) end
    published = #shown
  end
  shown, shown_ids = {}, {}
  frame.was_hot, frame.hot, frame.auto, frame.over = frame.hot, nil, 0, false
  local right
  mouse.x, mouse.y, mouse.down, right, mouse.pressed, mouse.released = input.mouse()
  mouse.scroll = input.scroll()
  if not mouse.down and not mouse.released then active = nil end
  if #stack > 0 then
    rq.log("uikit: a row or column was left open; closing it")
    stack = {}
  end
end)

local function play(which)
  local no = ui.theme.sound and ui.theme.sound[which]
  if no then pcall(rq.sound.play, no) end
end

local function id_of(o, kind)
  if o and o.id ~= nil then return kind .. ":" .. tostring(o.id) end
  frame.auto = frame.auto + 1
  return kind .. "#" .. frame.auto
end

---Kept per widget across updates (scroll, drag). id is opts.id of the widget.
function ui.state(id)
  local s = state[id]
  if not s then
    s = {}
    state[id] = s
  end
  return s
end

local function inside(x, y, w, h)
  return mouse.x >= x and mouse.y >= y and mouse.x < x + w and mouse.y < y + h
end

-- Hover, press and click for a box. The game sees no mouse while a widget is under it.
-- Returns hovered, clicked (released over the box it went down on), held.
local function interact(id, x, y, w, h, disabled)
  local over = inside(x, y, w, h)
  if over then
    frame.over = true
    hud.block_game_input()
  end
  if disabled then return false, false, false end
  if over then
    frame.hot = id
    if frame.was_hot ~= id and active == nil then play("hover") end
    if mouse.pressed then active = id end
  end
  local clicked = over and mouse.released and active == id
  return over, clicked, active == id and mouse.down
end

---True when the mouse was over any kit widget on this update so far.
function ui.hovered()
  return frame.over
end

-- ---- Layout ----

local function top() return stack[#stack] end

-- Where a widget of natural size w by h goes, and its width when o.w is "fill".
local function place(o, w, h)
  o = o or {}
  local box = top()
  if o.w == "fill" then
    if not box or not box.w then error("uikit: w = \"fill\" needs a row or column with a w", 3) end
    if box.dir == "row" then
      w = box.x + box.w - box.pad - box.cx
    else
      w = box.w - 2 * box.pad
    end
  elseif type(o.w) == "number" then
    w = o.w
  end
  if type(o.h) == "number" then h = o.h end
  local x, y = o.x, o.y
  if box and (x == nil or y == nil) then
    if box.dir == "row" then
      local ay = box.cy
      if box.h and box.align == "center" then ay = box.y + math.floor((box.h - h) / 2)
      elseif box.h and box.align == "end" then ay = box.y + box.h - box.pad - h end
      x, y = x or box.cx, y or ay
      box.cx = box.cx + w + box.gap
      box.used_w = box.cx - box.gap - box.x - box.pad
      box.used_h = math.max(box.used_h, h)
    else
      local ax = box.cx
      if box.w and box.align == "center" then ax = box.x + math.floor((box.w - w) / 2)
      elseif box.w and box.align == "end" then ax = box.x + box.w - box.pad - w end
      x, y = x or ax, y or box.cy
      box.cy = box.cy + h + box.gap
      box.used_h = box.cy - box.gap - box.y - box.pad
      box.used_w = math.max(box.used_w, w)
    end
  end
  x, y = x or 0, y or 0
  frame.last[1], frame.last[2], frame.last[3], frame.last[4] = x, y, w, h
  return x, y, w, h
end

---x, y, w, h of the last widget placed, as for a tooltip beside it.
function ui.last()
  local l = frame.last
  return l[1], l[2], l[3], l[4]
end

local function open_box(dir, o)
  o = o or {}
  local parent = top()
  local box = { dir = dir, gap = o.gap or 0, pad = o.pad or 0, align = o.align, used_w = 0, used_h = 0 }
  if parent and (o.x == nil or o.y == nil) then
    -- Its place in the parent; its size is known when it closes.
    box.x, box.y = o.x or parent.cx, o.y or parent.cy
  else
    box.x, box.y = o.x or 0, o.y or 0
  end
  box.w = type(o.w) == "number" and o.w or nil
  box.h = type(o.h) == "number" and o.h or nil
  if o.w == "fill" and parent and parent.w then
    box.w = parent.dir == "row" and (parent.x + parent.w - parent.pad - parent.cx) or (parent.w - 2 * parent.pad)
  end
  box.cx, box.cy = box.x + box.pad, box.y + box.pad
  box.placed = parent and (o.x == nil or o.y == nil)
  stack[#stack + 1] = box
  return box
end

local function close_box(box)
  stack[#stack] = nil
  local w = box.w or (box.used_w + 2 * box.pad)
  local h = box.h or (box.used_h + 2 * box.pad)
  if box.placed then
    local parent = top()
    if parent.dir == "row" then
      parent.cx = parent.cx + w + parent.gap
      parent.used_w = parent.cx - parent.gap - parent.x - parent.pad
      parent.used_h = math.max(parent.used_h, h)
    else
      parent.cy = parent.cy + h + parent.gap
      parent.used_h = parent.cy - parent.gap - parent.y - parent.pad
      parent.used_w = math.max(parent.used_w, w)
    end
  end
  return box.x, box.y, w, h
end

local function run_box(dir, o, fn)
  local box = open_box(dir, o)
  local ok, err = pcall(fn, box.x, box.y, box.w, box.h)
  while top() ~= box and #stack > 0 do stack[#stack] = nil end
  local x, y, w, h = close_box(box)
  if not ok then error(err, 0) end
  return x, y, w, h
end

---Widgets called in fn go side by side, left to right. opts: x, y (else placed in the parent),
---w, h (a number, or w = "fill"), gap, pad, align = "start" | "center" | "end" (up and down).
---Returns the row's x, y, w, h.
function ui.row(opts, fn)
  if type(opts) == "function" then opts, fn = nil, opts end
  return run_box("row", opts, fn)
end

---Widgets called in fn go one under the other. Same opts as row; align is left and right.
function ui.col(opts, fn)
  if type(opts) == "function" then opts, fn = nil, opts end
  return run_box("col", opts, fn)
end

---Empty space in a row or column.
function ui.space(w, h)
  place(nil, w or 0, h or 0)
end

-- ---- Widgets ----

local function text_w(str, style)
  return hud.text_width(str, style)
end

local function with(style, extra)
  local s = {}
  for k, v in pairs(style) do s[k] = v end
  for k, v in pairs(extra) do s[k] = v end
  return s
end

---A line of text. opts: x, y, w, h, size, color = {r, g, b}, dim (the theme's dim color),
---align, valign. Returns x, y, w, h.
function ui.label(text, o)
  o = o or {}
  local style = with(ui.theme.label, {})
  if o.size then style.size = o.size end
  local c = o.color or (o.dim and ui.theme.dim)
  if c then style.r, style.g, style.b = c.r or c[1], c.g or c[2], c.b or c[3] end
  local size = style.size or 16
  local x, y, w, h = place(o, text_w(text, style), size + 4)
  style.w, style.h = w, h
  style.align = o.align
  style.valign = o.valign or "center"
  hud.text(x, y, text, style)
  show("text", o.id or text, text, x, y, w, h)
  return x, y, w, h
end

local function pulse()
  return (math.cos(rq.time_ms() / 2000 * 2 * math.pi) + 1) / 2
end

---A game button with text. opts: x, y, w (default 150 or the text's width), h (40), id,
---disabled. Returns true on the update it is clicked.
function ui.button(text, o)
  o = o or {}
  local th = ui.theme
  local id = id_of(o, "button")
  local tw = text_w(text, th.button_text)
  local x, y, w, h = place(o, math.max(150, tw + 30), 40)
  local over, clicked = interact(id, x, y, w, h, o.disabled)
  ui.draw("button", x, y, w, h, o.disabled and { tint = { 128, 128, 128 } } or nil)
  hud.text(x, y, text, with(th.button_text, { w = w, h = h, align = "center", valign = "center" }))
  if over and not o.disabled then
    local c = th.button_cover
    hud.rect(x, y, w, h, c.r, c.g, c.b, math.floor(c.a * (128 + 128 * pulse()) / 255))
  end
  if clicked then play("click") end
  show("button", o.id or text, text, x, y, w, h, { disabled = o.disabled })
  return clicked
end

---A checkbox with its text. opts: x, y, w (the hover bar's width), id, disabled.
---Returns the new value and whether it changed on this update.
function ui.checkbox(value, text, o)
  o = o or {}
  local th = ui.theme
  local id = id_of(o, "check")
  local style = th.label
  local tw = text and text ~= "" and text_w(text, style) or 0
  local x, y, w, h = place(o, 30 + (tw > 0 and 12 + tw + 8 or 0), 30)
  local over, clicked = interact(id, x, y, w, h, o.disabled)
  local opts = o.disabled and { tint = { 128, 128, 128 } } or nil
  ui.draw("check_frame", x + 1, y + 1, 28, 28, opts)
  if value then ui.draw("check_mark", x, y, 30, 30, opts) end
  if tw > 0 then
    hud.text(x + 42, y, text, with(style, { h = h, valign = "center" }))
  end
  if over and not o.disabled then ui.draw("hover_bar", x, y, w, h) end
  show("check", o.id or text, text, x, y, w, h, { selected = value and true or false,
    value = value and "on" or "off", disabled = o.disabled })
  if clicked then
    play("click")
    return not value, true
  end
  return value, false
end

---One of several checkboxes, as the 3D tab's quality. list holds the texts. opts: x, y, dir =
---"col" (default) or "row", gap (default 20), id, disabled. Returns index, changed.
function ui.choice(list, index, o)
  o = o or {}
  local changed = false
  local key = o.id ~= nil and tostring(o.id) or nil
  local fn = function()
    for i, text in ipairs(list) do
      local v, c = ui.checkbox(i == index, text, { id = key and (key .. "." .. i), disabled = o.disabled })
      if c and v and i ~= index then
        index, changed = i, true
      end
    end
  end
  local box = { x = o.x, y = o.y, gap = o.gap or 20 }
  if o.dir == "row" then ui.row(box, fn) else ui.col(box, fn) end
  return index, changed
end

local function snap(v, mn, mx, step)
  if step and step > 0 then v = mn + math.floor((v - mn) / step + 0.5) * step end
  if v < mn then v = mn elseif v > mx then v = mx end
  return v
end

---A slider as on the Sound tab. opts: x, y, w (the track, default 200), min (0), max (1),
---step, low, high (texts at the ends), id, disabled. Drag the knob, click the track, or turn
---the wheel over it. Returns the new value and whether it changed on this update.
function ui.slider(value, o)
  o = o or {}
  local th = ui.theme
  local id = id_of(o, "slider")
  local mn, mx = o.min or 0, o.max or 1
  local tw = type(o.w) == "number" and o.w or 200
  local dim = with(th.label, { r = th.dim.r, g = th.dim.g, b = th.dim.b })
  local lw = o.low and (text_w(o.low, dim) + 13) or 0
  local hw = o.high and (text_w(o.high, dim) + 8) or 0
  local x, y, w, h = place({ x = o.x, y = o.y, h = o.h }, lw + tw + hw, 30)
  local tx, ty = x + lw, y + math.floor((h - 15) / 2) + 3
  local over, _, held = interact(id, tx - 12, ty - 19, tw + 24, 35, o.disabled)
  local old = value
  if held then
    value = snap(mn + (mouse.x - tx) / tw * (mx - mn), mn, mx, o.step)
  elseif over and mouse.scroll ~= 0 then
    local step = o.step or (mx - mn) / 20
    value = snap(value + mouse.scroll * step, mn, mx, o.step)
    input.scroll(true)
  end
  local opts = o.disabled and { tint = { 128, 128, 128 } } or nil
  if o.low then hud.text(x, y, o.low, with(dim, { h = h, valign = "center" })) end
  ui.draw("track", tx - 1, ty - 1, tw + 2, 17, opts)
  if o.high then hud.text(tx + tw + 8, y, o.high, with(dim, { h = h, valign = "center" })) end
  local f = mx > mn and (value - mn) / (mx - mn) or 0
  ui.draw((held or over) and "knob_hot" or "knob", tx + math.floor(f * tw) - 12, ty - 19, 24, 35, opts)
  -- The box is the track from min to max, so x = box.x + (v - min) / (max - min) * box.w.
  show("slider", o.id or o.label or "slider", o.label, tx, ty - 19, tw, 35,
    { value = string.format("%g of %g..%g", value, mn, mx), disabled = o.disabled })
  return value, value ~= old
end

---Tabs side by side, as at the top of Config. opts: x, y, w (each tab, default 120), id.
---Returns the index and whether it changed on this update.
function ui.tabs(list, index, o)
  o = o or {}
  local th = ui.theme
  local tabw = o.w or 120
  local changed = false
  local key = o.id ~= nil and tostring(o.id) or nil
  ui.row({ x = o.x, y = o.y }, function()
    for i, text in ipairs(list) do
      local id = id_of({ id = key and (key .. "." .. i) }, "tab")
      local x, y, w, h = place(nil, tabw, 40)
      local current = i == index
      local over, clicked = interact(id, x, y, w, h, current)
      local look = current and "current" or (over and "hover" or "normal")
      ui.draw(th.tab[look], x, y, w, h)
      hud.text(x, y, text, with(th.tab_text[look], { w = w, h = h, align = "center", valign = "center" }))
      show("tab", key and (key .. "-" .. i) or text, text, x, y, w, h, { selected = current })
      if clicked then
        play("click")
        index, changed = i, true
      end
    end
  end)
  return index, changed
end

---A Config page ("page", the default) or the 3D tab's dark block ("block") behind fn, which
---runs in a column inside it. opts: x, y, w, h (needed), style, pad (default 12), gap, title.
---Returns x, y, w, h.
function ui.panel(o, fn)
  o = o or {}
  if type(o.w) ~= "number" or type(o.h) ~= "number" then error("uikit.panel: w and h are needed", 2) end
  local x, y, w, h = place(o, o.w, o.h)
  ui.draw(o.style or "page", x, y, w, h)
  local pad = o.pad or 12
  local top_y = y + pad
  if o.title then
    hud.text(x + pad, top_y, o.title, ui.theme.label)
    top_y = top_y + (ui.theme.label.size or 16) + 10
  end
  if fn then
    ui.col({ x = x + pad, y = top_y, w = w - 2 * pad, gap = o.gap or 0 }, fn)
  end
  return x, y, w, h
end

local function item_text(it)
  if type(it) == "table" then return it.text or "" end
  return tostring(it)
end

---A list that scrolls, on a dark block. items are texts or {text, disabled}. opts: x, y, w, h
---(needed), row_h (default 30), id (keeps the scroll), block = false to leave out the block.
---Returns the index and whether it changed on this update.
function ui.list(items, index, o)
  o = o or {}
  if type(o.w) ~= "number" or type(o.h) ~= "number" then error("uikit.list: w and h are needed", 2) end
  local th = ui.theme
  local id = id_of(o, "list")
  local st = ui.state(id)
  local x, y, w, h = place(o, o.w, o.h)
  local rh = o.row_h or 30
  local inner = 3
  if o.block ~= false then ui.draw("block", x, y, w, h) end
  local view = h - 2 * inner
  local total = #items * rh
  local max_top = math.max(0, total - view)
  st.top = math.min(math.max(st.top or 0, 0), max_top)
  if inside(x, y, w, h) then
    frame.over = true
    hud.block_game_input()
    if mouse.scroll ~= 0 then
      st.top = math.min(math.max(st.top - mouse.scroll * rh, 0), max_top)
      input.scroll(true)
    end
  end
  local changed = false
  local cw = w - 2 * inner - (max_top > 0 and 8 or 0)
  hud.clip(x + inner, y + inner, cw, view)
  for i, it in ipairs(items) do
    local ry = y + inner + (i - 1) * rh - st.top
    if ry + rh > y and ry < y + h then
      local disabled = type(it) == "table" and it.disabled
      local over, clicked = false, false
      if inside(x + inner, y + inner, cw, view) then
        over, clicked = interact(id .. "." .. i, x + inner, ry, cw, rh, disabled)
      end
      if i == index then ui.draw("hover_bar", x + inner, ry, cw, rh) end
      if over then ui.draw("hover_bar", x + inner, ry, cw, rh, { alpha = 128 }) end
      local style = with(th.list_text, { h = rh, valign = "center" })
      if i == index then style.r, style.g, style.b = th.list_current.r, th.list_current.g, th.list_current.b end
      if disabled then style.r, style.g, style.b = 128, 128, 128 end
      hud.text(x + inner + 10, ry, item_text(it), style)
      if ry >= y and ry + rh <= y + h then
        show("row", (o.id ~= nil and tostring(o.id) or "list") .. "-" .. item_text(it), item_text(it),
          x + inner, ry, cw, rh, { selected = i == index, disabled = disabled, value = tostring(i) })
      end
      if clicked and i ~= index then
        play("click")
        index, changed = i, true
      end
    end
  end
  hud.clip()
  if max_top > 0 then
    local bar_h = math.max(20, math.floor(view * view / total))
    local bar_y = y + inner + math.floor((view - bar_h) * st.top / max_top)
    local c = th.scrollbar
    hud.rect(x + w - inner - 6, bar_y, 4, bar_h, c.r, c.g, c.b, c.a)
  end
  return index, changed
end

---Lists side by side, where an item with items opens the next list: a menu with levels.
---tree is a list of {text, items}. path holds the index picked at each level. opts: x, y,
---w (each list, default 200), h (needed), gap (default 10), id. Returns the new path (a new
---table when it changed), whether it changed, and the item at the end of the path.
function ui.menu(tree, path, o)
  o = o or {}
  if type(o.h) ~= "number" then error("uikit.menu: h is needed", 2) end
  local key = o.id ~= nil and tostring(o.id) or tostring(tree)
  local lw = o.w or 200
  local level, list = 1, tree
  local out, changed, leaf = {}, false, nil
  ui.row({ x = o.x, y = o.y, gap = o.gap or 10 }, function()
    while list and #list > 0 do
      local pick = path and path[level]
      if changed then pick = nil end
      local new, c = ui.list(list, pick, { id = key .. "." .. level, w = lw, h = o.h })
      if c then changed = true end
      if not new then break end
      out[level] = new
      leaf = list[new]
      list = type(leaf) == "table" and leaf.items or nil
      level = level + 1
    end
  end)
  if not changed then
    -- The path as given, cut to what still exists.
    if path and #out ~= #path then changed = true end
  end
  return changed and out or path, changed, leaf
end

return ui
