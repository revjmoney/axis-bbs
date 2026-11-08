# Renegade WFC Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A Renegade-style waiting-for-caller (WFC) screen drawn from themeable ANSI art with live pipe/MCI values, in an 80x25 terminal version and a 53x30 CYD version, opening one unified sysop menu by Renegade keys from the WFC and through `//` online.

**Architecture:** Portable pieces first, all tested on the PC build: the WFC's live values and tap hotspots (`bbs/wfc.*`), a resizable `AnsiTerm`, the unified sysop menu (`SysopShell` grows the touch menu's sections; `SysopMenu` is deleted), and a `Wfc` state in `Node` so every terminal (telnet sysop, panel console, serial) shows the same screen. The CYD then draws `WFCCYD.ANS` through `AnsiTerm(53,30)` and a new 6x8 cell renderer instead of the pixel-drawn WFC.

**Tech Stack:** C++17, ESP32 CYD (Arduino + ESP-IDF via PlatformIO env `cyd`), doctest PC tests (`axis_tests`), Python 3 for art tools.

**Spec:** `docs/superpowers/specs/2026-11-08-renegade-wfc-design.md`

## Global Constraints

- Two skins per theme: `WFC.ANS` 80x25 (terminal), `WFCCYD.ANS` 53x30 in 6x8 cells (CYD, 320x240).
- Theme files live in `/sd/themes/<look>/`; each WFC file stays under 7 KB (`kThemeArtCap` is 12 KB).
- Missing `WFCCYD.ANS`: the built-in CYD WFC art is shown. Missing `WFC.ANS`: a built-in 80x25 default.
- Keys from the WFC and `//`: S Settings (and Setup), U Users, # Menus, B Message bases, F File areas, L System / event log, Space local logon; E, A, ! show "coming soon".
- Ordinary callers never see the WFC or `//`; sysop access only. A setting turns telnet sysop access off.
- Every change made through the sysop menu goes through an `@` command (`run_`), as today.
- Art is streamed in pieces of at most 512 bytes (`expandArt` with an `ArtEmit`); never hold a screen twice.
- The CYD firmware is at 80.2% of flash: deleting `SysopMenu` and the pixel WFC must pay for what is added. Check `pio run -e cyd` size after each firmware task.
- Commits: no Co-Authored-By or any AI attribution. No em dashes in user-facing text.
- Build and test: `cmake --build build` then `./build/axis_tests.exe`. Known pre-existing failures: `test_files.cpp:392` ("A R E A S") and `test_node_login.cpp:160` ("free RAM").

## Review Focus

1. A theme with a broken WFC file (too long, cut off mid escape code, 0 bytes): the WFC still draws something readable, never blanks or hangs. Test in Task 5.
2. A non-sysop caller typing `//` at a menu: nothing happens beyond the normal menu reply; no sysop screen, no hint. Test in Task 4.
3. More nodes than the art has slots (a 4-board cluster with 9 nodes, art with `|N1`..`|N8`): extra nodes are left off, never written past the art. Test in Task 2.
4. A value longer than its padded field (a 40-character handle in a 12-wide `@N1##########@`): cut to the field, the art's layout holds. Test in Task 2.
5. The sysop's telnet line drops while on the WFC or in the sysop menu: the session slot is freed like any hangup. Test in Task 5.

---

### Task 1: AnsiTerm of any size up to 80x30

**Files:**
- Modify: `src/bbs/ansiterm.h`, `src/bbs/ansiterm.cpp`
- Test: `tests/test_ansiterm.cpp`

**Interfaces:**
- Produces: `explicit AnsiTerm(int cols = 80, int rows = 25)` (clamped to 1..80, 1..30); `int cols() const; int rows() const;`. `kCols`/`kRows` become `kMaxCols = 80`, `kMaxRows = 30`; every existing user of `AnsiTerm::kCols/kRows` in `src/main.cpp` switches to `cols()/rows()`.

- [ ] **Step 1: Failing tests:** `TEST_CASE("a 53 column terminal wraps at 53")`: feed 54 'x' into `AnsiTerm(53,30)`; `cell(1,0).ch == 'x'`, `row() == 1`. `TEST_CASE("cursor moves clamp to the terminal's size")`: `ESC[40;99H` on `AnsiTerm(53,30)` puts the cursor at row 29, col 52.
- [ ] **Step 2: Run** `./build/axis_tests.exe -tc="*53 column*,*clamp to the terminal*"`. Expected: compile error (no such constructor).
- [ ] **Step 3: Implement** with the cell array sized `[kMaxRows][kMaxCols]` and all wrap, clamp and erase logic using `cols_`, `rows_`.
- [ ] **Step 4: Run all tests.** Expected: new cases pass, existing `test_ansiterm.cpp` cases unchanged.
- [ ] **Step 5: Commit** `"AnsiTerm takes its size (up to 80x30)"`.

