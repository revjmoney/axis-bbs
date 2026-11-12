// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// NetCore: taking and making posts, flooding through the outbound log, link peer cursors and pools,
// retention, Rev-only areas and the posting store.
#include <algorithm>
#include <cstdio>
#include <ctime>

#include "net_testutil.h"

using namespace nettest;
using axis::bbs::Message;
using Took = NetCore::Took;

namespace {
uint32_t cursorOf(NetCore& core, const std::string& peer) {
    for (const auto& l : lines(core.saveCursors()))
        if (l.compare(0, peer.size(), peer) == 0) return (uint32_t)std::stoul(l.substr(peer.size() + 1));
    return 0xFFFFFFFFu;
}
std::string storedLine(Node& n, const std::string& id, const std::string& area = "sn_support") {
    for (int i = 1; i <= 2000; i++) {
        Message m;
        if (n.files.read(area, i, m) && netPostId(m.net) == id) return m.net;
    }
    return "";
}
}  // namespace

TEST_CASE("flood does not echo to path") {
    RevKeys rk;
    Node a(0x51), b(0x52), c(0x53);
    a.knows(b), a.knows(c), b.knows(a), b.knows(c), c.knows(a), c.knows(b);
    b.core.addLinkPeer(a.board, true);
    b.core.addLinkPeer(c.board, true);
    c.core.addLinkPeer(a.board, true);
    c.core.addLinkPeer(b.board, true);

    std::string line = a.post();
    REQUIRE(!line.empty());
    NetPost p = decodePost(line);
    CHECK(b.core.take(p, a.board) == Took::Stored);
    CHECK(b.pendingPosts(c.board).size() == 1);  // queued for C only
    CHECK(b.pendingPosts(a.board).empty());      // never back to the sender

    std::string relayed = storedLine(b, p.id);
    REQUIRE(!relayed.empty());
    NetPost viaB = decodePost(relayed);
    CHECK(viaB.path == a.board);  // B added the hop it came from
    CHECK(c.core.take(viaB, b.board) == Took::Stored);
    CHECK(c.pendingPosts(a.board).empty());  // A is on its path (and its origin)
    CHECK(c.pendingPosts(b.board).empty());  // B is its sender
    CHECK(decodePost(storedLine(c, p.id)).path == a.board + "," + b.board);
    CHECK(c.core.take(viaB, b.board) == Took::Duplicate);
    CHECK(b.core.take(p, c.board) == Took::Duplicate);

    // A fourth board D takes it from C. B is neither its origin nor its sender, only on its path,
    // and still gets nothing; an unrelated link peer E gets it.
    Node d(0x5B), e(0x5C);
    d.knows(a), d.knows(c);
    d.core.addLinkPeer(b.board, true);
    d.core.addLinkPeer(e.board, true);
    CHECK(d.core.take(decodePost(storedLine(c, p.id)), c.board) == Took::Stored);
    CHECK(d.pendingPosts(b.board).empty());
    CHECK(d.pendingPosts(e.board).size() == 1);
}

TEST_CASE("NetCore pins its own card and its link peers' cards") {
    RevKeys rk;
    Node n(0x5D), link(0x5E);
    Node rev(0xA0, kNow, "Rev");
    n.knows(rev);
    n.core.addLinkPeer(link.board, true);
    n.knows(link);
    for (int i = 0; i < 40; i++) n.core.takeCard(makeCard(keyOf((uint8_t)(0x10 + i)), "Spam"), "");
    CHECK(n.core.state().cardCount() <= kNetMaxCards);
    CHECK(n.core.state().card(n.board) != nullptr);
    CHECK(n.core.state().card(link.board) != nullptr);
    CHECK(n.core.state().card(rev.board) != nullptr);
}

TEST_CASE("banned board's posts dropped when relayed") {
    RevKeys rk;
    Node a(0x54), b(0x55), c(0x56), x(0x57);
    b.knows(a), b.knows(x);
    b.core.addLinkPeer(c.board, true);
    REQUIRE(b.core.takeNotice(makeNotice(rev0(), 0, "ban", x.board, 1), ""));
    std::string line = x.post();
    REQUIRE(!line.empty());
    CHECK(b.core.take(decodePost(line), a.board) == Took::Banned);
    CHECK(b.ids().empty());               // not stored
    CHECK(b.pendingPosts(c.board).empty());  // not queued
}

