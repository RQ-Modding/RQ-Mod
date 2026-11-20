-- Scenes from files (loader 2.60.0): rq.scene.file, rq.scene.check, rq.scene.functions and
-- rqmod-ctl playscene and scenefunctions.
--
-- A scene file is JSON in a mod's folder; the docs' schemas/scene.schema.json is its specification,
-- and scene_check.lua (check, below) says the same. It is read and checked each time it plays, so an
-- edit shows at the next play. It runs as its mod: run_as_mod (modloader_sandbox.c) puts the mod on
-- the call stack, so the read flags of its lines, its pictures (rq.image.add), io.open and rq.log's
-- tag are the mod's, also when it plays from rq.scene.start or from the preview.
local rq, read_json, run_as_mod, caller_id, ps_poll, ps_answer, check = ...
local scene = rq.scene
local NULL, ARRAY = check.NULL, check.ARRAY
local is_array, is_int, is_name, is_place = check.is_array, check.is_int, check.is_name, check.is_place
local unnull, quote = check.unnull, check.quote

local TITLE = "rq: the game went to the title"
local MAX_QUIET = 10000 -- steps in one frame before a loop counts as endless

local registry = {} -- mod id -> { name -> function } (rq.scene.functions)
local running = {}  -- the file scenes playing now, outermost first
local queued        -- the preview's file, until it starts
local frames = 0    -- frames seen by rq.on_frame, for the loop guard

-- FNV-1a, as the loader numbers a mod's characters, items and images.
local function fnv(s)
  local h = 2166136261
  for k = 1, #s do h = ((h ~ s:byte(k)) * 16777619) & 0xffffffff end
  return h
end

local function mod_no(id) return 1000 + fnv(id) % 9000 end

-- A game name (rq.ids), a number, or a mod's own id, as the number (or for a quest, the id) the
-- game's calls take; nil and why when there is none.
local KIND_TEXT = { quest = "quest", chara = "character", item = "item" }
local function find_game(kind, v)
  if is_int(v) then return v end
  local ok, no, why = pcall(rq.ids.find, kind, v)
  if ok and no then return no end
  if kind == "chara" then
    local n = mod_no(v)
    if rq.chara.id_of(n) == v then return n end
  elseif kind == "item" then
    local n = mod_no(v)
    if rq.item.id_of(n) == v then return n end
  elseif kind == "quest" then
    local ok2, n = pcall(rq.quests.no, v)
    if ok2 and n then return v end
  end
  if ok and type(why) == "string" and why:find("more than one", 1, true) then return nil, why end
  return nil, ("no %s %s: a name or key as rq.ids gives it, a number, or a mod's own id")
    :format(KIND_TEXT[kind], quote(v))
end

local function save_loaded()
  local ok, w = pcall(rq.world.current)
  return ok and w ~= nil
end

local function file_exists(path)
  local ok, fh = pcall(io.open, path, "rb")
  if ok and fh then
    fh:close()
    return true
  end
  return false
end

-- doc and the check's findings, or nil and the problems ("path: place: what"). Runs as the mod.
local function load_doc(f)
  local doc, why = read_json(f.path, NULL, ARRAY)
  if doc == nil then return nil, { f.path .. ": " .. tostring(why) } end
  local c = check.check(doc, { from = f.from, functions = registry[f.mod] or {}, find_game = find_game,
    save_loaded = save_loaded, file_exists = file_exists })
  if #c.problems > 0 then
    local out = {}
    for i, p in ipairs(c.problems) do out[i] = f.path .. ": " .. p end
    return nil, out
  end
  c.doc = doc
  return doc, c
end

---- the runner ----

local function fail(st, place, msg)
  error(st.f.path .. ": " .. place .. ": " .. msg, 0)
end

local function show(v)
  if math.type(v) == "float" and v == math.floor(v) and v > -1e15 and v < 1e15 then
    return string.format("%.0f", v)
  end
  return tostring(v)
end

