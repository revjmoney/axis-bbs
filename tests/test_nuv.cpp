// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include "doctest.h"
#include "bbs/nuv.h"
#include "bbs/users.h"

using namespace axis::bbs;

namespace {
User person(const char* handle, User::Status st, int security = 20) {
    User u;
    u.handle = handle;
    u.status = st;
    u.security = security;
    return u;
}
}  // namespace

TEST_CASE("three yes votes (the default) let an applicant in, and only then") {
    NuvRules r;
    User     a = person("cnc4life", User::Status::Pending, 10);
    CHECK(nuvCast(a, "ChipBreaker", true, "", r) == NuvOutcome::Counted);
    CHECK(nuvCast(a, "EndmillEd", true, "knows his stuff", r) == NuvOutcome::Counted);
    CHECK(a.status == User::Status::Pending);
    CHECK(nuvCast(a, "RotaryRick", true, "", r) == NuvOutcome::Validated);
    CHECK(a.status == User::Status::Validated);
    CHECK(a.security == 20);
    CHECK(nuvYes(a) == 3);
    CHECK(nuvCast(a, "LatheLarry", false, "", r) == NuvOutcome::NotPending);  // decided: no more votes
    CHECK(a.votes.size() == 3);
}

TEST_CASE("the counts are the sysop's: one yes, or two no") {
    NuvRules r;
    r.yes = 1;
    User a = person("A", User::Status::Pending);
    CHECK(nuvCast(a, "x", true, "", r) == NuvOutcome::Validated);
    r.yes = 5;
    r.no = 2;
    User b = person("B", User::Status::Pending);
    CHECK(nuvCast(b, "x", false, "lamer", r) == NuvOutcome::Counted);
    CHECK(nuvCast(b, "y", true, "", r) == NuvOutcome::Counted);
    CHECK(nuvCast(b, "z", false, "", r) == NuvOutcome::Denied);
    CHECK(b.status == User::Status::Denied);
    r.no = 0;  // nobody but the sysop turns anyone down
    User c = person("C", User::Status::Pending);
    for (const char* v : {"a", "b", "c", "d", "e", "f"}) CHECK(nuvCast(c, v, false, "", r) == NuvOutcome::Counted);
    CHECK(c.status == User::Status::Pending);
}

TEST_CASE("a second vote from the same caller replaces the first") {
    NuvRules r;
    User     a = person("A", User::Status::Pending);
    nuvCast(a, "ChipBreaker", false, "hmm", r);
    nuvCast(a, "chipbreaker", true, "changed my mind", r);
    CHECK(a.votes.size() == 1);
    CHECK(nuvYes(a) == 1);
    CHECK(nuvVoteBy(a, "CHIPBREAKER")->comment == "changed my mind");
}

TEST_CASE("who may vote: validated, at the level, not a twit, and only with voting on") {
    NuvRules r;
    CHECK(nuvMayVote(person("v", User::Status::Validated, 20), r));
    CHECK_FALSE(nuvMayVote(person("low", User::Status::Validated, 10), r));
    CHECK_FALSE(nuvMayVote(person("p", User::Status::Pending, 50), r));
    User t = person("t", User::Status::Validated, 50);
    t.twit = true;
    CHECK_FALSE(nuvMayVote(t, r));
    r.yes = 0;
    CHECK_FALSE(nuvMayVote(person("v", User::Status::Validated, 255), r));
}

TEST_CASE("the waiting list skips yourself, people you voted on, and everyone already decided") {
    UserStore s;
    s.add(person("Old", User::Status::Validated));
    s.add(person("First", User::Status::Pending));
    s.add(person("Me", User::Status::Pending));
    s.add(person("Gone", User::Status::Denied));
    s.add(person("Second", User::Status::Pending));
    NuvRules r;
    nuvCast(*s.find("second"), "me", true, "", r);
    auto w = nuvWaiting(s, "ME");
    REQUIRE(w.size() == 1);
    CHECK(w[0]->handle == "First");
    CHECK(nuvWaiting(s, "Old").size() == 3);
}

TEST_CASE("votes and their comments survive the users file and the link") {
    User a = person("cnc4life", User::Status::Pending);
    a.votes.push_back({"Spindle Kid", true, "good answers"});
    a.votes.push_back({"a=b", false, ""});
    User back;
    REQUIRE(parseUser(formatUser(a), back));
    REQUIRE(back.votes.size() == 2);
    CHECK(back.votes[0].voter == "Spindle Kid");
    CHECK(back.votes[0].yes);
    CHECK(back.votes[0].comment == "good answers");
    CHECK(back.votes[1].voter == "a-b");
    CHECK_FALSE(back.votes[1].yes);
    CHECK(back.votes[1].comment.empty());
}

TEST_CASE("two boards voting at once keep both votes, and a stale record can't undo a decided vote") {
    UserStore main;
    User      a = person("cnc4life", User::Status::Pending);
    a.votes.push_back({"x", true, ""});
    main.add(a);
    User fromB = a;
    fromB.votes = {{"y", true, ""}};  // board B had not seen x's vote yet
    main.upsert(fromB);
    CHECK(main.find("cnc4life")->votes.size() == 2);

    NuvRules r;
    nuvCast(*main.find("cnc4life"), "z", true, "", r);
    REQUIRE(main.find("cnc4life")->status == User::Status::Validated);
    User stale = fromB;  // still pending, from a board that had not heard
    stale.votes.push_back({"w", false, ""});
    main.upsert(stale);
    CHECK(main.find("cnc4life")->status == User::Status::Validated);
    CHECK(main.find("cnc4life")->security == 20);
    CHECK(main.find("cnc4life")->votes.size() == 4);
}
