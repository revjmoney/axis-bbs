# AXIS build options and installer: design

Status: draft for Rev's review, 2026-11-12. Owner: Rev. J. Money / jmscnc.com.
Branch: `build-options` (from main d441ad5, which has the board 1 memory work in it).

## Intent

Rev, and later other sysops, should be able to put the AXIS Micro BBS on a board, and the LORA FLOWTRON DATAMASTER 3000 SERIAL MODEM - LFD3K-SM on a radio, without Claude and without setting up a toolchain: plug the board into a computer, pick what to build, press Install. Turning features off buys flash space for new features and removes those features' memory spikes while the board is busy. Today Rev is the only person who has ever flashed AXIS, and only through Claude; this is the first way anyone can do it alone.

Rev's words: "more free memory and room for other features"; "a handful of pre-made sensible options, like just a bbs that answers telnet, radio on/off, no doors, no file areas, etc, one with everything my board is running right now"; "also github actions for custom"; "anyone can build, it's open source"; keep "users messages and settings"; "a terminal script that downloads any needed tools and then builds the firmware on the user's/sysop's pc"; "choose to build the modem firmware or both and flash the modem and the board or just one of them"; "if we click both it should give us options for the radio like which board, v3 or 4"; the BBS can be a "cyd or bare esp32 (possibly with a terminal screw bob for sensors and blinkies later)"; "blinky lights and animations on the page and fun progress bar for flashing with a text log of the flash"; "my jmscnc branding and monkey"; full names on the modem and everything else; "windows and linux" (no Mac).

## Goals

1. A **web installer page** on the AXIS site (GitHub Pages, `revjmoney.github.io/axis-bbs/install.html`) that flashes from the browser (Chrome or Edge, Web Serial).
2. **What to flash:** the AXIS Micro BBS board, the LFD3K-SM radio, or both (board first, then the radio).
   - BBS board: **CYD** (ESP32-2432S028R with the touch screen) or **bare ESP32** (no screen; one status LED; setup over the AXIS-SETUP hotspot).
   - Radio: **Heltec V4** or **Heltec V3**.
3. **Presets** for the board, prebuilt by CI on every release:

   | Preset | Features |
   |---|---|
   | Everything | everything Rev's boards run now: doors (LORD, scripts, DOS prompt), the ANSI viewer, file areas and transfers, the cluster, the LFD3K-SM modem link, No-IP / GoDaddy DNS / Discord, nets when they land |
   | No doors | Everything minus LORD, scripts and the DOS prompt |
   | No file areas | Everything minus file areas, uploads, downloads and the ANSI viewer (the viewer needs file areas) |
   | No radio | Everything minus the LFD3K-SM modem link |
   | Just a BBS | telnet, logins, messages, menus and the WFC; nothing else |

   New presets are new lines in one list.
4. **Each preset row shows its cost**, measured by CI: flash used, flash freed against Everything, and which busy-time memory spikes go away (from the measured peaks: LORD, the 44 KB ZIP inflate window, transfers and so on). The page states plainly that dropping features frees mostly flash, not idle free memory (our own code holds about 7 KB of static RAM; most of the always-taken 47 KB is Wi-Fi and lwIP).
5. **Custom builds:** any combination of feature switches, built by GitHub Actions on request, open to anyone with a GitHub account ("it's open source"). The result is published on the site under a fingerprint of the switches, so the same combination never builds twice and flashes at once the second time.
6. **Build it yourself:** a one-line script for **Windows (PowerShell)** and **Linux (sh)** that downloads only the tools it is missing into its own folder, fetches the source at the release the page shows, builds the board firmware, the radio firmware, or both with the chosen switches, and flashes them. The page fills the command in from the visitor's picks.
7. **Users, messages and settings survive every flash.** The installer and the script write the application only (the board's 2.25 MB app partition at 0x10000; the radio's modem app), never the NVS, LittleFS, the SD card or the radio's settings partition, and never erase the chip. A feature that is built out keeps its data (LORD's game, the file areas, nets), so building it back in picks up where it left off; menus simply hide what is not built in.
8. **Real progress:** the progress bar, the KB counter and the text log show what the flasher is really doing (the port, the chip and its MAC, each region written, the hash check, the reset), with fun stage names on top (BL4Z1NG BYT3S, FL0WTR0N ENGAGED, S0N1C B00M, DATAMASTER WRITING).
9. **Look:** Rev's branding: the Simian Tactical Unit monkey logo, "by Rev. J. Money / jmscnc.com", and full names everywhere ("AXIS Micro BBS", "LORA FLOWTRON DATAMASTER 3000 SERIAL MODEM - LFD3K-SM"). Retro and alive: blinking modem lights (OH CD SD RD HS EC DC AA), a flickering title, a scrolling marquee with the release name and tagline, a pulsing Install button, subtle scanlines; motion stays off for `prefers-reduced-motion`. The approved mockup is `installer-page-v4.html` (in `.superpowers/brainstorm/`, local).
10. **A bare ESP32 is set up as headless** by the installer: right after flashing, the page sends `@HEADLESS on` over the same serial port (the board keeps it in NVS through later flashes), the same thing Claude did for the ring board.

