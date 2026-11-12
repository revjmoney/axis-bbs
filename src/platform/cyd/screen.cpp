// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#pragma GCC optimize("Os")  // menus and screens: size over speed (the door CPU and transfers stay -O2)
#include "platform/cyd/screen.h"

#include "platform/cyd/ansi_screen.h"

static LGFX_Sprite full(&tft);  // 320x240, 4 bits a pixel: draw with pal:: numbers, only while a screen other than the WFC is up
static LGFX_Sprite band(&tft);  // 320x40, the WFC's band
static bool        failNext = false;

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

static uint32_t rgbOf(int i) { return (uint32_t)kPalette[i][0] << 16 | kPalette[i][1] << 8 | kPalette[i][2]; }

static void drop(LGFX_Sprite& s) {
    tft.waitDMA();  // a push may still be reading it
    s.deleteSprite();
    s.deletePalette();
}

// 320 wide, 4 bits a pixel, the palette above (both copies: the same pixels as the full copy always had)
static bool make(LGFX_Sprite& s, int h) {
    s.setColorDepth(4);
    if (s.createSprite(320, h)) {
        uint32_t rgb[16];
        for (int i = 0; i < 16; i++) rgb[i] = rgbOf(i);
        if (s.createPalette(rgb, 16)) return true;
    }
    drop(s);
    return false;
}

bool screenFull() {
    if (full.getBuffer()) return true;
    if (!boardHasPanel()) return false;
    if (failNext) {
        failNext = false;
        return false;
    }
    return make(full, 240);
}

void screenFullRelease() {
    if (full.getBuffer()) drop(full);
}

LovyanGFX&   screenGfx() { return full.getBuffer() ? (LovyanGFX&)full : (LovyanGFX&)tft; }
LGFX_Sprite* screenSprite() { return full.getBuffer() ? &full : nullptr; }
uint32_t     screenColor(uint8_t p) { return full.getBuffer() ? (uint32_t)(p & 15) : rgbOf(p & 15); }

void screenPushIfSprite() {
    if (boardHasPanel() && full.getBuffer()) full.pushSprite(0, 0);
}

bool screenBandsInit() {
    if (band.getBuffer()) return true;
    return boardHasPanel() && make(band, 40);
}

LGFX_Sprite* screenBand() { return band.getBuffer() ? &band : nullptr; }
void         screenFailNext() { failNext = true; }

// rows of a 4-bit copy as RGB332 hex (the PC tool reads RGB332, as before the copy went 4-bit)
static void shotRows(const uint8_t* p, int rows, void (*eachRow)()) {
    static const char* d = "0123456789ABCDEF";
    uint8_t            rgb332[16];
    for (int i = 0; i < 16; i++)
        rgb332[i] = (kPalette[i][0] * 7 / 255) << 5 | (kPalette[i][1] * 7 / 255) << 2 | (kPalette[i][2] * 3 / 255);
    char line[320 * 2 + 1];
    for (int y = 0; y < rows; y++, p += 160) {
        for (int x = 0; x < 320; x++) {
            uint8_t v = rgb332[(x & 1) ? (p[x / 2] & 15) : (p[x / 2] >> 4)];  // left pixel in the high nibble
            line[x * 2]     = d[v >> 4];
            line[x * 2 + 1] = d[v & 15];
        }
        line[320 * 2] = 0;
        Serial.println(line);
        if (eachRow) eachRow();
    }
}

void screenShot(void (*eachRow)(), const axis::bbs::AnsiTerm* wfc) {
    bool bands = wfc && band.getBuffer();
    if (!bands && !full.getBuffer()) {
        Serial.println("@SHOT NONE");  // drawn straight to the panel (or no panel): nothing to read back
        return;
    }
    Serial.print("@SHOT 320 240\n");  // (LF only, as the header always was)
    if (bands) {
        tft.waitDMA();
        for (int top = 0; top < 240; top += 40) {  // the WFC: its bands one after another
            AnsiScreen::renderCellsBand(band, *wfc, top / 10, 4);
            shotRows((const uint8_t*)band.getBuffer(), 40, eachRow);
        }
    } else {
        shotRows((const uint8_t*)full.getBuffer(), 240, eachRow);
    }
    Serial.println("@SHOT END");
}
