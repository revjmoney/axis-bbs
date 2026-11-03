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
