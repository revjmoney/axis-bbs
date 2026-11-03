# AXIS UNDERGROUND CNC: features and settings log

Living record of every feature and setting, updated as the project goes. Status: **planned**, **in progress**, **working** (tested end to end), **dropped**. Design: [v1 design](superpowers/specs/2026-11-02-axis-underground-bbs-v1-design.md).

## Features

| Feature | Status | Notes |
|---|---|---|
| Standalone BBS firmware for the CYD (ESP32-2432S028R) | planned | No demos or built-in games; the only games are installed DOS doors |
| Works with no SD card (minimal board) | planned | Everything in internal flash (LittleFS), LORD included; SD only adds space; file areas off without SD |
| Preliminary BBS firmware | working | Boots, joins the saved WiFi, listens on port 23 as `axis.local`, welcome + handle + menu (Info, Credits, Goodbye), busy screen for a second caller, 5 min idle logoff, sysop waiting-for-caller screen; tested with a live call 2026-11-02 |
| First-boot setup | planned | WiFi on the touchscreen, then the board asks with a tap: finish on the screen or over telnet (setup PIN); asks sysop account, names, location, address, port, timezone, FTN address, new-user defaults |
| One telnet node per board | in progress | Port 23, LAN only in v1; a second caller gets a "node busy" screen |
| Multi-board cluster | planned | Boards on the LAN join as node 2, 3, 4...; one BBS with shared data |
| One caller address | planned | Callers dial `axis.local`; the main answers with a free board's IP so each caller connects directly (no relay), so a failing board never drops other boards' callers |
| Main-board failover | planned | Lowest surviving node becomes main within ~5 s; callers stay connected; full replica on every board (SD, or LittleFS if it fits) |
| Real DOS doors on the board | planned | 8086 runtime, high-level DOS, FOSSIL on INT 14h to telnet, door code served from flash |
| LORD 4.07 | planned (first door) | Profiled on the PC: 92 KB written, 184 KB read-only in a short session |
| Native LORD rewrite | planned (after real LORD) | C++ port of Synchronet's LORD.js; same screens and play as the original, verified side by side; reads content from the installed LORD files; same save files |
| Door installer | planned | Downloads the unmodified lord407.zip hosted in this repo, unpacks to SD or LittleFS, copies code to the `door` partition |
| Drop files | planned | DOOR.SYS and DORINFO1.DEF first; others table-driven later |
| Users and security | planned | Handles, salted SHA-256 passwords, security levels, daily time limits |
| Renegade-style menus | planned | Menus as data files in storage, ANSI display files with MCI codes |
| Message bases | planned | JAM format, FTN fields (MSGID/REPLY, origin, SEEN-BY/PATH), local/echo/netmail |
| FidoNet packets | planned | Type-2+ PKT import/export via inbound/outbound folders; BinkP mailer in v2 |
| File areas | planned | FILES.BBS-style lists, ZMODEM download; uploads in v1.1 |
| Sysop console | planned | CYD screen: waiting-for-caller and live caller mirror (off during doors); telnet sysop screens |
| SD card tools | planned | Sysop screen: card info and Format SD card (FAT32, optional full erase), with password + typed confirmation |
| Storage check at boot | working | SD card if it answers, else internal flash; shown in Info and on the sysop screen |
| Nightly maintenance | planned | 4:00 AM: door maintenance, toss/scan, purge, log rotation |
| AXIS ANSI screens | planned | Original art by Claude from TheDraw fonts + CNC motifs; Rev edits later |
| Credits / about screen | working (basic) | "AXIS UNDERGROUND CNC by Rev. J. Money", rotatocam.com, jmscnc.com, license and third-party credits; required by the license |
| SD swap for door memory | planned (v2) | Waiting on a good SD card from Rev's stash |
| Internet access | planned (later) | Needs login throttling, bot filtering, non-default port |

## Settings

| Setting | Default | Where | Notes |
|---|---|---|---|
| Telnet port | 23 | config file | LAN only in v1 |
| Sysop name | Rev J Money | config file | Passed to doors exactly as set (LORD keys depend on it) |
| BBS name | Axis Underground CNC | config file | Passed to doors exactly as set |
| FTN address | 99:1/0 | config file | Private test zone until v2 joins fsxNet |
| Door RAM pool | 128 KB target | config file | Final value set by measurement on the board |
| Screen mirror during doors | off | fixed | Frees RAM for the door |
| Nightly maintenance time | 04:00 | config file | |
| Caller address | axis.local (mDNS, 2 s TTL) | config file | Sysop screens also show each board's IP |
| Cluster discovery | mDNS `_axisbbs._tcp` + UDP broadcast | fixed | |
| Cluster heartbeat / failover | 1 s / 5 s | config file | |
| WiFi networks | none | NVS `net` s0..s4 / p0..p4 | Same format as the CYD Demoscene firmware, so a board that ran it joins automatically; never in code or the repo |
| Idle logoff | 5 minutes | fixed (for now) | |
| Partition table | app 1.5 MB, door 768 KB, LittleFS ~1.7 MB | firmware | Same on every board |
| Data store | SD if present, else LittleFS | automatic | |

## Change log

- **2026-11-02:** Project started. Design approved (single node per board, real DOS doors, LORD first, no SD swap in v1, LAN only, separate firmware, reuse proven MIT/GPL-3/public-domain code first). Multi-board cluster with main-board failover (option B) and a no-SD minimal board added the same day. LORD 4.07 profiled on the i5 with a patched emu2; profiling tools are in `tools/profiling/`.
- **2026-11-02:** Added the faithful native LORD rewrite (after real LORD works), ported from Synchronet's GPL LORD.js, engine only, verified against real LORD by diffing screens.
- **2026-11-02:** Callers: one address (`axis.local`), never dropped by another board failing; the main answers the name with a free board's IP (direct connections, relay only as a race fallback).
- **2026-11-02:** License: GPL-3.0-or-later plus a GPLv3 7(b) attribution term (keep the copyright notices and the Rev. J. Money / rotatocam.com / jmscnc.com credit in the README and on the credits screen).
- **2026-11-02:** First firmware: boots, joins the saved WiFi, answers telnet calls on `axis.local:23` with a placeholder welcome, handle prompt and Info / Credits / Goodbye menu. Telnet filter and session logic have PC tests (17 passing). Flashed to the newer CYD (MAC 8c:94:df:4f:1c:4c, COM18): 911 KB of 1.5 MB flash, 230 KB RAM free while serving.
