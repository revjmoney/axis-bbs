# AXIS Nets Phase 2 (User Net, New Board Voting, Net Record Browser) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Boards can opt into User Net, get in by a signed sysop vote that every board tallies the same way (with Rev's admit, deny and veto on top), and carry four caller areas; every sysop can browse, filter and search the permanent net record and check each entry's signature.

**Architecture:** Phase 1's portable core in `src/net/` grows two record kinds (application `A`, vote `V`), two notice groups (member, votes) and a `NetMembers` tally that is a pure function of the signed records held, so arrival order never changes a result. `NetCore` learns a second area family (`un_*`) gated by membership and the sysop's carry list. `src/main.cpp` adds settings, commands and caller areas; `SysopShell` gets a User Net page and a record browser. Nothing changes on the wire framing (`AXISNET 2`); v0.53 boards keep syncing Sysop Net.

**Tech Stack:** C++17, Arduino-ESP32 2.0.17 on ESP-IDF 4.4.7 via PlatformIO (env `cyd`), Monocypher 4.0.2 (already vendored), doctest host tests via CMake/Ninja (`build/axis_tests`).

**Spec:** `docs/superpowers/specs/2026-11-11-axis-sysop-user-net-design.md` (approved by Rev 2026-11-11), sections "Two nets", "Joining User Net", "The net record", "Moderation". Read it, `docs/NETS.md` and `docs/superpowers/plans/2026-11-11-axis-nets-phase1.md` first.

**Later phases (their own plans):** Phase 3 Net Files, Phase 4 radio sync. Neither fits in flash after this phase until the flash work lands (see Flash and RAM).

## Flash and RAM (measured 2026-11-15, before this plan)

