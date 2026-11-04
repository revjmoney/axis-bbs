// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include <regex>
#include <string>

#include "doctest.h"
#include "bbs/theme.h"

using namespace axis::bbs;

namespace {
std::string visible(const std::string& s) { return std::regex_replace(s, std::regex("\x1b\\[[0-9;]*m"), ""); }
}  // namespace

TEST_CASE("fade text keeps the letters and fades each word from white edges to a grey middle") {
    std::string f = theme::fade("UNDERGROUND");
    CHECK(visible(f) == "UNDERGROUND");
    // one pass across the word: white, light grey, grey, light grey, white (no repeating pattern)
    std::string order;
    for (size_t at = 0; (at = f.find("\x1b[", at)) != std::string::npos; at++) {
        std::string code = f.substr(at, f.find('m', at) - at + 1);
        order += code == theme::WHITE ? "W" : code == theme::LGREY ? "L" : code == theme::DGREY ? "G" : "";
    }
    CHECK(order.substr(0, 5) == "WLGLW");
    CHECK(theme::fade("ok AXIS").find("ok") != std::string::npos);  // short words stay white and readable
}

TEST_CASE("a drippy window has straight sides and the rows inside") {
    std::string w = theme::window("DOWNLOAD", {{"file", "AXISFLSH.ZIP"}, {"size", "779K"}}, 60);
    CHECK(visible(w).find("AXISFLSH.ZIP") != std::string::npos);
    // every framed line is the same width on screen
    size_t pos = 0, width = 0;
    int    framed = 0;
    std::string v = visible(w);
    while (pos < v.size()) {
        size_t end = v.find("\r\n", pos);
        std::string line = v.substr(pos, end - pos);
        pos = end == std::string::npos ? v.size() : end + 2;
        if (line.find('\xdb') == 2 && line.rfind('\xdb') == line.size() - 1) {  // a "█ ... █" row
            if (!width) width = line.size();
            CHECK(line.size() == width);
            framed++;
        }
    }
    CHECK(framed >= 3);
}

TEST_CASE("theme colours are black, grey, orange and white only") {
    std::string w = theme::window("UPLOAD", {{"file", "X.ZIP"}}, 50) + theme::fade("hello");
    std::smatch m;
    std::string rest = w;
    while (std::regex_search(rest, m, std::regex("\x1b\\[([0-9;]*)m"))) {
        std::string codes = m[1];
        for (const char* banned : {";31", ";32", ";34", ";35", ";36", ";41", ";42", ";44", ";45", ";46"})
            CHECK(("; " + codes).find(banned) == std::string::npos);
        rest = m.suffix();
    }
}

TEST_CASE("a line that fits stays put; a long one scrolls through, pausing at each end") {
    using axis::bbs::theme::marquee;
    CHECK(marquee("short", 10, 0) == "short");
    CHECK(marquee("short", 10, 99999) == "short");
    std::string longLine = "0123456789ABCDEFGHIJ";  // 20 into 10
    CHECK(marquee(longLine, 10, 0) == "0123456789");      // starts at the start
    CHECK(marquee(longLine, 10, 600) == "0123456789");    // and waits a moment
    bool sawEnd = false, sawMiddle = false;
    for (uint32_t t = 0; t < 20000; t += 100) {
        std::string w = marquee(longLine, 10, t);
        CHECK(w.size() == 10);
        sawEnd = sawEnd || w == "ABCDEFGHIJ";
        sawMiddle = sawMiddle || w == "56789ABCDE";
    }
    CHECK(sawEnd);
    CHECK(sawMiddle);
}
