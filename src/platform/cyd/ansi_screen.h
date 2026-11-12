// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// An 80x30 CP437 text screen drawn with 4x8 cells (the VGA 8x16 font averaged 2x2),
// which fills the CYD's 320x240 panel exactly. Same idea as the CYD Demoscene ANSI player.
// renderCells draws any AnsiTerm in bigger 7x10 cells (the font sampled 2x2 a pixel): the waiting screen's 45x24 is 315x240.
#pragma once
#include <stdint.h>

#include "platform/cyd/board.h"

namespace axis::bbs {
class AnsiTerm;
}

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
    // easy to read: 40 columns from firstCol in full 8x8 cells (each pixel row merges two font rows); y0: where row 0 lands
    // in dst (negative for a band further down the screen), clipped to dst
    void renderZoom(LGFX_Sprite& dst, int rows, int firstCol, int y0 = 0) const;
    // all 80 columns in 4x8 cells, crisp: each pixel is lit if 2 or more of its 4 font pixels are (no blending)
    void renderSharp(LGFX_Sprite& dst, int rows, int y0 = 0) const;
    // art straight from flash (rows x 80 x {ch, attr}): no copy in RAM
    static void renderArt(LGFX_Sprite& dst, const uint8_t (*art)[kCols][2], int rows);
    // a whole terminal in 7x10 cells (the 8x16 font sampled 2x2 a pixel, blended): 45x24 is 315x240, centred
    static void renderCells(LGFX_Sprite& dst, const axis::bbs::AnsiTerm& t);
    // the same, `rows` terminal rows from firstRow into a band (7x10 cells, so a 40 pixel band is 4 rows)
    static void renderCellsBand(LGFX_Sprite& band, const axis::bbs::AnsiTerm& t, int firstRow, int rows);
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
    void render(LGFX_Sprite& dst, int y0, int rows) const;  // y0: where row 0 lands in dst (may be negative), clipped to dst
};
