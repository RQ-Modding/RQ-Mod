-- rq.item.lock, sell, donate and store (loader 2.9.0): what the vault's buttons do, for a mod.
-- Each is the game's own call (アイテム_ＬＯＣＫ, アイテム_売却, アイテム_寄付, アイテム_存在設定), so the
-- game's sounds, its restock, its donation count and what it does before an item goes all happen.
-- Selling without asking does the steps of アイテム_売却 after its question. The game's question
-- runs inside the call; rq.on_frame keeps running then, so a second sell or donate while one asks
-- returns false instead of asking again.
local rq = ...
local item = rq.item
local call = rq.ain.call

local SHOP, VAULT, STORAGE = 6, 7, 11
local KINDS = { special = 21, weapon = 1, armor = 11, item = 17 }
local asking = false

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
  if not ok or asking then return false end
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
  if not ok or asking then return false end
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
  if asking or ii <= 0 or not call("アイテム_確認", ii) or call("アイテム_存在", ii) ~= SHOP then return false end
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

rq.on_reset(function() asking = false end)
