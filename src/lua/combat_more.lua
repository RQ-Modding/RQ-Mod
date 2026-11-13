-- The damage limit, a physical hit's element, Athena's Studies and the weakness reveal (loader
-- 2.46.0, notes/plans/plan-new-mods.md in the workspace, step 2):
--   rq.combat.on_cap             the damage limit of a hit, in 最終ダメージ計算
--   rq.combat.on_element         the element of a physical hit, in 物理属性計算
--   rq.battle.on_study           the five Studies of Athena 2.0 after a fight
--   rq.battle.reveal_weakness    Weakness Search's effect and marks, for a mod
--
-- Built on public calls (rq.ain.call, global, set_global, hook, patch_int, method and stack) and
-- two private ones: priv.in_battle, and priv.frame_direct (modloader_hud.c: true in an rq.on_frame
-- callback itself, not in a game function it called), which tells a reveal to wait for a turn.
--   最終ダメージ計算(a, d, skill, 条件)   after the barriers and resistances the damage is multiplied,
--                           then held to the skill's limit: ▲ダメージ上限 = Ｓダメージ上限(skill, -1);
--                           skill 460 in world 3 has none on half its hits (RAND(2)); a limit not 0
--                           is raised by Damage Maximum, 倍率計算関数(limit, 100 +
--                           ＣＳ有無／ランク別効果(a, 289, 0)); damage over the limit is halved, over
--                           twice it quartered. 条件 "模擬" is the forecast.
--                           on_cap: the 289 there is patched to 0 (no skill, so the game adds
--                           nothing), and a hook on Ｓダメージ上限 called from it gives the limit
--                           after Damage Maximum and the callbacks.
--   物理属性計算(a, d, skill, 条件)   ◆攻撃属性: the weapon's element (Ｉ機能取得(weapon, 30)), the
--                           skill's (Ｓ属性) over it; then, with the weakness known
--                           (Ｃ弱点把握(d, -1) == 1) and no guard (FA[77] 0): on a basic attack
--                           (Ｓ基本技 1) Fire, Ice and Electric Arrows (254 to 256) take the weakness
--                           5, 4, 3; on skill 145, 331 to 335 take 5, 4, 3, 1, 2. Then a hit on the
--                           weakness: +50 to the final multiplier, the log, the big number; the
--                           effect's suffix; the knowledges 326 to 330.
--                           Ｃ弱点把握 is read twice there; the second read heads the skill 145
--                           block and always comes before the weakness check, so on_element answers
--                           at that read. A changed element is written to ◆攻撃属性 and the read
--                           gives 0, which skips the block; the rest runs on the new element.
--   Ｔ戦闘後の取得経験値算出(結果)   after the EXP: with Athena 2.0 (character 27) in rank 4 or 5 and the
--                           ally diagonally in front (PTA[1] or PTA[2]) standing, each Study on her
--                           bar (ＣＳ有無(athena, 336..340, 1)) adds to its counter when that ally's
--                           stat is higher: FA[176] attack +3, FA[177] accuracy +2, FA[178] speed
--                           +1, FA[179] HP +5, FA[180] critical +1; then Ｃ再計算, which sets her
--                           stats to 290, 150, 18, 860 and 0 plus the counters (accuracy stops at
--                           200, speed and critical at 100, in their setters). on_study reads the
--                           counters before and after: a counter that rose is a Study that fired.
--   戦闘_奇襲()             every fight (Ｒ８戦闘), after 戦闘_初期化 built the sprites and before the
--                           first turn. An enemy's weak point (T_BattleWeakPoint in
--                           g_sBattleMonster.m_asMonster[i], the enemy PTB[i + 1]) shows when its
--                           weakness was known as the sprite was built. Weakness Search (skill 200)
--                           sets Ｃ弱点把握(d, 1), plays 戦闘エフェクト_敵全体("全体・弱点サーチ",
--                           false) and blinks its targets' marks together (showWeakPointAll: 6
--                           toggles 100 ms apart, _W waits as its loop does). A weakness known in a
--                           fight is kept for the monster at the fight's end (■Ｍ／弱点把握).
local rq, priv = ...
local call, hook, g, set = rq.ain.call, rq.ain.hook, rq.ain.global, rq.ain.set_global
local battle, combat = rq.battle, rq.combat
local cb = priv.callbacks
local handlers, run, report = cb.handlers, cb.run, cb.report

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

-- The function that called `name`, from the AIN stack (the hooked function may be on it).
local function caller_of(name)
  local ok, st = pcall(rq.ain.stack, 6)
  if not ok or type(st) ~= "table" then return nil end
  for _, n in ipairs(st) do
    if n ~= name then return n end
  end
  return nil
