-- rq.item.lock, sell, donate and store (loader 2.9.0): what the vault's buttons do, for a mod.
-- Each is the game's own call (アイテム_ＬＯＣＫ, アイテム_売却, アイテム_寄付, アイテム_存在設定), so the
-- game's sounds, its restock, its donation count and what it does before an item goes all happen.
-- Selling without asking does the steps of アイテム_売却 after its question. The game's question
-- runs inside the call; rq.on_frame keeps running then, so a second sell or donate while one asks
-- returns false instead of asking again.
local rq, priv = ...
local item = rq.item
local call = rq.ain.call

local SHOP, VAULT, STORAGE = 6, 7, 11
local KINDS = { special = 21, weapon = 1, armor = 11, item = 17 }
local asking = false

-- A question of rq.item, or any of the game's dialogs (rq.ui.confirm and the rest, 2.19.0), shows:
-- the game has one list of dialog rows, so a second question would mix into it.
local function busy()
  return asking or (rq.ui.choosing ~= nil and rq.ui.choosing())
end

-- Who wears ii and the slot's kind (21, 1, 11, 17), by the characters' slots: a worn item's place
-- is its wearer (アイテム存在_装備中).
local function wearer(ii)
  for _, u in ipairs(rq.units()) do
    for slot, v in pairs(u.equipment or {}) do
      if v == ii and KINDS[slot] then return u.id, KINDS[slot] end
    end
  end
end

-- worn: also an item a character wears (sell and donate, 2.10.0). Returns ok, then the wearer and
-- the slot's kind for a worn item.
local function check(ii, where, unlocked, worn)
  if math.type(ii) ~= "integer" then error("rq.item." .. where .. ": expected an item instance (ii)", 3) end
  if ii <= 0 or not call("アイテム_確認", ii) then return false end
  local place = call("アイテム_存在", ii)
  local chara, kind
  if place ~= VAULT and place ~= STORAGE then
    if not worn then return false end
    chara, kind = wearer(ii)
    if not chara then return false end
  end
  -- The vault hides SELL and DONATE on a locked row (T_StoreItem::setButtonShow).
  if unlocked and call("アイテム_ＬＯＣＫ中", ii) then return false end
  return true, chara, kind
end

-- A game call that may show the game's question or notice: a second one meanwhile is refused.
local function modal(fn, ...)
  asking = true
  local ok, r = pcall(call, fn, ...)
  asking = false
  if not ok then error(r, 0) end
  return r
end

local function notice(text)
  return modal("選択_通知", text, "OK", false)
end

-- The name without its first and last letters, as the game's questions take it (GetPart(1, n - 2)).
local function inner(name)
  local a, b = utf8.offset(name, 2), utf8.offset(name, -1)
  if not a or not b or b < a then return name end
  return name:sub(a, b - 1)
end

local function yes(no, text)
  return modal("選択_確認_拡張", no, text, 1, "Yes", "No", -1, -1, false)
end

local function ask_of(opts, where)
  if opts == nil then return true end
  if type(opts) ~= "table" then error("rq.item." .. where .. ": opts is a table", 3) end
  return opts.ask ~= false
end

-- rq.uikit's rows and pickers read the item again at once (2.9.0).
local function changed(ii)
  local kit = rq.uikit
  if kit and kit.item_refresh then pcall(kit.item_refresh, ii) end
  return true
end

-- The calls in C that make, end or change instances tell rq.uikit's lists too (2.44.0): they
-- waited for the lists' 2-second relisting. Each upvalue keeps the call's name, so the C side's
-- argument errors still name it ("bad argument #1 to 'give'").
do
  local give, remove, set, transform, equip, edit = item.give, item.remove, item.set, item.transform,
    item.equip, item.edit
  local function relist(ii, ...)
    changed(math.type(ii) == "integer" and ii or nil)
    return ...
  end
  local function given(...)
    local ii = ...
    if math.type(ii) == "integer" then changed(ii) end
    return ...
  end
  if give then function item.give(...) return given(give(...)) end end
  if remove then function item.remove(ii, ...) return relist(ii, remove(ii, ...)) end end
  if set then function item.set(ii, ...) return relist(ii, set(ii, ...)) end end
  if transform then function item.transform(ii, ...) return relist(ii, transform(ii, ...)) end end
  if equip then function item.equip(ii, ...) return relist(ii, equip(ii, ...)) end end
  if edit then function item.edit(...) return relist(nil, edit(...)) end end
