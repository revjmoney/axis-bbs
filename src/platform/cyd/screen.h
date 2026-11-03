// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The sysop screens draw into an 8-bit off-screen copy, then push it to the panel.
// The copy is what @SHOT sends over serial, since this panel cannot be read back.
#pragma once
#include "platform/cyd/board.h"

extern LGFX_Sprite scr;   // 320x240, RGB332

bool screenInit();        // allocate the copy (76.8 KB)
uint8_t* screenLend(size_t& bytes);  // the copy's RAM for a door (drawing pauses); stays allocated
void     screenReturn();              // the door is done: the copy is ours again (redraw it)
bool     screenReady();               // the copy can be drawn on
void screenPush();        // show it on the panel
void screenShot();        // dump it over serial: "@SHOT 320 240" then hex rows, then "@SHOT END"
