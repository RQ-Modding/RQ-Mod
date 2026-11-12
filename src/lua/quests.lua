-- Quests and map objects (loader 2.39.0, notes/plans/plan-quests-map-objects.md in the workspace):
--   rq.quests.get, edit                  a quest's data as the quest list shows it; a mod's patch on it
--   rq.quests.run, set_run               the counters, bonuses and rules of the quest being played
--   rq.dungeon.objects, remove           the objects on the current map
--   rq.dungeon.place                     also the game's trap, placed enemy and escape device, and a
--                                        mod's own object ("custom")
--   rq.dungeon.on_object, looks          a mod's own object: its callback, the props it can look like
--   rq.dungeon.traps, set_traps, on_trap the trap kinds' weights, and a trap stepped on
-- Loader 2.41.0 (notes/plans/plan-new-quests.md):
--   rq.quests.define, no                 a mod's own quest with no map: a number from 303 to 500 in
--                                        the save, its data, its script (run) and how it opens
--   the quest calls take a defined quest's id; get's text_id and main; the callbacks' ctx.id
-- Loader 2.42.0 (notes/plans/plan-new-dungeons.md):
--   rq.quests.define's map              areas on the game's 4 by 4 grid, each a stock room of one
--                                        of its tilesets or one of its fixed maps, set up as its
--                                        quest scripts set theirs; get's map; set_run's progress
--
-- Built only on public calls: rq.ain.call, global, set_global, global_len, hook and stack.
--   Ｑ[no]                    a quest: 名前, 説明文[1..5], 発生場所, 特殊な条件, 状態, 種類, 難易度,
--                             参加回数, クリア回数, 取得名声値, 参加条件Ａ/Ｂ/Ｃ[1..3], メイン, キット,
--                             チェック (a mod's quest's mark: "rq:" and its id)
--   Ｑ初期化(no)              the entry as new (the game's own reset of 1 to 301)
--   Ｑ状態(no, 1)             opens a locked quest, adding it to the victory screen's new quests
--   クエスト処理／基本(8, …)  kit 0's script at the quest's start: a mod's quest runs its run there
--   ワールド切り替え処理      the world transfer, which resets quests 1 to 301 only
--   Ｑ起動時登録              writes every quest's data (the start, after a load, a world transfer)
--   Ｑ特殊な条件の処理        the story's special conditions, each time the quest menu opens
--   ◆クエスト…               the run's counters; ◆クエストボーナス有無, ◆クエストボーナス and
--                             ◆クエストボーナス最大 [1 feats, 2 level-ups, 3 placed enemies]
--   Ｍ[◆ＢＭＡＰ].maptt, maptz, maptv, mapnn [xx][yy]   a cell's object: code, number, rank, flag;
--                             xx = x + 1, yy = 50 - z in rq.dungeon.grid's cells
--   ＭＤ固定                  writes a cell and places its prop; the game calls it again for every
--                             cell when the party enters an area it visited
--   イベント処理              a map event: the cell's number, the object removed
--   クエスト処理(9, no, 4)    the quest's handling of map event `no`
--   トラップ処理, トラップ確定, RAND(100)   a trap: the kind drawn, then the disarm roll
--   クエスト処理(7, area)     an area entered (ＱＭ登録): the quest's settings, ＭＡＰ読み込み (on the
--                             dispatcher: ＱＭ登録 runs inside the hook on 基本's moments 8 and 9)
--   ＭＡＰ読み込み            the area's room (drawn from the tileset's stock rooms by the exits, or
--                             the one kept in Ｑ[no].Ｍ固有番号記憶), its exits and counted objects
--   発生ＭＸ/ＭＹ/ＭＰ, 広域…  the room's spots (mapd), 1 free, 2 taken; ＭＤランダム takes them
--   クエスト処理／基本(9, …, fg)  the map's moments; 割り込み２(fg) the game's own answer
--   Ｔマップ割り込み          the map hands over to the quest's script (a fight, a touch, the menu)
local rq, priv = ...
local call, hook, g, set = rq.ain.call, rq.ain.hook, rq.ain.global, rq.ain.set_global
local quests, dungeon = rq.quests, rq.dungeon

local function int(v)
  return math.type(v) == "integer"
end

local function gint(...)
  local ok, v = pcall(g, ...)
  if ok and int(v) then return v end
  return 0
end

local function gstr(...)
  local ok, v = pcall(g, ...)
  if ok and type(v) == "string" then return v end
  return ""
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

-- Characters, as the game counts them (字数制限 cuts by characters).
local function chars(s)
  return utf8.len(s)
end

local function quest_now()
  local ok, q = pcall(quests.current)
  return ok and int(q) and q > 0 and q or 0
end

