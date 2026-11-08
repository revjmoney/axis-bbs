// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// Hardware setup for the ESP32-2432S028R ("Cheap Yellow Display").
// Copied from the CYD Demoscene firmware (same author).
// Panel/touch pin map lifted from bdring's FluidDial CYD support.
#pragma once
#define LGFX_USE_V1
#include <LovyanGFX.hpp>

extern lgfx::LGFX_Device tft;

// Every board runs this one image. A headless board (a bare ESP32: the ring, a terminal-only
// node) has no panel and no touch, and its status light is one LED on GPIO 2 instead of the
// CYD's RGB. A screen can't be detected safely (an unread MISO line looks like an ILI9341, and
// the probe would drive whatever is wired to those pins), so it is a per-board setting kept in
// NVS: @HEADLESS on / off, then a reboot. The ring's MAC is headless without being told.
bool boardIsHeadless();
bool boardHeadlessSaved();          // what NVS says (the ring is headless either way)
bool boardSetHeadless(bool on);     // saves it; takes effect at the next boot
bool boardHasPanel();

// returns "ILI9341", "ST7789", or "none (headless node)" on the ring
const char* boardInit(uint8_t rotation, uint32_t spiHz);
bool boardTouch(int& x, int& y);
