# AXIS Nets Phase 3 (Net Files) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** An upload to a file area the sysop shares on User Net sends a small signed listing across the net. Callers on other member boards see it in a "Net Files" list and download it: their board connects to the home board and streams the file straight through to them with the normal download protocols, and nothing is stored on the way.

**Architecture:**
- The portable core in `src/net/` grows two record kinds:
  - listing `L`, which travels like a post;
  - fetch request `Q`, which never travels and is only used on a fetch call.
- Listings live in a hidden message area `nf_list`, reusing the message store, the outbound log, dedupe and Rev's kill and hold.
- A fetch is its own short call (`AXISFILE 1` on the telnet port) run by two pure state machines, `FetchServer` and `FetchClient`, in `src/net/fetch.*`.
- The download protocols gain a "not ready yet" hook, so they can read from a socket through a small replay window instead of from a file.
- `src/main.cpp` hosts the calls. `FileBrowser` gets a Net Files pseudo-area.
- Followers reach it all through the main over the existing cluster file protocol.

**Tech Stack:** C++17, Arduino-ESP32 2.0.17 on ESP-IDF 4.4.7 via PlatformIO (env `cyd`), Monocypher 4.0.2 (already vendored), doctest host tests via CMake/Ninja (`build/axis_tests`).

**Spec:** `docs/superpowers/specs/2026-11-11-axis-sysop-user-net-design.md`, the sections "Net Files", "Net Files reachability", "Transport security", "The net record" and "Constraints". Before starting, read it, `docs/NETS.md` and `docs/superpowers/plans/2026-11-15-axis-nets-phase2.md`.

**Base:** branch `nets/phase3`, cut from `nets/phase2` at 24f5ae5. Phase 2 is not merged or pushed yet. If Rev merges Phase 2 to main first, rebase this branch onto main before Task 1.

## Decisions for Rev (the spec says otherwise, or says nothing)

Each line is the plan's default. Rev's yes or no on the plan settles them.

1. **Relay through Rev's board for home boards that only dial out: moves to Phase 4.**
   - The spec wants these boards' files fetched through Rev's board. That relay pairs a caller's fetch with the home board's own outbound call to Rev's board. It is a third call kind and about 8 KB of flash.
   - In Phase 3, a home board with no host on its card shows "that board can't be reached right now", and its listings say so in the list.
   - Phase 4 adds radio legs and the relay together, since both change how a home board is reached.
2. **Files already in an area when the sysop first shares it are not listed by themselves.**
   - `@NET LISTALL <area>` lists them once, with the sysop's handle as the uploader, because no uploader is stored for old files.
   - New uploads are listed as they happen.
3. **"Counts as a download on the home board".**
   - No download counter exists on a board (`FileEntry` has none, and only uploads are counted per day).
   - The home board writes a `fetch served` entry to its net record and its event log, with the file, the caller's handle and board.
   - A per-file counter would be new work in the file library. It is not in this plan.
4. **Cluster followers: included (Task 6).**
   - A caller on a follower sees the Net Files list and downloads through the main, as the spec says.
   - The main runs the fetch and hands the bytes on over the cluster link. Nothing is stored on either board.
5. **Board 1 must be a User Net member for the bench.**
   - Listings and fetches are gated on membership, like User Net posts.
   - Before Task 8's bench, Rev types `@NETKEY SET` on his board and admits board 1 and the PC test peer (`@NETADMIT`).
   - Without that, the bench can only show the refusals.

## Flash and RAM (measured 2026-11-16, before this plan)

**Flash**
- App slot 2,359,296 B.
- `nets/phase2` at 9cf2102 (same code as 24f5ae5): pio 2,065,333 B, 87.5%.
- Nets off: 1,892,337 B, 80.2%.
- About 294 KB is free.
- **Phase 3 estimate: 32 to 45 KB, likely about 38 KB**, ending near 89.2%.
  - The estimate is scaled from Phase 2's like code: one record kind is about 2 KB; the record browser page, the session and pull changes, and the transfer job are similar sizes.
  - Task 6 (followers) is about 4 KB of that.