## Non-goals

- Mac support ("windows and linux").
- Turning features on or off at runtime: switches are compile time, because that is what frees flash.
- The screw-terminal breakout for sensors and blinkies: a later feature; the design leaves room for a `gpio` / sensors row but builds nothing for it.
- Changing the partition table, wiping, or "factory reset" from the installer.
- A paid or private build service; nothing beyond GitHub Actions and GitHub Pages.

## Anti-goals (a failure even if it works)

- Any flash that loses users, messages, settings, the radio's directory or saved forwards.
- Cost numbers that are guessed or stale, or a page that implies a big idle-memory gain that does not happen.
- A board left unbootable by a half-finished flash with no clear way back (the log must say what to do: replug and press Install again; the bootloader and partition table are written only if they differ).
- Secrets in CI logs, build artifacts or the site (Wi-Fi passwords, the cluster key, the public hostname, net seeds).
- The page and the script disagreeing about what a preset contains.

## Constraints (facts from the project)

- Board: ESP32 (CYD or bare), 4 MB flash, partitions: `nvs` 0x9000 (0x5000), `otadata` 0xe000, `app0` 0x10000 (0x240000 = 2,359,296 bytes), `spiffs` (LittleFS) 0x250000 (0x1B0000). One app slot, no OTA. Everything is about 2.08 MB today (88.2%).
- The CYD and a bare ESP32 run the same image (`env:cyd`; `env:headless` extends it); headless is a flag in NVS set once over serial (`@HEADLESS on`).
- Board build: PlatformIO, `platform = espressif32@6.9.0`, framework arduino + espidf (IDF 4.4.7 compiled from source), sdkconfig from `sdkconfig.defaults` (the generated `sdkconfig.cyd` / `sdkconfig.headless` are not tracked; a clean CI checkout generates them).
- Existing compile switches: `AXIS_SD_POOL`, `AXIS_HEAP_TRACE` (bench), nets will compile out with `-DAXIS_NETS=0`. Measured flash per feature (sizing 2026-11-11, approximate): cluster about 83 KB, LORD 67, file areas 39, DOS prompt 25, Axis Net 22, ANSI viewer 20, scripts 16, X/ZMODEM 15.
- Radio firmware lives in the separate public repo `revjmoney/lfd3k-sm` (Rust core + ESP-IDF 5.4.1 via PlatformIO; envs `heltec_v4` (the modem slot of the multimode layout), `heltec_v4_single`, `heltec_v3_single`). Its site is `revjmoney.github.io/lfd3k-sm`, the same origin as the AXIS site, so the installer can fetch its images directly.
- Rev's V4 already runs the multimode layout (picker plus four slots). Flashing the modem onto a multimode V4 must write only the modem's slot, never the picker or the other modes.
- Web Serial needs Chrome or Edge on desktop; Firefox and Safari cannot flash (the page says so).
- Both repos are public: scan for secrets before every push (project rule).
- No em dashes or double hyphens in any text Rev reads, on the page included. No AI attribution in commits.

## The parts Rev decided, as decided

- Presets: the five above, sensible and few; one is exactly Rev's current board.
- Custom builds through GitHub Actions, open to anyone.
- Data survives every flash.
- Targets: BBS board (CYD or bare ESP32), radio (V3 or V4), or both; "both" asks for the radio board too.
- A PC script for Windows and Linux that fetches its own tools and builds and flashes either or both.
- Branding, full names, blinky lights, animations, a fun progress bar with a real text log.

