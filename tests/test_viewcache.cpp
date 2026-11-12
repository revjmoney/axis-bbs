// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The ANSI viewer's SD view cache: one uncompressed copy per pack, made a step at a time while the board
// is idle, played with no inflate window held.
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
// Files by area. Like the card: a new file shows only on finish(true), never over one already there.
struct Store : FileLibrary {
    std::map<std::string, std::map<std::string, std::string>> areas;
    int  creates = 0, removes = 0;
    bool readOnly = false;
    std::vector<FileEntry> list(const std::string& a) override {
        std::vector<FileEntry> v;
        for (auto& f : areas[a])
            if (f.first != "FILES.BBS") v.push_back({f.first, (uint32_t)f.second.size(), ""});
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
            if (!keep || s->areas[area].count(name)) return false;
            s->areas[area][name] = data;
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
    bool remove(const std::string& a, const std::string& name) override {
        removes++;
        return areas[a].erase(name) > 0;
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

struct Zip {
    std::string                bytes;
    std::unique_ptr<ZipReader> z;
    explicit Zip(std::string b) : bytes(std::move(b)) {
        z.reset(new ZipReader(
            [this](uint32_t off, uint8_t* buf, size_t n) {
                if ((uint64_t)off + n > bytes.size()) return false;
                std::memcpy(buf, bytes.data() + off, n);
                return true;
            },
            (uint32_t)bytes.size()));
    }
    std::string piece(const ZipEntry& e) {
        std::vector<uint8_t> ws(ZipReader::kWorkspace);
        std::string          out;
        REQUIRE(z->extract(e, ws.data(), ws.size(), [&](const uint8_t* p, size_t n) {
            out.append((const char*)p, n);
            return true;
        }) == ZipReader::Result::Ok);
        return out;
    }
};

int runPass(viewcache::Filler& f) {
    int steps = 0;
    while (f.step() && steps < 1000) steps++;
    return steps;
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

TEST_CASE("view cache: a pass copies the pack into one stored ZIP of its pieces, byte for byte") {
    REQUIRE_FALSE(inflate::busy());
    Store             s = store();
    viewcache::Filler f(s, {"ansi"});
    int               steps = runPass(f);
    CHECK(steps < 1000);
    CHECK(f.copied() == 1);
    CHECK_FALSE(inflate::busy());
    REQUIRE(s.areas[viewcache::kArea].size() == 1);
    Zip src(s.areas["ansi"]["AXISTEST.ZIP"]);
    REQUIRE(src.z->open());
    std::string name = viewcache::cacheName(src.z->entries());
    REQUIRE(s.areas[viewcache::kArea].count(name) == 1);
    Zip copy(s.areas[viewcache::kArea][name]);
    REQUIRE(copy.z->open());
    CHECK(viewcache::matches(src.z->entries(), copy.z->entries()));
    REQUIRE(copy.z->entries().size() == 2);  // REV-AXIS.ANS (deflated) and FILE_ID.DIZ (stored); PROGRAM.EXE is not a piece
    for (auto& e : copy.z->entries()) {
        CHECK(e.method == 0);
        for (auto& o : src.z->entries())
            if (o.name == e.name) CHECK(copy.piece(e) == src.piece(o));
    }
    viewcache::Filler again(s, {"ansi"});
    runPass(again);
    CHECK(again.copied() == 0);
    CHECK(s.creates == 1);
}

TEST_CASE("view cache: small steps hold the window only through a deflated piece; a pass dropped part way keeps nothing") {
    REQUIRE_FALSE(inflate::busy());
    Store s = store();
    {
        viewcache::Filler f(s, {"ansi"}, nullptr, nullptr, 512);
        bool              sawBusy = false;
        for (int i = 0; i < 6; i++) {
            f.step();
            sawBusy = sawBusy || inflate::busy();
        }
        CHECK(sawBusy);
        CHECK(s.areas[viewcache::kArea].empty());
    }  // a caller came online
    CHECK_FALSE(inflate::busy());
    CHECK(s.areas[viewcache::kArea].empty());
    viewcache::Filler f(s, {"ansi"}, nullptr, nullptr, 512);
    CHECK(runPass(f) > 8);
    CHECK(f.copied() == 1);
    CHECK_FALSE(inflate::busy());
}

TEST_CASE("view cache: no room or no card space means no copy, and the window is never kept") {
    REQUIRE_FALSE(inflate::busy());
    Store             s = store();
    int               asked = 0;
    viewcache::Filler f(s, {"ansi"}, [&](uint32_t minFree, uint32_t) {
        asked++;
        CHECK(minFree == viewcache::Filler::kFillFree);
        return false;
    });
    for (int i = 0; i < 50; i++) {
        f.step();
        CHECK_FALSE(inflate::busy());
    }
    CHECK(asked > 0);
    CHECK(s.areas[viewcache::kArea].empty());

    Store             full = store();
    viewcache::Filler g(full, {"ansi"}, nullptr, [](uint64_t) { return false; });
    runPass(g);
    CHECK(g.copied() == 0);
    CHECK(full.creates == 0);
}

TEST_CASE("view cache: copies no pack matches are deleted; a changed pack gets a fresh copy") {
    Store s = store();
    s.areas[viewcache::kArea]["DEADBEEF-0003.ZIP"] = "an old copy";
    viewcache::Filler f(s, {"ansi"});
    runPass(f);
    CHECK(f.copied() == 1);
    CHECK(f.removed() == 1);
    CHECK(s.areas[viewcache::kArea].count("DEADBEEF-0003.ZIP") == 0);
    CHECK(s.areas[viewcache::kArea].size() == 1);
    s.areas["ansi"].erase("AXISTEST.ZIP");  // the pack is gone: its copy goes on the next pass
    viewcache::Filler g(s, {"ansi"});
    runPass(g);
    CHECK(g.removed() == 1);
    CHECK(s.areas[viewcache::kArea].empty());
}

TEST_CASE("view cache: a copied pack plays with no window, even on a busy board; without a copy it plays from the ZIP") {
    REQUIRE_FALSE(inflate::busy());
    Store s = store();
    {
        Viewer vw(s);
        vw.v.setRoom([](uint32_t, uint32_t) { return true; });
        vw.v.begin();
        vw.run("1\r");
        std::string out = vw.run("1\r");  // REV-AXIS.ANS from the ZIP: the window is held at "more"
        CHECK(out.find("line 1") != std::string::npos);
        CHECK(inflate::busy());
        vw.run("n");
        vw.run("q");
        vw.run("q");
        vw.run("q");
    }
    CHECK_FALSE(inflate::busy());
    viewcache::Filler f(s, {"ansi"});
    runPass(f);
    REQUIRE(f.copied() == 1);
    Viewer vw(s);
    vw.v.setRoom([](uint32_t, uint32_t) { return false; });  // a busy board
    vw.v.begin();
    vw.run("1\r");
    std::string out = vw.run("1\r");
    CHECK(out.find("line 1") != std::string::npos);
    CHECK(out.find(kShort) == std::string::npos);
    CHECK_FALSE(inflate::busy());  // off the card
    out = vw.run("n");
    CHECK(out.find("line 40") != std::string::npos);
    vw.run("q");
    vw.run("q");
    vw.run("q");
}

TEST_CASE("view cache: a damaged copy is never played; the pack plays from its ZIP") {
    REQUIRE_FALSE(inflate::busy());
    Store             s = store();
    viewcache::Filler f(s, {"ansi"});
    runPass(f);
    auto& copy = s.areas[viewcache::kArea].begin()->second;
    copy.resize(copy.size() / 2);  // cut short
    Viewer vw(s);
    vw.v.setRoom([](uint32_t, uint32_t) { return true; });
    vw.v.begin();
    vw.run("1\r");
    std::string out = vw.run("1\r");
    out += vw.run("n");
    CHECK(out.find("line 40") != std::string::npos);
    CHECK(out.find("damaged") == std::string::npos);
    vw.run("q");
    vw.run("q");
    vw.run("q");
    CHECK_FALSE(inflate::busy());
}
