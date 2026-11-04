# Sysop Control Panel Implementation Plan (part 1 of 3: controls)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A PC tray app that finds every AXIS board on the LAN and serves a sysop control panel (phone or PC browser) with every sysop and BBS control, auto-populated from what is online.

**Architecture:** Each board opens a small LAN-only "panel port" (TCP 2326). After a challenge login, the board runs panel lines through the same command handler as its serial console (one code path, so a panel button and a serial command always do the same thing), returns the output, and pushes event-log lines and a status line. New sysop actions are added as serial `@` commands. The PC app (Python, asyncio) discovers boards by the cluster's `AXIS!` UDP announcements, keeps one connection per board, routes settings and user changes to the current main, and serves the web UI plus a WebSocket to browsers.

**Tech Stack:** Firmware C++17 (Arduino ESP32, PlatformIO), PC tests CMake + doctest; PC app Python 3.12, aiohttp, pystray, Pillow, pytest.

**Spec:** `docs/superpowers/specs/2026-11-04-sysop-control-panel-design.md`

**Parts 2 and 3 (separate plans, after this lands):** console login (new sysop session, break in); the live screen wall.

## Global Constraints

- Panel reachable on the LAN only. The board's panel port is 2326 (cluster uses 2324 and 2325; the router forwards only an outside port to telnet 23).
- Nothing is shown or done before login. Login is the config sysop password or any user at security 255; the board checks a nonce challenge against the stored salted hash. The PC app never stores a password.
- A panel control and the same serial command run the same code.
- Callers are never slowed down by the panel; one panel connection per board.
- Flash: `cyd` and `headless` builds at or under 96% after this plan.
- Settings and user changes go to the current main (the board whose `@NODES` marks it main).
- Never put Rev's WiFi name, the public hostname or port, or any password in the repo, logs or screenshots.
- No AI attribution in commits; no em dashes or double hyphens in user-facing text.
- Firmware test command: `cmake --build build && ./build/axis_tests.exe` (Git Bash, from `F:\axis-bbs`). App test command: `python -m pytest tools/panel -q`.

## Review Focus

1. A wrong password three times on the panel port closes the connection and logs it (Task 3 test `three bad panel logins close the link and are logged`).
2. A board that drops off the LAN disappears from the panel within 15 s and comes back when it returns (Task 6 test `a board that goes quiet is dropped and comes back`).
3. A settings change sent while the main is changing hands goes to the new main, not a follower (Task 6 test `settings go to whichever board is main now`).
4. Command output larger than one TCP write (a long `@LOG`) arrives whole (Task 3 test `long command output arrives whole, ended by the done marker`).
5. A browser tab left open across a PC app restart reconnects and asks to log in again (Task 8 manual check, Step 3).

---

### Task 1: Flash room

- [ ] **Step 1:** Do Task 1 of `docs/superpowers/plans/2026-11-04-sd-disc-changer.md` (build option `AXIS_SD_POOL`, off by default). Follow its steps exactly.
- [ ] **Step 2:** Run `pio run -e cyd` and `pio run -e headless`. Expected: SUCCESS, both under 96%.
- [ ] **Step 3:** Commit as that task says.

### Task 2: One command handler for serial and panel

**Files:**
- Modify: `src/main.cpp` (`static void handleSerial()` at about line 1807 and every `Serial.print*` inside the command branches)

**Interfaces:**
- Produces: `static void runCommand(const String& cmd, Print& out);` every `@` command writes its reply to `out`. `handleSerial()` reads a line and calls `runCommand(cmd, Serial)`.

- [ ] **Step 1:** Split the command branches out of `handleSerial()` into `runCommand`, replacing `Serial.print`/`printf`/`println` inside them with `out.`. Leave boot logging and `logEvent` alone.
- [ ] **Step 2:** Build `pio run -e cyd`. Expected: SUCCESS.
- [ ] **Step 3:** Flash board 1 (COM18) and check `@NODES` and `@WEB` on the serial console answer as before.
- [ ] **Step 4:** Commit: `One command handler for the serial console (and the coming panel)`.

### Task 3: Panel port protocol (portable, PC-tested)

**Files:**
- Create: `src/bbs/panelproto.h`, `src/bbs/panelproto.cpp`
- Test: `tests/test_panelproto.cpp` (add to `CMakeLists.txt`)

