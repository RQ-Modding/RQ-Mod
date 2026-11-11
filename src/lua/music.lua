-- rq.music (loader 2.4.0): a mod's file plays where the game plays one of its music tracks.
--
-- The C half (modloader_music.c) keeps the files and opens them when the game prepares a music
-- channel. This half is the API and three hooks:
--   SYS_PlayBGM(no, loops, effect, fade, volume)  every music change of the game goes through it.
--       before: on_play callbacks; a file for this time only; a restart when the track already
--       plays but with another file than it should now. after: a once file nobody used is dropped.
--   音楽の名前(no)                                a mod's name for a track (the music room).
-- Which moment (2.27.0) is read from the AIN call stack in SYS_PlayBGM's hook: the game's doors
-- (音楽_戦闘, 迷宮音楽_再生, システム曲_再生, ＯＰ曲_再生) and the function that called 音楽.
-- A track the player switched off on a pack's Config page (2.27.0) is left to the game's, or to
-- the next mod down in load order that replaced it.
-- SYS_PlayBGM returns at once when the number is _音楽 (the track playing); setting _音楽 to
-- the lowest integer makes it start the track again, as the game's own load does.
-- rq.battle.start's music (2.36.0): basics.lua sets it for one fight through the loader's private
-- table (priv.fight_music); it is applied at the fight's own music, after the on_play callbacks.
local rq, c, priv = ...
local call, hook, global, set_global = rq.ain.call, rq.ain.hook, rq.ain.global, rq.ain.set_global

local music = {}
rq.music = music

local NONE = -2147483648
local names = {}          -- no -> { name, mod }
local cbs = {}            -- on_play callbacks
local hooked, name_hooked = false, false
local raw_names = false   -- 音楽の名前's hook leaves the game's name (the Config page asks)
local once                -- { no, file, mod } set by ctx:file in this SYS_PlayBGM
local unchecked = {}      -- no -> true: replaced before the game opened its music (init.lua)
local check_off
local from_mod = false    -- rq.music.play is calling the game
local entries = {}        -- no -> { { mod, full, rel, opts }, ... } in the order they came
local fight_music         -- { track } or { full, rel, mod }: rq.battle.start's, for its fight

local function mod_id()
  return rq.mod and rq.mod.id and rq.mod.id() or "?"
end

local function track(no, fn)
  if math.type(no) ~= "integer" or no < 1 then
    error(fn .. ": expected a music number, 1 or more", 3)
  end
end

local function check_opts(opts, fn)
  if opts == nil then return {} end
  if type(opts) ~= "table" then error(fn .. ": expected a table of options", 3) end
  for _, k in ipairs({ "loop_start", "loop_end" }) do
    local v = opts[k]
    if v ~= nil and (type(v) ~= "number" or v < 0) then
      error(fn .. ": " .. k .. " is seconds, 0 or more", 3)
    end
  end
  local v = opts.volume
  if v ~= nil and (math.type(v) ~= "integer" or v < 0 or v > 200) then
    error(fn .. ": volume is a whole number, 0 to 200", 3)
  end
  return opts
end

-- What should play for a number now: the once file, else the replacement, else the game's ("").
local function expected(no)
  if once and once.no == no then return once.file, once.mod end
  local r = c.get(no)
  if r then return r.file, r.mod end
  return "", ""
end

local Play = {}

function Play:file(path, opts)
  if type(path) ~= "string" then error("ctx:file: expected a path", 2) end
  opts = check_opts(opts, "ctx:file")
  local mod = mod_id()
  c.set(self.no, path, opts.loop_start, opts.loop_end, opts.volume or 100, mod, true)
  once = { no = self.no, file = path, mod = mod }
end

function Play:track(no)
  track(no, "ctx:track")
  if not c.exists(no) then error("ctx:track: the game has no music " .. no, 2) end
  if once and once.no == self.no then
    once = nil
    c.clear_once()
  end
  self.no = no
end

-- ---- Which moment (2.27.0) ----

