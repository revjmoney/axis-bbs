// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include "doctest.h"
#include "bbs/config.h"

using namespace axis::bbs;

TEST_CASE("config round trip keeps every field") {
    BbsConfig c;
    c.sysopHandle = "Rev";
    c.sysopSalt = "00112233445566778899aabbccddeeff";
    c.sysopHash = "abcd";
    c.sysopRealName = "Rev J Money";
    c.bbsName = "Axis Underground CNC";
    c.location = "Raleigh, NC";
    c.boardName = "axis";
    c.publicHost = "bbs.example.com";
    c.telnetPort = 2323;
    c.timezone = "PACIFIC";
    c.ftnAddress = "99:1/0";
    c.newUserSecurity = 20;
    c.dailyMinutes = 90;
    c.idleMinutes = 7;
    c.setupDone = true;
    BbsConfig d = parseConfig(formatConfig(c));
    CHECK(d.sysopHandle == "Rev");
    CHECK(d.sysopSalt == c.sysopSalt);
    CHECK(d.sysopHash == "abcd");
    CHECK(d.sysopRealName == "Rev J Money");
    CHECK(d.location == "Raleigh, NC");
    CHECK(d.publicHost == "bbs.example.com");
    CHECK(d.telnetPort == 2323);
    CHECK(d.timezone == "PACIFIC");
    CHECK(d.newUserSecurity == 20);
    CHECK(d.dailyMinutes == 90);
    CHECK(d.idleMinutes == 7);
    CHECK(d.setupDone);
}

TEST_CASE("config missing keys get defaults") {
    BbsConfig d = parseConfig("bbs_name=Other Board\n");
    CHECK(d.bbsName == "Other Board");
    CHECK(d.boardName == "axis");
    CHECK(d.telnetPort == 23);
    CHECK(d.timezone == "EASTERN");
    CHECK(d.ftnAddress == "99:1/0");
    CHECK(d.newUserSecurity == 10);
    CHECK(d.dailyMinutes == 60);
    CHECK(d.idleMinutes == 5);
}

TEST_CASE("config setupDone only after save") {
    CHECK_FALSE(parseConfig("bbs_name=X\n").setupDone);
    CHECK(parseConfig("setup_done=1\n").setupDone);
}

TEST_CASE("config unknown keys survive save") {
    BbsConfig d = parseConfig("future_option=on\nbbs_name=X\n");
    std::string out = formatConfig(d);
    CHECK(out.find("future_option=on") != std::string::npos);
}

TEST_CASE("config values with equals signs and spaces survive, CRLF accepted") {
    BbsConfig d = parseConfig("location= Somewhere = odd \r\nbbs_name=A B\r\n");
    CHECK(d.location == " Somewhere = odd ");
    CHECK(d.bbsName == "A B");
}

TEST_CASE("timezone rules") {
    CHECK(std::string(tzRule("EASTERN")) == "EST5EDT,M3.2.0,M11.1.0");
    CHECK(std::string(tzRule("nowhere")).empty());
    CHECK(tzNames().size() == 10);
}

TEST_CASE("config has LORD as the default door") {
    auto c = axis::bbs::parseConfig("");
    REQUIRE(c.doors.size() == 1);
    CHECK(c.doors[0].id == "lord");
    CHECK(c.doors[0].args == "1 /DREW");
}

TEST_CASE("config doors round trip") {
    auto c = axis::bbs::parseConfig("door1=bre|Barren Realms Elite|BRE.EXE|/N1|dorinfo\n");
    REQUIRE(c.doors.size() == 1);
    CHECK(c.doors[0].name == "Barren Realms Elite");
    CHECK(c.doors[0].drop == "dorinfo");
    auto d = axis::bbs::parseConfig(axis::bbs::formatConfig(c));
    REQUIRE(d.doors.size() == 1);
    CHECK(d.doors[0].exe == "BRE.EXE");
}
