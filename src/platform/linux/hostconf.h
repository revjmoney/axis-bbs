// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// <data>/axis.conf: the Linux program's own settings (the board's settings stay in flash/axis.ini). One key=value
// per line; '#' starts a comment; blank lines and unknown keys are ignored. A missing file means every default.
#pragma once
#include <string>

// Reads <dataDir>/axis.conf (once, before setup()). Logs what it read, or that it uses the defaults.
void hostConfLoad(const std::string& dataDir);

// The value of `key`, or `def` when the key is absent. An integer outside [lo, hi] or not a number is logged and
// gives `def`.
long        hostConfInt(const char* key, long def, long lo, long hi);
std::string hostConfStr(const char* key, const std::string& def);
