# AXIS Project 2, Plan A: the themeable list engine and per-look geometry Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.
>
> **Testing override (Rev's standing rule, binding):** "save all the main testing for the end." Build tasks end with
> a clean build on the i5 and a commit; the unit tests and the bench/board walk are the final task. Exception: a task
> whose deliverable is pure logic (the template parser/renderer) carries its own small unit test, since that is the
> cheapest way to pin it; everything else tests at the end.

**Goal:** Make the BBS's scrolling-list screens (the file browser, message browser and user list) render from a
theme-supplied list template with per-look geometry instead of hardcoded C++ columns, so a look restyles the list
body, columns, caption and status line, not just a header, with no template falling back to today's exact layout.

**Architecture:** A new `ListTemplate` (a small text format, one `.LST` file per list screen in a look's folder:
geometry, a caption line, a per-row template of value tokens, a status and key line). `listscreen.*` and the list
widgets (filebrowser, msgreader) render through the template when the active look provides one, else through today's
hardcoded frame. The look's folder is resolved by the existing theme lookup, extended to find `.LST` files. This is
the backbone for the rest of project 2 (coverage, the panel, runnable looks); it ships the three browsers as its
proof. All in the shared core, so the boards get it within the flash line.

**Tech Stack:** C++17, the existing `src/bbs` engine (pipecodes, listscreen, lightbar/ScrollList, theme,
themeprovider), CMake on the i5, PlatformIO for the board, doctest.

**Spec:** `docs/superpowers/specs/2026-11-19-axis-screen-engine-design.md` (the list-template and per-look-geometry
calls, and the "complete coverage" goal; this plan is its first subsystem).

## Global Constraints

- Builds and tests on the i5 only: `py -3.14 -I F:/axis-bbs/local/i5build.py test` and `... cyd`. i5build runs the
  OLD test binary if the test build fails to compile, so confirm the test count; build `axis_tests` by hand to see
  errors.
- The cyd image must stay at or under **2,200,000 B** (about 2,171,429 B at the start of this plan, ~28 KB headroom).
  Each task reports the Flash line. If a task would cross the line, make the new code PC-only behind a feature switch
  and say so.
- One copy of behaviour: a screen with **no** `.LST` template renders byte-identical to today (the hardcoded frame),
  so every existing look and theme is unchanged. Tests assert this.
- The boards keep working exactly as now; the board's 40-column sysop touch console is out of scope (unchanged).
- No new menu language: `.LST` is a layout template for list rows (geometry, caption, row, status, keys), not menu
  command logic; menus stay `.MNU`.
- No em dashes or double hyphens in prose, comments, logs or user-facing text. No AI attribution in commits.
- Never print or commit LAN addresses, MACs, keys or passwords.
- Privacy rules from the stats work hold: a converted list shows nothing a caller should not see.

## Review Focus

1. A list area with more items than one page: paging, the status count and the "more" behaviour match today across
   a template and the hardcoded fallback. (Task 4: `a long file area pages the same with a template and without`.)
2. A row value that overflows its field (a long filename, a 7-digit size): the template clips/pads to the field
   width and the row never wraps or shifts the columns. (Task 1: `a field token clips and pads to its width`.)
3. A malformed `.LST` (missing a line, a bad geometry, an unknown token): it falls back to the hardcoded frame and
   logs once, never a blank or scrolled screen. (Task 1: `a malformed template is rejected and the caller falls back`.)
4. A look whose `.LST` geometry puts the list off the 80x24 screen: geometry is clamped to the screen so no row
   draws off-screen. (Task 1: `geometry is clamped to the screen`.)
5. A radio (lite) caller: a template still renders, lightly, within the lite art rules (no heavier than the hardcoded
   lite list). (Task 4 bench: a lite list over the modem.)

---

### Task 1: The ListTemplate format: parse and render a row

**Files:**
- Create: `src/bbs/listtemplate.h`, `src/bbs/listtemplate.cpp`
- Modify: `CMakeLists.pc.txt` (add listtemplate.cpp to the core sources), the board source glob already takes `src/bbs`
- Test: `tests/test_listtemplate.cpp` (this task carries its own unit test, per the testing note)

**Interfaces:**
- Produces, `listtemplate.h`, namespace `axis::bbs`:
  ```cpp
  struct ListTemplate {
      int         x = 3, y = 6, width = 74, rows = 14;   // the ScrollList geometry; defaults = today's file list
      int         hi = 111, reg = 15;                     // the list colours (DOS attrs), today's defaults
      std::string head;        // head art key (artFor), empty = the title banner
      std::string caption;     // the column caption line, pipe codes (|07Name ...), empty = none
      bool        rule = true; // draw the ORANGE rule under the caption
      std::string row;         // the per-row template: value tokens @XX####@ / |codes for one list row
      std::string status;      // the status line (list row kListStatusRow), pipe codes + value tokens
      std::string keys;        // the key line (kListKeyRow)
  };
  bool parseListTemplate(const std::string& text, ListTemplate& out);  // false on a malformed file (missing row=, bad geom)
  std::string renderRow(const ListTemplate& t, const AcsUser& who, const ArtValue& field);  // one row, clipped+padded to width
  ```
  `field(name)` resolves a row token (`@FN20@` -> field "FN" padded/cut to 20). `renderRow` runs `expandArt` on
  `t.row` with `field` as the value callback, then clips the visible result to `t.width`. Geometry is clamped in
  `parseListTemplate` (x>=1, y>=4, x+width<=80, y+rows<=22) so no row lands off-screen; out-of-range is clamped, not
  rejected. A file with no `row=` line, or a `geom=` that is not four integers, returns false.
- File format (`.LST`, one per list screen, in a look's folder): lines `geom=x,y,width,rows`, `colours=hi,reg`,
  `head=KEY`, `caption=...`, `rule=0|1`, `row=...`, `status=...`, `keys=...`; `;` starts a comment; unknown keys are
  ignored (kept for a newer version, like the menu format).

- [ ] **Step 1: Write the failing tests** (names and assertions):
  - `a field token clips and pads to its width`: a row `|15@FN8@|07 @SZ5@` with field FN="verylongname" renders FN
    cut to 8 and SZ right-sized to 5.
  - `a malformed template is rejected`: text with no `row=` line, and text with `geom=3,6` (two fields), both give
    `parseListTemplate` == false.
  - `geometry is clamped to the screen`: `geom=70,6,40,14` clamps so `x+width<=80`; `geom=3,20,74,14` clamps rows so
    `y+rows<=22`.
  - `a round trip keeps the fields`: a full `.LST` text parses into the struct's exact values.
- [ ] **Step 2: Run them, expect FAIL** (listtemplate not defined).
- [ ] **Step 3: Implement** `parseListTemplate` and `renderRow` in `listtemplate.cpp` (reuse `expandArt` from
  pipecodes for `row`/`status`/`caption`; a small key=value line parser like `parseMenu`).
- [ ] **Step 4: Run them, expect PASS.**
- [ ] **Step 5: Build** `i5build.py test` (count rises by 4) and `i5build.py cyd` (Flash line, report it).
- [ ] **Step 6: Commit** `bbs: the list template format (parse and render a row)`.

### Task 2: Resolve a look's list template

**Files:**
- Modify: `src/app/themeprovider.h`, `src/app/themeprovider.cpp` (find `<look>/NAME.LST`), `src/bbs/theme.h`/`theme.cpp`
  (a `listTemplateFor(name)` entry like `menuScreenFile`), `src/app/session.cpp` (wire a `theme::setListTemplate` hook),
  `src/main.cpp` (the board wires the same hook; the follower-remote path like `themeArtRemote`)

**Interfaces:**
- Consumes: Task 1's `ListTemplate`, `parseListTemplate`.
- Produces: `bool theme::listTemplate(const std::string& name, ListTemplate& out);` returns true and fills `out` when
  the active look has `<themesDir>/<cfg.theme>/NAME.LST` (or the look's menu set `/menus/NAME.LST`), false when none
  (the caller then uses the hardcoded frame). A follower fetches the `.LST` text from the main over the link, like
  theme art (request letter reused, the text cached as menus are). The lookup order mirrors `openThemeScreen`:
  the theme folder, then the legacy folder, then the menu set.
- The names: `FILES.LST` (the file list), `MSGS.LST` (the message list), `USERS.LST` (the user list). Add these three
  to the `menuScreenFile`-style map so the resolver knows them.

- [ ] **Step 1:** Implement the resolver and the hook; no behaviour change yet (nothing calls it).
- [ ] **Step 2:** Build (`test` count unchanged, `cyd` Flash line).
- [ ] **Step 3:** Commit `theme: resolve a look's list template (.LST), with the follower fetch`.

### Task 3: listscreen renders through a template

**Files:**
- Modify: `src/bbs/listscreen.h`, `src/bbs/listscreen.cpp`

**Interfaces:**
- Consumes: Task 1 (`ListTemplate`, `renderRow`), Task 2 (`theme::listTemplate`).
- Produces: an overload set so a caller can drive the list from a template:
  - `const ListTemplate* listScreenTemplate(const std::string& name);` returns a cached template for the active look
    or nullptr (wraps `theme::listTemplate`).
  - `void listScreenTop(const std::string& title, const std::string& columns, const ListTemplate* tpl);` when `tpl`
    is non-null, it draws `tpl->head` (or the title head), the `tpl->caption` (expanded) and the rule per `tpl->rule`,
    at the template's geometry; when null, today's exact behaviour (the `columns` caption, the fixed rule).
  - `void listScreenStatus(const std::string& line, const ListTemplate* tpl);` and `listScreenKeys(..., tpl)` use the
    template's row positions when given, else rows 21 and 23.
  - The default geometry constants (`kListRow`, `kListRows`, `kListStatusRow`, `kListKeyRow`) stay as the fallback.
- Behaviour with a null template is byte-identical to today (a test in Task 4 pins this).

- [ ] **Step 1:** Implement; keep the old signatures working (default `tpl=nullptr`).
- [ ] **Step 2:** Build.
- [ ] **Step 3:** Commit `bbs: listscreen draws through a list template when the look has one`.

### Task 4: the file browser, message browser and user list use the template

**Files:**
- Modify: `src/bbs/filebrowser.cpp` (the area and file lists, the `ScrollList` geometry and the `"%-12.12s %7s  %s"`
  rows), `src/bbs/msgreader.cpp` (`showList` and its area list), `src/bbs/node.cpp` (`showUserList` ~2960: its
  hardcoded caption, rule and `snprintf` rows become a `USERS.LST`-driven list over a `ScrollList`)
- Create: `art/menus/FILES.LST`, `art/menus/MSGS.LST`, `art/menus/USERS.LST` (the default templates that reproduce
  today's columns exactly, so the default look is unchanged and the format is exercised), their fields documented in
  `docs/MENU-ART.md`
- Modify: `docs/MENU-ART.md` (document the `.LST` format and the three screens' field tokens)

**Interfaces:**
- Consumes: Tasks 1 to 3.
- Each browser: build its `ScrollList` from `listScreenTemplate(name)` geometry when present (else today's fixed
  geometry), and format each row through `renderRow` with a per-row `field` callback that returns the file/message/
  user fields (`FN` name, `SZ` size, `AB` about; `FR` from, `TO` to, `SU` subject; `HN` handle, `LO` location,
  `CA` calls, `TG` tagline). A null template keeps today's `snprintf` row exactly.
- The three default `.LST` files reproduce today's columns, so with them present the screens look identical; a sysop
  dropping a look's own `.LST` restyles them.

- [ ] **Step 1:** Implement the three conversions; the default `.LST` files reproduce today's layout.
- [ ] **Step 2:** Build (`cyd` Flash line, must be at or under 2,200,000 B; if over, move the `.LST` default rendering
  PC-only and keep the board on the hardcoded rows, noting it).
- [ ] **Step 3:** Commit `bbs: the file, message and user lists render from a list template`.

### Task 5: Testing (all of it, at the end)

**Files:**
- Create: `tests/test_listscreen.cpp` (in the core test list)

- [ ] **Step 1: Unit tests** (few):
  - `a null template renders the same rows as the hardcoded frame`: a small file area formatted both ways gives the
    same bytes (pins "one copy of behaviour").
  - `a long file area pages the same with a template and without`: 40 files, page 2, the status count and the rows
    match between template and fallback.
  - `the user list template hides a twit from a non-sysop`: the privacy/twit rule still applies through the template.
  - `a look's template restyles the columns`: a `FILES.LST` with a different caption and row order renders that order.
- [ ] **Step 2:** `i5build.py test` all pass (the baseline plus Task 1's and these); `i5build.py cyd` at or under
  2,200,000 B; both Linux programs build.
- [ ] **Bench** (screenshots, board when it is plugged in; the i5 otherwise):
  - The default look: the file, message and user lists look exactly as before (side by side with a pre-change shot).
  - A sample custom `FILES.LST` dropped into a look: the file list restyles (new columns), paging and download still
    work.
  - A lite (radio) caller sees a light list.
- [ ] **Brain:** note the `.LST` format and the three converted screens in `D:\brain\Projects\AXIS Native.md`.
- [ ] **Commit:** `tests: the list template and the converted list screens`.
