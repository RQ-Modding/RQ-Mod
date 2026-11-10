-- rq.battle.on_turn, rq.battle.on_hit and rq.combat.on_magic (loader 2.1.0; lethal turn damage 2.3.0;
-- numbers apart, Persevere, special skills, the forecast 2.26.0).
--
-- The start of a turn: Ｔ戦闘_行動開始(unit) calls Ｔ行動開始(iic), which resets the turn's flags,
-- ends a stun (and returns), does the poison damage (never below 1 HP) and the regeneration,
-- then shows ◆毒ダメージ有り as a damage number and ◆体力回復あり as a heal, and passes the turn
-- when ◆行動パスです is 1. on_turn runs after Ｔ行動開始 returns, so also after its early
-- returns, and adds to those three globals, which the game then shows as its own.
--
-- A hit: Ｔ物理攻撃行動 and Ｔ魔法攻撃行動 (attacker, defender, skill) return the damage; their
-- log line is ◆戦闘ログ２, which the caller adds to the battle log after they return. A charge
-- turn returns early with ◆溜めになった set: no hit happens then.
--
-- Magic damage: Ｔ魔法攻撃行動 calls 最終ダメージ計算 once with 条件 "" (the forecast passes
-- "模擬"), with ◆行動タイプ 2, and then takes ◆戦闘／ＤＭ from the target's HP.
--
-- Lethal turn damage (2.3.0): ctx:damage with lethal may bring HP to 0. The game's own display
-- (戦闘表示_状態確認, inside Ｔ戦闘_行動開始) shows a damage at HP 0 as a death: the fade, the
-- card, and 瀕死処理, which sets state 3 through Ｃ状態変化 (so on_down runs). The turn passes.
-- After the display (an after hook on Ｔ戦闘_行動開始) the game's on-death skills run: 死亡計算
-- with the killer when there is a living one (Kalar's curse, Hanny Zippo, skill 438), else the
-- two that depend only on the dying one (Hanny Zippo, 438); a Hanny Zippo revive is shown with
-- the game's 蘇生. 死亡計算's other flags are cleared by Ｔスルー行動 right after, and its weapon
-- 156 extra action is put back, as the killer is not acting. After the action (an after hook on
-- 戦闘_行動) the game's 戦闘_終了確認 decides whether the fight is over, as it does after an attack.
--
-- Numbers apart (2.26.0): a damage or heal with apart is taken at once but kept out of the game's
-- one number. Around 戦闘表示_状態確認 for that unit, the HP is first put back up by the parts not
-- shown yet, so the game's own number shows with its own HP; then each part shows with the game's
-- 戦闘味方_ダメージ / 戦闘敵_ダメージ (dead when it brings the HP to 0) or 自動回復 and 回復, the
-- HP going down or up one part at a time.
--
-- Persevere (2.26.0) at a turn start follows the game's hit rule (戦闘関係:8872): when a lethal
-- damage would take the last HP and the HP is not 1, skill 247 saves half the time, once a fight
-- (◆踏ん張り使用済み), and skill 481 always; 1 HP is left.
--
-- The forecast: Ｔ戦闘予測 runs the damage steps with 条件 "模擬"; ◆行動タイプ then still holds
-- the last real action, so a spell is told by its action kind (Ｓ行動種類 2).

local rq = ...
local call, hook, global, set_global = rq.ain.call, rq.ain.hook, rq.ain.global, rq.ain.set_global

-- A list of callbacks. add returns a remover; run calls each one and logs an error, so one
-- mod's error does not stop the next mod's callback.
local function callbacks(name)
  local cells = {}
  local self = {}
  function self.add(fn)
    if type(fn) ~= "function" then
      error(name .. ": expected a function", 3)
    end
    local cell = { fn = fn }
    cells[#cells + 1] = cell
    return function()
      cell.fn = nil
    end
  end
  function self.each(f)
    for i = 1, #cells do
      local fn = cells[i].fn
      if fn then
        local ok, err = pcall(f, fn)
        if not ok then
          rq.log(name .. ": " .. tostring(err))
        end
      end
    end
  end
  return self
end

local function side_of(unit)
  if unit > 0 and call("戦闘番号_味方確認", unit) then
    return "ally"
  elseif unit > 0 and call("戦闘番号_敵確認", unit) then
    return "enemy"
  end
  return "summon"
end

local function whole(n, fn)
  if math.type(n) ~= "integer" or n < 0 then
    error(fn .. ": expected a whole number, 0 or more", 3)
  end
end

local function add_log(text, fn)
  if type(text) ~= "string" then
    error(fn .. ": expected a string", 3)
  end
  set_global("◆戦闘ログ２", global("◆戦闘ログ２") .. text)
end

-- on_turn ------------------------------------------------------------------------------

local turn_cbs = callbacks("rq.battle.on_turn")
local turn_hooked, turn_unit = false, 0
local dying, end_check = nil, false -- the unit a lethal damage downed this turn; fight-end check
-- This turn start's numbers apart, { d = n } or { h = n } in the order they came, for one unit.
local parts, parts_unit, parts_iic = {}, 0, 0

local Turn = {}
Turn.__index = Turn

local function opt_bool(opts, field, fn)
  local v = opts[field]
  if v ~= nil and type(v) ~= "boolean" then
    error(fn .. ": " .. field .. " must be a boolean", 3)
  end
  return v
end

-- Skill 247 (half the time, once a fight) or 481 saves from a damage that takes the last HP.
local function persevere(iic)
  local saved = false
  if call("ＣＳ有無", iic, 247, 1) ~= 0 and global("◆踏ん張り使用済み") == 0 and call("RAND", 2) == 1 then
    set_global("◆踏ん張り使用済み", 1)
    saved = true
  end
  if call("ＣＳ有無", iic, 481, 0) ~= 0 then
    saved = true
  end
  return saved
end

-- self._lethal, self._source, self._apart and self._persevere are the defaults rq.status sets
-- around a state's on_turn.
function Turn:damage(n, opts)
  whole(n, "ctx:damage")
  local lethal, source = self._lethal or false, self._source or 0
  local apart, save = self._apart or false, self._persevere or false
  if opts ~= nil then
    if type(opts) ~= "table" then
      error("ctx:damage: expected a table of options", 2)
    end
    local v = opt_bool(opts, "lethal", "ctx:damage")
    if v ~= nil then lethal = v end
    if opts.source ~= nil then
      whole(opts.source, "ctx:damage source")
      source = opts.source
    end
    v = opt_bool(opts, "apart", "ctx:damage")
    if v ~= nil then apart = v end
    v = opt_bool(opts, "persevere", "ctx:damage")
    if v ~= nil then save = v end
  end
  if self.side == "summon" then
    lethal = false -- the game shows no down for a summon
    apart = false  -- nor a number
  end
  if self.downed then
    return 0
  end
  local hp = call("Ｃ体力", self.iic, -1)
  local d = math.min(n, lethal and hp or hp - 1)
  if d <= 0 then
    return 0
  end
  if d == hp and save and hp ~= 1 and persevere(self.iic) then
    d = hp - 1
    add_log(" " .. call("Ｃ短縮名前", self.iic, "") .. " (Persevere activated)", "ctx:damage")
  end
  call("Ｃ体力", self.iic, hp - d)
  if apart then
    parts[#parts + 1] = { d = d }
  else
    set_global("◆毒ダメージ有り", global("◆毒ダメージ有り") + d)
  end
  if hp - d == 0 then
    self.downed = true
    self:skip()
    dying = { unit = self.unit, iic = self.iic, side = self.side, source = source }
  end
  return d
end

function Turn:heal(n, opts)
  whole(n, "ctx:heal")
  local apart = self._apart or false
  if opts ~= nil then
    if type(opts) ~= "table" then
      error("ctx:heal: expected a table of options", 2)
    end
    local v = opt_bool(opts, "apart", "ctx:heal")
    if v ~= nil then apart = v end
  end
  if self.side == "summon" then
    apart = false
  end
  local hp = call("Ｃ体力", self.iic, -1)
  if self.downed or hp <= 0 then
    return 0
  end
  local d = math.min(n, call("Ｃ体力最大", self.iic, -1) - hp)
  if d <= 0 then
    return 0
  end
  call("Ｃ体力", self.iic, hp + d)
  if apart then
    parts[#parts + 1] = { h = d }
  else
    set_global("◆体力回復あり", global("◆体力回復あり") + d)
  end
  return d
end

function Turn:skip()
  set_global("◆行動パスです", 1)
  self.passing = true
end

function Turn:log(text)
  add_log(text, "ctx:log")
end

local function battle_log(text)
  call("戦闘ログ_追加", text, 5, false)
end

-- The game's on-death skills for a unit a lethal damage downed, after the display.
local function after_death(p)
  local iic = p.iic
  if call("Ｃ状態変化", iic, -1) ~= 3 then
    -- The display did not down it (it should, at HP 0): do what the game's display does.
    if call("Ｃ体力", iic, -1) ~= 0 then
      return
    end
    rq.log("rq.battle: lethal damage: unit " .. p.unit .. " not downed by the display")
    call(p.side == "ally" and "戦闘味方_瀕死処理" or "戦闘敵_瀕死処理", p.unit, true)
  end
  end_check = true
  local killer = p.source
  local kunit = killer > 0 and killer ~= iic and call("戦闘番号_取得", killer) or 0
  if kunit > 0 and call("Ｃ状態変化", killer, -1) ~= 3 then
    local extra = global("◆行動回数回復")
    set_global("◆死の呪い発動", 0)
    set_global("◆死んだ直後に蘇生したよ", 0)
    call("死亡計算", killer, iic, 0)
    set_global("◆行動回数回復", extra)
    if global("◆死の呪い発動") == 1 then
      local ally = call("戦闘番号_味方確認", kunit)
      call(ally and "戦闘_味方エフェクト_呪い発動" or "戦闘_敵エフェクト_呪い発動", kunit, true)
      call("戦闘表示_状態確認", kunit, 0, 0)
      battle_log(" " .. call("Ｃ短縮名前", killer, "") .. " (Kalar Curse activated)")
    end
  else
    set_global("◆死んだ直後に蘇生したよ", 0)
    if call("ＣＳ有無", iic, 438, 0) ~= 0 then
      set_global("◆戦闘金持ち", 1)
    end
    if call("Ｃハニージッポ回数", iic, -1) == 0 and call("ＣＳ有無", iic, 302, 1) ~= 0 then
      call("Ｃハニージッポ回数", iic, 1)
      set_global("◆死んだ直後に蘇生したよ", 1)
    end
  end
  if global("◆死んだ直後に蘇生したよ") == 1 then
    call(p.side == "ally" and "戦闘味方_蘇生" or "戦闘敵_蘇生", p.unit, true)
    battle_log(" " .. call("Ｃ短縮名前", iic, "") .. " (Hanny Zippo)")
  end
end

-- Shows this turn start's numbers apart, after the game's own (see the top of this file).
local shown_parts, showing = nil, false

local function show_parts(list, unit, iic)
  local ally = call("戦闘番号_味方確認", unit)
  local hp = call("Ｃ体力", iic, -1)
  for _, p in ipairs(list) do
    if p.d then
      hp = hp - p.d
      call("Ｃ体力", iic, hp)
      if ally then
        call("戦闘味方_ダメージ", unit, p.d, 0, false, hp == 0, -1, false, false)
      else
        call("戦闘敵_ダメージ", unit, p.d, 0, false, hp == 0, false, -1, -1)
      end
    else
      hp = hp + p.h
      call("Ｃ体力", iic, hp)
      if ally then
        call("戦闘エフェクト_味方単体", unit, "自動回復", false)
        call("戦闘味方_回復", unit, p.h, -1)
      else
        call("戦闘エフェクト_敵単体", unit, "自動回復", false)
        call("戦闘敵_回復", unit, p.h, -1)
      end
    end
  end
  if hp > 0 then
    call(ally and "戦闘味方_瀕死設定" or "戦闘敵_瀕死設定", unit, call("戦闘データ_瀕死状態", unit))
  end
end

local function turn_install()
  turn_hooked = true
  hook("Ｔ戦闘_行動開始", {
    before = function(h)
      turn_unit = h.args[1] or 0
      dying = nil
      parts, shown_parts = {}, nil
    end,
    after = function()
      parts, shown_parts = {}, nil
      local p = dying
      dying = nil
      if p then
        after_death(p)
      end
    end,
  })
  hook("戦闘表示_状態確認", {
    before = function(h)
      if showing or #parts == 0 or h.args[1] ~= parts_unit then
        return
      end
      local hp = call("Ｃ体力", parts_iic, -1)
      for _, p in ipairs(parts) do
        hp = hp + (p.d or 0) - (p.h or 0)
      end
      call("Ｃ体力", parts_iic, hp)
      shown_parts, parts = parts, {}
    end,
    after = function()
      local list = shown_parts
      shown_parts = nil
      if not list then
        return
      end
      showing = true
      local ok, err = pcall(show_parts, list, parts_unit, parts_iic)
      showing = false
      if not ok then
        rq.log("rq.battle: numbers apart: " .. tostring(err))
      end
    end,
  })
  hook("Ｔ行動開始", { after = function(h)
    local iic = h.args[1]
    local unit = turn_unit
    if unit <= 0 or call("戦闘番号_ＩＩＣ", unit) ~= iic then
      unit = call("戦闘番号_取得", iic)
    end
    local ctx = setmetatable({
      iic = iic,
      unit = unit,
      side = side_of(unit),
      turn = global("◆戦闘ターン"),
      passing = global("◆行動パスです") ~= 0,
      downed = false,
    }, Turn)
    parts, parts_unit, parts_iic = {}, unit, iic
    turn_cbs.each(function(fn) fn(ctx) end)
  end })
  hook("戦闘_行動", { after = function(h)
    if not end_check then
      return
    end
    end_check = false
    if (h.result or 0) == 0 then
      local r = call("戦闘_終了確認")
      if r > 0 then
        h.result = r
      end
    end
  end })
end

function rq.battle.on_turn(fn)
  local remove = turn_cbs.add(fn)
  if not turn_hooked then
    turn_install()
  end
  return remove
end

-- on_hit -------------------------------------------------------------------------------

local hit_cbs = callbacks("rq.battle.on_hit")
local hit_hooked, in_counter = false, false

local Hit = {}
Hit.__index = Hit

function Hit:log(text)
  add_log(text, "ctx:log")
end

local function hit_after(kind)
  return function(h)
    if global("◆溜めになった") ~= 0 then
      return
    end
    local a = h.args
    local defender = a[2]
    local ctx = setmetatable({
      attacker = a[1],
      defender = defender,
      skill = a[3],
      kind = kind,
      damage = h.result or 0,
      missed = global("◆攻撃外れた") == 1,
      crit = global("◆クリティカル出た") == 1,
      downed = call("Ｃ状態変化", defender, -1) == 3,
      counter = in_counter,
    }, Hit)
    hit_cbs.each(function(fn) fn(ctx) end)
  end
end

-- A special skill (Ｔ特殊行動, 2.26.0) returns 0, so its damage is the HP the target lost.
local special_hp = {}

local function special_after(h)
  local before = table.remove(special_hp)
  if global("◆溜めになった") ~= 0 then
    return
  end
  local a = h.args
  local defender = a[2] or 0
  local hp = defender > 0 and call("Ｃ体力", defender, -1) or 0
  local ctx = setmetatable({
    attacker = a[1],
    defender = defender,
    skill = a[3],
    kind = "special",
    damage = math.max(0, (before or hp) - hp),
    missed = global("◆攻撃外れた") == 1,
    crit = false,
    downed = defender > 0 and call("Ｃ状態変化", defender, -1) == 3,
    counter = in_counter,
  }, Hit)
  hit_cbs.each(function(fn) fn(ctx) end)
end

local function hit_install()
  hit_hooked = true
  hook("戦闘_カウンター実行", {
    before = function() in_counter = true end,
    after = function() in_counter = false end,
  })
  hook("Ｔ物理攻撃行動", { after = hit_after("physical") })
  hook("Ｔ魔法攻撃行動", { after = hit_after("magic") })
  hook("Ｔ特殊行動", {
    before = function(h)
      local defender = h.args[2] or 0
      special_hp[#special_hp + 1] = defender > 0 and call("Ｃ体力", defender, -1) or 0
    end,
    after = special_after,
  })
end

function rq.battle.on_hit(fn)
  local remove = hit_cbs.add(fn)
  if not hit_hooked then
    hit_install()
  end
  return remove
end

-- on_magic -----------------------------------------------------------------------------

local magic_cbs = callbacks("rq.combat.on_magic")
local magic_hooked = false

local function magic_install()
  magic_hooked = true
  hook("最終ダメージ計算", { after = function(h)
    local a = h.args
    local forecast = a[4] ~= ""
    if forecast then
      if call("Ｓ行動種類", a[3], -1) ~= 2 then
        return
      end
    elseif global("◆行動タイプ") ~= 2 then
      return
    end
    local dm = global("◆戦闘／ＤＭ")
    if dm <= 0 then
      return
    end
    local attacker, defender = a[1], a[2]
    local ctx = {
      attacker = attacker,
      defender = defender,
      skill = a[3],
      damage = dm,
      magic = call("Ｃ魔法力", attacker, -1),
      race = call("Ｃ種族", defender, -1),
      defender_type = call("Ｃタイプ", defender, -1),
      forecast = forecast,
    }
    local pct = 100
    magic_cbs.each(function(fn)
      local r = fn(ctx)
      if r ~= nil then
        if math.type(r) ~= "integer" or r < 0 then
          error("expected a whole percent, 0 or more, or nil", 0)
        end
        pct = pct * r / 100
      end
    end)
    if pct ~= 100 then
      set_global("◆戦闘／ＤＭ", math.floor(dm * pct / 100))
    end
  end })
end

function rq.combat.on_magic(fn)
  local remove = magic_cbs.add(fn)
  if not magic_hooked then
    magic_install()
  end
  return remove
end

-- on_forecast (2.26.0) --------------------------------------------------------------------

local forecast_cbs = callbacks("rq.battle.on_forecast")
local forecast_hooked = false

local function forecast_install()
  forecast_hooked = true
  hook("Ｔ戦闘予測", { after = function(h)
    local a = h.args
    local text = h.result or ""
    local ctx = { attacker = a[1], defender = a[2], skill = a[3], text = text }
    local more = ""
    forecast_cbs.each(function(fn)
      local r = fn(ctx)
      if r ~= nil then
        if type(r) ~= "string" then
          error("expected a string or nil", 0)
        end
        more = more .. r
      end
    end)
    if more ~= "" then
      h.result = text .. more
    end
  end })
end

function rq.battle.on_forecast(fn)
  local remove = forecast_cbs.add(fn)
  if not forecast_hooked then
    forecast_install()
  end
  return remove
end
