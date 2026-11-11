-- rq.monster (loader 2.36.0, notes/plans/plan-monsters-modes.md in the workspace):
--   rq.monster.get(no)              a monster's data, as the game's list has it
--   rq.monster.edit(no, patch)      changes a monster each time the game builds it
--   rq.monster.on_create(fn)        each enemy a fight builds, and the familiar
--
-- Built only on public calls: rq.ain.call, global, set_global, hook and stack.
--   Ｃ敵登録(iic, no)        the list: one if per monster, each calling Ｃ敵登録１, then ＣＬＶ and
--                           敵台詞登録 (the lines, Ｃ台詞 2 to 5). A number with no block builds nothing.
--   Ｃ敵登録１(44 args)      iic, no, model, name, boss, race, type, HP, speed, attack, defense, magic,
--                           magic defense, hit, evasion, crit, impact, resist, weak element, the five
--                           resistances, then five times (skill, text, motion, chance). It applies
--                           the world's tiers (random), race 60 and ◆敵強化率, then writes the slot.
--   Callers of Ｃ敵登録: ＰＴＢ登録 (a fight's enemies, slots 1 to 5), Ｔ戦闘前初期化 (the familiar,
--   slot 10), Ｃログ情報 (the familiar, for a log line), モンスター固有番号文字列 (a name, slot 1).
-- The slots: enemies 1 to 5, the familiar 10, the roster from 20; the game never names 6 to 9,
-- so get builds in 9.
local rq, priv = ...
local call, hook, global, set_global = rq.ain.call, rq.ain.hook, rq.ain.global, rq.ain.set_global

local monster = {}
rq.monster = monster

local SPARE = 9
local FAMILIAR = 10
local INT_MAX = 2147483647

local function int(v)
  return math.type(v) == "integer"
end

local function logf(fmt, ...)
  rq.log(string.format(fmt, ...))
end

local function mod_id()
  local ok, id = pcall(function() return rq.mod and rq.mod.id and rq.mod.id() end)
  return ok and id or "?"
end

local function need_game(name)
  if not pcall(global, "◆金") then
    error(name .. ": the game is not running yet. Call it from rq.on_frame or a callback, not at the top level of a mod", 3)
  end
end

-- ---- The fields ----

-- Ｃ敵登録１'s arguments by field (1-based, as h.args).
local ARG = {
  model = 3, name = 4, boss = 5, race = 6, type = 7, hp = 8, spd = 9, atk = 10, def = 11, mag = 12,
  mdef = 13, hit = 14, eva = 15, crit = 16, impact = 17, resist = 18, weak_element = 19,
  fire_res = 20, ice_res = 21, thunder_res = 22, light_res = 23, dark_res = 24,
}
local NUMBERS = { "model", "race", "type", "hp", "spd", "atk", "def", "mag", "mdef", "hit", "eva",
  "crit", "impact", "resist", "weak_element", "fire_res", "ice_res", "thunder_res", "light_res",
  "dark_res" }
local SKILL0 = 25 -- skill k: id at SKILL0 + 4(k-1), then text, motion, chance
local LINES = { wait = 2, attack = 3, big = 4, support = 5 }
local LINE_KINDS = { "wait", "attack", "big", "support" }

-- The slot's own setters and getters (-1 reads), for on_create's writes.
local SLOT_FN = {
  level = "ＣＬＶ", spd = "Ｃ素早さ", atk = "Ｃ攻撃力", def = "Ｃ防御力", mag = "Ｃ魔法力",
  mdef = "Ｃ魔抵力", hit = "Ｃ命中率", eva = "Ｃ回避率", crit = "Ｃ必殺率", impact = "Ｃ衝撃率",
  resist = "Ｃ耐性率", weak_element = "Ｃ弱点属性", fire_res = "Ｃ炎耐性", ice_res = "Ｃ氷耐性",
  thunder_res = "Ｃ雷耐性", light_res = "Ｃ光耐性", dark_res = "Ｃ闇耐性",
}
local WRITABLE = { name = true, hp = true }
for k in pairs(SLOT_FN) do WRITABLE[k] = true end

