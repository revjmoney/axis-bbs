// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// Board 1 memory fixes (docs/superpowers/plans/2026-11-11-board1-memory.md): each test drives the real code and fails if
// the fix it guards is taken back out.
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <new>
#include <string>
#include <vector>

#include "doctest.h"
#include "bbs/ansiterm.h"
#include "bbs/ansiviewer.h"
#include "bbs/artfetch.h"
#include "bbs/diag.h"
#include "bbs/heapwin.h"
#include "bbs/inflatelease.h"
#include "bbs/listtrim.h"
#include "bbs/memlog.h"
#include "bbs/node.h"
#include "bbs/password.h"
#include "bbs/pipecodes.h"
#include "bbs/users.h"
#include "bbs/webwork.h"
#include "bbs/wfc.h"
#include "bbs/zipreader.h"
#include "door/bytestream.h"

using namespace axis::bbs;
namespace fs = std::filesystem;

namespace {
SysInfo  info() { return SysInfo{}; }
uint32_t rng() { return 0x5A5A5A5Au; }

size_t count(const std::string& s, const std::string& what) {
    size_t n = 0;
    for (size_t i = s.find(what); i != std::string::npos; i = s.find(what, i + what.size())) n++;
    return n;
}

// numbered lines of plain text, about `bytes` long: every piece of it is different, so a resend or a lost piece shows
std::string numberedArt(size_t bytes) {
    std::string a;
    for (int i = 0; a.size() < bytes; i++) {
        char b[64];
        std::snprintf(b, sizeof b, "line %05d of the memory test art ......................\r\n", i);
        a += b;
    }
    return a;
}

// ViSiON/3's front door on menu files (as tests/test_node_login.cpp): the matrix, "j" for the pre-logon art, then LOGIN
const char* kMatrix = "cmd=J|LOGIN|*|0|\ncmd=C|NEWUSER|*|0|\ncmd=A|CHECKACCESS|*|0|\ncmd=D|DISCONNECT|*|0|\n"
                      "bar=49,10,31,5,J, Journey onward.\nbar=49,11,31,5,C, Create an account.\n"
                      "bar=49,12,31,5,A, Check your access.\nbar=49,13,31,5,D, Disconnect.\n";

struct FrontRig {
    UserStore                          users;
    std::map<std::string, std::string> fs = {{"MAIN.MNU", "prompt1=MAIN ->\ncmd=M|GOTO:MSGMENU|*|0|\n"}, {"PDMATRIX.MNU", kMatrix},
                                             {"PRELOGON1.ANS", "PRE ART"}, {"LOGIN.ANS", "LOGIN ART"}};
    std::map<std::string, int>         loads;  // menuLoad_ calls, by file
    Node                               n;
    std::string                        last;
    FrontRig() : n(1, info, BbsConfig()) {
        User u;
        u.handle = "Rev";
        u.salt = makeSalt(rng);
        u.hash = hashPassword(u.salt, "pass1234");
        u.status = User::Status::Validated;
        u.security = 20;
        users.add(u);
        n.setAccounts(&users, [] {}, rng, [] { return 1800000000u; });
    }
    void start() {
        n.setMenus([this](const std::string& f) {
            loads[f]++;
            return fs.count(f) ? fs[f] : std::string();
        });
        n.begin(0);
        last = n.takeOutput();
    }
    std::string type(const std::string& s) {
        n.input((const uint8_t*)s.data(), s.size(), 1000);
        last = n.takeOutput();
        return last;
    }
    int preLoads() const {
        int c = 0;
        for (auto& l : loads)
            if (l.first.rfind("PRELOGON", 0) == 0) c += l.second;
        return c;
    }
};

// the WFC refresh as it was before the emit path (b812314): a copy of the art with ESC[K before every CR LF, then expanded
std::string oldWfcRefresh(std::string art, const WfcSnapshot& s) {
    for (size_t i = art.find("\r\n"); i != std::string::npos; i = art.find("\r\n", i + 5)) art.insert(i, "\x1b[K");
    std::string out = "\x1b[0m\x1b[H";
    expandArt(art, AcsUser{}, wfcValues(s), [&out](const char* d, size_t n) { out.append(d, n); });
    out += "\x1b[0m";
    out += "\x1b[J";
    return out;
}
// the same refresh through a Node: the shared art (setWfcArt), drawn once, then a changed snapshot 5 s on
std::string nodeWfcRefresh(const std::string& art, WfcSnapshot& changed) {
    Node        n(1, info, BbsConfig());
    auto        shared = std::make_shared<const std::string>(art);
    WfcSnapshot cur;
    cur.bbsName = "AXIS";
    cur.sysop = "Rev";
    cur.callsToday = 3;
    n.setWfcArt([shared] { return shared; });
    n.setWfcSnapshot([&cur] { return cur; });
    n.openWfc(0);
    n.takeOutput();
    cur.callsToday = 4;
    changed = cur;
    n.poll(5000);
    return n.takeOutput();
}

std::string tempPath(const char* name) { return (fs::temp_directory_path() / name).string(); }
void        saveFile(const std::string& path, const std::string& bytes) {
    std::ofstream o(path, std::ios::binary | std::ios::trunc);
    o.write(bytes.data(), (std::streamsize)bytes.size());
}
std::string patternFile(size_t n) {
    std::string s(n, '\0');
    for (size_t i = 0; i < n; i++) s[i] = (char)('A' + (i * 7 + i / 251) % 26);
    return s;
}
// A stand-in copy of the main's "T name offset want" handler in main.cpp (that handler and themeArtPieceAppend are tied to the
// SD card and the settings, so they are not host-built): want 0 or over 7000 is 7000, no further than the cap, then the real
// readFileAppend of that piece. Only the follower side (assembleArt) and readFileAppend are the board's code here: if main's
// clamp drifts from this copy, this test does not catch it. requests counts the asks.
std::string mainTAnswer(const std::string& path, size_t off, size_t want, size_t cap, int& requests) {
    requests++;
    constexpr size_t kChunk = 7000;
    if (off > cap) return "0";
    size_t      w = want ? want : kChunk;
    std::string r = "1";
    if (w > kChunk) w = kChunk;
    if (off >= cap) return r;
    if (w > cap - off) w = cap - off;
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return r;
    readFileAppend(f, w, off, r);
    std::fclose(f);
    return r;
}
std::string followerFetch(const std::string& path, size_t cap, bool* answered, int& requests) {
    constexpr size_t kFollowerWant = 4096;
    return assembleArt(cap, kFollowerWant, true,
                       [&](size_t off, std::string& reply) {
                           reply = mainTAnswer(path, off, kFollowerWant, cap, requests);
                           return true;
                       },
                       answered);
}
}  // namespace

