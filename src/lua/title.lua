-- The RQ MOD Discord server from the title screen (loader 2.59.2): the engine's own, with or without
-- mods. A small Discord icon right of the version banner, at the bottom left of the main menu, while
-- the title waits for a choice (the banner's own rule). A click asks in the game's dialog
-- (rq.ui.confirm) whether to open the server's invite in the browser: Yes opens it, No leaves the
-- title as it was. Agents see the icon as the kit's button `button-rqmod-discord`.
-- Run by modloader_uikit.c with rq, the kit (rq.uikit) and its internals: title_banner (the banner's
-- box, or nil away from the title), discord_icon (the icon's CG number, 22 by 22, from
-- res/discord.png in the exe) and open_url.
local rq, ui, kit = ...

local URL = "https://discord.gg/maCKqfTGne"
local QUESTION = "Open the official RQ MOD Discord server\nin your browser?\n" .. URL
local BOX = 24 -- the button; the icon is 22 by 22 in its middle
local GAP = 8  -- from the banner's text to the button

local asking = false

rq.on_frame(function()
  local bx, by, bw, bh = kit.title_banner()
  if not bx then return end
  local icon = kit.discord_icon()
  if icon == 0 then return end
  local x, y = bx + bw + GAP, by + (bh - BOX) // 2
  if asking then
    -- The game's dialog shows, and its loop runs every rq.on_frame callback: the icon stays, as a
    -- picture only.
    rq.hud.cg(x + 1, y + 1, icon)
    return
  end
  if ui.icon_button(icon, { x = x, y = y, w = BOX, h = BOX, id = "rqmod-discord", label = "RQ MOD Discord",
      tip = "The official RQ MOD server on Discord." }) then
    asking = true
    local ok, yes = pcall(rq.ui.confirm, QUESTION)
    asking = false
    if not ok then error(yes, 0) end -- the HUD logs it; the icon works again on the next click
    if yes then kit.open_url(URL) end
  end
end)
