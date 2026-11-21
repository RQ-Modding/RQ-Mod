-- rq.sprite (loader 1.9.0): the game hooks that end every mod sprite.
--
-- Every load of a save runs セーブファイル_読込 (the System menu's Load, the title's Load and
-- Continue, and the suspend save): it closes the screens and reads the save, so the screen
-- a mod sprite was made for is gone. The title screen starts with T_Title@create. Both
-- remove all mod sprites, then call each rq.sprite.on_reset with "load" or "title".
--
-- rq.image.play_flash (loader 2.63.0) is here too: a Flash file played once on screen, as the
-- game plays its effects, is a sprite that shows it (rq.sprite.new's flash) and goes after its
-- last frame. The sprite is the calling mod's: rq.sprite.new names the nearest mod on the stack.
local rq, reset = ...
local hook = rq.ain.hook

hook("セーブファイル_読込", { before = function() reset("load") end })
hook("T_Title@create", { before = function() reset("title") end })

-- Options of play_flash that are the play's, not the sprite's.
local PLAY_KEYS = { loop = true, on_end = true, wait = true, from = true, to = true }

local function bad(msg)
  error("rq.image.play_flash: " .. msg, 3)
end

-- rq.sprite's own message, said by play_flash at the mod's line.
local function said(err)
  local text = tostring(err)
  text = text:gsub("^rq%.sprite%.new: ", ""):gsub("^rq%.sprite: ", "")
  return "rq.image.play_flash: " .. text
end

function rq.image.play_flash(key, opts)
  if type(key) ~= "string" then bad("key must be a string") end
  if opts ~= nil and type(opts) ~= "table" then bad("opts must be a table") end
  opts = opts or {}
  if opts.on_end ~= nil and type(opts.on_end) ~= "function" then bad("on_end must be a function") end
  -- rq.wait_until's own test, before anything is made: a job from rq.spawn, not another coroutine.
  -- A condition already true returns at once, without waiting.
  if opts.wait and not pcall(rq.wait_until, function() return true end) then
    bad("wait = true needs a job from rq.spawn")
  end
  local t = { flash = key }
  for k, v in pairs(opts) do
    if not PLAY_KEYS[k] then t[k] = v end
  end
  local ok, s = pcall(rq.sprite.new, t)
  if not ok then error(said(s), 2) end
  local done = false
  local user_end = opts.on_end
  local played, err = pcall(s.play, s, {
    from = opts.from,
    to = opts.to,
    loop = opts.loop and true or false,
    on_end = function(sp)
      done = true
      if user_end then
        local fine, e = pcall(user_end, sp)
        if not fine then rq.log("rq.image.play_flash on_end: " .. tostring(e)) end
      end
      if sp:alive() then sp:remove() end
    end,
  })
  if not played then
    s:remove()
    error(said(err), 2)
  end
  if opts.wait then
    rq.wait_until(function() return done or not s:alive() end)
  end
  return s
end
