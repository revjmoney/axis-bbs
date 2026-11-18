// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// Net Files listings in NetCore: who may list, dedupe with posts, the one-record-per-name rule in any arrival order, the
// caps and the trim, what is shown, the listing index (/net/files.idx) and its rebuilds, and the drop budgets.
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iterator>

#include "bbs/features.h"
#include "bbs/uploadproc.h"
#include "bbs/zipreader.h"
#include "net/crc.h"
#include "net_testutil.h"

using namespace nettest;
using axis::bbs::Message;
using Took = NetCore::Took;

namespace {

// A receiving board r and a home board h, both User Net members on both, each holding the other's card.
struct Pair {
    FNode r, h;
    Pair(uint8_t a, uint8_t b) : r(a), h(b) {
        r.knows(h);
        h.knows(r);
        r.admit({&r.board, &h.board});
        h.admit({&r.board, &h.board});
    }
    Took take(const NetListing& l, const char** nak = nullptr) { return r.core.takeListing(l, h.board, false, nak); }
};

std::string word(const char* w) { return w ? w : ""; }

size_t countName(FNode& n, const std::string& name) {
    size_t c = 0;
    for (const auto& l : n.records()) c += l.name == name;
    return c;
}

// A NetCore over a counting store (the file store under it), set up as an FNode is.
struct CountedNode {
    TempDir                   dir;
    axis::bbs::FileMessageStore files;
    CountingStore             store;
    MemOutLog                 log;
    KeyPair                   key;
    uint32_t                  now = kNow;
    NetCore                   core;
    AppsFile                  apps;
    IdxFile                   idx;
    NetCard                   card;
    explicit CountedNode(uint8_t seed)
        : files(dir.p.string()), store(files), key(keyOf(seed)), core(store, key, [this] { return now; }, log) {
        card = makeCard(key, "Counted");
        card.links = kNetCardLinks;
        REQUIRE(signCard(card, key));
        REQUIRE(core.setOwnCard(card));
        apps.wire(core);
        std::vector<std::string> all;
        for (const auto& a : userNetAreas()) all.push_back(a.id);
        core.setUserAreas(all);
        idx.wire(core);
        indexReady(core);
        REQUIRE(core.listingIndexReady());
    }
};

}  // namespace

TEST_CASE("listings: a non-member's listing is refused member; a board not carrying un_files or not a member refuses area") {
    RevKeys rk;
    {
        FNode r(0x81), h(0x82);
        r.knows(h);
        r.admit({&r.board});  // h is no member here
        const char* nak = nullptr;
        CHECK(r.core.takeListing(makeListing(h.key, 1), h.board, false, &nak) != Took::Stored);
        CHECK(word(nak) == kNakMember);
        CHECK(r.records().empty());
    }
    {
        FNode r(0x83, false), h(0x84);  // r carries no User Net area
        r.knows(h);
        r.admit({&r.board, &h.board});
        const char* nak = nullptr;
        CHECK(r.core.takeListing(makeListing(h.key, 1), h.board, false, &nak) != Took::Stored);
        CHECK(word(nak) == kNakArea);
    }
    {
        FNode r(0x85), h(0x86);
        r.knows(h);
        r.admit({&h.board});  // r itself is no member
        const char* nak = nullptr;
        CHECK(r.core.takeListing(makeListing(h.key, 1), h.board, false, &nak) != Took::Stored);
        CHECK(word(nak) == kNakArea);
        CHECK(r.core.originateListing(makeListing(r.key, 1)).empty());  // nor may it list its own
    }
}

TEST_CASE("listings: the listing id is deduped with its board's posts") {
    RevKeys rk;
    Pair p(0x87, 0x88);
    CHECK(p.r.core.take(makePost(p.h.key, 5, kNow), p.h.board) == Took::Stored);
    CHECK(p.take(makeListing(p.h.key, 5)) == Took::Duplicate);  // id <h>-5 is the post's
    CHECK(p.r.records().empty());
    CHECK(p.take(makeListing(p.h.key, 6)) == Took::Stored);
    CHECK(p.r.core.take(makePost(p.h.key, 6, kNow), p.h.board) == Took::Duplicate);
    CHECK(p.take(makeListing(p.h.key, 6)) == Took::Duplicate);
    CHECK(p.r.records().size() == 1);
}

TEST_CASE("listings: listing 5, re-listing 9 and removal 7 of one name end with 9 live in all 6 orders; removal 10 leaves none") {
    RevKeys rk;
    for (uint32_t removal : {7u, 10u}) {
        int order[3] = {0, 1, 2};
        int runs = 0;
        do {
            CAPTURE(removal);
            CAPTURE(order[0]);
            CAPTURE(order[1]);
            CAPTURE(order[2]);
            Pair p(0x89, 0x8A);  // a fresh receiver for each order
            const NetListing recs[3] = {makeListing(p.h.key, 5, "SAME.ZIP"), makeListing(p.h.key, 9, "SAME.ZIP"),
                                        makeListing(p.h.key, removal, "SAME.ZIP", true)};
            for (int i : order) {
                Took t = p.take(recs[i]);
                CHECK((t == Took::Stored || t == Took::Duplicate));
                indexReady(p.r.core);  // after a replace or removal the index is built again a piece a step
            }
            std::vector<uint32_t> live = p.r.liveSeqs("SAME.ZIP");
            std::vector<std::string> shown = p.r.page();
            NetListing found;
            if (removal == 7) {
                CHECK(live == std::vector<uint32_t>{9});
                CHECK(shown == std::vector<std::string>{p.h.board + "-9"});
                CHECK(p.r.core.findListing(p.h.board + "-9", found));
            } else {
                CHECK(live.empty());
                CHECK(shown.empty());
                CHECK_FALSE(p.r.core.findListing(p.h.board + "-9", found));
            }
            CHECK_FALSE(p.r.core.findListing(p.h.board + "-5", found));
            CHECK(countName(p.r, "SAME.ZIP") == 1);  // one record per name
            runs++;
        } while (std::next_permutation(order, order + 3));
        CHECK(runs == 6);
    }
}

TEST_CASE("listings: a re-listing replaces the lower one in either order; list, remove, list, remove keeps one record") {
    RevKeys rk;
    {
        Pair p(0x8B, 0x8C);
        CHECK(p.take(makeListing(p.h.key, 5, "A.ZIP")) == Took::Stored);
        CHECK(p.take(makeListing(p.h.key, 9, "A.ZIP")) == Took::Stored);
        CHECK(p.r.liveSeqs("A.ZIP") == std::vector<uint32_t>{9});
        CHECK(countName(p.r, "A.ZIP") == 1);
    }
    {
        Pair p(0x8B, 0x8C);
        CHECK(p.take(makeListing(p.h.key, 9, "A.ZIP")) == Took::Stored);
        CHECK(p.take(makeListing(p.h.key, 5, "A.ZIP")) == Took::Duplicate);
        CHECK(p.r.liveSeqs("A.ZIP") == std::vector<uint32_t>{9});
        CHECK(countName(p.r, "A.ZIP") == 1);
        // the same name in another area is another file
        CHECK(p.take(makeListing(p.h.key, 10, "A.ZIP", false, kNow, "other")) == Took::Stored);
        CHECK(countName(p.r, "A.ZIP") == 2);
    }
    {  // this board's own: list, remove, list, remove
        FNode n(0x8D);
        n.admit({&n.board});
        NetListing l = makeListing(n.key, 1, "OWN.TXT");
        NetListing gone = l;
        gone.gone = true;
        for (int i = 0; i < 2; i++) {
            CHECK_FALSE(n.core.originateListing(l).empty());
            CHECK(n.liveSeqs("OWN.TXT").size() == 1);
            indexReady(n.core);  // (the host's share queue waits for this too)
            CHECK_FALSE(n.core.originateListing(gone).empty());
            CHECK(n.liveSeqs("OWN.TXT").empty());
            indexReady(n.core);
        }
        CHECK(countName(n, "OWN.TXT") == 1);
        CHECK(n.records().back().gone);
    }
}

