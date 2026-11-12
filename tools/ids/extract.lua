-- Reads the names of the game's characters, skills, items and quests through the public rq.*
-- calls and writes them to ids-<kind>.tsv in the folder of the mod that runs it. Run it as a
-- Test Lab job with no other mod installed, so nothing a mod defined is read as the game's:
--   scripts\ids-extract.ps1
-- Each line: number, name, then one detail column (skill: action kind; item: slot name; chara: class;
-- monster: level,race).
-- The small lists (element, race, chara_type, item_kind, target, class, state) come from the
-- game's own name functions, called for 0 to 99.
-- Only names and numbers leave the game: no descriptions or other text.
-- ids-pictures.tsv gives the file browser (rq-browser) a picture for a name: a character's face and
-- standing picture, an item's icon. Each line: kind, number, CG, use. It is not part of rq.ids.
local t = _G.t
local out = {}
local pictures = {}

local function picture(kind, no, cg, use)
  if type(cg) == "number" and cg > 0 then pictures[#pictures + 1] = { kind, no, math.floor(cg), use } end
end

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
    local okinfo, info = pcall(rq.item.info, no)
    if it and it.name ~= "" and okinfo and info then picture("item", no, info.icon, "icon") end
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
      picture("chara", no, c.face, "face")
      picture("chara", no, c.portrait, "standing picture")
    else
      skipped = skipped + 1
    end
    if not had then rq.chara.dismiss(iic) end
  end
end
write("chara", charas)
out[#out + 1] = "chara NoName " .. skipped

-- Small lists: the game's name function for each number, with what it gives for "no name" left out.
local NONE = { [""] = true, ["___"] = true, ["?"] = true, ["？"] = true, ["＊＊＊"] = true, ["-----"] = true }
local function list(kind, fn, ...)
  local rows = {}
  for no = 0, 99 do
    local s = rq.ain.call(fn, no, ...)
    if type(s) == "string" and not NONE[s] and not s:match("^Type Details") then
      rows[#rows + 1] = { no, s, "" }
    end
  end
  write(kind, rows)
end
list("element", "属性文字列")
list("race", "種族文字列")
list("chara_type", "タイプ文字列")
list("item_kind", "Ｉ種類詳細／文字列")
list("target", "対象文字列")        -- in Japanese in the English game; gen_ids.py names them
list("class", "職業文字列", 0)      -- checks gen_ids.py's fixed list
list("state", "状態変化文字列")     -- the same; Japanese, and 0 has no name

-- Music: the game's track names (音楽の名前), for the tracks it has. The numbers go up to 213.
local tracks = {}
for no = 1, 299 do
  if rq.ain.call("音楽_あり", no) then
    local s = rq.ain.call("音楽の名前", no)
    if type(s) == "string" and not NONE[s] then tracks[#tracks + 1] = { no, s, "" } end
  end
end
write("music", tracks)

-- Monsters: each number loaded into slot 1, the scratch slot the game uses for the same thing
-- (モンスター固有番号文字列), read, and cleared. A number with no monster leaves the name empty.
local monsters = {}
for no = 1, 999 do
  rq.ain.call("Ｃ初期化", 1)
  rq.ain.call("Ｃ敵登録", 1, no)
  local name = rq.ain.call("Ｃ名前", 1, "")
  if type(name) == "string" and name ~= "" then
    -- 448 takes a party member's short name ("Rance CopyPaste"); keep the name it is registered with
    if name:match(" CopyPaste$") then name = "CopyPaste" end
    local c = rq.chara.get(1) or {}
    monsters[#monsters + 1] = { no, name, string.format("%d,%d", c.level or 0, c.race or 0) }
  end
end
rq.ain.call("Ｃ初期化", 1)
write("monster", monsters)

-- Quests: the game's quest list. 303 and up are mods' quests (rq.quests.define), such as the Lab's.
local quests = {}
for _, q in ipairs(rq.quests()) do
  if q.id < 303 then quests[#quests + 1] = { q.id, q.name, "" } end
end
write("quest", quests)

do
  local f = assert(io.open("ids-pictures.tsv", "wb"))
  f:write("# The game's pictures for rq.ids' names, for the file browser (rq-browser's Names tab). Not part of rq.ids.\n")
  f:write("# Written by tools/ids/extract.lua (scripts\\ids-extract.ps1). kind, number, CG, use.\n")
  for _, r in ipairs(pictures) do f:write(r[1], "\t", r[2], "\t", r[3], "\t", r[4], "\n") end
  f:close()
  out[#out + 1] = "pictures " .. #pictures
end

t.log(table.concat(out, ", "))
