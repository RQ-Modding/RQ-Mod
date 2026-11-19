-- Scenes from files (loader 2.60.0): rq.scene.file, rq.scene.check, rq.scene.functions and
-- rqmod-ctl playscene.
--
-- A scene file is JSON in a mod's folder; the docs' schemas/scene.schema.json is its specification,
-- and the checks below say the same. It is read and checked each time it plays, so an edit shows at
-- the next play. It runs as its mod: run_as_mod (modloader_sandbox.c) puts the mod on the call
-- stack, so the read flags of its lines, its pictures (rq.image.add), io.open and rq.log's tag are
-- the mod's, also when it plays from rq.scene.start or from the preview.
local rq, read_json, run_as_mod, caller_id, ps_poll, ps_answer = ...
local scene = rq.scene

local FORMAT = 1 -- the newest format this loader reads; it reads every older one
local TITLE = "rq: the game went to the title"
local NULL = setmetatable({}, { __name = "json null", __tostring = function() return "null" end })
local ARRAY = { __name = "json array" }
local NAME = "^[%a_][%w_]*$"
local EXT = { png = true, PNG = true, jpg = true, JPG = true, jpeg = true, JPEG = true,
  webp = true, WEBP = true, gif = true, GIF = true }
local MAX_QUIET = 10000 -- steps in one frame before a loop counts as endless

local registry = {} -- mod id -> { name -> function } (rq.scene.functions)
local running = {}  -- the file scenes playing now, outermost first
local queued        -- the preview's file, until it starts
local frames = 0    -- frames seen by rq.on_frame, for the loop guard

---- helpers ----

local function is_array(v) return type(v) == "table" and getmetatable(v) == ARRAY end
local function is_object(v) return type(v) == "table" and getmetatable(v) == nil end
local function is_int(v) return math.type(v) == "integer" end
local function is_name(v) return type(v) == "string" and v:find(NAME) ~= nil end
local function unnull(v) if v == NULL then return nil end return v end
local function quote(s) return '"' .. tostring(s) .. '"' end

-- A picture path: in the mod's folder (not absolute, no "..", no ":"), of a kind rq.image reads.
local function is_path(v)
  if type(v) ~= "string" or v:find("^[/\\]") or v:find("..", 1, true) or v:find(":", 1, true) then
    return false
  end
  local ext = v:match("%.(%w+)$")
  return ext ~= nil and EXT[ext] == true
end

