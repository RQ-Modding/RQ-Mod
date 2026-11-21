# rqmod-aff.exe: Rance Quest's Flash animations (.aff) from the command line: unpack one of the
# game's, check a file against what the engine plays, build one from PNG frames and WAV sounds.
# It checks with the engine's own rules (src/swf_check.c), as rq.image.add_flash does.
executable('rqmod-aff', 'aff.c', 'build.c', 'wav.c', '../../src/swf_check.c',
           include_directories : incdir,
           dependencies : [libsys4_dep, zlib, png, libm],
           c_args : ['-Wno-unused-parameter'],
           link_args : ['-static', '-lshell32'],
           win_subsystem : 'console',
           install : true)