TEST_CASE("P entries from a board banned after the post was stored are skipped") {
    RevKeys rk;
    Node b(0x58), c(0x59), x(0x5A);
    b.knows(x);
    b.core.addLinkPeer(c.board, true);
    REQUIRE(b.core.take(decodePost(x.post()), x.board) == Took::Stored);
    REQUIRE(b.core.takeNotice(makeNotice(rev0(), 0, "ban", x.board, 1), ""));
    CHECK(b.pendingPosts(c.board).empty());
    // the ban notice itself still goes out
    auto pend = b.core.pendingFor(c.board, 8);
    REQUIRE(pend.size() == 1);
    CHECK(pend[0].second[0] == 'N');
}

TEST_CASE("NetCore: ingress limit drops excess, the next day takes it") {
    RevKeys rk;
    Node a(0x61), b(0x62);
    b.knows(a);
    REQUIRE(b.core.takeNotice(makeNotice(rev0(), 0, "limits", "net", 1, "posts=3,bytes=100000"), ""));
    std::vector<NetPost> ps;
    for (int i = 0; i < 5; i++) ps.push_back(decodePost(a.post("sn_support", "S" + std::to_string(i))));
    CHECK(b.core.take(ps[0], a.board) == Took::Stored);
    CHECK(b.core.take(ps[1], a.board) == Took::Stored);
    CHECK(b.core.take(ps[2], a.board) == Took::Stored);
    CHECK(b.core.take(ps[3], a.board) == Took::Limited);  // the 4th that day
    b.now += kDay;
    CHECK(b.core.take(ps[3], a.board) == Took::Stored);  // refused posts are not marked seen
    CHECK(b.core.take(ps[4], a.board) == Took::Stored);
}

TEST_CASE("history posts skip ingress, every other check stays") {
    RevKeys rk;
    Node a(0x63), b(0x64);
    b.knows(a);
    REQUIRE(b.core.takeNotice(makeNotice(rev0(), 0, "limits", "net", 1, "posts=1,bytes=100000"), ""));
    std::vector<NetPost> ps;
    for (int i = 0; i < 5; i++) ps.push_back(decodePost(a.post()));
    CHECK(b.core.take(ps[0], a.board, true) == Took::Stored);
    CHECK(b.core.take(ps[1], a.board, true) == Took::Stored);
    CHECK(b.core.take(ps[2], a.board, true) == Took::Stored);
    CHECK(b.core.take(ps[3], a.board) == Took::Stored);  // history did not count
    CHECK(b.core.take(ps[4], a.board) == Took::Limited);
    NetPost forged = ps[4];
    forged.subject = "forged";
    CHECK(b.core.take(forged, a.board, true) == Took::Rejected);
}

TEST_CASE("seen is marked only after the post is stored") {
    RevKeys rk;
    Node a(0x65), b(0x66);
    NetPost p = decodePost(a.post());
    CHECK(b.core.take(p, a.board) == Took::Rejected);  // no card for A yet
    CHECK_FALSE(b.core.seen().has(p.id));
    b.knows(a);
    CHECK(b.core.take(p, a.board) == Took::Stored);
    CHECK(b.core.seen().has(p.id));
}

TEST_CASE("date window: old and future posts refused when the clock is set") {
    RevKeys rk;
    KeyPair ak = keyOf(0x67);
    Node b(0x68);
    REQUIRE(b.core.takeCard(makeCard(ak, "A"), ""));
    CHECK(b.core.take(makePost(ak, 1, kNow - 91 * kDay), "") == Took::Rejected);
    CHECK(b.core.take(makePost(ak, 2, kNow + 2 * kDay), "") == Took::Rejected);
    CHECK(b.core.take(makePost(ak, 3, kNow - 89 * kDay), "") == Took::Stored);
    CHECK(b.core.take(makePost(ak, 4, kNow + kDay / 2), "") == Took::Stored);

    Node c(0x69, 1000);  // no clock: no window
    REQUIRE(c.core.takeCard(makeCard(ak, "A"), ""));
    CHECK(c.core.take(makePost(ak, 1, kNow - 400 * kDay), "") == Took::Stored);
    CHECK(c.core.take(makePost(ak, 2, kNow + 400 * kDay), "") == Took::Stored);
}

