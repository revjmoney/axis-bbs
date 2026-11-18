# AXIS and LFD3K-SM hookup: Tasks 2 to 4 report (working note, not committed)

Branch `lfd3k-hookup`, three commits on top of 7bdd073, not pushed. The modem is the LORA FLOWTRON DATAMASTER 3000 SERIAL MODEM - LFD3K-SM. Only the plan's wire format was used; nothing from `F:\lfd3k-sm` was opened.

| Task | Commit | Message |
|---|---|---|
| 2 | 61b40ff | feat: accept LFD3K-LINK stats from the modem |
| 3 | 34f1c3f | feat: radio callers and link stats on the WFC and sysop screens |
| 4 | 477998e | feat: lite screens for slow radio callers |

## Task 2: accept the option and tag the caller

- `src/net/telnet.h/.cpp`
  - `kLinkOption = 90`, `kLinkPayloadMax = 160`, `struct LinkInfo`, `parseLinkInfo()`, `linkLabel()`.
  - `TelnetFilter::acceptLink(bool)` (off by default, so the Axis Net bridge's filter and anything else still refuse), `linkAgreed()`, `link()`, `linkReports()`.
  - Negotiation: WILL 90 gets DO 90 when accepted (once; a repeated WILL gets no answer), DONT 90 when not. WONT 90 after we agreed gets DONT 90. DO 90 from the far side still gets WONT (old path).
  - SB: the general cap stays 64 bytes (an existing test pins it); an SB 90 may hold 1 + 160. Anything past the cap sets `sbOver_` and the whole subnegotiation is dropped at IAC SE, never cut and used. SB 90 is only parsed after we answered DO 90. `subnegotiation()` now holds the NAWS handling too.
  - SB bytes never reach `toApp` (unchanged state machine).
- `src/net/telnetstream.h/.cpp`: `acceptLink()`, `takeLink(LinkInfo&)` (true once per new report), `bytesIn()` / `bytesOut()` counters.
- `src/bbs/config.h/.cpp`: `modemLink` (default true), `liteBelowBps` (default 7031, clamped to 0 or more).
- `src/bbs/sysopshell.cpp`: the two keys in Settings > Network.
- `src/bbs/node.h/.cpp`: `Node::setLink()`, `link()`, `viaModem()`, `linkTiers()` (tier range of the call), `WhoLine::link`; the who list shows the label after the activity; the STATS screen gets a `link` row ("RADIO SF7 -81  21875 bps"); `applyBoard()` passes the two new settings to callers already on.
- `src/main.cpp`: `answerIn()` calls `line->acceptLink(cfg.modemLink)` before `start()`; `takeLinkReport()` runs first in `serviceSession()` (before door, movie and transfer early returns), pushes the report into the node and logs "radio caller on node N: RADIO SF7 -81 via NAME" on the first one.

### LinkInfo

```cpp
struct LinkInfo {
    bool        any = false;                // a report has come in this call
    int         version = 0;                // v=
    std::string modem, kind, caller, tier;  // names: letters, digits and - _ . only (24 max); kind radio|wifi; tier SF5..SF12|WIFI
    int32_t     bps = -1;                   // 0..100000000, -1 unknown
    int         snrTenths = 0, rssi = 0;    // SNR -99.9..99.9 dB in tenths; RSSI -200..50 dBm
    bool        hasSnr = false, hasRssi = false;
    int32_t     retrains = -1;              // 0..999999, -1 unknown
    int         lzTenths = -1;              // 0..99.9 in tenths, 0 = off, -1 unknown
    bool        radio() const;
    int         sf() const;                 // 5..12, 0 otherwise
};
```

A report is rejected whole (old LinkInfo kept) when it is over 160 bytes, holds a byte outside 0x20..0x7e, or has no valid `v=`. Inside a good report a bad value leaves only that field unknown. Unknown keys are ignored. `caller=-` means none. Each good report replaces the last one completely (a missing key is unknown).

## New axis.ini keys

| Key | Default | Meaning |
|---|---|---|
| `modem_link` | 1 | Answer the modem's WILL 90 with DO 90. 0: DONT 90, the modem stays silent. |
| `lite_below_bps` | 7031 | Callers whose last report is at or below this get lite screens (SF9 is 7031.25 bps, so SF9 and slower). 0 = never. |

Snake case like every other key. The plan's spellings `modemLink` and `liteBelowBps` are accepted on read; the board always writes the snake case ones.

## Task 3: show it

- `src/bbs/wfc.h/.cpp`: `WfcNode::link` (in `==`), shown after the activity on the node line; `struct WfcRadio` and `wfcRadio(LinkInfo, who)`; `WfcSnapshot::radio` (in `operator==`); the new codes in `wfcValues()`. No ANS art edited: the node line carries the label.
- `src/main.cpp`
  - `wfcSnapshot()`: local node lines carry the label; `w.radio` from the newest report on the board (`lastRadio`, `lastRadioWho`).
  - Cluster: `serviceSession()` appends the label without RSSI ("  RADIO SF7") to the activity it publishes, so every board's WFC and who list show it and the status is not resent every 10 s.
  - Caller who list (local branch): `WhoLine::link`.
  - `drawNodeDetail()`: "from" shows "IP  RADIO SF7".
  - `hangUp()`: a radio call goes into `radioCalls` (newest first, 10 kept): handle, tier range, seconds, bytes in plus out, retrains.
  - mDNS: `modemSearchStart()` starts `mdns_query_async_new(nullptr, "_lfd3k", "_tcp", PTR, 3000 ms, 8)`; `modemSearchPoll()` runs every loop turn with a zero timeout (never blocks), keeps up to 8 modems (TXT `name=` else the instance name, sanitized to letters, digits, space, - _ . and 24 characters; first IPv4), frees results and the search.
  - `@MODEM` (and `@MODEM scan`): lines `now|node|handle|label|bps|tiers`, `call|handle|tiers|secs|bytes|retrains`, `found|name|ip`, `search|on`. It starts a search when none ran, the last is over a minute old, or `scan` was asked.
- `src/bbs/sysopshell.h/.cpp`: System > Modem (the last item, online and at the board, so existing item numbers do not move): the two settings (flip, and ask for the number), Radio callers now, Last radio calls, Modems on the LAN ("looking..." while a search runs), Look for modems again.
- `docs/WFC-CODES.md`: node line note and the radio codes table.

### New WFC pipe codes

| Code | Shows |
|---|---|
| `RL` | link label, "RADIO SF7 -81" or "WIFI" |
| `RW` | caller's handle (or the modem's caller name before login) |
| `RO` | modem name |
| `RT` | tier |
| `RB` | air bps |
| `RS` | SNR, one decimal |
| `RR` | RSSI dBm |
| `RN` | retrains |
| `RZ` | LZ ratio or "off" |

