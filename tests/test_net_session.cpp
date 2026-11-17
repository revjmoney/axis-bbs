// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// NetSession: two boards syncing in memory, the handshake's checks, and the session's limits.
#include <algorithm>

#include "net_testutil.h"

using namespace nettest;
using Took = NetCore::Took;

namespace {
// The text has a line that is exactly l ("uhistory 0" is no "history 0" line).
bool hasLine(const std::string& text, const std::string& l) {
    for (const auto& x : lines(text))
        if (x == l) return true;
    return false;
}

std::string lastLine(const std::string& out, const std::string& prefix) {
    std::string found;
    for (const auto& l : lines(out))
        if (l.compare(0, prefix.size(), prefix) == 0) found = l;
    return found;
}
}  // namespace

TEST_CASE("session syncs two cores") {
    RevKeys rk;
    Node a(0x91), b(0x92);
    a.core.addLinkPeer(b.board, true);  // linked before posting, so the posts go as E lines
    std::string id1 = netPostId(a.post("sn_support", "first")), id2 = netPostId(a.post("sn_bugs", "second"));
    REQUIRE(!b.post("sn_support", "b's own").empty());  // B is not empty: it asks for no history
    NetSession da(a.core, a.card, a.key, nonce(1), true, b.board);
    NetSession db(b.core, b.card, b.key, nonce(2), false, "");
    uint32_t ms = 0;
    run(da, db, ms);
    CHECK_MESSAGE(da.done(), da.why());
    CHECK_MESSAGE(db.done(), db.why());
    CHECK(da.peer() == b.board);
    CHECK(db.peer() == a.board);
    CHECK(da.postsSent() == 2);
    CHECK(db.postsTaken() == 2);
    CHECK(b.core.seen().has(id1));
    CHECK(b.core.seen().has(id2));
    CHECK(b.ids("sn_bugs").size() == 1);
    CHECK(a.core.pendingFor(b.board, 8).empty());  // both queues end empty
    CHECK(b.core.pendingFor(a.board, 8).empty());
    REQUIRE(b.core.state().card(a.board) != nullptr);  // B's state has A's card
    CHECK(a.core.state().card(b.board) != nullptr);
    CHECK(b.core.isLinkPeer(a.board));
}

TEST_CASE("new board gets state snapshot and history") {
    RevKeys rk;
    const std::string X = "0123456789abcdef";
    Node a(0x93), b(0x94);
    REQUIRE(a.core.originateNotice(makeNotice(rev0(), 0, "ban", X, 7)));
    REQUIRE(a.core.originateNotice(makeNotice(rev0(), 0, "limits", "net", 3, "posts=1,bytes=100000")));
    REQUIRE(!a.post("sn_support", "one").empty());
    REQUIRE(!a.post("sn_support", "two").empty());
    REQUIRE(!a.post("sn_bugs", "three").empty());  // more than B's ingress limit allows: history skips it
    REQUIRE(b.core.state().highSeq() == 0);
    NetSession da(a.core, a.card, a.key, nonce(1), true, b.board);
    NetSession db(b.core, b.card, b.key, nonce(2), false, "");
    uint32_t ms = 0;
    run(da, db, ms);
    CHECK_MESSAGE(da.done(), da.why());
    CHECK_MESSAGE(db.done(), db.why());
    CHECK(b.core.state().banned(X));
    CHECK(b.core.state().highSeq() == 7);
    CHECK(b.core.state().postsPerDay() == 1);
    CHECK(db.postsTaken() == 3);  // history, as H lines
    CHECK(b.ids().size() == 2);
    CHECK(b.ids("sn_bugs").size() == 1);
}

TEST_CASE("H lines without our WANT fail the session") {
    RevKeys rk;
    Node b(0x95);
    REQUIRE(!b.post().empty());  // B holds posts, so it never sends WANT
    NetSession s(b.core, b.card, b.key, nonce(2), false, "");
    RawDialer r(0x96);
    std::string after = r.handshake(s);
    CHECK(countPrefix(after, "PROOF ") == 1);
    REQUIRE(s.proven());
    s.tick(0, true);
    CHECK(countPrefix(s.takeOut(), "WANT") == 0);
    RawDialer::say(s, "SEQ 0");
    RawDialer::say(s, "H " + encode(makePost(r.key, 1, kNow)));
    CHECK(s.failed());
    CHECK(std::string(s.why()) == "bad line");
    CHECK(countPrefix(s.takeOut(), "BYE bad") == 1);
    CHECK(b.ids().size() == 1);

    Node c(0x97);
    REQUIRE(!c.post().empty());
    NetSession t(c.core, c.card, c.key, nonce(3), false, "");
    RawDialer r2(0x98);
    r2.handshake(t);
    RawDialer::say(t, "SEQ 0");
    RawDialer::say(t, "HEND");
    CHECK(t.failed());
}

TEST_CASE("session drops a frame over kNetMaxFrame") {
    Node b(0x99);
    NetSession s(b.core, b.card, b.key, nonce(2), false, "");
    std::string chunk(1000, 'A');
    size_t fed = 0;
    while (fed + chunk.size() <= kSyncMaxLine) {
        s.feed(chunk.data(), chunk.size());
        fed += chunk.size();
        REQUIRE_FALSE(s.failed());
    }
    std::string rest(kSyncMaxLine - fed, 'A');
    s.feed(rest.data(), rest.size());
    CHECK_FALSE(s.failed());  // exactly at the limit, still held
    s.feed("A", 1);           // one past it
    CHECK(s.failed());
    CHECK(std::string(s.why()) == "frame too big");
    CHECK(countPrefix(s.takeOut(), "BYE big") == 1);

    // 10 KB in one go, no newline
    NetSession t(b.core, b.card, b.key, nonce(3), false, "");
    std::string ten(10 * 1024, 'B');
    t.feed(ten.data(), ten.size());
    CHECK(t.failed());
    CHECK(std::string(t.why()) == "frame too big");
}

TEST_CASE("banned board refused at handshake") {
    RevKeys rk;
    Node a(0x9A), b(0x9B);
    REQUIRE(b.core.takeNotice(makeNotice(rev0(), 0, "ban", a.board, 1), ""));
    NetSession da(a.core, a.card, a.key, nonce(1), true, b.board);
    NetSession db(b.core, b.card, b.key, nonce(2), false, "");
    uint32_t ms = 0;
    run(da, db, ms);
    CHECK(db.failed());
    CHECK(std::string(db.why()) == "banned");
    CHECK(da.failed());
    CHECK(std::string(da.why()) == "bye banned");
    CHECK_FALSE(db.proven());

    // and the other way: a dialer refuses a banned answerer
    Node c(0x9C), d(0x9D);
    REQUIRE(c.core.takeNotice(makeNotice(rev0(), 0, "ban", d.board, 1), ""));
    NetSession dc(c.core, c.card, c.key, nonce(1), true, d.board);
    NetSession dd(d.core, d.card, d.key, nonce(2), false, "");
    ms = 0;
    run(dc, dd, ms);
    CHECK(std::string(dc.why()) == "banned");
    CHECK(std::string(dd.why()) == "bye banned");
}

TEST_CASE("a board blocked here is refused with BYE bad") {
    RevKeys rk;
    Node a(0x9E), b(0x9F);
    b.core.setLocalFilter([&](const std::string& x) { return x == a.board; }, nullptr);
    NetSession da(a.core, a.card, a.key, nonce(1), true, b.board);
    NetSession db(b.core, b.card, b.key, nonce(2), false, "");
    uint32_t ms = 0;
    run(da, db, ms);
    CHECK(std::string(db.why()) == "blocked");
    CHECK(std::string(da.why()) == "bye bad");
}

TEST_CASE("bad proof refused") {
    Node b(0xA2);
    NetSession s(b.core, b.card, b.key, nonce(2), false, "");
    RawDialer r(0xA3);
    std::string h = r.hello();
    s.feed(h.data(), h.size());
    std::string reply = s.takeOut();
    REQUIRE(countPrefix(reply, "AXISNET 2 ") == 1);
    CHECK(countPrefix(reply, "PROOF") == 0);  // the answerer signs nothing before the dialer's proof
    std::string aNonce = lines(reply)[0].substr(lines(reply)[0].size() - 16);
    // signed with a key that is not the card's
    std::string sig = signHex(keyOf(0xA4), transcriptOf('D', r.myNonce, aNonce, r.card.board, b.board));
    RawDialer::say(s, "PROOF " + sig);
    CHECK(s.failed());
    CHECK(std::string(s.why()) == "bad proof");
    std::string out = s.takeOut();
    CHECK(countPrefix(out, "BYE bad") == 1);
    CHECK(countPrefix(out, "PROOF") == 0);

    // the right key over the wrong role
    NetSession t(b.core, b.card, b.key, nonce(5), false, "");
    RawDialer r2(0xA5);
    h = r2.hello();
    t.feed(h.data(), h.size());
    reply = t.takeOut();
    aNonce = lines(reply)[0].substr(lines(reply)[0].size() - 16);
    RawDialer::say(t, "PROOF " + signHex(r2.key, transcriptOf('A', r2.myNonce, aNonce, r2.card.board, b.board)));
    CHECK(std::string(t.why()) == "bad proof");
}

