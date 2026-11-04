// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include <string>

#include "doctest.h"
#include "bbs/announce.h"

using namespace axis::bbs;

TEST_CASE("board events worth telling Discord become short lines; the rest stay on the board") {
    CHECK(announceLine("sysop: validated Kid Chips").find("**Kid Chips** joined the underground") != std::string::npos);
    CHECK(announceLine("Spindle uploaded TOOLPATH.ZIP (12 KB in 3 s)").find("**Spindle** uploaded TOOLPATH.ZIP") != std::string::npos);
    CHECK(announceLine("oneliner: Kid Chips: chips fly").find("chips fly") != std::string::npos);
    CHECK(announceLine("Kid Chips opened LORD").find("playing LORD") != std::string::npos);
    CHECK(announceLine("PAGE from Kid on node 3: help").find("paged") != std::string::npos);
    CHECK(announceLine("new user application waiting: tap USERS").find("new user") != std::string::npos);
    CHECK(announceLine("call from 192.168.1.5 (heap 50 KB)").empty());  // calls, addresses, heap: never
    CHECK(announceLine("cluster: node 3 joined").empty());
    CHECK(announceLine("Kid downloaded X.ZIP").empty());
}

TEST_CASE("nobody can ping the whole Discord through the board") {
    std::string l = announceLine("oneliner: Troll: @everyone @here look");
    CHECK(l.find("@everyone") == std::string::npos);
    CHECK(l.find("@here") == std::string::npos);
}
