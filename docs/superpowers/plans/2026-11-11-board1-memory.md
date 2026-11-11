# Board 1 Memory Headroom Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Raise board 1's idle free heap from about 91 KB to 125 KB or more and keep callers from ever crashing it, with every feature kept.

**Architecture:** Measure first (a windowed heap low, scoped peak logging, stack marks), then take one baseline on the boards. Next remove the per-screen and per-feature spikes: read files at their real size, stream output in 2 KB pieces, put a memory gate in front of TLS, lend one ZIP window across the board, stream list and user-file writes. Then raise the idle floor: draw the touchscreen WFC in 320x40 strips with the full sprite only on demand, size AnsiTerm to its screen, apply sdkconfig trims, and share one settings copy. A batched hardware test at the end proves it.

**Tech Stack:** C++17, Arduino-ESP32 2.0.17 on ESP-IDF 4.4.7 via PlatformIO (env `cyd`, `headless`), LovyanGFX 1.3.0, LittleFS, host tests with doctest via CMake/Ninja (`build/axis_tests`), Python 3 for the bench script.

**Spec:** `docs/superpowers/specs/2026-11-11-board1-memory-design.md` (approved by Rev 2026-11-11). Read it first. Each task names the GitHub issue that holds the detailed where and what (`gh issue view <n>`).

## Global Constraints

- Every feature stays and behaves as today. Caller output is byte for byte the same, except for two allowed changes: a brief sweep while the touchscreen WFC redraws, and short new notes where the board now declines something for lack of memory.
- LORD picks the same monster for the same random number.
- An older follower still joins, gets screens and logs callers in. The protocol only gains optional fields and new request letters. A new letter must be added to the list comment above `cluster->onRequest` in `src/main.cpp`. Free letters: `E` `G` `I`.
- No TLS for anything that carries a secret is ever downgraded to plain HTTP.
- Flash is 86.9% full (app max 0x240000 = 2,359,296 bytes). No task may add more than about 4 KB of flash.
- Build: `export PLATFORMIO_CORE_DIR='F:\CodexStorage\Tools\platformio'; pio run -e cyd` (and `-e headless`). Host: `cmake -S . -B build -G Ninja && cmake --build build && ./build/axis_tests`.
- `sdkconfig.cyd` is the file that counts. Edit it and delete `.pio/build/cyd` before building.
- Rev's style: each build task ends with a successful `pio run -e cyd`, a host build that compiles, and a commit. All tests are written and run in the last task. The one exception is Task 3, the early measuring session Rev OK'd.
- Commits: short conventional messages (`fix: ...`, `feat: ...`). No Co-Authored-By, no AI attribution. `git add` named files only. Never add Rev's untracked files (churn.txt, crash.txt, live.txt, ring_live.txt, shots/, the hookup plan files, *.log).
- Never print the cluster key, the public hostname or any password. Never touch COM15, COM9 or COM11. Flash board 1 (MAC `8c:94:df:4f:1c:4c`) and board 2 (MAC `8c:94:df:4f:14:18`) only after reading the MAC on that port.
- No em dashes or double hyphens in prose written for Rev (reports, notes, commit bodies).
- Work on branch `memory/board1` off `main`. Do not push until Rev says so.

## Review Focus

1. **A board whose largest block never reaches the TLS gate (`kTlsNeeds` + 32 KB = 80 KB).** DNS and No-IP would silently never update. Expected: an event-log line `ddns: waiting for memory (largest N KB)` once per hour while gated, and the update runs as soon as it fits. Test in Task 18: `web gate logs once an hour while gated`.
2. **Output spilled through the 2 KB sink by a node that has no line** (the serial WFC, the local node, the panel console). Expected: those nodes keep everything in `out_` for `takeOutput()` exactly as today, and nothing is lost or reordered. Test in Task 18: `node without a sink keeps all output for takeOutput`.
3. **A second caller in the ANSI viewer while an upload check holds the shared ZIP window.** Expected: the viewer shows the pack's file list with the one-line note, and the upload check is not disturbed. Test in Task 18: `inflate lease refuses a second owner and frees on give`.
4. **The touchscreen leaving the WFC when the full sprite cannot be allocated** (fragmented heap). Expected: the screen draws straight to the panel and still responds to touch. No crash and no blank screen. Bench check in Task 18 (force the failure with `@SPRITEFAIL` once).
5. **A list file trimmed while a follower appends to it at the same time** (rumors and ballots arrive through request `K`/`P` on the main loop). Expected: no lost or doubled line, and the count stays within cap + cap/4. Test in Task 18: `list append then trim keeps the newest cap lines in order`.

