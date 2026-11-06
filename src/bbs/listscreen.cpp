// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
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

std::string listScreenTop(const std::string& title, const std::string& columns) {
    std::string s = theme::CLS;
    s += theme::titleBar(title);
    s += std::string("  ") + theme::DGREY + columns + "\r\n";
    s += std::string("  ") + theme::DGREY + std::string(74, '\xc4') + theme::RESET + "\r\n";
    return s;
}

std::string listScreenStatus(const std::string& line) {
    return "\x1b[" + std::to_string(kListStatusRow) + ";1H\x1b[2K" + theme::TEXT + "  " + clip(line, 76);
}

std::string listScreenKeys(const std::string& keys) {
    return "\x1b[" + std::to_string(kListKeyRow) + ";1H\x1b[2K  " + keys + theme::RESET;
}

}  // namespace axis::bbs