end

-- A whole number from a callback, held to lo..hi; anything else raises (the caller reports it).
local function whole(r, lo, hi, what)
  if type(r) ~= "number" or r ~= r or r == math.huge or r == -math.huge then error(what, 0) end
  r = math.floor(r)
  if lo and r < lo then r = lo end
  if hi and r > hi then r = hi end
  return math.tointeger(r)
end

-- ---- rq.combat.on_cap: the damage limit ----

local DAMAGE_MAX = 289
local cap_list, add_cap = handlers("rq.combat.on_cap")
local cap_state = nil -- nil, "on" or "off"
local cap_hit = nil   -- the hit 最終ダメージ計算 works out now (outer: the one it interrupted)

-- The limit after Damage Maximum, as the game raises it, then the callbacks.
local function cap_decide(hit, base)
  local dm = 0
  local limit = base
  if base ~= 0 then
    local ok, v = pcall(call, "ＣＳ有無／ランク別効果", hit.attacker, DAMAGE_MAX, 0)
    dm = ok and int(v) and v or 0
    if dm ~= 0 then limit = call("倍率計算関数", base, 100 + dm) end
  end
  if #cap_list == 0 then return limit end
  local ctx = {
    attacker = hit.attacker, defender = hit.defender, skill = hit.skill, forecast = hit.forecast,
    magic = hit.magic, base = base, damage_max = dm, limit = limit,
  }
  for _, h in ipairs({ table.unpack(cap_list) }) do
    local ok, r = run(h, "rq.combat.on_cap", ctx)
    if ok and r ~= nil then
      local ok2, v = pcall(whole, r, 0, nil, "expected a limit, 0 for none, or nil")
      if ok2 then ctx.limit = v else report(h, "rq.combat.on_cap", v) end
    end
  end
  return ctx.limit
end

local function cap_install()
  if cap_state then return cap_state == "on" end
  local n = rq.ain.patch_int("最終ダメージ計算", DAMAGE_MAX, 0)
  if n == 0 then
    cap_state = "off"
    logf("rq.combat.on_cap: Damage Maximum's read in 最終ダメージ計算 not found; callbacks will not run")
    return false
  end
  if n ~= 1 then logf("rq.combat.on_cap: 289 in 最終ダメージ計算 found %d times", n) end
  cap_state = "on"
  hook("最終ダメージ計算", {
    before = function(h)
      local a = h.args
      local magic = false
      if int(a[3]) then
        local ok, k = pcall(call, "Ｓ行動種類", a[3], -1)
        magic = ok and k == 2
      end
      cap_hit = { attacker = a[1], defender = a[2], skill = a[3], forecast = a[4] == "模擬",
        magic = magic, outer = cap_hit }
    end,
    after = function()
      if cap_hit then cap_hit = cap_hit.outer end
    end,
  })
  hook("Ｓダメージ上限", { after = function(h)
    local hit = cap_hit
    if not hit or hit.done or h.args[2] ~= -1 or h.args[1] ~= hit.skill or not int(h.result) then return end
    if caller_of("Ｓダメージ上限") ~= "最終ダメージ計算" then return end
    hit.done = true
    local base = h.result
    local ok, v = pcall(cap_decide, hit, base)
    if ok then
      h.result = v
    else
      logf("rq.combat.on_cap: %s", tostring(v))
      -- Damage Maximum's read is patched out: raise the limit as the game would.
      local ok2, dm = pcall(call, "ＣＳ有無／ランク別効果", hit.attacker, DAMAGE_MAX, 0)
      if base ~= 0 and ok2 and int(dm) and dm ~= 0 then
        local ok3, r = pcall(call, "倍率計算関数", base, 100 + dm)
        if ok3 and int(r) then h.result = r end
      end
    end
  end })
  return true
end

function combat.on_cap(fn)
  local remove = add_cap(fn)
  cap_install()
  return remove
end

-- ---- rq.combat.on_element: a physical hit's element ----

local SKILL_145 = { { 331, 5 }, { 332, 4 }, { 333, 3 }, { 334, 1 }, { 335, 2 } } -- the game's order
local el_list, add_el = handlers("rq.combat.on_element")
local el_hooked = false
local el_hit = nil

