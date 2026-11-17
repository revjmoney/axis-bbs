// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The User Net tally (net/members.h): applications, votes and Rev's member notices, the rule over them,
// its caps, and saving and loading the apps file.
#include <algorithm>
#include <cstdio>
#include <random>

#include "net_testutil.h"

using namespace nettest;
using Status = NetMembers::Status;

namespace {

std::string idOf(uint32_t i) {
    char b[17];
    std::snprintf(b, sizeof b, "%016x", i);
    return b;
}

std::string revBoard() { return boardOf(rev0()); }

// One board's view of User Net: its NetState, its tally (Rev's board is the one with key rev0) and the
// lines taken, as its apps file holds them.
struct Tally {
    NetState st;
    std::string rev = revBoard();
    NetMembers m{st, [this](const std::string& b) { return b == rev; }};
    uint32_t seq = 0;
    std::string file;

    Tally() = default;
    Tally(const Tally&) = delete;
    Tally& operator=(const Tally&) = delete;

    bool notice(const std::string& kind, const std::string& target, const std::string& value = "") {
        return st.applyNotice(makeNotice(rev0(), 0, kind, target, ++seq, value), 1);
    }
    bool admit(const KeyPair& k, uint32_t appSeq = 1) { return notice("admit", boardOf(k), std::to_string(appSeq)); }
    bool votesNeeded(uint32_t yes, uint32_t no) {
        return notice("votes", "net", "yes=" + std::to_string(yes) + ",no=" + std::to_string(no));
    }
    bool app(const NetApp& a) {
        std::string l;
        bool ok = m.takeApp(a, l);
        if (ok) file += l + "\n";
        return ok;
    }
    bool vote(const NetVote& v) {
        std::string l;
        bool ok = m.takeVote(v, l);
        if (ok) file += l + "\n";
        return ok;
    }
    Status status(const KeyPair& k) const { return m.status(boardOf(k)); }
};

bool has(const std::vector<std::string>& v, const std::string& x) { return std::find(v.begin(), v.end(), x) != v.end(); }

}  // namespace

TEST_CASE("member and votes notices group") {
    RevKeys rk;
    const std::string X = "0123456789abcdef";
    CHECK(noticeGroup(makeNotice(rev0(), 0, "admit", X, 1, "1")) == "member");
    CHECK(noticeGroup(makeNotice(rev0(), 0, "deny", X, 2, "1")) == "member");
    CHECK(noticeGroup(makeNotice(rev0(), 0, "veto", X, 3)) == "member");
    CHECK(noticeGroup(makeNotice(rev0(), 0, "votes", "net", 4, "yes=4,no=2")) == "votes");
    CHECK(noticeGroup(makeNotice(rev0(), 0, "votes", X, 4, "yes=4,no=2")).empty());     // votes target net only
    CHECK(noticeGroup(makeNotice(rev0(), 0, "admit", "abc-1", 4, "1")).empty());       // a member target is a board id
    CHECK(noticeGroup(makeNotice(rev0(), 0, "admit", "0123456789ABCDEF", 4, "1")).empty());

    NetState s;
    CHECK(s.votesYes() == kDefaultVotesYes);
    CHECK(s.votesNo() == kDefaultVotesNo);
    CHECK(s.applyNotice(makeNotice(rev0(), 0, "votes", "net", 10, "yes=4,no=2"), 1));
    CHECK(s.votesYes() == 4);
    CHECK(s.votesNo() == 2);
    CHECK_FALSE(s.applyNotice(makeNotice(rev0(), 0, "votes", "net", 11, "yes=0,no=3"), 1));
    CHECK_FALSE(s.applyNotice(makeNotice(rev0(), 0, "votes", "net", 12, "yes=3,no=0"), 1));
    CHECK_FALSE(s.applyNotice(makeNotice(rev0(), 0, "votes", "net", 13, "yes=21,no=3"), 1));
    CHECK_FALSE(s.applyNotice(makeNotice(rev0(), 0, "votes", "net", 14, "yes=3,no=21"), 1));
    CHECK_FALSE(s.applyNotice(makeNotice(rev0(), 0, "votes", "net", 15, "yes=03,no=3"), 1));  // a leading zero
    CHECK_FALSE(s.applyNotice(makeNotice(rev0(), 0, "votes", "net", 16, "yes=3"), 1));
    CHECK(s.votesYes() == 4);
    CHECK(s.votesNo() == 2);
    CHECK(s.applyNotice(makeNotice(rev0(), 0, "votes", "net", 17, "yes=20,no=1"), 1));  // the edges
    CHECK(s.votesYes() == 20);
    CHECK(s.votesNo() == 1);

    // admit and deny name an app seq (decimal, one spelling, not 0); a veto names none.
    CHECK_FALSE(s.applyNotice(makeNotice(rev0(), 0, "admit", X, 20), 1));
    CHECK_FALSE(s.applyNotice(makeNotice(rev0(), 0, "admit", X, 21, "0"), 1));
    CHECK_FALSE(s.applyNotice(makeNotice(rev0(), 0, "admit", X, 22, "01"), 1));
    CHECK_FALSE(s.applyNotice(makeNotice(rev0(), 0, "deny", X, 23, "x"), 1));
    CHECK_FALSE(s.applyNotice(makeNotice(rev0(), 0, "veto", X, 24, "1"), 1));
    CHECK(s.memberNotice(X) == nullptr);
    CHECK(s.applyNotice(makeNotice(rev0(), 0, "veto", X, 25), 1));
    REQUIRE(s.memberNotice(X) != nullptr);
    CHECK(s.memberNotice(X)->kind == "veto");
    CHECK(s.applyNotice(makeNotice(rev0(), 0, "admit", X, 26, "2"), 1));  // the latest seq for the board stands
    CHECK(s.memberNotice(X)->kind == "admit");
    CHECK_FALSE(s.applyNotice(makeNotice(rev0(), 0, "deny", X, 26, "2"), 1));  // not newer
    CHECK(s.memberNoticeCount() == 1);
}

