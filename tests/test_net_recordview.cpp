// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// Reading the net record back (net/recordview.h): kinds and search, whether a signed line checks, a record's
// fields, and netSafe.
#include <algorithm>

#include "net_testutil.h"
#include "net/recordview.h"

using namespace nettest;

namespace {

// A record line as NetRecord writes it: "<unix> <who> <what>", 0x1F, then the signed line.
std::string rec(const std::string& who, const std::string& what, const std::string& signedLine = "") {
    return "1780000000 " + who + " " + what + "\x1f" + signedLine;
}

std::vector<std::string> fieldsOf(const std::string& signedLine) {
    std::vector<std::string> out;
    recordFields(
        signedLine, [](void* ctx, const std::string& l) { static_cast<std::vector<std::string>*>(ctx)->push_back(l); }, &out);
    return out;
}

bool has(const std::vector<std::string>& v, const std::string& x) { return std::find(v.begin(), v.end(), x) != v.end(); }

}  // namespace

TEST_CASE("record filters and search") {
    const std::string B = "0123456789abcdef", P = "fedcba9876543210";
    const std::vector<std::string> votes = {
        rec(P, "app " + B + " 1"),
        rec("local", "vote " + P + " on " + B + " yes"),
        rec(P, "notice admit " + B + " seq 3"),
        rec(P, "notice deny " + B + " seq 4"),
        rec(P, "notice veto " + B + " seq 5"),
        rec("local", "notice votes net seq 6"),
    };
    const std::vector<std::string> members = {
        rec(P, "card " + B + " 2"),
        rec(P, "member " + B + " admitted"),
        rec("local", "member " + B + " removed"),
        rec(P, "member " + B + " denied"),
    };
    const std::vector<std::string> actions = {
        rec("local", "notice kill " + B + "-4 seq 7"),
        rec(P, "notice hold " + B + "-5 seq 8"),
        rec(P, "notice ban " + B + " seq 9"),
        rec(P, "post stored " + B + "-6 sn_support"),
        rec(P, "session ok sent 3 took 2"),
        rec("local", "netkey set"),
        "1780000000 local netkey cleared",  // a line with no 0x1F at all
        "1780000000 local",                   // a head with fewer than two spaces
        "1780000000\x1f" "app " + B,          // the words after the separator are not the head
        rec(P, "noticeadmit " + B),           // no space after the word
    };
    for (const auto& l : votes) {
        CHECK_MESSAGE(recordMatches(l, RecordKind::Votes, ""), l);
        CHECK_FALSE(recordMatches(l, RecordKind::Members, ""));
        CHECK_FALSE(recordMatches(l, RecordKind::Actions, ""));
        CHECK(recordMatches(l, RecordKind::All, ""));
    }
    for (const auto& l : members) {
        CHECK_MESSAGE(recordMatches(l, RecordKind::Members, ""), l);
        CHECK_FALSE(recordMatches(l, RecordKind::Votes, ""));
        CHECK_FALSE(recordMatches(l, RecordKind::Actions, ""));
        CHECK(recordMatches(l, RecordKind::All, ""));
    }
    // A line with no 0x1F is all head, and its words decide as ever.
    CHECK(recordMatches("1780000000 local app " + B + " 1", RecordKind::Votes, ""));
    CHECK(recordMatches("1780000000 local member " + B + " admitted", RecordKind::Members, ""));
    for (size_t i = 0; i < actions.size(); i++) {
        CHECK_MESSAGE(recordMatches(actions[i], RecordKind::Actions, ""), actions[i]);
        CHECK_FALSE(recordMatches(actions[i], RecordKind::Votes, ""));
        CHECK_FALSE(recordMatches(actions[i], RecordKind::Members, ""));
        CHECK(recordMatches(actions[i], RecordKind::All, ""));
    }

    // Text: any case, in the head or in the signed line.
    KeyPair k = keyOf(0x71);
    NetPost p = makePost(k, 1, kNow, "sn_support", "Hello World", "Spindle Speeds");
    std::string stored = rec(P, "post stored " + p.id + " sn_support", encode(p));
    CHECK(recordMatches(stored, RecordKind::All, "WORLD"));
    CHECK(recordMatches(stored, RecordKind::All, "spindle speeds"));
    CHECK(recordMatches(stored, RecordKind::All, "POST STORED"));
    CHECK(recordMatches(stored, RecordKind::Actions, "hello"));
    CHECK_FALSE(recordMatches(stored, RecordKind::Votes, "hello"));  // the kind still has to match
    CHECK_FALSE(recordMatches(stored, RecordKind::All, "lathe"));
    CHECK(recordMatches(votes[0], RecordKind::Votes, B.substr(4, 6)));
    CHECK(recordMatches(votes[0], RecordKind::All, "APP " + B.substr(0, 4)));
    CHECK_FALSE(recordMatches(votes[0], RecordKind::Votes, "veto"));
    CHECK(recordMatches("", RecordKind::All, ""));
    CHECK_FALSE(recordMatches("", RecordKind::All, "x"));
}

