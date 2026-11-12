// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// Sysop Net state: Rev's notices by seq, the rotate chain, card caps and pins, seen posts and ingress.
#include "net_testutil.h"

using namespace nettest;

namespace {
const std::string X = "0123456789abcdef";  // a board id
std::string postId(uint32_t n) { return "fedcba9876543210-" + std::to_string(n); }

// Applies the lines of a snapshot (or save) to a state, as a joining board's session does.
void applyLines(NetState& s, const std::vector<std::string>& ls, uint32_t day) {
    for (const auto& l : ls) {
        if (l.empty()) continue;
        if (l[0] == 'C') {
            NetCard c;
            if (decode(l, c)) s.addCard(c);
        } else if (l[0] == 'N') {
            NetNotice n;
            if (decode(l, n)) s.applyNotice(n, day);
        }
    }
}
}  // namespace

TEST_CASE("notices apply by sequence not arrival") {
    RevKeys rk;
    NetState s;
    CHECK(s.applyNotice(makeNotice(rev0(), 0, "ban", X, 5), 100));
    CHECK(s.banned(X));
    CHECK_FALSE(s.applyNotice(makeNotice(rev0(), 0, "lift", X, 4), 100));  // older than the ban
    CHECK(s.banned(X));
    CHECK(s.applyNotice(makeNotice(rev0(), 0, "lift", X, 6), 100));
    CHECK_FALSE(s.banned(X));
    CHECK_FALSE(s.applyNotice(makeNotice(rev0(), 0, "ban", X, 5), 100));  // the ban replayed
    CHECK_FALSE(s.banned(X));
    CHECK(s.highSeq() == 6);

    // Signed by a key that is not Rev's, under either slot.
    CHECK_FALSE(s.applyNotice(makeNotice(keyOf(0x55), 0, "ban", X, 9), 100));
    CHECK_FALSE(s.applyNotice(makeNotice(keyOf(0x55), 1, "ban", X, 9), 100));
    // Rev's key 0 claiming to be slot 1.
    CHECK_FALSE(s.applyNotice(makeNotice(rev0(), 1, "ban", X, 9), 100));
    CHECK_FALSE(s.banned(X));
    // Rev's backup key in its own slot.
    CHECK(s.applyNotice(makeNotice(rev1(), 1, "ban", X, 9), 100));
    CHECK(s.banned(X));
}

TEST_CASE("notices are grouped by target shape") {
    RevKeys rk;
    NetState s;
    // a post target and a board target never meet
    CHECK(s.applyNotice(makeNotice(rev0(), 0, "hold", postId(1), 3), 1));
    CHECK(s.applyNotice(makeNotice(rev0(), 0, "ban", X, 2), 1));  // lower seq, other group: fine
    CHECK(s.held(postId(1)));
    CHECK(s.banned(X));
    CHECK(noticeGroup(makeNotice(rev0(), 0, "hold", postId(1), 3)) == "post");
    CHECK(noticeGroup(makeNotice(rev0(), 0, "ban", X, 3)) == "board");
    CHECK(noticeGroup(makeNotice(rev0(), 0, "limits", "net", 3, "posts=1,bytes=2")) == "limits");
    CHECK(noticeGroup(makeNotice(rev0(), 0, "rotate", "1", 3, pubOf(keyOf(9)))) == "rotate");
    // wrong shapes
    CHECK_FALSE(s.applyNotice(makeNotice(rev0(), 0, "ban", postId(2), 4), 1));   // ban needs a board
    CHECK_FALSE(s.applyNotice(makeNotice(rev0(), 0, "hold", X, 4), 1));          // hold needs a post
    CHECK_FALSE(s.applyNotice(makeNotice(rev0(), 0, "limits", X, 4, "posts=1,bytes=2"), 1));
    CHECK_FALSE(s.applyNotice(makeNotice(rev0(), 0, "limits", "net", 4, "posts=01,bytes=2"), 1));
    CHECK_FALSE(s.applyNotice(makeNotice(rev0(), 0, "limits", "net", 4, "bytes=2"), 1));
    CHECK(s.applyNotice(makeNotice(rev0(), 0, "limits", "net", 4, "posts=3,bytes=100000"), 1));
    CHECK(s.postsPerDay() == 3);
    CHECK(s.bytesPerDay() == 100000);
}