- Phase 4 (radio sync and the relay, about 20 to 25 KB) fits after it.

**Heap on board 1**
- Board 1 idle: 135,276 B free, largest block 110,592 B, lowest since boot 84,312 B.
- Phase 2's bench, largest block during a sync: 106 KB.

**New heap**
- The fetch client's replay window is 8 KB, plus one socket.
- The fetch server reads in 2 KB chunks.
- One fetch in and one fetch out at a time.
- Listings are never held in RAM: the hidden area and the outbound log hold them.

**Not measured yet:** the largest block during a download with a sync running. That is bench step 7. Any busy test is asked of Rev first.

## Global Constraints

**Where the work happens**
- Branch `nets/phase3` (made, see Base). Do not push until Rev says so.

**Flash budget and stop line**
- Budget for the phase: 45 KB. Every build task records the `cyd` image size (pio "Flash used") in its commit body.
- **Stop line:** if pio passes **2,115,000 B (89.6%)**, stop before the next task and tell Rev the numbers. Do not trim other features to make room.
- Everything new compiles out with `-DAXIS_NETS=0`. The fetch pieces also compile out when `AXIS_FILES` or `AXIS_TRANSFERS` is 0.

**Memory**
- No new net work starts while the largest free block is under `kNetHeapFloor` (40 KB).
- A fetch client starts only with at least `kNetHeapFloor + kFetchWindow` free in one block.
- No frame over `kNetMaxFrame` (9 KB). No TLS.

**Caps (exact values)**

| Constant | Value | Meaning |
| --- | --- | --- |
| `kFetchWindow` | 8 * 1024 | the client's replay window |
| `kFetchChunk` | 2048 | the server's read size |
| `kFetchStallMs` | 20000 | no bytes for this long ends a fetch |
| `kFetchConnectMs` | 15000 | connect plus handshake |
| `kFetchRetries` | 3 | re-requests at a new offset per download |
| `kNetServeMax` | 1 | fetches served at once |
| `kNetListingsMax` | 1000 | live listings held; 100 when the board has no SD card |
| `kNetListingsPerBoard` | 200 | live listings from one home board |
| `kNetListDesc` | 120 | description bytes |
| `kNetGoneKeepDays` | 120 | how long a removal (tombstone) is kept, the spec's dedupe window |

- A cap is met by evicting only that home board's own oldest listing. Past the total cap, the oldest listing of the board holding the most is evicted. A board's own listings are never evicted.

**Security**
- Signed only, never encrypted.
- A listing is checked against its home board's card, exactly as a post is.
- The fetch call is mutual:
  - The home board signs both nonces with the key on its card.
  - The caller's board then signs its request with both nonces, the listing id, the caller's handle and the start offset. A request cannot be replayed.
- A home board serves only:
  - its own live listing;
  - from an area it still shares;
  - a file whose size equals the listing's;
  - to a board that is a User Net member by its tally, not banned and not blocked here.
- Never commit any private key, Rev's hostname, the cluster key, a password, a LAN address or a board MAC.

**Compatibility**
- Phase 2 and v0.53 boards answer an unknown E kind with `NAK bad` and ignore unknown U kinds. Still, `L` lines go only to peers whose card has the token `net3` in its links. Phase 3 cards carry `internet,net2,net3`.
- `L` is never sent as an S or H line.

**Rev's style**
- Each build task ends with a successful `cyd` build and host build on the i5, then a commit with no AI attribution trailer.
- All tests are written and run in the last task.

**Build commands** (on the i5, never this PC; commit first, only committed work is built)
- Firmware: `py -3.14 -I F:/axis-bbs/local/i5build.py cyd`
- Host tests: `py -3.14 -I F:/axis-bbs/local/i5build.py test`
- Nets off: in the branch's i5 checkout, `PLATFORMIO_BUILD_FLAGS=-DAXIS_NETS=0 PLATFORMIO_BUILD_DIR=$HOME/builds/<dir>_nonets_pio ~/pio-venv/bin/pio run -e cyd`.
- After merging or rebasing onto main, move the i5 checkout's stale `sdkconfig.cyd` aside before building.

