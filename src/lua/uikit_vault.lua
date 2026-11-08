-- rq.uikit's vault widgets (loader 2.8.0): the vault's item rows and list, its filter column and
-- sub-filters, its counters, and an item picker over the player's items. Built from the game's
-- own CGs (11401 to 11424, 11072 to 11089, 11200 to 11224) and its numbers (T_StoreList,
-- T_StoreComment, T_StoreItem, T_LimitItem, T_LimitItemEx, T_LimitJob, T_LimitLock). The data
-- comes from the game's own item calls, and the filters and sorts are the vault's own tests
-- (アイテム倉庫_一覧_*, compItem_*), through rq.ain.call. notes/plans/plan-ui-kit-hub.md has the research.
local rq, ui, kit = ...
local hud = rq.hud
local H = kit.hub
local T, fitted, cover, GREY = H.T, H.fitted, H.cover, H.GREY
local interact, show, place, play = kit.interact, kit.show, kit.place, kit.play
local with, id_of, inside, text_w = kit.with, kit.id_of, kit.inside, kit.text_w
local frame = kit.frame
local focus, focus_group = kit.focus, kit.focus_group
local floor, max, min = math.floor, math.max, math.min
local call = rq.ain.call
local hub = H.theme

local function game(t)
  t.font = "game"
  return t
end

-- Two states of a CG side by side: the left one lit, the right one dim (or hovered).
local function pair(cg, w, h)
  return { cg = cg, src = { 0, 0, w, h } }, { cg = cg, src = { w, 0, w, h } }
end

-- ---- The theme ----

local function button(cg, w, h, x, y)
  local a, b = pair(cg, w, h)
  return { normal = a, hover = b, w = w, h = h, x = x, y = y }
end

hub.vault = {
  -- A row is cut from the panel it is painted in: the vault's (11401, at 532), the storage's red
  -- one (11403, at 0), the shop's green one with its GOLD box (11405, at 307) or a character's
  -- brown one (11415, at 307); rows are 381x128, 128 apart, at x 538 in each.
  rows = {
    vault = { cg = 11401, src = { 6, 0, 381, 128 } },
    storage = { cg = 11403, src = { 101, 0, 381, 128 } },
    shop = { cg = 11405, src = { 231, 0, 381, 128 } },
    equip = { cg = 11415, src = { 231, 0, 381, 128 } },
  },
  -- The shop's price (T_StoreList::setPos, T_Number 11103 mode 1): 7 digits right-aligned at
  -- +220,+2, 17 apart.
  price = { x = 220, y = 2, cells = 7, gap = 1 },
  -- T_StoreList: the name at +2,+1 in 202 (face 256, bold, dark red); the icon at +2,+25 with
  -- its rank badge (11420 + rank) at its bottom left; the level bar 11407 at +105,+80; the
  -- stats from +107,+27, 17 apart; the class icons from +107,+98, 34 apart; LOCKED at +297,+4.
  name = game { face = 256, size = 24, bold = true, r = 70, g = 6, b = 1, space = -4 },
  name_fit = { 24, 12, 1.0, 0.5 },
  info = game { face = 258, size = 20, r = 255, g = 245, b = 100, space = -10,
    shadow = { x = 1, y = 1, r = 96, g = 64, b = 48 } },
  number = game { face = 258, size = 20, r = 255, g = 255, b = 255, space = -10,
    shadow = { x = 1, y = 1, r = 0, g = 0, b = 0 } },
  info_fit = { 0.95, 0.6, 0.4 },
  level = { cg = 11407 },
  level_text = game { face = 258, size = 16, r = 0, g = 0, b = 0, space = -4,
    shadow = { x = 1, y = 1, r = 224, g = 0, b = 0 } },
  level_fit = { 16, 12, 1.0, 0.5 },
  locked = { cg = 11419 },
  rank = 11420,
  kinds = { [21] = 10407, [1] = 10408, [11] = 10409, [17] = 10410 },
  current = { r = 0, g = 255, b = 255, a = 128 },
  -- The row's buttons (T_StoreItem::setPos): LOCK / UNDO LOCK at +204,+1, SELL at +285,+7,
  -- DONATE at +335,+7.
  buttons = {
    lock = button(11417, 74, 27, 204, 1),
    unlock = button(11418, 74, 27, 204, 1),
    sell = button(11409, 40, 22, 285, 7),
    donate = button(11406, 40, 22, 335, 7),
  },
  -- The counters (T_StoreItem: 3 digits at 957,31, face 258, 20 px, bold, white) and the gold
  -- digits (11103, 16x23 each, 0 to 9).
  count = game { face = 258, size = 20, bold = true, r = 255, g = 255, b = 255 },
  gold = { cg = 11103, w = 16, h = 23 },
  count_box = { cg = 11401, src = { 422, 28, 57, 22 } },
  -- The storage's stock count in the storage screen (957,94 in a red box).
  stock_box = { cg = 11401, src = { 418, 88, 66, 26 } },
}