TEST_CASE("recordCheck says whether a signed line checks") {
    RevKeys rk;
    KeyPair k = keyOf(0x72), voter = keyOf(0x73);
    NetCard card = makeCard(k, "Checked");
    NetPost p = makePost(k, 1, kNow, "sn_support", "Hello", "Body");
    std::string line = encode(p);
    NetState held, empty;
    REQUIRE(held.addCard(card));
    const std::string b = card.board;

    CHECK(recordCheck("", held) == "local action, not signed");
    CHECK(recordCheck(line, held) == "signed by " + b + ", checks");
    CHECK(recordCheck(line, empty) == "signed by " + b + ", no card held to check it");
    std::string changed = line;
    size_t at = changed.find("Hello");
    REQUIRE(at != std::string::npos);
    changed[at] = 'J';  // one byte changed, still a post
    CHECK(recordCheck(changed, held) == "signed by " + b + ", does NOT check");

    // Cards, applications and votes check against their own pub, card held or not.
    CHECK(recordCheck(encode(card), empty) == "signed by " + b + ", checks");
    NetApp app = makeApp(k, 1, kNow, "blurb");
    CHECK(recordCheck(encode(app), empty) == "signed by " + b + ", checks");
    std::string appChanged = encode(app);
    appChanged[appChanged.find("blurb")] = 'B';
    CHECK(recordCheck(appChanged, empty) == "signed by " + b + ", does NOT check");
    NetVote v = makeVote(voter, b, 1, 1, true);
    CHECK(recordCheck(encode(v), empty) == "signed by " + boardOf(voter) + ", checks");
    std::string voteChanged = encode(v);
    voteChanged[voteChanged.find("\x1fy\x1f") + 1] = 'n';
    CHECK(recordCheck(voteChanged, empty) == "signed by " + boardOf(voter) + ", does NOT check");

    // Notices against Rev's keys now.
    NetNotice n = makeNotice(rev0(), 0, "ban", b, 3);
    CHECK(recordCheck(encode(n), empty) == "signed by Rev, checks");
    NetNotice fake = makeNotice(keyOf(0x74), 0, "ban", b, 4);
    CHECK(recordCheck(encode(fake), empty) == "signed by Rev, does NOT check");
    NetNotice old1 = makeNotice(rev1(), 1, "ban", b, 20);
    CHECK(recordCheck(encode(old1), empty) == "signed by Rev, checks");
    NetState rotated;
    REQUIRE(rotated.applyNotice(makeNotice(rev0(), 0, "rotate", "1", 10, pubOf(keyOf(0x75))), 1));
    CHECK(recordCheck(encode(old1), rotated) == "signed by Rev, does NOT check");  // key 1 was rotated out

    // Lines that are no record.
    CHECK(recordCheck("Xjunk", held) == "signed by ?, does NOT check");
    CHECK(recordCheck("P\x1fjunk", held) == "signed by ?, does NOT check");
    CHECK(recordCheck(line.substr(0, line.size() - 1), held) == "signed by ?, does NOT check");
}

