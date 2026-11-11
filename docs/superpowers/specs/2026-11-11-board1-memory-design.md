# Board 1 memory headroom: design

Date: 2026-11-11. Status: approved by Rev (2026-11-11), builder-checked.

## Why

Board 1 (the cluster main, the CYD with the SD card) idles at about 91 KB free heap (largest block about 69 KB). Under load it has dropped to 39 KB in the first minutes after boot, and once to 29 KB during a cluster rejoin with a call up, which panicked the board. Board 2 (a follower, no card) idles at about 113 KB.

Rev wants every feature kept, with more room underneath them. In his words: higher idle, callers don't crash.

A build menu that leaves features out was considered first and parked. This is why: our own code holds almost no static RAM (about 7 KB of `.bss`/`.data` across `src/`; the roughly 47 KB of static DRAM is mostly Wi-Fi and lwIP). Dropping a feature mostly frees flash and stops that feature's spikes. It does little for idle heap, and Rev keeps every feature anyway. The build options menu is the next project, after this one (see "Later").

## Goals (Rev's)

1. **Higher idle free heap on board 1.** Target: about 125 KB free or more at idle on board 1 with the WFC up (from about 91 KB). Largest block at idle 90 KB or more.
2. **Callers never crash it.** The busy test below runs to the end with no reset, no `ALLOC FAILED`, and no "low memory" refusal of a normal action. Lowest free heap during the test stays at or above 60 KB, and the largest block stays at or above 32 KB.
3. **The fixes in issues #8 to #24 that serve goals 1 and 2,** done and closed, with before and after numbers.

## Non-goals

- Leaving any feature out. LORD, file areas and transfers, the ANSI viewer, the DOS prompt, scripts, the cluster, No-IP, Discord and the modem link all stay, and they all behave as they do today.
- The build options menu and its cost table (next project).
- Message nets (Sysop Net, User Net): designed elsewhere.
- PSRAM, new hardware, or a partition table change.

## Anti-goals (a failure even if the numbers improve)

- Any change a caller can see, apart from the one Rev allowed (the touchscreen may show a brief sweep while the WFC redraws) and short new notes where the board now declines something for lack of memory instead of crashing. Caller output is otherwise byte for byte the same.
- LORD gameplay changes. The same monster must be picked for the same random number.
- Breaking an older follower. A board on the previous firmware must still join, get screens and log callers in (the cluster protocol only gains optional fields, like the `want` field on `T`).
- Trading a crash for a silent failure. Where memory really is short, the board says so (the event log and a short note to the caller) instead of dropping work quietly.
- Weaker security. TLS stays for anything that carries a secret.

## Constraints

