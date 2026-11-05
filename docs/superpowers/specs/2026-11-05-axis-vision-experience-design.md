# AXIS: the ViSiON-style board experience (design)

Date: 2026-11-05. Status: draft for Rev's approval.

## Intent and the why

AXIS already has a working core: telnet nodes, a cluster of boards, users, file areas, message bases, ZMODEM,
DOS doors (LORD runs registered), a sysop panel, Discord, and an ESP-IDF build with room to breathe. What it
lacks is the *feel* of a heavily modded 90s board. Rev's words: "we are almost there but the menus are lacking
and not consistent feeling... the whole flow feels off and the wfc screen is cool but not quite right from the
way a real bbs would have the wfc working."

The target feel is the boards Rev called and ran: ViSiON-X, ViSiON/2, Oblivion/2, Mystic and a heavily
modded Renegade. Lightbars everywhere, heavy ANSI on every screen, small animations, the full normal
main/file/message/door menus, and one consistent, slick flow from connect to logoff.

ViSiON/3 (github.com/ViSiON-3/vision-3-bbs, MIT, cloned read-only at `F:\vision-3-bbs`) is a modern,
readable recreation of ViSiON/2. It is the **reference for every menu, option and flow**: its menus,
keys, settings, editors and login sequence are what AXIS should have, built on our own C++ core.

## Goals

1. **The WFC is the heart of the board.** On the CYD's own screen, the sysop can do *everything*: every
   setting, every user, area, file, post, menu, door, event and log, plus live intervention. No PC or caller
   login needed.
2. **Callers get ViSiON/3's menus, options, keys and flow**, with lightbars *and* hotkeys on every menu.
3. **One really fancy AXIS look** for everything a caller, the sysop or Claude sees: all-new AXIS art in the
   heavy ANSI style of the reference screens (Iniquity WFC, ViSiON/3 MAIN, classic modded Renegade menus).
4. **Slick animation touches:** twinkling, a Larson scanner on the prompt, animated lightbars.
5. **ESP-IDF as we go:** the ESP-IDF build becomes the official firmware now; each area rebuilt for this
   design drops its Arduino pieces for native ESP-IDF ones, until Arduino is gone.

## The WFC (decided with Rev; mockup approved)

The CYD screen is 320x240: 40 columns x 15 rows in the 8x16 font. The shape is **Iniquity's WFC**, filled with
**ViSiON/3's whole sysop toolkit**:

- **Top:** a small AXIS logo, node and time, and ViSiON/3's counter row: total users, new users awaiting
  validation, mail waiting for the sysop, calls today, uptime.
- **Left: the lightbar menu box**, framed, with Iniquity's page indicator and mini scroll bar ("1/6").
  Up/down moves the bar, left/right turns the page, Enter selects, Esc backs out, f-keys/numbers jump.
  Six pages:
  1. **board:** local logon, sysop menu, users, snoop a node, chat, events
  2. **config:** ViSiON/3's config sections that apply (system setup, access and security, new users and
     voting, theme, ...)
  3. **areas:** message areas, file areas, conferences, doors
  4. **menus:** the menu editor, commands, lightbar positions, the login sequence
  5. **admin:** ViSiON/3's admin menu (validate, new-user voting queue, news, voting topics, purge)
  6. **system:** network, cluster, SD, Discord/DDNS, logs, restart
- **Right: "Online Now"** (handle, activity or menu name, node), with ViSiON/3's tabs **Callers / Bots /
  Events** for the activity logs and scheduled events.
- **Sysop pages** flash on the WFC; from it the sysop can **kick, snoop, type into** a caller's session and
  **split-screen chat** (ViSiON/3's wfc console features).
- **Drilling in** opens ViSiON/3's own screen for that function, with its real items and fields, in the same
  lightbar style (e.g. config → access and security → connection limits: max nodes, per IP, failed logins,
  lockout minutes, login tries, idle timeout...). The user editor follows ViSiON/3's `ue` (list with tags,
  then a two-column field editor); the menu editor follows `menuedit` (menu list → menu fields → command list
  → command fields) and **also edits lightbar positions**, which ViSiON/3 cannot.
- A help line along the bottom, as Iniquity has.

ViSiON/3 itself splits these across separate programs (wfc, config, ue, menuedit, the in-BBS admin menu);
AXIS merges them into this one screen.

## The caller side (decided with Rev)

- **Menu set and keys: ViSiON/3's** MAIN, MSGMENU, FILEM, DOORSM, EMAILM, BBSLISTM, RUMORM, INFORMM, ADMIN and
  the rest, with their key bindings and commands (`menus/v3/cfg/*.CFG` in the clone is the source of truth;
  the research summary is in this session's notes).
- **Every menu has lightbars and hotkeys together:** press the key, or arrow to the item and hit Enter.
  Yes/no prompts are lightbars too, as in ViSiON/3.
- **Login flow exactly as ViSiON/3 does it:** the pre-login lightbar matrix (log in, new user, check access,
  disconnect), random pre-logon art, the login screen, the configurable login sequence (sysop notices, mail
  scan, the fast-login lightbar, news, last callers, oneliners, your stats, new-user checks, new-user voting),
  then MAIN. The new-user application follows ViSiON/3's steps in order.
- **Menus are data, like ViSiON/3's:** per menu, its settings (title, clear screen, prompts, fallback, access,
  password, hotkey mode...), its commands (keys, command, access, hidden, node activity, auto-run), its art,
  and its lightbar layout (item positions and colours over the art). They live on the board's storage and
  the WFC's menu editor changes them.