TEST_CASE("a kill is final") {
    RevKeys rk;
    NetState s;
    CHECK(s.applyNotice(makeNotice(rev0(), 0, "hold", postId(1), 1), 1));
    CHECK(s.held(postId(1)));
    CHECK(s.applyNotice(makeNotice(rev0(), 0, "lift", postId(1), 2), 1));
    CHECK_FALSE(s.held(postId(1)));
    CHECK(s.applyNotice(makeNotice(rev0(), 0, "kill", postId(1), 3), 1));
    CHECK(s.killed(postId(1)));
    CHECK_FALSE(s.applyNotice(makeNotice(rev0(), 0, "lift", postId(1), 4), 1));
    CHECK_FALSE(s.applyNotice(makeNotice(rev0(), 0, "hold", postId(1), 5), 1));
    CHECK(s.killed(postId(1)));
}

TEST_CASE("rotate replaces the other key") {
    RevKeys rk;
    NetState s;
    KeyPair fresh = keyOf(0xB1);
    // A rotate is signed by one key and replaces the other slot.
    CHECK_FALSE(s.applyNotice(makeNotice(rev0(), 0, "rotate", "0", 10, pubOf(fresh)), 1));  // own slot
    CHECK_FALSE(s.applyNotice(makeNotice(rev0(), 0, "rotate", "1", 10, pubOf(rev0())), 1));  // both slots the same
    CHECK(s.applyNotice(makeNotice(rev0(), 0, "rotate", "1", 10, pubOf(fresh)), 1));
    CHECK(revKey(1) == pubOf(fresh));
    CHECK(revKey(0) == pubOf(rev0()));
    // The old key 1 above the rotate's seq is out.
    CHECK_FALSE(s.applyNotice(makeNotice(rev1(), 1, "ban", X, 11), 1));
    CHECK_FALSE(s.banned(X));
    // Below the rotate's seq the old key 1 was still in force.
    CHECK(s.applyNotice(makeNotice(rev1(), 1, "ban", X, 9), 1));
    CHECK(s.banned(X));
    // The new key 1 works above it.
    CHECK(s.applyNotice(makeNotice(fresh, 1, "lift", X, 12), 1));
    CHECK_FALSE(s.banned(X));
}

TEST_CASE("a fresh state learns rotated keys from a snapshot") {
    KeyPair fresh = keyOf(0xB2);
    std::vector<std::string> snap;
    {
        RevKeys rk;
        NetState a;
        REQUIRE(a.applyNotice(makeNotice(rev0(), 0, "rotate", "1", 10, pubOf(fresh)), 1));
        REQUIRE(a.applyNotice(makeNotice(fresh, 1, "ban", X, 12), 1));  // only the new key vouches for this
        REQUIRE(a.banned(X));
        snap = a.snapshot();
        REQUIRE(snap.size() == 2);
        CHECK(snap[0][0] == 'N');  // the rotate chain comes first
    }
    RevKeys rk;  // a joining board starts from the built-in keys
    REQUIRE(revKey(1) == pubOf(rev1()));
    NetState b;
    applyLines(b, snap, 1);
    CHECK(b.banned(X));
    CHECK(revKey(1) == pubOf(fresh));
    CHECK(b.highSeq() == 12);

    // In the wrong order the ban is refused (the keys at its seq are not known yet).
    RevKeys rk2;
    NetState c;
    applyLines(c, {snap[1], snap[0]}, 1);
    CHECK_FALSE(c.banned(X));
}

TEST_CASE("a late rotate re-verifies and drops later notices it invalidates") {
    RevKeys rk;
    NetState s;
    KeyPair fresh = keyOf(0xB3);
    const std::string Y = "1111111111111111", Z = "2222222222222222";
    CHECK(s.applyNotice(makeNotice(rev1(), 1, "ban", X, 20), 1));  // the old key 1, before the rotate is known
    CHECK(s.applyNotice(makeNotice(rev1(), 1, "ban", Y, 5), 1));   // the old key 1 below the rotate: stays good
    CHECK(s.applyNotice(makeNotice(rev0(), 0, "ban", Z, 21), 1));  // key 0 is untouched by the rotate
    CHECK(s.banned(X));
    // The rotate (seq 10) arrives late and is inserted into the chain.
    CHECK(s.applyNotice(makeNotice(rev0(), 0, "rotate", "1", 10, pubOf(fresh)), 1));
    CHECK_FALSE(s.banned(X));  // signed by a key rotated out at seq 10
    CHECK(s.banned(Y));
    CHECK(s.banned(Z));
    CHECK(s.highSeq() == 21);
    CHECK(s.noticeLine("board", X).empty());
    // Reloading what is saved gives the same answer.
    std::string saved = s.save();
    NetState t;
    t.load(saved, 1);
    CHECK_FALSE(t.banned(X));
    CHECK(t.banned(Y));
    CHECK(t.banned(Z));
    CHECK(revKey(1) == pubOf(fresh));
}

