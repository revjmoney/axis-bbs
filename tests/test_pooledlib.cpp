// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include <map>
#include <memory>
#include <string>

#include "doctest.h"
#include "cluster/mirrorjob.h"
#include "cluster/pooledlib.h"

using namespace axis::bbs;
using namespace axis::cluster;

namespace {
// One SD card in memory, listed in the order files were added (like FILES.BBS order). "down" makes
// it act like a board whose link just went: every call fails.
struct Card : FileLibrary {
    struct F {
        std::string name, data, desc;
        uint32_t    date = 0;
    };
    std::map<std::string, std::vector<F>> areas;
    bool                                  down = false;
    int                                   opens = 0, lists = 0, writes = 0;
    size_t                                biggestWrite = 0;

    F* find(const std::string& a, const std::string& n) {
        for (F& f : areas[a])
            if (f.name == n) return &f;
        return nullptr;
    }
    void add(const std::string& a, const std::string& n, const std::string& data, const std::string& desc = "", uint32_t date = 0) {
        areas[a].push_back({n, data, desc, date});
    }
    std::vector<FileEntry> list(const std::string& a) override {
        lists++;
        std::vector<FileEntry> v;
        if (down) return v;
        for (F& f : areas[a]) v.push_back({f.name, (uint32_t)f.data.size(), f.desc, f.date});
        return v;
    }
    bool read(const std::string& a, const std::string& n, std::string& out, size_t max) override {
        F* f = down ? nullptr : find(a, n);
        if (!f) return false;
        out = f->data.substr(0, max);
        return true;
    }
    std::unique_ptr<RandomFile> open(const std::string& a, const std::string& n) override {
        opens++;
        if (down || !find(a, n)) return nullptr;
        return FileLibrary::open(a, n);
    }
    struct W : WriteFile {
        Card*       card;
        std::string a, n, data;
        bool        write(const uint8_t* p, size_t k) override {
            if (card->down) return false;
            card->writes++;
            if (k > card->biggestWrite) card->biggestWrite = k;
            data.append((const char*)p, k);
            return true;
        }
        bool finish(bool keep) override {
            if (card->down) return false;
            if (keep) card->add(a, n, data);
            return true;
        }
    };
    std::unique_ptr<WriteFile> create(const std::string& a, const std::string& n) override {
        if (down) return nullptr;
        auto w = std::make_unique<W>();
        w->card = this;
        w->a = a;
        w->n = n;
        return w;
    }
    bool describe(const std::string& a, const std::string& n, const std::string& d) override {
        F* f = down ? nullptr : find(a, n);
        if (!f) return false;
        f->desc = d;
        return true;
    }
    bool remove(const std::string& a, const std::string& n) override {
        if (down) return false;
        auto& v = areas[a];
        for (size_t i = 0; i < v.size(); i++)
            if (v[i].name == n) {
                v.erase(v.begin() + (long)i);
                return true;
            }
        return false;
    }
    bool rename(const std::string& a, const std::string& from, const std::string& to) override {
        F* f = down ? nullptr : find(a, from);
        if (!f || find(a, to)) return false;
        f->name = to;
        return true;
    }
};

struct Pool {
    Card              local, b, c;
    uint64_t          freeLocal = 1000, freeB = 2000, freeC = 500;
    bool              aliveB = true, aliveC = true, haveC = false;
    PooledFileLibrary lib{[this] {
        std::vector<PoolCard> v{{"board-a", "this board", &local, freeLocal, true}, {"board-b", "node 2", &b, freeB, aliveB}};
        if (haveC) v.push_back({"board-c", "node 3", &c, freeC, aliveC});
        return v;
    }};
};

std::string slurp(FileLibrary& lib, const std::string& a, const std::string& n) {
    auto f = lib.open(a, n);
    if (!f) return "(none)";
    std::string s(f->size(), '\0');
    if (!s.empty() && !f->readAt(0, (uint8_t*)&s[0], s.size())) return "(error)";
    return s;
}

bool upload(FileLibrary& lib, const std::string& a, const std::string& n, const std::string& data) {
    auto w = lib.create(a, n);
    return w && w->write((const uint8_t*)data.data(), data.size()) && w->finish(true);
}
}  // namespace