TEST_CASE("the vote counts fall back to 3 and 3 when a rotate drops the votes notice") {
    RevKeys rk;
    NetState s;
    KeyPair fresh = keyOf(0xB7);
    CHECK(s.applyNotice(makeNotice(rev1(), 1, "votes", "net", 20, "yes=5,no=4"), 1));
    CHECK(s.votesYes() == 5);
    CHECK(s.votesNo() == 4);
    // A rotate at seq 10 replaces key 1: the votes notice (seq 20, old key 1) no longer checks.
    CHECK(s.applyNotice(makeNotice(rev0(), 0, "rotate", "1", 10, pubOf(fresh)), 1));
    CHECK(s.noticeLine("votes", "net").empty());
    CHECK(s.votesYes() == kDefaultVotesYes);
    CHECK(s.votesNo() == kDefaultVotesNo);
    CHECK(kDefaultVotesYes == 3);
    CHECK(kDefaultVotesNo == 3);
}

TEST_CASE("a 65th member seal target is refused; a held target may still change") {
    RevKeys rk;
    NetState s;
    uint32_t seq = 0;
    for (uint32_t i = 1; i <= kMaxMemberNotices; i++) REQUIRE(s.applyNotice(makeNotice(rev0(), 0, "admit", idOf(i), ++seq, "1"), 1));
    CHECK(s.memberNoticeCount() == kMaxMemberNotices);
    CHECK_FALSE(s.applyNotice(makeNotice(rev0(), 0, "admit", idOf(1000), ++seq, "1"), 1));
    CHECK_FALSE(s.applyNotice(makeNotice(rev0(), 0, "veto", idOf(1001), ++seq), 1));
    CHECK(s.memberNotice(idOf(1000)) == nullptr);
    // Nothing held was evicted, and a held target takes a newer notice.
    for (uint32_t i = 1; i <= kMaxMemberNotices; i++) REQUIRE(s.memberNotice(idOf(i)) != nullptr);
    CHECK(s.applyNotice(makeNotice(rev0(), 0, "veto", idOf(1), ++seq), 1));
    CHECK(s.memberNotice(idOf(1))->kind == "veto");
    CHECK(s.memberNoticeCount() == kMaxMemberNotices);
    // The other groups have their own room.
    CHECK(s.applyNotice(makeNotice(rev0(), 0, "ban", idOf(1000), ++seq), 1));
}