-- A list of handlers with the mod that added each; add returns the remover.
local function handlers()
  local list = {}
  local function add(fn)
    local h = { fn = fn, mod = mod_id() }
    list[#list + 1] = h
    return function()
      for i, x in ipairs(list) do
        if x == h then table.remove(list, i) return end
      end
    end
  end
  return list, add
end

-- The mods' load order (rq.mod.list), for patches made by several mods.
local function load_rank()
  local rank = {}
  local ok, ids = pcall(rq.mod.list)
  if ok and type(ids) == "table" then
    for i, id in ipairs(ids) do rank[id] = i end
  end
  return rank
end

-- A mod's quest's map (2.42.0), in the section at the end of the file.
local map_view, norm_map, map_objects, map_moment

-- The function that called `name`, from the AIN stack (the hooked function may be on it).
local function caller_of(name)
  local ok, st = pcall(rq.ain.stack, 6)
  if not ok or type(st) ~= "table" then return nil end
  for _, n in ipairs(st) do
    if n ~= name then return n end
  end
  return nil
end

-- ======================================================================================
-- rq.quests.get and edit
-- ======================================================================================

local LINES, SLOTS = 5, 3
local KIND_NAME = { [0] = "quest", [2] = "fights", [3] = "menu" }
local COND = { party_max = 1, member = 2, own = 3, member_level = 4, item = 5, special = 6 }
local NAME_MAX, PLACE_MAX, SPECIAL_MAX = 40, 100, 50

local function quest_count()
  local ok, n = pcall(rq.ain.global_len, "Ｑ")
  return ok and int(n) and n or 0
end

local function has_quest(no)
  return int(no) and no >= 1 and no < quest_count() and gstr("Ｑ", no, "名前") ~= ""
end

-- The game's quests are in Ｑ once a game is started or loaded (empty at the title).
local function data_ready()
  local ok, list = pcall(quests)
  return ok and type(list) == "table" and #list > 0
end

-- ---- A mod's quests (2.41.0): numbers 303 to 500, each marked in Ｑ[n].チェック ----
-- The mark is "rq:<id>" for a quest in the list, "rq<state>:<id>" for one whose mod is gone (its
-- state kept there, the quest at state 0). The game never touches 302 to 500, nor チェック.

local FIRST_NO, LAST_NO = 303, 500
local defs, def_list = {}, {} -- id -> the definition; the ids in the order they were defined
local no_cache = {}           -- id -> its number in the loaded save

local function parse_mark(s)
  local st, id = s:match("^rq(%d?):(.+)$")
  if not id then return nil end
  return id, st ~= "" and tonumber(st) or nil
end

local function mod_no(no)
  return int(no) and no >= FIRST_NO and no <= LAST_NO and no < quest_count()
end

-- The id marked on a number (nil for the game's quests), and the state a hidden mark keeps.
local function id_at(no)
  if not mod_no(no) then return nil end
  return parse_mark(gstr("Ｑ", no, "チェック"))
end

local function scan()
  no_cache = {}
  for n = FIRST_NO, math.min(LAST_NO, quest_count() - 1) do
    local id = id_at(n)
    if id and not no_cache[id] then no_cache[id] = n end
  end
end

-- An id's number in the loaded save, or nil. The marks are read at each registration of the
-- game's quests (a new game, a load, a world transfer), so a miss there is a miss.
local function no_of(id)
  local n = no_cache[id]
  if not n then return nil end
  if id_at(n) == id then return n end
  scan()
  return no_cache[id]
end

-- A number or a quest's id as its number; an error names the call.
local function resolve(v, name)
  if type(v) ~= "string" then return v end
  local n = no_of(v)
  if n then return n end
  if defs[v] then
    error(name .. ": quest " .. v .. " has no number: " ..
      (data_ready() and "no free number from 303 to 500 in this save" or "no save is loaded"), 3)
  end
  error(name .. ": no quest " .. v, 3)
end

-- The fields a patch writes, as the game holds them.
local function read_raw(no)
  local d = {
    name = gstr("Ｑ", no, "名前"),
    place = gstr("Ｑ", no, "発生場所"),
    special = gstr("Ｑ", no, "特殊な条件"),
    difficulty = gint("Ｑ", no, "難易度"),
    desc = {},
    conds = {},
  }
  for i = 1, LINES do d.desc[i] = gstr("Ｑ", no, "説明文", i) end
  for i = 1, SLOTS do
    d.conds[i] = { gint("Ｑ", no, "参加条件Ａ", i), gint("Ｑ", no, "参加条件Ｂ", i), gint("Ｑ", no, "参加条件Ｃ", i) }
  end
  return d
end

local COND_FIELD = { "参加条件Ａ", "参加条件Ｂ", "参加条件Ｃ" }

local function write_raw(no, d)
  local cur = read_raw(no)
  if cur.name ~= d.name then set("Ｑ", no, "名前", d.name) end
  if cur.place ~= d.place then set("Ｑ", no, "発生場所", d.place) end
  if cur.special ~= d.special then set("Ｑ", no, "特殊な条件", d.special) end
  if cur.difficulty ~= d.difficulty then set("Ｑ", no, "難易度", d.difficulty) end
  for i = 1, LINES do
    if cur.desc[i] ~= d.desc[i] then set("Ｑ", no, "説明文", i, d.desc[i]) end
  end
  for i = 1, SLOTS do
    for k = 1, 3 do
      if cur.conds[i][k] ~= d.conds[i][k] then set("Ｑ", no, COND_FIELD[k], i, d.conds[i][k]) end
    end
  end
end

local function copy_raw(d)
  local c = { name = d.name, place = d.place, special = d.special, difficulty = d.difficulty, desc = {}, conds = {} }
  for i = 1, LINES do c.desc[i] = d.desc[i] end
  for i = 1, SLOTS do c.conds[i] = { d.conds[i][1], d.conds[i][2], d.conds[i][3] } end
  return c
end

-- One condition slot as a table, with the quest list's text and whether it is met now.
local function cond_table(no, slot, a, b, c)
  local t
  if a == 1 then t = { kind = "party_max", count = b }
  elseif a == 2 then t = { kind = "member", chara = b }
  elseif a == 3 then t = { kind = "own", charas = { b, c } }
  elseif a == 4 then t = { kind = "member_level", chara = b, level = c }
  elseif a == 5 then t = { kind = "item", item = b }
  elseif a == 6 then t = { kind = "special" }
  else t = { kind = "other", code = a, b = b, c = c } end
  local ok, text = pcall(call, "クエスト_参加条件", no, slot)
  t.text = ok and type(text) == "string" and text or ""
  local okm, met = pcall(call, "クエスト_参加条件_有効", no, slot)
  t.met = okm and yes(met) or false
  return t
end

-- A patch's key: a game quest's number, or a mod's quest's id (its number differs between saves).
local patches = {} -- key -> { { mod, seq, patch }, ... }
local base = {}    -- number -> the game's own fields (read_raw), while a patch is on the quest
local seq = 0

local function key_of(no)
  local id = id_at(no)
  if id and defs[id] then return id end
  return no
end

local function key_no(key)
  if type(key) == "string" then return no_of(key) end
  return key
end

local function world_add()
  local w = gint("◆ワールド")
  return w >= 2 and (w - 1) * 2 or 0
end

-- A mod's quest's fields as its definition gives them, the world's addition on the difficulty
-- as Ｑ初期セット adds it to the game's (+2 per world after the first, when not 0).
local function def_raw(id)
  local d = copy_raw(defs[id].raw)
  if d.difficulty ~= 0 then d.difficulty = d.difficulty + world_add() end
  return d
end

local function ordered(key)
  local list = patches[key]
  if not list or #list == 0 then return nil end
  local rank = load_rank()
  local out = {}
  for i, p in ipairs(list) do out[i] = p end
  table.sort(out, function(x, y)
    local a, b = rank[x.mod] or math.huge, rank[y.mod] or math.huge
    if a ~= b then return a < b end
    return x.seq < y.seq
  end)
  return out
end

function quests.get(no)
  no = resolve(no, "rq.quests.get")
  if not int(no) or no <= 0 then error("rq.quests.get: expected a quest number or id", 2) end
  if not has_quest(no) then error("rq.quests.get: no quest " .. no, 2) end
  local d = read_raw(no)
  local desc, last = {}, 0
  for i = 1, LINES do
    desc[i] = d.desc[i]
    if d.desc[i] ~= "" then last = i end
  end
  for i = LINES, last + 1, -1 do desc[i] = nil end
  local conds, special = {}, ""
  if d.conds[1][1] ~= 0 then -- the game's rule: an empty first slot means no conditions
    for i = 1, SLOTS do
      local c = d.conds[i]
      if c[1] ~= 0 then
        conds[#conds + 1] = cond_table(no, i, c[1], c[2], c[3])
        if c[1] == 6 then special = d.special end
      end
    end
  end
  local kind = gint("Ｑ", no, "種類")
  local edited = {}
  for _, p in ipairs(ordered(key_of(no)) or {}) do edited[#edited + 1] = p.mod end
  local text_id = id_at(no)
  local def = text_id and defs[text_id]
  return {
    id = no,
    map = def and def.map and map_view(def.map) or nil,
    text_id = text_id,
    main = gint("Ｑ", no, "メイン") == 1,
    name = d.name,
    description = desc,
    place = d.place,
    special = special,
    state = gint("Ｑ", no, "状態"),
    kind = KIND_NAME[kind] or "other",
    difficulty = d.difficulty,
    fame = gint("Ｑ", no, "取得名声値"),
    tries = gint("Ｑ", no, "参加回数"),
    clears = gint("Ｑ", no, "クリア回数"),
    conditions = conds,
    can_take = yes(call("クエスト_有効", no)),
    edited = edited,
  }
end

-- A condition table of a patch as the game's three numbers, or nil and why not.
local function cond_raw(c, where)
  if type(c) ~= "table" then return nil, where .. " must be a table" end
  local k = COND[c.kind]
  if not k then
    return nil, where .. ".kind must be party_max, member, own, member_level, item or special"
  end
  local allowed = { kind = true, text = true, met = true }
  local function whole(v, low, high)
    return int(v) and v >= low and (not high or v <= high)
  end
  local out
  if k == 1 then
    if not whole(c.count, 1, 5) then return nil, where .. ".count must be 1 to 5" end
    allowed.count, out = true, { 1, c.count, 0 }
  elseif k == 2 then
    if not whole(c.chara, 1) then return nil, where .. ".chara must be a character number" end
    allowed.chara, out = true, { 2, c.chara, 0 }
  elseif k == 3 then
    local cs = c.charas
    if type(cs) ~= "table" or not whole(cs[1], 1) or not whole(cs[2], 1) or #cs ~= 2 then
      return nil, where .. ".charas must be two character numbers"
    end
    allowed.charas, out = true, { 3, cs[1], cs[2] }
  elseif k == 4 then
    if not whole(c.chara, 1) then return nil, where .. ".chara must be a character number" end
    if not whole(c.level, 1) then return nil, where .. ".level must be a whole number, 1 or more" end
    allowed.chara, allowed.level, out = true, true, { 4, c.chara, c.level }
  elseif k == 5 then
    if not whole(c.item, 1) then return nil, where .. ".item must be an item number" end
    allowed.item, out = true, { 5, c.item, 0 }
  else
    if c.text ~= nil then
      if type(c.text) ~= "string" or not chars(c.text) or chars(c.text) > SPECIAL_MAX then
        return nil, where .. ".text must be a text of up to " .. SPECIAL_MAX .. " characters"
      end
    end
    out = { 6, 0, 0 }
  end
  for f in pairs(c) do
    if not allowed[f] then return nil, where .. " has no field " .. tostring(f) end
  end
  return out
end

local PATCH = { name = true, description = true, place = true, difficulty = true, conditions = true, condition = true }

local function check_patch(patch)
  for k in pairs(patch) do
    if not PATCH[k] then return "no field " .. tostring(k) end
  end
  local function text(v, max)
    return type(v) == "string" and chars(v) ~= nil and chars(v) <= max
  end
  if patch.name ~= nil and (not text(patch.name, NAME_MAX) or patch.name == "") then
    return "name must be a text of 1 to " .. NAME_MAX .. " characters"
  end
  if patch.place ~= nil and not text(patch.place, PLACE_MAX) then
    return "place must be a text of up to " .. PLACE_MAX .. " characters"
  end
  if patch.difficulty ~= nil and (not int(patch.difficulty) or patch.difficulty < 0 or patch.difficulty > 999) then
    return "difficulty must be a whole number from 0 to 999"
  end
  if patch.description ~= nil then
    local d = patch.description
    if type(d) ~= "table" or #d > LINES then return "description must be a list of up to " .. LINES .. " lines" end
    for i, line in pairs(d) do
      if not int(i) or i < 1 or i > LINES or type(line) ~= "string" or not chars(line) then
        return "description must be a list of up to " .. LINES .. " lines"
      end
      if line:find("\n", 1, true) then return "description: one line per entry, no line breaks" end
    end
  end
  if patch.conditions ~= nil then
    local cs = patch.conditions
    if type(cs) ~= "table" or #cs > SLOTS then return "conditions must be a list of up to " .. SLOTS end
    for i, c in pairs(cs) do
      if not int(i) or i < 1 or i > #cs then return "conditions must be a list of up to " .. SLOTS end
      local _, why = cond_raw(c, "conditions[" .. i .. "]")
      if why then return why end
    end
  end
  if patch.condition ~= nil then
    local c = patch.condition
    if type(c) ~= "table" then return "condition must be a table { text = ..., ok = function }" end
    if not text(c.text, SPECIAL_MAX) or c.text == "" then
      return "condition.text must be a text of 1 to " .. SPECIAL_MAX .. " characters"
    end
    if type(c.ok) ~= "function" then return "condition.ok must be a function" end
    for f in pairs(c) do
      if f ~= "text" and f ~= "ok" then return "condition has no field " .. tostring(f) end
    end
    if patch.conditions and #patch.conditions >= SLOTS then
      return "condition: no free condition slot (conditions holds " .. SLOTS .. ")"
    end
  end
  return nil
end

local function copy_patch(p)
  local c = {}
  for k, v in pairs(p) do
    if k == "description" then
      c[k] = {}
      for i, line in ipairs(v) do c[k][i] = line end
    elseif k == "conditions" then
      c[k] = {}
      for i, x in ipairs(v) do c[k][i] = cond_raw(x, "") end
    elseif k == "condition" then
      c[k] = { text = v.text, ok = v.ok }
    else
      c[k] = v
    end
  end
  return c
end

-- A mod's own condition is met: ok() returned a true value. An error counts as met.
local function custom_met(no, cond, mod, what)
  local ok, r = pcall(cond.ok, no)
  if not ok then
    logf("%s (%s): quest %d: condition.ok: %s", what, mod, no, tostring(r))
    return true
  end
  return r ~= nil and r ~= false
end

-- The fields under the patches: the game's own for its quests, the definition's for a mod's.
local function base_of(key, no)
  if type(key) == "string" then return def_raw(key) end
  if not base[key] then base[key] = read_raw(no) end
  return base[key]
end

-- The fields with every patch on top, in load order; a mod's quest's own condition first.
local function compose(key, no)
  local d = copy_raw(base_of(key, no))
  local list = ordered(key) or {}
  for _, p in ipairs(list) do
    local patch = p.patch
    if patch.name then d.name = patch.name end
    if patch.place then d.place = patch.place end
    if patch.difficulty then d.difficulty = patch.difficulty end
    if patch.description then
      for i = 1, LINES do d.desc[i] = patch.description[i] or "" end
    end
    if patch.conditions then
      for i = 1, SLOTS do
        local c = patch.conditions[i]
        d.conds[i] = c and { c[1], c[2], c[3] } or { 0, 0, 0 }
      end
    end
  end
  -- A "special" condition's text: the last patch that gave one.
  for _, p in ipairs(list) do
    if p.patch.special_text then d.special = p.patch.special_text end
  end
  local texts = {}
  local def = type(key) == "string" and defs[key]
  if def and def.condition and not custom_met(no, def.condition, def.mod, "rq.quests.define") then
    texts[#texts + 1] = def.condition.text
  end
  for _, p in ipairs(list) do
    local c = p.patch.condition
    if c and not custom_met(no, c, p.mod, "rq.quests.edit") then texts[#texts + 1] = c.text end
  end
  if #texts > 0 then
    local active, six = d.conds[1][1] ~= 0, nil
    if active then
      for i = 1, SLOTS do
        if d.conds[i][1] == 6 then six = i break end
      end
    end
    if six then
      if d.special ~= "" then table.insert(texts, 1, d.special) end
    else
      local free
      if not active then
        free = 1
        for i = 2, SLOTS do d.conds[i] = { 0, 0, 0 } end
      else
        for i = 2, SLOTS do
          if d.conds[i][1] == 0 then free = i break end
        end
      end
      if free then
        d.conds[free] = { 6, 0, 0 }
      else
        logf("rq.quests.edit: quest %d: no free condition slot for a mod's own condition", no)
        texts = {}
      end
    end
    if #texts > 0 then d.special = table.concat(texts, " / ") end
  end
  return d
end

local function apply(key)
  local no = key_no(key)
  if not no or not has_quest(no) then return end
  local ok, err = pcall(function() write_raw(no, compose(key, no)) end)
  if not ok then logf("rq.quests.edit: quest %d: %s", no, tostring(err)) end
end

local function unpatch(key)
  if type(key) == "string" then
    apply(key) -- the definition's own fields back
  elseif base[key] and has_quest(key) then
    local ok, err = pcall(write_raw, key, base[key])
    if not ok then logf("rq.quests.edit: quest %d: %s", key, tostring(err)) end
  end
  base[key] = nil
end

local function has_custom(key)
  local def = type(key) == "string" and defs[key]
  if def and def.condition then return true end
  for _, p in ipairs(patches[key] or {}) do
    if p.patch.condition then return true end
  end
  return false
end

-- The keys whose fields are written again: the patched quests and the mods' quests with a
-- condition of their own.
local function live_keys()
  local out, seen = {}, {}
  for key, list in pairs(patches) do
    if #list > 0 then out[#out + 1], seen[key] = key, true end
  end
  for _, id in ipairs(def_list) do
    if defs[id].condition and not seen[id] then out[#out + 1] = id end
  end
  return out
end

-- Before the game writes its data: its own values back, so it runs on them. After: read them
-- again and put the patches on top. A mod's quest's fields come from its definition, so they are
-- not read back.
local edit_hooked = false
local function install_edit()
  if edit_hooked then return end
  edit_hooked = true
  local function before()
    for no in pairs(base) do
      if has_quest(no) then pcall(write_raw, no, base[no]) end
    end
  end
  local function after()
    for _, key in ipairs(live_keys()) do
      local no = key_no(key)
      if no and has_quest(no) then
        if type(key) ~= "string" then base[key] = read_raw(no) end
        apply(key)
      end
    end
  end
  hook("Ｑ起動時登録", { before = before, after = after })
  hook("Ｑ特殊な条件の処理", { before = before, after = after })
end

function quests.edit(no, patch)
  local key
  if type(no) == "string" then
    if not defs[no] then error("rq.quests.edit: no quest " .. no, 2) end
    key = no
  elseif not int(no) or no <= 0 then
    error("rq.quests.edit: expected a quest number or id", 2)
  end
  if patch ~= nil and type(patch) ~= "table" then
    error("rq.quests.edit: the patch must be a table, or nil to remove it", 2)
  end
  local ready = data_ready()
  if not key then
    if ready and not has_quest(no) then error("rq.quests.edit: no quest " .. no, 2) end
    key = ready and key_of(no) or no
  end
  if patch then
    local why = check_patch(patch)
    if why then error("rq.quests.edit: quest " .. no .. ": " .. why, 2) end
  end
  local mod = mod_id()
  local list = patches[key] or {}
  patches[key] = list
  for i = #list, 1, -1 do
    if list[i].mod == mod then table.remove(list, i) end
  end
  if patch then
    seq = seq + 1
    local p = copy_patch(patch)
    if patch.conditions then
      for _, c in ipairs(patch.conditions) do
        if c.kind == "special" and c.text then p.special_text = c.text end
      end
    end
    list[#list + 1] = { mod = mod, seq = seq, patch = p }
    install_edit()
  end
  if not ready then return end
  if #list == 0 then
    patches[key] = nil
    unpatch(key)
  else
    apply(key)
  end
end

-- A mod's own condition is asked again at refresh and can_take.
local raw_refresh, raw_can_take = quests.refresh, quests.can_take

function quests.can_take(no)
  no = resolve(no, "rq.quests.can_take")
  if not int(no) or no <= 0 then error("rq.quests.can_take: expected a quest number or id", 2) end
  local key = key_of(no)
  if has_custom(key) then apply(key) end
  return raw_can_take(no)
end

function quests.refresh()
  for _, key in ipairs(live_keys()) do
    if has_custom(key) then apply(key) end
  end
  return raw_refresh()
end

-- ======================================================================================
-- rq.quests.define and no (2.41.0): a mod's own quest with no map
-- ======================================================================================

local DEF = { id = true, name = true, text = true, place = true, fights = true, main = true,
  difficulty = true, fame = true, conditions = true, condition = true, open = true, after = true,
  run = true, map = true }
local RESULT = { clear = 1, fail = 2, abandon = 3, escape = 4 }
local FAME_MAX = 9999999
local TITLE = "rq: the game went to the title"

local function lines_of(text)
  if type(text) == "string" then
    local out = {}
    for line in (text .. "\n"):gmatch("(.-)\n") do out[#out + 1] = line end
    return out
  end
  return text or {}
end

local function check_def(def)
  for k in pairs(def) do
    if not DEF[k] then return "no field " .. tostring(k) end
  end
  if def.name == nil then return "name is needed" end
  if def.text ~= nil and type(def.text) ~= "string" and type(def.text) ~= "table" then
    return "text must be a text or a list of up to " .. LINES .. " lines"
  end
  local why = check_patch({ name = def.name, description = lines_of(def.text), place = def.place,
    difficulty = def.difficulty, conditions = def.conditions, condition = def.condition })
  if why then return (why:gsub("^description", "text")) end
  for _, k in ipairs({ "fights", "main", "open" }) do
    if def[k] ~= nil and type(def[k]) ~= "boolean" then return k .. " must be true or false" end
  end
  if def.fame ~= nil and (not int(def.fame) or def.fame < 0 or def.fame > FAME_MAX) then
    return "fame must be a whole number from 0 to " .. FAME_MAX
  end
  local a = def.after
  if a ~= nil then
    if type(a) == "string" then
      if a == def.id then return "after cannot be the quest itself" end
    elseif not int(a) or a <= 0 then
      return "after must be a quest number or a quest's id"
    end
    if def.open then return "open and after: give one or the other" end
  end
  if def.map == nil then
    if type(def.run) ~= "function" then return "run must be a function" end
  else
    if def.run ~= nil and type(def.run) ~= "function" then return "run must be a function" end
    if def.fights then return "fights and map: a quest with a map has the map's mark" end
  end
  return nil
end

-- The state a quest starts at: open, after a cleared quest, or locked.
local function start_state(def)
  if def.open then return 1 end
  local a = def.after
  if a == nil then return 0 end
  local n = type(a) == "string" and no_of(a) or a
  if n and has_quest(n) and gint("Ｑ", n, "状態") == 2 then return 1 end
  return 0
end

-- The fields the list shows but edit does not write: the kind's marks, main or sub, the script's
-- kit, and the fame as Ｑ初期セット writes it (only while the quest has none and no clear).
local function write_def(id, no)
  local def = defs[id]
  if gstr("Ｑ", no, "名前") == "" then set("Ｑ", no, "名前", def.raw.name) end
  set("Ｑ", no, "種類", def.map and 0 or def.fights and 2 or 3)
  set("Ｑ", no, "メイン", def.main and 1 or 9)
  set("Ｑ", no, "キット", 0)
  if gint("Ｑ", no, "取得名声値") == 0 and gint("Ｑ", no, "クリア回数") == 0 then
    set("Ｑ", no, "取得名声値", def.fame)
  end
  apply(id)
end

local function free_no()
  for n = FIRST_NO, math.min(LAST_NO, quest_count() - 1) do
    if gstr("Ｑ", n, "チェック") == "" and gstr("Ｑ", n, "名前") == "" then return n end
  end
  return nil
end

local full_logged = {}

-- After each registration of the game's quests (a new game, a load, a world transfer): the marks
-- read, a gone mod's quests hidden, a returning one's state back, new ones numbered, every mod's
-- quest written.
local function register_all()
  if not data_ready() then return end
  scan()
  for id, n in pairs(no_cache) do
    local _, kept = id_at(n)
    if not defs[id] then
      if kept == nil then
        set("Ｑ", n, "チェック", "rq" .. gint("Ｑ", n, "状態") .. ":" .. id)
        set("Ｑ", n, "状態", 0)
        logf("rq.quests: quest %d (%s): no mod defines it; hidden until it is defined again", n, id)
      end
    elseif kept ~= nil then
      set("Ｑ", n, "状態", kept)
      set("Ｑ", n, "チェック", "rq:" .. id)
    end
  end
  local fresh = {}
  for _, id in ipairs(def_list) do
    if not no_cache[id] then
      local n = free_no()
      if n then
        call("Ｑ初期化", n)
        set("Ｑ", n, "チェック", "rq:" .. id)
        no_cache[id] = n
        fresh[#fresh + 1] = id
      elseif not full_logged[id] then
        full_logged[id] = true
        logf("rq.quests.define: %s left out: no free number from %d to %d in this save", id, FIRST_NO, LAST_NO)
      end
    end
  end
  for _, id in ipairs(def_list) do
    local n = no_cache[id]
    if n then
      local ok, err = pcall(write_def, id, n)
      if not ok then logf("rq.quests.define: %s: %s", id, tostring(err)) end
    end
  end
  -- After every number is known, so after can name a quest numbered in the same pass.
  for _, id in ipairs(fresh) do set("Ｑ", no_cache[id], "状態", start_state(defs[id])) end
end

-- The world transfer resets the game's quests 1 to 301; the mods' are reset here, after it: each
-- defined one as new (tries, clears, fame, its starting state), a gone mod's number freed.
local function transfer()
  if not data_ready() then return end
  scan()
  for id, n in pairs(no_cache) do
    call("Ｑ初期化", n)
    if defs[id] then
      set("Ｑ", n, "チェック", "rq:" .. id)
      local ok, err = pcall(write_def, id, n)
      if not ok then logf("rq.quests.define: %s: %s", id, tostring(err)) end
    else
      no_cache[id] = nil
    end
  end
  for _, id in ipairs(def_list) do
    local n = no_cache[id]
    if n then set("Ｑ", n, "状態", start_state(defs[id])) end
  end
end

-- Placed at the loader's start, before any mod's hook: a gone mod's quests are hidden even when no
-- loaded mod defines a quest, and edit's after-hook finds the mods' quests already written.
hook("Ｑ起動時登録", { after = register_all })
hook("ワールド切り替え処理", { after = transfer })

-- The script: kit 0's moment 8 for a mod's number, so rq.quests.on_event's start (on クエスト処理,
-- the dispatcher) runs first, as for a game quest. With a map (2.42.0) run is the prologue, and nil
-- goes on to the map.
local function run_quest(def, id, no)
  local q = { id = id, no = no, tries = gint("Ｑ", no, "参加回数"), clears = gint("Ｑ", no, "クリア回数") }
  q.first = q.clears == 0
  local ok, r = pcall(def.run, q)
  if not ok then
    if tostring(r):find(TITLE, 1, true) then error(r, 0) end
    logf("rq.quests.define (%s) %s: run: %s; the quest is given up", def.mod, id, tostring(r))
    return RESULT.abandon
  end
  if r == nil then return not def.map and RESULT.clear or nil end
  local code = RESULT[r]
  if not code then
    logf("rq.quests.define (%s) %s: run returned %s, not clear, fail, abandon or escape; the quest is given up",
      def.mod, id, tostring(r))
    return RESULT.abandon
  end
  return code
end

local run_hooked = false
local function hook_run()
  if run_hooked then return end
  run_hooked = true
  hook("クエスト処理／基本", { before = function(h)
    local moment = h.args[1]
    if moment ~= 8 and moment ~= 9 then return end
    local no = quest_now()
    local id = id_at(no)
    local def = id and defs[id]
    if not def then return end
    if def.map then
      h.skip, h.result = true, map_moment(def, id, no, moment, h.args[2], h.args[3], run_quest)
    elseif moment == 8 then
      h.skip, h.result = true, run_quest(def, id, no)
    end
  end })
  -- Moment 7 (an area entered) comes through ＱＭ登録 from inside moment 8 and 9, while the hook
  -- above runs: a hook's callbacks do not run for a nested call of the same function, so the area
  -- is answered on the dispatcher, whose hook has ended by then (its skip keeps 基本 out).
  hook("クエスト処理", { before = function(h)
    if h.args[1] ~= 7 then return end
    local no = quest_now()
    local id = id_at(no)
    local def = id and defs[id]
    if not def or not def.map then return end
    h.skip, h.result = true, map_moment(def, id, no, 7, h.args[2], h.args[3], run_quest)
  end })
end

-- after: opened when that quest is cleared, through the game's Ｑ状態(no, 1), so the victory
-- screen lists it with the game's new quests.
local raw_on_clear = quests.on_clear
local after_hooked = false
local function hook_after()
  if after_hooked then return end
  after_hooked = true
  raw_on_clear(function(ctx)
    for _, id in ipairs(def_list) do
      local a = defs[id].after
      if a ~= nil then
        local src = type(a) == "string" and no_of(a) or a
        local n = no_of(id)
        if src == ctx.quest and n and gint("Ｑ", n, "状態") == 0 then call("Ｑ状態", n, 1) end
      end
    end
  end)
end

function quests.define(def)
  if type(def) ~= "table" then error("rq.quests.define: expected a table", 2) end
  local id = def.id
  if type(id) ~= "string" or id == "" or #id > 64 then
    error("rq.quests.define: id must be a text of 1 to 64 bytes", 2)
  end
  local why = check_def(def)
  if why then error("rq.quests.define: " .. id .. ": " .. why, 2) end
  local map
  if def.map ~= nil then
    map, why = norm_map(def.map)
    if not map then error("rq.quests.define: " .. id .. ": map" .. why, 2) end
  end
  local mod = mod_id()
  local old = defs[id]
  if old and old.mod ~= mod then error("rq.quests.define: " .. id .. " is defined by " .. old.mod, 2) end
  if map then map_objects(map, mod) end
  local raw = { name = def.name, place = def.place or "", special = "", difficulty = def.difficulty or 0,
    desc = {}, conds = {} }
  local text = lines_of(def.text)
  for i = 1, LINES do raw.desc[i] = text[i] or "" end
  for i = 1, SLOTS do
    local c = def.conditions and def.conditions[i]
    raw.conds[i] = c and cond_raw(c, "") or { 0, 0, 0 }
    if c and c.kind == "special" and c.text then raw.special = c.text end
  end
  defs[id] = {
    id = id, mod = mod, raw = raw, run = def.run, map = map,
    fights = def.fights == true, main = def.main == true, fame = def.fame or 0,
    open = def.open == true, after = def.after,
    condition = def.condition and { text = def.condition.text, ok = def.condition.ok } or nil,
  }
  if not old then def_list[#def_list + 1] = id end
  hook_run()
  if def.after ~= nil then hook_after() end
  if def.condition then install_edit() end
  if data_ready() then register_all() end
end

function quests.no(id)
  if type(id) ~= "string" then error("rq.quests.no: expected a quest's id", 2) end
  return no_of(id)
end

-- ---- The calls that take an id, and the callbacks' ctx.id ----

local function plain(msg)
  return (tostring(msg):gsub("^[%w_]+:%d+: ", ""))
end

local raw_state, raw_set_state, raw_unlock, raw_start =
  quests.state, quests.set_state, quests.unlock, quests.start

-- The C calls read the game's arrays: at a mod's top level (no VM yet) that is a crash pcall cannot
-- catch, so these say so first, as the loader's guard on the C calls does.
local function need_game(name)
  if not pcall(rq.ain.global, "◆金") then
    error(name .. ": the game is not running yet. Call it from rq.on_frame or a callback, not at the top level of a mod", 3)
  end
end

function quests.state(no)
  need_game("rq.quests.state")
  return raw_state(resolve(no, "rq.quests.state"))
end

function quests.set_state(no, state)
  need_game("rq.quests.set_state")
  no = resolve(no, "rq.quests.set_state")
  local ok, err = pcall(raw_set_state, no, state)
  if not ok then error(plain(err), 2) end
end

function quests.unlock(no)
  need_game("rq.quests.unlock")
  return raw_unlock(resolve(no, "rq.quests.unlock"))
end

function quests.start(no)
  no = resolve(no, "rq.quests.start")
  local ok, err = pcall(raw_start, no)
  if not ok then error(plain(err), 2) end
end

for _, name in ipairs({ "on_clear", "on_end", "on_take" }) do
  local raw = quests[name]
  if type(raw) == "function" then
    quests[name] = function(fn)
      if type(fn) ~= "function" then return raw(fn) end
      return raw(function(ctx)
        if type(ctx) == "table" and ctx.id == nil then ctx.id = (id_at(ctx.quest)) end
        return fn(ctx)
      end)
    end
  end
end

-- ======================================================================================
-- rq.quests.run and set_run
-- ======================================================================================

local BONUS = { feats = 1, level_ups = 2, enemies = 3 }
local BONUS_ORDER = { "feats", "level_ups", "enemies" }
local WARP = { north = "◆転移禁止／北", south = "◆転移禁止／南", east = "◆転移禁止／東", west = "◆転移禁止／西" }
local PARTY = { [0] = "normal", [1] = "none", [2] = "free" }
local PARTY_NO = { normal = 0, none = 1, free = 2 }
local EXP_BONUS_MAX = 50 -- ＥＸＰボーナス処理's cap

function quests.run()
  local q = quest_now()
  if q == 0 then return nil end
  local r = {
    quest = q,
    progress = gint("◆クエスト進行率"),
    fights = gint("◆クエスト戦闘回数"),
    fights_here = gint("◆クエスト戦闘回数／ＭＡＰ内"),
    enemies_beaten = gint("◆クエスト戦闘勝利回数／配置敵"),
    exp = gint("◆クエストＥＸＰ合計"),
    fame = gint("◆クエスト名声"),
    fee = gint("◆クエスト鑑定料"),
    gold_base = gint("◆クエストＧＯＬＤ"),
    exp_bonus = gint("◆クエストＥＸＰボーナス"),
    bonuses = {},
    party_change = PARTY[gint("◆クエストパーティ編成禁止")] or "normal",
    no_area_map = gint("◆クエストエリア表示禁止") == 1,
    no_warp = {},
  }
  for name, i in pairs(BONUS) do
    if gint("◆クエストボーナス有無", i) == 1 then
      r.bonuses[name] = { count = gint("◆クエストボーナス", i), max = gint("◆クエストボーナス最大", i) }
    end
  end
  for dir, gname in pairs(WARP) do r.no_warp[dir] = gint(gname) == 1 end
  return r
end

local RUN = { fame = true, exp_bonus = true, bonuses = true, party_change = true, no_area_map = true, no_warp = true,
  progress = true }

local function check_run(f)
  for k in pairs(f) do
    if not RUN[k] then return "no field " .. tostring(k) .. " to write" end
  end
  if f.progress ~= nil and (not int(f.progress) or f.progress < 0 or f.progress > 100) then
    return "progress must be a whole number from 0 to 100"
  end
  if f.fame ~= nil and (not int(f.fame) or f.fame < 0 or f.fame > 9999999) then
    return "fame must be a whole number from 0 to 9999999"
  end
  if f.exp_bonus ~= nil and (not int(f.exp_bonus) or f.exp_bonus < 0 or f.exp_bonus > EXP_BONUS_MAX) then
    return "exp_bonus must be a whole number from 0 to " .. EXP_BONUS_MAX
  end
  if f.bonuses ~= nil then
    if type(f.bonuses) ~= "table" then return "bonuses must be a table" end
    for k, b in pairs(f.bonuses) do
      if not BONUS[k] then return "bonuses: no bonus " .. tostring(k) .. " (feats, level_ups, enemies)" end
      if b ~= false then
        if type(b) ~= "table" then return "bonuses." .. k .. " must be { count, max } or false" end
        for f2, v in pairs(b) do
          if f2 ~= "count" and f2 ~= "max" then return "bonuses." .. k .. " has no field " .. tostring(f2) end
          if not int(v) or v < 0 or v > 99 then return "bonuses." .. k .. "." .. f2 .. " must be 0 to 99" end
        end
      end
    end
  end
  if f.party_change ~= nil and PARTY_NO[f.party_change] == nil then
    return "party_change must be normal, none or free"
  end
  if f.no_area_map ~= nil and type(f.no_area_map) ~= "boolean" then return "no_area_map must be true or false" end
  if f.no_warp ~= nil then
    if type(f.no_warp) ~= "table" then return "no_warp must be a table of north, south, east, west" end
    for k, v in pairs(f.no_warp) do
      if not WARP[k] then return "no_warp: no direction " .. tostring(k) end
      if type(v) ~= "boolean" then return "no_warp." .. k .. " must be true or false" end
    end
  end
  return nil
end

-- The flash the map shows when a bonus reaches its maximum (Ｔマップ割り込み).
local function bonus_flash(full)
  if full.level_ups and full.enemies then return "レベル＋１配置敵＋１" end
  if full.level_ups then return "レベル＋１" end
  if full.enemies then return "配置敵＋１" end
  if full.feats then return "冒険功績GET" end
  return nil
end

local function map_panel_up()
  return gint("g_sMapParts", "m_nMapNo") > 0
end

function quests.set_run(f)
  if type(f) ~= "table" then error("rq.quests.set_run: expected a table of fields", 2) end
  if quest_now() == 0 then error("rq.quests.set_run: not in a quest", 2) end
  local why = check_run(f)
  if why then error("rq.quests.set_run: " .. why, 2) end
  if f.fame ~= nil then set("◆クエスト名声", f.fame) end
  if f.exp_bonus ~= nil then set("◆クエストＥＸＰボーナス", f.exp_bonus) end
  -- The game's クエスト進行率書き換え only raises it; a mod's quest may also lower it.
  if f.progress ~= nil then set("◆クエスト進行率", f.progress) end
  local full = {}
  for _, name in ipairs(BONUS_ORDER) do
    local b = f.bonuses and f.bonuses[name]
    if b ~= nil then
      local i = BONUS[name]
      if b == false then
        set("◆クエストボーナス有無", i, 0)
      else
        local on = gint("◆クエストボーナス有無", i) == 1
        local old = on and gint("◆クエストボーナス", i) or 0
        local max = b.max or (on and gint("◆クエストボーナス最大", i) or 0)
        local count = b.count or old
        if count > max then count = max end
        set("◆クエストボーナス有無", i, 1)
        set("◆クエストボーナス最大", i, max)
        set("◆クエストボーナス", i, count)
        if max > 0 and count == max and old < max then full[name] = true end
      end
    end
  end
  if f.party_change ~= nil then set("◆クエストパーティ編成禁止", PARTY_NO[f.party_change]) end
  if f.no_area_map ~= nil then set("◆クエストエリア表示禁止", f.no_area_map and 1 or 0) end
  if f.no_warp then
    for dir, v in pairs(f.no_warp) do set(WARP[dir], v and 1 or 0) end
  end
  if map_panel_up() then
    local ok, err = pcall(call, "マップ部品_状態更新", 0, 0, 0)
    if not ok then logf("rq.quests.set_run: the quest panel: %s", tostring(err)) end
    local flash = bonus_flash(full)
    if flash and yes(call("マップ_表示中")) then
      ok, err = pcall(call, "効果_フラッシュ", flash, 0, 0, 9601)
      if not ok then logf("rq.quests.set_run: the flash: %s", tostring(err)) end
    end
  end
end

-- ======================================================================================
-- Map objects
-- ======================================================================================

local MAP = 50
local CODE_KIND = { [1] = "chest", [2] = "red-chest", [3] = "warp", [4] = "warp", [5] = "achievement",
  [6] = "trap", [8] = "event", [9] = "event", [10] = "event", [12] = "event", [13] = "spring",
  [15] = "escape", [16] = "enemy", [17] = "enemy", [18] = "event", [19] = "gold", [20] = "exp" }
local PLACE = { chest = 1, ["red-chest"] = 2, spring = 13, achievement = 5, exp = 20, gold = 19,
  trap = 6, enemy = 16, escape = 15 }
-- The game's props (Ｒ８置物配置 numbers), named from the Lab's pictures.
local LOOKS = {
  chest = 202, ["red-chest"] = 214, warp = 201, ["warp-2"] = 215, achievement = 210, trap = 206,
  enemy = 204, ["enemy-2"] = 205, spring = 203, escape = 217, gold = 213, exp = 212,
  event = 207, ["event-2"] = 216, ["event-3"] = 208, ["event-4"] = 218, hidden = 211,
}
local LOOK_NAME = {}
for name, prop in pairs(LOOKS) do LOOK_NAME[prop] = name end
local CUSTOM_CODE = 12          -- a silent map event: no jingle
local CUSTOM_BASE = 1000000000  -- a mod's object's number: CUSTOM_BASE + a hash of its id
local CUSTOM_SPAN = 1000000000
local KEEP = 100000             -- maptv = the look's prop, + KEEP when the object comes back

local function is_custom(n)
  return int(n) and n >= CUSTOM_BASE and n < CUSTOM_BASE + CUSTOM_SPAN
end

local function number_of(id)
  local h = 2166136261
  for i = 1, #id do h = ((h ~ id:byte(i)) * 16777619) & 0xffffffff end
  return CUSTOM_BASE + h % CUSTOM_SPAN
end

local known = {}   -- number -> id
local on_obj = {}  -- id -> { fn, mod }

local function check_id(id, name)
  if type(id) ~= "string" or id == "" or #id > 64 then
    error(name .. ": the id must be a text of 1 to 64 bytes", 3)
  end
end

-- The id's number; an error when another id has the same one.
local function claim(id, name)
  local n = number_of(id)
  if known[n] and known[n] ~= id then
    error(string.format("%s: the ids %q and %q share a number; rename one", name, known[n], id), 3)
  end
  known[n] = id
  return n
end

local function cell(xx, yy)
  local bm = gint("◆ＢＭＡＰ")
  return gint("Ｍ", bm, "maptt", xx, yy), gint("Ｍ", bm, "maptz", xx, yy), gint("Ｍ", bm, "maptv", xx, yy)
end

-- Takes an object off a cell as ＭＤ消し does, without touching the game's undo (◆戻し…).
local function clear_cell(xx, yy)
  local bm = gint("◆ＢＭＡＰ")
  call("Ｒ８置物配置", xx, yy, 0, -1)
  for _, f in ipairs({ "maptt", "maptz", "maptv", "mapnn" }) do set("Ｍ", bm, f, xx, yy, 0) end
end

function dungeon.looks()
  local out = {}
  for name in pairs(LOOKS) do out[#out + 1] = name end
  table.sort(out)
  return out
end

function dungeon.objects()
  local grid = dungeon.grid({ whole = true })
  if not grid then return nil end
  local out = {}
  for _, e in ipairs(grid.events or {}) do
    local tt = math.tointeger(e.tt) or 0
    if tt ~= 0 then
      local tz, tv = math.tointeger(e.tz) or 0, math.tointeger(e.tv) or 0
      local o = { x = math.tointeger(e.x), z = math.tointeger(e.z), kind = CODE_KIND[tt] or "other",
        code = tt, number = tz, rank = tv }
      if tt == CUSTOM_CODE and is_custom(tz) then
        o.kind, o.id, o.rank = "custom", known[tz], nil
        o.look, o.keep = LOOK_NAME[tv % KEEP], tv >= KEEP
      end
      out[#out + 1] = o
    end
  end
  table.sort(out, function(a, b)
    if a.z ~= b.z then return a.z > b.z end
    return a.x < b.x
  end)
  return out
end

local function check_cell(x, z, name)
  if not int(x) or not int(z) then error(name .. ": expected whole cell numbers", 3) end
  if x < 0 or x >= MAP or z < 0 or z >= MAP then error(name .. ": cell " .. x .. "," .. z .. " is off the map", 3) end
end

function dungeon.remove(x, z)
  check_cell(x, z, "rq.dungeon.remove")
  if not dungeon.grid() then error("rq.dungeon.remove: only on a dungeon map", 2) end
  local xx, yy = x + 1, MAP - z
  local tt = cell(xx, yy)
  if tt == 0 then return false end
  if tt == 3 or tt == 4 then error("rq.dungeon.remove: a warp cannot be removed", 2) end
  clear_cell(xx, yy)
  return true
end

function dungeon.place(kind, x, z, opts)
  local code = PLACE[kind]
  if not code and kind ~= "custom" then
    error("rq.dungeon.place: unknown kind " .. tostring(kind), 2)
  end
  if not int(x) or not int(z) then
    error("rq.dungeon.place: expected whole cell numbers", 2)
  end
  if opts ~= nil and type(opts) ~= "table" then error("rq.dungeon.place: opts must be a table", 2) end
  opts = opts or {}
  local tz, tv = opts.content or 0, opts.rank or 0
  if kind == "custom" then
    check_id(opts.id, "rq.dungeon.place")
    local look = opts.look or "event"
    if not LOOKS[look] then error("rq.dungeon.place: unknown look " .. tostring(look) .. " (rq.dungeon.looks())", 2) end
    if opts.keep ~= nil and type(opts.keep) ~= "boolean" then error("rq.dungeon.place: keep must be true or false", 2) end
    code, tz, tv = CUSTOM_CODE, claim(opts.id, "rq.dungeon.place"), LOOKS[look] + (opts.keep and KEEP or 0)
  end
  local grid = dungeon.grid()
  if not grid then
    error("rq.dungeon.place: only on a dungeon map", 2)
  end
  local room = grid.room
  local row = grid.rows[room.maxZ - z + 1]
  local at = x - room.minX + 1
  if not row or at < 1 or row:sub(at, at) ~= "." then
    error("rq.dungeon.place: cell " .. x .. "," .. z .. " is not free floor", 2)
  end
  return call("ＭＤ固定", x + 1, MAP - z, code, tz, tv) == 1
end

function dungeon.on_object(id, fn)
  check_id(id, "rq.dungeon.on_object")
  if type(fn) ~= "function" then error("rq.dungeon.on_object: expected a function", 2) end
  claim(id, "rq.dungeon.on_object")
  local mod = mod_id()
  local old = on_obj[id]
  if old and old.mod ~= mod then
    logf("rq.dungeon.on_object: %s replaces %s's callback for %s", mod, old.mod, id)
  end
  local h = { fn = fn, mod = mod }
  on_obj[id] = h
  return function()
    if on_obj[id] == h then on_obj[id] = nil end
  end
end

-- A step on a mod's object: the game's イベント処理 gives the number and removes the object; the
-- loader runs the callback there, then answers クエスト処理(9, number, 4) with its result.
local pending = nil

local function run_object(n, xx, yy, tv)
  local id = known[n]
  local h = id and on_obj[id]
  if not h then
    logf("rq.dungeon: an object with no rq.dungeon.on_object callback (%s)", id or ("number " .. n))
    return 0
  end
  local ctx = { id = id, x = xx - 1, z = MAP - yy, quest = quest_now(), area = gint("◆ＢＭＡＰ"),
    look = LOOK_NAME[tv % KEEP], keep = tv >= KEEP }
  function ctx.remove() ctx.keep = false end
  local ok, r = pcall(h.fn, ctx)
  if not ok then
    logf("rq.dungeon.on_object (%s) %s: %s", h.mod, id, tostring(r))
    r = nil
  end
  if ctx.keep == true then
    local prop = LOOKS[ctx.look] or (tv % KEEP)
    -- Put back with the object's own keep: a function's ctx.keep is for this step only.
    local put, err = pcall(call, "ＭＤ固定", xx, yy, CUSTOM_CODE, n, prop + (tv >= KEEP and KEEP or 0))
    if not put then logf("rq.dungeon.on_object %s: putting it back: %s", id, tostring(err)) end
  end
  if type(r) == "string" and RESULT[r] then r = RESULT[r] end -- 2.42.0
  if r ~= nil and not int(r) then
    logf("rq.dungeon.on_object (%s) %s: the result must be clear, fail, abandon, escape, a whole number or nil",
      h.mod, id)
    r = nil
  end
  return r or 0
end

hook("イベント処理", { before = function(h)
  local xx, yy = h.args[1], h.args[2]
  if not int(xx) or not int(yy) then return end
  local tt, tz, tv = cell(xx, yy)
  if tt ~= CUSTOM_CODE or not is_custom(tz) then return end
  call("ＭＤ消し", xx, yy)
  h.skip, h.result = true, tz
  pending = { no = tz, result = run_object(tz, xx, yy, tv) }
end })

hook("クエスト処理", { before = function(h)
  local a = h.args
  if a[1] ~= 9 or a[3] ~= 4 or not is_custom(a[2]) then return end
  local r = 0
  if pending and pending.no == a[2] then r = pending.result end
  pending = nil
  h.skip, h.result = true, r
end })

-- The map's own code takes the spring's, the achievement's, the EXP's and the gold's props by the
-- prop itself (T_Player@checkObject runs 回復の泉処理 and the others there and never reaches the
-- quest's script). On a mod's object with one of those looks, the touch goes on as an event's.
local PROP_TAKEN = { [203] = true, [210] = true, [212] = true, [213] = true }
hook("T_Player@checkObject", { before = function(h)
  local flag, hx, hy = h.args[1], h.args[2], h.args[3]
  if not PROP_TAKEN[flag] or not int(hx) or not int(hy) then return end
  if hx < 0 or hx >= MAP or hy < 0 or hy >= MAP then return end
  local tt, tz = cell(hx + 1, MAP - hy)
  if tt == CUSTOM_CODE and is_custom(tz) then h.args[1] = LOOKS.event end
end })

-- A mod's object puts its look on the cell each time the game writes it (also on re-entering an
-- area, which calls ＭＤ固定 for every cell again).
hook("ＭＤ固定", { after = function(h)
  local a = h.args
  if a[3] ~= CUSTOM_CODE or not is_custom(a[4]) or h.result ~= 1 then return end
  local prop = (int(a[5]) and a[5] or 0) % KEEP
  if prop > 0 and prop ~= LOOKS.hidden then
    local ok, err = pcall(call, "Ｒ８置物配置", a[1], a[2], prop, -1)
    if not ok then logf("rq.dungeon: a mod object's look: %s", tostring(err)) end
  end
end })

-- rq.quests.on_event and on_event_after do not see a mod's objects; their ev has a mod's quest's
-- id (2.41.0).
for _, name in ipairs({ "on_event", "on_event_after" }) do
  local raw = quests[name]
  if type(raw) == "function" then
    quests[name] = function(fn)
      if type(fn) ~= "function" then return raw(fn) end
      return raw(function(ev)
        if ev.code == 9 and ev.fg == 4 and is_custom(ev.no) then return nil end
        ev.id = (id_at(ev.quest))
        return fn(ev)
      end)
    end
  end
end

-- ======================================================================================
-- Traps
-- ======================================================================================

local TRAP = { shroom = 1, shock = 2, alarm = 3, pit = 6 }
local TRAP_NAME = { [1] = "shroom", [2] = "shock", [3] = "alarm", [6] = "pit" }
local TRAP_ORDER = { "shroom", "shock", "alarm", "pit" }

function dungeon.traps()
  if quest_now() == 0 then return nil end
  local t = {}
  for name, i in pairs(TRAP) do t[name] = gint("◆トラップ発生確率", i) end
  return t
end

function dungeon.set_traps(w)
  if type(w) ~= "table" then error("rq.dungeon.set_traps: expected a table { shroom, shock, alarm, pit }", 2) end
  if quest_now() == 0 then error("rq.dungeon.set_traps: not in a quest", 2) end
  for k, v in pairs(w) do
    if not TRAP[k] then error("rq.dungeon.set_traps: no trap kind " .. tostring(k) .. " (shroom, shock, alarm, pit)", 2) end
    if not int(v) or v < 0 or v > 10 then error("rq.dungeon.set_traps: " .. k .. " must be 0 to 10", 2) end
  end
  for _, k in ipairs(TRAP_ORDER) do
    if w[k] ~= nil then set("◆トラップ発生確率", TRAP[k], w[k]) end
  end
end

local trap_list, add_trap = handlers()
local forced_kind, forced_roll = nil, nil
local traps_hooked = false

-- トラップ処理's disarm chance and who tries, as the game counts them.
local function game_chance()
  local c, who = 20, 0
  if quest_now() == 190 then c = 0 end
  for slot = 1, 5 do
    local iic = gint("PTA", slot)
    if iic ~= 0 and c ~= 100 then
      if call("Ｃ職業", iic, -1) == 6 and who == 0 then
        who, c = iic, 50
      end
      local rank = call("ＣＳ有無／ランク", iic, 266, 0)
      if int(rank) and rank > call("Ｃトラップ解除回数", iic, -1) then
        who, c = iic, 100
      end
    end
  end
  return c, who ~= 0 and who or nil
end

local function install_traps()
  if traps_hooked then return end
  traps_hooked = true
  hook("トラップ処理", {
    before = function(h)
      forced_kind, forced_roll = nil, nil
      if #trap_list == 0 then return end
      local xx, yy = h.args[1], h.args[2]
      local kind = call("トラップ確定")
      local chance, who = game_chance()
      local ctx = { x = xx - 1, z = MAP - yy, kind = TRAP_NAME[kind] or "shock", chance = chance,
        who = who, cancel = false }
      local kept_kind, kept_chance = ctx.kind, chance
      for _, x in ipairs({ table.unpack(trap_list) }) do
        local ok, err = pcall(x.fn, ctx)
        if not ok then logf("rq.dungeon.on_trap (%s): %s", x.mod, tostring(err)) end
        if TRAP[ctx.kind] then
          kept_kind = ctx.kind
        else
          logf("rq.dungeon.on_trap (%s): kind must be shroom, shock, alarm or pit", x.mod)
          ctx.kind = kept_kind
        end
        local c = ctx.chance
        if type(c) == "number" and c == c then
          c = math.floor(c)
          kept_chance = c < 0 and 0 or c > 100 and 100 or math.tointeger(c)
        else
          logf("rq.dungeon.on_trap (%s): chance must be a number from 0 to 100", x.mod)
        end
        ctx.chance = kept_chance
      end
      if ctx.cancel == true then
        call("ＭＤ消し", xx, yy)
        h.skip, h.result = true, 0
        return
      end
      forced_kind = TRAP[kept_kind]
      if kept_chance ~= chance then
        local r = call("RAND", 100)
        forced_roll = kept_chance >= r and 0 or 101
      end
    end,
    after = function()
      forced_kind, forced_roll = nil, nil
    end,
  })
  hook("トラップ確定", { before = function(h)
    if forced_kind then
      h.skip, h.result = true, forced_kind
      forced_kind = nil
    end
  end })
  hook("RAND", { before = function(h)
    if forced_roll == nil or h.args[1] ~= 100 then return end
    if caller_of("RAND") ~= "トラップ処理" then return end
    h.skip, h.result = true, forced_roll
    forced_roll = nil
  end })
end

function dungeon.on_trap(fn)
  if type(fn) ~= "function" then error("rq.dungeon.on_trap: expected a function", 2) end
  install_traps()
  return add_trap(fn)
end

-- ======================================================================================
-- A mod's quest's map (2.42.0): areas on the game's 4 by 4 grid, each a stock room of one of the
-- game's tilesets or one of its fixed maps, set up at each entry as the game's quest scripts set
-- theirs (moment 7), entered after the prologue (moment 8), its moments answered (9)
-- ======================================================================================

local GRID = 16
-- The tilesets with stock rooms: the map number of their room 0 (ＭＡＰ読み込み) and the map music
-- their quests use most.
local TILESET = { [1] = { 1000, 1 }, [2] = { 2000, 11 }, [3] = { 3000, 1 }, [4] = { 4000, 2 },
  [5] = { 5000, 4 }, [6] = { 6000, 2 }, [7] = { 7000, 12 }, [9] = { 10000, 1 }, [10] = { 11000, 5 },
  [13] = { 14000, 2 }, [14] = { 15000, 6 }, [20] = { 16000, 6 }, [21] = { 17000, 6 } }
-- The others' music, for a fixed map.
local MUSIC = { [8] = 1, [11] = 10, [12] = 2, [15] = 2, [16] = 3, [17] = 73, [18] = 4, [19] = 14, [22] = 1 }
local MALGRID = { [14] = true, [20] = true, [21] = true }
local KINDS_MAX = 22
-- The stock rooms (mapd by 汎用番号): { spots, wide spots }. 401 to 409 are Malgrid's only. The
-- game's draw can give any of the first ten, so a drawn room counts on 17 spots and 1 wide one.
local ROOMS = { [1] = { 17, 1 }, [2] = { 20, 2 }, [101] = { 19, 1 }, [102] = { 21, 1 },
  [201] = { 20, 1 }, [202] = { 20, 1 }, [203] = { 20, 1 }, [204] = { 20, 1 }, [301] = { 18, 1 },
  [302] = { 20, 1 }, [401] = { 25, 1 }, [402] = { 25, 1 }, [403] = { 24, 1 }, [404] = { 25, 1 },
  [405] = { 25, 1 }, [406] = { 20, 1 }, [407] = { 24, 1 }, [408] = { 25, 1 }, [409] = { 25, 1 } }
local DRAW_SPOTS, DRAW_WIDE = 17, 1
-- The fixed maps the game's quests load that have their mapd: exits/wide spots/spots.
local FIXED_DATA = "11:NSEW/3/16 12:NSEW/5/25 13:S/1/25 14:NS/1/21 15:EW/5/25 16:NSEW/3/17 17:E/4/25 " ..
  "18:NSEW/4/25 19:NSEW/0/20 20:NS/0/14 21:NS/0/0 22:EW/0/0 23:NSEW/4/18 24:NS/1/25 25:SEW/1/18 26:NW/1/14 " ..
  "27:EW/4/19 28:EW/4/20 29:NW/3/19 30:-/1/20 31:S/1/8 34:NSEW/2/17 35:N/1/21 36:-/1/20 37:-/1/20 " ..
  "38:SEW/1/22 39:NSEW/1/21 40:EW/1/12 41:NSEW/1/21 42:NSEW/2/11 43:S/1/19 44:N/1/20 45:W/1/20 46:E/1/20 " ..
  "47:NSEW/1/17 48:NSEW/1/19 49:NSEW/1/21 54:NSEW/1/19 55:-/1/20 61:NSEW/1/20 62:E/0/20 63:N/1/25 " ..
  "64:S/1/20 65:NS/1/20 66:NE/1/22 67:EW/1/25 68:SW/1/23 69:-/0/0 70:N/0/0 71:-/1/23 72:NEW/1/25 " ..
  "73:-/2/25 74:NS/1/25 75:-/0/25 76:EW/1/21 77:NSEW/1/17 78:NSEW/2/20 83:NSEW/5/25 101:NSEW/5/25 " ..
  "103:NSEW/5/21 104:NSEW/5/25 105:NSEW/5/25 106:NSEW/5/24 121:NSEW/4/25 122:NSEW/1/20 123:NSEW/5/21 " ..
  "124:NSEW/5/25 125:NSEW/5/25 141:NSEW/3/21 142:NSEW/5/25 143:NSEW/5/25 144:NSEW/5/25 161:NSEW/5/25 " ..
  "162:NSEW/5/25 163:NSEW/5/25 240:NSEW/4/25 301:NSEW/0/25 302:NSEW/1/18 307:W/1/25 308:-/1/21 " ..
  "311:NSE/1/25 313:NSEW/1/18 315:NSEW/1/10 317:NSEW/1/18 322:NSEW/1/25 323:NSEW/1/25 328:E/0/0"
local FIXED = {}
for n, e, w, s in FIXED_DATA:gmatch("(%d+):([NSEW%-]+)/(%d+)/(%d+)") do
  FIXED[tonumber(n)] = { exits = e == "-" and "" or e, wide = tonumber(w), spots = tonumber(s) }
end
-- What the chests hold (クエスト基本登録's chest type): rank C, B or A, with an item bonus (+), with a
-- superior item (A++); nil, the game's 0, by Rance's level.
local LOOT = { C = 11, B = 21, A = 31, ["C+"] = 13, ["B+"] = 23, ["A+"] = 33, ["A++"] = 40 }
local LOOT_NO = { [0] = true, [11] = true, [12] = true, [13] = true, [14] = true, [21] = true, [22] = true,
  [23] = true, [24] = true, [31] = true, [32] = true, [33] = true, [34] = true, [40] = true }
local GOLD_DEFAULT, NAME_MAP_MAX, COUNT_MAX = 50, 40, 25
local STEP = { N = -4, S = 4, E = 1, W = -1 }
local BACK = { N = "S", S = "N", E = "W", W = "E" }
local MAP_FIELD = { tileset = true, name = true, gold = true, loot = true, music = true, background = true,
  traps = true, groups = true, bonuses = true, start = true, chatter = true, areas = true }
local AREA_FIELD = { exits = true, room = true, fixed = true, enemies = true, chests = true, traps = true,
  feats = true, objects = true, setup = true, tileset = true, name = true, gold = true, loot = true,
  music = true, background = true, groups = true }
local OBJ_FIELD = { kind = true, spot = true, item = true, id = true, look = true, keep = true, run = true }
local BONUS_NAME = { feats = true, enemies = true, level_ups = true }
local SAVE_MOD, SAVE_KEY = "rq:loader", "quest_maps"

local function whole(v, low, high)
  return int(v) and v >= low and v <= high
end

-- The settings the map gives and an area may give again; nil or why not.
local function check_settings(t, at)
  if t.tileset ~= nil and not whole(t.tileset, 1, KINDS_MAX) then
    return at .. "tileset must be a map kind, 1 to " .. KINDS_MAX .. " (rq.ids.map_kind)"
  end
  if t.name ~= nil and (type(t.name) ~= "string" or not chars(t.name) or chars(t.name) > NAME_MAP_MAX) then
    return at .. "name must be a text of up to " .. NAME_MAP_MAX .. " characters"
  end
  if t.gold ~= nil and not whole(t.gold, 0, 999999) then return at .. "gold must be a whole number from 0 to 999999" end
  if t.loot ~= nil and not LOOT[t.loot] and not LOOT_NO[t.loot] then
    return at .. "loot must be \"C\", \"B\", \"A\", \"C+\", \"B+\", \"A+\", \"A++\" or one of the game's chest types"
  end
  if t.music ~= nil and not whole(t.music, 0, 9999) then return at .. "music must be one of the game's map tracks" end
  if t.background ~= nil and not whole(t.background, 1, 9999) then
    return at .. "background must be a fight background's number (rq.battle.backgrounds)"
  end
  local gr = t.groups
  if gr ~= nil then
    if type(gr) ~= "table" or #gr < 1 or #gr > 5 then return at .. "groups must be a list of 1 to 5 groups" end
    for k, v in pairs(gr) do
      if not int(k) or k < 1 or k > #gr then return at .. "groups must be a list of 1 to 5 groups" end
      if not whole(v, 1, math.maxinteger) and (type(v) ~= "string" or v == "") then
        return at .. "groups[" .. k .. "] must be a group's number or a mod's group id"
      end
    end
  end
  return nil
end

local function check_object(o, at)
  if type(o) ~= "table" then return at .. " must be a table" end
  for k in pairs(o) do
    if not OBJ_FIELD[k] then return at .. " has no field " .. tostring(k) end
  end
  local custom = o.kind == "custom"
  if not custom and not PLACE[o.kind] then
    return at .. ".kind must be chest, red-chest, enemy, trap, achievement, spring, escape, gold, exp or custom"
  end
  local s = o.spot
  if s ~= nil and s ~= "any" and s ~= "wide" then
    local x, z = type(s) == "table" and (s.x or s[1]), type(s) == "table" and (s.z or s[2])
    if not whole(x, 0, MAP - 1) or not whole(z, 0, MAP - 1) then
      return at .. ".spot must be \"any\", \"wide\" or a cell { x, z } of 0 to " .. (MAP - 1)
    end
  end
  if o.item ~= nil then
    if o.kind ~= "chest" and o.kind ~= "red-chest" then return at .. ".item is for a chest" end
    if not whole(o.item, 1, math.maxinteger) and (type(o.item) ~= "string" or o.item == "") then
      return at .. ".item must be an item number or a mod's item id"
    end
  end
  if custom then
    if type(o.id) ~= "string" or o.id == "" or #o.id > 64 then return at .. ".id must be a text of 1 to 64 bytes" end
    if o.look ~= nil and not LOOKS[o.look] then return at .. ".look: no look " .. tostring(o.look) .. " (rq.dungeon.looks())" end
    if o.keep ~= nil and type(o.keep) ~= "boolean" then return at .. ".keep must be true or false" end
    if type(o.run) ~= "function" then return at .. ".run must be a function" end
    local n = number_of(o.id)
    if known[n] and known[n] ~= o.id then
      return string.format("%s: the ids %q and %q share a number; rename one", at, known[n], o.id)
    end
  else
    for _, k in ipairs({ "id", "look", "keep", "run" }) do
      if o[k] ~= nil then return at .. "." .. k .. " is for a custom object" end
    end
  end
  return nil
end

local function exits_of(e)
  local out = ""
  for _, l in ipairs({ "N", "S", "E", "W" }) do
    if e:find(l, 1, true) then out = out .. l end
  end
  return out
end

-- The objects of one area checked and copied, against the room's spots; or nil and why not.
local function area_objects(ar, at, spots, wide)
  local counted = 0
  for _, k in ipairs({ "enemies", "chests", "traps", "feats" }) do
    if ar[k] ~= nil and not whole(ar[k], 0, COUNT_MAX) then return nil, at .. "." .. k .. " must be 0 to " .. COUNT_MAX end
    counted = counted + (ar[k] or 0)
  end
  local objs, at_any, at_wide = {}, 0, 0
  if ar.objects ~= nil then
    if type(ar.objects) ~= "table" then return nil, at .. ".objects must be a list" end
    for k in pairs(ar.objects) do
      if not int(k) or k < 1 or k > #ar.objects then return nil, at .. ".objects must be a list" end
    end
    for i, o in ipairs(ar.objects) do
      local why = check_object(o, at .. ".objects[" .. i .. "]")
      if why then return nil, why end
      local s = o.spot
      local cell = type(s) == "table" and { x = s.x or s[1], z = s.z or s[2] } or nil
      if s == "wide" then at_wide = at_wide + 1 elseif not cell then at_any = at_any + 1 end
      objs[i] = { kind = o.kind, spot = cell or s or "any", item = o.item, id = o.id, look = o.look or "event",
        keep = o.keep == true, run = o.run }
    end
  end
  if counted + at_any > spots then
    return nil, string.format("%s: %d objects for the room's %d spots (enemies, chests, traps, feats and the " ..
      "objects at \"any\")", at, counted + at_any, spots)
  end
  if at_wide > wide then
    return nil, string.format("%s: %d objects at \"wide\" for the room's %d wide spot%s", at, at_wide, wide,
      wide == 1 and "" or "s")
  end
  return objs
end

-- One area checked and copied, its settings resolved; or nil and why not.
local function norm_area(m, a, ar)
  local at = ".areas[" .. a .. "]"
  if type(ar) ~= "table" then return nil, at .. " must be a table" end
  for k in pairs(ar) do
    if not AREA_FIELD[k] then return nil, at .. " has no field " .. tostring(k) end
  end
  local why = check_settings(ar, at .. ".")
  if why then return nil, why end
  local e = ar.exits
  if e == nil then e = "" end
  if type(e) ~= "string" or e:find("[^NSEW]") then return nil, at .. ".exits must be letters among N, S, E and W" end
  for l in e:gmatch(".") do
    local _, n = e:gsub(l, "")
    if n > 1 then return nil, at .. ".exits: " .. l .. " twice" end
  end
  local tileset = ar.tileset or m.tileset
  if not tileset then return nil, at .. ": no tileset (the map's tileset, or the area's)" end
  if ar.room ~= nil and ar.fixed ~= nil then return nil, at .. ": room or fixed, not both" end
  local spots, wide
  if ar.fixed ~= nil then
    local f = FIXED[ar.fixed]
    if not f then return nil, at .. ".fixed: " .. tostring(ar.fixed) .. " is not one of the game's fixed maps" end
    for l in e:gmatch(".") do
      if not f.exits:find(l, 1, true) then
        return nil, at .. ".exits: fixed map " .. ar.fixed .. " has no " .. l .. " exit (it has " ..
          (f.exits == "" and "none" or f.exits) .. ")"
      end
    end
    spots, wide = f.spots, f.wide
  else
    if not TILESET[tileset] then
      return nil, at .. ": tileset " .. tileset .. " has no stock rooms; give the area a fixed map"
    end
    if ar.room ~= nil then
      local r = ROOMS[ar.room]
      if not r then
        return nil, at .. ".room must be a stock room: 1, 2, 101, 102, 201 to 204, 301, 302, 401 to 409"
      end
      if ar.room >= 401 and not MALGRID[tileset] then
        return nil, at .. ".room: rooms 401 to 409 are Malgrid's (tilesets 14, 20, 21)"
      end
      spots, wide = r[1], r[2]
    else
      spots, wide = DRAW_SPOTS, DRAW_WIDE
    end
  end
  local objs
  objs, why = area_objects(ar, at, spots, wide)
  if not objs then return nil, why end
  if ar.setup ~= nil and type(ar.setup) ~= "function" then return nil, at .. ".setup must be a function" end
  local groups = ar.groups or m.groups
  if not groups then return nil, at .. ": no groups (the map's groups, or the area's)" end
  local copy_groups = {}
  for i, v in ipairs(groups) do copy_groups[i] = v end
  local loot = ar.loot or m.loot
  return {
    exits = exits_of(e), room = ar.room, fixed = ar.fixed,
    enemies = ar.enemies or 0, chests = ar.chests or 0, traps = ar.traps or 0, feats = ar.feats or 0,
    objects = objs, setup = ar.setup,
    tileset = tileset, name = ar.name or m.name or "", gold = ar.gold or m.gold or GOLD_DEFAULT,
    loot = LOOT[loot] or loot or 0,
    music = ar.music or m.music or (TILESET[tileset] and TILESET[tileset][2]) or MUSIC[tileset] or 1,
    background = ar.background or m.background, groups = copy_groups,
  }
end

-- Each exit leads to an area of the map that has the way back, and every area can be reached from
-- the start; nil or why not.
local function check_ways(areas, start)
  for a, ar in pairs(areas) do
    for l in ar.exits:gmatch(".") do
      local col = (a - 1) % 4 + 1
      local to = a + STEP[l]
      if to < 1 or to > GRID or (l == "E" and col == 4) or (l == "W" and col == 1) then
        return string.format(".areas[%d].exits: %s leads off the 4 by 4 grid", a, l)
      end
      local other = areas[to]
      if not other then return string.format(".areas[%d].exits: %s leads to area %d, which the map does not have", a, l, to) end
      if not other.exits:find(BACK[l], 1, true) then
        return string.format(".areas[%d].exits: %s leads to area %d, which has no %s exit back", a, l, to, BACK[l])
      end
    end
  end
  local seen, queue = { [start] = true }, { start }
  while #queue > 0 do
    local a = table.remove(queue)
    for l in areas[a].exits:gmatch(".") do
      local to = a + STEP[l]
      if not seen[to] then
        seen[to] = true
        queue[#queue + 1] = to
      end
    end
  end
  local list = {}
  for a in pairs(areas) do list[#list + 1] = a end
  table.sort(list)
  for _, a in ipairs(list) do
    if not seen[a] then return string.format(".areas[%d]: no way to it from the start, area %d", a, start) end
  end
  return nil
end

-- The map checked and copied, with each area's settings resolved; or nil and why not (after
-- "map", as define's error writes it).
norm_map = function(m)
  if type(m) ~= "table" then return nil, " must be a table" end
  for k in pairs(m) do
    if not MAP_FIELD[k] then return nil, " has no field " .. tostring(k) end
  end
  local why = check_settings(m, ".")
  if why then return nil, why end
  local tr = m.traps
  if tr ~= nil then
    if type(tr) ~= "table" then return nil, ".traps must be a table { shroom, shock, alarm, pit }" end
    for k, v in pairs(tr) do
      if not TRAP[k] then return nil, ".traps: no trap kind " .. tostring(k) .. " (shroom, shock, alarm, pit)" end
      if not whole(v, 0, 10) then return nil, ".traps." .. k .. " must be 0 to 10" end
    end
  end
  local bo = m.bonuses
  if bo ~= nil then
    if type(bo) ~= "table" then return nil, ".bonuses must be a table { feats, enemies, level_ups }" end
    for k, v in pairs(bo) do
      if not BONUS_NAME[k] then return nil, ".bonuses: no bonus " .. tostring(k) .. " (feats, enemies, level_ups)" end
      if not whole(v, 0, 99) then return nil, ".bonuses." .. k .. " must be 0 to 99" end
    end
  end
  if m.chatter ~= nil and type(m.chatter) ~= "boolean" then return nil, ".chatter must be true or false" end
  if type(m.areas) ~= "table" then return nil, ".areas must be a table of areas, 1 to 16" end
  local areas, low = {}, nil
  for a in pairs(m.areas) do
    if not whole(a, 1, GRID) then return nil, ".areas: an area is a number from 1 to 16 (" .. tostring(a) .. ")" end
  end
  for a = 1, GRID do
    if m.areas[a] ~= nil then
      local ar
      ar, why = norm_area(m, a, m.areas[a])
      if not ar then return nil, why end
      areas[a] = ar
      low = low or a
    end
  end
  if not low then return nil, ".areas: no area" end
  local start = m.start
  if start == nil then start = low end
  if not whole(start, 1, GRID) or not areas[start] then return nil, ".start: " .. tostring(start) .. " is not an area of the map" end
  why = check_ways(areas, start)
  if why then return nil, why end
  local t, b, sig = tr or {}, bo or {}, {}
  for a = 1, GRID do
    local ar = areas[a]
    if ar then
      sig[#sig + 1] = a .. ":" .. ar.tileset .. "/" .. (ar.fixed and ("f" .. ar.fixed) or ar.room or 0) .. "/" .. ar.exits
    end
  end
  return {
    areas = areas, start = start, chatter = m.chatter ~= false, tileset = m.tileset,
    traps = { t.shroom or 0, t.shock or 0, t.alarm or 0, t.pit or 0 },
    bonuses = { b.feats or 0, b.enemies or 0, b.level_ups or 0 },
    sig = table.concat(sig, ";"),
  }
end

-- The custom objects' functions, registered as rq.dungeon.on_object registers one.
map_objects = function(m, mod)
  for _, ar in pairs(m.areas) do
    for _, o in ipairs(ar.objects) do
      if o.kind == "custom" then
        known[number_of(o.id)] = o.id
        on_obj[o.id] = { fn = o.run, mod = mod }
      end
    end
  end
end

-- What get's map shows: the map's shape.
map_view = function(m)
  local areas = {}
  for a, ar in pairs(m.areas) do
    areas[a] = { exits = ar.exits, room = ar.room, fixed = ar.fixed, tileset = ar.tileset }
  end
  return { start = m.start, tileset = m.tileset, areas = areas }
end

-- ---- Moment 7: an area entered ----

-- An item's number from a number or a mod's item id (items' text ids: 1000 + FNV-1a % 9000).
local function item_no(v)
  if int(v) then return v end
  local h = 2166136261
  for i = 1, #v do h = ((h ~ v:byte(i)) * 16777619) & 0xffffffff end
  local no = 1000 + h % 9000
  local ok, id = pcall(rq.item.id_of, no)
  if ok and id == v then return no end
  return nil
end

-- A free spot of the room (as ＭＤランダム takes one, or a counted object's spot when none is free):
-- its index, cell and array.
local function free_spot(wide, a)
  local P, X, Y, n = "発生ＭＰ", "発生ＭＸ", "発生ＭＹ", 25
  if wide then P, X, Y, n = "広域ＭＰ", "広域ＭＸ", "広域ＭＹ", 5 end
  for i = 1, n do
    if gint(P, i) == 1 then return i, gint(X, i), gint(Y, i), P end
  end
  for i = 1, n do
    local xx, yy = gint(X, i), gint(Y, i)
    if gint(P, i) == 2 and xx > 0 and gint("Ｍ", a, "mapnn", xx, yy) == 0 then return i, xx, yy, P end
  end
  return nil
end

local function place_object(o, a, what)
  local code, tz, tv = PLACE[o.kind], 0, 0
  if o.kind == "custom" then
    code, tz, tv = CUSTOM_CODE, number_of(o.id), LOOKS[o.look] + (o.keep and KEEP or 0)
  elseif o.item ~= nil then
    tz = item_no(o.item)
    if not tz then
      logf("%s: no item %s; the chest holds the map's loot", what, tostring(o.item))
      tz = 0
    end
  end
  local spot = o.spot
  if type(spot) == "table" then
    local xx, yy = spot.x + 1, MAP - spot.z
    if yes(call("Ｒ８床確認", xx, yy)) and gint("Ｍ", a, "maptt", xx, yy) == 0 then
      return call("ＭＤ固定", xx, yy, code, tz, tv) == 1
    end
    logf("%s: cell %d,%d is not free floor; the object goes to a free spot", what, spot.x, spot.z)
    spot = "any"
  end
  local wide = spot == "wide"
  if o.kind ~= "custom" then
    if call("ＭＤランダム", wide and 1 or 0, code, tz, tv, 1) == 1 then return true end
  else
    -- ＭＤランダム has no prop for the loader's code: its way to a spot, then ＭＤ固定.
    local i, xx, yy, P = free_spot(wide, a)
    if i then
      set(P, i, 2)
      return call("ＭＤ固定", xx, yy, code, tz, tv) == 1
    end
  end
  logf("%s: no free %s left in the room", what, wide and "wide spot" or "spot")
  return false
end

local CAME_FROM = { [10] = "N", [11] = "S", [12] = "E", [13] = "W" }

local function rethrow_title(err)
  if tostring(err):find(TITLE, 1, true) then error(err, 0) end
end

local function area_settings(m, ar, what)
  local ok, err = pcall(function()
    call("ＭＡＰ基礎型登録", ar.tileset)
    if ar.background then set("◆戦闘背景番号", ar.background) end
    set("◆ＭＡＰ名前", ar.name)
    call("クエスト基本登録", ar.gold, ar.loot, ar.music)
    local t = m.traps
    call("トラップ確率登録", t[1], t[2], t[3], t[4])
    local nos = { 0, 0, 0, 0, 0 }
    for i, v in ipairs(ar.groups) do
      local n, why = priv.group_check(v)
      if n then nos[i] = n else logf("%s: groups: %s", what, tostring(why)) end
    end
    call("エンカウント中身登録", nos[1], nos[2], nos[3], nos[4], nos[5])
    local b = m.bonuses
    call("ボーナス登録", b[1], b[2], b[3])
  end)
  if not ok then
    rethrow_title(err)
    logf("%s: the settings: %s", what, tostring(err))
  end
end

local function enter_area(def, id, no, a)
  local m = def.map
  local ar = m.areas[a]
  local what = string.format("rq.quests.define (%s) %s: area %d", def.mod, id, a)
  if not ar then
    -- An area the map does not have (a save from another version of the mod): the start's
    -- settings, a drawn room and the way back.
    local s = m.areas[m.start]
    ar = setmetatable({ exits = CAME_FROM[gint("◆キャラ位置")] or "", enemies = 0, chests = 0, traps = 0,
      feats = 0, objects = {}, setup = false }, { __index = s })
    logf("%s is not on the map; a drawn room with the way back", what)
  end
  area_settings(m, ar, what)
  local first = gint("Ｍ", a, "初登録") == 0
  local base = TILESET[ar.tileset]
  local mapno = 0
  if m.areas[a] then mapno = ar.fixed or (ar.room and base and base[1] + ar.room) or 0 end
  local e = ar.exits
  local function bit(l) return e:find(l, 1, true) and 1 or 0 end
  call("ＭＡＰ読み込み", a, mapno, bit("N"), bit("S"), bit("E"), bit("W"), ar.enemies, ar.chests, ar.traps, ar.feats)
  if first then
    for i, o in ipairs(ar.objects) do
      local fine, why = pcall(place_object, o, a, what .. ": objects[" .. i .. "]")
      if not fine then
        rethrow_title(why)
        logf("%s: objects[%d]: %s", what, i, tostring(why))
      end
    end
  end
  if ar.setup then
    local ctx = { quest = no, id = id, area = a, row = (a - 1) // 4 + 1, col = (a - 1) % 4 + 1, first = first }
    local fine, why = pcall(ar.setup, ctx)
    if not fine then
      rethrow_title(why)
      logf("%s: setup: %s", what, tostring(why))
    end
  end
  return 0
end

-- ---- Moment 8: the prologue, the rooms kept, the first area ----

-- The rooms drawn are kept in the quest's entry (Ｑ[no].Ｍ固有番号記憶) as the game keeps its own; a
-- new shape of the map (an area's tileset, room or exits) draws them anew. The shape is kept in the
-- loader's part of rq.savedata, by the quest's id.
local function keep_rooms(def, id, no)
  local ok, all = pcall(priv.as_mod, SAVE_MOD, function() return rq.savedata.get(SAVE_KEY) end)
  if not ok or type(all) ~= "table" then all = {} end
  if all[id] == def.map.sig then return end
  call("Ｑ初期化／エリア情報", no)
  all[id] = def.map.sig
  local fine, err = pcall(priv.as_mod, SAVE_MOD, function() rq.savedata.set(SAVE_KEY, all) end)
  if not fine then logf("rq.quests.define %s: keeping the map's shape: %s", id, tostring(err)) end
end

local function start_map(def, id, no, run_quest)
  if def.run then
    local r = run_quest(def, id, no)
    if r then return r end
  end
  keep_rooms(def, id, no)
  local ok, err = pcall(call, "ＱＭ登録", def.map.start)
  if not ok then
    rethrow_title(err)
    logf("rq.quests.define (%s) %s: entering the map: %s; the quest is given up", def.mod, id, tostring(err))
    return RESULT.abandon
  end
  return 0
end

map_moment = function(def, id, no, moment, area, fg, run_quest)
  if moment == 8 then return start_map(def, id, no, run_quest) end
  if moment == 7 then return enter_area(def, id, no, area) end
  -- Moment 9: a mod's objects never reach here (the hook on クエスト処理 answers them), and a mod's
  -- map has no events of the game's. The rest gets the game's own answer, with its party lines
  -- after new areas held back when chatter is false (the count they wait on; the rules it keeps
  -- after a fight or a warp still run).
  if fg == 4 then return 0 end
  if not def.map.chatter and fg == 5 then set("◆ＢＭＡＰ新規読み込み回数", 0) end
  return call("割り込み２", fg)
end

-- ---- A save on a mod's map whose mod is gone ----

-- Placed at the loader's start: with no mod defining the quest, no hook of a mod's would run. The
-- first time the map hands over to the quest (a fight, a touch, the menu key) it ends as with the
-- escape device, the quest storage's items kept.
hook("Ｔマップ割り込み", { before = function(h)
  local no = gint("◆iiq")
  if not mod_no(no) or gint("◆ＭＡＰ実装") ~= 1 then return end
  local id = id_at(no)
  if not id then return end
  local def = defs[id]
  if def and def.map then return end
  logf("rq.quests: quest %d (%s): on a map no loaded mod defines; it ends as with the escape device", no, id)
  call("クエスト終了処理", RESULT.escape)
  call("Ｒ８リザルト画面", RESULT.escape)
  set("◆iiq", 0)
  h.skip, h.result = true, 1
end })