local function el_decide(hit, known)
  local a, d, skill = hit.attacker, hit.defender, hit.skill
  local guarded = gint("FA", 77) ~= 0
  local weak = call("Ｃ弱点属性", d, -1)
  local game = gint("◆攻撃属性")
  if known and skill == 145 and not guarded then
    for _, s in ipairs(SKILL_145) do
      if call("ＣＳ有無", a, s[1], 0) ~= 0 and weak == s[2] then game = s[2] end
    end
  end
  local okw, weapon = pcall(function() return call("Ｉ機能取得", call("Ｃ武器", a, -1), 30) end)
  local ctx = {
    attacker = a, defender = d, skill = skill, forecast = hit.forecast,
    element = game, weapon = okw and int(weapon) and weapon or 0, weakness = weak, known = known,
    guarded = guarded, basic = call("Ｓ基本技", skill, -1) == 1,
  }
  for _, h in ipairs({ table.unpack(el_list) }) do
    local ok, r = run(h, "rq.combat.on_element", ctx)
    if ok and r ~= nil then
      local ok2, v = pcall(whole, r, nil, nil, "expected an element 0 to 5, or nil")
      if ok2 and (v < 0 or v > 5) then ok2, v = false, "expected an element 0 to 5, or nil" end
      if ok2 then ctx.element = v else report(h, "rq.combat.on_element", v) end
    end
  end
  return ctx.element, game
end

local function el_install()
  if el_hooked then return end
  el_hooked = true
  hook("物理属性計算", {
    before = function(h)
      local a = h.args
      el_hit = { attacker = a[1], defender = a[2], skill = a[3], forecast = a[4] == "模擬", reads = 0,
        outer = el_hit }
    end,
    after = function()
      if el_hit then el_hit = el_hit.outer end
    end,
  })
  hook("Ｃ弱点把握", { after = function(h)
    local hit = el_hit
    if not hit or h.args[2] ~= -1 or h.args[1] ~= hit.defender then return end
    if caller_of("Ｃ弱点把握") ~= "物理属性計算" then return end
    hit.reads = hit.reads + 1
    if hit.reads ~= 2 or #el_list == 0 then return end
    local ok, el, game = pcall(el_decide, hit, h.result == 1)
    if not ok then
      logf("rq.combat.on_element: %s", tostring(el))
      return
    end
    if el ~= game then
      set("◆攻撃属性", el)
      h.result = 0 -- skips the skill 145 block; the rest of 物理属性計算 runs on the new element
    end
  end })
end

function combat.on_element(fn)
  local remove = add_el(fn)
  el_install()
  return remove
end

-- ---- rq.battle.on_study: Athena's Studies ----

local ATHENA = 27
local STUDIES = { -- the game's order
  { stat = "attack", skill = 336, fa = 176 },
  { stat = "accuracy", skill = 337, fa = 177 },
  { stat = "speed", skill = 338, fa = 178 },
  { stat = "hp", skill = 339, fa = 179 },
  { stat = "critical", skill = 340, fa = 180 },
}
local study_list, add_study = handlers("rq.battle.on_study")
local study_hooked = false
local study_was = nil

local function study_after()
  local was = study_was
  study_was = nil
  if not was or #study_list == 0 then return end
  local athena = call("Ｃ番号取得", ATHENA)
  local rank = call("ＰＴＡ有無／固有", ATHENA)
  local ally = (rank == 4 and gint("PTA", 1)) or (rank == 5 and gint("PTA", 2)) or 0
  local changed = false
  for i, s in ipairs(STUDIES) do
    local game = gint("FA", s.fa) - was[i]
    if game > 0 then
      local ctx = { iic = athena, ally = ally, stat = s.stat, skill = s.skill, base = game, gain = game }
      for _, h in ipairs({ table.unpack(study_list) }) do
        local ok, r = run(h, "rq.battle.on_study", ctx)
        if ok and r ~= nil then
          local ok2, v = pcall(whole, r, 0, nil, "expected a gain, 0 or more, or nil")
          if ok2 then ctx.gain = v else report(h, "rq.battle.on_study", v) end
        end
      end
      if ctx.gain ~= game then
        set("FA", s.fa, was[i] + ctx.gain)
        changed = true
      end
    end
  end
  if changed and athena > 0 then call("Ｃ再計算", athena) end
end

local function study_install()
  if study_hooked then return end
  study_hooked = true
  hook("Ｔ戦闘後の取得経験値算出", {
    before = function()
      study_was = nil
      if #study_list == 0 then return end
      local ok, rank = pcall(call, "ＰＴＡ有無／固有", ATHENA)
      if not ok or rank == 0 then return end
      local t = {}
      for i, s in ipairs(STUDIES) do t[i] = gint("FA", s.fa) end
      study_was = t
    end,
    after = function()
      local ok, err = pcall(study_after)
      if not ok then logf("rq.battle.on_study: %s", tostring(err)) end
    end,
  })
