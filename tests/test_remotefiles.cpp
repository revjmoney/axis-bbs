// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include "bbs/features.h"
#if AXIS_FILES  // a build that leaves the file areas out has nothing here to test
#include <map>
#include <memory>
#include <string>

#include "doctest.h"
#include "cluster/remotefiles.h"
#if AXIS_NETS
#include <deque>
#include <vector>

#include "bbs/filebrowser.h"
#include "net/zmodem.h"
#endif

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

TEST_CASE("a huge area's listing stops short of the link's frame limit instead of breaking the link") {
    Rig r;
    for (int i = 0; i < 400; i++)
        r.sd.areas["big"]["FILE" + std::to_string(1000 + i) + ".ZIP"] = "x";
    for (auto& f : r.sd.areas["big"]) r.sd.desc["big/" + f.first] = std::string(40, 'd');
    auto v = r.remote.list("big");
    CHECK(r.biggest < 8 * 1024 - 64);  // room for the frame's own fields
    CHECK(v.size() > 50);              // as many as fit
    CHECK(v.size() < 400);
    CHECK(v.back().desc == std::string(40, 'd'));  // and the last one whole
}

TEST_CASE("file and area names can't climb out of the file areas") {
    CHECK(safeFileName("ACID-96.ZIP"));
    CHECK(safeFileName("ansi"));
    for (const char* bad : {"", ".", "..", ".hidden", "../users.dat", "..\\users.dat", "a/b", "a\\b", "C:AUTOEXEC.BAT", "x\ny"})
        CHECK_FALSE(safeFileName(bad));
    CHECK_FALSE(safeFileName(std::string(65, 'a')));
}

TEST_CASE("the main refuses a follower's request with a path in an area or file name") {
    Rig         r;
    r.sd.areas[".."]["users.dat"] = "secret";
    std::string out;
    CHECK_FALSE(r.remote.read("..", "users.dat", out, 4096));
    CHECK_FALSE(r.remote.read("ansi", "../../users.dat", out, 4096));
    CHECK(r.remote.list("..").empty());
    CHECK(r.remote.open("ansi", "..\\README.TXT") == nullptr);
    CHECK(r.remote.create("ansi", "../evil.bin") == nullptr);
    CHECK_FALSE(r.remote.remove("..", "users.dat"));
    CHECK_FALSE(r.remote.rename("ansi", "README.TXT", "../README.TXT"));
    CHECK(r.sd.areas["ansi"].count("README.TXT") == 1);
    CHECK(r.sd.areas[".."].count("users.dat") == 1);
}

TEST_CASE("small reads on a follower come out of one link-sized piece: one round trip per 2 KB, bytes intact") {
    Rig  r;
    auto f = r.remote.open("ansi", "ACID-96.ZIP");
    REQUIRE(f);
    const std::string& want = r.sd.areas["ansi"]["ACID-96.ZIP"];
    int                before = r.asks;
    std::string        got;
    for (uint32_t off = 0; off < 4096; off += 512) {  // a stored piece read the way the viewer does
        uint8_t b[512];
        REQUIRE(f->readAt(off, b, sizeof b));
        got.append((const char*)b, sizeof b);
    }
    CHECK(got == want.substr(0, 4096));
    CHECK(r.asks - before == 2);  // 4 KB in two pieces, not eight
    uint8_t tail[3];
    REQUIRE(f->readAt(10000, tail, 3));  // a short last piece near the end
    CHECK(tail[0] == 0x1f);
    CHECK(tail[2] == 0xff);
    uint8_t back[4];
    REQUIRE(f->readAt(100, back, 4));  // back to the start: fetched again, still right
    CHECK(std::string((const char*)back, 4) == want.substr(100, 4));
    CHECK_FALSE(f->readAt(10000, tail, 4));
}

