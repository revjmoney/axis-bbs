// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The board's hardware on Linux. In the window start mode (window.h) the screen is LovyanGFX's SDL panel and the
// touch is the mouse on it; in the terminal and headless modes there is no panel, and the program runs as a headless
// board does.
#include "platform/cyd/board.h"

#include <Preferences.h>

#include "platform/linux/host.h"
#if AXIS_WINDOW
#include "platform/linux/window.h"
#endif

lgfx::LGFX_Device tft;  // the window's panel (window.cpp sets it); with none, nothing draws while boardHasPanel() is false

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
// panel. Only the window start mode gives it one (decided in main() before setup(), and fixed from then on).
bool boardIsHeadless() { return !axis::host::windowOn; }
bool boardHasPanel() { return !boardIsHeadless(); }

const char* boardInit(uint8_t rotation, uint32_t spiHz) {
    (void)spiHz;
    if (boardIsHeadless()) return "none (headless node)";
    tft.setRotation(rotation);  // the panel itself was made by windowOpen(), before this thread started
    return "SDL window";
}

// The mouse in the window, left button down. The board's own 180 degree touch fix (cyd/board.cpp) is for its
// XPT2046 and does not apply: the panel reports the point already in screen coordinates.
bool boardTouch(int& x, int& y) {
#if AXIS_WINDOW
    if (axis::host::windowOn) return windowTouch(x, y);
#endif
    (void)x;
    (void)y;
    return false;
}