## Review Focus

1. **A removal that arrives before its listing, or a listing replayed after its removal, must stay removed.** A tombstone for (home board, area, name) with seq N refuses any listing for that name with seq below N, in any arrival order (Task 8: all 6 orders of listing, re-listing, removal).
2. **A caller's download is cut off partway** (the home board drops, the caller hangs up, the receiver asks to resume or seeks back past the window). The fetch call closes, the transfer ends with a clear message, the fetch slot frees and nothing leaks. A seek outside the window becomes a new request at that offset, at most `kFetchRetries` times (Task 3).
3. **The file at home changes after its listing went out** (deleted, replaced with another size, or its area unshared). The home board answers `NO gone` or `NO size`, and the caller sees "that file is no longer there" before any protocol starts (Tasks 3 and 4).
4. **One member floods listings** (a huge shared area, or a hostile board). The per-board cap, the total cap and the daily ingress limit hold. Other boards' listings are never evicted for it (Task 2).
5. **The download gate.**
   - The caller's ratio and permission check uses the listing's size before any call is made.
   - A caller on a radio link over the size limit sees the estimate and decides.
   - An all-internet trip gets no warning (Task 5).

---

### Task 1: Listing and fetch records

**Files:**
- Modify: `src/net/record.h`, `src/net/record.cpp`
- Modify: `src/net/recordview.cpp` (show and match the new kind in the record browser)

**Interfaces:**
- Produces, in `record.h`:

```cpp
constexpr size_t kNetListDesc = 120;  // a listing's description, before escaping
enum class NetKind : char { Post='P', Card='C', Notice='N', App='A', Vote='V', Listing='L', Fetch='Q' };

// A file a member board offers. id "<board>-<seq>" from the board's post seq counter (the seen window
// dedupes it with its posts). gone: a removal of every listing of (board, area, name) with a lower seq.
// reach: how the home board is reached, "i", "r" or "ir" (Phase 3 writes "i" when its card has a host).
struct NetListing {
    std::string board, area, name, desc, uploader, reach;
    uint32_t seq = 0, size = 0, crc = 0, date = 0;
    bool gone = false;
    std::string sig;
};
// A caller's board's request on a fetch call. Never stored or passed on.
struct NetFetch {
    std::string board, listing, handle, dialNonce, servNonce;  // listing: the listing id
    uint32_t offset = 0;
    std::string sig;
};
std::string listingId(const NetListing& l);  // "<board>-<seq>"
// signedPart / encode / decode / sign* / verify* overloads for both, as for the other kinds:
bool signListing(NetListing& l, const KeyPair& k);
bool verifyListing(const NetListing& l, const std::string& pubHex);
bool signFetch(NetFetch& f, const KeyPair& k);
bool verifyFetch(const NetFetch& f, const std::string& pubHex);
```

**Field rules:**
- `area` is a plain token of at most 16 characters.
- `name` passes `bbs::safeFileName`.
- `uploader` has at most 30 characters, with no control bytes.
- `desc` is at most `kNetListDesc`, single line, with control bytes replaced by `?` (`netSafe`).
- A `gone` listing has `size` 0, `crc` 0 and an empty `desc`.
- `handle` is at most 30 characters. Nonces are 16 lowercase hex characters.
- `crc` is the CRC-32 of the whole file (`src/net/crc.h`).

- [ ] **Step 1:** Add the structs and functions above, mirroring `NetApp`'s encode and decode style (fields joined by 0x1F, kind letter first, lowercase hex).
- [ ] **Step 2:** In `recordview.cpp`, `recordMatches` puts `L` under Actions. `recordFields` shows name, size, area, uploader, reach and gone. `recordCheck` checks a listing against the home board's card when it is held.
- [ ] **Step 3:** Run the cyd and host builds on the i5. Both must succeed.
- [ ] **Step 4:** Commit `feat(net): listing and fetch records` with the image size in the body.

