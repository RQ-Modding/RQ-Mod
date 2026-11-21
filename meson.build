project('system4', 'c',
        meson_version: '>= 0.59',
        default_options : ['c_std=c11'])
add_project_arguments('-D_DEFAULT_SOURCE', language : 'c')
add_project_arguments('-DXSYS4_DATA_DIR="' + get_option('prefix') / get_option('datadir') / 'xsystem4"', language : 'c')

if get_option('buildtype').startswith('release')
    add_project_arguments('-DRELEASE', language : 'c')
endif

static_libs = false
if host_machine.system() == 'windows'
    static_libs = true
endif

zlib = dependency('zlib', static : static_libs)
libm = meson.get_compiler('c').find_library('m', required: false)
sdl2 = dependency('sdl2', static : static_libs)
ft2 = dependency('freetype2', static : static_libs)
ffi = dependency('libffi', static : static_libs)
tj = dependency('libturbojpeg', static : static_libs)
webp = dependency('libwebp', static : static_libs)
png = dependency('libpng', static : static_libs)
# cglm from its wrap (subprojects/cglm.wrap), static, in every build: MSYS2's mingw64 repository
# dropped its package (2026-11), so the release's build and a local one take the same cglm. Its
# license goes into the zip from dist/xsystem4/licenses/cglm.
cglm = subproject('cglm', default_options : ['default_library=static', 'install=false']).get_variable('cglm_dep')
sndfile = dependency('sndfile', static : static_libs)

avcodec = dependency('libavcodec', version : '>=59.37.100', required: false, static : static_libs)
avformat = dependency('libavformat', required: false, static : static_libs)
avutil = dependency('libavutil', version : '>=57.28.100', required: false, static : static_libs)
swscale = dependency('libswscale', required: false, static : static_libs)

gles = dependency('glesv2', version : '>=3', required: get_option('opengles'))
if gles.found()
    gl_deps = [gles]
    add_project_arguments('-DUSE_GLES', language : 'c')
else
    gl = dependency('GL')
    glew = dependency('glew')
    gl_deps = [gl, glew]
endif

chibi = dependency('chibi-scheme', required : false, static : static_libs)
readline = dependency('readline', required : false, static : static_libs)

flex = find_program('flex')
bison = find_program('bison')

incdir = include_directories('include')

libsys4_proj = subproject('libsys4')
libsys4_dep = libsys4_proj.get_variable('libsys4_dep')

subdir('src')
if host_machine.system() == 'windows' and get_option('modloader')
    subdir('tools/rqmod-ctl')
    subdir('tools/rqmod-aff')
    # Rance Browser (rance-browser.exe), the file browser, in its own repository
    # (subprojects/rance-browser, a git submodule). Built here with the loader's own names and rq.lua,
    # so its Names tab and Lua lines are this loader's; meson shares the engine's libsys4 with it.
    rqmod_version = run_command(find_program('python3', 'python'), '-c',
        'import re,sys; print(re.search(r"#define RQ_VERSION \"([^\"]+)\"", open(sys.argv[1], encoding="utf-8").read()).group(1))',
        files('include/modloader.h'), check : true).stdout().strip()
    subproject('rance-browser', default_options : [
        'rq_ids_dir=' + (meson.project_source_root() / 'data/ids'),
        'rq_lua=' + (meson.project_source_root() / 'dist/lua/rq.lua'),
        'build_note=RQ MOD ' + rqmod_version])
    # RQ Scene Editor (rq-scene-editor.exe), the window that writes a mod's scene files, in its own
    # repository (subprojects/rq-scene-editor, a git submodule). Built here with the loader's own
    # scene checker, so the editor and the game check a file with the same Lua file; it reads the
    # game through Rance Browser's library, and meson shares Rance Browser and libsys4 with it.
    subproject('rq-scene-editor', default_options : [
        'scene_check_lua=' + (meson.project_source_root() / 'src/lua/scene_check.lua'),
        'build_note=RQ MOD ' + rqmod_version])
endif

install_subdir('shaders', install_dir : get_option('datadir') / 'xsystem4')
install_subdir('fonts', install_dir : get_option('datadir') / 'xsystem4')
install_data('debugger.scm', install_dir : get_option('datadir') / 'xsystem4')
