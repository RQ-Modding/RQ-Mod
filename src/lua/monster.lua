-- rq.monster (loader 2.36.0):
--   rq.monster.get(no)              a monster's data, as the game's list has it
--   rq.monster.edit(no, patch)      changes a monster each time the game builds it
--   rq.monster.on_create(fn)        each enemy a fight builds, and the familiar
-- New monsters and enemy groups (loader 2.40.0):
--   rq.monster.define(def)          a monster of a mod's own on a game monster's data, its number
--                                   1000 + an FNV-1a hash of its id; its capture and drops
--   rq.monster.group(def)           an enemy group of a mod's own (rq.encounters.set_groups and
--                                   rq.battle.start's group are in basics.lua)
--   rq.monster.models(), model(def) the model table, and a game model at another scale (the table
--                                   is C: hll/MonsterInfo.c, through the private table)
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
--   Ｃカスタム登録(iic, no, name)   the game's build by name; the Data-less Ghost for an unknown name
--   ■Ｍ／撃破数, ■Ｍ／弱点把握(no, aa, how)   the kills and the weak point known, 501 each: 0 past 500
--   雑魚セット生成(placed)    draws the fight's group (◆戦闘グループ) from ◆エンカウント中身[1..5] and
--                           its line-up (◆前回エンカウント乱数, 6 for a placed enemy one time in 3);
--                           each group's block sets the EXP and the rank (FA[3]) and calls ＰＴＢ登録
--   エンカウント中身登録(a1..a5)   an area's groups; 戦闘グループ文字列(no) a group's name for the log
--   Ｔ特殊行動(user, target, skill)   the capture (skills 166, 451, 531): the chance, then
--                           確率演算(chance) && no item yet && Ｉクエスト倉庫は満杯ですか？() == 0, then the
--                           item and CG from the monster number; 捕獲／文字列(item) the scene's text
-- The slots: enemies 1 to 5, the familiar 10, the roster from 20; the game never names 6 to 9,
-- so get builds in 9.
local rq, priv = ...
local call, hook, global, set_global = rq.ain.call, rq.ain.hook, rq.ain.global, rq.ain.set_global

local monster = {}
rq.monster = monster

local SPARE = 9
local FAMILIAR = 10
local INT_MAX = 2147483647
local MOD_FIRST = 1000 -- a mod's monsters and groups: 1000 + hash % 9000
local CAPTURE_SKILLS = { [166] = true, [451] = true, [531] = true }

local function int(v)
  return math.type(v) == "integer"
end

local function logf(fmt, ...)
  rq.log(string.format(fmt, ...))
end

local logged = {}
local function log_once(key, fmt, ...)
  if logged[key] then return end
  logged[key] = true
  logf(fmt, ...)
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

local function gint(...)
  local ok, v = pcall(global, ...)
  if ok and int(v) then return v end
  return 0
end

-- The text ids' numbers, as items and characters have them (modloader_item.c): FNV-1a.
local function fnv(s)
  local h = 2166136261
  for i = 1, #s do
    h = ((h ~ s:byte(i)) * 16777619) & 0xffffffff
  end
  return h
end

local function id_no(id)
  return MOD_FIRST + fnv(id) % 9000
end

local function game_monster(no)
  local ok, name = pcall(rq.ids.name, "monster", no)
  return not ok or name ~= nil
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

-- ---- A mod's monsters (2.40.0) ----

local defs = {}    -- no -> { no, id, mod, base, patch, capture }
local def_ids = {} -- id -> no

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
  local def = defs[d.no]
  if def then
    d.id = def.id
    d.base = def.base
  end
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

-- One patch (an edit's, or a definition's fields) on Ｃ敵登録１'s arguments.
local function apply_patch(args, patch)
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

local function apply_args(args, no)
  local list = ordered(no)
  if not list then return end
  for _, p in ipairs(list) do apply_patch(args, p.patch) end
end

-- level and lines of one patch, written after Ｃ敵登録 (the game writes them after Ｃ敵登録１).
local function apply_after_patch(iic, patch)
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

local function apply_after(iic, no)
  local list = ordered(no)
  if not list then return end
  for _, p in ipairs(list) do apply_after_patch(iic, p.patch) end
end

