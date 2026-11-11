-- rq.uikit's windows (loader 2.32.0): the game's Quest Storage window with its item tiles
-- (rq.uikit.item_window), the quest panel's section heading and the box under it
-- (rq.uikit.section), the shop's speech balloon (rq.uikit.balloon) and the pieces of Trivia's
-- parchment, which rq.uikit.page draws with style = "parchment" (uikit_hub.lua). Built from the
-- game's own CGs (11335, 11014, 11110, 20001; cgdump of 2026-11-11) and its numbers
-- (T_StoreQuest, T_QuestComment, T_ShopMessage, T_KnowledgeList).
-- notes/plans/plan-map-panel-windows.md has the research.
local rq, ui, kit = ...
local hud, input = rq.hud, rq.input
local H = kit.hub
local T = H.T
local interact, show, place, play = kit.interact, kit.show, kit.place, kit.play
local with, id_of, text_w = kit.with, kit.id_of, kit.text_w
local mouse, frame = kit.mouse, kit.frame
local floor, max, min = math.floor, math.max, math.min
local hub = H.theme

local function game(t)
  t.font = "game"
  return t
end

-- ---- The theme ----

-- The Quest Storage window (T_StoreQuest, mode 0): 11335, 542x360 at 473,165. A grey band, the
-- yellow frame from y 34, 5x3 cells 106 apart with 2 pixel lines between the rows, the tiles at
-- +9,+39, the close button 10007 at +512,+6; no title. Rows are cut from it: the first with the band
-- (y 0 to 140); a middle one from the second row with the line above it (141 to 246); the last with
-- the line above it and the frame under it (247 to 359); a single row is the first and the frame's
-- last 5 lines (355). So a window of n rows is 146 high for one, else 42 + 106 n.
hub.item_window = {
  top = { cg = 11335, src = { 0, 0, 542, 141 } },
  middle = { cg = 11335, src = { 0, 141, 542, 106 } },
  last = { cg = 11335, src = { 0, 247, 542, 113 } },
  foot = { cg = 11335, src = { 0, 355, 542, 5 } },
  w = 542, cols = 5, cell = 106, tile_x = 9, tile_y = 39, band = 34,
  close_x = 512, close_y = 6,
  -- Where the game puts it; a taller window moves up so it ends at the 3D view's bottom (607).
  x = 473, y = 165, bottom = 607,
  title = game { face = 258, size = 22, bold = true, r = 236, g = 232, b = 176, outline = { 58, 53, 24 } },
  title_x = 10,
  -- The page's number between the arrows, and the text shown when there is nothing.
  page = game { face = 258, size = 20, r = 236, g = 232, b = 176 },
  empty = game { face = 258, size = 22, r = 60, g = 35, b = 10, outline = { 230, 210, 170 } },
}

-- The quest panel (T_QuestComment): 11014, 576x470, its four headings painted in. A heading's band
-- is rows 1 to 26: the ★ at the left end (x 0 to 28), the band fading from black to dark green at
-- 75% (its middle cut after the painted words, x 206 to 504), and the gold stripes of the slanted
-- right end (505 to 574). The see-through dark red box under it starts 29 lower, 32 in (rounded,
-- 4 pixel corners). The letters are the game's font in the heading's gold, sized down to fit; the
-- text in a box is the panel's (T_QuestComment::create: face 256, size 30, space -9).
hub.section = {
  left = { cg = 11014, src = { 0, 1, 29, 26 } },
  middle = { cg = 11014, src = { 206, 1, 299, 26 } },
  right = { cg = 11014, src = { 505, 1, 70, 26 } },
  box = { cg = 11014, src = { 32, 30, 536, 128 }, edge = { 4, 4, 4, 4 } },
  w = 575, h = 26, text_x = 31, box_x = 32, box_y = 29, box_right = 7,
  text = game { face = 258, size = 24, bold = true, r = 236, g = 172, b = 64, outline = { 40, 24, 0 } },
  fit = { 24, 14, 0.95, 0.5 },
  body = game { face = 256, size = 30, r = 255, g = 246, b = 213, space = -9 },
}

