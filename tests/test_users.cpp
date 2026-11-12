// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include "doctest.h"
#include "bbs/config.h"
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

TEST_CASE("a user's permissions survive a save: files, messages, doors, free downloads") {
    UserStore s;
    User      u;
    u.handle = "Lathe Lord";
    CHECK(u.permFiles);
    CHECK(u.permMessages);
    CHECK(u.permDoors);
    CHECK_FALSE(u.freeDownloads);
    u.permDoors = false;
    u.freeDownloads = true;
    u.uploads = 12;
    s.add(u);
    UserStore t;
    t.parse(s.format());
    const User& b = t.all()[0];
    CHECK(b.permFiles);
    CHECK_FALSE(b.permDoors);
    CHECK(b.freeDownloads);
    CHECK(mayDownload(b));
    User fresh;
    CHECK_FALSE(mayDownload(fresh));  // nothing uploaded yet
    fresh.uploads = 1;
    CHECK(mayDownload(fresh));
    fresh.permFiles = false;
    CHECK_FALSE(mayDownload(fresh));
}

TEST_CASE("the download ratio: up to ratio times what you uploaded, plus a free allowance") {
    User u;
    u.uploads = 100 * 1024;    // 100 KB given
    u.downloads = 250 * 1024;  // 250 KB taken
    CHECK(ratioBlock(u, 40 * 1024, 0, 0).empty());           // ratio off: anything goes
    CHECK(ratioBlock(u, 40 * 1024, 3, 0).empty());           // 1:3 -> 300 KB, 290 after this one
    std::string why = ratioBlock(u, 60 * 1024, 3, 0);        // would be 310 KB
    CHECK(why.find("50 KB") != std::string::npos);           // what's left to take
    CHECK(ratioBlock(u, 60 * 1024, 3, 16).empty());          // a 16 KB free allowance covers it
    u.freeDownloads = true;
    CHECK(ratioBlock(u, 900 * 1024, 3, 0).empty());          // the sysop's FREE DL
}

TEST_CASE("the ratio settings are kept in the settings file") {
    BbsConfig c = parseConfig("ratio=3\nfree_kb=512\n");
    CHECK(c.ratio == 3);
    CHECK(c.freeKb == 512);
    BbsConfig back = parseConfig(formatConfig(c));
    CHECK(back.ratio == 3);
    CHECK(back.freeKb == 512);
    CHECK(parseConfig("").ratio == 0);
}

TEST_CASE("users formatTo matches format") {
    UserStore st;
    for (const char* h : {"Rev", "Spindle Kid", "Chip Breaker"}) {
        User u;
        u.handle = h;
        u.salt = "salt";
        u.hash = "hash";
        u.answers = {{"Real name", std::string(h) + " Real"}, {"Machine", "Tormach 1100"}};
        u.votes = {{"Rev", true, "good egg"}, {"Spindle Kid", false, ""}};
        u.lastRead["general"] = 7;
        st.add(u);
    }
    std::string pieces;
    int         n = 0;
    st.formatTo([&](const std::string& p) {
        pieces += p;
        n++;
    });
    CHECK(pieces == st.format());
    CHECK(n >= 3);  // one block a user at least: never the whole file in one string
}
