-- rq.skilllist: the Potential Skills list in pages (loader 0.2.0; Lua since 2.18.0).
--
-- The game keeps the skills a character can buy in three arrays of 61 (rows 1 to 60) and cuts the
-- list at 36. This module makes the arrays longer and shows the list 36 at a time:
--   rq.ain.set_global_len    取得可能スキル, 取得可能スキルポイント and 取得不可能スキル説明 to 145
--                            (rows 1 to 144), grown again whenever they are shorter (a save made
--                            before, the title), so the game's own reads and the buy work on them
--   rq.ain.patch_int_after   スキルポン stops at row 145 instead of 54
--   before Ｔ購入スキル登録     rows 61 to 144 emptied, as the game empties rows 1 to 60
--   before T_SkillPoint@create        the window opens: page 1, then on_open
--   before / after キャラスキル_追加一覧  while the game builds the window's list, 取得可能スキル_数
--                            and _番号 answer the page's rows (36 at most, so the game's cut at 36
--                            never cuts); the game then sorts them with compSkill as before
--   after T_SkillPoint@delete         rows 61 to 144 emptied, so a save holds only what the game's
--                            own 60 rows would
-- The page buttons are a mod's own rq.gamebutton entries ("skill-window"). install() without
-- buttons = false adds two of them for the mod, Previous Page and Next Page, as the engine's C did.
local rq = ...
local ain = rq.ain
local hook, g, set, call = ain.hook, ain.global, ain.set_global, ain.call

local skilllist = {}
rq.skilllist = skilllist

local SIZE = 145          -- rows 1 to 144; row 0 is not used, as in the game
local GAME_ROWS = 61      -- the game's own size
local PAGE = 36
local ARRAYS = { "取得可能スキル", "取得可能スキルポイント", "取得不可能スキル説明" }

local installed, buttons = false, false
local page, pages = 1, 1
local built_for = nil -- the character the page belongs to
local slicing, slice = false, nil
local cbs = { open = {}, page = {}, buttons = {} }

local function logf(fmt, ...)
  rq.log("rq.skilllist: " .. string.format(fmt, ...))
end

