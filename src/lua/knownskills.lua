-- rq.knownskills: more known skills than the game's 17, shown in pages (loader 2.54.0).
--
-- Each character's skills are the rows of Ｃ[iic].スキル, 21 in the game: row 0 unused, rows 1 to 18
-- for 17 skills and the item effect skill, rows 19 and 20 walked but never filled. With `most`
-- known skills, the arrays have most + 4 rows and new skills take rows 1 to most + 1:
--   priv.patch_imm         the 21 of sＣ's constructor (every character made from now on has the
--                          longer array, at boot too: mods load before the game runs), the 21 of
--                          the 16 row accessors and of every loop over the rows, the cap (19 in
--                          ＣＳ空き番号取得), the bar's last row (20 in ＣＳ空き実装番号取得), the
--                          battle's loop (20 in Ｔ戦闘前初期化／キャラ) and the free-row check's 18
--                          (キャラスキル_空き確認). priv.skill_cap is that 18 for rq.chara.skill_room.
--   before セーブデータ_保存  rows past 18 go into rq.savedata (the loader's own key), and the arrays
--                          back to 21 with rows 19 and 20 empty, so the game writes its own save;
--   after                  the arrays grow and the rows come back
--   after セーブデータ_読み込み the arrays grow (a save holds 21) and the rows come back from it
--   キャラスキル_保有一覧     the list both screens show (the status page's card and the skill
--                          window's): 17 known skills and the item effect skill last. While the game
--                          builds it, キャラ_スキル数, キャラ_スキル番号 and キャラ_スキル保有 answer the
--                          page's rows; the game then sorts them with compSkill as before.
-- The page controls (install's controls, default on) are drawn for the mod that installs: a slim
-- scroll bar beside the status page's known skills, the wheel over them, and two arrows beside the
-- skill window's KNOWN SKILLS heading; Page Up and Page Down on both screens (2.57.0; the game reads
-- Page Up only in a message's wait, for the backlog, and both in Config's sliders).
local rq, priv = ...
local ain = rq.ain
local hook, g, set, call, len, set_len = ain.hook, ain.global, ain.set_global, ain.call, ain.global_len,
  ain.set_global_len

local ks = {}
rq.knownskills = ks

local GAME_SIZE, GAME_CAP, GAME_MOST = 21, 18, 17
local PAGE = 17
local MOST_MAX = 102 -- six pages
local ITEM_COLOR = 104 -- スキル_パネル色 of the item effect skills
local ITEM_SLOT = 569 -- the empty item effect slot: キャラ_スキル番号 answers 0 for it, so no list shows it
local FIELDS = { "番号", "保有", "実装中", "コスト", "使用禁止", "使用回数", "使用回数最大", "ランク",
  "戦闘使用済み", "確率", "条件", "一度使用", "表示文", "エフェクト", "モーション", "状態" }
local TEXT = { ["表示文"] = true, ["エフェクト"] = true }
local SAVE_MOD, SAVE_KEY = "rq:loader", "known_skills"

local installed = false
local most, size, cap = GAME_MOST, GAME_SIZE, GAME_CAP
local page, pages, built_for = 1, 1, nil
local slicing, slice = false, nil
local stash = nil -- the rows taken out for the save being written
local left = nil -- rows a lower setting has no room for, kept for the next save in the same shape
local sw_open = false

local function logf(fmt, ...)
  rq.log("rq.knownskills: " .. string.format(fmt, ...))
end

-- ---- The game's numbers ----

-- { function, local (nil: a plain PUSH), the game's number, the new one as f(size, cap), count }
local PATCHES = {
  { "sＣ@0", nil, 21, function(s) return s end, 1 },
  { "ＣＳ空き番号取得", "iic_s", 19, function(_, c) return c + 1 end, 1 },
  { "ＣＳ空き実装番号取得", "iic_s", 21, function(s) return s end, 1 },
  { "ＣＳ空き実装番号取得", nil, 20, function(s) return s - 1 end, 1 },
  { "キャラスキル_空き確認", nil, 18, function(_, c) return c end, 1 },
  { "Ｔ戦闘前初期化／キャラ", "loop", 20, function(s) return s - 1 end, 1 },
  { "恋人仙人／スキル禁止処理", "cs", 21, function(s) return s end, 2 },
}
for _, name in ipairs({ "Ｃスキル番号", "Ｃスキル状態", "Ｃスキル確率", "Ｃスキル条件", "Ｃスキル一度使用",
  "Ｃスキルモーション", "Ｃスキル使用回数", "Ｃスキル使用回数最大", "Ｃスキル戦闘使用済み", "Ｃスキルランク",
  "Ｃスキル実装中", "Ｃスキル使用禁止", "Ｃスキル保有", "Ｃスキルコスト", "Ｃスキル表示文", "Ｃスキルエフェクト" }) do
  PATCHES[#PATCHES + 1] = { name, "no", 21, function(s) return s end, 1 }
end
for _, p in ipairs({ { "ＣＳ全初期化", "iic_s" }, { "Ｃ再計算", "loop" }, { "Ｃ補給／スキル", "loop" },
  { "Ｃ補給／基本スキル", "loop" }, { "ＣＳ全実装解除", "iic_s" }, { "ＣＳ全使用済み", "iic_s" },
  { "ＣＳ全使用禁止", "iic_s" }, { "Ｃスキル強制実装Ａ", "iic_s" }, { "ＣＳ番号取得", "iic_s" },
  { "ＣＳ有無／機能", "iic_s" }, { "ＣＳ使用可能なスキルの有無", "iic_s" }, { "ＣＳランダムで使用済みにする", "iic_s" },
  { "ＣＳ残行動回数取得", "iic_s" }, { "Ｃスキルドレイン", "loop" }, { "Ｔ思考ルーチン", "iic_s" },
  { "ＣＳ使用禁止切り替え", "iic_s" }, { "クエスト処理／サイコロ", "j" }, { "関数／サイコロ／ヒーリング使用", "j" } }) do
  PATCHES[#PATCHES + 1] = { p[1], p[2], 21, function(s) return s end, 1 }
end

-- Every number is first counted (a patch to the same value changes nothing); only when each is
-- where the game's code has it are they changed, so a different AIN is left as it is.
local function patch_all()
  for _, p in ipairs(PATCHES) do
    local ok, n = pcall(priv.patch_imm, p[1], p[2], p[3], p[3])
    if not ok or n ~= p[5] then
      return false, string.format("%s: %s %s found %s times, not %d", p[1], p[2] or "PUSH", p[3],
        ok and tostring(n) or tostring(n), p[5])
    end
  end
  for _, p in ipairs(PATCHES) do
    local to = p[4](size, cap)
    if to ~= p[3] then priv.patch_imm(p[1], p[2], p[3], to) end
  end
  return true
end

-- ---- The arrays ----

local function charas()
  local ok, n = pcall(len, "Ｃ")
  return ok and n or 0
end

local function grow_all()
  for iic = 0, charas() - 1 do
    if len("Ｃ", iic, "スキル") < size then set_len("Ｃ", iic, "スキル", size) end
  end
end

local function clear_row(iic, r)
  for _, f in ipairs(FIELDS) do
    local empty = TEXT[f] and "" or 0
    if g("Ｃ", iic, "スキル", r, f) ~= empty then set("Ｃ", iic, "スキル", r, f, empty) end
  end
end

local function row_empty(iic, r)
  for _, f in ipairs(FIELDS) do
    if g("Ｃ", iic, "スキル", r, f) ~= (TEXT[f] and "" or 0) then return false end
  end
  return true
end

-- Rows past 18 of every character as { [iic] = { {row, field values in FIELDS' order}, ... } }.
local function take_rows()
  local all = {}
  for iic = 0, charas() - 1 do
    local n = len("Ｃ", iic, "スキル")
    local rows
    for r = GAME_CAP + 1, n - 1 do
      if not row_empty(iic, r) then
        local row = { r }
        for i, f in ipairs(FIELDS) do row[i + 1] = g("Ｃ", iic, "スキル", r, f) end
        rows = rows or {}
        rows[#rows + 1] = row
      end
    end
    if rows then all[tostring(iic)] = rows end
  end
  return all
end

local function holds(iic, skill)
  for r = 1, len("Ｃ", iic, "スキル") - 1 do
    if g("Ｃ", iic, "スキル", r, "番号") == skill then return true end
  end
  return false
end

-- Puts the rows back. A row past this cap (the setting lowered) or over a row in use goes to the
-- first empty row; with none, it stays in `left` and goes into the next save again, so a higher
-- setting brings it back. A skill the character holds again by then is dropped.
local function put_rows(all)
  grow_all()
  left = nil
  if type(all) ~= "table" then return end
  local kept, dropped = 0, 0
  for key, rows in pairs(all) do
    local iic = tonumber(key)
    if math.type(iic) == "integer" and iic >= 0 and iic < charas() and type(rows) == "table" then
      for _, row in ipairs(rows) do
        local r = type(row) == "table" and row[1]
        local skill = type(row) == "table" and row[2]
        if math.type(r) ~= "integer" or math.type(skill) ~= "integer" or r < 1 then
          dropped = dropped + 1
        elseif skill > 0 and (r > cap or not row_empty(iic, r)) and holds(iic, skill) then
          dropped = dropped + 1
        else
          if r > cap or not row_empty(iic, r) then
            r = nil
            for i = 1, cap do
              if row_empty(iic, i) then r = i break end
            end
          end
          if r then
            for i, f in ipairs(FIELDS) do
              local v = row[i + 1]
              if v ~= nil then set("Ｃ", iic, "スキル", r, f, v) end
            end
          else
            left = left or {}
            left[key] = left[key] or {}
            left[key][#left[key] + 1] = row
            kept = kept + 1
          end
        end
      end
    end
  end
  if kept > 0 then
    logf("%d skill row(s) do not fit in %d known skills: kept beside the save for a higher setting", kept, most)
  end
  if dropped > 0 then logf("%d skill row(s) left out (held again, or not a row)", dropped) end
end

-- take_rows and the rows waiting in `left`, for the save.
local function save_rows()
  local all = take_rows()
  for key, rows in pairs(left or {}) do
    all[key] = all[key] or {}
    for _, row in ipairs(rows) do all[key][#all[key] + 1] = row end
  end
  return all
end

local function saved(fn)
  local ok, r = pcall(priv.as_mod, SAVE_MOD, fn)
  if not ok then logf("savedata: %s", tostring(r)) end
  return ok and r or nil
end

-- ---- The pages ----

-- The held skills as the game lists them: known ones sorted by compSkill (panel color, then
-- number), the item effect skill apart, the empty item slot (569) left out.
local function held(iic)
  local known, item = {}, nil
  for r = 1, len("Ｃ", iic, "スキル") - 1 do
    local s = g("Ｃ", iic, "スキル", r, "番号")
    if s > 0 and s ~= ITEM_SLOT and g("Ｃ", iic, "スキル", r, "保有") ~= 0 then
      local color = call("スキル_パネル色", s)
      if color == ITEM_COLOR then
        item = s
      else
        known[#known + 1] = { s = s, c = color }
      end
    end
  end
  table.sort(known, function(a, b)
    if a.c ~= b.c then return a.c < b.c end
    return a.s < b.s
  end)
  return known, item
end

-- The page of iic's list; a slice only when there is more than one page.
local function build(iic)
  if iic ~= built_for then page = 1 end
  built_for = iic
  local known, item = held(iic)
  pages = math.max(1, (#known + PAGE - 1) // PAGE)
  page = math.min(math.max(page, 1), pages)
  if pages == 1 then return nil end
  local out = {}
  for i = (page - 1) * PAGE + 1, math.min(#known, page * PAGE) do out[#out + 1] = known[i].s end
  if item then out[#out + 1] = item end
  return out
end

local function status_shown()
  local n = rq.statuspage.chara()
  if not n then return nil end
  local ok, shown = pcall(g, "g_sStatus", "m_sCard", "m_bShow")
  if not ok or not shown then return nil end
  local ok2, lock = pcall(g, "g_sStatus", "m_sCard", "m_nLockMode")
  if not ok2 or lock ~= 0 then return nil end
  return n
end

local function window_chara()
  if not sw_open then return nil end
  local ok, n = pcall(ain.method, { "g_sSkillPoint", "m_sCard" }, "T_StatusCard@getCharaNo")
  return ok and math.type(n) == "integer" and n > 0 and n or nil
end

-- Draws the page again where it is shown: the skill window, else the status page.
local function redraw()
  if window_chara() then
    pcall(ain.method, { "g_sSkillPoint" }, "T_SkillPoint@hideComment", -1)
    local ok, r = pcall(ain.method, { "g_sSkillPoint" }, "T_SkillPoint@createSkill", window_chara(), -1)
    if not ok then logf("the skill window: %s", tostring(r)) return false end
    pcall(ain.method, { "g_sSkillPoint" }, "T_SkillPoint@setShow", true)
    return r and true or false
  end
  if status_shown() then return rq.statuspage.refresh() and true or false end
  return false
end

-- ---- The controls ----

local theme, sw_theme

local function themes()
  if theme then return theme, sw_theme end
  local ui = rq.uikit
  local hub = ui.themes.hub
  local function cut(p, top, bottom)
    local q = {}
    for k, v in pairs(p) do q[k] = v end
    q.edge, q.mid = { 1, top, 1, bottom }, { 4, 12 }
    return q
  end
  -- The game's scroll bar cut to 14 wide: one column of each bevel and the 12 middle ones (Moru chose
  -- it on 2026-11-15 from shots of the status page).
  local s = hub.scroll
  local slim = { w = 14, arrow = 40, min = 10,
    up = cut(s.up, 0, 0), up_hover = cut(s.up_hover, 0, 0),
    down = cut(s.down, 0, 0), down_hover = cut(s.down_hover, 0, 0),
    bar = cut(s.bar, 0, 2), track = cut({ cg = 11500, src = { 6, 100, 20, 447 } }, 2, 2) }
  theme = setmetatable({ scroll = slim }, { __index = hub })
  -- The arrows beside KNOWN SKILLS: the game's left and right arrows, two states of 40x20.
  local function arrow(cg)
    return { w = 40, h = 20, normal = { cg = cg, src = { 0, 0, 40, 20 } }, hover = { cg = cg, src = { 40, 0, 40, 20 } },
      text = {}, pad = 0 }
  end
  sw_theme = setmetatable({ styles = { button = { known_prev = arrow(10003), known_next = arrow(10004) } } },
    { __index = hub })
  return theme, sw_theme
end

local function turn(p)
  if p == page or p < 1 or p > pages then return end
  page = p
  redraw()
end

-- Page Up and Page Down where the controls show (2.57.0): a press turns a page; held, it turns again
-- after 400 ms and then every 150 ms, as the kit's keys. A key already down when the controls come up
-- does not count.
local PGUP, PGDN = 33, 34
local KEY_FIRST, KEY_NEXT = 400, 150
local key_at, keys_on = {}, false

local function page_keys(first)
  keys_on = true
  local function hit(code)
    if not rq.input.key(code) then
      key_at[code] = nil
      return false
    end
    local now, at = rq.time_ms(), key_at[code]
    if at == nil then
      key_at[code] = first and math.huge or now + KEY_FIRST
      return not first
    end
    if now >= at then
      key_at[code] = now + KEY_NEXT
      return true
    end
    return false
  end
  local up, down = hit(PGUP), hit(PGDN)
  if up and not down then
    turn(page - 1)
  elseif down and not up then
    turn(page + 1)
  end
end

-- The status page: rows known-0 to known-16 at x 271 to 413, y 127 to 567; the equipment column
-- from x 428. The bar fits the gap, as tall as the 17 rows.
local BAR = { x = 414, y = 127, h = 440, wheel = { 271, 127, 157, 440 } }
-- The skill window's heading band over the known column (x 264 to 517, y 92 to 118).
local PREV, NEXT = { x = 274, y = 95 }, { x = 470, y = 95 }

local function draw()
  local ui = rq.uikit
  local was_on = keys_on
  keys_on = false
  if window_chara() then
    local chara = window_chara()
    if chara ~= built_for or pages <= 1 then return end
    page_keys(not was_on)
    local _, swt = themes()
    local old = ui.theme
    ui.use(swt)
    local ok, err = pcall(function()
      local tip = string.format("Page %d of %d", page, pages)
      if ui.button("", { style = "known_prev", x = PREV.x, y = PREV.y, id = "known-prev", disabled = page <= 1,
        tip = { header = "Previous Page", text = tip } }) then turn(page - 1) end
      if ui.button("", { style = "known_next", x = NEXT.x, y = NEXT.y, id = "known-next", disabled = page >= pages,
        tip = { header = "Next Page", text = tip } }) then turn(page + 1) end
    end)
    ui.use(old)
    if not ok then logf("the arrows: %s", tostring(err)) end
    return
  end
  local chara = status_shown()
  if not chara or chara ~= built_for or pages <= 1 then return end
  -- rq.gamebutton's MODS list (2.59.0) is open over the page: the bar and Page Up / Page Down wait.
  if priv.status_mods_open then return end
  page_keys(not was_on)
  local th = themes()
  local old = ui.theme
  ui.use(th)
  local ok, top, changed = pcall(ui.scrollbar, page - 1, pages, 1,
    { x = BAR.x, y = BAR.y, h = BAR.h, id = "known-pages", wheel = BAR.wheel, label = "Known skills" })
  ui.use(old)
  if not ok then
    logf("the bar: %s", tostring(top))
  elseif changed then
    turn(top + 1)
  end
end

-- ---- Without install ----

-- A save made while no mod installs this is a plain save: the rows an earlier save kept apart are
-- gone from it too, so they never come back over the skills of a later game.
local always = false
local function hook_always()
  if always then return end
  local ok = pcall(hook, "セーブデータ_保存", { before = function()
    if installed then return end
    saved(function()
      if rq.savedata.get(SAVE_KEY) ~= nil then rq.savedata.set(SAVE_KEY, nil) end
    end)
  end })
  if ok then always = true end
end

hook_always()
if not always then
  local stop
  stop = rq.on_frame(function()
    hook_always()
    if always and stop then stop() end
  end)
end

-- ---- install ----

function ks.install(opts)
  if opts ~= nil and type(opts) ~= "table" then error("rq.knownskills.install: opts must be a table", 2) end
  opts = opts or {}
  local want = opts.most == nil and 34 or opts.most
  if math.type(want) ~= "integer" or want < GAME_MOST or want > MOST_MAX then
    error(string.format("rq.knownskills.install: most is a whole number from %d to %d", GAME_MOST, MOST_MAX), 2)
  end
  if installed then
    if want ~= most then logf("installed already with %d known skills; %d is not used", most, want) end
    return true
  end
  most, size, cap = want, want + 4, want + 1
  local ok, err = patch_all()
  if not ok then
    most, size, cap = GAME_MOST, GAME_SIZE, GAME_CAP
    error("rq.knownskills.install: the game's code is not as expected (" .. err .. ")", 2)
  end
  installed = true
  priv.skill_cap = cap

  hook("セーブデータ_保存", {
    before = function()
      local ok2, e = pcall(function()
        stash = save_rows()
        for iic = 0, charas() - 1 do
          if len("Ｃ", iic, "スキル") > GAME_SIZE then set_len("Ｃ", iic, "スキル", GAME_SIZE) end
          for r = GAME_CAP + 1, GAME_SIZE - 1 do clear_row(iic, r) end
        end
        saved(function() rq.savedata.set(SAVE_KEY, next(stash) and stash or nil) end)
      end)
      if not ok2 then logf("before the save: %s", tostring(e)) end
    end,
    after = function()
      local ok2, e = pcall(put_rows, stash)
      stash = nil
      if not ok2 then logf("after the save: %s", tostring(e)) end
    end,
  })
  hook("セーブデータ_読み込み", { after = function()
    local ok2, e = pcall(function() put_rows(saved(function() return rq.savedata.get(SAVE_KEY) end)) end)
    if not ok2 then logf("after the load: %s", tostring(e)) end
  end })

  hook("キャラスキル_保有一覧", {
    before = function(h)
      local ok2, r = pcall(build, h.args[1])
      if ok2 and r then
        slice, slicing = r, true
      elseif not ok2 then
        logf("the page: %s", tostring(r))
      end
    end,
    after = function() slicing, slice = false, nil end,
  })
  hook("キャラ_スキル数", { before = function(h)
    if slicing then h.skip, h.result = true, #slice end
  end })
  hook("キャラ_スキル番号", { before = function(h)
    if slicing then h.skip, h.result = true, slice[h.args[2]] or 0 end
  end })
  hook("キャラ_スキル保有", { before = function(h)
    if slicing then h.skip, h.result = true, slice[h.args[2]] and 1 or 0 end
  end })
  -- The status page opens on page 1. (on_open comes after the game built the list, so the reset
  -- is on close.)
  rq.statuspage.on_close(function() page, built_for = 1, nil end)
  -- T_SkillPoint::create calls delete first, so the window is open from create's end.
  hook("T_SkillPoint@create", { after = function(h) sw_open = (h.result and h.result ~= 0) and true or false end })
  hook("T_SkillPoint@delete", { after = function() sw_open = false end })

  if opts.controls ~= false then rq.on_frame(draw) end
  logf("on (%d known skills at most, rows 1 to %d, %s)", most, size - 1,
    opts.controls == false and "the mod's own page controls" or "the page bar and arrows")
  return true
end

function ks.most()
  return most
end

local function need(where)
  if not installed then error("rq.knownskills." .. where .. ": call rq.knownskills.install first", 3) end
end

function ks.page()
  need("page")
  return page
end

function ks.page_count()
  need("page_count")
  return pages
end

function ks.set_page(p)
  need("set_page")
  if math.type(p) ~= "integer" then error("rq.knownskills.set_page: page is a whole number", 2) end
  page = math.max(1, p)
  return redraw()
end