TEST_CASE("recordFields gives a record's fields one per line") {
    RevKeys rk;
    KeyPair k = keyOf(0x76), voter = keyOf(0x77);
    NetPost p = makePost(k, 9, kNow, "sn_bugs", "bell\x07 and del\x7f", "one\ntwo\x1b[2J\nthree");
    p.path = "0123456789abcdef";
    REQUIRE(signPost(p, k));
    auto f = fieldsOf(encode(p));
    std::vector<std::string> want = {"id " + p.id,
                                     "area sn_bugs",
                                     "board " + p.board,
                                     "from Sysop",
                                     "to All",
                                     "subject bell? and del?",
                                     "date " + netDateText(kNow),
                                     "path 0123456789abcdef",
                                     "body",
                                     "  one",
                                     "  two?[2J",
                                     "  three"};
    CHECK(f == want);

    // N newlines give N + 1 indented lines, at most 60, then how many more.
    auto bodyLines = [&](size_t newlines) {
        NetPost q = makePost(k, 10, kNow, "sn_support", "Lines", std::string(newlines, '\n'));
        auto g = fieldsOf(encode(q));
        auto at = std::find(g.begin(), g.end(), "body");
        REQUIRE(at != g.end());
        return std::vector<std::string>(at + 1, g.end());
    };
    auto b0 = bodyLines(0);
    CHECK(b0 == std::vector<std::string>{"  "});
    auto b2 = bodyLines(2);
    CHECK(b2.size() == 3);
    auto b59 = bodyLines(kRecordFieldLines - 1);
    CHECK(b59.size() == kRecordFieldLines);
    CHECK(b59.back() == "  ");
    auto b60 = bodyLines(kRecordFieldLines);
    REQUIRE(b60.size() == kRecordFieldLines + 1);
    CHECK(b60.back() == "  (1 more lines)");
    // The most newlines a post can hold (each is two bytes on the line; 5,000 would pass the frame).
    auto big = bodyLines(4000);
    REQUIRE(big.size() == kRecordFieldLines + 1);
    CHECK(big.back() == "  (" + std::to_string(4001 - kRecordFieldLines) + " more lines)");

    // An application: its id, fields and blurb split into lines.
    NetApp a = makeApp(k, 2, kNow, "first line\nsecond\x01line");
    auto af = fieldsOf(encode(a));
    CHECK(has(af, "application " + a.board + "-a2"));
    CHECK(has(af, "name Test Board"));
    CHECK(has(af, "mac 02:00:00:00:00:01"));
    CHECK(has(af, "date " + netDateText(kNow)));
    auto bl = std::find(af.begin(), af.end(), "blurb");
    REQUIRE(bl != af.end());
    REQUIRE(af.end() - bl == 3);
    CHECK(*(bl + 1) == "  first line");
    CHECK(*(bl + 2) == "  second?line");

    // A vote: whose, on which application, which way, its comment.
    NetVote v = makeVote(voter, a.board, 2, 4, false, kNow, "not\x1b yet");
    auto vf = fieldsOf(encode(v));
    CHECK(vf == std::vector<std::string>{"voter " + v.voter, "on " + a.board + "-a2", "vote no", "comment not? yet", "seq 4",
                                         "date " + netDateText(kNow)});

    // Cards and notices.
    NetCard c = makeCard(k, "Card Name", 3);
    auto cf = fieldsOf(encode(c));
    CHECK(has(cf, "name Card Name"));
    CHECK(has(cf, "seq 3"));
    CHECK(has(cf, "pub " + c.pub));
    NetNotice n = makeNotice(rev0(), 0, "admit", a.board, 11, "2");
    CHECK(fieldsOf(encode(n)) == std::vector<std::string>{"notice admit", "target " + a.board, "value 2", "seq 11", "key 0"});

    // Nothing for an empty or unreadable line.
    CHECK(fieldsOf("").empty());
    CHECK(fieldsOf("Xjunk").empty());
    CHECK(fieldsOf("P\x1fnot a post").empty());
    CHECK(fieldsOf("A\x1fnot an app").empty());
    recordFields(encode(p), nullptr, nullptr);  // no callback: nothing happens
}

TEST_CASE("netSafe turns control bytes into ? and keeps the rest") {
    std::string all;
    for (int c = 0; c < 256; c++) all += (char)c;
    std::string s = netSafe(all);
    REQUIRE(s.size() == 256);
    for (int c = 0; c < 256; c++) {
        char want = (c < 0x20 || c == 0x7f) ? '?' : (char)c;
        CHECK_MESSAGE(s[(size_t)c] == want, "byte " << c);
    }
    CHECK(netSafe("caf\xc3\xa9 ok") == "caf\xc3\xa9 ok");
    CHECK(netSafe("") == "");
}