---

## File map

| File | Responsibility | Tasks |
|---|---|---|
| `src/bbs/heapwin.h/.cpp` (new) | portable windowed heap low, testable on the PC | 1 |
| `src/platform/cyd/heapscope.h/.cpp` (new) | `HeapScope` logs when the lifetime minimum moves inside a scope | 1 |
| `src/bbs/diag.h/.cpp` | new `@STATUS` fields | 1 |
| `tools/busytest.py` (new) | the busy test driver and recorder | 2 |
| `docs/superpowers/plans/2026-11-11-board1-memory-report.md` (new) | baseline, 22 KB breakdown, final table | 3, 18 |
| `src/platform/cyd/web.cpp/.h` | TLS gate, no plain No-IP | 4 |
| `src/main.cpp` | file reads, `T` reply, exists answer, lease, lists, users save, WFC strips, art caches | many |
| `src/bbs/node.h/.cpp` | output sink, login cells, prelogon exists, WFC art provider, shared config | 6, 7, 8, 13, 16 |
| `src/lord/game.cpp` | seek-read one monster | 10 |
| `src/bbs/inflatelease.h/.cpp` (new) | one board-wide inflate window | 11 |
| `src/bbs/ansiviewer.cpp` | uses the lease, plain list with a note | 11 |
| `src/bbs/users.h/.cpp`, `src/platform/cyd/config_store.h/.cpp` | streamed users.dat write | 12 |
| `src/bbs/ansiterm.h/.cpp` | cells sized to the screen | 14 |
| `src/platform/cyd/screen.h/.cpp`, `src/platform/cyd/ansi_screen.h/.cpp` | strips, sprite on demand, headless skip | 15 |
| `sdkconfig.cyd`, `sdkconfig.headless-ring` | FATFS cache, stacks, TCP MSL | 17 |
| `tests/test_memory.cpp` (new) and existing test files | every test, Task 18 | 18 |

---

## Phase 1: measure

### Task 1: Heap instruments (#24)

**Files:**
- Create: `src/bbs/heapwin.h`, `src/bbs/heapwin.cpp`, `src/platform/cyd/heapscope.h`, `src/platform/cyd/heapscope.cpp`
- Modify: `src/bbs/diag.h`, `src/bbs/diag.cpp`, `src/main.cpp` (`@STATUS` around 3254, top of `loop()` around 4002, new `@HEAPWIN`), `src/platform/cyd/web.h/.cpp` (busy flag), `CMakeLists.pc.txt` (add heapwin.cpp to the host build)

**Interfaces:**
- Produces:
  - `struct axis::bbs::HeapWindow { uint32_t lowFree = UINT32_MAX, lowLargest = UINT32_MAX; std::string lowDoing; void sample(uint32_t freeB, uint32_t largest, const char* doing); void reset(); }`. `sample` keeps the lowest free and, separately, the lowest largest block. `lowDoing` is the `doing` at the moment `lowFree` was set.
  - `class HeapScope { public: explicit HeapScope(const char* name); ~HeapScope(); }`. At exit, if `heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT)` dropped inside the scope, it calls the hook with `heap low in <name>: min A->B, largest X, web=<0|1>`.
  - `void heapScopeHook(void (*log)(const std::string&))`. main.cpp sets it to `logEvent`.
  - `bool web::busy()`, true while the web task holds a request (set around `request()` in web.cpp).
  - `size_t axis::bbs::Node::outMax()` (static, the largest `out_` capacity seen) and a static `artMax` in main.cpp (largest art string read by `readThemeFile`/`menuFileLocal`).
  - `DiagSample` gains `winFree, winLargest, stkWeb, stkMdns, stkTimer, outMax, artMax` (uint32_t) and `std::string winDoing`. `diagFields` appends ` wmin=<winFree> wlarge=<winLargest> wdoing=<token> stk_web=<n> stk_mdns=<n> stk_timer=<n> outmax=<n> artmax=<n>`. `winDoing` becomes one token: spaces turn into `_`, cut to 24 chars.

