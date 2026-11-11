-- The basics a new creator tries first (loader 2.33.0, notes/plans/plan-creator-basics.md in the
-- workspace):
--   rq.game.gold, set_gold, add_gold                 the money
--   rq.game.charisma, add_charisma, set_charisma     the rank and points of the system bar
--   rq.encounters.info, hold, set_steps, on_steps, on_encounter   random fights on a quest's map
--   rq.chara.add_exp, level_up, on_level_up          EXP and levels, as the game gives them
--   (rq.chara.get's uncapped is in C, modloader_roster.c, as the rest of get)
-- And a fight's rewards (loader 2.34.0, notes/plans/plan-rewards-savedata.md):
--   rq.battle.on_reward                              the gold and EXP, before the result screen
--   rq.battle.bags, set_bag                          the enemies' gold bags
-- And the party (loader 2.35.0, notes/plans/plan-creator-comfort.md):
--   rq.party.roster, state, set_state, heal          where each character is, the game's heals
--   rq.chara.heal                                    one character's heal
--
-- Built only on public calls: rq.ain.call, global, set_global, method and hook.
--   所持金_設定 / 所持金＋ / 所持金－      the money, clamped to 0..999,999,999 (C_変数)
--   名声_ランク設定, 名声_ポイント＋ / －, 名声_ランクアップ, Ｔ名声最大書き換え   charisma
--   ランダムエンカウント_あり               true while the area has groups; the walk and the bar ask it
--   エンカウント再計算(条件)               the steps of a full bar, after each fight and area change
--   雑魚戦闘処理(配置敵)                   a fight from the map: 0 random, 1 an enemy on the map
--   キャラ_レベルアップ(iic)               a level that takes its EXP (a fight's result)
--   レベルアップ処理(iic)                  a free level: EXP back to 0 (story, training, the reserve)
--   Ｔ戦闘後の取得経験値算出(結果)         a fight's gold and EXP (results 1 and 4); gives the reserve
--                                         its share (Ｃ経験値追加) and returns the enemies defeated
--   Ｃ登録状態 / Ｃ状態変化 / Ｃ体力 / Ｃ体力最大   where a character is, the status, the HP
--   Ｃ補給／体力(iic, 条件) / Ｃ補給／スキル(iic, 条件)   the game's heals ("半分": up to half)
--   戦闘データ_作成 / 戦闘データ_削除      the gold bags rolled (after on_start) and cleared
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

-- ---- rq.battle.on_reward: a fight's gold and EXP (loader 2.34.0) ----
--
-- Ｔ戦闘後の取得経験値算出 sums everything; the result screen is made right after it returns
-- (戦闘リザルト_金袋入手), shows ◆取得金額 and ◆戦闘後／経験値１ and ５, adds PTA_EXP[1..5] to the
-- party (戦闘リザルト_ＥＸＰ加算) and pays ◆取得金額 as it closes (Ｒ８戦闘_後処理). Only the
-- reserve's share is given inside the function, so while a callback waits its Ｃ経験値追加 calls
-- are held and given after the callbacks, with the game's own check for a free level.

local battle = rq.battle
local CHEAT_GOLD = 228 -- FA[228]: the gold the game's cheat check counts (お金増えた)
local LUCKY = "幸福きゃん" -- the fight that gives the reserve nothing

local reward_list, add_reward = handlers()
local reward_hooked = false
local summing = nil -- { held = {iic...}, lucky } while the sum runs and a callback waits

-- A number rounded down and kept to 0..INT_MAX; nil for anything else.
local function count(v)
  if type(v) ~= "number" or v ~= v then return nil end
  v = math.floor(v)
  if v < 0 then v = 0 elseif v > INT_MAX then v = INT_MAX end
  return math.tointeger(v)
end

local function party_slots(name)
  local out = {}
  for slot = 1, 5 do
    local iic = gint(name, slot)
    if iic > 0 then out[#out + 1] = { slot = slot, iic = iic } end
  end
  return out
end

local function fallen(iic)
  local ok, st = pcall(call, "Ｃ状態変化", iic, -1)
  return ok and st == 3
end

-- The game's rule: the smallest ally EXP that is not 0, divided by 20.
local function reserve_rule(values, lucky)
  if lucky then return 0 end
  local low = 0
  for _, e in ipairs(values) do
    if e ~= 0 and (low == 0 or e < low) then low = e end
  end
  return low // 20
end

-- What each ally gets now: a written number, or the game's for a fallen or capped ally.
local function ally_values(allies, game)
  local out = {}
  for i = 1, #game.allies do
    local a = allies[i]
    local e = type(a) == "table" and count(rawget(a, "exp")) or nil
    if e == nil or game.locked[i] then e = game.allies[i] end
    out[i] = e
  end
  return out
end

local function reward_ctx(h, held)
  local slots = party_slots("PTA")
  local game = { gold = gint("◆取得金額"), exp = gint("◆戦闘後／経験値５"), allies = {}, locked = {} }
  local allies = {}
  for i, p in ipairs(slots) do
    local exp = gint("PTA_EXP", p.slot)
    local down, capped = fallen(p.iic), gint("PTA_EXP_B1", p.slot) == 1
    game.allies[i] = exp
    game.locked[i] = down or capped
    allies[i] = { iic = p.iic, exp = exp, down = down, capped = capped }
  end
  local enemies = {}
  for _, p in ipairs(party_slots("PTB")) do enemies[#enemies + 1] = p.iic end
  local state = { exp = game.exp, allies = allies }
  local ctx = {
    result = int(h.args[1]) and h.args[1] or 0,
    kills = int(h.result) and h.result or 0,
    bags = gint("◆取得金袋数"),
    gold = game.gold,
    base = gint("◆戦闘後／経験値１"),
    group = gint("◆戦闘グループ"),
    rank = gint("◆戦闘ランク"),
    allies = allies,
    enemies = enemies,
  }
  setmetatable(ctx, {
    __index = function(_, k)
      if k == "exp" then return state.exp end
      if k == "reserve" then
        if state.reserve ~= nil then return state.reserve end
        return reserve_rule(ally_values(allies, game), held.lucky)
      end
    end,
    __newindex = function(t, k, v)
      if k == "exp" then
        local n = count(v)
        if n == nil then error("rq.battle.on_reward: ctx.exp must be a number", 2) end
        local old = state.exp
        for i, a in ipairs(allies) do
          local e = count(rawget(a, "exp"))
          if e and e > 0 and not game.locked[i] then
            a.exp = old > 0 and math.min(INT_MAX, e * n // old) or n
          end
        end
        state.exp = n
      elseif k == "reserve" then
        local n = count(v)
        if n == nil then error("rq.battle.on_reward: ctx.reserve must be a number", 2) end
        state.reserve = n
      else
        rawset(t, k, v)
      end
    end,
  })
  return ctx, state, game, slots
end

-- Gives the reserve its share as the game's loop does: Ｃ経験値追加, then a free level for a
-- full bar under the cap. on_level_up reports it as a fight's (battle and free).
local function give_reserve(held, share)
  if share <= 0 then return end
  battle_depth = battle_depth + 1
  for _, iic in ipairs(held) do
    local ok, err = pcall(function()
      call("Ｃ経験値追加", iic, share)
      if call("ＣＥＸＰ", iic, -1) == call("ＣＥＸＰ最大", iic, -1)
          and call("ＣＬＶ", iic, -1) < call("ＣＬＶ最大", iic, -1) then
        call("レベルアップ処理", iic)
      end
    end)
    if not ok then logf("rq.battle.on_reward: the reserve's share for %d: %s", iic, tostring(err)) end
  end
  battle_depth = battle_depth - 1
end

local function write_back(ctx, state, game, slots)
  local gold = count(rawget(ctx, "gold"))
  if gold ~= nil and gold ~= game.gold then
    set("◆取得金額", gold)
    set("FA", CHEAT_GOLD, gint("FA", CHEAT_GOLD) + (gold - game.gold))
  end
  if state.exp ~= game.exp then
    set("◆戦闘後／経験値５", state.exp)
    local total = gint("◆クエストＥＸＰ合計") + (state.exp - game.exp)
    set("◆クエストＥＸＰ合計", math.max(0, math.min(INT_MAX, total)))
  end
  local values = ally_values(state.allies, game)
  for i, p in ipairs(slots) do
    local e, was = values[i], game.allies[i]
    if e ~= was then
      set("PTA_EXP", p.slot, e)
      -- The quest's level-up bonus, for a level the game's EXP would not have brought.
      local ok, exp, max = pcall(function()
        return call("ＣＥＸＰ", p.iic, -1), call("ＣＥＸＰ最大", p.iic, -1)
      end)
      if ok and int(exp) and int(max) and max <= exp + e and max > exp + was then
        pcall(call, "クエストボーナス／ＬＶＵＰ／加算")
      end
    end
  end
  return values
end

local function hook_reward()
  if reward_hooked then return end
  reward_hooked = true
  hook("Ｔ戦闘後の取得経験値算出", {
    before = function()
      summing = nil
      if #reward_list == 0 then return end
      local ok, ev = pcall(g, "◇戦闘イベント名")
      summing = { held = {}, lucky = ok and ev == LUCKY }
    end,
    after = function(h)
      local held = summing
      summing = nil
      if not held then return end
      local share
      local ok, err = pcall(function()
        local ctx, state, game, slots = reward_ctx(h, held)
        share = reserve_rule(game.allies, held.lucky) -- the game's, if anything below fails
        for _, x in ipairs({ table.unpack(reward_list) }) do run(x, "rq.battle.on_reward", ctx) end
        local values = write_back(ctx, state, game, slots)
        if state.reserve ~= nil then share = state.reserve else share = reserve_rule(values, held.lucky) end
      end)
      if not ok then logf("rq.battle.on_reward: %s", tostring(err)) end
      give_reserve(held.held, share or 0)
    end,
  })
  hook("Ｃ経験値追加", { before = function(h)
    if summing then
      summing.held[#summing.held + 1] = h.args[1]
      h.skip = true
    end
  end })
end

function battle.on_reward(fn)
  check_fn(fn, "rq.battle.on_reward")
  hook_reward()
  return add_reward(fn)
end

-- ---- rq.battle.bags, set_bag: the enemies' gold bags (loader 2.34.0) ----
--
-- g_anMonsterGold[i] is the bag of the enemy PTB[i + 1]: -1 none, 0 carried, 1 picked (when it
-- falls, 戦闘_モンスター消し). The game rolls them (50% each) in 戦闘_初期化, after on_start, so a
-- set_bag before the roll is kept and written over the roll.

local BAG_NONE, BAG_CARRIED, BAG_PICKED = -1, 0, 1
local bag_phase = nil -- "starting" from Ｔ戦闘前初期化 to the roll, "fight" until 戦闘データ_削除
local bag_wish = {}   -- enemy slot -> true or false, set before the roll
local bags_hooked = false

local function hook_bags()
  if bags_hooked then return end
  local ok = pcall(function()
    hook("Ｔ戦闘前初期化", { before = function()
      bag_phase = "starting"
      bag_wish = {}
    end })
    hook("戦闘データ_作成", { after = function()
      bag_phase = "fight"
      local wish = bag_wish
      bag_wish = {}
      for i = 0, 4 do
        local want = wish[gint("PTB", i + 1)]
        if want ~= nil then set("g_anMonsterGold", i, want and BAG_CARRIED or BAG_NONE) end
      end
    end })
    hook("戦闘データ_削除", { after = function()
      bag_phase = nil
      bag_wish = {}
    end })
  end)
  if ok then bags_hooked = true end
end

hook_bags()
do
  local stop
  stop = rq.on_frame(function()
    hook_bags()
    if bags_hooked and stop then stop() end
  end)
end

local function enemy_index(iic)
  for i = 0, 4 do
    if gint("PTB", i + 1) == iic then return i end
  end
  return nil
end

function battle.bags()
  need_game("rq.battle.bags")
  local out = {}
  if bag_phase == nil then return out end
  for i = 0, 4 do
    local iic = gint("PTB", i + 1)
    if iic > 0 then
      local v
      if bag_phase == "starting" then
        v = bag_wish[iic] and BAG_CARRIED or BAG_NONE
      else
        v = gint("g_anMonsterGold", i)
      end
      out[#out + 1] = { enemy = iic, carries = v >= BAG_CARRIED, picked = v == BAG_PICKED }
    end
  end
  return out
end

function battle.set_bag(iic, carries)
  need_game("rq.battle.set_bag")
  if not int(iic) then error("rq.battle.set_bag: expected an enemy's slot", 2) end
  if type(carries) ~= "boolean" then error("rq.battle.set_bag: expected true or false", 2) end
  local i = bag_phase and enemy_index(iic)
  if not i then error(string.format("rq.battle.set_bag: %d is not an enemy of this fight", iic), 2) end
  if bag_phase == "starting" then
    bag_wish[iic] = carries
    return true
  end
  local v = gint("g_anMonsterGold", i)
  if v == BAG_PICKED or fallen(iic) then return false end
  set("g_anMonsterGold", i, carries and BAG_CARRIED or BAG_NONE)
  return true
end

-- ---- rq.party: where each character is, and the game's heals (2.35.0) ----
-- 登録状態 (where a character is): 0 not recruited, 1 one of the five ranks, 2 the first army,
-- 3 the first army used in this quest (switched out: not placed again until the quest ends),
-- 4 the second army (C_キャラ.jaf, キャラ_スタメン to キャラ_２軍). 状態変化: 3 is downed, and the
-- game's party heals (ＰＴＡ体力回復, ＰＴＡスキル回復) skip it.

local party = rq.party
local DOWNED = 3

local function slot_count()
  local ok, n = pcall(call, "キャラ_数")
  if ok and int(n) and n >= 20 then return n end
  return 200
end

local function cint(name, iic)
  local ok, v = pcall(call, name, iic, -1)
  if ok and int(v) then return v end
  return 0
end

local function check_state(state, name, what)
  if not int(state) or state < 0 or state > 4 then
    error(string.format("%s: %s is a whole number from 0 to 4 (rq.ids.party_state)", name, what), 3)
  end
end

local function heal_opts(opts, name)
  if opts == nil then return {} end
  if type(opts) ~= "table" then error(name .. ": opts is a table", 3) end
  for _, k in ipairs({ "skills", "half", "all" }) do
    if opts[k] ~= nil and type(opts[k]) ~= "boolean" then error(name .. ": " .. k .. " is true or false", 3) end
  end
  return opts
end

-- The game's heal of one character: HP to the most, or up to half; the skill uses too. A downed
-- character is left as it is. Returns true when healed.
local function heal_one(iic, opts)
  if cint("Ｃ状態変化", iic) == DOWNED then return false end
  call("Ｃ補給／体力", iic, opts.half and "半分" or "")
  if opts.skills then call("Ｃ補給／スキル", iic, "") end
  return true
end

---rq.party.roster(state?): { iic, state, hp, hp_max, status } for each recruited character. 2.35.0.
function party.roster(state)
  if state ~= nil then check_state(state, "rq.party.roster", "state") end
  need_game("rq.party.roster")
  local out = {}
  for iic = 20, slot_count() do
    local st = cint("Ｃ登録状態", iic)
    if st ~= 0 and (state == nil or st == state) then
      out[#out + 1] = { iic = iic, state = st, hp = cint("Ｃ体力", iic), hp_max = cint("Ｃ体力最大", iic),
        status = cint("Ｃ状態変化", iic) }
    end
  end
  return out
end

---rq.party.state(iic): where a character is, 0 to 4; 0 for a slot with no character. 2.35.0.
function party.state(iic)
  if not int(iic) then error("rq.party.state: expected a roster slot", 2) end
  need_game("rq.party.state")
  if not chara.number(iic) then return 0 end
  return cint("Ｃ登録状態", iic)
end

---rq.party.set_state(iic, state): moves a character between the first army (2), used (3) and
---the second army (4). 2.35.0.
function party.set_state(iic, state)
  if not int(iic) then error("rq.party.set_state: expected a roster slot", 2) end
  check_state(state, "rq.party.set_state", "state")
  need_game("rq.party.set_state")
  if not chara.number(iic) then error(string.format("rq.party.set_state: no character at %d", iic), 2) end
  local now = cint("Ｃ登録状態", iic)
  if now == 0 then error(string.format("rq.party.set_state: %d is not recruited (rq.chara.recruit)", iic), 2) end
  if now == 1 or state == 1 then
    error("rq.party.set_state: the five ranks change with rq.party.set", 2)
  end
  if state == 0 then error("rq.party.set_state: a character leaves with rq.chara.dismiss", 2) end
  call("Ｃ登録状態", iic, state)
  return true
end

---rq.party.heal(opts?): the five ranks' HP to the most, the downed left as they are; skills,
---half, all (every recruited character). Returns how many were healed. 2.35.0.
function party.heal(opts)
  opts = heal_opts(opts, "rq.party.heal")
  need_game("rq.party.heal")
  local n = 0
  if opts.all then
    for iic = 20, slot_count() do
      if cint("Ｃ登録状態", iic) ~= 0 and heal_one(iic, opts) then n = n + 1 end
    end
  else
    for _, iic in ipairs(party.get()) do
      if iic ~= 0 and heal_one(iic, opts) then n = n + 1 end
    end
  end
  return n
end

---rq.chara.heal(iic, opts?): one character's HP to the most (half: up to half), skills: the
---skill uses too; a downed character is left as it is (false). 2.35.0.
function chara.heal(iic, opts)
  if not int(iic) then error("rq.chara.heal: expected a roster slot", 2) end
  opts = heal_opts(opts, "rq.chara.heal")
  if opts.all ~= nil then error("rq.chara.heal: all is rq.party.heal's", 2) end
  need_game("rq.chara.heal")
  if not chara.number(iic) then error(string.format("rq.chara.heal: no character at %d", iic), 2) end
  return heal_one(iic, opts)
end