Checked against the pipe code tokenizer: none collide with `CL DE CR PP`, `B0..B15`, `XY`, the long ViSiON/3 names, `RM` (RAM), `LC`, `N?`, `E?`, `K?`.

## Task 4: lite screens

- `Node::lite()`: `lite_below_bps > 0`, a report came, its `bps` is known and `bps <= lite_below_bps` (inclusive, so a modem reporting SF9 as 7031 gets lite). Asked at every screen, so a retrain changes the next screen. The art loaders read it through a shared flag (`liteNow_`, set in `setLink()` and `applyBoard()`), so a moved Node never leaves a loader pointing at the old object.
- `docs/FEATURES.md`: feature row, the two settings rows, change log entry.
- The Task 4 commit also carries one Task 3 fix: `applyNetwork()` forgets a running mDNS search before `MDNS.end()` frees it.
- How lite art is chosen: `Node::liteName("MAIN.ANS") == "MAIN.LITE.ANS"` (any `.ANS` name, not a look preview `look:FILE`, not a path, not already `.LITE.ANS`). For a lite caller every art lookup tries the lite name first and falls back to the full name when it is empty or missing:
  - `Node::setArt()` wraps the theme art loader (MATRIX, MAIN, BULLETIN, WFC ...).
  - `Node::setMenus()` wraps the menu file loader and the piece-by-piece menu art reader (and so the MenuRunner's).
  - `theme::setLite()` set from `Node::MciScope` (around every `begin`, `input`, `poll`): `theme::artFor()` tries `NAME.LITE.ANS` for header screens (STATS, DOORS ...).
  - `Node::startPoster()` picks `SAYLOR.LITE.ANS` once, so every streamed piece comes from the same file.
  - `theme::menuScreenFile()` maps lite names like their full ones (`PDMATRIX.LITE.ANS` to `MATRIX.LITE.ANS` in the theme folder).
- Splash: `begin()` skips it when already lite; `poll()` cuts it (clear screen, then the welcome) as soon as a lite report arrives, which is the normal case since the report comes after `begin()`.
- ANSI movies: `startDoor()` does not give the pack viewer its clock for a lite caller, so movies are not paced to 9600 baud (the line paces them).
- `docs/MENU-ART.md`: "Lite art for slow radio callers".

## Tests and builds

- Host: `cmake --build build` with MinGW from `D:\onerous\msys64\mingw64\bin`; `build/axis_tests`: 761 test cases passed, 0 failed (4697 assertions) after each task. No new tests (Task 5).
- Firmware: `pio run -e cyd` passed after each task. The default core dir failed on a stale `.pio` CMake cache that points at `F:\CodexStorage\Tools\platformio` (two `adc.c` sources); building with `PLATFORMIO_CORE_DIR=F:\CodexStorage\Tools\platformio` and its penv works. Flash use: about 2,011,253 bytes before (85.2%; that build overlapped my first edits, so approximate), 2,017,601 after Task 2, 2,030,229 after Task 3, 2,034,973 after Task 4 (86.3% of 2,359,296). RAM 47,392 bytes static.
- A scratch program (not in the repo) fed the filter WILL 90, a byte-at-a-time SB 90 report, and a 300-byte report: DO 90 answered, all fields parsed, label "RADIO SF7 -81", the oversized report dropped, caller bytes intact. With the link off it answered DONT 90.
- Nothing flashed, no serial port opened, nothing pushed.

## Concerns

1. With `login_splash=0` there is no splash to cut, so a radio caller's first screen (matrix, prelogon or welcome) goes out before the first report and is full weight. Every screen after that follows the report.
2. `@MODEM` from the telnet sysop menu on a follower runs on the main (follower @ commands go to the main), so it lists the main's radio callers and history only. The WFC node lines and who lists are cluster-wide (the label rides in the status activity).
3. Lite lookups cost one extra file open per screen for a lite caller (and a cluster request on a follower, where only `.MNU` files are cached). Only callers below the threshold pay it.
4. A lite file in `/menus` beats a full one in the theme folder for theme-drawn menu screens (the lite name is tried first everywhere). That is the intent, but a sysop should know it.
5. `takeLinkReport()` sets `screenDirty` on every report (every 10 s per radio caller), so the CYD screen redraws that often while a radio caller is on.
6. Spoofing: any telnet caller can send WILL 90 and SB 90 (plan says display only). The worst case is a wrong label or lite screens for that caller; names are restricted to `[A-Za-z0-9-_.]` (mDNS names also allow space) so nothing reaches terminals, `|` split @ output or the event log unsafely. A spoofed `bps` could get a caller lite art, which never locks a feature.
7. The radio call history and the last report live in RAM only; a restart clears them.
8. Plan deviations: `SysInfo` got no label field (STATS reads the node's own link); `Session` got no `LinkInfo` or `viaModem` field (the line's filter and the node hold them); on a board without a cluster the matrix `W` and multinode `w` who lists do not print `WhoLine::link` (the main Who's Online list does; in a cluster the label is in the activity everywhere).
9. Line endings: the edit script kept CRLF in files that had it. `git status` briefly showed six files as modified with no content diff after the first commit (CRLF stat noise); re-adding them cleared it, no content changed.
10. Brain note `D:\brain\Projects\AXIS BBS Firmware.md` updated with this progress (outside the repo).

## Fix round 1 (commit c952f88)

The fixes answer the review at `F:\lfd3k-sm\.superpowers\sdd\2026-11-10-lfd3k-sm-phase4-menu-scan\review-axis-hookup.md`.

- **M1, rate limits.**
  - `TelnetStream::takeLink()` hands over at most one report a second per line. Faster reports are still parsed, each replacing the last, and the newest is taken once the second is up.
  - `Node::linkShown()` is the short label without RSSI ("RADIO SF7"). It follows the link only when the label changes, and at most every 5 s. `Node::linkRedrawDue()` is true once each time it moves.
  - `takeLinkReport()` sets `screenDirty` only then, never per report. It also runs when no report came, so a held-back tier change still lands.
  - The cluster activity, the local WFC node lines and the local who list use `linkShown()`. So `setStatus` goes out only on a tier change, at most every 5 s.
  - `WfcRadio::operator==` now compares only `who`, `modem` and `tier`. The label with RSSI, bps, SNR, RSSI, retrains and LZ refresh with the next redraw (the minute tick at the latest).
  - `lastRadio` moves to another caller only for a new call, a tier change, the same caller, or when the shown one is over a minute old. Two radio callers no longer take turns.
  - Docs: WFC-CODES.md is updated for this.
- **M2.** `number()` sums in `int64_t` and refuses a tenth digit before the point. Checked with a scratch build using `-ftrapv`: hostile values (`snr=300000000.0`, `bps=999999999`, `retrains=9999999999`) clamp or are refused, with no trap.
- **L1.** `modemSearchForget()` (before `MDNS.end()` in `applyNetwork()`) waits up to 3.5 s for the search, about its own 3 s timeout, and then frees the results and the search. Only a search still running after that is left for `mdns_free`. `applyNetwork` is rare (settings save, WiFi join).
- **L2.** `Node::liteMiss_` is a shared list of lite names this call found missing (64 at most). The art loader, the menu loader, the menu piece reader and the poster skip them. On a follower each missing lite screen therefore costs one round of `T`/`U` requests per call, not one per screen. `theme::artFor` already had the follower's art cache.
- **L3.** `theme::menuScreenFile()` uses a static const table of literals for the lite names. There is no heap.
- **L4.** `wfcScreen()` in main.cpp sends `WFC.LITE.ANS` and `WFCCYD.LITE.ANS` to the WFC skin's folder, in both `themeArtLocal` and `themeArtPiece`.
- **L5.** `Node::lite()` also needs `link_.radio()`, so lite applies to radio calls only. MENU-ART.md says "a radio caller".
- **L6.** The filter ignores SB 90 once `acceptLink(false)` is set, even after DO 90. `pushBoardToCallers()` (run after every `@SET`) calls `acceptLink(cfg.modemLink)` on every line. `takeLinkReport()` also skips reports while `modem_link` is off.
- **Nits.**
  - The node detail "from" row is built as one `std::string`.
  - A found modem with no address shows "-".
  - When the mDNS search ends, an open Modem page rebuilds itself through the new `SysopShell::reloadIf("MODEM")` and `Node::sysopReload()`. This covers every telnet session, the local console node, and the board's own shell (which also redraws).
- **Builds and tests.** 761 of 761 test cases pass (4696 assertions). `pio run -e cyd` succeeds: flash 2,037,141 bytes (86.3% of 2,359,296). No new tests. Nothing pushed or flashed.
- **Left as is.** Waiting up to 3.5 s in `applyNetwork()` blocks the loop that long, but only while a search is running at a settings save or a WiFi join.

## Fix round 1 follow-up (commit 8accb31)

- `applyNetwork()` no longer blocks. It sets the clock zone at once.
  - When no mDNS modem search is running, it calls `applyNetworkNow()` straight away. That is the old body: `MDNS.end()`, `MDNS.begin()`, telnet and panel listeners.
  - When a search is running, it sets `networkPending` instead and returns.
- `applyNetworkDue()` runs every loop turn, right after `modemSearchPoll()`. Both now sit at the top of `loop()`, before any early return such as the WiFi picker or no WiFi. It runs `applyNetworkNow()` once the search has ended (the poll frees it) or 3.5 s have passed.
- `modemSearchForget()` never waits now. It frees a finished search; a search still running after 3.5 s is left to `MDNS.end()`, which frees it.
- No new search starts while a re-apply is pending. Callers and the panel keep running meanwhile.
- Tests: 761 of 761 pass. `pio run -e cyd`: flash 2,037,285 bytes (86.4%).