- ViSiON/3's art codes worth having come along: pipe colours, placeholders with width and alignment, and
  conditional regions (e.g. a sysop-only item that blanks to the same width for others).

## Look and animation (decided with Rev)

- **One AXIS look only.** The three selectable looks (axis, oblivion, iniquity) are retired. Every screen gets
  new, really fancy AXIS art in the heavy ANSI style of the reference shots.
- **Animations:** twinkling (stars/sparkles on art), a Larson scanner on the prompt line, animated lightbars
  (the bar slides or glows as it moves). Not full ANSI movies (the existing RotatoCAM connect splash stays).

## Non-goals

- Not a port of ViSiON/3's Go code: it is the reference for *what* and *how it flows*; the code is ours.
- ViSiON/3 features with no meaning on an ESP32 board are not built now (SSH and SSH keys, the dosemu path,
  ZipLab virus scanning, the QWK mobile API). **[Claude's call; Rev can overrule.]**
- No change to the cluster, the door runtime, file transfers or the stored data formats beyond what the
  menus need.

## Anti-goals (what would make it a failure even if it works)

- Screens or menus that feel like different programs (inconsistent keys, colours, frames or prompts).
- Copying ViSiON/3's, Iniquity's or anyone else's art or branding: all art is new AXIS art.
- Losing or rewriting the existing SD card data, users, messages or files.
- Crashes from RAM: the main board must stay stable with callers, a door and the WFC all busy.

## Constraints

- Board: ESP32 CYD, no PSRAM. ESP-IDF build: ~72 KB free on the main idle (was ~43 KB on Arduino). Any screen,
  list or menu must stream rather than build large strings (today's crashes were all big single
  allocations).
- Flash: ~94% used on the ESP-IDF build; retiring the old sysop shell, touch sysop menu and the two extra
  looks should win room back.
- CYD screen 40x15 in 8x16; callers are 80x25 over telnet (SyncTERM, NetRunner).
- Animations must be cheap on bandwidth and CPU: small cursor-addressed updates, never full redraws, and
  they stop while a transfer or door runs.
- Everything goes through the existing `@` command path so serial, panel, WFC and the in-BBS admin menu stay
  one code path.
- ViSiON/3 is MIT: note it in THIRD_PARTY_NOTICES as the design reference.

## Decisions from the review

- **WFC input (Rev): touch on the board plus a keyboard through the PC panel app** (or USB serial). On the
  CYD: tap a row to pick it, swipe or tap the page indicator to turn pages, an on-screen keyboard for text.
  From the PC: the panel shows the WFC and sends real arrows, Enter, Esc, f-keys and typing, so the field
  editors, snoop type-in and chat work like a local keyboard. No Bluetooth (it would eat the RAM we freed).
- **ViSiON/3 features AXIS lacks get built as real features**, with their own storage, because Rev asked for
  every menu and option: news, new-user voting, conferences, InfoForms, fast login, and the rest the menus
  reach. Oneliners, rumors, the BBS list, voting and last callers already exist and get ViSiON/3's flows.
- **The Bots tab** is ViSiON/3's: activity from connections that never logged in (scanners, bots).
- **Every board in the cluster** serves the same menu engine, menus and art. Menus and art come from the main's
  SD card, fetched over the cluster as theme art already is; the main's WFC shows, snoops and kicks nodes on
  every board through the existing cluster and panel screen feeds. **[Claude's call]**
- **Storage:** menus, lightbar layouts and art live on the main's SD card. Firmware keeps only a small plain
  fallback set (enough to log in and reach the sysop menus) for a missing or blank card. **[Claude's call]**
- **Access:** ViSiON/3's ACS strings map onto AXIS's existing security levels and flags (s10 = level,
  fX = flag); user records are not rewritten. **[Claude's call]**
- **ESP-IDF as we go:** Arduino pieces (display, touch, WiFi wrappers) may stay as a shim inside the ESP-IDF
  build until each is replaced; replacing them never blocks a feature. **[Claude's call]**

## Left to the builder

- The order of work (suggested: menu engine with lightbar+hotkey and art overlay first, then the login flow,
  then caller menus area by area, then the WFC, then the art pass and animations, with the ESP-IDF swaps
  riding along with each area).
- The on-board file format for menus (ViSiON/3 uses JSON; something leaner may suit the ESP32).
- The art itself, animation timings, and exact colours, within "one fancy AXIS look".
- How each existing feature (LORD setup, ANSI pack viewer, multinode chat, panel) maps onto the new menus.
