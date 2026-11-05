// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include "bbs/acs.h"

#include "doctest.h"
#include "bbs/users.h"

using namespace axis::bbs;

TEST_CASE("acs: empty and * allow, levels and flags, not, and/or, junk denies") {
    AcsUser u{25, "FM"};
    CHECK(acsAllows("", u));
    CHECK(acsAllows("*", u));
    CHECK(acsAllows("s10", u));
    CHECK_FALSE(acsAllows("s255", u));
    CHECK(acsAllows("fF", u));
    CHECK_FALSE(acsAllows("fD", u));
    CHECK(acsAllows("!fD", u));
    CHECK(acsAllows("s10&fM", u));
    CHECK_FALSE(acsAllows("s10&fD", u));
    CHECK(acsAllows("s255|fF", u));
    CHECK_FALSE(acsAllows("s1x?", u));  // not understood: fail closed
}

TEST_CASE("acs: a user's level and permissions become the level and flags; a sysop is level 255") {
    User u;
    u.security = 20;
    u.permFiles = true, u.permMessages = false, u.permDoors = true, u.freeDownloads = true;
    AcsUser a = acsFor(u, false);
    CHECK(a.level == 20);
    CHECK(a.flags.find('F') != std::string::npos);
    CHECK(a.flags.find('M') == std::string::npos);
    CHECK(a.flags.find('D') != std::string::npos);
    CHECK(a.flags.find('$') != std::string::npos);
    CHECK(acsFor(u, true).level == 255);
}
