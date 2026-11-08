# Renegade WFC: skinnable waiting-for-caller screens

Date: 2026-11-08. Project 1 of 4 (see "The four projects" at the end). Unifying the two sysop menus is part of this project.

## Intent

Rev wants AXIS's sysop side to work like Renegade BBS: a Waiting For Call (WFC) screen where one
key jumps straight into a sysop function, and the same functions reachable with `//` from a
logged-in menu. Underneath, the functions are AXIS's own (the existing sysop shell), not new
copies.

The WFC must be skinnable with ANSI art, the way the caller screens already are, and its live
values must come from pipe/MCI codes in the art, so restyling it means editing an `.ans` file,
not the firmware.

Why now: switching the theme in the sysop menu today only recolors the built-in CYD WFC. It never
shows the screen sets Rev made (the 7 zips in Downloads), which is not how he wanted themes to
work.

## Goals

1. A WFC drawn from ANSI art, in two sizes:
   - `WFC.ANS`, 80x25, for telnet and the sysop console (the terminal side).
   - `WFCCYD.ANS`, 53x30 (6x8 cells filling the 320x240 CYD screen), for the board's own
     touchscreen (the local side). Sized for the CYD, not a scrolled 80x25 screen.
2. The same pipe/MCI codes work in both files and fill in live values.
3. The WFC skin follows the theme by default, or the sysop picks a WFC skin separately
   (Settings > Look). This replaces today's colour-only switch.
4. Renegade keys, from the WFC and from `//` online (sysop access only):

   | Key | Opens |
   |---|---|
   | S | Settings (and Setup) |
   | U | Users |
   | # | Menus (.MNU editor) |
   | B | Message bases |
   | F | File areas |
   | L | System / event log |
   | Space | Local logon |
   | E, A, ! | "coming soon" until projects 2 and 4 land |

   `//` alone shows the Renegade-style sysop command menu; `//U` and so on jump straight in.
5. On the CYD, tapping a hotkey letter drawn in the art runs that key.
6. Rev's 7 screen sets (crt-hardcore, deep-sea, haunted-crt, occult-cathedral, skulls,
   underground, wasteland) install as themes the board actually uses, and each gets a drafted
   `WFC.ANS` and `WFCCYD.ANS` in its own style.

## Non-goals

- The event scheduler (E), archive editor (A) and upload validation (!) themselves: projects 2
  and 4. This project only reserves their keys.
- FidoNet mail: project 3.
- Renegade's live-session function keys (Alt-S split chat, Alt-K kick and so on). Not asked for.
- The Python controller and the placeholder C++ router from the reviewed documents: neither is
  used. The keys route into the unified sysop menu (see "Rev's answers").

## Anti-goals (failure even if it "works")

- Switching theme still showing the old built-in WFC in new colours.
- A WFC that goes blank or crashes when a theme has no WFC file, or a file is too big or broken.
- Live values hard-coded in firmware layout code, so a new skin needs a firmware change.
- A second, separate sysop menu system that drifts from the unified one.
- Caller sessions starved of RAM because the WFC holds a whole screen in memory twice.

## Constraints

- ESP32 CYD, small fragmented heap. Theme screens are capped (`kThemeArtCap` 12 KB, art kept
  near 7 KB) and streamed in pieces of at most 512 bytes through the existing pipe code engine
  (`src/bbs/pipecodes.h`).
- Theme art lives on the SD card in `/sd/themes/<theme>/` with fixed names (`MAIN.ANS`,
  `LOGIN.ANS`, `MATRIX.ANS`, `NEWUSER.ANS`, `PRELOGON.ANS`, section headers and so on). Followers
  in a cluster get theme files from the main over the link.
- The zips use their own names (`04_skull_axis_main_menu.ans` ...), not the board's, so they need
  a name mapping when installed. Several zip screens have no board screen yet (shop, mill, fast
  menu, logo banner).
- The existing built-in CYD WFC (pixel-drawn in `src/main.cpp`, `namespace wfc`, from
  `art/wfc.ans`) stays as the fallback.
- Existing pipe codes: `|00`-`|15`, `|B0`-`|B15`, `|CL`, `|XYcc,rr`, `|XX` placeholders,
  `@XX####@` padded fields, `%XX`, `{{ACS}}` blocks. New WFC values use the same mechanism.

