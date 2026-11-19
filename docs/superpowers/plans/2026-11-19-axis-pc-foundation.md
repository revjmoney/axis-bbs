# AXIS PC, Project 1a: the PC app foundation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.
>
> **Testing override (Rev's standing rule, binding):** "save all the main testing for the end". Build tasks end with a
> clean build and a commit; the unit tests and the bench checks are all in the last task.

**Goal:** A native PC BBS program, `axispc`, that runs the AXIS caller experience (login, menus, messages, files,
chat between nodes, oneliners, lists, last callers, stats, native LORD, the ANSI viewer, transfers, the sysop's
portable @ commands, the LFD3K-SM radio line) on Linux from the same shared C++ code the ESP32 runs, with no
4-session cap, and ships as an AppImage and .deb built by CI for x86_64, ARM64 and 32-bit ARM.

**Architecture:** The BBS flow that lives in the board's `src/main.cpp` as static functions over per-turn globals
(the `bindSession` pattern) moves into a new shared module `src/app/` built around a real `Session` object and a
`Host` interface (clock, random, storage roots, log, the caller line). `main.cpp` is rewired to call it (one copy,
both apps). The PC app `src/pcapp/` is a new entry point with its own session manager on one event-loop thread,
reusing the plan 1 Linux pieces (net gate, axis.conf, data folder, modem line).

**Tech Stack:** C++17, CMake 3.28, g++ 13 on Ubuntu 24.04 (the i5), PlatformIO espressif32@6.9.0 for the boards,
doctest, GitHub Actions, linuxdeploy (AppImage), CPack (.deb).

**Spec:** `docs/superpowers/specs/2026-11-19-axis-pc-design.md` (Goals 1-3, "How it is built", "Project 1's scope").

## Scope note (a split of the spec's project 1, for Rev)

The spec's project 1 includes Sysop Net and the web interface. Sysop Net's glue in `main.cpp` is about 3,200 lines
(the largest block) and needs its own socket and DNS abstraction; the web interface is ESP32-specific today. They
are **project 1b**, its own plan, right after this one. 1a ends with a working standalone PC BBS without them.

## Global Constraints

- Builds and tests run on the i5 only, from committed work: `py -3.14 -I F:/axis-bbs/local/i5build.py test` (host
  tests) and `py -3.14 -I F:/axis-bbs/local/i5build.py cyd` (board image). **i5build runs the OLD test binary when
  the test build fails to compile**: check that the test count matches what you expect, or build `axis_tests` by
  hand on the i5 (`cmake --build ~/builds/<branch>/build --target axis_tests`) to see errors.
- Board flash line: the `cyd` image must stay at or under **2,200,000 B** (it is 2,148,325 B with the bench image,
  about 2,143,000 B release, at the start of this plan).
- One copy of the BBS logic: code moved into `src/app/` is deleted from `main.cpp`, which calls the shared code.
- `main.cpp` keeps working on the boards exactly as now (same screens, commands, cluster behaviour). Board-only code
  (CYD drawing, touch, LEDs, Wi-Fi join/picker/hotspot, mDNS, OTA/web, SD format, heap trace, heap floors,
  `ESP.restart`) stays in `main.cpp` or `src/platform/cyd`.
- `src/app/` must compile for both the board (Arduino/ESP-IDF, `-Os`, exceptions on) and the PC; it includes no
  Arduino header and no ESP API. Board-only needs reach it through `Host`.
- Keep `__attribute__((noinline))` on the moved session functions (the board's loop stack depends on it), through a
  macro `AXIS_NOINLINE` defined in `src/app/host.h` (empty on the PC).
- The existing Linux program (`axis`, `main_linux.cpp` around `main.cpp`) keeps building and passing its tests.
- `axispc` uses plan 1's data folder layout (`<data>/sd`, `flash`, `prefs`, `axis.conf`), so a solo install moves
  over without conversion. It never touches Rev's running service (`/usr/local/bin/axis`, `/var/lib/axis`).
- C++ only in the product; no Python in `axispc`, its installers or its runtime.
- No em dashes or double hyphens in prose, logs or user-facing text. No AI attribution in commits.
- Never print or commit LAN addresses, MACs, keys or passwords.
- Pushing: `pc/app` goes to the public GitHub repo only in the last task, after the history scan, with Rev's
  standing "Yes, push for CI" (2026-11-19).

## Review Focus

1. **A board regression from the move**: a caller on a board sees a different screen, a door or transfer breaks, or
   the loop stack runs low. (Task 7: host tests plus a flashed board walking login, a post, LORD, a download.)
2. **Many callers at once on the PC** (more than the board's 2 to 4): node numbers, chat between nodes and who's
   online stay right with 10 callers. (Task 7: `ten callers get nodes 1 to 10 and see each other`.)
3. **A slow or stalled caller** must not slow the others (the board holds a slow caller's output in a backlog).
   (Task 7: `a caller who stops reading does not stall another caller's screen`.)
4. **A radio caller on the LFD3K-SM** is a node like a telnet caller, and a hang-up from either side frees the node.
   (Task 7 bench check 3.)
5. **Shutdown and restart with callers on**: SIGTERM hangs everyone up cleanly, saves users and stats, and the next
   start shows the same users. (Task 7: `a stop saves users and stats`.)

---

### Task 1: The Host interface and the shared board data services

**Files:**
- Create: `src/app/host.h`, `src/app/boarddata.h`, `src/app/boarddata.cpp`, `src/app/themeprovider.h`,
  `src/app/themeprovider.cpp`, `src/app/stats.h`, `src/app/stats.cpp`
- Modify: `src/main.cpp` (remove the moved code, call the new classes), `CMakeLists.pc.txt` (add `src/app/*.cpp` to
  `AXISCORE_SOURCES`), `CMakeLists.txt` (the board glob already takes `src/*`; check `src/app` is included)

**Interfaces:**
- Produces, `src/app/host.h`, namespace `axis::app`:
  ```cpp
  #define AXIS_NOINLINE  // __attribute__((noinline)) on the board (AXIS_BOARD), empty on the PC
  struct Host {
      virtual uint32_t    nowMs() = 0;              // millis()
      virtual uint32_t    unixNow() = 0;            // 0 when the clock is not set
      virtual uint32_t    random32() = 0;           // hwRandom()
      virtual void        event(const std::string& line) = 0;   // logEvent: the Logs screen and the event file
      virtual std::string sdRoot() = 0;             // storageRoot(): "/sd" on the board, <data>/sd on the PC
      virtual bool        hasSd() = 0;
      virtual uint64_t    sdFreeBytes() = 0;
      virtual std::string flashRoot() = 0;          // "/littlefs" on the board, <data>/flash on the PC
      virtual bool        roomFor(uint32_t minFree, uint32_t minLargest) { return true; }  // heap floors (board only)
      virtual ~Host() = default;
  };
  ```
- Produces `BoardData` (`boarddata.h`): the oneliners, last callers and lists (rumors, BBS list, votes, ballots)
  that `main.cpp` keeps at lines ~270-500 and ~839-860, with the same files, formats and limits; constructor
  `explicit BoardData(Host& h)`; methods with the same names and shapes as today's static functions (`oneLiners()`,
  `addOneLiner`, `lastCallers()`, `noteCaller`, `listLoad(name)`, `listSave(name, lines)`, and the vote/ballot ones).
- Produces `ThemeProvider` (`themeprovider.h`): `readThemeFile`, `themeArt*`, `posterPiece`, `menuFile`,
  `menuExists`, `menuArtPieces`, `scriptFile`, `scriptNames`, `wfcArtShared` and their caches (main.cpp ~500-840),
  constructor `ThemeProvider(Host& h)`. The follower branches (`cluster->request("E", ...)`) stay in `main.cpp` as a
  fallback callback: `std::function<bool(const std::string& req, std::string& reply)> remote;` empty on the PC.
- Produces `Stats` (`stats.h`): `dayStats`, `rollDayCounters`, `countCall`, `countPost`, `statsSave/Load` (main.cpp
  ~935-1000, 1099-1125), constructor `Stats(Host& h)`. `StatsShare`/`StatsInbox` stay where they are (cluster).
- `main.cpp` gets a `BoardHost : axis::app::Host` (in `src/platform/cyd/boardhost.h`) over `millis`,
  `hwRandom`, `logEvent`, `storageRoot`, `heap_caps_*`.

- [ ] **Step 1:** Implement the three classes by moving code, not rewriting it; keep file names, formats and limits
  byte-identical. `main.cpp` holds one instance of each and calls them.
- [ ] **Step 2:** Build: `i5build.py test` (1,174 pass), `i5build.py cyd` (Flash line at or under 2,200,000 B; note
  it in the report), and the Linux `axis` target.
- [ ] **Step 3:** Commit: `app: Host interface, board data, theme provider and stats move into the shared core`.

### Task 2: The Session object and the caller flow

**Files:**
- Create: `src/app/session.h`, `src/app/session.cpp`, `src/app/services.h`
- Modify: `src/main.cpp` (sessions become `std::vector<std::unique_ptr<axis::app::Session>>`; `bindSession`,
  `unbindSession` and the per-turn globals `line`, `node`, `nativeLord`, `lordFs`, `lordLocks`, `viewer`, `job`,
  `doorStartMs`, `jobStartMs`, `jobDrawnMs`, `callStartMs`, `gate`, `wizard` are removed)

**Interfaces:**
- Consumes: Task 1's `Host`, `BoardData`, `ThemeProvider`, `Stats`.
- Produces, `services.h`: `struct Services { Host& host; BoardData& data; ThemeProvider& theme; Stats& stats;
  bbs::UserStore& users; std::function<void()> saveUsers; std::shared_ptr<const bbs::BbsConfig> cfg;
  std::function<std::string(const std::string&)> sysopRun; std::function<void(int node, const std::string&,
  const std::string&, bool page)> chat; std::function<std::vector<bbs::Node::WhoLine>()> who;
  std::function<bbs::MessageStore&()> messages; std::function<bbs::FileLibrary&()> files; };`
  (cluster-aware stores and chat stay as callbacks `main.cpp` fills; the PC app fills the local versions.)
- Produces, `session.h`: `class Session` owning `std::unique_ptr<net::TelnetStream> line`, `std::unique_ptr<bbs::Node>
  node`, the LORD pieces, `viewer`, `job`, the timers and `lastStatus`, with:
  `Session(int slot, std::unique_ptr<net::TelnetStream> line, Services& s);`
  `void wireNode();` (every hook today's `answerIn` installs on `Node`, from `Services`),
  `AXIS_NOINLINE void input(uint32_t nowMs);` (callerInput), `AXIS_NOINLINE void service(uint32_t nowMs);`
  (serviceSession, serviceJob, postStatus), `AXIS_NOINLINE void hangUp(const char* why);`,
  `AXIS_NOINLINE void startDoor();` (startDoorInner: native LORD and the ANSI viewer), `void finishLord();`,
  `void finishViewer();`, `bool done() const;`, `int nodeNumber() const;`.
- Every helper that read the per-turn globals takes `Session&` or is a `Session` member. Hooks that outlive a turn
  (chat, `setDirect`, `setBackedUp`) capture the `Session*` (sessions are `unique_ptr`, so they do not move).

- [ ] **Step 1:** Move the code (main.cpp ~915-1090, 2131-2193, 5877-6040, 6056-6385, 8136-8338) into `Session`,
  replacing global reads with members. `main.cpp`'s `loop()` iterates `sessions` and calls `input`/`service`.
- [ ] **Step 2:** Build tests, board image (Flash line), Linux `axis`. Then flash board 1 with `i5build.py
  cyd-bench --flash COM17 --mac <board 1 MAC from the brain>` and walk one telnet login, a menu and a logoff
  (record it in the report; the full board walk is Task 7).
- [ ] **Step 3:** Commit: `app: the Session object and the caller flow, shared by the board and the PC`.

### Task 3: The portable sysop commands

**Files:**
- Create: `src/app/commands.h`, `src/app/commands.cpp`
- Modify: `src/main.cpp` (`runCommand` keeps only board-only commands and falls through to the table)

**Interfaces:**
- Produces: `bool runPortableCommand(const std::string& cmd, Services& s, std::vector<Session*>& sessions,
  bbs::Print& out);` returning false for a command it does not know. Moves every `@` command that does not need
  board hardware: `@USERS`, `@POSTS`, `@FILES`, `@SETTINGS`, `@NODES`, `@BCAST`, `@KICK`, the list and user
  commands, `@DOORSTATE` stays board-only. Board-only (stay in `main.cpp`): `@HEAPWIN`, `@SHOT`, `@TAP`, `@RESTART`,
  `@WIFI`, `@SD...`, `@LKEY`, `@LSCREEN`, `@SPRITEFAIL`, `@WFCFX`, `@DDNS`, `@CLUSTERKEY`, `@MODEM` (board side).
- `sysopshell.cpp`'s paging and reply limits are unchanged.

- [ ] **Step 1:** Move the commands; identical output text.
- [ ] **Step 2:** Build tests, board image, Linux `axis`.
- [ ] **Step 3:** Commit: `app: the portable sysop commands in the shared core`.

### Task 4: The PC caller line

**Files:**
- Create: `src/pcapp/pcline.h`, `src/pcapp/pcline.cpp`

**Interfaces:**
- Produces: `class PcLine : public net::TelnetStream` over a non-blocking socket fd (Linux now, Winsock-ready
  behind `#ifdef _WIN32` like `src/platform/pc/tcpstream.cpp`), with what `WifiStream` gives the board: a backlog
  for output the socket will not take yet, `setHold(bool)`, `drain()`, `backlog()`, `bytesIn()`, `bytesOut()`,
  `remoteIp()` (uint32_t, network order), `inject(const std::string&)`, `tapNode`. `static std::unique_ptr<PcLine>
  fromFd(int fd, uint32_t ip);`.
- The radio line: an LFD3K-SM call arrives as a socketpair fd from the modem line (as plan 1's `modemline.cpp`
  does through `hostnetInjectClient`), so it is a `PcLine` too, with its link label set the way the board sets it.

- [ ] **Step 1:** Implement, copying `WifiStream`'s backlog and hold behaviour (same thresholds, `kHoldLow` etc.).
- [ ] **Step 2:** Build (tests; `axispc` does not exist yet, so this compiles in the test core only).
- [ ] **Step 3:** Commit: `pcapp: the PC caller line (telnet, backlog, hold)`.

### Task 5: The `axispc` program

**Files:**
- Create: `src/pcapp/main.cpp`, `src/pcapp/pchost.h`, `src/pcapp/pchost.cpp`, `src/pcapp/listener.h`,
  `src/pcapp/listener.cpp`, `src/pcapp/console.h`, `src/pcapp/console.cpp`
- Modify: `CMakeLists.pc.txt` (new target `axispc` on UNIX: `src/pcapp/*.cpp`, `src/app` via `axiscore`,
  `src/platform/linux/{netgate,hostconf,paths,modemline}.cpp` adapted to not need the Arduino shim; no
  `main.cpp`, no `arduino_core.cpp`, no LovyanGFX)

**Interfaces:**
- `PcHost : app::Host` over `std::chrono::steady_clock`, `std::random_device`, the event log file
  `<data>/flash/events.log` (same rotation as the board), roots `<data>/sd` and `<data>/flash`; `roomFor` always true.
- `Listener`: telnet on `lan_port` (axis.conf, default 2323) and `public_port` (default 0), through `NetGate`
  (`max_callers` default 16, `max_per_ip` 3, `max_per_ip_lan` 8, `rate_per_ip` 20, as plan 1).
- `main.cpp` (pcapp): `axispc [--terminal | --headless] [--data DIR] [--seed DIR] [--modem /dev/ttyUSB0]`;
  default `--terminal` on a tty, `--headless` otherwise (plan 1's start-mode rule); refuses to run as root.
  One event-loop thread: accept, `modemLinePoll`, each `Session::input`/`service`, remove `done()` sessions, a 5 ms
  idle sleep when nothing moved. Node numbers: lowest free from 1, no cap but `max_callers`.
  SIGTERM/SIGINT: every session `hangUp("the BBS is stopping")`, save users and stats, exit 0.
  First boot (no `setupDone`): the setup gate with the PIN printed to the log, as the board's headless path does.
- `console.cpp`: the terminal mode's local sysop screen: an 80-column WFC from `wfcSnapshot()` data and the 80x25
  WFC art (`src/bbs/wfc80_art.h`), with the node list (pc/esp tags), the event lines, and keys for local logon and
  the in-BBS sysop menu (`SysopShell`), as the CYD's f1..f7 do.

- [ ] **Step 1:** Implement; build `axispc` on the i5 (`cmake --build ... --target axispc`), no warnings.
- [ ] **Step 2:** Smoke (not a test): `timeout 30 axispc --headless --data /tmp/axispc-t5` with `lan_port=2343` in
  its axis.conf: the setup PIN is logged and telnet on 2343 answers. Never touch Rev's service.
- [ ] **Step 3:** Commit: `pcapp: the axispc program (session manager, telnet, radio line, terminal WFC)`.

### Task 6: Packaging and CI

**Files:**
- Create: `.github/workflows/pc.yml`, `installer/pc/axispc.desktop`, `installer/pc/axispc.svg` (from the existing
  AXIS logo art), `installer/pc/axispc.service` (as `installer/linux/axis.service`, `ExecStart=/usr/bin/axispc
  --headless --data /var/lib/axis`, `AmbientCapabilities=CAP_NET_BIND_SERVICE`), `installer/pc/README.md`
- Modify: `CMakeLists.pc.txt` (install rules and CPack .deb: package `axispc`, depends only on libc/libstdc++)

**Interfaces:**
- `pc.yml`: on push to `pc/app` and on tags `pc-v*`; jobs on `ubuntu-24.04` (x86_64) and `ubuntu-24.04-arm`
  (ARM64) runners, and 32-bit ARM by cross-compiling (`arm-linux-gnueabihf` toolchain); each builds `axispc`, runs
  `axis_tests` (natively where the runner can), builds the .deb with CPack and the AppImage with linuxdeploy
  (pinned release), and uploads both as artifacts; on a `pc-v*` tag they attach to a GitHub release.
- Artifact names: `axispc-<version>-<arch>.AppImage`, `axispc_<version>_<debarch>.deb`.

- [ ] **Step 1:** Implement; check the workflow with `actionlint` if available on the i5, else by reading.
- [ ] **Step 2:** Build the .deb and AppImage locally on the i5 (x86_64) once: `cpack -G DEB` and the linuxdeploy
  command the workflow uses; the AppImage starts with `--help`.
- [ ] **Step 3:** Commit: `pcapp: .deb and AppImage packaging, CI for x86_64, ARM64 and 32-bit ARM`.

### Task 7: Testing (all of it, at the end), push and CI

**Files:**
- Create: `tests/test_app_session.cpp`, `tests/test_pcline.cpp` (in the Linux block of `CMakeLists.pc.txt`)
- Modify: `tests/` as needed for moved code (existing tests keep passing unchanged where behaviour is unchanged)

- [ ] **Step 1: Unit tests** (few, per Rev):
  - `board data keeps the same files and formats` (a oneliner and a last caller round-trip through `BoardData`).
  - `ten callers get nodes 1 to 10 and see each other` (ten `Session`s over `ScriptStream`-backed lines; who's online
    lists ten, a chat line reaches node 7).
  - `a caller who stops reading does not stall another caller's screen` (`PcLine` with a full socket buffer goes on
    hold; another session's output still flows).
  - `a stop saves users and stats` (a new user, a call counted, the shutdown path; reload shows both).
  - `a portable @ command answers the same on the board and the PC` (`@USERS 0` through `runPortableCommand`).
- [ ] **Step 2:** `i5build.py test` all pass (1,174 plus the new ones; check the count); `i5build.py cyd` at or under
  2,200,000 B; `axis` and `axispc` build.
- [ ] **Bench** (screenshots of each visible step, sent to Rev):
  1. Board 1 flashed with this branch (MAC checked): telnet login, a message post, LORD, a download, logoff; the WFC
     shows the call; the i5 cluster still joins (the old Linux `axis` still runs there).
  2. `axispc --terminal` on the i5 with its own data folder and ports (not Rev's service): the 80-column WFC; 10
     telnet callers at once; a post and a oneliner seen by another caller; SIGTERM saves and restarts clean.
  3. Radio: with Rev's go, stop the `axis` service for the test, run `axispc --headless --modem /dev/ttyUSB0`, one
     V4 to V3 call: it shows as a node with its radio tag, walks a menu, hangs up; then the service is started again.
- [ ] **Push and CI:** scan the branch history (`git log -p origin/main..pc/app`) for addresses, MACs, keys and AI
  attribution; push `pc/app`; the `pc.yml` run builds all three architectures and the artifacts download and start.
- [ ] **Brain:** `D:\brain\Projects\AXIS Native.md`: project 1a done, `axispc` usage, the CI artifacts.
- [ ] **Commit:** `tests: the shared session flow and the PC line`.
