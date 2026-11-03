# Axis Micro BBS Firmware

The software behind **AXIS UNDERGROUND CNC**. A real, old-school telnet BBS for the CYD (ESP32-2432S028R "Cheap Yellow Display"): ANSI menus, message bases with FidoNet-compatible storage, file areas, a sysop console, real DOS door games (starting with Legend of the Red Dragon) running on the board itself, and multi-board clustering where each CYD is one node.

Status: design approved, implementation starting.

- Design: [docs/superpowers/specs/2026-11-02-axis-underground-bbs-v1-design.md](docs/superpowers/specs/2026-11-02-axis-underground-bbs-v1-design.md)
- Features and settings log: [docs/FEATURES.md](docs/FEATURES.md)
- Profiling tools (LORD memory profile on a PC): [tools/profiling/](tools/profiling/)

## Credits

Axis Micro BBS Firmware and the AXIS UNDERGROUND CNC board are created by **Rev. J. Money**: [rotatocam.com](https://rotatocam.com) and [jmscnc.com](https://jmscnc.com).

## License

Copyright (C) 2026 Rev. J. Money.

This project is free software under the **GNU General Public License v3.0 or later** (see `LICENSE`). You can use, study, change and share it, but anything you distribute that is built from it must also be GPL-3.0 with its source available, and you must keep the copyright notices.

**Additional term under GPLv3 section 7(b):** any copy or derived work, in whole or in part, must keep the "Copyright (C) 2026 Rev. J. Money" notices in the source files, must keep the author credit (Rev. J. Money, rotatocam.com, jmscnc.com) in this README or an equivalent file, and, if it has an interactive interface, must keep that credit visible in its credits or about screen.

Third-party pieces keep their own licenses and are listed in `THIRD_PARTY_NOTICES.md` as they are added.
