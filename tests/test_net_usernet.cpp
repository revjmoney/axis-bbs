// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// User Net on whole nodes: posts to un_ areas and their NAKs, Rev's veto and admit, the User Net pull,
// applications and votes relayed by a board that carries no User Net area, and who may post to the nets.
#include <cctype>

#include "net_testutil.h"
#include "bbs/users.h"

using namespace nettest;
using Status = NetMembers::Status;

namespace {

// Lines "NAK <index> <word>" in out.
size_t naks(const std::string& out, const std::string& word) {
    size_t n = 0;
    for (const auto& l : lines(out))
        if (l.compare(0, 4, "NAK ") == 0 && l.size() > word.size() && l.compare(l.size() - word.size() - 1, std::string::npos, " " + word) == 0) n++;
    return n;
}

bool hasLine(const std::string& text, const std::string& l) {
    for (const auto& x : lines(text))
        if (x == l) return true;
    return false;
}

struct Said {
    std::string fromDialer, fromAnswerer;
};

// One session, d dialing a, run to the end; both must finish.
Said sync(Node& d, Node& a, uint8_t n) {
    NetSession sd(d.core, d.card, d.key, nonce(n), true, a.board);
    NetSession sa(a.core, a.card, a.key, nonce((uint8_t)(n + 1)), false, "");
    Said s;
    runBoth(sd, sa, s.fromDialer, s.fromAnswerer);
    CHECK_MESSAGE(sd.done(), sd.why());
    CHECK_MESSAGE(sa.done(), sa.why());
    return s;
}

bool sameHandle(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); i++)
        if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i])) return false;
    return true;
}

// The board's message store as main's NetStore applies mayPostToNets: a post to an sn_ or un_ area from a caller with
// net posts off, a twit or no account (other than the board's own sysop, by handle) is refused before anything is
// signed; everything else goes to NetPostingStore.
struct GatedStore : NetPostingStore {
    axis::bbs::UserStore& users;
    std::string sysop;
    GatedStore(axis::bbs::MessageStore& inner, NetCore& core, axis::bbs::UserStore& u, std::string s)
        : NetPostingStore(inner, core), users(u), sysop(std::move(s)) {}
    int post(const std::string& area, const axis::bbs::Message& m) override {
        if (isNetAreaId(area)) {
            const axis::bbs::User* u = users.find(m.from);
            if (u ? !mayPostToNets(u) : !sameHandle(m.from, sysop)) return 0;
        }
        return NetPostingStore::post(area, m);
    }
};

axis::bbs::Message message(const std::string& from, const std::string& subject = "Hi") {
    axis::bbs::Message m;
    m.from = from;
    m.to = "All";
    m.subject = subject;
    m.body = "Body";
    return m;
}

}  // namespace

TEST_CASE("un_ post rules") {
    RevKeys rk;
    {  // A board that does not carry the area NAKs area. Permanent: the sender's cursor moves past it.
        UNode a(0xC1), b(0xC2, false);
        for (UNode* n : {&a, &b}) {
            REQUIRE(n->seal("admit", a.board, 1));
            REQUIRE(n->seal("admit", b.board, 2));
        }
        a.core.addLinkPeer(b.board, true);
        REQUIRE(!a.post("un_chat", "hello").empty());
        Said s = sync(a, b, 1);
        CHECK(naks(s.fromAnswerer, "area") == 1);
        CHECK(b.ids("un_chat").empty());
        CHECK(a.core.pendingFor(b.board, 8).empty());
    }
    {  // Line by line: a non-member origin is NAKed member; once Rev admits it, its next post is taken.
        UNode b(0xC4);
        REQUIRE(b.seal("admit", b.board, 1));
        NetSession s(b.core, b.card, b.key, nonce(2), false, "");
        RawDialer r(0xC5);
        r.handshake(s);
        REQUIRE(s.proven());
        s.tick(0, true);
        s.takeOut();
        RawDialer::say(s, "SEQ 0");
        RawDialer::say(s, "E 0 " + encode(makePost(r.key, 1, kNow, "un_chat")));
        CHECK(countPrefix(s.takeOut(), "NAK 0 member") == 1);
        CHECK_FALSE(s.failed());
        REQUIRE(b.seal("admit", r.card.board, 2));
        RawDialer::say(s, "E 1 " + encode(makePost(r.key, 2, kNow, "un_chat")));
        CHECK(countPrefix(s.takeOut(), "ACK 1") == 1);
        CHECK(b.ids("un_chat").size() == 1);
    }
    {  // A board that carries the area but is no member itself NAKs area.
        UNode c(0xC6);
        NetSession s(c.core, c.card, c.key, nonce(2), false, "");
        RawDialer r(0xC7);
        REQUIRE(c.seal("admit", r.card.board, 1));
        r.handshake(s);
        REQUIRE(s.proven());
        s.tick(0, true);
        s.takeOut();
        RawDialer::say(s, "SEQ 0");
        RawDialer::say(s, "E 0 " + encode(makePost(r.key, 1, kNow, "un_chat")));
        CHECK(countPrefix(s.takeOut(), "NAK 0 area") == 1);
        CHECK(c.ids("un_chat").empty());
        // A Sysop Net post on the same session is taken as ever.
        RawDialer::say(s, "E 1 " + encode(makePost(r.key, 2, kNow, "sn_support")));
        CHECK(countPrefix(s.takeOut(), "ACK 1") == 1);
    }
    {  // A board that is no member cannot post to a User Net area at all.
        UNode d(0xC8);
        CHECK(d.post("un_chat", "not yet").empty());
        REQUIRE(d.seal("admit", d.board, 1));
        CHECK(!d.post("un_chat", "now").empty());
        d.core.setUserAreas({"un_tech"});
        CHECK(d.post("un_chat", "not carried").empty());
        CHECK(!d.post("un_tech", "carried").empty());
    }
}

