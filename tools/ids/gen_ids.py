"""Builds rq.ids from the names in data/ids.

  gen_ids.py           write src/lua/ids.lua, dist/lua/rq_ids.lua and data/ids/ids.json
  gen_ids.py --check   exit 1 when one of them is not what the data gives (for CI)

data/ids/<kind>.tsv comes from the game (tools/ids/extract.lua, run by the workspace's
scripts/ids-extract.ps1): number, name, detail. Classes and states keep the fixed lists below, and
their TSVs check them. The game names skill targets in Japanese only, so TARGET names them.

data/ids/preferred.tsv gives numbers of a repeated name more keys beside their numbered ones:
kind, name, number, key, why. An empty key is the plain key (FORTUNE beside FORTUNE_448); a key
can also be its own (CAPTURE_EX, USE_IBEPROFUN_EX). Hand-made; such a key never moves once released.

Keys are the English name in upper snake case: FORTUNE, KAORU_QUINCY, SKILL_AGAIN. A name the
game uses for more than one number gives each of them its number as a suffix (FORTUNE_158,
FORTUNE_448), so no number takes the plain key by chance and a key never moves to another number
when the data is read again. A key that would start with a digit starts with N_.
"""
import json
import os
import re
import sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(__file__), "..", ".."))
DATA = os.path.join(ROOT, "data", "ids")
OUT_LUA = os.path.join(ROOT, "src", "lua", "ids.lua")
OUT_META = os.path.join(ROOT, "dist", "lua", "rq_ids.lua")
OUT_JSON = os.path.join(DATA, "ids.json")

# kind, file or fixed rows, what the docs call it, what the number is
KINDS = [
    ("chara", "chara.tsv", "Characters", "character numbers (`no`), not roster slots: `rq.chara.find(rq.ids.chara.KAORU_QUINCY)` gives the slot"),
    ("chara_type", "chara_type.tsv", "Character types", "character type values (`タイプ`), as in `rq.chara.get(iic).type`"),
    ("class", None, "Classes", "class numbers, as in `rq.chara.get(iic).class`"),
    ("element", "element.tsv", "Elements", "element numbers (`属性`), as in a skill's `element` and `rq.chara.get(iic).weak_element`"),
    ("item", "item.tsv", "Items", "item numbers, not instances: `rq.item.give(rq.ids.item.SKILL_AGAIN)` makes one"),
    ("item_kind", "item_kind.tsv", "Item kinds", "item kinds (`種類詳細`): the weapon or armor kind, as in an item's `subtype`"),
    ("map_kind", None, "Map kinds", "map kinds (`◆ＭＡＰ基礎型`): the look of a quest's area, as in `rq.dungeon.on_area`'s `ctx.kind`. The game names none; these names are ours, after the picture the game gives each kind"),
    ("monster", "monster.tsv", "Monsters", "monster numbers, as `rq.battle.start` takes them and `rq.battle.snap()` gives them (`enemies[i].no`)"),
    ("music", "music.tsv", "Music", "music track numbers, as `rq.music.replace` and `rq.music.play` take them and `rq.music.current()` gives them"),
    ("party_state", None, "Party states", "where a character is (`登録状態`), as `rq.party.state` and `rq.chara.get(iic).state` give it and `rq.party.roster` takes it. The game names none; these names are ours, after its functions `キャラ_スタメン` to `キャラ_２軍`"),
    ("quest", "quest.tsv", "Quests", "quest numbers, as in `rq.quests()` and `rq.quests.start`"),
    ("race", "race.tsv", "Races", "races (`種族`), as in `rq.chara.get(iic).race` and the damage hooks' `ctx.race`"),
    ("skill", "skill.tsv", "Skills", "skill numbers, as in `rq.skill.get`"),
    ("state", None, "States", "status values (`状態変化`), as in `rq.chara.get(iic).status`"),
    ("target", "target.tsv", "Skill targets", "skill target codes (`対象`), as in a skill's `target`"),
]

# The game's names for skill targets (対象文字列) are Japanese in the English game too.
TARGET = {"近距離": "Melee", "遠距離": "Ranged", "全体": "All Enemies", "横一列": "Row",
          "自分": "Self", "味方": "Ally", "味方全体": "All Allies", "ランダムターゲット": "Random Target"}

# The game's names for states (状態変化文字列), to check FIXED["state"].
STATE_JA = {"毒": "Poison", "ぴより": "Stun", "瀕死": "Downed"}

