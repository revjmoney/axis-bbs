# AXIS UNDERGROUND CNC: BBS v1 design

Date: 2026-11-02. Status: draft for Rev's approval.

## 1. Intent

Turn the CYD (ESP32-2432S028R) into a real, working, old-school BBS called **AXIS UNDERGROUND CNC**: something that feels like finding an underground board in the 1990s, themed around CNC machining, toolpaths, rotary axes, G-code, RotatoCAM and demoscene culture.

It must be a real BBS, not a demo that looks like one: callers telnet in, log in with a handle, read and post messages, browse files, and play **real DOS door games** that run on the CYD itself. The first door is **Legend of the Red Dragon (LORD) 4.07**.

Rev's priorities, in his order: native implementation, working DOS doors, FidoNet-style message boards, a serious sysop console, and an unmistakable underground ANSI identity. Multi-node is wanted but is explicitly a later version.

## 2. Goals, non-goals, anti-goals

### Goals (v1)
- **A standalone BBS firmware** for the CYD, its own project and repo. It has no demos and no built-in games: the only games are DOS doors installed from the SD card. (Rev's decision.)
- **One telnet node**, with a complete caller experience: ANSI welcome, login and new-user signup, Renegade-style menus, message areas, file areas, door menu, logoff.
- **Real DOS doors running on the CYD**, starting with LORD 4.07, in a purpose-built 8086 door runtime. A door counts as supported only after it has been launched, played over telnet, and returned the caller to the BBS correctly.
- **Message storage that is genuinely FidoNet-compatible** (FTN addressing, MSGID/REPLY, origin lines, echomail and netmail areas), not a local board with a FidoNet label.
- **A faithful native LORD (after real LORD works):** a native C++ LORD that plays and looks the same as the original, byte for byte where possible, so it runs without the 8086 runtime and shares naturally across the cluster (section 5.12). Not a knock-off: it is only done when side-by-side tests against real LORD match. (Rev's decision.)
- **A sysop console**: a live view of the caller on the CYD screen plus full sysop screens over telnet.
- **Multi-board cluster:** a CYD that boots and finds an AXIS board already on the LAN joins it as node 2, 3, 4 and so on; together they are one BBS. If the main board goes offline, the next node takes over as main and callers carry on (section 5.11). (Rev's decision.)
- **Persistent configuration** on SD and NVS; nothing important lives only in RAM.
- **Minimal board with no SD card (Rev's decision):** the BBS, including LORD, must work on a bare CYD with no SD card, storing everything in internal flash. An SD card adds space (more doors' data, bigger message bases, file areas) but is never required.
- The AXIS ANSI identity across every screen. The ANSI renderer, CP437 font and touch/board code are copied from the CYD Demoscene project (same author, GPL) rather than written again.

### Non-goals (v1)
- More than one node **on a single board** (comes with the PSRAM mod or an ESP32-S3 board; see section 11). Multi-node in v1 comes from **several CYD boards clustering on the LAN**, one node per board (section 5.11).
- SD card swap for door memory (Rev is finding a good SD card for this; the memory layer must stay swap-ready).
- A BinkP mailer, QWK offline mail, inter-BBS door leagues, RIP graphics.
- Running doors that need a 386, protected mode, a DOS extender or an FPU.
- Exposure to the internet. v1 is **LAN only**; internet access (login throttling, bot filtering, non-default port, sysop screens off plain telnet) is a later step. (Rev's decision.)

### Anti-goals (failure even if it "works")
- A fake: simulated screens, scripted "users", a door that is really a re-implementation, or a feature labeled as working that was never tested end to end.
- A generic modern web or app UI. This is a terminal BBS in CP437 ANSI.
- Pulling demos, the clock, pets or any other demoscene feature into the BBS. The BBS is its own thing.
- Committing any third-party door files or copyrighted ANSI art to the repo. Doors are installed by the sysop from their own archives.
- Bypassing door registration or licensing. The runtime accepts whatever key a door itself accepts.

## 3. Constraints and measured facts

| Fact | Value | Source |
|---|---|---|
| CPU | ESP32-D0WD-V3, 2x Xtensa LX6 @ 240 MHz | board |
| RAM | 520 KB internal SRAM, **no PSRAM** | board, probe |
| Flash | 4 MB, all of it the BBS firmware's own (its own partition table, see 5.2) | board |
| Free heap, bare ESP32 sketch, no WiFi | 346 KB total, 114 KB largest block | probe on the spare board, 2026-11-02 |
| Free heap in the demo firmware with WiFi | about 92 KB, but with frame buffers and a demo arena allocated that the BBS firmware does not have | brain note |
| SD | SPI (VSPI), FATFS; ~1.7 MB/s sequential read at 20 MHz, ~3.5 ms random 4 KB read | research notes |
| LORD 4.07 per its EXE header | 348 KB load image + 129 KB min alloc = 467 KB; docs say "requires 350k conventional" | measured, LORD.DOC |
| **LORD 4.07 measured page touch** (short session: logon, new character, news, forest, fights, stats) | **92 KB written, 184 KB read-only touched**, plus the B800 text page | patched emu2 on the i5, 2026-11-02 |
| LORD runtime behavior | Turbo Pascal, real mode, FOSSIL or internal COM, overlays in LORD.OVR (EMS optional, `NOEMS`), drop file DOOR.SYS / DORINFO1.DEF | measured, LORD.DOC |

Key consequence: LORD fits a no-swap CYD only if its **code is served read-only from flash** and RAM holds only the pages it writes. The 92 KB figure is from a short session; longer play will touch more overlays and heap, so the budget below keeps headroom.

## 4. Decisions Rev made

- Real BBS, native code, Renegade as the main reference (also Mystic, ENiGMA½, Iniquity), AXIS UNDERGROUND CNC identity.
- v1 = **one node + real doors on the stock CYD**.
- **No SD swap in v1.**
- **LORD is the first door.**
- **Language:** C++ for the BBS; hand-optimized Xtensa assembly where it measurably pays (the 8086 core and other hot paths). Rev asked for assembly; each assembly routine stays only if it beats the C++ version on the board.
- **Modular memory:** parts of the BBS that are not in use must not occupy RAM (in particular, the BBS gets out of the way while a door runs).
- **The BBS is a standalone firmware with no games or demos built in.** The demoscene firmware is untouched.

## 5. Architecture

### 5.1 Firmware and project
AXIS is its own PlatformIO project and git repo (proposed `F:\axis-bbs`), not a mode of the demoscene firmware. It boots straight into the BBS: board init, SD mount, WiFi join (the demoscene firmware's saved-network logic is a good model), telnet listener, waiting-for-caller screen. There are no demos, frame-buffer double buffering or demo arenas, so the heap starts large and unfragmented.

Reused from the CYD Demoscene project by copying (same author, GPL-3.0-or-later, notices kept): board and touch correction, the ILI9341/LovyanGFX setup, the 4x8 ANSI text renderer and CP437 font, WiFi join and NTP, and the PC-side ANSI and TheDraw font tooling.

Source layout: `src/bbs/` (BBS core) and `src/door/` (door runtime). The door runtime core must also compile as a **PC build** (Windows or Linux, plain C++) so it can be tested against real doors on the PC before it ever touches the board.

### 5.2 Memory plan
- **Arenas per subsystem.** Each BBS subsystem (menus and screens, message bases, file areas, user editor, FidoNet tosser, sysop screens) allocates from its own arena and releases it when the caller leaves that part of the board. RAM holds working copies only; the SD card is the source of truth.
- **Door launch:** the BBS writes the caller's session state (a few hundred bytes) and the drop file, releases every arena except WiFi, the telnet connection, the node bridge and a minimal sysop status display, then starts the door runtime with all remaining RAM. On door exit the BBS reloads from SD and returns the caller to the door menu.
- **Door guest memory** is a 1 MB real-mode address space described by a 256-entry page table (4 KB pages). Each page is one of: backed read-only by flash (door code), backed by a RAM page (written data), zero-on-first-touch, or unmapped. Pages are allocated on first write (copy-on-write from flash for pre-loaded image pages). Because the largest free block may be ~100 KB, RAM pages come from a pool of individually allocated 4 KB blocks, never one big buffer.
- **Door RAM budget (target):** 128 KB of page pool for LORD (92 KB measured plus headroom). The real figure is set by measuring free heap in BBS mode with WiFi and telnet up. **Fallback order (Rev's decision):** first free more memory by turning off the CYD screen mirror while a door runs (the status line stays); if LORD still exceeds the pool, the runtime reports a clean "out of door memory" to the caller and the sysop log and returns to the BBS without corrupting anything. That outcome is the signal to add SD swap once Rev's card turns up. The page table interface is the hook where SD swap plugs in later.
- **Storage backends:** every subsystem reads and writes through one storage interface with three implementations: **SD card** (used when a card is present), **internal flash LittleFS** (minimal boards), and **cluster** (a follower asking the main, section 5.11). Nothing above that interface knows which one it is on.
- **One partition table for every board (my call, adjusted once the app size is known):** NVS + otadata 64 KB, **app 1.5 MB**, **`door` raw partition 768 KB** for door code images (LORD uses about 504 KB), and **LittleFS for the rest, about 1.7 MB**. No OTA slot in v1. With an SD card, data lives on the card and LittleFS holds config, fallback screens and caches; without one, LittleFS is the data store.
- **No-SD budget (minimal board):** LORD's game data is about 830 KB (LORDTXT.DAT, EVENTS.DAT, enemies, items, LORDRIP.DAT; docs, LORDCFG and batch files are not installed), BBS screens and menus about 150 to 250 KB, users and message bases with the remainder (about 600 KB), enforced by message-base size limits and purge rules. File areas are off on a minimal board (no room), and only doors whose code fits the `door` partition and whose data fits LittleFS can be installed. If the 1.5 MB app slot proves too small, the `door` partition shrinks first.
- **Door code in flash:** when the sysop installs a door, the BBS copies its EXE load image into the `door` partition, **relocated for the fixed load segment the runtime always uses**, and memory-maps it. LORD's image (348 KB) plus LORD.OVR (156 KB) take about 500 KB, leaving room for more doors. Overlay and data files are still read through DOS file calls; the flash copy is a fast path, the SD copy stays authoritative. A content key avoids re-copying an image that is already there. The partition belongs to the BBS alone; door images are added and removed by the sysop's door installer.

### 5.3 Node session and telnet
- One listening socket on port 23 (configurable). A second caller while the node is busy gets a short ANSI "node busy, try later" screen and is disconnected.
- Telnet handling in native code, outside the door: negotiate WILL ECHO, WILL SUPPRESS-GO-AHEAD, BINARY both ways; ask for NAWS and TTYPE; strip IAC sequences on input, turn CR NUL / CR LF into CR, double 0xFF on output. CP437 bytes pass through untouched.
- Session state per node: user record, security level, time left today, current menu, message read pointers, door in progress, connect time. Inactivity timeout and daily time limits as in classic BBSes.

### 5.4 Users and security
- User records on SD in a fixed-size record file (Renegade USERS.DAT style), indexed by handle. Fields: handle, real name (optional), location, password hash (salted SHA-256 via mbedTLS), security level, flags, time per day, calls, last call, posts, uploads/downloads, door stats.
- New-user application with handle uniqueness and a bad-handle list. Security levels gate menus, areas and doors. The first user created is the sysop (or a sysop account is set on the bench).

### 5.5 Menus and ANSI identity
- Renegade-style menu system: menus are data (a menu file per menu with commands, keys, security, display file), so menus can be edited without reflashing. Display files are `.ANS` on SD with an ASCII fallback and simple MCI-style codes (`|UN` handle, `|TL` time left, and so on) expanded at send time.
- The AXIS screens: welcome/matrix screen, login, new user, main menu, message, file, door, sysop, logoff, credits. The **credits / about screen** always shows "AXIS UNDERGROUND CNC by Rev. J. Money" with **rotatocam.com** and **jmscnc.com**, plus the license notice and third-party credits (required by the license's 7(b) term). They are original art for this project (TheDraw fonts from the existing font pack plus CNC motifs: toolpaths, rotary axes, G-code, the RotatoCAM logo). **Claude makes them** (Rev's decision), keeping them legible both in a full 80x25 terminal and on the CYD's 4x8 mirror; Rev edits them later in an ANSI editor. Being original, they are committed to the repo under the project license.

### 5.6 Message bases (FidoNet-compatible)
- Storage format: **JAM** message bases on SD (my call: an open, documented format used by real FTN software, which gives true compatibility; tools on the PC can read the bases). Areas: local, echomail, netmail.
- Every message carries FTN fields: from/to addresses (zone:net/node.point), MSGID and REPLY kludges, origin line, tear line, SEEN-BY/PATH for echomail. Replies thread by MSGID/REPLY.
- Reading: new-scan, read forward/back, reply, post, thread follow, per-user last-read pointers (JAM .JLR).
- **FTN address (Rev's decision):** a private test zone for v1 (my call: zone 99, e.g. `99:1/0` for the BBS, configurable), with the network named in config so packets and origin lines are consistent. Joining fsxNet (a real node number) comes with the v2 mailer.
- v1 FidoNet transport: **import and export of FTN Type-2+ `.PKT` packets** through SD `inbound/` and `outbound/` folders (toss and scan run as sysop commands and in nightly maintenance). A live mailer (BinkP over TCP) and QWK are v2.

### 5.7 File areas
- File areas as folders on SD, with a description list per area (FILES.BBS style) and a small index for fast listing.
- Listing, searching, viewing descriptions, and **ZMODEM download over telnet** (SyncTERM and NetRunner support it). Uploads (ZMODEM) are v1.1 unless they come cheaply with the same code.

**Default file areas (Rev's list, created by setup; the sysop can rename, add or remove them):**

| # | Area | What goes there |
|---|---|---|
| 1 | RotatoCAM: Pro | RotatoCAM pro versions, updates, licenses info |
| 2 | RotatoCAM: Free | free RotatoCAM versions and demos |
| 3 | CNC Software: Freeware | free CAM, CAD, G-code senders, post processors |
| 4 | CNC Software: Shareware and Trials | try-before-you-buy and time-limited CNC programs |
| 5 | CNC Software: Pro Demos | free versions and demos of pro CNC software |
| 6 | G-code and Toolpaths | ready-to-run programs, toolpath libraries |
| 7 | 3D Models | STL, STEP, OBJ, DXF and other model files |
| 8 | Text Files | text files, guides, how-tos, machinist references, scene text |
| 9 | Freeware | general free software |
| 10 | Shareware and Free Trials | general shareware, demos and trials |
| 11 | Abandonware | old software whose makers no longer sell or support it |
| 12 | BBS Doors | door game distribution archives (for example the unmodified lord407.zip) |
| 13 | CAD/CAM Software | CAD and CAM programs of any license the sysop can post |
| 14 | Post Processors | machine post processors for CAM programs |
| 15 | BBS Software | BBS packages, utilities, mailers, tossers |
| 16 | Music: Rev J Money | Rev J Money's own music |
| 17 | Music: Other | other music the sysop has the right to share (the sysop can split this into more areas) |

Abandonware is still copyrighted in most cases; the area exists, but AXIS ships no files in it and the sysop decides what to post. Areas 3, 4, 5, 9 and 10 hold only files whose licenses allow redistribution. Areas 13 to 17 were added by Rev on 2026-11-03.

### 5.8 Door runtime
The core of v1. Components:
1. **8086 CPU core** with the 80186/286 real-mode additions doors use; no protected mode, no FPU. Start from the MIT-licensed FabGL i8086 core (derived from 8086tiny) or write a fresh one; every memory access goes through the page-table accessor. Written in C++ first, then the dispatch loop, flag logic and memory accessor are hand-optimized in Xtensa assembly against a benchmark on the board.
2. **MZ loader** that places the door at a fixed segment, builds a PSP, environment and command line (`LORD.EXE 1 /DREW` style), and maps the pre-relocated flash image.
3. **High-level DOS (INT 21h) in C++**, no DOS kernel: file handles, FCBs where needed, directory search, date/time, memory blocks (alloc/resize/free over the page pool), exec of a batch-free door, sharing modes and **record locking (5Ch)** reported as working, SHARE reported installed, EMS and XMS reported absent. Behaviors learned from LORD so far: it opens files with AL=44h (access value 4; DOS masks access to bits 0 to 1, so treat as read), LORDCFG writes `lord.dat` in lowercase (case-insensitive file lookup on FATFS), and LORD runs as `LORD.EXE <node> NOEMS /DREW`.
4. **Minimal BIOS**: data area, INT 10h basics, INT 16h keyboard fed from telnet, INT 1Ah time, 18.2 Hz timer (INT 08h then 1Ch) driven by real time. The B800 text page is plain RAM (useful for the sysop view, never sent to the caller).
5. **FOSSIL driver on INT 14h** (functions 00h to 1Bh, signature 1954h, driver info structure) connected to the node's telnet bridge. Function list must be checked against the FOSSIL Rev 5 spec (FSC-0015 / X00REF) before coding, since the research could not load the spec text.
6. **Idle detection and yield**: LORD polls INT 16h AH=01h and calls INT 15h AX=1000h (DESQview give up time slice) while waiting for input. The runtime treats these as yield points so the CPU idles and WiFi stays responsive.
7. **Drop files**: DOOR.SYS (52 lines) and DORINFO1.DEF in v1, generated per node into the door's node folder; the other formats (PCBOARD.SYS, SFDOORS.DAT, CALLINFO.BBS, CHAIN.TXT, DOOR32.SYS) are a table-driven addition once a door needs them.
8. **Door management**: a door record per door (name, folder on SD, command line, drop file type, security, time limit, node folder, nightly maintenance command). LORD's daily maintenance runs from the nightly event.
9. **Exit handling**: return code captured, carrier loss handled (the door sees carrier drop via FOSSIL status and exits; a watchdog kills it after a timeout), time limit enforced, then back to the BBS.

### 5.9 Sysop console
- **On the CYD screen** (the mirror turns off while a door runs to free memory; the status line stays): a waiting-for-caller screen (AXIS art, uptime, last caller, calls today, free heap, SD space, WiFi signal); when a caller is on, a live mirror of what the caller sees (existing 4x8 ANSI renderer, 80x25) with a status bar (handle, security, time left, area or door). Touch actions: chat request, give or take time, kick, toggle the mirror.
- **BBS config menu (Rev's request), reachable two ways:** (1) **from the WFC screen** by tapping the CYD (sysop password or the setup PIN first), and (2) **from a local sysop login** (the sysop logs in on the board itself or over telnet with the sysop account). It edits every BBS setting the first-boot wizard set (names, location, address, port, timezone, FTN address, new-user defaults, idle and daily limits), plus doors, file areas, message areas, WiFi networks and the SD card tools, and saves immediately.
- **Over telnet**, for sysop-level users: user editor, security levels, message area and FTN configuration, file area configuration, door configuration, logs and errors, system status (heap, arenas, uptime, SD usage, WiFi), node status, nightly event control, and reboot.
- Configuration lives in files on SD (my call: simple INI-style text files, editable on a PC too), with WiFi and boot flags in NVS as today. Every sysop change is written immediately.
- Logs: a rolling system log and caller log on SD.
- **SD card tools (Rev's request):** a sysop screen showing what the card reports (answers or not, type, size, FAT type, free space) and a **Format SD card** action that writes a fresh FAT32 filesystem (FatFs `f_mkfs`), optionally after a full erase (SD erase commands) for a true wipe. Destructive, so it needs the sysop password plus typing `FORMAT`, shows exactly what will be lost, and refuses while a door or caller is using the card. A card that does not answer at all cannot be formatted by anything; the screen says so and suggests a PC formatter or a new card.

### 5.10 Nightly maintenance
A daily event (default 4:00 AM, matching the existing clock resync) that, with no caller online: runs door maintenance (LORD), tosses and scans FTN packets, packs and purges message bases per limits, and rotates logs.

### 5.11 Multi-board cluster (one node per board)
Rev's decisions: a board that finds an AXIS board already on the network joins it as the next node; if the main board goes offline, the next board shows a message and **takes over as the main board** so callers continue (option B).

- **Discovery:** at boot each board announces itself and listens on the LAN (my call: mDNS service `_axisbbs._tcp` plus a UDP broadcast fallback on a fixed port). No answer within a few seconds: become **main**, node 1. An answer: join the main as a **follower** and receive the next free node number. Node numbers are remembered per board (by MAC) so a board keeps its number across reboots.
- **Main board role:** authoritative copy of shared data (users, message bases, file area indexes, door shared data such as LORD's player and game files, the node table, logs) and the **lock manager** for door record locks and file sharing across boards. The sysop console on the main board shows every node.
- **Followers:** run their own caller and doors (door code from their own flash), and send every shared-data read, write and lock to the main. Each follower keeps a **full replica** of the shared data on its own storage (SD card, or LittleFS on a minimal board), kept current from the main's change stream. A minimal board can hold a full replica only while the shared data fits its LittleFS; boards without a complete replica can serve callers but are skipped when choosing a new main.
- **Replication model (my call):** the main assigns every change a sequence number in an append-only change log (record writes, message appends, file writes with offsets). Followers apply changes in order and acknowledge; a rejoining board asks for everything after its last sequence number, or a full resync if it is too far behind.
- **Failover:** the main sends a heartbeat (about once a second). If followers miss it for a set time (my call: 5 s), the **lowest-numbered surviving node that holds a complete replica** becomes main, bumps the cluster **epoch**, and the others follow it. Every board shows a message on its screen and to callers ("MAIN NODE LOST, NODE 2 IS NOW MAIN"); callers stay connected. A caller who was inside a door on the lost main board loses that session, but the shared data stays intact up to the last replicated change.
- **Split-brain guard:** every change and heartbeat carries the epoch. A board that comes back with an older epoch (the old main after a reboot or network glitch) must join as a follower and resync; it never acts as main again until it is elected. If two mains ever see each other, the higher epoch wins and the other demotes and resyncs from it; changes the loser made after the split are kept in a quarantine log for the sysop to review instead of being merged silently.
- **Doors across boards:** door file I/O for shared door folders and INT 21h record locks go through the main's lock manager, so two players on different boards share one LORD world, as multi-node LORD did on 1990s systems. Per-node files (drop files, node temp folders) stay local.
- **Built in from the first BBS stage:** all BBS and door data access goes through one storage interface with a local implementation (single board) and a cluster implementation (follower to main); the single-board case is just a cluster of one.
- **Callers: one address, never dropped (Rev's decisions).** Callers dial one name, `axis.local` (configurable). No board relays another board's caller, because a relay would drop every relayed caller if the relaying board died.
  - The **main board answers name lookups for `axis.local` with the IP of a free board** (itself only when no follower is free), over mDNS with a short TTL (my call: 2 s). The caller's terminal then connects **directly** to that board, so each caller's connection lives only on the board that serves them.
  - When the main fails, the new main takes over answering for `axis.local`; callers on the surviving boards stay connected and keep playing. The only caller who can be dropped is one whose own board loses power or crashes, since that board holds their session; no design can keep a TCP connection alive when its endpoint dies.
  - Races (a cached lookup sends two callers to the same board): the busy board **hands the caller to a free board by relaying** as a fallback and shows "node busy, try again" if none is free. Relays happen only in this rare case.
  - Callers who dial a board's IP directly are served by that board if free, otherwise handled the same way.
  - The sysop screens show every board's IP for terminals that cannot resolve `.local` names.

### 5.12 Native LORD rewrite (after real LORD)
Rev's decision: work on both, **real DOS LORD first**; then a rewrite that works and looks the same as the original, not a knock-off.

- **Starting point:** port LORD 5.02 JS (Synchronet `xtrn/lord/lord.js` by Deuce, GPL-2.0-or-later, about 17,900 lines, closely follows LORD 4.0x) to C++, keeping its structure where it is right and fixing it where it differs from the original. LORD.ts (GPL-3.0) is a second reference, notably for the LDY event scripts.
- **Content comes from the sysop's own LORD install, not from our repo:** the native door reads the game text, events, enemies and items from the original files of the installed shareware zip (LORDTXT.DAT, EVENTS.DAT, LENEMY.DAT, the .LDY scripts and so on), converted at install time if needed. The repo ships the engine only. (My call, to avoid redistributing Metropolis content.)
- **Fidelity is measured, not eyeballed:** with the same player file, inputs and random seed, the real LORD (in the 8086 runtime or the patched emu2 reference on the i5) and the native LORD must produce the same screens. A test harness drives both with the session driver and diffs their ANSI output. Differences are bugs unless documented as deliberate.
- **Save compatibility:** the native door uses LORD 4.07's own player and data file formats, so a sysop can switch between real and native LORD without losing players.
- **Runs as a native door:** same door menu, drop file and node handling as DOS doors; very small RAM; shares the cluster's storage and lock manager directly.
- **Licensing:** the port of LORD.js is GPL (GPL-2.0-or-later code used under GPL-3.0-or-later). LORD's name, text and game design belong to Metropolis Gameport and no permission from them is known; the engine-only approach keeps their content out of our repo. Rev accepts that position.

### 5.13 First-boot setup (Rev's request)
On the first boot of the firmware (no saved setup), the board walks the new sysop through setting up their BBS.

1. **WiFi on the touchscreen (always):** if no saved network is in range, the CYD shows a scan list (strongest first, open/locked, signal bars, a HIDDEN... entry for typing a name) and a touch keyboard for the password, then tests the join before saving. Reuses the CYD Demoscene touch keyboard (`wifikbd.cpp`, same author), including its lessons: open networks need a confirm, gadget hotspots are listed last, the password is never logged.
2. **The board asks with a tap how to finish:** two big buttons, **SET UP ON THIS SCREEN** and **SET UP OVER TELNET**.
   - **Over telnet:** the screen shows `telnet axis.local` (and the IP) and a 6-digit **setup PIN**. The first caller who enters the PIN gets the setup wizard in their terminal; anyone without the PIN gets a "this BBS is being set up" screen.
   - **On this screen:** the same wizard as touch screens, with the touch keyboard for text and pickers for choices.
3. **The wizard asks for** (each with a sensible default, all editable later from the sysop menu):
   - Sysop handle and password (this creates the sysop account), sysop real name.
   - BBS name (default "Axis Underground CNC"), location (city, state).
   - Address callers use: the board name (default `axis`, so `axis.local`) and an optional public hostname, telnet port (default 23).
   - Timezone (default US Eastern).
   - FidoNet address (default `99:1/0`, private test zone).
   - New-user defaults: security level, daily time limit, idle timeout (default 5 minutes).
4. **Saving:** settings go to the config file in storage (SD or LittleFS), the WiFi networks to NVS as today, and a "setup done" flag. The sysop menu can rerun the wizard. Until setup is done the BBS answers callers with the "being set up" screen only.
5. Boards that already have saved WiFi (like Rev's CYDs from the demoscene firmware) skip step 1 and go straight to step 2.

## 6. Staged roadmap with exit tests

Door work comes first because it is the riskiest part and Rev's top priority.

| Stage | Work | Exit test |
|---|---|---|
| 0 (done) | Profile LORD on the PC | LORD runs under patched emu2 on the i5; 92 KB written / 184 KB read-only in a short session |
| 1 | Door runtime PC build: CPU core, page-table memory, MZ loader, HLE DOS subset, BIOS basics | LORD 4.07 runs locally (`/L` style) in our runtime on the PC, through new character, forest fight, and clean quit |
| 2 | FOSSIL + telnet bridge + DOOR.SYS on the PC build | LORD played over telnet from SyncTERM against the PC build, quits back cleanly; carrier drop handled |
| 3 | CYD port of the runtime in BBS mode (minimal shell that just launches LORD) | LORD played over WiFi telnet on the CYD; measured free heap and peak page use logged; a long session does not run out of pages |
| 4 | Performance: benchmark harness, then hand Xtensa assembly for the hot paths | measured speedup recorded per routine; assembly kept only where it wins; LORD feels responsive at 33.6k-style output |
| 5 | BBS core: telnet session, users, login/new user, menus, ANSI display files, door menu | full call: new user, log in, menus, launch LORD, return, log off; user stats persist across reboot |
| 6 | Message bases: JAM storage, read/post/reply/thread, FTN fields, PKT import/export | post and reply survive reboot; a PKT exported is read correctly by a PC FTN tool, and a PKT from a PC tool imports correctly |
| 7 | File areas + ZMODEM download | download a file with SyncTERM, CRC matches |
| 8 | Sysop console (CYD screen + telnet sysop screens) and nightly maintenance | all sysop screens reachable; config changes persist; nightly event runs LORD maintenance and toss/scan |
| 9 | AXIS art pass and polish | every screen has AXIS art; Rev signs off on the look |
| 10 | Multi-board cluster: discovery, node numbering, shared-data access through the main, lock manager, replication, failover | two boards: node 2 joins automatically; a post on node 2 appears on node 1; callers dialing `axis.local` land on free boards directly; two callers share one LORD world; unplugging node 1 makes node 2 main within ~5 s while callers on node 2 and 3 stay connected and lose nothing, and new calls to `axis.local` reach the new main; node 1 rejoins as a follower and catches up |
| 11 | Native LORD rewrite (after stage 3 proves real LORD) | the side-by-side harness shows identical screens for a scripted session (logon, new character, forest fights, town, inn, bank, quit); real and native LORD share the same player files |

## 7. Testing approach
- The door runtime is tested first on the PC build against real doors, with the same source compiled for the board.
- A trace mode logs DOS and FOSSIL calls per door (like the emu2 traces used in stage 0) so incompatibilities show up as concrete missing calls.
- The patched emu2 on the i5 (`~/axis`) stays available as a reference to compare behavior and page-touch profiles.
- Board tests drive the BBS over telnet from a script (the prompt-reactive driver used for LORD), plus the existing serial bench commands. Never tap real WiFi networks in automated tests.

## 8. Licensing
- AXIS code is GPL-3.0-or-later, with an **additional term under GPLv3 section 7(b)** (Rev's decision): copies and derived works must keep the "Copyright (C) 2026 Rev. J. Money" notices, the author credit (Rev. J. Money, rotatocam.com, jmscnc.com) in the README, and that credit on the credits or about screen of any interactive interface. The exact wording is in the README.
- **Reuse policy (Rev's decision): build on proven open-source code wherever possible**, licensed MIT, GPL-3.0(-or-later) or public domain (licenses that are compatible with GPL-3.0-or-later, such as BSD, Apache-2.0 and GPL-2.0-or-later, are acceptable when no MIT/GPL-3/public-domain option is as good). Write new code only where no proven component exists or none fits the CYD's memory.
- **Improve what we reuse:** where a reused component can be made better for the CYD (smaller, faster, less RAM) by refactoring it, or by rewriting a hot path in hand-optimized Xtensa assembly, do it. Every such change is measured against the original on the board (speed, RAM, flash) and kept only if it wins. Original license headers and credits stay; our changes are noted in the file.
- Candidates to evaluate first (license and fitness to be verified at plan time, not assumed):
  - 8086 core: FabGL `i8086` (MIT, derived from 8086tiny, MIT); 8086tiny itself.
  - Telnet option handling: a small proven library if one fits, else our own (the protocol is small).
  - ZMODEM: an embeddable implementation with a compatible license (lrzsz is GPL-2.0-or-later and too big as-is; look for a small one, or port the core of lrzsz).
  - JAM message bases and FTN packet code: existing open implementations from FTN projects (license per project), or written from the published JAM and FTS/FSC specs.
  - Config parsing: inih (BSD-3) or similar.
  - Hashing: mbedTLS SHA-256, already part of the ESP32 SDK.
  - Board, display, ANSI renderer, CP437 font, WiFi and NTP: our own CYD Demoscene code (GPL-3.0-or-later).
- emu2 is GPL-2.0 and its "or later" status is unconfirmed, so it stays a reference to compare behavior, not code to copy, unless its license turns out to be GPL-2.0-or-later.
- No unpacked door files, door data or third-party ANSI art in the repo. LORD is owned by Metropolis (Gameport) and is still sold; sysops supply their own registration.
- **Exception (Rev's decision):** the repo hosts the **unmodified LORD 4.07 shareware distribution zip** (`lord407.zip`, as downloaded from gameport.com/demos/lord407.zip) in its own folder, e.g. `doors/lord407.zip`, with a NOTICE saying it is Metropolis Gameport shareware, not covered by the GPL, shared under LORD.DOC's terms (unmodified, free, website downloads allowed; not to be bundled on CD-ROM, floppy or other media without consent). The door installer downloads that zip from the repo over WiFi and unpacks it to the SD card, or straight into LittleFS on a minimal board, at install time. The zip is never unpacked into the repo, never modified, never baked into the firmware image or a prebuilt SD image. If Metropolis objects, the installer falls back to the Gameport URL.
- Registration keys are tied to the sysop name and BBS name; the BBS must pass exactly the configured sysop name and BBS name to LORD. Keys never go in the repo.

## 9. Left to the builder
- Exact source layout inside `src/bbs/` and `src/door/`, class design, arena sizes.
- Which proven component to adopt for each part (section 8 lists candidates), as long as the license fits and, for the CPU core, the memory accessor is pluggable.
- Telnet buffer sizes, task and core assignment (BBS logic vs door CPU vs display mirror).
- The JAM implementation details and the INI file schema.
- Which hot paths become assembly, decided by measurement.

## 10. Decisions from the builder check
- **Long LORD sessions:** turn off the screen mirror during doors first; if memory still runs out, LORD exits cleanly and that triggers adding SD swap. (Rev)
- **Network exposure:** LAN only for v1. (Rev)
- **No SD card required:** a minimal board stores everything in internal flash, LORD included; SD only adds space. (Rev)
- **Firmware:** the BBS is a separate firmware with no games or demos built in, so there is no shared app slot to outgrow and no shared flash partition. (Rev)
- **FTN address:** private test zone for v1, fsxNet with v2. (Rev)
- **ANSI screens:** Claude makes them, legible on a terminal and on the CYD mirror; Rev edits later. (Rev)
- **Hosting LORD:** the repo hosts the unmodified lord407.zip for the installer (section 8). (Rev)
- **Multi-board:** boards on the LAN cluster into one BBS, one node per board; the next node takes over if the main goes offline (option B, full replicas on every board). (Rev)
- **Callers:** one address (`axis.local`), and no caller is ever dropped because another board failed: the main answers the name with a free board's IP so every caller connects directly to the board serving them. (Rev)
- **LORD both ways:** real DOS LORD first; then a faithful native rewrite (from LORD.js) that looks and plays the same, verified side by side. (Rev)
- **Reuse first:** proven MIT, GPL-3 or public-domain code wherever possible; refactor it or rewrite hot paths in assembly when that measurably improves it (section 8). (Rev)

### Calls Claude made (check these)
- JAM as the message base format; INI-style text config files on SD.
- Partition layout for every board: app 1.5 MB, `door` 768 KB, LittleFS ~1.7 MB, no OTA slot in v1; file areas off on boards without SD.
- FTN test address `99:1/0`.
- ZMODEM download in v1, uploads in v1.1.
- Door RAM pool target 128 KB, set finally by measurement.
- Telnet port 23, a second caller gets a "node busy" screen.
- Nightly maintenance at 4:00 AM.
- Caller name `axis.local` answered by the main over mDNS with a 2 s TTL; relay only as a race fallback.
- Cluster: mDNS `_axisbbs._tcp` + UDP broadcast discovery, 1 s heartbeat, 5 s failover, lowest surviving node becomes main, epoch numbers, quarantine log instead of silent merges after a split.
- The cluster is stage 10 of v1, after the single-board BBS works, but its storage interface is used from stage 5 on.
- Door work (stages 1 to 4) comes before the BBS core (stage 5), because it is the riskiest part.

## 11. Later versions
- v2: SD swap for door memory; BinkP mailer and a real FTN node address; QWK; more drop file types; uploads.
- More than one node per board: the PSRAM mod on the CYD (8 MB chip on the SOIC-8 flash footprint, CS on GPIO16, CLK on GPIO17, RGB LED removed), or an ESP32-S3 board with 8 MB PSRAM, or the spare ESP32 as a door box over UART. Node isolation then comes from one page table and one FOSSIL bridge per node, which this design already has.
