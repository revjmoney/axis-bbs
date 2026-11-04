// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include "platform/cyd/ansi_screen.h"

#include <string.h>

#include "platform/cyd/screen.h"

#include "../../../third_party/vga_font/font8x16.h"  // IBM VGA 8x16 by VileR, CC BY-SA 4.0

namespace {
const uint8_t kCga[16][3] = {{0, 0, 0},     {0, 0, 170},    {0, 170, 0},    {0, 170, 170},
                             {170, 0, 0},   {170, 0, 170},  {170, 85, 0},   {170, 170, 170},
                             {85, 85, 85},  {85, 85, 255},  {85, 255, 85},  {85, 255, 255},
                             {255, 85, 85}, {255, 85, 255}, {255, 255, 85}, {255, 255, 255}};

uint8_t lut[256][5];     // per attribute: palette number for each coverage level (the nearest to the blend)
bool    ready = false;


// how much of a 2x2 block of font pixels is lit (0..4): two bits from each of two font rows
const uint8_t kPairBits[4] = {0, 1, 1, 2};
inline int cover(uint8_t top, uint8_t bottom, int x) { return kPairBits[(top >> (6 - 2 * x)) & 3] + kPairBits[(bottom >> (6 - 2 * x)) & 3]; }

// cells(r, c) -> {ch, attr}: an AnsiScreen's cells, or art straight from flash
template <typename Cells>
void renderCells(LGFX_Sprite& dst, int rows, Cells cells);

void build() {
    for (int a = 0; a < 256; a++) {
        const uint8_t* f = kCga[a & 15];
        const uint8_t* b = kCga[(a >> 4) & 7];  // bit 7 is blink, not bright background
        for (int l = 0; l <= 4; l++)
            lut[a][l] = palNearest((b[0] * (4 - l) + f[0] * l) / 4, (b[1] * (4 - l) + f[1] * l) / 4,
                               (b[2] * (4 - l) + f[2] * l) / 4);
    }
    ready = true;
}
}  // namespace

void AnsiScreen::load(const uint8_t art[kRows][kCols][2]) { memcpy(cells, art, sizeof cells); }

void AnsiScreen::text(int row, int col, const char* s, uint8_t attr) {
    if (row < 0 || row >= kRows) return;
    for (; *s && col < kCols; s++, col++)
        if (col >= 0) cells[row][col] = {(uint8_t)*s, attr};
}

void AnsiScreen::fill(int row, int col, int len, uint8_t ch, uint8_t attr) {
    for (int i = 0; i < len; i++)
        if (row >= 0 && row < kRows && col + i >= 0 && col + i < kCols) cells[row][col + i] = {ch, attr};
}

namespace {
template <typename Cells>
void renderCells(LGFX_Sprite& dst, int rows, Cells cells) {
    if (!ready) build();
    uint8_t* fb = (uint8_t*)dst.getBuffer();
    if (!fb) return;
    for (int r = 0; r < rows && r < AnsiScreen::kRows; r++)
        for (int y = 0; y < 8; y++) {
            uint8_t* out = fb + (r * 8 + y) * 160;
            for (int c = 0; c < AnsiScreen::kCols; c++) {  // the coverage straight from the font (no 8 KB table)
                AnsiCell       cell = cells(r, c);
                const uint8_t* g = &vga_font8x16[cell.ch * 16 + y * 2];
                const uint8_t* l = lut[cell.attr];
                out[0] = (uint8_t)(l[cover(g[0], g[1], 0)] << 4 | l[cover(g[0], g[1], 1)]);
                out[1] = (uint8_t)(l[cover(g[0], g[1], 2)] << 4 | l[cover(g[0], g[1], 3)]);
                out += 2;
            }
        }
}
}  // namespace

void AnsiScreen::render(LGFX_Sprite& dst, int rows) const {
    renderCells(dst, rows, [this](int r, int c) { return cells[r][c]; });
}

void AnsiScreen::renderArt(LGFX_Sprite& dst, const uint8_t (*art)[kCols][2], int rows) {
    renderCells(dst, rows, [art](int r, int c) { return AnsiCell{art[r][c][0], art[r][c][1]}; });
}

void BigText::clear(uint8_t attr) {
    for (auto& row : cells)
        for (auto& c : row) c = {' ', attr};
}

void BigText::text(int row, int col, const char* s, uint8_t attr) {
    if (row < 0 || row >= kRows) return;
    for (; *s && col < kCols; s++, col++)
        if (col >= 0) cells[row][col] = {(uint8_t)*s, attr};
}

void BigText::fill(int row, int col, int len, uint8_t ch, uint8_t attr) {
    for (int i = 0; i < len; i++)
        if (row >= 0 && row < kRows && col + i >= 0 && col + i < kCols) cells[row][col + i] = {ch, attr};
}

void BigText::render(LGFX_Sprite& dst, int y0, int rows) const {
    if (!ready) build();
    uint8_t* fb = (uint8_t*)dst.getBuffer();
    if (!fb) return;
    for (int r = 0; r < rows && r < kRows; r++)
        for (int y = 0; y < 16; y++) {
            int py = y0 + r * 16 + y;
            if (py < 0 || py >= 240) continue;
            uint8_t* out = fb + py * 160;
            for (int c = 0; c < kCols; c++, out += 4) {
                uint8_t        bits = vga_font8x16[cells[r][c].ch * 16 + y];
                const uint8_t* l    = lut[cells[r][c].attr];
                for (int b = 0; b < 8; b += 2)
                    out[b / 2] = (uint8_t)(((bits & (0x80 >> b)) ? l[4] : l[0]) << 4 | ((bits & (0x40 >> b)) ? l[4] : l[0]));
            }
        }
}
