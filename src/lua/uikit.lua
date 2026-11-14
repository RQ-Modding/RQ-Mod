-- rq.uikit (loader 1.7.0; 1.8.0 adds ui.layers, bold, and the widgets in rqmod-ctl ui; 2.6.0 the
-- hub's widgets, in uikit_hub.lua): widgets that look like the game's Config screen, drawn with rq.hud
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

-- blind: the game's choice menu shows, or did and a button that went down under it is still
-- down (2.7.0). The kit then sees no mouse, so a click on the menu does not reach a widget under it.
local frame = { auto = 0, hot = nil, was_hot = nil, over = false, last = { 0, 0, 0, 0 }, blind = false }
local mouse = { x = 0, y = 0, down = false, pressed = false, released = false, scroll = 0,
  right = false, right_released = false }
local active -- the widget the left button went down on, until it comes up
local state = {} -- kept across updates, by widget id
local stack = {} -- the open rows and columns

-- What was drawn on this update, for rqmod-ctl ui (section "kit"): sent on the next update,
-- when the list is whole.
local shown, shown_ids, published = {}, {}, 0

-- The keys' cursor (2.12.0), as the game's T_InputKeyboard: one selection for the mouse and the
-- keys. Every interact box is a stop; the arrows move between the stops of the last whole update.
-- src "mouse": the stop under the mouse (it draws as hovered); "keys": the one an arrow reached.
local KEY_FIRST, KEY_NEXT = 250, 50 -- the game's repeat: T_InputKeyboard::arrow*
local focus_update
local focus = {
  id = nil, src = "mouse",
  stops = {}, last = {},     -- {id, oid, x, y, w, h, nav, group}, in call order
  groups = {}, lgroups = {}, -- gid -> {ids = {...}, wrap, home, box}
  gorder = {}, lgorder = {}, -- the gids in call order
  on = false, was_on = false, -- rq.uikit.keys() on this update / the last
  edge = nil, ledge = nil,    -- keys{edge = fn} on this update / the last
  enter = false, page = 0,   -- Enter, and Page Up / Page Down (-1 / 1), on this update
  rep = {}, mx = nil, my = nil, moved = false, hit = false,
  hold = false,              -- a text field typed on the last update: the arrows are its caret's (2.29.0)
}

-- A widget's tip (2.13.0): the widget hot on this update with a tip, since when, and how it
-- came (the mouse, or the keys' cursor). hit: a tip widget was hot on this update.
local TIP_WAIT = 500 -- the game's wait before its comment window (説明_表示(..., 500, ...))
local tipst = { id = nil, t0 = 0, shown = false, src = "mouse", hit = false }
local tip_show -- set below, by the comment window

local function slug(str)
  str = tostring(str or ""):lower():gsub("[^%w]+", "-"):gsub("^%-+", ""):gsub("%-+$", "")
  return str:sub(1, 28)
end

-- kind-key, key being opts.id or the text; the same id twice gets -2, -3.
local function show(kind, key, label, x, y, w, h, extra)
  -- Under the game's choice menu the widgets take no input, so rqmod-ctl does not list them.
  if frame.blind then return end
  local id = kind .. "-" .. (slug(key) ~= "" and slug(key) or tostring(#shown + 1))
  local n = shown_ids[id]
  shown_ids[id] = (n or 1) + 1
  if n then id = id .. "-" .. n end
  local row = { id = id, type = kind, label = label and tostring(label) or id, x = x, y = y, w = w, h = h }
  if extra then for k, v in pairs(extra) do row[k] = v end end
  -- The keys' cursor: the shown box whose middle is in the focused stop (2.12.0).
  local f = focus.fstop
  if f and focus.was_on then
    local cx, cy = x + w / 2, y + h / 2
    if cx >= f.x and cy >= f.y and cx < f.x + f.w and cy < f.y + f.h then
      row.focused = true
      focus.fstop = nil
    end
  end
  shown[#shown + 1] = row
end

local function center(b) return b.x + b.w / 2, b.y + b.h / 2 end

local function stop_of(list, id)
  if id == nil then return nil end
  for i, st in ipairs(list) do
    if st.id == id or st.oid == id then return st, i end
  end
end

local function short_id(id) return type(id) == "string" and id:match("^[^:#]+:(.+)$") or nil end

-- A row of a list's cursor group, by the kit's id or the mod's (without the kind).
local function group_of(id)
  if id == nil then return nil end
  for gid, g in pairs(focus.lgroups) do
    for i, gi in ipairs(g.ids) do
      if gi == id or short_id(gi) == id then return g, i, gid, gi end
    end
  end
end

local function set_focus(id)
  if id ~= nil then focus.id, focus.src = id, "keys" end
end

-- The nearest stop past cur in the arrow's direction (its center past cur's edge that way): the
-- gap along the arrow plus twice the offset across it (0 when the boxes overlap across). Ties by
-- call order. wrap: with none, the stop the other way that lines up best, the farthest of those.
local function nearest(cur, dir, skip, wrap)
  local cx, cy = center(cur)
  local vert = dir == "up" or dir == "down"
  local sign = (dir == "down" or dir == "right") and 1 or -1
  local best, bs
  for _, st in ipairs(focus.last) do
    if st ~= cur and not (skip and st.group == skip) then
      local sx, sy = center(st)
      local past
      if vert then past = sign > 0 and sy > cur.y + cur.h or sign < 0 and sy < cur.y
      else past = sign > 0 and sx > cur.x + cur.w or sign < 0 and sx < cur.x end
      if past then
        local gap, across
        if vert then
          gap = sign > 0 and st.y - (cur.y + cur.h) or cur.y - (st.y + st.h)
          across = math.max(0, st.x - (cur.x + cur.w), cur.x - (st.x + st.w))
        else
          gap = sign > 0 and st.x - (cur.x + cur.w) or cur.x - (st.x + st.w)
          across = math.max(0, st.y - (cur.y + cur.h), cur.y - (st.y + st.h))
        end
        local score = math.max(0, gap) + 2 * across
        if not bs or score < bs then best, bs = st, score end
      end
    end
  end
  if best or not wrap then return best end
  -- Wrap: the far end the other way, lined up best.
  for _, st in ipairs(focus.last) do
    if st ~= cur and not (skip and st.group == skip) then
      local sx, sy = center(st)
      local back = vert and (cy - sy) * sign or (cx - sx) * sign
      local across = vert and math.max(0, st.x - (cur.x + cur.w), cur.x - (st.x + st.w))
        or math.max(0, st.y - (cur.y + cur.h), cur.y - (st.y + st.h))
      local score = across * 100000 - back -- the best lined up first, then the farthest
      if back >= 0 and (not bs or score < bs) then best, bs = st, score end
    end
  end
  return best
end

local function nav(dir)
  local stops = focus.last
  if #stops == 0 then return end
  local cur = stop_of(stops, focus.id)
  local vert = dir == "up" or dir == "down"
  local g, gi, gid = group_of(focus.id)
  -- No stop yet: a group's home (the list's current row), else the first or the last stop.
  if not cur and not g then
    for _, hgid in ipairs(focus.lgorder) do
      local hg = focus.lgroups[hgid]
      if hg.home and vert then return set_focus(hg.home) end
    end
    return set_focus(((dir == "down" or dir == "right") and stops[1] or stops[#stops]).id)
  end
  -- Overrides, as the game's arrow callbacks.
  local to
  if cur and type(cur.nav) == "table" then to = cur.nav[dir]
  elseif cur and type(cur.nav) == "function" then to = cur.nav(dir) end
  if to ~= nil then
    local st = stop_of(stops, to)
    if st then return set_focus(st.id) end
    local gfull = select(4, group_of(to))
    if gfull then return set_focus(gfull) end
  end
  -- Up and Down go through a list's rows, also those scrolled out of sight; at an end a list that
  -- wraps starts over, else the arrow goes on to the nearest stop outside, if any.
  if g and vert then
    local j = gi + (dir == "down" and 1 or -1)
    if g.ids[j] then return set_focus(g.ids[j]) end
    if g.wrap then return set_focus(g.ids[j < 1 and #g.ids or 1]) end
    if not cur then cur = g.box end
    if not cur then return end
    local st = nearest(cur, dir, gid, false)
    if st then set_focus(st.id) elseif focus.ledge then focus.ledge(dir) end
    return
  end
  if not cur then cur = g and g.box end
  if not cur then return end
  -- With an edge callback the cursor does not wrap: the arrow leaves the kit's widgets.
  local st = nearest(cur, dir, nil, focus.ledge == nil)
  if st then set_focus(st.id) elseif focus.ledge then focus.ledge(dir) end
end

local function key_rep(name, first, nxt)
  local down = input.key(input.code(name))
  local at = focus.rep[name]
  if not down then
    focus.rep[name] = nil
    return false
  end
  local now = rq.time_ms()
  if not at then
    -- A key already down when the cursor starts does not count.
    focus.rep[name] = first and math.huge or now + KEY_FIRST
    return not first
  end
  if now >= at then
    focus.rep[name] = now + (nxt or KEY_NEXT)
    return true
  end
  return false
end

focus_update = function()
  local moved = focus.mx ~= nil and (mouse.x ~= focus.mx or mouse.y ~= focus.my)
  focus.mx, focus.my = mouse.x, mouse.y
  -- The mouse moved off every stop on the last update: the selection goes, as the game's.
  if focus.moved and not focus.hit and focus.src == "mouse" then focus.id = nil end
  focus.moved, focus.hit = moved or mouse.pressed, false
  focus.last, focus.stops = focus.stops, {}
  focus.lgroups, focus.groups = focus.groups, {}
  focus.lgorder, focus.gorder = focus.gorder, {}
  local first = not focus.was_on
  focus.was_on, focus.on = focus.on, false
  focus.ledge, focus.edge = focus.edge, nil
  focus.enter, focus.page, focus.fstop = false, 0, nil
  if not focus.was_on or frame.blind then
    focus.rep = {}
    if not focus.was_on and focus.src == "keys" then focus.id, focus.src = nil, "mouse" end
    return
  end
  if first then
    for _, k in ipairs({ "up", "down", "left", "right", "enter", "pageup", "pagedown" }) do key_rep(k, true) end
    return
  end
  local hold = focus.hold
  focus.hold = false
  for _, dir in ipairs({ "up", "down", "left", "right" }) do
    if key_rep(dir) and not hold then nav(dir) end
  end
  if key_rep("enter") and focus.rep.enter ~= math.huge then
    focus.rep.enter = math.huge -- Enter does not repeat
    focus.enter = focus.id ~= nil
  end
  -- Config's sliders: after 250 ms on every update (T_ConfigSlider::scroll).
  if key_rep("pageup", false, 0) then focus.page = -1 end
  if key_rep("pagedown", false, 0) then focus.page = 1 end
end

rq.on_frame(function()
  if #shown > 0 or published > 0 then
    if rq.agent and rq.agent.widgets then pcall(rq.agent.widgets, shown) end
    published = #shown
  end
  shown, shown_ids = {}, {}
  frame.n = (frame.n or 0) + 1 -- the update's number (2.29.0)
  frame.was_hot, frame.hot, frame.auto, frame.over = frame.hot, nil, 0, false
  local was_right = mouse.right
  mouse.x, mouse.y, mouse.down, mouse.right, mouse.pressed, mouse.released = input.mouse()
  if rq.ui.choosing() then
    frame.blind = true
  elseif frame.blind and not mouse.down and not mouse.right and not mouse.released then
    frame.blind = false
  end
  if frame.blind then
    mouse.x, mouse.y, mouse.down, mouse.right, mouse.pressed, mouse.released = -100000, -100000,
      false, false, false, false
    was_right = false
  end
  mouse.right_released = was_right and not mouse.right
  mouse.scroll = frame.blind and 0 or input.scroll()
  if not mouse.down and not mouse.released then active = nil end
  if not tipst.hit then tipst.id = nil end
  tipst.hit = false
  focus_update()
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
-- o (2.12.0): nav (false: no stop; a table of overrides), oid (the mod's id for nav), group (the
-- list it is a row of), stop = false (takes the mouse only), view = {x, y, w, h} (the mouse counts
-- only inside it).
-- Returns hovered (by the mouse, or the keys' cursor), clicked (released over the box it went down
-- on, or Enter on the cursor), held, and hovered by the mouse alone.
local function interact(id, x, y, w, h, disabled, o)
  local over = inside(x, y, w, h)
  if over and o and o.view then over = inside(o.view[1], o.view[2], o.view[3], o.view[4]) end
  if over then
    frame.over = true
    hud.block_game_input()
  end
  if disabled then
    if over and o and o.tip and not frame.blind then tip_show(id, o.tip, o.tip_wait, x, y, w, h, "mouse") end
    return false, false, false, false
  end
  local stop = not frame.blind and not (o and (o.nav == false or o.stop == false))
  if stop then
    local st = { id = id, oid = o and o.oid or short_id(id), x = x, y = y, w = w, h = h, nav = o and o.nav,
      group = o and o.group }
    focus.stops[#focus.stops + 1] = st
    if focus.id == id then focus.fstop = st end
    if over and focus.moved then
      focus.id, focus.src, focus.hit = id, "mouse", true
    end
  end
  local hot = over
  local by_keys = focus.was_on and focus.src == "keys"
  if by_keys then hot = focus.id == id end
  if hot then
    frame.hot = id
    if frame.was_hot ~= id and active == nil then play("hover") end
    if o and o.tip and not frame.blind then tip_show(id, o.tip, o.tip_wait, x, y, w, h, by_keys and "keys" or "mouse") end
  end
  if over and mouse.pressed then active = id end
  local clicked = over and mouse.released and active == id
  if focus.enter and focus.id == id and stop then
    focus.enter = false
    clicked = true
  end
  return hot, clicked, active == id and mouse.down, over
end

---The kit takes the arrow keys, Enter, Page Up and Page Down on this update (2.12.0): they move the
---keys' cursor between widgets and are hidden from the game. ui.page calls it. opts.edge: a
---function(dir) called when an arrow finds no widget that way; the cursor then does not wrap.
function ui.keys(o)
  if o ~= nil and type(o) ~= "table" then error("uikit.keys: opts is a table", 2) end
  if o and o.edge ~= nil and type(o.edge) ~= "function" then error("uikit.keys: edge is a function", 2) end
  if frame.blind then return end
  focus.on = true
  if o and o.edge then focus.edge = o.edge end
  hud.block_keys({ "up", "down", "left", "right", "enter", "pageup", "pagedown" })
end

---The id of the widget under the keys' cursor (its opts.id, else the kit's own id), or nil.
function ui.focused()
  local st = stop_of(focus.last, focus.id)
  if st then return st.oid or st.id end
  if focus.id and group_of(focus.id) then return short_id(focus.id) or focus.id end
  return nil
end

---Puts the keys' cursor on a widget by its opts.id (or the kit's id), as an arrow would.
function ui.focus(id)
  if id == nil then
    focus.id, focus.src = nil, "mouse"
    return
  end
  local st = stop_of(focus.last, id)
  local gi = not st and select(4, group_of(id))
  set_focus(st and st.id or gi or id)
  ui.keys()
end

-- For lists (2.12.0): the rows of a list, all of them in order, so the arrows go through rows
-- scrolled out of sight. wrap: Up and Down go round. home: the row the cursor starts on. box: the
-- list's place, to leave it from a row that is out of sight.
local function focus_group(gid, ids, opts)
  opts = opts or {}
  if not focus.groups[gid] then focus.gorder[#focus.gorder + 1] = gid end
  focus.groups[gid] = { ids = ids, wrap = opts.wrap, home = opts.home, box = opts.box }
end

---Runs fn now and draws what it draws on the top layer, over every widget (rq.hud.top, 2.11.0).
function ui.top(fn)
  if type(fn) ~= "function" then error("uikit.top: expected a function", 2) end
  return hud.top(fn)
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
  local over, clicked = interact(id, x, y, w, h, o.disabled, { nav = o.nav, tip = o.tip, tip_wait = o.tip_wait })
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
  local over, clicked = interact(id, x, y, w, h, o.disabled, { nav = o.nav, tip = o.tip, tip_wait = o.tip_wait })
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
      local v, c = ui.checkbox(i == index, text, { id = key and (key .. "." .. i), disabled = o.disabled,
        tip = o.tips and o.tips[i], tip_wait = o.tip_wait })
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
---step, low, high (texts at the ends), band ({from, to}: that part of the track drawn lighter, as
---a recommended range, 2.44.0), id, disabled. Drag the knob, click the track, or turn the wheel
---over it. Returns the new value, whether it changed on this update, and whether the knob is held
---(2.44.0: a drag goes on, so the value is not final yet).
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
  local over, _, held, mouse_over = interact(id, tx - 12, ty - 19, tw + 24, 35, o.disabled,
    { nav = o.nav, tip = o.tip, tip_wait = o.tip_wait })
  local old = value
  if held then
    value = snap(mn + (mouse.x - tx) / tw * (mx - mn), mn, mx, o.step)
  elseif focus.page ~= 0 and focus.id == id and not o.disabled then
    -- Page Up and Page Down on the keys' cursor, 5% a step as Config's (2.12.0).
    value = snap(value + focus.page * (o.step or (mx - mn) / 20), mn, mx, o.step)
  elseif mouse_over and mouse.scroll ~= 0 then
    local step = o.step or (mx - mn) / 20
    value = snap(value + mouse.scroll * step, mn, mx, o.step)
    input.scroll(true)
  end
  local opts = o.disabled and { tint = { 128, 128, 128 } } or nil
  if o.low then hud.text(x, y, o.low, with(dim, { h = h, valign = "center" })) end
  ui.draw("track", tx - 1, ty - 1, tw + 2, 17, opts)
  if type(o.band) == "table" and mx > mn then
    -- The recommended part: a light veil over that part of the track (2.44.0).
    local b1, b2 = tonumber(o.band[1]) or mn, tonumber(o.band[2]) or mx
    if b2 < b1 then b1, b2 = b2, b1 end
    b1, b2 = math.max(mn, math.min(mx, b1)), math.max(mn, math.min(mx, b2))
    local x1 = tx + math.floor((b1 - mn) / (mx - mn) * tw)
    local x2 = tx + math.floor((b2 - mn) / (mx - mn) * tw)
    if x2 > x1 then hud.rect(x1, ty + 1, x2 - x1, 13, 190, 255, 190, o.disabled and 30 or 70) end
  end
  if o.high then hud.text(tx + tw + 8, y, o.high, with(dim, { h = h, valign = "center" })) end
  local f = mx > mn and (value - mn) / (mx - mn) or 0
  ui.draw((held or over) and "knob_hot" or "knob", tx + math.floor(f * tw) - 12, ty - 19, 24, 35, opts)
  -- The box is the track from min to max, so x = box.x + (v - min) / (max - min) * box.w.
  show("slider", o.id or o.label or "slider", o.label, tx, ty - 19, tw, 35,
    { value = string.format("%g of %g..%g", value, mn, mx), disabled = o.disabled })
  return value, value ~= old, held and not o.disabled and true or false
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
      local over, clicked = interact(id, x, y, w, h, current, { tip = o.tips and o.tips[i], tip_wait = o.tip_wait })
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
  -- The hub theme has the game's scroll bar (2.6.0); it needs room for its two arrows.
  local game_bar = max_top > 0 and th.scroll and ui.scrollbar and view >= 100
  local cw = w - 2 * inner - (max_top > 0 and (game_bar and 22 or 8) or 0)
  -- The keys' cursor goes through every row (2.12.0), and the list scrolls to keep it in sight.
  local ids = {}
  for i, it in ipairs(items) do
    if not (type(it) == "table" and it.disabled) then ids[#ids + 1] = id .. "." .. i end
  end
  focus_group(id, ids, { home = index and (id .. "." .. index) or nil, box = { x = x, y = y, w = w, h = h } })
  local fk = focus.src == "keys" and type(focus.id) == "string" and focus.id:sub(1, #id + 1) == id .. "."
    and tonumber(focus.id:sub(#id + 2))
  if fk then
    local fy = (fk - 1) * rh
    if fy < st.top then st.top = fy elseif fy + rh > st.top + view then st.top = fy + rh - view end
    st.top = math.min(math.max(st.top, 0), max_top)
  end
  local vbox = { x + inner, y + inner, cw, view }
  hud.clip_push(x + inner, y + inner, cw, view)
  for i, it in ipairs(items) do
    local ry = y + inner + (i - 1) * rh - st.top
    if ry + rh > y and ry < y + h then
      local disabled = type(it) == "table" and it.disabled
      local whole = ry >= y + inner and ry + rh <= y + inner + view
      local over, clicked = interact(id .. "." .. i, x + inner, ry, cw, rh, disabled,
        { group = id, view = vbox, stop = whole, tip = type(it) == "table" and it.tip or nil, tip_wait = o.tip_wait })
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
  hud.clip_pop()
  if game_bar then
    st.top = ui.scrollbar(st.top, total, view, { x = x + w - inner - 20, y = y + inner, h = view,
      id = id .. ".bar", step = rh })
  elseif max_top > 0 then
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

-- ---- The game's comment window (2.13.0): T_Comment, 説明_表示 ----

-- Three borders of 1 pixel, outside in, a paper CG inside them, a header and text in the
-- game's font. "red" is the game's type 1 (_説明_表示 with 1, the red skill comments).
local COMMENT = {
  normal = { border = { { 76, 232, 255 }, { 0, 0, 0 }, { 255, 255, 255 } }, cg = 10050,
    head = { 0, 0, 0 }, text = { 64, 64, 64 } },
  red = { border = { { 255, 120, 200 }, { 0, 0, 0 }, { 170, 0, 0 } }, cg = 10049,
    head = { 255, 215, 215 }, text = { 240, 160, 160 } },
}
local SUB = { 255, 0, 0 }
local WRAP, MOST, LINE, HEAD = 520, 20, 18, 22 -- WordWrapText 520, 20 lines, 20 - 2, header 22
local function cstyle(size, c)
  -- measure: widths as the game's MeasureText reckons them (rq.hud.text_width, 2.13.0).
  return { font = "game", face = 258, size = size, spacing = 0.95, space = -6, r = c[1], g = c[2], b = c[3],
    measure = true }
end

local function letters(str)
  local out = {}
  for _, c in utf8.codes(str, true) do out[#out + 1] = utf8.char(c) end
  return out
end

-- MeasureTextUntilPixel: how many letters fit in WRAP pixels.
local function fit_count(chs, style)
  if text_w(table.concat(chs), style) <= WRAP then return #chs end
  local lo, hi = 0, #chs
  while hi - lo > 1 do
    local mid = (lo + hi) // 2
    if text_w(table.concat(chs, "", 1, mid), style) <= WRAP then lo = mid else hi = mid end
  end
  return lo
end

-- T_Comment wraps at 520 pixels at spacing 1.0 (WordWrapText(text, ..., 520, 1.0, 1.0)), and
-- draws at 0.95. A line breaks after the last word that fits; a word longer than the line breaks
-- where it stops fitting, and a letter that is not ASCII (Japanese) can end a line. \n breaks a
-- line, and @ and $ with game, as the game's texts.
local function wrap(text, style, game)
  local out = {}
  if text == nil or text == "" then return out end
  text = tostring(text)
  if game then text = text:gsub("[@$]", "\n") end
  local ms = with(style, { spacing = 1.0 })
  for para in (text .. "\n"):gmatch("(.-)\n") do
    local chs = letters(para)
    while true do
      local n = #chs
      local fit = fit_count(chs, ms)
      if fit >= n then
        out[#out + 1] = table.concat(chs)
        break
      end
      local i = math.max(fit, 1) -- 1-based: the last letter that fits
      local function ascii_word(ch) return #ch == 1 and ch:byte() > 32 and ch:byte() < 128 end
      while i > 1 and ascii_word(chs[i]) do i = i - 1 end
      while i > 1 and #chs[i] == 1 and chs[i]:byte() <= 32 do i = i - 1 end
      if i == 1 then i = math.max(fit, 1) end
      out[#out + 1] = table.concat(chs, "", 1, i)
      while i < n and #chs[i + 1] == 1 and chs[i + 1]:byte() <= 32 do i = i + 1 end
      if i >= n then break end
      chs = { table.unpack(chs, i + 1) }
    end
  end
  return out
end

local function widest(lines, style)
  local w = 0
  for _, l in ipairs(lines) do w = math.max(w, text_w(l, style)) end
  return w
end

-- The window's layout, as T_Comment::create lays it out.
local function comment_layout(text, o)
  if text ~= nil and type(text) ~= "string" then error("uikit.comment: text is a string", 3) end
  if o.header ~= nil and type(o.header) ~= "string" then error("uikit.comment: header is a string", 3) end
  if o.sub ~= nil and type(o.sub) ~= "string" then error("uikit.comment: sub is a string", 3) end
  local look = COMMENT[o.style or "normal"]
  if not look then error("uikit.comment: style is \"normal\" or \"red\"", 3) end
  local ts, ss, hs = cstyle(20, look.text), cstyle(20, SUB), cstyle(HEAD, look.head)
  local lines, subs = wrap(text, ts, o.game), wrap(o.sub, ss, o.game)
  local header = o.header or ""
  if #lines == 0 and #subs == 0 and header == "" then return nil end
  if #lines + #subs > MOST then
    while #lines > MOST do table.remove(lines) end
    while #lines + #subs > MOST do table.remove(subs) end
  end
  local w1 = header ~= "" and text_w(header, hs) or 0
  local w2 = math.max(widest(lines, ts), widest(subs, ss)) + 10
  local W, H, tx, ty = math.max(w1, w2) + 8, 8, 4, 4
  local h2, h3 = 0, 0
  if #lines > 0 then h2 = LINE * #lines; H = H + h2 end
  if #subs > 0 then
    h3 = LINE * #subs
    if h2 > 0 then h2, h3 = h2 + 3, h3 + 3 end
    H = H + h3
  end
  if w1 > 0 then
    H, ty = H + HEAD - 2, ty + HEAD - 2
    if w1 > w2 then tx = tx + math.floor((w1 - w2) / 2) end
  end
  return { look = look, W = W, H = H, tx = tx, ty = ty, h2 = h2, lines = lines, subs = subs,
    header = header, ts = ts, ss = ss, hs = hs }
end

-- Where it goes, as T_Comment::setPos: at the mouse (11 right, 20 down) unless x and y are
-- given; left of the mouse (or ending at 1024) past the right edge, ending at 736 past the bottom.
local function comment_place(L, x, y)
  local W, H = L.W + 5, L.H + 5 -- with the shadow
  local mx
  if x == nil or y == nil then
    mx = mouse.x
    x, y = mouse.x + 11, mouse.y + 20
  else
    mx = 1024
  end
  if x + W > 1024 then x = mx - W end
  if y + H > 736 then y = 736 - H end
  return math.floor(x), math.floor(y)
end

local function comment_draw(L, x, y)
  local W, H, look = L.W, L.H, L.look
  hud.top(function()
    -- The shadow: black at half, 5 pixels right and below.
    hud.rect(x + W, y + 5, 5, H, 0, 0, 0, 128)
    hud.rect(x + 5, y + H, W - 5, 5, 0, 0, 0, 128)
    for i, c in ipairs(look.border) do
      hud.rect(x + i - 1, y + i - 1, W - 2 * (i - 1), H - 2 * (i - 1), c[1], c[2], c[3], 255)
    end
    local n = #look.border
    hud.cg(x + n, y + n, look.cg, { src = { 0, 0, W - 2 * n, H - 2 * n } })
    if L.header ~= "" then hud.text(x + 4, y + 4, L.header, L.hs) end
    for i, l in ipairs(L.lines) do hud.text(x + L.tx, y + L.ty + (i - 1) * LINE, l, L.ts) end
    for i, l in ipairs(L.subs) do hud.text(x + L.tx, y + L.ty + L.h2 + (i - 1) * LINE, l, L.ss) end
  end)
end

---The game's comment window (2.13.0), drawn now on the top layer. opts: header, sub (red, under
---the text), style ("normal" or "red"), x and y (default at the mouse), game (@ breaks a line, as
---in the game's texts), id. Returns x, y, w, h, or nil when there is nothing to show.
function ui.comment(text, o)
  o = o or {}
  if type(o) ~= "table" then error("uikit.comment: opts is a table", 2) end
  local L = comment_layout(text, o)
  if not L then return nil end
  local x, y = comment_place(L, o.x, o.y)
  comment_draw(L, x, y)
  show("comment", o.id or "comment", L.header ~= "" and L.header or (L.lines[1] or L.subs[1]), x, y, L.W, L.H,
    { value = table.concat(L.lines, "\n") })
  return x, y, L.W, L.H
end

---The size the comment window would have (2.13.0): w, h, without the shadow; nil for nothing.
function ui.comment_size(text, o)
  o = o or {}
  if type(o) ~= "table" then error("uikit.comment_size: opts is a table", 2) end
  local L = comment_layout(text, o)
  if not L then return nil end
  return L.W, L.H
end

-- A widget's tip: after the wait (500 ms) on the same widget, the comment shows with the game's
-- sound 5001 (効果音_説明): at the mouse, or at the widget's right edge for the keys' cursor (as
-- the battle's commands: left of it when it does not fit). tip: a text or {text, header, sub,
-- style, game, x, y}.
tip_show = function(id, tip, wait, x, y, w, h, src)
  tipst.hit = true
  local now = rq.time_ms()
  if tipst.id ~= id then
    tipst.id, tipst.t0, tipst.shown, tipst.src = id, now, false, src
  end
  -- A tip of the kit's own (the item card, 2.21.0): _draw(x, y, w, h, src) draws it; from the keys
  -- it may wait less (_keys_wait), as the status page shows its item card at once.
  local drawn = type(tip) == "table" and tip._draw
  local wt = wait or TIP_WAIT
  if drawn and wait == nil and tipst.src == "keys" and tip._keys_wait then wt = tip._keys_wait end
  if now - tipst.t0 < wt then return end
  if drawn then
    if not tipst.shown then
      tipst.shown = true
      pcall(rq.sound.play, 5001)
    end
    drawn(x, y, w, h, tipst.src)
    return
  end
  local o = type(tip) == "table" and tip or { text = tip }
  local L = comment_layout(o.text, o)
  if not L then return end
  if not tipst.shown then
    tipst.shown = true
    pcall(rq.sound.play, 5001)
  end
  local cx, cy = o.x, o.y
  if cx == nil and tipst.src == "keys" then
    cx, cy = x + w, y
    if cx + L.W + 5 > 1024 then cx = x - L.W - 5 end
  end
  cx, cy = comment_place(L, cx, cy)
  comment_draw(L, cx, cy)
  show("comment", "tip", L.header ~= "" and L.header or (L.lines[1] or L.subs[1]), cx, cy, L.W, L.H,
    { value = table.concat(L.lines, "\n") })
end

-- ---- Bars (2.14.0): T_Bar ----

-- The game's bar CGs. dir "up": upright, filling from the bottom (T_Bar type 2).
local BARS = {
  hp = { cg = 10125 },       -- HP on the status card and the character card
  mini = { cg = 10098 },     -- HP on the mini cards
  monster = { cg = 10142 },  -- a monster's HP
  exp = { cg = 10302 },      -- EXP on the battle results
  charisma = { cg = 10013 }, -- the hub's system info: charisma's progress
  quest = { cg = 11303 },    -- a quest on the map
  magnum = { cg = 11316, dir = "up" }, -- the Magnum gauge
}
local bars = {} -- id -> {shown, from, to, t0, ms}

local function num(v, what)
  if type(v) ~= "number" or v ~= v then error("uikit.bar: " .. what .. " is a number", 3) end
  return v
end

---A gauge, as the game's T_Bar: a CG (or a color) cut to value / max of its width, or of its height
---from the bottom with dir "up"; a value above 0 shows at least 1 pixel. opts: style (a name in
---BARS), cg, dir ("right" or "up"), color ({r, g, b, a}, with w and h, default 190 by 4), anim (ms:
---the value shown moves to a new value in a straight line over that time, as T_Bar::anime), id,
---x, y, tip, tip_wait. Returns the value shown.
function ui.bar(value, max, o)
  o = o or {}
  if type(o) ~= "table" then error("uikit.bar: opts is a table", 2) end
  value, max = num(value, "value"), num(max, "max")
  local look = o.style ~= nil and BARS[o.style]
  if o.style ~= nil and not look then
    local names = {}
    for k in pairs(BARS) do names[#names + 1] = k end
    table.sort(names)
    error("uikit.bar: style is one of " .. table.concat(names, ", "), 2)
  end
  local cg = o.cg or (look and look.cg)
  local dir = o.dir or (look and look.dir) or "right"
  if dir ~= "right" and dir ~= "up" then error("uikit.bar: dir is \"right\" or \"up\"", 2) end
  if o.anim ~= nil and (math.type(o.anim) ~= "integer" or o.anim < 0) then
    error("uikit.bar: anim is a whole number of ms, 0 or more", 2)
  end
  local W, H
  local color = o.color
  if cg then
    if math.type(cg) ~= "integer" then error("uikit.bar: cg is a CG number", 2) end
    W, H = hud.cg_size(cg)
    if not W then error("uikit.bar: no CG " .. cg, 2) end
  else
    if color == nil then error("uikit.bar: give a style, a cg or a color", 2) end
    if type(color) ~= "table" then error("uikit.bar: color is {r, g, b, a}", 2) end
    W, H = 190, 4
  end
  local x, y, w, h = place(o, W, H)
  if not cg then W, H = w, h end
  -- The value shown: the game takes no more than max and moves from the old one over anim ms.
  if max < 1 then return 0 end
  value = math.max(0, math.min(value, max))
  local id = id_of(o, "bar")
  local st = bars[id]
  local now = rq.time_ms()
  if not st or not o.anim or o.anim == 0 then
    st = { shown = value, to = value }
    bars[id] = st
  elseif st.to ~= value then
    st.from, st.to, st.t0, st.ms = st.shown, value, now, o.anim
  end
  if st.t0 then
    local t = now - st.t0
    if t >= st.ms then
      st.shown, st.t0 = st.to, nil
    else
      st.shown = st.from + (st.to - st.from) * t / st.ms
    end
  end
  local v = st.shown
  local function cut(full)
    if v <= 0 then return 0 end
    return math.max(1, math.floor(full * (v / max)))
  end
  if dir == "up" then
    local ch = cut(H)
    if ch > 0 then
      if cg then
        hud.cg(x, y + H - ch, cg, { src = { 0, H - ch, W, ch } })
      else
        hud.rect(x, y + H - ch, W, ch, color[1] or color.r or 0, color[2] or color.g or 0,
          color[3] or color.b or 0, color[4] or color.a or 255)
      end
    end
  else
    local cw = cut(W)
    if cw > 0 then
      if cg then
        hud.cg(x, y, cg, { src = { 0, 0, cw, H } })
      else
        hud.rect(x, y, cw, H, color[1] or color.r or 0, color[2] or color.g or 0,
          color[3] or color.b or 0, color[4] or color.a or 255)
      end
    end
  end
  -- It takes no click and is no stop for the keys; a tip shows under the mouse.
  if o.tip and not frame.blind and inside(x, y, W, H) then tip_show(id, o.tip, o.tip_wait, x, y, W, H, "mouse") end
  show("bar", o.id or "bar", o.id ~= nil and tostring(o.id) or "bar", x, y, W, H,
    { value = tostring(math.floor(v)) })
  return v
end

-- ---- A number field (2.22.0) ----

-- The digits are read from the keys (0 to 9 and the keypad), not from the system's text input, so
-- rqmod-ctl type and the Lab's keys can type them. While a field types, these keys are hidden from
-- the game; Enter, Esc and Backspace stay hidden until let go, so the key that ended the typing
-- does not reach the game (Esc would close Config) or a page.
local DIGIT_KEYS = {}
for d = 0, 9 do
  DIGIT_KEYS[0x30 + d] = d -- the row above the letters
  DIGIT_KEYS[0x60 + d] = d -- the keypad
end
-- Minus: the engine's key table (VK_NR_KEYCODES, up to 0x91) has the keypad's (VK_SUBTRACT), which
-- is also what rqmod-ctl's "minus" presses.
local MINUS_KEYS = { 0x6D }
local K_ENTER, K_ESC, K_BACK = input.code("enter"), input.code("esc"), input.code("backspace")
local K_SHIFT = input.code("shift")
local TYPE_KEYS = { K_ENTER, K_ESC, K_BACK, MINUS_KEYS[1] }
for c in pairs(DIGIT_KEYS) do TYPE_KEYS[#TYPE_KEYS + 1] = c end
local HOLD_FIRST, HOLD_NEXT = 400, 80 -- held - and +, as the scroll bar's arrows
local typing, typing_alive = nil, false -- the field typed into, and whether it was drawn this update
local held_after = {} -- keys still down from the typing

rq.on_frame(function()
  -- A field no longer drawn stops typing, so its keys go back to the game.
  if typing and not typing_alive then typing = nil end
  typing_alive = false
  local keep = {}
  for c in pairs(held_after) do
    if input.key(c) then keep[#keep + 1] = c else held_after[c] = nil end
  end
  if #keep > 0 then hud.block_keys(keep) end
end)

-- Whether a key belongs to a field's typing: one is typing, or the key ended it and is still down.
-- ui.page reads Esc through it, so Esc while typing puts the number back and does not close the page.
local function typed(code)
  return typing ~= nil or held_after[code] == true
end

local function whole(v, what)
  if math.type(v) ~= "integer" then error("uikit.stepper: " .. what .. " is a whole number", 3) end
  return v
end

local function clamp(v, mn, mx)
  if v < mn then return mn elseif v > mx then return mx end
  return v
end

---A number field: the number in a framed box between - and + (2.22.0). opts: x, y, id, min (0),
---max (999999), step (1), big (step x 10, with Shift held), w (the box), label (text left of it),
---disabled, tip, tip_wait, nav; h (2.49.0: the height, 18 to 30, the buttons square) and ends
---(2.49.0: "max", "min" or true, MAX after + and MIN before -). A click on the number, or Enter
---under the keys' cursor, types into it. Returns the value and whether it changed on this update.
function ui.stepper(value, o)
  o = o or {}
  if type(o) ~= "table" then error("uikit.stepper: opts is a table", 2) end
  whole(value, "value")
  local mn, mx = whole(o.min or 0, "min"), whole(o.max or 999999, "max")
  local step = whole(o.step or 1, "step")
  local big = whole(o.big or step * 10, "big")
  if mx < mn then error("uikit.stepper: max is below min", 2) end
  if step < 1 or big < 1 then error("uikit.stepper: step and big are 1 or more", 2) end
  if o.w ~= nil and math.type(o.w) ~= "integer" then error("uikit.stepper: w is a whole number of pixels", 2) end
  if o.h ~= nil and (math.type(o.h) ~= "integer" or o.h < 18 or o.h > 30) then
    error("uikit.stepper: h is a whole number of pixels, 18 to 30", 2)
  end
  local ends = o.ends
  if ends ~= nil and ends ~= false and ends ~= true and ends ~= "max" and ends ~= "min" then
    error("uikit.stepper: ends is \"max\", \"min\" or true", 2)
  end
  local th = ui.theme
  local id = id_of(o, "stepper")
  local st = ui.state(id)
  local key = o.id ~= nil and tostring(o.id) or o.label or "stepper"
  local style = th.label
  local dim = with(style, { r = th.dim.r, g = th.dim.g, b = th.dim.b })
  local disabled = o.disabled and true or false
  local B = o.h or 30
  local lw = o.label and (text_w(o.label, style) + 12) or 0
  local maxlen = math.max(#tostring(mx), #tostring(mn))
  local bw = o.w or math.max(64 * B // 30, text_w(string.rep("8", maxlen), style) + 24)
  -- MIN and MAX (2.49.0): the theme's button with its text, at least square.
  local bt = with(th.button_text, { size = math.min(th.button_text.size or 18, B - 6) })
  local et = with(bt, { size = math.min(bt.size, B - 10) })
  local has_min, has_max = ends == true or ends == "min", ends == true or ends == "max"
  local nw = has_min and math.max(B, text_w("MIN", et) + 12) or 0
  local xw = has_max and math.max(B, text_w("MAX", et) + 12) or 0
  local x, y, w, h = place({ x = o.x, y = o.y },
    lw + (has_min and nw + 4 or 0) + B + 4 + bw + 4 + B + (has_max and 4 + xw or 0), B)
  local dx = x + lw + (has_min and nw + 4 or 0)
  local bx, ix = dx + B + 4, dx + B + 4 + bw + 4
  local old = value
  value = clamp(value, mn, mx)
  local tint = disabled and { tint = { 128, 128, 128 } } or nil
  if o.label then hud.text(x, y, o.label, with(disabled and dim or style, { h = h, valign = "center" })) end
  if typing == id then typing_alive = true end
  local amount = input.key(K_SHIFT) and big or step

  -- - and +: a press changes the number, held it repeats; Enter under the keys' cursor once.
  local function arrow(sub, ax, sign, text)
    local bid = id .. "." .. sub
    local off = disabled or typing == id
    local over, clicked, held = interact(bid, ax, y, B, B, off, { nav = o.nav })
    ui.draw("button", ax, y, B, B, off and { tint = { 128, 128, 128 } } or nil)
    hud.text(ax, y, text, with(bt, { w = B, h = B, align = "center", valign = "center" }))
    if over and not off then
      local c = th.button_cover
      hud.rect(ax, y, B, B, c.r, c.g, c.b, math.floor(c.a * (128 + 128 * pulse()) / 255))
    end
    local go, now = false, rq.time_ms()
    if held then
      local at = st[sub]
      if not at then
        go, st[sub] = true, now + HOLD_FIRST
      elseif now >= at then
        go, st[sub] = true, now + HOLD_NEXT
      end
    else
      st[sub] = nil
      if clicked and not mouse.released then go = true end
    end
    if go and not off then
      local nv = clamp(value + sign * amount, mn, mx)
      if nv ~= value then
        value = nv
        play("click")
      end
    end
    show("button", key .. "-" .. sub, text, ax, y, B, B, { disabled = off })
  end
  -- MIN and MAX: a click puts the number at that end (2.49.0).
  local function to_end(sub, ex, ew, text, target)
    local bid = id .. "." .. sub
    local off = disabled or typing == id
    local over, clicked = interact(bid, ex, y, ew, B, off, { nav = o.nav })
    ui.draw("button", ex, y, ew, B, off and { tint = { 128, 128, 128 } } or nil)
    hud.text(ex, y, text, with(et, { w = ew, h = B, align = "center", valign = "center" }))
    if over and not off then
      local c = th.button_cover
      hud.rect(ex, y, ew, B, c.r, c.g, c.b, math.floor(c.a * (128 + 128 * pulse()) / 255))
    end
    if clicked and not off and value ~= target then
      value = target
      play("click")
    end
    show("button", key .. "-" .. sub, text, ex, y, ew, B, { disabled = off })
  end
  if has_min then to_end("min", x + lw, nw, "MIN", mn) end
  arrow("dec", dx, -1, "-")

  -- The box: a click or Enter types into it.
  local over, clicked = interact(id, bx, y, bw, B, disabled, { nav = o.nav, tip = o.tip, tip_wait = o.tip_wait })
  if clicked and not disabled and typing ~= id then
    if typing then rq.log("uikit.stepper: another field was typing; it keeps its number") end
    typing, typing_alive = id, true
    st.text, st.kd = "", {}
    for _, c in ipairs(TYPE_KEYS) do st.kd[c] = input.key(c) end -- the Enter that started it
    play("click")
  end
  if typing == id then
    hud.block_keys(TYPE_KEYS)
    local function hit(c)
      local down = input.key(c)
      local was = st.kd[c]
      st.kd[c] = down
      return down and not was
    end
    for c, d in pairs(DIGIT_KEYS) do
      if hit(c) and #st.text < maxlen then st.text = st.text .. d end
    end
    for _, c in ipairs(MINUS_KEYS) do
      if hit(c) and mn < 0 and st.text == "" then st.text = "-" end
    end
    if hit(K_BACK) then st.text = st.text:sub(1, -2) end
    local done, keep = false, false
    if hit(K_ENTER) then done, keep = true, true end
    if hit(K_ESC) then done, keep = true, false end
    -- A click elsewhere, or the keys' cursor gone to another widget, keeps it.
    if mouse.pressed and not inside(bx, y, bw, B) then done, keep = true, true end
    if focus.src == "keys" and focus.id ~= nil and focus.id ~= id then done, keep = true, true end
    if disabled then done, keep = true, false end
    if done then
      typing = nil
      for _, c in ipairs({ K_ENTER, K_ESC, K_BACK }) do
        if input.key(c) then held_after[c] = true end
      end
      local n = math.tointeger(tonumber(st.text))
      if keep and n then
        value = clamp(n, mn, mx)
        play("click")
      end
      st.text = nil
    end
  elseif not disabled then
    -- The wheel over the field, and Page Up and Page Down on the keys' cursor (Page Up raises it).
    local delta = 0
    if inside(dx, y, ix + B - dx, B) and mouse.scroll ~= 0 then
      delta = mouse.scroll
      input.scroll(true)
    elseif focus.page ~= 0 and (focus.id == id or focus.id == id .. ".dec" or focus.id == id .. ".inc") then
      delta = -focus.page
    end
    if delta ~= 0 then value = clamp(value + delta * amount, mn, mx) end
  end

  ui.draw("block", bx, y, bw, B, tint)
  local ns = with(disabled and dim or style, {})
  if typing == id then
    local c = th.button_cover
    hud.rect(bx + 3, y + 3, bw - 6, B - 6, c.r, c.g, c.b, 50)
    local t = st.text
    local tw = t ~= "" and text_w(t, ns) or 0
    if t ~= "" then hud.text(bx + bw - 12 - tw, y, t, with(ns, { h = B, valign = "center" })) end
    if (rq.time_ms() // 500) % 2 == 0 then hud.rect(bx + bw - 10, y + 6, 2, B - 12, 255, 255, 255, 220) end
  else
    local t = tostring(value)
    hud.text(bx + bw - 10 - text_w(t, ns), y, t, with(ns, { h = B, valign = "center" }))
    if over and not disabled then ui.draw("hover_bar", bx, y, bw, B, { alpha = 128 }) end
  end
  -- For agents: the number, and selected while it types.
  show("stepper", key, o.label, bx, y, bw, B, { value = tostring(value), disabled = disabled,
    selected = typing == id })
  arrow("inc", ix, 1, "+")
  if has_max then to_end("max", ix + B + 4, xw, "MAX", mx) end
  return value, value ~= old
end

---The field typing on this update, by its opts.id (or the kit's id), or nil (2.22.0).
function ui.typing()
  if not typing then return nil end
  return short_id(typing) or typing
end

-- ---- A text field (2.29.0) ----

-- On the HUD's text input (rq.hud.text_begin): the typed text, the caret, the editing keys and
-- Ctrl+V are the engine's. The field draws them, and owns Enter, Esc, a click elsewhere and the
-- keys' cursor. One field types at a time, a number field included (`typing` above).
local text_owner -- the kit id of the text field the HUD's text input is on for

rq.on_frame(function()
  -- The field was not drawn on the last update, or another field types: the text input goes.
  if text_owner and typing ~= text_owner then
    hud.text_end()
    text_owner = nil
  end
end)

---A text field (2.29.0): the text in a framed box. opts: x, y, id, w (the box, 240), label, max
---(bytes, 1 to 255, 40), placeholder, disabled, focus (types from the update it first shows),
---tip, tip_wait, nav. A click on the box, or Enter under the keys' cursor, types into it; Enter
---keeps the text, Esc puts back the one it started with, a click elsewhere keeps it. Returns the
---text (as typed so far while it types), whether a typing ended on this update with a new text
---kept, and how a typing ended on this update ("enter", "esc", "away"; nil otherwise).
function ui.input(value, o)
  if o ~= nil and type(o) ~= "table" then error("uikit.input: opts is a table", 2) end
  o = o or {}
  if type(value) ~= "string" then error("uikit.input: the text is a string", 2) end
  local max = o.max == nil and 40 or o.max
  if math.type(max) ~= "integer" or max < 1 or max > 255 then error("uikit.input: max is 1 to 255 bytes", 2) end
  if o.w ~= nil and math.type(o.w) ~= "integer" then error("uikit.input: w is a whole number of pixels", 2) end
  if o.placeholder ~= nil and type(o.placeholder) ~= "string" then error("uikit.input: placeholder is a string", 2) end
  if o.label ~= nil and type(o.label) ~= "string" then error("uikit.input: label is a string", 2) end
  local th = ui.theme
  local id = id_of(o, "input")
  local st = ui.state(id)
  local key = o.id ~= nil and tostring(o.id) or o.label or "input"
  local style = th.label
  local dim = with(style, { r = th.dim.r, g = th.dim.g, b = th.dim.b })
  local disabled = o.disabled and true or false
  local B, PAD = 30, 8
  local lw = o.label and (text_w(o.label, style) + 12) or 0
  local bw = o.w or 240
  local x, y, w, h = place({ x = o.x, y = o.y }, lw + bw, B)
  local bx = x + lw
  if o.label then hud.text(x, y, o.label, with(disabled and dim or style, { h = h, valign = "center" })) end
  if typing == id then typing_alive = true end
  local fresh = st.seen ~= frame.n - 1 -- not drawn on the last update
  st.seen = frame.n
  local changed, ended = false, nil

  local over, clicked = interact(id, bx, y, bw, B, disabled, { nav = o.nav, tip = o.tip, tip_wait = o.tip_wait })
  local start = typing ~= id and not disabled and not frame.blind and (clicked or (o.focus and fresh))
  if start then
    if typing then rq.log("uikit.input: another field was typing; it stops where it was") end
    typing, typing_alive, text_owner = id, true, id
    st.start, st.live, st.scroll = value, value, 0
    st.kd = { [K_ENTER] = input.key(K_ENTER), [K_ESC] = input.key(K_ESC) } -- the Enter that started it
    hud.text_begin(value, max)
    if clicked then play("click") end
  end
  if typing == id then
    hud.block_keys() -- the game gets no key, Ctrl (its message skip) included
    focus.hold = true -- the arrows move the caret
    local function hit(c)
      local down = input.key(c)
      local was = st.kd[c]
      st.kd[c] = down
      return down and not was
    end
    local text, on = hud.text_value()
    if on then st.live = text end
    local done, keep
    if hit(K_ENTER) then done, keep = "enter", true end
    if hit(K_ESC) then done, keep = "esc", false end
    if mouse.pressed and not inside(bx, y, bw, B) then done, keep = "away", true end
    if not on then done, keep = "away", true end -- a mod's rq.hud.text_end took it
    if disabled then done, keep = "away", false end
    if done then
      typing = nil
      if text_owner == id then
        text_owner = nil
        hud.text_end()
      end
      for _, c in ipairs({ K_ENTER, K_ESC, K_BACK }) do
        if input.key(c) then held_after[c] = true end
      end
      local new = keep and st.live or st.start
      changed, ended = new ~= st.start, done
      value = new
      st.start, st.live = nil, nil
      if done == "enter" then play("click") end
    else
      value = st.live
    end
  end

  ui.draw("block", bx, y, bw, B, disabled and { tint = { 128, 128, 128 } } or nil)
  local ns = with(disabled and dim or style, { h = B, valign = "center" })
  hud.clip_push(bx + 4, y + 2, bw - 8, B - 4)
  if typing == id then
    local c = th.button_cover
    hud.rect(bx + 3, y + 3, bw - 6, B - 6, c.r, c.g, c.b, 50)
    local _, _, caret = hud.text_value()
    local t = value
    caret = math.min(caret or #t, #t)
    local left = t:sub(1, caret)
    local cx = left ~= "" and text_w(left, ns) or 0
    local tw = t ~= "" and text_w(t, ns) or 0
    local room = bw - 2 * PAD - 2
    -- The text slides so the caret stays in the box, and no further than its end.
    if cx - st.scroll > room then st.scroll = cx - room end
    if cx < st.scroll then st.scroll = cx end
    if tw - st.scroll < room then st.scroll = math.max(0, tw - room) end
    if t ~= "" then hud.text(bx + PAD - st.scroll, y, t, ns) end
    if (rq.time_ms() // 500) % 2 == 0 then
      hud.rect(bx + PAD - st.scroll + cx, y + 6, 2, B - 12, 255, 255, 255, 220)
    end
  elseif value ~= "" then
    hud.text(bx + PAD, y, value, ns)
  elseif o.placeholder then
    hud.text(bx + PAD, y, o.placeholder, with(dim, { h = B, valign = "center" }))
  end
  hud.clip_pop()
  if typing ~= id and over and not disabled then ui.draw("hover_bar", bx, y, bw, B, { alpha = 128 }) end
  -- For agents: the text (as typed so far while it types), and selected while it types.
  show("input", key, o.label, bx, y, bw, B, { value = value, disabled = disabled, selected = typing == id })
  return value, changed, ended
end

-- What the hub's widgets (uikit_hub.lua) build on.
local kit = {
  typed = typed,
  copy = copy, piece = piece, interact = interact, show = show, place = place, play = play,
  with = with, id_of = id_of, inside = inside, pulse = pulse, text_w = text_w, run_box = run_box,
  comment_size = ui.comment_size,
  mouse = mouse, frame = frame, focus = focus, focus_group = focus_group, set_focus = set_focus,
}

return ui, kit