TEST_CASE("a dialer with an expected board rejects a different answerer before its proof") {
    RevKeys rk;
    Node a(0xA6), b(0xA7), c(0xA8);
    NetSession da(a.core, a.card, a.key, nonce(1), true, c.board);  // meant to reach C
    NetSession db(b.core, b.card, b.key, nonce(2), false, "");      // B answers
    std::string h = da.takeOut();
    db.feed(h.data(), h.size());
    std::string hb = db.takeOut();
    da.feed(hb.data(), hb.size());
    CHECK(da.failed());
    CHECK(std::string(da.why()) == "wrong board");
    std::string out = da.takeOut();
    CHECK(countPrefix(out, "PROOF") == 0);
    CHECK(countPrefix(out, "BYE bad") == 1);
}

TEST_CASE("signing oracle: a relayed nonce never makes A accept the attacker as C") {
    RevKeys rk;
    Node a(0xA9), c(0xAA);
    KeyPair mk = keyOf(0xAB);  // the attacker
    NetCard mc = makeCard(mk, "Mallory");

    // A means to dial C but reaches the attacker. The attacker opens its own session with C, as
    // itself, carrying A's nonce, and passes C's hello back to A.
    NetSession sa(a.core, a.card, a.key, nonce(1), true, c.board);
    NetSession sc(c.core, c.card, c.key, nonce(3), false, "");
    std::string helloA = lines(sa.takeOut())[0];
    std::string nA = helloA.substr(helloA.size() - 16);
    RawDialer::say(sc, "AXISNET 2 " + encode(mc) + " " + nA);
    std::string fromC = sc.takeOut();
    REQUIRE(countPrefix(fromC, "AXISNET 2 ") == 1);
    CHECK(countPrefix(fromC, "PROOF") == 0);  // C signs nothing before the attacker proves
    std::string helloC = lines(fromC)[0];
    std::string nC = helloC.substr(helloC.size() - 16);

    sa.feed(fromC.data(), fromC.size());  // A sees C's own hello: right board
    REQUIRE_FALSE(sa.failed());
    std::string proofA = lastLine(sa.takeOut(), "PROOF ");
    REQUIRE(!proofA.empty());

    SUBCASE("C's proof, passed back to A, does not check out") {
        // The attacker proves to C as itself; C then proves, over a transcript naming the attacker.
        RawDialer::say(sc, "PROOF " + signHex(mk, transcriptOf('D', nA, nC, mc.board, c.board)));
        REQUIRE(sc.proven());
        std::string proofC = lastLine(sc.takeOut(), "PROOF ");
        REQUIRE(!proofC.empty());
        RawDialer::say(sa, proofC);
        CHECK(sa.failed());
        CHECK(std::string(sa.why()) == "bad proof");
        CHECK_FALSE(sa.proven());
        CHECK_FALSE(a.core.isLinkPeer(c.board));
    }
    SUBCASE("A's proof, passed on to C, does not check out either") {
        RawDialer::say(sc, proofA);
        CHECK(sc.failed());
        CHECK(std::string(sc.why()) == "bad proof");
        CHECK_FALSE(sc.proven());
    }
}

TEST_CASE("heap gate pauses sending") {
    RevKeys rk;
    Node a(0xAC), b(0xAD);
    a.core.addLinkPeer(b.board, true);
    for (int i = 0; i < 3; i++) REQUIRE(!a.post().empty());
    REQUIRE(!b.post().empty());
    NetSession da(a.core, a.card, a.key, nonce(1), true, b.board);
    NetSession db(b.core, b.card, b.key, nonce(2), false, "");
    std::string allA;
    uint32_t ms = 0;
    for (int i = 0; i < 100; i++) {
        std::string x = da.takeOut();
        allA += x;
        db.feed(x.data(), x.size());
        std::string y = db.takeOut();
        da.feed(y.data(), y.size());
        da.tick(ms, false);  // A is short of heap
        db.tick(ms, true);
        ms += 10;
    }
    allA += da.takeOut();
    REQUIRE(da.proven());
    CHECK(countPrefix(allA, "E ") == 0);
    CHECK(countPrefix(allA, "SEND") == 0);
    CHECK_FALSE(da.failed());
    run(da, db, ms);  // heap back
    CHECK_MESSAGE(da.done(), da.why());
    CHECK_MESSAGE(db.done(), db.why());
    CHECK(db.postsTaken() == 3);
}

TEST_CASE("E lines: 4 per tick, 8 in flight, answered in order, then DONE") {
    RevKeys rk;
    Node a(0xAE);
    RawDialer r(0xAF);
    a.core.addLinkPeer(r.card.board, false);
    for (int i = 0; i < 10; i++) REQUIRE(!a.post("sn_support", "n" + std::to_string(i)).empty());
    NetSession s(a.core, a.card, a.key, nonce(4), false, "");
    std::string after = r.handshake(s);
    REQUIRE(s.proven());
    CHECK(countPrefix(after, "SEQ 0") == 1);
    RawDialer::say(s, "SEQ 0");
    RawDialer::say(s, "SEND");
    s.tick(0, true);
    std::string t1 = s.takeOut();
    CHECK(countPrefix(t1, "SEND") == 1);
    CHECK(countPrefix(t1, "S ") == 0);  // a link peer at the same seq gets no snapshot
    CHECK(countPrefix(t1, "E ") == 4);
    s.tick(10, true);
    std::string t2 = s.takeOut();
    CHECK(countPrefix(t2, "E ") == 4);
    s.tick(20, true);
    CHECK(countPrefix(s.takeOut(), "E ") == 0);  // 8 in flight: wait for answers
    std::vector<std::string> idx;
    for (const auto& l : lines(t1 + t2))
        if (l.compare(0, 2, "E ") == 0) idx.push_back(l.substr(2, l.find(' ', 2) - 2));
    REQUIRE(idx.size() == 8);
    for (size_t i = 0; i < idx.size(); i++) RawDialer::say(s, (i == 3 ? "NAK " : "ACK ") + idx[i] + (i == 3 ? " Limited" : ""));
    REQUIRE_FALSE(s.failed());
    CHECK(s.naks() == 1);
    s.tick(30, true);
    std::string t3 = s.takeOut();
    CHECK(countPrefix(t3, "E ") == 2);
    for (const auto& l : lines(t3))
        if (l.compare(0, 2, "E ") == 0) RawDialer::say(s, "ACK " + l.substr(2, l.find(' ', 2) - 2));
    s.tick(40, true);
    CHECK(countPrefix(s.takeOut(), "DONE") == 1);
    CHECK_FALSE(s.done());
    RawDialer::say(s, "DONE");
    CHECK(s.done());
    CHECK(s.postsSent() == 10);
    CHECK(a.core.pendingFor(r.card.board, 8).empty());
}

TEST_CASE("an ACK out of order fails the session") {
    RevKeys rk;
    Node a(0xB4);
    RawDialer r(0xB5);
    a.core.addLinkPeer(r.card.board, false);
    for (int i = 0; i < 3; i++) REQUIRE(!a.post().empty());
    NetSession s(a.core, a.card, a.key, nonce(4), false, "");
    r.handshake(s);
    RawDialer::say(s, "SEQ 0");
    RawDialer::say(s, "SEND");
    s.tick(0, true);
    std::vector<std::string> idx;
    for (const auto& l : lines(s.takeOut()))
        if (l.compare(0, 2, "E ") == 0) idx.push_back(l.substr(2, l.find(' ', 2) - 2));
    REQUIRE(idx.size() == 3);
    RawDialer::say(s, "ACK " + idx[1]);
    CHECK(s.failed());
    CHECK(std::string(s.why()) == "bad line");
    auto p = a.core.pendingFor(r.card.board, 8);
    CHECK(p.size() == 3);  // nothing was skipped
}

