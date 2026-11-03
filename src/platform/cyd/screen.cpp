// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include "platform/cyd/screen.h"

LGFX_Sprite scr(&tft);

bool screenInit() {
    scr.setColorDepth(8);
    return scr.createSprite(320, 240) != nullptr;
}

static bool lent = false;  // a door is using the copy's memory as guest pages

// Lending instead of freeing keeps the 76.8 KB block whole: after a door the heap may have the
// bytes but no single piece that big.
uint8_t* screenLend(size_t& bytes) {
    bytes = scr.getBuffer() ? (size_t)scr.width() * scr.height() : 0;
    lent = bytes != 0;
    return (uint8_t*)scr.getBuffer();
}

void screenReturn() { lent = false; }

bool screenReady() { return scr.getBuffer() != nullptr && !lent; }

void screenPush() {
    if (screenReady()) scr.pushSprite(0, 0);
}

void screenShot() {
    static const char* d = "0123456789ABCDEF";
    const uint8_t*     p = screenReady() ? (const uint8_t*)scr.getBuffer() : nullptr;
    if (!p) {
        Serial.println("@SHOT NONE");  // no copy while a door runs
        return;
    }
    Serial.printf("@SHOT %d %d\n", scr.width(), scr.height());
    char line[320 * 2 + 1];
    for (int y = 0; y < scr.height(); y++) {
        for (int x = 0; x < scr.width(); x++) {
            uint8_t v       = p[y * scr.width() + x];
            line[x * 2]     = d[v >> 4];
            line[x * 2 + 1] = d[v & 15];
        }
        line[scr.width() * 2] = 0;
        Serial.println(line);
    }
    Serial.println("@SHOT END");
}