// ---- heap window and @STATUS

TEST_CASE("heap window keeps the lowest free and largest separately") {
    HeapWindow w;
    w.sample(100, 80, "a");
    w.sample(90, 85, "b");
    w.sample(95, 70, "c");
    CHECK(w.lowFree == 90);
    CHECK(w.lowLargest == 70);
    CHECK(w.lowDoing == "b");
    w.reset();
    CHECK(w.lowFree == UINT32_MAX);
    CHECK(w.lowLargest == UINT32_MAX);
    CHECK(w.lowDoing.empty());
}

TEST_CASE("diag fields add the window and stacks") {
    DiagSample s;
    s.winFree = 61440;
    s.winLargest = 30000;
    s.winDoing = "door LORD by x";
    s.stkWeb = 700;
    s.stkMdns = 500;
    s.stkTimer = 300;
    std::string f = diagFields(s);
    CHECK(f.find(" wmin=61440") != std::string::npos);
    CHECK(f.find(" wlarge=30000") != std::string::npos);
    CHECK(f.find(" wdoing=door_LORD_by_x") != std::string::npos);
    CHECK(f.find(" stk_web=700") != std::string::npos);
    CHECK(f.find(" stk_mdns=500") != std::string::npos);
    CHECK(f.find(" stk_timer=300") != std::string::npos);
}

// ---- Node output through the 2 KB sink

TEST_CASE("node with a sink sends identical bytes") {
    const std::string art = numberedArt(11 * 1024);
    FrontRig          sink, plainRig;
    sink.fs["PRELOGON1.ANS"] = plainRig.fs["PRELOGON1.ANS"] = art;
    std::string       direct;
    size_t            biggestPiece = 0;
    sink.n.setDirect([&](const char* p, size_t len) {
        direct.append(p, len);
        if (len > biggestPiece) biggestPiece = len;
    });
    const size_t outMaxBefore = Node::outMax();

    sink.start();
    std::string viaSink = direct + sink.last;
    direct.clear();
    sink.type("j");
    viaSink += direct + sink.last;
    const size_t restCapacity = sink.last.capacity();  // takeOutput hands over the node's own buffer
    const size_t outMaxAfterSink = Node::outMax();

    plainRig.start();
    std::string whole = plainRig.last;
    plainRig.type("j");
    whole += plainRig.last;

    REQUIRE(whole.find(art) != std::string::npos);  // the art was drawn
    CHECK(viaSink == whole);                         // the same screens, in the same order
    CHECK(biggestPiece > 0);
    CHECK(biggestPiece <= 2 * Node::kOutSpill);      // it left in pieces, never the whole 11 KB at once
    CHECK(sink.last.size() < Node::kOutSpill);       // what the node still held is under one piece
    CHECK(restCapacity <= 2 * Node::kOutSpill);      // and its buffer never grew past two
    // outMax() is the board's high-water mark for the whole run (other tests feed it too): only meaningful when it was low
    if (outMaxBefore <= 2 * Node::kOutSpill) CHECK(outMaxAfterSink <= 2 * Node::kOutSpill);
}

