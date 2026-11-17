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