## Decided

- Two files per skin (80x25 terminal, 53x30 CYD). Missing CYD file: the built-in CYD WFC is
  shown. Missing terminal file: a built-in 80x25 default WFC is shown.
- The WFC skin defaults to the current theme; a separate `wfc` setting can name another theme
  folder. (`BbsConfig::wfc` already exists, currently forced equal to `theme`.)
- Keys route into sections of the unified sysop menu.
- The MCI set covers what today's CYD WFC shows: per node handle and activity, calls, posts and
  uploads today, last caller, SD free space, free RAM, IP address, uptime, time and date, newest
  events, the sysop page message, plus BBS name and sysop name.
- Claude drafts both WFC files for all 7 packs, matching each pack's palette and style.

## Rev's answers (2026-11-08)

- **One sysop menu.** The CYD's touch `SysopMenu` and the text `SysopShell` are unified into
  one sysop menu: one set of sections and actions, drawn as touch screens on the CYD and as text
  in a terminal. The WFC keys and `//` open sections of that one menu. Whatever either menu can
  do today stays possible in the unified one (Setup, Users, Files, Messages, Logs, System, Theme,
  Menus, Sections, Settings).
- **The 80x25 WFC shows in all three places:** (a) a sysop who connects over telnet and logs in,
  (b) the control panel's console on the PC, and (c) the serial console. Ordinary callers never
  see it. Space (local logon) starts a normal caller session in that same terminal.

Builder notes on these answers:
- (a) brings back sysop control over telnet, which was removed (`node.h`: "The telnet sysop
  shell is gone"). Telnet is plain text, so the sysop password crosses the network readable.
  Keep the existing login and security level check, and add a setting to turn telnet sysop
  access off; the panel and serial console stay the trusted paths.
- A telnet sysop sitting on the WFC holds one of the board's session slots, like any caller.
  The panel and serial consoles do not.

## Claude's calls (Rev to check)

- **Packs:** the 7 packs are already mapped to board names and staged in
  `local/sdstage/themes/{crt,sea,haunted,cathedral,skulls,underground,wasteland}` (with SHOP,
  MILL, FASTLOGN), and their look ids are in `kLooks` (`src/bbs/theme.cpp`). So goal 6 is: draft
  `WFC.ANS` and `WFCCYD.ANS` per pack, add them to those folders, deploy. No installer is needed.
- **`wfc` setting:** may name any known look id (the same list as `theme`), not any folder.
  Config stops forcing `wfc = theme`; an empty or unknown value means "follow the theme".
- **`#` Menus:** opens the existing main menu editor (the `@MENU*` rows). A real .MNU file
  editor is out of scope; if a `MAIN.MNU` file overrides those rows, the editor says so.
- **CYD touch:** keep tapping a node line for its details and the status lamps (redrawn on a
  timer in a region the art marks). The lightbar menu and the LOOK footer chip go: the hotkey
  letters replace the lightbar, and Settings > Look replaces the chip.
- **Cluster:** node slots and today's counts are cluster-wide, as on today's CYD WFC, from the
  data the board already has (the heartbeat). The WFC files are theme files and reach followers
  the same way the other theme files do.

## Left to the builder

- The exact MCI code names (follow the existing two-letter `|XX` style; document them in a
  table in `docs/`).
- How node lists repeat in the art (fixed slots per node, for example `|N1`..`|N8`, is fine).
- How the CYD finds tap targets for hotkey letters (for example a marker code beside each key,
  or a small hotspot list per skin).
- The zip-to-board name mapping and where the extra zip screens (shop, mill, fast menu) go.
- How often live values refresh on each side.
- Tests: pipe code expansion and key routing on the PC test build (`axis_tests`).

## The four projects (agreed order)

1. This one: Renegade WFC.
2. `!` Validate uploads (queue with Validate, Delete, Move, Info) and `A` Archive editor (per
   type: allowed, allowed unchecked, or rejected; AXISBBS.NFO on or off).
3. FidoNet mail: BinkP poll to a hub (fsxNet first), PKT toss into the JAM message bases,
   outbound echomail and netmail.
4. `E` Event scheduler: timed jobs, mainly polling the message net, plus purge, log cleanup,
   mirror and reboot.
