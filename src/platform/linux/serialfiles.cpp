// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The serial console's file commands (@PUT, @LS, @RM, @MKDIR, @TYPE), on Linux. They work on the data folder
// (plan task 2): until then none is taken here. isNetKeyPath is the board's check, unchanged.
#include "platform/cyd/serialfiles.h"

bool isNetKeyPath(const String& path) {
    size_t start = 0, n = path.length();
    for (size_t i = 0; i < n; i++)
        if (path[i] == '\0') return true;  // open() stops at a NUL: "netkey.txt\0x" would open the key
    for (size_t i = 0; i <= n; i++) {
        if (i < n && path[i] != '/' && path[i] != '\\') continue;
        String part = path.substring(start, i);
        start = i + 1;
        part.trim();
        while (part.endsWith(".") || part.endsWith(" ")) part.remove(part.length() - 1);  // FAT ignores these
        part.toLowerCase();
        if (part == "netkey.txt" || part == "netkey.tmp" || part == "netrev.key") return true;
    }
    return false;
}

bool serialFileCommand(const String& cmd) {
    (void)cmd;
    return false;
}
