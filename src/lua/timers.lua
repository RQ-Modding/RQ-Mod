-- Timers and waiting (loader 2.33.0):
--   rq.after(ms, fn)          fn once, ms of real time from now
--   rq.every(ms, fn)          fn every ms; false from fn stops it
--   rq.spawn(fn, ...)         fn as a coroutine the loader drives; a handle with done/ok/value/error
--   rq.wait(ms)               in a job: at least one update, then until ms have passed
--   rq.wait_until(cond, ms)   in a job: until cond() gives a true value, or ms have passed
--
-- One rq.on_frame callback drives them all, on rq.time_ms (real time: rq.speed does not change it).
-- A coroutine that waits inside one of the game's calls (rq.ui.confirm, rq.battle.start) runs the
-- game's own loop, and rq.on_frame keeps running in it: such a coroutine is "normal", not
-- "suspended", so it is left until the call returns, as scene.lua does with its scene.
local rq = ...

local function logf(fmt, ...)
  rq.log(string.format(fmt, ...))
end

local function mod_id()
  local ok, id = pcall(function() return rq.mod and rq.mod.id and rq.mod.id() end)
  return ok and id or "?"
end

local function check_ms(ms, name, positive)
  if type(ms) ~= "number" or ms ~= ms or ms < 0 or (positive and ms <= 0) then
    error(name .. ": the time must be a number of milliseconds" .. (positive and ", above 0" or ", 0 or more"), 3)
  end
end

local function check_fn(fn, name)
  if type(fn) ~= "function" then error(name .. ": expected a function", 3) end
end

local function once(t, what, err)
  local text = tostring(err)
  if not t.seen[text] then
    t.seen[text] = true
    logf("%s (%s): %s", what, t.mod, text)
  end
end

-- ---- rq.after and rq.every ----

local timers = {}

local function add_timer(t)
  timers[#timers + 1] = t
  return function() t.dead = true end
end

function rq.after(ms, fn)
  check_ms(ms, "rq.after")
  check_fn(fn, "rq.after")
  return add_timer({ at = rq.time_ms() + ms, fn = fn, mod = mod_id(), seen = {} })
end

function rq.every(ms, fn)
  check_ms(ms, "rq.every", true)
  check_fn(fn, "rq.every")
  return add_timer({ at = rq.time_ms() + ms, period = ms, fn = fn, mod = mod_id(), seen = {} })
end

local function run_timers()
  if #timers == 0 then return end
  for _, t in ipairs({ table.unpack(timers) }) do
    -- Checked at the moment it fires: a game call in another timer may have run this one already.
    local now = rq.time_ms()
    if not t.dead and not t.busy and now >= t.at then
      if t.period then
        t.at = t.at + t.period
        if t.at <= now then t.at = now + t.period end
      else
        t.dead = true
      end
      t.busy = true
      local ok, r = pcall(t.fn)
      t.busy = false
      if not ok then
        once(t, t.period and "rq.every" or "rq.after", r)
      elseif t.period and r == false then
        t.dead = true
      end
    end
  end
  local live = {}
  for _, t in ipairs(timers) do
    if not t.dead then live[#live + 1] = t end
  end
  timers = live
end

-- ---- rq.spawn, rq.wait and rq.wait_until ----

local jobs = {}  -- coroutine -> the handle of a job from rq.spawn
local waits = {} -- coroutine -> { at = ms } or { cond = true }: waiting in rq.wait / wait_until
local order = {} -- the coroutines above, in the order they started waiting or were spawned

local function track(co)
  for _, c in ipairs(order) do
    if c == co then return end
  end
  order[#order + 1] = co
end

local function finish(co, h, ok, ...)
  jobs[co] = nil
  waits[co] = nil
  if not h then return end
  h.done = true
  h.ok = ok
  if ok then
    h.values = table.pack(...)
    h.value = h.values[1]
  else
    h.error = ...
  end
end

local function resume(co)
  local h = jobs[co]
  local r = table.pack(coroutine.resume(co))
  if coroutine.status(co) == "dead" then
    if r[1] then
      finish(co, h, true, table.unpack(r, 2, r.n))
    else
      local err = r[2]
      logf("rq.spawn (%s): %s", h and h.mod or "?", rq.traceback(co, tostring(err)))
      finish(co, h, false, err)
    end
  end
end

function rq.spawn(fn, ...)
  check_fn(fn, "rq.spawn")
  local co = coroutine.create(fn)
  local h = { done = false, mod = mod_id() }
  function h.cancel()
    if h.done then return end
    h.cancelled = true
  end
  jobs[co] = h
  track(co)
  local args = table.pack(...)
  local r = table.pack(coroutine.resume(co, table.unpack(args, 1, args.n)))
  if coroutine.status(co) == "dead" then
    if r[1] then
      finish(co, h, true, table.unpack(r, 2, r.n))
    else
      logf("rq.spawn (%s): %s", h.mod, rq.traceback(co, tostring(r[2])))
      finish(co, h, false, r[2])
    end
  end
  return h
end

-- Only a job from rq.spawn: a coroutine that something else drives (a scene, a mod's own) would be
-- resumed from two places.
local function current(name)
  local co, main = coroutine.running()
  if main or not jobs[co] or not coroutine.isyieldable() then
    error(name .. ": call it inside a job from rq.spawn", 3)
  end
  return co
end

function rq.wait(ms)
  check_ms(ms, "rq.wait")
  local co = current("rq.wait")
  local stop = rq.time_ms() + ms
  track(co)
  repeat
    waits[co] = { at = stop }
    coroutine.yield()
  until rq.time_ms() >= stop
  waits[co] = nil
end

function rq.wait_until(cond, limit)
  check_fn(cond, "rq.wait_until")
  if limit ~= nil then check_ms(limit, "rq.wait_until") end
  local co = current("rq.wait_until")
  local stop = limit and rq.time_ms() + limit
  track(co)
  while true do
    local v = cond()
    if v then waits[co] = nil return v end
    if stop and rq.time_ms() >= stop then waits[co] = nil return nil end
    waits[co] = { cond = true }
    coroutine.yield()
  end
end

local function run_jobs()
  if #order == 0 then return end
  local now = rq.time_ms()
  for _, co in ipairs({ table.unpack(order) }) do
    local h, w = jobs[co], waits[co]
    if coroutine.status(co) == "dead" then
      jobs[co], waits[co] = nil, nil
    elseif coroutine.status(co) == "suspended" then
      if h and h.cancelled then
        if coroutine.close then pcall(coroutine.close, co) end
        finish(co, h, false, "cancelled")
      elseif w then
        if w.cond or now >= w.at then resume(co) end
      elseif h then
        -- A job that yields without rq.wait goes on at the next update.
        resume(co)
      end
    end
  end
  local live = {}
  for _, co in ipairs(order) do
    if coroutine.status(co) ~= "dead" and (jobs[co] or waits[co]) then live[#live + 1] = co end
  end
  order = live
end

local running = false
rq.on_frame(function()
  -- A game call in a timer or a job runs the game's loop, which runs this callback again: the
  -- nested run fires what is due, and the checks above keep anything from running twice.
  if running then
    run_timers()
    run_jobs()
    return
  end
  running = true
  local ok, err = pcall(function()
    run_timers()
    run_jobs()
  end)
  running = false
  if not ok then logf("rq timers: %s", tostring(err)) end
end)
