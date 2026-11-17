# Nets Phase 3 bench, 2026-11-17

Board 1 (COM17, MAC checked by write-flash) ran a bench-only build: nets/phase3 at c90758f plus a test-key swap in `src/net/revkeys.cpp` only (both of Rev's public keys replaced by two throwaway test keys). The image was 2,124,893 B (90.1%), the same size as c90758f. Rev's real seeds were never read or used. The real-key admit is untested on hardware. Board 2 was not used. The PC test peer `axis_netpeer` (built from the same test-key sources) ran on the build box, listening on port 2401.

**Arrangement.** Board 1 took test key 0 with `@NETKEY SET` and became a (test) Rev board. The peer ran on test key 1, so it was a Rev board too. A Rev board counts as a User Net member and carries all four areas, so both could share and fetch with no admit, vote or limits notice signed at all. So no admit or tally membership ran with any key, and a normal member's ingress limit never applied to the listings. Board 1 needed `net_user=1` for callers to get the Net Files row. It also needed `public_host` cleared for the bench, because the build box cannot resolve board 1's public name; the card then carried the LAN address. Both settings were set back afterwards.

| Step | Result |
| --- | --- |
| 1 @NET before | Captured on the real build: board 1 (its own id), `rev board no`, `user net off (application pending)`, `files: 0 listed, 0 shared areas`, 3 cards, outlog 0..11, link peers PCPEER and BenchPeer (both down). |
| 2 Share and upload | A bench file area `p3bench` (level 0) was added and put in `net_files_areas`. A bench caller uploaded P3SMALL.TXT (62 B) with ZMODEM; it was listed as `2762eeca612bdf6c-1794516696`, and the peer's `files` showed it after one sync. |
| 3 Peer fetch | The peer's `fetch` gave `62 bytes (crc ok)` with an identical md5. Board 1's record shows `fetch served 2762eeca612bdf6c-1794516696 to netpeer@39167b1fc58dd12f`. A 300 KB file from board 1 was fetched by the peer 3 times out of 3, crc ok and md5 identical. |
| 4 Peer's 300 KB file | The peer listed BIG300K.BIN (307,200 B), which showed in Net Files (screenshot 1). A caller on board 1 downloaded it with ZMODEM through Net Files at 114 to 140 KB/s; when whole, its md5 was identical (screenshots 2 and 3). But only 2 of 6 caller downloads and 3 of 7 `@NET FETCHTEST` runs completed. The rest ended 1 to 4 KB short (`closed`, or ZMODEM cancelled), 2.3 to 5.2 s in. The cause is open (see Bugs). Every failure was the board's fetch client against the PC peer's server. The reverse (board server, PC client) was 3 for 3, and board client against a board server was never tried. |
| 5 Unlist | After the peer's `unlist` and one sync, the row was gone from `@NET FILES` and from the caller's list (screenshot 4). `@NET FETCHTEST` of the old id gave `@ERR gone`. |
| 6 Busy heap test | Skipped, pending Rev's OK. Heap lines are under "Heap" below. |
| 7 I6 timings | Measured up to 259 held listings, not 1000: loop turns passed 5 s and the task watchdog fired, so the pull was stopped. Numbers below. |
| 8 Strict level | With `p3bench` at level 50 and the caller at 20, board 1's own P3SMALL.TXT was refused with `that file is in an area closed to your account` (screenshot 5). The peer's F0001.TXT downloaded fine (19 B, ZMODEM). |
| 9 "asked again from" | None seen, but coverage is partial. Serial was captured only while bench commands ran, and `@LOG` keeps only a short RAM tail, so the failing caller downloads in step 4 were not on serial. |
| 10 Restore | `@NETKEY CLEAR`, then board 1 was reflashed with the real build from the main worktree (i5build "building cyd for nets/phase3 (c90758f7)", 2,124,893 B, hash verified). After that it showed `rev board no`, board 1 (its own id), `user net off (application pending)`, and the same members and apps as step 1. `@NET MEMBERS` was empty, as in step 1. `@NETKEY CLEAR` again gave `@OK no net key was set`. `@SETTINGS` is identical to step 1, and `public_host` holds its original name. A sync rotation dials P3Peer, BenchPeer and PCPEER (no answer from any); it does not dial the test Rev card. Test residue is listed below. |
| 11 Cleanup | The throwaway worktree and branch were removed. On the build box, the `i5build/bench_p3-testkey` ref, its builds folder and the peer folder (which held the peer's test seed) were removed, along with the local `i5/i5build/bench_p3-testkey` tracking ref. The local test seeds were deleted. |

## I6 numbers (board 1, SD card, bench build)

The firmware has no loop-turn timer, so these are wall-clock figures. A serial `@STATUS` ping ran every 0.1 s during each pull; its reply delay, and the board's own `loop stalled <ms>` lines (logged over 2 s), give the turn length. A session stores 64 listings in 8 loop turns, so 8 listings a turn. The idle ping reply was 0.08 s median and 0.16 s max.

| Listings held | Loop turn while storing (s) | Per stored listing (s) |
| --- | --- | --- |
| 3 to 67 | 0.86, 0.95, 1.15, 1.35, 1.60, 1.81, 2.10, 2.37 | 0.11 to 0.30 |
| 67 to 131 | 2.55, 2.86, 2.97, 3.27, 3.39, 3.71, 3.94, 4.11 | 0.32 to 0.51 |
| 131 to 195 | 4.36, 4.46, 4.75, 5.08, 5.23, 5.43, 5.64, 5.88 (task watchdog fired, IDLE1 starved; no reset) | 0.55 to 0.74 |
| 195 to 259 | 6.13, 6.36, 6.59, 6.86, 7.21, 7.16, 7.38, 7.57 (watchdog each turn) | 0.77 to 0.95 |

- **Per stored listing:** grows linearly, about 3.4 ms for each listing already held. Straight-line extrapolation: about 3.4 s per listing and about 27 s per loop turn at 1000 held, and roughly 30 minutes of blocked loop to pull 1000. The 1000 point is not measured.
- **Where the time goes:** the watchdog backtraces show `NetCore::takeListing`, in `netIdxWrite` (an fopen on the SD card for each index write, from `idxHead`) and in `FileMessageStore::post`. This is the final review's I6.
- **Net Files page 1 draw:** measured at the caller, from the key to the page shown. 0.25 s with 4 listings. With 195 held: 0.75 to 1.41 s (3 runs), with loop turns of 0.6 to 0.96 s. Opening Files (the area list with the Net Files row) and quitting Net Files back to it each took a 1.8 s turn at 195.
- **Replace cost:** measured with board 1's own changes at 262 records held. A removal (rename, or `@FDEL`, which rewrites `nf_list` and rebuilds the index) took a 3.95 to 4.31 s loop turn. The new name's listing (an append) took 1.1 s. A peer's re-list of 8 names was sent, but it sits behind the peer's remaining 740 listings, so its replace on board 1 was not reached.

## Heap

- Idle on the test build: 135,860 free, largest 110,592.
- After step 3: 125,928 / 94,208. After step 4: 126,172 / 110,592. After step 5: 125,396 / 110,592. FETCHTEST line: [122/108 KB].
- Sync lines: [118/100], [117/92], [125/108], [127/108], [118/108], [120/100], [109/92], [120/100] KB.
- During the I6 pulls: lowest free 104,432, largest 90,112 to 102,400.
- During a Net Files page draw at 195 held: lowest free 100,576, largest 77,824.
- After the restore: 135,700 / 110,592.

## Test residue on board 1 under the real build (reported, not hidden)

Board 1 is not a member or a Rev board under the real build. No notice was signed in the bench (no admit, vote or limits). What remains:

- **Listings:** `nf_list` holds 257 live P3Peer listings (F0001 to F0257, test key 1) and the tombstones of the bench's removals. `@NET` says `files: 257 listed`, and `@NET FILES` lists them. Callers see no Net Files row while `net_user=0`. Their home board is not a member under the real keys.
- **Cards and link peers:** cards went from 3 to 5 (the test Rev board 2762eeca612bdf6c and P3Peer 39167b1fc58dd12f). P3Peer is now a link peer (queue 62), so `@NET SYNC` will dial its old address in turn and get no answer.
- **Outlog:** 0..11 became 0..280. PCPEER (274) and BenchPeer (270) have the bench's listing lines queued. These lines, and the two test cards, may go to any future link peer, not only those two.
- **Boot:** right after the real build booted, it logged `loop stalled 2211 ms`, likely work over the 257 held listings.
- **Other:** the bench caller was deleted (`@UDEL`), but its LORD warrior made by `@ADDUSER` may remain. The empty `/files/p3bench` folder may remain on the SD card. The event log and net record keep the bench lines.

## Bugs and notes

- **I6 (firmware, confirmed over budget):** see the table. Storing one listing blocks the loop for time that grows with `nf_list`, and the task watchdog fires from about 5 s turns, which is about 130 to 190 held listings on board 1.
- **Step 4 short fetches (cause open):** the board's fetch client, against the PC peer's server, lost the last 1 to 4 KB of a 300 KB file in 8 of 13 tries. FETCHTEST gave `closed`; a caller's ZMODEM was cancelled. Two suspects remain:
  - The peer's `pumpServe` gives a piece left in `pend` only 3 s after the server finishes, then closes. That cannot explain the FETCHTEST failures at 2.3 s.
  - The board client's handling of the home board's close (`main.cpp:4219`, `!connected() && available() <= 0` with a full window).
  - Board client against a board server was not tried. Next: a second-board or host repro, before any fix.
- **Minor:** sync log lines say `0 in` while listings were taken (only posts are counted). `@NET FETCHTEST` with no id shows the last finished run, not a refusal at begin (`@ERR gone` there was not kept as the last result).

## Re-bench (0fed318), 2026-11-17

Board 1 (COM17) ran a bench-only build: nets/phase3 at 0fed318 plus a test-key swap in `src/net/revkeys.cpp` only, with two fresh throwaway test keys (test board ids 081fbb2d9cd58175 and 3b3727361f2c21a3). The image was 2,125,717 B (90.1%). Rev's real seeds were never read or used. The PC peer `axis_netpeer` (named P3Peer2, on test key 1) was built on the build box from the same test-key tree and listened on port 2401. The arrangement matched the first run: both ends were (test) Rev boards, so no admit, vote or limits notice was signed. The first run's two test cards and its P3Peer listings were signed by its deleted test keys. They stayed on board 1 as residue and played no part. The busy heap test was skipped again.

Notes on the setup:
- **Starting count:** board 1 held 200 P3Peer listings at the start, not 257. The daily trim holds each home board to 200 live listings (`kNetListingsPerBoard`), and it ran after the first run's restore. So the series starts at 200 held, and there is no 100 point.
- **When the peer listed its files:** its 1001 listings (F0001 to F1000 plus BIG300K.BIN) were made after its first session with board 1. In a first try, with the listings made before the link, 24 clean sessions pulled nothing. This was seen once and not traced. Board 1 already showed `history done` from the first run, so a fresh board may behave differently.
- **First peer:** `net_first_peer` was pinned to the new peer's board id, because board 1 still remembered the old P3Peer board for that address and port.
- **Upper count:** held counts reach 1201 (the 200 old listings plus 1001 new) only until the next daily trim, which cuts each board back to 200.

### I6: loop turn per stored listing

The firmware has no loop-turn timer, so these are wall-clock figures. A serial `@STATUS` ping ran every 0.1 s, and its reply delay and the `loop stalled` lines give the turn length. Each session stored 64 listings in about 8 turns, as in the first run: the board reads 2 KB of a session's lines a turn (`kNetFeedPerTurn`), about 8 listings. Only turns inside a session with the peer are counted. Board 1 also dials three dead peers in turn, and each of those dials costs a 1.2 to 1.6 s turn that has nothing to do with storing. Those turns are left out.

| Listings held | First run (c90758f), turn while storing (s) | Re-bench (0fed318), turn while storing (s) | Re-bench, per stored listing (s) |
| --- | --- | --- | --- |
| about 100 | 3.0 to 3.3 | not measured (the series starts at 200) | |
| about 250 | 6.1 to 7.6 (watchdog every turn) | 0.59 to 0.68 | 0.07 to 0.09 |
| about 500 | not reached (extrapolated about 17 s) | 0.64 to 0.75 | 0.08 to 0.09 |
| about 750 | not reached | 0.73 to 0.83 | 0.09 to 0.10 |
| about 1000 | not reached (extrapolated about 27 s) | 0.82 to 0.97 | 0.10 to 0.12 |
| 1160 to 1201 | not reached | 0.89 to 0.95 | 0.11 to 0.12 |

- **Peak:** the longest store turn seen was 1.06 s (at about 1150 held).
- **Speed:** the pull of 1001 listings took 16 sessions and about 140 s. Storing caused no `loop stalled` line, and the task watchdog never fired while storing.
- **Growth:** the turn still grows a little, about 0.3 s more per turn over 1000 more held listings. That is about 0.04 ms per held listing for each stored listing. The first run measured 3.4 ms, so the new cost is about 100 times lower.
- **Heap during the pull:** 106,944 to 124,368 free, largest 90,112 to 98,304.

### I6: what is still over budget at 1200 held

These are single loop turns at about 1200 held. The task watchdog fires (IDLE1 starved) on every turn over about 5 s.

| Action at about 1200 held | First run (c90758f, listings held) | Re-bench (0fed318) |
| --- | --- | --- |
| Net Files page 1 draw, first open (caller side, screenshot rebench/2) | 0.75 to 1.41 s (195) | 8.2 s turn (watchdog), 8.87 s at the caller |
| Net Files next or previous page | not measured | 4.0 to 4.2 s turn every time (4 runs) |
| Net Files page 1, a later open | not measured | 4.38 s at the caller |
| Opening Files (the area list, screenshot rebench/1), going back to the menu | 1.8 s (195) | 1.9 to 2.1 s |
| A replace from the peer (re-list, higher seq) | not reached | 10.7 s and 8.0 s turns (watchdog) |
| A removal on board 1 (`@FREN`, `@FDEL`) | 3.95 to 4.31 s (262) | 8.7 s, 8.3 s, 8.2 s turns (watchdog) |
| A removal from the peer (`unlist`) | not reached | 11.7 s turn (watchdog) |
| An append (board 1's own upload listed) | 1.1 s (262) | 0.61 s |
| `@NET` (its file count) | not measured | 2.4 s (712), 3.9 s (1160), 5.1 s (1201) |
| `@NET FILES` | not measured | 4.1 to 8.2 s |
| The daily trim after the restore boot (real build) | 2.2 s (257) | 13.9 s (watchdog twice) |

- **Where the time goes** (decoded backtraces):
  - The page draw runs `NetCore::listingsPage`, which calls `eachListing`, a whole `scanHeaders` of `nf_list` for every page.
  - A replace or removal runs `FileMessageStore::drop` from `takeListing`, which rewrites all of `nf_list` in one turn.
  - The boot trim runs `trimListings`, an `eachListing` scan followed by a `drop`.
- **The index rebuild is stepped.** After the removal's 8.3 s drop turn, the index was rebuilt over about 6.5 s of turns of 0.10 to 0.21 s each (idle is 0.06 to 0.09 s), with no long turn.
- **Sessions during the rebuild:** board 1 dialed no session for about 10 s after the drop, so none ran during the rebuild. Afterwards, the peer's two new listings (F1001, F1002) and the replaced F0500 were all taken, and no `store` refusal showed on either side. **Not tested:** a session that waits out a rebuild with its listing lines parked and then takes them. Every session here carried a single changed line, and no session ran during a rebuild. A next bench could have the peer queue one re-list, then two new files a second apart, then sync once.

### Fetches

| Check | First run | Re-bench (0fed318) |
| --- | --- | --- |
| `@NET FETCHTEST` of the peer's 300 KB listing | 3 of 7 whole | 15 of 15 `@OK 307200 crc ok` (2.1 to 5.6 s each) |
| Caller ZMODEM download through Net Files | 2 of 6 whole | 5 of 5 whole, md5 identical (one at area level 50) |
| Peer fetches a 300 KB file from board 1 | 3 of 3 | 3 of 3, crc ok, md5 identical |
| "asked again from" lines | none seen | none seen |
| Build box `nstat` TcpExtTCPAbortOnData / TcpExtTCPAbortOnClose | not taken | 537 / 333 before, 537 / 333 after (flat) |

To download the peer's file, a caller has to page to it, because `D` and a name finds only files on the page shown. With 1200 listings, the first 300 KB listing was on the last page. Three download tries stopped at `No file by that name in this area` before any fetch, so the peer re-listed the file to bring it to page 1.

### Strict level and gone

- With `p3bench` at level 50 and the caller at 20, board 1's own P3BIG.BIN was refused with `that file is in an area closed to your account` (screenshot rebench/3). The peer's BIG300K.BIN still downloaded whole at that level.
- After the peer's `unlist F1002.TXT` and one sync, `@NET FETCHTEST` of its id gave `@ERR gone`. The replaced old BIG300K.BIN id also gave `@ERR gone`.

### Heap

- Idle on the bench build: 131,888 to 132,264 free, largest 110,592.
- During the pull: 106,944 to 124,368 free, largest 90,112 to 98,304.
- During Net Files page draws at 1201: lowest free 97,844, largest 77,824.
- After the restore: 132,356 free, largest 110,592.

### Restore and residue

- **Reflash:** `@NETKEY CLEAR`, then board 1 was reflashed with the real build from the main worktree (i5build "building cyd for nets/phase3 (0fed318f)", 2,125,721 B, hash verified).
- **State under the real build:**
  - `rev board no`, board 1 (its own id), `user net off (application pending)`.
  - `@NET MEMBERS` is empty, and the apps are as before.
  - A second `@NETKEY CLEAR` gave `@OK no net key was set`.
  - `@SETTINGS` is identical to the capture at the start, and `public_host` holds its original name.
  - `@AREAS` and `@USERS` no longer show `p3bench` or P3Bench.
- **Bench pieces removed:** the peer was stopped. The bench area, the bench caller and board 1's two bench files are gone.
- **Test residue, reported and not hidden:**
  - **Listings:** `files: 400 listed`. That is 200 P3Peer listings from the first run and 200 P3Peer2 listings; the boot trim cut P3Peer2 from 1002 to 200. The bench's tombstones are also still there.
  - **Cards:** 7 (was 5). The new ones are test board 081fbb2d9cd58175 and P3Peer2 3b3727361f2c21a3.
  - **Link peers:** P3Peer2 is now a link peer (queue 3), so sync rotations dial its dead address.
  - **Outlog:** it grew from 0..280 to 0..1297. The PCPEER (1291), BenchPeer (1287) and P3Peer (1079) queues hold the relayed test listings, which may go to any future link peer.
  - **Live cost:** the real board now scans these 400 listings and the tombstones at every boot trim and daily trim, and `@NET` took 1.37 s.
  - Board 1 is not a member or a Rev board under the real build.
- **Cleanup:**
  - Locally: the throwaway worktree, branch `bench/p3-testkey2` and its tracking ref were removed.
  - On the build box: the `i5build/bench_p3-testkey2` ref, its builds folder and the peer folder (with the peer's test seed) were removed.
  - The local test seeds and the bench caller password were deleted.

### Bugs and notes from the re-bench

- **I6, storing: fixed.** About 0.6 to 0.95 s a turn (peak 1.06 s) for about 8 listings, up to 1201 held, with no watchdog.
- **I6, still over budget at about 1200 held (firmware).** The kNetListingsMax of 1000 means a full board hits these regularly:
  - Net Files paging scans all of `nf_list` on every page (4 to 8 s, watchdog).
  - A replace or removal runs the store's `drop`, which rewrites `nf_list` in one turn (8 to 12 s, watchdog).
  - The daily trim took 13.9 s.
  - `@NET` and `@NET FILES` took 4 to 8 s.
- **Bug 2 (short fetches): not seen.** 23 of 23 fetches that ran were whole: 15 FETCHTEST runs, 5 caller downloads and 3 peer fetches from board 1. The build box abort counters stayed flat.
- **Minor:**
  - Each session with the peer logs `3 out` on board 1 while the peer logs `0 in`, every time.
  - Listings the peer made before its first link to board 1 were not sent to it (seen once, not traced; board 1 already had `history done`).
  - `D` by name finds only files on the page shown.
