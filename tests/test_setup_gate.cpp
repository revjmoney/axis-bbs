// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include "doctest.h"
#include "bbs/config.h"
#include "bbs/node.h"
#include "bbs/setup_gate.h"

using namespace axis::bbs;

static void type(SetupGate& g, const std::string& s) { g.input((const uint8_t*)s.data(), s.size()); }

TEST_CASE("pin gate explains setup and asks for the pin") {
    SetupGate g("123456");
    g.begin();
    std::string out = g.takeOutput();
    CHECK(out.find("being set up") != std::string::npos);
    CHECK(out.find("PIN") != std::string::npos);
}

TEST_CASE("pin gate passes on the right pin") {
    SetupGate g("123456");
    g.begin();
    type(g, "123456\r");
    CHECK(g.passed());
    CHECK_FALSE(g.rejected());
}

TEST_CASE("pin gate echoes stars, not digits") {
    SetupGate g("123456");
    g.begin();
    g.takeOutput();
    type(g, "12");
    CHECK(g.takeOutput() == "**");
}

TEST_CASE("pin gate rejects after 3 wrong tries") {
    SetupGate g("123456");
    g.begin();
    type(g, "111111\r");
    CHECK_FALSE(g.rejected());
    type(g, "222222\r333333\r");
    CHECK(g.rejected());
    CHECK_FALSE(g.passed());
}

static SysInfo info() { return SysInfo{}; }

TEST_CASE("node welcome shows the configured BBS name") {
    BbsConfig c;
    c.bbsName = "Shop Floor BBS";
    Node n(1, info, c);
    n.begin(0);
    CHECK(n.takeOutput().find("Shop Floor BBS") != std::string::npos);  // in THE BOARD box, as configured
}

TEST_CASE("node idle minutes come from config") {
    BbsConfig c;
    c.idleMinutes = 2;
    Node n(1, info, c);
    n.begin(0);
    n.poll(119000);
    CHECK_FALSE(n.wantsHangup());
    n.poll(120001);
    CHECK(n.wantsHangup());
}