### Task 2: NetCore listings

**Files:**
- Modify: `src/net/areas.h`, `src/net/areas.cpp`, `src/net/session.h`, `src/net/session.cpp`, `src/net/state.h` if `killed` or `held` lookups need the listing id (they take any id today)
- Modify: `src/main.cpp:2770` (card links `internet,net2,net3`)

**Interfaces:**
- Consumes: Task 1's `NetListing`.
- Produces, on `NetCore`:

```cpp
constexpr const char* kNetFilesArea = "nf_list";  // hidden message area; never in a caller's area list
// A listing from a peer, or made here (fromPeer ""). Same order of checks as take(NetPost):
// valid, banned, blocked, card and signature, this board carries un_files and is a member, the home
// board is a member (NAK area / member), killed, dedupe, tombstone, ingress (line size), store.
Took takeListing(const NetListing& l, const std::string& fromPeer, bool history = false, const char** nak = nullptr);
// This board's own listing for a file in a shared area, or its removal (gone). Signs with this board's
// key and the next post seq. Returns the id, or "" when this board is not a member carrying un_files.
std::string originateListing(NetListing l);
// Live listings, newest first, from skip on, at most max; each gives the stored line. Skips held, killed,
// banned-home and tombstoned ones. Returns the next skip, or -1 at the end.
int listingsPage(int skip, int max, const std::function<void(const NetListing&)>& each);
bool findListing(const std::string& id, NetListing& out);  // a live one only
```

**Store layout**
- Each listing or tombstone is one `Message` in `nf_list`:
  - `subject` = name, `from` = uploader, `to` = home board id, `date` as for posts, empty body, `net` = the encoded line.
- Taking a `gone` listing:
  - stores the tombstone;
  - then drops (one `store_.drop` pass) every live listing of the same (board, area, name) with a lower seq.
- A live listing is refused as `Duplicate` (ACKed) when a tombstone for its (board, area, name) has a higher seq.
- The daily trim drops tombstones older than `kNetGoneKeepDays` (with the clock set; by count without one), then applies the caps in Global Constraints.

**Outbound log and sync**
- New entry `L <msgnum> <id> <exclude>`. `resolve` reads it back from `nf_list` like a `P` entry, and skips it for a peer without `net3` (no read).
- Session:
  - Add `peerNet3_` from the card's links.
  - `takeEntry` routes `L` to `takeListing`, sharing the `kSyncUserEMax` per-session cap with `A` and `V`.
  - `takeUser` takes `L` lines (counted in `userRecsTaken_`, and they set `userGot_` when stored or held).
  - The U pull sends every live listing, then tombstones younger than 120 days, after the applications and votes and before the posts. This only happens when the peer is `net3` and asked for `un_files`.
- `kNakArea` and `kNakMember` are reused, so no new NAK word is needed.

- [ ] **Step 1:** Implement the API, the store layout, the log entry, `resolve`, the trim and caps, and the session changes above.
- [ ] **Step 2:** Check that `nf_list` never shows to callers. It must not be in `cfg.messageAreas` or in any newscan or menu list, and `isNetAreaId("nf_list")` stays false. Grep each message-area list builder in `node.cpp` and `main.cpp`, and note what you found in the report.
- [ ] **Step 3:** Run the cyd and host builds. Both must succeed.
- [ ] **Step 4:** Commit `feat(net): Net Files listings travel` with the image size in the body.

### Task 3: Fetch calls and the protocol ready hook

**Files:**
- Create: `src/net/fetch.h`, `src/net/fetch.cpp` (add to the host CMake list and `platformio.ini` source filter, the same way `members.cpp` was added)
- Modify: `src/net/transfer.h`, `src/net/zmodem.h/.cpp`, `src/net/xmodem.h/.cpp`
- Modify: `src/bbs/transferjob.h/.cpp` (a constructor taking an already-open `std::unique_ptr<RandomFile>`, `size` and `name` for a download)