local function keys_of(t)
  local ks = {}
  for k in pairs(t) do ks[#ks + 1] = k end
  table.sort(ks, function(a, b) return tostring(a) < tostring(b) end)
  return ks
end

local function quoted_list(ks)
  local q = {}
  for i, k in ipairs(ks) do q[i] = quote(k) end
  return table.concat(q, ", ")
end

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

---- the checks ----

local function add(c, place, msg)
  c.problems[#c.problems + 1] = place == "" and msg or (place .. ": " .. msg)
end

-- The simple types: nil when v fits, else what was expected.
local CHECK = {}
function CHECK.on(v) if v ~= true then return "expected true" end end
function CHECK.bool(v) if type(v) ~= "boolean" then return "expected true or false" end end
function CHECK.str(v) if type(v) ~= "string" then return "expected a text" end end
function CHECK.flag(v)
  if type(v) ~= "string" or v == "" then return "expected an event flag's name (a text)" end
end
function CHECK.nonempty(v)
  if type(v) ~= "string" or v == "" then return "expected a text that is not empty" end
end
function CHECK.int(v) if not is_int(v) then return "expected a whole number" end end
function CHECK.uint(v)
  if not is_int(v) or v < 0 then return "expected a whole number, 0 or more" end
end
function CHECK.speed(v)
  if not is_int(v) or v < -1 then return "expected a whole number, -1 or more" end
end
function CHECK.color(v)
  if not is_int(v) or v < 0 or v > 255 then return "expected a whole number from 0 to 255" end
end
function CHECK.chance(v)
  if not is_int(v) or v < 0 or v > 100 then return "expected a whole number from 0 to 100" end
end
function CHECK.number(v) if type(v) ~= "number" then return "expected a number" end end
function CHECK.name(v)
  if not is_name(v) then return "expected a name: letters, digits and _, not starting with a digit" end
end
function CHECK.value(v)
  local t = type(v)
  if not (t == "string" or t == "number" or t == "boolean" or v == NULL) then
    return "expected a text, a number, true, false or null"
  end
end

local COMPLEX = {}
local check_steps, check_cond

local function check_value(c, typ, v, place, trail)
  local simple = CHECK[typ]
  if simple then
    local m = simple(v)
    if m then add(c, place, m) end
    return
  end
  COMPLEX[typ](c, v, place, trail)
end

local function picture_at(c, v, place)
  c.pictures[#c.pictures + 1] = { v, place }
end

function COMPLEX.picture(c, v, place)
  if is_int(v) then
    if v < 0 then add(c, place, "expected a CG number, 0 or more") end
  elseif is_path(v) then
    picture_at(c, v, place)
  else
    add(c, place, "expected a picture: a CG number, or a path in the mod's folder ending in .png, "
      .. ".jpg, .jpeg, .webp or .gif (no \"..\", no \":\")")
  end
end

function COMPLEX.face(c, v, place)
  if is_int(v) then
    if v < 0 then add(c, place, "expected a face's CG number, 0 or more") end
  elseif is_name(v) then
    if c.cast[v] == nil then add(c, place, quote(v) .. " is not in the file's cast") end
  elseif is_path(v) then
    picture_at(c, v, place)
  else
    add(c, place, "expected a face: a CG number, a name of the file's cast, or a picture path")
  end
end

function COMPLEX.text(c, v, place)
  if type(v) == "string" then return end
  if is_array(v) and #v >= 1 then
    for k = 1, #v do
      if type(v[k]) ~= "string" then add(c, place .. "[" .. (k - 1) .. "]", "expected a text") end
    end
    return
  end
  add(c, place, "expected a text, or a list of pages (texts)")
end

function COMPLEX.args(c, v, place)
  if not is_array(v) then
    add(c, place, "expected a list of values")
    return
  end
  for k = 1, #v do
    local m = CHECK.value(v[k])
    if m then add(c, place .. "[" .. (k - 1) .. "]", m) end
  end
end

function COMPLEX.steps(c, v, place, trail) check_steps(c, v, place, trail) end
function COMPLEX.condition(c, v, place) check_cond(c, v, place) end

function COMPLEX.conditions(c, v, place)
  if not is_array(v) or #v == 0 then
    add(c, place, "expected a list of conditions")
    return
  end
  for k = 1, #v do check_cond(c, v[k], place .. "[" .. (k - 1) .. "]") end
end

function COMPLEX.fname(c, v, place)
  local m = CHECK.name(v)
  if m then add(c, place, m) else c.calls[#c.calls + 1] = { v, place } end
end

function COMPLEX.target(c, v, place)
  local m = CHECK.name(v)
  if m then add(c, place, m) else c.gotos[#c.gotos + 1] = { v, place } end
end

local function game_ref(kind)
  return function(c, v, place)
    if is_int(v) then
      if v < 1 then add(c, place, "expected a number, 1 or more") end
    elseif type(v) ~= "string" or v == "" then
      add(c, place, "expected a number or a name")
    else
      c.games[#c.games + 1] = { kind, v, place }
    end
  end
end
COMPLEX.quest = game_ref("quest")
COMPLEX.chara = game_ref("chara")
COMPLEX.item = game_ref("item")

local LAYERS = { wallpaper = true, background = true, portrait = true, mini = true, cg = true }
function COMPLEX.layers(c, v, place)
  if not is_object(v) or next(v) == nil then
    add(c, place, 'expected the layers to show, such as { "background": 5280 }')
    return
  end
  for _, k in ipairs(keys_of(v)) do
    if LAYERS[k] then
      COMPLEX.picture(c, v[k], place .. "." .. k)
    else
      add(c, place, "no layer " .. quote(k) .. ": wallpaper, background, portrait, mini or cg")
    end
  end
end

local ROW = { text = "str", id = "int", comment = "str", r = "color", g = "color", b = "color",
  sound = "uint", ["if"] = "condition", steps = "steps", ["goto"] = "target", ["end"] = "value" }

function COMPLEX.choice(c, v, place, trail)
  local usage = 'expected a list of rows, or { "call": "a function" } for rows from the mod\'s Lua'
  if is_object(v) then
    for _, k in ipairs(keys_of(v)) do
      if k == "call" then
        COMPLEX.fname(c, v.call, place .. ".call")
      elseif k == "args" then
        COMPLEX.args(c, v.args, place .. ".args")
      else
        add(c, place, 'rows from a function take "call" and "args", not ' .. quote(k))
      end
    end
    if v.call == nil then add(c, place, usage) end
    return
  end
  if not is_array(v) or #v == 0 then
    add(c, place, usage)
    return
  end
  local ids, picks = {}, 0
  for k = 1, #v do
    local row, rp, id = v[k], place .. "[" .. (k - 1) .. "]", nil
    if row == "-" then -- a separator
    elseif type(row) == "string" then
      picks, id = picks + 1, k
    elseif is_object(row) then
      picks = picks + 1
      for _, key in ipairs(keys_of(row)) do
        local typ = ROW[key]
        if typ then
          check_value(c, typ, row[key], rp .. "." .. key, trail)
        else
          add(c, rp, "a row has no field " .. quote(key))
        end
      end
      if row.text == nil then add(c, rp, 'a row needs "text"') end
      local ways = (row.steps ~= nil and 1 or 0) + (row["goto"] ~= nil and 1 or 0)
        + (row["end"] ~= nil and 1 or 0)
      if ways > 1 then add(c, rp, 'a row goes one way: "steps", "goto" or "end"') end
      id = is_int(row.id) and row.id or k
    else
      add(c, rp, 'expected a row: a text, "-", or an object with "text"')
    end
    if id then
      if ids[id] then
        add(c, rp, "the id " .. id .. " is also the id of row " .. ids[id] .. " (a row's id is its place, "
          .. "from 1, unless it has one)")
      else
        ids[id] = k - 1
      end
    end
  end
  if picks == 0 then add(c, place, "no row to pick") end
end

-- Each step: its main value's type, its options, and an option it needs.
local LINE = { text = "text", name = "str", hide_face = "bool", page = "bool", wait = "bool" }
local PICTURE = { effect = "uint", time = "uint" }
local STEP = {
  say = { "face", LINE, need = "text" },
  think = { "face", LINE, need = "text" },
  narrate = { "text", { paper = "bool", page = "bool", wait = "bool" } },
  clear_text = { "on" },
  speed = { "speed" },
  background = { "picture", PICTURE },
  wallpaper = { "picture", PICTURE },
  mask_wallpaper = { "picture", PICTURE },
  portrait = { "picture", PICTURE },
  mini = { "picture", PICTURE },
  cg = { "picture", PICTURE },
  background_in = { "picture", { time = "uint" } },
  background_out = { "on", { time = "uint" } },
  set = { "layers", { effect = "uint" } },
  black = { "on", PICTURE },
  white = { "on", PICTURE },
  clear = { "on" },
  flash = { "on", { time = "uint" } },
  shake = { "on", { mode = "uint" } },
  shake_face = { "on", { mode = "uint" } },
  shake_portrait = { "on", { mode = "uint" } },
  music = { "uint", { effect = "int", time = "int" } },
  sound = { "uint" },
  jingle = { "uint", { wait = "bool" } },
  wait_click = { "on" },
  choice = { "choice", { into = "name", x = "int", y = "int" } },
  ["if"] = { "condition", { ["then"] = "steps", ["else"] = "steps" }, need = "then" },
  flag = { "flag" },
  unflag = { "flag" },
  call = { "fname", { args = "args", into = "name" } },
  label = { "name" },
  ["goto"] = { "target" },
  ["end"] = { "value" },
  cmd = { "nonempty", { args = "args" } },
  note = { "str" },
}

local function check_step(c, st, place, trail)
  if not is_object(st) then
    add(c, place, 'expected a step: an object such as { "say": 2000, "text": "Hello." }')
    return
  end
  local all, kinds = keys_of(st), {}
  for _, k in ipairs(all) do
    if STEP[k] then kinds[#kinds + 1] = k end
  end
  if #kinds == 0 then
    add(c, place, #all == 0 and "an empty step"
      or ("not a step: no step is named " .. quoted_list(all)))
    return
  end
  if #kinds > 1 then
    add(c, place, "two steps in one object: " .. quoted_list(kinds) .. " (one step per object)")
    return
  end
  local kind = kinds[1]
  local spec = STEP[kind]
  c.kind[st], c.place[st] = kind, place
  check_value(c, spec[1], st[kind], place .. "." .. kind, trail)
  local opts = spec[2] or {}
  for _, k in ipairs(all) do
    if k ~= kind then
      if opts[k] then
        check_value(c, opts[k], st[k], place .. "." .. k, trail)
      else
        add(c, place, quote(kind) .. " has no field " .. quote(k))
      end
    end
  end
  if spec.need and st[spec.need] == nil then
    add(c, place, quote(kind) .. " needs " .. quote(spec.need))
  end
  if kind == "label" and is_name(st.label) then
    local old = c.labels[st.label]
    if old then
      add(c, place, "the label " .. quote(st.label) .. " is also at " .. old.place)
    else
      c.labels[st.label] = { trail = trail, place = place }
    end
  end
end

-- trail: where the list sits, as { list, i } pairs from the file's steps down; a label keeps
-- its own, and a goto rebuilds the runner's stack from it.
function check_steps(c, list, place, trail)
  if not is_array(list) then
    add(c, place, "expected a list of steps")
    return
  end
  for k = 1, #list do
    local t = {}
    for n, x in ipairs(trail) do t[n] = x end
    t[#t + 1] = { list = list, i = k }
    check_step(c, list[k], place .. "[" .. (k - 1) .. "]", t)
  end
end

local COND = { seen = "flag", var = "name", call = "fname", ["not"] = "condition",
  all = "conditions", any = "conditions", cleared = "quest", recruited = "chara",
  in_party = "chara", has_item = "item", gold = "uint", chance = "chance" }
local COND_OPTS = { var = { is = "value", at_least = "number", at_most = "number" },
  call = { args = "args" } }

function check_cond(c, v, place)
  if not is_object(v) then
    add(c, place, 'expected a condition, such as { "seen": "mymod:met" }')
    return
  end
  local all, kinds = keys_of(v), {}
  for _, k in ipairs(all) do
    if COND[k] then kinds[#kinds + 1] = k end
  end
  if #kinds == 0 then
    add(c, place, #all == 0 and "an empty condition"
      or ("not a condition: no condition is named " .. quoted_list(all)))
    return
  end
  if #kinds > 1 then
    add(c, place, "two conditions in one object: " .. quoted_list(kinds)
      .. ' (put them in { "all": [...] } or { "any": [...] })')
    return
  end
  local kind = kinds[1]
  c.kind[v] = kind
  check_value(c, COND[kind], v[kind], place .. "." .. kind)
  local opts = COND_OPTS[kind] or {}
  for _, k in ipairs(all) do
    if k ~= kind then
      if opts[k] then
        check_value(c, opts[k], v[k], place .. "." .. k)
      else
        add(c, place, quote(kind) .. " has no field " .. quote(k))
      end
    end
  end
  if kind == "var" and v.is == nil and v.at_least == nil and v.at_most == nil then
    add(c, place, '"var" needs "is", "at_least" or "at_most"')
  end
end

local function check_cast(c, v)
  if not is_object(v) then
    add(c, "cast", 'expected names for faces, such as { "mira": { "face": 2816 } }')
    return
  end
  for _, name in ipairs(keys_of(v)) do
    local e, p = v[name], "cast." .. name
    if not is_name(name) then
      add(c, "cast", quote(name) .. " is not a name: letters, digits and _, not starting with a digit")
    elseif not is_object(e) then
      add(c, p, 'expected { "face": a CG number or a picture path, "name": the name plate }')
    else
      for _, k in ipairs(keys_of(e)) do
        if k == "face" then
          if is_name(e.face) then
            add(c, p .. ".face", "expected a CG number or a picture path")
          else
            COMPLEX.picture(c, e.face, p .. ".face")
          end
        elseif k == "name" then
          check_value(c, "str", e.name, p .. ".name")
        else
          add(c, p, "a cast entry has no field " .. quote(k))
        end
      end
      if e.face == nil then add(c, p, 'a cast entry needs "face"') end
    end
  end
end

-- What a schema cannot say: the labels, the functions, the game's names and the pictures' files.
local function check_refs(c)
  for _, g in ipairs(c.gotos) do
    if not c.labels[g[1]] then add(c, g[2], "no label " .. quote(g[1]) .. " in the file") end
  end
  if c.f.from ~= nil and not (type(c.f.from) == "string" and c.labels[c.f.from]) then
    add(c, "", "no label " .. quote(c.f.from) .. " to start at")
  end
  local fns = registry[c.f.mod] or {}
  for _, x in ipairs(c.calls) do
    if type(fns[x[1]]) ~= "function" then
      add(c, x[2], "no function " .. quote(x[1]) .. ": the mod names its functions with "
        .. "rq.scene.functions")
    end
  end
  local loaded
  for _, g in ipairs(c.games) do
    local no, why = find_game(g[1], g[2])
    if not no then
      -- A mod's quest has its number only in a save that has it.
      if g[1] ~= "quest" then
        add(c, g[3], why)
      else
        if loaded == nil then loaded = save_loaded() end
        if loaded then add(c, g[3], why) end
      end
    end
  end
  local seen = {}
  for _, p in ipairs(c.pictures) do
    if not seen[p[1]] then
      seen[p[1]] = true
      local ok, fh = pcall(io.open, p[1], "rb")
      if ok and fh then
        fh:close()
      else
        add(c, p[2], "no file " .. p[1] .. " in the mod's folder")
      end
    end
  end
end

local function check_doc(doc, f)
  local c = { f = f, problems = {}, kind = {}, place = {}, labels = {}, gotos = {}, calls = {},
    games = {}, pictures = {}, cast = {} }
  if not is_object(doc) then
    add(c, "", 'expected an object with "format" and "steps"')
    return c
  end
  local fmt = doc.format
  if fmt == nil then
    add(c, "", 'needs "format": 1')
  elseif is_int(fmt) and fmt > FORMAT then
    add(c, "format", ("format %d is newer than this RQ MOD reads (%d): update RQ MOD"):format(fmt, FORMAT))
    return c
  elseif fmt ~= FORMAT then
    add(c, "format", "expected 1")
  end
  if is_object(doc.cast) then c.cast = doc.cast end
  for _, k in ipairs(keys_of(doc)) do
    if k == "$schema" then
      check_value(c, "str", doc[k], k)
    elseif k == "once" then
      check_value(c, "flag", doc.once, "once")
    elseif k == "cast" then
      check_cast(c, doc.cast)
    elseif k == "steps" then
      check_steps(c, doc.steps, "steps", {})
    elseif k ~= "format" then
      add(c, "", "a scene file has no field " .. quote(k))
    end
  end
  if doc.steps == nil then add(c, "", 'needs "steps"') end
  check_refs(c)
  return c
end

-- doc and the check's findings, or nil and the problems ("path: place: what"). Runs as the mod.
local function load_doc(f)
  local doc, why = read_json(f.path, NULL, ARRAY)
  if doc == nil then return nil, { f.path .. ": " .. tostring(why) } end
  local c = check_doc(doc, f)
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

local function stack_at(c, label)
  local trail, stack = c.labels[label].trail, {}
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
  if opts.from ~= nil and not is_name(opts.from) then
    error("rq.scene.file: from is a label's name", 2)
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

---- rqmod-ctl playscene ----

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

local function preview(req)
  local w, why = words(req)
  if not w then return "err " .. why end
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
  if #w < 2 or #w > 3 then return "err usage: playscene MOD FILE [LABEL] | playscene stop" end
  local f = run_as_mod(w[1], scene.file, w[2], { from = w[3] })
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

rq.on_frame(function()
  frames = frames + 1
  local req = ps_poll()
  if not req then return end
  local ok, answer = pcall(preview, req)
  if not ok then answer = "err " .. tostring(answer) end
  ps_answer(answer)
end)

rq.on_reset(function()
  running, queued = {}, nil
end)