-- {name} takes the variable; {{ and }} are braces. A variable that is not set stays as written.
local function fill(st, text, place)
  if not text:find("[{}]") then return text end
  local out = text:gsub("{{", "\1"):gsub("}}", "\2")
  out = out:gsub("{([%a_][%w_]*)}", function(name)
    local v = st.vars[name]
    if v == nil or v == NULL then
      rq.log(st.f.path .. ": " .. place .. ": no value for {" .. name .. "}")
      return nil
    end
    return show(v)
  end)
  out = out:gsub("\1", "{"):gsub("\2", "}")
  return out
end

local function picture(st, v)
  if v == nil or is_int(v) then return v end
  local cg = st.pics[v]
  if not cg then
    local key = st.f.mod .. ":scene:" .. v
    if #key >= 100 then key = st.f.mod .. ":scene:" .. string.format("%08x", fnv(v)) end
    cg = rq.image.add(key, v)
    st.pics[v] = cg
  end
  return cg
end

local function call_fn(st, name, args, place)
  local fns = registry[st.f.mod]
  local fn = fns and fns[name]
  if type(fn) ~= "function" then
    fail(st, place, "no function " .. quote(name) .. ": the mod names its functions with rq.scene.functions")
  end
  local a, n = {}, 0
  if args then
    n = #args
    for k = 1, n do a[k] = unnull(args[k]) end
  end
  local ctx = { vars = st.vars, path = st.f.path, step = place, mod = st.f.mod }
  return fn(ctx, st.s, table.unpack(a, 1, n))
end

local test
local TEST = {}
function TEST.seen(st, v)
  local x = scene.seen(v.seen)
  return x == true or (type(x) == "number" and x ~= 0)
end
function TEST.var(st, v)
  local x = st.vars[v.var]
  if x == NULL then x = nil end
  if v.is ~= nil and x ~= unnull(v.is) then return false end
  if v.at_least ~= nil and not (type(x) == "number" and x >= v.at_least) then return false end
  if v.at_most ~= nil and not (type(x) == "number" and x <= v.at_most) then return false end
  return true
end
function TEST.call(st, v, place)
  local r = call_fn(st, v.call, v.args, place)
  return r ~= nil and r ~= false
end
TEST["not"] = function(st, v, place) return not test(st, v["not"], place) end
function TEST.all(st, v, place)
  for _, x in ipairs(v.all) do
    if not test(st, x, place) then return false end
  end
  return true
end
function TEST.any(st, v, place)
  for _, x in ipairs(v.any) do
    if test(st, x, place) then return true end
  end
  return false
end
local function game(st, kind, v, place)
  local no, why = find_game(kind, v)
  if not no then fail(st, place, why) end
  return no
end
function TEST.cleared(st, v, place)
  local q = rq.quests.get(game(st, "quest", v.cleared, place))
  return (q.clears or 0) > 0 or q.state == 2
end
function TEST.recruited(st, v, place)
  return rq.chara.is_recruited(game(st, "chara", v.recruited, place)) == true
end
function TEST.in_party(st, v, place)
  local iic = rq.chara.find(game(st, "chara", v.in_party, place))
  return iic ~= nil and rq.party.state(iic) == 1
end
function TEST.has_item(st, v, place)
  return rq.item.count(game(st, "item", v.has_item, place)) > 0
end
function TEST.gold(st, v) return rq.game.gold() >= v.gold end
function TEST.chance(st, v) return math.random(1, 100) <= v.chance end

function test(st, v, place)
  return TEST[st.c.kind[v]](st, v, place)
end

local function pages(v)
  if type(v) == "string" then return { v } end
  return v
end

-- The first page through first(text, wait), with its frame; the next ones as the game writes them:
-- lines (R between them), then A, or B for a last page that does not wait.
local function show_pages(st, list, wait, place, first)
  local s, n = st.s, #list
  local w
  if n == 1 then w = wait end
  first(fill(st, list[1], place), w)
  for k = 2, n do
    local text = fill(st, list[k], place)
    local line_no = 0
    for line in (text .. "\n"):gmatch("(.-)\n") do
      if line_no > 0 then s.br() end
      s.text(line)
      line_no = line_no + 1
    end
    if k == n and wait == false then s.show() else s.wait() end
  end
end

