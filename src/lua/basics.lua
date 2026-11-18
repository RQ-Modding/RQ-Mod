-- The basics a new creator tries first (loader 2.33.0):
--   rq.game.gold, set_gold, add_gold                 the money
--   rq.game.charisma, add_charisma, set_charisma     the rank and points of the system bar
--   rq.encounters.info, hold, set_steps, on_steps, on_encounter   random fights on a quest's map
--   rq.encounters.set_groups (2.40.0)                the area's groups, a mod's among them
--   rq.chara.add_exp, level_up, on_level_up          EXP and levels, as the game gives them
--   (rq.chara.get's uncapped is in C, modloader_roster.c, as the rest of get)
-- And a fight's rewards (loader 2.34.0):
--   rq.battle.on_reward                              the gold and EXP, before the result screen
--   rq.battle.bags, set_bag                          the enemies' gold bags
-- And the party (loader 2.35.0):
--   rq.party.roster, state, set_state, heal          where each character is, the game's heals
--   rq.chara.heal                                    one character's heal
-- And the mode and a mod's fights (loader 2.36.0):
--   rq.game.mode, set_mode                           Classic or TADA (◆２倍モード)
--   rq.battle.start, backgrounds                     a fight of a mod's, the boss's rules and more
--
-- Built only on public calls: rq.ain.call, global, set_global, method and hook.
--   所持金_設定 / 所持金＋ / 所持金－      the money, clamped to 0..999,999,999 (C_変数)
--   名声_ランク設定, 名声_ポイント＋ / －, 名声_ランクアップ, Ｔ名声最大書き換え   charisma
--   ランダムエンカウント_あり               true while the area has groups; the walk and the bar ask it
--   エンカウント再計算(条件)               the steps of a full bar, after each fight and area change
--   エンカウント中身登録(a1..a5)           the area's groups (◆エンカウント中身[1..5]), 0 none (2.40.0)
--   雑魚戦闘処理(配置敵)                   a fight from the map: 0 random, 1 an enemy on the map
--   キャラ_レベルアップ(iic)               a level that takes its EXP (a fight's result)
--   レベルアップ処理(iic)                  a free level: EXP back to 0 (story, training, the reserve)
--   Ｔ戦闘後の取得経験値算出(結果)         a fight's gold and EXP (results 1 and 4); gives the reserve
--                                         its share (Ｃ経験値追加) and returns the enemies defeated
--   Ｃ登録状態 / Ｃ状態変化 / Ｃ体力 / Ｃ体力最大   where a character is, the status, the HP
--   Ｃ補給／体力(iic, 条件) / Ｃ補給／スキル(iic, 条件)   the game's heals ("半分": up to half)
--   戦闘データ_作成 / 戦闘データ_削除      the gold bags rolled (after on_start) and cleared
--   特定戦闘処理(exp, m1..m5)              the scripted fight: 1 won, 2 lost, 9 any other end
--   ボス戦闘処理(exp, m1..m5, 条件)        the boss fight; 条件 "強化" (◆敵強化率 130), "２倍体力"
--   ＰＴＢ登録(exp, m1..m5)                builds the enemies; ◆名前変更フラグ / ◆名前変更[1..5] after
--   ◆戦闘背景番号, g_sBattleBackInfo.m_asList[i].m_nNo / m_szName    the fight's background
--   システムバー_モード / _マップ番号 / _作成, 全画面色   the screen put back after a fight
-- rq.battle.start's internal calls (fight_mark, in_battle) and the monster and music checks come
-- in the loader's private table, the chunk's second argument.
local rq, priv = ...
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

-- ---- rq.game.mode: Classic or TADA (loader 2.36.0) ----
--
-- ◆２倍モード: 0 Classic, 1 TADA, chosen at the support menu (関数／クエスト／基本.jaf). The easy
-- mode (◆ｅａｓｙモード) is a leftover of the first Rance Quest that no code of Magnum's turns on;
-- on, Ｔクエストメニュー前 forces world 1 and deletes six quests each time the quest menu opens.

local MODES = { classic = 0, tada = 1 }

local function save_loaded()
  return rq.world and rq.world.current and rq.world.current() ~= nil
end

function game.mode()
  if not save_loaded() then return nil end
  local ok, v = pcall(g, "◆２倍モード")
  if not ok then return nil end
  return v == 1 and "tada" or "classic"
end

function game.set_mode(mode)
  if MODES[mode] == nil then error("rq.game.set_mode: the mode is \"classic\" or \"tada\"", 2) end
  need_game("rq.game.set_mode")
  if not save_loaded() then error("rq.game.set_mode: no save is loaded", 2) end
  set("◆２倍モード", MODES[mode])
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

-- The area's groups (2.40.0): the game's numbers and a mod's (rq.monster.group, in monster.lua,
-- which builds a mod's group after 雑魚セット生成), written as the area's script writes them.
function encounters.set_groups(list)
  need_game("rq.encounters.set_groups")
  if type(list) ~= "table" then error("rq.encounters.set_groups: expected a list of groups", 2) end
  local n = #list
  if n > 5 then error("rq.encounters.set_groups: at most 5 groups", 2) end
  for k in pairs(list) do
    if not int(k) or k < 1 or k > n then error("rq.encounters.set_groups: expected a list of groups", 2) end
  end
  local nos = {}
  for i = 1, 5 do
    if i <= n then
      local no, err = priv.group_check(list[i])
      if not no then error("rq.encounters.set_groups: " .. err, 2) end
      nos[i] = no
    else
      nos[i] = 0
    end
  end
  if not on_map() then error("rq.encounters.set_groups: not on a quest's map", 2) end
  call("エンカウント中身登録", nos[1], nos[2], nos[3], nos[4], nos[5])
  bar_show()
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
local fight_watch = nil -- rq.battle.start's fight: { gold = seed or nil, exp, paid, kills }

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
      if #reward_list == 0 and not fight_watch then return end
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
        local watch = fight_watch
        if watch and watch.gold then rawset(ctx, "gold", watch.gold) end
        for _, x in ipairs({ table.unpack(reward_list) }) do run(x, "rq.battle.on_reward", ctx) end
        local values = write_back(ctx, state, game, slots)
        if watch then
          watch.paid = true
          watch.exp = state.exp
          watch.gold_paid = count(rawget(ctx, "gold")) or game.gold
          watch.kills = ctx.kills
        end
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

-- ---- rq.party.on_go_home (2.47.0) ----
-- 割り込み２／戦闘後(fg), after each fight on a quest's map (from 割り込み２), sends someone home:
--   Aten (character 40) in a party of two or more: FA[317]++ (her fights this time), then
--   RAND(5) == 1 (1 in 5) plays アテン／引きこもり and ＰＴＡ解除 takes her out (FA[115] counts it);
--   else the wearer of item 209 (ＰＴＡ＿Ｉ有無(209)) in a party of two or more "went home" at once.
-- The callbacks run where the game decides: at RAND(5) inside it for Aten, after ＰＴＡ＿Ｉ有無(209)
-- inside it for the item. A changed chance is rolled with the game's RAND(100) and given to the
-- game as its own roll's answer (RAND(5) 1 or 2), or as no wearer (0).
local GO_HOME = "割り込み２／戦闘後"
local ATEN = 40
local home_list, add_home = handlers()
local home_hooked = false
local in_home = 0

local function caller_of(name)
  local ok, st = pcall(rq.ain.stack, 6)
  if not ok or type(st) ~= "table" then return nil end
  for _, n in ipairs(st) do
    if n ~= name then return n end
  end
  return nil
end

local function home_chance(ctx)
  for _, h in ipairs({ table.unpack(home_list) }) do
    local r = run(h, "rq.party.on_go_home", ctx)
    if r ~= nil then
      if int(r) and r >= 0 and r <= 100 then
        ctx.chance = r
      elseif not h.seen["return"] then
        h.seen["return"] = true
        logf("rq.party.on_go_home (%s): expected a chance from 0 to 100, or nil", h.mod)
      end
    end
  end
  return ctx.chance
end

local function home_install()
  if home_hooked then return end
  home_hooked = true
  hook(GO_HOME, {
    before = function() in_home = in_home + 1 end,
    after = function() if in_home > 0 then in_home = in_home - 1 end end,
  })
  hook("RAND", { before = function(h)
    if in_home == 0 or h.args[1] ~= 5 or #home_list == 0 then return end
    if caller_of("RAND") ~= GO_HOME then return end
    local okc, iic = pcall(call, "Ｃ番号取得", ATEN)
    if not okc or not int(iic) or iic == 0 then return end
    local c = home_chance({ iic = iic, no = ATEN, reason = "aten", chance = 20 })
    if c == 20 then return end
    h.skip, h.result = true, (c > 0 and call("RAND", 100) <= c) and 1 or 2
  end })
  hook("ＰＴＡ＿Ｉ有無", { after = function(h)
    if in_home == 0 or h.args[1] ~= 209 or #home_list == 0 then return end
    local iic = h.result
    if not int(iic) or iic == 0 then return end
    if caller_of("ＰＴＡ＿Ｉ有無") ~= GO_HOME then return end
    local okn, count = pcall(call, "ＰＴＡ総数")
    if not okn or count == 1 then return end
    local c = home_chance({ iic = iic, no = chara.number(iic), reason = "item", item = 209, chance = 100 })
    if c >= 100 then return end
    if c <= 0 or call("RAND", 100) > c then h.result = 0 end
  end })
end

---rq.party.on_go_home(fn): who the game would send home after a fight (Aten's Go Home, item 209),
---with the chance; fn returns another chance (0 to stay) or nil. 2.47.0.
function party.on_go_home(fn)
  check_fn(fn, "rq.party.on_go_home")
  home_install()
  return add_home(fn)
end

-- The game to the title inside 割り込み２／戦闘後 skips its after hook.
rq.on_reset(function() in_home = 0 end)

-- ---- rq.battle.start (loader 0.22.0; in Lua with the boss and the rest since 2.36.0) ----
--
-- The scripted fight (特定戦闘処理) or the boss fight (ボス戦闘処理), called with rq.ain.call: it
-- blocks until the fight and its result screens end, while rq.on_frame keeps running. The C side
-- keeps only the mark (priv.fight_mark: rq.party.set refuses, a restart inside forgets it).

local NONE = -2147483648
local TITLE_ERROR = "rq: the game went to the title" -- modloader_unwind.c
local OPTS = { enemies = true, exp = true, boss = true, strong = true, double_hp = true, names = true,
  background = true, music = true, escape = true, gold = true, group = true }
local HOW = { [1] = "won", [2] = "lost", [3] = "draw", [4] = "time", [5] = "fled", [6] = "fled" }

local start_hooked = false
local ptb_after = nil -- run once after ＰＴＢ登録 in rq.battle.start's fight (double HP with strong)

local function hook_start()
  if start_hooked then return end
  start_hooked = true
  hook("ＰＴＢ登録", { after = function()
    local f = ptb_after
    ptb_after = nil
    if f then
      local ok, err = pcall(f)
      if not ok then logf("rq.battle.start: %s", tostring(err)) end
    end
  end })
end

-- The game's "２倍体力" (ボス戦闘処理): each enemy's HP twice, four times in world 3.
local function double_hp()
  local w3 = gint("◆ワールド") == 3
  for slot = 1, 5 do
    local max = call("Ｃ体力最大", slot, -1)
    if max ~= 0 then
      call("Ｃ体力最大", slot, max * (w3 and 4 or 2))
    end
    local hp = call("Ｃ体力", slot, -1)
    if hp ~= 0 then
      call("Ｃ体力", slot, hp * (w3 and 4 or 2))
    end
  end
end

-- The background: a number in the game's list, or its name.
local function background_no(v)
  if int(v) then
    local ok, name = pcall(call, "戦闘背景_データ名", v)
    if v >= 0 and ok and type(name) == "string" and name ~= "" then return v end
    error("rq.battle.start: no background " .. v, 3)
  end
  if type(v) == "string" and v ~= "" then
    local ok, no = pcall(call, "戦闘背景_番号", v)
    if ok and int(no) and no > 0 then return no end
    error("rq.battle.start: no background named " .. v, 3)
  end
  error("rq.battle.start: background must be a number or a name", 3)
end

-- The fight's own callbacks, registered once for every rq.battle.start: on_end records the result,
-- the fallen and the kills; the escape chance is written before every mod's on_start callback
-- (priv.on_start_first), after the game's own (Ｔ戦闘前初期化 sets 20, 0 for a boss).
local watch_hooked = false
local function hook_watch()
  if watch_hooked then return end
  watch_hooked = true
  priv.on_start_first(function()
    local w = fight_watch
    if w and w.escape then set("◆逃げる確率", w.escape) end
  end)
  battle.on_end(function(ctx)
    local w = fight_watch
    if not w then return end
    w.code = ctx.result
    local fallen, killed = {}, 0
    for _, iic in ipairs(ctx.allies or {}) do
      local ok, hp = pcall(call, "Ｃ体力", iic, -1)
      if ok and hp == 0 then fallen[#fallen + 1] = iic end
    end
    for _, iic in ipairs(ctx.enemies or {}) do
      local ok, hp = pcall(call, "Ｃ体力", iic, -1)
      if ok and hp == 0 then killed = killed + 1 end
    end
    w.fallen, w.killed = fallen, killed
  end)
end

local function check_bool(opts, k)
  local v = opts[k]
  if v ~= nil and type(v) ~= "boolean" then error("rq.battle.start: " .. k .. " must be true or false", 3) end
  return v == true
end

local function read_opts(opts)
  if type(opts) ~= "table" then error("rq.battle.start: expected a table of options", 3) end
  for k in pairs(opts) do
    if not OPTS[k] then error("rq.battle.start: no option " .. tostring(k), 3) end
  end
  local o = { m = {} }
  local group_exp
  if opts.group ~= nil then
    -- A mod's group (2.40.0): a line-up drawn as the game draws one, and the group's EXP.
    if opts.enemies ~= nil then error("rq.battle.start: enemies or group, not both", 3) end
    local m, exp = priv.group_pick(opts.group)
    if not m then error("rq.battle.start: group must be a group from rq.monster.group, not " .. tostring(opts.group), 3) end
    o.m, group_exp = m, exp
  else
    local en = opts.enemies
    if type(en) ~= "table" then error("rq.battle.start: enemies must be a list of monster numbers", 3) end
    if #en > 5 then error("rq.battle.start: at most 5 ranks", 3) end
    for i = 1, 5 do
      local v = en[i]
      if v == nil or v == false then v = 0 end
      -- A mod's monster by its text id (2.40.0).
      if type(v) == "string" then
        local no = priv.monster_no(v)
        if not no then error(string.format("rq.battle.start: rank %d: no monster with the id %s", i, v), 3) end
        v = no
      end
      if not int(v) then error(string.format("rq.battle.start: rank %d is not a monster number", i), 3) end
      o.m[i] = v
    end
  end
  o.exp = opts.exp == nil and (group_exp or 0) or opts.exp
  if not int(o.exp) then error("rq.battle.start: exp must be a whole number", 3) end
  o.boss = check_bool(opts, "boss")
  o.strong = check_bool(opts, "strong")
  o.double_hp = check_bool(opts, "double_hp")
  if (o.strong or o.double_hp) and not o.boss then
    error("rq.battle.start: " .. (o.strong and "strong" or "double_hp") .. " needs boss = true", 3)
  end
  if opts.names ~= nil then
    if type(opts.names) ~= "table" then error("rq.battle.start: names must be a table of texts by rank", 3) end
    o.names = {}
    for k, v in pairs(opts.names) do
      if not int(k) or k < 1 or k > 5 then error("rq.battle.start: names: a rank from 1 to 5, not " .. tostring(k), 3) end
      if type(v) ~= "string" then error(string.format("rq.battle.start: names[%d] must be a text", k), 3) end
      o.names[k] = v
    end
  end
  if opts.escape ~= nil then
    if not int(opts.escape) or opts.escape < 0 or opts.escape > 100 then
      error("rq.battle.start: escape must be a whole number from 0 to 100", 3)
    end
    o.escape = opts.escape
  end
  if opts.gold ~= nil then
    if not int(opts.gold) or opts.gold < 0 then error("rq.battle.start: gold must be a whole number, 0 or more", 3) end
    o.gold = opts.gold
  end
  return o
end

function battle.start(opts)
  need_game("rq.battle.start")
  local o = read_opts(opts)
  if priv.in_battle() then error("rq.battle.start: a battle is already running", 2) end
  local has_enemy = false
  for i = 1, 5 do
    local v = o.m[i]
    if v ~= 0 then
      if v < 0 or not priv.monster_exists(v) then error("rq.battle.start: no monster number " .. v, 2) end
      has_enemy = true
    end
  end
  if not has_enemy then error("rq.battle.start: no enemies", 2) end
  local has_ally = false
  for _, iic in ipairs(rq.party.get()) do
    if iic ~= 0 then has_ally = true end
  end
  -- An empty party never gets a turn and the fight never ends.
  if not has_ally then error("rq.battle.start: the party is empty", 2) end
  local bg
  if opts.background ~= nil then bg = background_no(opts.background) end
  local music
  if opts.music ~= nil then
    if not int(opts.music) and type(opts.music) ~= "string" then
      error("rq.battle.start: music must be a track number or a path", 2)
    end
    local ok, spec = pcall(priv.fight_music_check, opts.music)
    if not ok then error("rq.battle.start: music: " .. tostring(spec), 2) end
    music = spec
  end

  -- What the fight did, from on_end and the rewards.
  local watch = { gold = o.gold, escape = o.escape, paid = false, exp = 0, gold_paid = 0, kills = 0 }
  hook_watch()
  hook_reward()
  hook_start()
  if o.strong and o.double_hp then ptb_after = double_hp end
  if o.names then
    set("◆名前変更フラグ", 1)
    for i = 1, 5 do set("◆名前変更", i, o.names[i] or "") end
  end
  local bg0 = gint("◆戦闘背景番号")
  if bg then set("◆戦闘背景番号", bg) end
  if music then priv.fight_music(music) end

  local okb, bar0 = pcall(call, "システムバー_モード")
  local okm, map0 = pcall(call, "システムバー_マップ番号")
  local m = o.m
  priv.fight_mark(true)
  fight_watch = watch
  local ok, re
  if o.boss then
    local cond = o.strong and "強化" or o.double_hp and "２倍体力" or ""
    ok, re = pcall(call, "ボス戦闘処理", o.exp, m[1], m[2], m[3], m[4], m[5], cond)
  else
    ok, re = pcall(call, "特定戦闘処理", o.exp, m[1], m[2], m[3], m[4], m[5])
  end
  fight_watch = nil
  priv.fight_mark(false)
  ptb_after = nil
  if music then priv.fight_music(nil) end
  -- The title (rq: the game went to the title) unwinds through here: no call into the game, and
  -- the error goes on as it came, so the loader's unwind finds it.
  if not ok and tostring(re):find(TITLE_ERROR, 1, true) then error(re, 0) end
  if bg then set("◆戦闘背景番号", bg0) end
  if o.names then set("◆名前変更フラグ", 0) end

  -- On a quest's map the game puts its screen back after a fight. Anywhere else (the town, the
  -- Party screen, Quests, the castle) the fight's black layer stayed over the screen and its
  -- footer was gone: both are put back here (2.22.1).
  if okb and int(bar0) and bar0 >= 0 then
    local okn, now = pcall(call, "システムバー_モード")
    if okn and now ~= bar0 then
      pcall(call, "全画面色", NONE, NONE, NONE)
      pcall(call, "システムバー_作成", bar0, okm and map0 or 0, 1)
    end
  end
  if not ok then error(re, 0) end

  local first = re == 1 and "won" or re == 2 and "lost" or "other"
  local info = {
    code = watch.code or 0,
    how = HOW[watch.code] or "other",
    exp = watch.paid and watch.exp or 0,
    gold = watch.paid and watch.gold_paid or 0,
    kills = watch.paid and watch.kills or (watch.killed or 0),
    fallen = watch.fallen or {},
  }
  return first, info
end

-- The game's list of fight backgrounds (g_sBattleBackInfo.m_asList), in its order.
function battle.backgrounds()
  need_game("rq.battle.backgrounds")
  local out = {}
  local ok, n = pcall(rq.ain.global_len, "g_sBattleBackInfo", "m_asList")
  if not ok or not int(n) then return out end
  for i = 0, n - 1 do
    local ok1, no = pcall(g, "g_sBattleBackInfo", "m_asList", i, "m_nNo")
    local ok2, name = pcall(g, "g_sBattleBackInfo", "m_asList", i, "m_szName")
    if ok1 and ok2 and int(no) then out[#out + 1] = { no = no, name = name } end
  end
  return out
end
