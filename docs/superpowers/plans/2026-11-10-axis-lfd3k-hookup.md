# AXIS and LFD3K-SM Hookup Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** AXIS knows when a caller comes in over the LORA FLOWTRON DATAMASTER 3000 SERIAL MODEM - LFD3K-SM, shows radio callers and their live link stats on the WFC and sysop screens, gives slow radio tiers lighter screens, and finds the modem on the LAN (and the modem finds AXIS) so hooking them together takes no typing of IP addresses.

**Architecture:** The two programs stay separate (AXIS is GPLv3, the modem is closed) and talk only over the network. The modem announces link stats inside the telnet stream it already opens to AXIS, as a private telnet option: it offers `IAC WILL 90`; a server that wants it answers `IAC DO 90`, and then the modem sends `IAC SB 90 <ASCII stats> IAC SE` at connect, on every retrain and every 10 s. Any other telnet server refuses the option and nothing changes. Discovery is mDNS both ways: AXIS already advertises `_telnet._tcp` as `axis.local`; the modem advertises `_lfd3k._tcp`. The modem never lets AXIS change its settings; the modem's owner confirms a found board in the modem's menu.

**Repos:** AXIS work in `F:\axis-bbs` on branch `lfd3k-hookup` (public GitHub repo; never pushed without Rev's OK). Modem work in `F:\lfd3k-sm` (private, closed) on its current branch.

**Spec:** `F:\lfd3k-sm\docs\2026-11-09-lfd3k-sm-design.md` section 10 (AXIS integration) and Rev's to-do list in `D:\brain\Projects\LFD3K-SM.md`.

## Global Constraints

- AXIS code stays GPLv3 and never includes modem code; the modem never includes AXIS code. The only shared thing is this wire format, written fresh on each side.
- No AI attribution in any commit (Rev's rule). No em dashes or double hyphens in user-facing text. AXIS secrets rules hold: never print or commit the cluster key, the public hostname or port, `new 1.txt`, or anything in `local/test-credentials.txt` / `local/test-users.txt`; leave Rev's untracked files (churn.txt, crash.txt, live.txt, ring_live.txt, shots/...) alone; `git add` only named files.
- Hardware rules: no subagent opens a serial port or flashes anything. Flashing the AXIS CYD boards follows the brain's AXIS rules (the controller only, after Rev's go and a MAC check); the V4 likewise.
- AXIS tests: `cmake -S . -B build -G Ninja && cmake --build build && build/axis_tests` (doctest); firmware `pio run -e cyd`. Modem tests: `cargo test --workspace` from PowerShell.
- Rev's rule: tests come in the final task of each repo's work; earlier tasks keep existing tests passing.

### Wire format: telnet option 90, "LFD3K-LINK"

- Option number 90 (unassigned by IANA in the 50 to 137 range). Modem offers `IAC WILL 90` right after AXIS's opening negotiation. AXIS answers `IAC DO 90` only when `modemLink` is on in axis.ini (default on).
- Payload: printable ASCII, at most 160 bytes, space-separated `key=value` with no spaces inside values:
  `v=1 modem=<modem name> kind=radio|wifi caller=<caller name or -> tier=<SF5..SF12|WIFI> bps=<air bps> snr=<x.x> rssi=<dBm> retrains=<n> lz=<ratio or 0>`
  Unknown keys are ignored; a missing key means unknown. `v` is the format version.
- Sent at connect (before any caller data reaches AXIS), on every retrain, and every 10 s; never more than once per second.
- AXIS treats it as display and screen-weight information only. It never grants rights, so a spoofed line can at worst mislabel a caller.

## Review Focus

1. A non-AXIS telnet server (or AXIS with `modemLink=0`) must see a normal client: the modem sends nothing on option 90 unless DO 90 came back. Modem test `link_option_only_after_do`.
2. A malformed, oversized or hostile SB 90 payload must never crash AXIS or leak into the caller's input; values are clamped and displayed safely. AXIS tests `link_sb_garbage_ignored`, `link_sb_oversize_dropped`.
3. Stats must not slow the caller's data: SB frames are tiny, rate limited, and the modem never sends them while the caller's data is backed up.
4. Lite screens for slow tiers must never lock a caller out of a feature: the same menus, just lighter art; the sysop can turn it off.
5. Discovery must never change modem settings without the modem owner's confirmation in the modem menu.

---

### Task 1 (modem repo): LFD3K-LINK option and mDNS

**Files:** `F:\lfd3k-sm\core\src\telnet.rs`, `core/src/wire.rs`, `core/src/modem.rs`, `firmware/src/wifi_link.c` (mDNS), `ffi`, `docs/FEATURES.md`.
- Telnet client: offer WILL 90 after the server's opening; on DO 90 mark the link option on; on DONT 90 or no answer, never send it.
- Stats source: the core's link state (tier, bps, last SNR/RSSI, retrains, LZ ratio) and the caller's name for bridged radio calls; `kind=wifi` for a USB wire call (no radio tier).
- Scheduling in tick per the wire format rules; SB bytes go ahead of queued caller data only when the wire output has room (never stalls caller data).
- mDNS: the modem advertises `_lfd3k._tcp` port 23 (its telnet-less service is informational; the port field is the modem's own name record) with TXT `name=<modem name> ver=0.1.0`, and browses `_telnet._tcp` once a minute; found hosts show in the menu's Forwards screen as "Found AXIS board <name> <ip>: add as forward and publish?" (Enter = AT+FWD + AT+PUB, then Save prompt). Nothing is added without that keypress.
- [ ] Build, 150+ tests pass, commit `feat: LFD3K-LINK telnet option and mDNS discovery`.

### Task 2 (AXIS): accept the option and tag the caller

**Files:** `src/net/telnet.h/.cpp`, `src/net/telnetstream.*`, `src/bbs/node.h/.cpp`, `src/main.cpp` (Session), `src/bbs/config.h/.cpp`.
- `BbsConfig`: `modemLink` (default on), `liteBelowBps` (default 7031, i.e. SF9 and slower get lite screens; 0 = never), axis.ini keys and the Settings section entries.
- `TelnetFilter`: on WILL 90 answer DO 90 when enabled (DONT otherwise); collect SB 90 payloads (bounded 160 bytes, discard longer), parse into a `LinkInfo` struct (all fields optional, clamped), never pass any of it to the caller's input.
- `Session`/`Node` carry the latest `LinkInfo` and a `viaModem` flag; `SysInfo`/`WhoLine` get a short link label like `RADIO SF7 -81` or `WIFI`.
- [ ] Host build, existing tests pass, commit `feat: accept LFD3K-LINK stats from the modem`.

### Task 3 (AXIS): show it

**Files:** `src/bbs/wfc.h/.cpp`, `art/wfc*.ans` and theme WFC files only if a new code needs a spot (prefer the existing node line), `docs/WFC-CODES.md`, `src/bbs/sysopshell.*`, `src/main.cpp` (panel rows, who lists).
- WFC: each node line shows the link label for radio callers; new pipe codes for the most recent radio link (tier, bps, SNR, RSSI, retrains, LZ) documented in WFC-CODES.md; `WfcSnapshot::operator==` includes them.
- Board panel (the CYD screen) per-session row "from" shows `RADIO SF7` beside the IP.
- Sysop shell: a `Modem` page under System listing radio callers now and the last 10 radio calls (name, tier range, duration, bytes, retrains), plus the modem(s) found by mDNS (name, IP) and the two settings.
- [ ] Host build, tests pass, commit `feat: radio callers and link stats on the WFC and sysop screens`.

### Task 4 (AXIS): lighter screens for slow tiers

- When a caller's `bps` is below `liteBelowBps`: skip the animated login splash, use the theme's lite art when present (`*.LITE.ANS` beside each ANS, falling back to the normal file), and cap ANSI animation delays to zero. Menus and features stay the same.
- Update the next `LinkInfo` each time: a caller who retrains up to a fast tier gets normal art from the next screen on.
- `docs/MENU-ART.md`: how to make lite art.
- [ ] Commit `feat: lite screens for slow radio callers`.

### Task 5: Tests and bench

- [ ] AXIS doctest: `link_option_negotiation`, `link_sb_parse_all_fields`, `link_sb_garbage_ignored`, `link_sb_oversize_dropped`, `link_sb_split_across_reads`, `link_never_reaches_caller_input`, `wfc_shows_radio_label`, `wfc_codes_link`, `lite_below_threshold`, `lite_off_when_zero`, `config_modem_keys_roundtrip`.
- [ ] Modem tests: `link_option_only_after_do`, `link_stats_rate_limited`, `link_stats_never_block_caller_data`, `link_payload_format`, `mdns_found_board_needs_keypress` (core side of the prompt).
- [ ] Bench (controller, Rev's go for flashing): PC modem over SimNet calls the V4, the V4 bridges to AXIS (board 1 port 23), the WFC and sysop Modem page show `RADIO SF5 ...`, a forced slow tier (`ATS21=2` on the PC side) gets lite screens.

## Later (own plan)

Sysop Net and User Net (message bases, menus, routing internet first and radio fallback, built on Axis Net cards and notes), and the ANSI world map of boards.
