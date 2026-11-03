-- rq.sprite (loader 1.9.0): the game hooks that end every mod sprite.
--
-- Every load of a save runs セーブファイル_読込 (the System menu's Load, the title's Load and
-- Continue, and the suspend save): it closes the screens and reads the save, so the screen
-- a mod sprite was made for is gone. The title screen starts with T_Title@create. Both
-- remove all mod sprites, then call each rq.sprite.on_reset with "load" or "title".
local rq, reset = ...
local hook = rq.ain.hook

hook("セーブファイル_読込", { before = function() reset("load") end })
hook("T_Title@create", { before = function() reset("title") end })