TEST_CASE("pool: an area lists every card's files once, this board's order first, a name matching in any case") {
    Pool p;
    p.local.add("cnc", "A.ZIP", std::string(100, 'a'), "the local copy's description", 10);
    p.local.add("cnc", "C.TXT", "notes");
    p.b.add("cnc", "B.ZIP", "bbb");
    p.b.add("cnc", "a.zip", std::string(150, 'A'), "", 20);  // the same file, a bigger copy
    auto v = p.lib.list("cnc");
    REQUIRE(v.size() == 3);
    CHECK(v[0].name == "a.zip");  // the largest copy is the one listed
    CHECK(v[0].size == 150);
    CHECK(v[0].date == 20);
    CHECK(v[0].desc == "the local copy's description");  // from whichever card describes it
    CHECK(v[1].name == "C.TXT");
    CHECK(v[2].name == "B.ZIP");
    std::vector<PooledFileLibrary::Spot> spots;
    auto                                 placed = PooledFileLibrary::locate("cnc", p.lib.cards(), spots);
    REQUIRE(placed.size() == 3);
    REQUIRE(spots.size() == 3);
    CHECK(spots[0].holders == 3);  // board-a (card 0) and board-b (card 1)
    CHECK(spots[0].first == 1);    // the listed copy: board-b's
    CHECK(spots[2].holders == 2);
}

TEST_CASE("pool: a download reads from the card that has the file, this board first when both do") {
    Pool p;
    p.b.add("cnc", "ONLYB.ZIP", "from b");
    p.local.add("cnc", "BOTH.ZIP", "same");
    p.b.add("cnc", "BOTH.ZIP", "same");
    p.lib.list("cnc");
    CHECK(slurp(p.lib, "cnc", "ONLYB.ZIP") == "from b");
    p.local.opens = p.b.opens = 0;
    CHECK(slurp(p.lib, "cnc", "BOTH.ZIP") == "same");
    CHECK(p.local.opens == 1);
    CHECK(p.b.opens == 0);
    std::string out;
    CHECK(p.lib.read("cnc", "ONLYB.ZIP", out, 3));
    CHECK(out == "fro");
    CHECK(slurp(p.lib, "cnc", "NOPE.ZIP") == "(none)");
    CHECK(slurp(p.lib, "other", "ONLYB.ZIP") == "(none)");
}

TEST_CASE("pool: a file not listed yet (just uploaded) is still found on whichever card has it") {
    Pool p;
    p.b.add("cnc", "NEW.ZIP", "fresh");
    CHECK(slurp(p.lib, "cnc", "NEW.ZIP") == "fresh");  // no listing first
}

TEST_CASE("pool: when the card with the listed copy is down, the download falls back to the mirror copy") {
    Pool p;
    p.b.add("cnc", "M.ZIP", "mirrored");
    p.local.add("cnc", "M.ZIP", "mirrored");
    p.lib.list("cnc");
    p.local.down = true;  // this board's card stopped answering
    CHECK(slurp(p.lib, "cnc", "M.ZIP") == "mirrored");
    p.local.down = false;
    p.aliveB = false;
    CHECK(slurp(p.lib, "cnc", "M.ZIP") == "mirrored");
}

TEST_CASE("pool: a card that is down adds nothing and costs nothing") {
    Pool p;
    p.local.add("cnc", "A.ZIP", "a");
    p.b.add("cnc", "B.ZIP", "b");
    p.aliveB = false;
    p.b.lists = 0;
    auto v = p.lib.list("cnc");
    REQUIRE(v.size() == 1);
    CHECK(v[0].name == "A.ZIP");
    CHECK(p.b.lists == 0);  // not even asked
    p.aliveB = true;
    p.b.down = true;  // listed as alive but its requests fail (the link just went)
    v = p.lib.list("cnc");
    CHECK(v.size() == 1);
    CHECK(slurp(p.lib, "cnc", "B.ZIP") == "(none)");
}

TEST_CASE("pool: an upload goes to the card with the most room, and its description goes with it") {
    Pool p;
    p.haveC = true;
    REQUIRE(upload(p.lib, "cnc", "UP.ZIP", "uploaded"));
    CHECK(p.b.find("cnc", "UP.ZIP"));  // b had 2000 free
    CHECK_FALSE(p.local.find("cnc", "UP.ZIP"));
    CHECK(p.lib.describe("cnc", "UP.ZIP", "a fresh upload"));
    CHECK(p.b.find("cnc", "UP.ZIP")->desc == "a fresh upload");
    p.freeLocal = 9000;
    REQUIRE(upload(p.lib, "cnc", "UP2.ZIP", "two"));
    CHECK(p.local.find("cnc", "UP2.ZIP"));
    p.freeLocal = 0;
    p.aliveB = false;  // the roomiest card is down: the next one
    REQUIRE(upload(p.lib, "cnc", "UP3.ZIP", "three"));
    CHECK(p.c.find("cnc", "UP3.ZIP"));
}