TEST_CASE("tally is the same in any order") {
    RevKeys rk;
    const KeyPair A = keyOf(0x51), B = keyOf(0x52), C = keyOf(0x53), D = keyOf(0x54);
    const std::string R = revBoard(), c = boardOf(C), d = boardOf(D);
    auto isRev = [R](const std::string& b) { return b == R; };
    // The six records: C's application, R, A and B voting yes on it, and R's admits of A and B.
    const NetApp appC = makeApp(C, 1, kNow);
    const NetVote votes[3] = {makeVote(rev0(), c, 1, 1, true), makeVote(A, c, 1, 1, true), makeVote(B, c, 1, 1, true)};
    const NetNotice admits[2] = {makeNotice(rev0(), 0, "admit", boardOf(A), 9001, "1"),
                                 makeNotice(rev0(), 0, "admit", boardOf(B), 9002, "1")};
    auto take = [&](NetState& st, NetMembers& m, int r) {
        std::string l;
        if (r == 0) m.takeApp(appC, l);
        else if (r <= 3) m.takeVote(votes[r - 1], l);
        else st.applyNotice(admits[r - 4], 1);
    };
    auto show = [](const std::vector<int>& o) {
        std::string s;
        for (int r : o) s += std::to_string(r);
        return s;
    };

    std::vector<int> order = {0, 1, 2, 3, 4, 5};
    int runs = 0, bad = 0;
    std::string firstBad;
    do {
        NetState st;
        NetMembers m(st, isRev);
        for (int r : order) take(st, m, r);
        if (m.status(c) != Status::Member || m.status(d) != Status::None || m.tally(c).yes != 3) {
            if (!bad++) firstBad = show(order);
        }
        runs++;
    } while (std::next_permutation(order.begin(), order.end()));
    CHECK(runs == 720);
    CHECK_MESSAGE(bad == 0, "first order that differs: " << firstBad);

    // The same with the app and vote tables filled to their caps by sealed apps first. The tables are
    // filled once; each order starts from a copy (the state put back, the tally copied).
    NetState st;
    NetMembers base(st, isRev);
    uint32_t seq = 0;
    std::vector<KeyPair> fillers, voters;
    for (size_t i = 0; i < kMaxOpenApps; i++) fillers.push_back(keyOf((uint8_t)(0x60 + i)));
    for (size_t i = 0; i < kMaxOpenApps - 1; i++) voters.push_back(keyOf((uint8_t)(0x70 + i)));
    for (const KeyPair& v : voters) REQUIRE(st.applyNotice(makeNotice(rev0(), 0, "admit", boardOf(v), ++seq, "1"), 1));
    for (size_t i = 0; i < fillers.size(); i++) {
        std::string l;
        REQUIRE(base.takeApp(makeApp(fillers[i], 1, kNow - 1000 + (uint32_t)i), l));
        REQUIRE(st.applyNotice(makeNotice(rev0(), 0, "deny", boardOf(fillers[i]), ++seq, "1"), 1));  // sealed
    }
    voters.push_back(rev0());
    for (const KeyPair& f : fillers)
        for (const KeyPair& v : voters) {
            std::string l;
            REQUIRE(base.takeVote(makeVote(v, boardOf(f), 1, 1, false), l));
        }
    REQUIRE(base.appCount() == kMaxOpenApps);
    REQUIRE(base.voteCount() == kMaxVotes);
    const NetState filled = st;

    // Every order again: a copy costs no signature checks, so each order checks only its six records.
    order = {0, 1, 2, 3, 4, 5};
    runs = bad = 0;
    do {
        st = filled;
        NetMembers m(base);
        for (int r : order) take(st, m, r);
        if (m.status(c) != Status::Member || m.status(d) != Status::None || m.tally(c).yes != 3 ||
            m.appCount() != kMaxOpenApps) {
            if (!bad++) firstBad = show(order);
        }
        runs++;
    } while (std::next_permutation(order.begin(), order.end()));
    CHECK(runs == 720);
    CHECK_MESSAGE(bad == 0, "first order that differs (full tables): " << firstBad);
}

TEST_CASE("vote before its app counts once the app comes") {
    RevKeys rk;
    Tally t;
    const KeyPair A = keyOf(0x51), B = keyOf(0x52), C = keyOf(0x53);
    REQUIRE(t.admit(A));
    REQUIRE(t.admit(B));
    const std::string c = boardOf(C);
    CHECK(t.vote(makeVote(rev0(), c, 1, 1, true)));
    CHECK(t.vote(makeVote(A, c, 1, 1, true)));
    CHECK(t.vote(makeVote(B, c, 1, 1, true)));
    CHECK(t.status(C) == Status::None);
    CHECK(t.m.tally(c).yes == 0);
    CHECK(t.m.voteCount() == 3);
    CHECK(t.app(makeApp(C)));
    CHECK(t.status(C) == Status::Member);
    CHECK(t.m.tally(c).yes == 3);
}