local function copy(t)
  if type(t) ~= "table" then return t end
  local out = {}
  for k, v in pairs(t) do out[k] = copy(v) end
  return out
end

-- The data from Ｃ敵登録１'s arguments (after the edits), as get returns it.
local function data_of(a)
  local d = { no = a[2], boss = (a[ARG.boss] or 0) ~= 0, name = a[ARG.name] or "" }
  for _, k in ipairs(NUMBERS) do d[k] = a[ARG[k]] or 0 end
  local skills = {}
  for k = 1, 5 do
    local at = SKILL0 + 4 * (k - 1)
    local id = a[at] or 0
    if id ~= 0 then
      skills[#skills + 1] = { slot = k, id = id, text = a[at + 1] or "", motion = a[at + 2] or 0,
        chance = a[at + 3] or 0 }
    end
  end
  d.skills = skills
  return d
end

local function read_lines(iic)
  local out = {}
  for _, k in ipairs(LINE_KINDS) do
    local ok, text = pcall(call, "Ｃ台詞", iic, LINES[k], "")
    if ok and type(text) == "string" and text ~= "" then out[k] = text end
  end
  return out
end

-- ---- Edits ----

local patches = {} -- no -> { { mod, seq, patch }, ... }
local seq = 0

-- The patches of one monster in load order (rq.mod.list), then any mod not listed yet.
local function ordered(no)
  local list = patches[no]
  if not list or #list == 0 then return nil end
  local rank = {}
  local ok, ids = pcall(rq.mod.list)
  if ok and type(ids) == "table" then
    for i, id in ipairs(ids) do rank[id] = i end
  end
  local out = {}
  for i, p in ipairs(list) do out[i] = p end
  table.sort(out, function(x, y)
    local a, b = rank[x.mod] or math.huge, rank[y.mod] or math.huge
    if a ~= b then return a < b end
    return x.seq < y.seq
  end)
  return out
end

local function apply_args(args, no)
  local list = ordered(no)
  if not list then return end
  for _, p in ipairs(list) do
    local patch = p.patch
    for k, at in pairs(ARG) do
      local v = patch[k]
      if v ~= nil then
        if k == "boss" then v = v and 1 or 0 end
        args[at] = v
      end
    end
    if patch.skills then
      for k = 1, 5 do
        local s = patch.skills[k]
        local at = SKILL0 + 4 * (k - 1)
        if s == false then
          args[at], args[at + 1], args[at + 2], args[at + 3] = 0, "", 1, 0
        elseif type(s) == "table" then
          if s.id ~= nil then args[at] = s.id end
          if s.text ~= nil then args[at + 1] = s.text end
          if s.motion ~= nil then args[at + 2] = s.motion end
          if s.chance ~= nil then args[at + 3] = s.chance end
        end
      end
    end
  end
end

-- level and lines, written after Ｃ敵登録 (the game writes them after Ｃ敵登録１).
local function apply_after(iic, no)
  local list = ordered(no)
  if not list then return end
  for _, p in ipairs(list) do
    local patch = p.patch
    if patch.level ~= nil then call("ＣＬＶ", iic, patch.level) end
    if patch.lines then
      for _, k in ipairs(LINE_KINDS) do
        local v = patch.lines[k]
        if v == false or v == "" then
          set_global("Ｃ", iic, "台詞", LINES[k], "")
        elseif type(v) == "string" then
          call("Ｃ台詞", iic, LINES[k], v)
        end
      end
    end
  end
end

-- ---- The hooks ----

local hooked = false
local capturing = nil   -- get: { data = ... } while it builds in the spare slot
local building = {}     -- iic -> { no, base } from Ｃ敵登録１, until Ｃ敵登録 returns
local create_list = {}  -- on_create callbacks: { fn, mod, seen }

-- The function that called Ｃ敵登録, from the AIN stack.
local function caller()
  local ok, st = pcall(rq.ain.stack, 8)
  if not ok or type(st) ~= "table" then return nil end
  for _, name in ipairs(st) do
    if name ~= "Ｃ敵登録" then return name end
  end
  return nil
end