#if AXIS_NETS
// Net Files through the cluster link: the main's net ops and upload hook, the follower's pages, fetch asks and
// the fetched file a follower's sender reads.
namespace {
const std::string S(1, '\x1f');

struct Up {
    std::string area, name, desc, who;
};
std::vector<Up>& ups() {
    static std::vector<Up> v;
    return v;
}
void onUploaded(const std::string& a, const std::string& n, const std::string& d, const std::string& w) { ups().push_back({a, n, d, w}); }

struct NetCall {
    const void*              owner;
    char                     op;
    std::vector<std::string> fields;
};
std::vector<NetCall>& netCalls() {
    static std::vector<NetCall> v;
    return v;
}
std::string netReply = "1";
std::string onNetOp(const void* owner, char op, const std::vector<std::string>& f) {
    netCalls().push_back({owner, op, f});
    return netReply;
}

// Sets the main's hooks for one test and clears them after.
struct Hooks {
    explicit Hooks(bool net = true, bool upload = true) {
        ups().clear();
        netCalls().clear();
        netReply = "1";
        FileServer::netOp = net ? onNetOp : nullptr;
        FileServer::uploaded = upload ? onUploaded : nullptr;
    }
    ~Hooks() {
        FileServer::netOp = nullptr;
        FileServer::uploaded = nullptr;
    }
};

struct OpenCounting : MemLibrary {
    int opens = 0;
    std::unique_ptr<RandomFile> open(const std::string& a, const std::string& n) override {
        opens++;
        return FileLibrary::open(a, n);
    }
};

// One whole upload as a follower sends it: C, W, F keep (or not); its id.
std::string upload(FileServer& s, const std::string& area, const std::string& name, bool keep = true) {
    std::string id = s.handle("C" + S + area + S + name).substr(1);
    s.handle("W" + S + id + S + "data");
    s.handle("F" + S + id + S + (keep ? "1" : "0"));
    return id;
}

#if AXIS_TRANSFERS
// The caller's line on a follower: the sender writes to a, the caller's program reads from b.
struct NetEnd : axis::door::ByteStream {
    std::deque<uint8_t>* in;
    std::deque<uint8_t>* out;
    int  available() override { return (int)in->size(); }
    int  peek() override { return in->empty() ? -1 : in->front(); }
    int  read() override {
        if (in->empty()) return -1;
        int c = in->front();
        in->pop_front();
        return c;
    }
    void write(const uint8_t* p, size_t n) override { out->insert(out->end(), p, p + n); }
    bool carrier() override { return true; }
};
struct NetLine {
    std::deque<uint8_t> ab, ba;
    NetEnd              a, b;
    NetLine() {
        a.in = &ba;
        a.out = &ab;
        b.in = &ab;
        b.out = &ba;
    }
};
#endif

uint32_t fakeNow = 0;
uint32_t fakeClock() { return fakeNow; }
}  // namespace

TEST_CASE("follower: G, b, t and e reach the main's net op with this server as owner and 8 fields; unset they answer 0") {
    OpenCounting sd;
    sd.areas["ansi"]["BIG.ZIP"] = std::string(6000, 'b');
    FileServer server(sd), other(sd);
    {
        Hooks h;
        netReply = "1page";
        for (char op : {'G', 'b', 't', 'e'}) CHECK(server.handle(std::string(1, op) + S + "a" + S + "b") == "1page");
        CHECK(other.handle("e") == "1page");
        REQUIRE(netCalls().size() == 5);
        for (size_t i = 0; i < 4; i++) {
            CHECK(netCalls()[i].owner == &server);
            CHECK(netCalls()[i].fields.size() == 8);
            CHECK(netCalls()[i].fields[0] == "a");
            CHECK(netCalls()[i].fields[1] == "b");
            CHECK(netCalls()[i].fields[2].empty());
        }
        CHECK(netCalls()[4].owner == &other);
        // A net op between two pieces of a download leaves the open file open.
        CHECK(server.handle("S" + S + "ansi" + S + "BIG.ZIP") == "16000");
        CHECK(server.handle("t" + S + "0" + S + "2048")[0] == '1');
        CHECK(server.handle("A" + S + "ansi" + S + "BIG.ZIP" + S + "0" + S + "100") == "1" + std::string(100, 'b'));
        CHECK(sd.opens == 1);
    }
    {
        Hooks h(false, false);
        for (char op : {'G', 'b', 't', 'e'}) CHECK(server.handle(std::string(1, op) + S + "0" + S + "15") == "0");
        // so a follower's page and fetch read "no" from a main without Net Files
        Rig r;
        std::vector<FileEntry>   rows;
        std::vector<std::string> ids, homes;
        CHECK(r.remote.netPage(0, 15, rows, ids, homes) == -1);
        CHECK(rows.empty());
        std::string rest;
        CHECK(r.remote.netFetch("0123456789abcdef-1", "Caller", 10, rest) == '0');
    }
}