TEST_CASE("rev-only areas") {
    RevKeys rk;
    Node a(0x6A), b(0x6B);
    Node rev(0xA0, kNow, "Rev");  // its card carries Rev's key 0
    b.knows(a), b.knows(rev);
    CHECK(a.post("sn_announce").empty());  // originate refuses
    CHECK(b.core.take(makePost(a.key, 1, kNow, "sn_announce"), a.board) == Took::Rejected);
    CHECK(b.core.take(makePost(a.key, 2, kNow, "sn_updates"), a.board) == Took::Rejected);
    CHECK(b.core.take(makePost(a.key, 3, kNow, "general"), a.board) == Took::Rejected);  // not a Sysop Net area
    CHECK(b.core.isRevBoard(rev.board));
    CHECK_FALSE(b.core.isRevBoard(a.board));
    std::string line = rev.post("sn_announce", "News");
    REQUIRE(!line.empty());
    CHECK(b.core.take(decodePost(line), rev.board) == Took::Stored);
}

TEST_CASE("held posts hidden, killed posts gone") {
    RevKeys rk;
    Node a(0x6C), b(0x6D);
    b.knows(a);
    NetPost p1 = decodePost(a.post("sn_support", "one")), p2 = decodePost(a.post("sn_support", "two")),
            p3 = decodePost(a.post("sn_support", "three"));
    REQUIRE(b.core.take(p1, a.board) == Took::Stored);
    REQUIRE(b.core.take(p2, a.board) == Took::Stored);
    REQUIRE(b.core.take(p3, a.board) == Took::Stored);
    NetPostingStore ps(b.store, b.core);
    CHECK(ps.headers("sn_support").size() == 3);

    REQUIRE(b.core.takeNotice(makeNotice(rev0(), 0, "hold", p1.id, 1), ""));
    auto h = ps.headers("sn_support");
    CHECK(h.size() == 2);
    Message m;
    CHECK_FALSE(ps.read("sn_support", 1, m));
    CHECK(b.files.read("sn_support", 1, m));  // still stored (and still travels)

    REQUIRE(b.core.takeNotice(makeNotice(rev0(), 0, "lift", p1.id, 2), ""));
    CHECK(ps.headers("sn_support").size() == 3);
    CHECK(ps.read("sn_support", 1, m));

    REQUIRE(b.core.takeNotice(makeNotice(rev0(), 0, "kill", p2.id, 3), ""));
    CHECK(ps.headers("sn_support").size() == 2);
    auto ids = b.ids();
    CHECK(std::find(ids.begin(), ids.end(), p2.id) == ids.end());  // dropped from the store
    CHECK(b.core.take(p2, a.board) == Took::Rejected);              // and never taken back

    b.core.setLocalFilter(nullptr, [&](const std::string& id) { return id == p3.id; });
    CHECK(ps.headers("sn_support").size() == 1);
    CHECK_FALSE(ps.read("sn_support", 3, m));
    CHECK(b.core.hiddenHere(p3.id));

    // Posting through the store: sn_ areas go out on the net, others are local.
    Message local;
    local.from = "Me";
    local.subject = "local";
    local.body = "x";
    int n = ps.post("general", local);
    REQUIRE(n > 0);
    CHECK(b.files.read("general", n, m));
    CHECK(m.net.empty());
    n = ps.post("sn_bugs", local);
    REQUIRE(n > 0);
    CHECK(b.files.read("sn_bugs", n, m));
    CHECK(netPostId(m.net).compare(0, 16, b.board) == 0);
}

