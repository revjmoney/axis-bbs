# Door Runtime on the CYD (Stage 3) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans (Rev chose native execution for plan 1). Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The real LORD 4.07 plays over WiFi telnet on the CYD itself: a caller picks Doors from the BBS menu, LORD runs in the board's own door runtime with its code served from the `door` flash partition, and the caller returns to the BBS when it exits.

**Architecture:** The portable door runtime from plan 1 stays as it is; this plan adds the board-side pieces behind its two interfaces (`HostFs`, `ByteStream`) and one new one (`ImageSource`, where the relocated program comes from). On the board the image lives in the 768 KB `door` partition (written once at install, memory-mapped read-only, cached by a content key), files come from the SD card (or LittleFS on a no-SD board) through a case-insensitive `FsHostFs`, and the caller's `WiFiClient` is wrapped with the telnet filter as a `ByteStream`. While a door runs, the 76.8 KB screen copy is freed and the CYD shows a plain status line.

**Tech Stack:** as plan 1 and the firmware (C++17, doctest + CMake on the PC; PlatformIO, Arduino-ESP32 2.x / ESP-IDF 4.4, LovyanGFX, SD, LittleFS, `esp_partition_*`).

**Spec:** `docs/superpowers/specs/2026-11-02-axis-underground-bbs-v1-design.md` sections 5.2 (memory plan, door code in flash), 5.8 (door runtime), 6 (stage 3 exit test).

## Global Constraints

