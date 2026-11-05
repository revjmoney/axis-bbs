// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include "bbs/lightbar.h"

#include "doctest.h"

using namespace axis::bbs;

static std::vector<BarItem> three() {
    return {{49, 10, 31, 5, "J", " Journey onward."}, {49, 11, 31, 5, "C", " Create an account."}, {49, 12, 31, 5, "D", " Disconnect."}};
}

TEST_CASE("lightbar: arrows wrap, hotkeys and digits pick, Enter picks the current") {
    Lightbar b(three());
    CHECK(b.key('\x1b') == -1);
    CHECK(b.key('[') == -1);
    CHECK(b.key('A') == -1);  // up from the first wraps to the last
    CHECK(b.current() == 2);
    CHECK(b.key('\r') == 2);
    CHECK(b.key('c') == 1);  // hotkey, either case
    CHECK(b.key('1') == 0);  // digit
}

TEST_CASE("lightbar: an arrow split across two packets still moves; a lone Esc after a pause is Esc") {
    Lightbar b(three());
    b.key('\x1b');
    b.key('[');
    b.key('B');
    CHECK(b.current() == 1);
    CHECK_FALSE(b.escaped());
    b.key('\x1b');
    b.keyTimeout();
    CHECK(b.escaped());
}

TEST_CASE("lightbar: moving redraws only the two rows that changed") {
    Lightbar b(three());
    b.draw();
    b.key('\x1b'), b.key('['), b.key('B');
    std::string r = b.takeRedraw();
    CHECK(r.find("\x1b[10;49H") != std::string::npos);
    CHECK(r.find("\x1b[11;49H") != std::string::npos);
    CHECK(r.find("\x1b[12;49H") == std::string::npos);
}

TEST_CASE("yes/no bar: Y, N, arrows and Enter") {
    YesNoBar y(10, 20, true);
    CHECK(y.key('\r') == 1);
    YesNoBar n(10, 20, true);
    CHECK(n.key('n') == 0);
    YesNoBar a(10, 20, true);
    a.key('\x1b'), a.key('['), a.key('C');  // right: over to No
    CHECK(a.key('\r') == 0);
}
