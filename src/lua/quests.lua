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
local rq = ...
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
  return {
    id = no,
    text_id = (id_at(no)),
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
  run = true }
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
  if type(def.run) ~= "function" then return "run must be a function" end
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
  set("Ｑ", no, "種類", def.fights and 2 or 3)
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
-- the dispatcher) runs first, as for a game quest.
local function run_quest(def, id, no)
  local q = { id = id, no = no, tries = gint("Ｑ", no, "参加回数"), clears = gint("Ｑ", no, "クリア回数") }
  q.first = q.clears == 0
  local ok, r = pcall(def.run, q)
  if not ok then
    if tostring(r):find(TITLE, 1, true) then error(r, 0) end
    logf("rq.quests.define (%s) %s: run: %s; the quest is given up", def.mod, id, tostring(r))
    return RESULT.abandon
  end
  if r == nil then return RESULT.clear end
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
    if h.args[1] ~= 8 then return end
    local no = quest_now()
    local id = id_at(no)
    local def = id and defs[id]
    if not def then return end
    h.skip, h.result = true, run_quest(def, id, no)
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
  local mod = mod_id()
  local old = defs[id]
  if old and old.mod ~= mod then error("rq.quests.define: " .. id .. " is defined by " .. old.mod, 2) end
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
    id = id, mod = mod, raw = raw, run = def.run,
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

local RUN = { fame = true, exp_bonus = true, bonuses = true, party_change = true, no_area_map = true, no_warp = true }

local function check_run(f)
  for k in pairs(f) do
    if not RUN[k] then return "no field " .. tostring(k) .. " to write" end
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
  if r ~= nil and not int(r) then
    logf("rq.dungeon.on_object (%s) %s: the result must be a whole number or nil", h.mod, id)
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
