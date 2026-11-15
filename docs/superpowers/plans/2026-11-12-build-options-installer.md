# AXIS Build Options and Installer Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Feature switches for the AXIS Micro BBS firmware, prebuilt presets and on-request custom builds with measured costs, a branded web installer that flashes the board and/or the LORA FLOWTRON DATAMASTER 3000 SERIAL MODEM - LFD3K-SM from the browser, and build-it-yourself scripts for Windows and Linux.

**Architecture:** One JSON source of truth (`installer/`) lists features, dependencies and presets; a Python helper turns a selection into build flags and a fingerprint; a PlatformIO extra script applies the flags. CI builds every preset on a release and publishes images, manifests and a cost table to a `gh-pages` branch that keeps them between deploys; a custom-build workflow does the same for one selection on request. The installer page (plain HTML/JS, esptool-js) and the PC scripts read the same JSON.

**Tech Stack:** C++17 firmware (PlatformIO, espressif32 6.9.0, Arduino + ESP-IDF 4.4.7), Python 3.12 (stdlib only, `unittest`), plain JavaScript (`node --test` for the logic), esptool-js (exact pinned version from cdn.jsdelivr.net/npm), GitHub Actions, GitHub Pages from the `gh-pages` branch, PowerShell 5.1 and POSIX sh.

**Spec:** `docs/superpowers/specs/2026-11-12-build-options-installer-design.md` (approved by Rev 2026-11-12). Mockup: `.superpowers/brainstorm/54584-1794514420/content/installer-page-v4.html` in `F:\axis-bbs` (local only; copy what you need).

## Global Constraints

