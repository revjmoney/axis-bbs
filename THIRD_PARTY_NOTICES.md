# Third-party notices

Axis Micro BBS Firmware includes these third-party components, each under its own license.

| Component | Where | License | Source |
|---|---|---|---|
| doctest 2.4.11 | `third_party/doctest/` (PC tests only) | MIT | https://github.com/doctest/doctest |
| SHA-256 by Brad Conte (crypto-algorithms) | `third_party/sha256/` | Public domain | https://github.com/B-Con/crypto-algorithms |
| FabGL i8086 core (from 8086tiny and 8086tiny plus) | `third_party/i8086/` | MIT | https://github.com/fdivitto/FabGL |
| LovyanGFX | fetched by PlatformIO | FreeBSD (BSD-2-Clause) | https://github.com/lovyan03/LovyanGFX |

## miniz (tinfl inflater) 3.0.2

- Files: `third_party/miniz/miniz_tinfl.c`, `miniz_tinfl.h`, `miniz_common.h` (unmodified), plus two
  small shims (`miniz.h`, `miniz_export.h`) so the inflater builds alone. Used by the ZIP reader for the
  ANSI Pack Viewer on the PC; the ESP32 build uses the copy of tinfl in the chip's ROM instead.
- Source: https://github.com/richgel999/miniz (tag 3.0.2)
- License: MIT (`third_party/miniz/LICENSE`), Copyright 2013-2014 RAD Game Tools and Valve Software,
  Copyright 2010-2014 Rich Geldreich and Tenacious Software LLC.


## IBM VGA 8x16 font (site web font)

`site/fonts/AxisVGA.woff2` is built by `tools/art/make_vga_webfont.py` from `third_party/vga_font/font8x16.h`: the IBM VGA 8x16 glyphs as packaged by VileR in The Ultimate Oldschool PC Font Pack (https://int10h.org/oldschool-pc-fonts/), licensed CC BY-SA 4.0 (https://creativecommons.org/licenses/by-sa/4.0/). The web font is shared under the same license.

## IBM PC BIOS 8x8 font (touchscreen WFC text)

`third_party/ibm_font8x8/font8x8.h` (converted by `tools/art/fon2h.py`) is the IBM PC BIOS 8x8 CP437 font as packaged by VileR in The Ultimate Oldschool PC Font Pack v2.2 (https://int10h.org/oldschool-pc-fonts/), licensed CC BY-SA 4.0 (https://creativecommons.org/licenses/by-sa/4.0/). The firmware draws the CYD touchscreen's WFC text cells from it.

## ViSiON/3 BBS (design reference)

The menu system, menu fields, command names, access strings, art codes and login flow follow ViSiON/3
(https://github.com/ViSiON-3/vision-3-bbs), a recreation of ViSiON/2, MIT licensed. AXIS's code is its own C++;
no ViSiON/3 art is used. `tools/menus/v3import.py` reads ViSiON/3's menu files to seed AXIS menus.

## SD library (patched)

`lib/SD` is Arduino-ESP32 2.0.17's SD library (Apache-2.0, Espressif Systems), with a fix in `sd_diskio.cpp`:
`sdcard_uninit` released its SPI lock after freeing the card (a use-after-free that crashes without heap poisoning).
