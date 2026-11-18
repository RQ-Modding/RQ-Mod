RQ MOD: mod support for Rance Quest Magnum
==========================================

RQ MOD runs Rance Quest Magnum on its own engine, a fork of xsystem4, and loads
mods written in Lua. The game's own files stay as they are: RanceQuest.exe still
starts the game without mods.


Install
-------

1. Extract this zip into the game's folder (the one with RanceQuest.exe), with
   its folders: xsystem4, lua and mods.
2. Start the game with xsystem4\xsystem4.exe. The window title shows "RQ MOD"
   and the version, and the main menu shows the version too.


Mods
----

Put each mod's folder in mods, with its mod.json and init.lua right inside it
(mods\camera-zoom\mod.json, not mods\camera-zoom\camera-zoom\mod.json). Delete
the folder to remove the mod. Restart the game after you add or remove one.
logs\rqmod.log has a "Loading <id>" line for each mod that loaded, and says why
for any other.

Mods are shared by their authors. Some can be found in the RQ-Modding
organization's repositories, https://github.com/RQ-Modding, until the RQ MOD
site lists the checked mods.


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


More
----

Source and releases: https://github.com/RQ-Modding/RQ-Mod
Credits and licenses: xsystem4\CREDITS.md, xsystem4\COPYING, xsystem4\licenses
Rance Quest Magnum is (c) AliceSoft. RQ MOD contains no game data; you need
your own copy of the game.
