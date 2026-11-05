# Menu Engine Foundation (plan 1 of the ViSiON-style experience) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the ESP-IDF build the official firmware and give AXIS a ViSiON/3-style data-driven menu engine: menu files, ACS, pipe/art codes, lightbars together with hotkeys, and lightbar yes/no prompts, running the existing features from MAIN, MSGMENU, FILEM and DOORSM on board 2.

**Architecture:** Five small portable units in `src/bbs/` (acs, pipecodes, menufile, lightbar, menurunner), each tested on the PC. `Node` hands `State::Menu` to a `MenuRunner` when the board has menu files, and maps ViSiON/3 `RUN:` function names onto its existing commands; with no menu files it keeps today's built-in menu. Menus load from the board's own storage first, then from the main's SD over the cluster (like theme art).

**Tech Stack:** C++17, ESP-IDF 4.4.7 with Arduino 2.0.17 as a component (PlatformIO `framework = arduino, espidf`), doctest PC tests via CMake.

**Spec:** `docs/superpowers/specs/2026-11-05-axis-vision-experience-design.md`

## Global Constraints

- Board: ESP32 CYD, no PSRAM; stream output in pieces, never build a whole screen or list as one big string (keep any single allocation under 4 KB in the menu path).
- Every sysop change goes through the existing `@` command path.
- ViSiON/3 (MIT) is the reference for menu fields, keys and flow; note it in `THIRD_PARTY_NOTICES.md`; copy no ViSiON/3 art.
- Never touch board 1 (COM18) until the whole series is done; test on board 2 (COM17). Board 2 is flashed from the ESP-IDF build.
- No em dashes or double hyphens in user-facing text; no AI attribution in commits.
- Menu keys: hotkeys and lightbar both work on every menu (Rev).

## Review Focus

1. A terminal's arrow sequence split across two telnet packets (ESC now, `[A` next poll) must still move the lightbar, not act as Esc.
2. A missing, empty or malformed menu file must never crash or strand the caller: fall back to the menu's FALLBACK, then MAIN, then the built-in menu.
3. An ACS string the parser doesn't understand denies (fail closed), and an empty ACS allows.
4. Big art (a 7 KB MAIN.ANS) plus a lightbar must render without one large allocation (streamed to the line).
5. A GOTO loop (menu A falls back to B, B to A) must stop after a few hops with the built-in menu, not spin.

---

### Task 1: ESP-IDF build becomes the official firmware

**Files:**
- Modify: `CMakeLists.txt` (root: one file for both builds), merge from branch `idf-spike` (`sdkconfig.defaults`, `src/CMakeLists.txt`, `lib/SD`, `platformio.ini`, `CMakeLists.pc.txt`)
- Modify: `README.md`, `CONTRIBUTING.md` (build commands)

**Interfaces:**
- Produces: `pio run -e cyd` builds ESP-IDF; `cmake -S . -B build && cmake --build build && ./build/axis_tests` still builds the PC tests.

- [ ] **Step 1:** `git merge idf-spike` into `door-runtime-cyd`; resolve `CMakeLists.txt` as below.
- [ ] **Step 2:** Root `CMakeLists.txt`: `if(DEFINED ENV{IDF_PATH})` → `include($ENV{IDF_PATH}/tools/cmake/project.cmake)` + `project(axis-bbs)`; `else()` → `include(${CMAKE_CURRENT_LIST_DIR}/CMakeLists.pc.txt)`.
- [ ] **Step 3:** Verify PC tests: `cmake -S . -B build && cmake --build build && ./build/axis_tests.exe` → `Status: SUCCESS!`
- [ ] **Step 4:** Verify firmware: `pio run -e cyd` and `pio run -e headless-ring` → `SUCCESS`.
- [ ] **Step 5:** README/CONTRIBUTING: build section says ESP-IDF via PlatformIO, PC tests unchanged; add ViSiON/3 to `THIRD_PARTY_NOTICES.md` as design reference.
- [ ] **Step 6:** Flash board 2 (`pio run -e cyd -t upload --upload-port COM17`), `@STATUS` shows heap > 120000. Commit.

### Task 2: ACS (access) strings

**Files:**
- Create: `src/bbs/acs.h`, `src/bbs/acs.cpp`
- Test: `tests/test_acs.cpp` (add to `CMakeLists.pc.txt`)