TEST_CASE("retention trims to 500 and 90 days") {
    RevKeys rk;
    // 510 posts in RAM (the file store takes about a minute for this many on Windows).
    MemStore ms;
    MemOutLog ml;
    KeyPair rk6e = keyOf(0x6E);
    NetCore r(ms, rk6e, [] { return kNow; }, ml);
    REQUIRE(r.setOwnCard(makeCard(rk6e, "R")));
    Message m;
    m.from = "Sysop";
    m.subject = "p";
    m.body = "b";
    for (int i = 0; i < 510; i++) REQUIRE(!r.originate("sn_bugs", m).empty());
    REQUIRE(ms.count("sn_bugs") == 510);
    r.trim(kNow);
    REQUIRE(ms.count("sn_bugs") == 500);
    CHECK(netPostId(ms.all("sn_bugs").front().net) == r.board() + "-11");  // the oldest went
    CHECK(netPostId(ms.all("sn_bugs").back().net) == r.board() + "-510");
    // a trim runs once a day at most
    for (int i = 0; i < 10; i++) REQUIRE(!r.originate("sn_bugs", m).empty());
    r.trim(kNow + 60);
    CHECK(ms.count("sn_bugs") == 510);
    r.trim(kNow + kDay);
    CHECK(ms.count("sn_bugs") == 500);

    Node q(0x6F);
    std::string oldId = netPostId(q.post("sn_support", "old"));
    q.now += 5 * kDay;
    std::string newId = netPostId(q.post("sn_support", "newer"));
    q.now = kNow + 92 * kDay;
    q.core.trim(q.now);
    auto left = q.ids();
    CHECK(left.size() == 1);
    CHECK(left[0] == newId);
    CHECK(oldId != newId);
}

TEST_CASE("cursor safety: a read failure retries, three in a row skip") {
    RevKeys rk;
    Node b(0x71), c(0x72);
    b.core.addLinkPeer(c.board, true);
    std::string id1 = netPostId(b.post());
    b.store.failReads = true;
    CHECK(b.pendingPosts(c.board).empty());
    CHECK(b.pendingPosts(c.board).empty());
    b.store.failReads = false;
    auto got = b.pendingPosts(c.board);  // two strikes, then it reads: still sent
    REQUIRE(got.size() == 1);
    CHECK(decodePost(got[0]).id == id1);

    // Three failures in a row: confirmed gone, the cursor moves on.
    Node d(0x73), e(0x74);
    d.core.addLinkPeer(e.board, true);
    d.post();
    d.store.failReads = true;
    CHECK(d.pendingPosts(e.board).empty());
    CHECK(d.pendingPosts(e.board).empty());
    CHECK(d.pendingPosts(e.board).empty());
    d.store.failReads = false;
    CHECK(d.pendingPosts(e.board).empty());
    std::string id2 = netPostId(d.post());
    auto after = d.pendingPosts(e.board);
    REQUIRE(after.size() == 1);
    CHECK(decodePost(after[0]).id == id2);
}

TEST_CASE("cursor safety: strikes count failures in a row only") {
    RevKeys rk;
    Node b(0x75), c(0x76);
    b.core.addLinkPeer(c.board, true);
    std::string id = netPostId(b.post());
    b.store.failReads = true;
    CHECK(b.pendingPosts(c.board).empty());  // strike 1
    CHECK(b.pendingPosts(c.board).empty());  // strike 2
    b.store.failReads = false;
    REQUIRE(b.pendingPosts(c.board).size() == 1);  // read fine (not acked yet)
    b.store.failReads = true;
    CHECK(b.pendingPosts(c.board).empty());  // a new failure, the first in a row
    b.store.failReads = false;
    auto got = b.pendingPosts(c.board);
    REQUIRE(got.size() == 1);
    CHECK(decodePost(got[0]).id == id);
}

TEST_CASE("cursor safety: an excluded entry advances without being sent") {
    RevKeys rk;
    Node b(0x77), c(0x78);
    b.knows(c);
    b.core.addLinkPeer(c.board, true);
    uint32_t at = b.log.next();
    REQUIRE(b.core.take(decodePost(c.post()), c.board) == Took::Stored);
    CHECK(cursorOf(b.core, c.board) == at);
    CHECK(b.core.pendingFor(c.board, 8).empty());
    CHECK(cursorOf(b.core, c.board) == b.log.next());
}