TEST_CASE("node without a sink keeps all output for takeOutput") {
    const std::string art = numberedArt(11 * 1024);
    FrontRig          r;
    r.fs["PRELOGON1.ANS"] = art;
    r.start();
    std::string out = r.type("j");
    CHECK(out.find(art) != std::string::npos);  // all of it, in order, nothing spilled anywhere
    CHECK(r.n.state() == Node::State::PreLogon);
    CHECK(r.n.takeOutput().empty());  // and taken once
}

TEST_CASE("spill: a write that throws is never sent again") {
    FrontRig    r;
    r.fs["PRELOGON1.ANS"] = numberedArt(11 * 1024);  // drawn through paintScreen: out_ spills to the sink in pieces
    std::string failed, later;
    bool        armed = false, thrown = false;
    r.n.setDirect([&](const char* p, size_t len) {
        if (armed && !thrown) {  // the first spilled piece of the pre-logon art fails (bad_alloc in the socket write)
            thrown = true;
            failed.assign(p, len);
            throw std::bad_alloc();
        }
        later.append(p, len);
    });
    r.start();
    later.clear();
    armed = true;
    bool threw = false;
    try {
        r.type("j");
    } catch (const std::bad_alloc&) {
        threw = true;
    }
    REQUIRE(threw);
    REQUIRE(failed.size() >= Node::kOutSpill);
    REQUIRE(failed.find("line 00000") != std::string::npos);  // it was the art's first piece
    // the piece that failed is gone from the node, its tail still in expandArt's buffer too: nothing is held to send again
    std::string next = r.n.takeOutput();
    CHECK(next.empty());
    r.n.input((const uint8_t*)" ", 1, 1000);  // the call goes on: the next screen
    next += later + r.n.takeOutput();
    // none of the failed piece goes out again: not one of its numbered lines (the last may be cut, so whole ones only)
    for (size_t at = failed.find("line "); at != std::string::npos; at = failed.find("line ", at + 1)) {
        std::string tag = failed.substr(at, 10);  // "line 00012"
        if (tag.size() < 10) break;
        CAPTURE(tag);
        CHECK(next.find(tag) == std::string::npos);
    }
}

// ---- the front door: LOGIN.ANS fields and pre-logon probes

TEST_CASE("login cells cached") {
    // art unique to this test (the cache is the board's, shared by every node): |{P} lands at row 10, column 20
    const std::string login = "\x1b[1;1Hmemory test login cells 7f3a\x1b[10;14HUser: |{P}\x1b[11;14HPass: |{O}";
    const uint32_t    before = Node::loginScans();
    for (int call = 0; call < 2; call++) {
        FrontRig r;
        r.fs["LOGIN.ANS"] = login;
        r.start();
        r.type("j");
        std::string screen = r.type(" ");
        REQUIRE(r.n.state() == Node::State::Login);
        CHECK(screen.find("|{P}") == std::string::npos);
        CHECK(screen.find("\x1b[10;20H") != std::string::npos);  // the handle goes where |{P} was
        CHECK(Node::loginScans() == before + 1);                 // the first call scans; the second is a hit
        r.type("Rev\r");
        CHECK(r.last.find("\x1b[11;20H") != std::string::npos);  // and the password where |{O} was
    }
}

TEST_CASE("prelogon uses exists and reads once") {
    FrontRig                   r;
    std::map<std::string, int> asked;
    r.fs.erase("PRELOGON1.ANS");
    r.fs["PRELOGON2.ANS"] = "PRE TWO ART";
    r.n.setExists([&](const std::string& f) {
        asked[f]++;
        return f == "PRELOGON2.ANS";
    });
    r.start();
    std::string out = r.type("j");
    CHECK(out.find("PRE TWO ART") != std::string::npos);
    CHECK(r.n.state() == Node::State::PreLogon);
    CHECK(r.preLoads() == 1);  // only the chosen screen is read
    CHECK(r.loads["PRELOGON2.ANS"] == 1);
    for (int i = 1; i <= 9; i++) CHECK(asked["PRELOGON" + std::to_string(i) + ".ANS"] == 1);  // every one asked, none read
}