**The wire** (each line ends with `\r\n`; after `OK` the bytes are raw):

```
dialer:  AXISFILE 1 <dialNonce16>                       (fits axisServe's 40-char first line)
server:  HI <serverBoard16> <servNonce16> <sig128>      sig over "axisfile|<dialNonce>|<servNonce>|<serverBoard>"
         or NO busy
dialer:  GET <encoded NetFetch>
server:  OK <bytes from offset to end>  then the bytes, then it closes
         or NO <word>   word: busy gone size member banned sig bad
```

**Interfaces:**

```cpp
// transfer.h: optional; when set, a sender asks before each block and, if false, sends nothing this
// step (it tries again next step). Its own receiver timers do not run while it waits on data.
using ReadyAt = std::function<bool(uint32_t offset, size_t n)>;
// ZmodemSender and XmodemSender get a setReady(ReadyAt) member.

// fetch.h
struct ServeFile {  // what the host found for a request
    bool ok = false; const char* no = nullptr; uint32_t size = 0;
    std::function<bool(uint32_t off, uint8_t* buf, size_t n)> read;
};
class FetchServer {  // one inbound fetch call; pure: the host feeds bytes and sends takeOut()
public:
    FetchServer(const std::string& myBoard, const KeyPair& key, const std::string& servNonce16,
                std::function<ServeFile(const NetFetch&)> check);  // check verifies the request against the state
    void feed(const char* p, size_t n);
    std::string takeOut(size_t room);  // at most room bytes; streams kFetchChunk reads while serving
    void tick(uint32_t nowMs);
    bool finished() const; bool failed() const; const char* why() const;
};
class FetchClient {  // one outbound fetch call
public:
    FetchClient(const NetListing& l, const std::string& homePub, const std::string& handle, const KeyPair& me,
                const std::string& myBoard, const std::string& dialNonce16, uint32_t offset);
    void feed(const char* p, size_t n);  // bytes stay in the kFetchWindow ring once streaming
    std::string takeOut();
    void tick(uint32_t nowMs);
    enum class St { Hello, Asked, Streaming, Done, Failed };
    St state() const; const char* why() const;  // why: the server's NO word, or "stall", "sig", "size"
    bool ready(uint32_t off, size_t n) const;   // [off, off+n) is in the window (or past the end of the file)
    bool read(uint32_t off, uint8_t* buf, size_t n);  // copies from the window; false if not in it
    bool behind(uint32_t off) const;             // off is before the window or beyond what has arrived + window
};
```

**The ring**
- The client keeps the newest `kFetchWindow` bytes received.
- It stops reading the socket while the ring is full and the sender has not consumed the oldest bytes. TCP then holds the home board back.
- `OK <n>` must equal `size - offset` of the listing, or the call fails `size`.

**The `RandomFile` the host builds over a `FetchClient`**
- `readAt` serves from the ring.
- When ZMODEM's `ZRPOS` lands outside the window (`behind`), the host closes the call and starts a new one with `offset` = that position, at most `kFetchRetries` times. The sender waits on `ReadyAt` meanwhile.
- XMODEM never seeks back past the current block, which is always in the window.

- [ ] **Step 1:** Add `ReadyAt` to both senders. While it is false, each sender's step returns at once and its own response timers do not start. `TransferJob`'s stall check is replaced, for a fetch-backed download, by `kFetchStallMs` in the client.
- [ ] **Step 2:** Implement `FetchServer` and `FetchClient` to the wire above.
- [ ] **Step 3:** Add the `TransferJob` constructor for an open `RandomFile`.
- [ ] **Step 4:** Run the cyd and host builds. Both must succeed.
- [ ] **Step 5:** Commit `feat(net): fetch calls and a ready hook for downloads` with the image size in the body.

### Task 4: Host side on the board

**Files:**
- Modify: `src/main.cpp`, `src/bbs/config.h`, `src/bbs/config.cpp`

**Settings** (in Network Setup and the config file, compiled out with nets):
- `net_files_areas`: comma list of local file area ids shared on User Net. Empty by default.
- `net_files_radio_kb`: the size limit for the radio warning, default 64.