TEST_CASE("pendingFor caps its lines at 12 KB and acked moves the cursor") {
    RevKeys rk;
    Node b(0x79), c(0x7A);
    b.core.addLinkPeer(c.board, true);
    for (int i = 0; i < 3; i++) b.post("sn_support", "big", std::string(5000, 'x'));
    auto p = b.core.pendingFor(c.board, 8);
    REQUIRE(p.size() == 2);  // a third would pass 12 KB
    CHECK(p[0].second.size() + p[1].second.size() <= kNetPendingBytes);

    Node d(0x7B), e(0x7C);
    d.core.addLinkPeer(e.board, true);
    for (int i = 0; i < 2; i++) d.post("sn_support", "huge", std::string(7000, 'y'));
    CHECK(d.core.pendingFor(e.board, 8).size() == 1);  // at least one, even past the cap alone

    Node f(0x7D), g(0x7E);
    f.core.addLinkPeer(g.board, true);
    for (int i = 0; i < 3; i++) f.post();
    auto q = f.core.pendingFor(g.board, 8);
    REQUIRE(q.size() == 3);
    CHECK(f.core.pendingFor(g.board, 2).size() == 2);
    f.core.acked(g.board, q[0].first);
    auto r = f.core.pendingFor(g.board, 8);
    REQUIRE(r.size() == 2);
    CHECK(r[0].first == q[1].first);
    f.core.acked(g.board, q[2].first);
    CHECK(f.core.pendingFor(g.board, 8).empty());
}

TEST_CASE("link peers: two pools of 4, the first peer pinned") {
    Node n(0x81);
    auto bd = [](int i) {
        char h[17];
        std::snprintf(h, sizeof h, "%016x", i + 1);
        return std::string(h);
    };
    n.core.setFirstPeer(bd(0));
    for (int i = 1; i <= 6; i++) {
        n.now += 1;
        n.core.addLinkPeer(bd(i), true);
    }
    CHECK(n.core.isLinkPeer(bd(0)));  // pinned
    CHECK(n.core.isLinkPeer(bd(6)));
    for (int i = 10; i <= 20; i++) {
        n.now += 1;
        n.core.addLinkPeer(bd(i), false);
    }
    size_t out = 0, in = 0;
    for (const auto& l : lines(n.core.saveCursors())) {
        if (l.size() > 4 && l.compare(l.size() - 4, 4, " out") == 0) out++;
        if (l.size() > 3 && l.compare(l.size() - 3, 3, " in") == 0) in++;
    }
    CHECK(out == kMaxOutboundPeers);
    CHECK(in == kMaxInboundPeers);
    CHECK(n.core.isLinkPeer(bd(0)));  // strangers dialing in never push out the boards dialed
    CHECK(n.core.isLinkPeer(bd(6)));
    CHECK(n.core.isLinkPeer(bd(20)));
    CHECK_FALSE(n.core.isLinkPeer(bd(10)));  // the inbound one synced longest ago went

    // cursors survive a save and load
    std::string saved = n.core.saveCursors();
    Node m(0x82);
    m.core.loadCursors(saved);
    CHECK(m.core.saveCursors() == saved);
}

TEST_CASE("netDateUnix reads what the old sscanf parse read") {
    auto old = [](const std::string& text) -> uint32_t {
        int y, mo, d, h, mi;
        if (std::sscanf(text.c_str(), "%d-%d-%d %d:%d", &y, &mo, &d, &h, &mi) != 5) return 0;
        std::tm tm = {};
        tm.tm_year = y - 1900;
        tm.tm_mon = mo - 1;
        tm.tm_mday = d;
        tm.tm_hour = h;
        tm.tm_min = mi;
        tm.tm_isdst = -1;
        std::time_t t = std::mktime(&tm);
        return t <= 0 ? 0 : (uint32_t)t;
    };
    for (const char* s : {"2026-11-11 08:05", "2026-1-2 3:4", "1999-12-31 23:59", " 2026-11-11 08:05", "2026-11-11  08:05",
                          "2026-11-11 08:05 trailing", "2026-11-11 8:5", "2026-11-11", "2026/11/11 08:05", "garbage", "",
                          "2026-11-11 08", "2026-11-1108:05", "2026-02-30 10:00", "1970-01-01 00:00"})
        CHECK_MESSAGE(netDateUnix(s) == old(s), s);
    uint32_t t = kNow - kNow % 60;
    CHECK(netDateUnix(netDateText(t)) == t);
}

