// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include "platform/cyd/screen.h"

LGFX_Sprite scr(&tft);

bool screenInit() {
    scr.setColorDepth(8);
    return scr.createSprite(320, 240) != nullptr;
}

void screenPush() { scr.pushSprite(0, 0); }

void screenShot() {
    static const char* d = "0123456789ABCDEF";
    const uint8_t*     p = (const uint8_t*)scr.getBuffer();
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
