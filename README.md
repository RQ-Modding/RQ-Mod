<p align="center"><img src="res/rq-mod.png" width="96" alt="RQ MOD icon"></p>

<h1 align="center">RQ MOD</h1>

<p align="center">Mod support for <b>Rance Quest Magnum</b>: write mods in Lua and load them at runtime, with no decompiling or recompiling of the game.</p>

---

RQ MOD is a fork of [xsystem4](https://github.com/nunuhara/xsystem4), the open-source implementation of AliceSoft's System 4 engine, made for Rance Quest Magnum only. It adds a Lua mod loader and an `rq.*` API that lets mods change the HUD, skills, items, characters, battles, quests and more.

> [!NOTE]
> RQ MOD is a **public beta**: the tools that help make new content are not done yet.

> [!IMPORTANT]
> The engine itself is the work of the xsystem4 authors (Nunuhara Cabbage, kichikuou and contributors). RQ MOD builds on it; see [CREDITS.md](CREDITS.md).

## Features

- Lua 5.4 mods loaded at startup from the `mods` folder, with dependencies, load order and conflicts declared in `mod.json`.
- A Mods tab in the game's Config screen, where each mod can add its own settings page.
- Debug overlay on F3 (mods can add to it).
- Logs and crash reports in the `logs` folder.
- Discord Rich Presence (can be turned off in Config, "Other 2").
- A link to the RQ MOD Discord server on the main menu, by the version (the game asks before it opens the browser).

RQ MOD targets Rance Quest Magnum on Windows. Other AliceSoft games are not supported; use [xsystem4](https://github.com/nunuhara/xsystem4) for those.

## Installing

The easy way is [RQ Manager](https://github.com/RQ-Modding/rq-manager): it installs RQ MOD and the mods you pick, keeps them up to date, turns mods off and on, and puts the last version back. Get it from the [Download page](https://rq-modding.github.io/rq-modding-docs/download), unzip it into the game folder and start `RQ Manager\rq-manager.exe`. By hand:

1. Install Rance Quest Magnum.
2. Download the latest `RQ-MOD-*.zip` from the [Download page](https://rq-modding.github.io/rq-modding-docs/download) (or the [Releases page](https://github.com/RQ-Modding/RQ-Mod/releases)).
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
   │   ├── api.html          the API reference, offline
   │   ├── config.json
   │   └── xsystem4.exe      start the game with this
   ├── RanceQuest.exe
   └── RQ-MOD-README.txt     these steps, in short
   ```

4. Start the game with `xsystem4\xsystem4.exe`.

To add a mod by hand, extract its folder into `mods`. To remove it, delete the folder. `logs\rqmod.log` lists each mod it loaded (`Loading <id>`) and says why it skipped any other.

The mods we checked are on the [Mods page](https://rq-modding.github.io/rq-modding-docs/mods), each with its download. Other mods are shared by their authors.

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
- Guides and the API reference: [rq-modding.github.io/rq-modding-docs](https://rq-modding.github.io/rq-modding-docs/). The release has the reference offline too, in `xsystem4\api.html`.
- A template to start a mod from, with a workflow that publishes its release zip: [rq-mod-template](https://github.com/RQ-Modding/rq-mod-template).
- Two programs in the release's `xsystem4` folder: [RQ Scene Editor](https://github.com/RQ-Modding/rq-scene-editor) (`rq-scene-editor.exe`) writes a mod's scenes as files, without Lua, and plays them in the running game; [Rance Browser](https://github.com/RQ-Modding/rance-browser) (`rance-browser.exe`) shows the game's pictures, music and sounds, each with the Lua line that replaces or plays it.

## Community

- [RQ MOD on Discord](https://discord.gg/maCKqfTGne), the official server
- [Rance & Alicesoft Western Community](https://discord.gg/RANCE) on Discord
- [Moru on X (Twitter)](https://x.com/Moru_zerinho6)

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

This gives `build/src/xsystem4.exe` and `build/tools/rqmod-ctl/rqmod-ctl.exe`, and from the submodules `build/subprojects/rance-browser/rance-browser.exe` and `build/subprojects/rq-scene-editor/rq-scene-editor.exe`. To run it, copy into the game's `xsystem4` folder: `xsystem4.exe`, `glew32.dll` (from `/mingw64/bin`), and the `fonts` and `shaders` folders. Then copy the contents of `dist/` into the game folder.

meson takes [cglm](https://github.com/recp/cglm) from GitHub at the first setup (`subprojects/cglm.wrap`) and builds it into the exe.

Rance Quest plays no movie, so the release has no FFmpeg: meson builds xsystem4's movie player on pl_mpeg (`include/pl_mpeg.h`), and on [FFmpeg](https://ffmpeg.org/) only when it finds one installed.

### Repository layout

| Path | What it is |
| --- | --- |
| `src/modloader*.c`, `src/lua/` | The mod loader and the `rq.*` API |
| `dist/` | Files shipped into the game folder (`RQ-MOD-README.txt`, `config.json`, `lua/rq.lua`, `lua/rq_ids.lua`, `.luarc.json`, two license files MSYS2 does not install) |
| `data/ids/` | The game's names and numbers behind `rq.ids`, read from the game by `tools/ids/extract.lua` |
| `tools/ids/` | `gen_ids.py` builds `src/lua/ids.lua`, `dist/lua/rq_ids.lua` and `data/ids/ids.json` from `data/ids` |
| `tools/apidoc/` | The API reference generator and the docs coverage check |
| `tools/rqmod-ctl/` | Command-line client for the agent pipe, used by test scripts |
| `res/` | Icon and Windows resources |
| `third_party/lua/` | Lua 5.4.7 |
| `subprojects/` | libsys4, Rance Browser and RQ Scene Editor, as git submodules (the two programs have their own repositories) |
| everything else | The xsystem4 engine |

## License

GPL-2.0-or-later, the same as xsystem4. See [COPYING](COPYING) and [CREDITS.md](CREDITS.md), which also names the libraries in the Windows build: SDL2, FreeType, libsndfile and the rest, each with its license in the release zip.
