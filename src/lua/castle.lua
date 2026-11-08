-- rq.castle: rooms made by mods in Rance Castle (loader 2.16.0).
--
-- A mod's room is one of the game's own castle areas. The game keeps six arrays of 51 slots for
-- its areas (ランス城エリア, 名, cg, cg文字列, new, ero) and uses 1 to 29; mods get 30 to 50. Each
-- time the castle opens, Ｒ８ランス城／前処理 rolls the rooms (it clears 1 to 29 and asks
-- 城内処理 for each), and T_CastleListManager::setAreaNo puts the shown areas 1 to 27 into the grid,
-- three cells a floor. Then the game builds every button, plate, face, NEW mark and arrow move from
-- those arrays and cells, and a click on a room calls Ｒ８ランス城／入室(area).
--
-- Built only on public calls: rq.ain.hook on those three functions and rq.ain.set_global.
--   after  Ｒ８ランス城／前処理      fill each room's slots (shown or not, name, face, NEW)
--   after  T_CastleListManager@setAreaNo   put each shown room in a free cell of its floor, or
--                                          of the nearest floor with one
--   before Ｒ８ランス城／入室        a mod's area: skip the game's call and run on_enter there; the
--                                    castle stays open, as the Gal monsters and Poster rooms do
local rq = ...
local call, hook, g, set = rq.ain.call, rq.ain.hook, rq.ain.global, rq.ain.set_global

-- rq.castle is the C query (rq.castle(), rq.castle.get(), since 0.2.0); this adds room and rooms to it.
local castle = rq.castle
if type(castle) ~= "table" then castle = {}; rq.castle = castle end

local FIRST, LAST = 30, 50
local FLOORS = { "Roof", "6F", "5F", "4F", "3F", "2F", "1F", "B1F", "B2F" }
local ROWS, COLS = 9, 3

local rooms = {}     -- in the order they were made
local by_area = {}   -- area -> room
local hooked = false

local function logf(fmt, ...)
  rq.log("rq.castle: " .. string.format(fmt, ...))
end

-- Runs one of a room's callbacks; an error is logged with the mod and the id.
local function run(room, what, fn, ...)
  local r = table.pack(pcall(fn, ...))
  if not r[1] then
    logf("%s %s %s: %s", room.mod, room.id, what, tostring(r[2]))
    return nil
  end
  return table.unpack(r, 2, r.n)
end

local function floor_row(f)
  if math.type(f) == "integer" and f >= 1 and f <= ROWS then return f - 1 end
  if type(f) == "string" then
    local want = f:upper()
    for i, name in ipairs(FLOORS) do
      if name:upper() == want then return i - 1 end
    end
  end
  return nil
end

local function face_cg(room)
  local f = room.face
  if type(f) == "function" then f = run(room, "face", f) end
  if math.type(f) ~= "integer" or f <= 0 then return 0 end
  if room.face_is_chara then
    local ok, cg = pcall(call, "Ｃ顔ＣＧ", f, -1)
    return ok and math.type(cg) == "integer" and cg > 0 and cg or 0
  end
  return f
end

local function is_shown(room)
  if room.removed then return false end
  if room.shown == nil then return true end
  return run(room, "shown", room.shown) and true or false
end

local function fill(room)
  local a = room.area
  local shown = is_shown(room)
  room.on = shown
  room.cell = nil
  set("ランス城エリア", a, shown and 1 or 0)
  set("ランス城エリア名", a, room.name)
  set("ランス城エリアcg", a, shown and face_cg(room) or 0)
  set("ランス城エリアcg文字列", a, "")
  local new = room.new
  if type(new) == "function" then new = run(room, "new", new) end
  set("ランス城エリアnew", a, (shown and new) and 1 or 0)
  set("ランス城エリアero", a, 0)
end

local function cell_free(idx)
  return g("g_sCastle", "m_sList", "m_anAreaNo", idx) <= 0
end

-- The floor's first free cell, else the nearest floor's (the one below first on a tie).
local function place(room)
  for d = 0, ROWS - 1 do
    for _, row in ipairs(d == 0 and { room.row } or { room.row + d, room.row - d }) do
      if row >= 0 and row < ROWS then
        for col = 0, COLS - 1 do
          local idx = row * COLS + col
          if cell_free(idx) then
            set("g_sCastle", "m_sList", "m_anAreaNo", idx, room.area)
            room.cell = idx
            return true
          end
        end
      end
    end
  end
  return false
end

