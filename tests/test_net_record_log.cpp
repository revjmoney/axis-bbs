// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The net record: an append-only log of what the board saw on the net, one file per month.
#include <map>

#include "net/netrecord.h"
#include "net_testutil.h"

using namespace nettest;

namespace {
struct Files {
    std::map<std::string, std::vector<std::string>> f;
    bool fail = false;
    std::function<bool(const std::string&, const std::string&)> appender() {
        return [this](const std::string& path, const std::string& line) {
            if (fail) return false;
            f[path].push_back(line);
            return true;
        };
    }
};
}  // namespace

TEST_CASE("record appends signed lines") {
    Files files;
    std::string month = "202611";
    uint32_t now = kNow;
    NetRecord rec(files.appender(), [&] { return month; }, [&] { return now; });
    CHECK(rec.note("0123456789abcdef", "post stored x-1 sn_support", "P\x1Fsigned"));
    CHECK(rec.note("", "ban set", ""));
    const auto& lines = files.f["/net/record/202611.log"];
    REQUIRE(lines.size() == 2);
    CHECK(lines[0] == std::to_string(kNow) + " 0123456789abcdef post stored x-1 sn_support\x1FP\x1Fsigned");
    CHECK(lines[1] == std::to_string(kNow) + " local ban set\x1F");
    CHECK(recordHead(lines[0]) == std::to_string(kNow) + " 0123456789abcdef post stored x-1 sn_support");
    std::vector<std::string> before = lines;

    // more notes never rewrite what is there
    now += 60;
    CHECK(rec.note("peer", "card", "C\x1Fx"));
    REQUIRE(files.f["/net/record/202611.log"].size() == 3);
    for (size_t i = 0; i < before.size(); i++) CHECK(files.f["/net/record/202611.log"][i] == before[i]);

    // a new month goes to its own file; the old one is left as it was
    month = "202612";
    CHECK(rec.note("peer", "next month", ""));
    CHECK(files.f["/net/record/202611.log"].size() == 3);
    CHECK(files.f["/net/record/202612.log"].size() == 1);
    month = "";
    CHECK(rec.note("peer", "no clock", ""));
    CHECK(files.f["/net/record/nodate.log"].size() == 1);
}

TEST_CASE("record lines carry no control bytes and stay under the cap") {
    Files files;
    NetRecord rec(files.appender(), [] { return std::string("202611"); });  // no clock: unix 0
    CHECK(rec.note("a\nb\x1F" "c\x1B", "what\r\nnext", ""));
    const auto& l = files.f["/net/record/202611.log"];
    REQUIRE(l.size() == 1);
    CHECK(l[0] == "0 a b c  what  next\x1F");
    // a signed line that would pass the cap is left out, the head stays
    CHECK(rec.note("p", "big", std::string(kNetRecordMaxLine, 'x')));
    CHECK(l[1] == "0 p big\x1F");
    // one with a newline is never written into the line
    CHECK(rec.note("p", "nl", "a\nb"));
    CHECK(l[2] == "0 p nl\x1F");
    // a long what is cut
    CHECK(rec.note("p", std::string(500, 'w'), ""));
    CHECK(recordHead(l[3]).size() == 200);
    files.fail = true;
    CHECK_FALSE(rec.note("p", "lost", ""));
    CHECK(rec.failures() == 1);
}

TEST_CASE("NetCore's events feed the record") {
    RevKeys rk;
    Files files;
    Node a(0xC1), b(0xC2);
    NetRecord rec(files.appender(), [] { return std::string("202611"); }, [&] { return b.now; });
    b.core.setOnEvent([&](const std::string& who, const std::string& what, const std::string& line) { rec.note(who, what, line); });
    NetPost p = decodePost(a.post());
    CHECK(b.core.take(p, a.board) == NetCore::Took::Rejected);  // no card yet
    b.knows(a);
    CHECK(b.core.take(p, a.board) == NetCore::Took::Stored);
    const auto& l = files.f["/net/record/202611.log"];
    bool rejected = false, stored = false, card = false;
    for (const auto& x : l) {
        std::string head = recordHead(x);
        if (head.find("post rejected signature " + p.id) != std::string::npos) rejected = true;
        if (head.find("post stored " + p.id) != std::string::npos) {
            stored = true;
            NetPost q;
            CHECK(decode(x.substr(x.find('\x1F') + 1), q));  // the signed line rides along
            CHECK(verifyPost(q, a.card.pub));
        }
        if (head.find("card " + a.board) != std::string::npos) card = true;
    }
    CHECK(rejected);
    CHECK(stored);
    CHECK(card);
}