- Updates write the app only (board: `app0` at 0x10000, 0x240000 bytes); never NVS (0x9000), otadata, LittleFS (0x250000), the SD card, or the radio's settings partition; never erase the chip.
- First install only when the board does not already run AXIS (or the radio LFD3K-SM) with the same partition layout, and only after the warning "This replaces everything on this board. Whatever it runs now, and its settings, will be gone." and an explicit confirm.
- Full names everywhere a person reads: "AXIS Micro BBS", "LORA FLOWTRON DATAMASTER 3000 SERIAL MODEM - LFD3K-SM"; branding "by Rev. J. Money / jmscnc.com" and the Simian Tactical Unit monkey logo (`D:\chaosape-site\img\simian-logo.png`).
- No em dashes and no double hyphens in any text a person reads (page, logs, script output, docs). Command flags keep their `--`.
- Every feature switch defaults ON; "Everything" equals today's `env:cyd` with no switch changed (`AXIS_SD_POOL` stays off).
- Windows (PowerShell 5.1) and Linux (sh) only; no Mac.
- Pin every GitHub Action by commit SHA (as `release.yml` does) and every CDN script by exact version.
- No secrets anywhere (Wi-Fi passwords, the cluster key, the public hostname, net seeds); scan before every push; no AI attribution in commits; `git add` named files only.
- Host tests: `cmake -S . -B build -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++` then `build/axis_tests` (test_four is flaky). Firmware: `export PLATFORMIO_CORE_DIR='F:\CodexStorage\Tools\platformio'; pio run -e cyd`.
- Testing on hardware is batched into the last task (Rev's rule). Flash Rev's boards only after checking the MAC (board 1 COM20 8c:94:df:4f:1c:4c, board 2 COM21 8c:94:df:4f:14:18), board 2 first; never COM9, COM11 or COM15 without asking.

## Review Focus

1. A selection that breaks a dependency (viewer without file areas) arriving from the issue form, the script or a hand-edited URL: refused everywhere with the reason, never built. Test in Task 1 (Python) and Task 8 (JS).
2. A board that runs AXIS with a different partition table, or a CYD image offered to a radio (or the reverse): the page detects it and does a first install with the warning, or refuses the mismatch; it never writes an app onto a foreign layout. Test in Task 8 (layout decision function).
3. The gh-pages publish racing a site deploy or another custom build: no published release or build is lost. Test in Task 6 (prune and merge logic run twice on the same tree).
4. Untrusted issue text (extra fields, a switch name with shell characters): never reaches a shell or the build; rejected. Test in Task 7 (parser).
5. The script run twice, or after a failed download: it reuses the tool folder and finishes; a partial download is not trusted. Test in Task 9 (dry-run mode on CI).

---

### Task 1: The feature and preset lists, and the selection helper

**Files:**
- Create: `installer/features.json`, `installer/presets.json`, `installer/peaks.json`, `installer/radio.json`
- Create: `tools/buildopts.py`
- Test: `tools/test_buildopts.py` (stdlib `unittest`; added to the `host` job in `.github/workflows/build.yml`)

**Interfaces:**
- Produces: `features.json` = list of `{ "id": "lord", "define": "AXIS_LORD", "label": "LORD", "about": "...", "needs": [], "needs_sd": true }` for ids `lord, scripts, dosprompt, files, ansiview, transfers, cluster, modemlink, web`, plus reserved `nets` and `gpio` with `"reserved": true` (not selectable yet). `presets.json` = list of `{ "id": "everything", "label": "Everything", "about": "Rev's board", "off": [] }`, `no-doors` (off lord, scripts, dosprompt), `no-files` (off files, ansiview, transfers), `no-radio` (off modemlink), `just-bbs` (off every selectable id), `no-sd` (off every feature with `needs_sd`: expected files, ansiview, transfers, lord, scripts; Task 5 confirms the list on a board with no card and corrects `needs_sd`). `peaks.json` = `{ id: { "peak_kb": n, "what": "..." } }` from the memory report (LORD about 20 KB with its event script, the ZIP window 44 KB, transfers, TLS about 48 KB for web). `radio.json` = `{ "version": "", "site": "https://revjmoney.github.io/lfd3k-sm/releases/" }`.
- Produces in `tools/buildopts.py`: `load(root) -> (features, presets)`, `resolve(off: set[str]) -> set[str]` (raises `ValueError` naming the broken dependency), `fingerprint(off: set[str], release: str) -> str` (first 12 hex of sha256 of `release + "|" + ",".join(sorted(off))`), `build_flags(off) -> list[str]` (`-DAXIS_X=0` for each off id, nothing for on), and a CLI: `python -I tools/buildopts.py flags --preset no-doors` / `--off a,b` prints the flags; `fingerprint ...` prints it; `check ...` exits 1 with the reason.

- [ ] **Step 1: Write the failing tests** in `tools/test_buildopts.py`: every preset resolves; `just-bbs` has every selectable id off; `resolve({"files"})` raises `ValueError` mentioning `ansiview` and `transfers`; a reserved id or an unknown id raises; `fingerprint` is stable across set order and changes with the release; `build_flags({"lord"}) == ["-DAXIS_LORD=0"]`; `everything` gives no flags.
- [ ] **Step 2: Run** `python -I -m unittest tools/test_buildopts.py` and see them fail.
- [ ] **Step 3: Write the four JSON files and `tools/buildopts.py`.**
- [ ] **Step 4: Run** the tests; PASS. Add the command to the `host` job.
- [ ] **Step 5: Commit** `feat: feature and preset lists, the selection helper`.

### Task 2: The switch mechanism in the build

**Files:**
- Create: `src/bbs/features.h` (each `AXIS_X` defaults to 1 when not defined; `static_assert`s for the dependencies in `features.json`)
- Create: `tools/pio_features.py` (PlatformIO extra script: reads `AXIS_OFF` (comma list) or `AXIS_PRESET` from the environment, calls `buildopts`, appends the flags; with neither set, does nothing)
- Modify: `platformio.ini` (add `pre:tools/pio_features.py` to `extra_scripts` of `env:cyd`), `CMakeLists.pc.txt` (an `AXIS_OFF` cache variable passed the same way, so host tests can build a switched-off core)
- Test: `tests/test_features.cpp` (host): with default flags every `AXIS_X` is 1

**Interfaces:**
- Consumes: `tools/buildopts.py` (Task 1).
- Produces: `#include "bbs/features.h"` and `#if AXIS_LORD` (etc.) for later tasks; `AXIS_OFF=lord,scripts pio run -e cyd` builds that combination.

- [ ] **Step 1: Write the failing host test** (all defines 1 by default).
- [ ] **Step 2: Implement** `features.h`, `pio_features.py`, the ini and CMake hooks.
- [ ] **Step 3: Verify:** host tests pass; `pio run -e cyd` (no env) gives a byte-identical `firmware.bin` size to before; `AXIS_OFF=lord pio run -e cyd` passes `-DAXIS_LORD=0` (seen in the verbose build) even though nothing is fenced yet.
- [ ] **Step 4: Commit** `feat: build switches from AXIS_OFF or AXIS_PRESET`.

### Task 3: Fence the doors (LORD, scripts, DOS prompt)

**Files:** `src/lord/*` (whole directory compiled only with `AXIS_LORD`), `src/bbs/script.*` (`AXIS_SCRIPTS`), `src/bbs/dosshell.*` (`AXIS_DOSPROMPT`), `src/main.cpp` (door dispatch at the `*LORD`, `*SCRIPT`, `*DOSSHELL` sites and their sysop/serial commands), `src/bbs/config.cpp` (doors list), menus and the sysop pages that list doors.

**Interfaces:**
- Consumes: `features.h` (Task 2).
- Produces: a build with any of the three off compiles and links; the Doors menu and the setup pages show only built-in doors; a saved settings line naming an absent door is kept as is (never rewritten or deleted) and simply not offered.

- [ ] **Step 1: Write failing host tests** (`tests/test_features.cpp`): with `AXIS_OFF=lord,scripts,dosprompt` the core builds and the door list offered to a caller contains none of them while the saved `doorN=` lines round-trip unchanged.
- [ ] **Step 2: Fence the code** at whole-file level where a feature owns the file, at call sites otherwise (no `#if` scattered inside unrelated functions when a small stub keeps the caller clean).
- [ ] **Step 3: Verify:** host tests pass with default flags and with the three off; `pio run -e cyd` with `AXIS_PRESET=no-doors` builds; record its `firmware.bin` size against Everything.
- [ ] **Step 4: Commit** `feat: doors can be built out`.

### Task 4: Fence file areas, transfers and the ANSI viewer

**Files:** `src/bbs/filelib.*`, `filebrowser.*`, `uploadproc.*`, `transferjob.*`, `archive.*`, `zipreader.*`, `inflatelease.*`, `ansiviewer.*`, `viewcache.*`, `src/net/xmodem.*`, `zmodem.*`, `src/platform/cyd/sdfiles.*` (file area side only; the SD card itself stays), `src/cluster/remotefiles.*`, `pooledlib.*`, `mirrorjob.*`, `src/main.cpp` (Transfer menu, viewCacheTick, makeAreaFolders), menus.

**Interfaces:** Produces: `AXIS_PRESET=no-files` builds; the Transfer menu and the viewer door vanish; the SD card still holds users, messages and themes.

- [ ] **Step 1: Failing host tests:** with `files,ansiview,transfers` off the core builds; `resolve` refuses `files` off alone (Task 1 already); the main menu offered has no Transfer entry.
- [ ] **Step 2: Fence.**
- [ ] **Step 3: Verify** host tests both ways and `pio run` for `no-files`; record the size.
- [ ] **Step 4: Commit** `feat: file areas, transfers and the ANSI viewer can be built out`.

### Task 5: Fence the cluster, the modem link and the web jobs, and prove Just a BBS

**Files:** `src/cluster/*` (`AXIS_CLUSTER`), the LFD3K-LINK code (option 90, `@MODEM`, the Modem page, WFC `|R` codes, mDNS `_lfd3k` search) (`AXIS_MODEMLINK`), `src/platform/cyd/web.*`, `src/bbs/noip.*`, `webwork.h`, announce (`AXIS_WEB`), `src/main.cpp`.

**Interfaces:** Produces: `no-radio` and `just-bbs` build; a board built without the cluster runs alone (it never joins or announces); without `web` there are no DNS or Discord jobs and their settings stay saved but unused.

- [ ] **Step 1: Failing host tests** for each switch off, and all selectable switches off together.
- [ ] **Step 2: Fence.**
- [ ] **Step 3: SD card notice:** when the board has no card, is not a follower of a main that has one, and was built with any `needs_sd` feature, the WFC shows "YOU NEED AN SD CARD FOR FULL FUNCTIONS" and `@STATUS` adds `sd=missing`; host test for the message choice (a `needs_sd` feature built in, no card and running alone gives the notice; the same as a follower of a main with a card gives none; the `no-sd` preset gives none).
- [ ] **Step 4: Verify:** every preset in `presets.json` builds with `pio run` (a loop over `buildopts.py presets`); host tests pass for default and `just-bbs`; write the five sizes into the task report.
- [ ] **Step 5: Commit** `feat: cluster, modem link and web jobs can be built out; the SD card notice`.

### Task 6: Release builds, the cost table, and Pages from gh-pages

**Files:**
- Create: `tools/buildcost.py` (`measure(build_dir) -> {"flash": int, "dram_static": int}` from `firmware.bin` size and the `.map` / `xtensa-esp32-elf-size`; `table(results, peaks) -> costs.json` with flash used, flash freed against Everything, and the spikes gone for each preset)
- Create: `tools/ghpages.py` (`merge(site_dir, gh_pages_dir)` copies the site's own files without touching `releases/` or `builds/`; `prune(gh_pages_dir, keep_releases=3, builds_cap=300*1024*1024)` drops the oldest releases past three and the oldest builds past the cap)
- Modify: `.github/workflows/pages.yml` (check out `gh-pages`, `ghpages.merge`, commit, push; GitHub Pages switched to "deploy from branch gh-pages" by Rev in the repo settings, noted in the task report), `.github/workflows/release.yml` (matrix over presets: build each, `buildcost`, write `releases/<tag>/<preset>/` with `firmware.bin`, `bootloader.bin`, `partitions.bin`, `boot_app0.bin` if PlatformIO makes it, and `manifest.json` (offsets, sha256, size, chip `ESP32`, preset, release), plus `releases/<tag>/costs.json` and `releases/index.json` (latest and previous), and the SD card kit zip (see Step 3); keep attaching the files to the GitHub Release as today)
- Test: `tools/test_buildcost.py`, `tools/test_ghpages.py`

**Interfaces:** Produces the URLs the page and scripts use: `https://revjmoney.github.io/axis-bbs/releases/index.json`, `.../releases/<tag>/costs.json`, `.../releases/<tag>/<preset>/manifest.json`, `.../releases/<tag>/sdcard-kit.zip`.

- [ ] **Step 1: Failing tests:** `table` gives freed = Everything minus preset; `merge` keeps an existing `releases/v1/` and `builds/abc/`; `prune` run twice gives the same tree and keeps exactly the three newest releases.
- [ ] **Step 2: Implement** `buildcost.py` and `ghpages.py`.
- [ ] **Step 3: SD card kit (Claude's call, flagged to Rev in the plan handoff):** a first install needs the card files `tools/install_cyd.py --stage` already prepares (themes, screens, LORD data, scripts); the release zips that staged folder as `sdcard-kit.zip`, and the page and scripts point to it for first installs.
- [ ] **Step 4: Workflows:** edit `pages.yml` and `release.yml`; validate them with `actionlint` if available, otherwise a YAML parse in the test.
- [ ] **Step 5: Run** the Python tests; PASS. Commit `feat: release builds every preset and publishes costs to gh-pages`.

### Task 7: Custom builds on request

**Files:**
- Create: `.github/ISSUE_TEMPLATE/build-request.yml` (a checkbox per selectable feature from `features.json`, the board target CYD or bare ESP32, nothing free-text that reaches the build)
- Create: `.github/workflows/custom-build.yml` (on issues opened/edited with the build-request label: job 1 `parse` (read-only, no checkout of untrusted refs) runs `tools/build_request.py`; job 2 `build` (contents: read) builds with `AXIS_OFF`; job 3 `publish` (contents: write to gh-pages only) copies the size-checked image to `builds/<fingerprint>/`, runs `ghpages.prune`, comments the installer link `install.html?build=<fingerprint>`, closes the issue)
- Create: `tools/build_request.py` (`parse(issue_body) -> (off: set[str], target: str)` from the form's exact checkbox lines only; `rate_ok(user, today_requests) -> bool` with a cap of 3 new builds per user per day, counted from the user's issues with the label in the last 24 h via the API; an existing fingerprint short-circuits to the link)
- Test: `tools/test_build_request.py`

- [ ] **Step 1: Failing tests:** a body with the form's lines parses; an unknown checkbox, a switch name with `;` or `$(`, or a free-text injection is rejected; a dependency break is rejected with its reason; the fourth request in 24 h is refused; the fingerprint matches `buildopts.fingerprint`.
- [ ] **Step 2: Implement** the parser, the form and the workflow (untrusted text only ever goes through `build_request.py`, passed by file, never interpolated into `run:` lines).
- [ ] **Step 3: Run** the tests; PASS. Commit `feat: custom builds from a GitHub issue`.

### Task 8: The installer page

**Files:**
- Create: `site/install.html`, `site/install.css`, `site/install.js` (the page), `site/installer-logic.js` (pure functions, no DOM), `site/media/simian-logo.png`
- Test: `site/installer-logic.test.mjs` (`node --test`)
- Modify: `site/index.html` (a link to the installer), the LFD3K-SM README and site get a link in Task 10

**Interfaces:**
- Consumes: `releases/index.json`, `costs.json`, `manifest.json` (Task 6), `builds/<fingerprint>/` (Task 7), `installer/features.json` and `presets.json` (copied into the site by `pages.yml`), the radio manifests (Task 10).
- Produces in `installer-logic.js`: `selection(presetId | offList)`, `checkDeps(off) -> null | reason`, `fingerprint(off, release)` (same result as the Python one, tested against fixtures written by Task 1), `planFlash({chip, partTable, appHeader}, target) -> { kind: "update" | "first" | "refuse", regions: [...], reason }`, `stageName(percent) -> "BL4Z1NG BYT3S" | "FL0WTR0N ENGAGED" | "S0N1C B00M" | "DATAMASTER WRITING"`.

- [ ] **Step 1: Failing tests** in `installer-logic.test.mjs`: deps refused with the reason; fingerprint equals the Python fixture; `planFlash` gives `update` (app region only, offset 0x10000) for a board whose partition table matches, `first` (bootloader 0x1000, partitions 0x8000, app) for an unknown or different table, `refuse` for a radio chip (ESP32-S3) offered the board image and for a CYD offered a radio image; a V4 with the multimode table gets only the modem slot.
- [ ] **Step 2: Implement `installer-logic.js`**; tests PASS.
- [ ] **Step 2b: 8-bit icons:** `installer/icons.json` holds a 16x16 grid per id (every feature id, every preset id, `cyd`, `esp32`, `heltec_v4`, `heltec_v3`, `both`, `sd`, `release`, `install`, `custom`, `step1` to `step3`), each row a 16-character string over a palette key (`.` transparent, `a` amber #f5a623, `c` cyan #55ffff, `g` green #55ff55, `w` white, `k` grey); `tools/icons.py` writes `site/icons/<id>.svg` (one rect per run of pixels) and fails on a wrong size, an unknown palette letter or a missing id from features/presets; a test in `tools/test_icons.py` covers those three failures. The icons are original pixel art drawn for AXIS (no copies of game, app or icon-set art). The page shows them scaled by whole multiples with `image-rendering: pixelated`, next to every choice, preset row, feature box, step heading and the Install button.
- [ ] **Step 3: Build the page** from the approved mockup: a large "YOU NEED AN SD CARD FOR FULL FUNCTIONS" notice near the top and an SD badge on every feature that needs the card (from `needs_sd`), with the No SD card preset for boards without one; monkey and jmscnc branding, full names, blinking OH CD SD RD HS EC DC AA lights, flickering title, release-name marquee, pulsing Install button, scanlines, `prefers-reduced-motion` honoured; a release picker (latest by default, the previous one offered, from `releases/index.json`); step 1 board (CYD or bare ESP32) / radio (V4 or V3) / both; step 2 presets from `costs.json` with the honest memory note, and Custom (opens the issue form prefilled); step 3 Install with esptool-js (exact pinned version): read the chip and MAC, read 0x8000 and the app header, show the first-install warning and require a confirm when `planFlash` says `first`, write with real per-block progress driving the block bar, the KB counter, the stage names and the text log; for a bare ESP32 send `@HEADLESS on` over the same port after the reset; then the radio if "both"; Chrome or Edge only (others get a plain message); "Or build it on your own PC" fills the Task 9 command from the picks; the SD card kit link for first installs.
- [ ] **Step 4: Verify** in the browser pane against a local `python -m http.server` of the assembled site with fixture manifests (no hardware): every step, the warning, the refusal, the log; screenshots for Rev.
- [ ] **Step 5: Commit** `feat: the AXIS Micro BBS installer page`.

### Task 9: Build-it-yourself scripts

**Files:** `site/install.ps1` (Windows PowerShell 5.1), `site/install.sh` (POSIX sh), `tools/axis_flash.py` (shared by both: reads the board like the page does and flashes `update` or `first` with the same warning and confirm, with esptool; prints the block bar, stage names and log)

**Interfaces:** `install.ps1 -Board <preset>|-Off a,b -Target cyd|esp32 -Radio v3|v4 -Flash -Port COMx -Release <tag>`, `install.sh --board ... --off ... --target ... --radio ... --flash --port /dev/ttyUSB0 --release <tag>`; `--dry-run` prints every step without downloading or flashing (used by CI).

- [ ] **Step 1: Failing tests:** `tools/test_axis_flash.py` (the decision is the same as `planFlash` for the same inputs; the warning text is exact); a CI job runs both scripts with `--dry-run` on ubuntu (sh) and windows (PowerShell) runners and checks the printed plan.
- [ ] **Step 2: Implement:** the first screen of both scripts says "YOU NEED AN SD CARD FOR FULL FUNCTIONS" (and that `--board no-sd` exists for boards without one); tools go into `%LOCALAPPDATA%\axis-build` or `~/.axis-build` (Python embeddable or venv, PlatformIO pinned 6.1.18, and for the radio rustup plus espup); every download is checked (size and sha256 where the vendor publishes it) and a partial one is deleted; the source is the release tag's tarball; the board build uses `AXIS_OFF`; the radio build uses the lfd3k-sm tag from `installer/radio.json`.
- [ ] **Step 3: Run** the tests and the dry runs; PASS. Commit `feat: build and flash on your own PC (Windows and Linux)`.

### Task 10: LFD3K-SM release images (in `F:\lfd3k-sm`)

**Files (lfd3k-sm repo, its own branch, fast-forward main first):** `.github/workflows/release.yml` (on tag `v*`: build `heltec_v4` (the multimode modem slot), `heltec_v4_single`, `heltec_v3_single`; publish `releases/<tag>/<env>/` with images and `manifest.json` (chip `ESP32-S3`, offsets, sha256, the slot for the multimode one) and `releases/index.json` to its `gh-pages` the same way as Task 6); README and site links to the AXIS installer.
**AXIS repo:** `installer/radio.json` gets the tag once the first radio release exists.

- [ ] **Step 1:** Workflow and manifest writer (reuse `tools/ghpages.py` logic by copying it, since the repos are separate).
- [ ] **Step 2: Verify** with a local build of the three envs and a dry run of the manifest writer.
- [ ] **Step 3: Commit** in lfd3k-sm `feat: release builds publish modem images for the installer`. Do not tag or push without Rev's OK.

### Task 11: Tests on the hardware, docs and brain (batched, last)

- [ ] **Step 1:** Run all host, Python and JS tests; build every preset locally; record sizes and compare with the CI cost table.
- [ ] **Step 2:** On Rev's boards (MAC checked, board 2 first): flash `no-doors` by the installer page from a local server with real manifests, confirm users, messages and settings survived (`@STATUS`, a login, the message count), then flash `Everything` back; the same through `install.ps1`. Screenshots and a capture of the log.
- [ ] **Step 3:** Board 2 already has no card (COM21, MAC 8c:94:df:4f:14:18). As a follower of board 1 it must keep every function and show no SD notice on Everything. Then, with Rev's OK to take it out of the cluster for the test (cluster off in its build, or board 1 left out), flash `no-sd`: logins, messages and menus work and the SD features are absent; and an Everything build running alone with no card shows the notice. Put board 2 back on Everything as a follower afterwards.
- [ ] **Step 3b:** First-install path on a board Rev chooses (or skip and say so): the warning shows, the confirm is required, the board boots.
- [ ] **Step 4:** Bare ESP32 path on the ring board (COM15 is the V3 now: ask Rev which port the bare ESP32 is on before touching any port).
- [ ] **Step 5:** the AXIS site's front page and `docs/INSTALL.md` say "YOU NEED AN SD CARD FOR FULL FUNCTIONS" near the top and link the No SD card preset; `docs/FEATURES.md` lines, the brain note `D:\brain\Projects\AXIS BBS Firmware.md`, and a short report for Rev. Push only with Rev's OK; the first release tag is Rev's.