TEST_CASE("the rotate chain caps at 16") {
    RevKeys rk;
    NetState s;
    for (uint32_t i = 0; i < kMaxRotates; i++)
        REQUIRE(s.applyNotice(makeNotice(rev0(), 0, "rotate", "1", 100 + i, pubOf(keyOf((uint8_t)(0xC0 + i)))), 1));
    CHECK_FALSE(s.applyNotice(makeNotice(rev0(), 0, "rotate", "1", 200, pubOf(keyOf(0xD0))), 1));
    CHECK(revKey(1) == pubOf(keyOf((uint8_t)(0xC0 + kMaxRotates - 1))));
    // rotateLine finds a rotate in the chain by its seq.
    CHECK(s.rotateLine(100).size() > 0);
    CHECK(s.rotateLine(99).empty());
}

TEST_CASE("post notices expire 150 days after they were applied; usedSeq survives") {
    RevKeys rk;
    NetState s;
    CHECK(s.applyNotice(makeNotice(rev0(), 0, "hold", postId(1), 30), 100));
    CHECK(s.applyNotice(makeNotice(rev0(), 0, "ban", X, 7), 100));
    s.prune(250);
    CHECK(s.held(postId(1)));
    s.prune(251);
    CHECK_FALSE(s.held(postId(1)));
    CHECK(s.banned(X));  // board notices do not expire
    CHECK(s.usedSeq() == 30);
    std::string saved = s.save();
    CHECK(saved.compare(0, 5, "H 30\n") == 0);
    NetState t;
    t.load(saved, 251);
    CHECK(t.usedSeq() == 30);
    CHECK(t.banned(X));
    // the day a notice was applied is saved with it
    NetState u;
    NetState v;
    REQUIRE(u.applyNotice(makeNotice(rev0(), 0, "hold", postId(2), 40), 100));
    v.load(u.save(), 200);
    CHECK(v.held(postId(2)));
    v.prune(251);
    CHECK_FALSE(v.held(postId(2)));
}

TEST_CASE("notice caps: 64 post notices, 64 board notices with lifts going first") {
    RevKeys rk;
    NetState s;
    for (uint32_t i = 1; i <= 70; i++) REQUIRE(s.applyNotice(makeNotice(rev0(), 0, "hold", postId(i), i), 1));
    size_t held = 0;
    for (uint32_t i = 1; i <= 70; i++) held += s.held(postId(i)) ? 1 : 0;
    CHECK(held == kMaxPostNotices);
    CHECK_FALSE(s.held(postId(1)));  // the oldest applied went first
    CHECK(s.held(postId(70)));

    NetState b;
    auto board = [](uint32_t i) {
        char h[17];
        std::snprintf(h, sizeof h, "%016x", i);
        return std::string(h);
    };
    // 10 lifts, then 60 bans: the cap of 64 drops the lifts, never a ban
    for (uint32_t i = 1; i <= 10; i++) REQUIRE(b.applyNotice(makeNotice(rev0(), 0, "lift", board(i), i), 1));
    for (uint32_t i = 11; i <= 70; i++) REQUIRE(b.applyNotice(makeNotice(rev0(), 0, "ban", board(i), i), 1));
    size_t bans = 0, lifts = 0;
    for (uint32_t i = 1; i <= 70; i++) {
        bans += b.banned(board(i)) ? 1 : 0;
        lifts += b.noticeLine("board", board(i)).empty() || b.banned(board(i)) ? 0 : 1;
    }
    CHECK(bans == 60);
    CHECK(lifts == 4);
}