local function add_cb(list, fn, where)
  if type(fn) ~= "function" then error("rq.skilllist." .. where .. ": expected a function", 3) end
  local cell = { fn = fn }
  list[#list + 1] = cell
  return function()
    for i, c in ipairs(list) do
      if c == cell then
        table.remove(list, i)
        return
      end
    end
  end
end

local function run(list, what, ...)
  local answer
  for _, c in ipairs({ table.unpack(list) }) do
    local ok, r = pcall(c.fn, ...)
    if not ok then
      logf("%s: %s", what, tostring(r))
    elseif r ~= nil then
      answer = r
    end
  end
  return answer
end

-- ---- The arrays ----

local function grow()
  for _, name in ipairs(ARRAYS) do
    local ok, n = pcall(ain.global_len, name)
    if not ok then
      logf("%s: %s", name, tostring(n))
    elseif n < SIZE then
      ain.set_global_len(name, SIZE)
    end
  end
end

local function clear_extra()
  for _, name in ipairs(ARRAYS) do
    local ok, n = pcall(ain.global_len, name)
    if ok then
      local empty = name == "取得不可能スキル説明" and "" or 0
      for i = GAME_ROWS, n - 1 do
        if g(name, i) ~= empty then set(name, i, empty) end
      end
    end
  end
end

-- ---- The page ----

local function owned(chara, skill)
  local r = call("キャラスキル_保有中", chara, skill)
  return r == true or (type(r) == "number" and r ~= 0)
end

-- The game's own filter in キャラスキル_追加一覧: a row with a skill, not panel color 104, not held.
local function build(chara)
  -- Another character in the open window (its scroll bar): that list from page 1 (2.22.1).
  if chara ~= built_for then page = 1 end
  built_for = chara
  local rows = {}
  local n = ain.global_len("取得可能スキル") - 1
  for i = 1, n do
    local skill = g("取得可能スキル", i)
    if skill > 0 and call("スキル_パネル色", skill) ~= 104 and not owned(chara, skill) then
      rows[#rows + 1] = skill
    end
  end
  pages = math.max(1, (#rows + PAGE - 1) // PAGE)
  if page < 1 then page = 1 end
  if page > pages then page = pages end
  slice = {}
  for i = (page - 1) * PAGE + 1, math.min(#rows, page * PAGE) do slice[#slice + 1] = rows[i] end
end

-- The page `dir` away (-1, +1, or 0 for this one), as on_page decides; nil to deny.
local function page_for(dir)
  if #cbs.page == 0 then
    local p = dir == 0 and page or page + dir
    if p < 1 or p > pages then return nil end
    return p
  end
  local answer
  for _, c in ipairs({ table.unpack(cbs.page) }) do
    local ok, r = pcall(c.fn, { dir = dir, page = page, pages = pages })
    if not ok then
      logf("on_page: %s", tostring(r))
    elseif type(r) == "number" then
      answer = r >= 1 and math.floor(r) or false
    elseif r ~= nil then
      answer = false
    end
  end
  return answer or nil
end

-- ---- The window ----

local WIN = { "g_sSkillPoint" }

local function chara_shown()
  local ok, n = pcall(ain.method, { "g_sSkillPoint", "m_sCard" }, "T_StatusCard@getCharaNo")
  return ok and type(n) == "number" and n > 0 and n or nil
end

function skilllist.refresh()
  local chara = chara_shown()
  if not chara then return false end
  pcall(ain.method, WIN, "T_SkillPoint@hideComment", -1)
  local ok, r = pcall(ain.method, WIN, "T_SkillPoint@createSkill", chara, -1)
  if not ok or not r then
    if not ok then logf("refresh: %s", tostring(r)) end
    return false
  end
  pcall(ain.method, WIN, "T_SkillPoint@setShow", true)
  return true
end

-- ---- The old page buttons: two "skill-window" entries for the mod that asks ----

local function add_buttons()
  buttons = true
  local function go(dir)
    local p = page_for(dir)
    if not p then return end
    page = p
    skilllist.refresh()
  end
  local ids = {}
  for _, b in ipairs({ { "skilllist-prev", "Previous Page", -1 }, { "skilllist-next", "Next Page", 1 } }) do
    local h = rq.gamebutton.add({ group = "skill-window", id = b[1], label = b[2],
      enabled = function() return page_for(b[3]) ~= nil end,
      on_click = function() go(b[3]) end })
    ids[#ids + 1] = h.id()
  end
  hook("T_NewSkillManager@create", { after = function(h)
    if not h.result or #cbs.buttons == 0 then return end
    local got = {}
    for _, gr in ipairs(rq.gamebutton.groups()) do
      if gr.name == "skill-window" then
        for _, e in ipairs(gr.entries) do got[e.id] = e.button or 0 end
      end
    end
    run(cbs.buttons, "on_buttons", got[ids[1]] or 0, got[ids[2]] or 0)
  end })
end

-- ---- install ----

function skilllist.install(opts)
  if opts ~= nil and type(opts) ~= "table" then error("rq.skilllist.install: opts must be a table", 2) end
  local want_buttons = not (opts and opts.buttons == false)
  if installed then
    if want_buttons and not buttons then add_buttons() end
    return true
  end
  -- The arrays are the game's state, there only once the game runs: they grow in the hooks.
  local n = ain.patch_int_after("スキルポン", "◆取得可能スキル番号", 54, SIZE)
  if n ~= 1 then error("rq.skilllist.install: スキルポン's stop at 54 not found (" .. n .. ")", 2) end
  installed = true

  hook("Ｔ購入スキル登録", { before = function()
    grow()
    clear_extra()
  end })
  hook("T_SkillPoint@create", { before = function()
    page = 1
    built_for = nil
    run(cbs.open, "on_open")
  end })
  hook("キャラスキル_追加一覧", {
    before = function(h)
      local ok, err = pcall(build, h.args[1])
      if ok then
        slicing = true
      else
        logf("the page: %s", tostring(err))
      end
    end,
    after = function() slicing = false end,
  })
  hook("取得可能スキル_数", { before = function(h)
    if slicing then
      h.skip = true
      h.result = #slice
    end
  end })
  hook("取得可能スキル_番号", { before = function(h)
    if slicing then
      h.skip = true
      h.result = slice[h.args[1]] or -1
    end
  end })
  hook("T_SkillPoint@delete", { after = function() clear_extra() end })

  if want_buttons then add_buttons() end
  logf("on (%d rows, %d per page, buttons %s)", SIZE - 1, PAGE, want_buttons and "Previous / Next" or "the mod's")
  return true
end

local function need()
  if not installed then error("call rq.skilllist.install first", 3) end
end

function skilllist.on_open(fn) return add_cb(cbs.open, fn, "on_open") end
function skilllist.on_page(fn) return add_cb(cbs.page, fn, "on_page") end
function skilllist.on_buttons(fn) return add_cb(cbs.buttons, fn, "on_buttons") end

function skilllist.set_page(p)
  if type(p) ~= "number" then error("rq.skilllist.set_page: page must be a number", 2) end
  need()
  page = math.max(1, math.floor(p))
end

function skilllist.page()
  need()
  return page
end

function skilllist.page_count()
  need()
  return pages
end