**Interfaces:**
- Consumes: `checkPassword`, `sha256Hex` (`src/bbs/password.h`), `UserStore`, `BbsConfig`.
- Produces (namespace `axis::bbs`):
  - `class PanelSession { public: PanelSession(const BbsConfig& cfg, const UserStore& users, std::string nonce); std::string greeting() const; std::string line(const std::string& in); bool authed() const; bool closed() const; std::string who() const; std::function<std::string(const std::string&)> run; std::function<void(const std::string&)> onEvent; };`
  - Wire format, one line each, `\n` terminated:
    - board: `AXISPANEL 1 <nonce>` (greeting)
    - app: `SALT <handle>` → board: `SALT <salt>` (`SALT -` for unknown; the config sysop handle maps to `sysopSalt`; users must be security 255)
    - app: `AUTH <handle> <proof>` where `proof = sha256Hex(nonce + hashPassword(salt, password))` → board: `OK` or `NO`; third `NO` → `BYE` and `closed()`; `onEvent("panel: 3 bad logins")`
    - after `OK`: `CMD <@command>` → board: the command's output lines, then `END`
    - board pushes at any time after `OK`: `EVT <event log line>` and `STAT <key=value ...>`
  - `line()` returns everything to send back (may be several lines). `CMD` before `OK` answers `NO`.
- [ ] **Step 1: Write the failing tests:**
  - `a sysop gets the salt and logs in with the proof`
  - `a non-sysop user is refused` (security 20 user: `SALT -`, `AUTH` → `NO`)
  - `commands before login are refused`
  - `three bad panel logins close the link and are logged`
  - `long command output arrives whole, ended by the done marker` (`run` returns 6 KB: reply holds it all and ends `END\n`)
- [ ] **Step 2:** Run. Expected: compile failure.
- [ ] **Step 3:** Implement.
- [ ] **Step 4:** Run the suite. Expected: all pass.
- [ ] **Step 5:** Commit: `Panel protocol: challenge login against the sysop hash, commands, events`.

### Task 4: New sysop commands

**Files:**
- Modify: `src/main.cpp` (`runCommand`), `src/bbs/node.h`, `src/bbs/node.cpp`
- Test: `tests/test_node.cpp`, `tests/test_users.cpp`

**Interfaces:**
- Produces serial commands (also panel commands through Task 2):
  - `@KICK <node>` (the CYD kick), `@ADDTIME <node> <minutes>` (this call only: `Node::addMinutes(int)`), `@FORCETC <node>` (as the Oblivion/2 force: `Node::forceTeleconference()`), `@NOTE <node> <text>` (a node message from the sysop, also answers a page), `@BCAST <text>`
  - `@USERS` (one line per user: handle, status, security, calls, flags), `@USER <handle>` (detail), `@VALIDATE <handle>`, `@DENY <handle>`, `@SEC <handle> <level>`, `@PERM <handle> <files|msgs|doors|freedl> <on|off>`, `@TWIT <handle> <on|off>`
  - `@SETTINGS` (axis.ini text with every `*_salt`/`*_hash` line left out), `@RUMORS`, `@LINERS`, `@BBSLIST`, `@VOTES`
  - `@STATUS` (one `key=value` line: board id, role, nodes, heap, largest block, uptime, RSSI, SD MB free, callers on)
- [ ] **Step 1: Write the failing tests** for the node and user-store parts: `added minutes extend this call only` (`minutesLeft()` grows by 10 after `addMinutes(10)`, a new call starts fresh), `a forced caller lands in the teleconference`, `validate, deny, level, permissions and twit change the stored user` (through `UserStore` functions the commands call).
- [ ] **Step 2:** Run. Expected: fail.
- [ ] **Step 3:** Implement the node/user functions, then the `@` commands calling them (cross-board actions go through the existing cluster node-link kinds, as the MultiNode menu does).
- [ ] **Step 4:** Run the suite. Expected: all pass. Build `cyd` and `headless` (under 96%).
- [ ] **Step 5:** Commit: `Sysop commands: kick, add time, force to teleconference, notes, users, settings and lists`.

### Task 5: Panel port on the boards

**Files:**
- Modify: `src/main.cpp`

**Interfaces:**
- Consumes: `PanelSession` (Task 3), `runCommand` (Task 2), `@STATUS` (Task 4).
- Produces: a `WiFiServer` on 2326, one client at a time (a second gets `BUSY` and is closed). The nonce comes from `hwRandom`. `run` captures `runCommand` output into a string (`StreamString` or a small `Print` that appends). Every `logEvent` line also goes out as `EVT`, and `STAT` every 5 s.
- [ ] **Step 1:** Implement.
- [ ] **Step 2:** Build, flash all three boards (COM18, COM17 `cyd`; COM15 `headless-ring`).
- [ ] **Step 3:** From the PC: a short Python script in the scratchpad connects to 192.168.1.164:2326, logs in with the sysop password read from `F:\axis-bbs\local\test-credentials.txt` (never printed), sends `CMD @NODES`, and prints the reply. Expected: the node list, then `END`; `STAT` lines every 5 s.
- [ ] **Step 4:** Commit: `Panel port 2326 on every board`.

