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
