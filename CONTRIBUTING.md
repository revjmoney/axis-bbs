# Contributing to Axis Micro BBS

Calling all sysops, coders, ANSI artists and BBS diehards. Axis is a from-scratch telnet BBS that lives on a $15 ESP32 touchscreen, and it gets better every time someone who loves this stuff pitches in. Bug fixes, new features, doors, ANSI art, docs, testing: all of it is welcome.

New here? Look for issues labelled [good first issue](https://github.com/revjmoney/axis-bbs/labels/good%20first%20issue) or [help wanted](https://github.com/revjmoney/axis-bbs/labels/help%20wanted), or come say hi on [Discord](https://discord.gg/23KqHWM6vd).

## Ways to help

### Report a bug
Open a [bug report](https://github.com/revjmoney/axis-bbs/issues/new?template=bug_report.md). The most useful reports have:
- the firmware version (bottom of the activity screen, or `@STATUS` on the serial console),
- your board (CYD, headless ESP32, ring board) and whether it has an SD card,
- what you did, what you expected, and what happened,
- the serial output if the board rebooted (115200 baud). An `ALLOC FAILED` line or a `Backtrace:` line tells us exactly where it broke.

### Suggest a feature
Open a [feature request](https://github.com/revjmoney/axis-bbs/issues/new?template=feature_request.md). Tell us the moment you wished it existed. Classic BBS features from the software you remember (Renegade, Telegard, Oblivion/2, Iniquity, PCBoard, WWIV, ViSiON) are especially welcome.

### Make ANSI art
Axis is built around ANSI art, and it always wants more. Good targets:
- **Theme screens:** each look (`axis`, `oblivion`, `iniquity`) loads screens from `/sd/themes/<look>/` on the card: `MAIN.ANS`, `MATRIX.ANS`, `GOODBYE.ANS`, `BULLETIN.ANS`, and a header per section (`FILES.ANS`, `MSGS.ANS`, `DOORS.ANS` and so on). A whole new look is very welcome.
- **Artpacks:** a ZIP of your pieces with a `FILE_ID.DIZ` goes in the ANSI Packs file area and shows in the built-in ANSI Pack Viewer.
- **Format:** 80 columns, CP437, 16 colours (iCE colours are fine). Keep a screen to 24 lines unless it is meant to scroll. MCI codes like `%TM` (time) and `%UN` (handle) are filled in live, so keep each code in one colour.
- Submit art with an [ANSI art issue](https://github.com/revjmoney/axis-bbs/issues/new?template=ansi_art.md) (attach the files) or as a pull request under `art/`. Sign your work; credit stays with you.

### Write code
Pull requests are very welcome. Some areas with room to grow:
- a **dial-up modem node** (real Hayes modems over RS-232),
- more **DOS doors** running on the built-in 8086 emulator, and fixes to the emulator itself,
- **FidoNet-style** message networking between boards,
- lower **RAM use** on the main board (it runs with about 40 KB free, so every KB counts),
- the **sysop control panel** (`tools/panel/`, Python).

## Building and testing

You need [PlatformIO](https://platformio.org/) and CMake with a C++17 compiler.

```bash
# the host tests (no board needed)
cmake -S . -B build && cmake --build build && ./build/axis_tests

# the firmware for a CYD board
pio run -e cyd
pio run -e cyd -t upload --upload-port <your port>

# the headless ESP32 build
pio run -e headless

# the control panel tests
cd tools/panel && python -m pytest -q
```

The firmware is built on **ESP-IDF 4.4.7 with Arduino as a component** (PlatformIO `framework = arduino, espidf`,
settings in `sdkconfig.defaults`). The first build downloads ESP-IDF and takes a while. The root `CMakeLists.txt`
serves both builds: under PlatformIO it is the ESP-IDF project, on the PC it builds the tests (`CMakeLists.pc.txt`).

After a change to `partitions.csv`, do a full upload (never `-t nobuild`) or the board boot-loops on the old table.

## Pull request guidelines

- **One change per PR,** with a short description of what and why.
- **Tests:** BBS logic lives in `src/bbs/` and is tested on the PC in `tests/`. Add a test for a bug fix or a new feature, and keep the whole suite passing.
- **Mind the RAM.** The board has no PSRAM. Avoid building whole screens or file lists in one big string, stream from SD a line at a time, and test on real hardware when you can. Mention free heap before and after if your change is RAM-heavy (`@STATUS` shows it).
- **Mind the flash.** The CYD firmware is close to full. Say how much your change adds (`pio run -e cyd` prints it).
- **Match the code around you:** naming, comment style, and the existing `@` command pattern for anything the sysop controls.
- **Licence:** Axis is GPL-3.0. By sending a pull request you agree your work is released under the same licence. Do not include code or art you do not have the right to share; note any third-party sources in `THIRD_PARTY_NOTICES.md`.

## Be excellent

Keep it friendly and keep the scene spirit alive: credit people, help newcomers, and keep discussions about the work. Harassment of any kind gets you removed.

Thanks for helping keep the BBS alive. See you on the board.