### Task 6: PC app core (discovery, board links, routing)

**Files:**
- Create: `tools/panel/axispanel/__init__.py`, `discovery.py`, `board.py`, `hub.py`
- Test: `tools/panel/tests/test_hub.py` (with a fake board server speaking the Task 3 protocol)

**Interfaces:**
- Produces:
  - `discovery.Listener(on_board: Callable[[str], None])` listening on UDP 2325 for `AXIS! <ip> <epoch> <node> <sd>` and calling `on_board(ip)` for each new IP.
  - `board.BoardLink(ip, port=2326)` with `async login(handle, password) -> bool`, `async command(cmd) -> list[str]`, an `events` asyncio queue (`("evt", line)` / `("stat", dict)`), and `alive` (False after 15 s without `STAT`).
  - `hub.Hub` owning every `BoardLink`; `main()` returns the link whose `@NODES` reply marks it main; `async command(cmd, board=None)` sends user, settings and list commands to the main and board commands to the named board.
- [ ] **Step 1: Write the failing tests:** `boards found by their announcements get a link`, `a board that goes quiet is dropped and comes back`, `settings go to whichever board is main now`, `a wrong password is reported, not retried`.
- [ ] **Step 2:** Run `python -m pytest tools/panel -q`. Expected: fail (modules missing).
- [ ] **Step 3:** Implement.
- [ ] **Step 4:** Run. Expected: all pass.
- [ ] **Step 5:** Commit: `Panel app core: finds the boards, one link each, routes to the main`.

### Task 7: Web UI and tray

**Files:**
- Create: `tools/panel/axispanel/web.py`, `tools/panel/axispanel/tray.py`, `tools/panel/axispanel/static/index.html`, `static/panel.css`, `static/panel.js`, `tools/panel/run.py`, `tools/panel/README.md`
- Test: `tools/panel/tests/test_web.py`

**Interfaces:**
- Consumes: `Hub` (Task 6).
- Produces: aiohttp app on `0.0.0.0:8326`: `GET /` the panel, `GET /ws` a WebSocket. Browser → app: `{"login": {"handle", "password"}}` (passed to the boards' challenge, never stored), `{"cmd": "@...", "board": ip|null}`. App → browser: `{"boards": [...]}` (id, ip, role, nodes, stats, alive), `{"evt": {board, line}}`, `{"reply": {cmd, lines}}`. Until login the WebSocket answers only `{"need": "login"}`.
  - Pages (tabs in the AXIS Oblivion/2 look of the website, VGA font from `site/fonts/AxisVGA.woff2`): **Nodes** (a card per node with kick, add time, force to teleconference, note), **Users** (list, detail, validate, deny, level, permissions, twit, add), **Settings** (`@SETTINGS` as a form; save sends `@SET key=value`), **Lists** (rumors, oneliners, BBS list, votes), **System** (per board: status, event log, No-IP and web, cards, LORD Setup note). Everything is built from `{"boards"}` and redrawn when it changes.
  - `tray.py`: a pystray icon with "Open panel" (default browser to `http://localhost:8326/`) and "Quit"; `run.py` starts hub, web and tray.
- [ ] **Step 1: Write the failing tests:** `the socket asks for login first`, `a command from the browser reaches the hub and the reply comes back`, `board changes are pushed to the browser`.
- [ ] **Step 2:** Run. Expected: fail.
- [ ] **Step 3:** Implement.
- [ ] **Step 4:** Run. Expected: all pass.
- [ ] **Step 5:** Commit: `Panel app: web UI and tray`.

### Task 8: Live check, docs, brain

- [ ] **Step 1:** Run `python tools/panel/run.py`; open the panel on the PC and on Rev's phone (same LAN). Expected: three boards and seven nodes appear without setup; `@NODES`, users and settings load.
- [ ] **Step 2:** Pull the headless board's USB: it disappears within 15 s; plug it back: it returns.
- [ ] **Step 3:** Restart the app with a browser tab open: the tab reconnects and asks to log in again.
- [ ] **Step 4:** Docs: a "Sysop control panel" topic in `site/docs.html` (what it is, how to start it, the panel port, the new `@` commands in the serial table), `README.md` row, `docs/FEATURES.md` row and change log, brain note. Screenshots only with nothing private visible.
- [ ] **Step 5:** Commit: `Docs: the sysop control panel`.
