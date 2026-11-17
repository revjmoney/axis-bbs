// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// What the command line asked for (main_linux.cpp). The start modes are plan task 5's and the data folder's
// layout plan task 2's; this only holds the two settings.
#pragma once
#include <string>

namespace axis::host {
extern bool        headless;  // --headless: no window and no terminal screen
extern std::string dataDir;   // --data DIR ("" when not given)
}  // namespace axis::host
