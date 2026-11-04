# SD Disc Changer Implementation Plan (part 1 of 2)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Every SD card in the cluster is a swappable "disc": its `/files` subfolders appear as file areas on every node when it goes in and vanish when it comes out, with a toast, and FILE_ID.DIZ fills in missing descriptions. Pool + mirror becomes the build option `AXIS_SD_POOL`.

**Architecture:** The disc logic is portable C++ tested on the PC: a disc model (`bbs/disc`), a card watcher driven by an abstract card slot (`bbs/discwatch`), a library that routes disc area ids to the card holding them (`cluster/disclib`), a DIZ scanner (`bbs/dizscan`), and a disc announcement in the cluster's `CardInfo` frame. Only the card slot (mount, presence, volume label) and the screen toast are CYD-specific. An unbuilt ESP-IDF stub marks the slot for the fork.

**Tech Stack:** C++17, Arduino ESP32 on PlatformIO (envs `cyd`, `headless`), CMake + MSYS2 g++ + doctest for PC tests, FatFs (`ff.h`) for the volume label.

**Spec:** `docs/superpowers/specs/2026-11-04-sd-disc-changer-design.md`

**Part 2 (separate plan, written after this lands):** the main's system data (`/msgs`, `/doors`, fixed upload area) backed up on another board's card, failover while the main's card is out, copy back and upload-folder combining when it returns. This plan only makes sure pulling the main's card doesn't crash it or move the main role.

## Global Constraints

- A disc is only the card's `/files` folder; each subfolder with at least one file is one area. Nothing outside `/files` is ever listed.
- A blank card (no `/files`, or no subfolder with a file) does nothing: no areas, no toast.
- Area names: `<disc name>: <configured area name>` when the folder matches a configured area id (case-insensitive), else `<disc name>: <folder>`. Matching folders take that area's security level; others are security 0.
- Disc name: the card's FAT volume label, trimmed; if empty, `Disc <node number of the board holding it>`.
- Same folder on two discs, or two copies of one disc: separate areas, never merged.
- On the main's card, folders matching a configured area id are the permanent areas (uploads land there) and are not part of its disc.
- A FILES.BBS description wins over FILE_ID.DIZ.
- Pulling the main's card must not hand the main role to another board, and must not crash it.
- `AXIS_SD_POOL` off by default (pool/mirror not compiled into firmware); the PC tests build with it on so pool tests keep running.
- No ESP-IDF conversion: one stub file only, not compiled, marked unfinished.
- Flash: both `cyd` and `headless` must end at or under 96%.
- Never add AI attribution to commits. No em dashes or double hyphens in user-facing text.
- PC test command: `cmake --build build && ./build/axis_tests.exe` (run from `F:\axis-bbs` in Git Bash).

## Review Focus

1. A card pulled while a caller downloads from it: `readAt` fails within the cluster timeout and the transfer ends; nothing waits forever (Task 3 test `a disc that leaves mid-read fails the read instead of blocking`).
2. A caller sitting in a disc area's listing when the disc leaves: the next listing is empty and the area drops from the area list; no crash (Task 7 test `a disc area that disappears leaves an empty list, not a crash`).
3. The same card re-inserted gets the same area ids (ids come from the volume serial, not insertion order) (Task 2 test `the same disc key always gives the same area ids`).
4. A card whose `/files` holds only empty folders or only FILES.BBS is blank: no disc-in event (Task 4 test `folders without files are a blank card`).
5. A volume label that is empty, all spaces, or longer than 20 characters still gives a clean name (Task 2 test `disc names are trimmed and fall back to Disc N`).

---

### Task 1: `AXIS_SD_POOL` build option

**Files:**
- Modify: `src/cluster/pooledlib.cpp`, `src/cluster/mirrorjob.cpp` (whole body inside `#if AXIS_SD_POOL`)
- Modify: `src/main.cpp` (every pool/mirror use: `cardFiles`, `poolCards`, `pooledFiles`, `mirrorJob`, `@CARDS` pool lines, `@MIRROR`, the mirror step in `loop()`, the pooled branch of `callerFiles()` and the follower FileServer base) inside `#if AXIS_SD_POOL`; with it off, those fall back to `sdFiles` as before the pool work
- Modify: `CMakeLists.txt` (`target_compile_definitions(axis_tests PRIVATE AXIS_SD_POOL=1)`)
- Modify: `platformio.ini` (a comment showing `-DAXIS_SD_POOL=1` as the opt-in; not set)