end

-- The money and donations on the system bar, as the vault updates them.
local function show_money()
  pcall(call, "システムバー_更新", 2, -1)
  pcall(call, "システム情報_更新", 0, -1)
end

function item.lock(ii, on)
  if not check(ii, "lock") then return false end
  local done = call(on == false and "アイテム_ＵＮＬＯＣＫ" or "アイテム_ＬＯＣＫ", ii)
  return done and changed(ii)
end

-- A worn item, before it is sold or donated (2.10.0): the game's tests and question, then it
-- comes off. The game's own アイテム_売却 and アイテム_寄付 cannot take it: they set its place
-- to 0 while the character still holds it. An item that vanishes when taken off is refused.
local function take_off(ii, chara, kind, ask, what, qno, question)
  if not call("アイテム_破棄可能", ii) then
    if ask then notice(call("テキスト_アイテム名", ii) .. "@cannot be " .. what .. ".") end
    return false
  end
  if call("アイテム_消滅します", ii) > 0 then return false end
  if call("アイテム_外せません", ii) > 0 then
    -- The game's "cannot be removed" notice.
    if ask then modal("アイテム_装備外し確認", ii) end
    return false
  end
  if ask and not yes(qno, question(inner(call("テキスト_アイテム名", ii)))) then return false end
  return call("キャラ_装備設定", chara, kind, 0)
end

local function sell_now(ii)
  call("効果音_売却")
  call("Ｔアイテム寄付売却捨てる前", ii)
  call("アイテム_存在設定", ii, 0)
  call("アイテム_補充", ii)
  call("所持金＋", call("Ｉ価格", ii, -1) > 1000 and 100 or 10)
  show_money()
  return changed(ii)
end

function item.sell(ii, opts)
  local ask = ask_of(opts, "sell")
  local ok, chara, kind = check(ii, "sell", true, true)
  if not ok or busy() then return false end
  if chara then
    if not take_off(ii, chara, kind, ask, "sold", 14, function(n) return "Sell@" .. n .. "?" end) then
      return false
    end
    return sell_now(ii)
  end
  if not ask then
    if not call("アイテム_破棄可能", ii) then return false end
    return sell_now(ii)
  end
  local sold = modal("アイテム_売却", ii)
  if sold then show_money() end
  return sold and changed(ii)
end

function item.donate(ii, opts)
  local ask = ask_of(opts, "donate")
  local ok, chara, kind = check(ii, "donate", true, true)
  if not ok or busy() then return false end
  if chara then
    if not take_off(ii, chara, kind, ask, "donated", 31, function(n) return "Donate " .. n .. "?" end) then
      return false
    end
    ask = false
  end
  -- Without a question, the game's "cannot be donated" notice would still stop the update.
  if not call("アイテム_破棄可能", ii) and not ask then return false end
  local done
  if ask then done = modal("アイテム_寄付", ii, true) else done = call("アイテム_寄付", ii, false) end
  if done then show_money() end
  return done and changed(ii)
end

-- The shop's stock (place 6), bought as the shop's drag onto the vault or a party member does
-- (T_EquitItem::setItem). 2.10.0.
function item.buy(ii, opts)
  if opts ~= nil and type(opts) ~= "table" then error("rq.item.buy: opts is a table", 2) end
  opts = opts or {}
  local ask = opts.ask ~= false
  local chara = opts.chara
  if chara ~= nil and math.type(chara) ~= "integer" then error("rq.item.buy: chara is a roster slot", 2) end
  if math.type(ii) ~= "integer" then error("rq.item.buy: expected an item instance (ii)", 2) end
  if busy() or ii <= 0 or not call("アイテム_確認", ii) or call("アイテム_存在", ii) ~= SHOP then return false end
  local kind
  if chara then
    if not call("キャラ_確認", chara) then return false end
    if not call("アイテム_装備可能", ii, chara) then
      if ask then notice("Unable to equip.") end
      return false
    end
    kind = call("アイテム_種類", ii)
  end
  local price = call("アイテム_価格", ii)
  if price < 0 then return false end
  if price > call("所持金") then
    if ask then notice("Not enough money.") end
    return false
  end
  if chara then
    if ask then
      if not modal("キャラ_装備外し確認", chara, kind) then return false end
    else
      -- Without questions: an old item that cannot come off, or would vanish, stops it.
      local old = call("キャラ_装備取得", chara, kind)
      if old < 0 then return false end
      if old > 0 and (call("アイテム_外せません", old) > 0 or call("アイテム_消滅します", old) > 0) then
        return false
      end
    end
  end
  if ask and not yes(10, "Purchase " .. inner(call("テキスト_アイテム名", ii)) .. "?") then return false end
  call("アイテム_存在設定", ii, VAULT)
  call("所持金－", price)
  call("消費金額＋", price)
  if chara then
    if call("キャラ_装備設定", chara, kind, ii) then call("効果音_アイテム装備", ii) end
  else
    call("効果音_ドロップ")
  end
  show_money()
  return changed(ii)