TEST_CASE("second vote replaces the first") {
    RevKeys rk;
    Tally t;
    const KeyPair A = keyOf(0x51), C = keyOf(0x53);
    const std::string c = boardOf(C);
    REQUIRE(t.admit(A));
    REQUIRE(t.app(makeApp(C)));
    NetVote first = makeVote(A, c, 1, 1, true), second = makeVote(A, c, 1, 2, false);
    CHECK(t.vote(first));
    CHECK(t.m.tally(c).yes == 1);
    CHECK(t.vote(second));
    CHECK(t.m.tally(c).yes == 0);
    CHECK(t.m.tally(c).no == 1);
    CHECK(t.m.voteCount() == 1);
    CHECK_FALSE(t.vote(first));  // replayed
    CHECK(t.m.tally(c).yes == 0);
    CHECK(t.m.tally(c).no == 1);
    CHECK(t.m.holds(encode(second)));
    CHECK_FALSE(t.m.holds(encode(first)));
    CHECK_FALSE(t.vote(second));  // the same vote again changes nothing
}

TEST_CASE("equal seq twins: the same one is held whatever order they come in") {
    RevKeys rk;
    const KeyPair A = keyOf(0x51), C = keyOf(0x53);
    const std::string c = boardOf(C);
    NetVote x = makeVote(A, c, 2, 5, true, kNow, "twin one"), y = makeVote(A, c, 2, 5, false, kNow, "twin two");
    NetApp p = makeApp(C, 2, kNow, "first blurb"), q = makeApp(C, 2, kNow, "second blurb");
    Tally t1, t2;
    for (Tally* t : {&t1, &t2}) REQUIRE(t->admit(A));
    CHECK(t1.app(p));
    t1.app(q);
    CHECK(t2.app(q));
    t2.app(p);
    CHECK(t1.m.holds(encode(p)) == t2.m.holds(encode(p)));
    CHECK(t1.m.holds(encode(q)) == t2.m.holds(encode(q)));
    CHECK(t1.m.holds(encode(p)) != t1.m.holds(encode(q)));
    CHECK(t1.m.appCount() == 1);
    CHECK(t1.vote(x));
    t1.vote(y);
    CHECK(t2.vote(y));
    t2.vote(x);
    CHECK(t1.m.holds(encode(x)) == t2.m.holds(encode(x)));
    CHECK(t1.m.holds(encode(y)) == t2.m.holds(encode(y)));
    CHECK(t1.m.holds(encode(x)) != t1.m.holds(encode(y)));
    CHECK(t1.m.voteCount() == 1);
    CHECK(t1.m.tally(c).yes == t2.m.tally(c).yes);
    CHECK(t1.m.tally(c).no == t2.m.tally(c).no);
}

TEST_CASE("yes wins when both counts are reached") {
    RevKeys rk;
    Tally t;
    const KeyPair A = keyOf(0x51), B = keyOf(0x52), C = keyOf(0x53), D = keyOf(0x54), E = keyOf(0x55);
    const std::string c = boardOf(C);
    REQUIRE(t.votesNeeded(2, 2));
    for (const KeyPair* k : {&A, &B, &D, &E}) REQUIRE(t.admit(*k));
    REQUIRE(t.app(makeApp(C)));
    CHECK(t.vote(makeVote(D, c, 1, 1, false)));
    CHECK(t.vote(makeVote(E, c, 1, 1, false)));
    CHECK(t.status(C) == Status::Denied);
    CHECK(t.vote(makeVote(A, c, 1, 1, true)));
    CHECK(t.vote(makeVote(B, c, 1, 1, true)));
    CHECK(t.m.tally(c).yes == 2);
    CHECK(t.m.tally(c).no == 2);
    CHECK(t.status(C) == Status::Member);
}