TEST_CASE("a peer that never reads makes the session fail") {
    Node b(0xB6);
    REQUIRE(!b.post().empty());
    NetSession s(b.core, b.card, b.key, nonce(2), false, "");
    RawDialer r(0xB7);
    r.handshake(s);
    RawDialer::say(s, "SEQ 0");
    // Each E line gets an answer; the peer reads none of them.
    int i = 0;
    for (; i < 3000 && !s.failed(); i++) RawDialer::say(s, "E " + std::to_string(i));
    CHECK(s.failed());
    CHECK(std::string(s.why()) == "peer not reading");
    CHECK(i < 2000);
}

TEST_CASE("session limits: silence, lines and the deadline") {
    Node b(0xB8);
    REQUIRE(!b.post().empty());

    // Silence counts complete lines only.
    NetSession s(b.core, b.card, b.key, nonce(2), false, "");
    s.tick(0, true);
    s.feed("AXIS", 4);  // half a line
    s.tick(20000, true);
    CHECK_FALSE(s.failed());
    s.tick(kSyncTimeoutMs + 1, true);
    CHECK(s.failed());
    CHECK(std::string(s.why()) == "timeout");

    // 4000 lines at most.
    NetSession t(b.core, b.card, b.key, nonce(3), false, "");
    RawDialer r(0xB9);
    r.handshake(t);
    RawDialer::say(t, "SEQ 0");
    int i = 0;
    for (; i < 5000 && !t.failed(); i++) {
        RawDialer::say(t, "E " + std::to_string(i));
        t.takeOut();
    }
    CHECK(std::string(t.why()) == "too many lines");
    CHECK(i == (int)kSyncMaxLines - 2);  // hello, proof and SEQ came first

    // 10 minutes at most, however busy (for a board that was a link peer already; see below for others).
    NetSession u(b.core, b.card, b.key, nonce(4), false, "");
    RawDialer r2(0xBA);
    b.core.addLinkPeer(r2.card.board, false);  // an inbound link peer with two completed sessions: trusted
    for (uint8_t k = 0; k < kNetTrustSessions; k++) b.core.sessionDone(r2.card.board);
    r2.handshake(u);
    RawDialer::say(u, "SEQ 0");
    uint32_t ms = 0;
    u.tick(ms, true);
    while (!u.failed() && ms < 2 * kSyncDeadlineMs) {
        ms += 20000;
        RawDialer::say(u, "E 1");
        u.takeOut();
        u.tick(ms, true);
    }
    CHECK(std::string(u.why()) == "too long");
    CHECK(ms > kSyncDeadlineMs);
    CHECK(ms <= kSyncDeadlineMs + 20000);
}

TEST_CASE("history is merged across areas by date, so an origin's posts in later areas are not lost") {
    RevKeys rk;
    Node a(0xC0), b(0xC1);
    // One post in the last area first, then 70 in the first area: the first area alone would lift the
    // receiver's window more than 64 seqs past the early post.
    a.now = kNow - 2 * kDay;
    std::string early = netPostId(a.post("sn_bugs", "early"));
    for (int i = 0; i < 70; i++) {
        a.now += 60;
        REQUIRE(!a.post("sn_support", "n" + std::to_string(i)).empty());
    }
    a.now = kNow;
    // The walk itself: oldest first across the areas, so seqs climb.
    HistoryCursor c;
    std::string l;
    std::vector<uint32_t> seqs;
    while (a.core.historyNext(c, 0, l)) {
        std::string id = decodePost(l).id;
        seqs.push_back((uint32_t)std::stoul(id.substr(17)));
    }
    REQUIRE(seqs.size() == 71);
    CHECK(seqs.front() == 1);
    CHECK(std::is_sorted(seqs.begin(), seqs.end()));
    // And over a session to a new board.
    NetSession da(a.core, a.card, a.key, nonce(1), true, b.board);
    NetSession db(b.core, b.card, b.key, nonce(2), false, "");
    uint32_t ms = 0;
    run(da, db, ms);
    CHECK_MESSAGE(da.done(), da.why());
    CHECK_MESSAGE(db.done(), db.why());
    CHECK(db.postsTaken() == 71);
    auto bugs = b.ids("sn_bugs");
    REQUIRE(bugs.size() == 1);
    CHECK(bugs[0] == early);
    CHECK(b.ids().size() == 70);
}

namespace {
// Runs two sessions back to back and returns everything the second one (b) said.
std::string runSaid(NetSession& a, NetSession& b) {
    std::string allB;
    uint32_t ms = 0;
    for (int i = 0; i < 5000 && !(settled(a) && settled(b)); i++) {
        std::string o = a.takeOut();
        b.feed(o.data(), o.size());
        std::string y = b.takeOut();
        allB += y;
        a.feed(y.data(), y.size());
        a.tick(ms, true);
        b.tick(ms, true);
        ms += 10;
    }
    std::string o = a.takeOut();
    b.feed(o.data(), o.size());
    std::string y = b.takeOut();
    allB += y;
    a.feed(y.data(), y.size());
    return allB;
}
}  // namespace

TEST_CASE("a history pull cut mid-way resumes on the next session and ends with all posts present") {
    RevKeys rk;
    Node a(0xE3), b(0xE4);
    std::vector<std::string> want;
    for (int i = 0; i < 40; i++) {
        a.now = kNow - 3 * kDay + (uint32_t)i * 60;
        want.push_back(netPostId(a.post("sn_support", "n" + std::to_string(i))));
    }
    a.now = kNow;
    REQUIRE(b.core.historyState() == -1);
    {  // The first pull is cut after some of it came (a dropped call, a deadline): no HEND.
        NetSession da(a.core, a.card, a.key, nonce(1), true, b.board);
        NetSession db(b.core, b.card, b.key, nonce(2), false, "");
        uint32_t ms = 0;
        for (int i = 0; i < 2000 && db.postsTaken() < 10 && !settled(da) && !settled(db); i++) {
            std::string o = da.takeOut();
            db.feed(o.data(), o.size());
            std::string y = db.takeOut();
            da.feed(y.data(), y.size());
            da.tick(ms, true);
            db.tick(ms, true);
            ms += 10;
        }
        REQUIRE(db.postsTaken() >= 10);
        REQUIRE(db.postsTaken() < want.size());
        CHECK(db.deadlineMs() == kSyncDeadlineMs);  // it sent WANT: the full deadline, though A is no link peer of it
    }
    CHECK(b.core.historyState() == 0);
    CHECK(b.core.hasNetPosts());  // the areas are no longer empty: the old rule would never have asked again
    // A reboot: the flag goes with the cursors.
    std::string saved = b.core.saveCursors();
    CHECK(hasLine(saved, "history 0"));
    b.core.loadCursors(saved);
    CHECK(b.core.historyState() == 0);
    {  // The next session asks again; repeats are dropped as duplicates.
        NetSession da(a.core, a.card, a.key, nonce(3), true, b.board);
        NetSession db(b.core, b.card, b.key, nonce(4), false, "");
        std::string said = runSaid(da, db);
        CHECK(countPrefix(said, "WANT") == 1);
        CHECK_MESSAGE(da.done(), da.why());
        CHECK_MESSAGE(db.done(), db.why());
    }
    auto got = b.ids();
    std::sort(got.begin(), got.end());
    std::sort(want.begin(), want.end());
    CHECK(got == want);
    CHECK(b.core.historyState() == 1);
    saved = b.core.saveCursors();
    CHECK(hasLine(saved, "history 1"));
    b.core.loadCursors(saved);
    CHECK(b.core.historyState() == 1);
    {  // Done: no WANT any more.
        NetSession da(a.core, a.card, a.key, nonce(5), true, b.board);
        NetSession db(b.core, b.card, b.key, nonce(6), false, "");
        std::string said = runSaid(da, db);
        CHECK_MESSAGE(db.done(), db.why());
        CHECK(countPrefix(said, "WANT") == 0);
    }
}

TEST_CASE("an upgraded board with posts and no history flag does not pull") {
    RevKeys rk;
    Node a(0xE5), b(0xE6);
    REQUIRE(!a.post("sn_support", "a's").empty());
    REQUIRE(!b.post("sn_bugs", "b's own, from before the upgrade").empty());
    b.core.loadCursors("");  // an older build's cursors.txt: no history line
    REQUIRE(b.core.historyState() == -1);
    NetSession da(a.core, a.card, a.key, nonce(1), true, b.board);
    NetSession db(b.core, b.card, b.key, nonce(2), false, "");
    std::string said = runSaid(da, db);
    CHECK_MESSAGE(db.done(), db.why());
    CHECK(countPrefix(said, "WANT") == 0);
    CHECK(db.postsTaken() == 0);
    CHECK(b.core.historyState() == 1);  // decided once, and saved from now on
    CHECK(hasLine(b.core.saveCursors(), "history 1"));
}

