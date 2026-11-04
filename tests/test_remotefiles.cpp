// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include <map>
#include <memory>
#include <string>

#include "doctest.h"
#include "cluster/remotefiles.h"

using namespace axis::bbs;
using namespace axis::cluster;

namespace {
// The main's SD card, in memory: area -> name -> bytes, plus FILES.BBS descriptions.
struct MemLibrary : FileLibrary {
    std::map<std::string, std::map<std::string, std::string>> areas;
    std::map<std::string, std::string>                        desc;  // "area/name" -> description

    std::vector<FileEntry> list(const std::string& a) override {
        std::vector<FileEntry> v;
        for (auto& f : areas[a]) v.push_back({f.first, (uint32_t)f.second.size(), desc[a + "/" + f.first]});
        return v;
    }
    bool read(const std::string& a, const std::string& n, std::string& out, size_t max) override {
        auto it = areas[a].find(n);
        if (it == areas[a].end()) return false;
        out = it->second.substr(0, max);
        return true;
    }
    struct W : WriteFile {
        MemLibrary* lib;
        std::string a, n, data;
        bool        write(const uint8_t* p, size_t k) override {
            data.append((const char*)p, k);
            return true;
        }
        bool finish(bool keep) override {
            if (keep) lib->areas[a][n] = data;
            return true;
        }
    };
    std::unique_ptr<WriteFile> create(const std::string& a, const std::string& n) override {
        auto w = std::make_unique<W>();
        w->lib = this;
        w->a = a;
        w->n = n;
        return w;
    }
    bool describe(const std::string& a, const std::string& n, const std::string& d) override {
        desc[a + "/" + n] = d;
        return true;
    }
    bool remove(const std::string& a, const std::string& n) override { return areas[a].erase(n) == 1; }
    bool rename(const std::string& a, const std::string& from, const std::string& to) override {
        if (!areas[a].count(from) || areas[a].count(to)) return false;
        areas[a][to] = areas[a][from];
        areas[a].erase(from);
        return true;
    }
};

struct Rig {
    MemLibrary        sd;
    FileServer        server{sd};
    int               asks = 0;
    size_t            biggest = 0;  // the largest reply, to keep link frames small
    bool              linkUp = true;
    RemoteFileLibrary remote{[this](const std::string& req, std::string& reply) {
        if (!linkUp) return false;
        asks++;
        reply = server.handle(req);
        if (reply.size() > biggest) biggest = reply.size();
        return true;
    }};
    Rig() {
        sd.areas["ansi"]["ACID-96.ZIP"] = std::string(10000, 'z') + std::string("\x1f\x00\xff", 3);  // binary safe?
        sd.desc["ansi/ACID-96.ZIP"] = "ACiD Productions, 1996";
        sd.areas["ansi"]["README.TXT"] = "hello";
    }
};
}  // namespace

TEST_CASE("a follower lists an area on the main's SD card, descriptions included") {
    Rig  r;
    auto v = r.remote.list("ansi");
    REQUIRE(v.size() == 2);
    CHECK(v[0].name == "ACID-96.ZIP");
    CHECK(v[0].size == 10003);
    CHECK(v[0].desc == "ACiD Productions, 1996");
}

TEST_CASE("a follower reads a whole small file") {
    Rig         r;
    std::string out;
    REQUIRE(r.remote.read("ansi", "README.TXT", out, 4096));
    CHECK(out == "hello");
    CHECK_FALSE(r.remote.read("ansi", "NOPE.TXT", out, 4096));
}

TEST_CASE("a download reads any part of a big file in small link-sized pieces, bytes intact") {
    Rig  r;
    auto f = r.remote.open("ansi", "ACID-96.ZIP");
    REQUIRE(f);
    CHECK(f->size() == 10003);
    std::string got(10003, '\0');
    REQUIRE(f->readAt(0, (uint8_t*)&got[0], got.size()));
    CHECK(got == r.sd.areas["ansi"]["ACID-96.ZIP"]);
    uint8_t tail[3];
    REQUIRE(f->readAt(10000, tail, 3));
    CHECK(tail[0] == 0x1f);
    CHECK(tail[1] == 0x00);
    CHECK(tail[2] == 0xff);
    CHECK(r.biggest <= FileServer::kChunk + 16);
    CHECK_FALSE(f->readAt(10000, tail, 4));  // past the end
}

TEST_CASE("an upload from a follower lands on the main's card only when kept") {
    Rig  r;
    auto w = r.remote.create("ansi", "NEW.ZIP");
    REQUIRE(w);
    std::string data(5000, 'q');
    data[17] = '\x1f';
    REQUIRE(w->write((const uint8_t*)data.data(), data.size()));
    CHECK(r.sd.areas["ansi"].count("NEW.ZIP") == 0);
    REQUIRE(w->finish(true));
    CHECK(r.sd.areas["ansi"]["NEW.ZIP"] == data);
    REQUIRE(r.remote.describe("ansi", "NEW.ZIP", "fresh upload"));
    CHECK(r.sd.desc["ansi/NEW.ZIP"] == "fresh upload");

    auto drop = r.remote.create("ansi", "JUNK.ZIP");
    drop->write((const uint8_t*)"x", 1);
    drop->finish(false);
    CHECK(r.sd.areas["ansi"].count("JUNK.ZIP") == 0);
}

TEST_CASE("an upload abandoned when the caller hangs up is thrown away on the main") {
    Rig r;
    {
        auto w = r.remote.create("ansi", "HALF.ZIP");
        w->write((const uint8_t*)"abc", 3);
    }
    CHECK(r.sd.areas["ansi"].count("HALF.ZIP") == 0);
    CHECK(r.server.openUploads() == 0);
}

TEST_CASE("sysop remove and rename go to the main") {
    Rig r;
    REQUIRE(r.remote.rename("ansi", "README.TXT", "READ.ME"));
    CHECK(r.sd.areas["ansi"].count("READ.ME") == 1);
    REQUIRE(r.remote.remove("ansi", "READ.ME"));
    CHECK(r.sd.areas["ansi"].count("READ.ME") == 0);
}

TEST_CASE("with the link down the follower's file areas are just empty, not broken") {
    Rig r;
    r.linkUp = false;
    CHECK(r.remote.list("ansi").empty());
    std::string out;
    CHECK_FALSE(r.remote.read("ansi", "README.TXT", out, 10));
    CHECK_FALSE(r.remote.open("ansi", "README.TXT"));
    CHECK_FALSE(r.remote.create("ansi", "X.ZIP"));
}