TEST_CASE("removed voter's votes stop counting") {
    RevKeys rk;
    const KeyPair A = keyOf(0x51), B = keyOf(0x52), C = keyOf(0x53);
    const std::string c = boardOf(C), a = boardOf(A);
    {
        Tally t;
        REQUIRE(t.votesNeeded(2, 3));
        REQUIRE(t.admit(A));
        REQUIRE(t.admit(B));
        REQUIRE(t.app(makeApp(C)));
        REQUIRE(t.vote(makeVote(A, c, 1, 1, true)));
        REQUIRE(t.vote(makeVote(B, c, 1, 1, true)));
        CHECK(t.status(C) == Status::Member);
        REQUIRE(t.notice("veto", a));
        CHECK(t.status(A) == Status::Vetoed);
        CHECK(t.m.tally(c).yes == 1);
        CHECK(t.status(C) == Status::Pending);  // not sealed: it falls back with A's vote gone
        CHECK_FALSE(has(t.m.members(), a));
        CHECK_FALSE(has(t.m.members(), c));
    }
    {
        Tally t;
        REQUIRE(t.votesNeeded(2, 3));
        REQUIRE(t.admit(A));
        REQUIRE(t.admit(B));
        REQUIRE(t.app(makeApp(C)));
        REQUIRE(t.vote(makeVote(A, c, 1, 1, true)));
        REQUIRE(t.vote(makeVote(B, c, 1, 1, true)));
        REQUIRE(t.admit(C));  // sealed
        REQUIRE(t.notice("veto", a));
        CHECK(t.status(C) == Status::Member);
        CHECK(has(t.m.members(), c));
    }
    {  // A ban removes a voter the same way.
        Tally t;
        REQUIRE(t.votesNeeded(2, 3));
        REQUIRE(t.admit(A));
        REQUIRE(t.admit(B));
        REQUIRE(t.app(makeApp(C)));
        REQUIRE(t.vote(makeVote(A, c, 1, 1, true)));
        REQUIRE(t.vote(makeVote(B, c, 1, 1, true)));
        REQUIRE(t.notice("ban", a));
        CHECK(t.status(A) == Status::None);
        CHECK(t.status(C) == Status::Pending);
    }
}

TEST_CASE("veto after admit removes, later admit restores (the tally)") {
    RevKeys rk;
    Tally t;
    const KeyPair C = keyOf(0x53);
    const std::string c = boardOf(C);
    REQUIRE(t.app(makeApp(C)));
    REQUIRE(t.admit(C));
    CHECK(t.status(C) == Status::Member);
    CHECK(has(t.m.members(), c));
    REQUIRE(t.notice("veto", c));
    CHECK(t.status(C) == Status::Vetoed);
    CHECK_FALSE(has(t.m.members(), c));
    CHECK_FALSE(t.app(makeApp(C, 2)));  // a vetoed board's new application is refused
    REQUIRE(t.admit(C));
    CHECK(t.status(C) == Status::Member);
    CHECK(has(t.m.members(), c));
}

TEST_CASE("deny then a new app reopens") {
    RevKeys rk;
    Tally t;
    const KeyPair A = keyOf(0x51), C = keyOf(0x53);
    const std::string c = boardOf(C);
    REQUIRE(t.admit(A));
    REQUIRE(t.app(makeApp(C, 1)));
    REQUIRE(t.notice("deny", c, "1"));
    CHECK(t.status(C) == Status::Denied);
    CHECK(t.vote(makeVote(A, c, 1, 1, true)));  // a vote on the denied app is held, but changes nothing
    CHECK(t.status(C) == Status::Denied);
    REQUIRE(t.app(makeApp(C, 2)));
    CHECK(t.status(C) == Status::Pending);
    CHECK(t.m.tally(c).yes == 0);  // the vote was on the old app
    CHECK_FALSE(t.vote(makeVote(A, c, 1, 2, true)));  // a vote on the replaced app is refused
    CHECK_FALSE(t.app(makeApp(C, 1)));  // the old app comes back: older than the held one
    CHECK(t.status(C) == Status::Pending);
}

TEST_CASE("bootstrap with Rev alone") {
    RevKeys rk;
    const KeyPair C = keyOf(0x53);
    const std::string c = boardOf(C);
    {
        Tally t;
        REQUIRE(t.votesNeeded(1, 3));
        REQUIRE(t.app(makeApp(C)));
        CHECK(t.status(C) == Status::Pending);
        REQUIRE(t.vote(makeVote(rev0(), c, 1, 1, true)));
        CHECK(t.status(C) == Status::Member);
        CHECK(t.m.members() == std::vector<std::string>{revBoard(), c});  // Rev's board first (known as a voter)
    }
    {
        Tally t;  // the default, 3 yes
        REQUIRE(t.app(makeApp(C)));
        REQUIRE(t.vote(makeVote(rev0(), c, 1, 1, true)));
        CHECK(t.status(C) == Status::Pending);
        REQUIRE(t.admit(C));
        CHECK(t.status(C) == Status::Member);
    }
    {  // Rev's board is a member by itself, and may not apply.
        Tally t;
        CHECK(t.m.status(revBoard()) == Status::Member);
        CHECK_FALSE(t.app(makeApp(rev0())));
    }
}

