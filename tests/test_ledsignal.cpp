// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include "doctest.h"
#include "bbs/ledsignal.h"

using namespace axis::bbs;

namespace {
bool lit(Rgb c) { return c.r || c.g || c.b; }
bool is(Rgb c, int r, int g, int b) { return (c.r > 0) == (r > 0) && (c.g > 0) == (g > 0) && (c.b > 0) == (b > 0); }
// was this colour ever shown over the span?
bool shows(const LedInputs& in, int r, int g, int b, uint32_t from = 0, uint32_t to = 20000) {
    for (uint32_t t = from; t < to; t += 10)
        if (lit(ledColor(in, t)) && is(ledColor(in, t), r, g, b)) return true;
    return false;
}
}  // namespace

TEST_CASE("an idle board gives a faint blue pulse now and then, and is dark the rest of the time") {
    LedInputs idle;
    CHECK(shows(idle, 0, 0, 1));
    int dark = 0;
    for (uint32_t t = 0; t < 10000; t += 10) dark += !lit(ledColor(idle, t));
    CHECK(dark > 900);
}

TEST_CASE("a caller online blinks white, a door running blinks red, pending users blink green") {
    LedInputs a;
    a.callers = 1;
    CHECK(shows(a, 1, 1, 1));
    LedInputs d;
    d.doorRunning = true;
    CHECK(shows(d, 1, 0, 0));
    LedInputs p;
    p.pendingUsers = 2;
    CHECK(shows(p, 0, 1, 0));
}

TEST_CASE("several signals take turns, so each one shows") {
    LedInputs in;
    in.callers = 1;
    in.doorRunning = true;
    in.pendingUsers = 1;
    in.transfer = true;
    in.chat = true;
    CHECK(shows(in, 1, 1, 1));
    CHECK(shows(in, 1, 0, 0));
    CHECK(shows(in, 0, 1, 0));
    CHECK(shows(in, 0, 1, 1));  // transfer: cyan
    CHECK(shows(in, 0, 0, 1));  // chat: blue
}

TEST_CASE("a page for the sysop strobes magenta over everything else") {
    LedInputs in;
    in.callers = 1;
    in.doorRunning = true;
    in.paged = true;
    for (uint32_t t = 0; t < 4000; t += 10)
        if (lit(ledColor(in, t))) CHECK(is(ledColor(in, t), 1, 0, 1));
    CHECK(shows(in, 1, 0, 1, 0, 4000));
}

TEST_CASE("a lost main shows yellow") {
    LedInputs in;
    in.mainLost = true;
    CHECK(shows(in, 1, 1, 0));
}

TEST_CASE("an eight light ring: an idle comet goes round, a door lights only the reds, a page strobes the reds") {
    LedInputs idle;
    uint8_t   a[8], b[8];
    ledRing(idle, 0, a);
    ledRing(idle, 600, b);
    int headA = 0, headB = 0;
    for (int i = 0; i < 8; i++) {
        if (a[i] > a[headA]) headA = i;
        if (b[i] > b[headB]) headB = i;
    }
    CHECK(a[headA] == 255);
    CHECK(headA != headB);  // it moved round the ring
    LedInputs door;
    door.doorRunning = true;
    door.callers = 1;
    for (uint32_t t = 0; t < 3000; t += 70) {
        ledRing(door, t, a);
        for (int i = 1; i < 8; i += 2) CHECK(a[i] == 0);  // odd places are the greens: dark
    }
    LedInputs paged;
    paged.paged = true;
    bool on = false, off = false;
    for (uint32_t t = 0; t < 1000; t += 25) {
        ledRing(paged, t, a);
        on = on || (a[0] == 255 && a[2] == 255 && a[4] == 255 && a[6] == 255);
        off = off || (a[0] == 0 && a[2] == 0);
    }
    CHECK(on);
    CHECK(off);
}
