# AXIS PC: the native BBS for Linux and Windows

Status: design, for Rev's approval. Written 2026-11-19 from the conversation that pivoted AXIS to a PC-first BBS.
Branch `pc/app` (worktree `F:\axis-bbs-pc`), cut from `native/cluster` 62f2cb5.

## Why

AXIS began on the ESP32 CYD: a full BBS in 4 MB of flash and ~120 KB of free heap. It proved the design, but the board
is now too small for what AXIS should become (Rev: "the esp32 is too small for all of the features and was a fun
experiment but this needs to grow"). The 8086 DOS runtime was already cut from the boards to save flash, the cluster
caps a board at 4 sessions, and many screens are hand-drawn in C++ to save memory.

AXIS should become the best telnet BBS software there is. Its edge, which no other package has (Rev): **retro and
radio dial-in**: callers on the LRA FLOWTRON DATAMASTER 3000 SERIAL MODEM - LFD3K-SM, on mesh radios, and on old
computers (C64, Amiga, Z80/CP/M machines).

## Goals

1. **A native PC BBS** for Linux first, then Windows: a compiled app with an installer (Linux: AppImage and .deb;
   Windows: an installer), built for as many CPU architectures as is practical (x86_64, ARM64, 32-bit ARM; RISC-V if cheap).
2. **Every ESP32 feature at the start**, then growth past it: more lines, real DOS doors, bigger file areas and screens.
3. **The ESP32 stays** as AXIS's smaller compatible version, standalone or as a cluster node, working as it does now.
   PC-only features are left out of its build.
4. **The PC is always main** when it is in a cluster. Boards join it as nodes and as radio/serial gateways; their
   callers reach PC-only features (DOS doors, more areas) through the PC.
5. **One coherent system**: every section (login, messages, files, doors, chat, user settings, sysop screens) is built
   from the same parts (lightbar lists, prompts, hotkeys, help line, theme art) and works the same way.
6. **Complete theme coverage**: every screen in the system comes from theme art plus a menu or list definition; no
   screen is drawn only by code.
7. **Old art and menu sets work**: the ViSiON, Renegade, Iniquity and Oblivion family first, then Mystic and
   Synchronet, then WWIV and underground-style Wildcat packs: their screens, their pipe/MCI/@ codes, and (by import)
   their menu definitions.
8. **Retro and radio dial-in are first-class**: the LFD3K-SM radio modem (already working on the boards), mesh
   radios, and terminal modes for old machines (40-column PETSCII for the C64, plain ASCII/VT52 for CP/M and Z80,
   a tested ANSI setting with IBM font for the Amiga), chosen at login.
9. **Art packs hosted on the BBS**: an Art Packs file area and the ANSI pack viewer; packs that are public domain or
   openly licensed also ship in the repo.

## Non-goals

- A rewrite in another language. The shared core stays C++ (the boards need it). New PC-only parts default to C++;
  Rust only for a self-contained piece where it clearly pays off.
- Python in the product. The BBS itself is already C++. The sysop panel (`tools/panel/axispanel`, Python) is product
  and gets rewritten in C++. Developer tools (builds, art checks, screenshots) may stay Python for now.
- Copying Go or JavaScript code from other BBS packages. ViSiON/3 (Go, MIT) and others are read for ideas and logic,
  ported into C++ with attribution where code shapes are followed.
- Making the ESP32 carry PC features it cannot fit.

## Anti-goals (failure even if it works)

- The boards stop working, or an ESP32 and a PC can no longer share a cluster or each other's data.
- Sections that look and behave differently from each other (the patchwork AXIS has today in places).
- A sysop's existing art or menu set that renders broken (blank codes, wrong colours, scrolled screens).
- The radio and retro callers treated as an afterthought behind the telnet experience.
- Shipping code from another BBS under a license that does not fit GPLv3 (Synchronet is GPLv2; check its "or later"
  terms before copying any of its code).

## Approaches considered

| | Approach | Trade-off | Verdict |
|---|---|---|---|
| A | Keep one program: the PC runs the board's `main.cpp` through the Linux layer, PC features behind switches | Never two copies; but `main.cpp` (8,572 lines) is built around one ESP32 loop, board memory floors and 4 sessions, and Windows needs a second platform layer under it | Not chosen |
| **B** | **A new PC app around the shared core** | Room to grow and to make the system coherent; shared features hook into two small apps instead of one | **Chosen by Rev** |
| C | Rewrite in Rust with compatible formats | Throws away working code; the boards could not share it, so the two drift apart | Dropped |

## How it is built (what Rev decided)

- **Shared core (C++17, `src/bbs`, `src/cluster`, `src/net`, `src/door`, `src/lord`, `dosemu/`)**: users, messages,
  files, doors, the cluster protocol, telnet, the LFD3K-SM link, Sysop Net, and the new screen and menu engine. It
  builds for both the boards and the PC; feature switches (`src/bbs/features.h`, as `AXIS_BENCH` does today) pick
  what each build gets.
- **The ESP32 app**: today's `src/main.cpp` with its platform layer, unchanged in role.
- **The PC app** (new): its own entry point and session manager (a thread or task per caller, no 4-session cap), its
  own config and data folder, the terminal/window/headless modes plan 1 built, the sysop panel in C++, and installers.
  It reuses plan 1 and plan 2's Linux pieces (net gate, modem line, profiles, the cluster network, call passing on
  port 23) where they fit.
