# API docs tools

`dist/lua/rq.lua` is the single source of the Lua API reference. Editors read it through the Lua
language server (LuaLS); `rqdoc.py` turns it into JSON and Markdown pages for the docs site; the
coverage check compares it with what the running engine really exposes.

```sh
python tools/apidoc/rqdoc.py json  dist/lua/rq.lua api.json        # the model, for other tools
python tools/apidoc/rqdoc.py md    dist/lua/rq.lua out/             # one page per namespace and type
python tools/apidoc/rqdoc.py html  dist/lua/rq.lua api.html data/ids/ids.json   # one offline page
python tools/apidoc/rqdoc.py stats dist/lua/rq.lua                  # what still needs the new format
python tools/apidoc/rqdoc.py cover dist/lua/rq.lua apidump.json     # undocumented / stale entries
```

`html` writes the release zip's `xsystem4/api.html` (the release workflow runs it): the same pages
as `md` in one file with no network, a search box (names, summaries and, with `ids.json`, the
game's numbers), both colour schemes, links between pages as anchors in the page.

`apidump.json` comes from the running game: `rqmod-ctl apidump > apidump.json` (the workspace
script `check-api-docs.ps1` boots the game and does both steps). The dump is taken after the
engine builds `rq` and before any mod runs, so it holds only the engine's API.

The coverage check skips names with a part starting with `_` (internal, not for mods) and the keys
inside a documented data field (for example the pieces of `rq.uikit.theme`).

## How to write an entry

Every public function, field, callback and type gets an entry. Write for a mod author who has
never seen the code. Say what it does now; history goes in `CHANGELOG.md`, not in the comment.

```lua
---Sets the game clock to `factor` times real time.          <- summary: one sentence, a verb
---
---The clock does not jump: game time goes on from where it    <- details: paragraphs, Markdown
---was, at the new rate.
---
---Since: 1.2.0                                               <- version that added it
---Changed: 1.3.0 Clamped to 0.25..8 (was 0.5..4).            <- optional, one line per change
---
---```lua                                                     <- optional example, runs as written
---rq.speed.set(2)
---```
---@param factor number How many times faster. Clamped to 0.25..8.
---@return number factor The factor now in use, after clamping.
---@see rq.speed.get
function speed.set(factor) end
```

Rules:

- **Summary**: the first paragraph, one sentence, starting with a verb ("Returns", "Sets",
  "Calls ... when ..."). For a namespace, what it is for.
- **Details**: what a mod author needs to use it right: units, ranges, when it runs, what it
  costs, what it does not do. Name other calls in backticks: `` `rq.time_ms` ``.
- **`Since:`** the loader version that added it. **`Changed:`** a version and what changed in
  behavior a mod could notice. Keep these on their own lines; the generator turns them into
  badges.
- **Example**: a `lua` fenced block that works if pasted into a mod.
- **`@param` / `@field`**: name, type, then a description that says the unit or range. Add `?`
  to the name when it may be left out, and say the default: `---@param n? integer Count. Default 1.`
- **`@return`**: type, a name, and what it means. One line per value.
- **Callbacks** (`on_*`): `@param fn fun(...)` with named arguments, and say when it runs and
  what its return value does. They return a function that removes the callback.
- **`@see`**: related calls by full name.
- Write `---Loader X.Y.Z.` prefixes no more; they are the old format and `stats` lists them.

These are plain LuaLS comments, so the editor shows the same text on hover.
