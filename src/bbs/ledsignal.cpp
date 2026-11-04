// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
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

}  // namespace axis::bbs
