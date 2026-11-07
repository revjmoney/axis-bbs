// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#pragma GCC optimize("Os")  // menus and screens: size over speed (the door CPU and transfers stay -O2)
#include "bbs/listscreen.h"

#include "bbs/theme.h"

namespace axis::bbs {

namespace {
std::string clip(const std::string& s, size_t n) {
    std::string o;
    o.reserve(n);
    for (unsigned char c : s) {
        if (o.size() >= n) break;
        if (c >= 0x20 && c < 0x7f) o += (char)c;
        else if (c == '\t') o += ' ';
    }
    return o;
}
}  // namespace

std::string listScreenHead(const std::string& artKey, const std::string& title) {
    std::string art = theme::artFor(artKey);  // FILES.ANS / MSGS.ANS when the theme pack is on the card
    if (art.empty() && artKey != title) art = theme::artFor(title);
    if (!art.empty()) return art;
    return theme::banner(title);
}

std::string listScreenTop(const std::string& title, const std::string& columns) {
    // The lightbar is pinned at row 6. A header .ANS is taller than that, so keep its top
    // (the block title) and park the column line on row 4.
    std::string key = title == "FILE AREAS" ? "FILES" : title == "MESSAGE AREAS" ? "MSGS" : title;
    std::string s = theme::CLS;
    s += listScreenHead(key, title);
    s += "\x1b[4;1H\x1b[J";
    s += std::string("  ") + theme::DGREY + columns + "\r\n";
    s += std::string("  ") + theme::ORANGE + std::string(74, '\xcd') + theme::RESET + "\r\n";
    return s;
}

std::string listScreenStatus(const std::string& line) {
    return "\x1b[" + std::to_string(kListStatusRow) + ";1H\x1b[2K" + theme::TEXT + "  " + clip(line, 76);
}

std::string listScreenKeys(const std::string& keys) {
    return "\x1b[" + std::to_string(kListKeyRow) + ";1H\x1b[2K  " + keys + theme::RESET;
}

}  // namespace axis::bbs