- [ ] **Step 1:** Write `HeapWindow` and the `diagFields` additions.
- [ ] **Step 2:** Write `HeapScope` and wrap these sites in it: `drawWfc`, `Node::wfcDraw` callers in main, `showLoginScreen` and `showPrelogon` call sites (wrap `node->input`/`node->poll`, labelled `node`), the `T` and `U` request handlers, `NativeLord::loadMonster` call path (wrap the door poll, label `door`), `saveUsers`, `ddnsCheck` and the Discord post in web.cpp.
- [ ] **Step 3:** At the top of `loop()`, call `heapWin.sample(free, largest, doingNow)` once per turn. Use the same text `noteDoing` keeps; expose it as `static const char* doingNow()`.
- [ ] **Step 4:** Stack marks: keep the web task handle (already `task_`) and look up `mdns` and `esp_timer` with `xTaskGetHandle`. Fill the `stk_*` fields with `uxTaskGetStackHighWaterMark` (words), or 0 when a task is absent.
- [ ] **Step 5:** `@HEAPWIN` resets the window and prints `@HEAPWIN reset`. It is a board-only command (add it to the board-only list beside `@STATS`).
- [ ] **Step 6:** Build: `pio run -e cyd` succeeds, and so does `cmake --build build`. Note the flash bytes in the commit body.
- [ ] **Step 7:** Commit: `git add` the files above; `git commit -m "feat: heap window, scoped heap lows and stack marks in @STATUS"`.

### Task 2: Busy test script

**Files:**
- Create: `tools/busytest.py`

**Interfaces:**
- Consumes: `@STATUS` fields from Task 1, `@HEAPWIN`. Test callers from `local/test-users.txt` (`handle|location|password`; never print or pass passwords on a command line, the same as `tools/seven_login.py`).
- Produces: `python -I tools/busytest.py --main 192.168.1.164 --follower 192.168.1.165 --port COM20 --minutes 15 --out <csv>`. It writes one CSV row every 30 s (`t,free,largest,wmin,wlarge,wdoing,resets`) and prints a summary line `PASS` or `FAIL` with the lowest numbers seen.

- [ ] **Step 1:** Callers, each a thread with a telnet socket, logged in the way `seven_login.py` does it:
  - A, on the main: Doors > LORD, forest fights in a loop (`F` then attack keys until the fight ends), and back to town.
  - B, on the main: Doors > ANSI viewer, opens each pack in the ANSI Packs area in turn and plays a piece.
  - C, on the main: walks the message areas, then the file areas, and downloads one file with ZMODEM. Use `sz`/`rz` from `tools/xfertest` if present; otherwise receive the ZMODEM stream raw and discard it.
  - D, on the follower: moves between menus so the main serves theme screens through `T`.
- [ ] **Step 2:** Events at fixed times: at 3 minutes, reset board 2 (write `@RESTART force` on its serial port, given by `--follower-port`) so it rejoins. At 5 minutes, force a DNS check (`@DDNS now` on the main's serial port; add that board-only command in this task if it does not exist: it sets the web task's timer due).
- [ ] **Step 3:** Poll `@STATUS` on `--port` every 30 s with DTR and RTS held low, the way `tools/sdput.py` opens the port. Detect a reset by `up=` going down. Fail if: a reset happens, a line contains `ALLOC FAILED`, `wmin` < 61440, or `wlarge` < 32768.
- [ ] **Step 4:** `python -I tools/busytest.py --help` runs. Commit: `git commit -m "feat: busy test for board memory"`.

### Task 3: Early measuring session (Rev's OK, before any fix)

**Files:**
- Create: `docs/superpowers/plans/2026-11-11-board1-memory-report.md` (numbers only: no hostnames, keys or passwords)

- [ ] **Step 1:** Back up board 1's flash first (`esptool read_flash 0 0x400000 local/backups/board1-COM20-<date>-pre-memory.bin`). Read the MAC on each port. Flash Task 1's build to board 1 and board 2: `pio run -e cyd -t upload --upload-port <port>`.
- [ ] **Step 2:** Idle reading: 15 minutes after boot, with at least one DNS cycle past, record `@STATUS` on both boards. Send `@SHOT` of board 1's WFC and keep the PNG in `shots/memory/` (local, not committed).
- [ ] **Step 3:** Run the busy test (Task 2) on the baseline and save the CSV next to the report.
- [ ] **Step 4:** The 22 KB breakdown. On board 1, sample `@STATUS` before and after toggling each suspect at runtime:
  - SD: unmount if a command exists. Otherwise compare against board 2 with `CONFIG_FATFS_PER_FILE_CACHE`, and count 10 open-file slots x 512 bytes.
  - User list in RAM: count the users times the average `formatUser` size.
  - `lastCallers`, `artCache`, `menuCache`, stats.
  - The web task stack: 7168 bytes, which exists only where No-IP, DNS or Discord is set, so check whether board 2 runs it.

  Write a table in the report: item, bytes, how measured.
