-- Skill buying (loader 2.45.0, notes/plans/plan-new-mods.md in the workspace):
--   the purchase record        after キャラスキル_新規追加 and キャラスキル_更新 (the skill window's
--                              buy and upgrade), the points paid are added to the row's own cost
--                              field, Ｃ[iic].スキル[row].コスト, which the game writes only as 0 and
--                              never reads. A row bought before this loader, then upgraded, gets
--                              its older steps priced first (rq.chara.price from its starting
--                              level). -1 marks a skill a mod gave (rq.chara.grant, edit's learn)
--   rq.chara.on_learn          each purchase, the game's or rq.chara.learn's
--   rq.chara.on_skill_reset    after Ｃスキルドレイン, the game's reset of a character's skills (2.46.0)
--   rq.chara.base_skills       the starting set, as Morurun's reset (Ｃスキルドレイン) rebuilds it:
--                              Ｃ生成／キャラデータ(iic, 0, "スキル"), then the reset's additions by
--                              flag, run on the character inside priv.keep, which puts it back
--   rq.chara.skill_points_max  Ｃ最大スキルポイント取得 (it clears FA[33]; priv.keep puts FA back)
--   rq.chara.learnable         Ｔ購入スキル登録's list, its arrays put back
--   rq.chara.price             the list's price at each step, the skill set to that step inside
--                              priv.keep
--   rq.chara.learn             the game's buy and upgrade, its questions (選択_確認_拡張) answered Yes
--   rq.chara.skill_room        the free rows the game's check (キャラスキル_空き確認) counts
--   rq.chara.slots             the record and the level on each row
--
-- Built on public calls (rq.ain.call, global, set_global, global_len, hook) and two private ones:
-- priv.keep (modloader_scene.c) and priv.forget_quiet (modloader_roster.c), so on_forget does not
-- see the skills the character builder removes while a starting set is worked out.
local rq, priv = ...
local call, hook, g, set, len = rq.ain.call, rq.ain.hook, rq.ain.global, rq.ain.set_global, rq.ain.global_len
local chara = rq.chara

local ROWS = 20           -- Ｃ[iic].スキル[1..20]
local USES_MAX = 99       -- スキルポン stops listing a use-count skill there
local RANK_MAX = 4        -- and a ranked skill there
local LIST = { { "取得可能スキル" }, { "取得可能スキルポイント" }, { "取得不可能スキル説明" }, { "◆取得可能スキル番号" } }

local cbs = priv.callbacks
local learned, add_learned = cbs.handlers("rq.chara.on_learn")

local function logf(fmt, ...)
  rq.log("rq.chara: " .. string.format(fmt, ...))
end

local function int(v)
  return math.type(v) == "integer"
end

-- A roster slot with a character: a slot past Ｃ is a fatal error in the game's getters.
local function exists(iic)
  if not int(iic) or iic <= 0 then return false end
  local ok, size = pcall(len, "Ｃ")
  if not ok or iic >= size then return false end
  return g("Ｃ", iic, "番号") ~= 0
end

local function need_chara(iic, name)
  if not exists(iic) then error(name .. ": no character at " .. tostring(iic), 3) end
end

-- A skill number the game's Ｓ table holds (a mod's defined skill too).
local function need_skill(skill, name)
  if not int(skill) or skill <= 0 then error(name .. ": expected a skill number", 3) end
  local ok, size = pcall(len, "Ｓ")
  if not ok or skill >= size then error(name .. ": no skill " .. skill, 3) end
end

local function stype(skill)
  return call("Ｓタイプ", skill, -1)
end

-- How far a skill is: most uses for a use-count skill (type 10), rank for a ranked one (20, 30),
-- 1 for the rest. Only 10, 20 and 30 can be bought.
local function buyable(t)
  return t == 10 or t == 20 or t == 30
end

local function row_of(iic, skill)
  local r = call("ＣＳ番号取得", iic, skill, 0)
  if r and r > 0 then return r end
  return nil
end

local function n_at(iic, row, skill)
  local t = stype(skill)
  if t == 10 then return g("Ｃ", iic, "スキル", row, "使用回数最大") end
  if t == 20 or t == 30 then return g("Ｃ", iic, "スキル", row, "ランク") end
  return 1