TEST_CASE("listings: the per-board cap drops that board's oldest; the total cap takes from the board with the most, never this board's") {
    RevKeys rk;
    FNode r(0x90), a(0x91), b(0x92);
    r.knows(a), r.knows(b);
    r.admit({&r.board, &a.board, &b.board});
    // a lists 5 (they arrive out of seq order), b 3, this board 2
    for (uint32_t s : {3u, 1u, 5u, 2u, 4u}) CHECK(r.core.takeListing(makeListing(a.key, s, "A" + std::to_string(s)), a.board) == Took::Stored);
    for (uint32_t s : {7u, 6u, 8u}) CHECK(r.core.takeListing(makeListing(b.key, s, "B" + std::to_string(s)), b.board) == Took::Stored);
    for (int i = 0; i < 2; i++) CHECK_FALSE(r.core.originateListing(makeListing(r.key, 1, "R" + std::to_string(i))).empty());
    {  // per board 3: a loses its two lowest seqs, b and this board keep all
        r.core.setListingCaps(1000, 3);
        r.core.trim(kNow);
        indexReady(r.core);  // a trim that dropped some: built again in pieces before the next trim
        std::vector<uint32_t> as, bs;
        size_t own = 0;
        for (const auto& l : r.records()) {
            if (l.board == a.board) as.push_back(l.seq);
            if (l.board == b.board) bs.push_back(l.seq);
            own += l.board == r.board;
        }
        std::sort(as.begin(), as.end());
        std::sort(bs.begin(), bs.end());
        CHECK(as == std::vector<uint32_t>{3, 4, 5});
        CHECK(bs == std::vector<uint32_t>{6, 7, 8});
        CHECK(own == 2);
    }
    {  // total 4 of others (3 + 3 = 6, this board's 2 not counted): one at a time from whoever holds the most
        r.now += kDay;  // the trim runs once a day
        r.core.setListingCaps(4, 200);
        r.core.trim(r.now);
        indexReady(r.core);
        std::vector<uint32_t> as, bs;
        size_t own = 0;
        for (const auto& l : r.records()) {
            if (l.board == a.board) as.push_back(l.seq);
            if (l.board == b.board) bs.push_back(l.seq);
            own += l.board == r.board;
        }
        std::sort(as.begin(), as.end());
        std::sort(bs.begin(), bs.end());
        CHECK(as == std::vector<uint32_t>{4, 5});
        CHECK(bs == std::vector<uint32_t>{7, 8});
        CHECK(own == 2);
    }
    {  // a total of 0: every other board's go, never this board's own
        r.now += kDay;
        r.core.setListingCaps(0, 200);
        r.core.trim(r.now);
        size_t own = 0, others = 0;
        for (const auto& l : r.records()) (l.board == r.board ? own : others)++;
        CHECK(own == 2);
        CHECK(others == 0);
        CHECK(r.page().size() == 2);
    }
}

TEST_CASE("listings: the total cap holds at intake; a removal, a replace and this board's own still come in, and a removal makes room") {
    RevKeys rk;
    CHECK(kNetListingsMax == 500);
    FNode r(0x95), a(0x96), b(0x97);
    r.knows(a), r.knows(b);
    r.admit({&r.board, &a.board, &b.board});
    r.core.setListingCaps(4, 200);
    auto live = [&] {
        size_t n = 0;
        for (const auto& l : r.records()) n += !l.gone;
        return n;
    };
    for (uint32_t s = 1; s <= 3; s++) CHECK(r.core.takeListing(makeListing(a.key, s, "A" + std::to_string(s)), a.board) == Took::Stored);
    CHECK(r.core.takeListing(makeListing(b.key, 1, "B1"), b.board) == Took::Stored);
    CHECK(live() == 4);
    // full: a new name from either board is refused, final ("limited": the sender moves on, it is not held)
    const char* why = nullptr;
    CHECK(r.core.takeListing(makeListing(b.key, 2, "B2"), b.board, false, &why) == Took::Limited);
    CHECK(std::string(why ? why : "") == kNakLimited);
    CHECK_FALSE(netNakTransient(why));
    CHECK(r.core.takeListing(makeListing(a.key, 4, "A4"), a.board, true) == Took::Limited);  // in a history pull too
    CHECK(live() == 4);
    // a re-listing of a name held replaces it (no more held), so it comes in
    CHECK(r.core.takeListing(makeListing(a.key, 5, "A1"), a.board) == Took::Stored);
    indexReady(r.core);
    CHECK(live() == 4);
    // this board's own always come in, past the cap, and are not counted toward it
    CHECK_FALSE(r.core.originateListing(makeListing(r.key, 1, "R1")).empty());
    indexReady(r.core);
    CHECK(live() == 5);
    CHECK(r.core.takeListing(makeListing(b.key, 3, "B3"), b.board) == Took::Limited);  // 4 from others: still full
    // a removal comes in at the cap and makes room
    CHECK(r.core.takeListing(makeListing(a.key, 6, "A2", true), a.board) == Took::Stored);
    indexReady(r.core);
    CHECK(live() == 4);
    CHECK(r.core.takeListing(makeListing(b.key, 4, "B4"), b.board) == Took::Stored);
    CHECK(live() == 5);
    CHECK(r.core.takeListing(makeListing(b.key, 5, "B5"), b.board) == Took::Limited);
}

TEST_CASE("listings: the per-board cap holds at intake, and this board's own do not count toward either cap") {
    RevKeys rk;
    FNode r(0x98), a(0x99), b(0x9A);
    r.knows(a), r.knows(b);
    r.admit({&r.board, &a.board, &b.board});
    r.core.setListingCaps(4, 2);
    for (int i = 0; i < 5; i++) CHECK_FALSE(r.core.originateListing(makeListing(r.key, 1, "R" + std::to_string(i))).empty());
    indexReady(r.core);
    // 5 of this board's own held, past both caps: a peer's still come in
    CHECK(r.core.takeListing(makeListing(a.key, 1, "A1"), a.board) == Took::Stored);
    CHECK(r.core.takeListing(makeListing(a.key, 2, "A2"), a.board) == Took::Stored);
    // a holds 2, its cap: a third is refused "limited", final; b still comes in
    const char* why = nullptr;
    CHECK(r.core.takeListing(makeListing(a.key, 3, "A3"), a.board, true, &why) == Took::Limited);
    CHECK(std::string(why ? why : "") == kNakLimited);
    CHECK(r.core.takeListing(makeListing(b.key, 1, "B1"), b.board) == Took::Stored);
    CHECK(r.core.takeListing(makeListing(b.key, 2, "B2"), b.board) == Took::Stored);
    // 4 from others: the total cap
    CHECK(r.core.takeListing(makeListing(b.key, 3, "B3"), b.board) == Took::Limited);
    // a re-listing of a held name still comes in at the per-board cap
    CHECK(r.core.takeListing(makeListing(a.key, 4, "A1"), a.board) == Took::Stored);
    indexReady(r.core);
    // and the trim keeps all of them (own are not counted in the total)
    r.core.trim(kNow);
    size_t own = 0, others = 0;
    for (const auto& l : r.records())
        if (!l.gone) (l.board == r.board ? own : others)++;
    CHECK(own == 5);
    CHECK(others == 4);
}

TEST_CASE("listings: with a clock the trim still keeps at most kNetGoneKeepCount tombstones, the newest") {
    RevKeys rk;
    Pair p(0x9B, 0x9C);
    p.r.core.setListingCaps(kNetListingsMax, kNetGoneKeepCount + 50);
    uint32_t n = kNetGoneKeepCount + 5;
    for (uint32_t s = 1; s <= n; s++)
        REQUIRE(p.r.core.takeListing(makeListing(p.h.key, s, "G" + std::to_string(s), true, kNow - 10 * kDay), p.h.board, true) ==
                Took::Stored);  // (a history pull: past the daily ingress limit)
    p.r.core.trim(kNow);
    indexReady(p.r.core);
    std::vector<uint32_t> seqs;
    for (const auto& l : p.r.records())
        if (l.gone) seqs.push_back(l.seq);
    CHECK(seqs.size() == kNetGoneKeepCount);
    CHECK(*std::min_element(seqs.begin(), seqs.end()) == 6);  // the 5 oldest went
}