TEST_CASE("prelogon: a chosen screen that reads empty goes to the login screen with no CLS of its own") {
    FrontRig r;
    r.fs.erase("PRELOGON1.ANS");  // exists says PRELOGON3.ANS is there, but it reads empty
    r.n.setExists([](const std::string& f) { return f == "PRELOGON3.ANS"; });
    r.start();
    std::string out = r.type("j");
    CHECK(r.preLoads() == 1);
    CHECK(out.find("LOGIN ART") != std::string::npos);
    CHECK(count(out, "\x1b[2J") == 1);  // the login screen's own clear only
    CHECK(out.find("(press a key)") == std::string::npos);
    CHECK(r.n.state() == Node::State::Login);
}

// ---- the board's one ZIP window

TEST_CASE("inflate lease refuses a second owner and frees on give") {
    REQUIRE_FALSE(inflate::busy());
    int      uploadCheck = 0, viewer = 0;
    uint8_t *w1 = nullptr, *s1 = nullptr, *w2 = nullptr, *s2 = nullptr;
    REQUIRE(inflate::take(&uploadCheck, w1, s1));
    CHECK(w1 != nullptr);
    CHECK(s1 != nullptr);
    CHECK(inflate::busy());
    CHECK_FALSE(inflate::take(&viewer, w2, s2));  // a second owner is refused
    CHECK(w2 == nullptr);
    uint8_t *again = nullptr, *againState = nullptr;
    CHECK(inflate::take(&uploadCheck, again, againState));  // the holder asking again gets the same window
    CHECK(again == w1);
    CHECK(againState == s1);
    inflate::give(&viewer);  // a give by anyone else does nothing
    CHECK(inflate::busy());
    inflate::give(&uploadCheck);
    CHECK_FALSE(inflate::busy());
    CHECK(inflate::take(&viewer, w2, s2));  // free again: the next owner gets it
    inflate::give(&viewer);
    CHECK_FALSE(inflate::busy());
}

namespace {
struct PackLibrary : FileLibrary {
    std::map<std::string, std::string> files;
    std::vector<FileEntry>             list(const std::string&) override {
        std::vector<FileEntry> v;
        for (auto& f : files) v.push_back({f.first, (uint32_t)f.second.size(), f.first == "AXISTEST.ZIP" ? "Test pack" : ""});
        return v;
    }
    bool read(const std::string&, const std::string& name, std::string& out, size_t max) override {
        auto it = files.find(name);
        if (it == files.end()) return false;
        out = it->second.substr(0, max);
        return true;
    }
};
}  // namespace

TEST_CASE("viewer shows the list with the note when the window is lent") {
    REQUIRE_FALSE(inflate::busy());
    PackLibrary lib;
    {
        std::ifstream in(std::string(AXIS_TEST_DATA) + "/pack.zip", std::ios::binary);
        lib.files["AXISTEST.ZIP"] = std::string((std::istreambuf_iterator<char>(in)), {});
        lib.files["FILES.BBS"] = "AXISTEST.ZIP Test pack\r\n";
    }
    axis::door::ScriptStream line;
    AnsiViewer               v{lib, "ansi", "ANSI Packs", nullptr, 0, line};  // no block of its own: it borrows the board's window
    auto                     run = [&](const std::string& keys) {
        line.push(keys);
        for (int i = 0; i < 20 && v.step(); i++) {
        }
        std::string out = line.output();
        line.clearOutput();
        return out;
    };
    int      uploadCheck = 0;
    uint8_t *w = nullptr, *s = nullptr;
    REQUIRE(inflate::take(&uploadCheck, w, s));  // an upload's ZIP check holds the window
    v.begin();
    run("1\r");                       // the pack
    std::string out = run("1\r");     // REV-AXIS.ANS, a deflated piece
    CHECK(out.find("Another caller is viewing a pack right now. Here is the file list.") != std::string::npos);
    CHECK(out.find("REV-AXIS.ANS") != std::string::npos);  // the list is on screen under the note
    uint8_t *w2 = nullptr, *s2 = nullptr;
    CHECK(inflate::take(&uploadCheck, w2, s2));  // the check is undisturbed: still its window
    CHECK(w2 == w);
    inflate::give(&uploadCheck);
    out = run("1\r");  // free again: the piece plays
    out += run("n");
    CHECK(out.find("line 40") != std::string::npos);
    run("q");
    run("q");
    run("q");
}

