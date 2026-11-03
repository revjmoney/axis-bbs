# AXIS changes to the FabGL i8086 core

Upstream: https://github.com/fdivitto/FabGL `src/emudevs/i8086.{h,cpp}` at commit fd6f1e3 (2025-02-27).
License: MIT (8086tiny by Adrian Cable, 8086tiny plus by Julian Olds, changes by Fabrizio Di Vittorio). See `LICENSE.TXT`.

All changes are made by `tools/vendor/refactor_i8086.py` from the pristine upstream files, so they can be re-applied to a newer upstream:

- Guest memory is reached only through `axis::door::MemoryBus` (`setBus()`), not a flat 1 MB array, so the door runtime can page it (RAM pages, read-only pages served from flash). Every `MEM8/MEM16` read, every write (21 assignments turned into `WR8/WR16` calls) and the four places that took raw pointers into the stack now go through the bus.
- Registers keep 8086tiny's trick of living at an address just past guest memory, now a fixed `kRegBase = 0x110000` instead of a pointer difference.
- Addresses wrap at 1 MB like a real 8086; stack and instruction offsets wrap inside their segment.
- Instructions are fetched into an 8-byte buffer instead of read through a pointer into memory, and the two unaligned 16-bit reads of instruction bytes were replaced (they would fault on the ESP32).
- The video-memory callbacks and the FabGL header dependency were removed; `setCallbacks()` takes the context, port and interrupt callbacks only.
- Added `reg()`, `setReg()` and `flagsWord()` for the high-level DOS layer.
- 80186 instructions are enabled by default (`I80186MODE 1`).
- Debug `printf` calls go to `I8086_LOG`, which is off by default.