-- The game's music functions between a caller and SYS_PlayBGM.
local WRAP = {
  ["SYS_PlayBGM"] = true, ["音楽"] = true, ["音楽１回再生"] = true, ["音楽_再生"] = true,
  ["音楽_戦闘"] = true, ["迷宮音楽_再生"] = true, ["システム曲_再生"] = true, ["ＯＰ曲_再生"] = true,
}
-- Callers that are the game's screens and system, not a scene: the extras, the system bar, a
-- save's load, the title, the quest list, the staff roll and ending, the end of a fight.
local OTHER = {
  ["T_MusicMode@after"] = true, ["T_MusicModeListManager@play"] = true, ["T_CgMode@run"] = true,
  ["システムバー音楽_復元"] = true, ["タイトル画面"] = true, ["クエスト_選択"] = true,
  ["CStaffRoll@Update"] = true, ["Ｒ８エンディングロール"] = true, ["Ｒ８リザルト画面"] = true,
  ["音楽_戦闘後"] = true,
  -- A mod's code that calls the game from rq.on_frame runs under the frame's update.
  ["UpdateView"] = true, ["UPDATE"] = true,
}
local CALLER_KIND = {
  ["ランス城_選択"] = "castle",
  ["Ｒ８ランス城／前処理"] = "castle",
  ["T_Result@success"] = "result",
  ["Ｒ８女の子モンスターＧＥＴ"] = "jingle",
}

local function classify()
  if from_mod then return "other" end
  local ok, st = pcall(rq.ain.stack, 64)
  if not ok then return "other" end
  local seen, caller, boss = {}, nil, false
  for _, name in ipairs(st) do
    if WRAP[name] then
      seen[name] = true
    elseif not caller then
      caller = name
    end
    if name == "ボス戦闘処理" then boss = true end
  end
  if seen["音楽_戦闘"] then return boss and "boss" or "fight" end
  if seen["迷宮音楽_再生"] then return "dungeon" end
  if seen["システム曲_再生"] then return "town" end
  if seen["ＯＰ曲_再生"] then return "opening" end
  if caller and CALLER_KIND[caller] then return CALLER_KIND[caller] end
  if rq.scene and rq.scene.active and rq.scene.active() then return "scene" end
  if not caller or OTHER[caller] or caller:sub(1, 4) == "SYS_" then return "other" end
  return "scene", caller
end

