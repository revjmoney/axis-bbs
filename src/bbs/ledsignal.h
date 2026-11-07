// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// Status light for this board. The bare board uses its blue LED (GPIO 2, active
// high), capped hard because full brightness is too much. A CYD uses its RGB LED
// (GPIO 4 red, 16 green, 17 blue, active low) with the same timing, peaked at
// about 75 percent. GPIO 2 stays a panel pin on the CYD.
//   door running     fast blink (red on a CYD)
//   no callers       slow dim breathe (the colour drifts on a CYD)
//   callers on       one fade-in blink per caller, each pulse its own colour
#pragma once
#include <cstdint>

namespace axis::bbs {

struct LedInputs {
    int  nodes = 0;  // callers on this board
    bool door = false;
};

struct LedColour {
    uint8_t r, g, b;  // mix, 0-255. Brightness is ledDuty, scaled per board.
};

constexpr uint8_t kLedFadePeak = 32;     // bare board, out of 255
constexpr uint8_t kLedBreathePeak = 16;  // idle stays under the fade
constexpr uint8_t kCydFadePeak = 191;    // about 75 percent of 255

// Duty for the bare board's LEDC. 0 is off. Already under the dim cap.
uint8_t ledDuty(const LedInputs& in, uint32_t nowMs);

// The same duty, scaled so a full fade is kCydFadePeak.
uint8_t ledCydDuty(uint8_t bareDuty);

// Colour for this moment. A door is red. Idle walks the hue. Each caller pulse
// takes the next colour. The driver multiplies this by the duty.
LedColour ledColour(const LedInputs& in, uint32_t nowMs);

}  // namespace axis::bbs