- [ ] **Step 5:** Write the stack marks (web, mdns, esp_timer) into the report. Those numbers decide Task 4 step 4 and Task 17 step 2.
- [ ] **Step 6:** Any item in the 22 KB table that is over 4 KB and has a simple fix becomes a new task in Phase 3, before Task 18, written in this plan's format. Show it to Rev before building it. Commit the report: `git commit -m "docs: board 1 memory baseline"`.

## Phase 2: stop the spikes

### Task 4: TLS gate, no plain No-IP (#8)

**Files:** Modify: `src/platform/cyd/web.cpp`, `src/platform/cyd/web.h`, `src/main.cpp` (log the drop counter)

**Interfaces:**
- Produces: `constexpr size_t kTlsReserve = 32 * 1024;`. `tlsFits()` returns `!door_ && largest >= kTlsNeeds + kTlsReserve`. `uint32_t web::droppedLines()` and `uint32_t web::gatedChecks()` (atomics).

- [ ] **Step 1:** `publicIp()` always uses `http://ip1.dynupdate.no-ip.com/`.
- [ ] **Step 2:** `noipCheck()`: remove the `https`→`http` replace. If `!tlsFits()`, set `noipResult_ = "noip: waiting for memory"`, increment `gatedChecks`, and return without touching `noipIp_`, so it retries next cycle. GoDaddy gets the same gate before `request`.
- [ ] **Step 3:** Discord: when the line is dropped because TLS does not fit (not because of a door), increment `droppedLines`. In main's loop, when `droppedLines` or `gatedChecks` changed, log at most once an hour: `discord: N lines dropped (low memory)` / `ddns: waiting for memory (largest N KB)`.
  Note: idle largest is about 69 KB until Phase 3, so on this branch DNS and No-IP wait until Task 15 lands. Expected, and the hourly log line shows it. The branch is not flashed between Task 3 and Task 18.
- [ ] **Step 4:** Only if Task 3's web stack mark shows more than 2 KB free: lower the stack 7168 → (7168 − free + 1024), rounded to 512. Otherwise leave it.
- [ ] **Step 5:** Build cyd and the host build. Commit: `fix: TLS waits for memory, No-IP never falls back to plain HTTP`.

### Task 5: Read files at their size (#10)

**Files:** Modify: `src/main.cpp` (`readThemeFile` around 308, `menuFileLocal` around 447, `themeArtRemote` around 398)

**Interfaces:**
- Produces: `static bool readThemeFileAppend(const char* folder, const std::string& name, size_t cap, size_t offset, std::string& out)`. It appends at most `min(fileSize - offset, cap)` bytes to `out`, cut at the first 0x1a, and returns whether the file opened. `readThemeFile(...)` keeps its signature and calls it with a fresh string.

- [ ] **Step 1:** Get the size with `fseek(f,0,SEEK_END)`/`ftell`, then `out.resize(old + n)` and a single `fread`, then cut at 0x1a. Do the same in `menuFileLocal`.
- [ ] **Step 2:** `themeArtRemote`: `all.reserve(cap)` for non-poster names before the loop.
- [ ] **Step 3:** Update `artMax` here (from Task 1).
- [ ] **Step 4:** Build. Commit: `fix: read art at its size instead of doubling`.

### Task 6: Stream caller output in 2 KB pieces (#11)

**Files:** Modify: `src/bbs/node.h`, `src/bbs/node.cpp` (`paintScreen` 431, `showLoginScreen` 464, `wfcDraw` 1317, `takeOutput` 3493), `src/bbs/menurunner.cpp` (emit around 70 to 84), `src/main.cpp` (`setDirect` around 2102 and every other place a caller Node is made)

