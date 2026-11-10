-- Which dungeon and which area (loader 2.24.0, notes/plans/plan-dungeon-areas.md in the workspace):
--   rq.dungeon.on_area      once per area the party enters
--   rq.dungeon.areas        the quest's area map, as the game draws it
--   area, row, col, name, kind, first and stratum on rq.dungeon.on_map's ctx, and the same (but
--   name and first) in rq.dungeon.info()
--
-- Built only on public calls: rq.ain.call, global and hook, rq.quests and rq.party.
--   ＱＭ登録(area)   the game's "enter an area": the quest's script sets the kind and the name and
--                    loads the map (ＭＡＰ読み込み), then Ｍ[area].初登録 marks the area visited
--   ◆ＢＭＡＰ        the area, 1 to 16, a cell of the quest's 4 by 4 area map (T_MapArea)
--   ◆ＭＡＰ基礎型    the map kind (ＭＡＰ基礎型登録), ◆ＭＡＰ名前 the name, ◆ＭＡＰ番号 the layout
--   FA[224]          the Malgrid Dungeon Xth Stratum's stratum
local rq = ...
local call, hook, g = rq.ain.call, rq.ain.hook, rq.ain.global
local dungeon = rq.dungeon

local AREAS = 16
-- The Malgrid Dungeon is one quest per stratum; the Xth (147) counts its own in FA[224].
local STRATA = { [121] = 1, [122] = 2, [123] = 3, [124] = 4, [125] = 5, [126] = 6, [127] = 7,
  [128] = 8, [145] = 9, [146] = 10 }
local MALGRID_X = 147

local function int(v)
  return math.type(v) == "integer"
end

local function gint(...)
  local ok, v = pcall(g, ...)
  if ok and int(v) then return v end
  return 0
end

local function yes(v)
  return v == true or v == 1
end

local function logf(fmt, ...)
  rq.log(string.format(fmt, ...))
end

local function mod_id()
  local ok, id = pcall(function() return rq.mod and rq.mod.id and rq.mod.id() end)
  return ok and id or "?"
end

local function quest_now()
  local ok, q = pcall(rq.quests.current)
  return ok and int(q) and q > 0 and q or 0
end

-- The area the party is in, 0 outside a quest or before one is entered.
local function area_now(q)
  if q <= 0 then return 0 end
  local a = gint("◆ＢＭＡＰ")
  return (a >= 1 and a <= AREAS) and a or 0
end

local function stratum(q)
  if STRATA[q] then return STRATA[q] end
  if q == MALGRID_X then
    local s = gint("FA", 224)
    return s > 0 and s or nil
  end
  return nil
end

-- The game clears the name to a full-width space at a quest's start.
local function area_name(q)
  if q <= 0 then return "" end
  local ok, s = pcall(g, "◆ＭＡＰ名前")
  if not ok or type(s) ~= "string" then return "" end
  if s:gsub("　", ""):match("^%s*$") then return "" end
  return s
end

local function first_visit(a)
  return a > 0 and gint("Ｍ", a, "初登録") == 0
end

-- area, row, col, kind and stratum into t.
local function fill(t, q, a)
  t.area = a
  t.row = a > 0 and (a - 1) // 4 + 1 or 0
  t.col = a > 0 and (a - 1) % 4 + 1 or 0
  t.kind = q > 0 and gint("◆ＭＡＰ基礎型") or 0
  t.stratum = stratum(q)
end

-- ---- on_map's ctx ----

local raw_on_map = dungeon.on_map
function dungeon.on_map(fn)
  if type(fn) ~= "function" then return raw_on_map(fn) end
  return raw_on_map(function(ctx)
    local q = int(ctx.quest) and ctx.quest or 0
    local a = area_now(q)
    fill(ctx, q, a)
    ctx.name = area_name(q)
    ctx.first = first_visit(a)
    return fn(ctx)
  end)
end

-- ---- info() ----

local raw_info = dungeon.info
function dungeon.info()
  local t = raw_info()
  if type(t) == "table" then
    local q = quest_now()
    t.quest = q
    fill(t, q, area_now(q))
  end
  return t
end

-- ---- on_area ----

local area_list = {}
local area_hooked = false
local pending = {} -- first-visit answers of the ＱＭ登録 calls under way, innermost last

local function party()
  local out = {}
  local ok, ranks = pcall(rq.party.get)
  if ok and type(ranks) == "table" then
    for _, id in ipairs(ranks) do
      if int(id) and id > 0 then out[#out + 1] = id end
    end
  end
  return out
end

local function run_area(a, first)
  local q = quest_now()
  local ok, state = pcall(rq.quests.state, q)
  local ctx = {
    quest = q,
    cleared = q > 0 and ok and state == 2,
    party = party(),
    map = gint("◆ＭＡＰ番号"),
    name = area_name(q),
    first = first,
  }
  fill(ctx, q, a)
  for _, h in ipairs({ table.unpack(area_list) }) do
    local fine, err = pcall(h.fn, ctx)
    if not fine then logf("rq.dungeon.on_area (%s): %s", h.mod, tostring(err)) end
  end
end

local function hook_area()
  if area_hooked then return end
  area_hooked = true
  hook("ＱＭ登録", {
    before = function(h)
      local a = h.args[1]
      pending[#pending + 1] = int(a) and a >= 1 and a <= AREAS and first_visit(a) or false
    end,
    after = function(h)
      local first = table.remove(pending)
      local a = h.args[1]
      if not int(a) or a < 1 or a > AREAS then return end -- the game refuses 17 and up
      if #area_list > 0 then run_area(a, first == true) end
    end,
  })
end

function dungeon.on_area(fn)
  if type(fn) ~= "function" then error("rq.dungeon.on_area: expected a function", 2) end
  hook_area()
  local h = { fn = fn, mod = mod_id() }
  area_list[#area_list + 1] = h
  return function()
    for i, x in ipairs(area_list) do
      if x == h then table.remove(area_list, i) return end
    end
  end
end

-- ---- areas() ----

-- The area map's rules, from C_クエスト.jaf; a route is drawn when either side of it is found.
function dungeon.areas()
  local q = quest_now()
  if q <= 0 then return nil end
  local here = area_now(q)
  local goal = call("クエスト_ゴールエリア表示", q)
  local out = {}
  for a = 1, AREAS do
    out[a] = {
      no = a,
      row = (a - 1) // 4 + 1,
      col = (a - 1) % 4 + 1,
      found = yes(call("クエスト_エリア表示_フラグ", q, a)),
      north = yes(call("クエスト_エリア表示_北", q, a)),
      east = yes(call("クエスト_エリア表示_東", q, a)),
      south = yes(call("クエスト_エリア表示_南", q, a)),
      west = yes(call("クエスト_エリア表示_西", q, a)),
      here = a == here,
      goal = int(goal) and a == goal,
    }
  end
  return out, gint("◆クエストエリア表示禁止") ~= 1
end
