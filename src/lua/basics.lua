-- The basics a new creator tries first (loader 2.33.0, notes/plans/plan-creator-basics.md in the
-- workspace):
--   rq.game.gold, set_gold, add_gold                 the money
--   rq.game.charisma, add_charisma, set_charisma     the rank and points of the system bar
--   rq.encounters.info, hold, set_steps, on_steps, on_encounter   random fights on a quest's map
--   rq.chara.add_exp, level_up, on_level_up          EXP and levels, as the game gives them
--   (rq.chara.get's uncapped is in C, modloader_roster.c, as the rest of get)
--
-- Built only on public calls: rq.ain.call, global, set_global, method and hook.
--   所持金_設定 / 所持金＋ / 所持金－      the money, clamped to 0..999,999,999 (C_変数)
--   名声_ランク設定, 名声_ポイント＋ / －, 名声_ランクアップ, Ｔ名声最大書き換え   charisma
--   ランダムエンカウント_あり               true while the area has groups; the walk and the bar ask it
--   エンカウント再計算(条件)               the steps of a full bar, after each fight and area change
--   雑魚戦闘処理(配置敵)                   a fight from the map: 0 random, 1 an enemy on the map
--   キャラ_レベルアップ(iic)               a level that takes its EXP (a fight's result)
--   レベルアップ処理(iic)                  a free level: EXP back to 0 (story, training, the reserve)
local rq = ...
local call, hook, g, set = rq.ain.call, rq.ain.hook, rq.ain.global, rq.ain.set_global

local function int(v)
  return math.type(v) == "integer"
end

local function yes(v)
  return v == true or v == 1
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
local function handlers()
  local list = {}
  local function add(fn)
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

-- Runs one handler; an error is logged once per text, as rq.on_frame's are.
local function run(h, what, ctx)
  local ok, r = pcall(h.fn, ctx)
  if ok then return r end
  local text = tostring(r)
  if not h.seen[text] then
    h.seen[text] = true
    logf("%s (%s): %s", what, h.mod, text)
  end
  return nil
end

local function check_fn(fn, name)
  if type(fn) ~= "function" then error(name .. ": expected a function", 3) end
end

-- As the loader's C calls say it (guard_game_calls), not as rq.ain.call would.
local function need_game(name)
  if not pcall(g, "◆金") then
    error(name .. ": the game is not running yet. Call it from rq.on_frame or a callback, not at the top level of a mod", 3)
  end
end

local function whole(v, name, what, lo)
  if not int(v) or (lo and v < lo) then
    error(string.format("%s: %s must be a whole number%s", name, what,
      lo and string.format(", %d or more", lo) or ""), 3)
  end
end

-- ---- rq.game: the money and charisma ----

local game = rq.game
if type(game) ~= "table" then game = {}; rq.game = game end

local GOLD_MAX = 999999999

-- The money, charisma and donations on the system bar, as the vault updates them.
local function show_bar()
  pcall(call, "システムバー_更新", 2, -1)
  pcall(call, "システム情報_更新", 0, -1)
end

function game.gold()
  need_game("rq.game.gold")
  return call("所持金")
end

function game.set_gold(n)
  need_game("rq.game.set_gold")
  whole(n, "rq.game.set_gold", "the money")
  if n < 0 then n = 0 elseif n > GOLD_MAX then n = GOLD_MAX end
  local v = call("所持金_設定", n)
  show_bar()
  return v
end

function game.add_gold(n)
  need_game("rq.game.add_gold")
  whole(n, "rq.game.add_gold", "the amount")
  if n > GOLD_MAX then n = GOLD_MAX elseif n < -GOLD_MAX then n = -GOLD_MAX end
  local v
  if n >= 0 then v = call("所持金＋", n) else v = call("所持金－", -n) end
  show_bar()
  return v
end

function game.charisma()
  need_game("rq.game.charisma")
  return call("名声_ランク"), call("名声_ポイント"), call("名声_ポイント最大")
end

function game.add_charisma(n)
  need_game("rq.game.add_charisma")
  whole(n, "rq.game.add_charisma", "the points")
  if n > GOLD_MAX then n = GOLD_MAX elseif n < -GOLD_MAX then n = -GOLD_MAX end
  if n >= 0 then
    call("名声_ポイント＋", n)
    -- As the result screen (T_Result::create): one rank at most; the rest waits.
    call("名声_ランクアップ")
  else
    call("名声_ポイント－", -n)
  end
  show_bar()
  return game.charisma()
end

function game.set_charisma(rank, points)
  need_game("rq.game.set_charisma")
  whole(rank, "rq.game.set_charisma", "the rank", 0)
  if points ~= nil then whole(points, "rq.game.set_charisma", "the points", 0) end
  call("名声_ランク設定", rank)
  call("Ｔ名声最大書き換え")
  if points ~= nil then set("◆名声", points) end
  show_bar()
  return game.charisma()
end

-- ---- rq.encounters: random fights on a quest's map ----

local encounters = rq.encounters
if type(encounters) ~= "table" then encounters = {}; rq.encounters = encounters end

local STEPS_MIN, STEPS_MAX = 1, 999
local holds = {} -- mod id -> true while that mod holds random fights off
local hold_hooked = false

local function held()
  local ids = {}
  for id in pairs(holds) do ids[#ids + 1] = id end
  table.sort(ids)
  return ids
end

local function groups()
  local out = {}
  for i = 1, 5 do
    local v = gint("◆エンカウント中身", i)
    if v ~= 0 then out[#out + 1] = v end
  end
  return out
end

local function on_map()
  local d = rq.dungeon
  local ok, v = pcall(function() return d and d.active and d.active() end)
  return ok and v == true
end

-- The bar follows the map panel's own show, as マップ部品_表示設定 does; a hold hides it.
local function bar_show()
  if not on_map() then return end
  local ok, shown = pcall(rq.ain.method, { "g_sMapParts" }, "T_MapParts@getShow")
  if ok and type(shown) == "boolean" then pcall(call, "エンカウントバー_表示設定", shown) end
end

local function bar_update()
  if on_map() then pcall(call, "エンカウントバー_更新") end
end

function encounters.info()
  if not on_map() then return nil end
  local steps = gint("◆ランダムエンカウント歩数")
  local ok, dist = pcall(call, "キャラ_歩き距離")
  local walked = 0
  if ok and type(dist) == "number" then walked = math.floor(dist / 4 + 1e-6) end
  if walked > steps then walked = steps end
  if walked < 0 then walked = 0 end
  return {
    groups = groups(),
    steps = steps,
    walked = walked,
    on = yes(call("ランダムエンカウント_あり")),
    held = held(),
  }
end

function encounters.hold(on)
  if type(on) ~= "boolean" then error("rq.encounters.hold: expected true or false", 2) end
  if not hold_hooked then
    hold_hooked = true
    hook("ランダムエンカウント_あり", { after = function(h)
      if next(holds) ~= nil then h.result = false end
    end })
  end
  holds[mod_id()] = on or nil
  bar_show()
end

function encounters.set_steps(n)
  need_game("rq.encounters.set_steps")
  whole(n, "rq.encounters.set_steps", "the steps")
  if n < STEPS_MIN or n > STEPS_MAX then
    error(string.format("rq.encounters.set_steps: the steps must be %d to %d", STEPS_MIN, STEPS_MAX), 2)
  end
  call("ランダムエンカウント_歩数設定", n)
  bar_update()
end

local steps_list, add_steps = handlers()
local steps_hooked = false

function encounters.on_steps(fn)
  check_fn(fn, "rq.encounters.on_steps")
  if not steps_hooked then
    steps_hooked = true
    hook("エンカウント再計算", { after = function(h)
      if #steps_list == 0 then return end
      local game_steps = gint("◆ランダムエンカウント歩数")
      local ctx = { steps = game_steps, area_change = h.args[1] == "エリア切り替え" }
      for _, x in ipairs({ table.unpack(steps_list) }) do
        local r = run(x, "rq.encounters.on_steps", ctx)
        if type(r) == "number" then
          r = math.floor(r)
          if r < STEPS_MIN then r = STEPS_MIN elseif r > STEPS_MAX then r = STEPS_MAX end
          ctx.steps = math.tointeger(r)
        end
      end
      if ctx.steps ~= game_steps then
        call("ランダムエンカウント_歩数設定", ctx.steps)
        bar_update()
      end
    end })
  end
  return add_steps(fn)
end

local enc_list, add_enc = handlers()
local enc_hooked = false

function encounters.on_encounter(fn)
  check_fn(fn, "rq.encounters.on_encounter")
  if not enc_hooked then
    enc_hooked = true
    hook("雑魚戦闘処理", { before = function(h)
      if #enc_list == 0 then return end
      local ctx = { placed = h.args[1] == 1, groups = groups() }
      local cancel = false
      for _, x in ipairs({ table.unpack(enc_list) }) do
        if run(x, "rq.encounters.on_encounter", ctx) == false then cancel = true end
      end
      if cancel then
        -- What the game returns when no fight happens (the adventurers ignored).
        h.skip = true
        h.result = 0
        bar_update()
      end
    end })
  end
  return add_enc(fn)
end

-- ---- rq.chara: EXP and levels ----

local chara = rq.chara
local ATHENA = 27 -- Athena 2.0: the game keeps her at level 1 with no EXP (レベルアップ処理)
local INT_MAX = 2147483647

local function level_of(iic)
  local ok, v = pcall(call, "キャラ_レベル", iic)
  if ok and int(v) then return v end
  return -1
end

local function chara_no(iic, name)
  if not int(iic) then error(name .. ": expected a roster slot", 3) end
  local no = chara.number(iic)
  if not no then error(string.format("%s: no character at %d", name, iic), 3) end
  return no
end

local lv_list, add_lv = handlers()
local lv_hooked = false
local battle_depth = 0 -- inside a fight's result (the party's and the reserve's level-ups)
local exp_from, free_from = {}, {}

local function fire_level(iic, from, free)
  if #lv_list == 0 then return end
  local level = level_of(iic)
  if level <= from then return end
  for _, x in ipairs({ table.unpack(lv_list) }) do
    run(x, "rq.chara.on_level_up", { iic = iic, level = level, from = from, free = free,
      battle = battle_depth > 0 })
  end
end

local function hook_levels()
  if lv_hooked then return end
  lv_hooked = true
  local function depth(name)
    hook(name, {
      before = function() battle_depth = battle_depth + 1 end,
      after = function() if battle_depth > 0 then battle_depth = battle_depth - 1 end end,
    })
  end
  depth("戦闘データ_レベルアップ")
  depth("Ｔ戦闘後の取得経験値算出")
  hook("キャラ_レベルアップ", {
    before = function(h) exp_from[h.args[1]] = level_of(h.args[1]) end,
    after = function(h)
      local iic = h.args[1]
      local from = exp_from[iic]
      exp_from[iic] = nil
      if yes(h.result) and from then fire_level(iic, from, false) end
    end,
  })
  hook("レベルアップ処理", {
    before = function(h) free_from[h.args[1]] = level_of(h.args[1]) end,
    after = function(h)
      local iic = h.args[1]
      local from = free_from[iic]
      free_from[iic] = nil
      if from then fire_level(iic, from, true) end
    end,
  })
end

function chara.on_level_up(fn)
  check_fn(fn, "rq.chara.on_level_up")
  hook_levels()
  return add_lv(fn)
end

function chara.add_exp(iic, n)
  need_game("rq.chara.add_exp")
  local no = chara_no(iic, "rq.chara.add_exp")
  whole(n, "rq.chara.add_exp", "the EXP", 0)
  if n == 0 or no == ATHENA then return 0 end
  -- The game gives none at the cap (Ｃ経験値追加).
  if level_of(iic) >= call("キャラ_レベル最大", iic) then return 0 end
  local exp = call("キャラ_ＥＸＰ", iic)
  exp = (exp > INT_MAX - n) and INT_MAX or exp + n
  call("キャラ_ＥＸＰ設定", iic, exp)
  -- As a fight's result (戦闘データ_レベルアップ): a level while the bar is full.
  local gained = 0
  while gained < 10000 and yes(call("キャラ_レベルアップ", iic)) do gained = gained + 1 end
  local max = call("キャラ_ＥＸＰ最大", iic)
  if int(max) and max >= 0 and call("キャラ_ＥＸＰ", iic) > max then call("キャラ_ＥＸＰ設定", iic, max) end
  return gained
end

function chara.level_up(iic, n)
  need_game("rq.chara.level_up")
  local no = chara_no(iic, "rq.chara.level_up")
  if n == nil then n = 1 end
  whole(n, "rq.chara.level_up", "the levels", 1)
  if no == ATHENA then return 0 end
  local gained = 0
  for _ = 1, n do
    local before = level_of(iic)
    call("レベルアップ処理", iic)
    if level_of(iic) <= before then break end
    gained = gained + 1
  end
  return gained
end
