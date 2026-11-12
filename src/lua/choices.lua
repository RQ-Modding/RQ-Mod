-- What enemies and the familiar choose (loader 2.38.0, notes/plans/plan-enemy-choices.md in the
-- workspace):
--   rq.battle.on_choose               the skill and the target of an enemy's or the familiar's turn
--   rq.battle.taunt, set_taunt        the game's own taunt (◆戦闘タゲ取り)
--
-- Built only on public calls: rq.ain.call, global, set_global and hook.
--   戦闘_行動(nNo)          one turn: 戦闘_準備確認 (a charged skill released, its target picked with
--                           Ｔ戦闘_対象選択), else 戦闘_行動選択, then 戦闘_連続行動 or 戦闘_通常行動,
--                           which call Ｔ戦闘_対象選択 for the target (once, or once per hit).
--   戦闘敵_行動選択(nNo)    the enemy's skill: one 乱数(100) against the slots' chances (cumulative
--                           thresholds, first match; skill 1 with none), then Standby (384) for a
--                           front-row skill (スキル_対象_前衛) from the back row (戦闘データ_敵後衛)
--                           while the front row stands (戦闘データ_敵前衛全滅 false).
--   戦闘従魔_行動選択(nNo)  the familiar's skill: always its skill 1.
--   Ｔ戦闘_対象選択(nNo, s) the target's battle number, 0 for none: the actor for a self skill, else
--                           Ｔターゲット決定 by the skill's target kind (ＰＴＡランダム, ＰＴＢランダム).
--   戦闘_行動終了(nNo, ...) the turn's end.
--   Ｃ敵スキル登録(iic, skill, motion, chance, text)   adds a skill to an enemy's list (Ｃ敵スキル追加:
--                           the skill's own log text, 99 uses); the list is built again each fight.
--   ◆戦闘タゲ取り          the ally a skill of function 55 named; ＰＴＡランダム takes it first while it
--                           stands, is not hidden and is in the rows the skill reaches. 0 at a fight's
--                           start (Ｔ戦闘前初期化).
-- Battle numbers (nNo) go to roster slots with 戦闘番号_ＩＩＣ and back with 戦闘番号_取得.
local rq, priv = ...
local call, hook, g, set = rq.ain.call, rq.ain.hook, rq.ain.global, rq.ain.set_global
local battle = rq.battle
local cb = priv.callbacks

local NAME = "rq.battle.on_choose"
local CHARA_MAX = 200 -- the game's character array Ｃ[201]
local SKILL_MAX = 1000 -- the game's Ｓ has 1001 entries
local SLOTS = 20 -- a character's skill slots
local STANDBY = 384
local ESCAPE, RELAX = 211, 217

local function int(v)
  return math.type(v) == "integer"
end

local function gint(...)
  local ok, v = pcall(g, ...)
  if ok and int(v) then return v end
  return 0
end

local function truthy(v)
  return v == true or v == 1
end

local function logf(fmt, ...)
  rq.log(string.format(fmt, ...))
end

local function real(iic)
  return int(iic) and iic > 0 and iic <= CHARA_MAX
end

local function standing(iic)
  return call("Ｃ状態変化", iic, -1) ~= 3 and call("Ｃ体力", iic, -1) > 0
end

local function hidden(iic)
  return call("Ｃ隠密率", iic, -1) ~= 0
end

local function unit_of(iic)
  if not real(iic) then return 0 end
  local n = call("戦闘番号_取得", iic)
  return int(n) and n > 0 and n or 0
end

local function iic_of(no)
  if not int(no) or no <= 0 then return nil end
  local iic = call("戦闘番号_ＩＩＣ", no)
  return real(iic) and iic or nil
end

-- ---- Skills and targets ----

-- nil and the reason for a skill a mod may not write.
local function bad_skill(s)
  if not int(s) then return "skill: expected a whole skill number, not " .. tostring(s) end
  if s < 1 or s > SKILL_MAX then return "skill: no skill number " .. s end
  if call("Ｓ名前", s, "") == "" then return "skill: no skill number " .. s end
  local kind = call("Ｓ行動種類", s, -1)
  if kind < 1 or kind > 4 then return "skill " .. s .. " does not act (an ability)" end
  if s == ESCAPE or s == RELAX then return "skill " .. s .. " is the party's flight" end
  return nil