**Hooks:**
- **Upload.** In `serviceJob` at the `countUpload()` site (main.cpp ~6759), for a good upload into a shared area: `originateListing` with:
  - name, size, crc (one read pass over the file in `kFetchChunk` pieces, done before the listing goes out), `req.desc`, `node->handle()`;
  - reach `"i"` when this board's card has a host.
- **Removal.** Wrap the caller-facing file library in `NetSharingFiles : bbs::FileLibrary`, the way `NetPostingStore` wraps the message store. Its `remove` and `rename` in a shared area originate a `gone` listing for the old name after the inner call succeeds; a rename also lists the new name. Everything else passes through. `callerFiles()` returns it on the main when nets are on.
- **Serving.** `axisServe` (main.cpp ~4333) checks `AXISFILE 1 ` before `AXISNET 2 `. It runs a `FetchServer` in a new `NetSlot`-like `serve` slot. That slot is separate from `in` and `out`, so a sync never blocks a fetch and a fetch never blocks a sync.
  - It refuses `NO busy` when:
    - the slot is taken (`kNetServeMax`);
    - `!netHeapOk()`;
    - the board is a follower;
    - the IP is backed off (failed proofs back off as for sync).
  - Its `check` callback:
    - looks the listing up with `findListing`;
    - checks it is this board's own, the area is still in `net_files_areas`, and `callerFiles().open()` gives the listed size;
    - checks the requester's card, signature, ban, block and membership.
  - It writes `fetch served <id> to <handle>@<board>` to the net record and the event log.
- **Fetching.** A `fetch` slot runs one `FetchClient` at a time for a local caller:
  - It dials the home board's card host:port through the same non-blocking DNS and connect path as `netDial`.
  - It writes `fetch <id> for <handle>` to the net record.
- **`NetFetchFile : RandomFile`** over the client, as Task 3 describes. `size()` is the listing size.
- **Commands** (serial and sysop, under `@NET`):
  - `@NET FILES [skip]` lists live listings as `<id> <home name> <area> <name> <size>`.
  - `@NET LISTALL <area>` lists every file already in a shared area once (Decision 2).
  - `@NET FETCHTEST <id>` fetches a listing to nowhere and prints `@OK <bytes> crc ok|bad`, or `@ERR <why>`. This is for the bench.
- `@NET` status adds `files: <n> listed, <m> shared areas`.

- [ ] **Step 1:** Implement the settings, the hooks, both slots, the file classes and the commands above.
- [ ] **Step 2:** Run the cyd, host and nets-off builds. All three must succeed.
- [ ] **Step 3:** Commit `feat(net): share, serve and fetch Net Files on the board` with the image size in the body.

### Task 5: The caller's Net Files list

**Files:**
- Modify: `src/bbs/filebrowser.h/.cpp`, `src/bbs/node.h/.cpp`, `src/main.cpp` (wiring)

**Interfaces:**

```cpp
// filebrowser.h: the host's view of Net Files, set by Node when nets are on and the caller may read User Net.
struct NetFilesView {
    std::function<int(int skip, int max, std::vector<FileEntry>& out, std::vector<std::string>& ids,
                      std::vector<std::string>& homes)> page;   // returns next skip or -1
    std::function<std::string(const std::string& id)> unreachable;  // "" or the reason text
    std::function<int32_t()> callerRadioBps;  // -1 when the caller's link is not radio (node->link())
    uint32_t radioLimitKb = 64;
};
void setNetFiles(NetFilesView v);
```

**The list**
- When a view is set, the area list ends with one row: "Net Files (User Net)".
- It opens a list in the same page layout as an area. The About column is replaced by the home board's name (12 characters) and then the description.
- Enter and `[D]` use the existing download gate with the listing's size.

**The radio warning**
- When `callerRadioBps() > 0` and the size is over `radioLimitKb`, the caller sees:
  `This file is <n> KB. Over the radio link at <bps> bps that's about <m> minutes. Fetch it anyway? (Y/N)`
  - `m` is `bytes * 8 / bps / 60` rounded to the nearest minute, and at least 1.
  - It reads "1 minute" for one.