**Interfaces:**
- Produces: `struct AcsUser { int level; std::string flags; };` and `bool acsAllows(const std::string& acs, const AcsUser& u);` plus `AcsUser acsFor(const User& u, bool sysop);` (flags: F files, M messages, D doors, $ free downloads, T twit; sysop → level 255).

- [ ] **Step 1: Write the failing tests**

```cpp
TEST_CASE("acs: empty and * allow, levels and flags, not, and/or, junk denies") {
    AcsUser u{25, "FM"};
    CHECK(acsAllows("", u));
    CHECK(acsAllows("*", u));
    CHECK(acsAllows("s10", u));
    CHECK_FALSE(acsAllows("s255", u));
    CHECK(acsAllows("fF", u));
    CHECK_FALSE(acsAllows("fD", u));
    CHECK(acsAllows("!fD", u));
    CHECK(acsAllows("s10&fM", u));
    CHECK_FALSE(acsAllows("s10&fD", u));
    CHECK(acsAllows("s255|fF", u));
    CHECK_FALSE(acsAllows("s1x?", u));  // not understood: fail closed
}
```

- [ ] **Step 2:** Run `./build/axis_tests.exe -tc="acs*"` → FAIL (not defined).
- [ ] **Step 3:** Implement: `&` binds tighter than `|`; a term is `*`, `s<n>` (level >= n), `f<c>` (flag present), each optionally prefixed by `!`.
- [ ] **Step 4:** Run → PASS; full suite green.
- [ ] **Step 5:** Commit.

### Task 3: Pipe colours, placeholders and conditional regions

**Files:**
- Create: `src/bbs/pipecodes.h`, `src/bbs/pipecodes.cpp`
- Test: `tests/test_pipecodes.cpp`

**Interfaces:**
- Consumes: `acsAllows`, `AcsUser` (Task 2).
- Produces: `void expandArt(const std::string& in, const AcsUser& who, const std::function<std::string(const std::string&)>& value, const std::function<void(const char*, size_t)>& emit);` — emits in pieces of at most 512 bytes. `std::string expandArt(const std::string& in, const AcsUser& who, const std::function<std::string(const std::string&)>& value);` (convenience, tests and short prompts).

- [ ] **Step 1: Write the failing tests**

```cpp
TEST_CASE("pipe colours, |CL, |XY and placeholders") {
    AcsUser u{10, ""};
    auto v = [](const std::string& k) { return k == "UH" ? std::string("Rev") : std::string(); };
    CHECK(expandArt("|15hi", u, v) == "\x1b[0;1;37mhi");
    CHECK(expandArt("|B1x", u, v) == "\x1b[44mx");
    CHECK(expandArt("|CL", u, v) == "\x1b[2J\x1b[H");
    CHECK(expandArt("|XY05,03", u, v) == "\x1b[3;5H");
    CHECK(expandArt("hi |UH!", u, v) == "hi Rev!");
    CHECK(expandArt("@UH####@", u, v) == "Rev     ");  // width from the field, left aligned
}
TEST_CASE("conditional regions blank to the same width when access is missing") {
    auto v = [](const std::string&) { return std::string(); };
    CHECK(expandArt("[{{S255}}[*] Sysop{{/}}]", AcsUser{255, ""}, v) == "[[*] Sysop]");
    CHECK(expandArt("[{{S255}}[*] Sysop{{/}}]", AcsUser{10, ""}, v) == "[         ]");
}
TEST_CASE("a 7 KB screen streams in pieces of at most 512 bytes") {
    std::string big(7000, 'x'), got;
    size_t biggest = 0;
    expandArt(big, AcsUser{10, ""}, [](const std::string&) { return std::string(); },
              [&](const char* d, size_t n) { got.append(d, n), biggest = std::max(biggest, n); });
    CHECK(got == big);
    CHECK(biggest <= 512);
}
```

- [ ] **Step 2:** Run `-tc="pipe*,conditional*,a 7 KB*"` → FAIL.
- [ ] **Step 3:** Implement in `pipecodes.cpp`. Colours `|00`-`|15` map DOS colour to SGR (bright = `1;`); `|B0`-`|B15` background; `|CL`; `|XYcc,rr`; `|XX` placeholder (two capital letters, value() empty → leave text out); `@KEY####@` placeholder padded to the field's width; `{{ACS}}...{{/}}` regions (blank with spaces, keep newlines and escape sequences out of the count).
- [ ] **Step 4:** Run → PASS; suite green. **Step 5:** Commit.

### Task 4: Menu files (ViSiON/3 fields, ESP32-lean format) and the importer