TEST_CASE("listings: the trim drops a tombstone at 121 days and keeps ones at 120 and 119") {
    RevKeys rk;
    Pair p(0x93, 0x94);
    CHECK(p.take(makeListing(p.h.key, 1, "OLD121", true, kNow - 121 * kDay)) == Took::Stored);
    CHECK(p.take(makeListing(p.h.key, 2, "OLD120", true, kNow - 120 * kDay)) == Took::Stored);
    CHECK(p.take(makeListing(p.h.key, 3, "OLD119", true, kNow - 119 * kDay)) == Took::Stored);
    CHECK(p.take(makeListing(p.h.key, 4, "LIVE", false, kNow - 300 * kDay)) == Took::Stored);  // live ones never age out
    p.r.core.trim(kNow);
    std::vector<std::string> names;
    for (const auto& l : p.r.records()) names.push_back(l.name);
    CHECK(names == std::vector<std::string>{"OLD120", "OLD119", "LIVE"});
    // The pull's tombstone batch gives the young ones (its age check reads the stored minute, so the one at exactly 120
    // days may fall either side), never a live listing.
    std::vector<std::string> out;
    uint32_t pos = 0;
    CHECK(p.r.core.listingLines(pos, true, out));
    std::vector<std::string> sent;
    for (const auto& line : out) {
        NetListing l;
        REQUIRE(decode(line, l));
        sent.push_back(l.name);
    }
    CHECK(std::find(sent.begin(), sent.end(), "OLD119") != sent.end());
    CHECK(std::find(sent.begin(), sent.end(), "LIVE") == sent.end());
    CHECK(sent.size() <= 2);
}

TEST_CASE("listings: killed, held and banned-home listings are not shown; the trim drops a banned board's live ones and keeps its tombstones") {
    RevKeys rk;
    FNode r(0x95), a(0x96), b(0x97);
    r.knows(a), r.knows(b);
    r.admit({&r.board, &a.board, &b.board});
    for (uint32_t s = 1; s <= 3; s++) CHECK(r.core.takeListing(makeListing(a.key, s, "A" + std::to_string(s)), a.board) == Took::Stored);
    CHECK(r.core.takeListing(makeListing(b.key, 1, "B1"), b.board) == Took::Stored);
    CHECK(r.core.takeListing(makeListing(b.key, 2, "B0", true), b.board) == Took::Stored);
    CHECK(r.page().size() == 4);
    std::string a1 = a.board + "-1", a2 = a.board + "-2", b1 = b.board + "-1";
    REQUIRE(r.core.takeNotice(makeNotice(rev0(), 0, "kill", a1, 101), ""));
    REQUIRE(r.core.takeNotice(makeNotice(rev0(), 0, "hold", a2, 102), ""));
    std::vector<std::string> shown = r.page();
    CHECK(shown == std::vector<std::string>{b1, a.board + "-3"});
    NetListing l;
    CHECK_FALSE(r.core.findListing(a1, l));
    CHECK_FALSE(r.core.findListing(a2, l));
    CHECK(r.core.findListing(a.board + "-3", l));
    REQUIRE(r.core.takeNotice(makeNotice(rev0(), 0, "ban", b.board, 103), ""));
    CHECK(r.page() == std::vector<std::string>{a.board + "-3"});
    CHECK_FALSE(r.core.findListing(b1, l));
    // A killed id is refused when it comes again.
    const char* nak = nullptr;
    CHECK(r.core.takeListing(makeListing(a.key, 1, "A1"), a.board, false, &nak) == Took::Rejected);
    CHECK(word(nak) == kNakKilled);
    r.core.trim(kNow);
    std::vector<std::string> left;
    for (const auto& x : r.records()) left.push_back(x.name + (x.gone ? "-gone" : ""));
    std::sort(left.begin(), left.end());
    // the killed one and b's live one are dropped; the held one stays stored (hidden); b's tombstone stays
    CHECK(left == std::vector<std::string>{"A2", "A3", "B0-gone"});
}

TEST_CASE("listings: nf_list is in no caller area list and nothing unsigned is posted into it") {
    RevKeys rk;
    CHECK_FALSE(isNetAreaId(kNetFilesArea));
    CHECK(netArea(kNetFilesArea) == nullptr);
    CHECK_FALSE(isUserNetArea(kNetFilesArea));
    CHECK(sysopNetArea(kNetFilesArea) == nullptr);
    for (const auto& a : userNetAreas()) CHECK(a.id != kNetFilesArea);
    for (const auto& a : sysopNetAreas()) CHECK(a.id != kNetFilesArea);
    FNode n(0x98);
    NetPostingStore callers(n.files, n.core);
    Message m;
    m.from = "Caller";
    m.subject = "FAKE.ZIP";
    CHECK(callers.post(kNetFilesArea, m) == 0);
    CHECK(callers.postAfter(kNetFilesArea, 5, m) == 0);  // (the default postAfter is its post)
    CHECK(n.records().empty());
}

TEST_CASE("listings: headers() cut a listing line to its board; a header scan gives it whole") {
    RevKeys rk;
    Pair p(0x99, 0x9A);
    NetListing l = makeListing(p.h.key, 1, "WHOLE.ZIP");
    CHECK(p.take(l) == Took::Stored);
    auto hs = p.r.files.headers(kNetFilesArea);
    REQUIRE(hs.size() == 1);
    CHECK(hs[0].net == std::string("L\x1F") + p.h.board);
    std::string whole;
    p.r.files.scanHeaders(kNetFilesArea, [&](const Message& m) {
        whole = m.net;
        return true;
    });
    CHECK(whole == encode(l));
    MemStore ms;  // the test store keeps the same rule
    Message x;
    x.net = encode(l);
    ms.post(kNetFilesArea, x);
    CHECK(ms.headers(kNetFilesArea)[0].net == std::string("L\x1F") + p.h.board);
    ms.scanHeaders(kNetFilesArea, [&](const Message& m) {
        CHECK(m.net == encode(l));
        return true;
    });
}

TEST_CASE("listing index: 64 takes read no whole area and no message by number") {
    RevKeys rk;
    CountedNode r(0x9B);
    UNode       h(0x9C);
    REQUIRE(r.core.takeCard(h.card, ""));
    REQUIRE(r.core.takeNotice(makeNotice(rev0(), 0, "admit", r.card.board, 1, "1"), ""));
    REQUIRE(r.core.takeNotice(makeNotice(rev0(), 0, "admit", h.board, 2, "1"), ""));
    r.store.reset();
    for (uint32_t s = 1; s <= 64; s++)
        CHECK(r.core.takeListing(makeListing(h.key, s, "F" + std::to_string(s) + ".ZIP"), h.board, true) == Took::Stored);
    CHECK(r.store.scans == 0);
    CHECK(r.store.reads == 0);
    CHECK(r.store.headerCalls == 0);
    CHECK(r.store.readsAt == 0);       // distinct names: nothing to read back
    CHECK(r.store.scansFrom == 64);    // one short scan of each new message, from where the area ended
    CHECK(r.store.posts == 1);         // the first, into an empty area
    CHECK(r.store.postAfters == 63);   // the rest after the last number the index head holds: no whole-area read
    // A re-listing reads back the one record it replaces, at its offset.
    r.store.reset();
    CHECK(r.core.takeListing(makeListing(h.key, 70, "F1.ZIP"), h.board, true) == Took::Stored);
    CHECK(r.store.readsAt == 1);
    CHECK(r.store.reads == 0);
    NetListing l;
    CHECK_FALSE(r.core.findListing(h.board + "-70", l));  // the replace's drop: the index is built again first
    indexReady(r.core);
    r.store.reset();
    CHECK(r.core.findListing(h.board + "-70", l));
    CHECK(r.store.scans + r.store.reads == 0);
    CHECK(r.store.readsAt == 1);
}