end

local function n_of(iic, skill)
  local row = row_of(iic, skill)
  return row and n_at(iic, row, skill) or 0
end

-- One step of a skill: 3 uses for skill 384 (キャラスキル_追加回数), 1 for the rest.
local function step_of(skill)
  return skill == 384 and 3 or 1
end

local function rows(iic)
  local out = {}
  for r = 1, ROWS do
    local id = g("Ｃ", iic, "スキル", r, "番号")
    if id > 0 and g("Ｃ", iic, "スキル", r, "保有") == 1 then
      out[#out + 1] = { id = id, n = n_at(iic, r, id) }
    end
  end
  return out
end

-- ---- the buy list ----

local function reason(text)
  text = tostring(text or ""):gsub("%$", "\n")
  return text
end

-- The list the game just filled: { skill, points, why } in its order.
local function read_list(iic)
  local n = g("◆取得可能スキル番号")
  local size = len("取得可能スキル")
  local out = {}
  for i = 1, math.min(n, size - 1) do
    local id = g("取得可能スキル", i)
    if id > 0 then
      local p = g("取得可能スキルポイント", i)
      local row = { skill = id, points = p, n = iic and n_of(iic, id) or nil }
      if p <= 0 then
        row.points = 0
        row.why = reason(g("取得不可能スキル説明", i))
      end
      out[#out + 1] = row
    end
  end
  return out
end

-- The price of a skill's next step in the list the game just filled: points, or nil and why.
local function list_price(skill)
  for _, r in ipairs(read_list(nil)) do
    if r.skill == skill then
      if r.points > 0 then return r.points end
      return nil, "condition", r.why
    end
  end
  return nil, "not sold"
end

local function fill(iic)
  call("Ｔ購入スキル登録", iic)
end

-- Runs fn with on_forget quiet, the character and the buy list put back after.
local function trying(paths, fn)
  priv.forget_quiet(true)
  local res = table.pack(pcall(priv.keep, paths, fn))
  priv.forget_quiet(false)
  if not res[1] then error(res[2], 0) end
  return table.unpack(res, 2, res.n)
end

local function with_list(extra)
  local paths = {}
  for i, p in ipairs(LIST) do paths[i] = p end
  if extra then paths[#paths + 1] = extra end
  return paths
end

-- Sets a skill to step n on the character (inside priv.keep only): 0 takes the row away.
local function set_n(iic, skill, n)
  local row = row_of(iic, skill)
  if n == 0 then
    if row then call("ＣＳ初期化", iic, row) end
    return true
  end
  if not row then
    row = call("ＣＳ追加", iic, skill, n)
    return row and row > 0
  end
  local t = stype(skill)
  if t == 10 then
    call("ＣＳ使用回数最大", iic, row, n)
  else
    call("ＣＳランク", iic, row, n)
  end
  return true
end

-- The game's price of a skill from n = from to n = to, on the character as it is now: the list at
-- each step. Returns points, or nil and why ("not sold", "max", "condition" with the game's text,
-- "room" when no row is free to try the skill).
local function price(iic, skill, from, to)
  local t = stype(skill)
  if not buyable(t) then return nil, "not sold" end
  if to <= from then return 0 end
  local top = t == 10 and USES_MAX or RANK_MAX
  if to > top then return nil, "max" end
  local inc = step_of(skill)
  return trying(with_list({ "Ｃ", iic }), function()
    local sum, k = 0, from
    while k < to do
      if not set_n(iic, skill, k) then return nil, "room" end
      fill(iic)
      local p, why, text = list_price(skill)
      if not p then return nil, why, text end
      sum = sum + p
      k = k + inc
    end
    return sum
  end)
end

-- ---- the starting set ----

local function base_set(iic)
  local had_familiar = row_of(iic, 321) ~= nil
  return trying({ { "Ｃ", iic } }, function()
    -- Ｃスキルドレイン, as the game resets a character's skills for Morurun
    call("Ｃ生成／キャラデータ", iic, 0, "スキル")
    if had_familiar then call("ＣＳ追加", iic, 321, 1) end
    local fa4 = call("ＣＦＡ", iic, 4, -1)
    if fa4 >= 2 and fa4 <= 5 then call("ＣＳ追加", iic, 355 + fa4, 0) end
    if call("ＣＦＡ", iic, 5, -1) == 1 then call("ＣＳ追加", iic, 311, 0) end
    if call("ＣＦＡ", iic, 36, -1) == 7 then call("ＣＳ追加", iic, 312, 0) end
    local fa15 = call("ＣＦＡ", iic, 15, -1)
    if fa15 ~= 0 then call("ＣＳ追加", iic, fa15, 0) end
    call("スキル／モルルン被害者追加", iic)
    return rows(iic)
  end)
end

-- ---- the purchase record ----

local function cost_of(iic, row)
  return g("Ｃ", iic, "スキル", row, "コスト")
end

local function set_cost(iic, row, v)
  set("Ｃ", iic, "スキル", row, "コスト", v)
end

local function base_n(iic, skill)
  local ok, set_ = pcall(base_set, iic)
  if not ok then
    logf("base_skills(%d): %s", iic, tostring(set_))
    return 0
  end
  for _, s in ipairs(set_) do
    if s.id == skill then return s.n end
  end
  return 0
end

local learning      -- rq.chara.learn in progress: { free = true when not paid }
local before = {}   -- iic .. ":" .. skill -> { n, cost } before an upgrade

local function key(iic, skill)
  return iic .. ":" .. skill
end

hook("キャラスキル_更新", { before = function(h)
  local iic, skill = h.args[1], h.args[2]
  if not exists(iic) then return end
  local row = row_of(iic, skill)
  if row then before[key(iic, skill)] = { n = n_at(iic, row, skill), cost = cost_of(iic, row) } end
end })

local function bought(iic, skill, points, new)
  if not exists(iic) then return end
  local row = row_of(iic, skill)
  if not row then return end
  local was = before[key(iic, skill)]
  before[key(iic, skill)] = nil
  local free = learning and learning.free
  local cost = cost_of(iic, row)
  if new then
    cost = free and -1 or points
  elseif free then
    if cost == 0 then cost = -1 end
  else
    if cost == -1 then
      cost = points
    elseif cost == 0 then
      -- bought before this loader: its older steps at the game's price, then this one
      local older = 0
      if was and was.n > 0 then
        local from = base_n(iic, skill)
        if was.n > from then
          local ok, p = pcall(price, iic, skill, from, was.n)
          if ok and p then older = p end
        end
      end
      cost = older + points
    else
      cost = cost + points
    end
  end
  set_cost(iic, row, cost)
  local ctx = {
    iic = iic, skill = skill, slot = row, points = free and 0 or points, cost = cost,
    new = new, n = n_at(iic, row, skill), by = learning and "learn" or "game",
  }
  for _, h in ipairs({ table.unpack(learned) }) do cbs.run(h, "rq.chara.on_learn", ctx) end
end

hook("キャラスキル_新規追加", { after = function(h)
  if h.result then bought(h.args[1], h.args[2], h.args[3], true) end
end })

hook("キャラスキル_更新", { after = function(h)
  if h.result then
    bought(h.args[1], h.args[2], h.args[3], false)
  else
    before[key(h.args[1], h.args[2])] = nil
  end
end })

-- rq.chara.learn answers the buy's questions: Learn ...? Increase usages ...? Raise level ...?
hook("選択_確認_拡張", { before = function(h)
  if learning then
    h.skip = true
    h.result = true
  end
end })

-- rq.chara.on_skill_reset (2.46.0): after Ｃスキルドレイン, the game's reset of a character's
-- skills (Morurun's reset, equipping Skill Again), which sets the free points to the game's own
-- count (Ｃ最大スキルポイント取得), so points a mod gave are gone.
local resets, add_reset = cbs.handlers("rq.chara.on_skill_reset")

hook("Ｃスキルドレイン", { after = function(h)
  if #resets == 0 then return end
  local iic = h.args[1]
  if not exists(iic) then return end
  local ctx = { iic = iic, no = g("Ｃ", iic, "番号"), points = call("Ｃスキルポイント", iic, -1) }
  for _, r in ipairs({ table.unpack(resets) }) do cbs.run(r, "rq.chara.on_skill_reset", ctx) end
end })

-- ---- the calls ----

function chara.on_learn(fn)
  return add_learned(fn)
end

function chara.on_skill_reset(fn)
  return add_reset(fn)
end

function chara.base_skills(iic)
  need_chara(iic, "rq.chara.base_skills")
  return base_set(iic)
end

function chara.skill_points_max(iic)
  need_chara(iic, "rq.chara.skill_points_max")
  return trying({ { "Ｃ", iic, "ＦＡ" } }, function()
    return call("Ｃ最大スキルポイント取得", iic)
  end)
end

function chara.learnable(iic)
  need_chara(iic, "rq.chara.learnable")
  return trying(with_list(), function()
    fill(iic)
    return read_list(iic)
  end)
end

function chara.price(iic, skill, from, to)
  need_chara(iic, "rq.chara.price")
  need_skill(skill, "rq.chara.price")
  if not int(from) or not int(to) or from < 0 or to < 0 then
    error("rq.chara.price: from and to are whole numbers from 0", 2)
  end
  return price(iic, skill, from, to)
end

-- Free skill rows for a new skill: the game's own check (キャラスキル_空き確認, have < 18 - keep - 1)
-- as a count, without its message.
local function free_rows(iic)
  local have = call("キャラスキル_保有数", iic, false)
  local keep = call("キャラ_スキル枠キープ数", iic)
  return math.max(0, 18 - keep - 1 - have)
end

local function room(iic)
  return free_rows(iic) > 0
end

function chara.skill_room(iic)
  need_chara(iic, "rq.chara.skill_room")
  return free_rows(iic)
end

function chara.learn(iic, skill, opts)
  need_chara(iic, "rq.chara.learn")
  need_skill(skill, "rq.chara.learn")
  if opts ~= nil and type(opts) ~= "table" then error("rq.chara.learn: opts is a table", 2) end
  opts = opts or {}
  if learning then error("rq.chara.learn: already learning", 2) end
  local t = stype(skill)
  if not buyable(t) then return false, "not sold" end
  local cur = n_of(iic, skill)
  local inc = step_of(skill)
  local to = opts.to
  if to == nil then to = cur + inc end
  if not int(to) or to < 0 then error("rq.chara.learn: to is a whole number from 0", 2) end
  if to <= cur then return true, 0 end
  local pay = opts.pay ~= false
  local total, why, text = price(iic, skill, cur, to)
  if not total then return false, why, text end
  if pay then
    local have = chara.skill_points(iic)
    if have < total then return false, "points", total - have end
  end
  if cur == 0 and not room(iic) then return false, "room" end
  learning = { free = not pay }
  local spent, k = 0, cur
  local ok, err = pcall(function()
    while k < to do
      local p, w, tx = trying(with_list(), function()
        fill(iic)
        return list_price(skill)
      end)
      if not p then
        why, text = w, tx
        return
      end
      if not pay then chara.add_skill_points(iic, p) end
      local done
      if k == 0 then
        done = call("キャラスキル_新規追加", iic, skill, p, false)
      else
        done = call("キャラスキル_更新", iic, skill, p)
      end
      if not done then
        if not pay then chara.add_skill_points(iic, -p) end
        why = "refused"
        return
      end
      if pay then spent = spent + p end
      k = k + inc
    end
  end)
  learning = nil
  if k ~= cur then call("キャラ_再計算", iic, not call("クエスト_攻略中")) end
  if not ok then error(err, 0) end
  if k < to then return false, why or "refused", text end
  return true, spent
end

-- rq.chara.grant (0.2.0) marks what it gives (2.45.0), as edit's learn does in C.
local c_grant = chara.grant
function chara.grant(iic, skill, n)
  local row = c_grant(iic, skill, n)
  if int(row) and row > 0 and exists(iic) and cost_of(iic, row) == 0 then set_cost(iic, row, -1) end
  return row
end

-- rq.chara.slots (0.2.0) shows the purchase record and the level (2.45.0).
local c_slots = chara.slots
function chara.slots(iic)
  local out = c_slots(iic)
  local here = exists(iic)
  for _, s in ipairs(out) do
    if here and s.skill > 0 then
      s.cost = cost_of(iic, s.slot)
      s.n = n_at(iic, s.slot, s.skill)
    else
      s.cost, s.n = 0, 0
    end
  end
  return out
end
