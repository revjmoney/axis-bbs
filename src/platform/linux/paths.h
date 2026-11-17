// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The data folder, and how the board's paths land in it. The board keeps its card at /sd and its internal flash at
// /littlefs; here they are <data>/sd and <data>/flash, with the NVS namespaces in <data>/prefs and logs in <data>/log.
// fs_remap.cpp sends every path the board's code hands to the C library through hostPath(), so main.cpp and the core
// keep their own paths unchanged.
#pragma once
#include <string>

// The data folder, absolute, no trailing slash ("" until axisDataSetup succeeded).
std::string axisDataDir();

// "/sd..." -> "<data>/sd...", "/littlefs..." -> "<data>/flash...", anything else unchanged. Only the exact folder or
// a path under it counts ("/sdcard" is left alone). A board path asked for before the data folder is set maps to
// "" so the call fails (ENOENT) instead of touching the real /sd or /littlefs.
std::string hostPath(const char* boardPath);

// Makes the data folder ready: `requested` from --data, or "" for the default (/var/lib/axis when the program runs
// as user axis, else ~/.axis). Creates it (mode 0700) with sd/, flash/, prefs/ and log/; if `seed` is given and
// sd/ is empty, copies that folder (a board card's contents) into sd/. On failure says why in `why` and returns false.
bool axisDataSetup(const std::string& requested, const std::string& seed, std::string& why);
