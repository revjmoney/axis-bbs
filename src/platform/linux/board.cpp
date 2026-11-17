// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The board's hardware on Linux: no panel and no touch yet (the window, an SDL panel, is plan task 6), so the
// program runs as a headless board does.
#include "platform/cyd/board.h"

#include <Preferences.h>

lgfx::LGFX_Device tft;  // no panel set: nothing draws to it while boardHasPanel() is false

bool boardHeadlessSaved() {
    Preferences p;
    p.begin("brd", true);
    bool on = p.getBool("headless", false);
    p.end();
    return on;
}

bool boardSetHeadless(bool on) {
    Preferences p;
    p.begin("brd", false);
    bool ok = p.putBool("headless", on) > 0;
    p.end();
    return ok;
}

// Terminal and headless start modes alike: the terminal shows the board's serial console (its WFC command), not a
// panel. Only the window start mode (task 6) will give it one.
bool boardIsHeadless() { return true; }
bool boardHasPanel() { return !boardIsHeadless(); }

const char* boardInit(uint8_t rotation, uint32_t spiHz) {
    (void)rotation;
    (void)spiHz;
    return "none (headless node)";
}

bool boardTouch(int& x, int& y) {
    (void)x;
    (void)y;
    return false;
}
