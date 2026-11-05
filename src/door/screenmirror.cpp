// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include "door/screenmirror.h"

namespace axis::door {

namespace {
constexpr int kCols = 80, kRows = 25;
const char    kAnsi[8] = {'0', '4', '2', '6', '1', '5', '3', '7'};  // DOS colour order to ANSI's

std::string sgr(uint8_t a) {
    std::string s = "\x1b[0;";
    if (a & 0x08) s += "1;";
    if (a & 0x80) s += "5;";
    s += '3', s += kAnsi[a & 7], s += ";4", s += kAnsi[(a >> 4) & 7], s += 'm';
    return s;
}
}  // namespace

std::string ScreenMirror::update(const std::vector<uint8_t>& page) {
    std::string all;
    update(page, [&all](const char* d, size_t n) { all.append(d, n); });
    return all;
}

void ScreenMirror::update(const std::vector<uint8_t>& page, const std::function<void(const char*, size_t)>& emit) {
    if (page.size() < (size_t)kCols * kRows * 2) return;
    std::string out;
    out.reserve(560);
    auto flush = [&] {
        if (!out.empty()) emit(out.data(), out.size());
        out.clear();
    };
    bool whole = last_.size() != page.size();
    if (whole) out += "\x1b[0m\x1b[2J";
    int atRow = -1, atCol = -1, attr = -1;
    for (int r = 0; r < kRows; r++)
        for (int c = 0; c < kCols; c++) {
            if (r == kRows - 1 && c == kCols - 1) continue;  // writing it would scroll the caller's screen
            size_t  i = (size_t)(r * kCols + c) * 2;
            uint8_t ch = page[i] ? page[i] : ' ', a = page[i + 1];
            if (!whole && last_[i] == page[i] && last_[i + 1] == a) continue;
            if (whole && ch == ' ' && (a & 0x70) == 0) continue;  // the clear already drew it
            if (r != atRow || c != atCol) out += "\x1b[" + std::to_string(r + 1) + ";" + std::to_string(c + 1) + "H";
            if (a != attr) out += sgr(a), attr = a;
            out += (char)ch;
            atRow = r, atCol = c + 1;
            if (out.size() >= 480) flush();  // a cell adds at most ~30 bytes: pieces stay under 512
        }
    flush();
    last_ = page;
}

}  // namespace axis::door