TEST_CASE("viewer limit: no room refuses the piece; room lost mid-piece stops it; both give the window back") {
    REQUIRE_FALSE(inflate::busy());
    PackLibrary lib;
    {
        std::ifstream in(std::string(AXIS_TEST_DATA) + "/pack.zip", std::ios::binary);
        lib.files["AXISTEST.ZIP"] = std::string((std::istreambuf_iterator<char>(in)), {});
        lib.files["FILES.BBS"] = "AXISTEST.ZIP Test pack\r\n";
    }
    axis::door::ScriptStream line;
    AnsiViewer               v{lib, "ansi", "ANSI Packs", nullptr, 0, line};
    bool                     room = false;
    int                      asked = 0;
    v.setRoom([&](uint32_t, uint32_t) {
        asked++;
        return room;
    });
    auto run = [&](const std::string& keys) {
        line.push(keys);
        for (int i = 0; i < 20 && v.step(); i++) {
        }
        std::string out = line.output();
        line.clearOutput();
        return out;
    };
    const char* note = "The board is short on memory for another movie right now. Try again in a minute.";
    v.begin();
    CHECK(asked == 0);  // lists never ask
    run("1\r");
    std::string out = run("1\r");  // REV-AXIS.ANS, deflated: the window is taken, the board has no room
    CHECK(asked >= 1);
    CHECK(out.find(note) != std::string::npos);
    CHECK(out.find("REV-AXIS.ANS") != std::string::npos);  // back on the list
    CHECK(out.find("line 1") == std::string::npos);        // nothing of the piece went out
    CHECK_FALSE(inflate::busy());

    room = true;
    out = run("1\r");  // room now: it plays, and stops at "more" (a 23-line page of 40) holding the window
    CHECK(out.find("line 1") != std::string::npos);
    CHECK(out.find(note) == std::string::npos);
    CHECK(inflate::busy());
    room = false;  // another caller took the memory while this one reads
    out = run("");
    CHECK(out.find(note) != std::string::npos);
    CHECK(out.find("line 40") == std::string::npos);
    CHECK_FALSE(inflate::busy());
    run("q");
    run("q");
    run("q");
}

// ---- lists: append, then trim lazily

TEST_CASE("list append then trim keeps the newest cap lines in order") {
    const size_t             cap = 4;
    std::vector<std::string> file;  // the list file, as listAppendLocal leaves it after each append
    for (int i = 1; i <= 7; i++) {
        file.push_back(std::to_string(i));
        size_t skip = listTrimSkip(file.size(), cap);
        if (skip) file.erase(file.begin(), file.begin() + (long)skip);  // the newest cap streamed to the temp file
        CHECK(file.size() <= cap + cap / 4);
    }
    CHECK(file == std::vector<std::string>{"3", "4", "5", "6", "7"});  // trimmed after the 6th, the 7th waits
    std::vector<std::string> seen(file.begin() + (long)listReadSkip(file.size(), cap), file.end());
    CHECK(seen == std::vector<std::string>{"4", "5", "6", "7"});  // a reader sees the newest cap, in order
    CHECK(listTrimSkip(5, 4) == 0);
    CHECK(listTrimSkip(6, 4) == 2);
    CHECK(listTrimSkip(9, 0) == 0);  // cap 0: never
    CHECK(listReadSkip(3, 4) == 0);
    CHECK(listReadSkip(9, 0) == 0);
}

// ---- AnsiTerm sized to its screen

TEST_CASE("ansiterm 45x24 holds only its cells") {
    CHECK(sizeof(AnsiTerm) < 45 * 24 * sizeof(AnsiTerm::Cell));  // the cells are not a fixed 80x30 block inside it
    AnsiTerm t(45, 24);
    CHECK(t.cols() == 45);
    CHECK(t.rows() == 24);
    t.feed("\x1b[24;45HX");
    CHECK(t.cell(23, 44).ch == 'X');
    CHECK(t.cell(0, 0).ch == ' ');
    t.feed("\x1b[1;1HY");
    CHECK(t.cell(0, 0).ch == 'Y');
    CHECK(t.cell(23, 44).ch == 'X');
}

// ---- settings shared by every node

TEST_CASE("node shares the settings") {
    BbsConfig c;
    c.bbsName = "Shared Board";
    auto cfg = std::make_shared<const BbsConfig>(c);
    Node a(1, info, cfg), b(2, info, cfg);
    CHECK(cfg.use_count() == 3);  // one copy for both
    for (Node* n : {&a, &b}) {
        n->setArt([](const std::string&) { return std::string("|BN"); });
        n->openWfc(0);
        CHECK(n->takeOutput().find("Shared Board") != std::string::npos);
    }
    BbsConfig nc = c;
    nc.filesOn = false;  // a section switch: callers already on follow it
    auto next = std::make_shared<const BbsConfig>(nc);
    REQUIRE(Node::onlySwitches(*cfg, *next));
    a.applyBoard(next, cfg, true);
    b.applyBoard(next, cfg, true);
    a.poll(100);
    b.poll(100);
    CHECK(cfg.use_count() == 1);   // the old copy is let go
    CHECK(next.use_count() == 3);  // and both share the new one
}

