-- rq.scene.start and rq.scene.active (loader 1.14.0).
--
-- rq.on_frame runs every callback each frame, also while one of them waits in a game call,
-- as a scene does. So a mod that called rq.scene.play from on_frame, with no coroutine and no
-- flag, started the same scene again inside the first. start plays the scene in a coroutine
-- of its own at the next frame, and says false while a mod scene plays, so a mod can call it
-- from anywhere, every frame too. It waits while the System menu or Config is open: they
-- are drawn over the message window, and the System menu has Save and Load. It also waits
-- for a scene of the game's own to end: the game never plays one scene inside another, and
-- a mod scene's ADVOFF there would clear the game's background under its next line.
local rq, menu_open = ...
local scene = rq.scene
local play = scene.play
local depth, pending, running = 0, nil, nil

-- Something is drawn on the game's scene layer: a face, a background, a CG.
local function game_scene()
  return rq.ain.call("画面_表示中")
end

-- Counts the scenes rq.scene.play has open, once and start included.
function scene.play(fn, ...)
  depth = depth + 1
  local r = table.pack(pcall(play, fn, ...))
  depth = depth - 1
  if not r[1] then error(r[2], 0) end
  return table.unpack(r, 2, r.n)
end

function scene.active()
  return depth > 0 or pending ~= nil or running ~= nil
end

function scene.start(fn, ...)
  if type(fn) ~= "function" then error("rq.scene.start: expected a function", 2) end
  if scene.active() then return false end
  pending = table.pack(fn, ...)
  return true
end

local function step()
  local ok, err = coroutine.resume(running)
  if coroutine.status(running) == "dead" then
    running = nil
    if not ok then rq.log("rq.scene.start: " .. tostring(err)) end
  end
end

rq.on_frame(function()
  if running then
    -- A scene that yields goes on at the next frame; one that waits in the game is left.
    if coroutine.status(running) == "suspended" then step() end
    return
  end
  if not pending or menu_open() or game_scene() then return end
  local p = pending
  pending = nil
  running = coroutine.create(function() scene.play(p[1], table.unpack(p, 2, p.n)) end)
  step()
end)

-- The title ends the scene's coroutine under this callback, which then never gets back here.
rq.on_reset(function()
  depth, pending, running = 0, nil, nil
end)