- ESP32 with about 320 KB SRAM and no PSRAM. Arduino-ESP32 2.0.17 on IDF 4.4.7 (`pio run -e cyd`, needs `PLATFORMIO_CORE_DIR=F:\CodexStorage\Tools\platformio`). Flash is 86.9% full, so no fix may add much code.
- IDF 4.4 cannot reset the lifetime heap minimum, so measurement needs our own windowed minimum (issue #24).
- `sdkconfig.cyd` is the file that counts. IDF ignores later `sdkconfig.defaults` edits, so edit the env file and do a clean build.
- Every board runs one image. Headless boards (`@HEADLESS on`) and followers must still work.
- Host tests (`cmake -S . -B build -G Ninja && cmake --build build && ./build/axis_tests`, 793 today) and the CI checks (Build, Sanitizers, CodeQL, Static analysis) stay green.
- Repo rules: no AI attribution in commits, `git add` named files only, leave Rev's untracked files alone, never print the cluster key or the public hostname, never touch COM15 or COM9 (or COM11). Board 1 and board 2 may be flashed after their MACs are checked (`8c:94:df:4f:1c:4c`, `8c:94:df:4f:14:18`).
- Rev's working style: hardware testing is batched into one final task, not done after each fix. Screenshots of each tested step. **One exception, Rev's OK (2026-11-11):** one early flash and measuring session on boards 1 and 2 right after phase 1, for the baseline, the 22 KB breakdown and the task stack marks. No fixes are tested there.

## The work, in order

Each item names its issue. The issue body already says where, what and how to check. This design only fixes the order, the scope and anything the issues leave open.

### Phase 1: measure (issue #24)

Needed first so every later fix has a before and after number.

- `HeapScope` around the known peak sites. It logs to the event log only when the lifetime minimum moves inside the scope, and it names a `webBusy` flag because the web task runs on core 0 at the same time.
- A windowed low in `@STATUS` (free, largest, and what the board was doing at that moment), reset by `@HEAPWIN`.
- Stack high-water marks for the web, mDNS and esp_timer tasks in `@STATUS`.
- `outmax=` and `artmax=` in `@STATUS`.
- **Explain the main's extra 22 KB.** With the tools in, take an idle reading on board 1 and on board 2 and account for the gap (SD/FATFS, main-role data such as user lists, stats and caches). Write the breakdown into the report. Any large, simple cause found here becomes an extra item in phase 3. Anything else is noted for later.

Baseline: before any fix, run the busy test (below) on the current firmware with only phase 1 added and record the numbers.

### Phase 2: stop the spikes ("callers don't crash")

Ranked by bytes saved at peak against risk:

1. **#8 No-IP and public IP.** The public IP lookup uses plain HTTP (it carries no secret). The HTTPS No-IP update runs only when the IP changed. GoDaddy DNS and Discord keep HTTPS. No TLS session may start unless the largest free block is at least `kTlsNeeds` plus a 32 KB reserve, so a handshake can never take the board under the busy-test floor (my call: gate on memory only, not on whether callers are online, so a busy board is never kept from updating DNS for hours). A gated DNS or No-IP update retries on the next cycle. A gated Discord line is dropped with an event-log entry, as lines are dropped today when TLS does not fit; there is no new queue. **The plain-HTTP No-IP fallback goes** (my call: today it sends the account login in the clear when TLS does not fit; the update now just waits for memory instead). Shrinking the web task stack is only done if the high-water mark from phase 1 shows room.
2. **#12 login screen** (about 30 KB): feed the AnsiTerm from the expand callback in pieces, and cache the field cells per (look, file).
3. **#11 `Node::out_` sink** (16 to 20 KB per screen): flush through `setDirect` at about 2 KB. Caller bytes stay identical (host test).
4. **#10 file reads by doubling** (about 16 KB per screen): size first, then read straight in.
5. **#14 main serving follower screens** (15 to 20 KB): build the reply in one string and move it into the frame. Followers ask for 4 KB pieces; older followers that ask for 7000 still work.
6. **#13 pre-logon probing** (16 to 24 KB): an `exists()` callback, and the chosen file is read once.
7. **#9 LORD monster** (31 KB): seek and read one 239-byte record, same pick.
8. **#20 ANSI viewer / ZIP window** (44 KB per extra user): one board-wide window lent to one viewer at a time. While it is lent, a second viewer gets the pack's plain file list at once with a one-line note that another caller is viewing; nothing waits, so the main loop is never held. (My call: the shared window, not repacking art as STORE, because existing packs on the card stay as they are.)
9. **#21 shared lists** (rumors, ballots): append without rewriting the whole file.
10. **#18 `users.format()`**: write users.dat in pieces, not one string.
11. **#15, #16 WFC art re-reads and follower art cache**: read WFC.ANS only when it changes, and cap the follower art cache (my call: 16 KB total, least recently used goes first).

### Phase 3: raise the idle floor ("higher idle")

1. **WFC drawn in strips (follow-up in #22)**, worth about 32 KB idle. The touchscreen WFC (45x24 cells, the idle screen) draws through a 320x40 sprite (6.4 KB) band by band. The full 38.4 KB sprite is created only while another touchscreen screen is up (setup, the touch keyboard, the sysop shell, the local console) and freed when the board goes back to the WFC. If the full sprite cannot be created at that moment (fragmented heap), that screen draws straight to the panel instead (slower, may flicker) rather than failing. Rev OK'd a brief sweep on a full WFC redraw. `@SHOT` keeps working: at the WFC it renders the bands one after another into the serial dump, so the PC tool still gets 320x240.
   Done together with **#17** (my call: it touches the same WFC code): size each AnsiTerm to the screen it serves (45x24 for the panel WFC, not 80x30), draw without copying the snapshot, and read the built-in art from flash in place. This also shrinks the login and shell terminals used by #12.
2. **#22 headless boards skip the sprite** (38.4 KB on any headless board, which can stand in as main).
3. **#23 sdkconfig**: FATFS per-file cache off (about 5 KB on the SD board), esp_timer and mDNS task stacks down about 1 KB each (only if phase 1 marks show room), TCP MSL 60 s to 10 s. Wi-Fi buffers, AMPDU, IPv6 and the loop stack are not touched.
4. **#19 one shared settings copy** (about 4 KB per caller and per extra node): `shared_ptr<const BbsConfig>`, with areas kept as indexes.
5. Any large and simple cause of the main's extra 22 KB found in phase 1.

### Phase 4: prove it (one batched hardware task)

Flash board 1 and board 2 (MACs checked first). Then:

- **Idle reading:** board 1 at the WFC 15 minutes after boot, at least one No-IP cycle past: free, largest, windowed low.
- **The busy test** (a script in `tools/`, so it can be rerun later): three telnet callers on board 1 at once. One plays LORD forest fights, one opens ANSI packs in the viewer, and one walks the message and file menus and downloads a file with ZMODEM. A fourth caller on board 2 views screens served by the main. Meanwhile board 2 is reset so it rejoins, and a No-IP or DNS check is forced (a serial command, or the timer brought forward for the test). Run for 15 minutes. Record the windowed low and largest block every 30 s.
- A before and after table (baseline from phase 1 against the final build), with screenshots of `@STATUS` and the WFC, into `docs/superpowers/plans/` as the report. Close each finished issue with its measured saving.

## What success looks like in the report

| | before | target |
|---|---|---|
| board 1 idle free | about 91 KB | 125 KB or more |
| board 1 idle largest | about 69 KB | 90 KB or more |
| board 1 lowest during the busy test | 29 to 39 KB seen | 60 KB or more |
| largest block during the busy test | under 32 KB seen | 32 KB or more |
| resets during the busy test | one panic seen | none |

If a target is missed, the report says by how much and what the phase 1 tools show is still holding the memory. It does not quietly lower the target.

## Left to the builder

- Exact strip height for the WFC bands (40 px suggested), as long as a full redraw stays under about 150 ms.
- Exact `HeapScope` placement beyond the sites listed in #24.
- How each fix is split into commits (one fix per commit, conventional messages).
- Whether #15 and #16 land as one change.
- How the busy test script drives LORD and the viewer (scripted telnet, like the existing `tools/seven_login.py`).

## Later (not this project)

**Build options with cost tracking** (Rev's original request). The install menu gains a keep or drop list (modem link, file areas and transfers, message nets as a reserved `nets` row, LORD, the script engine, the DOS prompt, the ANSI viewer, the cluster, No-IP/Discord/DNS), each compiled out with its own define. Each row shows flash and static RAM measured by CI once per release, plus the feature's peak RAM while running (from the phase 1 tools). The running total is approximate, because shared code only drops when everything using it is off. Its own design once this project is done.
