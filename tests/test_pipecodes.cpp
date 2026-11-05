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

TEST_CASE("AXIS's own %XX codes work too (the existing theme art uses them)") {
    auto v = [](const std::string& k) { return k == "%BN" ? std::string("AXIS") : std::string(); };  // %-codes come with their %
    CHECK(expandArt("%BN node", AcsUser{10, ""}, v) == "AXIS node");
    CHECK(expandArt("100%", AcsUser{10, ""}, v) == "100%");  // a lone percent stays
}

TEST_CASE("ViSiON/3's longer placeholder names win over two-letter ones") {
    auto v = [](const std::string& k) { return k == "CAN" ? std::string("General") : k == "CA" ? std::string("gen") : k == "TIME" ? std::string("10:42") : std::string(); };
    CHECK(expandArt("[|CAN] [|CA] |TIME", AcsUser{10, ""}, v) == "[General] [gen] 10:42");
}

TEST_CASE("optional groups drop when every placeholder inside is empty, and keep plain text") {
    auto both = [](const std::string& k) { return k == "UH" ? std::string("Felonius") : k == "GL" ? std::string("ViSiON/3") : k == "UN" ? std::string("SysOp") : std::string(); };
    auto none = [](const std::string& k) { return k == "UH" ? std::string("Felonius") : std::string(); };
    const char* p = "|UH|{ from |GL|}|{ (|UN)|}";
    CHECK(expandArt(p, AcsUser{10, ""}, both) == "Felonius from ViSiON/3 (SysOp)");
    CHECK(expandArt(p, AcsUser{10, ""}, none) == "Felonius");
    CHECK(expandArt("a|{ plain |}b", AcsUser{10, ""}, none) == "a plain b");
    CHECK(expandArt("x|{P}y", AcsUser{10, ""}, none) == "x|{P}y");  // login markers pass through
}

TEST_CASE("control codes: clear to end of line, carriage return, reset, save and restore the cursor") {
    auto v = [](const std::string&) { return std::string(); };
    CHECK(expandArt("|DE", AcsUser{10, ""}, v) == "\x1b[K");
    CHECK(expandArt("|CR", AcsUser{10, ""}, v) == "\r\n");
    CHECK(expandArt("|23", AcsUser{10, ""}, v) == "\x1b[0m");
    CHECK(expandArt("|P|PP", AcsUser{10, ""}, v) == "\x1b[s\x1b[u");
}
