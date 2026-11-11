// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The WFC's day counters (calls, posts and uploads today) and the lifetime call total, kept in internal flash
// (/stats.txt) so a reboot or a flash does not zero them. One text line:
//   v1 day=<key> calls=<n> posts=<n> uploads=<n> total=<n>
// The day key is the local day ((year + 1900) * 400 + day of the year), 0 while the clock has never been set.
#pragma once
#include <cstdint>
#include <string>

namespace axis::bbs {

struct DayStats {
    int      day = 0;  // the day the counts belong to (0 = clock not set yet)
    uint32_t calls = 0, posts = 0, uploads = 0;
    uint32_t total = 0;  // every call ever on this board: never reset at midnight

    // A new local day (key <= 0: the clock is not set, nothing changes). The first real key is adopted as it is (the
    // counts so far belong to it); a different key zeroes the day counts and keeps the total. True when anything changed.
    bool roll(int key);
    void call() { calls++, total++; }
    void post() { posts++; }
    void upload() { uploads++; }

    // The saved line back into the counters. A missing, broken or unknown file gives all zeros (false): never a crash.
    bool        parse(const std::string& text);
    std::string format() const;
};

// "@STATS total=<n>": n as plain digits (no sign, at most 4294967295). False for anything else.
bool parseStatTotal(const std::string& text, uint32_t& out);

}  // namespace axis::bbs
