// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include "doctest.h"
#include "bbs/users.h"

using namespace axis::bbs;

TEST_CASE("users round trip with their application answers") {
    UserStore s;
    User      u;
    u.handle = "Chip Breaker";
    u.salt = "ab";
    u.hash = "cd";
    u.location = "Conway, SC";
    u.answers = {{"Own a CNC?", "yes"}, {"Machine", "Shapeoko 4 with a rotary"}};
    s.add(u);
    UserStore t;
    t.parse(s.format());
    REQUIRE(t.all().size() == 1);
    CHECK(t.all()[0].id == 1);
    CHECK(t.all()[0].handle == "Chip Breaker");
    CHECK(t.all()[0].status == User::Status::Pending);
    REQUIRE(t.all()[0].answers.size() == 2);
    CHECK(t.all()[0].answers[1].second == "Shapeoko 4 with a rotary");
}

TEST_CASE("handles are found in any case and ids keep counting up") {
    UserStore s;
    User      a, b;
    a.handle = "Revman";
    b.handle = "Spindle";
    s.add(a);
    s.add(b);
    CHECK(s.find("REVMAN") != nullptr);
    CHECK(s.find("nobody") == nullptr);
    CHECK(s.byId(2)->handle == "Spindle");
    CHECK(s.pending() == 2);
}

TEST_CASE("answers can't break the file apart") {
    UserStore s;
    User      u;
    u.handle = "x";
    u.answers = {{"Why=let you in", "line one\r\nline two"}};
    s.add(u);
    UserStore t;
    t.parse(s.format());
    REQUIRE(t.all().size() == 1);
    REQUIRE(t.all()[0].answers.size() == 1);
    CHECK(t.all()[0].answers[0].second == "line one  line two");
}