TEST_CASE("onlySwitches: true for each switch field alone, false for any other field") {
    const BbsConfig base;
    CHECK(Node::onlySwitches(base, base));
    using Change = void (*)(BbsConfig&);
    const Change switches[] = {
        [](BbsConfig& c) { c.modemLink = !c.modemLink; },
        [](BbsConfig& c) { c.liteBelowBps += 1; },
        [](BbsConfig& c) { c.messagesOn = !c.messagesOn; },
        [](BbsConfig& c) { c.filesOn = !c.filesOn; },
        [](BbsConfig& c) { c.doorsOn = !c.doorsOn; },
        [](BbsConfig& c) { c.doors[0].enabled = !c.doors[0].enabled; },
    };
    for (Change ch : switches) {
        BbsConfig b = base;
        ch(b);
        CHECK(Node::onlySwitches(base, b));
    }
    const Change others[] = {
        [](BbsConfig& c) { c.sysopHandle += "x"; },
        [](BbsConfig& c) { c.sysopSalt += "x"; },
        [](BbsConfig& c) { c.sysopHash += "x"; },
        [](BbsConfig& c) { c.sysopRealName += "x"; },
        [](BbsConfig& c) { c.sysopLocation += "x"; },
        [](BbsConfig& c) { c.sysopEmail += "x"; },
        [](BbsConfig& c) { c.sysopTagline += "x"; },
        [](BbsConfig& c) { c.dosSalt += "x"; },
        [](BbsConfig& c) { c.dosHash += "x"; },
        [](BbsConfig& c) { c.nupSalt += "x"; },
        [](BbsConfig& c) { c.nupHash += "x"; },
        [](BbsConfig& c) { c.bbsName += "x"; },
        [](BbsConfig& c) { c.location += "x"; },
        [](BbsConfig& c) { c.boardName += "x"; },
        [](BbsConfig& c) { c.publicHost += "x"; },
        [](BbsConfig& c) { c.telnetPort += 1; },
        [](BbsConfig& c) { c.timezone += "x"; },
        [](BbsConfig& c) { c.ftnAddress += "x"; },
        [](BbsConfig& c) { c.newUserSecurity += 1; },
        [](BbsConfig& c) { c.dailyMinutes += 1; },
        [](BbsConfig& c) { c.idleMinutes += 1; },
        [](BbsConfig& c) { c.sessions += 1; },
        [](BbsConfig& c) { c.nuvYes += 1; },
        [](BbsConfig& c) { c.nuvNo += 1; },
        [](BbsConfig& c) { c.nuvLevel += 1; },
        [](BbsConfig& c) { c.ratio += 1; },
        [](BbsConfig& c) { c.freeKb += 1; },
        [](BbsConfig& c) { c.theme += "x"; },
        [](BbsConfig& c) { c.wfc += "x"; },
        [](BbsConfig& c) { c.loginSplash = !c.loginSplash; },
        [](BbsConfig& c) { c.sysopTelnet = !c.sysopTelnet; },
        [](BbsConfig& c) { c.setupDone = !c.setupDone; },
        [](BbsConfig& c) { c.unknown.push_back({"k", "v"}); },
        [](BbsConfig& c) { c.areas[0].name += "x"; },
        [](BbsConfig& c) { c.areas[0].minSecurity += 1; },
        [](BbsConfig& c) { c.areas[0].mirror = !c.areas[0].mirror; },
        [](BbsConfig& c) { c.areas.pop_back(); },
        [](BbsConfig& c) { c.msgAreas[0].id += "x"; },
        [](BbsConfig& c) { c.msgAreas[0].name += "x"; },
        [](BbsConfig& c) { c.mainMenu.push_back({"X", "Extra", "messages"}); },
    };
    int i = 0;
    for (Change ch : others) {
        BbsConfig b = base;
        ch(b);
        CAPTURE(i);
        CHECK_FALSE(Node::onlySwitches(base, b));
        i++;
    }
    BbsConfig withMenu = base, menuChanged;  // one menu item field
    withMenu.mainMenu = {{"M", "Messages", "messages"}};
    menuChanged = withMenu;
    menuChanged.mainMenu[0].cmd = "files";
    CHECK_FALSE(Node::onlySwitches(withMenu, menuChanged));
    menuChanged = withMenu;
    menuChanged.mainMenu[0].name = "Mail";
    CHECK_FALSE(Node::onlySwitches(withMenu, menuChanged));
}

