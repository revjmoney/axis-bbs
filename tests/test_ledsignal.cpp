// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include "doctest.h"
#include "bbs/ledsignal.h"

using namespace axis::bbs;

TEST_CASE("an idle board breathes under the dim cap and never reaches a fade") {
    LedInputs idle;
    uint8_t   peak = 0;
    bool      mid = false;
    for (uint32_t t = 0; t < 4000; t += 20) {
        uint8_t d = ledDuty(idle, t);
        if (d > peak) peak = d;
        if (d > 4 && d < kLedBreathePeak) mid = true;
    }
    CHECK(peak == kLedBreathePeak);
    CHECK(peak < kLedFadePeak);
    CHECK(mid);
    CHECK(ledDuty(idle, 0) == 0);
}

TEST_CASE("one caller fades in once, two callers fade in twice, then the light rests") {
    LedInputs one;
    one.nodes = 1;
    int rises = 0;
    bool up = false;
    for (uint32_t t = 0; t < 2400; t += 20) {
        bool on = ledDuty(one, t) > 8;
        if (on && !up) rises++;
        up = on;
    }
    CHECK(rises == 1);
    CHECK(ledDuty(one, 400) > 8);
    CHECK(ledDuty(one, 400) <= kLedFadePeak);

    LedInputs two;
    two.nodes = 2;
    rises = 0;
    up = false;
    for (uint32_t t = 0; t < 3400; t += 20) {
        bool on = ledDuty(two, t) > 8;
        if (on && !up) rises++;
        up = on;
    }
    CHECK(rises == 2);
    CHECK(ledDuty(two, 2500) == 0);  // the rest after the two fades
}

TEST_CASE("a door blinks fast and stays inside the dim cap") {
    LedInputs d;
    d.door = true;
    d.nodes = 3;
    CHECK(ledDuty(d, 0) == kLedFadePeak);
    CHECK(ledDuty(d, 100) == 0);
    CHECK(ledDuty(d, 200) == kLedFadePeak);
    CHECK(ledDuty(d, 40) == kLedFadePeak);
}

TEST_CASE("a cyd scales the same duty to about 75 percent") {
    CHECK(ledCydDuty(0) == 0);
    CHECK(ledCydDuty(kLedBreathePeak) == (uint8_t)((uint16_t)kLedBreathePeak * kCydFadePeak / kLedFadePeak));
    CHECK(ledCydDuty(kLedFadePeak) == kCydFadePeak);
    CHECK(kCydFadePeak > kLedFadePeak);
    CHECK(kCydFadePeak < 255);
}

TEST_CASE("a cyd door is red, idle drifts, and each caller pulse has its own colour") {
    LedInputs door;
    door.door = true;
    door.nodes = 2;
    LedColour red = ledColour(door, 40);
    CHECK(red.r == 255);
    CHECK(red.g == 0);
    CHECK(red.b == 0);

    LedInputs idle;
    LedColour a = ledColour(idle, 0);
    LedColour b = ledColour(idle, 4000);
    CHECK((a.r != b.r || a.g != b.g || a.b != b.b));

    LedInputs two;
    two.nodes = 2;
    LedColour p0 = ledColour(two, 200);
    LedColour p1 = ledColour(two, 1200);
    CHECK((p0.r != p1.r || p0.g != p1.g || p0.b != p1.b));
    CHECK((p0.r != 255 || p0.g != 0 || p0.b != 0));
    CHECK(ledColour(two, 2500).r == 0);
    CHECK(ledColour(two, 2500).g == 0);
    CHECK(ledColour(two, 2500).b == 0);
}