local function install()
  hooked = true
  hook("Ｒ８ランス城／前処理", { after = function()
    for _, room in ipairs(rooms) do fill(room) end
  end })
  hook("T_CastleListManager@setAreaNo", { after = function(h)
    local n = 0
    for _, room in ipairs(rooms) do
      if room.on and not room.removed then
        if place(room) then
          n = n + 1
        else
          logf("%s %s: no free cell on any floor this time", room.mod, room.id)
        end
      end
    end
    if n > 0 and math.type(h.result) == "integer" then h.result = h.result + n end
  end })
  hook("Ｒ８ランス城／入室", { before = function(h)
    local room = by_area[h.args[1]]
    if not room then return end
    h.skip = true
    if not room.removed then run(room, "on_enter", room.on_enter, room.handle) end
  end })
end

local function check_spec(spec)
  if type(spec) ~= "table" then error("rq.castle.room: spec must be a table", 3) end
  if type(spec.name) ~= "string" or spec.name == "" then error("rq.castle.room: name must be a non-empty string", 3) end
  if type(spec.on_enter) ~= "function" then error("rq.castle.room: on_enter must be a function", 3) end
  if spec.shown ~= nil and type(spec.shown) ~= "function" then error("rq.castle.room: shown must be a function", 3) end
  if spec.new ~= nil and type(spec.new) ~= "boolean" and type(spec.new) ~= "function" then
    error("rq.castle.room: new must be a boolean or a function", 3)
  end
  if spec.face ~= nil and math.type(spec.face) ~= "integer" and type(spec.face) ~= "function" then
    error("rq.castle.room: face must be a CG number or a function", 3)
  end
  if spec.chara ~= nil and math.type(spec.chara) ~= "integer" then error("rq.castle.room: chara must be a character id", 3) end
  local row = floor_row(spec.floor == nil and "B2F" or spec.floor)
  if not row then error("rq.castle.room: floor must be 1 to 9 or one of Roof, 6F .. 1F, B1F, B2F", 3) end
  return row
end

function castle.room(spec)
  local row = check_spec(spec)
  local area = FIRST + #rooms
  if area > LAST then error("rq.castle.room: no area left (" .. (LAST - FIRST + 1) .. " rooms at most)", 2) end
  local mod = rq.mod and rq.mod.id and rq.mod.id() or "?"
  local room = {
    area = area, row = row, mod = mod or "?", id = spec.id or spec.name, name = spec.name,
    shown = spec.shown, new = spec.new, on_enter = spec.on_enter,
    face = spec.chara or spec.face, face_is_chara = spec.chara ~= nil,
  }
  local handle = {}
  function handle.area() return room.area end
  function handle.floor()
    if room.cell then return FLOORS[room.cell // COLS + 1] end
  end
  function handle.where()
    local idx = room.cell
    if not idx or g("g_sCastle", "m_sList", "m_anAreaNo", idx) ~= room.area then return nil end
    local row2, col = idx // COLS, idx % COLS
    return { floor = FLOORS[row2 + 1], row = row2 + 1, col = col + 1,
      x = 145 + 286 * col + 3, y = 82 + 55 * row2 + 3, w = 207, h = 30 }
  end
  function handle.set(opts)
    if type(opts) ~= "table" then error("rq.castle room set: opts must be a table", 2) end
    if opts.name ~= nil then
      if type(opts.name) ~= "string" or opts.name == "" then error("rq.castle room set: name must be a non-empty string", 2) end
      room.name = opts.name
    end
    if opts.shown ~= nil then room.shown = opts.shown end
    if opts.new ~= nil then room.new = opts.new end
    if opts.floor ~= nil then
      local r = floor_row(opts.floor)
      if not r then error("rq.castle room set: bad floor", 2) end
      room.row = r
    end
  end
  function handle.remove()
    room.removed = true
    pcall(set, "ランス城エリア", room.area, 0)
  end
  room.handle = handle
  rooms[#rooms + 1] = room
  by_area[area] = room
  if not hooked then install() end
  return handle
end

-- Every mod room, for agents and the Lab: { id, mod, name, area, shown, floor, col }.
function castle.rooms()
  local out = {}
  for _, room in ipairs(rooms) do
    if not room.removed then
      local w = room.handle.where()
      out[#out + 1] = { id = room.id, mod = room.mod, name = room.name, area = room.area,
        shown = room.on == true, floor = w and w.floor, col = w and w.col }
    end
  end
  return out
end