TEST_CASE("follower: a kept upload, once described, is handed to the main's upload hook once, with the caller's handle") {
    MemLibrary sd;
    FileServer server(sd);
    Hooks      h;
    upload(server, "ansi", "NEW.ZIP");
    CHECK(ups().empty());  // not until its description
    CHECK(server.handle("D" + S + "ansi" + S + "NEW.ZIP" + S + "A new file" + S + "Caller") == "1");
    REQUIRE(ups().size() == 1);
    CHECK(ups()[0].area == "ansi");
    CHECK(ups()[0].name == "NEW.ZIP");
    CHECK(ups()[0].desc == "A new file");
    CHECK(ups()[0].who == "Caller");
    CHECK(server.handle("D" + S + "ansi" + S + "NEW.ZIP" + S + "Again" + S + "Caller") == "1");  // a sysop's later @FDESC
    CHECK(ups().size() == 1);
    // thrown away: nothing
    upload(server, "ansi", "LOST.ZIP", false);
    server.handle("D" + S + "ansi" + S + "LOST.ZIP" + S + "x" + S + "Caller");
    CHECK(ups().size() == 1);
    // an older follower's D has three fields: the handle is empty (the main gives the sysop's)
    upload(server, "ansi", "OLD.ZIP");
    server.handle("D" + S + "ansi" + S + "OLD.ZIP" + S + "From an old follower");
    REQUIRE(ups().size() == 2);
    CHECK(ups()[1].who.empty());
}

TEST_CASE("follower: a ZIP upload's rework is handed over once under its final name; a rejected one never") {
    MemLibrary sd;
    FileServer server(sd);
    Hooks      h;
    // the processor: the upload lands as a temp, is kept, the old name goes, the temp becomes the name, then D
    upload(server, "ansi", "UP0001.TMP");
    sd.areas["ansi"]["GAME.ZIP"] = "old";
    CHECK(server.handle("X" + S + "ansi" + S + "GAME.ZIP") == "1");
    CHECK(server.handle("N" + S + "ansi" + S + "UP0001.TMP" + S + "GAME.ZIP") == "1");
    CHECK(server.handle("D" + S + "ansi" + S + "GAME.ZIP" + S + "A game" + S + "Caller") == "1");
    REQUIRE(ups().size() == 1);
    CHECK(ups()[0].name == "GAME.ZIP");
    // rejected: the temp and the name removed, nothing described
    upload(server, "ansi", "UP0002.TMP");
    server.handle("X" + S + "ansi" + S + "UP0002.TMP");
    server.handle("X" + S + "ansi" + S + "BAD.ZIP");
    server.handle("D" + S + "ansi" + S + "UP0002.TMP" + S + "x" + S + "Caller");
    CHECK(ups().size() == 1);
}

TEST_CASE("follower: the main keeps the last 4 uploads waiting for their descriptions") {
    MemLibrary sd;
    FileServer server(sd);
    Hooks      h;
    for (int i = 0; i < 5; i++) upload(server, "ansi", "F" + std::to_string(i) + ".ZIP");
    for (int i = 0; i < 5; i++) server.handle("D" + S + "ansi" + S + "F" + std::to_string(i) + ".ZIP" + S + "d" + S + "Caller");
    REQUIRE(ups().size() == 4);
    CHECK(ups()[0].name == "F1.ZIP");  // the oldest went
    CHECK(ups()[3].name == "F4.ZIP");
}

TEST_CASE("follower: describe sends the caller's handle, or an empty one; a 0x1F typed in a description never reaches D") {
    std::string             sent;
    RemoteFileLibrary       lib([&](const std::string& req, std::string& reply) {
        sent = req;
        reply = "1";
        return true;
    });
    CHECK(lib.describe("ansi", "A.ZIP", "Nice"));
    CHECK(sent == "D" + S + "ansi" + S + "A.ZIP" + S + "Nice" + S);
    lib.who = [] { return std::string("Caller"); };
    CHECK(lib.describe("ansi", "A.ZIP", "Nice"));
    CHECK(sent == "D" + S + "ansi" + S + "A.ZIP" + S + "Nice" + S + "Caller");
    // The caller types a description with 0x1F in it: the browser's line editor keeps printable bytes only.
    MemLibrary    up;
    std::string   out;
    std::vector<FileArea> areas = {{"ansi", "ANSI Art"}};
    FileBrowser   b(areas, up, out);
    b.setLightbars(false);
    b.begin();
    for (char c : std::string("1\r")) b.key(c);
    for (char c : std::string("uOne\x1fTwo\r")) b.key(c);
    b.key('z');
    REQUIRE(b.wantsTransfer());
    CHECK(b.transfer().desc == "OneTwo");
}

