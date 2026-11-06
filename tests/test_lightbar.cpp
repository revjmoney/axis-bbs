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

static std::vector<std::string> many(int n) {
    std::vector<std::string> v;
    for (int i = 1; i <= n; i++) v.push_back("PACK" + std::to_string(i) + ".ZIP");
    return v;
}

TEST_CASE("scroll list: shows a window of the list, scrolls with the bar, page keys jump") {
    ScrollList l(4, 5, 30, 3);  // 3 rows at row 5
    l.setItems(many(10));
    std::string d = l.draw();
    CHECK(d.find("PACK1.ZIP") != std::string::npos);
    CHECK(d.find("PACK3.ZIP") != std::string::npos);
    CHECK(d.find("PACK4.ZIP") == std::string::npos);
    for (int i = 0; i < 3; i++) l.key('\x1b'), l.key('['), l.key('B');  // down 3: past the window
    CHECK(l.current() == 3);
    std::string r = l.takeRedraw();
    CHECK(r.find("PACK4.ZIP") != std::string::npos);  // the window moved
    CHECK(r.find("PACK1.ZIP") == std::string::npos);
    l.key('\x1b'), l.key('['), l.key('6'), l.key('~');  // page down
    CHECK(l.current() == 6);
    l.key('\x1b'), l.key('['), l.key('F');  // end
    CHECK(l.current() == 9);
}

TEST_CASE("scroll list: moving past either end reports the clamp and stays put") {
    ScrollList l(1, 1, 10, 3);
    l.setItems({"a", "b"});
    l.key('\x1b');
    l.key('[');
    l.key('B');
    CHECK(l.current() == 1);
    CHECK(l.clamped() == 0);
    l.key('\x1b');
    l.key('[');
    l.key('B');
    CHECK(l.current() == 1);
    CHECK(l.clamped() == 1);
    l.key('\x1b');
    l.key('[');
    l.key('A');
    l.key('\x1b');
    l.key('[');
    l.key('A');
    CHECK(l.current() == 0);
    CHECK(l.clamped() == -1);
}

TEST_CASE("scroll list: typed numbers then Enter pick by number; Enter alone picks the bar; a letter jumps") {
    ScrollList l(1, 1, 20, 5);
    l.setItems({"ACID-50A.ZIP", "BT-1980.ZIP", "CIA54.ZIP", "ICE-9401.ZIP"});
    CHECK(l.key('3') == -1);
    CHECK(l.key('\r') == 2);
    CHECK(l.key('\r') == 2);  // the bar sits on it now
    l.key('i');               // the next item starting with I
    CHECK(l.current() == 3);
    l.key('\x1b');
    l.keyTimeout();
    CHECK(l.escaped());
}
