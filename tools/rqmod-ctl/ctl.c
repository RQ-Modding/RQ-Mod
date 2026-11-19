/* rqmod-ctl: command-line client for the engine's agent pipe (named pipe "rqmod").
 * Used by test scripts to drive the running game. Windows only. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <shellapi.h>

#define PIPE_NAME "\\\\.\\pipe\\rqmod"

/* The arguments as UTF-8 (2.25.0), so a Japanese Flash file name reaches the engine
 * whole. The ANSI argv turns it into question marks. NULL on failure. */
static char **utf8_args(int *argc)
{
	LPWSTR *w;
	char **out;
	int n, i;

	w = CommandLineToArgvW(GetCommandLineW(), &n);
	if (!w)
		return NULL;
	out = (char **)calloc((size_t)n + 1, sizeof *out);
	for (i = 0; out && i < n; i++) {
		int len = WideCharToMultiByte(CP_UTF8, 0, w[i], -1, NULL, 0, NULL, NULL);
		out[i] = len > 0 ? (char *)malloc((size_t)len) : NULL;
		if (!out[i] || !WideCharToMultiByte(CP_UTF8, 0, w[i], -1, out[i], len, NULL, NULL)) {
			LocalFree(w);
			return NULL;
		}
	}
	LocalFree(w);
	if (!out)
		return NULL;
	*argc = n;
	return out;
}

