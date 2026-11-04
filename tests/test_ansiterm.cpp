// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include <string>

#include "doctest.h"
#include "bbs/ansiterm.h"

using namespace axis::bbs;

namespace {
std::string row(const AnsiTerm& t, int r) {
    std::string s;
    for (int c = 0; c < AnsiTerm::kCols; c++) s += (char)t.cell(r, c).ch;
    while (!s.empty() && s.back() == ' ') s.pop_back();
    return s;
}
}  // namespace

TEST_CASE("the terminal prints text, moves to new lines, and keeps colours per cell") {
    AnsiTerm t;
    t.feed("hello\r\n\x1b[1;33mAXIS\x1b[0m done");
    CHECK(row(t, 0) == "hello");
    CHECK(row(t, 1) == "AXIS done");
    CHECK((t.cell(1, 0).attr & 15) == 14);  // bright brown = yellow
    CHECK((t.cell(1, 5).attr & 15) == 7);
}

TEST_CASE("the cursor moves where ANSI says, and screens clear") {
    AnsiTerm t;
    t.feed("\x1b[2J\x1b[5;10Hx\x1b[2Ay\x1b[3Cz");
    CHECK(t.cell(4, 9).ch == 'x');
    CHECK(t.cell(2, 10).ch == 'y');
    CHECK(t.cell(2, 14).ch == 'z');
    t.feed("\x1b[2J");
    CHECK(row(t, 4).empty());
}

TEST_CASE("an 80-column row wraps by itself, and the bottom line scrolls the screen up") {
    AnsiTerm t;
    t.feed(std::string(80, 'a') + "b");
    CHECK(row(t, 0) == std::string(80, 'a'));
    CHECK(row(t, 1) == "b");
    AnsiTerm s;
    for (int i = 0; i < 30; i++) s.feed("line " + std::to_string(i) + "\r\n");
    CHECK(row(s, AnsiTerm::kRows - 2) == "line 29");
    CHECK(row(s, 0) == "line 6");
}

TEST_CASE("backspace, erase to end of line, and saved cursor positions work") {
    AnsiTerm t;
    t.feed("abc\b\bX");
    CHECK(row(t, 0) == "aXc");
    t.feed("\r\x1b[K");
    CHECK(row(t, 0).empty());
    t.feed("\x1b[3;3H\x1b[s\x1b[10;10H\x1b[uQ");
    CHECK(t.cell(2, 2).ch == 'Q');
}