TEST_CASE("veto after admit removes, later admit restores") {
    RevKeys rk;
    UNode a(0xC9), c(0xCA);
    for (UNode* n : {&a, &c}) {
        REQUIRE(n->seal("admit", a.board, 1));
        REQUIRE(n->seal("admit", c.board, 2));
    }
    c.core.addLinkPeer(a.board, true);
    REQUIRE(!c.post("un_chat", "first").empty());
    sync(c, a, 1);
    CHECK(a.ids("un_chat").size() == 1);

    REQUIRE(a.seal("veto", c.board, 3));  // reaches A first
    CHECK(a.core.members().status(c.board) == Status::Vetoed);
    REQUIRE(!c.post("un_chat", "second").empty());  // C has not heard of the veto yet
    Said s = sync(c, a, 3);
    CHECK(naks(s.fromAnswerer, "member") == 1);
    CHECK(a.ids("un_chat").size() == 1);
    CHECK(c.core.pendingFor(a.board, 8).empty());  // a permanent NAK: C's cursor moved past the post
    CHECK(c.core.members().status(c.board) == Status::Vetoed);  // the veto came over in A's snapshot
    CHECK(c.post("un_chat", "while vetoed").empty());

    for (UNode* n : {&a, &c}) REQUIRE(n->seal("admit", c.board, 4));
    CHECK(a.core.members().status(c.board) == Status::Member);
    CHECK(c.core.selfMember());
    REQUIRE(!c.post("un_chat", "back again").empty());
    sync(c, a, 5);
    CHECK(a.ids("un_chat").size() == 2);
}