local function line(st, step, place, kind)
  local who = step[kind]
  local face, name = who, nil
  if is_name(who) then
    local e = st.c.cast[who]
    face, name = e.face, e.name
  end
  face = picture(st, face)
  if step.name ~= nil then name = step.name end
  if name ~= nil then name = fill(st, name, place) end
  local o = { name = name, page = step.page }
  if step.hide_face then o.face = false end
  show_pages(st, pages(step.text), step.wait, place, function(text, w)
    o.wait = w
    st.s[kind](face, text, o)
  end)
end

local DO = {}
function DO.say(st, step, place) line(st, step, place, "say") end
function DO.think(st, step, place) line(st, step, place, "think") end
function DO.narrate(st, step, place)
  local o = { paper = step.paper, page = step.page }
  show_pages(st, pages(step.narrate), step.wait, place, function(text, w)
    o.wait = w
    st.s.narrate(text, o)
  end)
end
function DO.clear_text(st) st.s.clear_text() end
function DO.speed(st, step) st.s.speed(step.speed) end
for _, k in ipairs({ "background", "wallpaper", "mask_wallpaper", "portrait", "mini", "cg" }) do
  DO[k] = function(st, step) st.s[k](picture(st, step[k]), step.effect, step.time) end
end
function DO.background_in(st, step) st.s.background_in(picture(st, step.background_in), step.time) end
function DO.background_out(st, step) st.s.background_out(step.time) end
function DO.set(st, step)
  local l = step.set
  local wall, back = picture(st, l.wallpaper), picture(st, l.background)
  local portrait, mini, cg = picture(st, l.portrait), picture(st, l.mini), picture(st, l.cg)
  if step.effect ~= nil or cg ~= nil then
    st.s.effect_set(step.effect or 1, wall, back, portrait, mini, cg)
  else
    st.s.set(wall, back, portrait, mini)
  end
end
function DO.black(st, step) st.s.black(step.effect, step.time) end
function DO.white(st, step) st.s.white(step.effect, step.time) end
function DO.clear(st) st.s.clear() end
function DO.flash(st, step) st.s.flash(step.time) end
function DO.shake(st, step) st.s.shake(step.mode) end
function DO.shake_face(st, step) st.s.shake_face(step.mode) end
function DO.shake_portrait(st, step) st.s.shake_portrait(step.mode) end
function DO.music(st, step) st.s.music(step.music, step.effect, step.time) end
function DO.sound(st, step) st.s.sound(step.sound) end
function DO.jingle(st, step) st.s.jingle(step.jingle, step.wait) end
function DO.wait_click(st) st.s.wait_click() end
function DO.world_map(st, step)
  local w = step.world_map
  if type(w) == "table" then st.s.world_map(w.x, w.y, w.name) else st.s.world_map(w) end
end
function DO.flag(st, step) scene.seen(step.flag, true) end
function DO.unflag(st, step) scene.seen(step.unflag, false) end
function DO.label() end
function DO.note() end
DO["goto"] = function(st, step) return "goto", step["goto"] end
DO["end"] = function(st, step) return "end", unnull(step["end"]) end
DO["if"] = function(st, step, place)
  if test(st, step["if"], place .. ".if") then return "push", step["then"] end
  if step["else"] ~= nil then return "push", step["else"] end
end
function DO.call(st, step, place)
  local r = call_fn(st, step.call, step.args, place)
  if step.into then st.vars[step.into] = r end
end
function DO.cmd(st, step)
  local a, n = {}, 0
  if step.args then
    n = #step.args
    for k = 1, n do a[k] = unnull(step.args[k]) end
  end
  st.s.cmd[step.cmd](table.unpack(a, 1, n))
end

