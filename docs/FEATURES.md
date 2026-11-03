# AXIS UNDERGROUND CNC: features and settings log

Living record of every feature and setting, updated as the project goes. Status: **planned**, **in progress**, **working** (tested end to end), **dropped**. Design: [v1 design](superpowers/specs/2026-11-02-axis-underground-bbs-v1-design.md).

## Features

| Feature | Status | Notes |
|---|---|---|
| Standalone BBS firmware for the CYD (ESP32-2432S028R) | planned | No demos or built-in games; the only games are installed DOS doors |
| Works with no SD card (minimal board) | planned | Everything in internal flash (LittleFS), LORD included; SD only adds space; file areas off without SD |
| Preliminary BBS firmware | working | Boots, joins the saved WiFi, listens on port 23 as `axis.local`, welcome + handle + menu (Info, Credits, Goodbye), busy screen for a second caller, 5 min idle logoff, sysop waiting-for-caller screen; tested with a live call 2026-11-02 |
| First-boot setup | working (telnet path) | Tap screen + PIN + wizard over telnet tested live 2026-11-02; on-screen wizard and touch WiFi picker still to come | WiFi on the touchscreen, then the board asks with a tap: finish on the screen or over telnet (setup PIN); asks sysop account, names, location, address, port, timezone, FTN address, new-user defaults |
| One telnet node per board | in progress | Port 23, LAN only in v1; a second caller gets a "node busy" screen |
| Multi-board cluster | planned | Boards on the LAN join as node 2, 3, 4...; one BBS with shared data |
| One caller address | planned | Callers dial `axis.local`; the main answers with a free board's IP so each caller connects directly (no relay), so a failing board never drops other boards' callers |
| Main-board failover | planned | Lowest surviving node becomes main within ~5 s; callers stay connected; full replica on every board (SD, or LittleFS if it fits) |
| Real DOS doors on the board | in progress (PC build working) | 8086 runtime, high-level DOS, FOSSIL on INT 14h to telnet, door code served from flash |
| LORD 4.07 | in progress | Plays in the AXIS runtime on the PC locally and over telnet (FOSSIL + DOOR.SYS, node file NODE1.DAT: BBSTYPE DOORSYS, FOSSIL, BBSDROP C:\); hang-up handled; peak RAM 100 KB; CYD port next |
| Native LORD rewrite | planned (after real LORD) | C++ port of Synchronet's LORD.js; same screens and play as the original, verified side by side; reads content from the installed LORD files; same save files |
| Door installer | planned | Downloads the unmodified lord407.zip hosted in this repo, unpacks to SD or LittleFS, copies code to the `door` partition |
| Drop files | working (PC) | DOOR.SYS and DORINFO1.DEF first; others table-driven later |
| Users and security | planned | Handles, salted SHA-256 passwords, security levels, daily time limits |
| Renegade-style menus | planned | Menus as data files in storage, ANSI display files with MCI codes |
| Message bases | planned | JAM format, FTN fields (MSGID/REPLY, origin, SEEN-BY/PATH), local/echo/netmail |
| FidoNet packets | planned | Type-2+ PKT import/export via inbound/outbound folders; BinkP mailer in v2 |
| File areas | planned | Default areas: RotatoCAM Pro, RotatoCAM Free, CNC Freeware, CNC Shareware/Trials, CNC Pro Demos, G-code and Toolpaths, 3D Models, Text Files, Freeware, Shareware/Free Trials, Abandonware, BBS Doors. |
| File areas (old note) | planned | FILES.BBS-style lists, ZMODEM download; uploads in v1.1 |
| WFC screen (CYD) | working | Original drippy ANSI AXIS logo (art/wfc.ans, editable in PabloDraw/Moebius, rebuilt with tools/art/ans2header.py) in 4x8 CP437 cells, live panel in full-size 8x16 text: dial, ip, wifi, storage, calls, uptime + free RAM, sysop + time, caller online |
| BBS config menu | planned | From the WFC (tap the CYD, sysop password or PIN) or a local sysop login; edits all settings, doors, areas, WiFi, SD tools |
| Sysop console | planned | CYD screen: waiting-for-caller and live caller mirror (off during doors); telnet sysop screens |
| SD card tools | in progress | FAT32 format works (bench command `@SDFORMAT FORMAT`, card re-check `@SDCHECK`); sysop menu entry comes with the sysop menu | Sysop screen: card info and Format SD card (FAT32, optional full erase), with password + typed confirmation |
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
- **2026-11-02:** First-boot setup over telnet working (choice screen, setup PIN, wizard, settings saved to internal flash `/axis.ini`, BBS name and idle time used by the node). CYD screen screenshots via `@SHOT` + `tools/screenshots/cyd_shot.py`. SD format tool wiped Rev's 15 GB card to fresh FAT32. Third SD card works; first two were dead.
- **2026-11-02:** Drippy ANSI WFC on the CYD: original block-letter AXIS with paint drips (no TheDraw font needed; Font37DripR wasn't on this PC), status panel in big readable text, red frame. Default file areas added to the design.
- **2026-11-03:** Stage 1 reached on the PC: the real LORD 4.07 runs in the AXIS door runtime (refactored FabGL 8086 core, paged memory, high-level DOS, BIOS with hookable vectors). Fixes found by running LORD: interrupts on at start, CGA status port toggling, font info call, exit stops the CPU, a Windows read bug. Peak 100 KB RAM. 101 tests.
- **2026-11-03:** Stage 2 reached on the PC: LORD 4.07 plays over telnet through the AXIS FOSSIL driver (INT 14h, X00-style signature stub) with DOOR.SYS; hanging up ends the door cleanly. `doorhost --telnet 2323` hosts one caller for manual play. 118 tests.
- **2026-11-03:** LORD is on the board: `tools/door_prepare.py` builds a board-ready copy (fresh lord.dat, NODE1.DAT, no docs; 44 files, 964 KB), `tools/sdput.py COM18 build/lord-ready /doors/lord` uploads it over USB with byte sums (serial commands `@PUT`, `@LS`, `@RM`, `@MKDIR`, `@BAUD`). `@DOORLS <id>` lists a door folder, `@DOORCHECK <id> [force]` caches the relocated EXE in the `door` flash partition (LORD: 3.5 s to write, 281 ms to confirm). One file layer (POSIX file descriptors) now serves the PC tests and the board. SD card runs at 20 MHz (it was stuck at 400 kHz). 146 tests.
- **2026-11-03 (Rev):** queued: drippy super-fly ANSI login screen with board, software, active nodes and jmscnc.com info; a tap on the WFC opens the sysop menu (BBS/sysop setup, user editor, files, messages, logs); new file areas: Music (Rev J Money Music and others), CAD/CAM Software, Post Processors, BBS Software.
- **2026-11-03:** **LORD plays on the CYD.** BBS menu `[D] Doors` lists the doors from the settings (`doorN=id|name|exe|args|drop`, LORD by default); picking one runs it on the caller's telnet line with FOSSIL + DOOR.SYS, the panel shows `IN DOOR: <door>`, and the caller returns to the BBS menu afterwards (with a note if the door ran out of memory or time). Doors get the screen copy's 76.8 KB plus about 37 KB of spare instruction RAM as guest memory. Screenshot tool now renders door art correctly (VGA font, cursor moves).
