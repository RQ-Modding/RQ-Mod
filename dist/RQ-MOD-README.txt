RQ MOD: mod support for Rance Quest Magnum
==========================================

RQ MOD runs Rance Quest Magnum on its own engine, a fork of xsystem4, and loads
mods written in Lua. The game's own files stay as they are: RanceQuest.exe still
starts the game without mods.

RQ MOD is a public beta: the tools that help make new content are not done yet.


Install
-------

1. Extract this zip into the game's folder (the one with RanceQuest.exe), with
   its folders: xsystem4, lua and mods.
2. Start the game with xsystem4\xsystem4.exe. The window title shows "RQ MOD"
   and the version, and the main menu shows the version too, with a Discord
   icon beside it: click it to open the RQ MOD server in your browser (the
   game asks first).


Mods
----

RQ Manager installs, updates, turns off and removes the mods we checked, and
RQ MOD itself, with one click each:
https://rq-modding.github.io/rq-modding-docs/guide/manager

By hand: put each mod's folder in mods, with its mod.json and init.lua right
inside it (mods\camera-zoom\mod.json, not mods\camera-zoom\camera-zoom\mod.json).
Delete the folder to remove the mod. Restart the game after you add or remove
one. logs\rqmod.log has a "Loading <id>" line for each mod that loaded, and says
why for any other.

The mods we checked are on the Mods page, each with its download:
https://rq-modding.github.io/rq-modding-docs/mods
Other mods are shared by their authors.


Saves
-----

RQ MOD keeps its saves apart from the original game's, in
xsystem4\Rance Quest\SaveData. When the environment variable XSYSTEM4_HOME is
set, they go under that folder instead, and when only HOME is set, under
%HOME%\.xsystem4. It never writes the save in Documents\AliceSoft. Keep a copy
of the save folder before you try a mod that adds characters or items.


The network box
---------------

If the game shows "Failed to send data to server (getserverversion)", click
OK and play on: RQ MOD does not connect to the game's online server, and
nothing is lost. To stop the box, untick "Connect to network" in Config,
Other 2. It is off in a new install; a save copied from the original game may
have it on.


When something goes wrong
-------------------------

The logs folder, next to mods, holds rqmod.log (this run), rqmod.prev.log (the
run before) and, after a crash, rqmod-crash-<date>-<time>.log and .dmp. When
you report a problem, attach the crash .log (and the .dmp if it is small) and
say which mods you had installed.


Making mods
-----------

Guides and the API reference: https://rq-modding.github.io/rq-modding-docs/
The same API reference offline: xsystem4\api.html
lua\rq.lua gives your editor (VS Code with the Lua extension) the rq.* calls.
A template to start a mod from: https://github.com/RQ-Modding/rq-mod-template
xsystem4\rq-scene-editor.exe (RQ Scene Editor) writes a mod's scenes without
Lua and plays them in the running game; xsystem4\rance-browser.exe (Rance
Browser) shows the game's pictures, music, sounds and animations; and
xsystem4\rqmod-aff.exe, on the command line, makes an animation the game
plays from PNG frames. Their guides are on the docs site.


Community
---------

RQ MOD on Discord, the official server: https://discord.gg/maCKqfTGne
Rance & Alicesoft Western Community on Discord: https://discord.gg/RANCE
Moru on X (Twitter): https://x.com/Moru_zerinho6


More
----

Source and releases: https://github.com/RQ-Modding/RQ-Mod
Credits and licenses: xsystem4\CREDITS.md, xsystem4\COPYING, xsystem4\licenses
Rance Quest Magnum is (c) AliceSoft. RQ MOD contains no game data; you need
your own copy of the game.