**Interfaces:**
- Produces: macro `AXIS_SD_POOL` (0/undefined = disc changer build, 1 = pool + mirror build).

- [ ] **Step 1:** Wrap the code as listed. `Cluster::requestTo`, `CardInfo`, `cardBoards()` and `onMainRequest` stay unconditional (Task 6 and 8 reuse them).
- [ ] **Step 2:** Run `cmake --build build && ./build/axis_tests.exe`. Expected: `487 passed | 0 failed`.
- [ ] **Step 3:** Run `pio run -e cyd` and `pio run -e headless`. Expected: both SUCCESS, each Flash percentage lower than 95.5% and 96.6%.
- [ ] **Step 4:** Commit: `Build option AXIS_SD_POOL: pool and mirror only when asked for (off by default)`.

### Task 2: Disc model

**Files:**
- Create: `src/bbs/disc.h`, `src/bbs/disc.cpp`
- Test: `tests/test_disc.cpp` (add both to `CMakeLists.txt`)

**Interfaces:**
- Consumes: `axis::bbs::FileArea` (`src/bbs/config.h`).
- Produces (namespace `axis::bbs`):
  - `struct Disc { std::string key; std::string label; std::string board; std::vector<std::string> folders; };` key: 8 uppercase hex digits of the FAT volume serial; board: cluster board id ("" = this board).
  - `std::string discName(const std::string& volumeLabel, int nodeNumber);` trimmed, non-printable characters dropped, at most 20 characters; empty gives `"Disc " + nodeNumber`.
  - `std::string discAreaId(const std::string& key, const std::string& folder);` returns `key + "." + folder`.
  - `bool splitDiscAreaId(const std::string& id, std::string& key, std::string& folder);` true only for `<8 hex>.<non-empty folder>`.
  - `std::vector<std::string> discFolders(const std::vector<std::string>& foldersWithFiles, const std::vector<FileArea>& configured, bool systemCard);` drops configured ids (case-insensitive) when `systemCard`; sorts case-insensitively.
  - `std::vector<FileArea> discAreas(const std::vector<Disc>& discs, const std::vector<FileArea>& configured);` one area per folder, in disc order, ids from `discAreaId`, names and security per Global Constraints.

- [ ] **Step 1: Write the failing tests** in `tests/test_disc.cpp`:

```cpp
TEST_CASE("a disc's folders become areas named after the disc") {
    std::vector<FileArea> cfg = {{"games", "Game Files", 20}};
    Disc d{"1A2B3C4D", "NIGHTOWL 12", "", {"games", "utils"}};
    auto a = discAreas({d}, cfg);
    REQUIRE(a.size() == 2);
    CHECK(a[0].id == "1A2B3C4D.games");
    CHECK(a[0].name == "NIGHTOWL 12: Game Files");
    CHECK(a[0].minSecurity == 20);
    CHECK(a[1].name == "NIGHTOWL 12: utils");
    CHECK(a[1].minSecurity == 0);
}
TEST_CASE("the same folder on two discs is two areas") { /* two discs with "games": 2 areas, ids differ by key */ }
TEST_CASE("the same disc key always gives the same area ids") {
    CHECK(discAreaId("1A2B3C4D", "games") == "1A2B3C4D.games");
    std::string k, f;
    CHECK(splitDiscAreaId("1A2B3C4D.games", k, f));
    CHECK(k == "1A2B3C4D"); CHECK(f == "games");
    CHECK_FALSE(splitDiscAreaId("games", k, f));
    CHECK_FALSE(splitDiscAreaId("1A2B.games", k, f));
}
TEST_CASE("disc names are trimmed and fall back to Disc N") {
    CHECK(discName("  NIGHTOWL 12 ", 2) == "NIGHTOWL 12");
    CHECK(discName("", 3) == "Disc 3");
    CHECK(discName("    ", 3) == "Disc 3");
    CHECK(discName("ABCDEFGHIJKLMNOPQRSTUVWXYZ", 1) == "ABCDEFGHIJKLMNOPQRST");
}
TEST_CASE("on the system card the configured areas are not part of the disc") {
    std::vector<FileArea> cfg = {{"uploads", "Uploads", 0}};
    CHECK(discFolders({"Uploads", "games"}, cfg, true) == std::vector<std::string>{"games"});
    CHECK(discFolders({"uploads", "games"}, cfg, false).size() == 2);
}
```