TEST_CASE("applyBoard: a node on the old copy adopts the new one; a node of its own patches the six fields") {
    BbsConfig c;
    c.bbsName = "Board One";
    c.mainMenu = {{"M", "Messages", "messages"}};
    auto prev = std::make_shared<const BbsConfig>(c);

    SUBCASE("only switches changed and the node is on prev: it shares next") {
        Node      n(1, info, prev);
        BbsConfig nc = c;
        nc.doors.pop_back();  // a door went away
        nc.filesOn = false;
        auto next = std::make_shared<const BbsConfig>(nc);
        n.applyBoard(next, prev, Node::onlySwitches(*prev, *next));
        n.poll(100);
        CHECK(next.use_count() == 2);  // the same pointer, not a copy
        CHECK(prev.use_count() == 1);
        CHECK(n.doorChoices().size() == next->doors.size());
    }
    SUBCASE("more than switches changed: the node keeps its name and menu, takes the switches and doors") {
        Node      n(1, info, prev);
        BbsConfig nc = c;
        nc.bbsName = "Board Two";
        nc.mainMenu = {{"F", "Files", "files"}};
        nc.doors.pop_back();
        nc.filesOn = false;
        auto next = std::make_shared<const BbsConfig>(nc);
        n.applyBoard(next, prev, Node::onlySwitches(*prev, *next));
        n.poll(100);
        CHECK(next.use_count() == 1);  // not shared: the caller keeps the board they called into
        CHECK(n.doorChoices().size() == next->doors.size());
        CHECK(n.editedMenu().find("Messages") != std::string::npos);
        CHECK(n.editedMenu().find("Files") == std::string::npos);
        n.setArt([](const std::string&) { return std::string("|BN"); });
        n.openWfc(0);
        CHECK(n.takeOutput().find("Board One") != std::string::npos);
    }
    SUBCASE("a node with its own copy patches it even when only switches changed") {
        auto      mine = std::make_shared<const BbsConfig>(c);  // diverged: not main's prev
        Node      n(1, info, mine);
        BbsConfig nc = c;
        nc.doors.pop_back();
        auto next = std::make_shared<const BbsConfig>(nc);
        n.applyBoard(next, prev, true);
        n.poll(100);
        CHECK(next.use_count() == 1);
        CHECK(n.doorChoices().size() == next->doors.size());
        CHECK(n.editedMenu().find("Messages") != std::string::npos);
    }
}

TEST_CASE("applyBoard: a new user application keeps the copy it reads through a push") {
    UserStore users;
    BbsConfig c;
    auto      prev = std::make_shared<const BbsConfig>(c);
    std::weak_ptr<const BbsConfig> watch = prev;
    Node                           n(1, info, prev);
    n.setAccounts(&users, [] {}, rng, [] { return 1800000000u; });
    n.begin(0);
    n.takeOutput();
    std::string keys = "Chip Breaker\ry";
    n.input((const uint8_t*)keys.data(), keys.size(), 1000);
    REQUIRE(n.state() == Node::State::Applying);
    n.takeOutput();
    BbsConfig nc = c;
    nc.filesOn = false;
    auto next = std::make_shared<const BbsConfig>(nc);
    n.applyBoard(next, prev, true);
    prev.reset();  // main lets go of it too
    n.poll(1100);
    CHECK_FALSE(watch.expired());  // the application still holds the copy it was built on
    std::string answer = "Chip\r";
    n.input((const uint8_t*)answer.data(), answer.size(), 1200);
    CHECK(n.state() == Node::State::Applying);
    CHECK_FALSE(n.takeOutput().empty());
}

// ---- web gate notes

TEST_CASE("web gate logs once an hour while gated") {
    uint32_t last = 0;
    bool     logged = false;
    CHECK(gateLogDue(0, last, logged));  // the first note goes at once
    last = 0, logged = true;             // logged at 0
    CHECK_FALSE(gateLogDue(30u * 60000u, last, logged));
    CHECK_FALSE(gateLogDue(60u * 60000u, last, logged));
    CHECK(gateLogDue(61u * 60000u, last, logged));
    // a check with nothing new logs nothing and does not stamp: the next new note still goes at once
    CHECK(gateLogDue(62u * 60000u, last, logged));
    last = 62u * 60000u;  // logged then
    CHECK_FALSE(gateLogDue(62u * 60000u + 59u * 60000u, last, logged));
    CHECK(gateLogDue(0xFFFFFFF0u + 3600001u, 0xFFFFFFF0u, true));  // across the millis() wrap
}

TEST_CASE("web task: checks keep today's timing, and it runs only while there is work") {
    CHECK_FALSE(webCheckDue(60000u, true, true, 0));  // nothing in the first minute, not even @DDNS now
    CHECK(webCheckDue(60001u, true, false, 0));      // then the first check
    CHECK_FALSE(webCheckDue(60001u + 600000u, false, false, 60001u));
    CHECK(webCheckDue(60002u + 600000u, false, false, 60001u));  // every ten minutes after
    CHECK(webCheckDue(70000u, false, true, 65000u));             // @DDNS now
    CHECK(webCheckDue(0xFFFFF000u + 700001u, false, false, 0xFFFFF000u));  // across the millis() wrap
    CHECK_FALSE(webHasWork(false, false, 0));  // idle: no task
    CHECK(webHasWork(false, true, 0));
    CHECK(webHasWork(false, false, 1));
    CHECK_FALSE(webHasWork(true, true, 3));  // a door holds the heap: the work waits
}

