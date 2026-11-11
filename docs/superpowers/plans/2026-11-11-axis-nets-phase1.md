# AXIS Nets Phase 1 (Core Net and Sysop Net) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Every AXIS board joins Sysop Net over the internet, board to board with no server: posts flood between peers, signed by each board's own key, and Rev moderates and bans network-wide with his signed notices.

**Architecture:** A portable core in `src/net/`: signing, signed records (posts, cards, notices), dedupe, queues and a byte-in, byte-out sync session. It is host-testable and knows nothing about sockets. `src/main.cpp` owns the sockets: it accepts `AXISNET 2` on the existing telnet port (beside today's `AXISNET 1` greeting), dials peers, and pumps `NetSession` a piece at a time under a heap floor. Net posts land in ordinary message areas through a `MessageStore` wrapper, so followers read and post through the main as they already do.

**Tech Stack:** C++17, Arduino-ESP32 2.0.17 on ESP-IDF 4.4.7 via PlatformIO (env `cyd`), Monocypher 4.0.2 (EdDSA, CC0/BSD-2, vendored), doctest host tests via CMake/Ninja (`build/axis_tests`).

**Spec:** `docs/superpowers/specs/2026-11-11-axis-sysop-user-net-design.md` (approved by Rev 2026-11-11). Read it first.

**Later phases (their own plans, after this one ships):**
- Phase 2: User Net, new board voting with Rev's veto, and the full net record browser.
- Phase 3: Net Files (listings, streamed fetch, radio time estimate).
- Phase 4: radio sync through the LFD3K-SM modem.

## Global Constraints

**Where the work happens**
- Branch `nets/phase1` off `main`, after `memory/board1` is merged (both edit `src/main.cpp`).
- Do not push until Rev says so.

**Memory and flash**
- Board 1 idles at about 90 KB free heap. Net work never starts a piece while the largest free block is under `kNetHeapFloor = 40 * 1024`.
- No net frame exceeds `kNetMaxFrame = 9 * 1024` bytes.
- Net sync never opens TLS.
- A caller's session always wins over net work.
- Flash budget for the whole phase: about 80 KB. Everything compiles out with `-DAXIS_NETS=0`. The default is 1. The build options session will expose this as its `nets` option.

**Security**
- Signed only, never encrypted (Rev).
- Every post, card and notice is verified before it is stored or passed on. A record that fails verification is dropped and logged.
- Never commit any private key, Rev's hostname, the cluster key or a password.
- Rev's private keys stay in `local/` (untracked) and on Rev's board. Only the public halves go in `src/net/revkeys.h`.
- A MAC is shown, never trusted.

**Clocks**
- Rev's notices are ordered by their sequence number, never by time.
- Post dates are informational only.

**Protocol compatibility**
- `AXISNET 1` greetings keep working unchanged.
- The cluster protocol is untouched: only the main speaks on the net, and the cluster is one net node.

**Rev's style**
- Each build task ends with a successful `pio run -e cyd`, a host build that compiles (`cmake --build build`), and a commit.
- All tests are written and run in the last task.

**Build commands**
- Firmware: `export PLATFORMIO_CORE_DIR='F:\CodexStorage\Tools\platformio'; pio run -e cyd`.
- Host: `cmake -S . -B build -G Ninja && cmake --build build && ./build/axis_tests`.

**Commits**
- Short conventional messages. No Co-Authored-By, no AI attribution.
- `git add` named files only. Never add Rev's untracked files (churn.txt, crash.txt, live.txt, ring_live.txt, "new 1.txt", shots/, the hookup plan files).

**Hardware**
- Never touch COM15, COM9 or COM11.
- Flash board 1 (MAC `8c:94:df:4f:1c:4c`, COM20) or board 2 (MAC `8c:94:df:4f:14:18`) only after reading the MAC on that port.

**Prose**
- No em dashes or double hyphens in prose written for Rev.

## Review Focus

1. **A peer that sends a huge or endless frame.** The session must drop the connection at `kNetMaxFrame`, never buffer past it. Test: `session drops a frame over kNetMaxFrame`.
2. **Rev's notices arriving out of order: a lift before its ban, or a ban twice.** The highest sequence per target wins, and replays are ignored. Test: `notices apply by sequence not arrival`.
3. **Two peers flooding the same post back at each other.** It is stored once, and never queued back to the board it came from or to any board already on its path. Test: `flood does not echo to path`.
4. **A board posting fast.** The per-board daily ingress limit (posts and bytes) holds, and the excess is dropped with one log line per board per day. Test: `ingress limit drops excess`.
5. **A banned board's posts still in flight from other peers.** They are dropped on arrival everywhere, not only from the banned board's own connection. Test: `banned board's posts dropped when relayed`.

---

### Task 1: Vendor Monocypher and the signing wrapper

**Files:**
- Create: `third_party/monocypher/` (monocypher.c, monocypher.h, LICENCE.md from release 4.0.2, unmodified), plus a `library.json` like `third_party/sha256`'s.
- Create: `src/net/sign.h`, `src/net/sign.cpp`.
- Modify: `platformio.ini` (`lib_deps` gets `symlink://third_party/monocypher`), `CMakeLists.pc.txt` (add monocypher.c and src/net/*.cpp to `axiscore`).

**Interfaces (produces):**
- `namespace axis::net`
- `struct KeyPair { uint8_t secret[32]; uint8_t pub[32]; };`. `secret` is the 32-byte seed.
- `KeyPair keyFromSeed(const uint8_t seed[32]);`
- `std::string signHex(const KeyPair& k, const std::string& msg);` returns 128 hex characters.
- `bool verifyHex(const std::string& pubHex, const std::string& msg, const std::string& sigHex);`
- `std::string toHex(const uint8_t* p, size_t n); bool fromHex(const std::string& hex, uint8_t* out, size_t n);`
- `std::string boardId(const std::string& pubHex);` returns the first 16 hex characters of SHA-256 over the 32 public key bytes, using `third_party/sha256`.

- [ ] **Step 1: Get Rev's OK, then download** Monocypher 4.0.2 from its official release (`https://monocypher.org/download/monocypher-4.0.2.tar.gz`) into a fresh empty folder in the scratchpad. Check its SHA-512 against the one on the download page. Copy the three files in. **Downloads need Rev's explicit OK. Stop and ask if it hasn't been given.**
- [ ] **Step 2: Implement `sign.cpp`** with `crypto_eddsa_key_pair`, `crypto_eddsa_sign` and `crypto_eddsa_check`. Wipe secrets with `crypto_wipe`.
- [ ] **Step 3: Build** `pio run -e cyd` and `cmake --build build`. Note the flash growth in the commit body.
- [ ] **Step 4: Commit** `feat(net): vendor Monocypher 4.0.2 and add board signing`

### Task 2: Signed records (posts, cards, notices)

**Files:**
- Create: `src/net/record.h`, `src/net/record.cpp`.

**Interfaces (produces):**
- `enum class NetKind : char { Post = 'P', Card = 'C', Notice = 'N' };`
- `struct NetPost { std::string id, area, board, from, to, subject, body, path; uint32_t date = 0; std::string sig; };`
  - `id` is `<board>-<seq>`, where seq is the decimal per-board counter.
  - `path` lists the board ids it passed through, comma separated, oldest first. It is not signed.
- `struct NetCard { std::string board, pub, name, host, mac, sysop, location, links; int port = 23; uint32_t seq = 0; std::string sig; };`. A newer `seq` replaces an older card.
- `struct NetNotice { std::string kind, target, value; uint32_t seq = 0; int key = 0; std::string sig; };`
  - kinds: `hold`, `kill`, `ban`, `lift`, `limits`, `rotate`.
  - `key` is 0 or 1, saying which of Rev's two keys signed it.
- Canonical text: one record per line, fields joined by `0x1F`, kind letter first. The body's newlines are escaped as `\n` and backslashes as `\\`.
  - `std::string signedPart(const NetPost&)` covers every field except `path` and `sig`.
  - The same for cards and notices.
- `std::string encode(const NetPost&)`, and the same for `NetCard` and `NetNotice`.
- `bool decode(const std::string& line, NetPost&)`, and the same for the others. Each returns false on a wrong field count, a bad id form, a non-hex signature, an area outside `[a-z0-9_]{1,16}`, a subject over 72 bytes or a body over `kNetMaxBody = 8 * 1024`.
- `bool signPost(NetPost&, const KeyPair&)`; `bool verifyPost(const NetPost&, const std::string& pubHex)`. The same pair for cards. `bool verifyNotice(const NetNotice&)` checks against `revKey(n.key)`.
- `src/net/revkeys.h`: Rev's two public keys in hex.
  - `const std::string& revKey(int i);` reads them (i is 0 or 1).
  - `void setRevKeysForTest(const std::string& a, const std::string& b);` is the test hook.
  - Until Task 8 makes the keys, use two all-zero placeholders that `verifyNotice` always rejects.

- [ ] **Step 1: Implement** as above.
- [ ] **Step 2: Build** (both).
- [ ] **Step 3: Commit** `feat(net): signed post, card and notice records`

### Task 3: Net state (cards, Rev's notices, bans, limits) and dedupe

**Files:**
- Create: `src/net/state.h`, `src/net/state.cpp`.

**Interfaces (produces):**
- `class NetState`:
  - `bool addCard(const NetCard&)`: verified against its own `pub`, and `boardId(pub) == board`. It keeps the higher `seq`. Returns true when something changed.
  - `bool applyNotice(const NetNotice&)`: verified. For each `(kind group, target)`, the highest seq wins. hold/kill/lift on a post id share one group; ban/lift on a board id share another.
    - `rotate`, signed by key k, replaces key 1-k through `void setRevKey(int i, const std::string& hex)` (revkeys.h) and persists it in `/net/revkeys.txt`, which overrides the built-in keys at boot.
    - `limits`: `value` = `posts=<n>,bytes=<n>`.
  - `bool banned(const std::string& board) const`
  - `bool held(const std::string& postId) const`
  - `bool killed(const std::string& postId) const`
  - `uint32_t highSeq() const`: the highest notice seq applied, sent at handshake.
  - `std::vector<std::string> snapshot() const`: every current card and the latest notice per target, as encoded lines. This is the "current state" a new or returning board receives.
  - `std::string save() const; void load(const std::string&);`: one encoded line each.
- `class NetSeen`:
  - `bool add(const std::string& id, uint32_t nowDay)`: false if already seen.
  - `void prune(uint32_t nowDay)`: drops ids older than `kSeenDays = 120`.
  - `save`/`load`: text, `<day> <id>` per line.
- `class Ingress`:
  - `bool allow(const std::string& board, size_t bytes, uint32_t nowDay)`.
  - Defaults are `kDefaultPostsPerDay = 50` and `kDefaultBytesPerDay = 64 * 1024`, replaced by a `limits` notice.
  - `bool warnOnce(const std::string& board, uint32_t nowDay)`.

- [ ] **Step 1: Implement.**
- [ ] **Step 2: Build** (both).
- [ ] **Step 3: Commit** `feat(net): net state, Rev's notices, dedupe and ingress limits`

### Task 4: Net areas in the message base

**Files:**
- Modify: `src/bbs/msgbase.h` and `src/bbs/msgbase.cpp`.
  - `Message` gains `std::string net;`, holding the encoded `NetPost` line.
  - `FileMessageStore` writes and reads a `net=` header line, and only when it is non-empty.
  - The cluster `serveMessages` and `RemoteMessageStore` carry the field only if they already pass headers through. Otherwise followers simply don't see it (it is not needed there).
- Create: `src/net/areas.h`, `src/net/areas.cpp`.

**Interfaces:**
- Consumes: `MessageStore` (msgbase.h), `NetPost`, `NetState`, `NetSeen`, `Ingress`.
- Produces:
  - `const std::vector<NetArea>& sysopNetAreas();` with `struct NetArea { std::string id, name; bool revOnly; };`. Exact list:
    - `sn_support` "Sysop Net: Support" (revOnly false)
    - `sn_announce` "Sysop Net: Announcements" (revOnly true)
    - `sn_updates` "Sysop Net: Update Notices" (revOnly true)
    - `sn_bugs` "Sysop Net: Bug Reports" (revOnly false)
  - `class NetCore`. It owns `NetState`, `NetSeen`, `Ingress`, this board's `KeyPair`, its post counter, and per-peer outbound queues (`std::map<std::string, std::deque<std::string>>` of post ids, persisted by the host through `save()`/`load()` callbacks).
    - `NetCore(MessageStore& store, KeyPair me, std::function<uint32_t()> nowUnix)`
    - `enum class Took { Stored, Duplicate, Rejected, Banned, Limited };`
    - `Took take(const NetPost& p, const std::string& fromPeer)`. Checks, in order: decode limits, `banned(p.board)`, card known and signature verifies, revOnly areas need the post signed by a board whose card `pub` equals `revKey(0)` or `revKey(1)` (Rev's board signs with its board key, and its card carries the Rev key as its `pub`), dedupe, ingress. Then it stores into the message area (from/to/subject/date/body, plus `net`), appends `fromPeer`'s id to `path`, and queues the id for every known peer that is not `fromPeer` and not on `path`.
    - `std::string originate(const std::string& area, const Message& m)`. Builds, signs and stores a local post, queues it for every peer, and returns its id. revOnly areas refuse unless this board is Rev's.
    - `std::vector<std::string> pendingFor(const std::string& peer, size_t max)`: encoded lines.
    - `void acked(const std::string& peer, const std::string& id)`
    - Retention: `void trim(uint32_t nowUnix)`. Each net area keeps its newest `net_keep_posts` (500) posts and drops posts older than `net_keep_days` (90). Killed posts are dropped at once. Held posts are kept but hidden by `NetPostingStore`.
  - `class NetPostingStore : public MessageStore`. It wraps the real store. Message areas whose id starts with `sn_` go through `NetCore::originate` on `post()`, and `headers()` hides held and killed posts. Everything else passes straight through. The main installs it as the board's message store, so follower posts arrive through `serveMessages` and are covered.

- [ ] **Step 1: Implement.**
- [ ] **Step 2: Build** (both).
- [ ] **Step 3: Commit** `feat(net): Sysop Net areas, NetCore and posting store`

### Task 5: The sync session

**Files:**
- Create: `src/net/session.h`, `src/net/session.cpp`.

**Interfaces:**
- Consumes: `NetCore`, `NetState`, `signHex`/`verifyHex`.
- Produces: `class NetSession`, a pure state machine:
  - `NetSession(NetCore& core, const NetCard& me, bool dialer)`
  - `void feed(const char* p, size_t n)`: bytes from the socket.
  - `std::string takeOut()`: bytes for the socket.
  - `bool done() const; bool failed() const; const std::string& peer() const; const char* why() const;`
  - `void tick(uint32_t nowMs, bool heapOk)`. When `heapOk` is false it sends nothing new and waits. It fails after 30 s of silence.
- Wire: lines, each ending with `\n`.
  1. The dialer sends `AXISNET 2 <encoded own card> <nonceHex16>`. The answerer replies with the same.
  2. Each side sends `PROOF <sig of peer's nonce + own board id>`. Each side verifies the proof against the card's `pub`, then `core.state().addCard(card)`. A banned board is refused with `BYE banned`.
  3. Each side sends `SEQ <highSeq>`. The side with the higher seq sends `state().snapshot()` lines that the other lacks. A new board (seq 0) gets everything.
  4. Each side sends up to `kSyncPerTick = 4` lines from `pendingFor(peer)` per `tick`. The receiver answers each post with `ACK <id>`, or `NAK <id> <reason>` (a NAK also counts as handled). Notices and cards are applied as they arrive.
  5. When both queues are empty, each side sends `DONE`, and the session ends after both DONEs.
- Any line over `kNetMaxFrame` fails the session (`why()` = "frame too big").

- [ ] **Step 1: Implement.**
- [ ] **Step 2: Build** (both).
- [ ] **Step 3: Commit** `feat(net): board to board sync session`

### Task 6: Firmware wiring (identity, sockets, schedule, settings)

**Files:**
- Modify: `src/main.cpp`, `src/bbs/config.h`, `src/bbs/config.cpp`.

**Settings** (config keys, written by `kv` like `modem_link`):
- `net_sysop` (bool, default 1).
- `net_first_peer` (host:port, default empty).
- `net_sync_min` (int, default 15).
- `net_keep_posts` (500).
- `net_keep_days` (90).
- `net_name`, `net_sysop_name`, `net_location`. These default to the board name, the sysop's handle, and empty.

**Identity**
- On first boot the main makes a seed from 32 bytes of `esp_random()` and writes it as hex to internal flash `/netkey.txt` with `flashWrite`. Internal LittleFS survives normal reflashes. (Ruling: the spec says NVS; LittleFS gives the same lifetime and is already used for `/stats.txt`.)
- The board's card is built from the settings, `WiFi.macAddress()`, the public address Axis Net already uses (`axisSelfCard().host`) and the port. `seq` is bumped whenever any of those change.

**Sockets**
- `axisServe` reads the first line. `AXISNET 2 ...` hands the `WiFiClient` to a single global inbound slot `netIn` (one inbound and one outbound session at a time). A second arrival gets `BYE busy`.
- The loop pumps both slots without blocking: read what is available, `feed`, write `takeOut()`, then `tick(millis(), heapOk)`.
  - `heapOk` means `heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) >= kNetHeapFloor`, and no node is in `Node::State::Transfer`.
- Every `net_sync_min` minutes, and 10 s after a local net post, the main dials the next peer round robin, with `net_first_peer` first while no other cards are known, and opens `netOut`.
- Only the main does any of this (`asFollower()` returns early, like `axisProbeOne`).
- The whole thing sits inside `#if AXIS_NETS`.

**Areas**
- When `net_sysop` is on, the four `sysopNetAreas()` are added to the message areas at `minSecurity = kSysopSecurity`, inside a group headed "Sysop Net".
- `NetPostingStore` wraps the board's store.

**Persistence**, on the main's SD card under `/net/`:
- `state.txt`, `seen.txt`, `queue-<board>.txt` (written when they change, at most every 30 s).
- The record files (Task 7).

- [ ] **Step 1: Implement.**
- [ ] **Step 2: Build** `pio run -e cyd` with `-DAXIS_NETS=1` and with `-DAXIS_NETS=0`. Record both image sizes in the commit body.
- [ ] **Step 3: Commit** `feat(net): Sysop Net on the main board`

### Task 7: Net record and sysop commands and screens

**Files:**
- Create: `src/net/netrecord.h`, `src/net/netrecord.cpp`.
- Modify: `src/main.cpp`, `src/bbs/sysopshell.cpp`, `src/bbs/node.cpp` and `.h` (one new sysop screen state).

**Interfaces:**
- `class NetRecord`
  - `NetRecord(std::function<bool(const std::string& path, const std::string& line)> append, std::function<std::string()> yyyymm)`
  - `void note(const std::string& who, const std::string& what, const std::string& signedLine)`.
    - It appends `<unix> <who> <what>` then `0x1F` and the original signed line (empty for local actions) to `/net/record/<yyyymm>.log`.
    - Append only. Never trimmed by retention.
  - Every `take` result, `originate`, notice applied, card change, ban drop and local sysop action calls it.

**Serial and sysop commands** (in `boardOnlyCommand`, refused from followers and from telnet where marked):
- `@NET`: status (board id, peers known, queue depths, last sync per peer, high seq).
- `@NET SYNC`: dial now.
- `@NET PEERS`: one line per card (name, board id, MAC, host:port, link, last seen, banned).
- `@NET RECORD [n]`: last n record lines, default 20.
- On Rev's board only:
  - `@NETKEY SET <seedHex>`: serial only, never telnet. Stores Rev's daily key in `/netrev.key`, and the board's card then uses that key as its `pub`.
  - `@NETKEY CLEAR`: serial only. Deletes `/netrev.key`, and the board goes back to its own key.
  - `@NETBAN <board>`, `@NETHOLD <postId>`, `@NETKILL <postId>`, `@NETLIFT <id>`, `@NETLIMITS posts=<n>,bytes=<n>`. Each makes, signs and applies a notice with `highSeq()+1`, and it floods.
- **Local moderation (any sysop):**
  - `@NETHIDE <postId>`: hide one post on this board only.
  - `@NETBLOCK <board>`: refuse that board's sessions and posts here only. Kept in `/net/local.txt`. Never flooded.

**Sysop menu:** a Network Setup > Sysop Net page (the same lightbar style as the System > Modem page). It holds:
- This Node (name, board id, MAC, origin line, contact; editable fields map to the `net_*` settings).
- Peers.
- Sync and Routing (`net_sync_min`, `net_keep_posts`, `net_keep_days`, `net_first_peer`).
- Traffic and Logs (the record, newest first).
- Moderation (local hide and block lists; Rev's board also shows ban, hold, kill and lift).
- The on or off toggle for `net_sysop`.

- [ ] **Step 1: Implement.**
- [ ] **Step 2: Build** (both).
- [ ] **Step 3: Commit** `feat(net): net record, sysop commands and Sysop Net page`

### Task 8: Rev's keys, the install menu and docs

**Files:**
- Create: `tools/net/netkey.cpp`.
  - Add `add_executable(axis_netkey tools/net/netkey.cpp)` linked to `axiscore` in `CMakeLists.pc.txt`.
  - Commands:
    - `axis_netkey new <file>`: writes a 32-byte seed as hex to `<file>` and prints the public key hex.
    - `axis_netkey pub <file>`
  - It refuses to overwrite an existing file.
- Modify: `src/net/revkeys.h` and its `.cpp`, with the two public keys Rev makes.
- Modify: `tools/install_cyd.py`. If `local/net_first_peer.txt` exists, its one line (host:port) is written as `net_first_peer` during first-boot setup notes and the serial setup. It is never printed in full; show `set (hidden)`.
- Create: `docs/NETS.md`. What Sysop Net is, the settings, the commands, how Rev makes and rotates keys, and the files under `/net/`.

- [ ] **Step 1: Build `axis_netkey`.** Ask Rev to run `build/axis_netkey new local/netkeys/rev-daily.seed` and `build/axis_netkey new local/netkeys/rev-backup.seed`, and paste back only the two printed public keys. Add `local/netkeys/` to `.gitignore` if `local/` isn't already ignored. Check with `git check-ignore`.
- [ ] **Step 2: Put the public keys** in `revkeys.cpp`. Implement the install menu change and write the doc.
- [ ] **Step 3: Build** (both).
- [ ] **Step 4: Commit** `feat(net): Rev's public keys, net key tool, install first peer, docs`

### Task 9: All tests and the two-node bench

**Files:**
- Create: `tests/test_net_sign.cpp`, `tests/test_net_record.cpp`, `tests/test_net_state.cpp`, `tests/test_net_core.cpp`, `tests/test_net_session.cpp`, `tests/test_net_record_log.cpp`. Add each to `axis_tests` in `CMakeLists.pc.txt`.
- Create: `tools/net/netpeer.cpp` (`axis_netpeer`). A PC net node built from `NetCore` + `NetSession` over `platform/pc/tcpstream`, with a `FileMessageStore` in a temp folder. It can listen or dial, and has a `post <area> <subject> <body>` command. It is the second node for the bench, since board 1 and board 2 form one cluster, which is one net node.

**Host tests (doctest), with the assertions to make:**
- `keys sign and verify`: sign "hello" with `keyFromSeed` of 32 bytes `0x01`. `verifyHex` is true, false with one changed byte of the message, and false with one changed signature hex character. `boardId` is 16 hex characters.
- `post round trip`: encode, then decode, gives equal fields. A body with `\n` and `\\` survives.
- `post rejects`: a 9 KB body, a 73-byte subject, area `Bad-Area`, a 127-hex-character sig, and a wrong field count each give `decode` false.
- `tampered post fails verify`: change the subject after signing, and `verifyPost` is false. Change only the path, and it is still true.
- `notices apply by sequence not arrival` (Review Focus 2): ban seq 5, then lift seq 4, means still banned. Lift seq 6 lifts it. The ban seq 5 replayed afterwards changes nothing. A notice signed by a non-Rev key is rejected. In the test, the keys are set through the test hook `setRevKeysForTest(a, b)`.
- `rotate replaces the other key`: rotate signed by key 0 replaces key 1. After that, a notice signed by the old key 1 is rejected.
- `seen prunes after 120 days`: an id added on day 10 is still seen on day 129 and forgotten on day 131.
- `ingress limit drops excess` (Review Focus 4): with limits posts=3,bytes=100000, the 4th post that day is `Limited`, and posts are allowed again the next day. `warnOnce` is true once per board per day.
- `flood does not echo to path` (Review Focus 3): three cores A, B, C, all knowing each other. A post from A taken by B queues it for C only. When C takes it from B, it is queued for nobody (A is on its path, B is its sender). Taking it again gives `Duplicate`.
- `banned board's posts dropped when relayed` (Review Focus 5): ban board X, then B takes X's post from peer A, giving `Banned`. It is not stored and not queued.
- `rev-only areas`: a non-Rev board's post to `sn_announce` is `Rejected`, and `originate` refuses it. A board whose card `pub` is a Rev key is accepted.
- `held posts hidden, killed posts gone`: through `NetPostingStore::headers`, after hold and kill notices, then lift of the hold.
- `retention trims to 500 and 90 days`.
- `session syncs two cores`: two `NetSession`s are wired back to back in memory. A's two queued posts reach B, both queues end empty, both sessions are `done()`, and B's state has A's card.
- `new board gets state snapshot`: B at seq 0 receives A's ban list from A at seq 7.
- `session drops a frame over kNetMaxFrame` (Review Focus 1): feeding 10 KB with no newline fails the session with "frame too big", and the buffer never grows past the limit.
- `banned board refused at handshake`: the session ends with `BYE banned`.
- `bad proof refused`: the card's pub doesn't match the proof signature.
- `heap gate pauses sending`: `tick(…, false)` produces no new post lines, and `tick(…, true)` resumes.
- `record appends signed lines`: `note` writes to the month's file and never rewrites earlier lines.

**Bench (hardware), with screenshots or logs saved per step and no keys shown:**
1. Build and flash board 1 (MAC check first). Board 2 follows normally.
2. On the PC, run `build/axis_netpeer listen 2400`, and set board 1's `net_first_peer` to the PC's LAN IP `192.168.1.197:2400` over serial.
3. `@NET SYNC` on board 1. Board 1 and the PC peer exchange cards. `@NET PEERS` lists the PC node.
4. Post in Sysop Net: Support from a caller session on **board 2**, logged in as the sysop. Within 10 s it shows up in the PC peer's store. Post from the PC peer, and it shows in the board's Sysop Net: Support after the next sync.
5. Ban the PC peer from board 1 using a test Rev key loaded with `@NETKEY SET` (a throwaway seed, never Rev's real one). The PC peer's next session gets `BYE banned`.
6. `@LOG` and `@NET`: no heap floor pauses while idle, no crash. Free heap during a sync stays at or above 40 KB largest block.
7. Restore: clear the test key (`@NETKEY CLEAR`), and reset `net_first_peer`.

- [ ] **Step 1: Write all tests.** Run `./build/axis_tests` and expect every test to pass, old and new.
- [ ] **Step 2: Run the bench** as above.
- [ ] **Step 3: Commit** `test(net): Sysop Net tests and PC net peer`
- [ ] **Step 4: Update** `D:\brain\Projects\AXIS BBS Firmware.md` with what shipped.