TEST_CASE("User Net pull") {
    RevKeys rk;
    UNode a(0xD1), b(0xD3);
    Node plain(0xD2);  // a card without net2, as a v0.53 board's
    const KeyPair X = keyOf(0xD4), Y = keyOf(0xD5), Z = keyOf(0xD6), V1 = keyOf(0xD7), V2 = keyOf(0xD8);
    for (UNode* n : {&a, &b}) {
        REQUIRE(n->seal("admit", a.board, 1));
        REQUIRE(n->seal("admit", b.board, 2));
        REQUIRE(n->seal("admit", boardOf(V1), 3));
        REQUIRE(n->seal("admit", boardOf(V2), 4));
    }
    for (const KeyPair* k : {&X, &Y, &Z}) REQUIRE(a.core.takeApp(makeApp(*k), ""));
    REQUIRE(a.core.takeVote(makeVote(V1, boardOf(X), 1, 1, true), ""));
    REQUIRE(a.core.takeVote(makeVote(V2, boardOf(X), 1, 1, false), ""));
    REQUIRE(a.core.takeVote(makeVote(V1, boardOf(Y), 1, 1, true), ""));
    REQUIRE(a.core.members().offsets().size() == 6);
    for (int i = 0; i < 105; i++) REQUIRE(!a.post("un_chat", "chat " + std::to_string(i)).empty());
    for (int i = 0; i < 3; i++) REQUIRE(!a.post("un_tech", "tech " + std::to_string(i)).empty());

    {  // A peer without net2 in its card never gets UWANT (and asks for nothing, as a v0.53 board).
        plain.core.userHistoryGot();
        Said s = sync(a, plain, 1);
        CHECK(countPrefix(s.fromDialer, "UWANT") == 0);
        CHECK(countPrefix(s.fromDialer, "U ") == 0);
        CHECK(countPrefix(s.fromDialer, "UEND") == 0);
        CHECK(plain.core.members().appCount() == 0);
    }

    // B: history done, the User Net pull not.
    b.core.loadCursors("history 1\nuhistory 0\n");
    REQUIRE(b.core.historyState() == 1);
    REQUIRE(b.core.needUserHistory());
    {
        Said s = sync(b, a, 3);
        CHECK(hasLine(s.fromDialer, "UWANT un_chat,un_tech,un_games,un_files"));
        CHECK(countPrefix(s.fromDialer, "WANT ") == 0);
        CHECK(countPrefix(s.fromAnswerer, "U A") == 3);
        CHECK(countPrefix(s.fromAnswerer, "U V") == 3);
        CHECK(countPrefix(s.fromAnswerer, "U P") == 103);  // 100 of un_chat's 105, all 3 of un_tech
        CHECK(countPrefix(s.fromAnswerer, "UEND") == 1);
        CHECK(b.core.members().appCount() == 3);
        CHECK(b.core.members().voteCount() == 3);
        CHECK(b.core.members().tally(boardOf(X)).yes == 1);
        CHECK(b.core.members().tally(boardOf(X)).no == 1);
        CHECK(b.ids("un_chat").size() == kNetUserHistoryPerArea);
        CHECK(b.ids("un_tech").size() == 3);
        CHECK(b.apps.splitText().size() == 6);  // each record taken went to B's apps file
    }
    CHECK_FALSE(b.core.needUserHistory());
    CHECK(hasLine(b.core.saveCursors(), "uhistory 1"));
    CHECK(hasLine(b.core.saveCursors(), "history 1"));
    b.core.loadCursors(b.core.saveCursors());
    CHECK_FALSE(b.core.needUserHistory());
    {  // Done: no UWANT.
        Said s = sync(b, a, 5);
        CHECK(countPrefix(s.fromDialer, "UWANT") == 0);
    }
    // The host resets the flag when the board becomes a member or carries another area: asked again.
    b.core.resetUserHistory();
    CHECK(b.core.needUserHistory());
    CHECK(hasLine(b.core.saveCursors(), "uhistory 0"));
    {
        Said s = sync(b, a, 7);
        CHECK(countPrefix(s.fromDialer, "UWANT ") == 1);
        CHECK(countPrefix(s.fromAnswerer, "UEND") == 1);
    }
    CHECK_FALSE(b.core.needUserHistory());
}

TEST_CASE("a board voted in by three members asks for the User Net posts") {
    RevKeys rk;
    UNode x(0xE5), a(0xE6);
    const KeyPair M1 = keyOf(0xE7), M2 = keyOf(0xE8), M3 = keyOf(0xE9);
    for (UNode* n : {&x, &a}) {
        REQUIRE(n->seal("admit", boardOf(M1), 1));
        REQUIRE(n->seal("admit", boardOf(M2), 2));
        REQUIRE(n->seal("admit", boardOf(M3), 3));
        REQUIRE(n->seal("admit", a.board, 4));
    }
    REQUIRE(!a.post("un_chat", "one").empty());
    REQUIRE(!a.post("un_chat", "two").empty());
    NetApp app = makeApp(x.key);
    REQUIRE(x.core.takeApp(app, ""));
    CHECK(x.core.members().status(x.board) == Status::Pending);
    {  // Not a member: it asks for the applications and votes only.
        Said s = sync(x, a, 1);
        CHECK(hasLine(s.fromDialer, "UWANT -"));
        CHECK(x.ids("un_chat").empty());
        CHECK(a.core.members().status(x.board) == Status::Pending);  // A has its application now
    }
    for (const KeyPair* m : {&M1, &M2, &M3}) REQUIRE(x.core.takeVote(makeVote(*m, x.board, 1, 1, true), ""));
    CHECK(x.core.members().status(x.board) == Status::Member);
    CHECK(x.core.selfMember());
    x.core.resetUserHistory();  // as the host does on becoming a member
    {
        Said s = sync(x, a, 3);
        CHECK(hasLine(s.fromDialer, "UWANT un_chat,un_tech,un_games,un_files"));
        CHECK(x.ids("un_chat").size() == 2);
        CHECK(a.core.members().status(x.board) == Status::Member);  // the three votes went over too
    }
}

