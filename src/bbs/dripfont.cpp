// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// theme::drip: menu titles as big block letters in the AXIS look: a white and grey highlight on
// top, orange bodies, a dark shadow to the lower right, and acid drips running off the bottom.
#pragma GCC optimize("Os")  // menus and screens: size over speed (the door CPU and transfers stay -O2)
#include <cstdio>
#include <cstring>
#include <string>

#include "bbs/theme.h"

namespace axis::bbs::theme {

namespace {
struct Glyph {
    char        c;
    const char* rows[5];
};
// five rows each, '#' = a block; widths vary (I, T and Y are narrow, M and W wide)
const Glyph kFont[] = {
    {'A', {" ## ", "#  #", "####", "#  #", "#  #"}}, {'B', {"### ", "#  #", "### ", "#  #", "### "}},
    {'C', {" ###", "#   ", "#   ", "#   ", " ###"}}, {'D', {"### ", "#  #", "#  #", "#  #", "### "}},
    {'E', {"####", "#   ", "### ", "#   ", "####"}}, {'F', {"####", "#   ", "### ", "#   ", "#   "}},
    {'G', {" ###", "#   ", "# ##", "#  #", " ###"}}, {'H', {"#  #", "#  #", "####", "#  #", "#  #"}},
    {'I', {"###", " # ", " # ", " # ", "###"}},       {'J', {"  ##", "   #", "   #", "#  #", " ## "}},
    {'K', {"#  #", "# # ", "##  ", "# # ", "#  #"}}, {'L', {"#   ", "#   ", "#   ", "#   ", "####"}},
    {'M', {"#   #", "## ##", "# # #", "#   #", "#   #"}}, {'N', {"#  #", "## #", "# ##", "#  #", "#  #"}},
    {'O', {" ## ", "#  #", "#  #", "#  #", " ## "}}, {'P', {"### ", "#  #", "### ", "#   ", "#   "}},
    {'Q', {" ## ", "#  #", "#  #", "# ##", " ###"}}, {'R', {"### ", "#  #", "### ", "# # ", "#  #"}},
    {'S', {" ###", "#   ", " ## ", "   #", "### "}}, {'T', {"###", " # ", " # ", " # ", " # "}},
    {'U', {"#  #", "#  #", "#  #", "#  #", " ## "}}, {'V', {"#   #", "#   #", " # # ", " # # ", "  #  "}},
    {'W', {"#   #", "#   #", "# # #", "## ##", "#   #"}}, {'X', {"#  #", "#  #", " ## ", "#  #", "#  #"}},
    {'Y', {"# #", "# #", " # ", " # ", " # "}},       {'Z', {"####", "  # ", " #  ", "#   ", "####"}},
    {'0', {" ## ", "#  #", "#  #", "#  #", " ## "}}, {'1', {" # ", "## ", " # ", " # ", "###"}},
    {'2', {"### ", "   #", " ## ", "#   ", "####"}}, {'3', {"### ", "   #", " ## ", "   #", "### "}},
    {'4', {"#  #", "#  #", "####", "   #", "   #"}}, {'5', {"####", "#   ", "### ", "   #", "### "}},
    {'6', {" ## ", "#   ", "### ", "#  #", " ## "}}, {'7', {"####", "   #", "  # ", " #  ", " #  "}},
    {'8', {" ## ", "#  #", " ## ", "#  #", " ## "}}, {'9', {" ## ", "#  #", " ###", "   #", " ## "}},
    {' ', {"  ", "  ", "  ", "  ", "  "}},             {':', {" ", "#", " ", "#", " "}},
    {'-', {"   ", "   ", "###", "   ", "   "}},       {'!', {"#", "#", "#", " ", "#"}},
    {'?', {"### ", "   #", " ## ", "    ", " #  "}}, {'.', {" ", " ", " ", " ", "#"}},
    {'\'', {"#", "#", " ", " ", " "}},                {'&', {" #  ", "# # ", " #  ", "# # ", " # #"}},
};
const Glyph* glyph(char c) {
    if (c >= 'a' && c <= 'z') c = (char)(c - 32);
    for (const Glyph& g : kFont)
        if (g.c == c) return &g;
    return nullptr;
}
const int kAnsi[8] = {0, 4, 2, 6, 1, 5, 3, 7};  // CGA colour order to ANSI's

uint32_t mix(uint32_t x) {  // a small hash: the same drips for the same title, every time
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    return x ^ (x >> 16);
}
}  // namespace

std::string drip(const std::string& text) {
    if (look() == Look::Oblivion) return titleBar(text);
    // lay the letters out on a grid: 5 letter rows, then up to 3 rows of drips
    const int   kRows = 10;  // 5 letter rows, up to 5 of drips
    std::string grid[kRows];
    for (char c : text) {
        const Glyph* g = glyph(c);
        if (!g) continue;
        size_t w = std::strlen(g->rows[0]);
        for (int r = 0; r < 5; r++) grid[r] += std::string(g->rows[r]) + " ";
        for (int r = 5; r < kRows; r++) grid[r] += std::string(w + 1, ' ');
    }
    if (grid[0].size() * 2 <= 78)  // a short title: every column twice, for fat letters
        for (int r = 0; r < 5; r++) {
            std::string wide;
            for (char ch : grid[r]) wide += std::string(2, ch);
            grid[r] = wide;
        }
    for (int r = 5; r < kRows; r++) grid[r] = std::string(grid[0].size(), ' ');
    size_t width = grid[0].size();
    if (width > 76) width = 76;  // (a title that long gets cut, not wrapped)
    uint32_t seed = 2166136261u;
    for (char c : text) seed = (seed ^ (uint8_t)c) * 16777619u;
    for (size_t x = 0; x < width; x++)  // drips: under some of the bottom row's blocks
        if (grid[4][x] == '#') {
            int len = (int)(mix(seed + (uint32_t)x * 977u) % 10);  // most drip a little, a few run long
            len = len < 3 ? 0 : len < 6 ? 1 : len < 8 ? 2 : len < 9 ? 3 : 5;
            for (int r = 0; r < len; r++) grid[5 + r][x] = r == len - 1 ? 'v' : '|';
        }
    int last = 5;
    for (int r = 5; r < kRows; r++)
        if (grid[r].find_first_not_of(' ') != std::string::npos) last = r + 1;

    // two font rows to a text row with half blocks (letters about as tall as they are wide):
    // white over light grey on top, orange bodies, a brown base, brown drips
    const int colour[kRows] = {15, 7, 6, 6, 6, 6, 6, 6, 6, 6};  // CGA: white, grey, then brown-orange
    auto      block = [&](int r, size_t x) { return r < last && x < grid[r].size() && grid[r][x] != ' '; };
    std::string pad((80 - width) / 2, ' '), out;
    for (int r = 0; r < last; r += 2) {
        out += pad;
        int cur = -1;
        for (size_t x = 0; x < width; x++) {
            bool top = block(r, x), bottom = block(r + 1, x);
            int  fg = 0, bg = 0;
            char ch = ' ';
            if (top && bottom) {
                fg = colour[r];
                bg = colour[r + 1] & 7;
                ch = fg == bg ? '\xdb' : '\xdf';  // two colours: the top half's on the bottom's
            } else if (top) {
                fg = colour[r];
                ch = '\xdf';
            } else if (bottom) {
                fg = colour[r + 1];
                ch = '\xdc';
            } else if (x > 0 && (block(r, x - 1) || block(r + 1, x - 1)) && r > 0) {
                fg = 8;  // a dark shadow to the right
                ch = '\xb0';
            }
            int key = fg << 4 | bg;
            if (ch != ' ' && key != cur) {
                cur = key;
                char sgr[24];
                std::snprintf(sgr, sizeof sgr, "\x1b[0;%s3%d;4%dm", fg & 8 ? "1;" : "", kAnsi[fg & 7], kAnsi[bg & 7]);
                out += sgr;
            } else if (ch == ' ' && cur != -1 && (cur & 15) != 0) {
                out += "\x1b[0m";
                cur = -1;
            }
            out += ch;
        }
        out += std::string("\x1b[0m") + TEXT + "\r\n";
    }
    return out;
}

}  // namespace axis::bbs::theme