TEST_CASE("listing index: 50 appends each go after the head's last number, and every record points at its message") {
    RevKeys rk;
    CountedNode r(0xC6);
    UNode       h(0xC7);
    REQUIRE(r.core.takeCard(h.card, ""));
    REQUIRE(r.core.takeNotice(makeNotice(rev0(), 0, "admit", r.card.board, 1, "1"), ""));
    REQUIRE(r.core.takeNotice(makeNotice(rev0(), 0, "admit", h.board, 2, "1"), ""));
    // A junk message first: the head's last number counts it, so the listings go on from 2.
    Message junk;
    junk.subject = "junk";
    REQUIRE(r.files.post(kNetFilesArea, junk) == 1);
    r.idx.bytes.clear();
    r.idx.wire(r.core);
    indexReady(r.core);
    REQUIRE(r.core.listingIndexReady());
    r.store.reset();
    for (uint32_t s = 1; s <= 50; s++)
        REQUIRE(r.core.takeListing(makeListing(h.key, s, "G" + std::to_string(s) + ".ZIP"), h.board, true) == Took::Stored);
    CHECK(r.store.posts == 0);
    CHECK(r.store.postAfters == 50);
    CHECK(r.store.scans == 0);
    // The area as a scan gives it, against the index: same count, numbers, offsets and seqs, in order.
    std::vector<Message> ms;
    r.files.scanHeaders(kNetFilesArea, [&](const Message& m) { return ms.push_back(m), true; });
    REQUIRE(ms.size() == 51);
    REQUIRE(r.idx.word(2) == 50);
    CHECK(r.idx.word(5 + 2) == 1);    // hasLast
    CHECK(r.idx.word(5 + 3) == 51);   // lastNum
    CHECK(r.idx.word(5 + 4) == ms.back().at);
    for (uint32_t i = 0; i < 50; i++) {
        const Message& m = ms[i + 1];
        CHECK(m.num == (int)i + 2);
        CHECK(r.idx.word(11 + i * 6 + 2) == i + 1);           // seq
        CHECK(r.idx.word(11 + i * 6 + 3) == (uint32_t)m.num);  // num
        CHECK(r.idx.word(11 + i * 6 + 5) == m.at);             // at
    }
    NetListing l;
    CHECK(r.core.findListing(h.board + "-37", l));
    CHECK(l.name == "G37.ZIP");
    // A fresh look at the head agrees with the area (the tail check): ready with no rebuild.
    std::string key = r.idx.key();
    r.idx.wire(r.core);
    CHECK(indexReady(r.core) == 0);
    CHECK(r.core.listingIndexReady());
    CHECK(r.idx.key() == key);
}

TEST_CASE("listing index: listings passed to a peer are read at their offset by the index, by number while it rebuilds") {
    RevKeys rk;
    CountedNode r(0xCA);
    UNode       h(0xCB), q(0xCC);
    REQUIRE(r.core.takeCard(h.card, ""));
    REQUIRE(r.core.takeNotice(makeNotice(rev0(), 0, "admit", r.card.board, 1, "1"), ""));
    REQUIRE(r.core.takeNotice(makeNotice(rev0(), 0, "admit", h.board, 2, "1"), ""));
    r.core.addLinkPeer(q.board, true);  // a peer starts at the log's end: the listings below go to it
    for (uint32_t s = 1; s <= 20; s++)
        REQUIRE(r.core.takeListing(makeListing(h.key, s, "W" + std::to_string(s) + ".ZIP"), h.board, true) == Took::Stored);
    auto sent = [&] {
        std::vector<std::string> ids;
        for (const auto& e : r.core.pendingFor(q.board, 64)) {
            NetListing l;
            if (decode(e.second, l)) ids.push_back(listingId(l));
        }
        return ids;
    };
    r.store.reset();
    std::vector<std::string> ready = sent();
    REQUIRE(ready.size() == 20);
    CHECK(ready.front() == h.board + "-1");
    CHECK(ready.back() == h.board + "-20");
    CHECK(r.store.reads == 0);  // no read by number (each a scan of the area from its start)
    CHECK(r.store.scans == 0);
    CHECK(r.store.readsAt == 20);
    // While the index is built again: the same lines, read by number.
    r.idx.setWord(1, 0);
    r.idx.wire(r.core);
    REQUIRE(r.core.listingIndexStep());  // the look: unclean, a rebuild starts (20 left, one more call)
    REQUIRE_FALSE(r.core.listingIndexReady());
    r.store.reset();
    CHECK(sent() == ready);
    CHECK(r.store.reads == 20);
    CHECK(r.store.readsAt == 0);
}

TEST_CASE("listing index: a take reads the index in 16-record pieces (one read each, growing with the count) and never the area") {
    RevKeys rk;
    CountedNode r(0xCD);
    UNode       h(0xCE);
    REQUIRE(r.core.takeCard(h.card, ""));
    REQUIRE(r.core.takeNotice(makeNotice(rev0(), 0, "admit", r.card.board, 1, "1"), ""));
    REQUIRE(r.core.takeNotice(makeNotice(rev0(), 0, "admit", h.board, 2, "1"), ""));
    r.core.setListingCaps(1000, 1000);  // (more held than the default cap, to see the reads grow)
    uint32_t seq = 1;
    auto takeOne = [&](uint32_t held) {
        CAPTURE(held);
        r.idx.reads = 0;
        r.store.reset();
        REQUIRE(r.core.takeListing(makeListing(h.key, seq, "M" + std::to_string(seq) + ".ZIP"), h.board, true) == Took::Stored);
        seq++;
        uint32_t chunks = (held + 15) / 16;
        CHECK(r.idx.reads == (int)chunks);  // idxEach over every record: one read per 16; nothing else is read
        CHECK(r.store.scans == 0);
        CHECK(r.store.reads == 0);
        CHECK(r.store.posts == 0);
        CHECK(r.store.postAfters == 1);
        CHECK(r.store.scansFrom == 1);  // the one new message, from where the area ended
        return r.idx.reads;
    };
    for (; seq <= 50; seq++) REQUIRE(r.core.takeListing(makeListing(h.key, seq, "M" + std::to_string(seq) + ".ZIP"), h.board, true) == Took::Stored);
    int at50 = takeOne(50);
    for (; seq <= 500; seq++) REQUIRE(r.core.takeListing(makeListing(h.key, seq, "M" + std::to_string(seq) + ".ZIP"), h.board, true) == Took::Stored);
    int at500 = takeOne(500);
    CHECK(at50 == 4);
    CHECK(at500 == 32);  // grows with the count, a read per 16 records, on the one open handle
}

TEST_CASE("listing index: forgotten after the area was replaced behind it, the next take goes above the new area's numbers") {
    RevKeys rk;
    Pair p(0xCF, 0xD0);
    for (uint32_t s = 1; s <= 5; s++) REQUIRE(p.take(makeListing(p.h.key, s, "K" + std::to_string(s))) == Took::Stored);
    REQUIRE(p.r.core.listingIndexReady());
    // Another card's nf_list (a card swap and @SDCHECK, or a serial @PUT): numbers 1..20, so above the head's 5.
    std::error_code ec;
    for (const auto& e : fs::recursive_directory_iterator(p.r.dir.p, ec))
        if (e.path().filename().string() == std::string(kNetFilesArea) + ".msg") fs::remove(e.path(), ec);
    for (uint32_t s = 1; s <= 20; s++) {
        NetListing l = makeListing(p.h.key, 100 + s, "OTHER" + std::to_string(s));
        Message m;
        m.from = l.uploader;
        m.to = l.board;
        m.subject = l.name;
        m.net = encode(l);
        REQUIRE(p.r.files.post(kNetFilesArea, m) == (int)s);
    }
    p.r.core.listingIndexForget();  // what the host does with the handle at those points
    const char* nak = nullptr;
    CHECK(p.take(makeListing(p.h.key, 30, "NEW"), &nak) == Took::Rejected);  // the tail check: built again first
    CHECK(word(nak) == kNakStore);
    indexReady(p.r.core);
    REQUIRE(p.r.core.listingIndexReady());
    CHECK(p.take(makeListing(p.h.key, 30, "NEW")) == Took::Stored);
    std::vector<int> nums;
    p.r.files.scanHeaders(kNetFilesArea, [&](const Message& m) { return nums.push_back(m.num), true; });
    REQUIRE(nums.size() == 21);
    CHECK(nums.back() == 21);
    for (size_t i = 1; i < nums.size(); i++) CHECK(nums[i] > nums[i - 1]);  // ascending, so no number twice
    NetListing l;
    CHECK(p.r.core.findListing(p.h.board + "-30", l));
    CHECK(p.r.core.findListing(p.h.board + "-120", l));
}