- [ ] **Step 2:** Run the tests. Expected: compile failure (`bbs/disc.h` missing).
- [ ] **Step 3:** Implement the functions above in `src/bbs/disc.cpp`.
- [ ] **Step 4:** Run `cmake --build build && ./build/axis_tests.exe`. Expected: all pass (487 + 5).
- [ ] **Step 5:** Commit: `Disc model: a card's /files folders as areas named after the disc`.

### Task 3: Disc library

**Files:**
- Create: `src/cluster/disclib.h`, `src/cluster/disclib.cpp`
- Test: `tests/test_disclib.cpp`

**Interfaces:**
- Consumes: `Disc`, `splitDiscAreaId` (Task 2); `bbs::FileLibrary`.
- Produces (namespace `axis::cluster`):
  - `struct DiscCard { bbs::Disc disc; bbs::FileLibrary* files = nullptr; bool alive = false; };`
  - `class DiscLibrary : public bbs::FileLibrary { public: DiscLibrary(bbs::FileLibrary& system, std::function<std::vector<DiscCard>()> discs); ... };`
  - Area ids that `splitDiscAreaId` accepts go to the card whose `disc.key` matches, with the folder as that card's area id; a missing or not-alive disc gives an empty list, `false`, or `nullptr`. Every other id goes to `system`.
  - Disc areas are read-only through this library: `create`, `remove`, `rename` return `nullptr`/`false`; `list`, `read`, `open`, `describe` pass through.