TEST_CASE("cards: newer seq replaces, banned boards' cards dropped") {
    RevKeys rk;
    NetState s;
    KeyPair k = keyOf(0x41);
    CHECK(s.addCard(makeCard(k, "One", 2)));
    CHECK_FALSE(s.addCard(makeCard(k, "Old", 1)));
    CHECK_FALSE(s.addCard(makeCard(k, "Same", 2)));
    CHECK(s.addCard(makeCard(k, "Two", 3)));
    CHECK(s.card(boardOf(k))->name == "Two");
    NetCard forged = makeCard(k, "Forged", 9);
    forged.name = "Changed";
    CHECK_FALSE(s.addCard(forged));
    CHECK(s.applyNotice(makeNotice(rev0(), 0, "ban", boardOf(k), 1), 1));
    CHECK(s.card(boardOf(k)) == nullptr);
    CHECK_FALSE(s.addCard(makeCard(k, "Back", 10)));
}

TEST_CASE("card lockout: self-signed cards never evict a Rev-key card or the own card") {
    RevKeys rk;
    NetState s;
    KeyPair own = keyOf(0x42);
    s.setPinned([&](const std::string& b) { return b == boardOf(own); });
    REQUIRE(s.addCard(makeCard(rev0(), "Rev")));
    REQUIRE(s.addCard(makeCard(own, "Own")));
    for (int i = 0; i < 40; i++) s.addCard(makeCard(keyOf((uint8_t)(0x60 + i)), "Spam"));
    CHECK(s.cardCount() <= kNetMaxCards);
    CHECK(s.card(boardOf(rev0())) != nullptr);
    CHECK(s.card(boardOf(own)) != nullptr);
    CHECK(s.card(boardOf(keyOf(0x60 + 39))) != nullptr);  // the newest went in
    CHECK(s.card(boardOf(keyOf(0x60))) == nullptr);        // the oldest unpinned made room

    // Every slot pinned: a new card is refused rather than evicting one.
    NetState full;
    full.setPinned([](const std::string&) { return true; });
    for (int i = 0; i < (int)kNetMaxCards; i++) REQUIRE(full.addCard(makeCard(keyOf((uint8_t)(0x10 + i)), "P")));
    CHECK_FALSE(full.addCard(makeCard(keyOf(0xEE), "New")));
    CHECK(full.cardCount() == kNetMaxCards);
}

TEST_CASE("seen prunes after 120 days") {
    NetSeen s;
    std::string id = "0123456789abcdef-1";
    CHECK(s.add(id, 10));
    CHECK_FALSE(s.add(id, 10));
    s.prune(129);
    CHECK(s.has(id));
    s.prune(130);
    CHECK(s.has(id));
    s.prune(131);
    CHECK_FALSE(s.has(id));
    CHECK_FALSE(s.add("not-a-post-id", 1));
    CHECK_FALSE(s.add("0123456789ABCDEF-1", 1));
    CHECK_FALSE(s.add("0123456789abcdef-01", 1));
}

TEST_CASE("per-origin seen: window, jumps, out of order, save and load") {
    NetSeen s;
    const std::string b = "0123456789abcdef-", o = "fedcba9876543210-";
    CHECK(s.add(b + "100", 1));
    CHECK(s.add(b + "300", 1));  // a jump greater than 64
    CHECK(s.has(b + "300"));
    CHECK(s.has(b + "100"));      // far below the window: counted as seen
    CHECK(s.has(b + "235"));        // 65 below: past the window, counted as seen
    CHECK_FALSE(s.has(b + "236"));  // 64 below: the window's last bit, not seen yet
    CHECK_FALSE(s.has(b + "237"));
    CHECK_FALSE(s.has(b + "299"));
    CHECK(s.add(b + "290", 1));   // out of order inside the window
    CHECK(s.has(b + "290"));
    CHECK_FALSE(s.add(b + "290", 1));
    CHECK_FALSE(s.has(b + "291"));
    CHECK(s.add(b + "301", 1));   // a step of one keeps the window
    CHECK(s.has(b + "290"));
    CHECK(s.has(b + "300"));
    CHECK_FALSE(s.has(b + "302"));
    CHECK(s.add(b + "365", 1));   // exactly 64 up: the old high sits at the window's last bit
    CHECK(s.has(b + "301"));
    CHECK_FALSE(s.has(b + "302"));
    CHECK(s.add(o + "5", 2));     // origins are separate
    CHECK_FALSE(s.has(o + "4"));
    CHECK(s.size() == 2);

    NetSeen t;
    t.load(s.save());
    CHECK(t.size() == 2);
    for (uint32_t i = 200; i <= 370; i++) CHECK(t.has(b + std::to_string(i)) == s.has(b + std::to_string(i)));
    CHECK(t.has(o + "5"));
    CHECK_FALSE(t.has(o + "6"));
    // the day travels too
    t.prune(1 + kSeenDays + 1);
    CHECK_FALSE(t.has(b + "365"));
    CHECK(t.has(o + "5"));
}

