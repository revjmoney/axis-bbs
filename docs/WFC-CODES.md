# WFC art codes

Waiting-for-caller (WFC) screens are ANSI art with pipe codes, the same `|XX` style as the other BBS art. `@XX####@` pads or cuts the value to the field's width (the `@` signs count). Put a non-letter after a code, because a code followed straight by letters can be read as a longer name (`|NAME`, `|NODE`, `|TIME`, `|DATE`).

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
| `KL` | L | Logs |
| `KE` | E | Events |
| `KA` | A | Archive editor |
| `KV` | ! | Validate |
| `KO` | space | Local logon |

Example: `[|KS]ettings` draws `[S]ettings` with `S` tappable. A node line (`NA`..`NH`) is tappable anywhere on its row.
