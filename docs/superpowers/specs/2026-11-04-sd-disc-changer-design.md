# SD Disc Changer: Design

Date: 2026-11-04. Requested by Rev (sysop, AXIS UNDERGROUND CNC). Replaces SD card "pool + mirror"
as the default way the cluster uses its SD cards.

## Intent

Every SD card in the cluster behaves like one disc in an old CD-ROM changer, the way a Night Owl
shareware CD did on a 90s BBS. Rev pulls a card out of any board, pushes another in, and the BBS
notices by itself: that disc's file areas and files appear or disappear for every caller on every
node, and a "disc in" or "disc out" toast pops up. Descriptions come from the FILE_ID.DIZ built into
each archive, as they did on those discs. The point is the feel of a swappable library, with
curated "discs" Rev can trade in and out, not one big merged drive.

Pool + mirror (just built: commits 70f6564..1ecb3a5) merged every card into one set of areas and
copied files between cards. Rev decided that's not what he wants as the default. It stays in the
code as a build option.

## Goals

1. **A card is a disc.** Only the card's `/files` folder is the disc. Each subfolder of `/files`
   becomes one file area belonging to that disc. Everything else on the card (message bases,
   doors, backups, any other storage) is left alone and never listed.
2. **Hot swap.** When a card is inserted into or removed from any board, the BBS detects it without
   a reboot or a sysop command, and adds or drops that disc's areas within a few seconds.
3. **Blank cards are ignored.** A card with no `/files` folder, or an empty one, does nothing: no
   areas, no toast. When files later appear in its `/files`, the disc shows up.
4. **Toast.** "Disc in" and "disc out" notices: a toast on the CYD screens and a one-line notice to
   callers who are online (the existing node broadcast message is fine).
5. **FILE_ID.DIZ descriptions.** A file with no description gets one from the FILE_ID.DIZ inside
   its archive (ZIP at least), Night Owl style.
6. **Cluster wide.** A disc in any board (main or follower) is visible to callers on every node,
   and its files can be listed, viewed, and downloaded from any node.
7. **Pool + mirror becomes a build option.** A compile-time switch `AXIS_SD_POOL`, off by default.
   Off: the disc changer is built and the pool/mirror code is not compiled in (this also gets the
   headless build back under the flash limit). On: today's pool + mirror behaviour, unchanged.
   One firmware is one or the other. The PC tests keep covering both.
8. **Fork stub.** An ESP-IDF fork is coming later. Keep the disc logic in the portable code
   (`src/bbs`, `src/cluster`), with only the card mount and detect part in the platform layer. Add
   an empty ESP-IDF stub file for that platform part, clearly marked unfinished. **Do not do any
   ESP-IDF conversion now.**

## Non-goals

- No merging of cards into one area, and no mirroring, in the default build.
- No ESP-IDF port work beyond the empty stub.
- No new hardware (no card-detect wiring).
- Not changing how the user base (`/users.dat` in internal flash) is stored.

## Anti-goals (failure even if it "works")

- A pulled card crashes or freezes the board, or a caller mid-download hangs forever.
- A swap loses or corrupts message bases or settings.
- A caller sees files from a disc that's no longer in, or a disc that's in stays invisible.
- The main runs out of memory (it has roughly 48 to 60 KB free) scanning a big disc.

## Constraints and facts from the code

- Boards: CYD (ESP32-2432S028R, no PSRAM). Arduino on PlatformIO today. Card on SPI at `/sd`
  (`src/platform/cyd/storage.cpp`, `SD.begin(5, ...)`). File access already uses POSIX
  `open`/`opendir`/`stat` on `/sd` (`src/platform/cyd/sdfiles.cpp`).
- As far as we know, the CYD card slot has no card-detect pin. Detection has to be done by checking
  the card periodically (for example every few seconds) and remounting when a card reappears. The
  exact method is left to the builder, but it must not stall callers noticeably.
- **The main's card is also its system storage.** `storageRoot()` is `/sd` when a card is present,
  so the message bases (`/sd/msgs`) and door folders (`/sd/doors`) live on the main's card. The
  main is the board with a card (the cluster hands main to the SD board).
- File areas today come from the settings (`areaN=id|name|level` in axis.ini, `BbsConfig::areas`),
  with folders `/sd/files/<id>` made at boot (`makeAreaFolders`).
- Followers already reach the main's files over the cluster link (`RemoteFileLibrary`,
  `FileServer`), and the main can now ask a follower for its card's files (`Cluster::requestTo`,
  `MainReq`/`MainResp`, `CardInfo`, from the pool work). Reuse this plumbing for discs on followers.
- `ZipReader` (`src/bbs/zipreader.h`) already unpacks stored and deflated entries through the ROM's
  tinfl, but needs about 43 KB of workspace in one block (`kWorkspace`). The file browser today only
  shows a FILE_ID.DIZ that is stored uncompressed.