- App slot 2,359,296 B (`partitions.csv`, app0 0x240000). main d21b77a (v0.53 "Everything"): **2,282,912 B, 96.8%, 76,384 B free**. Static DRAM 48,960 B.
- Phase 1 cost about 114 KB of flash (on 92.3% vs off 87.5%). Its named symbols add up to about 73 KB, so strings, templates and inlined code add about half again on top of what `nm` shows.
- Phase 2 estimate, scaled the same way from like code (NUV 3.6 KB, the Sysop Net page 1.2 KB, one record kind about 2 KB, `NetState` 7.7 KB): **30 to 45 KB, likely about 38 KB**, ending near **98.4%** with about 37 KB free.
- Phase 3 (about 28 to 34 KB) and Phase 4 (about 12 to 16 KB) together need more than what would be left. They wait for the flash work (session "AXIS memory and flash").
- Heap, worst case with the caps below: about 7 KB for applications and votes plus up to 19 KB for 64 member seals (Rev-made only, like Phase 1's 64+64 notices; an outsider cannot inflate them). Typical: under 2 KB. The history pull peak stays at Phase 1's size (Task 3 keeps 800 posts total). Static RAM: under 0.5 KB.

## Global Constraints

**Where the work happens**
- Branch `nets/phase2` off main d21b77a (already made). Do not push until Rev says so.

**Flash budget and stop line**
- Budget for the phase: 45 KB. Every build task records the `cyd` image size in its commit body.
- **Stop line:** if a `cyd` image passes **2,330,000 B (98.8%)**, stop before the next task and tell Rev the numbers. Do not trim other features to make room; that is the flash session's work.
- Everything new compiles out with `-DAXIS_NETS=0`, like Phase 1.

**Memory**
- Net work never starts a piece while the largest free block is under `kNetHeapFloor` (40 KB), as in Phase 1. No frame over `kNetMaxFrame` (9 KB). No TLS.
- Caps: `kMaxOpenApps = 16` and `kMaxVotes = 256` (all applications together) in `src/net/members.h`; `kMaxMemberNotices = 64` in `src/net/state.h`. Full signed application and vote lines live on the storage root in `/net/apps.txt`, never all in RAM: RAM holds only the compact fields below.

**Security**
- Signed only, never encrypted. Every application, vote and notice is verified before it is stored, counted or passed on.
- A vote counts only from a board that is a member by the tally, and only its latest vote (highest vote seq) per applicant application.
- Rev's admit, deny and veto beat any count. A ban or veto removes membership at once.
- Never commit any private key, Rev's hostname, the cluster key, a password, a LAN address or a board MAC.

**Clocks**
- The tally never reads a clock. The 30-day wait before re-applying is enforced only when the clock is set.

**Compatibility with v0.53 boards in the field**
- They NAK `A` and `V` E lines and posts to `un_` areas as permanent `bad`, and ignore unknown kinds in snapshot (S before SEND) and history (H) lines. That is fine: they neither store nor relay User Net. Rev's hub must run Phase 2.
- Never send an `A` or `V` record as an S line after SEND (a v0.53 board fails the session on that).

**Rev's style**
- Each build task ends with a successful `cyd` build and host build on the i5, then a commit.
- All tests are written and run in the last task.

**Build commands** (on the i5, never this PC; commit first, only committed work is built)
- Firmware: `py -3.14 -I F:/axis-bbs/local/i5build.py cyd`
- Host tests: `py -3.14 -I F:/axis-bbs/local/i5build.py test`
- Nets off: in the branch's i5 checkout (`~/builds/nets_phase2`), `PLATFORMIO_BUILD_FLAGS=-DAXIS_NETS=0 ~/pio-venv/bin/pio run -e cyd`.

**Commits**
- Short conventional messages. No Co-Authored-By, no AI attribution. `git add` named files only.

**Hardware**
- Flash a board only after reading its MAC on that port (board MACs are in the brain note, not here). Never touch COM9, COM11 or COM15.

**Prose**
- No em dashes or double hyphens in prose written for Rev (docs, screens, commit messages).

## Review Focus

1. **The tally depends on arrival order.** The same set of applications, votes and notices must give the same members on every board whatever order they came in, including a vote that arrives before its application and a voter admitted after it voted. Test: `tally is the same in any order`.
2. **A voter loses membership (vetoed or banned).** Their votes stop counting at once, and a board they voted in that Rev has not sealed may fall back to pending. Test: `removed voter's votes stop counting`.
3. **Rev's veto after a board was voted in.** The board leaves User Net, its new posts are refused with `member`, and a later Rev admit (higher seq) brings it back. Test: `veto after admit removes, later admit restores`.
4. **A v0.53 peer.** A session that meets a record kind it does not know answers NAK `bad` and goes on, and A and V records go out only as E or H lines. Test: `unknown record kind is NAKed and the session goes on`.
5. **A local caller blocked from the nets, or a twit.** Their post to a `sn_` or `un_` area is refused before it is signed; local areas still work. Test: `no-net caller cannot post to net areas`.

---

### Task 1: Application and vote records, member and votes notices

**Files:**
- Modify: `src/net/record.h`, `src/net/record.cpp`, `src/net/state.h`, `src/net/state.cpp`.

**Interfaces (produces):**
- `NetKind::App = 'A'`, `NetKind::Vote = 'V'`.
- `struct NetApp { std::string board, pub, name, sysop, location, mac, blurb; uint32_t seq = 0, date = 0; std::string sig; };`
  - `pub` lets a board check an application before it holds the applicant's card. `boardId(pub) == board` is required.
  - Limits: name, sysop and location 36 bytes, `blurb` 240 bytes, mac `xx:xx:xx:xx:xx:xx` or empty. `seq` is the applicant's application counter (starts at 1).
- `struct NetVote { std::string voter, app; uint32_t appSeq = 0, seq = 0, date = 0; bool yes = false; std::string comment; std::string sig; };`
  - `app` is the applicant's board id. `seq` is the voter's vote counter; a higher one replaces a lower one for the same `(voter, app, appSeq)`. `comment` at most 72 bytes.
- `signedPart`, `encode`, `decode`, `signApp`/`verifyApp` (against its own `pub`), `signVote`/`verifyVote(const NetVote&, const std::string& pubHex)`, the same canonical form as Phase 1 (0x1F fields, kind letter first, `\n` and `\\` escaped).
- `std::string appId(const NetApp&)` returns `<board>-a<seq>`.
- New notice kinds in `groupOf`:
  - `admit`, `deny`, `veto` with a 16 hex board target go to group **member** (`"member"`). `admit` and `deny` carry the application seq in `value` (decimal); `veto` has an empty value. Highest seq per target wins, as for other groups.
  - `votes` with target `net` and value `yes=<1..20>,no=<1..20>` goes to group **votes** (`"votes"`).
- `NetState`: `uint32_t votesYes() const; uint32_t votesNo() const;` (defaults 3 and 3); `const NetNotice* memberNotice(const std::string& board) const;`. The member group is capped at `kMaxMemberNotices` (64): the oldest applied deny goes first, then the oldest admit; a veto is kept longest.

- [ ] **Step 1: Implement** as above. Reuse `cleanLine` and the Phase 1 field checks; a decode that fails any limit returns false.
- [ ] **Step 2: Build** firmware and host on the i5.
- [ ] **Step 3: Commit** `feat(net): application and vote records, member and votes notices` with the image size in the body.

### Task 2: NetMembers, the tally

**Files:**
- Create: `src/net/members.h`, `src/net/members.cpp`.

**Interfaces:**
- Consumes: `NetApp`, `NetVote`, `NetState` (cards, bans, member notices, `votesYes/No`), `isRevBoard` (passed in as a function so the class stays independent of `NetCore`).
- Produces: `class NetMembers`:
  - `enum class Status { None, Pending, Member, Denied, Vetoed };`
  - `NetMembers(const NetState& st, std::function<bool(const std::string& board)> isRev)`
  - `bool takeApp(const NetApp& a, std::string& line)`: verifies; keeps only the latest seq per board; refuses a board already a member or vetoed, and one with a newer app held. `line` gets the encoded record for the caller to store. False if nothing changed.
  - `bool takeVote(const NetVote& v, const std::string& voterPub, std::string& line)`: verifies against the voter's pub; keeps the highest `seq` per `(voter, app, appSeq)`. A vote for an app not held yet is kept (it may come first) and counts once the app arrives. A voter may not vote on itself.
  - `Status status(const std::string& board) const;` and `struct Tally { uint32_t yes, no; };` `Tally tally(const std::string& board) const;` (counting only member voters).
  - `std::vector<std::string> members() const;` (board ids, Rev's boards first).
  - `std::vector<std::string> waitingFor(const std::string& voter) const;` applicants still pending that this voter has not voted on, oldest app seq first.
  - `std::vector<std::pair<std::string, Status>> sealsDue() const;` on Rev's board only: applicants whose tally says Member or Denied but who have no member notice for that app seq yet. The host makes and floods `admit` or `deny` for each (auto seal).
  - `std::string save() const; void load(const std::string& text);` the full signed lines, one per line, for `/net/apps.txt`. RAM holds only `(board, seq, date)` per app and `(voter u64, app u64, appSeq, seq, yes)` per vote; `load` reads them back from the lines.
  - Caps `kMaxOpenApps = 16`, `kMaxVotes = 256`. A full app table drops the oldest app that is not pending; when all are pending a new one is refused. A full vote table drops votes for decided apps first, then refuses.
- The rule, written into `status` (one function, no order dependence):

```
1. banned (NetState)                         -> None
2. Rev's board                               -> Member
3. member notice for the board:
     veto                                    -> Vetoed
     admit (any app seq)                     -> Member
     deny for its latest app seq             -> Denied
     deny for an older seq                   -> fall through (it applied again)
4. M = Rev's boards + boards admitted by Rev, minus banned and vetoed
   repeat until M stops growing:
     for each held, not denied applicant b outside M:
       yes = latest yes votes on b's latest app from voters in M (voter != b)
       if yes >= votesYes: add b to M
5. b in M                                    -> Member
   no votes (same counting) >= votesNo       -> Denied
   app held                                  -> Pending
   otherwise                                 -> None
```

Yes wins when both counts are reached; it keeps step 4 growing only, so the loop ends and the result is the same in any order. Rev's board seals every result it sees (`sealsDue`), so a result stays put even if a voter later changes or loses membership.

- [ ] **Step 1: Implement** as above.
- [ ] **Step 2: Build** both.
- [ ] **Step 3: Commit** `feat(net): User Net membership tally` with the image size.

### Task 3: User Net areas in NetCore and the session

**Files:**
- Modify: `src/net/areas.h`, `src/net/areas.cpp`, `src/net/session.h`, `src/net/session.cpp`, `src/main.cpp` (the three `"sn_"` checks at `CountingMsgs::post`, `NoNetMsgs::post` and `NetStore::post`, which become `axis::net::isNetAreaId`).

**Interfaces:**
- Consumes: `NetMembers` (Task 2).
- Produces:
  - `const std::vector<NetArea>& userNetAreas();` exact list, all `revOnly` false:
    - `un_chat` "User Net: General Chat"
    - `un_tech` "User Net: Tech Help"
    - `un_games` "User Net: Games"
    - `un_files` "User Net: Files"
  - `const NetArea* netArea(const std::string& id);` finds either family. `isNetAreaId` is true for `sn_` and `un_`. `bool isUserNetArea(const std::string& id);`.
  - `NetCore` owns a `NetMembers` (`members()` accessor) and `void setUserAreas(std::vector<std::string> carried);` (empty: User Net off).
  - `take` adds, after the signature check: for an `un_` area, NAK `area` (`kNakArea`, permanent) when this board does not carry it or is not a member itself, and NAK `member` (`kNakMember`, **transient**: `netNakTransient` returns true for it, and the 3-strike rule of `nocard` applies) when the posting board is not a member by this board's tally.
  - `originate` refuses an `un_` area unless this board is a member and carries it.
  - `NetCore::takeApp(const NetApp&, const std::string& fromPeer)` and `takeVote(const NetVote&, const std::string& fromPeer)`: through `NetMembers`, append the line to `/net/apps.txt` via a host callback `setAppsSave(std::function<void(const std::string& text)>)`, log `A <board> <exclude>` or `V <app> <voter> <exclude>`, and note the record (`app <board> <seq>`, `vote <voter> on <app> yes|no`, then `member <board> admitted|denied` when the status changes).
  - `resolve` reads `A` and `V` entries back from the members table; an entry whose record is gone is skipped.
  - History: the walk covers every carried net area, with at most **800 posts in all**, shared evenly across the carried areas (200 each with Sysop Net alone, 100 each with all eight). After the posts it sends every held application and vote as H lines. `kSyncHistoryMax` stays 800 for posts; A and V H lines are counted apart and capped at `kMaxOpenApps + kMaxVotes`.
  - The session takes A and V on E and H lines (`takeEntry`, `takeHistory`). It never puts them on S lines.
  - `NetPostingStore` covers `un_` areas the same way as `sn_` (held, killed and hidden posts stay hidden).

- [ ] **Step 1: Implement.**
- [ ] **Step 2: Build** both.
- [ ] **Step 3: Commit** `feat(net): User Net areas, applications and votes on the wire` with the image size.

### Task 4: Firmware wiring (settings, caller areas, commands, auto seal)

**Files:**
- Modify: `src/main.cpp`, `src/bbs/config.h`, `src/bbs/config.cpp`, `src/bbs/users.h`, `src/bbs/users.cpp`, `src/bbs/node.cpp` (user editor key).

**Settings** (written by `kv` like `net_sysop`):
- `net_user` (bool, default 0): this board wants User Net.
- `net_user_areas` (comma list of `un_` ids, default all four).
- `net_user_level` (int, default 20): the lowest security that may read and post in User Net areas.

**Caller areas**
- `nodeCfg()` adds the carried `un_` areas, at `net_user_level`, after the Sysop Net ones, under a group headed "User Net", only while this board is a member (status Member) and `net_user` is on.
- Followers: the main's settings frame gains `net_un=<comma list>` beside `net_up=1` (the areas callers get right now; empty when none). A follower adds those areas; an old main never sends it, so a follower then shows none.

**Who may post to the nets**
- `User` gains `bool permNets = true;`, saved as `nonet=1` only when false. `@PERM <handle> nets on|off` sets it; the user editor gets key `n` (shown as "Net posts YES/no").
- `bool mayPostToNets(const bbs::User* u)` in `src/net/areas.h` (false for a null user, `permNets` off or `twit` on). `NetStore::post` (main) looks up `m.from` and refuses, before signing, a post to any net area when it is false. Followers' posts reach the same store through the main, so they are covered.

**Commands** (in `netCommand`, main only, the follower forwards as Phase 1 does):
- `@NETAPPLY <blurb>`: builds and signs this board's application (name, sysop, location from the `net_*` settings, MAC from `WiFi.macAddress()`), takes and floods it. Refused while `net_user` is 0, while already a member or pending, and with the clock set within 30 days of a deny of this board.
- `@NETVOTE <board id> Y|N [comment]`: only while this board is a member; signs with the board key and a vote seq kept in `/netseq.txt` as a third number (`netSeqStart` rules).
- `@NET APPS`: one line per held application: board id, name, sysop, location, MAC, status, `yes/no` counts, and this board's own vote if any.
- `@NET MEMBERS`: member board ids with names (from cards when held), Rev's boards marked.
- Rev's board only: `@NETADMIT <board>`, `@NETDENY <board>`, `@NETVETO <board> YES` (without `YES` it explains), `@NETVOTES yes=<n>,no=<n>`. Each makes, signs and applies a notice with `usedSeq()+1`, as Phase 1's `@NETBAN` does.

**Auto seal** (Rev's board only): after any app, vote or notice is taken, `sealsDue()` is turned into `admit` or `deny` notices, at most 4 per loop pass, under the heap floor.

**Persistence:** `/net/apps.txt` on the storage root, written when it changes, at most every 30 s, like `state.txt`.

- [ ] **Step 1: Implement.**
- [ ] **Step 2: Build** both, and the nets-off build once. Record all three sizes in the commit body.
- [ ] **Step 3: Commit** `feat(net): User Net on the main board, applying and voting`.

### Task 5: Sysop menus: User Net page and the net record browser

**Files:**
- Modify: `src/bbs/sysopshell.cpp`, `src/bbs/sysopshell.h`, `src/main.cpp` (record commands).
- Create: `src/net/recordview.h`, `src/net/recordview.cpp`.

**Interfaces:**
- `enum class RecordKind { All, Votes, Members, Actions };` Votes: `what` starts with `app `, `vote `, or `notice admit|deny|veto|votes`. Members: `card ` or `member `. Actions: everything else.
- `bool recordMatches(const std::string& line, RecordKind k, const std::string& text);` text is matched without case against the head and the signed line.
- `std::string recordCheck(const std::string& signedLine, const NetState& st, const NetMembers& m);` one of: `signed by <board>, checks`, `signed by <board>, does NOT check`, `signed by <board>, no card held to check it`, `local action, not signed`. Posts check against the held card, cards against their own pub, notices against Rev's keys, apps against their own pub, votes against the voter's card.

**Commands:**
- `@NET RECORD [n [skip]]` stays as it is.
- `@NET RECORD FIND <text> [n [skip]]`, `@NET RECORD KIND votes|members|actions [n [skip]]`: newest first across every month file, at most 100 lines, reading each file in pieces of 512 bytes from its end (never a whole file in RAM).
- `@NET RECORD MONTHS`: the month files with their sizes.
- `@NET RECORD SHOW <skip>`: one line in full: date, who, what, the decoded record's fields one per line, and `recordCheck`.

**Sysop menu**
- System > Sysop Net keeps its rows. "Traffic and logs" opens the browser: rows All, Votes, Members, Actions, Search (asks for text), Months. Each list shows 20 lines with Older and Newer rows; picking a line opens SHOW.
- System > **User Net** (after Sysop Net):
  - Status: member, pending (with the counts), denied, vetoed, or off.
  - `User Net (1 on, 0 off)`, `Areas carried` (toggle each of the four), `Caller level`.
  - `Apply` (asks for the blurb) while not a member.
  - **New Board Voting** (members only): one row per waiting applicant (name, location, counts); picking one shows the application, the votes and comments so far, then Yes, No, and a comment prompt.
  - Members.
  - Rev's board only: Admit, Deny, Veto, and Vote counts rows.

- [ ] **Step 1: Implement.**
- [ ] **Step 2: Build** both.
- [ ] **Step 3: Commit** `feat(net): User Net page and net record browser` with the image size.

### Task 6: PC peer, docs and features

**Files:**
- Modify: `tools/net/netpeer.cpp` (`axis_netpeer`): commands `apply <blurb>`, `vote <board> y|n [comment]`, `apps`, `members`, and `user on|off` (carries all four areas).
- Modify: `docs/NETS.md` (User Net, how to apply and vote, the counts, Rev's admit, deny and veto, auto seal, the new settings and commands, the record browser, `/net/apps.txt`, the `area` and `member` NAK words), `docs/FEATURES.md` (one dated entry).

- [ ] **Step 1: Implement and write.**
- [ ] **Step 2: Build** both.
- [ ] **Step 3: Commit** `feat(net): PC peer User Net commands and docs`.

### Task 7: All tests and the board bench

**Files:**
- Create: `tests/test_net_members.cpp`, `tests/test_net_usernet.cpp`, `tests/test_net_recordview.cpp`. Add them to `axis_tests` in `CMakeLists.pc.txt`. Extend `tests/test_net_record.cpp` and `tests/test_net_session.cpp`.

**Host tests, with the assertions to make:**
- `app and vote round trip`: encode then decode gives equal fields; a 241-byte blurb, a 73-byte comment, a bad MAC and an app whose `boardId(pub) != board` each fail.
- `tampered app or vote fails verify`.
- `member and votes notices group`: admit/deny/veto on a board id are group `member`; `votes yes=4,no=2` sets `votesYes()==4`, `votesNo()==2`; `votes yes=0,no=3` is rejected.
- `tally is the same in any order` (Review Focus 1): Rev board R, boards A B C D; votes R,A,B yes on C; R admits A and B; then every permutation of those 5 records (120) gives C Member and D None.
- `vote before its app counts once the app comes`.
- `second vote replaces the first`: A votes yes seq 1 then no seq 2 on C: tally yes 0, no 1; replaying seq 1 changes nothing.
- `yes wins when both counts are reached`.
- `removed voter's votes stop counting` (Review Focus 2): with votesYes 2, A and B vote C in; veto A: C is Pending again (not sealed); with a seal (admit C) C stays Member.
- `veto after admit removes, later admit restores` (Review Focus 3): and C's new post is NAKed `member` while vetoed.
- `deny then a new app reopens`: deny for app seq 1, then app seq 2 is Pending.
- `bootstrap with Rev alone`: votesYes 1, Rev votes yes, C becomes Member; with votesYes 3 Rev's admit is needed.
- `seals due on Rev's board`: after C reaches Member by votes, `sealsDue()` lists C Member; after the admit notice it is empty.
- `caps hold`: 17th open app refused while all 16 are pending; vote 257 refused when no decided app frees room.
- `un_ post rules`: a non-carrier NAKs `area`; a non-member origin is NAKed `member` and retried next session, passed over after 3.
- `history shares 800 posts across carried areas, then apps and votes`.
- `unknown record kind is NAKed and the session goes on` (Review Focus 4): feed `E 0 Z...`: NAK `0 bad`, the session is not failed; A and V never appear on S lines after SEND.
- `no-net caller cannot post to net areas` (Review Focus 5): `mayPostToNets` is false for `permNets` off, for a twit and for no user, true otherwise; a `NetPostingStore` test double that applies it as main's `NetStore` does refuses `sn_` and `un_` posts and still saves a local one.
- `record filters and search`: kinds and text match as specified; `recordCheck` gives `checks` for a good post, `does NOT check` after one changed byte, `no card held` without the card.
- `users keep nonet`: `nonet=1` round trips; absent means true.

**Bench (hardware, batched at the end, a screenshot or saved log per step, no keys, LAN addresses or MACs in anything saved to the repo).** Ask Rev before any busy test.
1. Build and flash board 1 (MAC check first). Board 2 follows normally (flash it too if it is on the same build line, MAC checked).
2. Run `build/axis_netpeer listen 2400` on the PC with `user on`; point board 1's `net_first_peer` at it over serial.
3. Board 1: `@SET net_user=1`, `@NETAPPLY test board`. The PC peer's `apps` shows it Pending. Screenshot of System > User Net on the CYD showing Pending.
4. The PC peer applies; board 1 (not a member) is refused `@NETVOTE`; the User Net page shows no voting row.
5. **Needs Rev's key, Rev's call:** Rev types `@NETKEY SET` on board 1 himself (never by Claude), then `@NETADMIT` for the PC peer from the User Net page; the PC peer's `members` lists it. A caller on board 2 posts in User Net: General Chat; it reaches the PC peer. `@NETVETO` the PC peer: its next post is NAKed `member`. `@NETKEY CLEAR`. If Rev skips this step, the host tests cover it.
6. Record browser: Votes list, Search for the test board's id, SHOW on an app line says `checks`. Screenshot.
7. `@LOG`, `@HEAPWIN` and `@NET`: no crash, largest block during sync at or above 40 KB.
8. Restore: `net_user=0` and clear `net_first_peer`. The test application stays in board 1's record and `/net/apps.txt` (the record is append only); it shows as None once `net_user` is off.

- [ ] **Step 1: Write all tests.** `py -3.14 -I F:/axis-bbs/local/i5build.py test`: every test passes, old and new.
- [ ] **Step 2: Run the bench** as above.
- [ ] **Step 3: Commit** `test(net): User Net, voting and record browser tests`.
- [ ] **Step 4: Update** `D:\brain\Projects\AXIS BBS Firmware.md` and `docs/FEATURES.md` with what shipped and the final image size.
