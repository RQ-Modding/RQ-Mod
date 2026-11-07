-- rq.status: battle states made by mods, one registry every mod shares (loader 2.2.0;
-- lethal states 2.3.0).
--
-- Built only on public calls: rq.battle.on_turn / on_end / on_down / on_support, rq.combat
-- on_physical / on_accuracy / on_magic, rq.battle.set_buff, and rq.ain.hook on two game
-- functions:
--   Ｔ戦闘前初期化   the start of a fight, before on_start: the registry is emptied here, so a
--                    state a mod adds in its own on_start stays, whatever the callback order.
--   戦闘_行動終了    the end of every actor's turn, a passed one too: turns count down here.
-- The game's cures are skill numbers checked in Ｔ補助行動: 227, 237 and 220 clear ▼ and every
-- state ("all"); 218 and 219 cure poison only ("poison").
local rq = ...
local call, hook = rq.ain.call, rq.ain.hook

local status = {}
rq.status = status

local CURES = { [227] = "all", [237] = "all", [220] = "all", [218] = "poison", [219] = "poison" }
local STACKS = { refresh = true, add = true, keep = true }

local defs, ndefs = {}, 0       -- id -> def
local by_iic = {}               -- iic -> list of instances, in the order they came
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

local function show(s)
  local def = defs[s.id]
  if not def.icon then return end
  local b = {}
  for k, v in pairs(def.icon) do b[k] = v end
  b.text = icon_text(def, s)
  local ok, err = pcall(rq.battle.set_buff, s.iic, def.key, b)
  if not ok then logf("%s icon: %s", s.id, tostring(err)) end
end

local function hide(s)
  local def = defs[s.id]
  if def.icon then pcall(rq.battle.set_buff, s.iic, def.key, nil) end
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
  hide(s)
  local def = defs[s.id]
  if def.on_remove then run(def, "on_remove", def.on_remove, s, reason) end
  each_cb(remove_cbs, "on_remove", s, reason)
  return true
end

-- Removes the instances of iic that pass test(s, def), oldest first; returns how many.
local function drop_where(iic, test, reason)
  local list = by_iic[iic]
  if not list then return 0 end
  local snapshot, n = { table.unpack(list) }, 0
  for _, s in ipairs(snapshot) do
    if test(s, defs[s.id]) and drop(s, reason) then n = n + 1 end
  end
  return n
end

local function each_on(iic, fn)
  local list = by_iic[iic]
  if not list then return end
  for _, s in ipairs({ table.unpack(list) }) do fn(s, defs[s.id]) end
end

-- The game's events ---------------------------------------------------------------------

local hooked, combat_hooked, magic_hooked = false, false, false

-- A state's on_turn gets ctx with its own defaults for ctx:damage: lethal from the definition,
-- the source as the killer. Once the bearer is down, the states after it do not tick.
local function on_turn(ctx)
  actor = ctx.iic
  each_on(ctx.iic, function(s, def)
    if def.on_turn and not ctx.downed then
      ctx._lethal, ctx._source = def.lethal, s.source
      run(def, "on_turn", def.on_turn, s, ctx)
      ctx._lethal, ctx._source = nil, nil
    end
  end)
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
    by_iic, fighting, actor = {}, true, 0
  end })
  hook("戦闘_行動終了", { before = function(h)
    local unit = h.args[1] or 0
    if unit > 0 then on_turn_end(call("戦闘番号_ＩＩＣ", unit)) end
  end })
  rq.battle.on_turn(on_turn)
  rq.battle.on_end(function()
    for iic in pairs(by_iic) do
      drop_where(iic, function() return true end, "fight_end")
    end
    by_iic, fighting, actor = {}, false, 0
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
  for _, f in ipairs({ "on_turn", "on_add", "on_remove", "physical", "magic", "accuracy", "text" }) do
    optional(def, f, "function", "rq.status.define")
  end
  optional(def, "icon", "table", "rq.status.define")
  optional(def, "lethal", "boolean", "rq.status.define")
  optional(def, "tags", "table", "rq.status.define")
  optional(def, "cured_by", "table", "rq.status.define")
  local cured_by = def.cured_by or (kind == "bad" and { "all" } or {})
  local cured, tags = {}, {}
  for _, c in ipairs(cured_by) do
    if c ~= "all" and c ~= "poison" and math.type(c) ~= "integer" then
      error("rq.status.define: cured_by holds \"all\", \"poison\" or skill numbers", 2)
    end
    cured[c] = true
  end
  for _, t in ipairs(def.tags or {}) do
    if type(t) ~= "string" then error("rq.status.define: tags are strings", 2) end
    tags[t] = true
  end
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
  ndefs = ndefs + 1
  local mod = rq.mod and rq.mod.id and rq.mod.id() or nil
  defs[id] = {
    id = id, name = def.name or id, kind = kind, mod = mod or "?", turns = turns, stack = stack,
    cured = cured, cured_by = { table.unpack(cured_by) }, tags = tags, tag_list = { table.unpack(def.tags or {}) },
    keep_on_down = def.keep_on_down == true, lethal = def.lethal == true, icon = icon, key = "st:" .. ndefs,
    on_turn = def.on_turn, on_add = def.on_add, on_remove = def.on_remove,
    physical = def.physical, magic = def.magic, accuracy = def.accuracy, text = def.text,
  }
  return id
end

function status.info(id)
  check_id(id, "rq.status.info")
  local d = defs[id]
  if not d then return nil end
  return {
    id = d.id, name = d.name, kind = d.kind, mod = d.mod, turns = d.turns, stack = d.stack,
    cured_by = { table.unpack(d.cured_by) }, tags = { table.unpack(d.tag_list) }, keep_on_down = d.keep_on_down,
    lethal = d.lethal,
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
  if not fighting or iic <= 0 or call("Ｃ状態変化", iic, -1) == 3 then
    return nil
  end
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
  by_iic[iic] = by_iic[iic] or {}
  table.insert(by_iic[iic], s)
  show(s)
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
  return drop_where(iic, test, "cured")
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
