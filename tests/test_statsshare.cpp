// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include "bbs/statsshare.h"

#include "doctest.h"

using namespace axis::bbs;

namespace {
constexpr int kDay = 2026 * 400 + 314;

DayStats mainStats(uint32_t calls, uint32_t posts, uint32_t uploads, uint32_t total) {
    DayStats s;
    s.day = kDay, s.calls = calls, s.posts = posts, s.uploads = uploads, s.total = total;
    return s;
}
}  // namespace

TEST_CASE("stats share: a follower's counts add up and show alone until the main answers") {
    StatsShare f;
    CHECK_FALSE(f.waiting());
    f.call();
    f.call();
    f.upload();
    f.caller("Spindle Kid");
    CHECK(f.waiting());
    CHECK_FALSE(f.known());
    DayStats s = f.shown();
    CHECK(s.calls == 2);
    CHECK(s.total == 2);
    CHECK(s.uploads == 1);
    CHECK(s.posts == 0);
    CHECK(s.day == 0);
    CHECK(f.lastCaller() == "Spindle Kid");
}

TEST_CASE("stats share: a batch is cleared only by the main's answer, and a resend keeps its id") {
    StatsShare f;
    f.call();
    std::string req = f.request(77);
    CHECK(req == std::string("B\x1f" "77\x1f" "1\x1f" "0\x1f" "0\x1f", 11));
    f.call();  // counted while the batch is out: it waits for the next one
    CHECK(f.shown().calls == 2);
    CHECK_FALSE(f.answer("0"));        // the main said no
    CHECK_FALSE(f.answer(""));         // nothing
    CHECK_FALSE(f.answer("1garbage"));  // not its numbers
    CHECK(f.waiting());
    CHECK(f.request(99) == req);  // the same batch again, same id, not the new call
    CHECK(f.shown().calls == 2);
    CHECK(f.answer("1" + encodeStatsSnapshot(mainStats(5, 3, 1, 140), "ChipBreaker")));
    CHECK(f.known());
    DayStats s = f.shown();  // the main's numbers (with the acknowledged call in them) plus the one still pending
    CHECK(s.calls == 6);
    CHECK(s.total == 141);
    CHECK(s.posts == 3);
    CHECK(s.uploads == 1);
    CHECK(s.day == kDay);
    CHECK(f.lastCaller() == "ChipBreaker");
    CHECK(f.waiting());
    CHECK(f.request(100).find("B\x1f" "100\x1f" "1\x1f") == 0);  // the next batch is a new one
    CHECK_FALSE(f.answer("1"));  // an answer without numbers does not clear it
    CHECK(f.waiting());
}

TEST_CASE("stats share: the heartbeat's numbers replace the last ones; the pending counts stay on top") {
    StatsShare f;
    f.upload();
    CHECK(f.heard(encodeStatsSnapshot(mainStats(2, 1, 0, 10), "")));
    CHECK(f.shown().uploads == 1);
    CHECK(f.shown().calls == 2);
    CHECK(f.heard(encodeStatsSnapshot(mainStats(3, 1, 0, 11), "RotaryRick")));
    CHECK(f.shown().calls == 3);
    CHECK(f.shown().total == 11);
    CHECK(f.lastCaller() == "RotaryRick");
    CHECK_FALSE(f.heard("not stats"));
    CHECK_FALSE(f.heard("v1 day=1 calls=x posts=0 uploads=0 total=0\nX"));
    CHECK(f.shown().calls == 3);  // garbage changes nothing
    f.caller("EndmillEd");  // a caller here, not yet sent, is the newest
    CHECK(f.lastCaller() == "EndmillEd");
}

