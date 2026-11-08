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
