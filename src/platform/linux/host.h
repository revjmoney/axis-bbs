// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// What the command line asked for (main_linux.cpp), and where the board's serial console is. The start modes are in
// startmode.h and the data folder's layout in paths.h; this only holds the settings.
#pragma once
#include <string>

#include "platform/linux/startmode.h"

namespace axis::host {
extern StartMode   startMode;  // detectStartMode()'s answer, after any fallback
extern std::string dataDir;    // --data DIR ("" when not given)
// The board's serial console (Serial): what it reads (-1: nothing, input ignored, as headless) and where it writes.
// Terminal mode reads stdin and writes the terminal; headless writes stdout (the log) and reads nothing.
extern int serialIn;
extern int serialOut;
// Called by delay() before and between its naps of at most 10 ms (modemline.cpp pumps the radio line from it, as
// the board's network runs by itself while main.cpp waits inside one loop() turn). nullptr: delay() just sleeps.
extern void (*delayHook)();
}  // namespace axis::host