FIXED = {
    # modloader_query.c job_name: the game's class numbers
    "class": [(1, "Fighter"), (2, "Guard"), (3, "Gunner"), (4, "Sorcerer"), (5, "Healer"),
              (6, "Ranger"), (7, "Brawler"), (8, "Civilian")],
    # modloader_states.c: 状態変化
    "state": [(0, "None"), (1, "Poison"), (2, "Stun"), (3, "Downed")],
    # C_キャラ.jaf: 登録状態, where a character is (キャラ_スタメン, _１軍, _１軍使用済み, _２軍)
    "party_state": [
        (0, "None", "not recruited"),
        (1, "Party", "one of the five ranks (rq.party.get)"),
        (2, "Reserve", "the first army, free to join the party"),
        (3, "Used", "the first army, switched out in this quest: not placed again until the quest ends"),
        (4, "Second", "the second army"),
    ],
    # ＭＡＰ基礎型登録 (ＴＡＤＡ/関数／.jaf): the kinds the quests' scripts set, named by their look
    # (each kind's CG, gＭ 5610 to 5795); detail: places.
    "map_kind": [
        (1, "Cave", "Epiphany Cave, Cripple Dungeon, Despair"),
        (2, "Forest", "Kalar Forest, Mushroom Forest"),
        (3, "Building", "a department store, Hanny Secret Base, Zombie Room"),
        (4, "Snow", "Mount Hakkoda, Yukihira, Prohibited Area"),
        (5, "Castle", "Curse Castle, Tringer's Castle, Peperon"),
        (6, "Field", "Meg's Ranch, Clicquot Pasture, Hanny Village"),
        (7, "Mountain", "Rising Dragon Mountain"),
        (8, "Ruins", "a stone corridor: Lerolen, Wamasya, Abandoned Mine"),
        (9, "Water Cave", "Underwater Shrine, Squidsquidsquid Dungeon"),
        (10, "Burning Forest", "Kalar Forest on fire, in The Third Army of Helman"),
        (11, "Tower", "Tower of Babel"),
        (12, "Road", "Route 11 to 13, Arizona, Mesopotamia"),
        (13, "Waterside", "Kawanakajima"),
        (14, "Malgrid", "Malgrid Dungeon, strata 1 to 10"),
        (15, "Camp", "Cidade North Yard"),
        (16, "Abyss", "The Abyss, Suspicious Okayu's Lair"),
        (17, "Dream World", "Snicker World, Chortle World"),
        (18, "Kaiz Underground", "Kaiz Underground 12F to 20F"),
        (19, "Passageway", "Acetal Passageway, Boarding Passageway"),
        (20, "Malgrid X", "Malgrid Dungeon Xth Stratum, strata up to 15"),
        (21, "Malgrid X Deep", "Malgrid Dungeon Xth Stratum, strata past 15"),
        (22, "Secret Base", "the Shepherds' Secret Base"),
    ],
}

# Characters the game writes in its own font codes
SPELL = {"･": "e", "é": "e"}
WORDS = {"&": " and ", "+": " plus ", "△": " triangle "}

ACTION = {"0": "ability", "1": "physical", "2": "magic", "3": "special", "4": "support"}
CLASS = {str(n): name for n, name in FIXED["class"]}


def key_of(name):
    s = "".join(SPELL.get(c, c) for c in name)
    for a, b in WORDS.items():
        s = s.replace(a, b)
    s = re.sub(r"(?<=\d)\.(?=\d)", "_", s)   # Athena 2.0 -> ATHENA_2_0
    s = re.sub(r"['.!?#~]", "", s)           # Lia's -> LIAS, P. -> P
    s = re.sub(r"[^A-Za-z0-9]+", "_", s).strip("_").upper()
    if not s:
        s = "UNNAMED"
    if s[0].isdigit():
        s = "N_" + s
    return s


def read_tsv(fname):
    rows = []
    with open(os.path.join(DATA, fname), encoding="utf-8") as f:
        for line in f:
            line = line.rstrip("\r\n")
            if not line or line.startswith("#"):
                continue
            rows.append((line.split("\t") + ["", "", "", ""])[:5])
    return rows


def check_fixed(kind, names):
    """The game's own list must give the numbers and names of FIXED[kind] (0, None, is ours)."""
    if not os.path.exists(os.path.join(DATA, kind + ".tsv")):
        return
    game = {int(no): names.get(name, name) for no, name, *_ in read_tsv(kind + ".tsv")}
    ours = {n: name for n, name, *_ in FIXED[kind] if n != 0}
    if game != ours:
        sys.exit("gen_ids: the game's %s list %r differs from FIXED %r" % (kind, game, ours))


