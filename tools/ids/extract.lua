-- Reads the names of the game's characters, skills, items and quests through the public rq.*
-- calls and writes them to ids-<kind>.tsv in the folder of the mod that runs it. Run it as a
-- Test Lab job with no other mod installed, so nothing a mod defined is read as the game's:
--   scripts\ids-extract.ps1
-- Each line: number, name, then one detail column (skill: action kind; item: slot name; chara: class).
-- Only names and numbers leave the game: no descriptions or other text.
local t = _G.t
local out = {}

local function clean(s)
  return (tostring(s or ""):gsub("[\t\r\n]", " "))
end

local function write(kind, rows)
  local f = assert(io.open("ids-" .. kind .. ".tsv", "wb"))
  for _, r in ipairs(rows) do
    f:write(r[1], "\t", clean(r[2]), "\t", tostring(r[3] or ""), "\n")
  end
  f:close()
  out[#out + 1] = kind .. " " .. #rows
end

-- Skills: one number each. rq.skill.get needs rq.skill.install.
assert(rq.skill.install(), "rq.skill.install failed")
local skills = {}
for no = 1, 1000 do
  local ok, s = pcall(rq.skill.get, no)
  if ok and s and s.name and s.name ~= "" and not s.name:match("^skill%-%d+$") then
    skills[#skills + 1] = { no, s.name, s.koudou }
  end
end
write("skill", skills)

-- Items: make one copy, read it, remove it. The game refuses a number it does not know.
local items = {}
for no = 1, 999 do
  local ok, ii = pcall(rq.item.give, no)
  if ok and ii then
    local it = rq.item.get(ii)
    if it and it.name ~= "" then items[#items + 1] = { no, it.name, it.type_name } end
    rq.item.remove(ii)
  end
end
write("item", items)

-- Characters: recruit, read, dismiss. A number the game has no character for comes back
-- as "NoName". Characters already in the roster are read where they are.
local charas, skipped = {}, 0
for no = 1, 999 do
  local had = rq.chara.find(no)
  local ok, iic = pcall(rq.chara.recruit, no)
  if ok and iic then
    local c = rq.chara.get(iic)
    if c and c.name ~= "" and c.name ~= "NoName" then
      charas[#charas + 1] = { no, c.name, c.class }
    else
      skipped = skipped + 1
    end
    if not had then rq.chara.dismiss(iic) end
  end
end
write("chara", charas)
out[#out + 1] = "chara NoName " .. skipped

-- Quests: the game's quest list.
local quests = {}
for _, q in ipairs(rq.quests()) do quests[#quests + 1] = { q.id, q.name, "" } end
write("quest", quests)

t.log(table.concat(out, ", "))
