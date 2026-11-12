// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#pragma GCC optimize("Os")  // menus and screens: size over speed (the door CPU and transfers stay -O2)
#include "platform/cyd/ansi_screen.h"

#include <string.h>

#include "bbs/ansiterm.h"
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
void renderGrid(LGFX_Sprite& dst, int rows, Cells cells);

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
void renderGrid(LGFX_Sprite& dst, int rows, Cells cells) {
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
    renderGrid(dst, rows, [this](int r, int c) { return cells[r][c]; });
}

void AnsiScreen::renderArt(LGFX_Sprite& dst, const uint8_t (*art)[kCols][2], int rows) {
    renderGrid(dst, rows, [art](int r, int c) { return AnsiCell{art[r][c][0], art[r][c][1]}; });
}

namespace {
// terminal rows firstRow.. into fb from its top, in 7x10 cells: each pixel samples the 8x16 glyph at 2x2 points (a quarter and
// three quarters across the pixel); how many of the 4 are lit picks the blend. 45 cells are 315 pixels: 2 black columns on the
// left, 3 on the right. Rows past the terminal's end are black. (renderCells and renderCellsBand both draw here.)
void cellRows(uint8_t* fb, const axis::bbs::AnsiTerm& t, int firstRow, int rows) {
    if (!ready) build();
    static const uint8_t kCol[7][2] = {{0, 0}, {1, 2}, {2, 3}, {3, 4}, {4, 5}, {6, 6}, {7, 7}};
    static const uint8_t kRow[10][2] = {{0, 1}, {2, 2}, {3, 4}, {5, 6}, {6, 7}, {8, 9}, {10, 10}, {11, 12}, {13, 14}, {14, 15}};
    const int kLeft = 2;
    int cols = t.cols() < 45 ? t.cols() : 45, last = t.rows() < 24 ? t.rows() : 24;
    for (int i = 0; i < rows; i++) {
        int r = firstRow + i;
        for (int y = 0; y < 10; y++) {
            uint8_t* out = fb + (i * 10 + y) * 160;
            memset(out, 0, 160);
            if (r < 0 || r >= last) continue;
            int px = kLeft;
            for (int c = 0; c < cols; c++) {
                const auto&    cell = t.cell(r, c);
                uint8_t        a = vga_font8x16[cell.ch * 16 + kRow[y][0]], b = vga_font8x16[cell.ch * 16 + kRow[y][1]];
                const uint8_t* l = lut[cell.attr];
                for (int x = 0; x < 7; x++, px++) {
                    uint8_t m0 = 0x80 >> kCol[x][0], m1 = 0x80 >> kCol[x][1];
                    uint8_t v = l[!!(a & m0) + !!(a & m1) + !!(b & m0) + !!(b & m1)];
                    out[px >> 1] |= (px & 1) ? v : (uint8_t)(v << 4);
                }
            }
        }
    }
}
}  // namespace

void AnsiScreen::renderCells(LGFX_Sprite& dst, const axis::bbs::AnsiTerm& t) {
    uint8_t* fb = (uint8_t*)dst.getBuffer();
    if (fb) cellRows(fb, t, 0, t.rows() < 24 ? t.rows() : 24);
}

void AnsiScreen::renderCellsBand(LGFX_Sprite& band, const axis::bbs::AnsiTerm& t, int firstRow, int rows) {
    uint8_t* fb = (uint8_t*)band.getBuffer();
    if (rows > band.height() / 10) rows = band.height() / 10;
    if (fb) cellRows(fb, t, firstRow, rows);
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

void AnsiScreen::renderZoom(LGFX_Sprite& dst, int rows, int firstCol, int y0) const {
    if (!ready) build();
    uint8_t* fb = (uint8_t*)dst.getBuffer();
    if (!fb) return;
    for (int r = 0; r < rows && r < kRows; r++)
        for (int y = 0; y < 8; y++) {
            int py = y0 + r * 8 + y;
            if (py < 0) continue;
            if (py >= dst.height()) return;
            uint8_t* out = fb + py * 160;
            for (int c = 0; c < 40; c++, out += 4) {
                int            col = firstCol + c;
                const AnsiCell cell = col >= 0 && col < kCols ? cells[r][col] : AnsiCell{' ', 0x07};
                uint8_t        bits = vga_font8x16[cell.ch * 16 + y * 2] | vga_font8x16[cell.ch * 16 + y * 2 + 1];
                const uint8_t* l = lut[cell.attr];
                for (int b = 0; b < 8; b += 2)
                    out[b / 2] = (uint8_t)(((bits & (0x80 >> b)) ? l[4] : l[0]) << 4 | ((bits & (0x40 >> b)) ? l[4] : l[0]));
            }
        }
}

void AnsiScreen::renderSharp(LGFX_Sprite& dst, int rows, int y0) const {
    if (!ready) build();
    uint8_t* fb = (uint8_t*)dst.getBuffer();
    if (!fb) return;
    for (int r = 0; r < rows && r < kRows; r++)
        for (int y = 0; y < 8; y++) {
            int py = y0 + r * 8 + y;
            if (py < 0) continue;
            if (py >= dst.height()) return;
            uint8_t* out = fb + py * 160;
            for (int c = 0; c < kCols; c++, out += 2) {
                const AnsiCell& cell = cells[r][c];
                const uint8_t*  g = &vga_font8x16[cell.ch * 16 + y * 2];
                const uint8_t*  l = lut[cell.attr];
                uint8_t         px[4];
                for (int x = 0; x < 4; x++) px[x] = cover(g[0], g[1], x) >= 2 ? l[4] : l[0];
                out[0] = (uint8_t)(px[0] << 4 | px[1]);
                out[1] = (uint8_t)(px[2] << 4 | px[3]);
            }
        }
}

void BigText::render(LGFX_Sprite& dst, int y0, int rows) const {
    if (!ready) build();
    uint8_t* fb = (uint8_t*)dst.getBuffer();
    if (!fb) return;
    for (int r = 0; r < rows && r < kRows; r++)
        for (int y = 0; y < 16; y++) {
            int py = y0 + r * 16 + y;
            if (py < 0 || py >= dst.height()) continue;
            uint8_t* out = fb + py * 160;
            for (int c = 0; c < kCols; c++, out += 4) {
                uint8_t        bits = vga_font8x16[cells[r][c].ch * 16 + y];
                const uint8_t* l    = lut[cells[r][c].attr];
                for (int b = 0; b < 8; b += 2)
                    out[b / 2] = (uint8_t)(((bits & (0x80 >> b)) ? l[4] : l[0]) << 4 | ((bits & (0x40 >> b)) ? l[4] : l[0]));
            }
        }
}
