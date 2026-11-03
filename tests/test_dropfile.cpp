// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include <sstream>
#include <string>
#include <vector>

#include "doctest.h"
#include "door/dropfile.h"

using namespace axis::door;

static std::vector<std::string> lines(const std::string& s) {
    std::vector<std::string> v;
    size_t                   at = 0;
    while (at < s.size()) {
        size_t e = s.find("\r\n", at);
        if (e == std::string::npos) break;
        v.push_back(s.substr(at, e - at));
        at = e + 2;
    }
    return v;
}

TEST_CASE("door.sys has 52 CRLF lines") {
    CallerInfo c;
    c.alias = "Revman";
    std::string d = makeDoorSys(c);
    CHECK(lines(d).size() == 52);
    CHECK(d.substr(d.size() - 2) == "\r\n");
}

TEST_CASE("door.sys line 1 is COM1: for a caller and COM0: for local") {
    CallerInfo c;
    CHECK(lines(makeDoorSys(c))[0] == "COM1:");
    c.comPort = 0;
    CHECK(lines(makeDoorSys(c))[0] == "COM0:");
}

TEST_CASE("door.sys carries the sysop name, alias, node, security and time exactly") {
    CallerInfo c;
    c.alias = "Revman";
    c.realName = "Rev J Money";
    c.node = 2;
    c.security = 50;
    c.minutesLeft = 45;
    auto l = lines(makeDoorSys(c));
    CHECK(l[3] == "2");
    CHECK(l[9] == "Rev J Money");
    CHECK(l[14] == "50");
    CHECK(l[17] == std::to_string(45 * 60));
    CHECK(l[18] == "45");
    CHECK(l[19] == "GR");
    CHECK(l[34] == "Rev J Money");  // sysop (default)
    CHECK(l[35] == "Revman");
    CHECK(l[39] == "Y");            // record locking
}

TEST_CASE("dorinfo has 13 lines with the BBS and sysop names and FOSSIL flag") {
    CallerInfo c;
    c.alias = "Revman";
    auto l = lines(makeDorinfo(c));
    REQUIRE(l.size() == 13);
    CHECK(l[0] == "Axis Underground CNC");
    CHECK(l[1] == "Rev");
    CHECK(l[3] == "COM1");
    CHECK(l[6] == "Revman");
    CHECK(l[9] == "1");
    CHECK(l[12] == "-1");
}