end

function item.store(ii, where)
  if where ~= "vault" and where ~= "storage" then
    error("rq.item.store: where is \"vault\" or \"storage\"", 2)
  end
  if not check(ii, "store") then return false end
  call("アイテム_存在設定", ii, where == "vault" and VAULT or STORAGE)
  return changed(ii)
end

-- rq.item.info(no) (2.20.0): an item number's row data with no instance in the game, read as the
-- game's own lookups read it (Ｉ固有名前: a temporary instance, read, deleted). priv.quiet keeps the
-- rq.item.on_create listeners from seeing that instance; the item's own definition still runs.
local infos = {}   -- no -> { t = time, row = row or false }

local function read_info(no)
  if priv and priv.quiet then priv.quiet(true) end
  local ok, ii = pcall(call, "Ｉ生成", no, 0)
  if priv and priv.quiet then priv.quiet(false) end
  if not ok or type(ii) ~= "number" or ii <= 0 then return nil end
  local row
  ok = pcall(function()
    row = {
      no = no,
      name = call("アイテム_名前", ii),
      level = call("アイテム_実装条件", ii),
      info = call("アイテム_情報", ii),
      icon = call("アイテム_ＣＧ番号", ii),
      rank = call("アイテム_レアランク", ii),
      kind = call("アイテム_種類", ii),
      price = call("アイテム_価格", ii),
      discard = call("アイテム_破棄可能", ii),
      classes = {},
    }
    for c = 1, 8 do
      if call("アイテム_装備職業", ii, c) > 0 then row.classes[#row.classes + 1] = c end
    end
  end)
  pcall(call, "Ｉ削除", ii)
  return ok and row or nil
end

local function copy(row)
  local out = {}
  for k, v in pairs(row) do out[k] = v end
  out.classes = {}
  for i, c in ipairs(row.classes) do out.classes[i] = c end
  return out
end

function item.info(no)
  if math.type(no) ~= "integer" then error("rq.item.info: expected an item number", 2) end
  if no <= 0 then return nil end
  local now = rq.time_ms()
  local hit = infos[no]
  if not hit or now - hit.t >= 1000 then
    hit = { t = now, row = read_info(no) or false }
    infos[no] = hit
  end
  return hit.row and copy(hit.row) or nil
end

-- ---- rq.item.on_can_equip (2.46.0, notes/plans/plan-new-mods.md in the workspace, step 2) ----
--
-- アイテム_装備可能(ii, iic) is the one check of the vault, the status page, the shop, the storage
-- screen's filters and the equip list (T_EquipList). Its order: the item's level (Ｉ装備ＬＶ, 0 for
-- none) against the character's (キャラ_レベル); then the item's character list
-- (アイテム_装備キャラ 1 to アイテム_装備キャラ数, by Ｃ番号), which decides alone when it names
-- anyone; else the class (アイテム_装備職業 of キャラ_職業). The callbacks run in an after hook; the
-- reasons are worked out only when a callback reads them.

local can_list = {}
local can_hooked = false

local function can_why(ii, iic)
  local reasons = {}
  local lv = call("アイテム_装備レベル", ii)
  local clv = call("キャラ_レベル", iic)
  if lv > 0 and clv < lv then reasons[#reasons + 1] = "level" end
  local named, listed = false, false
  local no = call("キャラ_番号", iic)
  for i = 1, call("アイテム_装備キャラ数") do
    local x = call("アイテム_装備キャラ", ii, i)
    if x > 0 then
      listed = true
      if x == no then named = true end
    end
  end
  if listed then
    if not named then reasons[#reasons + 1] = "chara" end
  elseif call("アイテム_装備職業", ii, call("キャラ_職業", iic)) <= 0 then
    reasons[#reasons + 1] = "class"
  end
  return { reasons = reasons, reason = reasons[1], level = lv, chara_level = clv }
end

local CAN_LAZY = { reasons = true, reason = true, level = true, chara_level = true }

local function can_ctx(ii, iic, allowed)
  local ctx = { ii = ii, iic = iic, allowed = allowed }
  return setmetatable(ctx, { __index = function(t, k)
    if k == "no" then
      local v = call("Ｉ固有番号", ii, -1)
      rawset(t, "no", v)
      return v
    end
    if k == "chara" then
      local v = call("キャラ_番号", iic)
      rawset(t, "chara", v)
      return v
    end
    if CAN_LAZY[k] then
      local why = can_why(ii, iic)
      for f in pairs(CAN_LAZY) do rawset(t, f, why[f]) end
      return why[k]
    end
  end })
end

local function can_install()
  if can_hooked then return end
  can_hooked = true
  rq.ain.hook("アイテム_装備可能", { after = function(h)
    if #can_list == 0 then return end
    local ii, iic = h.args[1], h.args[2]
    if math.type(ii) ~= "integer" or math.type(iic) ~= "integer" or ii <= 0 or iic <= 0 then return end
    local okc, real = pcall(call, "アイテム_確認", ii)
    if not okc or not real then return end
    local ctx = can_ctx(ii, iic, h.result == true)
    for _, x in ipairs({ table.unpack(can_list) }) do
      local ok, r = pcall(x.fn, ctx)
      if not ok then
        r = tostring(r)
        if not x.seen[r] then
          x.seen[r] = true
          rq.log(("rq.item.on_can_equip (%s): %s"):format(x.mod, r))
        end
      elseif r == true or r == false then
        rawset(ctx, "allowed", r)
      elseif r ~= nil and not x.seen["return"] then
        x.seen["return"] = true
        rq.log(("rq.item.on_can_equip (%s): expected true, false or nil"):format(x.mod))
      end
    end
    h.result = rawget(ctx, "allowed") == true
  end })
end

function item.on_can_equip(fn)
  if type(fn) ~= "function" then error("rq.item.on_can_equip: expected a function", 2) end
  local id = "?"
  pcall(function() id = rq.mod.id() or "?" end)
  local x = { fn = fn, mod = id, seen = {} }
  can_list[#can_list + 1] = x
  can_install()
  return function()
    for i, y in ipairs(can_list) do
      if y == x then table.remove(can_list, i) return end
    end
  end
end

-- ---- rq.item.appraise (2.47.0) ----
-- Ｉ鑑定(ii) is the appraisal screen's own call (アイテム_鑑定 adds 効果音_鑑定 and puts the item in
-- Quest Storage, Ｉ存在 8): a rare item (Ｉレアランク not 0) gets one bonus for each of the quest's
-- three bonuses that was met (◆クエストボーナス有無[n] 1 and ◆クエストボーナス[n] at its most),
-- effects 21 to 25 stop at 99, then 未確定 0 and the name, CG and description of the item number
-- (Ｉ情報 "自動生成"). appraise leaves the item where it is.

function item.appraise(ii, opts)
  if math.type(ii) ~= "integer" then error("rq.item.appraise: expected an item instance (ii)", 2) end
  if opts ~= nil and type(opts) ~= "table" then error("rq.item.appraise: opts is a table", 2) end
  if opts and opts.sound ~= nil and type(opts.sound) ~= "boolean" then
    error("rq.item.appraise: sound is true or false", 2)
  end
  if ii <= 0 or not call("アイテム_確認", ii) then return false end
  if call("Ｉ未確定", ii, -1) ~= 1 then return false end
  call("Ｉ鑑定", ii)
  if opts and opts.sound then pcall(call, "効果音_鑑定") end
  changed(ii)
  return true
end

-- ---- The Quest Storage window's tiles (2.47.0) ----
-- T_StoreQuest (g_sStoreQuest) is opened by Ｒ８クエスト倉庫(mode): 0 from the map panel's button
-- (its X and the erase buttons), 1 from Give Up (Yes and No, the items go with the quest), 2 at a
-- failure (the items go). m_sList.m_anItemNo holds the instances in the window's order, 15 at
-- most; tile i (from 0) is at 482 + (i % 5) * 106, 204 + (i // 5) * 106, 100 x 100 in every mode
-- (getIconX, getIconY). After the erase button T_StoreQuest::execErase builds the window again:
-- m_sList.create(8, 0, ...), createIcon, setZ(m_spBase's z), setButtonActive, setShow(true).
local g = rq.ain.global
local QS_MODES = { [0] = "view", [1] = "give-up", [2] = "failure" }

local function qs_shown()
  local ok, show = pcall(g, "g_sStoreQuest", "m_spBase", "m_bShow")
  if not ok or show ~= true then return false end
  local ok2, sp = pcall(g, "g_sStoreQuest", "m_spBase", "m_nSpNo")
  return ok2 and math.type(sp) == "integer" and sp > 0
end

function rq.ui.quest_storage()
  if not qs_shown() then return nil end
  local ok, n = pcall(rq.ain.global_len, "g_sStoreQuest", "m_sList", "m_anItemNo")
  if not ok or math.type(n) ~= "integer" then n = 0 end
  local tiles = {}
  for i = 0, math.min(n, 15) - 1 do
    local ii = g("g_sStoreQuest", "m_sList", "m_anItemNo", i)
    if math.type(ii) == "integer" and ii > 0 then
      tiles[#tiles + 1] = { index = i + 1, ii = ii, x = 482 + (i % 5) * 106, y = 204 + (i // 5) * 106,
        w = 100, h = 100, identified = call("Ｉ未確定", ii, -1) ~= 1 }
    end
  end
  local mode = g("g_sStoreQuest", "m_nMode")
  -- The game's question over it (the erase button's "Discard ...?", Give Up's "Abandon this quest?").
  local okd, sp = pcall(g, "g_sSelect", "m_spBase", "m_nSpNo")
  local okv, shown = pcall(g, "g_sSelect", "m_spBase", "m_bShow")
  return {
    mode = QS_MODES[mode] or tostring(mode),
    z = g("g_sStoreQuest", "m_spBase", "m_nZ"),
    tiles = tiles,
    dialog = (okd and math.type(sp) == "integer" and sp > 0 and okv and shown == true) or false,
  }
end

function rq.ui.quest_storage_refresh()
  if not qs_shown() then return false end
  local m = rq.ain.method
  local ok, err = pcall(function()
    m({ "g_sStoreQuest" }, "T_StoreQuest@hideComment", -1)
    m({ "g_sStoreQuest", "m_sList" }, "T_ItemList@create", 8, 0, 0, 0, 0, 0, 0)
    m({ "g_sStoreQuest" }, "T_StoreQuest@createIcon")
    m({ "g_sStoreQuest" }, "T_StoreQuest@setZ", g("g_sStoreQuest", "m_spBase", "m_nZ"))
    m({ "g_sStoreQuest" }, "T_StoreQuest@setButtonActive")
    m({ "g_sStoreQuest" }, "T_StoreQuest@setShow", true)
  end)
  if not ok then rq.log("rq.ui.quest_storage_refresh: " .. tostring(err)) end
  return ok
end

-- ---- rq.shop.stock while the shop shows (2.47.0) ----
-- T_Shop::buyItem builds the list again (アイテム倉庫_再作成) before on_buy runs, so an item stocked
-- then, or from a button while the shop shows, waited for the next visit. The shop's list is
-- g_sStoreItem with m_nStoreType 3 (T_StoreItem::create; -1 once it closes).
local raw_stock = rq.shop and rq.shop.stock

if raw_stock then
  function rq.shop.stock(no)
    local ii = raw_stock(no)
    if math.type(ii) == "integer" and ii > 0 then
      local ok, kind = pcall(g, "g_sStoreItem", "m_nStoreType")
      if ok and kind == 3 then pcall(call, "アイテム倉庫_再作成", -1, -1, -1, -1, -1) end
    end
    return ii
  end
end

rq.on_reset(function()
  asking = false
  infos = {}
end)