-- The filter column (T_LimitItem, T_LimitJob, T_LimitLock): see-through CGs with the lit state
-- left and the dim one right. From the categories' top left: categories 34 apart, the classes in
-- two columns 37 by 34 from +0,+190, Locked and Unlocked 34 apart from +0,+346, Equippable and
-- Unequippable (T_LimitEquip, a character's list) 34 apart from +0,+434. The column's grey and
-- its "[FILTERING]" heads are painted into each panel: the vault's with Locked, the shop's
-- without it, a character's with Locked and Equippable.
local CATS = { { 21, "special" }, { 1, "weapon" }, { 11, "armor" }, { 17, "item" } }
hub.filters = {
  back = { cg = 11401, src = { 412, 122, 80, 445 }, x = -1, y = -32 },
  backs = {
    vault = { cg = 11401, src = { 412, 122, 80, 445 }, x = -1, y = -32 },
    shop = { cg = 11405, src = { 637, 122, 80, 357 }, x = -1, y = -32 },
    equip = { cg = 11415, src = { 637, 122, 80, 533 }, x = -1, y = -32 },
  },
  cats = {},
  classes = {},
  lock = {},
  equip = {},
  -- T_LimitItemEx: the panel 11200 (381x127) over the list's first row, the title in it, and its
  -- buttons right to left from +334,+32, 47 apart.
  sub = {
    base = { cg = 11200 },
    title = game { face = 258, size = 22, bold = true, r = 193, g = 193, b = 193, space = -6 },
    [1] = { title = "Weapons: click buttons to filter by type", cgs = { 11201, 11202, 11203, 11204, 11205, 11206, 11207, 11210 },
      names = { "daggers", "swords", "hammers", "spears", "bows", "gloves", "staves", "others" } },
    [11] = { title = "Armor: click buttons to filter by type", cgs = { 11211, 11212, 11213, 11214, 11210 },
      names = { "leather", "plate", "robes", "gi", "others" } },
    [21] = { title = "Special Items: click buttons to filter by rank", cgs = { 11221, 11222, 11223, 11224, 11210 },
      names = { "rank-c", "rank-b", "rank-a", "rank-s", "others" } },
  },
}
for i, c in ipairs(CATS) do
  local h = c[1] == 17 and 64 or 30
  local a, b = pair(11080 + i, 73, h)
  hub.filters.cats[i] = { type = c[1], name = c[2], lit = a, dim = b, h = h }
end
for i = 1, 8 do
  local a, b = pair(11071 + i, 36, 30)
  hub.filters.classes[i] = { lit = a, dim = b }
end
for i = 1, 2 do
  local a, b = pair(11087 + i, 73, 30)
  hub.filters.lock[i] = { lit = a, dim = b, name = i == 1 and "locked" or "unlocked" }
  local c, d = pair(11085 + i, 73, 30)
  hub.filters.equip[i] = { lit = c, dim = d, name = i == 1 and "equippable" or "unequippable" }
end

-- ---- An item's row data, from the game ----

local class_icons = {}
local function class_icon(c)
  local cg = class_icons[c]
  if cg == nil then
    local ok, v = pcall(call, "職業_アイコン", c)
    cg = ok and v or 0
    class_icons[c] = cg
  end
  return cg
end

---The data for an item row, read from the game for an instance: ii, name, level (the level line,
---or ""), info (the stats line with the game's marks), icon, rank, kind, locked, price, place and
---classes (the class numbers that may equip it). nil when there is no such instance.
function ui.item_data(ii)
  if math.type(ii) ~= "integer" then error("uikit.item_data: expected an item instance (ii)", 2) end
  if ii <= 0 or not call("アイテム_確認", ii) then return nil end
  local row = {
    ii = ii,
    name = call("アイテム_名前", ii),
    level = call("アイテム_実装条件", ii),
    info = call("アイテム_情報", ii),
    icon = call("アイテム_ＣＧ番号", ii),
    rank = call("アイテム_レアランク", ii),
    kind = call("アイテム_種類", ii),
    locked = call("アイテム_ＬＯＣＫ中", ii),
    price = call("アイテム_価格", ii),
    place = call("アイテム_存在", ii),
    discard = call("アイテム_破棄可能", ii),
    classes = {},
  }
  for c = 1, 8 do
    if call("アイテム_装備職業", ii, c) > 0 then row.classes[#row.classes + 1] = c end
  end
  return row
end

-- Rows read for lists, by ii, for a second; ui.item_refresh drops them.
local cache, cache_n = {}, 0
local function data_of(v)
  if type(v) == "table" then return v end
  local hit = cache[v]
  local now = rq.time_ms()
  if hit and now - hit.t < 1000 then return hit.row end
  local row = ui.item_data(v)
  if cache_n > 400 then cache, cache_n = {}, 0 end
  if not hit then cache_n = cache_n + 1 end
  cache[v] = { row = row, t = now }
  return row
end

---Drops the rows item lists keep (for a second) of an instance, or of all with no ii, so a
---change shows at once.
local changes = 0 -- counts item changes made through rq.item, so pickers list again
function ui.item_refresh(ii)
  if ii == nil then cache, cache_n = {}, 0 else cache[ii] = nil end
  changes = changes + 1
end

-- ---- The stats line ----

-- The characters T_StoreComment::isNumber2 counts as a number: digits, + and the full-width
-- digits, ＋ － Ａ Ｂ Ｃ Ｓ; a point only inside a number; a minus sign before one.
local NUM = { ["+"] = true, ["＋"] = true, ["－"] = true, ["−"] = true, ["Ａ"] = true, ["Ｂ"] = true,
  ["Ｃ"] = true, ["Ｓ"] = true }
for d = 0, 9 do
  NUM[tostring(d)] = true
  NUM[utf8.char(0xFF10 + d)] = true
end
local POINT = { ["."] = true, ["．"] = true }
local MINUS = { ["-"] = true, ["―"] = true }

local function is_num(ch, i, point, neg)
  local c = ch[i] or " "
  if NUM[c] then return true end
  if POINT[c] then return point end
  if MINUS[c] then return neg and is_num(ch, i + 1, point, false) end
  return false
end

local function chars(s)
  local out = {}
  for _, c in utf8.codes(s) do out[#out + 1] = utf8.char(c) end
  return out
end

-- T_StoreComment::convertLine: {kind, text} parts, kind 1 for a number (white), 0 for text
-- (yellow). `...` keeps its numbers yellow; |...| is white.
local function parts_of(line)
  local ch, out = chars(line), {}
  local function add(kind, a, b)
    if b >= a then out[#out + 1] = { kind, table.concat(ch, "", a, b) } end
  end
  local i, n, start, force = 1, #ch, 1, false
  while i <= n do
    local c = ch[i]
    if c == "`" or c == "|" then
      add(force and (c == "|" and 1 or 0) or 0, start, i - 1)
      force = not force
      start = i + 1
      i = i + 1
    elseif not force and is_num(ch, i, false, true) then
      add(0, start, i - 1)
      local j = i
      while j <= n and is_num(ch, j, true, true) do j = j + 1 end
      add(1, i, j - 1)
      start, i = j, j
    else
      i = i + 1
    end
  end
  add(0, start, n)
  return out
end

-- The game's text wrap (New/DrawTextCode.jaf), measured as the game measures: marks and all, and
-- without the shadow (rq.hud.text_width counts its offset).
local function bare(style)
  local s = with(style, {})
  s.shadow = nil
  return s
end

local function width(chs, n, style)
  return text_w(table.concat(chs, "", 1, n), style)
end

-- MeasureTextUntilPixel: how many characters fit in maxw.
local function fit_count(chs, style, maxw)
  if width(chs, #chs, style) <= maxw then return #chs end
  local lo, hi = 0, #chs
  while hi - lo > 1 do
    local mid = (lo + hi) // 2
    if width(chs, mid, style) <= maxw then lo = mid else hi = mid end
  end
  return lo
end

-- FitText3: the widest spacing, in steps of 1/20, at which the line fits.
local function fit3(line, style, maxw, maxs, mins)
  local s1, s2 = floor(mins * 20 + 0.5), floor(maxs * 20 + 0.5)
  local probe = with(style, {})
  while true do
    local sI = (s1 + s2) // 2
    if sI == s1 then return sI / 20 end
    probe.spacing = sI / 20
    if text_w(line, probe) <= maxw then s1 = sI else s2 = sI end
  end
end

-- WordWrapText2: a line that fits at the widest spacing keeps it; one that fits at the least gets
-- the widest that fits; else it breaks at the last space before the end of what fits (English),
-- or after any other character (Japanese).
local function wrap2(text, style, maxw, maxs, mins, out)
  while true do
    local chs = chars(text)
    local n = #chs
    local imax = fit_count(chs, with(style, { spacing = maxs }), maxw)
    if imax == n then
      out[#out + 1] = { text, maxs }
      return
    end
    local imin = imax
    if maxs > mins then
      imin = fit_count(chs, with(style, { spacing = mins }), maxw)
      if imin == n then
        out[#out + 1] = { text, fit3(text, style, maxw, maxs, mins) }
        return
      end
    end
    local function code(k) return utf8.codepoint(chs[k]) end
    local i = max(imin, 1)
    local c = code(i)
    while c > 32 and c < 128 and i > 1 do
      i = i - 1
      c = code(i)
    end
    while c <= 32 and i > 1 do
      i = i - 1
      c = code(i)
    end
    if i == 1 then i = max(imin, 1) end
    local line = table.concat(chs, "", 1, i)
    out[#out + 1] = { line, fit3(line, style, maxw, maxs * 1.5, mins) }
    local j = i + 1
    while j <= n and code(j) <= 32 do j = j + 1 end
    if j > n then return end
    text = table.concat(chs, "", j, n)
  end
end

-- WordWrapText: each of $, @ and a line break starts a line.
local function wrap(text, style, maxw, maxs, mins)
  local out = {}
  for seg in (text .. "\n"):gmatch("([^$@\n]*)[$@\n]") do wrap2(seg, style, maxw, maxs, mins, out) end
  return out
end

-- T_StoreComment::setText2: wraps at 264, and while there are more lines than room, makes the first
-- line break a space and wraps again tighter. Kept by text.
local info_cache, info_n = {}, 0
local function info_lines(text, most)
  local key = most .. "\0" .. text
  local hit = info_cache[key]
  if hit then return hit end
  local v = T("vault")
  local fit = v.info_fit
  local style = bare(v.info)
  local lines = wrap(text, style, 264, fit[1], fit[2])
  while #lines > most do
    local joined, k = text:gsub("[$@\n]", " ", 1)
    if k == 0 then break end
    text = joined
    lines = wrap(text, style, 264, 1.0, fit[3])
  end
  local out = {}
  for i = 1, min(most, #lines) do out[i] = { parts = parts_of(lines[i][1]), spacing = lines[i][2] } end
  if info_n > 300 then info_cache, info_n = {}, 0 end
  info_cache[key] = out
  info_n = info_n + 1
  return out
end

local function draw_info(x, y, text, most)
  local v = T("vault")
  for li, line in ipairs(info_lines(text, most)) do
    local cx = x
    local ly = y + (li - 1) * 17
    for _, p in ipairs(line.parts) do
      local st = with(p[1] == 1 and v.number or v.info, { spacing = line.spacing })
      hud.text(cx, ly + (p[1] == 1 and 1 or 2), p[2], st)
      st.shadow = nil
      cx = cx + text_w(p[2], st)
    end
  end
end

-- ---- Keys ----

local key_hit, keys_seen = H.key_hit, H.keys_seen

-- ---- Dragging rows ----

-- One drag at a time: the row, its list and group, where it started, and whether it moved far
-- enough to be a drag. A drop lands on the update the button comes up and is kept for that update.
local drag, dropped = nil, nil
local mouse = kit.mouse
rq.on_frame(function()
  dropped = nil
  if drag and (drag.done or (not mouse.down and not mouse.released)) then drag = nil end
end)

-- ---- The row ----

local BUTTONS = { "lock", "sell", "donate" }
local KEY_OF = { lock = 76, unlock = 76, sell = 83, donate = 68 } -- L, S, D

-- The row's name, as T_DrawText draws it in 202 by 24: centered, from 24 px down to 12; past
-- that in two lines of 12 px (wrapped as WordWrapText does), and cut at 202.
local function draw_name(x, y, name, v)
  local ns = fitted(name, v.name, v.name_fit, 202)
  if text_w(name, ns) <= 202 then
    hud.text(x, y, name, with(ns, { w = 202, h = 22, align = "center", valign = "center" }))
    return
  end
  local small = with(v.name, { size = v.name_fit[2] })
  local lines = wrap(name, small, 202, v.name_fit[3], v.name_fit[4])
  hud.clip_push(x, y, 202, 24)
  for i = 1, min(2, #lines) do
    hud.text(x, y + (i - 1) * 12, lines[i][1], with(small, { spacing = lines[i][2], w = 202, h = 12,
      align = "center", valign = "center" }))
  end
  hud.clip_pop()
end

---A vault item row (381 by 128): the name, the icon with its rank, the level bar, the stats in
---the game's colors, the classes that may equip it, LOCKED, the shop's price, and the row's
---buttons. row is ui.item_data's table, an ii, or a table of your own with the same fields.
---opts: x, y, id, style ("vault", "storage", "shop" or "equip"), buttons ({lock, sell, donate} =
---true), current, disabled. Returns clicked (the row) and the button clicked: "lock", "unlock",
---"sell" or "donate".
function ui.item_row(row, o)
  o = o or {}
  if row == nil then error("uikit.item_row: expected a row or an ii", 2) end
  local r = data_of(row)
  if not r then error("uikit.item_row: no item instance " .. tostring(row), 2) end
  local v = T("vault")
  local style = o.style or "vault"
  local bg = v.rows[style]
  if not bg then error("uikit.item_row: style is \"vault\", \"storage\", \"shop\" or \"equip\"", 2) end
  local key = o.id ~= nil and tostring(o.id) or ("item-" .. tostring(r.ii or r.name or "row"))
  local x, y, w, h = place(o, 381, 128)
  -- The buttons first: a click on one is not a click on the row. A locked row has no SELL or
  -- DONATE, as in the vault (T_StoreItem::setButtonShow).
  local pressed, on_button
  local btns = {}
  if o.buttons and not o.disabled then
    for _, name in ipairs(BUTTONS) do
      if o.buttons[name] and not (name ~= "lock" and r.locked) then
        local bname = (name == "lock" and r.locked) and "unlock" or name
        local b = v.buttons[bname]
        local bid = id_of({ id = key .. "." .. bname }, "itembtn")
        local over, clicked, _, mouse_on = interact(bid, x + b.x, y + b.y, b.w, b.h, false)
        if mouse_on then on_button = true end
        if clicked then pressed = bname end
        btns[#btns + 1] = { b = b, over = over, name = bname }
      end
    end
  end
  local rid = id_of({ id = key }, "itemrow")
  local over, clicked = interact(rid, x, y, w, h, o.disabled or on_button,
    { group = o.group, nav = o.nav, tip = o.tip, tip_wait = o.tip_wait })
  ui.draw(bg, x, y, w, h, o.disabled and GREY or nil)
  if r.name and r.name ~= "" then draw_name(x + 2, y + 1, r.name, v) end
  if r.icon and r.icon > 0 then
    hud.cg(x + 2, y + 25, r.icon, { w = 100, h = 100, alpha = o.disabled and 128 or nil })
  end
  if r.rank and r.rank > 0 then
    local cg = v.rank + r.rank
    local _, rh = hud.cg_size(cg)
    hud.cg(x + 2, y + 25 + 100 - (rh or 30), cg)
  end
  local has_level = r.level ~= nil and r.level ~= ""
  if has_level then
    ui.draw(v.level, x + 105, y + 80)
    local lw = 274
    hud.text(x + 105, y + 80, r.level, with(fitted(r.level, v.level_text, v.level_fit, lw), { w = lw, h = 17,
      align = "center", valign = "center" }))
  end
  if r.info and r.info ~= "" then draw_info(x + 107, y + 27, r.info, has_level and 3 or 4) end
  for i, c in ipairs(r.classes or {}) do
    local cg = class_icon(c)
    if cg and cg > 0 then hud.cg(x + 107 + 34 * (c - 1), y + 98, cg) end
    if i > 8 then break end
  end
  if r.locked and style ~= "shop" then ui.draw(v.locked, x + 297, y + 4) end
  if style == "shop" then
    -- The shop shows the price in its GOLD box instead of the kind.
    local p = v.price
    ui.number(max(0, math.tointeger(r.price) or 0), { x = x + p.x, y = y + p.y, font = "gold",
      w = p.cells * (v.gold.w + p.gap) - p.gap, align = "right", gap = p.gap })
  elseif not o.buttons and r.kind and v.kinds[r.kind] then
    hud.cg(x + 206, y + 4, v.kinds[r.kind])
  end
  for _, b in ipairs(btns) do
    ui.draw(b.over and b.b.hover or b.b.normal, x + b.b.x, y + b.b.y, b.b.w, b.b.h)
    show("button", key .. "-" .. b.name, b.name, x + b.b.x, y + b.b.y, b.b.w, b.b.h)
  end
  -- The game's keys on the current row: L locks, S sells, D donates (T_StoreItem::checkKey*).
  if o.keys and o.current and not pressed then
    local st = ui.state("itemrowkeys:" .. key)
    for _, b in ipairs(btns) do
      if key_hit(st, KEY_OF[b.name]) then pressed = b.name end
    end
    keys_seen(st)
  end
  if over and not o.disabled and not on_button then cover(x, y, w, h) end
  if o.current then
    local c = v.current
    hud.rect(x, y, w, h, c.r, c.g, c.b, c.a)
  end
  if clicked or pressed then play("click") end
  show("row", key, r.name, x, y, w, h, { selected = o.current and true or false, disabled = o.disabled,
    value = r.ii and tostring(r.ii) or nil })
  return clicked, pressed
end

---The vault's item list: rows 128 apart (6 by default) and the game's scroll bar beside them,
---which the wheel over the rows moves a row at a time. rows hold row tables or ii numbers.
---opts: x, y, id (keeps the scroll), shown, style, buttons, bar ({y, h}), keys (the arrow keys,
---Page Up and Down, Home, End and Enter, and L, S, D on the current row), drag (a group name:
---rows can be dragged to another list of the group). Returns the current index, whether it
---changed, the button clicked with the index of its row, and whether a row was chosen (clicked,
---or Enter).
function ui.item_list(rows, index, o)
  o = o or {}
  if type(rows) ~= "table" then error("uikit.item_list: expected a list of rows", 2) end
  if o.drag ~= nil and type(o.drag) ~= "string" then error("uikit.item_list: drag is a group name", 2) end
  local id = id_of(o, "itemlist")
  local st = ui.state(id)
  local n = o.shown or 6
  local x, y, w, h = place(o, 381 + 1 + 20, n * 128)
  local max_top = max(0, #rows - n)
  st.top = min(max(st.top or 0, 0), max_top)
  local key = o.id ~= nil and tostring(o.id) or "items"
  local changed, button, at, chosen = false, nil, nil, false
  -- The rows are a list for the kit's keys' cursor (2.12.0): Up and Down go through them and the
  -- list follows; at its ends they go on to the widget beyond. Enter clicks the row under it.
  local pre = "itemrow:" .. key .. "."
  local ids = {}
  for k, r in ipairs(rows) do
    if not (type(r) == "table" and r.disabled) then ids[#ids + 1] = pre .. k end
  end
  -- With keys, the cursor starts on the current row, else the first, as the list's keys did.
  local home = index or (o.keys and #rows > 0 and 1) or nil
  focus_group(id, ids, { home = home and (pre .. home) or nil, box = { x = x, y = y, w = 381, h = h } })
  local fk = focus.src == "keys" and type(focus.id) == "string" and focus.id:sub(1, #pre) == pre
    and tonumber(focus.id:sub(#pre + 1))
  -- Keys first, so the row they make current draws as current on this update.
  if o.keys and #rows > 0 then
    ui.keys()
    local old = index
    -- With keys, the cursor's row is the current one, as the vault's.
    local i = fk or index or 0
    if key_hit(st, "pagedown") then i = i + n end
    if key_hit(st, "pageup") then i = i - n end
    if key_hit(st, "home") then i = 1 end
    if key_hit(st, "end") then i = #rows end
    if i ~= (index or 0) and i ~= 0 then
      i = min(max(i, 1), #rows)
      if i ~= old then
        index, changed = i, true
        if i ~= fk then play("hover") end
      end
      if i ~= fk then kit.set_focus(pre .. i) end
      fk = i
    end
    keys_seen(st)
    if not frame.blind then hud.block_keys({ "home", "end", "l", "s", "d" }) end
  end
  if fk then
    if fk <= st.top then st.top = fk - 1 elseif fk > st.top + n then st.top = fk - n end
    st.top = min(max(st.top, 0), max_top)
  end
  for i = 1, n do
    local k = st.top + i
    local r = rows[k]
    if r ~= nil then
      local ry = y + (i - 1) * 128
      local dis = type(r) == "table" and r.disabled
      local c, b = ui.item_row(r, { x = x, y = ry, style = o.style, buttons = o.buttons,
        current = k == index, id = key .. "." .. k, disabled = dis, keys = o.keys, group = id })
      if c and not (drag and drag.moved) then
        if k ~= index then index, changed = k, true end
        chosen = true
      end
      if b then button, at = b, k end
      -- A press on a row may start a drag.
      if o.drag and not dis and mouse.pressed and inside(x, ry, 381, 128) and not drag then
        drag = { group = o.drag, from = key, row = r, index = k, sx = mouse.x, sy = mouse.y }
      end
    end
  end
  local by, bh = y, h
  if type(o.bar) == "table" then by, bh = o.bar[1] or o.bar.y or y, o.bar[2] or o.bar.h or h end
  st.top = ui.scrollbar(st.top, #rows, n, { x = x + 382, y = by, h = bh, id = key .. ".bar",
    wheel = { x, y, 381, h } })
  if inside(x, y, 381, h) then
    frame.over = true
    hud.block_game_input()
  end
  if o.drag and drag and drag.group == o.drag then
    if not drag.moved and mouse.down and (math.abs(mouse.x - drag.sx) > 6 or math.abs(mouse.y - drag.sy) > 6) then
      drag.moved = true
    end
    if drag.moved then
      if mouse.released or not mouse.down then
        drag.done = true
        if drag.from ~= key and inside(x, y, 381 + 22, h) and not dropped then
          dropped = { group = o.drag, row = drag.row, from = drag.from, to = key, index = drag.index }
          play("click")
        end
      else
        -- The row's icon follows the mouse, drawn by the list it came from on the top layer
        -- (2.11.0), over every widget and clip.
        if drag.from == key then
          local dr = data_of(drag.row)
          if dr and dr.icon and dr.icon > 0 then
            hud.top(function() hud.cg(mouse.x - 50, mouse.y - 50, dr.icon, { w = 100, h = 100 }) end)
          end
        end
        if inside(x, y, 381, h) and drag.from ~= key then
          local c = T("vault").current
          hud.rect(x, y, 381, h, c.r, c.g, c.b, 48)
        end
      end
    end
  end
  return index, changed, button, at, chosen
end

---The row dropped on this update on another list of a drag group: the row (an ii or a table),
---the id of the list it came from, the id of the list it was dropped on, and its index there. nil
---when nothing was dropped.
function ui.item_dropped(group)
  if group ~= nil and type(group) ~= "string" then error("uikit.item_dropped: group is a name", 2) end
  local d = dropped
  if not d or (group ~= nil and d.group ~= group) then return nil end
  return d.row, d.from, d.to, d.index
end

-- ---- Counters ----

-- The game's digit fonts (2.14.0), each a CG of 10 cells, by where the game uses them.
local DIGITS = {
  gold = 11103,      -- the vault's and the shop's gold
  hp = 10096,        -- HP in battle, the mini cards' skill uses
  level = 10122,     -- levels and maximums on the cards, skill uses on the status page
  level_max = 10124, -- a level at its maximum, on the cards
  status = 10321,    -- the status page's points, EXP and gold, the level up, the results
  small = 10210,     -- the mini-mini cards' skill uses
  world = 10507,     -- the world level on the title
  gallery = 12252,   -- the CG mode's page numbers
}

---A number: the vault's counter (font "count", default), or a digit font of the game's: a name in
---DIGITS or a CG number (2.14.0). opts: x, y, w (with align: "left", "center", the default, or
---"right"), digits (pads with zeros), gap (pixels between digits), hyphen (the font's CG has an 11th
---cell, "-", for negative numbers), tint (multiplies the digits' colors, 2.15.0). Returns x, y, w, h.
function ui.number(value, o)
  o = o or {}
  if math.type(value) ~= "integer" then error("uikit.number: expected a whole number", 2) end
  local align = o.align or "center"
  if align ~= "left" and align ~= "center" and align ~= "right" then
    error("uikit.number: align is \"left\", \"center\" or \"right\"", 2)
  end
  local v = T("vault")
  local font = o.font
  if font ~= nil and font ~= "count" then
    -- A digit font (2.14.0): a CG cut across into 10 cells, 0 to 9, or 11 with a hyphen, as the
    -- game's T_NumberCg (ＣＧ_分割_横).
    local cg = DIGITS[font] or (math.type(font) == "integer" and font) or nil
    if not cg then
      local names = {}
      for k in pairs(DIGITS) do names[#names + 1] = '"' .. k .. '"' end
      table.sort(names)
      error("uikit.number: font is \"count\", " .. table.concat(names, ", ") .. " or a CG number", 2)
    end
    local cw, ch = hud.cg_size(cg)
    if not cw then error("uikit.number: no CG " .. cg, 2) end
    cw = cw // (o.hyphen and 11 or 10)
    -- The game draws no less than 0; with a hyphen a negative number gets its "-".
    local s = tostring(o.hyphen and value or math.max(0, value))
    local minus = s:sub(1, 1) == "-"
    if minus then s = s:sub(2) end
    if o.digits and #s < o.digits then s = string.rep("0", o.digits - #s) .. s end
    if minus then s = "-" .. s end
    local gap = o.gap or 0
    local tw = #s * (cw + gap) - gap
    local x, y = place(o, o.w or tw, ch)
    if o.w then
      if align == "center" then x = x + (o.w - tw) // 2 elseif align == "right" then x = x + o.w - tw end
    end
    for i = 1, #s do
      local c = s:byte(i)
      local d = c == 45 and 10 or c - 48
      hud.cg(x + (i - 1) * (cw + gap), y, cg, { src = { d * cw, 0, cw, ch }, tint = o.tint })
    end
    return x, y, tw, ch
  end
  local s = tostring(value)
  if o.digits and #s < o.digits then s = string.rep("0", o.digits - #s) .. s end
  local tw = text_w(s, v.count)
  local x, y = place(o, o.w or tw, v.count.size)
  hud.text(x, y, s, with(v.count, o.w and { w = o.w, align = align } or {}))
  return x, y, o.w or tw, v.count.size
end

-- ---- Filters ----

local function check_state(st, where)
  if type(st) ~= "table" then error(where .. ": expected a filter state table", 3) end
  for _, f in ipairs({ "type", "sub", "class", "lock", "equip", "chara" }) do
    local val = st[f]
    if val ~= nil and math.type(val) ~= "integer" then error(where .. ": " .. f .. " is a whole number", 3) end
  end
end

-- One toggle: lit when nothing is picked or it is the one picked.
local function toggle(lit, dim, x, y, w, h, on, id, label, picked)
  local over, clicked = interact(id, x, y, w, h, false)
  ui.draw(on and lit or dim, x, y, w, h)
  if over then cover(x, y, w, h) end
  show("filter", label, label, x, y, w, h, { selected = picked })
  return clicked
end

local FILTER_STYLES = { vault = true, shop = true, equip = true }

---The vault's filter column: the categories (Special, Weapon, Armor, Item / Other), the classes,
---Locked / Unlocked, Equippable / Unequippable (style "equip"), and the sub-filters of the picked
---category above the list. state holds type (0, 21, 1, 11 or 17), sub, class (0 to 8), lock (0, 1
---or 2), equip (0, 1 or 2); a click picks one, a second click on it clears it. opts: x, y (the
---categories' top left), id, style ("vault", "shop" or "equip": the column painted in that
---panel, and its groups), back (the column's grey, default true), classes, lock, equip (false
---leaves them out), sub ({x, y} for the sub-filters, default x - 407, y - 153; false leaves them
---out). Returns state and whether it changed.
function ui.filters(state, o)
  o = o or {}
  state = state or {}
  check_state(state, "uikit.filters")
  local style = o.style or "vault"
  if not FILTER_STYLES[style] then error("uikit.filters: style is \"vault\", \"shop\" or \"equip\"", 2) end
  local f = T("filters")
  local key = o.id ~= nil and tostring(o.id) or "filters"
  local show_lock = o.lock ~= false and style ~= "shop"
  local show_equip = o.equip ~= false and style == "equip"
  local x, y = place(o, 75, show_equip and 500 or show_lock and 410 or 326)
  local changed = false
  local back = (f.backs and f.backs[style]) or f.back
  if o.back ~= false and back then ui.draw(back, x + back.x, y + back.y) end
  local ty, sub, cl, lk, eq = state.type or 0, state.sub or 0, state.class or 0, state.lock or 0, state.equip or 0
  for i, c in ipairs(f.cats) do
    local on = ty == 0 or ty == c.type
    local id = id_of({ id = key .. ".cat." .. c.type }, "filter")
    if toggle(c.lit, c.dim, x, y + 34 * (i - 1), 73, c.h, on, id, key .. "-" .. c.name, ty == c.type) then
      ty = ty == c.type and 0 or c.type
      sub, changed = 0, true
    end
  end
  if o.classes ~= false then
    for i = 1, 8 do
      local c = f.classes[i]
      local on = cl == 0 or cl == i
      local id = id_of({ id = key .. ".class." .. i }, "filter")
      if toggle(c.lit, c.dim, x + 37 * ((i - 1) % 2), y + 190 + 34 * ((i - 1) // 2), 36, 30, on, id,
          key .. "-class-" .. i, cl == i) then
        cl = cl == i and 0 or i
        changed = true
      end
    end
  end
  if show_lock then
    for i = 1, 2 do
      local c = f.lock[i]
      local on = lk == 0 or lk == i
      local id = id_of({ id = key .. ".lock." .. i }, "filter")
      if toggle(c.lit, c.dim, x, y + 346 + 34 * (i - 1), 73, 30, on, id, key .. "-" .. c.name, lk == i) then
        lk = lk == i and 0 or i
        changed = true
      end
    end
  end
  if show_equip then
    for i = 1, 2 do
      local c = f.equip[i]
      local on = eq == 0 or eq == i
      local id = id_of({ id = key .. ".equip." .. i }, "filter")
      if toggle(c.lit, c.dim, x, y + 434 + 34 * (i - 1), 73, 30, on, id, key .. "-" .. c.name, eq == i) then
        eq = eq == i and 0 or i
        changed = true
      end
    end
  end
  local sp = f.sub[ty]
  if sp and o.sub ~= false then
    local px, py
    if type(o.sub) == "table" then px, py = o.sub[1] or o.sub.x, o.sub[2] or o.sub.y
    else px, py = x - 407, y - 153 end
    ui.draw(f.sub.base, px, py)
    hud.text(px, py, sp.title, with(f.sub.title, { w = 381, h = 32, align = "center", valign = "center" }))
    for k, cg in ipairs(sp.cgs) do
      local lit, dim = pair(cg, 43, 91)
      local on = sub == 0 or sub == k
      local id = id_of({ id = key .. ".sub." .. k }, "filter")
      if toggle(lit, dim, px + 334 - 47 * (k - 1), py + 32, 43, 91, on, id, key .. "-" .. sp.names[k], sub == k) then
        sub = sub == k and 0 or k
        changed = true
      end
    end
    if inside(px, py, 381, 127) then
      frame.over = true
      hud.block_game_input()
    end
  end
  if inside(x - 2, y - 2, 77, (show_equip and 500 or show_lock and 414 or 330)) then
    frame.over = true
    hud.block_game_input()
  end
  if changed then
    state.type, state.sub, state.class, state.lock, state.equip = ty, sub, cl, lk, eq
    play("click")
  end
  return state, changed
end

local function ii_of(v)
  if type(v) == "table" then return v.ii or v.id end
  return v
end

---The items of a list that pass a filter state, as the vault tests them (アイテム倉庫_一覧_*).
---list holds ii numbers, item_data rows or rq.items() entries. state.equip needs state.chara.
---Returns a new list.
function ui.item_filter(list, state)
  if type(list) ~= "table" then error("uikit.item_filter: expected a list", 2) end
  state = state or {}
  check_state(state, "uikit.item_filter")
  local ty, sub, cl, lk = state.type or 0, state.sub or 0, state.class or 0, state.lock or 0
  local eq, ch = state.equip or 0, state.chara or 0
  local out = {}
  for _, v in ipairs(list) do
    local ii = ii_of(v)
    if math.type(ii) == "integer" and ii > 0
        and (ty <= 0 or call("アイテム倉庫_一覧_種類確認", ii, ty, sub))
        and (cl <= 0 or call("アイテム倉庫_一覧_職業確認", ii, cl))
        and (eq <= 0 or ch <= 0 or call("アイテム倉庫_一覧_装備タイプ確認", ii, eq, ch))
        and (lk <= 0 or call("アイテム倉庫_一覧_ＬＯＣＫタイプ確認", ii, lk)) then
      out[#out + 1] = v
    end
  end
  return out
end

-- ---- Sorting ----

-- T_ItemList::sort: the vault's sort modes and their comparers.
local SORTS = {
  [1] = "compItem_OriginalNo", [2] = "compItem_Attack", [3] = "compItem_Defense", [4] = "compItem_Rank",
  [5] = "compItem_Syllabary", [11] = "compItem_Attack", [12] = "compItem_Magic", [13] = "compItem_Hit",
  [14] = "compItem_Critical", [15] = "compItem_Shock", [16] = "compItem_Speed", [17] = "compItem_Defense",
  [18] = "compItem_Resist", [19] = "compItem_Avoid", [20] = "compItem_Stand", [21] = "compItem_Hp",
  [22] = "compItem_ResistFire", [23] = "compItem_ResistIce", [24] = "compItem_ResistThunder",
  [25] = "compItem_ResistLight", [26] = "compItem_ResistDarkness", [27] = "compItem_RankUp",
  [28] = "compItem_RankDown",
}

---Sorts a list in place the way the vault sorts: mode 1 (the default: by item number), or 11 to
---28 (the vault's "Default Sort" menu: Attack, Magic, ... High Enhancement Rank). Ties keep the
---item-number order. Returns the list. Runs the game's comparer for each pair: not every update.
function ui.item_sort(list, mode)
  if type(list) ~= "table" then error("uikit.item_sort: expected a list", 2) end
  mode = mode or 1
  local cmp = SORTS[mode]
  if not cmp then error("uikit.item_sort: no sort mode " .. tostring(mode), 2) end
  table.sort(list, function(a, b)
    local x, y = ii_of(a), ii_of(b)
    if x == y then return false end
    local c = call(cmp, x, y)
    if c == 0 and cmp ~= "compItem_OriginalNo" then c = call("compItem_OriginalNo", x, y) end
    return c < 0
  end)
  return list
end

---The vault's sort modes for its "Default Sort" menu, 11 to 28: {mode, name} each, with the
---game's names.
function ui.item_sorts()
  local out = {}
  for m = 11, 28 do out[#out + 1] = { mode = m, name = call("アイテム倉庫_ソート名", m) } end
  return out
end

-- ---- The picker ----

-- The places (アイテム_存在) of each list, and the row style and filter column each has.
local WHERE = {
  vault = { places = { 7 }, style = "vault" },
  storage = { places = { 11 }, style = "storage" },
  both = { places = { 7, 11 }, style = "vault" },
  shop = { places = { 6 }, style = "shop", filters = "shop" },
  quest = { places = { 8 }, style = "vault", unsorted = true },
}

-- The sort mode every store starts with and saves (アイテムソートモード, kept in the save):
-- 1 or 11 to 28.
local function saved_sort()
  local m = call("アイテムソートモード")
  return SORTS[m] and (m == 1 or m > 10) and m or 1
end

-- Under a list's filter column: the rest of the column's grey, down to the panel's foot (the
-- panels are 768 high), and the store's SORT (T_StoreItem: a T_Sort of "Default Sort" and the
-- short name of the mode, its bottom right on 1020,764, the mode in use framed). "Default Sort"
-- sorts by item number; the second button opens the game's sort menu, current or not. A new
-- mode is saved, as the game's stores do. x, y: the list's top left.
local function store_sort(st, key, style, x, y)
  local b = T("filters").backs[style]
  local top = b.src[2] + b.src[4]
  if top < 768 then
    hud.cg(x + 407 + b.x, y + 154 + b.y + b.src[4], b.cg, { src = { b.src[1], top, b.src[3], 768 - top } })
  end
  local _, _, pressed = ui.sort({ "Default Sort", call("アイテム倉庫_ソート名_short", st.sort) },
    st.sort > 10 and 2 or 1, { x = x + 405, y = y + 682, back = false, again = 2, id = key .. "-sort" })
  local function set(m)
    st.sort = m
    call("アイテムソートモード_設定", m)
  end
  if pressed == 1 and st.sort ~= 1 then
    set(1)
  elseif pressed == 2 then
    local rows = {}
    for _, s in ipairs(ui.item_sorts()) do rows[#rows + 1] = { id = s.mode, text = s.name } end
    rq.ui.choose(rows, function(m)
      if SORTS[m] and m > 10 then set(m) end
    end)
  end
end

-- The instances in those places, as the vault lists them (アイテム倉庫_一覧: 1 to アイテム_数).
local function items_in(places)
  local want = {}
  for _, p in ipairs(places) do want[p] = true end
  local out = {}
  for ii = 1, call("アイテム_数") do
    if want[call("アイテム_存在", ii)] then out[#out + 1] = ii end
  end
  return out
end

---An item picker over the player's items, laid out as the vault: the list with its scroll bar,
---the filter column 407 right and 154 down, the count over it, and the store's SORT under the
---column ("Default Sort" and the mode's short name, 405 right and 682 down). opts: x, y, id
---(keeps the filters, sort and scroll), where ("vault", "storage", "both", "shop" or "quest"),
---chara (a character: the column gets Equippable / Unequippable for them), shown, style, sort (a
---mode; default the stores' saved one), keys. Returns the ii picked on this update, else nil, then
---the current ii.
function ui.item_picker(o)
  o = o or {}
  local wh = WHERE[o.where or "vault"]
  if not wh then error("uikit.item_picker: where is \"vault\", \"storage\", \"both\", \"shop\" or \"quest\"", 2) end
  if o.chara ~= nil and math.type(o.chara) ~= "integer" then error("uikit.item_picker: chara is a character number", 2) end
  local key = o.id ~= nil and tostring(o.id) or "picker"
  local st = ui.state("picker:" .. key)
  st.filter = st.filter or { type = 0, sub = 0, class = 0, lock = 0, equip = 0 }
  st.filter.equip = st.filter.equip or 0
  st.filter.chara = o.chara
  if o.sort ~= nil and not SORTS[o.sort] then error("uikit.item_picker: no sort mode " .. tostring(o.sort), 2) end
  -- As the game's lists (T_ItemList::sort), a mode under 10 is the default.
  st.sort = st.sort or (o.sort and (o.sort < 10 and 1 or o.sort)) or saved_sort()
  local x, y = place(o, 402 + 5 + 75, (o.shown or 6) * 128)
  local sig = (o.where or "vault") .. ":" .. st.filter.type .. ":" .. st.filter.sub .. ":" .. st.filter.class .. ":"
    .. st.filter.lock .. ":" .. st.filter.equip .. ":" .. tostring(o.chara) .. ":" .. st.sort .. ":" .. changes
  if st.sig ~= sig or not st.list or (st.stamp and rq.time_ms() - st.stamp > 2000) then
    local list = ui.item_filter(items_in(wh.places), st.filter)
    if not (wh.unsorted and st.sort == 1) then ui.item_sort(list, st.sort) end
    st.list, st.sig, st.stamp = list, sig, rq.time_ms()
    st.index = nil
    if st.ii then
      for i, ii in ipairs(list) do if ii == st.ii then st.index = i end end
    end
  end
  if st.sorted_as ~= st.sort then
    -- A new sort goes back to the top, as T_StoreListManager::sort does.
    if st.sorted_as then ui.state("itemlist:" .. key .. "-list").top = 0 end
    st.sorted_as = st.sort
  end
  local picked
  -- With a category's sub-filters up over the first row, the list starts a row lower, as the
  -- vault's does, and its bar stays whole.
  local shown = o.shown or 6
  local down = (T("filters").sub[st.filter.type] and 1 or 0) * 128
  local index, changed, _, _, chosen = ui.item_list(st.list, st.index, { x = x, y = y + down, id = key .. "-list",
    shown = shown - down // 128, style = o.style or (o.chara and "equip") or wh.style, bar = { y, shown * 128 }, keys = o.keys })
  if changed then
    st.index = index
    st.ii = st.list[index]
  end
  if chosen and index then picked = st.list[index] end
  local fstyle = o.chara and "equip" or wh.filters or "vault"
  local fx, fy = x + 407, y + 154
  local _, fchanged = ui.filters(st.filter, { x = fx, y = fy, id = key, style = fstyle })
  if fchanged then ui.state("itemlist:" .. key .. "-list").top = 0 end
  if fstyle ~= "shop" then
    ui.draw(T("vault").count_box, x + 416, y + 28)
    ui.number(#st.list, { x = x + 419, y = y + 31, w = 39 })
  end
  store_sort(st, key, fstyle, x, y)
  return picked, st.ii
end

-- ---- The storage ----

---The game's storage screen: the storage's red list on the left and the vault's list right of
---it (437 apart, as the game places them), each with its scroll bar, the vault's count and the
---storage's in their boxes, and over the vault's half its filter column and SORT. As in the game,
---the filters and the sort apply to both lists, and the counts are of each list as filtered. Drag a
---row from one list to the other to move the item (rq.item.store). opts: x, y (the storage list's
---top left, default 101, 0), id (keeps the filters, sort and scroll: rq.uikit.state("storage:" ..
---id) has filter and sort), shown. Returns the ii moved on this update and where it went ("vault"
---or "storage"), else nil.
function ui.item_storage(o)
  o = o or {}
  local key = o.id ~= nil and tostring(o.id) or "storage"
  local st = ui.state("storage:" .. key)
  st.filter = st.filter or { type = 0, sub = 0, class = 0, lock = 0 }
  st.sort = st.sort or saved_sort()
  local shown = o.shown or 6
  local x, y = place({ x = o.x or 101, y = o.y or 0 }, 437 + 402 + 5 + 75, shown * 128)
  local f = st.filter
  local sig = changes .. ":" .. (f.type or 0) .. ":" .. (f.sub or 0) .. ":" .. (f.class or 0) .. ":"
    .. (f.lock or 0) .. ":" .. st.sort
  if st.sig ~= sig or not st.vault or rq.time_ms() - (st.stamp or 0) > 2000 then
    -- In the game's order: the store's sort mode (アイテム倉庫_一覧 makes both lists by item
    -- number, then T_StoreList sorts them with the saved mode).
    st.vault = ui.item_sort(ui.item_filter(items_in({ 7 }), f), st.sort)
    st.storage = ui.item_sort(ui.item_filter(items_in({ 11 }), f), st.sort)
    st.sig, st.stamp = sig, rq.time_ms()
  end
  local group = "storage:" .. key
  local sid, vid = key .. "-storage", key .. "-vault"
  if st.sorted_as ~= st.sort then
    -- A new sort goes back to the top, as T_StoreListManager::sort does.
    if st.sorted_as then
      ui.state("itemlist:" .. sid).top = 0
      ui.state("itemlist:" .. vid).top = 0
    end
    st.sorted_as = st.sort
  end
  st.si = ui.item_list(st.storage, st.si, { x = x, y = y, id = sid, shown = shown, style = "storage", drag = group })
  local vx = x + 437
  -- With a category's sub-filters up over the vault list's first row, that list starts a row lower.
  local down = (T("filters").sub[f.type or 0] and 1 or 0) * 128
  st.vi = ui.item_list(st.vault, st.vi, { x = vx, y = y + down, id = vid, shown = shown - down // 128,
    style = "vault", drag = group, bar = { y, shown * 128 } })
  local _, fchanged = ui.filters(f, { x = vx + 407, y = y + 154, id = key })
  if fchanged then
    ui.state("itemlist:" .. sid).top = 0
    ui.state("itemlist:" .. vid).top = 0
  end
  store_sort(st, key, "vault", vx, y)
  local v = T("vault")
  ui.draw(v.count_box, vx + 416, y + 28)
  ui.number(#st.vault, { x = vx + 419, y = y + 31, w = 39 })
  ui.draw(v.stock_box, vx + 412, y + 88)
  ui.number(#st.storage, { x = vx + 419, y = y + 94, w = 39 })
  local row, _, to = ui.item_dropped(group)
  if row ~= nil then
    local ii = ii_of(row)
    local where = to == sid and "storage" or "vault"
    if math.type(ii) == "integer" and rq.item.store(ii, where) then
      st.sig = nil
      return ii, where
    end
  end
  return nil
end
