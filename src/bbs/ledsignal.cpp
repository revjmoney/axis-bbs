// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#pragma GCC optimize("Os")  // menus and screens: size over speed (the door CPU and transfers stay -O2)
#include "bbs/ledsignal.h"

namespace axis::bbs {

namespace {
constexpr uint32_t kTurnMs = 2000;

Rgb rgb(uint8_t r, uint8_t g, uint8_t b) {
    Rgb c;
    c.r = r;
    c.g = g;
    c.b = b;
    return c;
}
bool   on(uint32_t t, uint32_t period, uint32_t lit) { return t % period < lit; }
bool   twice(uint32_t t, uint32_t period) { return t % period < 120 || (t % period >= 240 && t % period < 360); }
uint8_t breathe(uint32_t t, uint32_t period) {  // up and down, 0..255
    uint32_t p = t % period, half = period / 2;
    return (uint8_t)((p < half ? p : period - p) * 255 / half);
}
}  // namespace

Rgb ledColor(const LedInputs& in, uint32_t t) {
    if (in.paged) return on(t, 250, 90) ? rgb(255, 0, 255) : Rgb();

    enum Signal { Lost, Door, Transfer, Pending, Chat, Caller };
    Signal active[6];
    int    n = 0;
    if (in.mainLost) active[n++] = Lost;
    if (in.doorRunning) active[n++] = Door;
    if (in.transfer) active[n++] = Transfer;
    if (in.pendingUsers > 0) active[n++] = Pending;
    if (in.chat) active[n++] = Chat;
    if (in.callers > 0) active[n++] = Caller;
    if (n == 0) return on(t, 5000, 60) ? rgb(0, 0, 40) : Rgb();  // idle: alive

    uint32_t in_turn = t % kTurnMs;
    switch (active[(t / kTurnMs) % (uint32_t)n]) {
        case Lost: return twice(in_turn, 1000) ? rgb(255, 160, 0) : Rgb();
        case Door: return on(in_turn, 1000, 500) ? rgb(255, 0, 0) : Rgb();
        case Transfer: {
            uint8_t v = breathe(in_turn, kTurnMs);
            return v ? rgb(0, v, v) : Rgb();
        }
        case Pending: return on(in_turn, kTurnMs, 1200) ? rgb(0, 255, 0) : Rgb();
        case Chat: return twice(in_turn, 1000) ? rgb(0, 0, 255) : Rgb();
        case Caller: return on(in_turn, 1000, 100) ? rgb(255, 255, 255) : Rgb();
    }
    return Rgb();
}

void ledRing(const LedInputs& in, uint32_t t, uint8_t out[8]) {
    for (int i = 0; i < 8; i++) out[i] = 0;
    if (in.paged) {  // the sysop is wanted: every red, 4 flashes a second
        if (t % 250 < 125)
            for (int i = 0; i < 8; i += 2) out[i] = 255;
        return;
    }
    if (in.mainLost) {  // reds breathing slowly
        uint32_t ph = t % 2000;
        uint8_t  v = (uint8_t)(ph < 1000 ? ph * 255 / 1000 : (2000 - ph) * 255 / 1000);
        for (int i = 0; i < 8; i += 2) out[i] = v;
        return;
    }
    uint32_t step = in.callers > 0 ? 90 : 300;  // ms per place round the ring
    bool     reds = in.doorRunning, greens = in.transfer && !in.doorRunning;
    int      places = reds || greens ? 4 : 8;   // a door or a transfer: just that colour's four
    auto     at = [&](int k) { return reds ? k * 2 : greens ? k * 2 + 1 : k; };
    int      head = (int)(t / step % (uint32_t)places);
    static const uint8_t trail[] = {255, 90, 30, 8};
    for (int comet = 0; comet < (in.chat ? 2 : 1); comet++) {
        int h = (head + comet * places / 2) % places;
        for (int k = 0; k < 4 && k < places; k++) {
            int     i = at((h - k + places) % places);
            uint8_t v = trail[k];
            if (v > out[i]) out[i] = v;
        }
    }
    if (in.pendingUsers > 0 && t % 2000 < 150 && !reds)  // new users waiting: a green flash every 2 s
        for (int i = 1; i < 8; i += 2) out[i] = 255;
}

}  // namespace axis::bbs