TEST_CASE("seals due on Rev's board") {
    RevKeys rk;
    Tally t;
    const KeyPair C = keyOf(0x53), D = keyOf(0x54);
    const std::string c = boardOf(C), d = boardOf(D);
    REQUIRE(t.votesNeeded(1, 1));
    REQUIRE(t.app(makeApp(C)));
    REQUIRE(t.app(makeApp(D)));
    CHECK(t.m.sealsDue().empty());
    REQUIRE(t.vote(makeVote(rev0(), c, 1, 1, true)));
    REQUIRE(t.vote(makeVote(rev0(), d, 1, 1, false)));
    CHECK(t.status(C) == Status::Member);
    CHECK(t.status(D) == Status::Denied);
    auto due = t.m.sealsDue();
    REQUIRE(due.size() == 2);
    std::vector<std::pair<std::string, Status>> want = {{c, Status::Member}, {d, Status::Denied}};
    std::sort(want.begin(), want.end());
    CHECK(due == want);
    REQUIRE(t.admit(C));
    due = t.m.sealsDue();
    REQUIRE(due.size() == 1);
    CHECK(due[0].first == d);
    REQUIRE(t.notice("deny", d, "1"));
    CHECK(t.m.sealsDue().empty());
}

TEST_CASE("caps hold") {
    RevKeys rk;
    Tally t;
    REQUIRE(t.votesNeeded(kMaxVotesNeeded, kMaxVotesNeeded));  // 16 votes each leave every app Pending
    std::vector<KeyPair> apps, voters;
    for (size_t i = 0; i < kMaxOpenApps + 1; i++) apps.push_back(keyOf((uint8_t)(0x60 + i)));
    for (size_t i = 0; i < kMaxOpenApps + 1; i++) voters.push_back(keyOf((uint8_t)(0x80 + i)));
    for (const KeyPair& v : voters) REQUIRE(t.admit(v));
    for (size_t i = 0; i < kMaxOpenApps; i++) REQUIRE(t.app(makeApp(apps[i], 1, kNow + (uint32_t)i)));
    CHECK(t.m.appCount() == kMaxOpenApps);
    // The 17th open app is refused while all 16 are pending.
    CHECK_FALSE(t.app(makeApp(apps[kMaxOpenApps])));
    CHECK(t.m.appCount() == kMaxOpenApps);
    uint32_t off;
    CHECK_FALSE(t.m.heldApp(boardOf(apps[kMaxOpenApps]), off));
    // 16 voters on 16 apps fill the 256 votes.
    for (size_t a = 0; a < kMaxOpenApps; a++)
        for (size_t v = 0; v < kMaxOpenApps; v++) REQUIRE(t.vote(makeVote(voters[v], boardOf(apps[a]), 1, 1, true)));
    CHECK(t.m.voteCount() == kMaxVotes);
    for (size_t a = 0; a < kMaxOpenApps; a++) REQUIRE(t.m.status(boardOf(apps[a])) == Status::Pending);
    // Vote 257 is refused: no decided app frees room.
    NetVote extra = makeVote(voters[kMaxOpenApps], boardOf(apps[1]), 1, 1, true);
    CHECK_FALSE(t.vote(extra));
    CHECK(t.m.voteCount() == kMaxVotes);
    CHECK(t.m.tally(boardOf(apps[1])).yes == kMaxOpenApps);
    // Once Rev seals one app, a vote on it makes room for the new one.
    REQUIRE(t.notice("deny", boardOf(apps[0]), "1"));
    CHECK(t.vote(extra));
    CHECK(t.m.voteCount() == kMaxVotes);
    CHECK(t.m.tally(boardOf(apps[1])).yes == kMaxOpenApps + 1);
}