-- Frostbuy's balloon (T_ShopMessage): 11110, 384x116 at 20,87; the body is rows 0 to 93, with
-- 12 pixel rounded edges, see-through (60% at the edge, 75% inside); the tail hangs from x 146 to
-- 167 down to row 115. The text box is at +10,+9, 367 wide, face 258, size 28, color 80,25,0,
-- space -8, spacing 0.9; lines 25 apart.
hub.balloon = {
  top = { cg = 11110, src = { 0, 0, 384, 82 }, edge = { 12, 12, 12, 0 } },
  base = { cg = 11110, src = { 0, 82, 384, 12 }, edge = { 12, 0, 12, 0 }, mid = { 200, 100 } },
  left = { cg = 11110, src = { 0, 82, 140, 34 }, edge = { 12, 0, 0, 0 }, mid = { 20, 100 } },
  tail = { cg = 11110, src = { 140, 82, 32, 34 } },
  right = { cg = 11110, src = { 172, 82, 212, 34 }, edge = { 0, 0, 12, 0 }, mid = { 28, 100 } },
  w = 384, h = 94, edge = 12, tail_x = 140, tail_w = 32, tail_h = 22,
  pad_x = 10, pad_y = 9, pad_r = 7, line_h = 25,
  text = game { face = 258, size = 28, r = 80, g = 25, b = 0, space = -8, spacing = 0.9 },
}

-- Trivia's page (T_Knowledge, T_KnowledgeList): the parchment of 20001 with its black line, x 11 to
-- 710 and y 17 to 718; the vine strip down each side is kept (30 pixels each side), and the middle
-- rows are cut, not squashed, for a lower page. The text is at +39,+18 (50,35 on the screen): face
-- 256, size 28, color 90,50,10, space -8.
hub.parchment = {
  paper = { cg = 20001, src = { 11, 17, 700, 702 }, edge = { 30, 6, 30, 8 }, vfit = "crop" },
  x = 162, y = 56, w = 700, h = 536,
  pad_x = 39, pad_y = 18, pad_b = 16, close_x = 36, close_y = 14,
  text = game { face = 256, size = 28, r = 90, g = 50, b = 10, space = -8, edge = false },
  dim = { r = 150, g = 120, b = 90 },
  title_h = 40,
}

-- ---- The Quest Storage window ----

local ESC = input.code("esc")

-- An entry of item_window's list: what item_icon takes, or { item = that, judged, tip, disabled }.
local function entry_of(e)
  if type(e) == "table" and e.item ~= nil then return e.item, e end
  return e, nil
end

local function window_h(v, rows)
  if rows == 1 then return 141 + 5 end
  return 141 + v.cell * (rows - 2) + 113
end

