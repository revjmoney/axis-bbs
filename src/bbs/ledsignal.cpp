// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#pragma GCC optimize("Os")
#include "bbs/ledsignal.h"

namespace axis::bbs {

namespace {
constexpr int      kMaxNodes = 8;
constexpr uint32_t kRise = 480, kFall = 220, kGap = 300, kRest = 1400;
constexpr uint32_t kPulse = kRise + kFall + kGap;

// One colour per caller pulse, in order. Red is reserved for a door.
constexpr LedColour kPulseCol[kMaxNodes] = {
    {0, 80, 255},      // blue
    {0, 255, 220},     // cyan
    {0, 255, 40},      // green
    {255, 180, 0},     // amber
    {255, 70, 0},      // orange
    {255, 0, 180},     // magenta
    {220, 220, 255},   // white
    {160, 0, 255},     // violet
};

int clampNodes(int n) {
    if (n < 0) return 0;
    if (n > kMaxNodes) return kMaxNodes;
    return n;
}

// Full saturation. h is 0..255 around the wheel.
void hueRgb(uint8_t h, uint8_t& r, uint8_t& g, uint8_t& b) {
    uint8_t region = h / 43;
    uint8_t rem = (uint8_t)((h - region * 43) * 6);
    uint8_t q = (uint8_t)(255 - rem);
    switch (region) {
    case 0: r = 255; g = rem; b = 0; break;
    case 1: r = q; g = 255; b = 0; break;
    case 2: r = 0; g = 255; b = rem; break;
    case 3: r = 0; g = q; b = 255; break;
    case 4: r = rem; g = 0; b = 255; break;
    default: r = 255; g = 0; b = q; break;
    }
}
}  // namespace

uint8_t ledDuty(const LedInputs& in, uint32_t t) {
    if (in.door) return (t % 200) < 70 ? kLedFadePeak : 0;

    int n = clampNodes(in.nodes);
    if (n == 0) {
        constexpr uint32_t period = 4000, half = period / 2;
        uint32_t           p = t % period;
        uint32_t           up = p < half ? p : period - p;
        return (uint8_t)(up * kLedBreathePeak / half);
    }
    uint32_t cycle = (uint32_t)n * kPulse + kRest;
    uint32_t p = t % cycle;
    if (p >= (uint32_t)n * kPulse) return 0;
    uint32_t u = p % kPulse;
    if (u < kRise) return (uint8_t)(u * kLedFadePeak / kRise);
    if (u < kRise + kFall) return (uint8_t)((kRise + kFall - u) * kLedFadePeak / kFall);
    return 0;
}

uint8_t ledCydDuty(uint8_t bare) {
    uint32_t v = (uint32_t)bare * kCydFadePeak / kLedFadePeak;
    if (v > 255) v = 255;
    return (uint8_t)v;
}

LedColour ledColour(const LedInputs& in, uint32_t t) {
    if (in.door) return {255, 0, 0};
    int n = clampNodes(in.nodes);
    if (n == 0) {
        LedColour c;
        hueRgb((uint8_t)((t / 32) & 255), c.r, c.g, c.b);  // one lap in about 8 seconds
        return c;
    }
    uint32_t cycle = (uint32_t)n * kPulse + kRest;
    uint32_t p = t % cycle;
    if (p >= (uint32_t)n * kPulse) return {0, 0, 0};
    return kPulseCol[p / kPulse];
}

}  // namespace axis::bbs