TEST_CASE("a pull that got no H line at all is not done: the next session asks again") {
    RevKeys rk;
    Node a(0xE7), b(0xE8);  // both empty
    for (int round = 0; round < 2; round++) {
        NetSession da(a.core, a.card, a.key, nonce(1), true, b.board);
        NetSession db(b.core, b.card, b.key, nonce(2), false, "");
        std::string said = runSaid(da, db);
        CHECK_MESSAGE(db.done(), db.why());
        CHECK(countPrefix(said, "WANT") == 1);
        CHECK(b.core.historyState() == 0);
    }
}

TEST_CASE("a junk H line plus HEND leaves history not done; a later real pull completes it") {
    RevKeys rk;
    Node a(0xEA), b(0xEB);  // b is empty and asks
    REQUIRE(!a.post("sn_support", "a's").empty());
    {
        NetSession s(b.core, b.card, b.key, nonce(2), false, "");
        RawDialer r(0xEC);
        r.handshake(s);
        s.tick(0, true);
        CHECK(countPrefix(s.takeOut(), "WANT") == 1);
        RawDialer::say(s, "SEQ 0");
        RawDialer::say(s, "H junk-not-a-post");
        RawDialer::say(s, "HEND");
        CHECK(b.core.historyState() == 0);
    }
    NetSession da(a.core, a.card, a.key, nonce(1), true, b.board);
    NetSession db(b.core, b.card, b.key, nonce(3), false, "");
    std::string said = runSaid(da, db);
    CHECK_MESSAGE(db.done(), db.why());
    CHECK(countPrefix(said, "WANT") == 1);
    CHECK(db.postsTaken() == 1);
    CHECK(b.core.historyState() == 1);
}

TEST_CASE("WANT gives a stranger's session the full deadline; the stranger stays untrusted") {
    RevKeys rk;
    // This side asks: a new board answering a stranger.
    Node b(0xE9);
    RawDialer r(0xEA);
    NetSession s(b.core, b.card, b.key, nonce(1), false, "");
    r.handshake(s);
    CHECK(s.deadlineMs() == kSyncStrangerDeadlineMs);
    RawDialer::say(s, "SEQ 0");
    s.tick(0, true);
    CHECK(countPrefix(s.takeOut(), "WANT") == 1);
    CHECK(s.deadlineMs() == kSyncDeadlineMs);
    CHECK_FALSE(s.wasTrusted());  // the host still gives its address the cooldown
    // The stranger asks: a board with posts answering it.
    Node c(0xEB);
    REQUIRE(!c.post().empty());
    RawDialer r2(0xEC);
    NetSession t(c.core, c.card, c.key, nonce(2), false, "");
    r2.handshake(t);
    RawDialer::say(t, "SEQ 0");
    t.tick(0, true);
    CHECK(countPrefix(t.takeOut(), "WANT") == 0);
    CHECK(t.deadlineMs() == kSyncStrangerDeadlineMs);
    RawDialer::say(t, "WANT 30");
    CHECK(t.deadlineMs() == kSyncDeadlineMs);
    CHECK_FALSE(t.wasTrusted());
}

namespace {
// The E lines in out, as (index, record).
std::vector<std::pair<std::string, std::string>> eLines(const std::string& out) {
    std::vector<std::pair<std::string, std::string>> v;
    for (const auto& l : lines(out))
        if (l.compare(0, 2, "E ") == 0) {
            size_t sp = l.find(' ', 2);
            v.emplace_back(l.substr(2, sp - 2), l.substr(sp + 1));
        }
    return v;
}
}  // namespace

TEST_CASE("a transient NAK holds the cursor and stops the session's sending; permanent ones move it") {
    RevKeys rk;
    Node a(0xC2);
    RawDialer r(0xC3);
    a.core.addLinkPeer(r.card.board, false);
    for (int i = 0; i < 6; i++) REQUIRE(!a.post("sn_support", "n" + std::to_string(i)).empty());
    for (int round = 1; round <= (int)kNetNakStrikes; round++) {
        NetSession s(a.core, a.card, a.key, nonce(4), false, "");
        r.handshake(s);
        RawDialer::say(s, "SEQ 0");
        RawDialer::say(s, "SEND");
        s.tick(0, true);
        s.tick(10, true);
        auto sent = eLines(s.takeOut());
        size_t k = 0;
        if (round == 1) {
            REQUIRE(sent.size() == 6);
            RawDialer::say(s, "NAK " + sent[k++].first + " " + kNakSig);  // permanent: passed over
        } else {
            REQUIRE(sent.size() == 5);  // the held one and everything after it, again
        }
        CHECK(decodePost(sent[k].second).id == a.board + "-2");
        RawDialer::say(s, "NAK " + sent[k++].first + " " + kNakNoCard);
        for (; k < sent.size(); k++) RawDialer::say(s, "ACK " + sent[k].first);  // these must not move the cursor
        REQUIRE_FALSE(s.failed());
        CHECK(s.naks() == (round == 1 ? 2u : 1u));
        s.tick(20, true);
        std::string more = s.takeOut();
        CHECK(eLines(more).empty());  // nothing more this session
        CHECK(countPrefix(more, "DONE") == 1);
        RawDialer::say(s, "DONE");
        CHECK(s.done());
        auto next = a.pendingPosts(r.card.board);
        if (round < (int)kNetNakStrikes) {
            REQUIRE(next.size() == 5);
            CHECK(decodePost(next[0]).id == a.board + "-2");
        } else {  // held three sessions in a row: it cannot go to this peer, the cursor moves on
            REQUIRE(next.size() == 4);
            CHECK(decodePost(next[0]).id == a.board + "-3");
        }
    }
    CHECK(netNakTransient(kNakStore));
    CHECK_FALSE(netNakTransient(kNakLimited));
    CHECK_FALSE(netNakTransient("Limited"));  // an older board's word: permanent
}

TEST_CASE("a refused post gets its NAK word: nocard and store are transient") {
    RevKeys rk;
    Node a(0xC4), b(0xC5);
    NetPost p = decodePost(a.post());
    const char* why = nullptr;
    CHECK(b.core.take(p, a.board, false, &why) == Took::Rejected);
    CHECK(std::string(why) == kNakNoCard);
    b.knows(a);
    NetPost forged = p;
    forged.subject = "changed";
    CHECK(b.core.take(forged, a.board, false, &why) == Took::Rejected);
    CHECK(std::string(why) == kNakSig);
    CHECK(b.core.take(makePost(a.key, 9, kNow, "sn_announce"), a.board, false, &why) == Took::Rejected);
    CHECK(std::string(why) == kNakRevOnly);
    CHECK(b.core.take(p, a.board, false, &why) == Took::Stored);
    CHECK(why == nullptr);
    CHECK(b.core.take(p, a.board, false, &why) == Took::Duplicate);
    CHECK(why == nullptr);
}

TEST_CASE("nocard for a post whose origin's card the sender lacks too moves the cursor at once") {
    RevKeys rk;
    Node a(0xE0), x(0xE1);
    RawDialer r(0xE2);
    a.knows(x);
    a.core.addLinkPeer(r.card.board, true);
    NetPost px = decodePost(x.post("sn_support", "from x"));
    REQUIRE(a.core.take(px, x.board) == Took::Stored);
    REQUIRE(!a.post("sn_support", "a's own").empty());
    // X's card goes the way a card really goes: newer cards push it out of the full state.
    for (int i = 0; i < (int)kNetMaxCards; i++) REQUIRE(a.core.takeCard(makeCard(keyOf((uint8_t)(0x10 + i)), "Filler"), ""));
    REQUIRE(a.core.state().card(x.board) == nullptr);
    NetSession s(a.core, a.card, a.key, nonce(20), false, "");
    r.handshake(s);
    RawDialer::say(s, "SEQ 0");
    RawDialer::say(s, "SEND");
    s.tick(0, true);
    // The peer refuses X's post with nocard and takes everything else (A's own post, the filler cards).
    std::string all;
    size_t posts = 0;
    bool doneSent = false;
    for (uint32_t ms = 10; ms < 1000 && !doneSent && !s.failed(); ms += 10) {
        s.tick(ms, true);
        std::string out = s.takeOut();
        all += out;
        doneSent = countPrefix(out, "DONE") == 1;
        for (auto& e : eLines(out)) {
            bool isX = e.second[0] == (char)NetKind::Post && decodePost(e.second).id == px.id;
            if (e.second[0] == (char)NetKind::Post) posts++;
            // nocard for X's post: no session could cure it, so it is final and holds nothing back.
            RawDialer::say(s, isX ? "NAK " + e.first + " " + kNakNoCard : "ACK " + e.first);
        }
    }
    REQUIRE_FALSE(s.failed());
    CHECK(doneSent);
    CHECK(posts == 2);  // X's and A's own, each once: the nocard did not stop the session
    CHECK(countPrefix(all, "S " + encode(x.card)) == 0);  // no card here to send ahead
    CHECK(s.naks() == 1);
    RawDialer::say(s, "DONE");
    CHECK(s.done());
    CHECK(a.pendingPosts(r.card.board).empty());  // the cursor moved past X's post at once, no strikes needed
    CHECK(a.core.pendingFor(r.card.board, 8).empty());
}

