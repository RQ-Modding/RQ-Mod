-- rq.scene.start and rq.scene.active (loader 1.14.0).
--
-- rq.on_frame runs every callback each frame, also while one of them waits in a game call,
-- as a scene does. So a mod that called rq.scene.play from on_frame, with no coroutine and no
-- flag, started the same scene again inside the first. start plays the scene in a coroutine
-- of its own at the next frame, and says false while a mod scene plays, so a mod can call it
-- from anywhere, every frame too. It waits while the System menu or Config is open: they
-- are drawn over the message window, and the System menu has Save and Load. It also waits
-- for a scene of the game's own to end: the game never plays one scene inside another, and
-- a mod scene's ADVOFF there would clear the game's background under its next line. And it
-- waits for the game's save to end (2.50.0).
local rq, menu_open, menu_comment = ...
local scene = rq.scene
local play = scene.play

-- A row's comment as lines the game's menu never cuts (2.45.1): T_Select::showComment cuts at
-- each "@" and every 28 bytes, mid-word, before the window starts its lines at "\n" too
-- (rq_lua_menu_comment in modloader_scene.c). rq.scene.choice (in C's scene layer) gets it here.
local c_choice = scene.choice
function scene.choice(items, o)
  if type(items) == "table" then
    local copy = {}
    for i, it in ipairs(items) do
      if type(it) == "table" and type(it.comment) == "string" and it.comment ~= "" then
        local c = {}
        for k, v in pairs(it) do c[k] = v end
        c.comment = menu_comment(it.comment)
        copy[i] = c
      else
        copy[i] = it
      end
    end
    items = copy
  end
  return c_choice(items, o)
end
-- The game's moment for an item that comes from somewhere (2.50.0): the jingle Ｒ８ジングル(1008), then
-- Ｒ８どこかからアイテム(ii), T_ItemComment::field's window with the item's icon and name, which waits
-- for a click. The game plays it so when a reward comes in a scene (Ｒ８アイテム入手 in
-- 構造体／アイテム.jaf, the dice's prize in 関数／サイコロ.jaf). It only shows the item: where the
-- item is stays the mod's call (rq.item.give, rq.item.store).
function scene.item(ii, o)
  if math.type(ii) ~= "integer" then error("rq.scene.item: expected an item instance (ii)", 2) end
  if o ~= nil and type(o) ~= "table" then error("rq.scene.item: o is a table", 2) end
  o = o or {}
  if ii <= 0 or not rq.ain.call("アイテム_確認", ii) then
    error("rq.scene.item: no item instance " .. ii, 2)
  end
  local jingle = o.jingle
  if jingle == nil then jingle = 1008 end
  if jingle ~= false and math.type(jingle) ~= "integer" then
    error("rq.scene.item: jingle is a jingle number or false", 2)
  end
  if jingle then rq.ain.call("Ｒ８ジングル", jingle, false) end
  rq.ain.call("Ｒ８どこかからアイテム", ii)
end

local depth, pending, running = 0, nil, nil

-- Something is drawn on the game's scene layer: a face, a background, a CG.
local function game_scene()
  return rq.ain.call("画面_表示中")
end

-- The game is saving (2.50.0). Its autosave shows "Auto-Saving" and waits 700 ms in UPDATE
-- (T_SaveMessage::wait), so rq.on_frame runs under it: a scene or a menu opened there would play
-- inside the save, under its message.
local SAVING = { ["自動セーブ"] = true, ["中断セーブ"] = true, ["セーブファイル_保存"] = true }
local function saving()
  local ok, names = pcall(rq.ain.stack, 64)
  if not ok or type(names) ~= "table" then return false end
  for _, n in ipairs(names) do
    if SAVING[n] then return true end
  end
  return false
end

-- The vault's and Frostbuy's item window (g_sStoreItem: m_nStoreType 1 or 2 the vault, 3 a rack of
-- the shop, -1 closed) is drawn at z 9211 and up, over the message window (8101 to 8301). A mod scene
-- there hides it and shows it again at its end (2.50.0). T_StoreItem::setShow leaves the frame on the
-- rack the window shows (m_spLock, CG 11416) shown, so that one hides on its own.
local function store_open()
  local ok, kind = pcall(rq.ain.global, "g_sStoreItem", "m_nStoreType")
  return ok and math.type(kind) == "integer" and kind > 0
end

local function store_hide()
  if not store_open() then return false end
  pcall(rq.ain.method, { "g_sStoreItem" }, "T_StoreItem@setShow", false)
  pcall(rq.ain.method, { "g_sStoreItem", "m_spLock" }, "T_Sprite@setShow", 0)
  return true
end

local function store_show()
  if store_open() then pcall(rq.ain.method, { "g_sStoreItem" }, "T_StoreItem@setShow", true) end
end

-- The track the game plays (its _音楽, 0 for none).
local function music_now()
  local ok, no = pcall(rq.ain.call, "音楽再生中番号取得")
  return ok and math.type(no) == "integer" and no or nil
end

-- Counts the scenes rq.scene.play has open, once and start included. The outermost one puts back
-- the track that played before it, when the scene changed it (2.62.2): the game plays a screen's
-- music only when the screen starts (the town's in ゲーム_メイン's loop, before g_sTown.run), so
-- a scene's music went on over the hub until the player left it.
function scene.play(fn, ...)
  depth = depth + 1
  local hid = depth == 1 and store_hide()
  local music = depth == 1 and music_now()
  local r = table.pack(pcall(play, fn, ...))
  local to_title = not r[1] and tostring(r[2]):find("rq: the game went to the title", 1, true)
  if hid and not to_title then store_show() end
  if music and not to_title and music_now() ~= music then pcall(rq.ain.call, "音楽", music, -1, -1) end
  depth = depth - 1
  if not r[1] then error(r[2], 0) end
  return table.unpack(r, 2, r.n)
end

function scene.active()
  return depth > 0 or pending ~= nil or running ~= nil
end

-- A function, or a table that can be called: a scene file (rq.scene.file, 2.60.0).
local function callable(fn)
  if type(fn) == "function" then return true end
  local mt = getmetatable(fn)
  return type(fn) == "table" and type(mt) == "table" and mt.__call ~= nil
end

function scene.start(fn, ...)
  if not callable(fn) then error("rq.scene.start: expected a function or a scene file", 2) end
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
  if not pending or menu_open() or game_scene() or saving() then return end
  local p = pending
  pending = nil
  running = coroutine.create(function() scene.play(p[1], table.unpack(p, 2, p.n)) end)
  step()
end)

-- rq.ui.choose (loader 2.7.0): the game's choice menu outside a scene, on the hub and the
-- game's other screens. It opens at the next update from the callback below, as start does, and
-- waits for the same things, and for a choice menu that is already open. The game's own code
-- opens this menu from its update loop too (the vault's sort, アイテム倉庫_ソート選択): it is
-- modal by itself, since the game's loop under it waits until it returns. The rows get ids 101 and
-- up, past the ids rqmod-ctl names for the game's menus (1, 2, 11 to 28); Return is 999 as in
-- the vault's menu, so rqmod-ctl lists it as "return".
local BACK = 999
local ask, asking -- the one queued, and the one open
local call = rq.ain.call

local function choose_rows(items, o)
  if type(items) ~= "table" then error("rq.ui.choose: expected a list of rows", 3) end
  local rows, ids, picks = {}, {}, 0
  for i, it in ipairs(items) do
    if it == "-" then
      rows[#rows + 1] = { sep = true }
    else
      local text, id, r, g, b, comment, sound
      if type(it) == "string" then
        text, id = it, i
      elseif type(it) == "table" then
        text = it.text
        if it.id ~= nil then id = it.id else id = i end
        r, g, b, comment, sound = it.r, it.g, it.b, it.comment, it.sound
      end
      if type(text) ~= "string" then
        error("rq.ui.choose: row " .. i .. " has no text", 3)
      end
      picks = picks + 1
      ids[100 + picks] = id
      rows[#rows + 1] = { no = 100 + picks, text = text, r = r or -1, g = g or -1, b = b or -1,
        comment = comment or "", sound = sound or -1 }
    end
  end
  if picks == 0 then error("rq.ui.choose: no row to pick", 3) end
  if picks > 20 then error("rq.ui.choose: at most 20 rows; the menu does not scroll", 3) end
  return rows, ids
end

function rq.ui.choose(items, fn, o)
  if type(fn) ~= "function" then error("rq.ui.choose: expected a function", 2) end
  o = o or {}
  if o.message ~= nil and type(o.message) ~= "string" then
    error("rq.ui.choose: message is a text", 2)
  end
  local rows, ids = choose_rows(items, o)
  if ask or asking then return false end
  local back = o.back
  if back == nil or back == true then back = "Return" end
  ask = { rows = rows, ids = ids, fn = fn, back = back, message = o.message,
    cancel = o.cancel ~= false, cursor = o.cursor, x = o.x, y = o.y }
  return true
end

local function open_choice(a)
  if a.x then call("選択_位置設定", a.x, a.y or -1) end
  if a.message then
    for line in (a.message .. "\n"):gmatch("(.-)\n") do
      call("選択_テキスト追加", line, -1, -1, -1)
    end
    call("選択_区切り追加")
  end
  local cursor
  for _, r in ipairs(a.rows) do
    if r.sep then
      call("選択_区切り追加")
    else
      call("選択_項目追加", r.no, r.text, r.r, r.g, r.b, menu_comment(r.comment), r.sound)
      if a.cursor ~= nil and a.ids[r.no] == a.cursor then cursor = r.no end
    end
  end
  if a.back then
    call("選択_区切り追加")
    call("選択_項目追加", BACK, a.back, -1, -1, -1, "", -1)
  end
  if cursor then call("選択_カーソル設定", cursor) end
  local no = call("選択_実行", a.cancel, 0, 0, false)
  if no == BACK then return 0 end
  local id = a.ids[no]
  if id == nil then return 0 end
  return id
end

rq.on_frame(function()
  if not ask or asking or scene.active() or menu_open() or rq.ui.choosing() or game_scene()
    or saving() then
    return
  end
  local a = ask
  ask, asking = nil, a
  local ok, id = pcall(open_choice, a)
  asking = nil
  if not ok then
    if tostring(id):find("rq: the game went to the title", 1, true) then error(id, 0) end
    rq.log("rq.ui.choose: " .. tostring(id))
    return
  end
  local ok2, err = pcall(a.fn, id)
  if not ok2 then
    if tostring(err):find("rq: the game went to the title", 1, true) then error(err, 0) end
    rq.log("rq.ui.choose: " .. tostring(err))
  end
end)

-- The title ends the scene's coroutine under this callback, which then never gets back here.
rq.on_reset(function()
  depth, pending, running = 0, nil, nil
  ask, asking = nil, nil
end)