TEST_CASE("seen keeps 48 origins, least recently used goes") {
    NetSeen s;
    auto id = [](int i) {
        char h[17];
        std::snprintf(h, sizeof h, "%016x", i + 1);
        return std::string(h) + "-1";
    };
    for (int i = 0; i < (int)kNetMaxSeenBoards; i++) REQUIRE(s.add(id(i), 1));
    REQUIRE(s.add(id(0).substr(0, 17) + "2", 1));  // touch origin 0 again
    REQUIRE(s.add(id(100), 1));
    CHECK(s.size() == kNetMaxSeenBoards);
    CHECK(s.has(id(0)));
    CHECK_FALSE(s.has(id(1)));  // the least recently used went
    CHECK(s.has(id(100)));
}

TEST_CASE("ingress limit drops excess") {
    Ingress in;
    in.setLimits(3, 100000);
    CHECK(in.allow(X, 10, 50));
    CHECK(in.allow(X, 10, 50));
    CHECK(in.allow(X, 10, 50));
    CHECK_FALSE(in.allow(X, 10, 50));  // the 4th that day
    CHECK(in.allow("1111111111111111", 10, 50));  // other boards have their own count
    CHECK(in.warnOnce(X, 50));
    CHECK_FALSE(in.warnOnce(X, 50));
    CHECK(in.allow(X, 10, 51));  // the next day
    CHECK(in.warnOnce(X, 51));

    Ingress bytes;
    bytes.setLimits(100, 1000);
    CHECK(bytes.allow(X, 600, 1));
    CHECK_FALSE(bytes.allow(X, 500, 1));
    CHECK(bytes.allow(X, 400, 1));
    CHECK_FALSE(bytes.allow(X, 1, 1));
}

TEST_CASE("ingress counts 48 boards per day, the least recently used making room") {
    Ingress in;
    in.setLimits(2, 1000);
    auto b = [](int i) {
        char h[17];
        std::snprintf(h, sizeof h, "%016x", i + 1);
        return std::string(h);
    };
    for (int i = 0; i < (int)kNetMaxIngressBoards; i++) REQUIRE(in.allow(b(i), 1, 7));
    REQUIRE(in.allow(b(0), 1, 7));        // board 0 is at its limit and now the most recently used
    CHECK_FALSE(in.allow(b(0), 1, 7));
    CHECK(in.allow(b(100), 1, 7));        // a 49th board that day is counted: board 1 (used longest ago) made room
    CHECK(in.allow(b(101), 1, 7));        // and so on: a flood of boards never locks the others out
    CHECK_FALSE(in.allow(b(0), 1, 7));    // board 0 kept its count
    CHECK(in.allow(b(1), 1, 7));          // board 1 starts a fresh count
    CHECK(in.allow(b(100), 1, 8));        // the next day
}

TEST_CASE("state save and load verify every record again") {
    RevKeys rk;
    NetState s;
    KeyPair k = keyOf(0x44);
    REQUIRE(s.addCard(makeCard(k, "Saved")));
    REQUIRE(s.applyNotice(makeNotice(rev0(), 0, "ban", X, 3), 5));
    REQUIRE(s.applyNotice(makeNotice(rev0(), 0, "limits", "net", 4, "posts=7,bytes=9000"), 5));
    std::string saved = s.save();
    NetState t;
    t.load(saved + "junk line\n5 N\x1Fnonsense\n", 5);
    CHECK(t.card(boardOf(k)) != nullptr);
    CHECK(t.banned(X));
    CHECK(t.postsPerDay() == 7);
    CHECK(t.highSeq() == 4);
    // A notice whose signature was altered on the card is not loaded.
    std::string bad = saved;
    size_t at = bad.find("ban");
    REQUIRE(at != std::string::npos);
    size_t sigAt = bad.find('\n', at) - 1;
    bad[sigAt] = bad[sigAt] == '0' ? '1' : '0';
    NetState u;
    u.load(bad, 5);
    CHECK_FALSE(u.banned(X));
}
