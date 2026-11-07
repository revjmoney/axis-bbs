// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#pragma GCC optimize("Os")  // menus and screens: size over speed (the door CPU and transfers stay -O2)
#include "platform/cyd/screen.h"

LGFX_Sprite scr(&tft);

const uint8_t kPalette[16][3] = {{0, 0, 0},       {0, 0, 170},    {0, 170, 0},    {0, 170, 170},
                                 {170, 0, 0},     {170, 0, 170},  {170, 85, 0},   {170, 170, 170},
                                 {85, 85, 85},    {85, 85, 255},  {85, 255, 85},  {85, 255, 255},
                                 {255, 130, 0},   {40, 40, 40},   {255, 255, 85}, {255, 255, 255}};

uint8_t palNearest(int r, int g, int b) {
    int best = 0, bestD = 1 << 30;
    for (int i = 0; i < 16; i++) {
        int dr = r - kPalette[i][0], dg = g - kPalette[i][1], db = b - kPalette[i][2];
        int d = dr * dr * 3 + dg * dg * 4 + db * db * 2;  // eyes care most about green, least about blue
        if (d < bestD) {
            bestD = d;
            best = i;
        }
    }
    return (uint8_t)best;
}

bool screenInit() {
    scr.setColorDepth(4);
    if (!scr.createSprite(320, 240)) return false;
    uint32_t rgb[16];
    for (int i = 0; i < 16; i++) rgb[i] = (uint32_t)kPalette[i][0] << 16 | kPalette[i][1] << 8 | kPalette[i][2];
    return scr.createPalette(rgb, 16);
}

static bool lent = false;  // a door is using the copy's memory as guest pages

// Lending instead of freeing keeps the 38.4 KB block whole: after a door the heap may have the
// bytes but no single piece that big.
uint8_t* screenLend(size_t& bytes) {
    bytes = scr.getBuffer() ? (size_t)scr.width() * scr.height() / 2 : 0;
    lent = bytes != 0;
    return (uint8_t*)scr.getBuffer();
}

void screenReturn() { lent = false; }

bool screenReady() { return scr.getBuffer() != nullptr && !lent; }

void screenPush() {
    if (!boardHasPanel()) return;  // the ring runs this same image and has no panel
    if (screenReady()) scr.pushSprite(0, 0);
}

void screenShot(void (*eachRow)()) {
    static const char* d = "0123456789ABCDEF";
    const uint8_t*     p = screenReady() ? (const uint8_t*)scr.getBuffer() : nullptr;
    if (!p) {
        Serial.println("@SHOT NONE");  // no copy while a door runs
        return;
    }
    uint8_t rgb332[16];  // the PC tool reads RGB332, as before the copy went 4-bit
    for (int i = 0; i < 16; i++)
        rgb332[i] = (kPalette[i][0] * 7 / 255) << 5 | (kPalette[i][1] * 7 / 255) << 2 | (kPalette[i][2] * 3 / 255);
    Serial.printf("@SHOT %d %d\n", scr.width(), scr.height());
    char line[320 * 2 + 1];
    for (int y = 0; y < scr.height(); y++) {
        for (int x = 0; x < scr.width(); x++) {
            uint8_t two = p[y * scr.width() / 2 + x / 2];
            uint8_t v   = rgb332[(x & 1) ? (two & 15) : (two >> 4)];  // left pixel in the high nibble
            line[x * 2]     = d[v >> 4];
            line[x * 2 + 1] = d[v & 15];
        }
        line[scr.width() * 2] = 0;
        Serial.println(line);
        if (eachRow) eachRow();
    }
    Serial.println("@SHOT END");
}