TEST_CASE("an origin's card goes ahead of its first post, and a receiver takes it after SEND") {
    RevKeys rk;
    Node x(0xC6), a(0xC7), b(0xC8);
    a.knows(x);
    a.core.addLinkPeer(b.board, true);
    NetPost p1 = decodePost(x.post("sn_support", "one")), p2 = decodePost(x.post("sn_support", "two"));
    REQUIRE(a.core.take(p1, x.board) == Took::Stored);
    REQUIRE(a.core.take(p2, x.board) == Took::Stored);
    REQUIRE(!a.post("sn_support", "a's own").empty());
    // Over a real session to B, which does not know X: without the card ahead, X's posts would be NAKed nocard.
    REQUIRE(!b.post("sn_bugs", "b's own").empty());  // B is not empty: no history pull
    b.core.addLinkPeer(a.board, true);               // a link peer at the same seq gets no snapshot
    REQUIRE(b.core.state().card(x.board) == nullptr);
    NetSession da(a.core, a.card, a.key, nonce(1), true, b.board);
    NetSession db(b.core, b.card, b.key, nonce(2), false, "");
    uint32_t ms = 0;
    std::string allA;
    for (int i = 0; i < 2000 && !(settled(da) && settled(db)); i++) {
        std::string o = da.takeOut();
        allA += o;
        db.feed(o.data(), o.size());
        std::string y = db.takeOut();
        da.feed(y.data(), y.size());
        da.tick(ms, true);
        db.tick(ms, true);
        ms += 10;
    }
    CHECK_MESSAGE(da.done(), da.why());
    CHECK_MESSAGE(db.done(), db.why());
    CHECK(b.core.state().card(x.board) != nullptr);
    CHECK(db.postsTaken() == 3);
    CHECK(db.naksSent() == 0);
    // The card went once, on an S line before X's first post.
    std::string card = "S " + encode(x.card);
    size_t at = allA.find(card);
    CHECK(countPrefix(allA, card) == 1);
    REQUIRE(at != std::string::npos);
    CHECK(at < allA.find(p1.id));
}

TEST_CASE("after SEND an S line may carry only a card") {
    RevKeys rk;
    Node a(0xCA);
    RawDialer r(0xCB);
    NetSession s(a.core, a.card, a.key, nonce(7), false, "");
    r.handshake(s);
    RawDialer::say(s, "SEQ 0");
    RawDialer::say(s, "SEND");
    RawDialer::say(s, "S " + encode(makeCard(keyOf(0xCC), "Late")));
    CHECK_FALSE(s.failed());
    CHECK(a.core.state().card(boardOf(keyOf(0xCC))) != nullptr);
    RawDialer::say(s, "S " + encode(makeNotice(rev0(), 0, "ban", "0123456789abcdef", 3)));
    CHECK(s.failed());
}

TEST_CASE("short of heap the silence timer stands still; the deadline does not") {
    RevKeys rk;
    Node b(0xCD);
    RawDialer r(0xCE);
    b.core.addLinkPeer(r.card.board, false);  // a trusted link peer: the full deadline
    for (uint8_t k = 0; k < kNetTrustSessions; k++) b.core.sessionDone(r.card.board);
    NetSession s(b.core, b.card, b.key, nonce(8), false, "");
    s.tick(0, true);
    r.handshake(s);
    RawDialer::say(s, "SEQ 0");
    uint32_t ms = 0;
    for (; ms <= 5 * 60 * 1000; ms += 1000) s.tick(ms, false);  // the host reads nothing for 5 minutes
    CHECK_FALSE(s.failed());
    s.tick(ms + kSyncTimeoutMs - 1000, true);  // heap back: silence counts from the last paused tick
    CHECK_FALSE(s.failed());
    s.tick(ms + kSyncTimeoutMs + 1000, true);
    CHECK(s.failed());
    CHECK(std::string(s.why()) == "timeout");

    NetSession t(b.core, b.card, b.key, nonce(9), false, "");
    t.tick(0, true);
    r.handshake(t);
    for (ms = 0; ms <= kSyncDeadlineMs + 2000 && !t.failed(); ms += 1000) t.tick(ms, false);
    CHECK(t.failed());
    CHECK(std::string(t.why()) == "too long");
}

TEST_CASE("a max-size post line is taken whole through the in-place parse") {
    RevKeys rk;
    Node a(0xCF), b(0xD0);
    b.knows(a);
    RawDialer r(0xD1);
    // The biggest body that still fits a frame once escaped.
    std::string body(kNetMaxBody, 'x');
    for (size_t i = 0; i < body.size(); i += 97) body[i] = '\n';
    NetPost p;
    p.board = a.board;
    p.id = a.board + "-1";
    p.area = "sn_support";
    p.from = "Sysop";
    p.to = "All";
    p.subject = "Big";
    p.body = body;
    p.date = kNow;
    while (!signPost(p, a.key)) p.body.pop_back();
    std::string line = encode(p);
    REQUIRE(line.size() > kNetMaxBody);
    NetSession s(b.core, b.card, b.key, nonce(10), false, "");
    r.handshake(s);
    RawDialer::say(s, "SEQ 0");
    RawDialer::say(s, "E 7 " + line);
    CHECK_FALSE(s.failed());
    CHECK(countPrefix(s.takeOut(), "ACK 7") == 1);
    axis::bbs::Message m;
    REQUIRE(b.files.read("sn_support", 1, m));
    CHECK(m.body == p.body);
    NetPost back = decodePost(m.net);
    CHECK(back.id == p.id);
    CHECK(back.path == r.card.board);  // the hop was added
}

TEST_CASE("an inbound session from a board that was not a link peer gets 2 minutes") {
    RevKeys rk;
    Node b(0xD2);
    REQUIRE(!b.post().empty());
    auto runFor = [&](NetSession& s) {
        uint32_t ms = 0;
        s.tick(ms, true);
        while (!s.failed() && ms < 2 * kSyncDeadlineMs) {
            ms += 5000;
            RawDialer::say(s, "E " + std::to_string(ms));
            s.takeOut();
            s.tick(ms, true);
        }
        return ms;
    };
    RawDialer stranger(0xD3);
    NetSession s(b.core, b.card, b.key, nonce(11), false, "");
    stranger.handshake(s);
    REQUIRE(s.proven());
    CHECK_FALSE(s.wasLinkPeer());
    RawDialer::say(s, "SEQ 0");
    uint32_t ms = runFor(s);
    CHECK(std::string(s.why()) == "too long");
    CHECK(ms > kSyncStrangerDeadlineMs);
    CHECK(ms <= kSyncStrangerDeadlineMs + 5000);
    // The first session put it in the inbound pool, but that alone buys nothing: its second session is
    // still a stranger's (2 minutes, and the host cools its address down afterwards).
    REQUIRE(b.core.isLinkPeer(stranger.card.board));
    NetSession t(b.core, b.card, b.key, nonce(12), false, "");
    stranger.handshake(t);
    CHECK(t.wasLinkPeer());
    CHECK_FALSE(t.wasTrusted());
    CHECK(t.deadlineMs() == kSyncStrangerDeadlineMs);
    RawDialer::say(t, "SEQ 0");
    ms = runFor(t);
    CHECK(std::string(t.why()) == "too long");
    CHECK(ms > kSyncStrangerDeadlineMs);
    CHECK(ms <= kSyncStrangerDeadlineMs + 5000);
    // A dialer always has the full deadline (it chose the board).
    Node c(0xD4);
    c.knows(b);
    NetSession d(c.core, c.card, c.key, nonce(13), true, b.board);
    CHECK(d.deadlineMs() == kSyncDeadlineMs);
    CHECK(s.deadlineMs() == kSyncStrangerDeadlineMs);
}

