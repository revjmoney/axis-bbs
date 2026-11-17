// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// What the command line asked for (main_linux.cpp). The start modes are plan task 5's and the data folder's
// layout plan task 2's; this only holds the two settings.
#pragma once
#include <string>

namespace axis::host {
extern bool        headless;  // --headless: no window and no terminal screen
extern std::string dataDir;   // --data DIR ("" when not given)
// Called by delay() before and between its naps of at most 10 ms (modemline.cpp pumps the radio line from it, as
// the board's network runs by itself while main.cpp waits inside one loop() turn). nullptr: delay() just sleeps.
extern void (*delayHook)();
}  // namespace axis::host
