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

TEST_CASE("one user travels as a record and lands by handle, keeping this board's own id") {
    UserStore main, follower;
    User      a;
    a.handle = "Spindle Kid";
    a.status = User::Status::Validated;
    main.add(User{});  // the main already has someone, so ids differ between boards
    main.all()[0].handle = "Rev";
    User& onMain = main.add(a);
    std::string rec = formatUser(onMain);
    User        got;
    REQUIRE(parseUser(rec, got));
    CHECK(got.handle == "Spindle Kid");
    User& placed = follower.upsert(got);
    CHECK(placed.id == 1);  // new here: this board's next id
    got.calls = 7;
    User& again = follower.upsert(got);
    CHECK(follower.all().size() == 1);
    CHECK(again.id == 1);
    CHECK(again.calls == 7);
    CHECK(again.status == User::Status::Validated);
}

TEST_CASE("the user sync sends only the users that changed since last time") {
    UserStore s;
    User      a, b;
    a.handle = "Rev";
    b.handle = "Chip Breaker";
    s.add(a);
    s.add(b);
    UserSync sync;
    CHECK(sync.changed(s).size() == 2);  // first time: everyone
    CHECK(sync.changed(s).empty());
    s.find("chip breaker")->calls = 3;
    auto c = sync.changed(s);
    REQUIRE(c.size() == 1);
    User got;
    REQUIRE(parseUser(c[0], got));
    CHECK(got.handle == "Chip Breaker");
    CHECK(got.calls == 3);
}

TEST_CASE("a record received from elsewhere is not sent back out again") {
    UserStore s;
    UserSync  sync;
    User      a;
    a.handle = "Rev";
    s.add(a);
    sync.changed(s);
    User in;
    in.handle = "Lathe Lord";
    sync.received(formatUser(s.upsert(in)));
    CHECK(sync.changed(s).empty());
}