### Task 2: WFC live values and tap hotspots

**Files:**
- Create: `src/bbs/wfc.h`, `src/bbs/wfc.cpp`
- Modify: `CMakeLists.pc.txt` (add `src/bbs/wfc.cpp`, `tests/test_wfc.cpp`)
- Test: `tests/test_wfc.cpp`
- Create: `docs/WFC-CODES.md` (the code table below, for art makers)

**Interfaces:**
- Produces:
  ```cpp
  namespace axis::bbs {
  struct WfcNode { int number = 0; std::string handle, activity; };  // handle "" = waiting
  struct WfcSnapshot {
      std::string bbsName, sysop, lastCaller, ip, time, date, page;
      std::vector<WfcNode> nodes;           // cluster-wide, by number
      std::vector<std::string> events;      // newest first
      uint32_t callsToday = 0, postsToday = 0, uploadsToday = 0, sdFreeKb = 0, ramFreeKb = 0, uptimeS = 0;
      bool main = true;                     // "main board" / "follower board"
  };
  ArtValue wfcValues(const WfcSnapshot& s);  // the |XX / @XX###@ value function for expandArt
  struct WfcHotspot { int row, col; char key; int node; };  // key: S U # B F L E A ! space; node: >0 for a node line
  // Expands WFC art into `term`, recording where the |K? and |N? codes landed.
  std::vector<WfcHotspot> drawWfc(const std::string& art, const WfcSnapshot& s, AnsiTerm& term);
  char wfcKeyAt(const std::vector<WfcHotspot>& spots, int row, int col, int* node);  // 0 = none
  }
  ```
- Codes (two-letter, the existing `|XX` style; `BN` already exists and keeps its meaning): `BN` BBS name, `SN` sysop, `N1`..`N8` node line ("1 Handle  activity" or "1 waiting"), `CT` calls today, `PT` posts today, `UT` uploads today, `LC` last caller, `SD` SD free (e.g. "1.2G"), `RM` RAM free KB, `IP`, `UP` uptime ("3d 04:12"), `TM` time, `DT` date, `E1`..`E5` newest events, `PG` sysop page text, `MB` "main board" or "follower board". Hotkey markers `KS KU K# KB KF KL KE KA K! K_` (K_ = Space) print the key's letter and record a hotspot.

- [ ] **Step 1: Write the failing tests** in `tests/test_wfc.cpp`:
  - `TEST_CASE("wfc values fill the codes")`: snapshot with bbsName "AXIS", callsToday 12, node {2,"Spindle Kid","doors"}; `expandArt("|BN |CT @N2##############@", {}, wfcValues(s))` contains "AXIS 12 2 Spindle Kid".
  - `TEST_CASE("a node past the art's slots is left off")`: 9 nodes, art uses `|N1`..`|N8`: output has no "9 ".
  - `TEST_CASE("a value longer than its field is cut to the field")`: handle of 40 'x', `@N1##########@` (14 wide) yields exactly 14 characters there.
  - `TEST_CASE("hotkeys and node lines become tap targets")`: art `"|XY05,03[|KS]ettings"` + `"|XY01,10|N1"` drawn into `AnsiTerm(53,30)`; `wfcKeyAt(spots, 2, 5, &n) == 'S'`; tapping row 9 returns node 1 via `n`.