- Measured on the board (2026-11-03): 194,960 bytes free at boot with the screen copy allocated; 108,940 bytes free once WiFi and the listener are up. Freeing the 76,800-byte screen copy during a door leaves about 185 KB. Door RAM budget stays **128 KB** (LORD's measured peak is **100 KB**).
- Partition table is unchanged: `door` raw partition, subtype **0x41**, offset **0x190000**, size **0xC0000** (768 KB). The image is written in 4 KB erase sectors and mapped with `esp_partition_mmap` (64 KB MMU pages, data mapping).
- Image cache key: FNV-1a 32-bit over the original EXE bytes plus the load segment (`kLoadSeg` **0x0200**), stored in NVS namespace `door`, key `img_<door id>`, with the image length. A matching key means no re-flash.
- Door files live on the board under **`/doors/<id>/`** (LORD: `/doors/lord/`); `HostFs` drive C: is that folder. LORD runs as `LORD.EXE 1 /DREW` with `DOOR.SYS` written into the folder and `NODE1.DAT` = `BBSTYPE DOORSYS`, `FOSSIL`, `BBSDROP C:\` (exact text in `tests/test_lord_telnet.cpp`).
- `src/door/*` stays portable (no Arduino/ESP-IDF headers). Board code lives in `src/platform/cyd/`.
- No large stack buffers in code that runs on the Arduino loop task (8 KB stack): buffers over 512 bytes go on the heap or in static storage.
- Serial bench commands follow the existing style (`@NAME args`, one line, reply lines start with `axis:`); never log passwords or WiFi secrets.
- LORD and its files are never committed; they are uploaded to the board's SD card by tooling from a local LORD folder.
- Prose: no em dashes or double hyphens.

## Review Focus

1. The `door` partition holds an older or different image (another door, a previous LORD version, a half-finished write after a power cut): the board must re-flash before running, never execute a stale image. Test: Task 2 `cache key changes when the exe changes`, Task 4 board check `@DOORCHECK` after a forced key mismatch.
2. The caller hangs up during LORD on WiFi: the door must get its grace period, then the board must return to the waiting-for-caller screen with the heap back where it was. Test: Task 5 live check (heap before and after).
3. Free heap is lower than expected (fragmentation after many calls): the session must end with OutOfMemory and the caller must get a clear message, not a reboot. Test: Task 5 `door out of memory returns the caller to the menu` (PC, Node-level) and live check with a 32 KB budget.
4. File names on LittleFS are case-sensitive: `C:\LORD.DAT` must find `lord.dat`. Test: Task 1 resolver tests (listing callback, mixed case).
5. Network writes block while WiFi is weak: the door must not stall the board forever. Test: Task 3 `wifi stream gives up on a stalled write` (bounded write, same rule as the PC stream).

---

### Task 1: Portable pieces the board needs (resolver, image source, stack hygiene)

**Files:**
- Create: `src/door/pathresolve.h/.cpp`, `src/door/imagesource.h`, `tests/test_pathresolve.cpp`
- Modify: `src/door/session.h/.cpp` (`DoorConfig::dir` becomes `std::string`; `load()` reads into a heap buffer; uses `ImageSource` when given), `src/platform/pc/stdhostfs.cpp` (uses the resolver; rejects path parts containing `:`), `CMakeLists.txt`

**Interfaces (produces):**
```cpp
namespace axis::door {
// Turn a DOS path into the real path on a case-sensitive or case-insensitive store.
// list(dir) returns the names in a host directory ("" = the door root); empty if it isn't one.
std::string resolveDosPath(const std::string& cwd, const std::string& dosPath,
                           const std::function<std::vector<std::string>(const std::string&)>& list);
// "" if the path tries to leave the door folder or contains ':' in a part

struct ImageSource {  // where the relocated program image comes from
    virtual const uint8_t* image(const std::vector<uint8_t>& exe, const MzInfo& mz, uint16_t loadSeg, size_t& len) = 0;
    virtual ~ImageSource() = default;
};
// DoorConfig gains: ImageSource* imageSource = nullptr;  (nullptr = relocate into RAM, as on the PC)
}
```
- [ ] Failing tests: `resolver matches names in any case` (list returns `lord.dat`; `C:\LORD.DAT` resolves to `lord.dat`); `resolver keeps the asked case for a new file`; `resolver clamps .. at the root`; `resolver rejects a part with a colon`; `session runs a program from an injected image source` (a test `ImageSource` returning a pre-relocated buffer; program exits with code 3; the session never builds its own copy).
- [ ] Run, FAIL; implement; run the whole suite (LORD included); commit.

### Task 2: Door image cache in the `door` partition (portable key, board writer)

**Files:**
- Create: `src/door/imagekey.h/.cpp`, `tests/test_imagekey.cpp`, `src/platform/cyd/flashimage.h/.cpp`
- Modify: `src/main.cpp` (bench command `@DOORCHECK <id>`)

**Interfaces (produces):**
```cpp
namespace axis::door { uint32_t imageKey(const uint8_t* exe, size_t len, uint16_t loadSeg); }  // FNV-1a 32
class FlashImageSource : public axis::door::ImageSource {  // board only
public:
    explicit FlashImageSource(const char* doorId);
    const uint8_t* image(const std::vector<uint8_t>& exe, const axis::door::MzInfo& mz, uint16_t loadSeg, size_t& len) override;
    // relocates in 4 KB chunks straight into flash (never the whole image in RAM), stores the key last,
    // maps the partition read-only; returns nullptr if the image is bigger than the partition
};
```
- [ ] Failing tests (PC): `cache key is stable`, `cache key changes when the exe changes`, `cache key changes with the load segment`.
- [ ] Implement `imageKey`; PC tests pass.
- [ ] Implement `FlashImageSource`: relocation is done chunk by chunk (apply the relocation entries that fall inside each 4 KB chunk), key written to NVS only after the last chunk verifies, so a power cut leaves a mismatched key and forces a re-flash.
- [ ] Board check: `@DOORCHECK lord` relocates LORD.EXE from the SD card into flash, maps it, compares every byte against a fresh relocation done in small chunks, prints `axis: door image lord ok <len> bytes key <hex>`; a second run prints `cached`; editing one byte of the key in NVS (`@DOORCHECK lord force`) re-flashes.
- [ ] Commit.

### Task 3: Board file system and caller stream

**Files:**
- Create: `src/platform/cyd/fshostfs.h/.cpp` (HostFs over Arduino `fs::FS`, root folder, uses `resolveDosPath`), `src/platform/cyd/wifistream.h/.cpp` (ByteStream over `WiFiClient` + `TelnetFilter`; `peek()`; bounded writes: give up and drop carrier after **10 s** without progress), `tests/test_wifistream_logic.cpp` only if any logic can be made portable (otherwise covered live)
- Modify: `src/main.cpp` (bench `@DOORLS <id>` lists the door folder through FsHostFs)

- [ ] Implement `FsHostFs`: open/read/write/seek/close/stat/remove/rename/mkdir/rmdir/find/fileKey/fileTime against SD (or LittleFS when there is no card); zero-length write truncates (rewrite the file to the current size when the FS has no truncate).
- [ ] Implement `WifiStream`; reuse the existing firmware telnet negotiation (move it out of `main.cpp` so the BBS and doors share one stream class).
- [ ] Board checks: `@DOORLS lord` lists LORD's files with sizes; a telnet call still gets the BBS (the main loop now uses `WifiStream`), screenshots via `tools/screenshots/bbs_shots.py`.
- [ ] Commit.

### Task 4: Getting LORD onto the board

**Files:**
- Create: `tools/door_prepare.py` (PC: copy a LORD folder, run LORDCFG once through `build/doorhost.exe` to create `lord.dat`, write `NODE1.DAT`, drop docs and LORDCFG), `tools/sdput.py` (upload a folder to the board over serial)
- Modify: `src/main.cpp` (serial file commands `@PUT path size` with 512-byte acked chunks and a sum, `@LS dir`, `@RM path`, `@MKDIR path`, `@BAUD n`; same protocol as the CYD Demoscene `@PUT`)

- [ ] Implement the serial file commands; `tools/sdput.py COM18 <folder> /doors/lord` uploads and verifies every file's sum.
- [ ] Run `tools/door_prepare.py F:\axis-bbs-work\doors\lord407\game build/lord-ready` then upload to `/doors/lord/`; `@DOORLS lord` matches the folder; `@DOORCHECK lord` passes.
- [ ] Commit.

### Task 5: Doors from the BBS menu on the board

**Files:**
- Modify: `src/bbs/node.h/.cpp` (menu gains `[D] Doors`, a door list from config, and `doorRequested()` / `resumeAfterDoor(result)`), `tests/test_node.cpp`, `src/main.cpp` (runs `DoorSession` in `loop()` in 30 ms steps, frees the screen copy during the door and draws a plain `IN DOOR: LORD (handle, time)` line straight to the panel, restores the WFC afterwards), `src/bbs/config.h/.cpp` (door list: id, name, exe, args, drop file; default LORD entry)

- [ ] Failing PC tests: `menu shows Doors`; `D lists the configured doors`; `choosing LORD requests the door` (`doorRequested()` returns `lord`); `after the door the caller is back at the menu`; `door out of memory returns the caller to the menu` (with a message).
- [ ] Implement; PC suite passes.
- [ ] Board: telnet in, `D`, `1`, LORD starts; quit LORD; back at the BBS menu; heap printed before and after the door (`axis: door lord done result N, peak RAM K KB, heap before X after Y`).
- [ ] Commit.

### Task 6: Stage 3 exit test, speed and screenshots

**Files:**
- Modify: `tools/screenshots/bbs_shots.py` (prompt-aware mode: a step waits for its regex before typing, like the LORD tests), `docs/FEATURES.md`, brain note
- Create: `tools/doors/bench.py` (writes `BENCH.EXE`: a fixed 8086 loop that prints instructions per second through INT 21h), `shots/lord-cyd/` scripts and screenshots

- [ ] Stage 3 exit test, live on the CYD over WiFi: logon, new character, forest fight, quit, back at the BBS menu; screenshots of each step.
- [ ] A 10-minute automated session (repeated forest fights and town visits): no OutOfMemory, peak RAM recorded.
- [ ] Hang-up during LORD: the board returns to WFC after the grace period with the heap back to its pre-door value (within 4 KB).
- [ ] Speed: run `BENCH.EXE` on the board and on the PC; record instructions per second in FEATURES and the brain (input for plan 4, the assembly pass).
- [ ] Commit; update FEATURES (LORD on the CYD: working), brain status.
