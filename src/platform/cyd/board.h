// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// Hardware setup for the ESP32-2432S028R ("Cheap Yellow Display").
// Copied from the CYD Demoscene firmware (same author).
// Panel/touch pin map lifted from bdring's FluidDial CYD support.
#pragma once
#define LGFX_USE_V1
#include <LovyanGFX.hpp>

extern lgfx::LGFX_Device tft;

// The bare board shares this image with the two screens. Its MAC is the only safe tell:
// a panel probe drives the same pins as the old light breakout.
bool boardIsRing();
bool boardHasPanel();

// returns "ILI9341", "ST7789", or "none (headless node)" on the ring
const char* boardInit(uint8_t rotation, uint32_t spiHz);
bool boardTouch(int& x, int& y);
