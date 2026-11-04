# Sysop control panel build notes

Rev (2026-11-04): build all of part 1 first, test live at the end, keep notes along the way.
PC unit tests run per task; board flashing and live checks wait for the end.

## Live checks to do at the end
- Task 2: `@NODES`, `@WEB` answer as before on the serial console.
- Task 5: panel port login + `CMD @NODES` from the PC; STAT every 5 s.
- Task 8: the full panel on PC and phone; unplug/replug the headless board; app restart.
- Also untested live: LORD Setup, NUP prompt.

## Notes
- Task 1 done: AXIS_SD_POOL off by default. cyd 95.0%, headless 96.1% (just over the ~96% line; watch it).
- Task 2 done: runCommand(cmd, out). File transfer commands (@PUT @LS ...) stay serial-only. Not run on a board yet.
- Task 3 done: PanelSession (src/bbs/panelproto.*). Login also logs 'panel: <who> logged in'.
- Task 4: new commands act on THIS board's nodes only (@KICK/@ADDTIME/@FORCETC/@NOTE/@BCAST); the panel must send each to the board holding the node. No node unit test for addMinutes (shifts callStart_). cyd 95.3%.
- Rev (mid-build): set sysop and user passwords on the touchscreen; then he'll set everything to 1234 himself (Claude doesn't type passwords). Add @USERPASS handle pw + a SET PASSWORD button on the touch sysop menu (touchkeyboard) + a panel control.
- Touch passwords done: SET PASS on a user's ACCESS screen, SYSOP PW on SYSTEM; OK runs @USERPASS/@SYSOPPASS (4+ chars). @SYSOPPASS minimum lowered 6 -> 4 to match the wizard (so Rev can use 1234). Not tried on the screen yet.
- Task 5 done: panel port 2326 (pumpPanel in main.cpp), one client, EVT on every logEvent, STAT every 5 s. cyd 95.8%, headless 96.9% (62 KB left in the app partition; the ~96% line is soft, but headless is the tightest: watch it, -Os on UI files is the next lever). Not run on a board yet.
- Task 6 done: tools/panel/axispanel (discovery, board, hub), 5 pytest cases.
- Rev: removed the auto-validation paragraph from docs.html and FEATURES.md (text only; behavior unchanged). Pushed.
- Task 7 done: tools/panel web UI + tray + run.py. 8 pytest cases. UI not opened in a browser yet (end-of-build testing).

## Status at the end of part 1's build (2026-11-04)
- Built and committed: Tasks 1 to 7 (flash option, runCommand, panel protocol, sysop commands, panel port, app core, web UI + tray), plus touch passwords, @USERPASS, NUP, and the doc text removal.
- PC tests: firmware 501/501, panel app 8/8.
- Flash: cyd 95.8%, headless 96.9%.
- NOT flashed to the boards yet; nothing in part 1 tried live. Next: flash all three boards, then the live checks listed at the top, then docs (Task 8 Step 4).
- Parts 2+3 firmware done: WifiStream tap + inject, PanelStream (panel-only sysop session, beginLocal), panel lines WALL ON/OFF, CON NEW, CON JOIN n, CON OFF, KEY <b64>; board sends SCR <node> <b64>. When the PC is behind, SCR pieces are dropped (no resync snapshot yet: a tile may look garbled until the screen redraws; Ctrl-L / redraw in the BBS fixes it). The panel disconnecting ends its console. cyd 95.9%, headless 97.1% (54 KB left: tight).
- Parts 2+3 app done: board.py SCR -> hub.on_screen -> browser {"scr"}; console via {"con": new|join N|off} and {"key": base64}; term.js (80x25 ANSI + CP437) for the wall tiles and the console. The wall only streams while the Wall/Console tab is open. 9 pytest cases.
- Live checks to add at the end: wall tiles fill as callers move around; New sysop session on board 1 and on the headless board; Break in on a caller (they see the notice, keys work, Leave tells them); panel closing ends its console; a busy board says so.
