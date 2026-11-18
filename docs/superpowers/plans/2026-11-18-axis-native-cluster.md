# AXIS Native, Plan 2: the cluster Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.
>
> **Testing override (Rev's ruling, binding):** "please do not write a million tests for this. save all the main
> testing for the end." This plan does **not** follow the method's per-task TDD pattern. Build tasks end with a
> clean build on the i5 and a commit. Every test, including the few small unit tests for pure functions, is
> written and run in the final task (Task 8).

**Goal:** The Linux `axis` program joins a CYD cluster as a follower when a card main is up, keeps a mirror of
the main's SD card (a full copy once, live follow of changes, a noon check that copies only what differs), takes
over as main only from a cardless main, and hands main back to the card board with `@HANDBACK`; with no CYD main
on the LAN it stays the solo BBS of plan 1.

**Architecture:** One `axis` binary, now built with the cluster (`AXIS_CLUSTER=1`) and a native-only switch
(`AXIS_NATIVE=1`), runs in one of three **profiles** chosen before `setup()`: Solo (plan 1's data, the cluster
muted), Follower (no card, the cluster's own flash and prefs) and MirrorMain (the mirror is its card). A profile
change is a drain and a re-exec of the program, never a live swap under `main.cpp`. The boards gain a "mirror"
card kind, a protocol version in the announcement, a card tree service and a change feed, all carried by the
existing `FileReq`/`MainReq` frames under the free request letter `I`; no new frame types.

**Tech Stack:** C++17, CMake 3.28, g++ 13 on Ubuntu 24.04 (the i5), PlatformIO espressif32@6.9.0 (Arduino plus
ESP-IDF 4.4.7) for the boards, LovyanGFX 1.3.0 from the i5's PlatformIO libdeps, doctest.

**Spec:** `docs/superpowers/specs/2026-11-17-axis-native-design.md` (Goals 2 and 5, goal 6, Non-goals, the
Calls section). Plan 1 (`docs/superpowers/plans/2026-11-17-axis-native-linux-solo.md`) built everything this
plan stands on; its tasks are not repeated.

## Questions for Rev

Each has a default the plan builds until Rev answers, so no task waits except Task 3.

1. **Flash line (blocks Task 3).** Main is 2,132,757 B, 7,757 B over the 2,125,000 B stop line, and this plan
   adds firmware (Task 3 measures how much). Trim from the candidates Task 3 lists, raise the line, or both?
2. **The card main dies and the i5 is the only AXIS left** (no cardless board to yield to it). Default built:
   after 60 s with no main the i5 restarts as the **solo** BBS (your rule: "no CYD main heard: solo"), and
   rejoins as a follower when the card board comes back. Alternative: it takes over from its mirror. One constant
   (`kNoMainAction` in `clusterhost.cpp`) switches it.
3. **`@HANDBACK` finds a file on the card board's card that changed after the takeover** (the card board ran on
   its own for a while before it rejoined). Default built: the hand-back checks every file first, and if any
   changed it **stops before writing anything**, lists them, and the i5 stays main. Alternatives: skip those
   files and hand back the rest, or overwrite them.
4. **Sysop Net while the i5 is main.** Its keys live only in a board's internal flash and never travel, so during a
   takeover Sysop Net does not run anywhere until `@HANDBACK`. OK, or should the nets wait for this to be solved?

## Calls made in this plan

- **The ranking of card kinds** (the spec says both "a board with a real card always outranks a mirror for main"
  and "the card board comes back: the i5 stays main"). Settled as this table; the first spec line holds for who is
  *given* main, the second for who *keeps* it:

  | Situation | None (no card) | Card | Mirror (the i5) |
  |---|---|---|---|
  | Main gets a Hello from a follower of this kind | yields to Card at once; to Mirror only once it has been main `kMirrorYieldMs` (90 s) with no Card follower linked | never yields | never yields (only `@HANDBACK`) |
  | Main gets CardInfo, or the 30 s re-yield | yields to Card; else, after the same 90 s, to a linked Mirror | never | never |
  | Two mains hear each other (rank, then epoch, node, address as today) | rank 0 | rank 1 | rank 2 |
  | A Discovering board's `minEpoch_` filter (today it lets a card main through) | filtered | let through | let through |
  | The i5 as a follower | | | never becomes main by itself (no discovery timeout, no failover); only a Yield from a None main, or its saved MirrorMain state |
  | The i5 just after `@HANDBACK` (state `handedback`) | | | declines every Yield and never goes solo, until it has followed a Card main once |

  A Mirror main only exists after a takeover, so rank 2 is what keeps the i5 main when the card board returns,
  even if that board booted, heard nothing and made itself main for a moment. The 90 s wait and the `handedback`
  state stop a cardless board 2 from handing main straight back to the i5 while board 1 is still booting (after
  a hand-back, or after a power cut where board 2 boots first); 90 s is longer than a board's boot, 3 s listen
  and join.
- **No new frame types.** `FrameReader` closes a link on a type it does not know (`known()` in protocol.cpp), so
  a new frame would cut old boards off. New things ride `FileReq` (follower to main) and `MainReq` (main to
  follower) under request letter `I`, which no request uses today; an old board answers `0`. The change feed is
  therefore a poll, not a push.
- **The wire stays readable to old boards.** The announcement becomes `AXIS! <ip> <epoch> <node> <sd> <ver>` and
  Hello's fourth field keeps its place; `<sd>` is `0`, `1` or `m`, and `<ver>` is `2`. Old boards test `== "1"`
  and ignore extra fields, so they read `m` as "no card".
- **Rollout order:** flash board 2, check it still clusters with board 1 on old firmware, flash board 1, then let
  the i5 cluster. Board to board, old and new behave alike (`m` never comes from a board). The i5 clusters only
  with a main that announces version 2; with an older main it stays solo and logs
  `axis: cluster: the main runs older firmware: flash it to cluster with this computer` (at most every 10 min).
- **Profiles and restarts.** Solo keeps plan 1's folders exactly (`<data>/sd`, `flash`, `prefs`), so Rev's seeded
  solo data needs no move (the spec's `solo/` name is not used). The cluster profiles share `<data>/cluster/flash`
  and `<data>/cluster/prefs`; the mirror is `<data>/mirror`. Users, settings, lists and stats live in flash on every
  board, so this keeps the solo users out of the cluster's user sync and the cluster's out of solo. Switching
  profile drains and re-execs (`execv("/proc/self/exe", argv)`), because `main.cpp` reads storage, users, settings
  and message stores once at boot.
- **Solo stays silent** without touching `main.cpp`: in the Solo profile the host's `WiFiUDP` on 2325 sends
  nothing and hands what it hears to the solo watch, and the link port 2324 neither listens nor dials, so
  `main.cpp`'s cluster (with or without a key) is a main of one that nobody hears.
- **The cluster key** lives in `<data>/cluster.key` (mode 0600) for both profiles: the host's `Preferences` maps
  namespace `clu`, key `key` there, so `@CLUSTERKEY` typed in either profile sets it and the solo watch can check
  signatures.
- **Takeover is a restart.** On a Yield the i5 declines in place, waits until its own callers are gone (at most
  120 s), saves `state=main`, and re-execs as MirrorMain, which claims main at once with `m` and a higher epoch;
  the cardless main hears a rank 2 main, checks the key and steps down (the existing probe). It declines for good
  while the mirror's first full copy is not complete.
- **Hand back by reboot, not Yield.** `@HANDBACK` copies the changed files, then sends `I E`; the card board
  restarts the way `@RESTART` does, and the i5 re-execs as a Follower in state `handedback` (it declines every
  Yield until it has followed a Card main), so the card board becomes main from a clean boot (no stale caches of
  files rewritten under it), even if cardless board 2 is main for a moment meanwhile.
- **What is mirrored:** the card tree, except files ending `.log`, temp files ending `.ax_`, and
  `System Volume Information`. Not mirrored, because they live in a board's internal flash: users and settings
  (already copied by user sync and the Settings frame), oneliners, last callers, lists, WFC stats, Net keys.
  Settings changed on the i5 during a takeover are not carried back (the card board keeps its own, as today).
- **Change detection on the boards** is one choke point: the board links with `-Wl,--wrap` on `fopen`, `open`,
  `rename`, `unlink`, `remove`, `mkdir`, `rmdir`, `truncate`, and a write under `/sd/` notes the path in a fixed
  ring of 32 paths (paths over 96 bytes, or a ring overrun, set "lost" so the i5 compares the whole tree). The
  ring records only while a Mirror follower is linked. On the i5, the takeover change log is taken the same way at
  plan 1's `fs_remap.cpp` wrappers.
- **Live follow** polls `I C` every 10 s and fetches a path once it has been quiet 15 s (LORD and the message
  base write in bursts). A gap (new feed generation, or "lost") runs a compare pass, the same pass as noon.
- **Pace:** `mirror_rate` bytes per second (default 65,536) while nobody is on a call anywhere in the cluster, a
  quarter of it while someone is; at most one request per main loop turn; pieces of 4,096 B. At that rate a
  1 GB card's first copy takes about 4.5 hours idle, so the first copy adopts files Rev pre-loads into
  `<data>/mirror` from a card copy when their size matches (Task 6), and `mirror_rate` can be raised.
- **Names:** "card mirror" everywhere for the new thing: `@CARDMIRROR`, `cardmirror.*`, `CardKind::Mirror`.
  `@MIRROR`, `MirrorJob` and the area flag `|m` already mean the SD pool's area mirror and are not touched.
- **`@HANDBACK` and `@CARDMIRROR`** are added to `boardOnlyCommand` (they run where typed, on the i5; about 25 B
  on the board). They are handled by the host through one `#if AXIS_NATIVE` line in `runCommand`.
- **At most 4 sessions** in the cluster profiles (the heartbeat and `setSlots` hold 4): the host's wrapped
  `configLoad` clamps `sessions` to 4 there; radio callers count as sessions like telnet ones. Solo keeps its own.
- **Any authenticated follower may read the card tree** (`I` read ops); only a board whose main announced `m` obeys
  `I` write ops, and only over `MainReq`.

## Global Constraints

- **Builds and tests run on the i5 only**, from inside this worktree with the work committed:
  `py -3.14 -I F:/axis-bbs/local/i5build.py test` (host tests) and `py -3.14 -I F:/axis-bbs/local/i5build.py cyd`
  (board image). The native program builds with cmake on the i5 in that same checkout:
  `cmake -S ~/builds/<branch> -B ~/builds/<branch>/build-native -DLOVYANGFX_DIR=$HOME/axis-bbs/.pio/libdeps/cyd/LovyanGFX -DAXIS_WINDOW=ON`
  then `cmake --build ~/builds/<branch>/build-native --target axis` (and once with `-DAXIS_WINDOW=OFF`).
- **Board flash:** main is 2,132,757 B against Rev's stop line of 2,125,000 B (Rev accepted the overrun for now).
  The final image must be at or under the line, or under a new line Rev sets in Task 3.
- **The board's firmware may change** (Rev: "yes change the firmware"), but `main.cpp` edits stay a few lines
  each, are listed in the task report, and sit apart from the nets session's areas (it edits `main.cpp` too).
  Host-only hooks in `main.cpp` and the core are under `#if AXIS_NATIVE` (0 on the boards, defined in
  `src/cluster/cluster.h` when unset; not a feature in `installer/features.json`), so the board pays nothing for them.
- **No new `Frame` types;** `known()` in `src/cluster/protocol.cpp` is unchanged.
- **Mixed versions:** a board on old firmware and one on new keep clustering as they do today. The i5 joins only
  a version 2 main. Flash order as in Calls.
- **Never print or commit** the cluster key, passwords, LAN addresses or board MACs, in code, docs, logs written
  to the repo, or commit messages. The `i5build.py` docstring holds an address and a MAC: never copy it.
- **Boards:** board 1 is the main with the SD card, board 2 a cardless follower. Flashing them is allowed; always
  check the MAC first (`i5build.py cyd --flash COMxx --mac ...`, the MAC from the brain, never written here), and
  coordinate with the nets session, which also uses them.
- **LAN only in a cluster:** the public port is never open in the Follower or MirrorMain profile, and closes the
  moment the solo watch hears a main. The cluster ports (UDP 2325, TCP 2324) take only LAN addresses
  (`NetGate::isLan`) and do not go through the callers' accept gate.
- **The radio line keeps working as a follower:** the LRA FLOWTRON DATAMASTER 3000 SERIAL MODEM - LFD3K-SM (the
  V3) stays a telnet-server caller (`hostnetInjectClient`), so a radio caller becomes a cluster node exactly as a
  telnet caller does. `modemline.cpp` changes only to hold off answering during a drain.
- No em dashes or double hyphens in prose, logs or user-facing text. Commits carry no AI attribution.
- No downloads, never sudo; Rev runs `install.sh` and `harden-i5.sh`. Never `pkill -f` inline over ssh.

## Review Focus

1. **A card board returns during a takeover and makes itself main before it hears the i5** (two mains). The card
   board must step down to the i5 and its card must not be written by anything but `@HANDBACK`.
   (Task 8: `two mains: a mirror main stays main over a card main`; bench check 7.)
2. **A board on old firmware in the cluster during the rollout.** The i5 must not join an old main, and must never
   be a Mirror main next to an old card main (both would think they win). (Task 8:
   `a follower that needs version 2 does not join a main without it`; bench check 2.)
3. **A Yield arrives before the first full copy finished, or while the i5 has callers.** The i5 must decline
   (stay follower) rather than serve a partial card, and must not drop its callers before 120 s.
   (Task 8: `a Yield the host declines leaves the board a follower`; bench check 6.)
4. **The link drops in the middle of a file copy or a hand-back** (a board reboots, Wi-Fi blips). No half file
   may ever appear under its real name on either side; the copy retries later; a hand-back stops and the i5 stays
   main. (Task 8: `a write shows the file only once finished, at the size given`; bench check 8.)
5. **The public port while clustered.** It must close when a main is heard (before the drain ends) and never
   open in a cluster profile, even after a restart with `public_port` set. (Bench check 3, with `ss -ltn`.)

---

### Task 1: Card kinds, protocol version and host hooks in the cluster core

**Files:**
- Modify: `src/cluster/protocol.h`, `src/cluster/protocol.cpp`, `src/cluster/cluster.h`, `src/cluster/cluster.cpp`,
  `src/bbs/sysopshell.cpp` (`boardOnlyCommand`)

**Interfaces:**
- Produces (all builds, boards included):
  - `protocol.h`: `constexpr uint32_t kProtocolVersion = 2;` `enum class CardKind : uint8_t { None, Card, Mirror };`
    `char kindChar(CardKind k);` (`'0' '1' 'm'`) `CardKind kindOf(const std::string& field);` (unknown text is None)
    `std::string sealDatagram(const std::string& key, const std::string& d);`
    `bool openDatagram(const std::string& key, std::string& d);` (today's `Cluster::sign`/`unsign` bodies, moved;
    the bytes on the wire do not change; `Cluster::sign`/`unsign` call them).
  - `Cluster`: `void setCardKind(CardKind k);` (`setHasFiles(sd)` becomes `setCardKind(sd ? Card : None)`, kept
    for `main.cpp`); `CardKind cardKind() const;` `CardKind mainKind() const;` (follower: from the announcement
    it joined; `mainHasCard()` is `mainKind() == CardKind::Card`); `uint32_t mainVersion() const;` (1 when the
    announcement had no version); `bool hasMirrorFollower() const;` (main: a linked follower said `m`).
  - Wire: `announce()` sends `AXIS! <ip> <epoch> <node> <kindChar> 2`; `sendHello()` puts `kindChar` in field 3;
    `Link` gains `CardKind kind` from Hello; `cards_` still takes only `1` (a Mirror is never a pool card);
    CardInfo is sent only by kind Card.
  - Rules, exactly the table in Calls: Hello and CardInfo yield only when `kind_ == None`; a None main with a
    Mirror follower and no Card follower sends Yield to it once it has been main `kMirrorYieldMs = 90000` with no
    Card follower linked, then every `kYieldAgainMs` (30 s), from the Main branch of `tick()`; the two-mains test
    in `handleDatagram` compares `rank(kind)` (None 0, Card 1, Mirror 2) before epoch, node and address; the
    Discovering filter `epoch < minEpoch_ && !theirSd` becomes `epoch < minEpoch_ && theirKind == None` (a
    restarted Mirror main may claim an epoch below what its followers now ask for).
  - `boardOnlyCommand` gains `"@HANDBACK"` and `"@CARDMIRROR"`.
- Produces (`#if AXIS_NATIVE` only):
  ```cpp
  struct HostHooks {
      bool     mirror = false;          // this program holds a mirror: setHasFiles(x) means CardKind::Mirror
      bool     neverMain = false;       // never main by itself: Discovering keeps asking, a lost main means discover()
      uint32_t minMainVersion = 1;      // Discovering joins only a main announcing at least this
      bool     claimMain = false;       // start(): becomeMain at once at claimEpoch (key required, else alone as today)
      uint32_t claimEpoch = 0;
      std::function<bool()> onYield;    // a Yield arrived (checked at the end of tick, where yield_ acts): true takes main now
  };
  static HostHooks defaultHost;         // copied into each new Cluster by its constructor
  void setHost(const HostHooks& h);     // the tests set one per board
  static Cluster* instance();           // the last Cluster constructed and not destroyed (main.cpp's), else nullptr
  ```

- [ ] **Step 1:** Implement the above. Keep every existing behaviour for boards with kinds None and Card (the 56
  existing cluster tests must pass unchanged in Task 8).
- [ ] **Step 2:** Build: `py -3.14 -I F:/axis-bbs/local/i5build.py test` compiles (results are read in Task 8) and
  `py -3.14 -I F:/axis-bbs/local/i5build.py cyd` builds; note the image size in the report.
- [ ] **Step 3:** Commit: `cluster: card kinds (mirror), protocol version 2, native host hooks`.

### Task 2: Card tree service and change feed on the boards

**Files:**
- Create: `src/cluster/cardtree.h`, `src/cluster/cardtree.cpp` (portable, in `AXISCORE_SOURCES`),
  `src/platform/cyd/cardwatch.cpp` (the board's `__wrap_` functions)
- Modify: `CMakeLists.pc.txt` (add `cardtree.cpp` to the core), `platformio.ini` (`build_flags`:
  `-Wl,--wrap=fopen -Wl,--wrap=open -Wl,--wrap=rename -Wl,--wrap=unlink -Wl,--wrap=remove -Wl,--wrap=mkdir
  -Wl,--wrap=rmdir -Wl,--wrap=truncate`), `src/main.cpp` (four small edits, below)

**Interfaces:**
- `cardtree.h`, namespace `axis::cluster`:
  - `constexpr size_t kTreePiece = 4096, kTreeListMax = 7 * 1024;`
  - `bool cardPathOk(const std::string& rel);` relative to the card: not empty, at most 200 bytes, no leading `/`,
    no `\`, no `:`, no byte below 0x20, no empty, `.` or `..` component, components at most 64 bytes.
  - `bool cardPathMirrored(const std::string& rel);` `cardPathOk` and not ending `.log` or `.ax_` (any case), and
    no component equal to `System Volume Information`.
  - `class CardFeed { static constexpr size_t kMax = 32, kPathMax = 96; void setOn(bool on, uint32_t gen); bool on() const;
    void note(const char* boardPath); std::string answer(uint32_t gen, uint32_t seq) const; };` `note` takes only
    `/sd/...`, strips `/sd/`, ignores paths `cardPathMirrored` refuses, moves a path already in the ring to the
    newest sequence number; `setOn(true, gen)` from off starts generation `gen` with an empty ring.
    `CardFeed& cardFeed();` one per program.
  - `class CardTreeServer { explicit CardTreeServer(std::string root); std::string handleRead(const std::string& req);
    std::string handleWrite(const std::string& req); bool finished() const; void drop(); };`
- Requests (fields split by 0x1F; replies `1` plus data, or `0`; every path checked with `cardPathMirrored`):

  | Request | Side | Reply data |
  |---|---|---|
  | `I V` | read | `2` (the tree service's version) |
  | `I D <dir> <skip>` | read | lines `f<1F><name><1F><size><1F><mtime>` and `d<1F><name>`, sorted by name, from entry `skip`; stops before `kTreeListMax`; a last line `+<1F><next skip>` when more. `<dir>` empty is the card root |
  | `I S <path>` | read and write | `<size><1F><mtime>`; `0` when missing |
  | `I R <path> <off> <len>` | read | the bytes, `len` at most `kTreePiece`; the file stays open for the next piece |
  | `I C <gen> <seq>` | read | `<gen><1F><seq><1F><lost>` then `\n` and one changed path per line after the asker's `seq`; `lost` 1 when `gen` differs, the ring overran past `seq`, or a path was too long |
  | `I O <path>` | write | opens `<path>.ax_` truncated, making parent folders |
  | `I P <off> <data>` | write | appends `data` (last field, raw) when `off` equals the bytes written so far |
  | `I F <size> <mtime>` | write | closes, checks `size`, renames over `<path>` and sets its time to `mtime`; replies `<size><1F><mtime>` as the card now reports them; a wrong size deletes the temp and answers `0` |
  | `I X <path>` | write | removes it (missing counts as done) |
  | `I E` | write | ends a hand-back: `finished()` turns true |

- `main.cpp` edits (each one or two lines, listed in the report):
  1. `cluster->onRequest`: before the file server fallthrough, `I` requests go to a static
     `CardTreeServer cardTree(storageRoot())` `handleRead`, or `0` when `!storageHasSd()`.
  2. `cluster->onMainRequest`: `I` requests go to `cardTree.handleWrite` only when `storageHasSd()` and
     `cluster->mainKind() == CardKind::Mirror`, else `0`; when `cardTree.finished()`, the board restarts from
     `loop()` (not inside the request) by the same path `@RESTART` takes (`netSaveNow()` with the nets, then
     `ESP.restart()`), about 500 ms later so the reply leaves first. `onMainGone` also calls `cardTree.drop()`.
  3. Next to `cluster->tick(millis())`: `cardFeed().setOn(cluster->role() == Role::Main && cluster->hasMirrorFollower(), hwRandom())`.
  4. `#if AXIS_NATIVE`: at the end of `runCommand`'s chain, `else if (nativeCommand(cmd, out)) {}` with
     `bool nativeCommand(const String& cmd, Print& out);` declared above it (defined in Task 7), and
     `void nativeRun(const char* cmd)` defined after `runCommand`: runs one @ command with its output thrown
     away (the host's drain uses `@BCAST`, which reaches every session; a `Cluster::send` broadcast skips the
     session holding the sender's node number).
- `cardwatch.cpp`: `extern "C"` `__wrap_fopen` (notes when the mode has `w`, `a` or `+`), `__wrap_open` (when
  flags have `O_WRONLY`, `O_RDWR`, `O_CREAT` or `O_TRUNC`), `__wrap_rename` (both paths), `__wrap_unlink`,
  `__wrap_remove`, `__wrap_mkdir`, `__wrap_rmdir`, `__wrap_truncate`; each calls `__real_` first and notes only
  on success and only while `cardFeed().on()`.

- [ ] **Step 1:** Implement the above.
- [ ] **Step 2:** Build tests and board image as in Task 1 Step 2; note the size.
- [ ] **Step 3:** Commit: `cluster: card tree service and change feed for the card mirror`.

### Task 3: Measure the firmware cost and get the image under the line

**Files:**
- Modify: per Rev's pick (candidates below); nothing else.

**Interfaces:**
- Produces: a size table in the task report: main's image (2,132,757 B), after Task 1, after Task 2, and the
  line (2,125,000 B).

- [ ] **Step 1:** `py -3.14 -I F:/axis-bbs/local/i5build.py cyd --fetch`, then `python -I tools/buildcost.py measure`
  on the fetched build, and the 40 largest symbols of our own code from the map (`xtensa-esp32-elf-nm --size-sort`
  on the i5). Write the table and the candidates with their measured savings:
  - `CONFIG_NEWLIB_NANO_FORMAT=y` in `sdkconfig.defaults` (printf from ROM; `%lld`, `%llu` and `%f` stop working,
    so every such use in `src/` must be listed first);
  - bench-only @ commands behind a bench build flag like `env:cyd-trace` (`@SPRITEFAIL`, `@HEAPWIN`, `@TAP`,
    `@LKEY`, `@LSCREEN`, `@SHOT`, `@DOORSTATE`);
  - the `@MIRROR` handler and its text in a build without `AXIS_SD_POOL` (the default image, where it can only
    say "not in this build");
  - a release preset with a feature off (`installer/features.json`, for example `AXIS_DOSPROMPT`);
  - raising the stop line.
- [ ] **Step 2:** Ask Rev to pick (Question 1). **Stop here until he answers.**
- [ ] **Step 3:** Apply his pick; rebuild until `cyd` is at or under the line he set.
- [ ] **Step 4:** Commit: `board: flash trim for the card mirror` (or no commit if he raised the line only).

### Task 4: Profiles, data folders, the key file and restarts on Linux

**Files:**
- Create: `src/platform/linux/clusterhost.h`, `src/platform/linux/clusterhost.cpp` (the state file and the boot
  decision in this task; Task 5 and 6 add to it)
- Modify: `src/platform/linux/paths.h`, `paths.cpp`, `preferences.cpp`, `storage.cpp`, `main_linux.cpp`,
  `hostnet.cpp` (its `configLoad` wrap: the sessions clamp), `CMakeLists.pc.txt`

**Interfaces:**
- `paths.h`: `enum class Profile { Solo, Follower, MirrorMain }; void axisSetProfile(Profile p); Profile axisProfile();`
  set once, before `setup()`. Mapping:

  | Board path | Solo | Follower | MirrorMain |
  |---|---|---|---|
  | `/sd...` | `<data>/sd...` | `""` (fails ENOENT, as a cardless board) | `<data>/mirror...` |
  | `/littlefs...` | `<data>/flash...` | `<data>/cluster/flash...` | `<data>/cluster/flash...` |
  | prefs namespaces | `<data>/prefs/` | `<data>/cluster/prefs/` | `<data>/cluster/prefs/` |

  `axisDataSetup` also creates `cluster/flash`, `cluster/prefs` and `mirror` (0700).
- `preferences.cpp`: namespace `clu`, key `key` reads and writes `<data>/cluster.key` (0600, the key alone, no
  newline) in every profile; never logged.
- `storage.cpp`: `storageHasSd()` is true in Solo and MirrorMain, false in Follower (it already follows the folder).
- `configLoad` wrap: in Follower and MirrorMain, `sessions` above 4 loads as 4.
- `clusterhost.h`:
  ```cpp
  enum class Standing { Follower, Main, HandedBack };
  struct ClusterState { Standing state = Standing::Follower; uint32_t epoch = 0; long since = 0; };  // <data>/cluster/state: state=follower|main|handedback, epoch=, since=
  bool        clusterStateLoad(ClusterState& s);
  bool        clusterStateSave(const ClusterState& s);   // temp file + rename
  std::string clusterKeyRead();                          // "" when unset
  // Before setup(): state main (and a key) is MirrorMain; state handedback is Follower; else listen on UDP 2325 up
  // to 5000 ms for an announcement that opens with the key: version 2 is Follower, older logs the "older firmware"
  // line; nothing heard is Solo.
  Profile     decideProfile(uint32_t listenMs = 5000);
  void        clusterHostStart(Profile p);               // sets Cluster::defaultHost for the profile (Task 5)
  void        clusterHostPoll(uint32_t nowMs);           // from turn(), after loop() (Tasks 5 to 7)
  ```
- `main_linux.cpp`: `decideProfile`, `axisSetProfile`, `clusterHostStart` before `setup()`; `turn()` calls
  `clusterHostPoll(millis())`. The current `shutdown()` becomes `stopEverything()` (hang up callers and the
  modem, restore the terminal) used by both `shutdown()` and
  `[[noreturn]] void hostRestart(const char* why);` which logs `axis: restarting: <why>`, then
  `execv("/proc/self/exe", argv)` with the original arguments, and `_exit(3)` if exec fails (systemd restarts on
  3). Every socket and file the host opens is `O_CLOEXEC`.
- `CMakeLists.pc.txt`: `axiscore_solo` becomes `axiscore_native` with `AXIS_CLUSTER=1 AXIS_NATIVE=1` (`AXIS_SD_POOL`
  stays unset, so 0); `axis` gets `AXIS_CLUSTER=1 AXIS_NATIVE=1`, `src/platform/cyd/clusternet.cpp` and the new
  Linux files; `axiscore` (the tests' core) gets `AXIS_NATIVE=1` too.

- [ ] **Step 1:** Implement; build `axis` on the i5 with window ON and OFF, no errors.
- [ ] **Step 2:** Smoke: with no board on the LAN, `axis --headless --data /tmp/axis-t4` logs
  `axis: profile solo` after about 5 s and plan 1's telnet login still works. (Not a test.)
- [ ] **Step 3:** Commit: `native: profiles, cluster data folders, key file, restart`.

### Task 5: The cluster network, the solo watch and the drain

**Files:**
- Modify: `src/platform/linux/arduino/WiFiUdp.h`, `src/platform/linux/hostnet.cpp`, `hostnet.h`,
  `src/platform/linux/clusterhost.cpp`, `src/platform/linux/modemline.cpp`, `modemline.h`,
  `installer/linux/harden-i5.sh` (TCP 2324 from the LAN), `installer/linux/README.md`
- Reuse unchanged: `src/platform/cyd/clusternet.cpp`

**Interfaces:**
- `WiFiUDP` becomes real: `begin(port)` binds a non-blocking UDP socket on `INADDR_ANY:port` with `SO_BROADCAST`
  and `SO_REUSEADDR`; `beginPacket/write/endPacket` send one datagram; `parsePacket/read/remoteIP` take one,
  dropping any from a non-LAN address (`NetGate::isLan`). In the Solo profile, port 2325 sends nothing and its
  datagrams go to `soloWatchHeard(const std::string& d, uint32_t fromIp)` instead of `parsePacket`.
- `WiFiServer` on `kLinkPort` (2324): LAN addresses only, no accept gate, no radio queue, no public port; in Solo
  it does not listen. `WiFiClient::connect` to port 2324 fails at once in Solo.
- `hostnet.h`: `void hostnetClosePublic();` (closes the public listener now; it never reopens in this run)
  `void hostnetHold(bool on);` (on: new telnet callers get `This BBS is joining its cluster. Please call back in a few minutes.\r\n` and are closed).
  In Follower and MirrorMain, `public_port` is ignored and logged once: `axis: net: public port off while clustered`.
- `modemline.h`: `void modemLineHold(bool on);` (on: a `RING` is not answered).
- `clusterhost.cpp`, Solo: `soloWatchHeard` opens the datagram with `clusterKeyRead()` and parses
  `AXIS! ip epoch node sd [ver]`. A version 2 main starts the drain: `hostnetClosePublic()`, `hostnetHold(true)`,
  `modemLineHold(true)`, and every minute a broadcast to solo callers through
  `nativeRun("@BCAST This BBS joins its cluster in N minutes. Please finish up.")`;
  when no caller is left, or after `drain_minutes` (axis.conf, default 10), `hostRestart("joining the cluster")`.
  If that main is not heard for 60 s, the drain is cancelled (hold off; the public port stays closed until the next start).
- `clusterhost.cpp`, `defaultHost` per profile: Follower `{mirror=true, neverMain=true, minMainVersion=2, onYield=takeoverWanted}`;
  MirrorMain `{mirror=true, claimMain=true, claimEpoch=state.epoch}`; Solo none.
- Follower watchdog: when `role()` has not been Follower for `no_main_seconds` (axis.conf, default 60), do
  `kNoMainAction` (Question 2; default: clear nothing, `hostRestart("no main on the LAN: solo")` with the next
  `decideProfile` then finding Solo). Not in state `handedback` (Calls table); that state turns into `follower`
  the first time `mainKind() == CardKind::Card`, and `decideProfile` gives Follower for it even when nothing is heard.
- First join: `<data>/cluster/flash` starts empty, so the board is not set up until the main's Settings frame
  arrives (`main.cpp`'s `onSettings` `!wasSetUp` path sets it up and leaves setup mode). In the cluster profiles
  `main_linux.cpp` does not print `kNotSetUp`; in terminal mode `clusterHostPoll` checks `configLoad().setupDone`
  every 2 s until true, then injects `WFC\r` once.

- [ ] **Step 1:** Implement; build window ON and OFF.
- [ ] **Step 2:** Commit: `native: cluster network, solo watch, drain into the cluster`.

### Task 6: The card mirror: first copy, live follow, noon check, takeover

**Files:**
- Create: `src/platform/linux/cardmirror.h`, `cardmirror.cpp` (pure C++17 and POSIX, no Arduino and no host
  globals, like `netgate.cpp`), `src/platform/linux/mirrorhost.cpp` (the driver)
- Modify: `src/platform/linux/clusterhost.cpp`, `CMakeLists.pc.txt` (`cardmirror.cpp` into `axis`, and into
  `axis_tests` with `tests/test_cardmirror.cpp` inside the existing `if(UNIX)` block next to `test_netgate.cpp`)

**Interfaces:**
- `cardmirror.h`:
  ```cpp
  struct CardEntry { std::string path; uint64_t size = 0; int64_t mtime = 0; };
  struct Manifest  { std::map<std::string, CardEntry> files; uint32_t gen = 0, seq = 0; bool complete = false; std::string noon; };  // noon: "YYYY-MM-DD" of the last noon check (other compare passes leave it)
  std::string formatManifest(const Manifest& m);     // "#gen=", "#seq=", "#complete=", "#noon=" lines, then path<1F>size<1F>mtime
  bool        parseManifest(const std::string& text, Manifest& out);
  struct MirrorDiff { std::vector<CardEntry> copy; std::vector<std::string> remove; };
  MirrorDiff  diffListing(const Manifest& m, const std::vector<CardEntry>& card);  // copy: missing or size/mtime differ; remove: in m, not on the card
  bool        noonDue(const std::string& lastNoon, const std::string& today, int nowMinutes, int noonMinutes);  // today > lastNoon and now >= noon
  class Pace { public: Pace(uint32_t bytesPerSec); void setBusy(bool someoneOn); bool take(uint32_t bytes, uint32_t nowMs); };  // busy: a quarter of the rate
  ```
- Files on the i5: `<data>/mirror.manifest` (written temp + rename, at most every 5 s and at the end of a pass).
- `mirrorhost.cpp` (`void mirrorPoll(uint32_t nowMs)` from `clusterHostPoll`, Follower only, never inside a
  request; one request per turn, all through `Cluster::instance()->request(req, reply, 4000)`):
  - Ready when role is Follower, `mainVersion() >= 2` and `I V` answered `2`.
  - **First copy:** manifest missing or `complete=0`: walk the tree with `I D`, copy every file, then
    `complete=1`. A restart resumes (files already in the manifest at the same size and time are skipped). A file
    already in `<data>/mirror` at the main's size but not in the manifest is adopted without copying (recorded
    with the main's size and time), so Rev can pre-load the mirror from a card copy; the first copy only.
  - **Noon:** the noon check is the compare pass below run when `noonDue(manifest.noon, ...)`; only it writes
    `manifest.noon`.
  - **Copy one file:** `I S`, pieces with `I R` into `<data>/mirror/<path>.ax_`, `I S` again; unchanged: rename,
    record in the manifest; changed: drop the temp and retry later (at most 3 times a pass). Before a copy,
    `statvfs` on the mirror must leave 64 MiB after it, else stop with `axis: mirror: disk full, copy stopped`.
  - **Live follow:** `I C <gen> <seq>` every 10 s; new paths wait until quiet 15 s, then are copied (or removed
    from the mirror when `I S` says missing). `lost=1` or a new `gen` runs a compare pass.
  - **Compare pass** (noon, a gap, `@CARDMIRROR CHECK`): walk with `I D`, `diffListing`, apply; log
    `axis: mirror: check: copied N, removed M, matched K` or `axis: mirror: check: matches (K files)`. Noon from
    `noon` (axis.conf, `HH:MM`, default `12:00`, local time), via `noonDue`; skipped while MirrorMain.
  - **Pace:** `Pace(mirror_rate)` (axis.conf, default 65536); busy when any `nodes()` entry has a handle; a
    request counts its reply size, at least 256 B.
- `takeoverWanted()` (the Follower `onYield`): returns false always. In state `handedback` it does nothing else
  (logs `axis: mirror: not taking main: just handed it back`). Otherwise, if the manifest is `complete=1`, it starts a
  takeover: `modemLineHold(true)`, `hostnetHold(true)`, then once no caller is on this program (at most
  `takeover_wait_seconds`, default 120), saves `state main, epoch = instance()->epoch() + 1, since = now`, copies
  `mirror.manifest` to `mirror.takeover` (the card's state at takeover, for Question 3), and
  `hostRestart("taking over as main from the mirror")`. Incomplete: logs `axis: mirror: not taking main: the first copy is not finished`.

- [ ] **Step 1:** Implement; build window ON and OFF.
- [ ] **Step 2:** Commit: `native: card mirror with live follow, noon check and takeover`.

### Task 7: The takeover change log, `@HANDBACK` and `@CARDMIRROR`

**Files:**
- Create: `src/platform/linux/handback.cpp`, `src/platform/linux/nativecmd.cpp`
- Modify: `src/platform/linux/fs_remap.cpp`, `src/platform/linux/clusterhost.cpp`, `installer/linux/README.md`

**Interfaces:**
- `fs_remap.cpp`: in MirrorMain, each successful write-type call on a `/sd/...` path (the same set as
  `cardwatch.cpp`) calls `void takeoverNote(const char* boardPath);`, which keeps a set in memory and appends new
  `cardPathMirrored` paths to `<data>/mirror.changes` (one per line, loaded back at start).
- `handback.cpp` (`void handbackPoll(uint32_t nowMs)` from `clusterHostPoll`, MirrorMain only):
  - Target: the one live card board in `Cluster::instance()->cardBoards()` (or the node named in `@HANDBACK GO <node>`).
  - **Check first:** `I S` on the target for every path in `mirror.changes`, compared with `mirror.takeover`; any
    difference stops it (Question 3 default), listed in the reply and the log; nothing written.
  - **Copy:** each path through `requestTo(board, ...)` (`I O`, `I P` pieces, `I F size mtime`, or `I X` when the
    mirror no longer has it), paced like the mirror; any failed request stops with
    `axis: handback stopped at <path>: <why>`, the i5 stays main, `mirror.changes` is kept.
  - Up to 3 passes, each over the paths noted since the last one began; paths noted after the final pass began
    are copied into `<data>/handback-left/` and listed.
  - **Finish:** `I E` to the target, then the manifest updated with each `I F` reply, `mirror.changes` cleared,
    state saved as `handedback`, `hostRestart("main handed back")`.
- `nativecmd.cpp`: `bool nativeCommand(const String& cmd, Print& out);` handles, and returns true for:
  - `@CARDMIRROR`: profile, state, `complete`, files and bytes in the manifest, last check, pending live-follow
    paths, last error; `@CARDMIRROR CHECK` starts a compare pass now (Follower).
  - `@HANDBACK`: what would be copied (paths, bytes, target, callers online); `@HANDBACK GO [node]` starts it,
    refused outside MirrorMain or with no live card board; `@HANDBACK STOP` stops between files.
  - Anything else returns false.

- [ ] **Step 1:** Implement; build window ON and OFF.
- [ ] **Step 2:** Commit: `native: takeover change log, @HANDBACK and @CARDMIRROR`.

### Task 8: Testing (all of it, at the end)

Run on the i5 and the bench. Capture a PNG for each visible step and send them to Rev (his standing request).
Never put a LAN address, MAC or key in a screenshot that goes into the repo.

**Files:**
- Modify: `tests/test_cluster.cpp`
- Create: `tests/test_cardtree.cpp` (in the core test list), `tests/test_cardmirror.cpp` (Linux block)

- [ ] **Step 1: Write the small unit tests.** On the existing `FakeLan` (per-board `setHost`):
  - `a cardless main hands main to a mirror follower only after 90 s with no card follower`: None main A, Mirror B
    with `onYield` returning true; at 89 s of A being main B is still Follower; after 91 s B is Main, A is
    Follower, and A's last heard announcement ends `m 2`.
  - `a cardless main that hears a card board inside the 90 s hands main to the card board, not the mirror`.
  - `a cardless main hands main to the card follower, not the mirror one`: A None main, B Mirror, C Card; C ends Main.
  - `a card main never hands main to a mirror follower`: A Card main, B Mirror; after 40 s of ticks A is Main.
  - `a mirror main keeps main when a card board joins`: B claims main (`claimMain`), A Card joins; A is Follower
    and B sent no Yield.
  - `two mains: a mirror main stays main over a card main`: both main from the start; after the probe A (Card) is
    Follower of B (Mirror).
  - `a follower that may never be main keeps looking when its main is lost`: B `neverMain` Follower of A; A powered
    off; after 60 s B is Discovering and has sent no `AXIS!`.
  - `a follower that needs version 2 does not join a main without it`: a datagram `AXIS! <ip> 5 1 1` sealed with
    `sealDatagram`; B (`minMainVersion=2`) stays Discovering and dials nobody.
  - `a Yield the host declines leaves the board a follower`: `onYield` false; B stays Follower.
  - `the announcement says kind and version, and an old reader sees m as no card`: fields 3 and 4 are `m` and `2`;
    `field == "1"` is false.
  - `test_cardtree.cpp` (on a temp folder): `a directory listing comes in sorted pages under 7 KB, with size and time`;
    `paths that leave the card are refused` (`../x`, `/etc/passwd`, `a/../../b`, `a\b`, empty, a 0x01 byte: all `0`);
    `logs, temp files and System Volume Information are never listed or served`;
    `a read piece is exactly the bytes asked for and never more than 4096`;
    `a write shows the file only once finished, at the size given` (before `F` only `x.ax_` exists; a wrong size
    leaves neither); `the change feed lists each changed path once after a sequence number` (note a, b, a: after
    seq 0 the reply is b then a);
    `the change feed says lost after 33 paths, for another generation, and for a path over 96 bytes`;
    `the change feed notes nothing while off, and nothing outside /sd/`.
  - `test_cardmirror.cpp`: `a manifest survives format and parse`;
    `the diff copies what is missing or different and removes what the card no longer has` (equal entries give an
    empty diff); `the noon check is due once a day at or after noon` (`noonDue("2026-11-17","2026-11-18",719,720)`
    false, `noonDue("2026-11-17","2026-11-18",720,720)` true, `("2026-11-18","2026-11-18",900,720)` false);
    `the pace gives the full rate when idle and a quarter when someone is on`.
- [ ] **Step 2: Host tests.** `py -3.14 -I F:/axis-bbs/local/i5build.py test`: every test passes (the 1150 before
  this plan plus the new ones).
- [ ] **Step 3: Board image.** `py -3.14 -I F:/axis-bbs/local/i5build.py cyd`: at or under the line from Task 3.
- [ ] **Step 4: Native builds.** `axis` builds with `-DAXIS_WINDOW=ON` and `OFF` in `~/builds/<branch>/build-native`.
- [ ] **Bench.** Tell the nets session first and agree a slot; check each board's MAC before every flash.
  - **Check 1, rollout:** flash board 2; it still clusters with board 1 on old firmware (logins, a post). Flash
    board 1; both cluster as before.
  - **Check 2, old main:** with board 1 still old (before its flash), start the i5: it stays solo and logs the
    "older firmware" line once.
  - **Check 3, solo to follower:** i5 solo with `public_port` set and one telnet caller; boot board 1. The public
    port is gone from `ss -ltn` within 5 s; the caller gets the broadcast; after they log off the i5 re-execs and
    shows as a node on board 1's WFC. On this first join (empty `cluster/flash`) the i5 sets itself up from the
    main's settings and the terminal WFC opens by itself; a window shows the WFC, not the setup screens. After a
    restart of `axis`, the public port still does not open.
  - **Check 4, first copy and live follow:** the first copy completes (`@CARDMIRROR` says `complete`); a post
    and an upload made on board 1 are in `<data>/mirror` within 60 s; a post by a telnet caller on the i5 lands
    on board 1's card.
  - **Check 5, noon and gaps:** stop the i5, upload a file on board 1, start the i5: the join's compare copies
    only that file. `@CARDMIRROR CHECK` then says `matches`. Force noon by setting `noon` to two minutes ahead:
    one check runs, once.
  - **Check 6, radio as a follower:** a call from the V4 through the V3 on the i5 connects as a cluster node
    (`RADIO SFn` on the WFC), walks a menu, hangs up. One or two calls only.
  - **Check 7, takeover:** power board 1 off; board 2 becomes main, yields to the i5, the i5 re-execs as main
    (`@CARDMIRROR` says MirrorMain), board 2 joins it; a caller posts. Power board 1 on: it joins as a follower,
    the i5 stays main (`@CARDS` on the i5 lists board 1's card). Repeat with board 1 starting while the i5 is
    briefly unreachable, so it makes itself main: it steps down to the i5.
  - **Check 8, hand-back (board 2 powered and on the cluster throughout):** `@HANDBACK` lists the takeover's
    changes; `@HANDBACK GO`; board 1 restarts and is main, board 2 and the i5 are followers, the i5 has not taken
    main back from board 2 meanwhile, and the post from check 7 is on board 1. Once, pull board 1's power mid
    copy: the i5 stays main, `@HANDBACK GO` again completes.
  - **Check 9, no main at all:** with every board off, the i5 does what Rev answered for Question 2.
  - **Check 10, solo is still plan 1:** with every board off, a telnet login, the public port open (`ss -ltn`),
    and one radio call, as in plan 1's Task 9.
- [ ] **Brain:** update `D:\brain\Projects\AXIS Native.md` (plan 2 status, profiles, folders, commands, axis.conf
  keys `noon`, `mirror_rate`, `drain_minutes`, `no_main_seconds`, `takeover_wait_seconds`) and the cluster part
  of `D:\brain\Projects\AXIS BBS Firmware.md` (card kind `m`, protocol version 2, `I` requests, the flash result).
- [ ] **Commit:** `tests: card mirror, card kinds and the card tree` (the brain is outside the repo). Report the
  results to Rev with the screenshots.
