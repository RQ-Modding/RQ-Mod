<p align="center"><img src="res/rq-mod.png" width="96" alt="RQ MOD icon"></p>

<h1 align="center">RQ MOD</h1>

<p align="center">Mod support for <b>Rance Quest Magnum</b>: write mods in Lua and load them at runtime, with no decompiling or recompiling of the game.</p>

---

RQ MOD is a fork of [xsystem4](https://github.com/nunuhara/xsystem4), the open-source implementation of AliceSoft's System 4 engine, made for Rance Quest Magnum only. It adds a Lua mod loader and an `rq.*` API that lets mods change the HUD, skills, items, characters, battles, quests and more.

> [!IMPORTANT]
> The engine itself is the work of the xsystem4 authors (Nunuhara Cabbage, kichikuou and contributors). RQ MOD builds on it; see [CREDITS.md](CREDITS.md).

## Features

- Lua 5.4 mods loaded at startup from the `mods` folder, with dependencies, load order and conflicts declared in `mod.json`.
- A Mods tab in the game's Config screen, where each mod can add its own settings page.
- Debug overlay on F3 (mods can add to it).
- Logs and crash reports in the `logs` folder.
- Discord Rich Presence (can be turned off in Config, "Other 2").

RQ MOD targets Rance Quest Magnum on Windows. Other AliceSoft games are not supported; use [xsystem4](https://github.com/nunuhara/xsystem4) for those.

## Installing

1. Install Rance Quest Magnum.
2. Download the latest `RQ-MOD-*.zip` from the [Releases page](https://github.com/RQ-Modding/RQ-Mod/releases).
3. Extract it into the game folder, so it looks like this:

   ```text
   Rance Quest Magnum/
   ├── Data/
   ├── DLL/
   ├── Face CG/
   ├── lua/
   │   └── rq.lua            definitions for mod authors' editors
   ├── mods/                 put mods here, one folder each
   │   └── camera-zoom/
   │       ├── init.lua
   │       └── mod.json
   ├── xsystem4/
   │   ├── config.json
   │   └── xsystem4.exe      start the game with this
   └── RanceQuest.exe
   ```

4. Start the game with `xsystem4\xsystem4.exe`.

To add a mod, extract its folder into `mods`. To remove it, delete the folder. `logs\rqmod.log` lists each mod it loaded (`Loading <id>`) and says why it skipped any other.

## When something goes wrong

Everything is written to the `logs` folder next to `mods`:

| File | What it is |
| --- | --- |
| `rqmod.log` | This run's log. |
| `rqmod.prev.log` | The run before. |
| `rqmod-crash-<date>-<time>.log` | Written when the game crashes: what happened, where, the game-script call stack and the last log lines. |
| `rqmod-crash-<date>-<time>.dmp` | A small Windows minidump of the same moment. |
| `rqmod-unclean-<date>-<time>.log` | The log of a run that never closed cleanly (crash, kill, hang). |

When you report a problem, attach the crash `.log` (and the `.dmp` if it is small) and say which mods you had installed.

## Making mods

Mods are Lua 5.4. A mod is a folder in `mods` with an `init.lua` and a `mod.json`.

- API definitions for your editor (VS Code with the Lua extension): `lua/rq.lua`, and the game's numbers by name for `rq.ids` in `lua/rq_ids.lua`. The release includes a `.luarc.json` that points the Lua extension at it.
- Version history of the API: [CHANGELOG.md](CHANGELOG.md).
- Full modding documentation: coming soon at the RQ Modding organization.

## Building from source

Windows, with [MSYS2](https://www.msys2.org/). In the **MINGW64** shell:

```sh
pacman -S git flex bison mingw-w64-x86_64-gcc mingw-w64-x86_64-meson mingw-w64-x86_64-pkg-config \
  mingw-w64-x86_64-SDL2 mingw-w64-x86_64-freetype mingw-w64-x86_64-libjpeg-turbo \
  mingw-w64-x86_64-libwebp mingw-w64-x86_64-libpng mingw-w64-x86_64-libsndfile \
  mingw-w64-x86_64-glew mingw-w64-x86_64-python
git clone --recurse-submodules https://github.com/RQ-Modding/RQ-Mod.git
cd RQ-Mod
meson setup build --buildtype=release
ninja -C build
```

This gives `build/src/xsystem4.exe` and `build/tools/rqmod-ctl/rqmod-ctl.exe`. To run it, copy into the game's `xsystem4` folder: `xsystem4.exe`, `glew32.dll` (from `/mingw64/bin`), and the `fonts` and `shaders` folders. Then copy the contents of `dist/` into the game folder.

Movie playback with FFmpeg is optional; the [release workflow](.github/workflows/build.yml) shows how to build the small static FFmpeg it uses.

### Repository layout

| Path | What it is |
| --- | --- |
| `src/modloader*.c`, `src/lua/` | The mod loader and the `rq.*` API |
| `dist/` | Files shipped into the game folder (`config.json`, `lua/rq.lua`, `lua/rq_ids.lua`, `.luarc.json`) |
| `data/ids/` | The game's names and numbers behind `rq.ids`, read from the game by `tools/ids/extract.lua` |
| `tools/ids/` | `gen_ids.py` builds `src/lua/ids.lua`, `dist/lua/rq_ids.lua` and `data/ids/ids.json` from `data/ids` |
| `tools/apidoc/` | The API reference generator and the docs coverage check |
| `tools/rqmod-ctl/` | Command-line client for the agent pipe, used by test scripts |
| `res/` | Icon and Windows resources |
| `third_party/lua/` | Lua 5.4.7 |
| everything else | The xsystem4 engine |

## License

GPL-2.0-or-later, the same as xsystem4. See [COPYING](COPYING) and [CREDITS.md](CREDITS.md).
