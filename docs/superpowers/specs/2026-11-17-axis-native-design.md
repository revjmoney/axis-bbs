# AXIS Native: one AXIS program for Linux (then maybe Windows)

Status: design, for Rev's approval. Linux first; Windows decided after Rev tests the Linux build.

## Why

Rev wants AXIS to run as a native program on a PC: first to host a BBS on the i5 (the Ubuntu box) for radio
testing and LAN callers, later possibly on the internet. It must be the same AXIS (same screens, menus, doors,
LORD, users, message bases) as the CYD boards, not a look-alike, and it must show the full WFC like a board does.

The GHOSTBOARD emulator (AXIS in QEMU/PICSimLab on the i5) proved the wish but is slow and fragile; it is
stopped. A native build drops the CYD's memory limits and runs at full PC speed.

## Goals

1. **One executable** (`axis`) that senses how it was started:
   - started from a terminal (stdin and stdout are a TTY): draws the full WFC in that terminal as ANSI;
   - started from the desktop (no TTY, and `DISPLAY` or `WAYLAND_DISPLAY` set): opens a window showing the
     full WFC, drawn pixel for pixel like the CYD screen, scalable;
   - neither (systemd, cron, nohup): runs headless, logging to stdout/journal.
   - `--terminal`, `--window`, `--headless` force a mode.
2. **Same BBS as the boards.** Reuse the portable core unchanged where possible: `src/bbs`, `src/net`,
   `src/cluster`, `src/door`, `src/lord`, `dosemu`, `third_party` (all already built by `CMakeLists.pc.txt`
   as `axiscore` and covered by the 916 host tests). The cluster protocol and board firmware do change, for
   the backup, live follow, mirror card kind and handback (goal 5).