- [ ] **Step 2: Run** `cmake --build build && ./build/axis_tests.exe -tc="*wfc*,*hotkeys*,*node past*,*longer than its field*"`. Expected: build fails, `bbs/wfc.h` not found.
- [ ] **Step 3: Implement** `wfcValues`, `drawWfc` (calls `expandArt` with an emit that feeds `term`; the value function records `term.row()/col()` for `K?` and `N?` codes before returning the text) and `wfcKeyAt` (a hit is the hotspot's cell or one cell either side). Uses `AnsiTerm(cols, rows)` from Task 1.
- [ ] **Step 4: Run the tests.** Expected: the 4 new cases pass, only the 2 known failures remain.
- [ ] **Step 5: Commit** `git add src/bbs/wfc.* tests/test_wfc.cpp CMakeLists.pc.txt docs/WFC-CODES.md && git commit -m "WFC live values and tap hotspots from pipe codes"`

### Task 3: One sysop menu (SysopShell absorbs SysopMenu)

**Files:**
- Modify: `src/bbs/sysopshell.h`, `src/bbs/sysopshell.cpp`, `src/main.cpp` (any `@` command the new items need)
- Delete (at the end of Task 8, once the CYD no longer uses it): `src/bbs/sysop_menu.h`, `src/bbs/sysop_menu.cpp`, `tests/test_sysop_menu.cpp`
- Test: `tests/test_sysopshell.cpp`

**Interfaces:**
- Produces:
  ```cpp
  enum class SysopSection { Top, Settings, Users, Menus, MsgBases, FileAreas, System, Look };
  void SysopShell::beginAt(SysopSection s);  // straight into a section; Esc from there leaves the shell
  ```
- New items (each runs an `@` command, as every item does): in **System**: Restart (`@RESTART`, confirm), Rerun setup (`@RESETSETUP`, confirm), Format SD card (`@SDFORMAT`, typed confirm "FORMAT"; add `@SDFORMAT` in `src/main.cpp` from the code behind today's `SysopMenu::Action::FormatSd`), Station; in **Settings**: toggles for files, messages, doors, free downloads (`@SET files_on=0/1` and the matching keys), Telnet sysop access (`@SET sysop_telnet=0/1`); new **Look** section: theme (`@SET theme=<id>`) and WFC skin (`@SET wfc=<id or blank>`) from the look list in `src/bbs/theme.cpp`.

- [ ] **Step 1: Failing tests:** `TEST_CASE("the one sysop menu reaches every touch menu feature")`: walk the shell with a fake `run` that records commands; picking Restart then Y sends "@RESTART"; Look > theme "skulls" sends "@SET theme=skulls"; System > Format SD needs "FORMAT" typed, anything else sends nothing. `TEST_CASE("beginAt opens a section and Esc leaves the shell")`: `beginAt(SysopSection::Users)` draws "USERS"; Esc makes `open()` false.
- [ ] **Step 2: Run** `./build/axis_tests.exe -tc="*one sysop menu*,*beginAt*"`. Expected: compile error (`SysopSection` unknown).
- [ ] **Step 3: Implement** the section enum, `beginAt`, the new items and `@SDFORMAT`.
- [ ] **Step 4: Run all tests.** Expected: pass.
- [ ] **Step 5: Commit** `"One sysop menu: the shell gains the touch menu's sections"`.

### Task 4: Renegade keys and `//` online

**Files:**
- Modify: `src/bbs/node.h`, `src/bbs/node.cpp`, `src/bbs/config.h`, `src/bbs/config.cpp`
- Test: `tests/test_node_menus.cpp`, `tests/test_config.cpp`

**Interfaces:**
- Consumes: `SysopShell`, `SysopSection`, `beginAt` (Task 3).
- Produces:
  ```cpp
  // 'S','U','#','B','F','L' -> a section; 'E','A','!' -> comingSoon = true; anything else false
  bool renegadeKey(char key, SysopSection& section, bool& comingSoon);
  ```
  `Node::State::Sysop` (the shell runs in the session) and `Node::State::Wfc` (Task 5). `Node::setSysopRun` becomes real again (stores the runner). `BbsConfig::sysopTelnet` (ini key `sysop_telnet`, default `true`).
- Online: at any menu prompt a sysop typing `//` then Enter opens the WFC (Task 5); `//U` (any key above) opens the shell at that section; E, A, ! print "coming soon". Non-sysops and remote sysops with `sysop_telnet=0` get the menu's normal unknown-command reply.

- [ ] **Step 1: Failing tests:** `TEST_CASE("a sysop's //U opens the user section")`: log a sysop in, type "//U\r", output contains "USERS" and state is `Sysop`. `TEST_CASE("// is nothing special for a caller")`: normal caller types "//\r" and "//U\r": no "USERS", no "WFC", state stays `Menu`. `TEST_CASE("telnet sysop access off")`: `sysopTelnet=false`, remote sysop "//U\r" behaves like a caller; local (`beginLocal`) still works. `TEST_CASE("E A and ! are coming soon")`: "//E\r" output contains "coming soon". In `test_config.cpp`: `sysop_telnet=0` parses and formats back.
- [ ] **Step 2: Run** `./build/axis_tests.exe -tc="*//*,*telnet sysop*,*coming soon*"`. Expected: fail.
- [ ] **Step 3: Implement** `renegadeKey`, the `//` read at the menu prompt, the `Sysop` state hosting a `SysopShell` (80 columns, `run` from `setSysopRun`), and the config key.
- [ ] **Step 4: Run all tests.** Expected: pass.
- [ ] **Step 5: Commit** `"Renegade // sysop keys online, with a switch for telnet"`.

### Task 5: The terminal WFC (Node::State::Wfc)

**Files:**
- Modify: `src/bbs/node.h`, `src/bbs/node.cpp`
- Create: `art/wfc80.ans` (built-in 80x25 default), compiled in the same way `art/wfc.ans` is (`tools/art/ans2header.py`)
- Test: `tests/test_node_menus.cpp`

**Interfaces:**
- Consumes: `wfcValues`, `WfcSnapshot` (Task 2); `renegadeKey`, `Sysop` state (Task 4).
- Produces: `void Node::openWfc(uint32_t nowMs)`; `void Node::setWfcSnapshot(std::function<WfcSnapshot()>)`; the WFC art comes through the node's existing theme art loader as `WFC.ANS` (WFC skin folder first, then the built-in default). Redraw every 5 s while idle. Keys: the Renegade keys open the shell at that section and Esc returns to the WFC; Space starts a normal session as that sysop at the main menu; Q leaves (telnet: logoff; panel and serial: the console closes).

- [ ] **Step 1: Failing tests:** `TEST_CASE("the wfc draws the theme's art with live values")`: art loader returns "|BN calls |CT"; output contains "AXIS calls 3". `TEST_CASE("no WFC.ANS: the built-in screen")`: loader returns ""; output contains the built-in's title text. `TEST_CASE("a broken WFC file still draws")`: loader returns 20 KB of 'x', and separately "ESC[1;" cut off: output is non-empty and the node keeps answering keys. `TEST_CASE("U from the wfc and back")`: "U" shows "USERS", Esc shows the WFC again. `TEST_CASE("space logs the sysop on")`: state becomes `Menu`. `TEST_CASE("hanging up on the wfc frees the node")`: carrier drop leads to `Done`.
- [ ] **Step 2: Run** `./build/axis_tests.exe -tc="*wfc*,*space logs*,*hanging up on the wfc*"`. Expected: fail.
- [ ] **Step 3: Implement** the state, the loader fallback (anything over `kThemeArtCap` is cut, as `readThemeFile` already does) and the 5 s redraw (only when the snapshot changed).
- [ ] **Step 4: Run all tests.** Expected: pass.
- [ ] **Step 5: Commit** `"The terminal WFC: themed ANSI, live codes, Renegade keys"`.

### Task 6: WFC skin setting separate from the theme

**Files:**
- Modify: `src/bbs/config.cpp:108,163`, `src/bbs/theme.h/.cpp` if a lookup helper is needed
- Test: `tests/test_config.cpp`

**Interfaces:**
- Produces: `std::string wfcFolder(const BbsConfig& c)`: `c.wfc` when it is a known look id, else `c.theme`. `formatConfig` writes `wfc=` as set (blank allowed).

- [ ] **Step 1: Failing tests:** `wfc=skulls` with `theme=sea` keeps both; `wfcFolder` gives "skulls"; `wfc=` (blank) and `wfc=nosuch` give "sea".
- [ ] **Step 2: Run** `./build/axis_tests.exe -tc="*wfc*"`. Expected: fail (config forces `wfc = theme`).
- [ ] **Step 3: Implement**: remove `c.wfc = c.theme`, add `wfcFolder`, use it wherever `WFC.ANS` / `WFCCYD.ANS` are loaded.
- [ ] **Step 4: Run all tests.** Expected: pass.
- [ ] **Step 5: Commit** `"The WFC skin can differ from the theme"`.

### Task 7: Panel console and serial console open the WFC (firmware)

**Files:**
- Modify: `src/main.cpp` (`panelConsoleNew` path near line 1902; the serial reader near line 3413; `startCluster` data for the snapshot)

**Interfaces:**
- Consumes: `Node::openWfc`, `setWfcSnapshot` (Task 5), `setSysopRun` (Task 4).
- Produces: `static axis::bbs::WfcSnapshot wfcSnapshot()` in `main.cpp`, filled from the cluster's nodes (cluster-wide), the event log, today's counters, `storageFreeBytes`, heap, IP, uptime and the page text. The panel's `CON NEW` session calls `openWfc` after `beginLocal`. On serial, a line `WFC` turns the serial port into a terminal running a local node in the WFC until Q; `@` commands work again after.

- [ ] **Step 1: Build** `pio run -e cyd` and note flash use (baseline 80.2%).
- [ ] **Step 2: Implement** the three wirings and `wfcSnapshot()`.
- [ ] **Step 3: Build** `pio run -e cyd`. Expected: builds; flash use recorded.
- [ ] **Step 4: Flash and check on the board**: panel console shows the WFC; `U` opens users; Space logs on; serial `WFC` shows it in a terminal; a telnet sysop `//` shows it. Save a screenshot per step (`tools/screenshots/`) and send them to Rev.
- [ ] **Step 5: Commit** `"Panel and serial consoles open the WFC"`.

### Task 8: The CYD WFC from WFCCYD.ANS, and the touch menu goes

**Files:**
- Modify: `src/platform/cyd/ansi_screen.h/.cpp`, `src/main.cpp` (`namespace wfc`, `drawWfc`, the WFC tap handler near line 2615, the `SysopMenu` paths)
- Create: `art/wfccyd.ans` (built-in 53x30 default, made from today's look) and its compiled header
- Delete: `src/bbs/sysop_menu.*`, `tests/test_sysop_menu.cpp`, and the old pixel WFC code

**Interfaces:**
- Consumes: `drawWfc`, `wfcKeyAt` (Task 2), `AnsiTerm(53,30)` (Task 1), `SysopShell` 40 columns with `tapRow` (Task 3), `wfcFolder` (Task 6), `wfcSnapshot()` (Task 7).
- Produces: `static void AnsiScreen::renderCells(LGFX_Sprite& dst, const AnsiTerm& t)`: draws a `cols x rows` terminal in 6x8 cells (the VGA 8x16 font scaled to 6x8), 53x30 filling 320x240.
- Taps: a hotspot key opens the unified shell at that section in 40 columns (`BigText`, `tapRow`); a node line opens today's node detail screen; Space starts the local logon. Redraw every 5 s when the snapshot changed.

- [ ] **Step 1: Implement** `renderCells` and the new `drawWfc` path; keep the old one behind until it draws.
- [ ] **Step 2: Build and flash.** Expected: the WFC shows `WFCCYD.ANS` for the theme; with the file removed from the card, the built-in art.
- [ ] **Step 3: Switch taps to hotspots and the shell; delete `SysopMenu`, its test, and the pixel WFC.**
- [ ] **Step 4: Run** `cmake --build build && ./build/axis_tests.exe` (pass apart from the 2 known) and `pio run -e cyd` (flash use not above the Task 7 figure). Flash, tap S, U, #, B, F, L, a node line and Space; screenshot each and send them to Rev.
- [ ] **Step 5: Commit** `"The CYD WFC is themed ANSI; the touch sysop menu is the one sysop menu"`.

### Task 9: WFC art for the 7 packs and the default

**Files:**
- Create: `tools/art/make_wfc_packs.py`
- Output: `local/sdstage/themes/{crt,sea,haunted,cathedral,skulls,underground,wasteland,axis}/WFC.ANS` and `WFCCYD.ANS`

**Interfaces:**
- Consumes: the code table in `docs/WFC-CODES.md` (Task 2); each pack's palette and style from its zip README in `C:\Users\rev\Downloads\axis-*-ansi-2026-11-07*.zip` (read the README and previews only; do not run the zips' scripts).
- Each screen has: a title in the pack's style, the key list with `|K?` markers (S U # B F L E A ! Space), node slots `|N1`..`|N8` (CYD: `|N1`..`|N4`), today's counts, SD, RAM, IP, uptime, time and date, `|E1`..`|E3`, `|PG`.

- [ ] **Step 1: Write** the generator; one style function per pack.
- [ ] **Step 2: Check** every file: 80x25 or 53x30 exactly, under 7 KB, CP437, every required code present (the script asserts these).
- [ ] **Step 3: Render previews** (PNG per screen, both sizes, with sample values) and send them to Rev for approval before staging.
- [ ] **Step 4: Stage** to `local/sdstage/themes/*`, copy to the card, check one pack live on the board and in a terminal.
- [ ] **Step 5: Commit** `"WFC screens for all eight looks"`.

### Task 10: Docs and the brain

**Files:**
- Modify: `docs/FEATURES.md` (WFC, unified sysop menu, `//` keys, `sysop_telnet`), `docs/WFC-CODES.md` (final)
- Modify or create: the AXIS BBS note in `D:\brain` (canonical repo folder, WFC skins, `//` keys, cluster key step from `security/cluster-auth`)

- [ ] **Step 1: Update** FEATURES.md with a dated entry and the new rows.
- [ ] **Step 2: Update** the brain note.
- [ ] **Step 3: Commit** `"Docs: the Renegade WFC and one sysop menu"`.