- **Compatibility** comes from the shared core plus shared formats on disk (users.dat, message bases, file areas,
  door data, axis.ini) and on the wire (the cluster protocol, protocol version 2 and later, no new frame types that
  old boards would drop).
- **The cluster**: the PC is always main when present. Plan 2's mirror, takeover and hand-back are replaced by a
  one-time move of a card board's files onto the PC; boards with or without cards join the PC as nodes. Call passing
  works both ways (tested: board to PC on port 23).
- **Screen and menu engine** (shared core, complete coverage): every screen is theme art plus a menu or list
  definition; it renders the code sets of the target families (ViSiON/Renegade pipe codes, Mystic MCI, Synchronet @
  codes, PCBoard/Wildcat @X where packs need them) and imports their menu definitions into AXIS menus.
- **Languages**: C++ throughout; Python out of the product.
- **Packaging**: CI builds AppImage and .deb for x86_64, ARM64 and 32-bit ARM, and a Windows installer.

## Roadmap (each is its own project with its own short design and plan)

1. **PC app foundation**: the new entry point, session manager, config, data folder, logging; runs every current
   feature through the shared core on Linux; CI builds and an AppImage.
2. **Screen and menu engine**: complete coverage, the code sets, menu import for the ViSiON/Renegade/Iniquity/Oblivion
   family first; the doors menu becomes a lightbar like every other list; art pack comparison and the Art Packs area.
3. **PC-main cluster**: boards join the PC; card files moved to the PC once; board callers reach PC-only features.
4. **Retro callers**: terminal detection and choice at login; PETSCII 40-column, ASCII/VT52, Amiga ANSI.
5. **Mesh dial-in**: callers over mesh radios beside the LFD3K-SM.
6. **DOS doors on the PC**: engine A (`dosemu/`, the 8086 runtime) with LORD, Black Jack and FOUR, solo and for
   board callers; then compatible doors found and set up.
7. **Windows build and installer.**
8. **More architectures and installers** (RISC-V, packages for distributions) as cheap.

Order reasoning: 1 is the base everything stands on; 2 is the coherence and art compatibility Rev asked for and every
later section is built with it; 3 to 5 are AXIS's edge; doors and Windows follow.

## What happens to work in progress