int main(int argc, char **argv)
{
	char line[512];
	char *reply;
	HANDLE h;
	DWORD n, w, got;
	int i, used = 0;
	const int reply_cap = 128 * 1024;
	char **uargv;
	int uargc = 0;

	uargv = utf8_args(&uargc);
	if (uargv) {
		argc = uargc;
		argv = uargv;
	}
	if (argc < 2) {
		fprintf(stderr,
			"rqmod-ctl - send input to the running game (RQ MOD's xsystem4.exe).\n"
			"\n"
			"  rqmod-ctl ui                 JSON screen dump (one object)\n"
			"  rqmod-ctl mods               JSON mods:[{name,path}] of loaded mods\n"
			"  rqmod-ctl apidump            JSON of the engine's rq.* API, for the docs coverage check\n"
			"  rqmod-ctl facts              one unit, quest 126, dungeon cell, battle\n"
			"  rqmod-ctl hover <id>         move onto a control and wait until hovered\n"
			"  rqmod-ctl press <id>         click only if that control is hovered\n"
			"  rqmod-ctl drag <src> <dst>   hold-drag from one control onto another\n"
			"  rqmod-ctl dialog             JSON dump of an open native message box\n"
			"  rqmod-ctl ungrab             release the agent cursor\n"
			"  rqmod-ctl pause              hold the game on this frame (ui and shot still work)\n"
			"  rqmod-ctl resume             let the game run again\n"
			"  rqmod-ctl shot <path.png>    save the game surface as a PNG (ASCII path)\n"
			"  rqmod-ctl cgdump FROM TO DIR save the game's CGs in that number range as PNG\n"
			"  rqmod-ctl flashdump NAME DIR save every bitmap of a game Flash file as\n"
			"                               <id>_<w>x<h>.png; NAME as in rq.image.flash_bitmap\n"
			"                               (a bare name is Data/Flash/System/NAME.swf)\n"
			"  rqmod-ctl playscene MOD FILE [LABEL]\n"
			"                               play a scene file of mod MOD (a path in its folder),\n"
			"                               read anew, from LABEL if given; answers ok, err busy,\n"
			"                               or the file's problems, one per line\n"
			"  rqmod-ctl playscene stop     end a scene file before its next step\n"
			"  rqmod-ctl grid               JSON 2D room (walls, pits, events, player)\n"
			"  rqmod-ctl goto <x> <z>       walk to that map cell, avoiding event tiles\n"
			"  rqmod-ctl goto portal        walk to the eastmost teleporter in the room\n"
			"  rqmod-ctl step up|down|left|right\n"
			"                               one world-axis step (stops on a message or battle turn)\n"
			"\n"
			"  Screens: main-menu, config, hub, frostbuy, shop-buy, party-edit,\n"
			"  status, skill-shop, vault, vault-storage, select, system-menu, party-summary,\n"
			"  quests, quest-storage, dungeon, battle, message,\n"
			"  native-dialog (ui switches to native-dialog while a Windows\n"
			"  message box is up; hover = keyboard focus, e.g. hover dialog-ok\n"
			"  then press dialog-ok).\n"
			"  A game control under a mod's widget, or under a page that\n"
			"  captures the mouse, is \"covered\": true; press refuses it\n"
			"  (err covered by a mod's widget), as a player could not click it.\n"
			"  The kit control under the kit's keys cursor is \"focused\": true;\n"
			"  key down / key enter move and click it where the kit has the keys.\n"
			"  Hub ids: rance-castle frostbuy party quests, member-1..5,\n"
			"  status-1..5 and party-summary (hoverOnly, see \"visible\"),\n"
			"  system vault trivia, info: title money charisma charisma-progress\n"
			"  donations. System menu: load cg-mode music title-screen quit web\n"
			"  config backlog save.\n"
			"  Frostbuy: rack-special rack-weapon rack-armor rack-items, same\n"
			"  hub tabs + system bar. Shop-buy (storeType 3): item-N filters;\n"
			"  drag item-N drop-vault (or drop-0..4) to buy; drop list appears\n"
			"  mid-hold. Vault from shop: sell-N + sellFlag. Select: yes no.\n"
			"  Party edit: size-10 size-32, sort-join sort-high-lv sort-low-lv\n"
			"  sort-custom, filter-fighter..filter-civilian, call-a call-b call-c,\n"
			"  rank-1..5, unit-0.., unit-status-N, rank-status-N; JSON units[] + partyState.\n"
			"  Quests: quest-0..12 (value is the quest number), take, sort-quest-id,\n"
			"  scroll; JSON quests[] + questState. Quest storage: close, or yes/no\n"
			"  when Abandon opened it (storage.mode 1). Dungeon: area-show/area-hide,\n"
			"  storage, party, abandon; JSON dungeon{areaShown,mapNo,mapX,mapZ,name}.\n"
			"  Message: auto, skip, msg-config on the right of the text box.\n"
			"  Battle (ally turn): unit-<battleNo> hover rects (1-5 ally, 11-15\n"
			"  enemy), skill-<buttonId> for a visible skill (label is the skill\n"
			"  name, value is tone:uses:target:skillNo). tone is red for panel\n"
			"  100 and blue for panel 101. JSON battle{phase,actor,units,skills}.\n"
			"  Lua rq.agent.control(id, {x,y,w,h,label}) is a HUD rect, not a T_Button.\n"
			"  Status: close skills, equip-special/weapon/armor/item,\n"
			"  active-0..5 known-0..17 (value is the skill id), remove-special..\n"
			"  (hoverOnly); JSON status{stats,exp,skills, equipment.special}.\n"
			"  Skill shop: offer-N upgrade-N forget-N close; JSON skillShop{page,pages}.\n"
			"  Lua rq.agent.control rows appear while that button is shown.\n"
			"  Vault: close storage, item-N donate-N sell-N\n"
			"  lock-N unlock-N item-status-N, filter-special/weapon/armor/\n"
			"  item-other, rank-c.., weapon-bow, armor-plate, filter-fighter..,\n"
			"  sort-default sort-current, scroll-up/down; JSON items[] +\n"
			"  vaultState. Select: yes no magic.\n"
			"\n"
			"  rqmod-ctl hold f3            hold the debug menu (release f3 closes it)\n"
			"  rqmod-ctl key 6              Rendering Stats, while f3 is held\n"
			"  rqmod-ctl key +              Master +10%, while f3 is held\n"
			"  rqmod-ctl key -              Master -10%, while f3 is held\n"
			"  rqmod-ctl key m              Mute Master, while f3 is held\n"
			"  While f3 is held, ui JSON includes debug.actions (id, key, label, page, on).\n"
			"  When Rendering Stats is on, debug also has fps, memoryMb, avgFps, vpf,\n"
			"  screen, and overlays (vault, status, battle, item-previews, debug).\n"
			"\n"
			"  rqmod-ctl key enter\n"
			"  rqmod-ctl key z\n"
			"  rqmod-ctl key esc\n"
			"  rqmod-ctl click 512 384     (game pixels, AliceStart ViewWidth x ViewHeight)\n"
			"  rqmod-ctl click right 100 100\n"
			"  rqmod-ctl move 512 384\n"
			"  rqmod-ctl type hello         keys of letters, digits and space; while a text\n"
			"                               field types (rq.uikit.input), any UTF-8 text into it\n"
			"  rqmod-ctl hold left\n"
			"  rqmod-ctl release left\n"
			"  rqmod-ctl wheel 1\n"
			"  rqmod-ctl ping\n"
			"\n"
			"Pipe: %s\n",
			PIPE_NAME);
		return 2;
	}
	for (i = 1; i < argc; i++) {
		int len = (int)strlen(argv[i]);
		/* playscene's FILE may hold a space: the game reads a quoted word as one. */
		int quote = i > 1 && _stricmp(argv[1], "playscene") == 0 && strchr(argv[i], ' ') != NULL;
		if (used + len + 4 >= (int)sizeof line)
			break;
		if (used)
			line[used++] = ' ';
		if (quote)
			line[used++] = '"';
		memcpy(line + used, argv[i], (size_t)len);
		used += len;
		if (quote)
			line[used++] = '"';
	}
	line[used++] = '\n';
	line[used] = 0;

	h = CreateFileA(PIPE_NAME, GENERIC_READ | GENERIC_WRITE, 0, NULL,
			OPEN_EXISTING, 0, NULL);
	if (h == INVALID_HANDLE_VALUE) {
		fprintf(stderr, "rqmod-ctl: cannot open %s (is the game running?)\n", PIPE_NAME);
		return 1;
	}
	if (!WriteFile(h, line, (DWORD)used, &w, NULL)) {
		fprintf(stderr, "rqmod-ctl: write failed\n");
		CloseHandle(h);
		return 1;
	}
	reply = (char *)calloc((size_t)reply_cap, 1);
	if (!reply) {
		CloseHandle(h);
		return 1;
	}
	got = 0;
	while (got + 1 < (DWORD)reply_cap) {
		n = 0;
		if (!ReadFile(h, reply + got, (DWORD)reply_cap - 1 - got, &n, NULL) || n == 0)
			break;
		got += n;
	}
	reply[got] = 0;
	CloseHandle(h);
	if (got)
		fputs(reply, stdout);
	i = (reply[0] == 'e');
	free(reply);
	return i ? 1 : 0;
}
