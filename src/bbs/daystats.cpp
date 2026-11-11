// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include "bbs/daystats.h"

namespace axis::bbs {

bool DayStats::roll(int key) {
    if (key <= 0 || key == day) return false;
    if (day) calls = posts = uploads = 0;
    day = key;
    return true;
}

bool parseStatTotal(const std::string& text, uint32_t& out) {
    if (text.empty() || text.size() > 10) return false;
    uint64_t n = 0;
    for (char c : text) {
        if (c < '0' || c > '9') return false;
        n = n * 10 + (uint64_t)(c - '0');
    }
    if (n > 0xFFFFFFFFull) return false;
    out = (uint32_t)n;
    return true;
}

bool DayStats::parse(const std::string& text) {
    *this = DayStats();
    std::string line = text.substr(0, text.find('\n'));
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.compare(0, 3, "v1 ") != 0) return false;
    DayStats s;
    int      seen = 0;
    for (size_t pos = 3; pos < line.size();) {
        size_t      end = line.find(' ', pos);
        if (end == std::string::npos) end = line.size();
        std::string word = line.substr(pos, end - pos);
        pos = end + 1;
        size_t eq = word.find('=');
        if (eq == std::string::npos) continue;
        std::string k = word.substr(0, eq);
        uint32_t    v = 0;
        if (!parseStatTotal(word.substr(eq + 1), v)) return false;
        if (k == "day") s.day = v > 0x7FFFFFFFu ? 0 : (int)v, seen |= 1;
        else if (k == "calls") s.calls = v, seen |= 2;
        else if (k == "posts") s.posts = v, seen |= 4;
        else if (k == "uploads") s.uploads = v, seen |= 8;
        else if (k == "total") s.total = v, seen |= 16;
    }
    if (seen != 31) return false;
    *this = s;
    return true;
}

std::string DayStats::format() const {
    return "v1 day=" + std::to_string(day) + " calls=" + std::to_string(calls) + " posts=" + std::to_string(posts) +
           " uploads=" + std::to_string(uploads) + " total=" + std::to_string(total) + "\n";
}

}  // namespace axis::bbs
