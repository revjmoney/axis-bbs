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

## End-of-build test pass (2026-11-04)
Passed live:
- All three boards flashed; old and new serial commands answer; cluster whole (7 nodes).
- Panel port: 3 bad logins -> NO NO NO BYE and logged; good login; commands; @SETTINGS never shows a salt/hash; STAT every 5 s.
- Panel app: login before anything; finds all three boards; routes user/settings commands to the main and node commands to the right board.
- Console, new sysop session: opens on the headless board as Rev with a live screen; Leave ends it.
- Console, break in (C4MH34D on board 2): caller told on join and leave; sysop keys work; panel sees the screen. 7/7.
- Unplug (headless held in reset): offline after 14 s, back 19 s after reboot.
- LORD Setup: the sysop's Setup entry runs LORDCFG and its screen comes through the mirror.
Fixed during the pass:
- Followers don't announce themselves: the app now reads each board's @NODES to find the rest.
- availableForWrite() is always 0 on this ESP32 core (every screen piece was dropped): replaced with a 32 KB/s cap.
- A killed app left the boards answering BUSY: a new panel connection now replaces the old one.
- A login right after the app starts waited for no board: it now waits up to 8 s for the main to announce.
- Panel command replies were copied in RAM (main has ~42 KB free): now streamed straight to the socket.
- LORDCFG.EXE was not on the card: uploaded to /sd/doors/lord (the card staging tool should copy it too).
Learned: opening board 1's serial port (CH340) resets it; two "reboots" in the log were my own scripts, not crashes.
Not tested by Claude (needs Rev): the NUP prompt (setting one means typing a password), the touchscreen SET PASS / SYSOP PW buttons (needs fingers), and the panel's look in a real browser on PC and phone.

## Ideas parked (Rev, 2026-11-04)
- A fourth look in the ViSiON/3 style (fire logo, two-column "(M) Messages Menu" lists, a blue rumor bar at the bottom). References: https://retroshed.us/vision-3-v3net/ and https://github.com/ViSiON-3/vision-3-bbs (a modern ViSiON/3 rewrite, multinode, the closest cousin to AXIS but on a PC; Rev: "almost exactly what we built but on the esp32").
