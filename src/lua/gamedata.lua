-- Game data for mods (loader 2.23.0):
--   rq.world.current, on_transfer, on_transfer_after   the world and the game's world transfer
--   rq.shop.donations, set_donations                   the donations count and Frostbuy's goal
--   rq.quests.on_take, can_take, refresh               taking a quest from the list
--   rq.quests.set_state(no, 1)                         writes the state the game's rule refuses
--   rq.chara.absorbed, set_absorbed                    the five items a Morurun absorbed
--
-- Built only on public calls: rq.ain.call, global, set_global, method and hook.
--   before / after ワールド切り替え処理   the game's reset of a world transfer (ＴＡＤＡ/関数／Ｔ初期化)
--   before 選択_確認_拡張 with id 11       T_Quest::execOk's "Take Quest@<name>?", before the autosave
local rq = ...
local call, hook, g, set = rq.ain.call, rq.ain.hook, rq.ain.global, rq.ain.set_global

local function logf(fmt, ...)
  rq.log(string.format(fmt, ...))
end

local function mod_id()
  local ok, id = pcall(function() return rq.mod and rq.mod.id and rq.mod.id() end)
  return ok and id or "?"
end

-- A list of handlers with the mod that added each; add returns the remover.
local function handlers()
  local list = {}
  local function add(fn)
    local h = { fn = fn, mod = mod_id() }
    list[#list + 1] = h
    return function()
      for i, x in ipairs(list) do
        if x == h then table.remove(list, i) return end
      end
    end
  end
  return list, add
end

local function int(v)
  return math.type(v) == "integer"
end

-- ---- rq.world ----

local world = rq.world
if type(world) ~= "table" then world = {}; rq.world = world end

local function world_now()
  local ok, w = pcall(g, "◆ワールド")
  if ok and int(w) then return w end
  return 0
end

-- Whether a save is loaded (2.56.1). The game sets ◆ワールド to 1 at boot (Ｔ初期化, before the
-- title), so the world alone said 1 at the title. タイトル画面 (each time the title shows) clears it;
-- Ｔロード直後処理 (the autosave's and the suspend save's load) and Ｔクエスト (each quest's start,
-- the new game's prologue first) set it. Each hook is placed once; one that fails is tried again
-- each frame, as savedata.lua's are.
local loaded = false
local LOADED_HOOKS = {
  { "タイトル画面", false },
  { "Ｔロード直後処理", true },
  { "Ｔクエスト", true },
}
local loaded_placed = {}

local function hook_loaded()
  local all = true
  for i, h in ipairs(LOADED_HOOKS) do
    if not loaded_placed[i] then
      local value = h[2]
      if pcall(hook, h[1], { before = function() loaded = value end }) then
        loaded_placed[i] = true
      else
        all = false
      end
    end
  end
  return all
end

if not hook_loaded() then
  local stop
  stop = rq.on_frame(function()
    if hook_loaded() and stop then stop() end
  end)
end

function world.current()
  if not loaded then return nil end
  local w = world_now()
  return w >= 1 and w or nil
end

local before_list, add_before = handlers()
local after_list, add_after = handlers()
local transfer_hooked = false

local function run_transfer(list, what)
  local w = world_now()
  for _, h in ipairs({ table.unpack(list) }) do
    local ok, err = pcall(h.fn, { world = w, from = w - 1 })
    if not ok then logf("rq.world.%s (%s): %s", what, h.mod, tostring(err)) end
  end
end

local function hook_transfer()
  if transfer_hooked then return end
  transfer_hooked = true
  hook("ワールド切り替え処理", {
    before = function() run_transfer(before_list, "on_transfer") end,
    after = function() run_transfer(after_list, "on_transfer_after") end,
  })
end

local function check_fn(fn, name)
  if type(fn) ~= "function" then error(name .. ": expected a function", 3) end
end

function world.on_transfer(fn)
  check_fn(fn, "rq.world.on_transfer")
  hook_transfer()
  return add_before(fn)
end

function world.on_transfer_after(fn)
  check_fn(fn, "rq.world.on_transfer_after")
  hook_transfer()
  return add_after(fn)
end

-- ---- rq.shop: donations ----

local shop = rq.shop
if type(shop) ~= "table" then shop = {}; rq.shop = shop end

function shop.donations()
  return g("◆寄付"), g("◆寄付ボーナス")
end

function shop.set_donations(count, goal)
  if not int(count) or count < 0 then
    error("rq.shop.set_donations: the count must be a whole number, 0 or more", 2)
  end
  if goal ~= nil and (not int(goal) or goal < 0) then
    error("rq.shop.set_donations: the goal must be a whole number, 0 or more", 2)
  end
  set("◆寄付", count)
  if goal ~= nil then set("◆寄付ボーナス", goal) end
end

-- ---- rq.quests ----

local quests = rq.quests
local TAKE_ID = 11 -- the "Take Quest" question's don't-ask-again id, used only by T_Quest::execOk

local function quest_no(no, name)
  if not int(no) or no <= 0 then error(name .. ": expected a quest number", 3) end
end

function quests.can_take(no)
  quest_no(no, "rq.quests.can_take")
  local v = call("クエスト_有効", no)
  return v == true or v == 1
end

-- The list is open while its base sprite lives (T_Quest::create attaches it, delete lets it go).
local function list_open()
  local ok, sp = pcall(g, "g_sQuest", "m_spBase", "m_nSpNo")
  return ok and int(sp) and sp > 0
end

function quests.refresh()
  if not list_open() then return false end
  local ok, sel = pcall(g, "g_sQuest", "m_sList", "m_nSelectNo")
  if not ok or not int(sel) then sel = -1 end
  -- As T_Quest::execAddFavorite: the rows made again, then the chosen quest's buttons.
  rq.ain.method({ "g_sQuest" }, "T_Quest@remakeList", sel > 0 and sel or -1, -1)
  if sel > 0 then rq.ain.method({ "g_sQuest" }, "T_Quest@lock", sel) end
  return true
end

local take_list, add_take = handlers()
local take_hooked = false

local function hook_take()
  if take_hooked then return end
  take_hooked = true
  hook("選択_確認_拡張", { before = function(h)
    if h.args[1] ~= TAKE_ID or #take_list == 0 then return end
    local ok, sel = pcall(g, "g_sQuest", "m_sList", "m_nSelectNo")
    if not ok or not int(sel) or sel <= 0 then return end
    local ctx = { quest = sel, text = h.args[2] }
    for _, x in ipairs({ table.unpack(take_list) }) do
      local fine, r = pcall(x.fn, ctx)
      if not fine then
        logf("rq.quests.on_take (%s): %s", x.mod, tostring(r))
      elseif r ~= nil then
        h.skip, h.result = true, r == true
        return
      end
    end
  end })
end

function quests.on_take(fn)
  check_fn(fn, "rq.quests.on_take")
  hook_take()
  return add_take(fn)
end

-- Ｑ状態(no, 1) sets 1 only from 0 (the game's rule); on an available or cleared quest the call
-- writes the state itself, so it does what it says.
local raw_set_state = quests.set_state
function quests.set_state(no, state)
  raw_set_state(no, state)
  if state == 1 and quests.state(no) ~= 1 then set("Ｑ", no, "状態", 1) end
end

-- ---- rq.chara: the Morurun's absorbed items ----

local chara = rq.chara
local SLOTS = { 17, 18, 19, 20, 21 } -- ＣＦＡ: Ｃ体内特殊アイテムの取得処理 fills these
local SPECIAL = 21
local NEVER = { [304] = true, [305] = true, [306] = true, [307] = true, [309] = true, [310] = true }

local function check_chara(iic, name)
  if not int(iic) or iic <= 0 or not chara.get(iic) then error(name .. ": no such character", 3) end
end

function chara.absorbed(iic)
  check_chara(iic, "rq.chara.absorbed")
  local out = {}
  for i, n in ipairs(SLOTS) do out[i] = call("ＣＦＡ", iic, n, -1) end
  return out
end

function chara.set_absorbed(iic, slot, no)
  check_chara(iic, "rq.chara.set_absorbed")
  if not int(slot) or slot < 1 or slot > 5 then
    error("rq.chara.set_absorbed: the slot must be 1 to 5", 2)
  end
  if not int(no) or no < 0 then error("rq.chara.set_absorbed: expected an item number, or 0", 2) end
  if no ~= 0 then
    if NEVER[no] then error("rq.chara.set_absorbed: the game never absorbs item " .. no, 2) end
    local d = rq.item.info(no)
    if not d then error("rq.chara.set_absorbed: no item " .. no, 2) end
    if d.kind ~= SPECIAL then
      error("rq.chara.set_absorbed: item " .. no .. " is not a special item (kind 21)", 2)
    end
  end
  call("ＣＦＡ", iic, SLOTS[slot], no)
  rq.chara.recalc(iic, quests.current() == 0)
end