TEST_CASE("follower: netPage reads rows and the next skip; a short row is passed over; -2 passes through") {
    std::string             reply;
    RemoteFileLibrary       lib([&](const std::string&, std::string& r) {
        r = reply;
        return true;
    });
    std::vector<FileEntry>   rows;
    std::vector<std::string> ids, homes;
    reply = "115\n" "0123456789abcdef-1" + S + "A.ZIP" + S + "1000" + S + "First" + S + "Home One" + S + "1" + "\n" +
            "0123456789abcdef-2" + S + "B.ZIP" + "\n" +  // short: passed over
            "0123456789abcdef-3" + S + "C.ZIP" + S + "3000" + S + "(can't be reached)" + S + "Home Three" + S + "0";
    CHECK(lib.netPage(0, 15, rows, ids, homes) == 15);
    REQUIRE(rows.size() == 2);
    CHECK(rows[0].name == "A.ZIP");
    CHECK(rows[0].size == 1000);
    CHECK(rows[0].desc == "First");
    CHECK(ids[0] == "0123456789abcdef-1");
    CHECK(homes[0] == "Home One");
    CHECK(rows[1].desc == "(can't be reached)");
    CHECK(homes[1] == "Home Three");
    rows.clear(), ids.clear(), homes.clear();
    reply = "1-1";
    CHECK(lib.netPage(15, 15, rows, ids, homes) == -1);
    reply = "1-2";
    CHECK(lib.netPage(0, 15, rows, ids, homes) == -2);
    CHECK(rows.empty());
    reply = "0";
    CHECK(lib.netPage(0, 15, rows, ids, homes) == -1);
}

TEST_CASE("follower: netFetch sends the id, handle and caller level and gives the main's letter; no main gives 0") {
    std::string       sent, reply = "2";
    bool              up = true;
    RemoteFileLibrary lib([&](const std::string& req, std::string& r) {
        if (!up) return false;
        sent = req;
        r = reply;
        return true;
    });
    std::string rest;
    CHECK(lib.netFetch("0123456789abcdef-4", "Caller", 20, rest) == '2');
    CHECK(sent == "b" + S + "0123456789abcdef-4" + S + "Caller" + S + "0" + S + "20");
    reply = "3ansi" + S + "OWN.ZIP";
    CHECK(lib.netFetch("0123456789abcdef-4", "", 20, rest) == '3');
    CHECK(rest == "ansi" + S + "OWN.ZIP");
    reply = "0level";
    CHECK(lib.netFetch("0123456789abcdef-4", "Caller", 5, rest) == '0');
    CHECK(rest == "level");
    up = false;
    CHECK(lib.netFetch("0123456789abcdef-4", "Caller", 5, rest) == 0);
}

TEST_CASE("follower: the fetched file waits on 2, reads a 2 KB piece on 1, ends on 0 or a lost main") {
    std::vector<std::string> asked;
    std::vector<std::string> replies;
    bool                     up = true;
    RemoteFileLibrary        lib([&](const std::string& req, std::string& r) {
        if (!up) return false;
        asked.push_back(req);
        r = replies.empty() ? "2" : replies.front();
        if (!replies.empty()) replies.erase(replies.begin());
        return true;
    });
    fakeNow = 1000;
    {
        RemoteFetchFile f(lib, 5000, fakeClock);
        CHECK_FALSE(f.ready(0, 1024));  // "2": the main has not got the bytes yet
        REQUIRE(asked.size() == 1);
        CHECK(asked[0] == "t" + S + "0" + S + "2048");
        fakeNow += RemoteFetchFile::kWaitMs - 1;
        CHECK_FALSE(f.ready(0, 1024));
        CHECK(asked.size() == 1);  // no new ask within kWaitMs
        fakeNow += 1;
        replies = {"1" + std::string(2048, 'x')};
        CHECK(f.ready(0, 1024));
        CHECK(asked.size() == 2);
        uint8_t b[1024];
        CHECK(f.readAt(0, b, 1024));
        CHECK(f.readAt(1024, b, 1024));  // inside the piece held: no new ask
        CHECK(asked.size() == 2);
        CHECK(b[0] == 'x');
        CHECK(f.word().empty());
        replies = {"1" + std::string(2048, 'y'), "1" + std::string(5000 - 4096, 'z')};
        CHECK(f.ready(2048, 1024));
        CHECK(asked.back() == "t" + S + "2048" + S + "2048");
        CHECK(f.ready(4096, 904));
        CHECK(asked.back() == "t" + S + "4096" + S + "904");  // the last piece: min(2048, size - off)
        CHECK(f.readAt(4096, b, 904));
        CHECK(b[0] == 'z');
        replies = {"0stall"};
        CHECK(f.ready(0, 1024));  // a failed fetch: ready, and reads fail
        CHECK_FALSE(f.readAt(0, b, 1024));
        CHECK(f.word() == "stall");
    }
    {
        RemoteFetchFile f(lib, 5000, fakeClock);
        replies = {"1abc"};  // short
        CHECK(f.ready(0, 1024));
        CHECK(f.word() == "closed");
    }
    {
        RemoteFetchFile f(lib, 5000, fakeClock);
        up = false;
        CHECK(f.ready(0, 1024));
        uint8_t b[16];
        CHECK_FALSE(f.readAt(0, b, 16));
        CHECK(f.word() == "closed");
        up = true;
    }
}