-- Separators where the shown rows leave them: none first, last or twice in a row.
local function tidy(rows)
  local out = {}
  for _, r in ipairs(rows) do
    if r ~= "-" or (#out > 0 and out[#out] ~= "-") then out[#out + 1] = r end
  end
  while out[#out] == "-" do out[#out] = nil end
  return out
end

local function lua_rows(st, rows, place)
  if type(rows) ~= "table" then fail(st, place, "the function gave no list of rows") end
  local picks = 0
  for i, it in ipairs(rows) do
    if it ~= "-" then
      local text = type(it) == "table" and it.text or it
      if type(text) ~= "string" then fail(st, place, "row " .. i .. " of the function's rows has no text") end
      if type(it) == "table" and it.id ~= nil and not is_int(it.id) then
        fail(st, place, "row " .. i .. " of the function's rows: its id is a whole number")
      end
      picks = picks + 1
    end
  end
  if picks == 0 then fail(st, place, "the function gave no row to pick") end
  return rows
end

function DO.choice(st, step, place)
  local src, rows, pick_of = step.choice, {}, {}
  if is_array(src) then
    for j, row in ipairs(src) do
      if row == "-" then
        rows[#rows + 1] = "-"
      elseif type(row) == "string" then
        rows[#rows + 1] = { id = j, text = fill(st, row, place) }
      elseif row["if"] == nil or test(st, row["if"], place .. ".choice[" .. (j - 1) .. "].if") then
        local id = row.id or j
        rows[#rows + 1] = { id = id, text = fill(st, row.text, place),
          comment = row.comment and fill(st, row.comment, place), r = row.r, g = row.g, b = row.b,
          sound = row.sound }
        pick_of[id] = row
      end
    end
    rows = tidy(rows)
    if #rows == 0 then fail(st, place, "no row of the choice is shown: each one's \"if\" is false") end
  else
    rows = lua_rows(st, call_fn(st, src.call, src.args, place), place)
  end
  local o
  if step.x ~= nil then o = { x = step.x, y = step.y } end
  local id = st.s.choice(rows, o)
  if step.into then st.vars[step.into] = id end
  local row = pick_of[id]
  if row then
    if row.steps ~= nil then return "push", row.steps end
    if row["goto"] ~= nil then return "goto", row["goto"] end
    if row["end"] ~= nil then return "end", unnull(row["end"]) end
  end
end

-- The runner's stack that plays on from a label or a step's place (2.61.0): each outer list at the
-- step after the one that holds the next list, the innermost at the step itself. The check has
-- found both.
local function stack_at(c, at)
  local trail = is_place(at) and c.trail[c.at[at]] or c.labels[at].trail
  local stack = {}
  for k, t in ipairs(trail) do
    stack[k] = { list = t.list, i = k < #trail and t.i + 1 or t.i }
  end
  return stack
end

local function loop(st)
  local c = st.c
  local stack
  if st.f.from then
    stack = stack_at(c, st.f.from)
  else
    stack = { { list = c.doc.steps, i = 1 } }
  end
  local quiet, at_frame = 0, frames
  while true do
    if st.stopped then return nil end
    local top = stack[#stack]
    if not top then return nil end
    local step = top.list[top.i]
    if step == nil then
      stack[#stack] = nil
    else
      top.i = top.i + 1
      local place = c.place[step]
      if frames ~= at_frame then
        quiet, at_frame = 0, frames
      else
        quiet = quiet + 1
        if quiet > MAX_QUIET then
          fail(st, place, MAX_QUIET .. " steps with no frame of the game between them: a goto that loops "
            .. "with no line, choice or wait in it")
        end
      end
      st.at = place -- for playscene status
      local r = table.pack(pcall(DO[c.kind[step]], st, step, place))
      if not r[1] then
        local e = tostring(r[2])
        if e:find(TITLE, 1, true) or e:sub(1, #st.f.path + 2) == st.f.path .. ": " then error(r[2], 0) end
        fail(st, place, e)
      end
      local how, a = r[2], r[3]
      if how == "end" then
        return a
      elseif how == "push" then
        stack[#stack + 1] = { list = a, i = 1 }
      elseif how == "goto" then
        stack = stack_at(c, a)
      end
    end
  end
end

-- Runs as the mod. An error is logged with the mod's tag and ends the scene, as a Lua scene's does;
-- the title's goes on up.
local function run(f, doc, c, s)
  local st = { f = f, c = c, s = s or scene, vars = {}, pics = {}, stopped = false }
  if f.vars then
    for k, v in pairs(f.vars) do st.vars[k] = v end
  end
  running[#running + 1] = st
  local r = table.pack(pcall(loop, st))
  for i = #running, 1, -1 do
    if running[i] == st then table.remove(running, i) end
  end
  if not r[1] then
    if tostring(r[2]):find(TITLE, 1, true) then error(r[2], 0) end
    rq.log("scene error: " .. tostring(r[2]))
    return nil
  end
  return r[2]
end

local function log_problems(f, problems)
  run_as_mod(f.mod, function()
    rq.log(("%s has %d problem%s; it does not play:"):format(f.path, #problems, #problems == 1 and "" or "s"))
    for i = 1, math.min(#problems, 50) do rq.log("  " .. problems[i]) end
    if #problems > 50 then rq.log("  ... and " .. (#problems - 50) .. " more") end
  end)
end

---- rq.scene.file, check, functions ----

local File = { __name = "rq.SceneFile" }
File.__index = File
File.__tostring = function(f) return "scene file " .. f.mod .. ": " .. f.path end

-- Called in a scene that plays already: its steps there, with no scene of its own.
File.__call = function(f, s)
  local doc, c = run_as_mod(f.mod, load_doc, f)
  if not doc then
    error(f.path .. " does not play: " .. c[1] .. (#c > 1 and (" (and " .. (#c - 1) .. " more)") or ""), 2)
  end
  if doc.once ~= nil then
    if scene.seen(doc.once) then return nil end
    scene.seen(doc.once, true)
  end
  return run_as_mod(f.mod, run, f, doc, c, s)
end

local inner_play = scene.play
function scene.play(fn, ...)
  if getmetatable(fn) ~= File then return inner_play(fn, ...) end
  local f = fn
  if f.cancelled then return nil end
  f.started = true
  local doc, c = run_as_mod(f.mod, load_doc, f)
  if not doc then
    log_problems(f, c)
    return nil
  end
  if doc.once ~= nil then
    if scene.seen(doc.once) then return nil end
    scene.seen(doc.once, true)
  end
  return inner_play(function(s) return run_as_mod(f.mod, run, f, doc, c, s) end)
end

function scene.file(path, opts)
  if type(path) ~= "string" or path == "" then
    error("rq.scene.file: expected a path in the mod's folder", 2)
  end
  if opts ~= nil and type(opts) ~= "table" then error("rq.scene.file: opts is a table", 2) end
  opts = opts or {}
  if opts.vars ~= nil and type(opts.vars) ~= "table" then
    error("rq.scene.file: vars is a table of names and values", 2)
  end
  if opts.from ~= nil and not is_name(opts.from) and not is_place(opts.from) then
    error("rq.scene.file: from is a label's name or a step's place, such as \"steps[4]\"", 2)
  end
  local mod = caller_id()
  if not mod then error("rq.scene.file: call it from a mod", 2) end
  return setmetatable({ path = path, mod = mod, vars = opts.vars, from = opts.from }, File)
end

function scene.check(path)
  if type(path) ~= "string" or path == "" then
    error("rq.scene.check: expected a path in the mod's folder", 2)
  end
  local mod = caller_id()
  if not mod then error("rq.scene.check: call it from a mod", 2) end
  local doc, c = run_as_mod(mod, load_doc, { path = path, mod = mod })
  if doc then return {} end
  return c
end

function scene.functions(fns)
  if type(fns) ~= "table" then error("rq.scene.functions: expected a table of functions by name", 2) end
  local mod = caller_id()
  if not mod then error("rq.scene.functions: call it from a mod", 2) end
  local mine = registry[mod] or {}
  for name, fn in pairs(fns) do
    if not is_name(name) then
      error("rq.scene.functions: " .. quote(name) .. " is not a name: letters, digits and _, not "
        .. "starting with a digit", 2)
    end
    if type(fn) ~= "function" and fn ~= false then
      error("rq.scene.functions: " .. name .. " is not a function (false takes one away)", 2)
    end
  end
  for name, fn in pairs(fns) do mine[name] = fn or nil end
  registry[mod] = mine
end

---- rqmod-ctl playscene and scenefunctions ----

local function words(s)
  local out, i = {}, 1
  while true do
    local a = s:find("%S", i)
    if not a then return out end
    if s:sub(a, a) == '"' then
      local b = s:find('"', a + 1, true)
      if not b then return nil, "a quote that does not end" end
      out[#out + 1] = s:sub(a + 1, b - 1)
      i = b + 1
    else
      local b = s:find("%s", a) or #s + 1
      out[#out + 1] = s:sub(a, b - 1)
      i = b
    end
  end
end

local function word(s)
  if s:find("[%s\"]") then return '"' .. s .. '"' end
  return s
end

-- A value typed after playscene's FILE: a whole number, a number with a decimal point, true or
-- false, else the text as written; in single quotes, the text inside them (2.62.1: '5' is a text).
local function typed(s)
  if #s >= 2 and s:sub(1, 1) == "'" and s:sub(-1) == "'" then return s:sub(2, -2) end
  if s == "true" then return true end
  if s == "false" then return false end
  if s:find("^%-?%d+$") then return math.tointeger(tonumber(s)) or tonumber(s) end
  if s:find("^%-?%d+%.%d+$") then return tonumber(s) end
  return s
end

-- One line per file that plays, the outermost first (a file can play another): playing MOD FILE
-- PLACE, PLACE the step playing or waiting; else waiting MOD FILE (started, the game has not let
-- it in yet) or idle.
local function status()
  local out = {}
  for _, st in ipairs(running) do
    out[#out + 1] = ("playing %s %s %s"):format(st.f.mod, word(st.f.path), st.at or "steps[0]")
  end
  if #out > 0 then return table.concat(out, "\n") end
  if queued and not queued.started and not queued.cancelled then
    return ("waiting %s %s"):format(queued.mod, word(queued.path))
  end
  return "idle"
end

local USAGE = "err usage: playscene MOD FILE [FROM] [name=value ...] | playscene stop | playscene status"

local function playscene(w)
  if #w == 1 and w[1] == "status" then return status() end
  if #w == 1 and w[1] == "stop" then
    local n = 0
    for _, st in ipairs(running) do
      st.stopped = true
      n = n + 1
    end
    if queued and not queued.started then
      queued.cancelled = true
      n = n + 1
    end
    if n == 0 then return "err no scene file plays" end
    return "ok"
  end
  if #w < 2 then return USAGE end
  if not pcall(run_as_mod, w[1], function() end) then return "err no mod " .. w[1] end
  local from, vars
  for k = 3, #w do
    local name, v = w[k]:match("^([^=]*)=(.*)$")
    if name then
      if not is_name(name) then
        return "err " .. quote(name) .. " is not a name: letters, digits and _, not starting with a digit"
      end
      vars = vars or {}
      vars[name] = typed(v)
    elseif from == nil then
      from = w[k]
    else
      return USAGE
    end
  end
  local f = run_as_mod(w[1], scene.file, w[2], { from = from, vars = vars })
  local doc, c = run_as_mod(w[1], load_doc, f)
  if not doc then
    local out = { ("err the file has %d problem%s"):format(#c, #c == 1 and "" or "s") }
    for i = 1, math.min(#c, 50) do out[#out + 1] = c[i] end
    if #c > 50 then out[#out + 1] = "... and " .. (#c - 50) .. " more" end
    return table.concat(out, "\n")
  end
  if scene.active() then return "err busy: a scene plays or waits to" end
  queued = f
  scene.start(f)
  return "ok"
end

-- ok, then the names the mod gave rq.scene.functions, one per line, in order.
local function scenefunctions(w)
  if #w ~= 1 then return "err usage: scenefunctions MOD" end
  if not pcall(run_as_mod, w[1], function() end) then return "err no mod " .. w[1] end
  local names = {}
  for name in pairs(registry[w[1]] or {}) do names[#names + 1] = name end
  table.sort(names)
  table.insert(names, 1, "ok")
  return table.concat(names, "\n")
end

local function pipe(req)
  local w, why = words(req)
  if not w then return "err " .. why end
  local cmd = table.remove(w, 1)
  if cmd == "playscene" then return playscene(w) end
  if cmd == "scenefunctions" then return scenefunctions(w) end
  return "err no command " .. tostring(cmd)
end

rq.on_frame(function()
  frames = frames + 1
  local req = ps_poll()
  if not req then return end
  local ok, answer = pcall(pipe, req)
  if not ok then answer = "err " .. tostring(answer) end
  ps_answer(answer)
end)

rq.on_reset(function()
  running, queued = {}, nil
end)