end

-- The skill's target kind: "self", "other", "own" or nil for none (all, a row, all allies).
local function aim(skill)
  local tk = call("Ｓ対象", skill, -1)
  if tk == 10 then return "self" end
  if tk == 1 or tk == 2 or tk == 20 then return "other", tk == 1 end
  if tk == 11 or tk == 13 then return "own" end
  return nil
end

-- The units a skill may aim at, by the game's rules (Ｔターゲット決定, ＰＴＡランダム, ＰＴＢランダム):
-- standing, on the side the target kind names, no hidden ally, the front row for melee while one
-- stands there.
local function legal_targets(actor, side, skill)
  local kind, melee = aim(skill)
  if kind == nil then return {} end
  if kind == "self" then return { actor } end
  local arr
  if kind == "other" then
    arr = side == "enemy" and "PTA" or "PTB"
  else
    arr = side == "enemy" and "PTB" or "PTA"
  end
  local units = {}
  for r = 1, 5 do
    local iic = gint(arr, r)
    if real(iic) and standing(iic) and not (arr == "PTA" and hidden(iic)) then
      units[#units + 1] = { iic = iic, rank = r }
    end
  end
  if melee then
    local front = {}
    for _, u in ipairs(units) do
      if u.rank <= 3 then front[#front + 1] = u end
    end
    if #front > 0 then units = front end
  end
  local out = {}
  for i, u in ipairs(units) do out[i] = u.iic end
  return out
end

local function is_legal(actor, side, skill, target)
  for _, t in ipairs(legal_targets(actor, side, skill)) do
    if t == target then return true end
  end
  return false
end

-- The skill an enemy would get from the game's Standby rule.
local function standby_rule(no, side, skill)
  if side ~= "enemy" or skill == STANDBY then return skill end
  if truthy(call("戦闘データ_敵後衛", no)) and not truthy(call("戦闘データ_敵前衛全滅"))
    and truthy(call("スキル_対象_前衛", skill)) then
    return STANDBY
  end
  return skill
end

-- The game's own target roll, called by the loader; the Ｔ戦闘_対象選択 hook leaves it alone.
local self_target = false
local function game_target(no, skill)
  if skill == ESCAPE then return nil end
  self_target = true
  local ok, r = pcall(call, "Ｔ戦闘_対象選択", no, skill)
  self_target = false
  if not ok then
    logf("%s: the game's target: %s", NAME, tostring(r))
    return nil
  end
  return iic_of(r)
end

-- Skills the loader added to a list in this fight: added[iic][skill] = true.
local added = {}

local function read_skills(iic)
  local list = {}
  for s = 1, SLOTS do
    local id = call("ＣＳ番号", iic, s, -1)
    if id ~= 0 and call("ＣＳ保有", iic, s, -1) == 1 then
      list[#list + 1] = {
        slot = s,
        id = id,
        text = call("ＣＳ表示文", iic, s, ""),
        motion = call("ＣＳモーション", iic, s, -1),
        chance = call("ＣＳ確率", iic, s, -1),
        added = (added[iic] and added[iic][id]) or false,
      }
    end
  end
  return list
end

local function rank_of(iic, side)
  if side ~= "enemy" then return 0 end
  for r = 1, 5 do
    if gint("PTB", r) == iic then return r end
  end
  return 0
end

-- Puts a skill the actor does not have at the end of its list, for this fight: chance 0 (never
-- drawn), the motion of its first skill, the skill's own log text. false when the list is full.
local function ensure_skill(iic, skill)
  if call("ＣＳ番号取得", iic, skill, 0) ~= 0 then return true end
  local motion = call("ＣＳモーション", iic, 1, -1)
  if motion < 0 then motion = 0 end
  call("Ｃ敵スキル登録", iic, skill, motion, 0, "")
  if call("ＣＳ番号取得", iic, skill, 0) == 0 then return false end
  added[iic] = added[iic] or {}
  added[iic][skill] = true
  return true
end

-- ---- The ctx ----

-- `target` is kept outside the ctx's table, so every write reaches __newindex: a callback that
-- writes the unit the game rolled has set the target too (it then takes every hit of a multi-hit
-- skill). The loader's own writes go through set_target and do not count.
local targets = setmetatable({}, { __mode = "k" }) -- ctx -> target, false for nil
local wrote = setmetatable({}, { __mode = "k" }) -- ctx -> true once a callback wrote the target

local ChooseCtx = {}
local CtxMeta = {
  __index = function(ctx, k)
    if k == "target" then return targets[ctx] or nil end
    return ChooseCtx[k]
  end,
  __newindex = function(ctx, k, v)
    if k == "target" then
      targets[ctx] = v == nil and false or v
      wrote[ctx] = true
    else
      rawset(ctx, k, v)
    end
  end,
  -- pairs lists `target` with the other fields.
  __pairs = function(ctx)
    local done = false
    return function(t, k)
      if not done then
        local nk, nv = next(t, k)
        if nk ~= nil then return nk, nv end
        done = true
        if targets[t] then return "target", targets[t] end
      end
      return nil
    end, ctx, nil
  end,
}

local function set_target(ctx, v)
  targets[ctx] = v == nil and false or v
end

function ChooseCtx:targets(skill)
  if skill == nil then skill = self.skill end
  if not int(skill) or skill < 1 or skill > SKILL_MAX then
    error("ctx:targets: expected a skill number, not " .. tostring(skill), 2)
  end
  return legal_targets(self.iic, self.side, skill)
end

local choose_list, add_choose = cb.handlers(NAME)

-- Runs the callbacks on ctx, checking each one's writes. Returns whether a mod set the target.
local function run_choose(ctx)
  local set_by_mod = false
  for _, x in ipairs({ table.unpack(choose_list) }) do
    local s0, t0 = ctx.skill, ctx.target
    wrote[ctx] = nil
    local ok = cb.run(x, NAME, ctx)
    local w = wrote[ctx]
    wrote[ctx] = nil
    if not ok then
      ctx.skill = s0
      set_target(ctx, t0)
    else
      local s1, t1 = ctx.skill, ctx.target
      local skill = s0
      if s1 ~= s0 then
        local why = bad_skill(s1)
        if not why and ctx.charged then why = "skill: a charged skill cannot change; write the target only" end
        if why then
          cb.report(x, NAME, why)
        else
          skill = standby_rule(ctx.unit, ctx.side, s1)
          ctx.standby = skill ~= s1
        end
      end
      ctx.skill = skill
      local target = t0
      if aim(skill) == nil then
        target = nil
      elseif w and t1 == nil then
        -- nil: the game picks.
        target = game_target(ctx.unit, skill)
        set_by_mod = false
      elseif w and is_legal(ctx.iic, ctx.side, skill, t1) then
        -- Also the unit the game rolled: a target the mod set.
        target = t1
        set_by_mod = true
      else
        if w then
          cb.report(x, NAME, "target: " .. tostring(t1) .. " is not a unit skill " .. skill .. " may aim at")
        end
        -- A new skill: a target a mod set stays while it fits; else the game rolls for the skill.
        if skill ~= s0 and not (set_by_mod and target ~= nil and is_legal(ctx.iic, ctx.side, skill, target)) then
          target = game_target(ctx.unit, skill)
          set_by_mod = false
        end
      end
      set_target(ctx, target)
    end
  end
  return set_by_mod
end

-- ---- The hooks ----

local hooked = false
local drawing = false -- 戦闘敵_行動選択 runs: スキル_対象_前衛 is the Standby rule
local drawn, drawn_front = nil, false
local pending = nil -- { no, unit, iic, set, calls }: the target the turn's Ｔ戦闘_対象選択 returns
local prep = nil -- the nNo while 戦闘_準備確認 runs

local function new_ctx(iic, no, side, skill, target)
  local ctx = setmetatable({
    iic = iic,
    unit = no,
    side = side,
    rank = rank_of(iic, side),
    turn = gint("◆戦闘ターン"),
    skill = skill,
    rolled = skill,
    standby = false,
    charged = false,
    skills = read_skills(iic),
  }, CtxMeta)
  set_target(ctx, target)
  return ctx
end

local function choose_after(h, side)
  drawing = false
  local was, front = drawn, drawn_front
  drawn, drawn_front = nil, false
  pending = nil
  if #choose_list == 0 or not priv.in_battle() then return end
  local no, skill = h.args[1], h.result
  if not int(no) or not int(skill) then return end
  local iic = iic_of(no)
  if not iic then return end
  local target = game_target(no, skill)
  local ctx = new_ctx(iic, no, side, skill, target)
  if side == "enemy" and skill == STANDBY and front and int(was) then
    ctx.rolled, ctx.standby = was, true
  end
  local set_by_mod = run_choose(ctx)
  -- Standby is not in most lists, and the game uses it from none, as its own rule does.
  if ctx.skill ~= skill and ctx.skill ~= STANDBY and not ensure_skill(iic, ctx.skill) then
    logf("%s: no free skill slot on %d for skill %d; the game's choice stays", NAME, iic, ctx.skill)
    ctx.skill, set_by_mod = skill, false
    set_target(ctx, target)
  end
  pending = { no = no, unit = unit_of(ctx.target), iic = ctx.target, set = set_by_mod, calls = 0 }
  h.result = ctx.skill
end

local function install()
  if hooked then return end
  hooked = true
  hook("Ｔ戦闘前初期化", { before = function()
    added, pending, prep, drawing = {}, nil, nil, false
  end })
  hook("戦闘敵_行動選択", {
    before = function()
      drawing, drawn, drawn_front = #choose_list > 0, nil, false
    end,
    after = function(h) choose_after(h, "enemy") end,
  })
  hook("戦闘従魔_行動選択", { after = function(h) choose_after(h, "familiar") end })
  hook("スキル_対象_前衛", { after = function(h)
    if drawing then
      drawn, drawn_front = h.args[1], truthy(h.result)
    end
  end })
  hook("戦闘_準備確認", {
    before = function(h) prep = h.args[1] end,
    after = function() prep = nil end,
  })
  hook("Ｔ戦闘_対象選択", {
    before = function(h)
      if self_target then return end
      local p = pending
      if not p or h.args[1] ~= p.no then return end
      p.calls = p.calls + 1
      if p.calls == 1 then
        h.skip, h.result = true, p.unit
      elseif p.set and real(p.iic) and standing(p.iic) then
        h.skip, h.result = true, unit_of(p.iic)
      end
    end,
    after = function(h)
      -- A charged skill released: only its target can change.
      if self_target or prep == nil or h.args[1] ~= prep or #choose_list == 0 then return end
      prep = nil
      local no, skill = h.args[1], h.args[2]
      local side
      if truthy(call("戦闘番号_敵確認", no)) then
        side = "enemy"
      elseif truthy(call("戦闘番号_従魔確認", no)) then
        side = "familiar"
      else
        return
      end
      local iic = iic_of(no)
      if not iic or not int(skill) then return end
      local ctx = new_ctx(iic, no, side, skill, iic_of(h.result))
      ctx.charged = true
      run_choose(ctx)
      h.result = unit_of(ctx.target)
    end,
  })
  hook("戦闘_行動終了", { before = function() pending = nil end })
end

function battle.on_choose(fn)
  local remove = add_choose(fn)
  install()
  return remove
end

-- ---- The taunt ----

function battle.taunt()
  if not priv.in_battle() then return nil end
  local t = gint("◆戦闘タゲ取り")
  return t > 0 and t or nil
end

function battle.set_taunt(iic)
  if iic ~= nil and not int(iic) then
    error("rq.battle.set_taunt: expected an ally's roster slot or nil", 2)
  end
  if not priv.in_battle() then error("rq.battle.set_taunt: not in a fight", 2) end
  if iic == nil then
    set("◆戦闘タゲ取り", 0)
    return
  end
  local found = false
  for r = 1, 5 do
    if gint("PTA", r) == iic then found = true end
  end
  if not found then error("rq.battle.set_taunt: " .. iic .. " is not an ally in this fight", 2) end
  if not standing(iic) then error("rq.battle.set_taunt: " .. iic .. " is down", 2) end
  set("◆戦闘タゲ取り", iic)
end
