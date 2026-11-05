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

TEST_CASE("fade text keeps the letters and fades each word from dark edges to a bright middle") {
    std::string f = theme::fade("UNDERGROUND");
    CHECK(visible(f) == "UNDERGROUND");
    // one pass across the word: grey, light grey, white, light grey, grey (no repeating pattern)
    std::string order;
    for (size_t at = 0; (at = f.find("\x1b[", at)) != std::string::npos; at++) {
        std::string code = f.substr(at, f.find('m', at) - at + 1);
        order += code == theme::WHITE ? "W" : code == theme::LGREY ? "L" : code == theme::DGREY ? "G" : "";
    }
    CHECK(order.substr(0, 5) == "GLWLG");
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

TEST_CASE("a drip banner is big block letters with drips hanging off, the same every time, inside 80 columns") {
    using axis::bbs::theme::drip;
    std::string b = drip("MAIN MENU");
    CHECK(b == drip("MAIN MENU"));
    CHECK(b.find('\xdb') != std::string::npos);  // full blocks
    int rows = 0;
    for (size_t p = 0; (p = b.find("\r\n", p)) != std::string::npos; p += 2) rows++;
    CHECK(rows >= 3);  // five letter rows and the drips, two to a text row (half blocks)
    std::string plainRows = std::regex_replace(b, std::regex("\x1b\[[0-9;]*m"), "");
    size_t start = 0;
    for (size_t p; (p = plainRows.find("\r\n", start)) != std::string::npos; start = p + 2) CHECK(p - start < 80);
    CHECK(drip("MESSAGE AREAS") != drip("FILE AREAS"));
}

TEST_CASE("the Oblivion look swaps the palette for greys and cyans, and back") {
    theme::use(theme::Look::Oblivion);
    CHECK(std::string(theme::ORANGE) == "\x1b[0;36m");  // the accent: cyan
    CHECK(std::string(theme::KEY) == "\x1b[1;36m");     // hotkeys: bright cyan
    CHECK(theme::look() == theme::Look::Oblivion);
    std::string w = theme::window("FILE INFO", {{"file", "PACK.ZIP"}});
    CHECK(w.find("\xc9") != std::string::npos);  // a double-line box, no drips
    CHECK(w.find("\xdf") == std::string::npos);
    CHECK(visible(w).find("PACK.ZIP") != std::string::npos);
    std::string d = visible(theme::drip("MENU"));
    CHECK(d.find("M E N U") != std::string::npos);  // an Oblivion title bar, spaced letters
    theme::use(theme::Look::Axis);
    CHECK(std::string(theme::ORANGE) == "\x1b[0;33m");
    CHECK(theme::window("X", {}).find("\xc9") == std::string::npos);
}

TEST_CASE("pipe codes become ANSI colours: |00 to |15 the text, |16 to |23 the background") {
    CHECK(theme::pipes("|11Hi|08x") == "\x1b[0;1;36mHi\x1b[0;1;30mx");
    CHECK(theme::pipes("|03a|07b|15c") == "\x1b[0;36ma\x1b[0;37mb\x1b[0;1;37mc");
    CHECK(theme::pipes("|17blue") == "\x1b[44mblue");
    CHECK(theme::pipes("100|% and |x stay") == "100|% and |x stay");
}

TEST_CASE("a theme name from the settings picks the look; anything unknown is the Axis look") {
    CHECK(theme::lookFrom("oblivion") == theme::Look::Oblivion);
    CHECK(theme::lookFrom("OBLIVION") == theme::Look::Oblivion);
    CHECK(theme::lookFrom("axis") == theme::Look::Axis);
    CHECK(theme::lookFrom("") == theme::Look::Axis);
    CHECK(std::string(theme::lookName(theme::Look::Oblivion)) == "oblivion");
}

TEST_CASE("the iniquity look fades in greens and is named in the settings") {
    theme::use(theme::lookFrom("iniquity"));
    CHECK(theme::look() == theme::Look::Iniquity);
    CHECK(std::string(theme::lookName(theme::Look::Iniquity)) == "iniquity");
    CHECK(theme::fade("UNDERGROUND").find("[0;32m") != std::string::npos);  // dark green at the ends
    theme::use(theme::Look::Axis);
}

TEST_CASE("a section title uses the theme's header screen when the card has one") {
    theme::setArt([](const std::string& name) { return name == "BBSLIST.ANS" ? std::string("<bbs list art>") : std::string(); });
    CHECK(theme::drip("BBS LIST").find("<bbs list art>") != std::string::npos);
    CHECK(theme::drip("DOORS").find("<bbs list art>") == std::string::npos);  // none for doors: the stock title
    theme::setArt(nullptr);
}
