-- rq.statuspage (loader 2.20.0): a character's status page for mods.
--
-- The page is g_sStatus (T_Status, the only one). ステータス_実行 runs T_Status::run: create, its own
-- loop, delete. T_Status::setChara draws the card (T_StatusCard::set) on open, on a scroll to another
-- character, after every equip, skill or profile change and when the skill window closes.
-- Built on public calls only:
--   after T_Status@setChara   open (the first draw), another character, and every draw
--   after T_Status@run        the page closed (T_Status@delete also runs inside create)
--   T_Status@setChara         refresh(), through rq.ain.method
local rq = ...
local method, g, call = rq.ain.method, rq.ain.global, rq.ain.call

local page = {}
rq.statuspage = page

local MODES = { [0] = "party", [11] = "party-edit", [12] = "party-store", [20] = "battle-result" }

local cbs = { open = {}, chara = {}, update = {}, close = {} }
local state = { open = false, chara = nil, mode = nil, drawing = false }
local hooked = false

local function logf(fmt, ...)
  rq.log("rq.statuspage: " .. string.format(fmt, ...))
end

local function card_chara()
  local ok, n = pcall(method, { "g_sStatus", "m_sCard" }, "T_StatusCard@getCharaNo")
  return ok and math.type(n) == "integer" and n > 0 and n or nil
end

local function fire(kind, ...)
  for _, fn in ipairs(cbs[kind]) do
    local ok, err = pcall(fn, ...)
    if not ok then logf("on_%s: %s", kind, tostring(err)) end
  end
end

local function install()
  if hooked then return end
  hooked = true
  rq.ain.hook("T_Status@setChara", { after = function(h)
    if not h.result then return end
    local n = card_chara()
    if not n then return end
    state.drawing = true
    if not state.open then
      state.open = true
      state.mode = page.mode()
      state.chara = n
      fire("open", n, state.mode)
    elseif n ~= state.chara then
      local old = state.chara
      state.chara = n
      fire("chara", n, old)
    end
    fire("update", n)
    state.drawing = false
  end })
  rq.ain.hook("T_Status@run", { after = function()
    if not state.open then return end
    state.open, state.chara, state.mode, state.drawing = false, nil, nil, false
    fire("close")
  end })
end

local function on(kind, where)
  return function(fn)
    if type(fn) ~= "function" then error("rq.statuspage." .. where .. ": expected a function", 2) end
    cbs[kind][#cbs[kind] + 1] = fn
    install()
    return function()
      for i, f in ipairs(cbs[kind]) do
        if f == fn then table.remove(cbs[kind], i) return end
      end
    end
  end
end

page.on_open = on("open", "on_open")
page.on_chara = on("chara", "on_chara")
page.on_update = on("update", "on_update")
page.on_close = on("close", "on_close")

-- The page is open while T_Status's mode is set (create sets it; delete clears it to -1).
local function game_mode()
  local ok, m = pcall(g, "g_sStatus", "m_nMode")
  return ok and math.type(m) == "integer" and m >= 0 and m or nil
end

function page.chara()
  if not game_mode() then return nil end
  return card_chara()
end

function page.mode()
  local m = game_mode()
  return m and MODES[m] or nil
end

function page.refresh()
  install()
  if state.drawing then return false end
  local n = page.chara()
  if not n then return false end
  -- Hidden under the skill window (the game draws the card again when it closes), or a skill
  -- being moved (the page's lock mode).
  local ok, shown = pcall(g, "g_sStatus", "m_sCard", "m_bShow")
  if not ok or not shown then return false end
  local ok2, lock = pcall(g, "g_sStatus", "m_sCard", "m_nLockMode")
  if not ok2 or lock ~= 0 then return false end
  local ok3, r = pcall(method, { "g_sStatus" }, "T_Status@setChara", n, true)
  if not ok3 then
    logf("refresh: %s", tostring(r))
    return false
  end
  -- The item list beside the page, as T_Status::scroll rebuilds it.
  local ok4, items = pcall(g, "g_sStatus", "m_bItemOpen")
  if ok4 and items then
    pcall(call, "アイテム倉庫_再作成", -1, -1, -1, -1, -1)
    local ok5, kind = pcall(call, "アイテム倉庫_アイテム種別")
    if ok5 then
      local idx = call("ステータス_アイテムインデクス", kind)
      pcall(method, { "g_sStatus" }, "T_Status@showItemComment", idx)
    end
  end
  return r ~= false
end

rq.on_reset(function()
  state.open, state.chara, state.mode, state.drawing = false, nil, nil, false
end)
