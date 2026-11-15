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
--   rq.chara.boosts            (2.48.0) the Boost skills whose effect the game keeps after an ERASE
--   learnable's after, needs   (2.48.0) the list after some steps, worked out on the character inside
--                              priv.keep; the game's condition text read as skills and a level
--   on_forget's kept           (2.48.0) the erased skill is a Boost whose effect stays
--   forget's player, on_forget's player and mod
--                              (2.51.0) rq.chara.forget marks its erasure while it runs (the
--                              skill, opts.player, the calling mod by rq.mod.id); on_forget's ctx
--                              reads the mark for caller "lua". player is also true for the skill
--                              window's ERASE (キャラスキル_消去). forget's third value, the points
--                              the callbacks gave back, is counted in C
--   rq.chara.forget_points     (2.52.0) what the on_forget callbacks would give back for an
--                              erasure, without it: each callback registered (kept in forget_fns
--                              until its remover runs) called with the ctx the erasure would have,
--                              preview true
--   on_forget's n and preview  (2.52.0) the skill's level in its row (n_at); false for an erasure
--   rq.chara.set_cost          (2.53.0) writes the purchase record of a row the character holds, for
--                              a mod whose skill takes the place of others (their records added)
--   rq.chara.on_has_skill      (2.55.0) the game's check for a skill, ＣＳ番号取得, answered "no" by a
--                              mod: one hook per skill number with rq.ain.hook's `only`, so the
--                              game's checks for other skills never enter Lua
--
-- Built on public calls (rq.ain.call, global, set_global, global_len, hook) and two private ones:
-- priv.keep (modloader_scene.c) and priv.forget_quiet (modloader_roster.c), so on_forget does not
-- see the skills the character builder removes while a starting set is worked out.
local rq, priv = ...
local call, hook, g, set, len = rq.ain.call, rq.ain.hook, rq.ain.global, rq.ain.set_global, rq.ain.global_len
local chara = rq.chara

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

-- ---- the Boosts (2.48.0) ----
-- Ｃ再計算, in the third world only (◆ワールド 3): a Boost at rank 4 sets its flag (FA[38] to
-- FA[45]); the first of a pair sets 1, the second (455 to 457, which the list sells once the first is
-- at rank 4 or its flag is 1) sets 2. While its flag covers a Boost, the character keeps the effect
-- without the skill (+800 HP, +300 Attack or Magic, +50 to a rate). Ｃスキルドレイン clears the
-- flags, and Ｃ再計算 in another world does too.
local BOOSTS = {
  { skill = 276, fa = 38, step = 1 }, { skill = 455, fa = 38, step = 2, first = 276 },
  { skill = 277, fa = 39, step = 1 }, { skill = 456, fa = 39, step = 2, first = 277 },
  { skill = 278, fa = 40, step = 1 }, { skill = 457, fa = 40, step = 2, first = 278 },
  { skill = 279, fa = 41, step = 1 }, { skill = 280, fa = 42, step = 1 },
  { skill = 281, fa = 43, step = 1 }, { skill = 282, fa = 44, step = 1 },
  { skill = 283, fa = 45, step = 1 },
}
local BOOST_OF = {}
for _, b in ipairs(BOOSTS) do BOOST_OF[b.skill] = b end

local function keeps_boosts()
  return g("◆ワールド") == 3
end

local function boost_kept(iic, b)
  return keeps_boosts() and call("ＣＦＡ", iic, b.fa, -1) >= b.step
end

-- What Ｃ再計算 does to the flag of a Boost now at rank n (inside priv.keep only).
local function mark_boost(iic, skill, n)
  local b = BOOST_OF[skill]
  if not b or n < RANK_MAX or not keeps_boosts() then return end
  local f = call("ＣＦＡ", iic, b.fa, -1)
  if f == b.step - 1 then call("ＣＦＡ", iic, b.fa, b.step) end
end

local function rows(iic)
  local out = {}
  -- Rows 1 to 20 in the game; more with rq.knownskills (2.54.0).
  for r = 1, len("Ｃ", iic, "スキル") - 1 do
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

-- ---- the game's conditions as data (2.48.0) ----
-- Ｔ購入スキル登録 writes why a listed skill is not for sale as text, one "・Condition: " a line:
-- "Over LV 25", or a skill's name with "N+" (at least N uses), "+" (at least 1), "★" (rank 1 +
-- the stars) or nothing (at least 1). A name is read as the longest skill name the line starts with,
-- among the skills in the list and on the character first, then the whole table.
local all_names -- name -> lowest skill number, built once

local function name_of(id)
  local ok, s = pcall(rq.skill.get, id)
  return ok and s and s.name or ""
end