- An all-internet trip gets no question.
- Phase 4 adds the board-to-board leg to the same check through a second bps.

**Starting the download**
- "that board can't be reached right now" comes from `unreachable`.
- Then the protocol is asked. A new `St::NetConnect` state shows "Connecting to <home>..." while the host's fetch slot connects.
- `NO gone`/`size` gives "that file is no longer there". `busy` gives "that board is busy, try again in a minute". The slot being taken locally gives the same busy text.
- Then the normal transfer runs, and `Node::transferDone` counts it against the caller as usual.
- Sysops pass the gate as today.

- [ ] **Step 1:** Implement the view, the list row, the warning, the connect state and the messages above.
- [ ] **Step 2:** Run the cyd and host builds. Both must succeed.
- [ ] **Step 3:** Commit `feat(net): Net Files list for callers` with the image size in the body.

### Task 6: Followers

**Files:**
- Modify: `src/cluster/remotefiles.h/.cpp`, `src/main.cpp`

**The protocol additions** (the existing op letter + 0x1F fields, reply `1`/`0` + data):
- `G` lists a Net Files page: `skip`, and the reply is lines of `id\x1Fname\x1Fsize\x1Fdesc\x1Fhome\x1Freach` plus the next skip.
- `B` begins a fetch for a follower's caller: `id`, `handle`, `offset`. It replies `1<size>`, `0<word>`, or `2` meaning "connecting, ask again".
- `T` takes bytes: `offset`, `length` (at most 2 KB). It replies `1<bytes>`, `2` meaning not ready yet, or `0<word>`.
- `E` ends the fetch.

**The two sides**
- On the main, these drive the same `fetch` slot as a local caller. One fetch per board in all.
- On the follower, `RemoteFetchFile : RandomFile` turns `2` into `ReadyAt` false, so the sender waits.
- A main without nets, or an older main, answers `0` to an unknown op, and the follower shows no Net Files row.

- [ ] **Step 1:** Implement the ops on the main's `FileServer` and the follower's view and file over them.
- [ ] **Step 2:** Run the cyd and host builds. Both must succeed.
- [ ] **Step 3:** Commit `feat(net): Net Files for callers on a follower` with the image size in the body.

### Task 7: PC peer, sysop page and docs

**Files:**
- Modify: `tools/net/netpeer.cpp`, `src/bbs/sysopshell.cpp`, `docs/NETS.md`, `docs/FEATURES.md`

**`axis_netpeer`, new commands:**
- `share <dir>` serves files from a folder as its area `pc`.
- `list <file> [desc]` sends a listing.
- `unlist <file>` sends the removal.
- `files` lists what it holds.
- `fetch <id> <outfile>` runs a `FetchClient` to a file.
- It answers `AXISFILE 1` calls while listening.

**Sysop shell**
- The User Net page gets a "Net Files" row: shared areas (pick from the file areas), listed count, the radio size limit.
- The record browser's Actions filter already includes `L` (Task 1).

**Docs**
- `NETS.md` gets a "Net Files" section covering:
  - sharing, the commands and the caps;
  - the fetch call and its NO words;
  - what a follower does;
  - the decisions above.
- `FEATURES.md` gets a dated entry. Its final size and test count are filled in by Task 8.

- [ ] **Step 1:** Implement the peer commands and the sysop page row, and write both docs.
- [ ] **Step 2:** Run the cyd and host builds. Both must succeed.
- [ ] **Step 3:** Commit `feat(net): PC peer Net Files and docs` with the image size in the body.

### Task 8: Tests, builds and the bench

**Files:**
- Create: `tests/test_net_listing.cpp`, `tests/test_net_fetch.cpp`
- Extend: `tests/test_net_session.cpp`, `tests/test_net_record.cpp`, `tests/test_net_recordview.cpp`, `tests/test_filebrowser*.cpp` (whichever covers the browser today)

