# Sysop Control Panel: Design

Date: 2026-11-04. Requested by Rev (sysop, AXIS UNDERGROUND CNC).

## Intent

One place to run the whole BBS, like an NVR for a security camera system: every board, node and
SD card that is online, with every sysop and BBS control, a way to log into any node as the sysop
from the panel, and a live wall of every node's screen. Today the controls are scattered across
the CYD touch sysop menu, the `*` user editor over telnet, and about thirty `@` commands on each
board's serial console. The panel pulls them together and builds itself from what is online.

## Goals, in Rev's priority order

1. **Controls (first).** Every sysop and BBS control in one place:
   - **Callers and nodes:** who is on each node, what they are doing, time left. Kick, chat with
     them, page and answer, force into the teleconference, add time.
   - **Users:** the `*` editor's powers: list, validate, deny, security level, permissions, free
     downloads, twit, add a user (`@ADDUSER`), make a sysop (`@SYSOP`), set the sysop password
     (`@SYSOPPASS`).
   - **BBS settings:** everything `@SET` and the setup wizard reach (names, theme, areas, doors,
     ratio, time limits), with a save that goes to every board as `@SET` does today.
   - **Lists and content:** rumors (`@RUMOR`), oneliners, BBS list, voting booth, bulletins.
   - **System:** each board's event log (`@LOG`), status (`@NODES`, `@CARDS`, `@MIRROR`, `@WEB`),
     No-IP and the other web extras (`@NOIP`, `@NOIPNOW`, `@DDNS`, `@DISCORD`), the SD card check,
     door state, and LORD Setup.
2. **Local console login (second).** Pick a node and take it over as the sysop: type into the BBS
   from the panel as if sitting at the board's LOCAL console.
3. **Screen wall (third).** One tile per node across every board, showing exactly what that caller
   sees, live. Click a tile to blow it up.
4. **Auto-populates.** Boards, nodes and SD cards appear when they come online and vanish when
   they leave; the tile wall, node info and stats follow along (heap, uptime, WiFi signal, card
   space, callers today, door use).
5. **Phone or PC on the home network.** It opens in a browser on Rev's phone or PC on the LAN.

## Non-goals

- No access from outside the home network (no port forward for the panel).
- No replacement for the boards' own touch console and serial console; both keep working.
- Not part of the disc changer work.

## Anti-goals

- A panel that slows callers down or runs a board out of memory (the CYD main has about 48 to
  60 KB free).
- Sysop powers reachable without the sysop password.
- A control on the panel that does something different from the same control on the board.

## How it fits together (Claude's calls, Rev can overrule)

- **A Windows app on Rev's PC**, running in the system tray. It finds the boards on the LAN by
  itself (the boards already broadcast `AXIS!` announcements on the cluster port; the app can listen
  for them or for mDNS), keeps one connection to each board, and serves the panel as a web page on
  the LAN (`http://<pc>:<port>`), so a phone or a PC browser opens it.
- **The boards talk only to the PC app**, not to every browser, which keeps their memory cost to one
  connection each. Each board streams what is new: node screens (the bytes each caller is sent),
  status and events. It takes commands back (the same actions as the serial `@` commands and the
  touch menu).
- **Login:** the panel asks for the sysop password before showing anything, and the app checks it
  against the BBS sysop account (the boards hold the salted hash; the app never stores the password).
- **Screens:** the app keeps an 80x25 terminal per node by feeding each node's output through an
  ANSI terminal emulator. The browser draws the tiles from that. The firmware's ANSI terminal
  emulator (`bbs/ansiterm`) can be reused on the PC.
- **Console login:** the app opens a sysop session on the chosen board (the same path as the CYD
  LOCAL button) and relays keys and screen between the browser and that session.

## Constraints and facts

- Boards: CYD (ESP32-2432S028R) and headless ESP32 nodes, Arduino on PlatformIO today (an ESP-IDF
  fork is coming later). Today's cluster: board 1 (SD, main, nodes 1+2, 192.168.1.164), board 2
  (nodes 3+4, .165), headless ESP32 (nodes 5 to 7, .202).
- Flash: CYD build about 95.7%, headless about 96.3% (limit about 96%). New firmware code must stay
  small. The disc changer plan (`AXIS_SD_POOL` off by default) frees about 22 KB.
- Existing serial commands: @ADDUSER @CARDS @DDNS @DISCORD @DOORCHECK @DOORLS @DOORSTATE @DOSPASS
  @LED @LOG @LOGCLEAR @MIRROR @NODES @NOIP @NOIPNOW @RESETSETUP @RUMOR @SDCHECK @SET @SHOT @SYSOP
  @SYSOPPASS @TAP @WEB @WEBPING @WEBTEST @WIFIPICK.
- The cluster protocol (frames over TCP between boards) and the per-board telnet server already
  exist. Telnet port 23 is forwarded from the internet to board 1 on an outside port, so anything new
  for the panel must not be reachable through that forward.
- Never put Rev's WiFi name, the test board's public hostname or port, or any password in the repo
  or in screenshots.

## Left to the builder

- The PC app's language and UI toolkit (it must run on Windows 11 and serve the panel to a phone
  browser).
- The board-side protocol (a new TCP port, an extension of the cluster link, or something else), as
  long as it costs a board one connection and little memory, and is LAN only.
- How the boards are discovered.
- The panel's look (it should match the AXIS Oblivion/2 style of the website).
- How screen data is sent (raw caller bytes versus diffs) and how often.
