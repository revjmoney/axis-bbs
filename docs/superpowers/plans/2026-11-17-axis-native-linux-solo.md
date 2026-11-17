# AXIS Native, Plan 1: Linux solo Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A Linux `axis` program on the i5 that runs the board's own AXIS as a solo BBS, takes telnet and V3
radio callers, and shows the full WFC in a terminal, in a window, or runs headless, with hardened networking.

**Architecture:** Compile the board's unchanged `src/main.cpp` (plus the portable core already in
`CMakeLists.pc.txt`) against a Linux host layer in `src/platform/linux/` that supplies the Arduino and
ESP-IDF calls `main.cpp` uses. The window is LovyanGFX's own SDL2 panel; the terminal is the board's serial
WFC on stdin and stdout. The cluster is compiled out (`AXIS_CLUSTER=0`); plan 2 adds it.

**Tech Stack:** C++17, CMake 3.28, g++ 13 on Ubuntu 24.04 (the i5), LovyanGFX 1.3.0 (from the i5's
PlatformIO libdeps, not downloaded), SDL2 (optional, `libsdl2-dev`), systemd, ufw, fail2ban.

**Spec:** `docs/superpowers/specs/2026-11-17-axis-native-design.md` (read "Code layout", the amended
paragraph, first).

## Global Constraints

- **`src/main.cpp` and the core (`src/bbs`, `src/net`, `src/cluster`, `src/door`, `src/lord`, `dosemu`)
  are not edited.** If a change there is truly unavoidable, it must be a few lines, change nothing for the
  board, be listed in the task report, and the board build (`F:/axis-bbs/local/i5build.py cyd`) must be
  shown to still build at the same or smaller size. The AXIS nets session is editing `main.cpp` in parallel.
- Builds and runs happen **on the i5** (address in the i5 brain note), never on this PC. Never print or commit LAN
  addresses, MACs, the cluster key or passwords in code, docs or commit messages (use placeholders).
- **No downloads.** LovyanGFX comes from the i5's existing PlatformIO copy
  (`~/axis-bbs/.pio/libdeps/cyd/LovyanGFX`), passed in as the CMake cache variable `LOVYANGFX_DIR`.
  SDL2 dev files are installed by Rev (`sudo apt install libsdl2-dev`); never run sudo.
- The program is one executable named `axis`. Window support is the CMake option `AXIS_WINDOW`
  (default ON if `find_package(SDL2)` succeeds, else OFF); without it the binary still builds and runs
  terminal and headless.
- Cluster off: build with `-DAXIS_CLUSTER=0`. Every other feature flag keeps the board's default
  (`src/bbs/features.h`).
- Data folder: `--data DIR`; default `/var/lib/axis` when running as user `axis`, else `~/.axis`.
  Inside it: `sd/` (the board's `/sd`), `flash/` (the board's LittleFS), `prefs/` (NVS namespaces),
  `axis.conf`, `log/`.
- Ports: the board's telnet server (`WiFiServer server`, port 23 on the board) listens on the **LAN port
  2323**. The **public port is off by default** (`public_port=0`); when set, it also listens there, solo only.
  The panel server (`2326`) listens on LAN only.
