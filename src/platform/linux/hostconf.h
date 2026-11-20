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

// The file text with `key` set to `value`. Every other line stays, including comments. A comment on the key's own
// line stays too. The key is appended when it was not there. False (and `out` unchanged) when the key is not a
// plain name or the value holds a newline or a '#'.
bool hostConfRewrite(const std::string& text, const char* key, const std::string& value, std::string& out);

// Sets `key` in memory and in <data>/axis.conf (the file hostConfLoad read). False when the key is refused or the
// file cannot be written; memory is left as it was.
bool hostConfSet(const char* key, const std::string& value);
