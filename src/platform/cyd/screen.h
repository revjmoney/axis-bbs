// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The sysop screens draw into a 4-bit off-screen copy (16 colours, 38.4 KB), then push it to the
// panel. It was 8-bit (76.8 KB), but a main board serving the cluster ran out of heap, and the
// screens only ever used a handful of colours. The copy is what @SHOT sends over serial, since
// this panel cannot be read back.
// The WFC (the idle screen) needs no full copy: it is drawn through a 320x40 band (6.4 KB), 40
// pixel rows at a time. The full copy is made only while another screen is up and goes when the
// WFC is back. With no room for it, that screen draws straight to the panel. A headless board has
// neither.
#pragma once
#include "platform/cyd/board.h"

namespace axis::bbs {
class AnsiTerm;
}

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

bool         screenFull();          // make the full copy (38.4 KB) if there is none; false: no room (draw straight to the panel)
void         screenFullRelease();   // back at the WFC: the full copy goes (no screenGfx() reference may outlive this)
LovyanGFX&   screenGfx();           // where a screen draws: the full copy, else the panel itself
LGFX_Sprite* screenSprite();        // the full copy, or nullptr
uint32_t     screenColor(uint8_t p);  // a pal:: colour for screenGfx(): its number in the copy, its RGB on the panel
void         screenPushIfSprite();  // show the full copy (drawing that went straight to the panel needs no push)
bool         screenBandsInit();     // the WFC's 320x40 band (6.4 KB), made once
LGFX_Sprite* screenBand();          // the band, or nullptr
void         screenFailNext();      // @SPRITEFAIL (bench): the next screenFull() that has to make the copy fails
// dump the screen over serial: "@SHOT 320 240", RGB332 hex rows, "@SHOT END"; eachRow keeps the board's work going (it
// takes ~13 s). wfc: the WFC's terminal, drawn band by band into the dump. No wfc and no full copy: "@SHOT NONE".
void screenShot(void (*eachRow)() = nullptr, const axis::bbs::AnsiTerm* wfc = nullptr);