## Claude's calls (Rev can overrule)

1. **One switch per feature** as a compile define (`AXIS_LORD`, `AXIS_SCRIPTS`, `AXIS_DOSPROMPT`, `AXIS_FILES`, `AXIS_ANSIVIEW`, `AXIS_TRANSFERS`, `AXIS_CLUSTER`, `AXIS_MODEMLINK`, `AXIS_WEB` (No-IP / DNS / Discord), `AXIS_NETS` (reserved), `AXIS_GPIO` (reserved)), default on. Code that a switch removes is fenced at its call sites and in the build file list; menus, the setup wizard, the sysop pages and serial commands hide what is off.
2. **Dependencies are declared, not guessed:** the ANSI viewer and transfers need file areas; nets will need the cluster's link code or their own. A combination that breaks a dependency is refused by the page, the script and CI with the reason.
3. **One source of truth:** `build/features.json` (each switch: name, label, description, needs) and `build/presets.json` (each preset: name, label, switches). CI, the installer page and both scripts read these files; nothing hard-codes a feature list.
4. **CI on every release tag:** build every preset for the board (the image is the same for the CYD and a bare ESP32), and the radio for V4 (multimode slot and single) and V3; measure flash with the size tool and static RAM from the map; take busy-time peaks from the memory work's measured table (`@HEAPWIN` / HeapScope numbers, kept in `build/peaks.json`, updated by hand when a bench run changes them); publish the images, a manifest per image, and `costs.json` to the site under `releases/<version>/`.
5. **Custom builds** start from a GitHub issue form ("Build request") with a box per switch; a workflow checks the combination against `features.json`, computes the fingerprint (a hash of the sorted switches plus the release), and if `builds/<fingerprint>/` is not on the site yet, builds it and publishes it there, then comments on the issue with a link that opens the installer set to that build and closes the issue. Images are served from the site (Pages), never from a GitHub download link, so the browser can fetch them.
6. **The flasher** is esptool-js (the library behind ESPHome's web installer), pinned to an exact version, loaded from cdn.jsdelivr.net/npm; it reports every block written, which drives the real progress bar and the log. Before writing, the page reads the chip (type, MAC, flash size) and refuses a mismatch (for example, a radio image on a CYD) with a plain message.
7. **The radio on a V4:** the page looks at the V4's partition table first; with the multimode layout it writes only the modem slot (the modem keeps its own settings partition); without it, it offers the single-mode modem image. A V3 gets `heltec_v3_single`.
8. **Scripts:** `install.ps1` (Windows) and `install.sh` (Linux) on the site, same switches and presets as the page (`--board <preset>|--features a,b,c`, `--target cyd|esp32`, `--radio v3|v4`, `--flash`, `--port`). Tools go into one folder (`%LOCALAPPDATA%\axis-build` or `~/.axis-build`): a portable Python if needed, PlatformIO, and for the radio rustup with espup. Re-runs reuse it. The script prints the same stage names and a progress bar from esptool's output.
9. **Versions:** the page offers the latest release by default and the previous one, so a sysop can go back if a release misbehaves.
10. **Where it lives:** `site/install.html` (plus its JS and CSS) in the AXIS repo, linked from the AXIS site's front page and from the LFD3K-SM site's README and page.

## Left to the builder

- Exactly where each feature's code is fenced, and how each menu, page and serial command hides an absent feature.
- The CI matrix layout, caching (PlatformIO packages, Rust), and how release and custom builds share steps.
- The issue form's exact fields and the fingerprint format.
- How the page parses a V4's partition table over esptool-js.
- The page's layout details within the approved mockup.
- Tests: every preset builds in CI; a dependency-breaking combination is refused; the fingerprint is stable; the scripts' argument parsing; the page's preset and switch logic (unit tests for the shared JSON logic); a flash of every preset on Rev's boards before the first release (board 2 first).

## Later

- The screw-terminal breakout for sensors and blinkies on a bare ESP32 (`AXIS_GPIO`).
- The nets row (`AXIS_NETS`) once nets land on main.
- Voice builds of the LFD3K-SM once the voice work lands.