TEST_CASE("listing index: a forced key collision still gives the right answer; five records on one key are refused store") {
    RevKeys rk;
    Pair p(0x9D, 0x9E);
    for (uint32_t s = 1; s <= 6; s++) CHECK(p.take(makeListing(p.h.key, s, "N" + std::to_string(s))) == Took::Stored);
    // Record i is at 44 + 24 * i: key, board, seq, num, flags, at.
    auto rec = [&](size_t i, size_t w) { return 44 + 24 * i + 4 * w; };
    std::string& b = p.r.idx.bytes;
    std::memcpy(&b[rec(1, 0)], &b[rec(0, 0)], 4);  // N2's record now looks like N1's name
    // A re-listing of N1 reads both back and drops only N1.
    CHECK(p.take(makeListing(p.h.key, 10, "N1")) == Took::Stored);
    indexReady(p.r.core);
    CHECK(p.r.liveSeqs("N1") == std::vector<uint32_t>{10});
    CHECK(p.r.liveSeqs("N2") == std::vector<uint32_t>{2});
    // After that rewrite the index was built again with a new key. Force five onto one key: refused, transient.
    for (size_t i = 1; i < 5; i++) std::memcpy(&b[rec(i, 0)], &b[rec(0, 0)], 4);
    NetListing first;
    std::string name0;
    {
        uint32_t seq0 = 0;
        std::memcpy(&seq0, &b[rec(0, 2)], 4);
        for (const auto& l : p.r.records())
            if (l.seq == seq0) name0 = l.name;
    }
    REQUIRE(!name0.empty());
    const char* nak = nullptr;
    CHECK(p.r.core.takeListing(makeListing(p.h.key, 20, name0), p.h.board, false, &nak) == Took::Rejected);
    CHECK(word(nak) == kNakStore);
    CHECK(countName(p.r, name0) == 1);
}

TEST_CASE("listing index: an unclean head is built again in pieces, listings refused store until it is done") {
    RevKeys rk;
    Pair p(0xA2, 0xA3);
    for (uint32_t s = 1; s <= 40; s++) CHECK(p.take(makeListing(p.h.key, s, "U" + std::to_string(s))) == Took::Stored);
    std::string key = p.r.idx.key();
    p.r.idx.setWord(1, 0);   // a reset during a change left it unclean
    p.r.idx.wire(p.r.core);  // the next boot
    const char* nak = nullptr;
    CHECK(p.take(makeListing(p.h.key, 41, "U41"), &nak) == Took::Rejected);
    CHECK(word(nak) == kNakStore);
    NetListing l;
    CHECK_FALSE(p.r.core.findListing(p.h.board + "-5", l));
    CHECK(p.r.core.originateListing(makeListing(p.r.key, 1, "MINE")).empty());
    CHECK(p.r.core.listingIndexStep());  // 32 of the 40: more to do
    CHECK_FALSE(p.r.core.listingIndexReady());
    CHECK_FALSE(p.r.core.listingIndexStep());  // the last 8
    REQUIRE(p.r.core.listingIndexReady());
    CHECK(p.r.idx.key() != key);
    CHECK(p.r.idx.word(2) == 40);
    CHECK(p.r.core.findListing(p.h.board + "-5", l));
    CHECK(p.take(makeListing(p.h.key, 41, "U41")) == Took::Stored);
    CHECK(p.take(makeListing(p.h.key, 42, "U1")) == Took::Stored);  // the replace still finds U1
    CHECK(p.r.liveSeqs("U1") == std::vector<uint32_t>{42});
}

TEST_CASE("listing index: after a replace it is built again in pieces from the tick, takes refused store until it is ready") {
    RevKeys rk;
    Pair p(0xC8, 0xC9);
    for (uint32_t s = 1; s <= 40; s++) REQUIRE(p.take(makeListing(p.h.key, s, "V" + std::to_string(s))) == Took::Stored);
    std::string key = p.r.idx.key();
    REQUIRE(p.take(makeListing(p.h.key, 41, "V1")) == Took::Stored);  // replaces V1 (seq 1): the area is rewritten
    CHECK_FALSE(p.r.core.listingIndexReady());
    CHECK(p.r.idx.word(1) == 0);  // the head is unclean until the rebuild is done
    NetListing l;
    CHECK_FALSE(p.r.core.findListing(p.h.board + "-41", l));
    const char* nak = nullptr;
    CHECK(p.take(makeListing(p.h.key, 42, "V42"), &nak) == Took::Rejected);
    CHECK(word(nak) == kNakStore);
    CHECK(p.r.records().size() == 40);  // nothing stored meanwhile
    // 40 records, kNetIndexStep (32) a call: two calls, the first leaving more to do.
    CHECK(p.r.core.listingIndexStep());
    CHECK_FALSE(p.r.core.listingIndexReady());
    CHECK(p.take(makeListing(p.h.key, 42, "V42"), &nak) == Took::Rejected);
    CHECK(word(nak) == kNakStore);
    CHECK_FALSE(p.r.core.listingIndexStep());
    REQUIRE(p.r.core.listingIndexReady());
    CHECK(p.r.idx.key() != key);  // a new key
    CHECK(p.r.idx.word(1) == 1);
    CHECK(p.r.idx.word(2) == 40);
    CHECK(p.r.core.findListing(p.h.board + "-41", l));
    CHECK(l.name == "V1");
    CHECK_FALSE(p.r.core.findListing(p.h.board + "-1", l));
    CHECK(p.r.liveSeqs("V1") == std::vector<uint32_t>{41});
    CHECK(p.take(makeListing(p.h.key, 42, "V42")) == Took::Stored);  // and the next is taken
    CHECK(p.r.core.findListing(p.h.board + "-42", l));
}

TEST_CASE("listing index: a deleted index, a message behind its back or a lost area rebuild it with the same answers") {
    RevKeys rk;
    Pair p(0xA4, 0xA5);
    for (uint32_t s = 1; s <= 5; s++) CHECK(p.take(makeListing(p.h.key, s, "D" + std::to_string(s))) == Took::Stored);
    NetListing l;
    {  // deleted between two takes
        p.r.idx.bytes.clear();
        p.r.idx.wire(p.r.core);
        indexReady(p.r.core);
        REQUIRE(p.r.core.listingIndexReady());
        CHECK(p.r.core.findListing(p.h.board + "-3", l));
        CHECK(p.take(makeListing(p.h.key, 3, "D3")) == Took::Duplicate);
        CHECK(p.take(makeListing(p.h.key, 6, "D2")) == Took::Stored);
        CHECK(p.r.liveSeqs("D2") == std::vector<uint32_t>{6});
    }
    {  // a message posted straight into nf_list (a sysop's move): the tail check sees it at the next look
        std::string key = p.r.idx.key();
        Message m;
        m.subject = "junk";
        REQUIRE(p.r.files.post(kNetFilesArea, m) > 0);
        p.r.idx.wire(p.r.core);
        indexReady(p.r.core);
        REQUIRE(p.r.core.listingIndexReady());
        CHECK(p.r.idx.key() != key);  // built again
        CHECK(p.r.core.findListing(p.h.board + "-4", l));
        // and with that junk the last message, a fresh look is ready at once, with no rebuild
        key = p.r.idx.key();
        int draws = p.r.idx.draws;
        p.r.idx.wire(p.r.core);
        CHECK(indexReady(p.r.core) == 0);
        CHECK(p.r.core.listingIndexReady());
        CHECK(p.r.idx.key() == key);
        CHECK(p.r.idx.draws == draws);
    }
    {  // the area is lost: the index is built again, empty
        std::error_code ec;
        for (const auto& e : fs::recursive_directory_iterator(p.r.dir.p, ec))
            if (e.path().filename().string().find(kNetFilesArea) != std::string::npos) fs::remove(e.path(), ec);
        p.r.idx.wire(p.r.core);
        indexReady(p.r.core);
        REQUIRE(p.r.core.listingIndexReady());
        CHECK(p.r.idx.word(2) == 0);
        CHECK_FALSE(p.r.core.findListing(p.h.board + "-4", l));
        CHECK(p.take(makeListing(p.h.key, 7, "D7")) == Took::Stored);
    }
}