TEST_CASE("caps never evict what the tally needs") {
    RevKeys rk;
    Tally t;
    std::vector<KeyPair> apps;
    for (size_t i = 0; i < kMaxOpenApps + 1; i++) apps.push_back(keyOf((uint8_t)(0x60 + i)));
    for (size_t i = 0; i < kMaxOpenApps; i++) REQUIRE(t.app(makeApp(apps[i], 1, kNow + (uint32_t)i)));
    NetApp late = makeApp(apps[kMaxOpenApps]);
    CHECK_FALSE(t.app(late));  // 16 unsealed pending apps: refused
    for (size_t i = 0; i < kMaxOpenApps; i++) CHECK(t.m.status(boardOf(apps[i])) == Status::Pending);
    // One of them sealed (denied): it goes, and the new one is taken.
    REQUIRE(t.notice("deny", boardOf(apps[5]), "1"));
    CHECK(t.app(late));
    CHECK(t.m.appCount() == kMaxOpenApps);
    uint32_t off;
    CHECK_FALSE(t.m.heldApp(boardOf(apps[5]), off));
    CHECK(t.m.heldApp(boardOf(apps[kMaxOpenApps]), off));
    CHECK(t.m.status(boardOf(apps[kMaxOpenApps])) == Status::Pending);
    for (size_t i = 0; i < kMaxOpenApps; i++)
        if (i != 5) CHECK(t.m.heldApp(boardOf(apps[i]), off));
    // A 65th member seal target is refused (and so cannot be counted on to free room).
    for (uint32_t i = (uint32_t)t.st.memberNoticeCount(); i < kMaxMemberNotices; i++) REQUIRE(t.notice("admit", idOf(500 + i), "1"));
    CHECK(t.st.memberNoticeCount() == kMaxMemberNotices);
    CHECK_FALSE(t.notice("deny", boardOf(apps[6]), "1"));
    CHECK(t.m.status(boardOf(apps[6])) == Status::Pending);
}

TEST_CASE("a full app table with a banned pending app still refuses a new one") {
    RevKeys rk;
    Tally t;
    std::vector<KeyPair> apps;
    for (size_t i = 0; i < kMaxOpenApps + 1; i++) apps.push_back(keyOf((uint8_t)(0x60 + i)));
    for (size_t i = 0; i < kMaxOpenApps; i++) REQUIRE(t.app(makeApp(apps[i], 1, kNow + (uint32_t)i)));
    REQUIRE(t.notice("ban", boardOf(apps[0])));  // a ban is no seal: a lift brings the app back
    CHECK(t.m.status(boardOf(apps[0])) == Status::None);
    CHECK_FALSE(t.app(makeApp(apps[kMaxOpenApps])));
    uint32_t off;
    CHECK(t.m.heldApp(boardOf(apps[0]), off));
    REQUIRE(t.notice("lift", boardOf(apps[0])));
    CHECK(t.m.status(boardOf(apps[0])) == Status::Pending);
}

TEST_CASE("votes check without cards") {
    RevKeys rk;
    Tally t;
    const KeyPair A = keyOf(0x51), C = keyOf(0x53);
    const std::string c = boardOf(C);
    REQUIRE(t.st.cardCount() == 0);
    REQUIRE(t.app(makeApp(C)));
    NetVote v = makeVote(A, c, 1, 1, true);
    CHECK(t.vote(v));  // no card for A, and A is no member yet: taken all the same
    CHECK(t.m.holds(encode(v)));
    CHECK(t.m.tally(c).yes == 0);
    REQUIRE(t.admit(A));
    CHECK(t.m.tally(c).yes == 1);
    // save keeps exactly the held lines; a fresh tally loads them back.
    std::string saved = t.m.save(t.file);
    CHECK(saved == t.file);
    NetMembers again(t.st, [](const std::string& b) { return b == revBoard(); });
    again.load(saved);
    CHECK(again.appCount() == 1);
    CHECK(again.voteCount() == 1);
    CHECK(again.holds(encode(v)));
    CHECK(again.tally(c).yes == 1);
    CHECK(again.status(c) == Status::Pending);
    // A forged vote (a byte changed) is not loaded.
    std::string forged = saved;
    size_t at = forged.find("\x1fy\x1f");
    REQUIRE(at != std::string::npos);
    forged[at + 1] = 'n';
    NetMembers third(t.st, [](const std::string& b) { return b == revBoard(); });
    third.load(forged);
    CHECK(third.voteCount() == 0);
    CHECK(third.appCount() == 1);
}