TEST_CASE("an inbound-only link peer is trusted after two completed sessions; failed ones do not count") {
    RevKeys rk;
    Node b(0xD5);
    REQUIRE(!b.post().empty());
    RawDialer peer(0xD6);
    // One session to the end: SEQ, SEND, DONE both ways.
    auto complete = [&](uint8_t n) {
        NetSession s(b.core, b.card, b.key, nonce(n), false, "");
        peer.handshake(s);
        bool trusted = s.wasTrusted();
        RawDialer::say(s, "SEQ 0");
        RawDialer::say(s, "SEND");
        for (uint32_t ms = 0; ms < 1000 && !s.done(); ms += 10) s.tick(ms, true);
        RawDialer::say(s, "DONE");
        CHECK_MESSAGE(s.done(), s.why());
        return trusted;
    };
    {  // A failed session (cut by its deadline) counts for nothing.
        NetSession s(b.core, b.card, b.key, nonce(30), false, "");
        peer.handshake(s);
        CHECK_FALSE(s.wasTrusted());
        s.tick(0, true);
        s.tick(kSyncStrangerDeadlineMs + 1, true);
        REQUIRE(s.failed());
    }
    CHECK_FALSE(b.core.trustedPeer(peer.card.board));
    CHECK_FALSE(complete(31));  // still a stranger: no completed session yet
    CHECK_FALSE(b.core.trustedPeer(peer.card.board));
    CHECK_FALSE(complete(32));  // one completed session
    CHECK(b.core.trustedPeer(peer.card.board));
    NetSession s(b.core, b.card, b.key, nonce(33), false, "");
    peer.handshake(s);
    CHECK(s.wasTrusted());
    CHECK(s.deadlineMs() == kSyncDeadlineMs);
    // The count survives a reboot with the cursors.
    std::string saved = b.core.saveCursors();
    b.core.loadCursors(saved);
    CHECK(b.core.trustedPeer(peer.card.board));
    CHECK(b.core.saveCursors() == saved);
}

TEST_CASE("unknown record kind is NAKed and the session goes on") {
    RevKeys rk;
    {
        Node b(0x5A);
        REQUIRE(!b.post().empty());  // B holds posts, so it asks for no history
        NetSession s(b.core, b.card, b.key, nonce(2), false, "");
        RawDialer r(0x5B);
        r.handshake(s);
        REQUIRE(s.proven());
        s.tick(0, true);
        s.takeOut();
        RawDialer::say(s, "SEQ 0");
        RawDialer::say(s, "S Z\x1f" "a kind from a later build");  // passed over in the snapshot too
        CHECK_FALSE(s.failed());
        RawDialer::say(s, "E 0 Z\x1f" "a kind from a later build");
        CHECK(countPrefix(s.takeOut(), "NAK 0 bad") == 1);
        CHECK_FALSE(s.failed());
        RawDialer::say(s, "E 1 " + encode(makePost(r.key, 1, kNow)));
        CHECK(countPrefix(s.takeOut(), "ACK 1") == 1);
        CHECK_FALSE(s.failed());
        CHECK(b.ids().size() == 2);
    }

    UNode a(0x5C);
    REQUIRE(a.seal("admit", a.board, 1));
    REQUIRE(!a.post("sn_support", "for history").empty());
    REQUIRE(!a.post("un_chat", "user net").empty());
    const KeyPair X = keyOf(0x5D), V = keyOf(0x5E);
    REQUIRE(a.core.takeApp(makeApp(X), ""));
    REQUIRE(a.core.takeVote(makeVote(V, boardOf(X), 1, 1, true), ""));
    auto noUserRecords = [](const std::string& out, const char* prefix) {
        size_t bad = 0, n = std::string(prefix).size();
        for (const auto& l : lines(out))
            if (l.compare(0, n, prefix) == 0 && l.size() > n && (l[n] == 'A' || l[n] == 'V')) bad++;
        return bad;
    };
    {  // A new Phase 2 board: the snapshot, history and the User Net pull. A and V go only on U lines.
        UNode b(0x5F);
        NetSession da(a.core, a.card, a.key, nonce(3), true, b.board);
        NetSession db(b.core, b.card, b.key, nonce(4), false, "");
        std::string fa, fb;
        runBoth(da, db, fa, fb);
        CHECK_MESSAGE(da.done(), da.why());
        CHECK_MESSAGE(db.done(), db.why());
        CHECK(countPrefix(fa, "S ") > 0);
        CHECK(countPrefix(fa, "H P") == 1);
        CHECK(noUserRecords(fa, "S ") == 0);
        CHECK(noUserRecords(fa, "H ") == 0);
        CHECK(noUserRecords(fb, "S ") == 0);
        CHECK(noUserRecords(fb, "H ") == 0);
        CHECK(countPrefix(fa, "U A") == 1);
        CHECK(countPrefix(fa, "U V") == 1);
        CHECK(b.core.members().appCount() == 1);
        CHECK(b.core.members().voteCount() == 1);
    }
    {  // A board whose card lacks net2 (as a v0.53 board's): no UWANT, no U lines, and no A, V or un_ post E lines.
        Node old(0x60);
        old.core.userHistoryGot();  // it never asks for the User Net pull itself, as a v0.53 board would not
        a.core.addLinkPeer(old.board, true);
        const KeyPair Y = keyOf(0x61);
        REQUIRE(a.core.takeApp(makeApp(Y), ""));
        REQUIRE(a.core.takeVote(makeVote(V, boardOf(Y), 1, 1, false), ""));
        REQUIRE(!a.post("un_chat", "not for older boards").empty());
        REQUIRE(!a.post("sn_support", "for every board").empty());
        NetSession da(a.core, a.card, a.key, nonce(5), true, old.board);
        NetSession dold(old.core, old.card, old.key, nonce(6), false, "");
        std::string fa, fo;
        runBoth(da, dold, fa, fo);
        CHECK_MESSAGE(da.done(), da.why());
        CHECK_MESSAGE(dold.done(), dold.why());
        CHECK(countPrefix(fa, "UWANT") == 0);
        CHECK(countPrefix(fa, "U ") == 0);
        CHECK(countPrefix(fa, "UEND") == 0);
        CHECK(noUserRecords(fa, "S ") == 0);
        CHECK(noUserRecords(fa, "H ") == 0);
        size_t eLines = 0, userE = 0;
        for (const auto& l : lines(fa)) {
            if (l.compare(0, 2, "E ") != 0) continue;
            eLines++;
            size_t sp = l.find(' ', 2);
            REQUIRE(sp != std::string::npos);
            if (l[sp + 1] == 'A' || l[sp + 1] == 'V' || l.find("\x1fun_") != std::string::npos) userE++;
        }
        CHECK(eLines >= 1);
        CHECK(userE == 0);
        CHECK(old.core.members().appCount() == 0);
        CHECK(old.ids("un_chat").empty());
        CHECK(a.core.pendingFor(old.board, 64, false).empty());  // passed over, never read again
    }
}

// Net Files on the wire: L lines on E and U lines, the listings pull (fhistory) and its holds.

namespace {
struct Said {
    std::string fromDialer, fromAnswerer;
};

// One session, d dialing a, run to the end; both must finish.
Said syncFiles(Node& d, Node& a, uint8_t n) {
    NetSession sd(d.core, d.card, d.key, nonce(n), true, a.board);
    NetSession sa(a.core, a.card, a.key, nonce((uint8_t)(n + 1)), false, "");
    Said s;
    runBoth(sd, sa, s.fromDialer, s.fromAnswerer);
    CHECK_MESSAGE(sd.done(), sd.why());
    CHECK_MESSAGE(sa.done(), sa.why());
    return s;
}

// Lines of out that start with prefix ("E " or "U ") and carry a listing record.
size_t listingLinesIn(const std::string& out, const std::string& prefix) {
    size_t n = 0;
    for (const auto& l : lines(out)) {
        if (l.compare(0, prefix.size(), prefix) != 0) continue;
        size_t at = prefix == "E " ? l.find(' ', 2) + 1 : prefix.size();
        if (at < l.size() && l.compare(at, 2, "L\x1F") == 0) n++;
    }
    return n;
}

// Every pull of this board is done (history, User Net, listings).
void pullsDone(NetCore& c) {
    c.historyGot();
    c.userHistoryGot();
    c.filesHistoryGot();
}

// A receiver r3 holding seq 2 of NAME.ZIP from h, while h holds seq 5 (its pull replaces r3's), and r3's daily drop
// budget spent by y's re-listings: each pull's NAME.ZIP is refused "store".
struct HeldPull {
    FNode h, r3, y;
    HeldPull(uint8_t a, uint8_t b, uint8_t c) : h(a), r3(b), y(c) {
        for (FNode* n : {&h, &r3}) n->admit({&h.board, &r3.board, &y.board});
        r3.knows(y);
        r3.knows(h);
        h.core.setPostSeq(5);
        REQUIRE(!h.core.originateListing(makeListing(h.key, 1, "NAME.ZIP")).empty());
        REQUIRE(r3.core.takeListing(makeListing(h.key, 2, "NAME.ZIP"), h.board) == Took::Stored);
        uint32_t seq = 1;
        REQUIRE(r3.core.takeListing(makeListing(y.key, seq++, "SPEND.ZIP"), y.board, true) == Took::Stored);
        for (int k = 0; k < 8; k++) {
            r3.core.listingSessionStart();
            for (int i = 0; i < 8; i++) {
                REQUIRE(r3.core.takeListing(makeListing(y.key, seq++, "SPEND.ZIP"), y.board, true) == Took::Stored);
                indexReady(r3.core);  // each replace: the index built again before the next
            }
        }
        r3.core.listingSessionStart();
        REQUIRE(r3.core.takeListing(makeListing(y.key, seq++, "SPEND.ZIP"), y.board, true) == Took::Rejected);
    }
    std::string cursors() { return r3.core.saveCursors(); }
};
}  // namespace