local function run_create(h, ctx)
  local ok, err = pcall(h.fn, ctx)
  if ok then return end
  local text = tostring(err)
  if not h.seen[text] then
    h.seen[text] = true
    logf("rq.monster.on_create (%s): %s", h.mod, text)
  end
end

local function count(v)
  if type(v) ~= "number" or v ~= v then return nil end
  v = math.floor(v)
  if v < 0 then v = 0 elseif v > INT_MAX then v = INT_MAX end
  return math.tointeger(v)
end

-- What the enemy in the slot is now, in on_create's names.
local function slot_values(iic)
  local v = { name = call("Ｃ名前", iic, ""), hp = call("Ｃ体力最大", iic, -1) }
  for k, fn in pairs(SLOT_FN) do v[k] = call(fn, iic, -1) end
  return v
end

local function write_slot(iic, built, vals)
  if vals.name ~= built.name and vals.name ~= "" then call("Ｃ名前", iic, vals.name) end
  if vals.hp ~= built.hp then
    call("Ｃ体力最大", iic, vals.hp)
    call("Ｃ体力", iic, vals.hp)
  end
  for k, fn in pairs(SLOT_FN) do
    if vals[k] ~= built[k] then call(fn, iic, vals[k]) end
  end
end

local function fire_create(iic, no, base, familiar)
  local built = slot_values(iic)
  local vals = copy(built)
  local function gi(name)
    local ok, v = pcall(global, name)
    return ok and int(v) and v or 0
  end
  local fields = {
    iic = iic, no = no, rank = familiar and 0 or iic, familiar = familiar,
    world = rq.world and rq.world.current and rq.world.current() or 0,
    boss = base.boss, fight_rank = gi("◆戦闘ランク"), group = gi("◆戦闘グループ"),
    strength = gi("◆敵強化率"),
  }
  local ctx = setmetatable({}, {
    __index = function(_, k)
      if k == "base" then return copy(base) end
      if WRITABLE[k] then return vals[k] end
      return fields[k]
    end,
    __newindex = function(_, k, v)
      if k == "name" then
        if type(v) ~= "string" or v == "" then error("rq.monster.on_create: ctx.name must be a text", 2) end
        vals.name = v
      elseif WRITABLE[k] then
        local n = count(v)
        if n == nil then error("rq.monster.on_create: ctx." .. k .. " must be a number", 2) end
        vals[k] = n
      else
        error("rq.monster.on_create: ctx." .. tostring(k) .. " cannot be written", 2)
      end
    end,
  })
  for _, h in ipairs({ table.unpack(create_list) }) do run_create(h, ctx) end
  local ok, err = pcall(write_slot, iic, built, vals)
  if not ok then logf("rq.monster.on_create: writing slot %d: %s", iic, tostring(err)) end
end

local function install()
  if hooked then return end
  hooked = true
  hook("Ｃ敵登録１", {
    before = function(h)
      local a = h.args
      local iic, no = a[1], a[2]
      if not int(iic) or not int(no) then return end
      local ok, err = pcall(apply_args, a, no)
      if not ok then logf("rq.monster.edit: monster %d: %s", no, tostring(err)) end
      local base = data_of(a)
      if capturing and iic == SPARE then
        capturing.data = base
        h.skip = true -- no world tier drawn, nothing written
      end
      building[iic] = { no = no, base = base }
    end,
  })
  hook("Ｃ敵登録", {
    before = function(h)
      local iic = h.args[1]
      if int(iic) then building[iic] = nil end
    end,
    after = function(h)
      local iic = h.args[1]
      if not int(iic) then return end
      local b = building[iic]
      building[iic] = nil
      if not b then return end -- nothing built
      local ok, err = pcall(apply_after, iic, b.no)
      if not ok then logf("rq.monster.edit: monster %d: %s", b.no, tostring(err)) end
      local okl, level = pcall(call, "ＣＬＶ", iic, -1)
      b.base.level = okl and int(level) and level or 0
      b.base.lines = read_lines(iic)
      if capturing and iic == SPARE then return end
      if #create_list == 0 then return end
      local who = caller()
      local familiar = iic == FAMILIAR and who == "Ｔ戦闘前初期化"
      if (iic >= 1 and iic <= 5 and who == "ＰＴＢ登録") or familiar then
        local ok2, err2 = pcall(fire_create, iic, b.no, b.base, familiar)
        if not ok2 then logf("rq.monster.on_create: %s", tostring(err2)) end
      end
    end,
  })
