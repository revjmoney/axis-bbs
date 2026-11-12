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

## Final (Task 18, 2026-11-12)

Measured on branch memory/board1 at 5023104 (cyd image: flash 2070945 bytes, 87.8%; RAM 47840), flashed to both boards after reading their MACs (board 1 8c:94:df:4f:1c:4c on COM20, board 2 8c:94:df:4f:14:18 on COM21). Both boards were backed up first: `local/backups/board1-COM20-2026-11-12-pre-memory-final.bin` and `local/backups/board2-COM21-2026-11-12-pre-memory-final.bin` (4 MB each, in the main checkout, not committed). Theme crt, WFC skin cyan, unchanged. Raw files stay local in `F:\axis-bbs-work\memory\final\`; screenshots in `shots/memory/` (not committed).

### Before and after

| | before (Task 3) | target | after | result |
|---|---|---|---|---|
| board 1 idle free | 91468 | 125 KB (128000) or more | 140636 | PASS, 12636 over |
| board 1 idle largest | 81920 | 90 KB (92160) or more | 110592 | PASS, 18432 over |
| board 1 lowest free in the busy test (30 s polls) | 46732 | 60 KB (61440) or more | 57704 | FAIL, 3736 short |
| board 1 windowed low in the busy test (wmin, after @HEAPWIN) | 36568 | 61440 or more | 38560 | FAIL, 22880 short |
| largest block in the busy test (30 s polls) | 34816 | 32 KB (32768) or more | 28672 | FAIL, 4096 short |
| largest block in the window (wlarge) | 13312 | 32768 or more | 25600 | FAIL, 7168 short |
| resets in the busy test | 5 panics on board 1, 2 on board 2 | none | 0 | PASS |
| ALLOC FAILED lines | 18 | none | 0 | PASS |
| lowest since boot (min, reported, not judged) | 11492 | | 25804 | |

So the busy test FAILS its memory floors, although nothing panicked or failed to allocate and the callers on board 1 did more work than in the baseline.

Idle readings (15 minutes after boot, two DNS checks past, no callers):

```
board 1 up 911: heap=140636 largest=110592 min=98876 stack=3224 wmin=101300 wlarge=90112 stk_web=3048 stk_mdns=1300 stk_timer=2372
board 2 up 1067: heap=149544 largest=110592 min=104192 stack=2820 wmin=139572 wlarge=110592 stk_web=0 stk_mdns=1304 stk_timer=2372
```

An earlier reading the same night on 3318659 (the image before the fix below) gave board 1 140496 / 102400 at up 951, and board 2 149560 / 110592. Board 1 is now 8908 bytes behind board 2 at idle (it was 21984).

### Busy test

Same callers, events and flags as Task 3: 15 minutes, A (LORD) and B (ANSI viewer) on board 1, C (messages and files) and D (follower menus), the follower reset at 3 minutes, `@DDNS now` at 5 minutes, board 1 restarted 3 minutes before. A was given another test user because the Task 3 warrior is dead today.

| caller | board | did before (Task 3) | did after |
|---|---|---|---|
| A, LORD | main | 10 entries, 9 fights won, 1 death | 12 entries, 7 fights won, 1 death, 86 town screens |
| B, ANSI viewer | main | 13 packs opened, 30 refused for memory | 20 packs opened and played, 38 packs that did not show their pieces (reason not traced; 0 ALLOC FAILED) |
| C, messages and files | handed to board 2 | 12 areas, 8 posts read | 18 message areas, 12 posts read, 7 file areas |
| D, follower menus | board 2 | 121 screens | 349 screens |

The ZMODEM download is proven separately, as in Task 3: caller C alone on board 1 took NET1NY09.ZIP, 8233 of 8233 bytes, ZFIN seen, lowest free 123120. In the full run C's test user is refused downloads by the board's upload ratio ("Downloads open up once you've uploaded something"): a test user choice, not memory.

Lowest polled rows: free 57704 with largest 43008 at 62 s, and free 59280 with largest 28672 at 272 s, both while the ANSI viewer played a piece and LORD ran. The window agrees: `wdoing=door_ANSI_Pack_Viewer_by` for wmin 38560 and wlarge 25600. HeapScope's lowest lines are `heap low in door: min 43320->40156, largest 43008` at 60 s (the viewer starting while LORD is open). The lifetime low of 25804 came between two polls and left no "heap low in" line, so it was outside the HeapScope sites. Caller B alone (no LORD) gives wmin 67848 and wlarge 40960, which would pass. The miss is the viewer's 32 KB window plus its 12016 byte state while LORD also runs. After the run the largest block at rest stays at 73728 (110592 after a fresh boot), the same kind of fragmentation Task 3 saw after callers.

`outmax` (the biggest caller output buffer) peaked at 3928 bytes in the run (15712 in the baseline). `artmax` peaked at 10764.

### Bug found on the hardware and fixed

The first busy run on 3318659 reported PASS while A and B never reached the main menu: the end of a screen was held back until the caller's next key. A trace showed the main menu prompt stopping at `[00:59] <handle> -` for 4 s, its last bytes arriving only after the next key. Screens sent in 2 KB pieces (Task 6) go out through the line without a flush, and `sendApp` skipped its flush when the node had nothing left. Fixed in 5023104 (`sendApp` flushes even when empty). The same trace then logged in, reached LORD and won 4 fights with no key nudges. The busy test script now fails a run in which a caller never does its job (fcf990f), so a stuck caller cannot pass again.

### Extra checks

- WFC redraw: no timing line exists, so a script pinged board 1 over serial every 5 ms for 200 s and logged the gaps between answers. Normal gap 5.8 ms; the longest stalls, including the minute redraw, were 60.6 to 63.0 ms. A full redraw (snapshot, art, six bands) holds the loop at most about 63 ms, under the 150 ms budget.
- New 8x8 WFC font: `@SHOT` of both boards. Box frames join, the node and today panels read cleanly. The second look (`wfc=classic`) draws the same built-in art on this card (the crt/cyan folder has no WFCCYD.ANS); it was set back to cyan, and @SETTINGS hashed the same before and after.
- `@SPRITEFAIL`, then node 1's detail by `@TAP`: `@SHOT` answers NONE, free stays at 140552 (no 38.4 KB copy made), BACK returns to the WFC and `@SHOT` works again. No ALLOC FAILED. The panel colours on that path were not seen (no camera). The same screen without `@SPRITEFAIL` shoots normally.
- Web task on demand: Discord is not set on board 1, so no post was tested. Four `@DDNS now` runs: free back at 140476 to 140496 within 25 s each time, largest 102400, `stk_web` 3044 free of 7168 (1108 before). No-IP answered over HTTPS. The runs left no log line of their own, so each one is not individually confirmed. `stk_web` stayed at 3044 to 3052 through the busy test.
- Stack marks now: mdns 1248 to 1304 free of 3072, esp_timer 2372 of 2560, loop 2280 lowest in the busy test.

### Each issue

| issue | change | measured |
|---|---|---|
| #24 (Task 1) | heap window, wdoing, stack marks, HeapScope | works: @HEAPWIN resets the window and wdoing names the viewer; the 25804 low still escaped the HeapScope sites |
| #8 (Task 4) | TLS gate, no plain No-IP | not separately measurable (no gate note in these runs) |
| #10 (Task 5) | files read at their size | not separately measurable |
| #11 (Task 6) | caller output in 2 KB pieces | outmax 15712 to 3928 (11784 less at peak); the three expandArt panics are gone. Its missing flush was the held-tail bug above |
| #12 (Task 7) | login screen without the whole expansion | the login panic (15361 byte alloc) is gone; not separately measurable |
| #13 (Task 8) | pre-logon checks without reading files | not separately measurable |
| #14 (Task 9) | main serves follower screens in place | not separately measurable |
| #9 (Task 10) | LORD reads one monster | the 31310 byte LENEMY.DAT read is gone (one record now), and its panic with it |
| #20 (Task 11) | one ZIP window for the board | 0 ALLOC FAILED (13 of 12016 before); 20 packs opened (13 before) |
| #21, #18 (Task 12) | lists and users.dat streamed | not separately measurable |
| #15, #16 (Task 13) | WFC art read once, follower art cache 16 KB | artmax 10764, under the cap |
| #17 (Task 14) | AnsiTerm holds only its cells | AnsiTerm object 4864 to 88 bytes plus 45 x 24 cells (from the build) |
| #22 (Task 15) | WFC in 40 px bands, full copy only away from the WFC | about 32 KB at the idle WFC (38400 byte copy replaced by a 6400 byte band); redraw at most about 63 ms |
| Task 15b | real 8x8 font on the WFC | 0 bytes of RAM (font in flash, +1952 flash) |
| #19 (Task 16) | nodes share one settings copy | about 4 KB per node (estimate from the build) |
| #23 (Task 17) | FATFS per file cache off, mdns 3072, esp_timer 2560, TIME_WAIT 10 s | 5120 + 1024 + 1024 bytes by size; the stacks keep margin (above) |
| Task 17b | web task only while it has work | about 7.5 KB at idle (7168 stack plus TCB); heap back to idle after each run |
| all together | | board 1 idle free 91468 to 140636 (+49168), largest 81920 to 110592 (+28672) |

The main's extra memory at idle is now 8908 bytes (was 21984): the FATFS mount without the per file cache (about 3100), the web line queue (2644), the last callers list (about 1500), and a remainder of about 1650. Task 3's "unexplained ~2100" row was that same remainder: block headers, the main's cluster bookkeeping, the card's structs and the web setting strings.

### Missed targets

The busy floors are missed by 3736 bytes of free (polled), 4096 of largest (polled), and 22880 / 7168 in the window. All of them fall while the ANSI viewer holds its 32 KB window and 12016 byte state at the same time as a LORD game on the same board (wdoing and HeapScope both point there). Nothing panicked. Options for Rev, not built: a smaller viewer window when a door is already running, or the viewer declining to start (with its note) below a heap level.

## After the final (2026-11-12, later): the misses traced and closed

Rev chose to trace the busy-test dip, then a viewer limit, then an SD view cache so every piece stays viewable.

### What the dip was

A heap-trace bench build (`pio run -e cyd-trace`, `-DAXIS_HEAP_TRACE=1`, sdkconfig.cyd-trace with standalone heap tracing; prints `@HT` lines of the live allocations and their callers at each new low after `@HEAPWIN`) showed, at the lowest point: the viewer's inflate window and state (32768 + 12016), LORD's whole EVENTS.LDY script held as one string (18047 bytes, `src/lord/game.cpp:198` via `realm.cpp` loadScript), the viewer's 4 KB first read, the open pack's entry list (3072, grown by doubling), WiFi and lwIP buffers, and small lists. The low fell 13 KB when the LORD script loaded.

### What changed

| change | commit | effect |
|---|---|---|
| LORD reads .LDY scripts from the file, 2 KB at a time, with the section and label positions found in one pass at load; the file stays open while the script runs | b807a28, 342d2be, 3355438 | about 16 KB less while a forest event runs; on a follower about as many link round trips as the old whole read |
| viewer gives back its 4 KB first read once sent; ZIP entry list sized exactly | faef180 | about 3.5 KB and 1 to 2 KB |
| viewer limit: with the window taken the board keeps 60 KB free and a 32 KB block, checked at the start and every turn a piece plays, or the piece is refused or stopped with the note | 9bc3bda, 65db52f | with an 8 KB cushion every piece was refused on a busy board; without it 12 played, 92 refused, low 59724 |
| SD view cache: one stored (uncompressed) copy of each art pack in file area `viewpacks`, named by a fingerprint of its pieces, made at idle 16 KB a loop turn by a Filler, deleted when no pack matches; the viewer plays cached pieces with no window | 9b606de to 94dbd6a | pieces play on a busy board; the limit only applies to a pack not copied yet |
| the SD card mount is tried three times at boot | 6c36784 | a flash reset mid-write had left board 1 cardless, so the cardless board took main |
| final-review fixes: the Filler steps aside while the web task has work, catches bad_alloc, waits 5 minutes after boot; a follower reads remote files through a 2 KB read-ahead; a piece that unpacks past its size fails its pack; a listing closes its folder on bad_alloc | 6f24e79, 644be9e | |

The first try at the cache (a file per piece, `/sd/files/viewcache`) left thousands of files on board 1's card. It is left alone: deleting them one by one in a FAT folder that size stalls the loop for seconds a file. The sysop can delete that folder from a PC.

### Final numbers (6c36784 on the boards; 644be9e adds only the review fixes and was flashed after)

| | target | result |
|---|---|---|
| board 1 idle free (before the test, cache already filled) | 128000 | 140692 (140964 on 644be9e right after boot) |
| board 1 idle largest | 92160 | 110592 |
| windowed low in the busy test (wmin after @HEAPWIN) | 61440 | 78100, PASS |
| largest in the window (wlarge) | 32768 | 69632, PASS |
| lowest polled free / largest | | 101680 / 90112 |
| resets, ALLOC FAILED | none | 0, 0 |
| ANSI pieces played by caller B | | 20 of 20, none refused |

The run's summary line says FAIL only because caller C's test user is refused downloads by the upload ratio (a test user choice, not memory); its first row (taken before `@HEAPWIN`, while the cache Filler worked at idle) holds the 67592 / 47104 the summary prints. The cache pass copied 141 packs in 487 s. Caller A had no forest fights left that day, so LORD's script reader ran only in the host tests (a forest event with every 2 KB window edge moved through CR, LF and CR LF).

### Parked (rulings, for later)

- A pack that cannot be opened in a pass stops that pass from deleting stale copies (safe; card growth is capped by the 256 MB the Filler always leaves).
- Any connection (even a pre-login scanner) drops a pass in progress; it comes back two minutes later and keeps every finished copy.
- A follower's LORD script handle stays open across key waits; closing it after the main went silent waits for the link timeout.
- A pack whose copy failed (damaged piece or a card write error) is not tried again until a reboot.
- Opening a cached pack on a follower reads two central directories over the link.
- The DNS check every 10 minutes drops a fill pass, which then reopens every pack; finished copies stay.
