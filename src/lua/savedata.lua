-- rq.savedata's moments (loader 2.34.0, notes/plans/plan-rewards-savedata.md in the workspace).
-- rq.savedata itself is in C (modloader_roster.c): kept in memory, written beside the game's slot
-- file when the game writes it, read back when the game reads it, deleted with it.
--
--   rq.saves.on_save(fn)    just before the game writes a save (Ｔセーブ直前処理), ctx.suspend
--   rq.saves.on_load(fn)    after the game loaded one (Ｔロード直後処理(fg), fg 1 = the suspend save)
--
-- The game's save (C_Save.jaf):
--   自動セーブ(mode)              the save (slot 30): before a quest, after one, the SYSTEM menu
--   中断セーブ()                  the suspend save (slot 930), then the game closes
--   セーブデータ_バックアップ()   once a day, the old save's three files into Backup\YYYYMMDD,
--                                 each by _セーブデータ_バックアップ(folder, file)
--   T_Title@create                the title: no save is loaded, so no mod data
local rq, clear, backup = ...
local hook = rq.ain.hook
local saves = rq.saves

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
  local function add(fn, name)
    if type(fn) ~= "function" then error(name .. ": expected a function", 3) end
    local h = { fn = fn, mod = mod_id(), seen = {} }
    list[#list + 1] = h
    return function()
      for i, x in ipairs(list) do
        if x == h then table.remove(list, i) return end
      end
    end
  end
  return list, add
end

-- Runs every handler; an error is logged once per text, as rq.on_frame's are.
local function run_all(list, what, ctx)
  for _, h in ipairs({ table.unpack(list) }) do
    local ok, err = pcall(h.fn, ctx)
    if not ok then
      local text = tostring(err)
      if not h.seen[text] then
        h.seen[text] = true
        logf("%s (%s): %s", what, h.mod, text)
      end
    end
  end
end

-- ---- the title and the backup: always ----

local always = false
local function hook_always()
  if always then return end
  local ok = pcall(function()
    hook("T_Title@create", { before = function() clear() end })
    -- Each of the save's files copied into the day's folder: the data file goes with the slot
    -- file (the others have none beside them).
    hook("_セーブデータ_バックアップ", { after = function(h)
      local folder, file = h.args[1], h.args[2]
      if h.result and h.result ~= 0 and type(folder) == "string" and type(file) == "string" then
        backup(file, folder)
      end
    end })
  end)
  if ok then always = true end
end

hook_always()
do
  local stop
  stop = rq.on_frame(function()
    hook_always()
    if always and stop then stop() end
  end)
end

-- ---- rq.saves.on_save, on_load ----

local save_list, add_save = handlers()
local load_list, add_load = handlers()
local suspending = false
local save_hooked, load_hooked = false, false

function saves.on_save(fn)
  if type(fn) ~= "function" then error("rq.saves.on_save: expected a function", 2) end
  if not save_hooked then
    save_hooked = true
    hook("中断セーブ", {
      before = function() suspending = true end,
      after = function() suspending = false end,
    })
    hook("Ｔセーブ直前処理", { before = function()
      if #save_list > 0 then run_all(save_list, "rq.saves.on_save", { suspend = suspending }) end
    end })
  end
  return add_save(fn, "rq.saves.on_save")
end

function saves.on_load(fn)
  if type(fn) ~= "function" then error("rq.saves.on_load: expected a function", 2) end
  if not load_hooked then
    load_hooked = true
    hook("Ｔロード直後処理", { after = function(h)
      if #load_list > 0 then run_all(load_list, "rq.saves.on_load", { suspend = h.args[1] == 1 }) end
    end })
  end
  return add_load(fn, "rq.saves.on_load")
end
