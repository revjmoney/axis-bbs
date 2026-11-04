// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The CYD's RGB LED tells the sysop what the whole cluster is doing, from across the room. Each
// active signal gets a 2 second turn, so several can show at once:
//   white flash once a second   a caller is online (any node)
//   red 1 Hz blink              a door is running (any node)
//   green long blink            new user applications are waiting
//   cyan breathing              a file transfer is running
//   blue double blink           someone is in the teleconference
//   yellow double blink         the main board was lost (failover) a moment ago
//   magenta strobe              a caller paged the sysop (over everything else, for a minute)
//   faint blue pulse every 5 s  idle: the board is alive
#pragma once
#include <cstdint>

namespace axis::bbs {

struct Rgb {
    uint8_t r = 0, g = 0, b = 0;
};

struct LedInputs {
    int  callers = 0, pendingUsers = 0;
    bool doorRunning = false, transfer = false, chat = false, paged = false, mainLost = false;
};

Rgb ledColor(const LedInputs& in, uint32_t nowMs);

// Eight single-colour lights in a ring (a headless node's status ring), red at even places and
// green at odd ones, as brightness 0-255: a comet goes round (faster with callers on; reds only
// while a door runs, greens only for a transfer, two comets for chat), new users flash the greens,
// a page strobes the reds, a lost main pulses them slowly.
void ledRing(const LedInputs& in, uint32_t nowMs, uint8_t out[8]);

}  // namespace axis::bbs