TEST_CASE("listings go on E lines only to a net3 peer, the home board's card ahead; a Phase 2 peer gets none") {
    RevKeys rk;
    FNode h(0xC1), r3(0xC2), x(0xC3);
    UNode r2(0xC4);  // a Phase 2 board: net2, no net3
    for (FNode* n : {&h, &r3}) n->admit({&h.board, &r3.board, &x.board, &r2.board});
    REQUIRE(r2.seal("admit", h.board, 1));
    REQUIRE(r2.seal("admit", r2.board, 2));
    REQUIRE(r2.seal("admit", x.board, 3));
    h.knows(x);
    pullsDone(r3.core);
    pullsDone(r2.core);
    h.core.addLinkPeer(r3.board, true);
    h.core.addLinkPeer(r2.board, true);
    REQUIRE(!h.post().empty());  // h holds a post, so it asks for no history
    std::string own = h.core.originateListing(makeListing(h.key, 1, "HOME.ZIP"));
    REQUIRE(!own.empty());
    REQUIRE(h.core.takeListing(makeListing(x.key, 7, "RELAY.ZIP"), x.board) == Took::Stored);  // from x, passed on
    {
        Said s = syncFiles(h, r3, 1);
        CHECK(listingLinesIn(s.fromDialer, "E ") == 2);
        CHECK(countPrefix(s.fromAnswerer, "NAK") == 0);
        CHECK(r3.core.state().card(x.board) != nullptr);  // x's card went ahead of its listing
        NetListing l;
        CHECK(r3.core.findListing(own, l));
        CHECK(r3.core.findListing(x.board + "-7", l));
        CHECK(r3.page().size() == 2);
    }
    {
        Said s = syncFiles(h, r2, 3);
        CHECK(listingLinesIn(s.fromDialer, "E ") == 0);
        CHECK(listingLinesIn(s.fromDialer, "U ") == 0);
        CHECK(countPrefix(s.fromAnswerer, "NAK") == 0);
        CHECK(r2.ids(kNetFilesArea).empty());
        CHECK(h.core.pendingFor(r2.board, 64, true, false).empty());
    }
}

TEST_CASE("a U pull that asks for un_files gets live listings and young tombstones, then UEND F; a Phase 2 peer gets UEND") {
    RevKeys rk;
    FNode h(0xC5), r3(0xC6), old(0xC8);
    UNode r2(0xC7);
    for (FNode* n : {&h, &r3}) n->admit({&h.board, &r3.board, &r2.board, &old.board});
    REQUIRE(r2.seal("admit", h.board, 1));
    REQUIRE(r2.seal("admit", r2.board, 2));
    h.knows(old);
    r3.knows(old);
    for (int i = 1; i <= 3; i++) REQUIRE(!h.core.originateListing(makeListing(h.key, 1, "LIVE" + std::to_string(i))).empty());
    REQUIRE(!h.core.originateListing(makeListing(h.key, 1, "GONE", true)).empty());  // a young tombstone
    // an old tombstone, as one relayed long after it was made
    REQUIRE(h.core.takeListing(makeListing(old.key, 3, "AGED", true, kNow - 200 * kDay), old.board) == Took::Stored);
    REQUIRE(r3.core.needFilesHistory());
    {
        Said s = syncFiles(r3, h, 1);
        CHECK(hasLine(s.fromDialer, "UWANT un_chat,un_tech,un_games,un_files"));
        CHECK(listingLinesIn(s.fromAnswerer, "U ") == 4);
        CHECK(hasLine(s.fromAnswerer, "UEND F"));
        CHECK_FALSE(r3.core.needFilesHistory());
        CHECK(hasLine(r3.core.saveCursors(), "fhistory 1"));
        CHECK(r3.page().size() == 3);
        std::vector<std::string> names;
        for (const auto& l : r3.records()) names.push_back(l.name + (l.gone ? "-gone" : ""));
        std::sort(names.begin(), names.end());
        CHECK(names == std::vector<std::string>{"GONE-gone", "LIVE1", "LIVE2", "LIVE3"});
    }
    {  // done: a second session asks for no listings
        Said s = syncFiles(r3, h, 3);
        CHECK(countPrefix(s.fromDialer, "UWANT") == 0);
        CHECK(listingLinesIn(s.fromAnswerer, "U ") == 0);
    }
    {  // a Phase 2 board's pull: no L lines and a plain UEND
        Said s = syncFiles(r2, h, 5);
        CHECK(countPrefix(s.fromDialer, "UWANT") == 1);
        CHECK(listingLinesIn(s.fromAnswerer, "U ") == 0);
        CHECK(hasLine(s.fromAnswerer, "UEND"));
        CHECK_FALSE(hasLine(s.fromAnswerer, "UEND F"));
    }
}

TEST_CASE("a peer that holds no listings ends the pull with UEND F and that marks it done") {
    RevKeys rk;
    FNode h(0xC9), r3(0xCA);
    for (FNode* n : {&h, &r3}) n->admit({&h.board, &r3.board});
    REQUIRE(r3.core.needFilesHistory());
    Said s = syncFiles(r3, h, 1);
    CHECK(listingLinesIn(s.fromAnswerer, "U ") == 0);
    CHECK(hasLine(s.fromAnswerer, "UEND F"));
    CHECK_FALSE(r3.core.needFilesHistory());
}

TEST_CASE("an upgraded Phase 2 board with uhistory done and no fhistory line asks for the listings once") {
    RevKeys rk;
    FNode h(0xCB), r3(0xCC);
    for (FNode* n : {&h, &r3}) n->admit({&h.board, &r3.board});
    REQUIRE(!h.core.originateListing(makeListing(h.key, 1, "OLDER.ZIP")).empty());
    r3.core.loadCursors("history 1\nuhistory 1\n");
    CHECK_FALSE(r3.core.needUserHistory());
    CHECK(r3.core.needFilesHistory());
    Said s = syncFiles(r3, h, 1);
    CHECK(countPrefix(s.fromDialer, "UWANT") == 1);
    CHECK(listingLinesIn(s.fromAnswerer, "U ") == 1);
    CHECK(hasLine(s.fromAnswerer, "UEND F"));
    CHECK(hasLine(r3.core.saveCursors(), "fhistory 1"));
}

TEST_CASE("a pull of 600 listings is past the 512 cap on applications and votes and still reaches UEND F") {
    RevKeys rk;
    FNode h(0xCD), r3(0xCE);
    for (FNode* n : {&h, &r3}) n->admit({&h.board, &r3.board});
    for (int i = 0; i < 600; i++) REQUIRE(!h.core.originateListing(makeListing(h.key, 1, "M" + std::to_string(i))).empty());
    Said s = syncFiles(r3, h, 1);
    CHECK(listingLinesIn(s.fromAnswerer, "U ") == 600);
    CHECK(hasLine(s.fromAnswerer, "UEND F"));
    CHECK(r3.records().size() == 600);
    CHECK_FALSE(r3.core.needFilesHistory());
    Said again = syncFiles(r3, h, 3);
    CHECK(countPrefix(again.fromDialer, "UWANT") == 0);
}