- [ ] **Step 1: Write the failing tests** (reuse the `MemLibrary` pattern from `tests/test_remotefiles.cpp`):
  - `disc area ids reach the card holding the disc` (list of `"1A2B3C4D.games"` returns the card's `games` files)
  - `other area ids go to the system card` (list of `"uploads"` returns the system library's files)
  - `a disc that is out lists nothing and opens nothing` (alive false: empty list, `open` is nullptr)
  - `uploads never land on a disc` (`create("1A2B3C4D.games", "X.ZIP")` is nullptr; `create("uploads", ...)` is not)
  - `a disc that leaves mid-read fails the read instead of blocking` (open a file, mark the card's library so `readAt` returns false, then `readAt` returns false)
- [ ] **Step 2:** Run. Expected: compile failure.
- [ ] **Step 3:** Implement `DiscLibrary`.
- [ ] **Step 4:** Run the suite. Expected: all pass.
- [ ] **Step 5:** Commit: `Disc library: each disc's areas served from the card holding it`.

### Task 4: Disc watcher

**Files:**
- Create: `src/bbs/discwatch.h`, `src/bbs/discwatch.cpp`
- Test: `tests/test_discwatch.cpp`

**Interfaces:**
- Produces (namespace `axis::bbs`):
  - `class CardSlot { public: virtual bool present() = 0; virtual bool mount() = 0; virtual void unmount() = 0; virtual std::string volumeLabel() = 0; virtual std::string volumeKey() = 0; virtual std::vector<std::string> foldersWithFiles() = 0; virtual ~CardSlot() = default; };` `present()` must do a real card read; `foldersWithFiles()` lists `/files` subfolders holding at least one file other than FILES.BBS and dot files.
  - `class DiscWatcher { public: explicit DiscWatcher(CardSlot& slot); void tick(uint32_t nowMs); bool cardIn() const; bool discIn() const; const std::string& key() const; const std::string& label() const; const std::vector<std::string>& folders() const; void rescan(); std::function<void(bool in)> onDisc; std::function<void(bool in)> onCard; static constexpr uint32_t kPollMs = 3000, kRescanMs = 30000; };`
  - Rules: while a card is in, `present()` every `kPollMs`; two failed checks in a row = card out (unmount, `onCard(false)`, and `onDisc(false)` if a disc was in). While out, try `mount()` every `kPollMs`; success = `onCard(true)`, then read label, key and folders. `discIn()` is true when folders is non-empty; `onDisc(true)` fires when it becomes true (on mount or on a later rescan), `onDisc(false)` when it becomes false. Folders are re-read every `kRescanMs` and on `rescan()`.

- [ ] **Step 1: Write the failing tests** with a `FakeSlot` whose fields (`in`, `label`, `key`, `folders`) the test changes:
  - `a card with files is a disc in` (insert with folders {"games"}: after one poll `onDisc(true)`, `folders()=={"games"}`)
  - `folders without files are a blank card` (folders empty: `onCard(true)` fires, `onDisc` never fires)
  - `files showing up on a blank card bring the disc in` (blank, then folders set, after `kRescanMs` `onDisc(true)`)
  - `one missed check is not a pull, two are` (present false once: still in; twice: `onDisc(false)`, `cardIn()` false)
  - `a new card after a pull is mounted and read` (pull, insert a card with a different key: `key()` is the new key)
- [ ] **Step 2:** Run. Expected: compile failure.
- [ ] **Step 3:** Implement `DiscWatcher`.
- [ ] **Step 4:** Run the suite. Expected: all pass.
- [ ] **Step 5:** Commit: `Disc watcher: notices a card going in or out and a disc appearing on it`.

### Task 5: FILE_ID.DIZ scanner

**Files:**
- Create: `src/bbs/dizscan.h`, `src/bbs/dizscan.cpp`
- Create: `tests/data/diz_deflate.zip`, `tests/data/diz_stored.zip` (made with Python `zipfile`: a `FILE_ID.DIZ` of three lines plus `README.TXT`; one `ZIP_DEFLATED`, one `ZIP_STORED`)
- Test: `tests/test_dizscan.cpp`

**Interfaces:**
- Consumes: `ZipReader` (`src/bbs/zipreader.h`, `kWorkspace`), `FileLibrary`.
- Produces (namespace `axis::bbs`):
  - `std::string dizLine(const std::string& diz);` drops CR and control characters (ANSI escape sequences removed), joins lines with one space, collapses runs of spaces, trims, at most 120 characters.
  - `class DizScanner { public: DizScanner(FileLibrary& lib, std::function<std::vector<std::string>()> areas); bool step(); void reset(); };` `step()` handles at most one file: the next `.ZIP` (case-insensitive) in the next area whose description is empty and that it hasn't tried since `reset()`. It reads `FILE_ID.DIZ` (case-insensitive name, root of the zip, at most 4 KB) with a workspace of `ZipReader::kWorkspace` bytes allocated with `new (std::nothrow)` for that step only, and calls `lib.describe(area, name, dizLine(diz))` when it isn't empty. Returns false when every area is done (or the workspace can't be had), true while there is more.
  - Descriptions are cached by `describe()` into the folder's FILES.BBS, which travels with the disc and is already hidden from listings. (Ruling against the spec's "hidden cache": FILES.BBS is the standard place and needs no new format.)

- [ ] **Step 1: Write the failing tests:**
  - `a DIZ becomes one clean line` (`dizLine("NIGHT OWL\r\n  Disc 12\x1b[1;33m!\r\n") == "NIGHT OWL Disc 12 !"`; a 300-character input gives 120)
  - `a deflated FILE_ID.DIZ fills in a missing description` (MemLibrary area `games` with `diz_deflate.zip`, empty desc: after `step()` calls the describe with the three DIZ lines joined)
  - `a stored FILE_ID.DIZ works too`
  - `a file that already has a description is left alone` (desc "from FILES.BBS": describe never called)
  - `a zip without a DIZ is tried once` (two `step()` calls after `reset()` touch it once; the scanner then reports done)
- [ ] **Step 2:** Run. Expected: compile failure.
- [ ] **Step 3:** Implement.
- [ ] **Step 4:** Run the suite. Expected: all pass.
- [ ] **Step 5:** Commit: `DIZ scanner: FILE_ID.DIZ fills in missing descriptions, one file a step`.

### Task 6: Discs in the cluster

**Files:**
- Modify: `src/cluster/cluster.h`, `src/cluster/cluster.cpp`
- Test: `tests/test_cluster.cpp`

**Interfaces:**
- Consumes: `bbs::Disc` (Task 2).
- Produces:
  - `void Cluster::setDisc(const std::string& key, const std::string& label, const std::vector<std::string>& folders);` (empty key = no disc). A follower sends a `CardInfo` frame at once; the periodic `CardInfo` carries it too.
  - `CardInfo` fields become `{has card "0"/"1", free bytes, key, label, folders joined with '/'}` (folder names can't contain '/'). A main reading an old 2-field `CardInfo` treats it as no disc.
  - `CardBoard` gains `bbs::Disc disc;` (its `board` field = the board id).
  - `void Cluster::setHasFiles(bool)` may now be called at runtime on a follower (a card inserted later): it sends `CardInfo` straight away.
  - Unchanged rule, now pinned by a test: a main never yields because of a card change after it is main.

- [ ] **Step 1: Write the failing tests** (use the existing two-cluster harness in `tests/test_cluster.cpp`):
  - `a follower's disc shows on the main` (follower `setHasFiles(true)`, `setDisc("1A2B3C4D","NIGHTOWL 12",{"games","utils"})`: main `cardBoards()[0].disc.folders == {"games","utils"}`, label and key match)
  - `a disc taken out of a follower leaves the main's list` (`setDisc("", "", {})`: main's board entry has an empty key)
  - `a follower that leaves takes its disc with it` (link closes: `alive` false)
  - `a card inserted into a follower at runtime is announced` (`setHasFiles(true)` after joining: main sees the card)
  - `the main keeps the main role when its own card goes` (main `setHasFiles(false)` at runtime, follower with a card: after several ticks the main is still `Role::Main` and the follower still `Role::Follower`)
- [ ] **Step 2:** Run. Expected: the new tests fail.
- [ ] **Step 3:** Implement. For the last test, the main must not act on its own runtime card loss (no yield, no announce flag change that loses a split-brain tie it would have won).
- [ ] **Step 4:** Run the suite. Expected: all pass.
- [ ] **Step 5:** Commit: `Cluster: each card board announces its disc to the main`.

### Task 7: Disc areas for callers

**Files:**
- Modify: `src/bbs/node.h`, `src/bbs/node.cpp` (line 888: `myAreas_ = openTo(cfg_.areas);`)
- Test: `tests/test_node.cpp`

**Interfaces:**
- Produces: `void Node::setDiscAreas(std::function<std::vector<FileArea>()> get);` The file-area list is `openTo(cfg_.areas + get())`, rebuilt every time the caller opens the file areas.

- [ ] **Step 1: Write the failing tests** (follow the existing file-area tests in `tests/test_node.cpp`):
  - `disc areas are listed after the configured ones` (`setDiscAreas` returns `NIGHTOWL 12: utils`: the area menu shows it after the configured areas)
  - `a disc area that disappears leaves an empty list, not a crash` (caller enters the disc area; the function now returns no disc areas and the library lists nothing for it; caller re-lists and goes back: no crash, and the area is gone from the menu)
- [ ] **Step 2:** Run. Expected: fail.
- [ ] **Step 3:** Implement.
- [ ] **Step 4:** Run the suite. Expected: all pass.
- [ ] **Step 5:** Commit: `Callers see every disc's areas, rebuilt each time they open the file areas`.

### Task 8: CYD wiring (card slot, toasts, @DISCS)

**Files:**
- Create: `src/platform/cyd/cardslot.h`, `src/platform/cyd/cardslot.cpp`
- Modify: `src/platform/cyd/storage.h`, `src/platform/cyd/storage.cpp`, `src/main.cpp`

**Interfaces:**
- Consumes: everything above.
- Produces:
  - `class CydCardSlot : public axis::bbs::CardSlot` in `cardslot.cpp`: `present()` reads sector 0 through `SD.readRAW`; `mount()` is `SD.end()` then the same `SD.begin(...)` sequence as `storageInit()`; `volumeLabel()`/`volumeKey()` from FatFs `f_getlabel("0:", label, &serial)` (key = serial as 8 uppercase hex digits); `foldersWithFiles()` walks `/sd/files`.
  - `storageHasSd()` reports the card as it is now (updated by the slot); `storageRoot()` stays what it was at boot (part 2 handles the main's system data).
  - `main.cpp`: one `DiscWatcher` ticked from `loop()` on every board. On a follower: `setHasFiles`, `setDisc` on changes, `onMainRequest` serves `sdFiles` (already does). On the main: `DiscLibrary(sdFiles, discs)` is `callerFiles()` and the base of the FileServer for followers (when `AXIS_SD_POOL` is off); its disc list is this board's disc (if in, using `discFolders(..., systemCard=true)`) plus every `cardBoards()` entry with a key, each through a `RemoteFileLibrary` over `requestTo`. `Node::setDiscAreas` on every node returns `discAreas(...)` (followers get the main's area list through the existing settings/lists channel: add disc areas to what the main shares). The main never calls `setHasFiles(false)` at runtime.
  - Toast: on any disc in/out the board holding it shows a 4-second box on its CYD screen (`DISC IN: NIGHTOWL 12` / `DISC OUT: NIGHTOWL 12`), and the main sends a node broadcast to callers online with the same text plus the node number.
  - `DizScanner` steps on the board holding a disc, over that disc's folders, only when no door or transfer runs, no upload is open, and `heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) >= ZipReader::kWorkspace + 8192`; `reset()` on every disc in.
  - Serial `@DISCS`: each disc (name, key, board, folders, files described by DIZ so far).

- [ ] **Step 1:** Implement as above.
- [ ] **Step 2:** Run `cmake --build build && ./build/axis_tests.exe` (all pass), then `pio run -e cyd` and `pio run -e headless`. Expected: SUCCESS, both at or under 96% flash.
- [ ] **Step 3:** Flash board 1 (`--upload-port COM18`) and board 2 (COM17). With a card holding `/files/test/HELLO.ZIP` in board 2: `@DISCS` on board 1 lists it; a caller on either board sees `<label>: test`; pull it: toast and broadcast within 10 s, area gone; push it back: back with the same area id. Pull board 1's card: board 1 stays main, no reboot (check with `@NODES` and the serial log), messages may be unavailable (part 2).
- [ ] **Step 4:** Commit: `Disc changer on the CYD: card slot, toasts, @DISCS`.

### Task 9: ESP-IDF stub

**Files:**
- Create: `ports/espidf/cardslot_espidf.cpp` (outside `src/`, so neither PlatformIO nor CMake builds it)

- [ ] **Step 1:** A file whose first lines say `UNFINISHED: the ESP-IDF fork's CardSlot (sdspi mount, f_getlabel). Not built. See src/platform/cyd/cardslot.cpp for the Arduino version.`, then a `CardSlot` subclass skeleton whose methods return `false`/empty, each with a one-line note of the ESP-IDF call it will use (`esp_vfs_fat_sdspi_mount`, `sdmmc_read_sectors`, `f_getlabel`).
- [ ] **Step 2:** Run `pio run -e cyd`. Expected: SUCCESS, size unchanged from Task 8.
- [ ] **Step 3:** Commit: `ESP-IDF fork: unfinished card slot stub`.

### Task 10: Docs and brain

**Files:**
- Modify: `docs/FEATURES.md`, `site/docs.html` (the `#sd-pool` panel becomes "SD disc changer", with pool + mirror described as the `AXIS_SD_POOL` build option; `@DISCS` in the serial table), `README.md` (feature table row), `site/index.html` (landing card text)
- Modify: `D:\brain\Projects\AXIS Underground CNC.md` (status line)

- [ ] **Step 1:** Write the text. Do not put the public hostname or port anywhere public (Rev's rule). No em dashes.
- [ ] **Step 2:** Search `site`, `README.md` and `docs` for the test board's public hostname and port (they are in the brain note, not here). Expected: no matches.
- [ ] **Step 3:** Commit: `Docs: the SD disc changer`.