TEST_CASE("pool: an upload whose card refuses it goes to the next roomiest card") {
    Pool p;
    p.b.down = true;  // alive by the cluster's count, but its create fails
    REQUIRE(upload(p.lib, "cnc", "UP.ZIP", "x"));
    CHECK(p.local.find("cnc", "UP.ZIP"));
}

TEST_CASE("pool: a rezipped upload keeps its description on the card that ends up holding it") {
    Pool p;
    REQUIRE(upload(p.lib, "cnc", "PACK.ZIP", "original"));  // lands on b
    p.freeB = 10;  // b filled up meanwhile: the temp copy goes to this board
    REQUIRE(upload(p.lib, "cnc", "PACK.ZIP.tmp", "rezipped"));
    CHECK(p.lib.remove("cnc", "PACK.ZIP"));
    CHECK(p.lib.rename("cnc", "PACK.ZIP.tmp", "PACK.ZIP"));
    CHECK(p.lib.describe("cnc", "PACK.ZIP", "checked"));
    REQUIRE(p.local.find("cnc", "PACK.ZIP"));
    CHECK(p.local.find("cnc", "PACK.ZIP")->desc == "checked");
    CHECK_FALSE(p.b.find("cnc", "PACK.ZIP"));
    auto v = p.lib.list("cnc");
    REQUIRE(v.size() == 1);
    CHECK(v[0].desc == "checked");
}

TEST_CASE("pool: the sysop's remove and rename reach every copy") {
    Pool p;
    p.local.add("cnc", "TWO.ZIP", "x");
    p.b.add("cnc", "TWO.ZIP", "x");
    CHECK(p.lib.rename("cnc", "TWO.ZIP", "DUO.ZIP"));
    CHECK(p.local.find("cnc", "DUO.ZIP"));
    CHECK(p.b.find("cnc", "DUO.ZIP"));
    CHECK(p.lib.remove("cnc", "DUO.ZIP"));
    CHECK(p.local.areas["cnc"].empty());
    CHECK(p.b.areas["cnc"].empty());
    CHECK_FALSE(p.lib.remove("cnc", "DUO.ZIP"));  // nothing left
}

TEST_CASE("pool: with only this board's card it is just that card") {
    Card              local;
    PooledFileLibrary lib{[&] { return std::vector<PoolCard>{{"board-a", "this board", &local, 100, true}}; }};
    local.add("cnc", "A.ZIP", "a", "first", 5);
    local.add("cnc", "B.ZIP", "bb");
    auto v = lib.list("cnc");
    REQUIRE(v.size() == 2);
    CHECK(v[0].name == "A.ZIP");
    CHECK(v[0].desc == "first");
    CHECK(v[0].date == 5);
    REQUIRE(upload(lib, "cnc", "C.ZIP", "c"));
    CHECK(local.find("cnc", "C.ZIP"));
    CHECK(lib.describe("cnc", "C.ZIP", "three"));
    CHECK(local.find("cnc", "C.ZIP")->desc == "three");
    CHECK(slurp(lib, "cnc", "B.ZIP") == "bb");
}

TEST_CASE("pool: with no cards at all nothing is there and uploads are refused") {
    PooledFileLibrary lib{[] { return std::vector<PoolCard>{}; }};
    CHECK(lib.list("cnc").empty());
    CHECK_FALSE(lib.open("cnc", "A.ZIP"));
    CHECK_FALSE(lib.create("cnc", "A.ZIP"));
    CHECK_FALSE(lib.describe("cnc", "A.ZIP", "x"));
}

// The mirror: files in a mirrored area held by one live card get a copy on a second card.
namespace {
struct Mirror : Pool {
    std::vector<std::string> log;
    MirrorJob                job{lib, [] { return std::vector<std::string>{"cnc"}; }};
    uint32_t                 now = 1000;
    Mirror() {
        freeLocal = 2u << 20;  // a target keeps a spare megabyte
        freeB = 4u << 20;
        freeC = 3u << 20;
        job.onEvent = [this](const std::string& e) { log.push_back(e); };
    }
    int steps(int n) {  // run up to n steps, return how many it took to finish a copy (0: none)
        uint32_t before = job.copied();
        for (int i = 1; i <= n; i++) {
            job.step(now += 10);
            if (job.copied() != before) return i;
        }
        return 0;
    }
};
std::string bytes(size_t n) {
    std::string s(n, 'x');
    for (size_t i = 0; i < n; i++) s[i] = (char)(i * 7 + i / 251);
    return s;
}
}  // namespace