TEST_CASE("listing index: each build draws a new key, names with equal crc32 do not collide, and no key bytes are in it") {
    RevKeys rk;
    Pair p(0xA6, 0xA7);
    // Two names whose "board|area|name" crc32 values are equal.
    std::map<uint32_t, std::string> seen;
    uint32_t                        rnd = 99;
    std::string n1, n2;
    for (int i = 0; i < 400000 && n2.empty(); i++) {
        // (names that differ only in a few bytes never collide: crc32 catches every burst of 32 bits or less)
        std::string name = "C";
        for (int k = 0; k < 10; k++) {
            rnd = rnd * 1103515245u + 12345u;
            name += (char)('A' + (rnd >> 16) % 26);
        }
        name += ".BIN";
        std::string k = p.h.board + "|files|" + name;
        uint32_t c = crc32((const uint8_t*)k.data(), k.size());
        auto it = seen.find(c);
        if (it != seen.end()) {
            n1 = it->second;
            n2 = name;
        } else {
            seen[c] = name;
        }
    }
    std::map<uint32_t, std::string>().swap(seen);
    REQUIRE(!n2.empty());
    CHECK(p.take(makeListing(p.h.key, 1, n1)) == Took::Stored);
    CHECK(p.take(makeListing(p.h.key, 2, n2)) == Took::Stored);
    auto keyOfRec = [&](size_t i) {
        uint32_t w = 0;
        std::memcpy(&w, &p.r.idx.bytes[44 + 24 * i], 4);
        return w;
    };
    CHECK(keyOfRec(0) != keyOfRec(1));
    CHECK(p.r.liveSeqs(n1) == std::vector<uint32_t>{1});
    CHECK(p.r.liveSeqs(n2) == std::vector<uint32_t>{2});
    // A rebuild: a new key, so new record hashes for the same names.
    std::string key = p.r.idx.key();
    uint32_t    h0 = keyOfRec(0);
    p.r.idx.bytes.clear();
    p.r.idx.wire(p.r.core);
    indexReady(p.r.core);
    REQUIRE(p.r.core.listingIndexReady());
    CHECK(p.r.idx.key() != key);
    CHECK(keyOfRec(0) != h0);
    // Nothing of the board's key pair is in the file: no 4 byte window of its secret or public key.
    const std::string& b = p.r.idx.bytes;
    int found = 0;
    for (const uint8_t* part : {p.r.key.secret, p.r.key.pub})
        for (size_t i = 0; i + 4 <= 32; i++)
            if (b.find(std::string((const char*)part + i, 4)) != std::string::npos) found++;
    CHECK(found == 0);
}

TEST_CASE("listing index: a trim that drops nothing leaves the index as it was; after a drop the later listings are still found") {
    RevKeys rk;
    Pair p(0xA8, 0xA9);
    CHECK(p.take(makeListing(p.h.key, 1, "T1", true, kNow - 119 * kDay)) == Took::Stored);  // near the age line, kept
    CHECK(p.take(makeListing(p.h.key, 2, "T2")) == Took::Stored);
    std::string before = p.r.idx.bytes;
    p.r.core.trim(kNow);
    CHECK(p.r.idx.bytes == before);
    // A drop (the removal of T2's earlier copy) moves every later offset; a listing stored after it is still found.
    CHECK(p.take(makeListing(p.h.key, 3, "T3")) == Took::Stored);
    CHECK(p.take(makeListing(p.h.key, 4, "T4")) == Took::Stored);
    CHECK(p.take(makeListing(p.h.key, 5, "T2", true)) == Took::Stored);
    NetListing l;
    indexReady(p.r.core);
    CHECK(p.r.core.findListing(p.h.board + "-4", l));
    CHECK(l.name == "T4");
    CHECK(p.r.core.findListing(p.h.board + "-3", l));
    CHECK_FALSE(p.r.core.findListing(p.h.board + "-2", l));
}

TEST_CASE("listings: the drop budgets, 8 a session and 64 a day; this board's own are exempt") {
    RevKeys rk;
    {  // per session: 20 ascending re-listings of one name from a peer
        Pair p(0xAA, 0xAB);
        p.r.core.listingSessionStart();
        int stored = 0, store = 0;
        for (uint32_t s = 1; s <= 20; s++) {
            const char* nak = nullptr;
            Took t = p.take(makeListing(p.h.key, s, "R.ZIP"), &nak);
            stored += t == Took::Stored;
            store += word(nak) == kNakStore;
            indexReady(p.r.core);  // each replace: the index built again before the next
        }
        CHECK(stored == 9);  // the first replaces nothing, then 8 replacements
        CHECK(store == 11);
        CHECK(p.r.liveSeqs("R.ZIP") == std::vector<uint32_t>{9});
        p.r.core.listingSessionStart();  // the next session takes 8 more
        stored = 0;
        for (uint32_t s = 10; s <= 20; s++) {
            stored += p.take(makeListing(p.h.key, s, "R.ZIP")) == Took::Stored;
            indexReady(p.r.core);
        }
        CHECK(stored == 8);
        CHECK(countName(p.r, "R.ZIP") == 1);
        // this board's own removal still goes through, budget or not
        CHECK_FALSE(p.r.core.originateListing(makeListing(p.r.key, 1, "OWN")).empty());
        indexReady(p.r.core);
        NetListing g = makeListing(p.r.key, 1, "OWN", true);
        CHECK_FALSE(p.r.core.originateListing(g).empty());
        CHECK(p.r.liveSeqs("OWN").empty());
    }
    {  // per day, with the clock set: 64 across sessions, then the next day 8 more (pull lines, which skip ingress)
        Pair p(0xAC, 0xAD);
        uint32_t seq = 1, stored = 0;
        CHECK(p.r.core.takeListing(makeListing(p.h.key, seq++, "D.ZIP"), p.h.board, true) == Took::Stored);
        for (int session = 0; session < 9; session++) {
            p.r.core.listingSessionStart();
            for (int i = 0; i < 8; i++) {
                stored += p.r.core.takeListing(makeListing(p.h.key, seq++, "D.ZIP"), p.h.board, true) == Took::Stored;
                indexReady(p.r.core);
            }
        }
        CHECK(stored == 64);
        p.r.now += kDay;
        p.r.core.listingSessionStart();
        stored = 0;
        for (int i = 0; i < 10; i++) {
            stored += p.r.core.takeListing(makeListing(p.h.key, seq++, "D.ZIP", false, p.r.now), p.h.board, true) == Took::Stored;
            indexReady(p.r.core);
        }
        CHECK(stored == 8);
        CHECK_FALSE(p.r.core.originateListing(makeListing(p.r.key, 1, "OWN2")).empty());
        indexReady(p.r.core);
        CHECK_FALSE(p.r.core.originateListing(makeListing(p.r.key, 1, "OWN2", true)).empty());  // both budgets spent
        CHECK(p.r.liveSeqs("OWN2").empty());
    }
}

TEST_CASE("listings: without a clock the daily drop budget comes back with the trim") {
    RevKeys rk;
    FNode   r(0xAF), h(0xB0);
    r.now = 1000;  // no clock
    r.knows(h);
    r.admit({&r.board, &h.board});
    uint32_t seq = 1, stored = 0;
    CHECK(r.core.takeListing(makeListing(h.key, seq++, "N.ZIP", false, 1000), h.board, true) == Took::Stored);
    for (int session = 0; session < 9; session++) {
        r.core.listingSessionStart();
        for (int i = 0; i < 8; i++) {
            stored += r.core.takeListing(makeListing(h.key, seq++, "N.ZIP", false, 1000), h.board, true) == Took::Stored;
            indexReady(r.core);
        }
    }
    CHECK(stored == 64);
    r.core.trim(0);
    r.core.listingSessionStart();
    stored = 0;
    for (int i = 0; i < 10; i++) {
        stored += r.core.takeListing(makeListing(h.key, seq++, "N.ZIP", false, 1000), h.board, true) == Took::Stored;
        indexReady(r.core);
    }
    CHECK(stored == 8);
}

TEST_CASE("listings: one refused past the drop budget is not charged to its home board's ingress") {
    RevKeys rk;
    Pair p(0xB1, 0xB2);
    REQUIRE(p.r.core.takeNotice(makeNotice(rev0(), 0, "limits", "net", 104, "posts=12,bytes=100000"), ""));
    p.r.core.listingSessionStart();
    for (uint32_t s = 1; s <= 9; s++) {
        REQUIRE(p.take(makeListing(p.h.key, s, "I.ZIP")) == Took::Stored);  // 9 of 12
        indexReady(p.r.core);
    }
    const char* nak = nullptr;
    for (uint32_t s = 10; s <= 30; s++) CHECK(p.take(makeListing(p.h.key, s, "I.ZIP"), &nak) == Took::Rejected);
    CHECK(word(nak) == kNakStore);
    // 3 left of the 12: fresh names are still taken
    for (uint32_t s = 31; s <= 33; s++) CHECK(p.take(makeListing(p.h.key, s, "J" + std::to_string(s))) == Took::Stored);
    CHECK(p.take(makeListing(p.h.key, 34, "J34"), &nak) == Took::Limited);
    CHECK(word(nak) == kNakLimited);
}