TEST_CASE("A, V and L E lines share the per-session cap; past it NAK store") {
    RevKeys rk;
    FNode b(0xD0), x(0xD2);
    RawDialer r(0xD1);
    b.knows(x);
    b.admit({&b.board, &x.board, &r.card.board});
    NetSession s(b.core, b.card, b.key, nonce(2), false, "");
    r.handshake(s);
    REQUIRE(s.proven());
    s.tick(0, true);
    s.takeOut();
    RawDialer::say(s, "SEQ 0");
    uint32_t i = 0;
    for (; i < 32; i++) RawDialer::say(s, "E " + std::to_string(i) + " " + encode(makeListing(r.key, i + 1, "R" + std::to_string(i))));
    for (; i < kSyncUserEMax; i++) RawDialer::say(s, "E " + std::to_string(i) + " " + encode(makeListing(x.key, i + 1, "X" + std::to_string(i))));
    std::string out = s.takeOut();
    CHECK(countPrefix(out, "ACK ") == kSyncUserEMax);
    RawDialer::say(s, "E " + std::to_string(i) + " " + encode(makeApp(keyOf(0xD3))));  // an application, past the cap
    CHECK(countPrefix(s.takeOut(), "NAK " + std::to_string(i) + " store") == 1);
    i++;
    RawDialer::say(s, "E " + std::to_string(i) + " " + encode(makeVote(keyOf(0xD3), boardOf(x.key), 1, 1, true)));  // a vote
    CHECK(countPrefix(s.takeOut(), "NAK " + std::to_string(i) + " store") == 1);
    i++;
    RawDialer::say(s, "E " + std::to_string(i) + " " + encode(makeListing(x.key, 200, "LATE")));
    CHECK(countPrefix(s.takeOut(), "NAK " + std::to_string(i) + " store") == 1);
    CHECK_FALSE(s.failed());
    CHECK(b.records().size() == kSyncUserEMax);
}

TEST_CASE("on E lines the 9th replacing listing of a session is NAKed store, transient") {
    RevKeys rk;
    FNode b(0xD4);
    RawDialer r(0xD5);
    b.admit({&b.board, &r.card.board});
    NetSession s(b.core, b.card, b.key, nonce(2), false, "");
    r.handshake(s);
    REQUIRE(s.proven());
    s.tick(0, true);
    s.takeOut();
    RawDialer::say(s, "SEQ 0");
    for (uint32_t i = 0; i < 10; i++) {
        RawDialer::say(s, "E " + std::to_string(i) + " " + encode(makeListing(r.key, i + 1, "SAME.ZIP")));
        indexReady(b.core);  // the host's tick builds the index again between lines
    }
    std::string out = s.takeOut();
    CHECK(countPrefix(out, "ACK ") == 9);  // the first, then 8 that replace
    CHECK(countPrefix(out, "NAK 9 store") == 1);
    CHECK(b.liveSeqs("SAME.ZIP") == std::vector<uint32_t>{9});
}

TEST_CASE("fhistory: a nocard refusal does not hold the listings pull") {
    RevKeys rk;
    // A hand-driven net3 peer answers r3's UWANT with a listing from x, whose card r3 does not hold (a real peer would
    // have sent it in its snapshot), then UEND F.
    FNode     r3(0xD7);
    RawDialer r(0xD6);
    const KeyPair x = keyOf(0xD8);
    r.card.links = kNetCardLinks;
    REQUIRE(signCard(r.card, r.key));
    r3.admit({&r3.board, &r.card.board});
    REQUIRE(r3.seal("admit", boardOf(x), 90));
    NetSession s(r3.core, r3.card, r3.key, nonce(2), false, "");
    r.handshake(s);
    REQUIRE(s.proven());
    s.tick(0, true);
    std::string asked = s.takeOut();
    CHECK(hasLine(asked, "UWANT un_chat,un_tech,un_games,un_files"));
    RawDialer::say(s, "SEQ 0");
    RawDialer::say(s, "SEND");
    RawDialer::say(s, "U " + encode(makeListing(x, 1, "NOCARD.ZIP")));
    RawDialer::say(s, "UEND F");
    s.tick(10, true);
    CHECK_FALSE(s.failed());
    CHECK(r3.records().empty());  // x's card is not held here: nocard
    CHECK_FALSE(r3.core.needFilesHistory());
    CHECK(hasLine(r3.core.saveCursors(), "fhistory 1"));
}

TEST_CASE("fhistory: store refusals hold the pull for 3 pulls in a row, then it is done") {
    RevKeys rk;
    HeldPull p(0xD9, 0xDA, 0xDB);
    syncFiles(p.r3, p.h, 1);
    CHECK(p.r3.core.needFilesHistory());
    CHECK(hasLine(p.cursors(), "fhistory 0"));
    CHECK(hasLine(p.cursors(), "fheld 1"));
    syncFiles(p.r3, p.h, 3);
    CHECK(p.r3.core.needFilesHistory());
    CHECK(hasLine(p.cursors(), "fheld 2"));
    syncFiles(p.r3, p.h, 5);
    CHECK_FALSE(p.r3.core.needFilesHistory());
    CHECK(hasLine(p.cursors(), "fhistory 1"));
    CHECK(countPrefix(p.cursors(), "fheld") == 0);
    CHECK(p.r3.liveSeqs("NAME.ZIP") == std::vector<uint32_t>{2});  // never replaced: the budget stayed spent
}

TEST_CASE("fhistory: the held count survives a save and load of the cursors") {
    RevKeys rk;
    HeldPull p(0xDC, 0xDD, 0xDE);
    syncFiles(p.r3, p.h, 1);
    std::string saved = p.cursors();
    CHECK(hasLine(saved, "fheld 1"));
    p.r3.core.loadCursors(saved);
    CHECK(hasLine(p.cursors(), "fheld 1"));
    syncFiles(p.r3, p.h, 3);
    CHECK(p.r3.core.needFilesHistory());
    syncFiles(p.r3, p.h, 5);
    CHECK_FALSE(p.r3.core.needFilesHistory());
}

TEST_CASE("fhistory: a held pull then a clean one is done, and the held count is cleared") {
    RevKeys rk;
    HeldPull p(0xE0, 0xE1, 0xE2);
    syncFiles(p.r3, p.h, 1);
    CHECK(hasLine(p.cursors(), "fheld 1"));
    p.r3.now += kDay;  // a new day: the budget is back
    syncFiles(p.r3, p.h, 3);
    CHECK_FALSE(p.r3.core.needFilesHistory());
    CHECK(countPrefix(p.cursors(), "fheld") == 0);
    CHECK(p.r3.liveSeqs("NAME.ZIP") == std::vector<uint32_t>{5});
}

TEST_CASE("fhistory: a pull that reaches the session's listing cap counts as held; three in a row end it") {
    RevKeys rk;
    // A hand-driven net3 peer answers r3's UWANT with kSyncUserListingsMax listings (from x, whose card r3 does not
    // hold: each is read, counted and refused nocard), then UEND F. A cap reached is not every listing the peer holds.
    FNode     r3(0xE3);
    RawDialer r(0xE4);
    const KeyPair x = keyOf(0xE5);
    r.card.links = kNetCardLinks;
    REQUIRE(signCard(r.card, r.key));
    r3.admit({&r3.board, &r.card.board});
    REQUIRE(r3.seal("admit", boardOf(x), 90));
    std::vector<std::string> us;
    for (uint32_t i = 0; i < kSyncUserListingsMax; i++) us.push_back("U " + encode(makeListing(x, i + 1, "C" + std::to_string(i))));
    for (int pull = 1; pull <= 3; pull++) {
        NetSession s(r3.core, r3.card, r3.key, nonce((uint8_t)(10 + pull)), false, "");
        r.handshake(s);
        REQUIRE(s.proven());
        s.tick(0, true);
        CHECK(hasLine(s.takeOut(), "UWANT un_chat,un_tech,un_games,un_files"));
        RawDialer::say(s, "SEQ 0");
        RawDialer::say(s, "SEND");
        for (const std::string& u : us) RawDialer::say(s, u);
        RawDialer::say(s, "UEND F");
        s.tick(10, true);
        CHECK_FALSE(s.failed());
        if (pull < 3) {
            CHECK(r3.core.needFilesHistory());
            CHECK(hasLine(r3.core.saveCursors(), "fheld " + std::to_string(pull)));
        }
    }
    CHECK_FALSE(r3.core.needFilesHistory());
    CHECK(hasLine(r3.core.saveCursors(), "fhistory 1"));
    CHECK(countPrefix(r3.core.saveCursors(), "fheld") == 0);
}
