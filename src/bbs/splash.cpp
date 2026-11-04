// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include "bbs/splash.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "bbs/rotatocam_splash.h"

namespace axis::bbs {

namespace {
const int kAnsi[8] = {0, 4, 2, 6, 1, 5, 3, 7};  // CGA colour order to ANSI's

std::string sgr(uint8_t attr) {
    char b[24];
    std::snprintf(b, sizeof b, "\x1b[0;%s3%d;4%dm", (attr & 8) ? "1;" : "", kAnsi[attr & 7], kAnsi[(attr >> 4) & 7]);
    return b;
}
std::string at(int row, int col) {  // 0-based cell to an ANSI position
    char b[16];
    std::snprintf(b, sizeof b, "\x1b[%d;%dH", row + 1, col + 1);
    return b;
}
const uint8_t* cell(int y, int x) { return &kSplashGrid[(y * kSplashW + x) * 3]; }

// the page's idle effects, the same maths (t in seconds since the art finished)
void effect(int x, int y, double t, uint8_t& ch, uint8_t& fg, uint8_t& bg) {
    const char* tag = kSplashTags[cell(y, x)[2]];
    if (!std::strcmp(tag, "rot") || !std::strcmp(tag, "cam")) {
        double band = std::fmod(t * 40, 130) - 25, d = std::fabs(x + (y - 3) * 1.2 - band);
        if (d < 1.6) fg = 15;
        else if (d < 3.5) fg |= 8;
    } else if (!std::strcmp(tag, "path")) {
        double ph = std::fmod(std::fmod(x - t * 30, 14) + 14, 14);
        fg = ph < 4 ? 13 : 5;
        if (x > 52) fg = 13;
    } else if (!std::strcmp(tag, "pivot")) {
        fg = std::sin(t * 5) > 0 ? 14 : 6;
    } else if (!std::strcmp(tag, "flute")) {
        ch = ((int)std::floor(t * 14 + y) % 2) ? 178 : 177;
    } else if (!std::strcmp(tag, "chip")) {
        const uint8_t chips[3] = {249, 250, 32};
        ch = chips[(int)std::floor(t * 9 + x * 7 + y * 3) % 3];
    } else if (!std::strcmp(tag, "tag")) {
        double d = std::fabs(x - (std::fmod(t * 25, 110) - 15));
        fg = d > 2 ? 15 : 11;
    } else if (!std::strcmp(tag, "marq")) {
        ch = 32;
    }
    (void)bg;
}
}  // namespace

std::string Splash::step(uint32_t now) {
    uint32_t    t = now - start_;
    std::string out;
    if (rowsSent_ < 0) {
        out = "\x1b[0m\x1b[2J\x1b[H";
        rowsSent_ = 0;
        std::memset(shown_, 0, sizeof shown_);
        haveShown_ = true;
    }
    if (rowsSent_ < kSplashH) {  // typing the art in, a row at a time
        int due = t >= kTypeMs ? kSplashH : (int)(t * (uint32_t)kSplashH / kTypeMs) + 1;
        for (; rowsSent_ < due && rowsSent_ < kSplashH; rowsSent_++) {
            int y = rowsSent_, last = kSplashW;
            while (last > 0 && cell(y, last - 1)[0] == 32 && (cell(y, last - 1)[1] >> 4) == 0) last--;
            out += at(y, 0);
            int attr = -1;
            for (int x = 0; x < last; x++) {
                const uint8_t* c = cell(y, x);
                if (c[1] != attr) out += sgr((uint8_t)(attr = c[1]));
                out += (char)c[0];
                shown_[y][x][0] = c[0];
                shown_[y][x][1] = c[1];
            }
            for (int x = last; x < kSplashW; x++) shown_[y][x][0] = 32, shown_[y][x][1] = 7;
        }
        return out;
    }
    if (done(now) || now - lastFrame_ < kFrameMs) return out;
    lastFrame_ = now;
    double fx = (t - kTypeMs) / 1000.0;
    int    attr = -1, curY = -1, curX = -1;
    auto   put = [&](int y, int x, uint8_t ch, uint8_t a) {
        if (shown_[y][x][0] == ch && shown_[y][x][1] == a) return;
        if (y != curY || x != curX) out += at(y, x);
        if (a != attr) out += sgr((uint8_t)(attr = a));
        out += (char)ch;
        shown_[y][x][0] = ch;
        shown_[y][x][1] = a;
        curY = y;
        curX = x + 1;
    };
    for (int y = 0; y < kSplashH - 1; y++)  // the moving parts (the marquee row is below)
        for (int x = 0; x < kSplashW; x++) {
            const uint8_t* c = cell(y, x);
            if (!c[2]) continue;
            uint8_t ch = c[0], fg = c[1] & 15, bg = c[1] >> 4;
            effect(x, y, fx, ch, fg, bg);
            put(y, x, ch, (uint8_t)(fg | bg << 4));
        }
    // the marquee: the message scrolls a character at a time, its letters cycling colour
    size_t len = std::strlen(kSplashMarquee), cyc = sizeof kSplashCycle;
    int    start = (int)std::floor(fx * 6), sh = (int)std::floor(fx * 8);
    for (int x = 0; x < kSplashW - 1; x++) {  // not the last column: the terminal would wrap
        size_t j = (size_t)(start + x) % len;
        char   ch = kSplashMarquee[j];
        put(kSplashH - 1, x, (uint8_t)ch, ch == ':' ? 8 : kSplashCycle[(j + (size_t)sh) % cyc]);
    }
    return out;
}

}  // namespace axis::bbs