#if AXIS_TRANSFERS
TEST_CASE("follower: a ZMODEM sender over the fetched file is held by 2 answers, then delivers the file whole") {
    std::string file(9000, '\0');
    for (size_t i = 0; i < file.size(); i++) file[i] = (char)(i * 7 + (i >> 8));
    int               waits = 0;
    RemoteFileLibrary lib([&](const std::string& req, std::string& r) {
        if (req[0] != 't') return false;
        if (waits++ % 3 != 2) {  // two "2" answers before each piece
            r = "2";
            return true;
        }
        auto     f = req.find('\x1f', 2);
        uint32_t off = (uint32_t)std::stoul(req.substr(2, f - 2)), k = (uint32_t)std::stoul(req.substr(f + 1));
        r = "1" + file.substr(off, k);
        return true;
    });
    fakeNow = 0;
    RemoteFetchFile      rf(lib, (uint32_t)file.size(), fakeClock);
    NetLine              l;
    std::vector<uint8_t> got;
    bool                 ended = false;
    axis::net::ZmodemSender s(l.a, [&](uint32_t off, uint8_t* buf, size_t n) { return rf.readAt(off, buf, n); },
                              (uint32_t)file.size(), "FAR.ZIP");
    s.setReady([&](uint32_t off, size_t n) { return rf.ready(off, n); });
    axis::net::ZmodemReceiver r(
        l.b, [](const std::string&, uint32_t) { return true; },
        [&](const uint8_t* p, size_t n) { got.insert(got.end(), p, p + n); return true; }, [&](bool ok) { ended = ok; });
    bool a = true, b = true;
    for (int i = 0; i < 40000 && (a || b); i++, fakeNow += 10) {
        if (a) a = s.step(fakeNow);
        if (b) b = r.step(fakeNow);
    }
    CHECK(s.result() == axis::net::Transfer::Result::Ok);
    CHECK(ended);
    CHECK(std::string(got.begin(), got.end()) == file);
    CHECK(waits > 5);
    CHECK(rf.word().empty());
}

TEST_CASE("follower: a 0-byte Net File downloads with no ask of the main") {
    int               asks = 0;
    RemoteFileLibrary lib([&](const std::string&, std::string& r) {
        asks++;
        r = "0closed";
        return true;
    });
    RemoteFetchFile      rf(lib, 0, fakeClock);
    NetLine              l;
    bool                 ended = false;
    std::vector<uint8_t> got;
    axis::net::ZmodemSender s(l.a, [&](uint32_t off, uint8_t* buf, size_t n) { return rf.readAt(off, buf, n); }, 0, "EMPTY.TXT");
    s.setReady([&](uint32_t off, size_t n) { return rf.ready(off, n); });
    axis::net::ZmodemReceiver r(
        l.b, [](const std::string&, uint32_t) { return true; },
        [&](const uint8_t* p, size_t n) { got.insert(got.end(), p, p + n); return true; }, [&](bool ok) { ended = ok; });
    bool a = true, b = true;
    uint32_t now = 0;
    for (int i = 0; i < 20000 && (a || b); i++, now += 10) {
        if (a) a = s.step(now);
        if (b) b = r.step(now);
    }
    CHECK(s.result() == axis::net::Transfer::Result::Ok);
    CHECK(ended);
    CHECK(got.empty());
    CHECK(asks == 0);
}
#endif  // AXIS_TRANSFERS
#endif  // AXIS_NETS

#endif  // AXIS_FILES
