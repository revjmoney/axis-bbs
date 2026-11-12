// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// NetSession: two boards syncing in memory, the handshake's checks, and the session's limits.
#include <algorithm>

#include "net_testutil.h"

using namespace nettest;
using Took = NetCore::Took;

namespace {
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

    // 10 minutes at most, however busy.
    NetSession u(b.core, b.card, b.key, nonce(4), false, "");
    RawDialer r2(0xBA);
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