local function name_table()
  if all_names then return all_names end
  all_names = {}
  local ok, size = pcall(len, "Ｓ")
  if not ok then return all_names end
  for id = 1, size - 1 do
    local nm = name_of(id)
    if nm ~= "" and not all_names[nm] then all_names[nm] = id end
  end
  return all_names
end

-- The level a condition's tail asks for, or nil when it is not one.
local function tail_level(rest)
  if rest == "" or rest == "+" then return 1 end
  local d = rest:match("^%s*(%d+)%+$")
  if d then return tonumber(d) end
  local stars = select(2, rest:gsub("★", ""))
  if stars > 0 and rest:gsub("★", "") == "" then return stars + 1 end
  return nil
end

local function read_clause(clause, near)
  local best, best_n, best_len
  local function try(id, nm)
    if nm ~= "" and #nm > (best_len or 0) and clause:sub(1, #nm) == nm then
      local n = tail_level(clause:sub(#nm + 1))
      if n then best, best_n, best_len = id, n, #nm end
    end
  end
  for _, id in ipairs(near) do try(id, name_of(id)) end
  if not best then
    for nm, id in pairs(name_table()) do try(id, nm) end
  end
  return best, best_n
end

-- needs (skills and levels) and level (the character level) from a row's text. The game writes a
-- level from a number in full-width digits ("Over LV ４８", string2).
local function conditions(text, near)
  text = text:gsub("\xEF\xBC([\x90-\x99])", function(c) return string.char(c:byte() - 0x90 + 48) end)
  local needs, level = {}, nil
  for line in (text .. "\n"):gmatch("([^\n]*)\n") do
    local clause = line:match("Condition:%s*(.-)%s*$")
    if clause and clause ~= "" then
      local lv = clause:match("^Over LV%s*(%d+)$")
      if lv then
        level = math.max(level or 0, tonumber(lv))
      else
        local id, n = read_clause(clause, near)
        if id then needs[#needs + 1] = { id = id, n = n } end
      end
    end
  end
  return needs, level
end

-- The list the game just filled: { skill, points, n, step, why, needs, level } in its order.
local function read_list(iic)
  local n = g("◆取得可能スキル番号")
  local size = len("取得可能スキル")
  local out, near = {}, {}
  for i = 1, math.min(n, size - 1) do
    local id = g("取得可能スキル", i)
    if id > 0 then
      local p = g("取得可能スキルポイント", i)
      local row = { skill = id, points = p, n = iic and n_of(iic, id) or nil, step = step_of(id) }
      if p <= 0 then
        row.points = 0
        row.why = reason(g("取得不可能スキル説明", i))
      end
      out[#out + 1] = row
      near[#near + 1] = id
    end
  end
  if iic then
    for _, r in ipairs(rows(iic)) do near[#near + 1] = r.id end
    for _, row in ipairs(out) do
      if row.why then
        local ok, needs, level = pcall(conditions, row.why, near)
        if ok then row.needs, row.level = needs, level end
      end
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

-- rq.chara.on_has_skill (2.55.0, notes/plans/plan-dynamic-immature.md in the workspace):
-- ＣＳ番号取得(iic, skill, fg) is the game's check for a skill: the row of the first copy held (fg
-- 0, ＣＳ保有 1), set in an action slot (1, ＣＳ実装中 not 0) or held and not set (2), else 0.
-- ＣＳ有無, ＣＳ有無／スタメン and ＣＳ総数取得／スタメン (the party) ask through it. ctx.caller is the
-- first function on the stack past these helpers: the game code that asks.
local HAS_HELPERS = { ["ＣＳ番号取得"] = true, ["ＣＳ有無"] = true, ["ＣＳ有無／スタメン"] = true,
  ["ＣＳ番号取得／スタメン"] = true, ["ＣＳ総数取得／スタメン"] = true }
local HAS_MODES = { [0] = "held", [1] = "set", [2] = "unset" }
local has_lists = {}

local function asker()
  local ok, st = pcall(rq.ain.stack, 8)
  if not ok or type(st) ~= "table" then return nil end
  for _, n in ipairs(st) do
    if not HAS_HELPERS[n] then return n end
  end
end

local function has_ctx(iic, skill, fg)
  return setmetatable({ iic = iic, skill = skill, mode = HAS_MODES[fg] or "held" }, { __index = function(t, k)
    local v
    if k == "chara" then
      v = g("Ｃ", iic, "番号")
    elseif k == "row" then
      -- The hook does not fire again while its callbacks run: this is the game's answer.
      v = call("ＣＳ番号取得", iic, skill, fg)
    elseif k == "caller" then
      v = asker()
    else
      return nil
    end
    rawset(t, k, v)
    return v
  end })
end

local function has_install(skill)
  local list, add = cbs.handlers("rq.chara.on_has_skill")
  has_lists[skill] = { list = list, add = add }
  hook("ＣＳ番号取得", { only = { [2] = skill }, before = function(h)
    if #list == 0 then return end
    local iic, fg = h.args[1], h.args[3]
    if not exists(iic) or not int(fg) then return end
    local ctx = has_ctx(iic, skill, fg)
    for _, x in ipairs({ table.unpack(list) }) do
      local ok, r = cbs.run(x, "rq.chara.on_has_skill", ctx)
      if ok and r == false then
        h.skip = true
        h.result = 0
        return
      elseif ok and r ~= nil then
        cbs.report(x, "rq.chara.on_has_skill", "expected false or nil")
      end
    end
  end })
end

-- ---- the calls ----

function chara.on_has_skill(skill, fn)
  -- The game's skill table is empty while the mods load: only the range is checked.
  if not int(skill) or skill < 1 or skill > 1000 then
    error("rq.chara.on_has_skill: expected a skill number, 1 to 1000", 2)
  end
  if type(fn) ~= "function" then error("rq.chara.on_has_skill: expected a function", 2) end
  if not has_lists[skill] then has_install(skill) end
  return has_lists[skill].add(fn)
end

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

-- Free skill rows for a new skill: the game's own check (キャラスキル_空き確認, have < 18 - keep - 1)
-- as a count, without its message. rq.knownskills sets priv.skill_cap, the game's 18 (2.54.0).
local function free_rows(iic)
  local have = call("キャラスキル_保有数", iic, false)
  local keep = call("キャラ_スキル枠キープ数", iic)
  return math.max(0, (priv.skill_cap or 18) - keep - 1 - have)
end

local function room(iic)
  return free_rows(iic) > 0
end

-- learnable's opts.after: { {skill, n}, ... }, checked before anything is tried.
local function read_steps(opts)
  if opts == nil then return nil end
  if type(opts) ~= "table" then error("rq.chara.learnable: opts is a table", 3) end
  local after = opts.after
  if after == nil then return nil end
  if type(after) ~= "table" then error("rq.chara.learnable: after is a list of steps", 3) end
  local out = {}
  for i, st in ipairs(after) do
    if type(st) ~= "table" or not int(st.skill) or st.skill <= 0 or not int(st.n) or st.n < 0 then
      error("rq.chara.learnable: step " .. i .. " is not {skill = number, n = level from 0}", 3)
    end
    need_skill(st.skill, "rq.chara.learnable")
    out[i] = st
  end
  return out
end

function chara.learnable(iic, opts)
  need_chara(iic, "rq.chara.learnable")
  local steps = read_steps(opts)
  if not steps or #steps == 0 then
    return trying(with_list(), function()
      fill(iic)
      return read_list(iic), free_rows(iic)
    end)
  end
  return trying(with_list({ "Ｃ", iic }), function()
    for i, st in ipairs(steps) do
      if not set_n(iic, st.skill, st.n) then return nil, i end
      mark_boost(iic, st.skill, st.n)
    end
    fill(iic)
    return read_list(iic), free_rows(iic)
  end)
end

function chara.boosts(iic)
  need_chara(iic, "rq.chara.boosts")
  local keeps = keeps_boosts()
  local out = {}
  for _, b in ipairs(BOOSTS) do
    out[#out + 1] = { skill = b.skill, step = b.step, first = b.first, kept = keeps and boost_kept(iic, b) }
  end
  return out, keeps
end

-- The erasure rq.chara.forget is making: { iic, skill, player, mod } (2.51.0).
local forgetting = nil
local ERASE = "キャラスキル_消去" -- the skill window's ERASE, after its two questions

-- Each callback registered, in order (2.52.0), for forget_points: { fn }, taken off by its
-- remover. The C call under this wrapper is already wrapped with removers (modloader_scene.c's
-- removable runs before this file), so this wrapper returns that remover and adds its own part.
local forget_fns = {}

-- rq.chara.on_forget (0.4.0): the ctx says whether the erased skill is a Boost whose effect stays,
-- whether the player chose the erasure, the mod that called rq.chara.forget, and the skill's level.
local c_on_forget = chara.on_forget
function chara.on_forget(fn)
  if type(fn) ~= "function" then return c_on_forget(fn) end
  local remove = c_on_forget(function(ctx)
    local b = BOOST_OF[ctx.skill]
    local here = exists(ctx.iic)
    ctx.kept = b ~= nil and here and boost_kept(ctx.iic, b)
    local f = ctx.caller == "lua" and forgetting or nil
    ctx.player = ctx.caller == ERASE
      or (f ~= nil and f.player and ctx.iic == f.iic and ctx.skill == f.skill) or false
    ctx.mod = f and f.mod or nil
    ctx.n = here and ctx.slot > 0 and n_at(ctx.iic, ctx.slot, ctx.skill) or 0
    ctx.preview = false
    return fn(ctx)
  end)
  local entry = { fn = fn }
  forget_fns[#forget_fns + 1] = entry
  return function()
    if type(remove) == "function" then remove() end
    for i, e in ipairs(forget_fns) do
      if e == entry then
        table.remove(forget_fns, i)
        break
      end
    end
  end
end

-- rq.chara.forget (0.4.0): opts.player (2.51.0), and the points given back (C's third value).
local c_forget = chara.forget
function chara.forget(iic, skill, opts)
  local i, s = math.tointeger(iic), math.tointeger(skill)
  if not i or not s then error("rq.chara.forget: iic and skill are whole numbers", 2) end
  if opts ~= nil and type(opts) ~= "table" then error("rq.chara.forget: opts is a table", 2) end
  local player = opts and opts.player
  if player ~= nil and type(player) ~= "boolean" then
    error("rq.chara.forget: opts.player is true or false", 2)
  end
  local outer = forgetting
  forgetting = { iic = i, skill = s, player = player == true, mod = rq.mod.id() }
  local ok, removed, cost, back = pcall(c_forget, i, s)
  forgetting = outer
  if not ok then error(removed, 2) end
  if not removed then return false end
  return true, cost, back
end

-- rq.chara.forget_points (2.52.0): the points the on_forget callbacks would give back for erasing
-- the skill, now or (opts.n, cost, kept) as its row would be; nothing is erased or given.
function chara.forget_points(iic, skill, opts)
  need_chara(iic, "rq.chara.forget_points")
  need_skill(skill, "rq.chara.forget_points")
  if opts ~= nil and type(opts) ~= "table" then error("rq.chara.forget_points: opts is a table", 2) end
  opts = opts or {}
  for _, k in ipairs({ "player", "kept" }) do
    if opts[k] ~= nil and type(opts[k]) ~= "boolean" then
      error("rq.chara.forget_points: opts." .. k .. " is true or false", 2)
    end
  end
  if opts.n ~= nil and (not int(opts.n) or opts.n < 1) then
    error("rq.chara.forget_points: opts.n is a whole number from 1", 2)
  end
  if opts.cost ~= nil and (not int(opts.cost) or opts.cost < -1) then
    error("rq.chara.forget_points: opts.cost is a whole number from -1", 2)
  end
  local row = row_of(iic, skill)
  if not row and opts.n == nil then return 0 end
  local n = opts.n or n_at(iic, row, skill)
  local t = stype(skill)
  local rank = row and g("Ｃ", iic, "スキル", row, "ランク") or 0
  if opts.n and (t == 20 or t == 30) then rank = opts.n end
  local kept = opts.kept
  if kept == nil then
    local b = BOOST_OF[skill]
    kept = b ~= nil and (boost_kept(iic, b) or (opts.n ~= nil and keeps_boosts() and n >= RANK_MAX))
  end
  local cost = opts.cost
  if cost == nil then cost = row and cost_of(iic, row) or 0 end
  local base = { iic = iic, skill = skill, slot = row or 0, rank = rank, n = n, cost = cost,
    caller = "lua", kept = kept, player = opts.player == true, mod = rq.mod.id(), preview = true }
  local total = 0
  for _, e in ipairs({ table.unpack(forget_fns) }) do
    local ctx = {}
    for k, v in pairs(base) do ctx[k] = v end
    local ok, v = pcall(e.fn, ctx)
    if not ok then
      logf("on_forget error (forget_points): %s", tostring(v))
    elseif type(v) == "number" or type(v) == "string" then
      -- As the game's erasure counts them: a whole number above 0.
      local x = math.tointeger(tonumber(v) or 0)
      if x and x > 0 then total = total + x end
    end
  end
  return total
end

-- rq.chara.set_cost (2.53.0): the purchase record of the character's row of the skill.
function chara.set_cost(iic, skill, cost)
  need_chara(iic, "rq.chara.set_cost")
  need_skill(skill, "rq.chara.set_cost")
  if not int(cost) or cost < -1 then error("rq.chara.set_cost: cost is a whole number from -1", 2) end
  local row = row_of(iic, skill)
  if not row then return false end
  set_cost(iic, row, cost)
  return true
end

function chara.price(iic, skill, from, to)
  need_chara(iic, "rq.chara.price")
  need_skill(skill, "rq.chara.price")
  if not int(from) or not int(to) or from < 0 or to < 0 then
    error("rq.chara.price: from and to are whole numbers from 0", 2)
  end
  return price(iic, skill, from, to)
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
