# AXIS Project 2, Plan B: the dialect code set (ViSiON/3 and Renegade) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.
>
> **Testing rule (Rev's standing rule, binding):** finish the code first, all testing at the end. Tasks 1 to 4 end with
> a clean build on the i5 (the existing test count unchanged) and the cyd Flash line; every new test is in Task 5.

**Goal:** A look can say which BBS family its art comes from (`axis`, `vision3`, `renegade`), and the art renderer
reads that family's codes, so a sysop's ViSiON/3 or Renegade/Iniquity art renders correctly with no codes printed,
no wrong colours and nothing scrolled off. Looks that say nothing render byte for byte as today.

**Architecture:** A small `ArtStyle` (the dialect plus its colour slots) travels with every art expansion.
`pipecodes.cpp` branches on `style.dialect` only where the families differ. A look declares its style in an optional
`THEME.CFG` in its folder, resolved and fetched by followers through the same path as the `.LST` files (plan A). The
local ANSI terminal (`AnsiTerm`: the PC console, the window, the board's LCD WFC) gains the two baseline sequences
real packs use and it lacks. The spec also allows a per-screen hint (SAUCE or a first line); this plan uses the
look's `THEME.CFG` only, since a pack is one family, and leaves the hint for the import plan if a mixed pack turns up.

**Tech Stack:** C++17, `src/bbs` (pipecodes, theme, menurunner, node, listtemplate, ansiterm), `src/app`
(themeprovider, session), CMake on the i5, PlatformIO for the board, doctest.

**Spec:** `docs/superpowers/specs/2026-11-19-axis-screen-engine-design.md` (Goal 2, "The code set grows as a dialect
table", the board-versus-PC split, and the anti-goal "a sysop's real art pack that renders as garbage"). This is
roadmap item 1. Evidence for every code below: the art survey `F:/axis-bbs/local/menupacks/REPORT.md` section 4 and
the ViSiON/3 docs in `F:/axis-bbs/local/menupacks/vision/vision3-github/src/docs/sysop/` (menus/menu-system.md
L290-380, advanced/string-editor.md L404-490, configuration/configuration.md L1591-1610).

**Branch:** `pc/dialects`, cut from `pc/screens` (PR 30 stays as it is; this becomes its own PR on top).

## Global Constraints

- Builds and tests on the i5 only: `py -3.14 -I F:/axis-bbs/local/i5build.py test` and `... cyd`. i5build runs the
  OLD test binary if the test build fails to compile, so confirm the test count; it builds committed work only.
- The cyd image must stay at or under **2,200,000 B** (2,184,929 B at the start of this plan, about 15 KB left).
  Each task reports the Flash line. If a task would cross it, that part goes PC-only behind a feature switch in
  `src/bbs/features.h` and the task says so.
- One copy of behaviour: a look with no `THEME.CFG`, or `dialect=axis`, renders byte-identical to today. Every
  existing look and theme is unchanged.
- The heavier dialects (Mystic, Synchronet, WWIV) are NOT in this plan (PC-only, a later plan).
- Unknown codes never print as text: an unknown value name prints blank, as today.
- No em dashes or double hyphens in prose, comments, logs or user-facing text. No AI attribution in commits.
- Never print or commit LAN addresses, MACs, keys or passwords.
- Privacy rules hold: a value code shows nothing a caller should not see.

## The code table (decided here; the implementer copies these values)

| Code | `axis` (today) | `vision3` | `renegade` |
|---|---|---|---|
| `\|00`-`\|15` | foreground, DOS order | same | same |
| `\|16`-`\|22` | today's bytes (unchanged) | same as axis | background 0-6, DOS order (`ESC[4Xm`) |
| `\|23` | reset (`ESC[0m`) | reset | background 7 (light grey) |
| `\|24`-`\|31` | today's bytes | same as axis | background (n-24) with blink set (`ESC[5;4Xm`, iCE bright background) |
| `\|B0`-`\|B15` | background, DOS order | same | same |
| `\|C1`-`\|C7` | not a code (today's bytes) | the look's colour slot as a full attribute (fg, bg, bright) | not a code |
| `\|\|` | today's bytes | one literal `\|` | one literal `\|` |
| `^0`-`^9` | not a code | not a code | the look's colour slot 0-9 as a full attribute |
| `\|CS` | not a code | not a code | clear screen (same bytes as `\|CL`) |
| `%XX` | AXIS `mci` names (UN BN SN NN DT TM VN) | same | AXIS names plus `%TL` minutes left, `%MA` message area, `%FA` file area, `%CF` / `%CM` blank, `%MP` blank, `%PE` blank, `%NL` blank |
| `\|XX` / long names | today's table | today's plus `CAN`, `CFAN` (current area names), `UPLDS`, `DNLDS`, `POSTS`, `LCALL` where AXIS has the data, else blank | same as axis |
| 0x1A (Ctrl-Z) | menu and theme art stop here (the SAUCE record is never drawn) | same | same |

Slot defaults when `THEME.CFG` gives none: `vision3` `c1..c7` = `7,15,7,15,8,11,31` (ViSiON/3's defaults; 31 is
white on blue). `renegade` `r0..r9` = `7,11,14,15,9,13,12,10,8,3` (AXIS's defaults; a look sets its own). A slot value
is a packed DOS attribute byte (background in the high nibble, bright foreground at bit 3, blink at bit 7).

Outside this table nothing changes. For example, `|B` order stays DOS order in every dialect (`|B1` blue). This
matches today's AXIS and ViSiON/3's own sample screen, even though one ViSiON/3 doc page lists ANSI order.

## Review Focus

1. A Renegade look whose art uses `|23` for a grey background: it shows grey, not a reset, and an AXIS look's `|23`
   is still a reset. (Task 5: `the same |23 is a background in renegade and a reset in axis`.)
2. A ViSiON/3 screen with a SAUCE record and Ctrl-Z, drawn as menu art through `artPiece` in pieces: nothing after
   0x1A reaches the caller, even when 0x1A falls at a piece boundary. (Task 5: `art stops at ctrl-z across pieces`.)
3. A look with a broken `THEME.CFG` (unknown dialect, a slot out of range, garbage): the look still draws, in the
   `axis` dialect with defaults, and the problem is logged once. (Task 5: `a bad theme cfg falls back to axis`.)
4. A real ViSiON/3 pack screen fed to the local terminal (`AnsiTerm` 80x25): no row is written past the last row and
   no pipe code is left in the cells. (Task 5: `the vision3 pack screens render clean`.)
5. A caller on a follower board, where the main serves the look: the follower uses the same dialect as the main.
   (Task 5: `a follower reads the look's style from the main`.)

---

### Task 1: The dialect in the renderer

**Files:**
- Create: `src/bbs/artstyle.h`, `src/bbs/artstyle.cpp`
- Modify: `src/bbs/pipecodes.h`, `src/bbs/pipecodes.cpp`, `CMakeLists.pc.txt` (artstyle.cpp in the core sources;
  the board takes `src/bbs` by glob)

**Interfaces:**
- Produces, `artstyle.h`, namespace `axis::bbs`:
  ```cpp
  enum class Dialect : uint8_t { Axis, Vision3, Renegade };
  struct ArtStyle {
      Dialect dialect = Dialect::Axis;
      uint8_t c[7]  = {7, 15, 7, 15, 8, 11, 31};               // ViSiON/3 |C1..|C7
      uint8_t r[10] = {7, 11, 14, 15, 9, 13, 12, 10, 8, 3};    // Renegade ^0..^9
  };
  const char* dialectName(Dialect d);                       // "axis", "vision3", "renegade"
  bool        dialectFrom(const std::string& name, Dialect& out);  // case-insensitive; false when unknown
  std::string attrSgr(uint8_t attr);                        // a packed DOS attribute as one SGR sequence
  bool parseThemeCfg(const std::string& text, ArtStyle& out);  // key=value lines; false on any bad line
  ```
  `THEME.CFG` format: `dialect=axis|vision3|renegade`, `c1=`..`c7=` and `r0=`..`r9=` (0..255 each); `;` starts a
  comment; unknown keys are ignored (kept for the per-look geometry block a later plan adds). An unknown dialect,
  a slot that is not a number 0..255, or a line with no `=` makes `parseThemeCfg` return false and leave `out` as
  `ArtStyle{}`.
- Produces, `pipecodes.h`: each `expandArt` and `artPiece` overload gains a trailing
  `const ArtStyle& style = ArtStyle{}`. The default keeps every existing caller byte-identical.

- [ ] **Step 1:** Implement `artstyle.*` (the parser in the style of `parseListTemplate` in
  `src/bbs/listtemplate.cpp`; `attrSgr` builds `ESC[0;` + `1;` when bit 3 + `5;` when bit 7 + `3f;4b` + `m` with
  the DOS-to-ANSI order already in `pipecodes.cpp` `kAnsi`).
- [ ] **Step 2:** In `pipecodes.cpp`, thread `style` through `expand()` (and the `{{ACS}}` blank-width call and
  `keepGroup`), and add the branches in the code table: `|16`-`|31` for `Renegade`; `|C1`-`|C7` for `Vision3`;
  `||` for `Vision3` and `Renegade`; `^0`-`^9` and `|CS` for `Renegade`. Every branch tests `style.dialect` first,
  so the `Axis` path keeps its exact code. `%XX` names stay the caller's `value` job (Task 3).
- [ ] **Step 3:** Ctrl-Z in `artPiece` (all dialects): once a 0x1A is seen, it and everything after it in this art
  are dropped, including later pieces. Keep the state in `carry`'s owner: add a trailing `bool* ended = nullptr`
  after `style` (null means today's behaviour plus the in-piece cut), and give the two callers that stream pieces
  (`Node` art drawing at node.cpp ~821 and `MenuRunner` at menurunner.cpp ~71) their own flag reset per screen.
- [ ] **Step 4:** Build: `i5build.py test` (count unchanged, 1208, read for compile errors) and `i5build.py cyd`
  (Flash line).
- [ ] **Step 5:** Commit `bbs: art dialects (axis, vision3, renegade) in the code expander`.

### Task 2: A look declares its style

**Files:**
- Modify: `src/bbs/theme.h`, `src/bbs/theme.cpp` (a `THEME.CFG` reader hook and the cached style), `src/app/themeprovider.h`,
  `src/app/themeprovider.cpp` (read `THEME.CFG` through the `.LST` path, follower fetch included), `src/app/session.cpp`
  (wire the hook in `wireNode`)

**Interfaces:**
- Consumes: Task 1 (`ArtStyle`, `parseThemeCfg`).
- Produces, `theme.h`:
  ```cpp
  void     setThemeCfg(std::function<std::string()> read, std::function<void(const std::string&)> bad);
  ArtStyle artStyle();   // the active look's style; ArtStyle{} when it has no THEME.CFG or a bad one
  void     forgetArtStyle();   // drop the cached style (a look or config change)
  ```
  `artStyle()` reads and parses once and caches the result until `forgetArtStyle()` or a new `setThemeCfg`. A bad
  file logs once through `bad` ("THEME.CFG for <look>: <what was wrong>, drawing it as axis").
- Produces, `ThemeProvider`: `std::string themeCfgText();` returns the active look's `THEME.CFG` text, or "" when
  there is none. The lookup is the theme folder, then the legacy folder (no `/menus` fallback: a menu set has no
  look). It is capped at 1024 B (a longer file counts as none), and `THEME.CFG` is added to the `menuScreenFile`
  whitelist. A follower fetches it from the main with the same E/T requests the `.LST` files use (plan A,
  `listTemplateText`), cached the same way and cleared by `forgetRemote()`.
- Every place that changes the look or swaps the config calls `theme::forgetArtStyle()`. Find them by where
  `forgetRemote()` and the theme setting change are handled (main.cpp settings receipt, axispc's live config swap,
  the sysop's look change).

- [ ] **Step 1:** Implement the hook, the provider reader and the wiring.
- [ ] **Step 2:** Build (`test` count unchanged, `cyd` Flash line).
- [ ] **Step 3:** Commit `theme: a look can declare its art dialect and colours (THEME.CFG)`.

### Task 3: Every art path uses the look's style

**Files:**
- Modify: `src/bbs/node.cpp` (menu art ~545, ~562, the art drawer ~821, `loadScreen`, `menuValue`),
  `src/bbs/menurunner.h`, `src/bbs/menurunner.cpp` (`setStyle(const ArtStyle&)`, used for art and prompts),
  `src/bbs/listtemplate.cpp` (renderLine passes the style to expandArt), `docs/MENU-ART.md`

**Interfaces:**
- Consumes: Task 1 (`expandArt(..., style)`, `artPiece(..., style, ended)`), Task 2 (`theme::artStyle()`).
- The WFC (`wfc.cpp`, node.cpp ~1550 and ~1660) stays on `ArtStyle{}`: the waiting screen is AXIS's own art.
- `Node::menuValue` gains the value names in the code table (`CAN`, `CFAN`, `UPLDS`, `DNLDS`, `POSTS`, `LCALL` from
  what `User` and the browsers already hold; blank where AXIS has nothing). Add `CAN` and `CFAN` to `kLongNames` in
  pipecodes.cpp only if they are missing. Renegade's `%TL`, `%MA`, `%FA` go in `Node::mci` and print blank unless
  the look's dialect is `renegade` (so an AXIS look's `%TL` text is unchanged). `%CF %CM %MP %PE %NL` print blank in
  `renegade`.
- `docs/MENU-ART.md` gains a "Dialects" section: `THEME.CFG`, the code table above, and the slot defaults.

- [ ] **Step 1:** Implement: the Node holds no style copy; it asks `theme::artStyle()` at each screen it draws.
- [ ] **Step 2:** Build (`test` count unchanged, `cyd` Flash line; this is the task most likely to cost flash, so
  report the delta).
- [ ] **Step 3:** Commit `bbs: menus, screens and list templates draw in the look's dialect`.

### Task 4: The local terminal's ANSI baseline

**Files:**
- Modify: `src/bbs/ansiterm.h`, `src/bbs/ansiterm.cpp`

**Interfaces:**
- `AnsiTerm` (used by the PC console, the window and the board's LCD screen) gains:
  - `ESC[nG` (CHA): move to column n (1-based, default 1, clamped). The ViSiON/3 art uses it 14 times.
  - SGR 5 (blink) as iCE colour: it sets the background's bright bit (background 8-15), and SGR 25 or 0 clears it. The
    cell's background then holds 0-15. Check every reader of the cell background (the CYD screen in
    `src/platform/cyd/ansi_screen.cpp` and `screen.cpp`, the PC console `src/pcapp/console.cpp` `sgr()`, the window)
    so a background of 8-15 draws as the bright colour, not as garbage.
  - `ESC[1J` (clear from the top to the cursor) beside the existing 0 and 2.
- What it already does is unchanged: CUP, CUU/CUD/CUF/CUB with clamping (so `ESC[255D` goes to column 1),
  `s`/`u`, `2J`, `K`, and ignoring `?7h`.

- [ ] **Step 1:** Implement.
- [ ] **Step 2:** Build (`test` count unchanged, `cyd` Flash line).
- [ ] **Step 3:** Commit `bbs: the local terminal knows CHA, iCE colour and clear to the cursor`.

### Task 5: Testing (all of it, at the end)

**Files:**
- Create: `tests/test_dialect.cpp`, `tests/data/packs/vision3/` (a few small ViSiON/3 screens from the MIT pack,
  copied unchanged with its `LICENSE`: `YOURSTAT.ANS`, `MSGAREA.TOP`, `KONFIG.ANS`, and one file that has a SAUCE
  record; none from the survey's exclude list: not `V2CHAT1/2.ANS`, not `ZIPLAB.*`), and add the test file to
  `CMakeLists.pc.txt`.

- [ ] **Step 1: Unit tests** (doctest, assertions on exact bytes):
  - `the axis dialect is byte-identical to today`: a string with every code family (`|07|23|16|31|B1|C1||^1|CS%UN`)
    expands to the same bytes with `ArtStyle{}` as the pipecodes.cpp at this plan's start (`git show
    e99ef6d:src/bbs/pipecodes.cpp`) produced (write the expected bytes out by hand from that code).
  - `the same |23 is a background in renegade and a reset in axis`: `|23` gives `ESC[0m` in axis and `ESC[47m` in
    renegade; `|17` gives `ESC[44m` in renegade; `|25` gives `ESC[5;44m` in renegade.
  - `vision3 theme colours come from the look`: `|C7` with defaults gives white on blue (`attrSgr(31)`); with
    `c7=78` it gives `attrSgr(78)`; `||` gives `|`.
  - `renegade slots and clear screen`: `^3` gives `attrSgr(r[3])`, `|CS` gives `ESC[2J ESC[H`.
  - `art stops at ctrl-z across pieces`: a screen split so 0x1A ends one piece and `SAUCE00` starts the next, fed
    through `artPiece`, has no byte after the picture.
  - `a bad theme cfg falls back to axis`: `dialect=mystic`, `c3=300`, `garbage` each make `parseThemeCfg` false; with
    such a file installed through `theme::setThemeCfg`, `artStyle()` is `ArtStyle{}` and the bad callback ran once.
  - `a follower reads the look's style from the main`: drive `ThemeProvider::themeCfgText` as a follower the way the
    plan A list template follower path is driven (or through its menu fetch seam), and the main's `THEME.CFG` text
    comes back.
  - `the local terminal: CHA, iCE and 1J`: `ESC[10G` puts the next glyph at column 10; `ESC[5;44m` gives a cell
    background of 9; `ESC[1J` clears to the cursor.
  - `the vision3 pack screens render clean`: each file under `tests/data/packs/vision3/`, expanded with
    `dialect=vision3` and fed to `AnsiTerm(80, 25)`: no cell holds a `|` followed by a code the vision3 table knows,
    nothing was written below row 25 (track the furthest row written), and no byte from a SAUCE record shows.
- [ ] **Step 2:** `i5build.py test`: all pass, the count rises from 1208 by these cases; `i5build.py cyd` at or under
  2,200,000 B; axispc links.
- [ ] **Bench** (no board flashing without Rev; list for Rev if it cannot be done on the i5's build alone):
  - An AXIS look on the PC console and a telnet caller: unchanged.
  - A look folder with `THEME.CFG` `dialect=vision3` and the ViSiON/3 MAIN art: colours right, nothing printed as
    codes, the SAUCE record not drawn.
  - A `dialect=renegade` look with a `|23` and `^1` test screen: grey background, the prompt colour.
- [ ] **Brain:** add the dialects, `THEME.CFG` and the code table pointer to `D:/brain/Projects/AXIS Native.md`
  (and a line in `AXIS BBS Firmware.md`), linked with `[[AXIS BBS Firmware]]`.
- [ ] **Commit:** `tests: art dialects, THEME.CFG and the terminal baseline`.
