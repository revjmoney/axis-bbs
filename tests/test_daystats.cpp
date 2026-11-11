// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include "bbs/daystats.h"
#include "bbs/sysopshell.h"
#include "bbs/wfc.h"
#include "bbs/wfc80_art.h"
#include "platform/cyd/wfccyd_art.h"

#include "doctest.h"

using namespace axis::bbs;

namespace {
constexpr int kDay = 2026 * 400 + 314, kNextDay = kDay + 1;

DayStats reboot(const DayStats& s) {  // what the board has after a restart: the saved line read back
    DayStats r;
    REQUIRE(r.parse(s.format()));
    return r;
}
}  // namespace

TEST_CASE("day stats: the saved line reads back the same") {
    DayStats s;
    s.day = kDay, s.calls = 7, s.posts = 3, s.uploads = 2, s.total = 1234;
    CHECK(s.format() == "v1 day=" + std::to_string(kDay) + " calls=7 posts=3 uploads=2 total=1234\n");
    DayStats r = reboot(s);
    CHECK(r.day == kDay);
    CHECK(r.calls == 7);
    CHECK(r.posts == 3);
    CHECK(r.uploads == 2);
    CHECK(r.total == 1234);
    CHECK(r.parse("v1 total=9 uploads=1 posts=0 calls=4 day=0\r\n"));  // any order, CR LF
    CHECK(r.total == 9);
    CHECK(r.calls == 4);
    CHECK(r.day == 0);
    CHECK(r.parse("v1 day=1 calls=1 posts=1 uploads=1 total=4294967295 later=5\n"));  // a key it does not know is skipped
    CHECK(r.total == 4294967295u);
}

TEST_CASE("day stats: a missing or broken file starts from zero") {
    for (const char* bad : {"", "\n", "garbage", "v2 day=1 calls=1 posts=1 uploads=1 total=1", "v1 day=1 calls=1 posts=1 uploads=1",
                            "v1 day=1 calls=x posts=1 uploads=1 total=1", "v1 day=1 calls=-3 posts=1 uploads=1 total=1",
                            "v1 day=1 calls=1 posts=1 uploads=1 total=99999999999", "v1 day=1 calls=1 posts=1 uploads=1 total=",
                            "\xff\xfe\x00\x01", "v1 "}) {
        DayStats s;
        s.calls = s.total = 5;
        CHECK_FALSE(s.parse(std::string(bad, bad + std::char_traits<char>::length(bad))));
        CHECK(s.calls == 0);
        CHECK(s.posts == 0);
        CHECK(s.uploads == 0);
        CHECK(s.total == 0);
        CHECK(s.day == 0);
    }
    DayStats s;
    CHECK_FALSE(s.parse(std::string("v1 day=1\0 calls=1", 17)));
}

TEST_CASE("day stats: a reboot on the same day keeps counting") {
    DayStats s;
    CHECK(s.roll(kDay));
    s.call(), s.call(), s.post(), s.upload();
    DayStats r = reboot(s);
    CHECK_FALSE(r.roll(kDay));  // the clock comes back on the same day: nothing resets
    r.call();
    CHECK(r.calls == 3);
    CHECK(r.posts == 1);
    CHECK(r.uploads == 1);
    CHECK(r.total == 3);
}

TEST_CASE("day stats: the next day zeroes the day counts and keeps the total") {
    DayStats s;
    s.roll(kDay);
    s.call(), s.call(), s.post(), s.upload();
    DayStats r = reboot(s);  // off overnight
    CHECK(r.roll(kNextDay));
    CHECK(r.day == kNextDay);
    CHECK(r.calls == 0);
    CHECK(r.posts == 0);
    CHECK(r.uploads == 0);
    CHECK(r.total == 2);
    r.call();
    CHECK(r.calls == 1);
    CHECK(r.total == 3);
    CHECK(s.roll(kNextDay));  // the same at midnight while running
    CHECK(s.calls == 0);
    CHECK(s.total == 2);
}

TEST_CASE("day stats: an unset clock never resets") {
    DayStats s;
    s.call(), s.post();
    CHECK_FALSE(s.roll(0));
    CHECK_FALSE(s.roll(-1));
    CHECK(s.calls == 1);
    CHECK(s.roll(kDay));  // the first real day takes the counts so far
    CHECK(s.calls == 1);
    CHECK(s.posts == 1);
    DayStats r = reboot(s);
    CHECK_FALSE(r.roll(0));  // rebooted with no clock yet: the saved day and counts stay
    r.call();
    CHECK(r.day == kDay);
    CHECK(r.calls == 2);
    CHECK(r.roll(kNextDay));  // the clock comes back a day later
    CHECK(r.calls == 0);
    CHECK(r.total == 2);
}

TEST_CASE("day stats: the total never resets") {
    DayStats s;
    s.total = 100;
    for (int d = 0; d < 5; d++) {
        s.roll(kDay + d);
        s.call();
        s = reboot(s);
    }
    CHECK(s.total == 105);
    CHECK(s.calls == 1);
}

TEST_CASE("@STATS total= takes plain digits only") {
    uint32_t n = 7;
    CHECK(parseStatTotal("0", n));
    CHECK(n == 0);
    CHECK(parseStatTotal("4294967295", n));
    CHECK(n == 4294967295u);
    CHECK(parseStatTotal("00012", n));
    CHECK(n == 12);
    n = 7;
    for (const char* bad : {"", "-1", "+5", "4294967296", "99999999999", "12a", " 12", "12 ", "1.5", "0x10"}) {
        CHECK_FALSE(parseStatTotal(bad, n));
        CHECK(n == 7);
    }
    CHECK(boardOnlyCommand("@STATS"));  // each board keeps its own counters: a follower answers for itself
    CHECK(boardOnlyCommand("@stats total=5"));
    CHECK_FALSE(boardOnlyCommand("@STATUS"));
}

TEST_CASE("wfc: TC is the lifetime call total") {
    WfcSnapshot s;
    s.callsToday = 3;
    s.totalCalls = 4821;
    CHECK(expandArt("|CT/|TC @TC####@|", {}, wfcValues(s)) == "3/4821 4821    |");
    WfcSnapshot t = s;
    CHECK(t == s);
    t.totalCalls++;
    CHECK_FALSE(t == s);  // a new call redraws the screen
}

TEST_CASE("wfc: both built-in screens show TC") {
    WfcSnapshot s;
    s.totalCalls = 4821;
    AnsiTerm cyd(45, 24);
    drawWfc(kWfcCydAns, s, cyd);
    auto has = [](AnsiTerm& t, const std::string& text) {
        for (int r = 0; r < t.rows(); r++) {
            std::string row;
            for (int c = 0; c < t.cols(); c++) row += (char)t.cell(r, c).ch;
            if (row.find(text) != std::string::npos) return true;
        }
        return false;
    };
    CHECK(has(cyd, "total 4821"));
    bool big = false;  // the classic 80x25 screen (CGA cells): its calls row carries the total field
    for (int r = 0; r < kWfc80Rows; r++) {
        std::string row;
        for (int c = 0; c < 80; c++) row += (char)kWfc80Art[r][c][0];
        big = big || row.find("Total @TC") != std::string::npos;
    }
    CHECK(big);
}
