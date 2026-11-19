// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The card mirror's bookkeeping (src/platform/linux/cardmirror.h): the manifest, the listing difference, the noon
// check and the copy's pace.
#include <string>
#include <vector>

#include "doctest.h"
#include "platform/linux/cardmirror.h"

namespace {
CardEntry entry(const char* path, uint64_t size, int64_t mtime) {
    CardEntry e;
    e.path = path;
    e.size = size;
    e.mtime = mtime;
    return e;
}
Manifest manifestOf(std::vector<CardEntry> files) {
    Manifest m;
    for (const CardEntry& e : files) m.files[e.path] = e;
    return m;
}
}  // namespace

TEST_CASE("a manifest survives format and parse") {
    Manifest m = manifestOf({entry("a.txt", 10, 1700000000), entry("dir/with space.dat", 0, 0), entry("dir/big.bin", 5000000000ull, 1700000123)});
    m.gen = 3;
    m.seq = 41;
    m.complete = true;
    m.noon = "2026-11-17";
    Manifest back;
    REQUIRE(parseManifest(formatManifest(m), back));
    CHECK(back.gen == 3);
    CHECK(back.seq == 41);
    CHECK(back.complete);
    CHECK(back.noon == "2026-11-17");
    REQUIRE(back.files.size() == 3);
    for (const auto& kv : m.files) {
        REQUIRE(back.files.count(kv.first) == 1);
        const CardEntry& e = back.files[kv.first];
        CHECK(e.path == kv.second.path);
        CHECK(e.size == kv.second.size);
        CHECK(e.mtime == kv.second.mtime);
    }
    Manifest empty, again;
    REQUIRE(parseManifest(formatManifest(empty), again));  // the first manifest of a fresh mirror
    CHECK_FALSE(again.complete);
    CHECK(again.files.empty());
    Manifest keep = m;  // what it cannot read leaves its output alone
    CHECK_FALSE(parseManifest("#gen=x\n", keep));
    CHECK_FALSE(parseManifest("not a manifest line\n", keep));
    CHECK(keep.gen == 3);
    CHECK(keep.files.size() == 3);
    // a path outside the mirror is dropped, never kept
    std::string sep(1, '\x1f');
    Manifest    dropped;
    REQUIRE(parseManifest("ok.txt" + sep + "1" + sep + "2\n../evil" + sep + "1" + sep + "2\nx.log" + sep + "1" + sep + "2\n", dropped));
    CHECK(dropped.files.size() == 1);
    CHECK(dropped.files.count("ok.txt") == 1);
}

TEST_CASE("the diff copies what is missing or different and removes what the card no longer has") {
    Manifest m = manifestOf({entry("same", 10, 100), entry("bigger", 10, 100), entry("newer", 10, 100), entry("gone", 4, 4)});
    std::vector<CardEntry> card = {entry("same", 10, 100), entry("bigger", 11, 100), entry("newer", 10, 101), entry("fresh", 1, 1)};
    MirrorDiff d = diffListing(m, card);
    REQUIRE(d.copy.size() == 3);
    CHECK(d.copy[0].path == "bigger");
    CHECK(d.copy[1].path == "newer");
    CHECK(d.copy[2].path == "fresh");
    CHECK(d.copy[2].size == 1);
    REQUIRE(d.remove.size() == 1);
    CHECK(d.remove[0] == "gone");

    Manifest               equal = manifestOf({entry("a", 1, 2), entry("b/c", 3, 4)});
    std::vector<CardEntry> same = {entry("b/c", 3, 4), entry("a", 1, 2)};
    MirrorDiff             none = diffListing(equal, same);
    CHECK(none.copy.empty());
    CHECK(none.remove.empty());
}

TEST_CASE("the noon check is due once a day at or after noon") {
    CHECK_FALSE(noonDue("2026-11-17", "2026-11-18", 719, 720));
    CHECK(noonDue("2026-11-17", "2026-11-18", 720, 720));
    CHECK_FALSE(noonDue("2026-11-18", "2026-11-18", 900, 720));
    CHECK(noonDue("", "2026-11-18", 721, 720));  // never checked: the first noon
}

TEST_CASE("the pace gives the full rate when idle and a quarter when someone is on") {
    auto sent = [](bool busy) {  // 10 s of 10 ms turns, each asking for 100 bytes
        Pace     p(1000);
        p.setBusy(busy);
        uint64_t bytes = 0;
        for (uint32_t t = 0; t <= 10000; t += 10)
            if (p.take(100, t)) bytes += 100;
        return bytes;
    };
    uint64_t idle = sent(false), busy = sent(true);
    CHECK(idle >= 10000);
    CHECK(idle <= 11200);
    CHECK(busy >= 2500);
    CHECK(busy <= 3000);
    CHECK(busy * 3 < idle);
    Pace p(1000);  // a reply bigger than what was taken is charged after the fact
    REQUIRE(p.take(100, 0));
    p.charge(5000);
    CHECK_FALSE(p.take(1, 0));
    CHECK_FALSE(p.take(1, 1000));  // a second later the budget is still overdrawn
    CHECK(p.take(1, 6000));
}