3. **Lines in:** telnet (TCP) and the LFD3K-SM modem on a serial port (the V3 on the i5's USB), the modem
   being a line of its own: answer `RING` (or the modem's auto-answer `CONNECT`), pass bytes, hang up on
   `NO CARRIER`, read LFD3K-LINK reports so the WFC shows `RADIO SFn`. No separate bridge script.
4. **Hard network code.** Built to internet grade from day one (see Network).
5. **Cluster behaviour (Rev's rules):**
   - On start it listens for the cluster (UDP 2325).
   - **No CYD main heard:** runs as its **own separate BBS** on its own data (solo mode).
   - **A CYD main with an SD card is up:** joins as a **follower** with the cluster key and serves the same
     BBS. It **never takes main** from a board that has a card (the existing "cardless main hands main to a
     board with a card" rule must not see the i5's disk as a card in this case).
   - **Daily check, copy only if needed (Rev: "only backup the sd if we need to"):** as a follower, at
     **12:00 local time** each day, the i5 compares the main's card listing (path, size, time) with its
     **mirror folder** and copies only what is missing or different; if the mirror already matches (the
     live follow below normally keeps it so), nothing is copied. The one full copy happens the first time
     the i5 joins a cluster with an empty mirror. Paced so callers on the boards don't feel it. Skipped
     while the i5 is main.
   - **A main without an SD card is up:** the i5 **takes over as main**, serving the cluster's files from
     the mirror (the mirror counts as its card).
   - **The card board comes back** after the takeover: the **i5 stays main**, the board joins as a
     follower; nothing is lost. A sysop command (`@HANDBACK`) copies what changed since the takeover onto
     that board's card and then hands main back to it.
   - **Live follow (Rev, option c):** between noon copies the i5 also copies new posts, uploads and other card
     changes as they happen (the main tells followers what changed; the i5 fetches it, paced). So a takeover
     loses almost nothing, not up to a day.
   - **The card main dies:** the cluster's normal failover runs. If the new main is cardless, the i5 takes
     over from its mirror (kept current by the live follow).
   - The mirror and the solo BBS data are separate folders and never mix.
   - **Firmware and protocol may change (Rev: yes).** The boards get new cluster requests and are reflashed;
     the protocol stays compatible with older boards (an old board ignores what it does not know). New:
     list the whole card tree, read any card file in pieces, a change feed (path, size, time) from the main,
     write a file onto a follower's card (for `@HANDBACK`), and a distinct **mirror** card kind in the
     announcement, so boards know the i5 holds a mirror, not a real card. A board with a real card always
     outranks a mirror for main; a mirror outranks no card.
6. **Exposure:** **LAN only** while part of a cluster. **Internet only in solo mode:** a second, public
   listen port opens only while solo and closes the moment it joins a cluster (Rev forwards that port on the
   router; no UPnP).
7. **i5 hardening** delivered as a script Rev runs with sudo (Claude does not change system security settings
   or use Rev's password): firewall (ufw: SSH and RDP from the LAN only, BBS LAN port from the LAN, the public
   BBS port from anywhere), SSH key-only plus fail2ban, a listing of every listening service, unattended
   security updates. The BBS runs as its own unprivileged user under a locked-down systemd unit.

## Non-goals (this version)

- Windows build (decided after Rev tests Linux). The design keeps the OS-specific code in one place so it can
  follow.
- Automatic handback of main to a returning card board (option 2, rejected: an interrupted copy risks data).
- The i5 becoming main of a cluster that has a card board (only the cardless-main takeover above).
- The CYD's touch screen pages beyond the WFC itself (Wi-Fi picker, hotspot setup, touch keyboard): a PC has a
  keyboard; the sysop uses the WFC hotkeys, a local login, or telnet.
- UPnP or any automatic router changes.

## Anti-goals (failure even if it "works")

- A forked BBS: AXIS behaviour that drifts from the boards because the native program reimplements core logic.
- The i5 grabbing main and the boards' data getting overwritten or lost.
- A public port that stays open while the i5 is in the cluster.
- A slow or malicious caller stalling other callers, or exhausting memory or file handles.

## Approaches considered (window drawing)

- **A. SDL2 (chosen, Claude's call; Rev did not object).** One drawing layer for Linux and Windows; scaling,
  full screen, keyboard and mouse handled. Linux build needs `libsdl2-dev` (Rev runs
  `sudo apt install libsdl2-dev` once; the runtime is already on the i5). Windows later: SDL2 linked
  statically, still one .exe.
- **B. X11 directly.** Nothing to install, but a Windows version would need its own window code, and scaling
  is on us.
- Dropped: a web page (not native), launching a terminal window on double-click (cannot draw the CYD's big
  fonts and art).

## How it is built

### Code layout

- `src/platform/native/` (new): everything OS-specific for the PC program.
  - `netloop.*`: the epoll event loop (Linux); listeners, connections, timers.
  - `linenet.*`: telnet lines (wraps the existing `net/telnet` + `TelnetStream` logic over non-blocking
    sockets).
  - `modemline.*`: the LFD3K-SM serial line (termios, DTR/RTS held so the CP2102 does not reset the board,
    as the GHOSTBOARD bridge does).
  - `clusternet.*`: `axis::cluster::Net` over Linux UDP/TCP (the CYD one is `platform/cyd/clusternet.*`).
  - `config.*`: settings file in the data folder, replacing NVS (`config_store`).
  - `screen_term.*`, `screen_sdl.*`: the two WFC front ends; both paint the same cell grid that
    `platform/cyd/ansi_screen` paints on the CYD (move the shared, hardware-free part of `AnsiScreen`/`BigText`
    into the core if it is not already portable).
  - `startmode.*`: TTY/desktop/headless detection.
  - `mirror.*`: the daily noon copy and `@HANDBACK`.
- `src/native_main.cpp` (new): the program's main. It does what `src/main.cpp` does on the board (wiring
  nodes, WFC snapshot, stats, cluster, doors, nets) but over the native platform. Logic that `main.cpp`
  holds which is not board-specific should be **moved into the core** (shared by both mains) rather than
  copied, where that is a clean move; copying is allowed only for board glue.
- `CMakeLists.pc.txt` gains an `axis` executable target. The board build changes only for the new cluster
  requests (goal 5); boards are reflashed with it (Rev has OK'd reflashing AXIS boards anytime).

### Data folder

`/var/lib/axis` when run as the service, `~/.axis` otherwise (`--data DIR` overrides):
- `solo/`: the solo BBS's card-like tree (themes, menus, users, message bases, file areas, door files), seeded
  on first run from a copy of a board's SD card folder Rev points at (`--seed DIR`), else the stock files.
- `mirror/`: the cluster card mirror, plus a manifest of what was copied and when.
- `axis.conf`: settings (BBS name, ports, cluster key file path, noon time, limits).
- `log/`: rotating logs (the core's `rotatelog`).

### Network (the "badass" part)

- One **epoll** loop, non-blocking sockets everywhere, no thread per caller; doors that need their own time
  slice run as the board runs them (stepped from the loop).
- Limits (all in `axis.conf`, sane defaults): max callers total; max callers per IP; new-connection rate
  per IP and global; handshake timeout (telnet negotiation must finish in N s); idle timeout; max input line
  length; max queued output per caller (the existing hold mode, `kHoldMax`); max telnet subnegotiation length.
- Login protection: the existing login guard, plus a per-IP ban after repeated failures (time-boxed, logged).
- Telnet parser fuzzed (libFuzzer or a doctest junk-input loop) before it ships.
- `SO_REUSEADDR`, `TCP_NODELAY` on caller sockets, `TCP keepalive`, `RLIMIT_NOFILE` checked at start.
- Two listeners: the LAN port (2323, bound to the LAN interface) always; the public port only in solo mode.
- Cluster traffic accepted only from the LAN subnet and only with the cluster key (existing protocol checks).
- Runs as user `axis`, systemd unit with `NoNewPrivileges`, `ProtectSystem=strict`, `ProtectHome`,
  `ReadWritePaths=/var/lib/axis`, `PrivateTmp`, `CapabilityBoundingSet=` (empty), `RestrictAddressFamilies`.

### WFC

- The board builds a `WfcSnapshot` and renders the WFC ANSI through `AnsiTerm` into a cell grid; both native
  front ends reuse that exactly.
- Terminal: write the grid as ANSI to stdout (alternate screen, cursor hidden, restored on exit and on
  Ctrl-C). Keys are the WFC hotkeys.
- Window: SDL2 window at 320x240 times an integer scale (default 3), same font and art as the CYD; mouse
  clicks stand in for touch on the hotkeys and node lines; F11 full screen.
- Headless: no WFC; status lines go to the log.

### Testing

- All existing host tests still pass.
- New host tests: start-mode detection, config file, connection limits and bans (driven through a fake clock
  and loopback sockets), modem line state machine (scripted modem text), mirror manifest and change
  detection, the "never take main from a card board" rule, solo vs follower switching, public port open/close.
- On the i5: telnet calls from the PC, a radio call from the V4 through the V3, a follower join against
  board 1, a noon copy (time forced), and the hardening script run by Rev.

## Calls made in the design (Claude's, for Rev to check)

- **What is mirrored:** the whole card tree (users, message bases, file areas, LORD and door data, themes,
  menus, settings), except logs. Once the i5 is main it writes live changes into `mirror/` and keeps a
  change log since the takeover; that log is what `@HANDBACK` copies back. The noon copy stops while the i5
  is main.
- **Solo mode is silent on the cluster:** while solo the i5 does not announce itself as main (so a card board
  booting later never joins the solo BBS or mixes data with it); it only listens. When it hears a CYD main
  appear, it stops taking new solo callers, closes the public port at once, lets solo callers on the line
  finish (or 10 minutes at most, with a warning on screen), then joins as a follower.
- **Callers the i5 adds to the cluster:** as a follower or cluster main it takes at most 4 nodes, like a
  board (the heartbeat and the boards' WFC node lines are sized for that); extra callers get a "board full"
  screen. In solo mode the limits are the internet-grade ones in `axis.conf`.
- **Radio line reference:** the modem text (`RING`, `CONNECT <tier> ...`, `NO CARRIER`) and the LFD3K-LINK
  report format are taken from `F:\cyd-3000-ghostboard\tools\lfd3k_bridge.py` and the LFD3K-SM repo
  (`F:\lfd3k-sm`, docs and firmware), plus the core's existing `test_lfd3k_link.cpp`.

## Left to the builder

- Exact defaults for the limits; the settings file format (keep it simple, key=value).
- Whether the WFC terminal front end draws BigText as block characters or plain text.
- How the mirror copy is paced (bytes per second or pieces per loop turn).
