// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The sysop screens draw into a 4-bit off-screen copy (16 colours, 38.4 KB), then push it to the
// panel. It was 8-bit (76.8 KB), but a main board serving the cluster ran out of heap, and the
// screens only ever used a handful of colours. The copy is what @SHOT sends over serial, since
// this panel cannot be read back.
#pragma once
#include "platform/cyd/board.h"

extern LGFX_Sprite scr;  // 320x240, 4 bits a pixel: draw with pal:: numbers, not RGB565 colours

// The palette: the CGA colours in attribute order (so an ANSI colour number is its index), with
// the two the screens never use (light red, light magenta) given to the theme's orange and dim grey.
namespace pal {
enum : uint8_t {
    BLACK, BLUE, GREEN, CYAN, RED, MAGENTA, BROWN, LIGHTGREY,
    DARKGREY, LIGHTBLUE, LIGHTGREEN, LIGHTCYAN, ORANGE, DIM, YELLOW, WHITE
};
}
extern const uint8_t kPalette[16][3];      // r, g, b
uint8_t palNearest(int r, int g, int b);  // the palette colour closest to this one

bool screenInit();        // allocate the copy (38.4 KB)
uint8_t* screenLend(size_t& bytes);  // the copy's RAM for a door (drawing pauses); stays allocated
void     screenReturn();              // the door is done: the copy is ours again (redraw it)
bool     screenReady();               // the copy can be drawn on
void screenPush();        // show it on the panel
void screenShot(void (*eachRow)() = nullptr);  // dump it over serial: "@SHOT 320 240", RGB332 hex rows, "@SHOT END";
                                               // eachRow keeps the board's work going (it takes ~13 s)
