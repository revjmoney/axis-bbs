// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The ANSI viewer's SD view cache: pieces unpacked once, then played with no inflate window held.
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

#include "doctest.h"
#include "bbs/ansiviewer.h"
#include "bbs/inflatelease.h"
#include "bbs/viewcache.h"
#include "bbs/zipreader.h"
#include "door/bytestream.h"

using namespace axis::bbs;

namespace {
// Files by area, uploads too (finish(true) makes one visible), so the cache area works like the card.
struct Store : FileLibrary {
    std::map<std::string, std::map<std::string, std::string>> areas;
    int  creates = 0;
    bool readOnly = false;
    std::vector<FileEntry> list(const std::string& a) override {
        std::vector<FileEntry> v;
        for (auto& f : areas[a])
            if (f.first != "FILES.BBS") v.push_back({f.first, (uint32_t)f.second.size(), f.first == "AXISTEST.ZIP" ? "Test pack" : ""});
        return v;
    }
    bool read(const std::string& a, const std::string& name, std::string& out, size_t max) override {
        auto& m = areas[a];
        auto  it = m.find(name);
        if (it == m.end()) return false;
        out = it->second.substr(0, max);
        return true;
    }
    struct Writer : WriteFile {
        Store*      s;
        std::string area, name, data;
        bool        write(const uint8_t* p, size_t n) override {
            data.append((const char*)p, n);
            return true;
        }
        bool finish(bool keep) override {
            if (keep) s->areas[area][name] = data;
            return true;
        }
    };
    std::unique_ptr<WriteFile> create(const std::string& a, const std::string& name) override {
        if (readOnly) return nullptr;
        creates++;
        std::unique_ptr<Writer> w(new Writer);
        w->s = this;
        w->area = a;
        w->name = name;
        return std::unique_ptr<WriteFile>(w.release());
    }
};

std::string packBytes() {
    std::ifstream in(std::string(AXIS_TEST_DATA) + "/pack.zip", std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), {});
}

Store store() {
    Store s;
    s.areas["ansi"]["AXISTEST.ZIP"] = packBytes();
    s.areas["ansi"]["FILES.BBS"] = "AXISTEST.ZIP Test pack\r\n";
    return s;
}

const ZipEntry& entryNamed(ZipReader& z, const std::string& n) {
    for (auto& e : z.entries())
        if (e.name == n) return e;
    FAIL("no entry " << n);
    return z.entries()[0];
}

struct Viewer {
    Store&                   lib;
    axis::door::ScriptStream line;
    AnsiViewer               v{lib, "ansi", "ANSI Packs", nullptr, 0, line};  // the board's way: it borrows the window
    explicit Viewer(Store& s) : lib(s) { v.setCache(true); }
    std::string run(const std::string& keys) {
        line.push(keys);
        for (int i = 0; i < 20 && v.step(); i++) {
        }
        std::string out = line.output();
        line.clearOutput();
        return out;
    }
};
const char* kShort = "The board is short on memory for another movie right now. Try again in a minute.";
}  // namespace

TEST_CASE("view cache: a piece unpacks to its CRC-and-size name, whole and right, and opens at its size") {
    Store       s = store();
    std::string pack = s.areas["ansi"]["AXISTEST.ZIP"];
    ZipReader   z([&](uint32_t off, uint8_t* b, size_t n) {
        if (off + n > pack.size()) return false;
        std::memcpy(b, pack.data() + off, n);
        return true;
    }, (uint32_t)pack.size());
    REQUIRE(z.open());
    const ZipEntry& e = entryNamed(z, "REV-AXIS.ANS");
    REQUIRE(e.method == 8);
    CHECK(viewcache::name(e).size() <= 64);
    CHECK_FALSE(viewcache::open(s, e));
    std::vector<uint8_t> ws(ZipReader::kWorkspace);
    REQUIRE(viewcache::unpack(s, z, e, ws.data(), ws.size(), nullptr));
    std::string want;
    REQUIRE(z.extract(e, ws.data(), ws.size(), [&](const uint8_t* p, size_t n) {
        want.append((const char*)p, n);
        return true;
    }) == ZipReader::Result::Ok);
    CHECK(s.areas[viewcache::kArea][viewcache::name(e)] == want);
    auto f = viewcache::open(s, e);
    REQUIRE(f);
    CHECK(f->size() == e.usize);
    s.areas[viewcache::kArea][viewcache::name(e)].pop_back();  // a short copy counts as none
    CHECK_FALSE(viewcache::open(s, e));
    ZipEntry wrong = e;  // a CRC that does not match: nothing is kept
    wrong.crc ^= 1;
    CHECK_FALSE(viewcache::unpack(s, z, wrong, ws.data(), ws.size(), nullptr));
    CHECK(s.areas[viewcache::kArea].count(viewcache::name(wrong)) == 0);
}