- Plan 2 (`native/cluster`, unpushed): its card kinds, protocol version 2, card tree service, profiles, cluster
  network and call passing carry into project 3; the mirror/takeover/hand-back logic is retired there. The branch
  stays as it is until project 3 decides what to keep.
- The i5 keeps running the plan 2 build as a follower of board 1 until project 3 lands.
- The menu/art pack collection feeds project 2: 22 packs in `F:\axis-bbs\local\menupacks` (local only), survey in
  its `REPORT.md`. Repo-safe: ViSiON/3 v3 (MIT), Mystic P1XArion (CC0), Synchronet text/menu (GPL v2 or later),
  Oblivion2-XRM defaults (zlib), WWIV 5 install set (Apache-2.0), each with listed exclusions. The 16colo scene sets
  have no license statement (or board-use only) and stay local unless their artists give permission. First code
  sets: ANSI plus the ViSiON/3 pipe set, then Synchronet Ctrl-A and `@` codes, Mystic MCI, Renegade/Iniquity `%`
  tokens and WWIV codes.

## Project 1's scope (settled from the builder check)

- **Moving logic out of `main.cpp`**: most of the BBS flow (sessions, `sysopRun`, doors, chat, cluster pages) lives
  in the board's `main.cpp` today. It moves into the shared core **one feature at a time**, and `main.cpp` is rewired
  to call the shared code, so there is never a second copy. Each step passes the host tests; a board is flashed and
  checked at the end of each project (not after every step).
- **Standalone first**: in project 1 the PC app runs as a standalone BBS, with the LFD3K-SM modem line, Sysop Net and
  the web interface. It takes no cluster role until project 3; meanwhile the existing Linux program keeps its plan 2
  cluster role.
- **The existing Linux program** (`main_linux.cpp` around the board's `main.cpp`) keeps building until project 3 has
  the PC app doing its cluster job; then it is removed. The PC app's data folder keeps plan 1's layout (`sd`,
  `flash`, `prefs`, `axis.conf`) so a solo install moves over without conversion.
- **The sysop panel** (Rev, 2026-11-19: "Inside the BBS"): the C++ panel is the in-BBS sysop menu grown into the full
  panel, reachable from the WFC, telnet and the window, built with the project 2 screen engine so it shares the look.
  The Python `tools/panel/axispanel` keeps working until then and is retired once the in-BBS panel covers it.
- **CI** (Rev, 2026-11-19: "Yes, push for CI"): GitHub Actions in the public AXIS repo builds the AppImage, .deb and
  (later) the Windows installer for each architecture. `pc/app` is pushed there when project 1's code is ready, after
  the usual scan of the history for addresses, MACs, keys and AI attribution.
- **Board-only hardware** stays on the boards: the 320x240 CYD screens, touch keyboard, LED, Wi-Fi picker and setup
  portal, and the heap floors. The PC app's window and terminal show an 80-column WFC (from the existing 80x25 WFC
  art) instead.

## Calls made in this design (Claude's, for Rev to check)

- The branch `pc/app` is cut from `native/cluster`, so today's Linux work comes along; nothing is pushed.
- Logic leaves `main.cpp` one feature at a time into the shared core (no second copy); boards are flashed and
  checked at the end of each project.
- Project 1 is standalone (modem line, Sysop Net, web included; no cluster role until project 3).
- The old Linux program stays until project 3 replaces it; the PC app keeps plan 1's data folder layout.
- The PC app shows an 80-column WFC; the CYD's 320x240 screens stay board-only.
- The roadmap order above (foundation, screens, cluster, retro, mesh, doors, Windows).
- Developer tools may stay Python for now; only the shipped sysop panel is rewritten first.
- Synchronet code is read only, not copied, until its license terms are checked.
- RISC-V only if a cross toolchain makes it cheap.

## Left to each project's design

Session threading model, config file format, data folder layout on Windows, the menu import formats' details, the
terminal detection method, the mesh radio stack, and CI tooling.
