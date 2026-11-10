-- rq.status: battle states made by mods, one registry every mod shares (loader 2.2.0;
-- lethal states 2.3.0; the game's cures and dispels, the AUTO button, immunity, numbers apart,
-- Persevere and states that last the quest 2.26.0).
--
-- Built only on public calls: rq.battle.on_turn / on_start / on_end / on_down / on_support,
-- rq.combat on_physical / on_accuracy / on_magic, rq.battle.set_buff and states,
-- rq.quests.on_end / on_take / current, and rq.ain.hook on these game functions:
--   Ｔ戦闘前初期化   the start of a fight, before on_start: the registry is emptied here (but for
--                    the allies' states that last the quest), so a state a mod adds in its own
--                    on_start stays, whatever the callback order.
--   戦闘_行動終了    the end of every actor's turn, a passed one too: turns count down here.
--   ダメージ後計算   after a hit that did damage: the game's buff dispels (戦闘関係:9002-9038).
--   Ｔ魔法攻撃行動   spell 183, which takes protection, else both ▲ (戦闘関係:6465).
--   Ｔ補助行動       skill 498, which heals, else cures ▼ attack, else ▼ defense (戦闘関係:7056).
--   ＰＴＡ異常状態キャラ取得   the AUTO button's target for Status Restore (227).
-- The game's cures are skill numbers checked in Ｔ補助行動: 227, 237 and 220 clear ▼ and every
-- state ("all"); 218 and 219 cure poison only ("poison"). Skill 475 at the start of a turn
-- (Ｔ行動開始, 戦闘関係:5436) tries poison, ▼ attack and ▼ defense at 50% each and stops at the
-- first it cures; when it cured none, a mod state of "all" gets the same roll, one at most.
local rq = ...
local call, hook = rq.ain.call, rq.ain.hook
local global, set_global = rq.ain.global, rq.ain.set_global

local status = {}
rq.status = status

local CURES = { [227] = "all", [237] = "all", [220] = "all", [218] = "poison", [219] = "poison" }
local STACKS = { refresh = true, add = true, keep = true }
local LASTS = { fight = true, quest = true }
local IMMUNE_LISTS = { races = true, types = true, charas = true }
local IMMUNE_FLAGS = { boss = true, barrier = true }
local ALLY = 20                 -- allies are roster slots 20 to 200; enemies 1 to 5

local defs, ndefs = {}, 0       -- id -> def
local by_iic = {}               -- iic -> list of instances, in the order they came
local immune_by_no = {}         -- character number -> { ids = {}, tags = {} }
local fighting, actor = false, 0
local add_cbs, remove_cbs = {}, {}

local function logf(fmt, ...)
  rq.log("rq.status: " .. string.format(fmt, ...))
end

-- Calls a definition's callback; an error is logged with the mod and the id.
local function run(def, what, fn, ...)
  local r = table.pack(pcall(fn, ...))
  if not r[1] then
    logf("%s %s %s: %s", def.mod, def.id, what, tostring(r[2]))
    return
  end
  return table.unpack(r, 2, r.n)
end

local function each_cb(cbs, what, ...)
  for i = 1, #cbs do
    local fn = cbs[i].fn
    if fn then
      local ok, err = pcall(fn, ...)
      if not ok then logf("%s: %s", what, tostring(err)) end
    end
  end
end

-- var: "◆戦闘ログ２" (the default), or "◆ログ２追加" inside a hit, which the game adds to the hit's
-- line once it has written it.
local function add_log(text, var)
  var = var or "◆戦闘ログ２"
  set_global(var, global(var) .. text)
end

-- The icon ------------------------------------------------------------------------------

local function icon_text(def, s)
  if def.text then
    local t = run(def, "text", def.text, s)
    return t ~= nil and tostring(t):sub(1, 15) or ""
  end
  if s.turns then return tostring(s.turns) end
  if s.stacks > 1 then return tostring(s.stacks) end
  return ""
end

-- Shows the icon; false when the card is not there yet (set_buff before the bearer stood on one).
local function show(s)
  local def = defs[s.id]
  if not def.icon or not fighting then return true end
  local b = {}
  for k, v in pairs(def.icon) do b[k] = v end
  b.text = icon_text(def, s)
  local ok, r = pcall(rq.battle.set_buff, s.iic, def.key, b)
  if not ok then
    logf("%s icon: %s", s.id, tostring(r))
    return true
  end
  return r ~= false
end

local function hide(s)
  local def = defs[s.id]
  if def.icon and fighting then pcall(rq.battle.set_buff, s.iic, def.key, nil) end
end

-- Instances -----------------------------------------------------------------------------

local function find(iic, id)
  local list = by_iic[iic]
  if not list then return nil end
  for i, s in ipairs(list) do
    if s.id == id then return s, i end
  end
end

local function drop(s, reason)
  local _, i = find(s.iic, s.id)
  if not i then return false end
  table.remove(by_iic[s.iic], i)
  if #by_iic[s.iic] == 0 then by_iic[s.iic] = nil end
  hide(s)
  local def = defs[s.id]
  if def.on_remove then run(def, "on_remove", def.on_remove, s, reason) end
  each_cb(remove_cbs, "on_remove", s, reason)
  return true
end

-- Removes the instances of iic that pass test(s, def), oldest first; returns them.
local function drop_where(iic, test, reason)
  local list = by_iic[iic]
  if not list then return {} end
  local snapshot, gone = { table.unpack(list) }, {}
  for _, s in ipairs(snapshot) do
    if test(s, defs[s.id]) and drop(s, reason) then gone[#gone + 1] = s end
  end
  return gone
end

local function each_on(iic, fn)
  local list = by_iic[iic]
  if not list then return end
  for _, s in ipairs({ table.unpack(list) }) do fn(s, defs[s.id]) end
end

local function kept_past_fight(s, def)
  return def.lasts == "quest" and s.iic >= ALLY
end

-- States kept from an earlier fight show their icon once the bearer's card is there.
local function show_waiting()
  for _, list in pairs(by_iic) do
    for _, s in ipairs(list) do
      if s._wait and show(s) then s._wait = nil end
    end
  end
end

-- Immunity ------------------------------------------------------------------------------

local function is_immune(iic, def)
  local im = def.immune
  local no = call("Ｃ番号", iic, -1)
  if im then
    if im.boss and call("Ｃボス", iic, -1) == 1 then return true end
    if im.barrier and call("Ｃバリア", iic, -1) ~= 0 then return true end
    if im.races and im.races[call("Ｃ種族", iic, -1)] then return true end
    if im.types and im.types[call("Ｃタイプ", iic, -1)] then return true end
    if im.charas and im.charas[no] then return true end
  end
  local by = immune_by_no[no]
  if by then
    if by.ids[def.id] then return true end
    for t in pairs(def.tags) do
      if by.tags[t] then return true end
    end
  end
  return false
end

-- The game's cures and dispels ----------------------------------------------------------

local function short(iic)
  return call("Ｃ短縮名前", iic, "")
end

-- Skill 475, after the bearer's states ticked: one state of "all" at 50%, when the game's own
-- roll cured nothing and the game reached its roll (not on a stun's end or a petrified turn).
local function auto_cure(ctx)
  local iic = ctx.iic
  if ctx.downed or not by_iic[iic] or call("ＣＳ有無", iic, 475, 0) == 0 then return end
  if global("◆状態回復あり") ~= 0 or global("◆ぴより治った") ~= 0 then return end
  if call("Ｃ番号", iic, -1) == 51 and global("◆カロリア硬質化中") == 1 then return end
  for _, s in ipairs({ table.unpack(by_iic[iic]) }) do
    if defs[s.id].cured.all and call("RAND", 2) == 1 then
      set_global("◆状態回復あり", 1)
      drop(s, "cured")
      return
    end
  end
end

-- The buff dispels in ダメージ後計算. 515's roll is the game's where it can be seen (the target
-- had a ▲ before); else the loader rolls its own 50%.
local dispel_seen = {}

local function had_up(iic)
  local st = rq.battle.states(iic)
  return st ~= nil and (st.atk_up or st.def_up)
end

local function dispel(iic, loud, var)
  local gone = drop_where(iic, function(_, def) return def.cured.dispel end, "cured")
  if loud then
    for _, s in ipairs(gone) do add_log(" (" .. defs[s.id].name .. " canceled)", var) end
  end
end

local function dispel_after(h)
  local up_before = table.remove(dispel_seen)
  local a, d, sk = h.args[1], h.args[2], h.args[3]
  if not d or not by_iic[d] then return end
  local quiet = call("Ｓ機能", sk, -1) == 89
  local loud = sk == 519 or sk == 491
    or (call("Ｃ職業", a, -1) == 3 and call("ＣＳ有無", a, 257, 0) ~= 0 and call("Ｓ基本技", sk, -1) == 1)
  if sk == 515 and not loud then
    if up_before then
      loud = not had_up(d)
    else
      loud = call("RAND", 2) == 1
    end
  end
  if loud or quiet then dispel(d, loud, "◆ログ２追加") end
end

-- Spell 183: protection first; without it, the good states go as both ▲ do.
local spell_seen = {}

local function spell_before(h)
  local d = h.args[2] or 0
  spell_seen[#spell_seen + 1] = h.args[3] == 183 and d > 0 and call("Ｃ守護率", d, -1) == 0
end

local function spell_after(h)
  local take = table.remove(spell_seen)
  if take and global("◆溜めになった") == 0 and by_iic[h.args[2]] then
    dispel(h.args[2], true)
  end
end

-- Skill 498: when it heals no HP and cures no ▼, the oldest state of "all" goes.
local support_seen = {}

local function support_before(h)
  local t = h.args[2] or 0
  local idle = false
  if h.args[3] == 498 and t > 0 and by_iic[t] and call("Ｃ状態変化", t, -1) ~= 3 then
    local st = rq.battle.states(t)
    idle = call("Ｃ体力", t, -1) >= call("Ｃ体力最大", t, -1) and not (st and (st.atk_down or st.def_down))
  end
  support_seen[#support_seen + 1] = idle and t or false
end

local function support_after()
  local t = table.remove(support_seen)
  if not t then return end
  for _, s in ipairs({ table.unpack(by_iic[t] or {}) }) do
    if defs[s.id].cured.all then
      drop(s, "cured")
      add_log(" " .. short(t) .. " recovered from " .. defs[s.id].name .. ".")
      return
    end
  end
end

-- The AUTO button's target for 227: the game's scan, a mod state that 227 cures counted too.
local function auto_target(h)
  local last = 0
  for i = 1, 5 do
    local iic = global("PTA", i)
    if iic ~= 0 and call("Ｃ状態変化", iic, -1) ~= 3 then
      local st = rq.battle.states(iic)
      local bad = st ~= nil and (st.poison or st.stun or st.atk_down or st.def_down)
      if not bad then
        for _, s in ipairs(by_iic[iic] or {}) do
          local cured = defs[s.id].cured
          if cured.all or cured[227] then
            bad = true
            break
          end
        end
      end
      if bad then last = iic end
    end
  end
  h.result = last
end

-- The game's events ---------------------------------------------------------------------

local hooked, combat_hooked, magic_hooked, quest_hooked = false, false, false, false

-- A state's on_turn gets ctx with its own defaults: lethal from the definition, the source as
-- the killer, its numbers apart, and Persevere from the definition. Once the bearer is down, the
-- states after it do not tick.
local function on_turn(ctx)
  actor = ctx.iic
  show_waiting()
  each_on(ctx.iic, function(s, def)
    if def.on_turn and not ctx.downed then
      ctx._lethal, ctx._source, ctx._apart, ctx._persevere = def.lethal, s.source, true, def.persevere
      run(def, "on_turn", def.on_turn, s, ctx)
      ctx._lethal, ctx._source, ctx._apart, ctx._persevere = nil, nil, nil, nil
    end
  end)
  auto_cure(ctx)
end

local function on_turn_end(iic)
  actor = 0
  each_on(iic, function(s)
    if s._fresh then
      s._fresh = nil
    elseif s.turns then
      s.turns = s.turns - 1
      if s.turns <= 0 then
        drop(s, "expired")
      else
        show(s)
      end
    end
  end)
end

local function install()
  hooked = true
  hook("Ｔ戦闘前初期化", { before = function()
    -- A quest state from another quest (one the game never ended, such as after a load) goes
    -- with "quest_end"; anything else left from an unfinished fight goes quietly.
    local quest = rq.quests.current()
    for iic in pairs(by_iic) do
      drop_where(iic, function(s, def)
        return kept_past_fight(s, def) and (s._quest == 0 or s._quest ~= quest)
      end, "quest_end")
    end
    local kept = {}
    for iic, list in pairs(by_iic) do
      for _, s in ipairs(list) do
        if kept_past_fight(s, defs[s.id]) then
          s._fresh, s._wait = nil, true
          kept[iic] = kept[iic] or {}
          table.insert(kept[iic], s)
        end
      end
    end
    by_iic, fighting, actor = kept, true, 0
  end })
  hook("戦闘_行動終了", { before = function(h)
    local unit = h.args[1] or 0
    if unit > 0 then on_turn_end(call("戦闘番号_ＩＩＣ", unit)) end
  end })
  hook("ダメージ後計算", {
    before = function(h)
      local d = h.args[2] or 0
      dispel_seen[#dispel_seen + 1] = d > 0 and had_up(d)
    end,
    after = dispel_after,
  })
  hook("Ｔ魔法攻撃行動", { before = spell_before, after = spell_after })
  hook("Ｔ補助行動", { before = support_before, after = support_after })
  hook("ＰＴＡ異常状態キャラ取得", { after = auto_target })
  rq.battle.on_turn(on_turn)
  rq.battle.on_start(show_waiting)
  rq.battle.on_end(function()
    for iic in pairs(by_iic) do
      drop_where(iic, function(s, def) return not kept_past_fight(s, def) end, "fight_end")
    end
    fighting, actor = false, 0
  end)
  rq.battle.on_down(function(ctx)
    drop_where(ctx.iic, function(_, def) return not def.keep_on_down end, "down")
  end)
  rq.battle.on_support(function(ctx)
    local class = CURES[ctx.skill]
    drop_where(ctx.target, function(_, def)
      return (class and def.cured[class]) or def.cured[ctx.skill]
    end, "cured")
  end)
end

-- The quest's end, however it ends, and Take on the quest list (a new quest; states left from a
-- quest the game did not end, such as after a load, go there).
local function install_quest()
  quest_hooked = true
  local function quest_end()
    for iic in pairs(by_iic) do
      drop_where(iic, function(_, def) return def.lasts == "quest" end, "quest_end")
    end
  end
  rq.quests.on_end(quest_end)
  rq.quests.on_take(function() quest_end() end)
end

-- Asks every state on the attacker and the defender, as one combat callback.
local function ask_both(ctx, field, fn)
  each_on(ctx.attacker, function(s, def)
    if def[field] then fn(run(def, field, def[field], s, "attacker", ctx)) end
  end)
  each_on(ctx.defender, function(s, def)
    if def[field] then fn(run(def, field, def[field], s, "defender", ctx)) end
  end)
end

local function install_combat()
  combat_hooked = true
  rq.combat.install()
  rq.combat.on_physical(function(ctx)
    local atk, pct = nil, 100
    ask_both(ctx, "physical", function(a, p)
      if math.type(a) == "integer" then atk = a ctx.attack = a end
      if math.type(p) == "integer" then pct = pct * p / 100 end
    end)
    if atk == nil and pct == 100 then return nil end
    return atk, math.floor(pct)
  end)
  rq.combat.on_accuracy(function(ctx)
    local sum = 0
    ask_both(ctx, "accuracy", function(n)
      if math.type(n) == "integer" then sum = sum + n end
    end)
    return sum
  end)
end

local function install_magic()
  magic_hooked = true
  rq.combat.on_magic(function(ctx)
    local pct = 100
    ask_both(ctx, "magic", function(p)
      if math.type(p) == "integer" then pct = pct * p / 100 end
    end)
    if pct == 100 then return nil end
    return math.max(0, math.floor(pct))
  end)
end

-- The calls -----------------------------------------------------------------------------

local function check_id(id, fn)
  if type(id) ~= "string" or not id:match("^[%w_%-]+:[%w_%-]+$") then
    error(fn .. ": an id is \"<mod>:<name>\", got " .. tostring(id), 3)
  end
end

local function known(id, fn)
  check_id(id, fn)
  local def = defs[id]
  if not def then error(fn .. ": no state " .. id .. " is defined", 3) end
  return def
end

local function optional(t, field, kind, fn)
  local v = t[field]
  if v ~= nil and type(v) ~= kind then
    error(fn .. ": " .. field .. " must be a " .. kind, 3)
  end
  return v
end

-- immune = { boss = true, barrier = true, races = {..}, types = {..}, charas = {..} } -> sets.
local function read_immune(t)
  local fn = "rq.status.define"
  local out, list = {}, {}
  for k, v in pairs(t) do
    if IMMUNE_FLAGS[k] then
      if type(v) ~= "boolean" then error(fn .. ": immune." .. k .. " must be a boolean", 3) end
      out[k] = v or nil
      list[k] = v
    elseif IMMUNE_LISTS[k] then
      if type(v) ~= "table" then error(fn .. ": immune." .. k .. " must be a list of numbers", 3) end
      local set, copy = {}, {}
      for _, n in ipairs(v) do
        if math.type(n) ~= "integer" then error(fn .. ": immune." .. k .. " holds whole numbers", 3) end
        set[n] = true
        copy[#copy + 1] = n
      end
      out[k] = set
      list[k] = copy
    else
      error(fn .. ": immune takes boss, barrier, races, types and charas, not " .. tostring(k), 3)
    end
  end
  return out, list
end

function status.define(id, def)
  check_id(id, "rq.status.define")
  if type(def) ~= "table" then error("rq.status.define: expected a definition table", 2) end
  if defs[id] then
    error("rq.status.define: " .. id .. " is already defined by " .. defs[id].mod, 2)
  end
  local kind = def.kind or "bad"
  if kind ~= "bad" and kind ~= "good" then
    error("rq.status.define: kind is \"bad\" or \"good\"", 2)
  end
  local turns = def.turns
  if turns ~= nil and (math.type(turns) ~= "integer" or turns < 1) then
    error("rq.status.define: turns is a whole number, 1 or more, or nil", 2)
  end
  local stack = def.stack or "refresh"
  if not STACKS[stack] and not (math.type(stack) == "integer" and stack >= 1) then
    error("rq.status.define: stack is \"refresh\", \"add\", \"keep\" or a whole number, 1 or more", 2)
  end
  local lasts = def.lasts or "fight"
  if not LASTS[lasts] then
    error("rq.status.define: lasts is \"fight\" or \"quest\"", 2)
  end
  for _, f in ipairs({ "on_turn", "on_add", "on_remove", "physical", "magic", "accuracy", "text" }) do
    optional(def, f, "function", "rq.status.define")
  end
  optional(def, "icon", "table", "rq.status.define")
  optional(def, "lethal", "boolean", "rq.status.define")
  optional(def, "persevere", "boolean", "rq.status.define")
  optional(def, "tags", "table", "rq.status.define")
  optional(def, "cured_by", "table", "rq.status.define")
  optional(def, "immune", "table", "rq.status.define")
  optional(def, "desc", "string", "rq.status.define")
  local cured_by = def.cured_by or (kind == "bad" and { "all" } or { "dispel" })
  local cured, tags = {}, {}
  for _, c in ipairs(cured_by) do
    if c ~= "all" and c ~= "poison" and c ~= "dispel" and math.type(c) ~= "integer" then
      error("rq.status.define: cured_by holds \"all\", \"poison\", \"dispel\" or skill numbers", 2)
    end
    cured[c] = true
  end
  for _, t in ipairs(def.tags or {}) do
    if type(t) ~= "string" then error("rq.status.define: tags are strings", 2) end
    tags[t] = true
  end
  local immune, immune_list
  if def.immune then immune, immune_list = read_immune(def.immune) end
  local icon
  if def.icon then
    icon = {}
    for k, v in pairs(def.icon) do icon[k] = v end
    icon.text = nil
    rq.battle.install_flame()
  end
  if (def.physical or def.accuracy) and not combat_hooked then install_combat() end
  if def.magic and not magic_hooked then install_magic() end
  if not hooked then install() end
  if lasts == "quest" and not quest_hooked then install_quest() end
  ndefs = ndefs + 1
  local mod = rq.mod and rq.mod.id and rq.mod.id() or nil
  defs[id] = {
    id = id, name = def.name or id, kind = kind, mod = mod or "?", turns = turns, stack = stack,
    cured = cured, cured_by = { table.unpack(cured_by) }, tags = tags, tag_list = { table.unpack(def.tags or {}) },
    keep_on_down = def.keep_on_down == true, lethal = def.lethal == true, persevere = def.persevere ~= false,
    lasts = lasts, desc = def.desc, immune = immune, immune_list = immune_list,
    icon = icon, key = "st:" .. ndefs,
    on_turn = def.on_turn, on_add = def.on_add, on_remove = def.on_remove,
    physical = def.physical, magic = def.magic, accuracy = def.accuracy, text = def.text,
  }
  return id
end

local function copy_immune(list)
  if not list then return nil end
  local out = {}
  for k, v in pairs(list) do
    out[k] = type(v) == "table" and { table.unpack(v) } or v
  end
  return out
end

function status.info(id)
  check_id(id, "rq.status.info")
  local d = defs[id]
  if not d then return nil end
  return {
    id = d.id, name = d.name, kind = d.kind, mod = d.mod, turns = d.turns, stack = d.stack,
    cured_by = { table.unpack(d.cured_by) }, tags = { table.unpack(d.tag_list) }, keep_on_down = d.keep_on_down,
    lethal = d.lethal, persevere = d.persevere, lasts = d.lasts, desc = d.desc, immune = copy_immune(d.immune_list),
  }
end

local function whole_or_nil(v, field, min, fn)
  if v ~= nil and (math.type(v) ~= "integer" or v < min) then
    error(fn .. ": " .. field .. " is a whole number, " .. min .. " or more", 3)
  end
  return v
end

function status.add(iic, id, opts)
  local def = known(id, "rq.status.add")
  if math.type(iic) ~= "integer" then error("rq.status.add: expected a roster slot", 2) end
  opts = opts or {}
  if type(opts) ~= "table" then error("rq.status.add: opts must be a table", 2) end
  local turns = whole_or_nil(opts.turns, "turns", 1, "rq.status.add") or def.turns
  local stacks = whole_or_nil(opts.stacks, "stacks", 1, "rq.status.add") or 1
  local power = opts.power
  if power ~= nil and type(power) ~= "number" then error("rq.status.add: power must be a number", 2) end
  if iic <= 0 then return nil, "no_fight" end
  if not fighting then
    if def.lasts ~= "quest" or iic < ALLY then return nil, "no_fight" end
    if rq.quests.current() == 0 then return nil, "no_quest" end
  end
  if call("Ｃ状態変化", iic, -1) == 3 then return nil, "down" end
  if is_immune(iic, def) then return nil, "immune" end
  local s = find(iic, id)
  if s then
    local mode = def.stack
    if mode == "keep" then return s, false end
    if mode == "add" then
      if s.turns and turns then s.turns = s.turns + turns else s.turns = turns end
    else
      s.turns = turns
    end
    if math.type(mode) == "integer" then s.stacks = math.min(mode, s.stacks + stacks) end
    if power ~= nil then s.power = power end
    if opts.source ~= nil then s.source = opts.source end
    if iic == actor then s._fresh = true end
    show(s)
    return s, false
  end
  local max = math.type(def.stack) == "integer" and def.stack or 1
  s = {
    id = id, iic = iic, kind = def.kind, turns = turns, stacks = math.min(max, stacks),
    power = power or 0, source = opts.source or 0, data = type(opts.data) == "table" and opts.data or {},
    _fresh = iic == actor or nil,
  }
  if def.lasts == "quest" then s._quest = rq.quests.current() end
  by_iic[iic] = by_iic[iic] or {}
  table.insert(by_iic[iic], s)
  if not show(s) then s._wait = true end
  if def.on_add then run(def, "on_add", def.on_add, s) end
  each_cb(add_cbs, "on_add", s)
  return s, true
end

function status.get(iic, id)
  known(id, "rq.status.get")
  return (find(iic, id))
end

function status.list(iic)
  local out = {}
  for _, s in ipairs(by_iic[iic] or {}) do out[#out + 1] = s end
  return out
end

function status.remove(iic, id)
  known(id, "rq.status.remove")
  local s = find(iic, id)
  return s ~= nil and drop(s, "removed")
end

function status.cure(iic, what)
  local test
  if what == "all" then
    test = function() return true end
  elseif what == "bad" or what == "good" then
    test = function(s) return s.kind == what end
  elseif type(what) == "table" and type(what.tag) == "string" then
    test = function(_, def) return def.tags[what.tag] end
  elseif type(what) == "string" then
    known(what, "rq.status.cure")
    test = function(s) return s.id == what end
  else
    error("rq.status.cure: expected an id, \"bad\", \"good\", \"all\" or { tag = ... }", 2)
  end
  return #drop_where(iic, test, "cured")
end

function status.immune(iic, id)
  local def = known(id, "rq.status.immune")
  if math.type(iic) ~= "integer" then error("rq.status.immune: expected a roster slot", 2) end
  if iic <= 0 then return false end
  return is_immune(iic, def)
end

function status.set_immune(no, what, on)
  if math.type(no) ~= "integer" or no < 1 then
    error("rq.status.set_immune: expected a character number, 1 or more", 2)
  end
  if on ~= nil and type(on) ~= "boolean" then
    error("rq.status.set_immune: on must be a boolean", 2)
  end
  on = on ~= false or nil
  local by = immune_by_no[no] or { ids = {}, tags = {} }
  if type(what) == "string" then
    check_id(what, "rq.status.set_immune")
    by.ids[what] = on
  elseif type(what) == "table" and type(what.tag) == "string" then
    by.tags[what.tag] = on
  else
    error("rq.status.set_immune: expected an id or { tag = ... }", 2)
  end
  immune_by_no[no] = by
end

local function adder(cbs, name)
  return function(fn)
    if type(fn) ~= "function" then error(name .. ": expected a function", 2) end
    local cell = { fn = fn }
    cbs[#cbs + 1] = cell
    return function() cell.fn = nil end
  end
end

status.on_add = adder(add_cbs, "rq.status.on_add")
status.on_remove = adder(remove_cbs, "rq.status.on_remove")