TEST_CASE("view cache: the first view unpacks, every view plays off the card with no window, even on a busy board") {
    REQUIRE_FALSE(inflate::busy());
    Store    s = store();
    Viewer   vw(s);
    uint32_t askedFree = 0;
    bool     room = true;
    vw.v.setRoom([&](uint32_t minFree, uint32_t) {
        askedFree = minFree;
        return room;
    });
    vw.v.begin();
    vw.run("1\r");
    std::string out = vw.run("1\r");  // REV-AXIS.ANS: unpacked (the looser unpack floor), then played from the cache
    CHECK(askedFree == AnsiViewer::kUnpackFree);
    CHECK(s.areas[viewcache::kArea].size() == 1);
    CHECK(out.find("line 1") != std::string::npos);
    CHECK_FALSE(inflate::busy());  // waiting at "more" off the card: no window held
    out = vw.run("n");
    CHECK(out.find("line 40") != std::string::npos);
    CHECK(out.find(kShort) == std::string::npos);
    room = false;  // a busy board: the cached piece plays all the same, and nothing asks
    askedFree = 0;
    out = vw.run("p");  // P on the shown piece: the same piece again
    out += vw.run("n");
    CHECK(askedFree == 0);
    CHECK(out.find("line 40") != std::string::npos);
    CHECK(out.find(kShort) == std::string::npos);
    CHECK(s.creates == 1);  // unpacked once
    vw.run("q");
    vw.run("q");
    vw.run("q");
    CHECK_FALSE(inflate::busy());
}

TEST_CASE("view cache: no room to unpack refuses with the note; a store that cannot keep it plays from the ZIP") {
    REQUIRE_FALSE(inflate::busy());
    Store  s = store();
    Viewer vw(s);
    bool   room = false;
    vw.v.setRoom([&](uint32_t, uint32_t) { return room; });
    vw.v.begin();
    vw.run("1\r");
    std::string out = vw.run("1\r");
    CHECK(out.find(kShort) != std::string::npos);
    CHECK(s.areas[viewcache::kArea].empty());
    CHECK_FALSE(inflate::busy());

    Store  ro = store();
    ro.readOnly = true;
    Viewer v2(ro);
    v2.v.setRoom([](uint32_t, uint32_t) { return true; });
    v2.v.begin();
    v2.run("1\r");
    out = v2.run("1\r");
    CHECK(out.find("line 1") != std::string::npos);
    CHECK(inflate::busy());  // straight from the ZIP: the window is held while it waits at "more"
    out = v2.run("n");
    CHECK(out.find("line 40") != std::string::npos);
    CHECK_FALSE(inflate::busy());
    v2.run("q");
    v2.run("q");
    v2.run("q");
}

TEST_CASE("view cache: the filler unpacks every deflated piece once, skips stored ones, and waits while the window is lent") {
    REQUIRE_FALSE(inflate::busy());
    Store s = store();
    s.areas["scene"]["OTHER.ZIP"] = packBytes();  // the same pieces in a second area: one shared copy
    s.areas["scene"]["README.TXT"] = "not a pack";
    {
        int      holder = 0;
        uint8_t *w = nullptr, *st = nullptr;
        REQUIRE(inflate::take(&holder, w, st));
        viewcache::Filler f(s, {"ansi", "scene"});
        for (int i = 0; i < 10; i++) f.step();
        CHECK(f.unpacked() == 0);  // a caller's piece has the window: nothing is taken from it
        inflate::give(&holder);
    }
    viewcache::Filler f(s, {"ansi", "scene", "missing"});
    int steps = 0;
    while (f.step() && steps < 100) steps++;
    CHECK(steps < 100);
    CHECK(f.unpacked() == 1);                        // REV-AXIS.ANS; FILE_ID.DIZ is stored
    CHECK(s.areas[viewcache::kArea].size() == 1);
    CHECK_FALSE(inflate::busy());
    viewcache::Filler again(s, {"ansi", "scene"});
    while (again.step()) {
    }
    CHECK(again.unpacked() == 0);
    CHECK(s.creates == 1);
}

TEST_CASE("view cache: the filler unpacks a piece across steps, and dropping it part way keeps nothing and frees the window") {
    REQUIRE_FALSE(inflate::busy());
    Store s = store();
    {
        viewcache::Filler f(s, {"ansi"}, 512);  // REV-AXIS.ANS is 3703 bytes: several steps
        int                steps = 0;
        while (steps < 4) {
            f.step();
            steps++;
        }
        CHECK(inflate::busy());  // held from the piece's first step to its last
        CHECK(s.areas[viewcache::kArea].empty());
    }  // a caller came online: the pass is dropped mid-piece
    CHECK_FALSE(inflate::busy());
    CHECK(s.areas[viewcache::kArea].empty());
    viewcache::Filler f(s, {"ansi"}, 512);
    int steps = 0;
    while (f.step() && steps < 100) steps++;
    CHECK(steps > 6);
    CHECK(f.unpacked() == 1);
    CHECK(s.areas[viewcache::kArea].size() == 1);
    CHECK_FALSE(inflate::busy());
}

TEST_CASE("view cache: a piece over the on-view limit is not unpacked while the caller waits; it plays from the ZIP") {
    REQUIRE_FALSE(inflate::busy());
    Store  s = store();
    Viewer vw(s);
    vw.v.setCache(true, 1000);  // REV-AXIS.ANS (3703 bytes) is over it
    vw.v.setRoom([](uint32_t, uint32_t) { return true; });
    vw.v.begin();
    vw.run("1\r");
    std::string out = vw.run("1\r");
    CHECK(out.find("line 1") != std::string::npos);
    CHECK(s.creates == 0);
    CHECK(inflate::busy());  // from the ZIP: the window is held at "more"
    out = vw.run("n");
    CHECK(out.find("line 40") != std::string::npos);
    CHECK_FALSE(inflate::busy());
    vw.run("q");
    vw.run("q");
    vw.run("q");
}
