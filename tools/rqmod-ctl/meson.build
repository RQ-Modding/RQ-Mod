# rqmod-ctl.exe: sends commands to the running game over \.\pipe\rqmod.
executable('rqmod-ctl', 'ctl.c',
           c_args : ['-Wno-unused-parameter'],
           link_args : ['-static', '-lshell32'],
           win_subsystem : 'console',
           install : true)