end

function battle.on_study(fn)
  local remove = add_study(fn)
  study_install()
  return remove
end

-- ---- rq.battle.reveal_weakness ----

local reveal_hooked = false
local shown = false    -- the fight has shown (戦闘_奇襲 ran) and has not ended
local waiting = {}     -- reveals asked for before the fight showed, or from a frame callback

-- The enemies' sprites: m_asMonster[i] is the enemy PTB[i + 1].
local function weak_point(i)
  return { "g_sBattleMonster", "m_asMonster", i, "m_sWeakPoint" }
end

local function play(req)
  local rows = {}
  for i = 1, 5 do
    local e = gint("PTB", i)
    if e > 0 and (not req.only or req.only[e]) then
      local okd, dead = pcall(rq.ain.method, { "g_sBattleMonster", "m_asMonster", i - 1 }, "T_BattleMonster@isDead")
      if okd and not dead and call("Ｃ状態変化", e, -1) ~= 3 then
        call("Ｃ弱点把握", e, 1)
        rows[#rows + 1] = i - 1
      end
    end
  end
  if #rows == 0 then return 0 end
  if req.log then pcall(battle.log, req.log) end
  if req.effect then call("戦闘エフェクト_敵全体", "全体・弱点サーチ", false) end
  for _, i in ipairs(rows) do
    rq.ain.method(weak_point(i), "T_BattleWeakPoint@setShowWeakPoint", true)
  end
  if req.blink then
    local show = true
    for _ = 1, 6 do
      show = not show
      for _, i in ipairs(rows) do rq.ain.method(weak_point(i), "T_BattleWeakPoint@setShow", show) end
      call("_W", 100)
    end
  end
  for _, i in ipairs(rows) do
    rq.ain.method(weak_point(i), "T_BattleWeakPoint@setShow", true)
    pcall(call, "戦闘表示_状態確認", call("戦闘番号_敵", i), 0, 0)
  end
  call("_W", 0)
  return #rows
end

local function flush()
  local list = waiting
  waiting = {}
  for _, req in ipairs(list) do
    local ok, err = pcall(play, req)
    if not ok then logf("rq.battle.reveal_weakness (%s): %s", req.mod, tostring(err)) end
  end
end

local function reveal_install()
  if reveal_hooked then return end
  reveal_hooked = true
  hook("Ｔ戦闘前初期化", { before = function()
    shown = false
    waiting = {}
  end })
  hook("戦闘_奇襲", { after = function()
    shown = true
    flush()
  end })
  -- A reveal asked for from a frame callback while the fight runs plays at the next turn's start.
  hook("Ｔ行動開始", { after = function()
    if #waiting > 0 then flush() end
  end })
  hook("戦闘データ_削除", { before = function()
    shown = false
    waiting = {}
  end })
end

function battle.reveal_weakness(enemies, opts)
  if enemies ~= nil and type(enemies) ~= "table" then
    error("rq.battle.reveal_weakness: enemies is a list of enemy slots, or nil for all", 2)
  end
  if opts ~= nil and type(opts) ~= "table" then error("rq.battle.reveal_weakness: opts is a table", 2) end
  opts = opts or {}
  if opts.log ~= nil and type(opts.log) ~= "string" then error("rq.battle.reveal_weakness: log is a string", 2) end
  if not priv.in_battle() then error("rq.battle.reveal_weakness: not in a fight", 2) end
  local only
  if enemies then
    only = {}
    for _, e in ipairs(enemies) do
      if not int(e) or e < 1 or e >= 20 then
        error("rq.battle.reveal_weakness: an enemy is a slot from 1 to 19", 2)
      end
      only[e] = true
    end
  end
  reveal_install()
  local id = "?"
  pcall(function() id = rq.mod.id() or "?" end)
  local req = { only = only, effect = opts.effect ~= false, blink = opts.effect ~= false,
    log = opts.log ~= "" and opts.log or nil, mod = id }
  -- Before the fight shows, or from an rq.on_frame callback itself (a timer, the HUD): wait for a
  -- moment of the game's own flow. Inside it (a turn or hit callback, also under a fight that
  -- rq.battle.start opened from a frame callback), play now.
  if not shown or priv.frame_direct() then
    waiting[#waiting + 1] = req
    return nil
  end
  local ok, n = pcall(play, req)
  if not ok then error("rq.battle.reveal_weakness: " .. tostring(n), 2) end
  return n
end