---The game's Quest Storage window holding item tiles. See rq.lua for the options.
function ui.item_window(items, o)
  if type(items) ~= "table" then error("uikit.item_window: items is a list", 2) end
  o = o or {}
  if type(o) ~= "table" then error("uikit.item_window: opts is a table", 2) end
  local n = #items
  local rows = o.rows
  if rows == nil then rows = n > 15 and 4 or 3 end
  if math.type(rows) ~= "integer" or rows < 1 or rows > 5 then
    error("uikit.item_window: rows is 1 to 5", 2)
  end
  if o.cell ~= nil and type(o.cell) ~= "function" then error("uikit.item_window: cell is a function", 2) end
  local v = T("item_window")
  local per = v.cols * rows
  local wh = window_h(v, rows)
  local x = o.x or v.x
  local y = o.y or min(v.y, v.bottom - wh)
  local key = o.id ~= nil and tostring(o.id) or "itemwindow"
  local st = ui.state("itemwindow:" .. key)
  local pages = max(1, (n + per - 1) // per)
  st.page = min(max(st.page or 1, 1), pages)
  -- Modal, as a page: the game gets no mouse and no keys while it shows.
  if not frame.blind then
    hud.capture_mouse()
    hud.block_keys()
    ui.keys()
  end
  frame.over = true
  ui.draw(v.top, x, y)
  if rows == 1 then
    ui.draw(v.foot, x, y + 141)
  else
    for r = 2, rows - 1 do ui.draw(v.middle, x, y + 141 + v.cell * (r - 2)) end
    ui.draw(v.last, x, y + 141 + v.cell * (rows - 2))
  end
  local band_room = v.close_x - v.title_x - 8
  -- Pages: arrows and the page's number at the band's right, before the close button.
  if pages > 1 then
    local bw = 34
    local px = x + v.close_x - 8 - bw * 2 - 56
    if ui.button("<", { style = "short", x = px, y = y + 4, w = bw, h = 26, id = key .. ".prev",
        disabled = st.page <= 1 }) then
      st.page = st.page - 1
    end
    local label = st.page .. "/" .. pages
    hud.text(px + bw, y + 4, label, with(v.page, { w = 56, h = 26, align = "center", valign = "center" }))
    if ui.button(">", { style = "short", x = px + bw + 56, y = y + 4, w = bw, h = 26, id = key .. ".next",
        disabled = st.page >= pages }) then
      st.page = st.page + 1
    end
    band_room = px - x - v.title_x - 8
  end
  if o.title then
    -- The clip starts left of the text, so its outline is not cut.
    hud.clip_push(x + v.title_x - 4, y, max(1, band_room + 4), v.band)
    hud.text(x + v.title_x, y, o.title, with(v.title, { h = v.band, valign = "center" }))
    hud.clip_pop()
  end
  local closed = ui.close({ x = x + v.close_x, y = y + v.close_y, id = key })
  -- A right click or Esc closes when let go, as on a page.
  local esc = input.key(ESC) and not kit.typed(ESC)
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
  show("page", key, o.title, x, y, v.w, wh)
  local clicked
  local first = (st.page - 1) * per
  for i = 1, min(per, n - first) do
    local k = first + i
    local item, e = entry_of(items[k])
    local c, r = (i - 1) % v.cols, (i - 1) // v.cols
    local tx, ty = x + v.tile_x + v.cell * c, y + v.tile_y + v.cell * r
    if item ~= nil then
      local hit = ui.item_icon(item, { x = tx, y = ty, id = key .. "." .. k, judged = e and e.judged,
        tip = e and e.tip, disabled = e and e.disabled })
      if hit then clicked = k end
    end
    if o.cell then
      local ok, err = pcall(o.cell, k, tx, ty, items[k])
      if not ok then error(err, 0) end
    end
  end
  if n == 0 and o.empty then
    hud.text(x + v.tile_x, y + v.tile_y, o.empty,
      with(v.empty, { w = v.cell * v.cols - 12, h = 100, align = "center", valign = "center" }))
  end
  if closed then st.esc = nil end
  return not closed, clicked
end

-- ---- The quest panel's heading ----

---A heading in the quest panel's look, with an optional box under it. See rq.lua for the options.
function ui.section(text, o)
  if type(text) ~= "string" then error("uikit.section: text is a string", 2) end
  o = o or {}
  local v = T("section")
  if o.box ~= nil and (math.type(o.box) ~= "integer" or o.box < 8) then
    error("uikit.section: box is a height of 8 or more", 2)
  end
  local total = v.h + (o.box and (v.box_y - v.h + o.box) or 0)
  local x, y, w = place(o, v.w, total)
  w = max(w, 29 + 70 + 1)
  ui.draw(v.left, x, y)
  ui.draw(v.middle, x + 29, y, w - 29 - 70, v.h)
  ui.draw(v.right, x + w - 70, y)
  local room = w - v.text_x - 70
  local ts = H.fitted(text, v.text, v.fit, room)
  hud.text(x + v.text_x, y, text, with(ts, { h = v.h + 2, valign = "center" }))
  show("text", o.id or text, text, x, y, w, v.h)
  if not o.box then return x + v.text_x, y, room, v.h end
  local bx, by, bw = x + v.box_x, y + v.box_y, w - v.box_x - v.box_right
  ui.draw(v.box, bx, by, bw, o.box)
  return bx, by, bw, o.box
end

-- ---- The shop's balloon ----

---Text in Frostbuy's speech balloon. See rq.lua for the options.
function ui.balloon(text, o)
  if type(text) ~= "string" then error("uikit.balloon: text is a string", 2) end
  o = o or {}
  local v = T("balloon")
  local st = v.text
  local tail = o.tail
  if tail == nil then tail = true end
  local w = o.w or v.w
  if type(w) ~= "number" or w < 2 * v.edge + v.tail_w + 8 then error("uikit.balloon: w is too small", 2) end
  local lines = kit.wrap(text, st, w - v.pad_x - v.pad_r, st.spacing, st.spacing)
  local body = o.h or max(v.h, v.pad_y * 2 + #lines * v.line_h + 6)
  local x, y = place(o, w, body + (tail and v.tail_h or 0))
  ui.draw(v.top, x, y, w, body - v.edge)
  local by = y + body - v.edge
  if tail then
    local tx = floor(o.tail_x or v.tail_x)
    tx = min(max(tx, v.edge), w - v.edge - v.tail_w)
    ui.draw(v.left, x, by, tx, v.edge + v.tail_h)
    ui.draw(v.tail, x + tx, by)
    ui.draw(v.right, x + tx + v.tail_w, by, w - tx - v.tail_w, v.edge + v.tail_h)
  else
    ui.draw(v.base, x, by, w, v.edge)
  end
  hud.clip_push(x + v.pad_x, y + v.pad_y, w - v.pad_x - v.pad_r, body - v.pad_y)
  for i, l in ipairs(lines) do
    hud.text(x + v.pad_x, y + v.pad_y + (i - 1) * v.line_h, l[1], with(st, { spacing = l[2] }))
  end
  hud.clip_pop()
  show("text", o.id or text, text, x, y, w, body)
  return x, y, w, body
end
