// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// Rev's RotatoCAM ANSI splash (his rotatocam.html, made into rotatocam_splash.h), played to a caller
// before the login screen: typed in like a 9600 baud modem, then a few seconds of its moving parts
// (the light sweeping the logo, the tool path, the pivot, the flutes spinning, chips flying, the
// tag sweep) and the colour-cycling marquee, sent as small cursor-positioned updates.
#pragma once
#include <cstdint>
#include <string>

namespace axis::bbs {

class Splash {
public:
    static constexpr uint32_t kTypeMs = 2000, kFxMs = 5000, kFrameMs = 120;

    explicit Splash(uint32_t startMs) : start_(startMs) {}
    std::string step(uint32_t nowMs);  // what to send the caller now ("" = nothing yet)
    bool        done(uint32_t nowMs) const { return nowMs - start_ > kTypeMs + kFxMs; }

private:
    uint32_t start_, lastFrame_ = 0;
    int      rowsSent_ = -1;     // rows typed in so far (-1: not even the clear screen)
    uint8_t  shown_[24][80][2] = {};  // what the caller's screen has now: character, attribute
    bool     haveShown_ = false;
};

}  // namespace axis::bbs