- Flash: cyd build 95.5%, headless 96.6% (limit about 96%).
- Tests: CMake + doctest on the PC (`build/axis_tests.exe`), 487 cases, all passing.

## Decided

- Disc = the card's `/files` folder; each subfolder = one area (Rev).
- Blank card = nothing happens until files show up (Rev).
- Toast on disc in and out (Rev, "maybe pop up a toast").
- Pool + mirror kept as build option `AXIS_SD_POOL`, off by default (Rev agreed to the option).
- Disc changer built now, on the current Arduino firmware, before the ESP-IDF fork (Rev).
- ESP-IDF: stub only, left unfinished (Rev).

## My calls (Claude's, Rev can overrule)

- **Disc name.** The card's FAT volume label. If there's no label, "Disc" plus the number of the
  board it's in. Area names are shown as `<disc name>: <folder>`.
- **Area security and naming.** A disc folder whose name matches a configured area id takes that
  area's security level and shows as `<disc name>: <configured area name>`. Any other folder shows as
  `<disc name>: <folder>` and is open to everyone (security 0).
- **Same folder on two discs, or two copies of one disc:** always separate areas, never merged
  (consistent with "no merging"). Area ids must be unique across boards and discs and stay stable
  while other discs come and go; the scheme is the builder's.
- **Descriptions.** A FILES.BBS description in the folder wins over FILE_ID.DIZ.
- **DIZ extraction happens once, on the board holding the card.** When a disc comes in, that board
  reads the DIZ of files that have no description and caches the result on the card, so a listing
  never unpacks anything. The cache location and format are left to the builder (it must live
  inside `/files` so it travels with the disc, and be hidden from listings). On the main, the
  extraction only runs when the 43 KB workspace fits and nobody is mid-transfer.
## Rev's answers (after the builder check)

- **The main's card is swappable too.** Its system data (message bases `/msgs`, door folders
  `/doors`) is kept mirrored on **another board's SD card** as a fallback. When the main's card is
  pulled while the BBS runs, the BBS keeps working from that backup copy.
- **The main stays main.** Pulling the main's card must not hand the main role to another board
  (today's election prefers a board with a card; once the cluster is running, a card change on the
  main must not trigger that).
- **Uploads go to a fixed upload area** on the main's card, never onto a curated disc.

## My calls (follow from Rev's answers; Rev can overrule)

- **System data vs disc.** On the main's card, `/msgs`, `/doors`, and the fixed upload area are
  system data; only the card's other `/files` subfolders are its disc. The fixed upload area is the
  configured area(s) (`areaN=` in axis.ini); `makeAreaFolders()` keeps making those folders, but only
  on the main's card, and they are listed as permanent areas (no disc name), not as part of a disc.
  Folders on any other card that match a configured area id are disc areas as described above.
- **Where the backup lives.** One follower with a card holds it, in a hidden folder outside
  `/files` (so it is never listed as a disc). Pick the card with the most room; if that board goes
  away, the backup moves to another card board when one is available. The main keeps it current in
  the background (new and changed message-base files and door files), reusing the pool/mirror copy
  machinery where it fits, without blocking callers.
- **While the main's card is out:**
  - The main reads and writes the message bases and doors through the backup board, over the cluster
    link.
  - Uploads keep working: they go to the fixed upload area on the backup card (Rev).
  - The main's own disc areas disappear (disc out toast) like any other disc.
  - If there is no backup (no other card board), messages and doors are offline until a card returns,
    and callers get a clear notice instead of errors. The board must not crash.
- **When a card goes back into the main:**
  - If it is this BBS's system card (it carries a system id the BBS writes), anything that changed on
    the backup while it was out (new messages, door saves) is copied back first, then the main
    switches back to it.
  - **The upload folders are combined (Rev):** files uploaded to the backup card while the main's
    card was out are added to the main's upload area, descriptions included. My call: if both sides
    have a file of the same name with different contents, both are kept (the incoming one gets a
    numbered name) so no upload is ever lost. This applies whichever card becomes the system card.
  - If it is a blank card or a card with no `/msgs`, the system data is copied onto it from the
    backup, and it becomes the system card.
  - If it holds another BBS's `/msgs` (a different system id), it is not adopted. It's treated as a
    disc only, the BBS stays on the backup, and the sysop is told on the console and the screen.
- **What counts as "pulled".** A card that stops answering for a few seconds. The toast and the
  failover happen then; a caller mid-download from that card gets a clean "disc removed" end to the
  transfer, not a hang.

## Left to the builder

- How the backup copy is kept current, and how changes are copied back (file level is fine; the
  message base's own file layout decides what "changed" means).
- The system id format.

- How card presence is checked and how often, and how a remount is done safely.
- How the cluster announces discs (new frames or an extension of `CardInfo`).
- The DIZ cache format.
- The toast's look on the CYD screen.
- Tests: the disc logic must be PC-testable with fake cards that can be "pulled" and "inserted".