**Files:**
- Create: `src/bbs/menufile.h`, `src/bbs/menufile.cpp`, `tools/menus/v3import.py`
- Test: `tests/test_menufile.cpp`

**Interfaces:**
- Produces:

```cpp
struct MenuCmd { std::string keys, cmd, acs, activity; bool hidden = false; };
struct BarItem { int x = 1, y = 1, hi = 31, reg = 7; std::string key, text; };
struct MenuDef {
    std::string title, prompt1, prompt2, fallback, acs = "*", pass, help;
    bool clr = true, usePrompt = true, forceHotkey = false;
    std::vector<MenuCmd> cmds;
    std::vector<BarItem> bars;
};
bool parseMenu(const std::string& text, MenuDef& out);  // false on a file with no commands
std::string formatMenu(const MenuDef& m);
```

File format (one text file per menu, `/menus/NAME.MNU`; `;` starts a comment): `title=`, `clr=1`, `prompt=1`, `prompt1=`, `prompt2=`, `fallback=`, `acs=`, `pass=`, `help=`, `hotkey=1`, then `cmd=KEYS|CMD|ACS|HIDDEN(0/1)|ACTIVITY` per command and `bar=X,Y,HI,REG,KEY,TEXT` per lightbar item (ViSiON/3's .BAR fields).

- [ ] **Step 1: Write the failing tests**

```cpp
TEST_CASE("a menu file reads ViSiON/3's fields, commands and lightbar items, and writes back the same") {
    const char* f = "title=Message Menu\nprompt1=|08[|15Message Menu|08] ->\nfallback=MAIN\nhotkey=0\n"
                    "cmd=R|RUN:READMSGS|*|0|Reading Messages\ncmd=/SYSOP|GOTO:ADMIN|s255|1|\n"
                    "bar=10,5,31,7,R,Read messages\n";
    MenuDef m;
    REQUIRE(parseMenu(f, m));
    CHECK(m.title == "Message Menu");
    CHECK(m.fallback == "MAIN");
    REQUIRE(m.cmds.size() == 2);
    CHECK(m.cmds[1].keys == "/SYSOP");
    CHECK(m.cmds[1].hidden);
    REQUIRE(m.bars.size() == 1);
    CHECK(m.bars[0].x == 10);
    CHECK(m.bars[0].text == "Read messages");
    MenuDef again;
    REQUIRE(parseMenu(formatMenu(m), again));
    CHECK(formatMenu(again) == formatMenu(m));
}
TEST_CASE("a menu file with no commands is refused") {
    MenuDef m;
    CHECK_FALSE(parseMenu("title=Empty\n", m));
    CHECK_FALSE(parseMenu("", m));
}
```

- [ ] **Step 2:** Run → FAIL. **Step 3:** Implement. **Step 4:** Run → PASS.
- [ ] **Step 5:** `tools/menus/v3import.py V3_MENU_DIR OUT_DIR`: reads ViSiON/3 `mnu/NAME.MNU`, `cfg/NAME.CFG`, `bar/NAME.BAR` (JSON / CSV) and writes our `NAME.MNU`; no art. Check by hand: `python -I tools/menus/v3import.py F:/vision-3-bbs/menus/v3 /tmp/m && cat /tmp/m/MSGMENU.MNU` shows the `cmd=` lines of MSGMENU.CFG.
- [ ] **Step 6:** Commit.

### Task 5: Lightbars (with hotkeys) and the yes/no lightbar

**Files:**
- Create: `src/bbs/lightbar.h`, `src/bbs/lightbar.cpp`
- Test: `tests/test_lightbar.cpp`

**Interfaces:**
- Consumes: `BarItem` (Task 4).
- Produces:

```cpp
class Lightbar {
public:
    explicit Lightbar(std::vector<BarItem> items);
    std::string draw() const;              // every item, the current one in its hi colour
    // a key byte from the caller; returns the chosen item's index once picked, -1 otherwise.
    // ESC [ A/B (and ESC O A/B) move with wrap, even when split across calls; 1-9 jump; a hotkey picks;
    // Enter picks the current; a lone ESC (nothing after it within the same call... see keyTimeout) is Esc.
    int key(char c);
    bool escaped() const;                  // a lone Esc was pressed
    void keyTimeout();                     // the caller paused after ESC: treat it as Esc
    std::string takeRedraw();              // only the two rows that changed since the last draw/takeRedraw
    int current() const;
};
// a two-item yes/no bar at (x,y), Y and N hotkeys; returns 1 yes, 0 no, -1 not yet
class YesNoBar { public: YesNoBar(int x, int y, bool defaultYes); std::string draw() const; int key(char c); };
```

- [ ] **Step 1: Write the failing tests**

```cpp
static std::vector<BarItem> three() {
    return {{49, 10, 31, 5, "J", " Journey onward."}, {49, 11, 31, 5, "C", " Create an account."}, {49, 12, 31, 5, "D", " Disconnect."}};
}
TEST_CASE("lightbar: arrows wrap, hotkeys and digits pick, Enter picks the current") {
    Lightbar b(three());
    CHECK(b.key('\x1b') == -1);
    CHECK(b.key('[') == -1);
    CHECK(b.key('A') == -1);  // up from the first wraps to the last
    CHECK(b.current() == 2);
    CHECK(b.key('\r') == 2);
    CHECK(b.key('c') == 1);   // hotkey, either case
    CHECK(b.key('1') == 0);   // digit
}
TEST_CASE("lightbar: an arrow split across two packets still moves; a lone Esc after a pause is Esc") {
    Lightbar b(three());
    b.key('\x1b');
    b.key('[');
    b.key('B');
    CHECK(b.current() == 1);
    b.key('\x1b');
    b.keyTimeout();
    CHECK(b.escaped());
}
TEST_CASE("lightbar: moving redraws only the two rows that changed") {
    Lightbar b(three());
    b.draw();
    b.key('\x1b'), b.key('['), b.key('B');
    std::string r = b.takeRedraw();
    CHECK(r.find("\x1b[10;49H") != std::string::npos);
    CHECK(r.find("\x1b[11;49H") != std::string::npos);
    CHECK(r.find("\x1b[12;49H") == std::string::npos);
}
TEST_CASE("yes/no bar: Y, N, arrows and Enter") {
    YesNoBar y(10, 20, true);
    CHECK(y.key('\r') == 1);
    YesNoBar n(10, 20, true);
    CHECK(n.key('n') == 0);
}
```

- [ ] **Step 2:** Run → FAIL. **Step 3:** Implement (colours are DOS attributes `bg*16+fg`, written as SGR). **Step 4:** PASS. **Step 5:** Commit.

### Task 6: The menu runner (ViSiON/3's load loop)

**Files:**
- Create: `src/bbs/menurunner.h`, `src/bbs/menurunner.cpp`
- Test: `tests/test_menurunner.cpp`

**Interfaces:**
- Consumes: Tasks 2-5.
- Produces:

```cpp
struct MenuAction { enum Kind { None, Goto, Run, Door, Logoff, Quit } kind = None; std::string arg, args; };
class MenuRunner {
public:
    using Loader = std::function<std::string(const std::string& file)>;   // "MAIN.MNU", "MAIN.ANS": "" if none
    using Emit = std::function<void(const char*, size_t)>;
    MenuRunner(Loader load, Emit emit, std::function<std::string(const std::string&)> value);
    bool enter(const std::string& name, const AcsUser& who);  // false: no such menu or not allowed (caller falls back)
    MenuAction key(char c);           // hotkeys, typed multi-key commands (/SYSOP + Enter), and the lightbar
    void tick(uint32_t nowMs);        // Esc timeout for split arrow keys
    const std::string& current() const;
    const std::string& activity() const;  // the picked command's NODE_ACTIVITY, for Who's Online
};
```

Load loop: `//` commands run once per session and `~~` on every load (returned as `Run` actions one per `key(0)` call before input), then `|CL` if clr, then the art `NAME.ANS` through `expandArt` (streamed), then the lightbar draw if the menu has `bar=` items, then `prompt1` (expanded). Input: a single key that matches a visible command's KEYS (space-separated list) runs at once unless some command's key starts with that character and is longer (then collect until Enter); arrows/Enter go to the lightbar and its picked item's KEY runs like a hotkey. `G` (logoff with confirm) and `/G` (hang up) work on every menu. No match → redraw the prompt.

- [ ] **Step 1: Write the failing tests** (an in-memory Loader map):

```cpp
TEST_CASE("runner: shows art and prompt, a hotkey runs its command, GOTO returns a Goto") {
    std::map<std::string, std::string> fs = {
        {"MAIN.MNU", "title=Main\nprompt1=|07Command ->\ncmd=M|GOTO:MSGMENU|*|0|\ncmd=R|RUN:READMSGS|*|0|Reading\n"},
        {"MAIN.ANS", "ART"}};
    std::string out;
    MenuRunner r([&](const std::string& f) { return fs.count(f) ? fs[f] : std::string(); },
                 [&](const char* d, size_t n) { out.append(d, n); }, [](const std::string&) { return std::string(); });
    REQUIRE(r.enter("MAIN", AcsUser{10, ""}));
    CHECK(out.find("ART") != std::string::npos);
    CHECK(out.find("Command ->") != std::string::npos);
    MenuAction a = r.key('m');
    CHECK(a.kind == MenuAction::Goto);
    CHECK(a.arg == "MSGMENU");
    a = r.key('R');
    CHECK(a.kind == MenuAction::Run);
    CHECK(a.arg == "READMSGS");
    CHECK(r.activity() == "Reading");
}
TEST_CASE("runner: /SYSOP is typed then Enter; a hidden sysop command is refused below its ACS") {
    std::map<std::string, std::string> fs = {{"MAIN.MNU", "cmd=/SYSOP|GOTO:ADMIN|s255|1|\ncmd=M|GOTO:MSGMENU|*|0|\n"}};
    std::string out;
    MenuRunner r([&](const std::string& f) { return fs.count(f) ? fs[f] : std::string(); },
                 [&](const char* d, size_t n) { out.append(d, n); }, [](const std::string&) { return std::string(); });
    REQUIRE(r.enter("MAIN", AcsUser{10, ""}));
    r.key('/'); r.key('S'); r.key('Y'); r.key('S'); r.key('O'); r.key('P');
    CHECK(r.key('\r').kind == MenuAction::None);  // not a sysop
}
TEST_CASE("runner: a lightbar pick runs the item's key") {
    std::map<std::string, std::string> fs = {{"MAIN.MNU", "cmd=M|GOTO:MSGMENU|*|0|\ncmd=F|GOTO:FILEM|*|0|\nbar=5,5,31,7,M,Messages\nbar=5,6,31,7,F,Files\n"}};
    std::string out;
    MenuRunner r([&](const std::string& f) { return fs.count(f) ? fs[f] : std::string(); },
                 [&](const char* d, size_t n) { out.append(d, n); }, [](const std::string&) { return std::string(); });
    REQUIRE(r.enter("MAIN", AcsUser{10, ""}));
    r.key('\x1b'); r.key('['); r.key('B');
    MenuAction a = r.key('\r');
    CHECK(a.kind == MenuAction::Goto);
    CHECK(a.arg == "FILEM");
}
TEST_CASE("runner: a missing or empty menu is refused (the caller falls back)") {
    MenuRunner r([](const std::string&) { return std::string(); }, [](const char*, size_t) {}, [](const std::string&) { return std::string(); });
    CHECK_FALSE(r.enter("NOPE", AcsUser{10, ""}));
}
```

- [ ] **Step 2:** Run → FAIL. **Step 3:** Implement. **Step 4:** PASS. **Step 5:** Commit.

### Task 7: Node runs the menu engine; ViSiON/3 functions map to AXIS commands

**Files:**
- Modify: `src/bbs/node.h`, `src/bbs/node.cpp` (extract the main-menu `switch` into `void Node::command(char c)`; add the runner)
- Modify: `src/main.cpp` (menu loader: board storage `/menus/NAME` first, then the main over the cluster, as `themeArt`)
- Test: `tests/test_node_menus.cpp`

**Interfaces:**
- Consumes: `MenuRunner`, `MenuAction`, `acsFor` (Tasks 2, 6).
- Produces: `void Node::setMenus(MenuRunner::Loader load);` (no loader or no `MAIN.MNU` → the built-in menu as today); `static char v3Command(const std::string& func);` the table from ViSiON/3 `RUN:` names to the existing command keys: `READMSGS`/`LISTMSGAR`/`SELECTMSGAREA` → `'m'`, `NEWSCAN` → `'s'`, `COMPOSEMSG` → `'m'`, `LISTFILES`/`SELECTFILEAREA`/`DOWNLOADFILE`/`UPLOADFILE`/`SEARCH_FILES`/`FILE_NEWSCAN` → `'f'`, `CHAT` → `'p'`, `ONELINER` → `'o'`, `LISTUSERS` → `'l'`, `LASTCALLERS` → `'w'`, `WHOISONLINE` → `'w'`, `RUMORSLIST`/`RANDOMRUMOR` → `'r'`, `BBSLIST` → `'b'`, `VOTE` → `'v'`, `SHOWSTATS`/`SYSTEMSTATS` → `'i'`, `USERCONFIG` → `'u'`, `SENDPRIVMAIL`/`READPRIVMAIL` → `'e'`, `MAINLOGOFF` → `'g'`; `DOOR:CODE` → the door with that id; anything else → "Coming soon to AXIS." and the prompt again.

- [ ] **Step 1: Write the failing tests** (Rig like `tests/test_extras.cpp`, with `setMenus` over a map):

```cpp
// Rig as in tests/test_extras.cpp; menus: a std::map<std::string, std::string> handed to setMenus
TEST_CASE("with menu files the caller lands on MAIN.MNU and its keys run AXIS features") {
    Rig r;
    std::map<std::string, std::string> fs = {{"MAIN.MNU", "prompt1=AXIS MAIN ->\ncmd=O|RUN:ONELINER|*|0|Oneliners\n"}, {"MAIN.ANS", "MAIN ART"}};
    r.n.setMenus([&](const std::string& f) { return fs.count(f) ? fs[f] : std::string(); });
    r.login();
    std::string menu = r.type("?");
    CHECK(menu.find("MAIN ART") != std::string::npos);
    CHECK(menu.find("AXIS MAIN ->") != std::string::npos);
    CHECK(r.type("o").find("ONELINERS") != std::string::npos);  // the existing oneliners screen's header
}
TEST_CASE("a GOTO to a missing menu, whose fallback loops, ends on the built-in menu") {
    Rig r;
    std::map<std::string, std::string> fs = {{"MAIN.MNU", "fallback=MAIN\ncmd=X|GOTO:NOPE|*|0|\n"}};
    r.n.setMenus([&](const std::string& f) { return fs.count(f) ? fs[f] : std::string(); });
    r.login();
    r.type("x");
    CHECK(r.n.state() == Node::State::Menu);
    CHECK(r.type("?").find("Message") != std::string::npos);  // the built-in menu's items
}
TEST_CASE("an unknown RUN: function says coming soon and keeps the caller on the menu") {
    Rig r;
    std::map<std::string, std::string> fs = {{"MAIN.MNU", "cmd=X|RUN:SPONSORMENU|*|0|\n"}};
    r.n.setMenus([&](const std::string& f) { return fs.count(f) ? fs[f] : std::string(); });
    r.login();
    CHECK(r.type("x").find("Coming soon to AXIS.") != std::string::npos);
    CHECK(r.n.state() == Node::State::Menu);
}
```

(Without `setMenus` the existing extras tests are the proof the built-in menu is unchanged.)

- [ ] **Step 2:** Run → FAIL. **Step 3:** Implement; GOTO depth capped at 3 per key. **Step 4:** PASS; full suite green.
- [ ] **Step 5:** Firmware builds; commit.

### Task 8: A starter AXIS menu set on board 2, and a hand test

**Files:**
- Create: `art/menus/MAIN.MNU`, `MSGMENU.MNU`, `FILEM.MNU`, `DOORSM.MNU` (ViSiON/3's keys and commands via `v3import.py`, plus `bar=` items laid over a simple placeholder AXIS screen), `art/menus/*.ANS` (placeholder art: the existing axis header + item text; the real art pass is a later plan)
- Modify: `docs/FEATURES.md`, brain note `D:\brain\Projects\AXIS Underground CNC.md`

- [ ] **Step 1:** Upload to board 2's own storage: `MSYS_NO_PATHCONV=1 python -I tools/sdput.py COM17 art/menus /menus`.
- [ ] **Step 2:** Telnet to board 2 (`192.168.1.165:23`) with the test account from `local/` (never printed), check: MAIN shows; arrows move the bar; Enter and hotkeys both open Messages/Files/Doors; `G` asks and logs off; screenshots of each step sent to Rev.
- [ ] **Step 3:** `@STATUS` on board 2 during a menu: heap stays > 100000; no reboots in 10 minutes.
- [ ] **Step 4:** FEATURES + brain updated; commit; push.

---

**Next plans in the series** (each written when this one lands): 2 the login flow (matrix, pre-logon, login screen, login sequence, fast login, new-user application); 3 the caller areas on ViSiON/3's flows (messages, files, doors, email, rumors, BBS list, InfoForms, news, voting, conferences); 4 the WFC (pages, Online Now, tabs, snoop/chat/kick, every editor, panel keyboard); 5 the AXIS art pass and animations; ESP-IDF swaps ride along in each.