TEST_CASE("listings: a removal far below the seen window on an E line is still stored and drops its listing") {
    RevKeys rk;
    Pair p(0xB3, 0xB4);
    CHECK(p.take(makeListing(p.h.key, 1, "OLD.ZIP")) == Took::Stored);
    for (uint32_t s = 10; s <= 80; s++) CHECK(p.r.core.take(makePost(p.h.key, s, kNow), p.h.board, true) == Took::Stored);
    std::string id = p.h.board + "-5";
    REQUIRE(p.r.core.seen().belowWindow(id));
    CHECK(p.take(makeListing(p.h.key, 5, "OLD.ZIP", true)) == Took::Stored);
    CHECK(p.r.liveSeqs("OLD.ZIP").empty());
    CHECK(countName(p.r, "OLD.ZIP") == 1);
    const char* nak = nullptr;
    CHECK(p.take(makeListing(p.h.key, 5, "OLD.ZIP", true), &nak) == Took::Rejected);  // the index is still being built
    CHECK(word(nak) == kNakStore);
    indexReady(p.r.core);
    CHECK(p.take(makeListing(p.h.key, 5, "OLD.ZIP", true)) == Took::Duplicate);  // held now
}

TEST_CASE("listings: listingsPage counts once; a store, a drop and a notice each make it count again") {
    RevKeys rk;
    Pair p(0xB5, 0xB6);
    for (uint32_t s = 1; s <= 20; s++) CHECK(p.take(makeListing(p.h.key, s, "P" + std::to_string(s))) == Took::Stored);
    int next = 0;
    std::vector<std::string> first = p.r.page(0, 15, &next);
    CHECK(first.size() == 15);
    CHECK(first.front() == p.h.board + "-20");
    CHECK(next == 15);
    std::vector<std::string> second = p.r.page(15, 15, &next);
    CHECK(second.size() == 5);
    CHECK(second.back() == p.h.board + "-1");
    CHECK(next == -1);
    CHECK(p.take(makeListing(p.h.key, 21, "P21")) == Took::Stored);
    CHECK(p.r.page(0, 15).front() == p.h.board + "-21");
    CHECK(p.take(makeListing(p.h.key, 22, "P21", true)) == Took::Stored);  // a drop
    CHECK(p.r.page(0, 15).front() == p.h.board + "-20");
    CHECK(p.r.page(0, 100).size() == 20);
    REQUIRE(p.r.core.takeNotice(makeNotice(rev0(), 0, "hold", p.h.board + "-20", 105), ""));
    CHECK(p.r.page(0, 15).front() == p.h.board + "-19");
    CHECK(p.r.page(0, 100).size() == 19);
    CHECK(p.r.page(19, 15).empty());
}

TEST_CASE("listings: a page draw is one scan once the count is held; a store, a drop and a notice count again") {
    RevKeys rk;
    CountedNode r(0xB7);
    UNode       h(0xB8);
    REQUIRE(r.core.takeCard(h.card, ""));
    REQUIRE(r.core.takeNotice(makeNotice(rev0(), 0, "admit", r.card.board, 1, "1"), ""));
    REQUIRE(r.core.takeNotice(makeNotice(rev0(), 0, "admit", h.board, 2, "1"), ""));
    for (uint32_t s = 1; s <= 20; s++) REQUIRE(r.core.takeListing(makeListing(h.key, s, "S" + std::to_string(s)), h.board) == Took::Stored);
    auto draw = [&] {
        r.store.reset();
        int n = 0;
        r.core.listingsPage(0, 15, [&](const NetListing&) { n++; });
        CHECK(n == 15);
        return r.store.scans;
    };
    CHECK(draw() == 2);  // the count, then the page
    CHECK(draw() == 1);
    CHECK(r.core.takeListing(makeListing(h.key, 21, "S21"), h.board) == Took::Stored);
    CHECK(draw() == 2);
    CHECK(draw() == 1);
    CHECK(r.core.takeListing(makeListing(h.key, 22, "S21", true), h.board) == Took::Stored);
    CHECK(draw() == 2);
    REQUIRE(r.core.takeNotice(makeNotice(rev0(), 0, "kill", h.board + "-3", 3), ""));
    CHECK(draw() == 2);
    CHECK(draw() == 1);
    r.now += kDay;
    r.core.trim(r.now);  // drops the killed one
    CHECK(draw() == 2);
}

TEST_CASE("listings: the pull reads nf_list in batches that resume where they stopped, never a whole-area scan") {
    RevKeys rk;
    CountedNode r(0xB9);
    UNode       h(0xBA);
    REQUIRE(r.core.takeCard(h.card, ""));
    REQUIRE(r.core.takeNotice(makeNotice(rev0(), 0, "admit", r.card.board, 1, "1"), ""));
    REQUIRE(r.core.takeNotice(makeNotice(rev0(), 0, "admit", h.board, 2, "1"), ""));
    for (uint32_t s = 1; s <= 100; s++)
        REQUIRE(r.core.takeListing(makeListing(h.key, s, "B" + std::to_string(s) + ".ZIP"), h.board, true) == Took::Stored);
    REQUIRE(r.core.takeListing(makeListing(h.key, 101, "B1.ZIP", true), h.board, true) == Took::Stored);  // one removal
    r.store.reset();
    std::vector<std::string> live, tombs;
    uint32_t                 pos = 0;
    int                      batches = 0;
    for (bool end = false; !end && batches < 100; batches++) {
        size_t before = live.size(), bytes = 0;
        end = r.core.listingLines(pos, false, live);
        for (size_t i = before; i < live.size(); i++) bytes += live[i].size();
        CHECK(bytes <= kNetPendingBytes);
    }
    CHECK(live.size() == 99);
    CHECK(batches >= 2);
    CHECK(r.store.scansFrom == batches);  // one resumed scan a batch
    CHECK(r.store.scans == 0);
    pos = 0;
    while (!r.core.listingLines(pos, true, tombs)) {
    }
    REQUIRE(tombs.size() == 1);
    NetListing g;
    REQUIRE(decode(tombs[0], g));
    CHECK(g.gone);
    CHECK(g.name == "B1.ZIP");
}

TEST_CASE("listings: blocked and hidden listings are not shown, found or pulled; the count follows either list") {
    RevKeys rk;
    FNode r(0xBB), a(0xBC), b(0xBD);
    r.knows(a), r.knows(b);
    r.admit({&r.board, &a.board, &b.board});
    std::vector<std::string> blocks, hides;
    r.core.setLocalFilter([&](const std::string& x) { return std::find(blocks.begin(), blocks.end(), x) != blocks.end(); },
                          [&](const std::string& id) { return std::find(hides.begin(), hides.end(), id) != hides.end(); });
    for (uint32_t s = 1; s <= 2; s++) REQUIRE(r.core.takeListing(makeListing(a.key, s, "A" + std::to_string(s)), a.board) == Took::Stored);
    REQUIRE(r.core.takeListing(makeListing(b.key, 1, "B1"), b.board) == Took::Stored);
    std::string a1 = a.board + "-1", a2 = a.board + "-2", b1 = b.board + "-1";
    auto pulled = [&] {
        std::vector<std::string> lines, ids;
        uint32_t pos = 0;
        while (!r.core.listingLines(pos, false, lines)) {
        }
        for (const std::string& line : lines) {
            NetListing l;
            REQUIRE(decode(line, l));
            ids.push_back(listingId(l));
        }
        return ids;
    };
    NetListing l;
    CHECK(r.page() == std::vector<std::string>{b1, a2, a1});  // (the count is held from here)
    blocks.push_back(a.board);
    r.core.moderationChanged();
    CHECK(r.page() == std::vector<std::string>{b1});
    CHECK_FALSE(r.core.findListing(a1, l));
    CHECK_FALSE(r.core.findListing(a2, l));
    CHECK(pulled() == std::vector<std::string>{b1});
    CHECK(r.core.takeListing(makeListing(a.key, 3, "A3"), a.board) == Took::Rejected);  // a new one is refused
    blocks.clear();
    r.core.moderationChanged();
    CHECK(r.page() == std::vector<std::string>{b1, a2, a1});
    CHECK(r.core.findListing(a1, l));
    hides.push_back(a2);
    r.core.moderationChanged();
    CHECK(r.page() == std::vector<std::string>{b1, a1});
    CHECK_FALSE(r.core.findListing(a2, l));
    CHECK(r.core.findListing(a1, l));
    CHECK(pulled() == std::vector<std::string>{a1, b1});
    hides.clear();
    r.core.moderationChanged();
    CHECK(r.page().size() == 3);
    CHECK(r.core.findListing(a2, l));
}

