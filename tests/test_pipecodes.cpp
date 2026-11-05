// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include "bbs/pipecodes.h"

#include <algorithm>

#include "doctest.h"

using namespace axis::bbs;

TEST_CASE("pipe colours, |CL, |XY and placeholders") {
    AcsUser u{10, ""};
    auto    v = [](const std::string& k) { return k == "UH" ? std::string("Rev") : std::string(); };
    CHECK(expandArt("|15hi", u, v) == "\x1b[0;1;37mhi");
    CHECK(expandArt("|B1x", u, v) == "\x1b[44mx");
    CHECK(expandArt("|CL", u, v) == "\x1b[2J\x1b[H");
    CHECK(expandArt("|XY05,03", u, v) == "\x1b[3;5H");
    CHECK(expandArt("hi |UH!", u, v) == "hi Rev!");
    CHECK(expandArt("@UH####@", u, v) == "Rev     ");  // width from the field, left aligned
}

TEST_CASE("conditional regions blank to the same width when access is missing") {
    auto v = [](const std::string&) { return std::string(); };
    CHECK(expandArt("[{{S255}}[*] Sysop{{/}}]", AcsUser{255, ""}, v) == "[[*] Sysop]");
    CHECK(expandArt("[{{S255}}[*] Sysop{{/}}]", AcsUser{10, ""}, v) == "[         ]");
}

TEST_CASE("a 7 KB screen streams in pieces of at most 512 bytes") {
    std::string big(7000, 'x'), got;
    size_t      biggest = 0;
    expandArt(big, AcsUser{10, ""}, [](const std::string&) { return std::string(); },
              [&](const char* d, size_t n) { got.append(d, n), biggest = std::max(biggest, n); });
    CHECK(got == big);
    CHECK(biggest <= 512);
}