end

-- ---- The calls ----

local function check_no(no, name)
  if not int(no) or no < 1 then error(name .. ": the monster number must be a whole number, 1 or more", 3) end
end

function monster.get(no)
  check_no(no, "rq.monster.get")
  need_game("rq.monster.get")
  install()
  local cap = { data = nil }
  pcall(call, "Ｃ初期化", SPARE)
  capturing = cap
  local ok, err = pcall(call, "Ｃ敵登録", SPARE, no)
  capturing = nil
  pcall(call, "Ｃ初期化", SPARE)
  building[SPARE] = nil
  if not ok then error("rq.monster.get: " .. tostring(err), 2) end
  return cap.data and copy(cap.data) or nil
end

local FIELDS = { level = "number", name = "string", boss = "boolean", skills = "table", lines = "table" }
for _, k in ipairs(NUMBERS) do FIELDS[k] = "number" end

local function check_patch(no, patch)
  local function bad(fmt, ...)
    error(string.format("rq.monster.edit: monster %d: " .. fmt, no, ...), 4)
  end
  for k, v in pairs(patch) do
    local want = FIELDS[k]
    if not want then bad("no field %s", tostring(k)) end
    if want == "number" then
      if not int(v) or v < 0 then bad("%s must be a whole number, 0 or more", k) end
    elseif type(v) ~= want then
      bad("%s must be a %s", k, want == "string" and "text" or want == "boolean" and "true or false" or "table")
    end
  end
  if patch.name == "" then bad("name cannot be empty") end
  if patch.skills then
    for k, s in pairs(patch.skills) do
      if not int(k) or k < 1 or k > 5 then bad("skills: a position from 1 to 5, not %s", tostring(k)) end
      if s ~= false and type(s) ~= "table" then bad("skills[%d] must be a table or false", k) end
      if type(s) == "table" then
        for f, v in pairs(s) do
          if f == "text" then
            if type(v) ~= "string" then bad("skills[%d].text must be a text", k) end
          elseif f == "id" or f == "motion" or f == "chance" then
            if not int(v) or v < 0 then bad("skills[%d].%s must be a whole number, 0 or more", k, f) end
          else
            bad("skills[%d] has no field %s", k, tostring(f))
          end
        end
      end
    end
  end
  if patch.lines then
    for k, v in pairs(patch.lines) do
      if not LINES[k] then bad("lines: no kind %s (wait, attack, big, support)", tostring(k)) end
      if v ~= false and type(v) ~= "string" then bad("lines.%s must be a text or false", k) end
    end
  end
end

function monster.edit(no, patch)
  check_no(no, "rq.monster.edit")
  local okn, known = pcall(rq.ids.name, "monster", no)
  if okn and known == nil then
    error("rq.monster.edit: no monster number " .. no, 2)
  end
  if patch ~= nil and type(patch) ~= "table" then
    error("rq.monster.edit: the patch must be a table, or nil to remove it", 2)
  end
  if patch then check_patch(no, patch) end
  local mod = mod_id()
  local list = patches[no] or {}
  patches[no] = list
  for i = #list, 1, -1 do
    if list[i].mod == mod then table.remove(list, i) end
  end
  if patch then
    seq = seq + 1
    list[#list + 1] = { mod = mod, seq = seq, patch = copy(patch) }
    install()
  end
end

function monster.on_create(fn)
  if type(fn) ~= "function" then error("rq.monster.on_create: expected a function", 2) end
  local h = { fn = fn, mod = mod_id(), seen = {} }
  create_list[#create_list + 1] = h
  install()
  return function()
    for i, x in ipairs(create_list) do
      if x == h then table.remove(create_list, i) return end
    end
  end
end

-- For rq.battle.start (basics.lua): whether the list has a number.
priv.monster_exists = function(no)
  return monster.get(no) ~= nil
end