def race_names():
    return {no: name for no, name, *_ in read_tsv("race.tsv")}


def read_kind(kind, fname):
    if fname is None:
        check_fixed(kind, STATE_JA if kind == "state" else {})
        return [{"no": n, "name": name, "detail": (rest or [""])[0]} for n, name, *rest in FIXED[kind]]
    rows = []
    for no, name, detail, *_ in read_tsv(fname):
        if kind == "skill":
            detail = ACTION.get(detail, detail)
        elif kind == "chara":
            detail = CLASS.get(detail, detail)
        elif kind == "monster":
            lv, race = (detail.split(",") + ["0", "0"])[:2]
            detail = "Lv %s, %s" % (lv, race_names().get(race, "race " + race))
        elif kind == "target":
            if name not in TARGET:
                sys.exit("gen_ids: no English name for skill target %s %s (add it to TARGET)" % (no, name))
            name, detail = TARGET[name], name
        rows.append({"no": int(no), "name": name, "detail": detail})
    rows.sort(key=lambda r: r["no"])
    return rows


def read_preferred():
    """[(kind, name, number, key)]; key is the plain key when the file leaves it empty."""
    out = []
    if os.path.exists(os.path.join(DATA, "preferred.tsv")):
        for kind, name, no, key, _ in read_tsv("preferred.tsv"):
            if kind != "kind":  # the header
                out.append((kind, name, int(no), key or key_of(name)))
    return out


def with_keys(kind, rows, preferred):
    groups = {}
    for r in rows:
        groups.setdefault(key_of(r["name"]), []).append(r)
    for key, members in groups.items():
        for r in members:
            r["key"] = key if len(members) == 1 else "%s_%d" % (key, r["no"])
    taken = {r["key"] for r in rows}
    for pkind, name, no, key in preferred:
        if pkind != kind:
            continue
        members = groups.get(key_of(name), [])
        if len(members) < 2:
            sys.exit("gen_ids: preferred.tsv: %s %r is not a repeated name" % (kind, name))
        hit = [r for r in members if r["no"] == no]
        if not hit:
            sys.exit("gen_ids: preferred.tsv: %s %r has no number %d" % (kind, name, no))
        if key in taken or key != key_of(key):
            sys.exit("gen_ids: preferred.tsv: %s key %s is taken or not upper snake case" % (kind, key))
        taken.add(key)
        hit[0].setdefault("aliases", []).append(key)
    return rows


def class_name(kind):
    return "rq.Ids" + "".join(p.capitalize() for p in kind.split("_"))


def model():
    out = {"kinds": []}
    preferred = read_preferred()
    known = {k[0] for k in KINDS}
    for pkind, *_ in preferred:
        if pkind not in known:
            sys.exit("gen_ids: preferred.tsv: unknown kind %s" % pkind)
    for kind, fname, title, what in KINDS:
        out["kinds"].append({"kind": kind, "title": title, "what": what,
                             "entries": with_keys(kind, read_kind(kind, fname), preferred)})
    return out


