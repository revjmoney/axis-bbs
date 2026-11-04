// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// An 80x30 CP437 text screen drawn with 4x8 cells (the VGA 8x16 font averaged 2x2),
// which fills the CYD's 320x240 panel exactly. Same idea as the CYD Demoscene ANSI player.
#pragma once
#include <stdint.h>

#include "platform/cyd/board.h"

struct AnsiCell {
    uint8_t ch, attr;  // CP437 character, CGA attribute (fg | bg << 4)
};

class AnsiScreen {
public:
    static constexpr int kCols = 80, kRows = 30;
    AnsiCell cells[kRows][kCols];

    void load(const uint8_t art[kRows][kCols][2]);
    void text(int row, int col, const char* s, uint8_t attr);  // clipped at the right edge
    void fill(int row, int col, int len, uint8_t ch, uint8_t attr);
    void render(LGFX_Sprite& dst, int rows = kRows) const;      // top `rows` rows, onto the 4-bit sprite
    // easy to read: 40 columns from firstCol in full 8x8 cells (each pixel row merges two font rows)
    void renderZoom(LGFX_Sprite& dst, int rows, int firstCol) const;
    // all 80 columns in 4x8 cells, crisp: each pixel is lit if 2 or more of its 4 font pixels are (no blending)
    void renderSharp(LGFX_Sprite& dst, int rows) const;
    // art straight from flash (rows x 80 x {ch, attr}): no copy in RAM
    static void renderArt(LGFX_Sprite& dst, const uint8_t (*art)[kCols][2], int rows);
};

// A 40-column grid of full-size 8x16 cells, for text that has to be easy to read (15 rows fill
// the panel; callers render as many as they use).
class BigText {
public:
    static constexpr int kCols = 40, kRows = 15;
    AnsiCell cells[kRows][kCols];
    void clear(uint8_t attr = 0x07);
    void text(int row, int col, const char* s, uint8_t attr);
    void fill(int row, int col, int len, uint8_t ch, uint8_t attr);
    void render(LGFX_Sprite& dst, int y0, int rows) const;
};