TEST_CASE("stats share: batch and snapshot read back, garbage is turned away") {
    StatsCounts c;
    c.calls = 3, c.posts = 1, c.uploads = 2, c.who = "Spindle Kid";
    uint32_t    id = 0;
    StatsCounts got;
    REQUIRE(decodeStatsBatch(encodeStatsBatch(42, c), id, got));
    CHECK(id == 42);
    CHECK(got.calls == 3);
    CHECK(got.posts == 1);
    CHECK(got.uploads == 2);
    CHECK(got.who == "Spindle Kid");
    CHECK_FALSE(decodeStatsBatch("B", id, got));
    CHECK_FALSE(decodeStatsBatch(std::string("B\x1f" "0\x1f" "1\x1f" "0\x1f" "0\x1f"), id, got));  // id 0
    CHECK_FALSE(decodeStatsBatch(std::string("B\x1f" "5\x1f" "-1\x1f" "0\x1f" "0\x1f"), id, got));
    CHECK_FALSE(decodeStatsBatch(std::string("B\x1f" "5\x1f" "1\x1f" "0\x1f"), id, got));  // a field short
    CHECK_FALSE(decodeStatsBatch(std::string("B\x1f" "5\x1f" "100001\x1f" "0\x1f" "0\x1f"), id, got));  // too many
    CHECK_FALSE(decodeStatsBatch(std::string("B\x1f" "5\x1f" "1\x1f" "0\x1f" "0\x1f" "a\x1f" "b"), id, got));
    CHECK_FALSE(decodeStatsBatch(std::string("B\x1f" "5\x1f" "1\x1f" "0\x1f" "0\x1f" "bad\nname"), id, got));
    CHECK_FALSE(decodeStatsBatch(std::string("K\x1f" "5\x1f" "1\x1f" "0\x1f" "0\x1f"), id, got));
    CHECK(id == 42);  // nothing changed by the bad ones
    DayStats    s;
    std::string last;
    REQUIRE(decodeStatsSnapshot(encodeStatsSnapshot(mainStats(1, 2, 3, 4), "Cut\x1fter\nX"), s, last));
    CHECK(last == "CutterX");  // no separators or line ends get through
    CHECK(s.total == 4);
    CHECK_FALSE(decodeStatsSnapshot("v1 day=1 calls=1 posts=1 uploads=1\n", s, last));  // total missing
    CHECK_FALSE(decodeStatsSnapshot("v1 day=1 calls=1 posts=1 uploads=1 total=1", s, last));  // no line end
    CHECK_FALSE(decodeStatsSnapshot(std::string(300, 'v'), s, last));
    CHECK(s.total == 4);
}

TEST_CASE("stats inbox: the main applies a batch once, a resend is answered without counting") {
    StatsInbox  in;
    DayStats    s = mainStats(5, 2, 0, 100);
    std::string last = "ChipBreaker";
    StatsShare  f;
    f.call();
    f.upload();
    f.caller("EndmillEd");
    std::string req = f.request(9001);
    CHECK(in.apply(req, s, last) == StatsInbox::Result::Applied);
    CHECK(s.calls == 6);
    CHECK(s.total == 101);
    CHECK(s.uploads == 1);
    CHECK(s.posts == 2);
    CHECK(last == "EndmillEd");
    // the answer was lost: the follower sends the same batch again
    CHECK(f.request(1) == req);
    CHECK(in.apply(req, s, last) == StatsInbox::Result::Again);
    CHECK(s.calls == 6);
    CHECK(s.total == 101);
    CHECK(f.answer("1" + encodeStatsSnapshot(s, last)));
    CHECK_FALSE(f.waiting());
    CHECK(f.shown().calls == 6);  // the follower now shows exactly the main's numbers
    CHECK(f.shown().total == 101);
    CHECK(in.apply("B\x1fjunk", s, last) == StatsInbox::Result::Bad);
    CHECK(s.calls == 6);
    StatsCounts none;  // a batch without a caller keeps the main's last caller
    none.calls = 1;
    CHECK(in.apply(encodeStatsBatch(9002, none), s, last) == StatsInbox::Result::Applied);
    CHECK(last == "EndmillEd");
    CHECK(s.calls == 7);
}

TEST_CASE("stats share: a link drop loses nothing, and the follower never rolls the day itself") {
    StatsInbox  in;
    DayStats    m = mainStats(0, 0, 0, 50);
    std::string last;
    StatsShare  f;
    f.call();
    std::string first = f.request(1);  // the link went: no answer, the batch stays out
    f.call();
    f.call();
    CHECK(f.shown().calls == 3);
    CHECK(f.request(2) == first);  // back: the batch out goes first
    CHECK(in.apply(first, m, last) == StatsInbox::Result::Applied);
    CHECK(f.answer("1" + encodeStatsSnapshot(m, last)));
    CHECK(in.apply(f.request(3), m, last) == StatsInbox::Result::Applied);
    CHECK(f.answer("1" + encodeStatsSnapshot(m, last)));
    CHECK(m.calls == 3);
    CHECK(m.total == 53);
    CHECK(f.shown().calls == 3);
    m.roll(kDay + 1);  // midnight on the main
    CHECK(f.heard(encodeStatsSnapshot(m, last)));
    CHECK(f.shown().calls == 0);
    CHECK(f.shown().total == 53);
}