TEST_CASE("listings: listedHere is this board's own live record of that area and name, shown or not") {
    RevKeys rk;
    FNode h(0xBE), x(0xBF);
    h.knows(x);
    h.admit({&h.board, &x.board});
    NetListing own;
    own.area = "files";
    own.name = "OWN.ZIP";
    own.desc = "a file";
    own.uploader = "Sysop";
    own.reach = "i";
    own.size = 10;
    own.crc = 1;
    std::string id = h.core.originateListing(own);
    REQUIRE(!id.empty());
    REQUIRE(h.core.takeListing(makeListing(x.key, 1, "THEIRS.ZIP"), x.board) == Took::Stored);
    CHECK(h.core.listedHere("files", "OWN.ZIP"));
    CHECK_FALSE(h.core.listedHere("other", "OWN.ZIP"));
    CHECK_FALSE(h.core.listedHere("files", "OWN.ZIP.NEW"));
    CHECK_FALSE(h.core.listedHere("files", "THEIRS.ZIP"));  // another board's
    h.core.setLocalFilter(nullptr, [&](const std::string& i) { return i == id; });  // hidden here: still this board's
    CHECK(h.core.listedHere("files", "OWN.ZIP"));
    own.gone = true;
    REQUIRE(!h.core.originateListing(own).empty());
    CHECK_FALSE(h.core.listedHere("files", "OWN.ZIP"));
}

#if AXIS_TRANSFERS
namespace {
// The board's sharing hooks over a file library in memory, as main.cpp wires them: NetSharingFiles queues a removal,
// and for a rename a removal then the new name (lookDesc); netFilesUploaded queues the upload with its uploader; and
// netShareStep sends a removal only of a name listedHere, and a rename's new name only with its removal.
struct SharingLib : axis::bbs::FileLibrary {
    struct Item {
        std::string name, uploader;
        bool        gone = false, renamed = false;
    };
    std::map<std::string, std::string> files;  // name -> bytes, one area ("files")
    std::vector<Item>                  q;
    std::vector<axis::bbs::FileEntry> list(const std::string&) override {
        std::vector<axis::bbs::FileEntry> v;
        for (auto& f : files) v.push_back({f.first, (uint32_t)f.second.size(), ""});
        return v;
    }
    bool read(const std::string&, const std::string& n, std::string& out, size_t max) override {
        auto it = files.find(n);
        if (it == files.end()) return false;
        out = it->second.substr(0, max);
        return true;
    }
    struct W : axis::bbs::WriteFile {
        SharingLib* lib = nullptr;
        std::string name, data;
        bool        write(const uint8_t* p, size_t n) override {
            data.append((const char*)p, n);
            return true;
        }
        bool finish(bool keep) override {
            if (keep) lib->files[name] = data;
            return true;
        }
    };
    std::unique_ptr<axis::bbs::WriteFile> create(const std::string&, const std::string& n) override {
        std::unique_ptr<W> w(new W);
        w->lib = this;
        w->name = n;
        return std::unique_ptr<axis::bbs::WriteFile>(w.release());
    }
    bool remove(const std::string&, const std::string& n) override {
        if (!files.erase(n)) return false;
        q.push_back({n, "", true, false});
        return true;
    }
    bool rename(const std::string&, const std::string& from, const std::string& to) override {
        auto it = files.find(from);
        if (it == files.end() || files.count(to)) return false;
        files[to] = it->second;
        files.erase(it);
        q.push_back({from, "", true, false});
        q.push_back({to, "Sysop", false, true});
        return true;
    }
    void uploaded(const std::string& n, const std::string& who) { q.push_back({n, who, false, false}); }
    void drain(NetCore& core) {
        bool goneSent = false;
        for (const Item& it : q) {
            indexReady(core);  // as the host's share queue, which waits for the index before each one
            if (it.gone && !(goneSent = core.listedHere("files", it.name))) continue;
            if (it.renamed && !goneSent) continue;
            if (!it.gone && !files.count(it.name)) continue;
            NetListing l;
            l.area = "files";
            l.name = it.name;
            l.gone = it.gone;
            l.uploader = it.uploader;
            l.reach = "i";
            l.desc = "a file";
            if (!it.gone) {
                const std::string& f = files[it.name];
                l.size = (uint32_t)f.size();
                l.crc = crc32((const uint8_t*)f.data(), f.size());
            }
            REQUIRE(!core.originateListing(l).empty());
        }
        q.clear();
    }
};
}  // namespace

TEST_CASE("listings: a rezipped upload into a shared area is listed once by its caller, with no removal sent") {
    RevKeys rk;
    FNode h(0xC5);
    h.admit({&h.board});
    SharingLib    lib;
    std::ifstream in(std::string(AXIS_TEST_DATA) + "/pack.zip", std::ios::binary);
    lib.files["PACK.ZIP"] = std::string((std::istreambuf_iterator<char>(in)), {});
    REQUIRE(lib.files["PACK.ZIP"].size() > 100);
    std::vector<uint8_t>       ws(axis::bbs::ZipReader::kWorkspace);
    axis::bbs::UploadProcessor p(lib, "files", "PACK.ZIP", axis::bbs::UploadInfo(), ws.data(), ws.size());
    for (int i = 0; i < 1000 && p.step(); i++) {
    }
    REQUIRE(p.result() == axis::bbs::UploadProcessor::Result::Rezipped);
    CHECK(lib.q.size() == 3);  // the hooks fired: the upload's removal, then the temp's rename (removal, new name)
    lib.uploaded("PACK.ZIP", "Caller");
    uint32_t seq = h.core.postSeq();
    lib.drain(h.core);
    std::vector<NetListing> rec = h.records();
    REQUIRE(rec.size() == 1);
    CHECK(rec[0].name == "PACK.ZIP");
    CHECK_FALSE(rec[0].gone);
    CHECK(rec[0].uploader == "Caller");
    CHECK(rec[0].crc == crc32((const uint8_t*)lib.files["PACK.ZIP"].data(), lib.files["PACK.ZIP"].size()));  // the rezipped copy
    CHECK(h.core.postSeq() == seq + 1);  // one seq spent
    // A removal of a file never listed sends nothing, nor a rename of one; a listed one's rename and removal do.
    lib.files["OLD.TXT"] = "old";
    lib.files["NEW.TXT"] = "new";
    REQUIRE(lib.remove("files", "OLD.TXT"));
    REQUIRE(lib.rename("files", "NEW.TXT", "NEWER.TXT"));
    lib.drain(h.core);
    CHECK(h.records().size() == 1);
    CHECK(h.core.postSeq() == seq + 1);
    REQUIRE(lib.rename("files", "PACK.ZIP", "ART.ZIP"));
    lib.drain(h.core);
    CHECK(h.liveSeqs("PACK.ZIP").empty());
    CHECK(h.liveSeqs("ART.ZIP").size() == 1);
    CHECK_FALSE(h.core.listedHere("files", "PACK.ZIP"));
    CHECK(h.core.listedHere("files", "ART.ZIP"));
    REQUIRE(lib.remove("files", "ART.ZIP"));
    lib.drain(h.core);
    CHECK(h.liveSeqs("ART.ZIP").empty());
    size_t tombs = 0;
    for (const NetListing& x : h.records()) tombs += x.gone;
    CHECK(tombs == 2);
}
#endif  // AXIS_TRANSFERS
