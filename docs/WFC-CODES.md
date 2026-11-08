# WFC art codes

Waiting-for-caller (WFC) screens are ANSI art with pipe codes. Each theme (or WFC skin) folder can hold two: `WFC.ANS`, 80x25, for telnet and the sysop consoles, and `WFCCYD.ANS`, 45x24, for the CYD's own screen (cells of 7x10 pixels, 315x240, centred). Without one, a built-in screen is shown (`art/wfc80.ans`, `art/wfccyd.ans`). Keep a full-width row free of CR LF (it wraps by itself) and leave the last column of the last row empty, or the screen scrolls. They use the same `|XX` style as the other BBS art. `@XX####@` pads or cuts the value to the field's width (the `@` signs count). Put a non-letter after a code, because a code followed straight by letters can be read as a longer name (`|NAME`, `|NODE`, `|TIME`, `|DATE`).

| Code | Shows |
|---|---|
| `BN` | BBS name |
| `SN` | Sysop |
| `NA`..`NH` | Node lines 1 to 8, in the order the cluster lists them: "1 Handle  activity" or "1 waiting". A node past the eighth is left off. |
| `CT` | Calls today |
| `PT` | Posts today |
| `UT` | Uploads today |
| `LC` | Last caller |
| `SD` | SD free (for example "1.2G") |
| `RM` | RAM free, in KB |
| `IP` | IP address |
| `UP` | Uptime ("3d 04:12") |
| `TM` | Time |
| `DT` | Date |
| `EA`..`EE` | Newest five events, newest first |
| `PG` | Sysop page text |
| `MB` | "main board" or "follower board" |

## Hotkeys (tappable on the CYD)

Each prints the key's letter where it stands and makes that spot a tap target (the cell and one cell either side).

| Code | Key | Does |
|---|---|---|
| `KS` | S | Settings |
| `KU` | U | Users |
| `KM` | # | Menus |
| `KB` | B | Boards |
| `KF` | F | Files |
| `KL` | L | Logs (the sysop menu's System section, where the event log is) |
| `KE` | E | Events |
| `KA` | A | Archive editor |
| `KV` | ! | Validate |
| `KO` | space | Local logon |

Example: `[|KS]ettings` draws `[S]ettings` with `S` tappable. A node line (`NA`..`NH`) is tappable anywhere on its row.

The same letters work at the keyboard on the WFC and, for sysops, after `//` online (`//` alone opens the WFC, `//U` goes straight to Users). `L` opens the System section of the sysop menu (the event log is there). `E`, `A` and `!` say "coming soon" for now. A line that starts with `@` leaves the WFC on the serial console.

## Art rules

- WFC.ANS is 80x25 and WFCCYD.ANS is 45x24. No row may run past the grid and the art may not be taller than it.
- Put an SGR reset (`ESC[0m`) before every CR LF, so a background colour never bleeds into the next row.
- Send no CR LF after a full-width row: the terminal wraps by itself, and a CR LF would leave a blank row.
- Leave the last column of the last row empty, or the screen scrolls.
- Do not use `|CL`. The WFC refreshes in place (home, then overwrite), so a clear in the art would make every refresh flicker.
- A refresh over long values must leave the same screen as a fresh draw, so keep fields to their `@XX####@` width.
- `tools/art/make_wfc_packs.py` draws all eight looks and checks these rules.