// ---- WFC refresh ESC[K

TEST_CASE("wfc refresh: ESC[K through the emit path matches the old copy and insert") {
    SUBCASE("CR LF on and around a 480-byte piece boundary, a last line with no CR LF") {
        for (size_t pad = 470; pad <= 490; pad++) {
            std::string art = std::string(pad, 'a') + "\r\nname |BN calls |CT\r\n" + std::string(500, 'b') + "\r\nlast line, no CR LF";
            WfcSnapshot s;
            std::string got = nodeWfcRefresh(art, s);
            CAPTURE(pad);
            CHECK(got == oldWfcRefresh(art, s));
        }
    }
    SUBCASE("CR CR LF") {
        std::string art = "one\r\r\ntwo |BN\r\r\n\r\nthree";
        WfcSnapshot s;
        std::string got = nodeWfcRefresh(art, s);
        CHECK(got == oldWfcRefresh(art, s));
    }
    SUBCASE("a false {{ACS}} block across lines") {
        std::string art = "top |BN\r\n{{s200}}hidden one\r\nhidden two\r\n{{/}}after\r\nend";
        WfcSnapshot s;
        std::string got = nodeWfcRefresh(art, s);
        CHECK(got == oldWfcRefresh(art, s));
        CHECK(got.find("hidden") == std::string::npos);
    }
    SUBCASE("art with |CR takes the copy and still matches") {
        std::string art = "top|CRmiddle |BN\r\nend";
        WfcSnapshot s;
        std::string got = nodeWfcRefresh(art, s);
        CHECK(got == oldWfcRefresh(art, s));
    }
}

// ---- follower theme art: T pieces and the cache

TEST_CASE("follower T assembly: 8192 bytes, the 12288 cap, and 0x1a at 4096") {
    const size_t cap = 12288;
    int          requests = 0;
    bool         answered = false;
    const std::string p = tempPath("axis_memtest_t.ans");

    std::string f8 = patternFile(8192);
    saveFile(p, f8);
    CHECK(followerFetch(p, cap, &answered, requests) == f8);
    CHECK(answered);
    CHECK(requests == 3);  // 4096, 4096, then the empty end

    std::string f12 = patternFile(12288);
    saveFile(p, f12);
    requests = 0;
    CHECK(followerFetch(p, cap, &answered, requests) == f12);
    CHECK(requests == 3);  // the cap ends it: no fourth ask

    std::string big = patternFile(13000);
    saveFile(p, big);
    requests = 0;
    CHECK(followerFetch(p, cap, &answered, requests) == big.substr(0, cap));

    std::string sauce = patternFile(8192);
    sauce[4096] = '\x1a';
    saveFile(p, sauce);
    requests = 0;
    CHECK(followerFetch(p, cap, &answered, requests) == sauce.substr(0, 4096));
    CHECK(answered);

    std::string small = patternFile(100);
    saveFile(p, small);
    requests = 0;
    CHECK(followerFetch(p, cap, &answered, requests) == small);
    CHECK(requests == 1);

    bool        partial = true;
    std::string none = assembleArt(cap, 4096, true, [](size_t, std::string&) { return false; }, &partial);
    CHECK(none.empty());
    CHECK_FALSE(partial);  // an unanswered ask is not a real "none"
    std::remove(p.c_str());
}

TEST_CASE("art cache: bytes always the sum, an oversized entry is not kept and drops the stale one") {
    ArtCache c(16384);
    auto     sum = [&c] {
        size_t s = 0;
        for (auto& e : c.v) s += e.second.size();
        return s;
    };
    c.put("A.ANS", std::string(6000, 'a'));
    c.put("B.ANS", std::string(6000, 'b'));
    CHECK(c.bytes == sum());
    CHECK(c.find("A.ANS") != nullptr);  // A is now the newest
    c.put("C.ANS", std::string(6000, 'c'));  // over 16 KB: the least recently used (B) goes
    CHECK(c.bytes == sum());
    CHECK(c.bytes <= 16384);
    CHECK(c.find("B.ANS") == nullptr);
    CHECK(c.find("A.ANS") != nullptr);
    c.put("A.ANS", std::string(100, 'A'));  // replaced in place
    CHECK(c.bytes == sum());
    CHECK(*c.find("A.ANS") == std::string(100, 'A'));
    c.put("C.ANS", std::string(20000, 'C'));  // too big to keep: and the old C must not stay behind
    CHECK(c.find("C.ANS") == nullptr);
    CHECK(c.bytes == sum());
    CHECK(c.bytes == 100);
    c.clear();
    CHECK(c.bytes == 0);
    CHECK(c.v.empty());
}
