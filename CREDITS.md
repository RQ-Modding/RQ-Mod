# Credits

RQ MOD is a fork. Most of this code was written by other people.

| Project | Authors | License | Where |
| --- | --- | --- | --- |
| [xsystem4](https://github.com/nunuhara/xsystem4) | Nunuhara Cabbage, kichikuou and contributors | GPL-2.0-or-later | the engine (`src/`, `include/`, `shaders/`) |
| [libsys4](https://github.com/nunuhara/libsys4) | Nunuhara Cabbage and contributors | GPL-2.0-or-later | `subprojects/libsys4` |
| [Lua 5.4.7](https://www.lua.org/) | Lua.org, PUC-Rio | MIT | `third_party/lua` |
| [cJSON](https://github.com/DaveGamble/cJSON) | Dave Gamble and contributors | MIT | `src/cJSON.c` |
| [PL_MPEG](https://github.com/phoboslab/pl_mpeg) | Dominic Szablewski | MIT | `include/pl_mpeg.h`, the movie player (`src/movie_plmpeg.c`); Rance Quest plays no movie |
| [Rance Browser](https://github.com/RQ-Modding/rance-browser) | RQ Modding, with code of xsystem4 and libsys4 | GPL-2.0-or-later | `subprojects/rance-browser` (`rance-browser.exe`), its own repository |
| [Dear ImGui 1.92.9](https://github.com/ocornut/imgui) | Omar Cornut and contributors | MIT | in Rance Browser only (`subprojects/rance-browser/third_party/cimgui/imgui`) |
| [cimgui 1.92.9b](https://github.com/cimgui/cimgui) | Stephan Dilly and contributors | MIT | in Rance Browser only (`subprojects/rance-browser/third_party/cimgui`) |
| VL Gothic / VL PGothic | Daisuke Suzuki, Project Vine, M+ FONTS PROJECT and others | BSD-style, see `fonts/VL-*.license` | `fonts/` |
| Hanazono Mincho A | GlyphWiki project | Hanazono Font License / OFL, see `fonts/HanaMinA.license` | `fonts/` |

Copyright headers in source files belong to their authors and stay as they are.

## Libraries in the Windows build

The release's `xsystem4.exe` is linked with these libraries from [MSYS2](https://www.msys2.org/) (MINGW64), all but GLEW (`glew32.dll`) inside the exe. Each one's license is in the release zip under `xsystem4/licenses/<folder>`.

| Library | License | Folder |
| --- | --- | --- |
| [SDL2](https://www.libsdl.org/) | Zlib | `SDL2` |
| [FreeType](https://freetype.org/) | FreeType License or GPL-2.0 | `freetype` |
| [HarfBuzz](https://harfbuzz.github.io/) | MIT ("Old MIT") | `harfbuzz` |
| [Graphite](https://github.com/silnrsi/graphite) (graphite2) | LGPL-2.1-or-later, MPL-2.0, GPL-2.0-or-later or MIT | `graphite2` |
| [GLib](https://gitlab.gnome.org/GNOME/glib) | LGPL-2.1-or-later | `glib2` |
| [gettext](https://www.gnu.org/software/gettext/) runtime (libintl) | LGPL-2.1-or-later | `gettext-runtime` |
| [PCRE2](https://github.com/PCRE2Project/pcre2) | BSD-3-Clause with the PCRE2 exception | `pcre2` |
| [Brotli](https://github.com/google/brotli) | MIT | `brotli` |
| [bzip2](https://sourceware.org/bzip2/) | bzip2-1.0.6 (BSD-style) | `bzip2` |
| [zlib](https://zlib.net/) | Zlib | `zlib` |
| [libpng](http://www.libpng.org/pub/png/libpng.html) | libpng-2.0 | `libpng` |
| [libjpeg-turbo](https://libjpeg-turbo.org/) | IJG, BSD-3-Clause and Zlib | `libjpeg-turbo` |
| [libwebp](https://chromium.googlesource.com/webm/libwebp) (with sharpyuv and webpdemux) | BSD-3-Clause | `libwebp` |
| [libdeflate](https://github.com/ebiggers/libdeflate) | MIT | `libdeflate` |
| [libsndfile](https://libsndfile.github.io/libsndfile/) | LGPL-2.1-or-later | `libsndfile` |
| [FLAC](https://xiph.org/flac/) | BSD-3-Clause | `flac` |
| [libogg](https://xiph.org/ogg/) | BSD-3-Clause | `libogg` |
| [libvorbis](https://xiph.org/vorbis/) | BSD-3-Clause | `libvorbis` |
| [Opus](https://opus-codec.org/) | BSD-3-Clause | `opus` |
| [mpg123](https://www.mpg123.de/) | LGPL-2.1 | `mpg123` |
| [LAME](https://lame.sourceforge.io/) | LGPL-2.0-or-later (taken as LGPL-2.1) | `lame` |
| [libffi](https://sourceware.org/libffi/) | MIT | `libffi` |
| [GLEW](https://glew.sourceforge.net/) | BSD-3-Clause and MIT | `glew` |
| [cglm](https://github.com/recp/cglm) | MIT | `cglm` |
| GCC runtime (libgcc, libstdc++, libatomic) | GPL-3.0-or-later with the GCC Runtime Library Exception 3.1 | `gcc` |
| [mingw-w64](https://www.mingw-w64.org/) runtime (with its libssp) and winpthreads | ZPL-2.1, MIT and public domain | `crt`, `winpthreads` |

MSYS2 installs no license file for libvorbis and LAME, so this repository carries them in `dist/xsystem4/licenses/`. `rance-browser.exe` is linked with some of the same libraries; its own credits are in `xsystem4/licenses/rance-browser/`.

Rance Quest Magnum is © AliceSoft. RQ MOD contains no game data; you need your own copy of the game.

RQ MOD: the mod loader, the `rq.*` Lua API and the Rance Quest changes, by Moru and the RQ Modding contributors.
