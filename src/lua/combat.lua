-- Combat formulas and the last rq.ain hooks (loader 2.37.0, notes/plans/plan-combat-hooks.md in the
-- workspace):
--   rq.battle.on_order, on_order_after, familiar      the turn list and the familiar
--   rq.combat.on_stun, on_poison, on_counter          the rolls, with the rate the game rolls
--   rq.combat.on_heal                                 a skill's heal, a revive and HP absorb
--   rq.battle.escape, set_escape, rq.combat.on_escape the escape chance and the flight
--   rq.battle.set_ambush                              the ambush (read in on_start's ctx, C)
--   rq.battle.on_log, log                             each line of the battle log
--
-- Built only on public calls: rq.ain.call, global, set_global, hook, patch_int and stack.
--   Ｔ順番生成(fg)          the whole turn list ◆戦闘順番[1..50] (roster slots), at a fight's start
--                           (fg 0) and after a switch or a shuffle (1). It reads each speed once per
--                           round with Ｃ素早さ, nowhere else in a fight. PTA[1..5] allies, PTB[1..5]
--                           enemies (slots 1 to 5), PTC[1] the familiar (slot 10).
--   Ｔ戦闘前初期化          the fight's setup: the familiar (built with Ｃ敵登録(10, no), its speed
--                           held to the party's top), ◆奇襲, ◆逃げる確率; on_start runs at its tail.
--   確率演算(rate)          the game's roll: true at 100 or more, false at 0, else RAND(100) <= rate.
--   衝撃計算(a, d, skill, 条件)   the stun: skips skills 120, 448 and 152 and a target stunned the
--                           turn before (連続ぴより(d, 99)); adds " (Stun rate = N)" to ◆戦闘ログ１;
--                           with 条件 "模擬" (the forecast) writes ◆ＡＡ and returns; else rolls.
--   毒計算(a, d, skill)     the poison, only on a target with no status: a rate by skill, weapon or
--                           Poison Attack (264); rolls only when it is not 0.
--   ダメージ後計算(a, d, skill)   after a hit that did damage: HP absorb (function 51) and the
--                           counter roll (Counter, 259, on the character hit; 0 for the familiar's
--                           attack, an area skill, a stunned target, a target at 0 HP, a hit Ally
--                           Guard took, which rq.counter.on_guard_counter may let through).
--   Ｔ補助行動(a, d, skill) the support skills: the heals write Ｃ体力, ◆体力回復あり (the number
--                           shown) and " recovered N HP."; Escape (211) rolls RAND(100) against
--                           ◆逃げる確率 (0 with an enemy standing that has skill 437, never in a
--                           boss fight) and sets ◆逃げる発動 (1 fled, 2 failed).
--   Ｔ特殊行動(a, d, skill) "Relax, relax" (217) ends the fight (◆逃げた発動) unless fight rank 2,
--                           a boss fight or FA[75] 3.
--   戦闘ログ_追加(text, color, end)   every line of the battle log; an empty text adds nothing.
local rq, priv = ...
local call, hook, g, set = rq.ain.call, rq.ain.hook, rq.ain.global, rq.ain.set_global
local battle, combat = rq.battle, rq.combat

local FAMILIAR = 10
local CHARA_MAX = 200 -- the game's character array Ｃ[201]
local MAX_ORDER = 50
local MAX_CHAIN = 10 -- counters in a row before the loader stops granting them

local function int(v)
  return math.type(v) == "integer"
end

local function gint(...)
  local ok, v = pcall(g, ...)
  if ok and int(v) then return v end
  return 0
end

local function logf(fmt, ...)
  rq.log(string.format(fmt, ...))
end

local function mod_id()
  local ok, id = pcall(function() return rq.mod and rq.mod.id and rq.mod.id() end)
  return ok and id or "?"
end

-- A list of handlers with the mod that added each; add returns the remover.
local function handlers(name)
  local list = {}
  local function add(fn)
    if type(fn) ~= "function" then error(name .. ": expected a function", 3) end
    local h = { fn = fn, mod = mod_id(), seen = {} }
    list[#list + 1] = h
    return function()
      for i, x in ipairs(list) do
        if x == h then table.remove(list, i) return end
      end
    end
  end
  return list, add
end

-- An error of one handler, logged once per text, naming the mod.
local function report(h, name, text)
  text = tostring(text)
  if not h.seen[text] then
    h.seen[text] = true
    logf("%s (%s): %s", name, h.mod, text)
  end
end

-- Runs one handler; an error is reported and the handler counts as returning nothing.
local function run(h, name, ...)
  local ok, r = pcall(h.fn, ...)
  if ok then return true, r end
  report(h, name, r)
  return false
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

local function on_stack(name)
  local ok, st = pcall(rq.ain.stack, 32)
  if not ok or type(st) ~= "table" then return false end
  for _, n in ipairs(st) do
    if n == name then return true end
  end
  return false
end

local function in_fight(name)
  if not priv.in_battle() then error(name .. ": not in a fight", 3) end
end

-- What a roll callback returned: nil keeps the rate, true 100, false 0, a number 0 to 100.
local function rate_of(r, rate)
  if r == nil then return rate end
  if r == true then return 100 end
  if r == false then return 0 end
  if type(r) ~= "number" or r ~= r then
    error("expected a rate 0 to 100, true, false or nil", 0)
  end
  r = math.floor(r)
  if r < 0 then r = 0 elseif r > 100 then r = 100 end
  return math.tointeger(r)
end

-- Runs roll callbacks on ctx; each sees the rate the one before left. Returns the final rate.
local function run_rolls(list, name, ctx)
  for _, h in ipairs({ table.unpack(list) }) do
    local ok, r = run(h, name, ctx)
    if ok then
      local ok2, v = pcall(rate_of, r, ctx.rate)
      if ok2 then
        ctx.rate = v
      else
        report(h, name, v)
      end
    end
  end
  return ctx.rate
end

-- The game's own roll, called by the loader; the 確率演算 hook leaves it alone.
local self_roll = false
local function roll(rate)
  self_roll = true
  local ok, r = pcall(call, "確率演算", rate)
  self_roll = false
  if not ok then error(r, 0) end
  return r == true or r == 1
end

-- ---- The units of a fight ----

local function side_unit(iic, side, rank)
  local u = { iic = iic, side = side, rank = rank, spd = call("Ｃ素早さ", iic, -1) }
  local st = call("Ｃ状態変化", iic, -1)
  if side == "ally" then
    u.out = st == 3 or call("ＣＳ使用可能なスキルの有無", iic) == 0
  elseif side == "enemy" then
    u.out = st == 3
  else
    u.out = false
  end
  return u
end

-- ---- The familiar ----

local fam = { built = false, own_spd = nil, no = nil, item = false }
local fam_hooked = false

priv.familiar_built = function(no)
  fam.built = true
  fam.no = no
  fam.own_spd = call("Ｃ素早さ", FAMILIAR, -1)
  fam.item = false -- found when asked
end

local function fam_install()
  if fam_hooked then return end
  fam_hooked = true
  hook("Ｔ戦闘前初期化", { before = function()
    fam.built, fam.own_spd, fam.no, fam.item = false, nil, nil, false
  end })
  priv.monster_install()
end

-- The familiar item: the owner's held item, or the vault's for one that jumped in.
local function fam_item()
  if fam.item ~= false then return fam.item, fam.joined end
  local owner = gint("◆従魔オーナー番号")
  local cg = gint("◆従魔アイテム番号")
  local no = call("Ｃ番号", FAMILIAR, -1)
  local function is_it(ii)
    return ii and ii > 0 and call("Ｉ種類詳細", ii, -1) == 20 and call("ＩＣＧ番号", ii, -1) == cg
      and call("Ｉ効果", ii, 1, -1) == no
  end
  local held = owner > 0 and call("Ｃ持ち物", owner, -1) or 0
  if is_it(held) then
    fam.item, fam.joined = held, false
  else
    fam.item, fam.joined = nil, true
    for ii = 1, 3000 do
      if call("Ｉ存在", ii, -1) == 7 and is_it(ii) then
        fam.item = ii
        break
      end
    end
  end
  return fam.item, fam.joined
end

local function familiar_info()
  if gint("PTC", 1) ~= FAMILIAR then return nil end
  local item, joined = fam_item()
  local f = {
    iic = FAMILIAR,
    no = call("Ｃ番号", FAMILIAR, -1),
    owner = gint("◆従魔オーナー番号"),
    item = item,
    joined = joined,
    spd = call("Ｃ素早さ", FAMILIAR, -1),
  }
  f.own_spd = fam.built and fam.own_spd or f.spd
  if item then f.growth = call("Ｉ効果", item, 3, -1) end
  return f
end

function battle.familiar()
  fam_install()
  if not priv.in_battle() then return nil end
  return familiar_info()
end

-- ---- The turn order ----

local order_list, add_order = handlers("rq.battle.on_order")
local order_after_list, add_order_after = handlers("rq.battle.on_order_after")
local order_hooked = false
local saved_spd = nil -- { [iic] = spd } put back after the build

local function fight_units()
  local list, by = {}, {}
  for r = 1, 5 do
    local iic = gint("PTA", r)
    if iic > 0 then list[#list + 1] = side_unit(iic, "ally", r) end
  end
  for r = 1, 5 do
    local iic = gint("PTB", r)
    if iic > 0 then list[#list + 1] = side_unit(iic, "enemy", r) end
  end
  if gint("PTC", 1) == FAMILIAR then
    local u = side_unit(FAMILIAR, "familiar", 0)
    local f = familiar_info()
    if f then
      u.owner, u.item, u.own_spd = f.owner, f.item, f.own_spd
    end
    list[#list + 1] = u
  end
  for _, u in ipairs(list) do by[u.iic] = u end
  return list, by
end

local OrderCtx = {}
OrderCtx.__index = OrderCtx

function OrderCtx:speed(iic, n)
  if not int(iic) or not self._by[iic] then
    error("ctx:speed: " .. tostring(iic) .. " is not a unit of this fight", 2)
  end
  if not int(n) or n < 0 or n > 9999 then
    error("ctx:speed: expected a whole speed, 0 to 9999", 2)
  end
  self._want[iic] = n
  self._by[iic].spd = n
end

local OrderAfterCtx = {}
OrderAfterCtx.__index = OrderAfterCtx

function OrderAfterCtx:set_order(list)
  if type(list) ~= "table" then error("ctx:set_order: expected a list of units", 2) end
  local n = #list
  if n < 1 or n > MAX_ORDER then
    error("ctx:set_order: expected 1 to " .. MAX_ORDER .. " entries", 2)
  end
  local copy = {}
  for i = 1, n do
    local iic = list[i]
    if not int(iic) or not self._by[iic] then
      error("ctx:set_order: entry " .. i .. " (" .. tostring(iic) .. ") is not a unit of this fight", 2)
    end
    copy[i] = iic
  end
  self.order = copy
  self._changed = true
end

local function read_order()
  local list = {}
  for i = 1, MAX_ORDER do
    local iic = gint("◆戦闘順番", i)
    if iic == 0 then break end
    list[#list + 1] = iic
  end
  return list
end

local function order_install()
  if order_hooked then return end
  order_hooked = true
  fam_install()
  hook("Ｔ順番生成", {
    before = function(h)
      saved_spd = nil
      if #order_list == 0 then return end
      local units, by = fight_units()
      local ctx = setmetatable({ rebuild = h.args[1] == 1, units = units, _by = by, _want = {} }, OrderCtx)
      for _, x in ipairs({ table.unpack(order_list) }) do run(x, "rq.battle.on_order", ctx) end
      for iic, n in pairs(ctx._want) do
        local cur = call("Ｃ素早さ", iic, -1)
        if cur ~= n then
          saved_spd = saved_spd or {}
          if saved_spd[iic] == nil then saved_spd[iic] = cur end
          call("Ｃ素早さ", iic, n)
        end
      end
    end,
    after = function(h)
      local back = saved_spd
      saved_spd = nil
      if back then
        for iic, v in pairs(back) do call("Ｃ素早さ", iic, v) end
      end
      if #order_after_list == 0 then return end
      local units, by = fight_units()
      local ctx = setmetatable({ rebuild = h.args[1] == 1, units = units, order = read_order(), _by = by }, OrderAfterCtx)
      for _, x in ipairs({ table.unpack(order_after_list) }) do run(x, "rq.battle.on_order_after", ctx) end
      if ctx._changed then
        for i = 1, MAX_ORDER do set("◆戦闘順番", i, ctx.order[i] or 0) end
      end
    end,
  })
end

function battle.on_order(fn)
  local remove = add_order(fn)
  order_install()
  return remove
end

function battle.on_order_after(fn)
  local remove = add_order_after(fn)
  order_install()
  return remove
end

-- ---- The rolls ----

-- roll_ctx is the roll being made: set by the hook of the function that rolls, used by the hook on
-- 確率演算 when that function is the caller.
local roll_ctx = nil
local prob_hooked = false

local function prob_install()
  if prob_hooked then return end
  prob_hooked = true
  hook("確率演算", { before = function(h)
    local r = roll_ctx
    if not r or r.done or self_roll then return end
    if caller_of("確率演算") ~= r.fn then return end
    r.done = true
    local ok, v = pcall(r.decide, h.args[1])
    if not ok then
      logf("%s: %s", r.name, tostring(v))
    elseif v ~= nil then
      h.args[1] = v
    end
  end })
end

local function push_roll(r)
  r.prev = roll_ctx
  roll_ctx = r
end

local function pop_roll(r)
  if roll_ctx == r then roll_ctx = r.prev end
end

-- The stun.

local STUN_SKIP = { [120] = true, [448] = true, [152] = true }
local stun_list, add_stun = handlers("rq.combat.on_stun")
local stun_hooked = false
local stun_cur = nil

local function stun_decide(r, rate)
  local game = r.blocked and 0 or rate
  local final = game
  if #stun_list > 0 then
    final = run_rolls(stun_list, "rq.combat.on_stun", {
      attacker = r.attacker, defender = r.defender, skill = r.skill, rate = game, base = rate,
      blocked = r.blocked, forecast = r.forecast,
    })
  end
  if r.blocked or final ~= rate then
    set("◆戦闘ログ１", r.log1 .. (final > 0 and (" (Stun rate = " .. final .. ")") or ""))
  end
  return final
end

local function stun_install()
  if stun_hooked then return end
  stun_hooked = true
  prob_install()
  -- The skills the game never stuns with go on to the formula, for base; stun_decide holds them
  -- at 0 unless a callback gives more.
  for no in pairs(STUN_SKIP) do
    local n = rq.ain.patch_int("衝撃計算", no, -1)
    if n ~= 1 then logf("rq.combat.on_stun: skill %d's skip in 衝撃計算 found %d times", no, n) end
  end
  hook("連続ぴより", { before = function(h)
    local r = stun_cur
    if not r or h.args[2] ~= 99 or caller_of("連続ぴより") ~= "衝撃計算" then return end
    if call("連続ぴより", h.args[1], 99) == 1 and not r.blocked then r.blocked = "repeat" end
    h.skip = true
    h.result = 0
  end })
  hook("衝撃計算", {
    before = function(h)
      local a = h.args
      local r = {
        fn = "衝撃計算", name = "rq.combat.on_stun",
        attacker = a[1], defender = a[2], skill = a[3], forecast = a[4] == "模擬",
        blocked = STUN_SKIP[a[3]] and "skill" or nil,
        log1 = g("◆戦闘ログ１"), outer = stun_cur,
      }
      r.decide = function(rate) return stun_decide(r, rate) end
      push_roll(r)
      stun_cur = r
    end,
    after = function()
      local r = stun_cur
      if not r then return end
      stun_cur = r.outer
      pop_roll(r)
      if r.forecast and not r.done then
        r.done = true
        local ok, v = pcall(stun_decide, r, gint("◆ＡＡ"))
        if ok then set("◆ＡＡ", v) else logf("rq.combat.on_stun: %s", tostring(v)) end
      end
    end,
  })
end

function combat.on_stun(fn)
  local remove = add_stun(fn)
  stun_install()
  return remove
end

-- The poison.

local poison_list, add_poison = handlers("rq.combat.on_poison")
local poison_hooked = false

local function poison_decide(r, rate)
  local final = run_rolls(poison_list, "rq.combat.on_poison", {
    attacker = r.attacker, defender = r.defender, skill = r.skill, rate = rate,
  })
  if final ~= rate then
    set("◆戦闘ログ１", r.log1 .. (final > 0 and (" (Poison = " .. final .. ")") or ""))
  end
  return final
end

local function poison_install()
  if poison_hooked then return end
  poison_hooked = true
  prob_install()
  hook("毒計算", {
    before = function(h)
      local a = h.args
      local r = {
        fn = "毒計算", name = "rq.combat.on_poison",
        attacker = a[1], defender = a[2], skill = a[3],
        clean = call("Ｃ状態変化", a[2], -1) == 0, log1 = g("◆戦闘ログ１"),
      }
      r.decide = function(rate) return poison_decide(r, rate) end
      push_roll(r)
    end,
    after = function(h)
      local r = roll_ctx
      if not r or r.fn ~= "毒計算" then return end
      pop_roll(r)
      -- The game's rate was 0, so it did not roll: the callbacks may give one.
      if r.done or not r.clean or #poison_list == 0 then return end
      r.done = true
      local ok, final = pcall(poison_decide, r, 0)
      if not ok then logf("rq.combat.on_poison: %s", tostring(final)) return end
      if final <= 0 or call("Ｃ状態変化", r.defender, -1) ~= 0 then return end
      if roll(final) then
        -- No " (Poison)" on the second line: the game writes that line again after 毒計算.
        call("Ｃ状態変化", r.defender, 1)
        set("◆毒りました", 1)
      end
    end,
  })
end

function combat.on_poison(fn)
  local remove = add_poison(fn)
  poison_install()
  return remove
end

-- The counter and HP absorb, both in ダメージ後計算.

local counter_list, add_counter = handlers("rq.combat.on_counter")
local heal_list, add_heal = handlers("rq.combat.on_heal")
local chain = 0 -- counters granted in a row

local function counter_ctx(r, rate)
  local d = r.defender
  local guarded = gint("FA", 77) == 1
  local base = call("ＣＳ有無／ランク別効果", d, 259, 0)
  local blocked
  if r.attacker == FAMILIAR then
    blocked = "familiar"
  elseif call("Ｓ対象", r.skill, -1) ~= 1 then
    blocked = "area"
  elseif call("Ｃ状態変化", d, -1) == 2 then
    blocked = "stunned"
  elseif guarded and rate == 0 and base > 0 then
    blocked = "guard"
  end
  local in_counter = on_stack("戦闘_カウンター実行")
  if not in_counter then chain = 0 end
  return {
    attacker = r.attacker, defender = d, skill = r.skill, rate = rate, base = base,
    damage = gint("◆戦闘／ＤＭ"), guarded = guarded, blocked = blocked, counter = in_counter,
  }
end

local function counter_decide(r, rate)
  if #counter_list == 0 or call("Ｃ体力", r.defender, -1) <= 0 then return nil end
  local ctx = counter_ctx(r, rate)
  local final = run_rolls(counter_list, "rq.combat.on_counter", ctx)
  if ctx.counter and chain >= MAX_CHAIN and final > rate then
    final = rate
  end
  return final
end

local function heal_decide(healer, target, skill, kind, amount)
  local ctx = { healer = healer, target = target, skill = skill, kind = kind, amount = amount }
  for _, h in ipairs({ table.unpack(heal_list) }) do
    run(h, "rq.combat.on_heal", ctx)
    local v = ctx.amount
    if type(v) ~= "number" or v ~= v then
      report(h, "rq.combat.on_heal", "ctx.amount must be a number")
      v = amount
    end
    v = math.floor(v)
    if v < 0 then v = 0 end
    ctx.amount = math.tointeger(v) or amount
  end
  return ctx.amount
end

-- " recovered N HP." (or "full HP.") in ◆戦闘ログ２ with the new number.
local function heal_log(final)
  local log2 = g("◆戦闘ログ２")
  local n
  log2, n = log2:gsub(" recovered %d+ HP%.", " recovered " .. final .. " HP.", 1)
  if n == 0 then
    log2 = log2:gsub(" recovered full HP%.", " recovered " .. final .. " HP.", 1)
  end
  set("◆戦闘ログ２", log2)
end

local damage_hooked = false

local function damage_install()
  if damage_hooked then return end
  damage_hooked = true
  prob_install()
  hook("ダメージ後計算", {
    before = function(h)
      local a = h.args
      local r = {
        fn = "ダメージ後計算", name = "rq.combat.on_counter",
        attacker = a[1], defender = a[2], skill = a[3], hp = call("Ｃ体力", a[1], -1),
      }
      r.decide = function(rate) return counter_decide(r, rate) end
      push_roll(r)
    end,
    after = function()
      local r = roll_ctx
      if not r or r.fn ~= "ダメージ後計算" then return end
      pop_roll(r)
      -- HP absorb: the attacker got the damage (the number the game shows is the skill's share).
      if #heal_list > 0 and call("Ｓ機能", r.skill, -1) == 51 then
        local amount = gint("◆戦闘／ＤＭ")
        local ok, final = pcall(heal_decide, r.attacker, r.attacker, r.skill, "absorb", amount)
        if ok and final ~= amount then
          call("Ｃ体力", r.attacker, r.hp + final)
          set("◆体力回復あり", final)
        elseif not ok then
          logf("rq.combat.on_heal: %s", tostring(final))
        end
      end
      -- The counter, for a character without Counter (the game rolled no counter).
      if not r.done and #counter_list > 0 then
        r.done = true
        local ok, final = pcall(counter_decide, r, 0)
        if not ok then
          logf("rq.combat.on_counter: %s", tostring(final))
        elseif final and final > 0 and roll(final) then
          set("◆カウンター発動", 1)
          set("◆ログ２追加", g("◆ログ２追加") .. " (counter)")
        end
      end
      if gint("◆カウンター発動") == 1 then chain = chain + 1 end
    end,
  })
end

function combat.on_counter(fn)
  local remove = add_counter(fn)
  damage_install()
  return remove
end

-- ---- Heals and escapes in the support and special skills ----

local escape_list, add_escape = handlers("rq.combat.on_escape")
local support_hooked = false
local support_stack = {}

local function escape_decide(iic, skill, rate, blocked)
  return run_rolls(escape_list, "rq.combat.on_escape", {
    iic = iic, skill = skill, rate = rate, blocked = blocked,
  })
end

local function support_after(s)
  local a1, a2, skill = s.a1, s.a2, s.skill
  -- A heal: the game set ◆体力回復あり (reset before each action) to the HP it gave.
  local amount = gint("◆体力回復あり")
  if #heal_list > 0 and amount > 0 then
    local revive = s.state == 3 and call("Ｃ状態変化", a2, -1) ~= 3
    local kind = revive and "revive" or "skill"
    local final = heal_decide(a1, a2, skill, kind, amount)
    if final ~= amount then
      if revive then
        if final < 1 then final = 1 end
        call("Ｃ体力", a2, final)
      else
        call("Ｃ体力", a2, s.hp + final)
        heal_log(final)
      end
      set("◆体力回復あり", final)
    end
  end
  -- Escape (211).
  if skill == 211 and #escape_list > 0 and gint("◆ボス戦") == 0 then
    local fled = gint("◆逃げる発動") == 1
    local blocked = nil
    local rate = s.chance
    if gint("◆逃げる確率") == 0 and s.chance ~= 0 then
      blocked, rate = "enemy", 0 -- an enemy standing with skill 437
    end
    local final = escape_decide(a1, skill, rate, blocked)
    if final ~= rate then
      local now = roll(final)
      if now ~= fled then
        if now then
          set("◆逃げる発動", 1)
          set("FA", 37, gint("FA", 37) + 1)
          set("FA", 98, gint("FA", 98) + 1)
          set("◆戦闘後アイテム取得", 0)
        else
          set("◆逃げる発動", 2)
          set("FA", 37, s.fa37)
          set("FA", 98, s.fa98)
          set("◆戦闘後アイテム取得", s.drop)
        end
      end
    end
  end
end

local function special_after(s)
  if s.skill ~= 217 or #escape_list == 0 or gint("◆ボス戦") ~= 0 then return end
  local ended = gint("◆逃げた発動") == 1
  local blocked
  if gint("◆戦闘ランク") == 2 then
    blocked = "rank"
  elseif gint("FA", 75) == 3 then
    blocked = "quest"
  end
  local rate = ended and 100 or 0
  local final = escape_decide(s.a1, 217, rate, blocked)
  if final == rate then return end
  local now = roll(final)
  if now == ended then return end
  if now then
    set("◆逃げた発動", 1)
    set("◆戦闘ログ２", " Relax, relax, it's over.→End")
  else
    set("◆逃げた発動", 0)
    set("◆戦闘ログ２", " Relax, relax, the fight will go on...")
  end
end

local function support_install()
  if support_hooked then return end
  support_hooked = true
  hook("Ｔ補助行動", {
    before = function(h)
      local a = h.args
      -- A skill with no target (Escape, Standby) gets the slot -1: Ｃ[-1] stops the game.
      local real = math.type(a[2]) == "integer" and a[2] > 0 and a[2] <= CHARA_MAX
      support_stack[#support_stack + 1] = {
        a1 = a[1], a2 = a[2], skill = a[3],
        hp = real and call("Ｃ体力", a[2], -1) or 0, state = real and call("Ｃ状態変化", a[2], -1) or 0,
        chance = gint("◆逃げる確率"), fa37 = gint("FA", 37), fa98 = gint("FA", 98),
        drop = gint("◆戦闘後アイテム取得"),
      }
    end,
    after = function()
      local s = table.remove(support_stack)
      if not s then return end
      local ok, err = pcall(support_after, s)
      if not ok then logf("rq.combat: %s", tostring(err)) end
    end,
  })
  hook("Ｔ特殊行動", {
    before = function(h)
      local a = h.args
      support_stack[#support_stack + 1] = { special = true, a1 = a[1], a2 = a[2], skill = a[3] }
    end,
    after = function()
      local s = table.remove(support_stack)
      if not s then return end
      local ok, err = pcall(special_after, s)
      if not ok then logf("rq.combat.on_escape: %s", tostring(err)) end
    end,
  })
end

function combat.on_heal(fn)
  local remove = add_heal(fn)
  support_install()
  damage_install()
  return remove
end

function combat.on_escape(fn)
  local remove = add_escape(fn)
  support_install()
  return remove
end

-- ---- The escape chance and the ambush ----

function battle.escape()
  if not priv.in_battle() then return nil end
  return gint("◆ボス戦") ~= 0 and 0 or gint("◆逃げる確率")
end

function battle.set_escape(n)
  if not int(n) or n < 0 or n > 100 then
    error("rq.battle.set_escape: expected a whole chance, 0 to 100", 2)
  end
  in_fight("rq.battle.set_escape")
  return call("戦闘逃げる率_設定", n)
end

local AMBUSH = { none = 0, surprise = 1, first_strike = 2, ambushed = 11 }

function battle.set_ambush(kind, iic)
  local v = AMBUSH[kind]
  if v == nil then
    error("rq.battle.set_ambush: expected \"none\", \"surprise\", \"first_strike\" or \"ambushed\"", 2)
  end
  if not on_stack("Ｔ戦闘前初期化") then
    error("rq.battle.set_ambush: call it from rq.battle.on_start, before the turn list is built", 2)
  end
  if kind == "first_strike" then
    local first, ally = nil, {}
    for r = 1, 5 do
      local a = gint("PTA", r)
      if a > 0 then
        ally[a] = true
        first = first or a
      end
    end
    if iic == nil then
      iic = first
    elseif not int(iic) or not ally[iic] then
      error("rq.battle.set_ambush: " .. tostring(iic) .. " is not an ally in this fight", 2)
    end
    set("FA", 73, iic or 0)
  elseif iic ~= nil then
    error("rq.battle.set_ambush: only \"first_strike\" takes an ally", 2)
  end
  set("◆奇襲", v)
end

-- ---- The battle log ----

local log_list, add_log = handlers("rq.battle.on_log")
local log_hooked = false

local function log_install()
  if log_hooked then return end
  log_hooked = true
  hook("戦闘ログ_追加", { before = function(h)
    local a = h.args
    local text = a[1]
    if type(text) ~= "string" or text == "" or #log_list == 0 then return end
    local ctx = { text = text, color = a[2], last = a[3] == true or a[3] == 1 }
    for _, x in ipairs({ table.unpack(log_list) }) do
      local ok, r = run(x, "rq.battle.on_log", ctx)
      if ok then
        if r == false then
          h.skip = true
          h.result = false
          return
        elseif type(r) == "string" then
          ctx.text = r
        elseif r ~= nil then
          report(x, "rq.battle.on_log", "expected false, a string or nil")
        end
      end
    end
    if ctx.text ~= text then a[1] = ctx.text end
  end })
end

function battle.on_log(fn)
  local remove = add_log(fn)
  log_install()
  return remove
end

function battle.log(text)
  if type(text) ~= "string" then error("rq.battle.log: expected a string", 2) end
  in_fight("rq.battle.log")
  if text == "" then return false end
  local r = call("戦闘ログ_追加", text, 5, false)
  return r == true or r == 1
end

-- choices.lua (2.38.0) runs after this file and shares the callback lists' helpers.
priv.callbacks = { handlers = handlers, run = run, report = report }