**Interfaces:**
- Consumes: `Node::setDirect(std::function<void(const char*, size_t)>)` (exists).
- Produces: `static constexpr size_t Node::kOutSpill = 2048;` and a private `void Node::spill()`. When `direct_` is set and `out_.size() >= kOutSpill`, it sends `out_` through `direct_`, then `out_.clear()` (capacity kept). Call it from every emit lambda that appends art (paintScreen, showLoginScreen, wfcDraw, MenuRunner's emit through a callback Node gives it), so bytes always leave in order.

- [ ] **Step 1:** Add `spill()` and call it in those emit lambdas.
- [ ] **Step 2:** In main.cpp, every Node with a caller line gets `setDirect`. Capture that session's own line pointer, not the global `line`. Nodes without a line (serial WFC, local node, panel console) get none, so they keep the old behavior.
- [ ] **Step 3:** Update `outMax` in `takeOutput` and `spill`.
- [ ] **Step 4:** Build. Commit: `fix: caller screens leave in 2 KB pieces`.

### Task 7: Login screen without the whole expansion (#12)

**Files:** Modify: `src/bbs/node.cpp` (`showLoginScreen` 464), `src/bbs/ansiterm.h/.cpp` (a `feed(const char*, size_t)` overload if absent)

**Interfaces:**
- Produces: a file-static cache in node.cpp, `struct LoginCells { size_t key; int at[2][2]; }`, holding at most 4 entries. The key is `std::hash<std::string>{}(art)` of the raw LOGIN.ANS.

- [ ] **Step 1:** On a cache hit, set `loginAt_` from the cache and skip the AnsiTerm entirely.
- [ ] **Step 2:** On a miss, make the AnsiTerm (80x25) and feed it from the `expandArt` emit callback in pieces. No `drawn` string. Scan, store, free.
- [ ] **Step 3:** Build. Commit: `fix: login screen scans its fields once and without a copy`.

### Task 8: Pre-logon checks without reading files (#13)

**Files:** Modify: `src/bbs/node.h/.cpp` (`showPrelogon` 449), `src/main.cpp` (exists provider, request `E`)

**Interfaces:**
- Produces: `void Node::setExists(std::function<bool(const std::string&)> exists)`. With it set, `showPrelogon` probes with `exists_`. Without it, it uses today's `menuLoad_` probe.
- Request `E\x1f<name>` → `"1"` or `"0"`. The main resolves a name the same way `menuFile` does: the theme screen through `openThemeScreen`, else `/menus/<name>`, using `stat`/`fopen` with no read. A follower caches the answers in `std::map<std::string,bool> existsCache`, cleared wherever `artCache.clear()` runs. If the request is not answered (an older main), the follower falls back to `menuLoad_(name)` non-empty.

- [ ] **Step 1:** Add `setExists` and use it in `showPrelogon`.
- [ ] **Step 2:** Main side: the `E` handler, and add `E` to the letters comment.
- [ ] **Step 3:** Build. Commit: `fix: pre-logon screens are checked, not read`.

### Task 9: Main serves follower screens without copies (#14)

**Files:** Modify: `src/main.cpp` (`T` handler around 2575, `themeArtRemote`), `src/cluster/cluster.cpp` (response into the frame around 583)

**Interfaces:**
- Consumes: `readThemeFileAppend` from Task 5.
- Produces: the `T` reply is built as `std::string r; r.reserve(1 + want); r = '1'; readThemeFileAppend(..., r);`. The response string is moved into the frame (change the response path to take `std::string&&`, no copy). Followers ask with `want = 4096`, and `themeArtRemote` ends a file when `chunk.size() < want`.

- [ ] **Step 1:** Make the changes above. Posters keep `kPosterChunk` and the existing `want` handling.
- [ ] **Step 2:** Build. Commit: `fix: main serves follower screens in place`.

### Task 10: LORD reads one monster (#9)

**Files:** Modify: `src/lord/game.cpp` (`loadMonster` 359)

- [ ] **Step 1:** `fs_.stat("LENEMY.DAT", fi)` gives `n = fi.size / kMon` (fail if `n < 1`). Pick `idx` with the same `roll()` calls in the same order as today. Then `open`, `seek(h, (int64_t)idx * kMon, 0)`, `read` 239 bytes into `uint8_t rec[kMon]`, `close`, and parse from `rec` exactly as now.
- [ ] **Step 2:** Build. Commit: `fix: LORD reads one monster record, not the whole file`.

### Task 11: One board-wide ZIP window (#20)

**Files:** Create: `src/bbs/inflatelease.h`, `src/bbs/inflatelease.cpp`. Modify: `src/bbs/ansiviewer.h/.cpp` (`grabInflate` around 55), `src/main.cpp` (`ZipBuf zipRam` around 640, upload check around 3917), `CMakeLists.pc.txt`

**Interfaces:**
- Produces: `namespace axis::bbs::inflate { bool take(const void* owner, uint8_t*& window, uint8_t*& state); void give(const void* owner); bool busy(); constexpr size_t kWindow = 32768; }`.
  - The first `take` mallocs the window and `ZipReader::kStateBytes`. A `take` by another owner while it is held returns false.
  - `give` frees both, so nothing is held at idle. A `give` by a non-owner does nothing.
- `AnsiViewer::grabInflate` uses `inflate::take(this, ...)`, and its destructor or close calls `give(this)`. When `take` fails because another caller holds the window, the viewer shows the pack's plain file list and this exact line: `Another caller is viewing a pack right now. Here is the file list.` When it fails for lack of memory, it keeps today's message.
- The upload check replaces `zipRam` with the lease (owner = the TransferJob).

- [ ] **Step 1:** Make the changes above.
- [ ] **Step 2:** Build. Commit: `fix: one ZIP window for the board, lent to one caller at a time`.

### Task 12: Lists and users.dat written without whole copies (#21, #18)

**Files:** Modify: `src/main.cpp` (`listAddLocal` 260, `listStore`, `saveUsers` 933, the `/users.dat` write at 2552), `src/bbs/users.h/.cpp`, `src/platform/cyd/config_store.h/.cpp`

**Interfaces:**
- Produces:
  - `void UserStore::formatTo(const std::function<void(const std::string&)>& put) const` emits one user block per call. `format()` is rewritten on top of it, so it gives the same bytes.
  - `bool flashWriteStream(const char* path, const std::function<bool(File&)>& fill)` writes `/save.tmp`, then remove and rename, the same safety as `flashWrite`.
  - `static bool listAppendLocal(const std::string& n, const std::string& line, size_t cap)` for the plain lists (rumors, ballots, bbslist and the rest; axisnet and netnotes keep their merge path). It opens `/list_<n>.txt` with `"a"`, writes `line.substr(0,120)+'\n'`, and bumps `listCount[n]` (counted lazily on first use, line by line). When the count passes `cap + cap/4`, it streams the newest `cap` lines into `/list_<n>.tmp` and renames the file. The cap values stay as today (ballots 500, rumors 300, others 50).

- [ ] **Step 1:** `formatTo` and `flashWriteStream`; `saveUsers` and the 2552 site use them.
- [ ] **Step 2:** `listAppendLocal` replaces the vector path for plain lists.
- [ ] **Step 3:** Build. Commit: `fix: stream users.dat and list writes`.

### Task 13: WFC art read once, follower art cache capped (#15, #16)

**Files:** Modify: `src/main.cpp` (`artCache` 303, `themeArt` 418), `src/bbs/node.h/.cpp` (`wfcDraw` 1317)

**Interfaces:**
- Produces:
  - `void Node::setWfcArt(std::function<std::shared_ptr<const std::string>()> art)`. `wfcDraw` uses it when it is set (the cleaned WFC.ANS, or null for the built-in). The refresh path writes `ESC[K` before each CR LF from inside the emit callback instead of copying the art.
  - In main: one `static std::shared_ptr<const std::string> wfcAnsShared` plus its folder key, reloaded only when the WFC folder changes or the settings arrive (where `artCache.clear()` runs).
  - `artCache` keeps a total byte count and a use order. It holds at most 16384 bytes in total; the least recently used entry is evicted first. A single entry larger than 16384 is not cached.

- [ ] **Step 1:** Make the changes above.
- [ ] **Step 2:** Build. Commit: `fix: WFC art read once, follower art cache capped at 16 KB`.

## Phase 3: raise the idle floor

### Task 14: AnsiTerm sized to its screen (#17)

**Files:** Modify: `src/bbs/ansiterm.h/.cpp`, `src/main.cpp` (`drawWfc` 1121, built-in art at 1140)

**Interfaces:**
- Produces: `AnsiTerm` keeps `std::vector<Cell> cells_` of `cols*rows`, replacing `Cell cells_[kMaxRows][kMaxCols]`. `cell(r,c)` returns `cells_[r*cols_+c]`. The constructor clamps as today. `kMaxCols`/`kMaxRows` stay as limits.
- `drawWfc` reads the built-in art from flash without copying it into `wfcArt`: keep `const char* wfcBuiltin` and pass a `std::string_view` (or `const char*`, len) to `axis::bbs::drawWfc`. Add that overload if it only takes `std::string`.
- Anything else #17 lists (the snapshot copied per redraw) is removed if it is still there.

- [ ] **Step 1:** Make the changes above.
- [ ] **Step 2:** Build. Commit: `fix: AnsiTerm holds only its own cells`.

### Task 15: WFC in strips, full sprite on demand, headless skip (#22 and follow-up)

**Files:** Modify: `src/platform/cyd/screen.h/.cpp`, `src/platform/cyd/ansi_screen.h/.cpp`, `src/main.cpp` (`drawWfc`, `screenInit` call around 3764, every non-WFC screen that draws into `scr`, `@SHOT`)

**Interfaces:**
- Produces in screen.h:
  - `bool screenFull()`: creates the 320x240 4-bit sprite plus palette if it does not exist, and returns false if that allocation fails.
  - `void screenFullRelease()`: deletes it.
  - `LovyanGFX& screenGfx()`: the full sprite when it exists, else `tft` (draw straight to the panel).
  - `void screenPushIfSprite()`: pushes only when drawing went into the sprite.
  - `bool screenBandsInit()`: a 320x40 4-bit band sprite plus palette, made once, about 6.4 KB.
- In ansi_screen.h: `static void renderCellsBand(LGFX_Sprite& band, const axis::bbs::AnsiTerm& t, int firstRow, int rows);` (7x10 cells, so a 40 px band is 4 rows).
- `drawWfc` renders the term band by band: for `y = 0..23 step 4`, `renderCellsBand(band, *t, y, 4)` then `band.pushSprite(0, y*10)`. Every non-WFC panel screen calls `screenFull()` first and draws into `screenGfx()`. Mechanically replace `scr.` with a local `auto& g = screenGfx();`. The return to the WFC calls `screenFullRelease()`.
- `@SHOT` at the WFC rebuilds the term through a factored `static std::unique_ptr<AnsiTerm> wfcTerm()` (the part of `drawWfc` before render) and prints rows band by band in the same `@SHOT 320 240` format. On other screens it uses the full sprite, or `@SHOT NONE` when drawing went straight to the panel.
- Headless: skip `screenInit()`/`screenBandsInit()` when `boardIsHeadless()`.
- A test hook `@SPRITEFAIL` (board-only): the next `screenFull()` returns false once.
- Remove `screenLend`/`screenReturn` (no callers).

- [ ] **Step 1:** screen and band API, `renderCellsBand`.
- [ ] **Step 2:** `drawWfc` in bands. Full sprite on demand at each non-WFC screen, released on the way back.
- [ ] **Step 3:** `@SHOT` and `@SPRITEFAIL`, headless skip.
- [ ] **Step 4:** Build cyd and headless. Commit: `feat: WFC drawn in strips, full screen sprite only when needed`.

### Task 16: One shared settings copy (#19)

**Files:** Modify: `src/bbs/node.h/.cpp`, `src/main.cpp` (Node construction and `applyBoard` callers)

**Interfaces:**
- Produces: `Node` holds `std::shared_ptr<const BbsConfig> cfg_`. The constructor takes `std::shared_ptr<const BbsConfig>` (keep a `BbsConfig` overload that wraps, for tests). `applyBoard(std::shared_ptr<const BbsConfig>)` swaps the pointer. Any place in Node that mutates config uses `BbsConfig& cfgMut()`, which copies on first write (grep `cfg_.` for assignments).
- main.cpp keeps one `std::shared_ptr<const BbsConfig> boardCfg`, replaced when settings change, and passes it to every Node.
- `myAreas_`/`myMsgAreas_` are filled only while the file browser or message reader is open, and cleared (`clear()` plus `shrink_to_fit()`) when it closes. FileBrowser and MessageReader take the vector by reference, so this gives the same saving as indexes without changing them.

- [ ] **Step 1:** Make the changes above.
- [ ] **Step 2:** Build cyd and the host build (tests construct Nodes, so keep them compiling). Commit: `fix: nodes share one settings copy`.

### Task 17: sdkconfig trims (#23)

**Files:** Modify: `sdkconfig.cyd`, `sdkconfig.headless-ring`

- [ ] **Step 1:** `# CONFIG_FATFS_PER_FILE_CACHE is not set`, and `CONFIG_LWIP_TCP_MSL=10000`.
- [ ] **Step 2:** Only if Task 3's marks show more than 1.5 KB free on that task: `CONFIG_ESP_TIMER_TASK_STACK_SIZE=2560` and `CONFIG_MDNS_TASK_STACK_SIZE=3072`.
- [ ] **Step 3:** Delete `.pio/build/cyd` and `.pio/build/headless`, build both, and confirm the values in `.pio/build/cyd/config/sdkconfig.h`. Commit: `fix: smaller FATFS, stacks and TIME_WAIT`.

(Any extra tasks from Task 3 step 6 go here.)

## Phase 4: prove it

### Task 18: All tests, flash, busy test, report

**Files:** Create: `tests/test_memory.cpp`. Modify: `CMakeLists.pc.txt` (add it), the existing test files named below, the report, `docs/FEATURES.md`, the brain note `D:\brain\Projects\AXIS BBS Firmware.md`.

- [ ] **Step 1: Write the host tests** (doctest, in `tests/test_memory.cpp` unless named):
  - `heap window keeps the lowest free and largest separately`: sample (100,80,"a"), (90,85,"b"), (95,70,"c"). Then `lowFree==90`, `lowLargest==70`, `lowDoing=="b"`. After `reset()`, `lowFree==UINT32_MAX`.
  - `diag fields add the window and stacks`: `diagFields` with `winFree=61440, winDoing="door LORD by x"` contains ` wmin=61440` and ` wdoing=door_LORD_by_x`.
  - `node with a sink sends identical bytes`: two Nodes show the same 11 KB art. One has `setDirect` collecting into a string, the other doesn't. Then `direct + takeOutput() == takeOutput()` of the other, and `outMax() <= 2*kOutSpill` for the sink node.
  - `node without a sink keeps all output for takeOutput` (Review Focus 2).
  - `login cells cached`: two logins with a LOGIN.ANS holding `|{P}` at row 10 col 20 give `loginAt_` 10,20 both times, and the second makes no AnsiTerm (count with a static counter in a test build, or assert through timing-free behavior: the menuValue hook is called once only).
  - `prelogon uses exists and reads once`: `setExists` true for PRELOGON2.ANS only, and a `menuLoad_` counter. After `showPrelogon`, the counter == 1.
  - `LORD picks the same monster`: in `tests/test_lordnative.cpp`, build a LENEMY.DAT of 30 distinct records (extend the existing `oneMonster()` helper with a name per index, the way line 661 builds `mon`). For levels 1, 2 and 3 with a fixed seed, the fighter's name, str and hp equal the record at the index the old code computes (`(level-1)*11 + roll(11)`, with the same seed).
  - `inflate lease refuses a second owner and frees on give` (Review Focus 3).
  - `viewer shows the list with the note when the window is lent`: the second AnsiViewer's output contains `Another caller is viewing a pack right now. Here is the file list.`
  - `users formatTo matches format`: in `tests/test_users.cpp`, the concatenated blocks == `format()` for a store with 3 users, answers and votes.
  - `list append then trim keeps the newest cap lines in order` (Review Focus 5): factor the trim decision into a portable `std::vector<std::string> listTrim(std::vector<std::string> lines, size_t cap)` in `src/bbs/` if main.cpp is not host-built, and test cap 4 with 7 lines appended: 4..7 remain.
  - `ansiterm 45x24 holds only its cells`: `sizeof` of the cells vector capacity == 45*24 cells, and `cell(23,44)` is writable.
  - `node shares the settings`: two Nodes from one `shared_ptr` see the same `bbsName`; after `applyBoard(next)` the old pointer's use count drops.
  - `web gate logs once an hour while gated` (Review Focus 1): factor the once-an-hour decision into `bool axis::bbs::gateLogDue(uint32_t nowMs, uint32_t& lastMs)` in `src/bbs/` and test 0 → due, 30 min → not, 61 min → due.
- [ ] **Step 2: Run the host tests.** `cmake -S . -B build -G Ninja && cmake --build build && ./build/axis_tests` shows all pass, 793 plus the new ones.
- [ ] **Step 3: Firmware builds.** `pio run -e cyd` and `pio run -e headless` succeed. Note the flash %.
- [ ] **Step 4: Flash.** Read the MACs, then flash board 1 and board 2.
- [ ] **Step 5: Idle reading** as in Task 3 step 2. Send a screenshot of `@STATUS` and `@SHOT` of the WFC to Rev (`shots/memory/`). Watch one full WFC redraw on the touchscreen and note the sweep.
- [ ] **Step 6: Busy test.** `python -I tools/busytest.py ...` gives `PASS`. Save the CSV.
- [ ] **Step 7: Review Focus 4.** Send `@SPRITEFAIL`, then tap into a node detail on the touchscreen. The screen draws and the back button returns to the WFC. Photograph or `@SHOT`.
- [ ] **Step 8: Report.** Fill in the before and after table from the spec (idle free, idle largest, busy low, busy largest, resets), plus one line per issue with its measured saving. A missed target says by how much and what `wdoing`/`HeapScope` events show.
- [ ] **Step 9: Close out.** Commit tests and report (`test: memory fixes`, `docs: board 1 memory report`). Ask Rev before pushing. After the push, close #8 to #24 that were done, each with its saving, and tick #25. Add a FEATURES.md line. Update the brain note with the new idle and peak numbers, the new commands (`@HEAPWIN`, `@DDNS now`, `@SPRITEFAIL`), request letter `E`, and that the build options menu is next.