- Testing: **no per-task tests.** Each build task ends with a clean build on the i5 and a commit. All
  testing is Task 9, with only the few automated tests named there (Rev: "please do not write a million
  tests for this. save all the main testing for the end").
- No em dashes or double hyphens in user-facing text or docs. Commits carry no AI attribution.
- Never use `pkill -f` inline over ssh; stop processes from script files.

## Review Focus

1. A caller who connects and sends nothing (or only half a telnet handshake) must not hold a node or stall
   other callers; the existing handshake and idle timeouts must fire. (Task 9, check 4.)
2. A slow reader (radio speeds or a deliberately slow TCP reader) must not stall a second caller who is
   being served at the same time. (Task 9, check 5.)
3. A connection flood from one IP (hundreds per second) must be refused at the accept gate before any
   `main.cpp` code runs for it, and other IPs must still get in. (Task 9, check 6.)
4. The V3 unplugged mid-call or `/dev/ttyUSB0` vanishing must hang up that caller cleanly and the program
   must keep running and pick the modem up again when it comes back. (Task 9, check 8.)
5. Ctrl-C, SIGTERM or closing the window mid-call must leave the terminal sane (not raw), flush users and
   message bases the way the board's own save paths do, and close sockets. (Task 9, check 9.)

---

### Task 1: Host layer skeleton that compiles and links `main.cpp`

**Files:**
- Create: `src/platform/linux/arduino/` with headers named exactly as `main.cpp` and `platform/cyd/*.h`
  include them: `Arduino.h`, `WiFi.h`, `WiFiUdp.h`, `ESPmDNS.h`, `Preferences.h`, `SD.h`, `LittleFS.h`,
  `FS.h`, `WebServer.h`, `base64.h`, `esp_heap_caps.h`, `esp_system.h`, `esp_random.h`, `esp_timer.h`,
  `mdns.h`, `freertos/FreeRTOS.h`, `freertos/task.h`, `lwip/sockets.h`, `lwip/dns.h`,
  `soc/soc_memory_types.h`, `mbedtls/base64.h` (find the full list by compiling; the spike found these).
- Create: `src/platform/linux/arduino_core.cpp` (String, time, Serial, Print/Stream, random, ESP and heap
  info, pins as no-ops), `src/platform/linux/main_linux.cpp` (process `main()` that calls `setup()` then
  `loop()` forever).
- Create: Linux versions of every `src/platform/cyd/*.cpp` the board links, in `src/platform/linux/`:
  `board.cpp`, `wifi.cpp`, `web.cpp`, `storage.cpp`, `config_store.cpp`, `sdfiles.cpp`, `serialfiles.cpp`,
  `heapscope.cpp`, `screen.cpp`, `ansi_screen.cpp` (or reuse the cyd one if it compiles against
  LovyanGFX's host build), `tlserror.cpp`. `clusternet.cpp` is not built (`AXIS_CLUSTER=0`). Each keeps the
  cyd header's signatures exactly; in this task bodies may be minimal stubs that later tasks fill in.
- Modify: `CMakeLists.pc.txt`: add `add_executable(axis ...)` linking `axiscore`, with
  `target_compile_definitions(axis PRIVATE AXIS_CLUSTER=0 LGFX_USE_V1)`, include dirs
  `src/platform/linux/arduino` (first), `${LOVYANGFX_DIR}/src`, and the cyd headers' dir. Add
  `option(AXIS_WINDOW ...)` and `LOVYANGFX_DIR` as described in Global Constraints. Linux only
  (`if(UNIX AND NOT APPLE)`); the `axis_tests` target is untouched.

**Interfaces:**
- Produces: `uint32_t millis(); uint32_t micros(); void delay(uint32_t ms);` (monotonic clock, `delay`
  sleeps); `class String` with the subset of the Arduino API `main.cpp` uses (`c_str, length, +=, +, ==,
  equalsIgnoreCase, startsWith, endsWith, indexOf, substring, toInt, trim, toUpperCase, toLowerCase,
  replace, reserve, printf-free constructors from int/unsigned/float`), backed by `std::string`;
  `class Print { virtual size_t write(uint8_t); virtual size_t write(const uint8_t*, size_t); print,
  println, printf }`; `class Stream : public Print { available, read, peek }`; global `Serial` of type
  `HostSerial : public Stream` (stdin/stdout; mode set in Task 5); `uint32_t esp_random()` (getrandom);
  `ESP.restart()` (clean exit with code 3 so systemd restarts it), `ESP.getFreeHeap()` and
  `heap_caps_get_free_size/get_largest_free_block` returning fixed large values (64 MiB, so the board's
  memory guards never trip); `RTC_NOINIT_ATTR` empty.
- LovyanGFX: when `AXIS_WINDOW` is OFF, `tft` is an `lgfx::LGFX_Device` with no panel and
  `boardIsHeadless()` returns true; Task 6 adds the SDL panel.

- [ ] **Step 1:** Build the way the repo already builds the PC tree on the i5: `CMakeLists.txt` includes
  `CMakeLists.pc.txt` when `IDF_PATH` is unset, so `cmake -S . -B build && cmake --build build --target axis`
  in a copy of this branch on the i5 (`F:/axis-bbs/local/i5build.py` shows how it stages a branch there;
  a `tar` over ssh into `~/axis-native` also works), with
  `-DLOVYANGFX_DIR=$HOME/axis-bbs/.pio/libdeps/cyd/LovyanGFX`.
- [ ] **Step 2:** Add headers and stubs until `make axis` compiles and links with no errors.
- [ ] **Step 3:** Run `./axis --headless --data /tmp/axis-t1` for 5 s; expected: the board's boot log
  lines on stdout (`axis: ...`), no crash. (A smoke run, not a test.)
- [ ] **Step 4:** Commit: `native: Linux host layer skeleton, main.cpp builds as the axis program`.

### Task 2: Storage, settings and the data folder

**Files:**
- Create: `src/platform/linux/paths.h/.cpp`, `src/platform/linux/fs_remap.h` (force-included into
  `main.cpp` and the `src/platform/linux/*.cpp` only, via `target_compile_options(... -include ...)` on
  those sources, never on `axiscore`).
- Modify: `src/platform/linux/storage.cpp`, `config_store.cpp`, `sdfiles.cpp`, Arduino `File`, `SD`,
  `LittleFS`, `Preferences` in the host headers.

**Interfaces:**
- Produces: `std::string axisDataDir();` (set from `--data` or the default in Global Constraints, created
  with mode 0700); `std::string hostPath(const char* boardPath);` mapping `"/sd..."` to
  `<data>/sd...`, `"/littlefs..."` to `<data>/flash...`, anything else unchanged.
- `fs_remap.h` routes `fopen, stat, mkdir, rename, remove, opendir, unlink, rmdir, access, open` through
  `hostPath` for code that passes the board's absolute paths. Arduino `LittleFS.open("/x")` maps to
  `<data>/flash/x`; `SD.open("/x")` to `<data>/sd/x`; `File` wraps a `FILE*` with the Arduino methods
  `main.cpp` calls (`read, write, print, available, size, close, operator bool, isDirectory,
  openNextFile, name, seek, position`).
- `storageRoot()` returns `"/sd"` (the board path; the remap makes it real) so `PosixHostFs` roots work.
  `storageInit()` reports the SD as present; `storageFreeBytes()` from `statvfs` on `<data>/sd`.
- `Preferences` stores each namespace as `<data>/prefs/<namespace>.ini` (key=value, values escaped),
  written atomically (temp file + rename).
- First run: if `<data>/sd` is empty and `--seed DIR` was given, copy `DIR` into it (a board card folder);
  without `--seed`, start empty and let the board's own first-boot code create what it creates.

- [ ] **Step 1:** Implement the above; rebuild on the i5 with no errors.
- [ ] **Step 2:** Smoke run with `--seed` pointed at a copy of a card folder Rev provides (ask in the
  report if none is on the i5; do not invent one); expected: boot log shows the themes and areas found.
- [ ] **Step 3:** Commit: `native: data folder, SD and flash paths, settings files`.

### Task 3: Network: telnet server, client sockets, hardened accept gate

**Files:**
- Create: `src/platform/linux/netgate.h/.cpp`, `src/platform/linux/hostnet.cpp` (WiFi, WiFiServer,
  WiFiClient, IPAddress, DNS, mDNS stand-ins).
- Modify: `src/platform/linux/arduino/WiFi.h`, `lwip/sockets.h`, `lwip/dns.h`, `ESPmDNS.h`, `mdns.h`,
  `src/platform/linux/wifi.cpp`.

**Interfaces:**
- `WiFiServer(uint16_t port)`: port 23 maps to `lan_port` (2323) plus, if non-zero, `public_port`;
  any other port (2326) maps to itself, LAN only. `begin()` binds non-blocking sockets with
  `SO_REUSEADDR`; `hasClient()` / `available()` / `accept()` take the next connection that **passed the
  gate**. `WiFiClient` wraps a non-blocking fd: `connected, available, read, peek, write, stop, remoteIP,
  remotePort, fd()`; `TCP_NODELAY` and keepalive on. `::send(fd, ..., MSG_DONTWAIT)` from `main.cpp` and
  `wifistream.h` works as is on Linux.
- `class NetGate { bool admit(uint32_t ip, bool lan, uint32_t nowMs); void closed(uint32_t ip); }`:
  refuses (accept, then close at once, before `main.cpp` sees it) when: total open callers ≥ `max_callers`
  (default 16); open from this IP ≥ `max_per_ip` (default 3; LAN addresses 8); new connections from this
  IP in the last 60 s ≥ `rate_per_ip` (default 20). Over the limit for 60 s straight bans the IP for
  10 minutes (logged as `axis: gate: ban <ip> 10 min`). Defaults live in `axis.conf` keys `max_callers`,
  `max_per_ip`, `rate_per_ip`. "LAN" means the address is in an RFC 1918 or link-local range.
  The board's own `LoginGuard` (bad passwords) still runs after the gate, unchanged.
- `RLIMIT_NOFILE` raised to its hard limit at start; if below 256, log a warning.
- `WiFi.localIP()` is the first non-loopback IPv4 address; `WiFi.status()` is `WL_CONNECTED`;
  `WiFi.macAddress()` a stable value derived from `/etc/machine-id` (not a real MAC).
- DNS (`dns_gethostbyname` with callback): resolve with `getaddrinfo` on a short-lived thread, deliver the
  callback from the main loop (a queue drained in `loop()`'s next `delay()`/`millis()` turn, or a hook in
  `main_linux.cpp` between `loop()` calls). mDNS publish and the `_lfd3k._tcp` query are no-ops that log
  once (plan 1 is solo; the V3 is reached by serial in Task 4).

- [ ] **Step 1:** Implement; rebuild on the i5.
- [ ] **Step 2:** Smoke: run headless, `telnet localhost 2323` from the i5, reach the board's login or
  setup screen.
- [ ] **Step 3:** Commit: `native: telnet server and sockets with an accept gate`.

### Task 4: The V3 radio line on a serial port

**Files:**
- Create: `src/platform/linux/modemline.h/.cpp`.
- Modify: `src/platform/linux/main_linux.cpp` (start it), `hostnet.cpp` (inject its calls).

**Interfaces:**
- `--modem /dev/ttyUSB0` (or `modem=` in `axis.conf`). Opens the tty at 115200 8N1 raw with termios,
  **DTR and RTS raised and kept raised** (so the CP2102 does not reset the V3; see the GHOSTBOARD bridge).
  Changes no modem settings.
- Behaviour is a port of `F:/cyd-3000-ghostboard/tools/lfd3k_bridge.py` (read it): on `RING` send `ATA`;
  on `CONNECT ...` create a `socketpair`, hand one end to the telnet `WiFiServer`'s accept queue as a caller
  from `127.0.0.2` (marked as radio so the gate never refuses it on rate), and pump bytes both ways; offer
  LFD3K-LINK telnet option 90 the way the bridge does (WILL 90 after AXIS negotiates; a report
  `v=1 modem=<name> kind=radio caller=- tier=SFn bps=n` from the `CONNECT` line, every 10 s), so
  `main.cpp`'s existing `acceptLink` path tags the caller `RADIO SFn`; on `NO CARRIER` close the pair; when
  AXIS hangs up, send `+++`, wait the guard time, then `ATH`.
- If the tty vanishes or errors, close any call, log `axis: modem gone`, retry opening every 5 s.

- [ ] **Step 1:** Implement; rebuild on the i5.
- [ ] **Step 2:** Commit: `native: LFD3K-SM radio line on a serial port`. (Live radio test is Task 9.)

### Task 5: Start modes and the terminal WFC

**Files:**
- Create: `src/platform/linux/startmode.h/.cpp`.
- Modify: `main_linux.cpp`, the `HostSerial` in `arduino_core.cpp`, `board.cpp`.

**Interfaces:**
- `enum class StartMode { Terminal, Window, Headless }; StartMode detectStartMode(int argc, char** argv);`
  Flags win (`--terminal`, `--window`, `--headless`); else Terminal when `isatty(0) && isatty(1)`; else
  Window when `AXIS_WINDOW` is built in and `DISPLAY` or `WAYLAND_DISPLAY` is set; else Headless.
- Terminal: stdin raw (termios, restored by an `atexit` and signal handler), alternate screen and hidden
  cursor on start, restored on exit. `Serial` reads stdin and writes stdout. At start the program feeds
  `WFC\r` into `Serial` input once `boardCfg->setupDone`, so the board's own serial WFC opens; an `@` at the
  start of a line drops to the board's @ command shell exactly as on the board. Board log lines that would
  print while the WFC owns the screen already go quiet on the board (`logEvent` checks `serialWfc`).
- Headless: `Serial` output to stdout (journal), input ignored.
- Window: Task 6. Until then `--window` falls back to Headless with a log line.
- Signals: SIGINT and SIGTERM set a flag; the main loop finishes the current `loop()` turn, lets the board's
  normal hang-up and save paths run for each open caller (call the same functions `main.cpp` uses on
  `ESP.restart()` paths if they save; otherwise just close sockets, since the board saves users on change),
  restores the terminal, and exits 0. SIGPIPE ignored.

- [ ] **Step 1:** Implement; rebuild on the i5.
- [ ] **Step 2:** Commit: `native: start modes and the terminal WFC`.

### Task 6: The desktop window

**Files:**
- Modify: `src/platform/linux/board.cpp`, `screen.cpp`, `main_linux.cpp`, `CMakeLists.pc.txt`.

**Interfaces:**
- With `AXIS_WINDOW` ON: `tft` is an `lgfx::LGFX_Device` with `lgfx::Panel_sdl` (320x240, the CYD
  rotation), window title `AXIS - <BBS name>`, scale 3 (`--scale N`, 1 to 6). Follow LovyanGFX's SDL
  usage: the SDL event loop runs on the main thread (`lgfx::Panel_sdl::main(fn)` or its setup/loop
  pair), and AXIS's `setup()`/`loop()` run in the function it is given. `boardIsHeadless()` false,
  `boardHasPanel()` true, `boardTouch()` reads the panel's touch (mouse). F11 toggles full screen if the
  panel supports it; closing the window is the same as SIGTERM.
- Prerequisite: Rev has run `sudo apt install libsdl2-dev`. If `find_package(SDL2)` fails, report it and
  stop this task (do not install anything).

- [ ] **Step 1:** Implement; rebuild with `-DAXIS_WINDOW=ON`, and once more with OFF (must still build).
- [ ] **Step 2:** Commit: `native: desktop window through LovyanGFX's SDL panel`.

### Task 7: Service and install

**Files:**
- Create: `installer/linux/axis.service`, `installer/linux/install.sh`, `installer/linux/README.md`.

**Interfaces:**
- `install.sh` (run by Rev with sudo): creates system user `axis` (no login shell), `/var/lib/axis`
  (0700, owned by `axis`), copies the `axis` binary to `/usr/local/bin/axis`, installs and enables the
  unit, adds `axis` to group `dialout` (for the V3). Takes `--modem /dev/ttyUSB0` and `--seed DIR`
  (writes `modem=` to `axis.conf`, copies the seed). Idempotent; prints what it did.
- `axis.service`: `User=axis`, `ExecStart=/usr/local/bin/axis --headless --data /var/lib/axis`,
  `Restart=on-failure` (and on exit code 3, the `ESP.restart()` stand-in), `NoNewPrivileges=yes`,
  `ProtectSystem=strict`, `ProtectHome=yes`, `ReadWritePaths=/var/lib/axis`, `PrivateTmp=yes`,
  `CapabilityBoundingSet=`, `RestrictAddressFamilies=AF_INET AF_INET6 AF_UNIX`,
  `DeviceAllow=/dev/ttyUSB0 rw` with `PrivateDevices=no` limited by `DevicePolicy=closed`
  plus `DeviceAllow=char-ttyUSB rw`, `LimitNOFILE=4096`, `MemoryMax=512M`.
- README: how to stop the old bridge (`~/emu/v3bridge.py`, via a script file) before the service takes the
  V3; how to watch it (`journalctl -u axis -f`); how to run it in a terminal instead.

- [ ] **Step 1:** Write the files; `systemd-analyze verify installer/linux/axis.service` on the i5 shows no
  errors (no sudo needed for verify).
- [ ] **Step 2:** Commit: `native: systemd service and installer`.

### Task 8: i5 hardening script

**Files:**
- Create: `installer/linux/harden-i5.sh`, section in `installer/linux/README.md`.

**Interfaces:**
- Run by Rev with sudo; prints every change; `--dry-run` prints without changing. Never locks Rev out:
  - **ufw:** default deny incoming, allow outgoing; allow SSH (22) and RDP (3389) **from the LAN subnet
    only** (detected from the default route's interface, printed for Rev to confirm with a y/N prompt);
    allow the LAN BBS port 2323 and the panel port 2326 from the LAN; allow the public BBS port from
    anywhere only if `public_port` is set in `/var/lib/axis/axis.conf`; allow UDP 2325 from the LAN (plan 2).
  - **SSH:** key-only (`PasswordAuthentication no`, `KbdInteractiveAuthentication no`,
    `PermitRootLogin no`) **only if** the invoking user's `~/.ssh/authorized_keys` has at least one key;
    otherwise skip with a message. Validate with `sshd -t` before reloading.
  - **fail2ban:** install if missing and enable the `sshd` jail (bantime 1h, maxretry 5).
  - **Listening services:** print `ss -tulpn` so Rev sees what else listens.
  - **Updates:** enable `unattended-upgrades` for security updates.
- The script is not run by Claude.

- [ ] **Step 1:** Write it; `bash -n` and `shellcheck` (if present on the i5) clean; a `--dry-run` as revj
  without sudo prints the plan (it may stop early where root is needed; that's fine).
- [ ] **Step 2:** Commit: `native: i5 hardening script`.

### Task 9: Testing (all of it, at the end)

Run on the i5. Capture a PNG screenshot for each visible step (window over RDP, terminal via a screenshot
of the session or `@SHOT`-style capture) and send them to Rev, per his standing request.

- [ ] **Check 1: existing tests.** The host test suite (`axis_tests`) passes unchanged.
- [ ] **Check 2: board unchanged.** `py F:/axis-bbs/local/i5build.py cyd` on this branch builds, and the
  image size equals main's (proves `main.cpp` and the core were not changed for the board).
- [ ] **Check 3: start modes.** Over ssh with a TTY: terminal WFC appears, `@` drops to the @ shell,
  `WFC` returns, Ctrl-C restores the terminal. `ssh ... axis < /dev/null > log` runs headless. Over RDP,
  double-click (or run from a desktop launcher): the window opens with the CYD screens; clicks act as touch.
- [ ] **Check 4: silent caller.** Open a TCP connection to 2323 and send nothing; it is dropped by the
  existing timeouts, and a second telnet caller logs in meanwhile without delay.
- [ ] **Check 5: slow reader.** One caller reads at about 300 bytes/s (a small Python reader on the PC or
  i5); a second caller's menus stay responsive (no loop stall over 1 s in the log's `slow turn:` lines).
- [ ] **Check 6: flood.** 500 connection attempts in 5 s from one IP: the gate refuses past the limits and
  bans the IP; a caller from another address still logs in. One small automated test for `NetGate`
  (limits and ban expiry with a fake clock) goes in `tests/test_netgate.cpp` and runs with `axis_tests`.
- [ ] **Check 7: radio call.** Stop the old bridge (script file), start `axis --modem /dev/ttyUSB0`, dial
  `AXISMODEM-E4B0` from the V4 on COM11 here; the call connects, the WFC shows `RADIO SFn`, a short menu walk
  works, hang up from both ends once. Keep it to one or two calls.
- [ ] **Check 8: modem unplug.** Ask Rev to unplug the V3 during a call (or simulate by closing the tty
  from a script): the caller is hung up, the program keeps running, and the line comes back when replugged.
- [ ] **Check 9: shutdown.** SIGTERM during a call: the program exits 0 within 5 s, the terminal (if any)
  is sane, and users and messages created during the run are present on the next start.
- [ ] **Check 10: service.** After Rev runs `install.sh`, `systemctl status axis` is active; a telnet call
  works; `journalctl -u axis` shows the boot log.
- [ ] **Brain:** update `D:\brain\Projects` (the AXIS note and a new "AXIS Native" note linked from it,
  plus the GHOSTBOARD note: emulator stopped 2026-11-17) and `D:\brain\Reference\i5 Ubuntu Box.md`
  (the service, ports, data folder).
- [ ] **Commit** any test files and the brain is outside the repo. Report results to Rev with the
  screenshots.