TEST_CASE("an application and a vote go out on E lines to a net2 link peer") {
    RevKeys rk;
    UNode a(0xF0), b(0xF1);
    const KeyPair X = keyOf(0xF2), V = keyOf(0xF3);
    a.core.userHistoryGot();  // no User Net pull either way: only E lines can carry the records
    b.core.userHistoryGot();
    a.core.addLinkPeer(b.board, true);
    NetApp app = makeApp(X);
    NetVote v = makeVote(V, boardOf(X), 1, 1, true);
    REQUIRE(a.core.takeApp(app, ""));
    REQUIRE(a.core.takeVote(v, ""));
    REQUIRE(a.core.members().offsets().size() == 2);  // both lines are in the apps file, their offsets known
    auto pend = a.core.pendingFor(b.board, 8, true);
    CHECK(pend.size() == 2);
    if (pend.size() == 2) {
        CHECK(pend[0].second == encode(app));
        CHECK(pend[1].second == encode(v));
    }
    Said s = sync(a, b, 1);
    CHECK(countPrefix(s.fromDialer, "E ") == 2);
    CHECK(b.core.members().appCount() == 1);
    CHECK(b.core.members().voteCount() == 1);
}

TEST_CASE("applications and votes cross a board that carries no User Net area") {
    RevKeys rk;
    UNode a(0xEA), b(0xEB, false), c(0xEC);
    const KeyPair X = keyOf(0xED), V = keyOf(0xEE);
    a.core.addLinkPeer(b.board, true);
    b.core.addLinkPeer(c.board, true);
    REQUIRE(a.core.takeApp(makeApp(X), ""));
    REQUIRE(a.core.takeVote(makeVote(V, boardOf(X), 1, 1, true), ""));
    sync(a, b, 1);
    CHECK(b.core.members().appCount() == 1);
    CHECK(b.core.members().voteCount() == 1);
    CHECK(b.core.members().status(boardOf(X)) == Status::Pending);
    sync(b, c, 3);
    CHECK(c.core.members().appCount() == 1);
    CHECK(c.core.members().voteCount() == 1);
}

TEST_CASE("no-net caller cannot post to net areas") {
    RevKeys rk;
    using axis::bbs::User;
    User ok, nonet, twit;
    ok.handle = "Spindle";
    nonet.handle = "Quiet";
    nonet.permNets = false;
    twit.handle = "Shadow";
    twit.twit = true;
    CHECK_FALSE(mayPostToNets(nullptr));
    CHECK_FALSE(mayPostToNets(&nonet));
    CHECK_FALSE(mayPostToNets(&twit));
    CHECK(mayPostToNets(&ok));
    User both = nonet;
    both.twit = true;
    CHECK_FALSE(mayPostToNets(&both));

    UNode n(0xEF);
    REQUIRE(n.seal("admit", n.board, 1));
    axis::bbs::UserStore users;
    users.add(ok);
    users.add(nonet);
    users.add(twit);
    GatedStore g(n.store, n.core, users, "Sysop");
    CHECK(g.post("sn_support", message("Quiet")) == 0);
    CHECK(g.post("un_chat", message("Quiet")) == 0);
    CHECK(g.post("sn_support", message("shadow")) == 0);
    CHECK(g.post("sn_support", message("Nobody")) == 0);  // no account, and not the sysop
    CHECK(n.ids("sn_support").empty());
    CHECK(n.ids("un_chat").empty());
    CHECK(n.core.postSeq() == 1);  // nothing was signed
    // A local area still takes the post.
    int num = g.post("general", message("Quiet", "local"));
    CHECK(num > 0);
    axis::bbs::Message back;
    REQUIRE(n.files.read("general", num, back));
    CHECK(back.from == "Quiet");
    CHECK(back.net.empty());
    // Callers allowed, and the board's own sysop by handle, go out on the nets.
    CHECK(g.post("sn_support", message("Spindle")) > 0);
    CHECK(g.post("un_chat", message("Spindle")) > 0);
    CHECK(g.post("sn_support", message("sysop")) > 0);
    CHECK(n.ids("sn_support").size() == 2);
    CHECK(n.ids("un_chat").size() == 1);
}