def lua_str(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


RUNTIME = r'''
-- How find and find_all compare: lower case, é as e, nothing but letters and digits.
local function fold(s)
  s = s:gsub("\239\189\165", "e"):gsub("\195\169", "e")
  return (s:lower():gsub("[^%w]", ""))
end

local ids = {}
local names = {}
local extra = {}  -- kind = { folded preferred key = number }, for find

for kind, list in pairs(DATA) do
  local t, byno = {}, {}
  for i = 1, #list, 3 do
    t[list[i + 1]] = list[i]
    byno[list[i]] = list[i + 2]
  end
  names[kind] = byno
  extra[kind] = {}
  for key, no in pairs(ALIAS[kind] or {}) do
    t[key] = no
    extra[kind][fold(key)] = no
  end
  ids[kind] = setmetatable(t, {
    __index = function(_, k)
      error("rq.ids." .. kind .. " has no " .. tostring(k) .. " (rq.ids.find looks a name up)", 2)
    end,
    __newindex = function() error("rq.ids." .. kind .. " cannot be changed", 2) end,
  })
end

local function kind_of(kind)
  if type(kind) ~= "string" or not DATA[kind] then
    error("rq.ids: unknown kind " .. tostring(kind) .. " (" .. KINDS .. ")", 3)
  end
  return DATA[kind]
end

function ids.find(kind, name)
  local list = kind_of(kind)
  if type(name) ~= "string" then error("rq.ids.find: the name must be a string", 2) end
  local want, hits, by_key = fold(name), {}, {}
  for i = 1, #list, 3 do
    if fold(list[i + 2]) == want then hits[#hits + 1] = list[i] end
    if fold(list[i + 1]) == want then by_key[#by_key + 1] = list[i] end
  end
  -- A repeated name with a plain key gives that number; a key of its own is found as a key.
  local named = extra[kind][want]
  if #hits > 1 and named then return named end
  if #hits == 0 then hits = by_key end
  if #hits == 0 and named then return named end
  if #hits == 1 then return hits[1] end
  if #hits == 0 then return nil, "no " .. kind .. " named " .. string.format("%q", name) end
  table.sort(hits)
  return nil, "more than one " .. kind .. " is named " .. string.format("%q", name) .. ": "
    .. table.concat(hits, ", ")
end

function ids.find_all(kind, text)
  local list = kind_of(kind)
  if type(text) ~= "string" then error("rq.ids.find_all: the text must be a string", 2) end
  local want, out = fold(text), {}
  for i = 1, #list, 3 do
    if fold(list[i + 2]):find(want, 1, true) then out[#out + 1] = list[i] end
  end
  table.sort(out)
  return out
end

function ids.name(kind, no)
  kind_of(kind)
  return names[kind][no]
end

rq.ids = setmetatable(ids, {
  __newindex = function() error("rq.ids cannot be changed", 2) end,
})
'''


def write_runtime(m):
    lines = ["-- rq.ids (since loader 1.15.0): the game's numbers by name.",
             "-- Generated by tools/ids/gen_ids.py from data/ids. Do not edit.",
             "local rq = ...", "", "-- kind = { number, key, name, ... }", "local DATA = {"]
    for k in m["kinds"]:
        lines.append("  %s = {" % k["kind"])
        for r in k["entries"]:
            lines.append("    %d, %s, %s," % (r["no"], lua_str(r["key"]), lua_str(r["name"])))
        lines.append("  },")
    lines.append("}")
    lines += ["", "-- kind = { key = number }: more keys for numbers of a repeated name (data/ids/preferred.tsv)",
              "local ALIAS = {"]
    for k in m["kinds"]:
        al = [(a, r["no"]) for r in k["entries"] for a in r.get("aliases", [])]
        if al:
            lines.append("  %s = {" % k["kind"])
            lines += ["    %s = %d," % (a, no) for a, no in al]
            lines.append("  },")
    lines += ["}", "", "local KINDS = %s" % lua_str(", ".join(k["kind"] for k in m["kinds"]))]
    return "\n".join(lines) + "\n" + RUNTIME


def write_meta(m):
    lines = ["---@meta", "-- The game's numbers by name, for `rq.ids` (since loader 1.15.0).",
             "-- Generated by tools/ids/gen_ids.py from data/ids. Do not edit.", ""]
    for k in m["kinds"]:
        lines += ["---%s by name: %s." % (k["title"], k["what"]), "---",
                  "---A key that is not here raises an error; `rq.ids.find` looks a name up.",
                  "---@class %s" % class_name(k["kind"])]
        for r in k["entries"]:
            extra = " (%s)" % r["detail"] if r["detail"] else ""
            lines.append("---@field %s integer %d: %s%s" % (r["key"], r["no"], r["name"].replace("\n", " "), extra))
            for a in r.get("aliases", []):
                what = "The plain key" if a == key_of(r["name"]) else "A key of its own"
                lines.append("---@field %s integer %d: %s%s. %s; also `%s`." % (
                    a, r["no"], r["name"].replace("\n", " "), extra, what, r["key"]))
        lines.append("")
    return "\n".join(lines)


def write_json(m):
    return json.dumps(m, ensure_ascii=False, indent=1) + "\n"


def main(argv):
    m = model()
    outs = [(OUT_LUA, write_runtime(m)), (OUT_META, write_meta(m)), (OUT_JSON, write_json(m))]
    if "--check" in argv:
        bad = []
        for path, text in outs:
            old = open(path, encoding="utf-8").read() if os.path.exists(path) else None
            if old != text:
                bad.append(os.path.relpath(path, ROOT))
        for b in bad:
            print("out of date: %s (run tools/ids/gen_ids.py)" % b)
        return 1 if bad else 0
    for path, text in outs:
        with open(path, "w", encoding="utf-8", newline="\n") as f:
            f.write(text)
    print("; ".join("%s %d" % (k["kind"], len(k["entries"])) for k in m["kinds"]))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