**Records**
- Encode and decode round trips for `L` and `Q`.
- Each field rule in Task 1 refused (name with `/`, 121-byte desc, a gone listing with a size, an uppercase nonce).
- Sign and verify; a tampered byte fails.

**Listings**
- A listing from a non-member is refused `member`, and a board not carrying `un_files` refuses with `area`.
- The listing id is deduped with that board's posts.
- **Order independence:** listing seq 5, re-listing seq 9, removal seq 7 for one name, in all 6 orders. Each order ends with exactly seq 9 live. With the removal at seq 10 instead, all 6 orders end with none live.
- The per-board cap evicts that board's oldest listing only. The total cap evicts from the board with the most and never this board's own.
- The trim drops a tombstone at 121 days and keeps one at 119.
- A killed or held id is not in `listingsPage`. A banned home board's listings are not either.
- `nf_list` is in no caller area list (Task 2 step 2's finding, asserted).

**Session**
- `L` goes on E lines only to a `net3` peer.
- A U pull with `un_files` asked sends live listings and young tombstones.
- `L` lines share the per-session E cap with `A` and `V`.
- A Phase 2 peer (no `net3`) gets no `L`.

**Fetch**
- A full call over a memory pipe sends a 5,000-byte file. The bytes and the crc match.
- The server refuses each of these with its word:
  - `HI` signed by the wrong key (the client fails `sig`);
  - a replayed `GET` from an earlier call (nonce mismatch: `sig`);
  - a non-member: `member`;
  - a removed listing: `gone`;
  - a file whose size changed: `size`;
  - a second call while one is serving: `busy`.
- An `OK` whose count disagrees with the listing fails `size`.
- No bytes for `kFetchStallMs` ends the call.
- The ring:
  - `ready` and `read` inside the window work;
  - `behind` is true below the window and past it;
  - the client stops taking bytes while the ring is full.
- A ZMODEM sender over a `FetchClient` with a forced `ZRPOS` back past the window: the host's re-request path resumes at that offset and the received file is whole.
- XMODEM over a client delivers the file.
- `ReadyAt` false holds a sender without its timers running.

**Caller UI**
- The minutes text gives 340 KB at 3906 bps = "about 12 minutes".
- There is no question when `callerRadioBps` is -1, or when the size is at or under the limit.
- The unreachable text is shown for a home board whose card has no host.
- `NO gone` shows "that file is no longer there".

**Follower**
- A `G` page round trip.
- `B`/`T` with `2` holds the sender, then delivers.
- An unknown op from an old main hides the row.

**Steps**
- [ ] **Step 1:** Write the tests above. Run the host build. All tests pass (Phase 2 had 1020).
- [ ] **Step 2:** Run the cyd and nets-off builds. Fill in the final image size and test count in `FEATURES.md`. Commit `test(net): Net Files tests`.
- [ ] **Step 3: Bench** (board 1, MAC checked before writing; board 2 too if connected; the PC peer on the i5). Take a screenshot per step into `shots/nets-phase3/`, with no IP or MAC in them, plus `BENCH.md`.
  1. **Rev's step (Decision 5).** Rev admits board 1 and the PC peer.
  2. Flash board 1 and check the boot, `@NET` and `files: 0 listed`.
  3. Share an area on board 1 and upload a small file from a caller. The peer's `files` shows the listing.
  4. The peer runs `fetch <id>`. The file is whole, and board 1's record shows `fetch served`.
  5. The peer lists a 300 KB file. On board 1, the caller's Net Files row shows it, and a ZMODEM download from the caller completes. Then `@NET FETCHTEST`.
  6. `unlist` on the peer. The row is gone on board 1, and a fetch of the old id gives the gone message.
  7. The largest block during step 5's download with a `@NET SYNC` running. Ask Rev before any busy test.
  8. Restore: unshare, stop the peer, and `net_user` back to how Rev wants it.
- [ ] **Step 4:** Commit `test(net): phase 3 bench screenshots and log`.