-- The monster numbers in the enemy slots (PTB[1..5]).
local function enemies()
  local out = {}
  for i = 1, 5 do
    local ok, iic = pcall(global, "PTB", i)
    if ok and type(iic) == "number" and iic ~= 0 then
      local ok2, no = pcall(call, "Ｃ番号", iic, -1)
      if ok2 and type(no) == "number" and no > 0 then out[#out + 1] = { slot = i, no = no } end
    end
  end
  return out
end

local function dungeon_info()
  if not (rq.dungeon and rq.dungeon.active and rq.dungeon.active()) then return nil end
  local ok, info = pcall(rq.dungeon.info)
  if ok and type(info) == "table" and (info.quest or 0) ~= 0 then return info end
  return nil
end

-- Fields worked out only when a callback reads them.
local LAZY = {
  enemies = function(ctx)
    if ctx.kind ~= "fight" and ctx.kind ~= "boss" then return nil end
    local list = {}
    for i, e in ipairs(enemies()) do list[i] = e.no end
    return list
  end,
  boss = function(ctx)
    if ctx.kind ~= "boss" then return nil end
    local list = enemies()
    for _, e in ipairs(list) do if e.slot == 2 then return e.no end end
    return list[1] and list[1].no or nil
  end,
  area = function() local i = dungeon_info(); return i and i.area or nil end,
  map_kind = function() local i = dungeon_info(); return i and i.kind or nil end,
  world = function() return rq.world and rq.world.current and rq.world.current() or nil end,
}

Play.__index = function(ctx, k)
  local m = Play[k]
  if m ~= nil then return m end
  local f = LAZY[k]
  if f then
    local v = f(ctx)
    if v ~= nil then rawset(ctx, k, v) end
    return v
  end
  return nil
end

local function install()
  hooked = true
  hook("SYS_PlayBGM", {
    before = function(h)
      local no = h.args[1] or 0
      once = nil
      if no <= 0 then return end
      local playing = global("_音楽")
      local kind, scene_name = classify()
      local ctx = setmetatable({
        no = no,
        playing = playing,
        kind = kind,
        scene = scene_name,
        quest = rq.quests and rq.quests.current and rq.quests.current() or 0,
        loops = h.args[2] or 0,
      }, Play)
      for i = 1, #cbs do
        local fn = cbs[i].fn
        if fn then
          local ok, err = pcall(fn, ctx)
          if not ok then rq.log("rq.music.on_play: " .. tostring(err)) end
        end
      end
      if fight_music and (kind == "fight" or kind == "boss") then
        local s = fight_music
        if s.track then
          if once and once.no == ctx.no then
            once = nil
            c.clear_once()
          end
          ctx.no = s.track
        else
          local ok, err = pcall(c.set, ctx.no, s.full, nil, nil, 100, s.mod, true, s.rel)
          if ok then
            once = { no = ctx.no, file = s.rel, mod = s.mod }
          else
            rq.log("rq.battle.start: music: " .. tostring(err))
          end
        end
      end
      if ctx.no ~= no then
        h.args[1] = ctx.no
      end
      -- The track already plays: start it again when another file should play now.
      if ctx.no == playing then
        local file, mod = c.playing(ctx.no)
        local want, want_mod = expected(ctx.no)
        if file ~= nil and (file ~= want or (want ~= "" and mod ~= want_mod)) then
          set_global("_音楽", NONE)
        end
      end
    end,
    after = function()
      if once then
        once = nil
        c.clear_once()
      end
    end,
  })
end

-- A replacement made in init.lua, before the game opened its archives, is checked at the first
-- frame: a number the game has not is logged and dropped.
local function check_later(no)
  unchecked[no] = true
  if check_off then return end
  check_off = rq.on_frame(function()
    if c.exists(1) == nil then return end
    check_off()
    for n in pairs(unchecked) do
      if not c.exists(n) then
        local r = c.get(n)
        rq.log(string.format("rq.music: %s replaced music %d, which the game has not; dropped",
          r and r.mod or "?", n))
        c.clear(n)
        names[n] = nil
        entries[n] = nil
      end
    end
    unchecked = {}
  end)
end

local function install_names()
  name_hooked = true
  hook("音楽の名前", { after = function(h)
    local n = names[h.args[1]]
    if n and not raw_names then h.result = n.name end
  end })
end

-- ---- The player's switches (2.27.0) ----
-- rq.store names its keys by the mod whose code is on the stack. A coroutine's stack holds only
-- this file, so the keys are the loader's ("music:<mod>:<key>"), the same whichever mod asks.
local function loader_store(fn, ...)
  local co = coroutine.create(fn)
  local ok, r = coroutine.resume(co, ...)
  if not ok then error(r, 0) end
  return r
end

local function switch_key(mod, key) return "music:" .. mod .. ":" .. key end

local function is_on(mod, key)
  return loader_store(function() return rq.store.global.get(switch_key(mod, key), true) ~= false end)
end

local function set_on(mod, key, on)
  -- On is the default, so it is kept as no key at all.
  local v = false
  if on then v = nil end
  loader_store(function() rq.store.global.set(switch_key(mod, key), v) end)
end

local function track_key(no) return "track:" .. no end
-- A row's widget id: t52 for a track, m.<key> for a moment.
local function row_id(key)
  local no = key:match("^track:(%d+)$")
  if no then return "t" .. no end
  return "m." .. key:gsub("^moment:", "")
end

-- Puts the right file behind a number: the last mod in load order whose switch is on, else the
-- game's. Names follow it.
local function apply(no)
  local list = entries[no]
  local top
  if list then
    for i = #list, 1, -1 do
      if is_on(list[i].mod, track_key(no)) then top = list[i]; break end
    end
  end
  if top then
    local o = top.opts
    local ok, info = pcall(c.set, no, top.full, o.loop_start, o.loop_end, o.volume or 100, top.mod, false, top.rel)
    if not ok then
      rq.log(string.format("rq.music: %s's '%s' for track %d: %s", top.mod, top.rel, no, tostring(info)))
      c.clear(no)
    end
    if o.name then
      names[no] = { name = o.name, mod = top.mod }
      if not name_hooked then install_names() end
    else
      names[no] = nil
    end
    return ok and info or nil
  end
  c.clear(no)
  names[no] = nil
  return nil
end

-- The track plays now: start it again, so a change shows at once.
local function replay(no)
  if global("_音楽") ~= no then return end
  set_global("_音楽", NONE)
  from_mod = true
  local ok, err = pcall(call, "音楽", no, -1, -1)
  from_mod = false
  if not ok then rq.log("rq.music: " .. tostring(err)) end
end

function music.replace(no, path, opts)
  track(no, "rq.music.replace")
  if type(path) ~= "string" then error("rq.music.replace: expected a path", 2) end
  opts = check_opts(opts, "rq.music.replace")
  if opts.name ~= nil and type(opts.name) ~= "string" then
    error("rq.music.replace: name must be a string", 2)
  end
  local exists = c.exists(no)
  if exists == false then error("rq.music.replace: the game has no music " .. no, 2) end
  local mod = mod_id()
  local full = c.resolve(path)
  -- The file is checked now, whatever the switch says.
  local probe = c.set(no, full, opts.loop_start, opts.loop_end, opts.volume or 100, mod, true, path)
  c.clear_once()
  local list = entries[no] or {}
  entries[no] = list
  for i = #list, 1, -1 do
    if list[i].mod == mod then table.remove(list, i) end
  end
  local below = list[#list]
  list[#list + 1] = { mod = mod, full = full, rel = path, opts = {
    loop_start = opts.loop_start, loop_end = opts.loop_end, volume = opts.volume, name = opts.name } }
  if below then
    rq.log(string.format("rq.music: track %d: %s's '%s' replaces %s's '%s'", no, mod, path, below.mod, below.rel))
  end
  local info = apply(no)
  if not info then
    probe.off = true
    info = probe
  end
  if exists == nil then check_later(no) end
  if not hooked then install() end
  return info
end

function music.restore(no)
  track(no, "rq.music.restore")
  local list, mod, had = entries[no], mod_id(), false
  if list then
    for i = #list, 1, -1 do
      if list[i].mod == mod then table.remove(list, i); had = true end
    end
  end
  if not had then return false end
  apply(no)
  return true
end

---Whether the player left this mod's switch on (rq.music.page, music_list). 2.27.0.
function music.enabled(key)
  if type(key) ~= "string" and math.type(key) ~= "integer" then
    error("rq.music.enabled: expected a moment's key or a track number", 2)
  end
  if math.type(key) == "integer" then key = track_key(key) else key = "moment:" .. key end
  return is_on(mod_id(), key)
end

-- ---- A pack's Config page (2.27.0) ----

local preview -- { mod, no, back } while a page plays a track
local frames, seen, watch_off = 0, 0, nil

local function stop_preview()
  if not preview then return end
  local back = preview.back
  preview = nil
  set_global("_音楽", NONE)
  from_mod = true
  local ok, err = pcall(call, "音楽", back > 0 and back or 0, -1, -1)
  from_mod = false
  if not ok then rq.log("rq.music: " .. tostring(err)) end
end

local function start_preview(mod, e, no)
  local back = preview and preview.back or global("_音楽")
  local o = e.opts
  local ok, err = pcall(c.set, no, e.full, o.loop_start, o.loop_end, o.volume or 100, mod, true, e.rel)
  if not ok then rq.log("rq.music: " .. tostring(err)); return end
  once = { no = no, file = e.rel, mod = mod }
  preview = { mod = mod, no = no, back = back }
  seen = frames
  if not watch_off then
    -- The list stops being drawn (another page, Config closed): the game's music comes back.
    watch_off = rq.on_frame(function()
      frames = frames + 1
      if not preview then
        watch_off(); watch_off = nil
      elseif frames - seen > 3 then
        stop_preview()
      end
    end)
  end
  set_global("_音楽", NONE)
  from_mod = true
  ok, err = pcall(call, "音楽", no, -1, -1)
  from_mod = false
  if not ok then rq.log("rq.music: " .. tostring(err)) end
end


local function game_name(no)
  raw_names = true
  local ok, n = pcall(call, "音楽の名前", no)
  raw_names = false
  return ok and type(n) == "string" and n or ""
end

-- The rows of one mod: its tracks (by number) and the moments it named.
local function rows_of(mod, moments)
  local rows = {}
  local nos = {}
  for no, list in pairs(entries) do
    for _, e in ipairs(list) do
      if e.mod == mod then nos[#nos + 1] = no; break end
    end
  end
  table.sort(nos)
  for _, no in ipairs(nos) do
    local e
    for _, x in ipairs(entries[no]) do if x.mod == mod then e = x end end
    rows[#rows + 1] = { key = track_key(no), no = no, entry = e,
      text = string.format("%d  %s", no, e.opts.name or e.rel:match("([^/\\]+)$") or e.rel) }
  end
  for _, m in ipairs(moments or {}) do
    rows[#rows + 1] = { key = "moment:" .. m.key, text = m.name or m.key, tip = m.tip }
  end
  return rows
end

local function check_moments(m, fn)
  if m == nil then return {} end
  if type(m) ~= "table" then error(fn .. ": moments is a list of { key, name }", 3) end
  for i, x in ipairs(m) do
    if type(x) ~= "table" or type(x.key) ~= "string" then
      error(fn .. ": moments[" .. i .. "] needs a key (a string)", 3)
    end
    if x.name ~= nil and type(x.name) ~= "string" then
      error(fn .. ": moments[" .. i .. "].name must be a string", 3)
    end
  end
  return m
end

local function set_row(mod, r, on)
  set_on(mod, r.key, on)
  if r.no then
    apply(r.no)
    if not (preview and preview.no == r.no) then replay(r.no) end
  end
end

local PER_PAGE = 8

-- Draws the switches of one mod's pack. ui is rq.uikit; o: x, y, w, h, id, moments.
local function draw_list(ui, mod, o)
  -- Widget ids stay short: rqmod-ctl lists them as check-music-t52, button-music-play-52.
  local id = o.id or "music"
  local st = ui.state("musiclist:" .. mod .. ":" .. id)
  seen = frames
  st.page = st.page or 1
  local rows = rows_of(mod, o.moments)
  local w = o.w or 700
  local pages = math.max(1, math.ceil(#rows / PER_PAGE))
  if st.page > pages then st.page = pages end
  ui.col({ x = o.x, y = o.y, gap = 6 }, function()
    ui.row({ h = 40, gap = 12, align = "center" }, function()
      if ui.button("All on", { id = id .. ".all_on", w = 150 }) then
        for _, r in ipairs(rows) do set_row(mod, r, true) end
      end
      if ui.button("All off", { id = id .. ".all_off", w = 150 }) then
        for _, r in ipairs(rows) do set_row(mod, r, false) end
      end
      ui.label(#rows == 1 and "1 switch" or (#rows .. " switches"), { dim = true, h = 40 })
    end)
    if #rows == 0 then
      ui.label("Nothing to switch yet.", { dim = true })
    end
    local first = (st.page - 1) * PER_PAGE + 1
    for i = first, math.min(#rows, first + PER_PAGE - 1) do
      local r = rows[i]
      ui.row({ h = 40, gap = 12, align = "center" }, function()
        local on = is_on(mod, r.key)
        local v, changed = ui.checkbox(on, r.text, { id = id .. "." .. row_id(r.key), w = math.max(200, w - 330), tip = r.tip })
        if changed then set_row(mod, r, v) end
        if r.no then
          ui.label(game_name(r.no), { dim = true, w = 170, h = 40 })
          local playing = preview and preview.mod == mod and preview.no == r.no
          if ui.button(playing and "Stop" or "Play", { id = id .. ".play." .. r.no, w = 110 }) then
            if playing then stop_preview() else start_preview(mod, r.entry, r.no) end
          end
        end
      end)
    end
    if pages > 1 then
      ui.row({ h = 40, gap = 12, align = "center" }, function()
        if ui.button("<", { id = id .. ".prev", w = 60, disabled = st.page <= 1 }) then st.page = st.page - 1 end
        ui.label(string.format("Page %d / %d", st.page, pages), { w = 120, h = 40, align = "center" })
        if ui.button(">", { id = id .. ".next", w = 60, disabled = st.page >= pages }) then st.page = st.page + 1 end
      end)
    end
  end)
end

---A Config page for this mod's pack: a switch for each track it replaced and each moment it
---names, a play button, all on and off. 2.27.0.
function music.page(opts)
  if opts ~= nil and type(opts) ~= "table" then error("rq.music.page: expected a table of options", 2) end
  opts = opts or {}
  if opts.title ~= nil and type(opts.title) ~= "string" then error("rq.music.page: title must be a string", 2) end
  local moments = check_moments(opts.moments, "rq.music.page")
  if not rq.config then error("rq.music.page: rq.config is missing", 2) end
  local mod = mod_id()
  local title = opts.title
  if not title then
      title = mod .. ": music"
  end
  return rq.config.page({
    id = opts.id or (mod .. ":music"),
    title = title,
    draw = function(ui, box)
      draw_list(ui, mod, { x = box.x, y = box.y, w = box.w, h = box.h, moments = moments })
    end,
  })
end

local function music_list(opts)
  if type(opts) ~= "table" then error("rq.uikit.music_list: expected a table of options", 2) end
  local moments = check_moments(opts.moments, "rq.uikit.music_list")
  draw_list(rq.uikit, mod_id(), { x = opts.x, y = opts.y, w = opts.w, id = opts.id, moments = moments })
end

if rq.uikit then rq.uikit.music_list = music_list end

local function name_of(no)
  local ok, n = pcall(call, "音楽の名前", no)
  return ok and type(n) == "string" and n or ""
end

function music.info(no)
  track(no, "rq.music.info")
  local out = { no = no, name = name_of(no), exists = c.exists(no) == true }
  local r = c.get(no)
  if r then
    out.file, out.mod, out.volume = r.file, r.mod, r.volume
    out.loop_start, out.loop_end, out.seconds = r.loop_start, r.loop_end, r.seconds
  elseif out.exists then
    out.seconds = c.game_seconds(no)
  end
  return out
end

function music.current()
  local no = global("_音楽")
  if no <= 0 then return nil end
  local file, mod, pos, len = c.playing(no)
  if file == nil then return nil end
  return {
    no = no,
    name = name_of(no),
    file = file ~= "" and file or nil,
    mod = file ~= "" and mod or nil,
    pos = pos,
    seconds = len,
  }
end

function music.play(no, opts)
  track(no, "rq.music.play")
  if opts ~= nil and type(opts) ~= "table" then error("rq.music.play: expected a table of options", 2) end
  opts = opts or {}
  if not c.exists(no) then error("rq.music.play: the game has no music " .. no, 2) end
  if opts.restart and global("_音楽") == no then set_global("_音楽", NONE) end
  from_mod = true
  local ok, err = pcall(call, opts.once and "音楽１回再生" or "音楽", no, -1, -1)
  from_mod = false
  if not ok then error(err, 0) end
end

function music.stop()
  from_mod = true
  local ok, err = pcall(call, "音楽", 0, -1, -1)
  from_mod = false
  if not ok then error(err, 0) end
end

function music.on_play(fn)
  if type(fn) ~= "function" then error("rq.music.on_play: expected a function", 2) end
  local cell = { fn = fn }
  cbs[#cbs + 1] = cell
  if not hooked then install() end
  return function() cell.fn = nil end
end

-- ---- rq.battle.start's music (2.36.0) ----

-- Checks a fight's music while the mod that asked is on the stack: a game track, or a file in
-- its folder. Returns what fight_music takes; raises for a track the game has not, a path
-- outside the mod's folder, or a file that is not a sound.
priv.fight_music_check = function(v)
  if math.type(v) == "integer" then
    if v < 1 or not c.exists(v) then error("the game has no music " .. v, 0) end
    return { track = v }
  end
  local mod = mod_id()
  local full = c.resolve(v)
  c.set(51, full, nil, nil, 100, mod, true, v) -- the file is checked now
  c.clear_once()
  return { full = full, rel = v, mod = mod }
end

-- Set before the fight, nil after it.
priv.fight_music = function(spec)
  fight_music = spec
  if spec and not hooked then install() end
end
