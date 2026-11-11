# Board 1 memory: baseline report (Task 3)

Measured 2026-11-11 on the Task 1 and 2 firmware (branch memory/board1, commit 8762fd0, flash 87.1%), before any memory fix. Board 1 (main, SD card, COM20, MAC 8c:94:df:4f:1c:4c, 192.168.1.164) and board 2 (follower, no card, COM21, MAC 8c:94:df:4f:14:18, 192.168.1.165), MACs read before flashing. Board 1 was backed up first to `local/backups/board1-COM20-2026-11-11-pre-memory.bin` (4 MB, in the main checkout, not committed). Theme crt, WFC skin cyan, unchanged.

Raw files stay local in `F:\axis-bbs-work\memory\` (not in the repo; `shots/` is not git-ignored here): `busy-baseline.csv`, `busy-baseline.log`, `busy-baseline.serial.log` (may hold the public hostname, keep it local), the single caller trials `trial*.csv`, and the idle WFC screenshot `b1-wfc-idle.png`.

All heap numbers are bytes. Stack marks are bytes left (ESP-IDF's high water mark is in bytes: the web task shows 6260 free of 7168 before its first check).

## 1. Idle

Both boards 15 minutes or more after boot, no callers, the first DNS check (60 s after boot) past.

Board 1, up 1123 s:

```
id=8C:94:DF:4F:1C:4C role=main nodes=2 on=0 heap=91468 largest=81920 up=1123 rssi=-48 sd_mb=14903 ip=192.168.1.164 min=53580 stack=3220 door_peak=0 door=none door_stack=0 reset=poweron wmin=59252 wlarge=45056 wdoing=idle stk_web=1108 stk_mdns=2260 stk_timer=3392 outmax=0 artmax=0
```

Board 2, up 952 s:

```
id=8C:94:DF:4F:14:18 role=follower nodes=2 on=0 heap=113452 largest=106496 up=952 rssi=-52 sd_mb=0 ip=192.168.1.165 min=82060 stack=2944 door_peak=0 door=none door_stack=0 reset=poweron wmin=106976 wlarge=94208 wdoing=idle stk_web=0 stk_mdns=2172 stk_timer=3392 outmax=0 artmax=0
```

| | board 1 | board 2 |
|---|---|---|
| free | 91468 | 113452 |
| largest block | 81920 | 106496 |
| lowest since boot (min) | 53580 | 82060 |
| windowed low (wmin / wlarge) | 59252 / 45056 | 106976 / 94208 |

Board 1's idle low comes from its own boot DNS check (HTTPS, about 60 s after boot), with no caller on: the board's log says `heap low in web: min 58268->57552` after the flash, and after the last boot of the day `heap low in web: min 76132->57136, largest 55296`. So board 1's `min` sits at 53 to 59 KB from boot onward, before anyone calls. A second idle reading after a clean restart (up 171 s, before the busy test) gave free 91452, largest 81920, min 59084.

**Largest block after callers.** Every reading with no caller on, taken right after callers hung up, shows free about 91 KB but largest only 55296 (6 readings: after the LORD, viewer, messages and LORD plus follower trials). Only fresh or settled boots show 77824 to 86016 (and 81920 again at up 570 after the last boot). Task 4's TLS gate wants about 80 KB largest, so right after a busy spell it will usually wait.

## 2. Busy test

`tools/busytest.py`, all four callers, 15 minutes, the follower reset at 3 minutes and `@DDNS now` at 5 minutes, board 1 restarted 3 minutes before the run:

```
python -I tools/busytest.py --main 192.168.1.164 --follower 192.168.1.165 --port COM20 --follower-port COM21 --minutes 15 --users <test users file> --out busy-baseline.csv
```

**Result: FAIL. Board 1 panicked 5 times in 15 minutes (the run stopped at the fifth, at 895 s). The first panic came 77 s after the first caller dialed.** 18 ALLOC FAILED lines. Board 2 also panicked twice (its own log says "while: idle"), both in the same minute as board 1's fourth and fifth panics, while it held the relayed caller C and caller D. It was not watched on serial, so there is no backtrace for those two. Both boards came back on their own with their roles and the card.

Lowest numbers in the 30 s polls (board 1):

| | lowest |
|---|---|
| free (poll) | 46732 |
| largest block (poll) | 34816 |
| min since boot (between polls) | 11492 |
| windowed low wmin / wlarge | 36568 / 13312 |
| largest block in the ALLOC FAILED lines | down to 3584 |

Every panic is `abort()` from a failed `operator new` (an exception nobody catches), decoded with addr2line on this build:

| time | failed alloc | free / largest then | where |
|---|---|---|---|
| 77 s | 15713 | 35992 / 14336 | `Node` output string grows (node.cpp:342) while `expandArt` (pipecodes.cpp:75) writes a screen |
| 112 s | 15361 | 29072 / 15360 | a string in the pipe code expansion (pipecodes.cpp:190) under `Node::showLoginScreen` (node.cpp:468): the login screen, not node.cpp:342 |
| 175 s | 31310 | 55148 / 24576 | LORD `NativeLord::readFile` from `loadMonster` (game.cpp:360), the whole LENEMY.DAT |
| 758 s | 15713 | 31824 / 12800 | node.cpp:342 from expandArt again |
| 895 s | 15713 | 32204 / 13312 | node.cpp:342 from expandArt again |

The 13 ALLOC FAILED lines that did not panic are all 12016 bytes. That is `ZipReader::kStateBytes` (the 10992 byte `tinfl_decompressor` from firmware.elf plus the 1024 byte input buffer), which `AnsiViewer::grabInflate` asks for next to its 32 KB window; the viewer survives the failure and the pack does not open.

What each caller did (counts from the script):

| caller | where | did |
|---|---|---|
| A, LORD | main | 10 LORD entries, 9 forest fights won, 1 death, 63 town screens (forest events and no fight left) |
| B, ANSI viewer | main | 13 packs opened and a piece played in each, 30 packs refused for memory (12016 byte inflate) |
| C, messages and files | handed to board 2 | 12 message areas, 8 posts read; no ZMODEM in the full run (see below) |
| D, follower menus | board 2 | 121 menu screens served |

Notes on the run:

- The main keeps two sessions of its own. A and B dial first and stay on board 1; C (the third call to 192.168.1.164) is handed to board 2 every time (`call from ... passed to 192.168.1.165`). So C's load lands on board 2, not board 1. The plan expected three callers on board 1.
- C never reached its file walk in the full run: each reset dropped every call and C restarted from the message menus. Its ZMODEM path works: alone on board 1 it walked the messages, three file areas and downloaded NET1NY09.ZIP, 8233 of 8233 bytes, ZFIN seen (`trialC.csv`). A separate test of the receiver took BBS0991A.ZIP, 72122 of 72122 bytes.
- Single caller trials, each alone on board 1, same firmware: LORD alone, wmin 57060 (no failure). ANSI viewer alone, min 6192, largest 6400, 3 ALLOC FAILED of 12016, no reset. Messages and files alone, wmin 63040, no failure.
- `@DDNS now` at 300 s ran with the largest block at about 35 KB. The web stack mark after it was 4420, not 1108, so that check did not take the deep (HTTPS) path.

### Instrument concern (Task 1, not fixed here)

`wmin` misses the real lows. In the same boot, `min` (the IDF low since boot) was 11492 while `wmin` said 36568, and `wdoing` named the ANSI viewer while the deepest lows were in `node` and `T` (the board's "heap low in" log lines). The window is sampled only at the top of `loop()`, so lows inside one pass (a screen being expanded, a door step) do not reach it. `wdoing` is also cut at 24 characters, so the handle is lost (`door_ANSI_Pack_Viewer_by`). Neither field works as Task 18's pass number as it stands: `wmin` misses the lows, and `min` (since boot) already holds the boot DNS check's 53 to 59 KB, under the 60 KB busy floor, before any caller. Task 18 needs a window that resets with `@HEAPWIN` and is fed from the deep spots too (the places that write the "heap low in" lines), or the busy floor read from those lines.

## 3. The main's extra 22 KB

Measured gap at idle: board 2 113452 minus board 1 91468 = **21984 bytes**.

There is no command that unmounts the card or stops the web task at runtime (`@SDCHECK` unmounts and mounts again at once), so the SD and web rows are sizes, not before/after readings. Struct sizes come from the debug info in this build's firmware.elf (`gdb -batch -ex "p sizeof(...)"`).

| item | bytes | how measured |
|---|---|---|
| FATFS mount on the card: `vfs_fat_ctx_t` (2660, holds the FATFS object with its 512 byte window and two 1 KB path buffers) plus 10 open file slots (`SD.begin(..., 10)`) of 552 each (`FIL`, 512 of it the per file cache), plus 40 bytes of flags | 8220 | computed from sizeof in firmware.elf; board 2 has no card so does not mount it |
| Web task (No-IP, DNS, Discord): stack 7168, task control block 344, line queue 8 x 320 = 2560 plus 84 | 10156 | computed from sizeof; `stk_web=0` on board 2 shows the task does not exist there (it starts only where No-IP, DNS or Discord is set, and those are set on board 1 only) |
| Last callers list, main only (`lastCallers`, up to 20 lines) | about 1500 | estimated: 20 x (24 byte string plus its line on the heap) |
| User list | 0 | both boards hold all 9 users (`@USERS`: 9 on each board, board 1 read after the busy test; the cluster log "node 3 has every user (9 sent)") |
| `artCache`, `menuCache` | 0 on board 1 | follower only (main.cpp `themeArt`, `menuFile`); board 2's are empty at idle (`artmax=0`) |
| Day stats | under 100 | both boards keep their own |
| Unexplained (heap block headers on the items above, the main's cluster bookkeeping, the card's SPI and card structs, web settings strings) | about 2100 | gap minus the rows above |
| **Total** | **21984** | |

So two items explain about 18.4 KB of the 22 KB: the FATFS mount (8.2 KB) and the web task (10.2 KB).

## 4. Stack marks

Bytes left, lowest seen anywhere today.

| task | stack size | lowest free | when |
|---|---|---|---|
| web | 7168 | **1108** | right after the boot DNS check (seen after every clean boot on board 1); 4420 after `@DDNS now` at low heap |
| mdns | 4096 | 2124 (board 2), 2260 (board 1) | idle and busy |
| esp_timer | 3584 | 3392 | constant, both boards |
| Arduino loop (`stack` field) | 8192 | 1364 (board 1, messages and files trial), 1748 in the full run | busy |

What they decide:

- Task 4 step 4: the web task has 1108 free, under 2 KB, so its stack stays at 7168.
- Task 17 step 2: mdns has more than 1.5 KB free on both boards (2124 lowest) and esp_timer has 3392 free, so `CONFIG_MDNS_TASK_STACK_SIZE=3072` (about 1.1 KB left) and `CONFIG_ESP_TIMER_TASK_STACK_SIZE=2560` go ahead.

## 5. Candidates for Phase 3

Not built. For Rev to decide.

1. **Web task only while it works (about 7.5 KB idle).** The web task's 7168 byte stack and its control block sit idle 10 minutes at a time. Start the task when a DNS check is due or a Discord line is queued, and let it end itself when the queue is empty and no check is due; keep the queue so lines still wait. The stack mark (1108 free) says the size cannot shrink, but it need not exist between checks. Over 4 KB, small change in `src/platform/cyd/web.cpp`. Draft in the plan's format:

   ### Task 17b: Web task on demand
   **Files:** Modify: `src/platform/cyd/web.cpp`
   - [ ] **Step 1:** `startTask()` creates the task only when `ddnsDue_`, a queued line, or the 10 minute timer says there is work; `run()` returns (and the task deletes itself, `task_ = nullptr`) after the queue is empty and no check is due.
   - [ ] **Step 2:** A one shot timer (or the main loop's existing tick) calls `startTask()` when the next check is due. `@DDNS now` and `announce()` call it too.
   - [ ] **Step 3:** Build cyd and the host build. Commit: `fix: web task runs only while it has work`.

2. **FATFS: already covered by Task 17 step 1.** Turning off `CONFIG_FATFS_PER_FILE_CACHE` drops the 512 byte cache from each of the 10 file slots: 5120 of the 8220 bytes. No new task. (Cutting `SD.begin`'s 10 slots would only save 40 bytes each after that.)

3. **Line queue 8 x 320 = 2560 bytes.** Four entries would save 1280. Under 4 KB, noted for later.

The panics in section 2 are already Phase 2 items: the output string growing a whole screen (Task 6, #11), the login screen expansion (Task 7, #12), LENEMY.DAT read whole (Task 10, #9) and the viewer's inflate state (Task 11, #20). The busy test now confirms each one with a backtrace.
