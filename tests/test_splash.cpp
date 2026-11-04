// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include <regex>
#include <string>

#include "doctest.h"
#include "bbs/splash.h"

using namespace axis::bbs;

namespace {
std::string run(Splash& s, uint32_t from, uint32_t to, uint32_t step = 20) {
    std::string out;
    for (uint32_t t = from; t <= to; t += step) out += s.step(t);
    return out;
}
std::string textOnly(const std::string& s) { return std::regex_replace(s, std::regex("\x1b\\[[0-9;]*[A-Za-z]"), ""); }
}  // namespace

TEST_CASE("the splash clears the screen and types the art in like a 9600 baud modem") {
    Splash      s(0);
    std::string first = s.step(0);
    CHECK(first.rfind("\x1b[0m\x1b[2J", 0) == 0);
    std::string early = first + run(s, 20, 300);
    std::string all = early + run(s, 320, Splash::kTypeMs);
    CHECK(textOnly(early).size() < textOnly(all).size() / 3);  // not all at once
    CHECK(all.find("\x1b[24;1H") != std::string::npos);        // every row got there
}

TEST_CASE("then the parts move: only the cells that change are sent, by position") {
    Splash s(0);
    run(s, 0, Splash::kTypeMs);
    std::string fx = run(s, Splash::kTypeMs + 20, Splash::kTypeMs + 2000);
    CHECK_FALSE(fx.empty());
    CHECK(fx.find("\x1b[2J") == std::string::npos);  // no full redraws
    CHECK(std::regex_search(fx, std::regex("\x1b\\[[0-9]+;[0-9]+H")));
    CHECK(fx.size() < 40000);  // light enough for a slow line
}

TEST_CASE("the marquee scrolls the RotatoCAM message across the bottom row") {
    Splash      s(0);
    std::string out = run(s, 0, Splash::kTypeMs + Splash::kFxMs);
    CHECK(out.find("5-axis") != std::string::npos);
    CHECK(out.find("\x1b[24;1H") != std::string::npos);
}

TEST_CASE("the splash ends by itself") {
    Splash s(0);
    CHECK_FALSE(s.done(Splash::kTypeMs));
    CHECK(s.done(Splash::kTypeMs + Splash::kFxMs + 1));
}
