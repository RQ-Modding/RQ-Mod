-- rq.music (loader 2.4.0): a mod's file plays where the game plays one of its music tracks.
--
-- The C half (modloader_music.c) keeps the files and opens them when the game prepares a music
-- channel. This half is the API and three hooks:
--   SYS_PlayBGM(no, loops, effect, fade, volume)  every music change of the game goes through it.
--       before: on_play callbacks; a file for this time only; a restart when the track already
--       plays but with another file than it should now. after: a once file nobody used is dropped.
--   音楽_戦闘 / 迷宮音楽_再生                     tell on_play a fight's or a dungeon's track.
--   音楽の名前(no)                                a mod's name for a track (the music room).
-- SYS_PlayBGM returns at once when the number is _音楽 (the track playing); setting _音楽 to
-- the lowest integer makes it start the track again, as the game's own load does.
local rq, c = ...
local call, hook, global, set_global = rq.ain.call, rq.ain.hook, rq.ain.global, rq.ain.set_global

local music = {}
rq.music = music

local NONE = -2147483648
local names = {}          -- no -> { name, mod }
local cbs = {}            -- on_play callbacks
local kind = "other"
local hooked, name_hooked = false, false
local once                -- { no, file, mod } set by ctx:file in this SYS_PlayBGM
local unchecked = {}      -- no -> true: replaced before the game opened its music (init.lua)
local check_off

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
  if v ~= nil and (math.type(v) ~= "integer" or v < 0 or v > 100) then
    error(fn .. ": volume is a whole number, 0 to 100", 3)
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
Play.__index = Play

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

local function install()
  hooked = true
  hook("音楽_戦闘", { before = function() kind = "fight" end, after = function() kind = "other" end })
  hook("迷宮音楽_再生", { before = function() kind = "dungeon" end, after = function() kind = "other" end })
  hook("SYS_PlayBGM", {
    before = function(h)
      local no = h.args[1] or 0
      once = nil
      if no <= 0 then return end
      local playing = global("_音楽")
      local ctx = setmetatable({
        no = no,
        playing = playing,
        kind = kind,
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
      end
    end
    unchecked = {}
  end)
end

local function install_names()
  name_hooked = true
  hook("音楽の名前", { after = function(h)
    local n = names[h.args[1]]
    if n then h.result = n.name end
  end })
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
  local old = c.get(no)
  local info = c.set(no, path, opts.loop_start, opts.loop_end, opts.volume or 100, mod, false)
  if old and old.mod ~= mod then
    rq.log(string.format("rq.music: track %d: %s's '%s' replaces %s's '%s'", no, mod, path, old.mod, old.file))
  end
  if opts.name then
    names[no] = { name = opts.name, mod = mod }
    if not name_hooked then install_names() end
  elseif names[no] then
    names[no] = nil
  end
  if exists == nil then check_later(no) end
  if not hooked then install() end
  return info
end

function music.restore(no)
  track(no, "rq.music.restore")
  local r = c.get(no)
  if not r or r.mod ~= mod_id() then return false end
  if names[no] and names[no].mod == r.mod then names[no] = nil end
  return c.clear(no)
end

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
  call(opts.once and "音楽１回再生" or "音楽", no, -1, -1)
end

function music.stop()
  call("音楽", 0, -1, -1)
end

function music.on_play(fn)
  if type(fn) ~= "function" then error("rq.music.on_play: expected a function", 2) end
  local cell = { fn = fn }
  cbs[#cbs + 1] = cell
  if not hooked then install() end
  return function() cell.fn = nil end
end
