# Sysop control panel build notes

Rev (2026-11-04): build all of part 1 first, test live at the end, keep notes along the way.
PC unit tests run per task; board flashing and live checks wait for the end.

## Live checks to do at the end
- Task 2: `@NODES`, `@WEB` answer as before on the serial console.
- Task 5: panel port login + `CMD @NODES` from the PC; STAT every 5 s.
- Task 8: the full panel on PC and phone; unplug/replug the headless board; app restart.
- Also untested live: LORD Setup, NUP prompt.

## Notes
