// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include "bbs/loginseq.h"

#include "doctest.h"

using namespace axis::bbs;

TEST_CASE("login sequence: a file's steps in order, with clear, pause, level and data") {
    auto s = parseLoginSeq("; the board's own\nstep=DISPLAYFILE|1|1|0|BULLETIN\nstep=ONELINERS|0|0|0|\nstep=NEWUSERVAL|0|0|255|\n");
    REQUIRE(s.size() == 3);
    CHECK(s[0].cmd == "DISPLAYFILE");
    CHECK(s[0].clear);
    CHECK(s[0].pause);
    CHECK(s[0].data == "BULLETIN");
    CHECK(s[2].level == 255);
}

TEST_CASE("login sequence: no file means ViSiON/3's shipped template") {
    auto s = parseLoginSeq("");
    REQUIRE(s.size() == 11);
    CHECK(s[0].cmd == "SYSOPNOTICES");
    CHECK(s[1].cmd == "NMAILSCAN");
    CHECK(s[1].pause);
    CHECK(s[2].cmd == "DISPLAYFILE");
    CHECK(s[2].data == "SAYLOR");
    CHECK(s[2].pause);
    CHECK(s[3].cmd == "FASTLOGIN");
    CHECK(s[4].cmd == "PRINTNEWS");
    CHECK(s[4].pause);
    CHECK(s[9].cmd == "NEWUSERVAL");
    CHECK(s[9].level == 255);
}