TEST_CASE("an outsider's vote still counts once it is admitted, whatever outsider votes come later") {
    RevKeys rk;
    Tally t;
    const KeyPair A = keyOf(0x51), C = keyOf(0x53), J = keyOf(0x5F);
    const std::string c = boardOf(C);
    REQUIRE(t.app(makeApp(C)));
    REQUIRE(t.vote(makeVote(A, c, 1, 1, true, kNow)));  // an outsider when it came
    REQUIRE(t.admit(A));
    CHECK(t.m.tally(c).yes == 1);
    // 40 later dated votes from a board that is no member, on applications not held: the outsider pool
    // keeps 32, the oldest outsider vote going first, never one the tally counts.
    for (uint32_t i = 0; i < 40; i++) t.vote(makeVote(J, idOf(0x1000 + i), 1, 1, true, kNow + 10 + i));
    CHECK(t.m.voteCount() == 1 + kMaxOutsiderVotes);
    CHECK(t.m.tally(c).yes == 1);
    CHECK(t.m.holds(encode(makeVote(A, c, 1, 1, true, kNow))));
    // Saved and loaded in any order, the same.
    NetMembers again(t.st, [](const std::string& b) { return b == revBoard(); });
    again.load(t.m.save(t.file));
    CHECK(again.voteCount() == 1 + kMaxOutsiderVotes);
    CHECK(again.tally(c).yes == 1);
}

TEST_CASE("a shuffled save and load keeps a chain of more than 32 votes") {
    RevKeys rk;
    Tally t;
    // Seeds: Rev's board and two admitted boards. Each applicant is voted in by the three before it, so
    // most votes come from boards that are members only through the chain.
    std::vector<KeyPair> chain = {rev0(), keyOf(0x51), keyOf(0x52)};
    REQUIRE(t.admit(chain[1]));
    REQUIRE(t.admit(chain[2]));
    const size_t n = 15;
    for (size_t i = 0; i < n; i++) chain.push_back(keyOf((uint8_t)(0x60 + i)));
    size_t votes = 0;
    for (size_t i = 3; i < chain.size(); i++) {
        REQUIRE(t.app(makeApp(chain[i])));
        for (size_t j = i - 3; j < i; j++) {
            REQUIRE(t.vote(makeVote(chain[j], boardOf(chain[i]), 1, 1, true)));
            votes++;
        }
    }
    REQUIRE(votes == 45);
    for (size_t i = 3; i < chain.size(); i++) REQUIRE(t.status(chain[i]) == Status::Member);
    REQUIRE(t.m.voteCount() == votes);

    std::string saved = t.m.save(t.file);
    auto ls = lines(saved);
    std::mt19937 rng(12345);
    for (int round = 0; round < 5; round++) {
        std::shuffle(ls.begin(), ls.end(), rng);
        std::string text;
        for (const auto& l : ls) text += l + "\n";
        NetMembers again(t.st, [](const std::string& b) { return b == revBoard(); });
        again.load(text);
        CHECK(again.appCount() == n);
        CHECK(again.voteCount() == votes);
        size_t members = 0;
        for (size_t i = 3; i < chain.size(); i++) members += again.status(boardOf(chain[i])) == Status::Member;
        CHECK(members == n);
        // Loaded a line at a time with offsets, the same.
        NetMembers lined(t.st, [](const std::string& b) { return b == revBoard(); });
        lined.beginLoad();
        uint32_t off = 0;
        for (const auto& l : ls) {
            lined.loadLine(l, off);
            off += (uint32_t)l.size() + 1;
        }
        lined.endLoad();
        CHECK(lined.voteCount() == votes);
        CHECK(lined.offsets().size() == n + votes);
        CHECK(lined.status(boardOf(chain.back())) == Status::Member);
    }
}

TEST_CASE("a load passes over junk and a torn last line") {
    RevKeys rk;
    Tally t;
    const KeyPair A = keyOf(0x51), C = keyOf(0x53), D = keyOf(0x54);
    REQUIRE(t.admit(A));
    REQUIRE(t.app(makeApp(C)));
    REQUIRE(t.vote(makeVote(A, boardOf(C), 1, 1, true)));
    std::string torn = encode(makeApp(D));
    torn.resize(torn.size() / 2);
    std::string text = "junk line\nA\x1fnot\x1f" "an app\n\n" + t.file + "V\x1f" "broken\n" + torn;
    NetMembers again(t.st, [](const std::string& b) { return b == revBoard(); });
    again.load(text);
    CHECK(again.appCount() == 1);
    CHECK(again.voteCount() == 1);
    CHECK(again.tally(boardOf(C)).yes == 1);
    // save drops what is not held: the junk, the torn line and duplicates.
    CHECK(again.save(text + t.file) == t.file);
}