-- The drops of a monster: its definition's, unless an edit's patch has drops (the last in load
-- order wins, as every field of a patch).
local function drops_of(no)
  local out = defs[no] and defs[no].patch.drops or nil
  local list = ordered(no)
  if list then
    for _, p in ipairs(list) do
      if p.patch.drops ~= nil then out = p.patch.drops end
    end
  end
  return out
end

-- ---- The hooks ----

local hooked = false
local capturing = nil   -- get: { data = ... } while it builds in the spare slot
local building = {}     -- iic -> { no, base } from Ｃ敵登録１, until Ｃ敵登録 returns
local redirect = {}     -- iic -> the definition whose base Ｃ敵登録 builds, until it returns
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
  local fields = {
    iic = iic, no = no, id = defs[no] and defs[no].id or nil, rank = familiar and 0 or iic,
    familiar = familiar, world = rq.world and rq.world.current and rq.world.current() or 0,
    boss = base.boss, fight_rank = gint("◆戦闘ランク"), group = gint("◆戦闘グループ"),
    strength = gint("◆敵強化率"),
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

-- After Ｃ敵登録 built something in iic: the definition's and the edits' level and lines, then
-- on_create and the familiar's watch.
local function after_build(iic, b)
  local def = defs[b.no]
  if def then
    local ok, err = pcall(apply_after_patch, iic, def.patch)
    if not ok then logf("rq.monster.define: %s: %s", def.id, tostring(err)) end
  end
  local ok, err = pcall(apply_after, iic, b.no)
  if not ok then logf("rq.monster.edit: monster %d: %s", b.no, tostring(err)) end
  local okl, level = pcall(call, "ＣＬＶ", iic, -1)
  b.base.level = okl and int(level) and level or 0
  b.base.lines = read_lines(iic)
  if capturing and iic == SPARE then return end
  local watch = iic == FAMILIAR and priv.familiar_built
  if #create_list == 0 and not watch then return end
  local who = caller()
  local familiar = iic == FAMILIAR and who == "Ｔ戦闘前初期化"
  if #create_list > 0 and ((iic >= 1 and iic <= 5 and who == "ＰＴＢ登録") or familiar) then
    local ok2, err2 = pcall(fire_create, iic, b.no, b.base, familiar)
    if not ok2 then logf("rq.monster.on_create: %s", tostring(err2)) end
  end
  -- combat.lua (2.37.0): the familiar's speed as built, before the game holds it to the
  -- party's top speed, for rq.battle.familiar and on_order.
  if familiar and watch then
    local ok3, err3 = pcall(priv.familiar_built, b.no)
    if not ok3 then logf("rq.battle.familiar: %s", tostring(err3)) end
  end
end

-- Placed when monster.lua runs, so a save whose mod is gone still gets a monster for a number of
-- 1000 or more (a familiar item, a group in an area's list).
local function install()
  if hooked then return end
  hooked = true
  hook("Ｃ敵登録１", {
    before = function(h)
      local a = h.args
      local iic, no = a[1], a[2]
      if not int(iic) or not int(no) then return end
      -- A mod's monster: Ｃ敵登録 runs its base's block, which comes here with the base's number.
      local r = redirect[iic]
      if r and no == r.base then
        redirect[iic] = nil
        no = r.no
        a[2] = no
        local okd, errd = pcall(apply_patch, a, r.patch)
        if not okd then logf("rq.monster.define: %s: %s", r.id, tostring(errd)) end
      end
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
      local iic, no = h.args[1], h.args[2]
      if not int(iic) then return end
      building[iic] = nil
      redirect[iic] = nil
      local def = int(no) and defs[no]
      if def then
        redirect[iic] = def
        h.args[2] = def.base
      end
    end,
    after = function(h)
      local iic = h.args[1]
      if not int(iic) then return end
      local r = redirect[iic]
      redirect[iic] = nil
      local b = building[iic]
      building[iic] = nil
      local no = r and r.no or h.args[2]
      if not b and int(no) and no >= MOD_FIRST and not defs[no] and not (capturing and iic == SPARE) then
        -- A mod's number with no definition (its mod is gone): the game's Data-less Ghost.
        log_once("ghost" .. no, "rq.monster: no monster %d (a mod's that is not loaded?); built as the Data-less Ghost", no)
        local ok, err = pcall(call, "Ｃカスタム登録", iic, no, "")
        if not ok then logf("rq.monster: monster %d: %s", no, tostring(err)) end
        b = building[iic]
        building[iic] = nil
      end
      if not b then return end -- nothing built
      after_build(iic, b)
    end,
  })
end

-- ---- The calls ----

local function check_no(no, name)
  if not int(no) or no < 1 then error(name .. ": the monster number must be a whole number, 1 or more", 3) end
end

-- A number, or a defined monster's text id.
local function resolve(v, name)
  if type(v) == "string" then
    local no = def_ids[v]
    if not no then error(name .. ": no monster with the id " .. v, 3) end
    return no
  end
  check_no(v, name)
  return v
end

-- The kills and the weak point known (2.40.0), through the game's own accessors; a mod's monster's
-- are answered from the save (hook_kills).
local function kept(no)
  local okk, kills = pcall(call, "■Ｍ／撃破数", no, 0, "")
  local okw, weak = pcall(call, "■Ｍ／弱点把握", no, 0, "")
  return okk and int(kills) and kills or 0, okw and weak == 1
end

function monster.get(no)
  no = resolve(no, "rq.monster.get")
  need_game("rq.monster.get")
  install()
  local cap = { data = nil }
  pcall(call, "Ｃ初期化", SPARE)
  capturing = cap
  local ok, err = pcall(call, "Ｃ敵登録", SPARE, no)
  capturing = nil
  pcall(call, "Ｃ初期化", SPARE)
  building[SPARE] = nil
  redirect[SPARE] = nil
  if not ok then error("rq.monster.get: " .. tostring(err), 2) end
  if not cap.data then return nil end
  local d = copy(cap.data)
  d.kills, d.weak_known = kept(no)
  return d
end

local FIELDS = { level = "number", name = "string", boss = "boolean", skills = "table", lines = "table",
  drops = "table" }
for _, k in ipairs(NUMBERS) do FIELDS[k] = "number" end
local MAX_DROPS = 8

local function check_item_ref(v)
  return (int(v) and v > 0) or (type(v) == "string" and v ~= "")
end

-- who: the error's start, "rq.monster.edit: monster 12" or "rq.monster.define: mymod:x".
local function check_patch(who, patch)
  local function bad(fmt, ...)
    error(string.format(who .. ": " .. fmt, ...), 4)
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
  if patch.drops then
    local n = #patch.drops
    if n > MAX_DROPS then bad("drops: at most %d", MAX_DROPS) end
    for k, d in pairs(patch.drops) do
      if not int(k) or k < 1 or k > n then bad("drops must be a list") end
      if type(d) ~= "table" then bad("drops[%d] must be a table", k) end
      for f in pairs(d) do
        if f ~= "item" and f ~= "chance" then bad("drops[%d] has no field %s", k, tostring(f)) end
      end
      if not check_item_ref(d.item) then bad("drops[%d].item must be an item number or a text id", k) end
      if not int(d.chance) or d.chance < 1 or d.chance > 100 then bad("drops[%d].chance must be a whole number from 1 to 100", k) end
    end
  end
end

local drops_hooked = false
local install_drops -- below

function monster.edit(no, patch)
  no = resolve(no, "rq.monster.edit")
  if not defs[no] then
    local okn, known = pcall(rq.ids.name, "monster", no)
    if okn and known == nil then
      error("rq.monster.edit: no monster number " .. no, 2)
    end
  end
  if patch ~= nil and type(patch) ~= "table" then
    error("rq.monster.edit: the patch must be a table, or nil to remove it", 2)
  end
  if patch then check_patch("rq.monster.edit: monster " .. no, patch) end
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
    if patch.drops then install_drops() end
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

-- ---- The kills and weak points of a mod's monsters, in the save (2.40.0) ----
--
-- The loader's own part of rq.savedata (as_mod), by the monster's id: { kills = n, weak = 0 or 1 }.

local SAVE_MOD, SAVE_KEY = "rq:loader", "monsters"

local function saved_all()
  local ok, all = pcall(priv.as_mod, SAVE_MOD, function() return rq.savedata.get(SAVE_KEY) end)
  if ok and type(all) == "table" then return all end
  return {}
end

local function saved(id, field)
  local r = saved_all()[id]
  local v = type(r) == "table" and r[field] or 0
  return int(v) and v or 0
end

local function save(id, field, v)
  local all = saved_all()
  local r = type(all[id]) == "table" and all[id] or {}
  r[field] = v
  all[id] = r
  local ok, err = pcall(priv.as_mod, SAVE_MOD, function() rq.savedata.set(SAVE_KEY, all) end)
  if not ok then log_once("save" .. tostring(err), "rq.monster: keeping %s's %s: %s", id, field, tostring(err)) end
end

local kills_hooked = false
local function hook_kills()
  if kills_hooked then return end
  kills_hooked = true
  for _, spec in ipairs({ { "■Ｍ／撃破数", "kills" }, { "■Ｍ／弱点把握", "weak" } }) do
    hook(spec[1], { before = function(h)
      local no = h.args[1]
      local def = int(no) and defs[no]
      if not def then return end
      h.skip = true
      local aa, how = h.args[2], h.args[3]
      local now = saved(def.id, spec[2])
      if aa == 0 then
        h.result = now
        return
      end
      if how == "加算" then now = now + aa else now = aa end
      save(def.id, spec[2], now)
      h.result = 0
    end })
  end
end

-- ---- Items by number or text id ----

local function item_no(v)
  if int(v) then return v end
  local no = id_no(v)
  local ok, id = pcall(rq.item.id_of, no)
  if ok and id == v then return no end
  return nil
end

local function item_exists(no)
  local ok, info = pcall(rq.item.info, no)
  return ok and info ~= nil
end

-- ---- The capture (2.40.0) ----
--
-- In Ｔ特殊行動 the capture asks Ｉクエスト倉庫は満杯ですか？ twice: in the chance's rules, then in the
-- test after 確率演算(chance) and "no item yet". At the second, after a roll that succeeded, a mod's
-- monster gets its item and CG, which the game's table (by monster number) then leaves alone.

local capture_items = {} -- item number -> the scene's text
local cap = nil          -- { target, stage, rolled } while a capture skill runs
local capture_hooked = false

-- Whether the hooked function (name) was called by Ｔ特殊行動 itself.
local function in_capture_skill(name)
  local ok, st = pcall(rq.ain.stack, 4)
  if not ok or type(st) ~= "table" then return false end
  for _, f in ipairs(st) do
    if f ~= name then return f == "Ｔ特殊行動" end
  end
  return false
end

local function hook_capture()
  if capture_hooked then return end
  capture_hooked = true
  hook("Ｔ特殊行動", {
    before = function(h)
      cap = CAPTURE_SKILLS[h.args[3]] and { target = h.args[2], stage = 0 } or nil
    end,
    after = function() cap = nil end,
  })
  hook("確率演算", { after = function(h)
    if cap and cap.stage == 1 and in_capture_skill("確率演算") then cap.rolled = h.result == true end
  end })
  hook("Ｉクエスト倉庫は満杯ですか？", { after = function(h)
    if not cap or not in_capture_skill("Ｉクエスト倉庫は満杯ですか？") then return end
    if cap.stage == 0 then
      cap.stage = 1
      return
    end
    if cap.stage ~= 1 or cap.rolled == nil then return end
    cap.stage = 2
    if cap.rolled ~= true or h.result ~= 0 or gint("◆戦闘後アイテム取得") ~= 0 then return end
    local okn, no = pcall(call, "Ｃ番号", cap.target, -1)
    local def = okn and int(no) and defs[no]
    if not def or not def.capture then return end
    local c = def.capture
    local item = item_no(c.item)
    if not item or not item_exists(item) then
      log_once("capture" .. def.id, "rq.monster: %s's capture item %s is not defined; the monster escapes", def.id, tostring(c.item))
      return
    end
    capture_items[item] = c.text
    set_global("◆戦闘後アイテム取得", item)
    set_global("◆戦闘後アイテム取得／女の子", c.cg)
  end })
  hook("捕獲／文字列", { before = function(h)
    local text = capture_items[h.args[1]]
    if text then
      h.skip = true
      h.result = text
    end
  end })
end

-- ---- Drops (2.40.0) ----
--
-- Before every mod's rq.battle.on_end (priv.on_end_first): after a won fight with no item yet and
-- room in the quest storage, each enemy down, in rank order, rolls its drops through the game's
-- 確率演算; the first that rolls is the fight's one item.

local function roll_drops(ctx)
  if ctx.result ~= 1 then return end
  if gint("◆戦闘後アイテム取得") ~= 0 then return end
  local okf, full = pcall(call, "Ｉクエスト倉庫は満杯ですか？")
  if not okf or full ~= 0 then return end
  for _, iic in ipairs(ctx.enemies or {}) do
    local okn, no = pcall(call, "Ｃ番号", iic, -1)
    local oks, state = pcall(call, "Ｃ状態変化", iic, -1)
    if okn and oks and int(no) and state == 3 then
      local list = drops_of(no)
      for _, d in ipairs(list or {}) do
        local item = item_no(d.item)
        if not item or not item_exists(item) then
          log_once("drop" .. tostring(d.item), "rq.monster: monster %d's drop %s is not an item", no, tostring(d.item))
        else
          local okr, hit = pcall(call, "確率演算", d.chance)
          if okr and hit == true then
            set_global("◆戦闘後アイテム取得", item)
            return
          end
        end
      end
    end
  end
end

install_drops = function()
  if drops_hooked then return end
  drops_hooked = true
  priv.on_end_first(function(ctx)
    local ok, err = pcall(roll_drops, ctx)
    if not ok then log_once("drops" .. tostring(err), "rq.monster: drops: %s", tostring(err)) end
  end)
end

-- ---- define ----

local CAPTURE_FIELDS = { item = true, cg = true, text = true }

local function check_capture(who, c)
  local function bad(fmt, ...) error(string.format(who .. ": " .. fmt, ...), 4) end
  if type(c) ~= "table" then bad("capture must be a table") end
  for k in pairs(c) do
    if not CAPTURE_FIELDS[k] then bad("capture has no field %s", tostring(k)) end
  end
  if not check_item_ref(c.item) then bad("capture.item must be an item number or a text id") end
  if c.cg ~= nil and not (int(c.cg) and c.cg > 0) then bad("capture.cg must be a CG number") end
  if c.text ~= nil and type(c.text) ~= "string" then bad("capture.text must be a text") end
  return { item = c.item, cg = c.cg or 0, text = c.text or "" }
end

local DEF_ONLY = { id = true, base = true, capture = true }

function monster.define(def)
  if type(def) ~= "table" then error("rq.monster.define: expected a table", 2) end
  local id = def.id
  if type(id) ~= "string" or id == "" then error("rq.monster.define: id must be a text", 2) end
  local who = "rq.monster.define: " .. id
  local base = def.base
  if not int(base) or base < 1 or base >= MOD_FIRST or not game_monster(base) then
    error(who .. ": base must be a monster number of the game's list", 2)
  end
  local patch = {}
  for k, v in pairs(def) do
    if not DEF_ONLY[k] then patch[k] = v end
  end
  check_patch(who, patch)
  local capture = def.capture ~= nil and check_capture(who, def.capture) or nil
  local no = id_no(id)
  local mod = mod_id()
  local old = defs[no]
  if old and old.id ~= id then
    error(string.format("rq.monster.define: %s and %s have the same number %d; change one id", id, old.id, no), 2)
  end
  if old and old.mod ~= mod then error(string.format("%s: defined by %s", who, old.mod), 2) end
  defs[no] = { no = no, id = id, mod = mod, base = base, patch = copy(patch), capture = capture }
  def_ids[id] = no
  install()
  hook_kills()
  if capture then hook_capture() end
  if patch.drops then install_drops() end
  return no
end

-- ---- Groups (2.40.0) ----

-- The game's groups: a block in 雑魚セット生成 for each number from 2 to 135 but 71 and 72.
local function game_group(no)
  return int(no) and no >= 2 and no <= 135 and no ~= 71 and no ~= 72
end

local groups = {}    -- no -> { no, id, mod, name, exp, rank, lineups }
local group_ids = {} -- id -> no
local groups_hooked = false

-- A group's line-up for ra (1 to 6): the mod's line-ups in turn.
local function lineup(r, ra)
  return r.lineups[(ra - 1) % #r.lineups + 1]
end

-- An area's groups of 1000 or more that no mod defined (a save whose mod is gone) become the area's
-- first game group, else group 135, as 雑魚セット生成 does for an area with none.
local function fix_area_groups()
  if gint("◆ランダムエンカウント有無") <= 0 then return end
  local list, first = {}, nil
  for i = 1, 5 do
    list[i] = gint("◆エンカウント中身", i)
    if list[i] > 0 and list[i] < MOD_FIRST and not first then first = list[i] end
  end
  for i = 1, 5 do
    local v = list[i]
    if v >= MOD_FIRST and not groups[v] then
      local to = first or 135
      log_once("group" .. v, "rq.monster: no group %d (a mod's that is not loaded?); group %d drawn in its place", v, to)
      pcall(set_global, "◆エンカウント中身", i, to)
    end
  end
end

local function build_group(placed)
  local r = groups[gint("◆戦闘グループ")]
  if not r then return end
  local ra = gint("◆前回エンカウント乱数")
  if ra < 1 or ra > 5 then ra = 1 end
  if placed == 1 then
    local ok, v = pcall(call, "RAND", 3)
    if ok and v == 1 then ra = 6 end
  end
  local m = lineup(r, ra)
  local world = gint("◆ワールド")
  set_global("FA", 3, r.rank + (world >= 2 and (world - 1) * 2 or 0))
  call("ＰＴＢ登録", r.exp, m[1], m[2], m[3], m[4], m[5])
end

local function hook_groups()
  if groups_hooked then return end
  groups_hooked = true
  hook("雑魚セット生成", {
    before = function()
      local ok, err = pcall(fix_area_groups)
      if not ok then log_once("fix" .. tostring(err), "rq.monster: groups: %s", tostring(err)) end
    end,
    after = function(h)
      local ok, err = pcall(build_group, h.args[1])
      if not ok then logf("rq.monster: group %d: %s", gint("◆戦闘グループ"), tostring(err)) end
    end,
  })
  hook("戦闘グループ文字列", { before = function(h)
    local r = groups[h.args[1]]
    if r then
      h.skip = true
      h.result = r.name
    end
  end })
end

local GROUP_FIELDS = { id = true, name = true, exp = true, rank = true, lineups = true }

-- A line-up's rank: a monster number (0 or false for none) or a mod monster's text id.
local function lineup_monster(who, v, i, k)
  if v == nil or v == false or v == 0 then return 0 end
  if type(v) == "string" and v ~= "" then return def_ids[v] or id_no(v) end
  if int(v) and v >= 1 then
    if v < MOD_FIRST and not game_monster(v) then
      error(string.format("%s: lineups[%d][%d]: no monster number %d", who, i, k, v), 4)
    end
    return v
  end
  error(string.format("%s: lineups[%d][%d] must be a monster number or text id, or 0", who, i, k), 4)
end

local function check_group(who, def)
  local function bad(fmt, ...) error(string.format(who .. ": " .. fmt, ...), 4) end
  for k in pairs(def) do
    if not GROUP_FIELDS[k] then bad("no field %s", tostring(k)) end
  end
  if def.name ~= nil and (type(def.name) ~= "string" or def.name == "") then bad("name must be a text") end
  if def.exp ~= nil and (not int(def.exp) or def.exp < 0) then bad("exp must be a whole number, 0 or more") end
  if def.rank ~= nil and (not int(def.rank) or def.rank < 0) then bad("rank must be a whole number, 0 or more") end
  local lu = def.lineups
  if type(lu) ~= "table" or #lu < 1 or #lu > 6 then bad("lineups must be a list of 1 to 6 line-ups") end
  local out = {}
  for i = 1, #lu do
    local l = lu[i]
    if type(l) ~= "table" then bad("lineups[%d] must be a list of up to 5 monsters", i) end
    for k in pairs(l) do
      if not int(k) or k < 1 or k > 5 then bad("lineups[%d]: a rank from 1 to 5, not %s", i, tostring(k)) end
    end
    local m, any = {}, false
    for k = 1, 5 do
      m[k] = lineup_monster(who, l[k], i, k)
      if m[k] ~= 0 then any = true end
    end
    if not any then bad("lineups[%d] has no monster", i) end
    out[i] = m
  end
  for k in pairs(lu) do
    if not int(k) or k < 1 or k > #lu then bad("lineups must be a list") end
  end
  return out
end

function monster.group(def)
  if type(def) ~= "table" then error("rq.monster.group: expected a table", 2) end
  local id = def.id
  if type(id) ~= "string" or id == "" then error("rq.monster.group: id must be a text", 2) end
  local who = "rq.monster.group: " .. id
  local lineups = check_group(who, def)
  local no = id_no(id)
  local mod = mod_id()
  local old = groups[no]
  if old and old.id ~= id then
    error(string.format("rq.monster.group: %s and %s have the same number %d; change one id", id, old.id, no), 2)
  end
  if old and old.mod ~= mod then error(string.format("%s: defined by %s", who, old.mod), 2) end
  groups[no] = { no = no, id = id, mod = mod, name = def.name or id, exp = def.exp or 10, rank = def.rank or 0,
    lineups = lineups }
  group_ids[id] = no
  hook_groups()
  return no
end

-- For rq.encounters.set_groups (basics.lua): a game group or a mod's, by number or id.
priv.group_check = function(v)
  if type(v) == "string" then
    local no = group_ids[v]
    if no then return no end
    return nil, "no group with the id " .. v
  end
  if game_group(v) or (int(v) and groups[v]) then return v end
  return nil, "no group " .. tostring(v)
end

-- For rq.battle.start's group (basics.lua): a mod's group, its EXP and a line-up drawn as the game
-- draws one (not the last one again).
priv.group_pick = function(v)
  local no = type(v) == "string" and group_ids[v] or v
  local r = int(no) and groups[no]
  if not r then return nil end
  local ra = call("RAND", 5)
  local last = gint("◆前回エンカウント乱数")
  if ra == last then ra = call("RAND", 5) end
  set_global("◆前回エンカウント乱数", ra)
  return copy(lineup(r, ra)), r.exp, r.no
end

-- ---- Models (2.40.0) ----

local MODEL0 = 100000
local models_made = {} -- id -> { slot, mod, no }
local nmodels = 0

local function model_table()
  local n, file = priv.models_count()
  if not n then error("the game's model table cannot be read", 3) end
  return n, file
end

function monster.models()
  local n, file = model_table()
  local by_index = {}
  for id, m in pairs(models_made) do by_index[m.no - MODEL0] = id end
  local out = {}
  for i = 0, n - 1 do
    local name, poly, scale, x, y, z, motions = priv.model_info(i)
    if name then
      out[#out + 1] = { model = MODEL0 + i, name = name, poly = poly, scale = scale, x = x, y = y, z = z,
        motions = motions, id = i >= file and by_index[i] or nil }
    end
  end
  return out
end

local MODEL_FIELDS = { id = true, base = true, scale = true, x = true, y = true, z = true }

function monster.model(def)
  if type(def) ~= "table" then error("rq.monster.model: expected a table", 2) end
  local id = def.id
  if type(id) ~= "string" or id == "" then error("rq.monster.model: id must be a text", 2) end
  local who = "rq.monster.model: " .. id
  for k in pairs(def) do
    if not MODEL_FIELDS[k] then error(who .. ": no field " .. tostring(k), 2) end
  end
  local n, file = model_table()
  local base = def.base
  if not int(base) or base < MODEL0 or base >= MODEL0 + file then
    error(string.format("%s: base must be a game model, %d to %d", who, MODEL0, MODEL0 + file - 1), 2)
  end
  local name, _, scale, x, y, z = priv.model_info(base - MODEL0)
  if not name then error(who .. ": no model " .. base, 2) end
  for _, k in ipairs({ "scale", "x", "y", "z" }) do
    local v = def[k]
    if v ~= nil and (type(v) ~= "number" or v ~= v or v == math.huge or v == -math.huge) then
      error(who .. ": " .. k .. " must be a number", 2)
    end
  end
  if def.scale ~= nil and (def.scale <= 0 or def.scale > 50) then error(who .. ": scale must be above 0, at most 50", 2) end
  local mod = mod_id()
  local old = models_made[id]
  if old and old.mod ~= mod then error(string.format("%s: made by %s", who, old.mod), 2) end
  local at = priv.model_derive(old and old.slot or -1, base - MODEL0, def.scale or scale, def.x or x,
    def.y or y, def.z or z)
  if not at then error(who .. ": the model cannot be made", 2) end
  if not old then
    old = { slot = nmodels, mod = mod }
    nmodels = nmodels + 1
    models_made[id] = old
  end
  old.no = MODEL0 + at
  return old.no
end

-- For rq.battle.start (basics.lua): whether the list has a number, and a defined monster's number.
-- combat.lua (2.37.0) places the hooks to watch the familiar being built.
priv.monster_install = install

priv.monster_exists = function(no)
  return monster.get(no) ~= nil
end

priv.monster_no = function(id)
  return def_ids[id]
end

-- Placed now: a number of 1000 or more with no definition (a familiar item, an area's group left
-- by a mod that is gone) is answered even when no mod calls rq.monster.
install()
hook_groups()
hook_groups()
