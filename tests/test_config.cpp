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
    REQUIRE(c.doors.size() == 3);
    CHECK(c.doors[0].id == "lord");
    CHECK(c.doors[1].id == "blackjack");
    CHECK(c.doors[2].exe == "*ANSIVIEW");  // the built-in ANSI Pack Viewer
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

TEST_CASE("config has the default file areas, music and CAD/CAM included") {
    auto c = parseConfig("");
    auto has = [&](const std::string& name) {
        for (auto& a : c.areas)
            if (a.name == name) return true;
        return false;
    };
    CHECK(c.areas.size() == 19);
    CHECK(has("ANSI Packs"));
    CHECK(c.areas[0].name == "Axis Micro BBS Firmware");  // our own area comes first
    CHECK(has("RotatoCAM: Pro"));
    CHECK(has("Abandonware"));
    CHECK(has("CAD/CAM Software"));
    CHECK(has("Post Processors"));
    CHECK(has("BBS Software"));
    CHECK(has("Music: Rev J Money"));
    CHECK(has("Music: Other"));
}

TEST_CASE("config file areas round trip and replace the defaults") {
    auto c = parseConfig("area1=gcode|G-code and Toolpaths\narea2=music-rev|Music: Rev J Money\n");
    REQUIRE(c.areas.size() == 2);
    CHECK(c.areas[1].id == "music-rev");
    auto d = parseConfig(formatConfig(c));
    REQUIRE(d.areas.size() == 2);
    CHECK(d.areas[0].name == "G-code and Toolpaths");
}

TEST_CASE("file areas sort by number and a repeated number replaces the earlier one") {
    auto c = parseConfig("area2=gcode|G-code\narea1=text|Text\narea2=models|3D Models\narea0=axis|Axis Micro BBS Firmware\n");
    REQUIRE(c.areas.size() == 3);
    CHECK(c.areas[0].id == "axis");
    CHECK(c.areas[1].id == "text");
    CHECK(c.areas[2].id == "models");
}