TEST_CASE("mirror: a file only one card holds is copied, byte for byte, to the card with the most room") {
    Mirror m;
    m.haveC = true;
    m.local.add("cnc", "ONE.ZIP", bytes(5000), "a lonely file");
    m.local.add("cnc", "BOTH.ZIP", "x");
    m.b.add("cnc", "BOTH.ZIP", "x");
    m.local.add("text", "OTHER.TXT", "not a mirrored area");
    REQUIRE(m.steps(50));
    REQUIRE(m.b.find("cnc", "ONE.ZIP"));  // b has 2000 free, c 500
    CHECK(m.b.find("cnc", "ONE.ZIP")->data == bytes(5000));
    CHECK(m.b.find("cnc", "ONE.ZIP")->desc == "a lonely file");  // its description goes with it
    CHECK_FALSE(m.c.find("cnc", "ONE.ZIP"));
    CHECK(m.b.areas["cnc"].size() == 2);  // BOTH.ZIP was already on two cards
    CHECK(m.b.areas["text"].empty());
    REQUIRE(m.log.size() == 1);
    CHECK(m.log[0] == "mirror: copied ONE.ZIP to node 2");
    CHECK(m.steps(50) == 0);  // nothing left to do
    CHECK(m.job.copied() == 1);
}

TEST_CASE("mirror: a copy goes a piece at a time and shows only when it is whole") {
    Mirror m;
    m.b.add("cnc", "BIG.ZIP", bytes(5000));
    m.freeLocal = 1ull << 30;
    int took = m.steps(50);
    REQUIRE(took);
    CHECK(took >= 3);  // 5000 bytes in 2 KB pieces, one per step
    CHECK(m.local.writes == 3);
    CHECK(m.local.biggestWrite <= MirrorJob::kChunk);
    CHECK(m.local.find("cnc", "BIG.ZIP")->data == bytes(5000));
    CHECK(m.log.back() == "mirror: copied BIG.ZIP to this board");
}

TEST_CASE("mirror: no copy before it is whole, and a board leaving mid-copy throws it away to try again later") {
    Mirror m;
    m.local.add("cnc", "BIG.ZIP", bytes(9000));
    for (int i = 0; i < 20 && m.b.writes == 0; i++) m.job.step(m.now += 10);
    REQUIRE(m.job.copying());
    CHECK_FALSE(m.b.find("cnc", "BIG.ZIP"));  // not shown half written
    m.aliveB = false;  // board b leaves
    m.job.step(m.now += 10);
    CHECK_FALSE(m.job.copying());
    CHECK_FALSE(m.b.find("cnc", "BIG.ZIP"));
    CHECK(m.job.failed() == 1);
    REQUIRE_FALSE(m.log.empty());
    CHECK(m.log.back().find("BIG.ZIP") != std::string::npos);
    m.aliveB = true;  // it comes back: the next pass copies the file again
    CHECK(m.steps(100) == 0);  // not straight away: it rests between passes
    m.now += MirrorJob::kRestMs;
    REQUIRE(m.steps(100));
    CHECK(m.b.find("cnc", "BIG.ZIP")->data == bytes(9000));
}

TEST_CASE("mirror: a target whose writes start failing (its link just went) is dropped the same way") {
    Mirror m;
    m.local.add("cnc", "BIG.ZIP", bytes(9000));
    for (int i = 0; i < 20 && m.b.writes == 0; i++) m.job.step(m.now += 10);
    REQUIRE(m.job.copying());
    m.b.down = true;
    m.job.step(m.now += 10);
    CHECK_FALSE(m.job.copying());
    CHECK(m.job.failed() == 1);
    m.b.down = false;
    CHECK_FALSE(m.b.find("cnc", "BIG.ZIP"));
}

TEST_CASE("mirror: with no other card that has room, nothing is copied and nothing breaks") {
    Mirror m;
    m.local.add("cnc", "ONE.ZIP", bytes(100));
    m.freeB = 1000;  // less than the spare a target must keep
    CHECK(m.steps(50) == 0);
    CHECK(m.b.areas["cnc"].empty());
    m.aliveB = false;  // only one live card
    CHECK(m.steps(50) == 0);
    CHECK(m.job.failed() == 0);
    CHECK_FALSE(m.job.state().empty());
}