TEST_CASE("net walks stream the headers and never build a header list") {
    RevKeys rk;
    TempDir dir;
    axis::bbs::FileMessageStore files(dir.p.string());
    NoHeadersStore store(files);
    MemOutLog log;
    KeyPair k = keyOf(0x90);
    uint32_t now = kNow;
    NetCore core(store, k, [&] { return now; }, log);
    REQUIRE(core.setOwnCard(makeCard(k, "Streamer")));
    CHECK_FALSE(core.hasNetPosts());
    Message m;
    m.from = "Sysop";
    m.subject = "s";
    m.body = "b";
    std::vector<std::string> ids;
    for (int i = 0; i < 6; i++) ids.push_back(core.originate(i % 2 ? "sn_bugs" : "sn_support", m));
    for (const auto& id : ids) REQUIRE(!id.empty());
    CHECK(core.hasNetPosts());
    HistoryCursor hc;
    std::string hl;
    int sent = 0;
    while (core.historyNext(hc, 0, hl)) sent++;
    CHECK(sent == 6);
    // A kill drops the post from its area by a header scan.
    REQUIRE(core.takeNotice(makeNotice(rev0(), 0, "kill", ids[3], 1), ""));
    int left = 0;
    files.scanHeaders("sn_bugs", [&](const Message& h) {
        CHECK(netPostId(h.net) != ids[3]);
        left++;
        return true;
    });
    CHECK(left == 2);
    // Retention by count, then by age.
    core.setRetention(2, kNetKeepDays);
    core.trim(now);
    int support = 0;
    files.scanHeaders("sn_support", [&](const Message&) { return ++support > 0; });
    CHECK(support == 2);
    now += 100 * kDay;
    core.trim(now);
    CHECK_FALSE(core.hasNetPosts());
    CHECK(store.headerCalls == 0);
}

TEST_CASE("the file store's header scan sees every post and stops when asked") {
    TempDir dir;
    axis::bbs::FileMessageStore files(dir.p.string());
    Message m;
    m.from = "a";
    m.subject = "s";
    m.net = std::string("P\x1F") + "0123456789abcdef-1\x1F" + std::string(3000, 'x');
    m.body = std::string(5000, 'y');
    for (int i = 0; i < 205; i++) REQUIRE(files.post("sn_support", m) == i + 1);
    int seen = 0;
    files.scanHeaders("sn_support", [&](const Message& h) {
        seen++;
        CHECK(h.body.empty());
        CHECK(netPostId(h.net) == "0123456789abcdef-1");
        CHECK(h.net.size() < 64);  // only the start of the net line
        return true;
    });
    CHECK(seen == 205);  // more than headers() gives
    int stopped = 0;
    files.scanHeaders("sn_support", [&](const Message&) { return ++stopped < 3; });
    CHECK(stopped == 3);
    Message back;
    REQUIRE(files.read("sn_support", 205, back));
    CHECK(back.net == m.net);
    CHECK(back.body == m.body);
}

TEST_CASE("a history post below the seen window is taken once, by its area") {
    RevKeys rk;
    Node a(0x92), b(0x93);
    b.knows(a);
    std::vector<NetPost> ps;
    for (uint32_t s = 1; s <= 100; s++) ps.push_back(makePost(a.key, s, kNow, s < 50 ? "sn_bugs" : "sn_support"));
    REQUIRE(b.core.take(ps[99], a.board, true) == Took::Stored);  // seq 100 first: the window is 36..99
    CHECK(b.core.seen().belowWindow(ps[0].id));
    CHECK_FALSE(b.core.seen().belowWindow(ps[60].id));
    CHECK(b.core.take(ps[0], a.board) == Took::Duplicate);         // an E line: the window alone decides
    CHECK(b.core.take(ps[0], a.board, true) == Took::Stored);      // history: not in sn_bugs yet
    CHECK(b.core.take(ps[0], a.board, true) == Took::Duplicate);   // now it is
    CHECK(b.core.take(ps[60], a.board, true) == Took::Stored);     // inside the window as before
    CHECK(b.core.take(ps[60], a.board, true) == Took::Duplicate);
    CHECK(b.ids("sn_bugs").size() == 1);
}
