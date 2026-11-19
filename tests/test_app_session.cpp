// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The shared caller flow (app/session.h) that the board and the PC both run over one Services: the board data files
// round-trip, ten callers get their own nodes and see each other, a stop saves users and stats, and a portable @ command
// answers the same whichever program asks. The caller lines are in-memory (a net::TelnetStream over a door::ScriptStream),
// so no socket and no real timing are needed; the clock is injected.
#include <ctime>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "doctest.h"
#include "app/boarddata.h"
#include "app/commands.h"
#include "app/services.h"
#include "app/session.h"
#include "app/stats.h"
#include "app/themeprovider.h"
#include "bbs/config.h"
#include "bbs/node.h"
#include "bbs/password.h"
#include "bbs/theme.h"
#include "bbs/users.h"
#include "door/bytestream.h"
#include "net/telnetstream.h"

using namespace axis;
namespace fs = std::filesystem;

namespace {

uint32_t testRng() { return 0x5A5A5A5Au; }

// A caller line with no socket: its raw bytes come from and go to a door::ScriptStream the test keeps (the keys a caller
// typed are pushed into it; what the caller is sent lands in its output). The clock is the fixture's, by reference.
struct ScriptLine : net::TelnetStream {
    door::ScriptStream& ss;
    uint32_t&           clock;
    ScriptLine(door::ScriptStream& s, uint32_t& c) : ss(s), clock(c) {}
    int rawRead(uint8_t* buf, size_t n) override {
        size_t k = 0;
        while (k < n && ss.available() > 0) {
            int c = ss.read();
            if (c < 0) break;
            buf[k++] = (uint8_t)c;
        }
        return (int)k;
    }
    int rawWrite(const uint8_t* p, size_t n) override {
        if (!ss.carrier()) return -1;
        ss.write(p, n);
        return (int)n;
    }
    bool     rawConnected() override { return ss.carrier(); }
    void     rawClose() override { ss.hangUp(); }
    uint32_t nowMs() override { return clock; }
};

// A host with everything the shared code reads: a fixed clock, random numbers, an event list, and temp storage folders.
struct TestHost : app::Host {
    uint32_t                 ms = 1000, unixSecs = 1780000000u;  // a set clock (2026-05-28), so the day counters roll
    std::string              sd, flash;
    std::vector<std::string> events;
    uint32_t                 nowMs() override { return ms; }
    uint32_t                 unixNow() override { return unixSecs; }
    uint32_t                 random32() override { return 0x12345678u; }
    void                     event(const std::string& l) override { events.push_back(l); }
    std::string              sdRoot() override { return sd; }
    bool                     hasSd() override { return false; }
    uint64_t                 sdFreeBytes() override { return 0; }
    std::string              flashRoot() override { return flash; }
};

// A temp folder of its own, removed afterwards.
struct TempDir {
    fs::path p;
    TempDir() {
        static int n = 0;
        p = fs::temp_directory_path() / ("axis_app_" + std::to_string((unsigned long long)::time(nullptr)) + "_" + std::to_string(n++));
        fs::remove_all(p);
        fs::create_directories(p);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(p, ec);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
};

bbs::User validated(const std::string& handle, int security = 20, uint32_t calls = 0) {
    bbs::User u;
    u.handle = handle;
    u.salt = bbs::makeSalt(testRng);
    u.hash = bbs::hashPassword(u.salt, "pw");
    u.status = bbs::User::Status::Validated;
    u.security = security;
    u.calls = calls;
    return u;
}

}  // namespace

TEST_CASE("board data keeps the same files and formats") {
    TempDir  dir;
    TestHost host;
    host.flash = (dir.p / "flash").string();
    fs::create_directories(host.flash);

    {
        app::BoardData d(host);
        d.load();  // nothing there yet
        d.addOneLiner("Rev: this thing slaps!");
        d.noteCaller("ChipBreaker|3|12:00|Nowhere");
        for (int i = 0; i < 25; i++) d.addOneLiner("filler " + std::to_string(i));  // the wall caps at the newest 20
    }
    CHECK(fs::exists(fs::path(host.flash) / "oneliners.txt"));
    CHECK(fs::exists(fs::path(host.flash) / "callers.txt"));

    // A fresh BoardData loads the same files: same text, same cap.
    app::BoardData back(host);
    back.load();
    CHECK(back.oneLiners().size() == 20);               // the cap held
    CHECK(back.oneLiners().back() == "filler 24");      // the newest kept
    REQUIRE(back.lastCallers().size() == 1);
    CHECK(back.lastCallers()[0] == "ChipBreaker|3|12:00|Nowhere");  // exact text, case and all

    // A one-line round-trip with no filler, to pin the exact text.
    TempDir  dir2;
    TestHost h2;
    h2.flash = (dir2.p / "flash").string();
    fs::create_directories(h2.flash);
    app::BoardData one(h2);
    one.load();
    one.addOneLiner("Kid: WARez 4 LiFE");
    app::BoardData one2(h2);
    one2.load();
    REQUIRE(one2.oneLiners().size() == 1);
    CHECK(one2.oneLiners()[0] == "Kid: WARez 4 LiFE");
}

namespace {

// Ten callers on one Services: in-memory lines, one BoardData/Stats/UserStore, the who and node-link hooks the PC wires.
struct Floor {
    TempDir                               dir;
    TestHost                              host;
    std::shared_ptr<bbs::BbsConfig>       cfg = std::make_shared<bbs::BbsConfig>();
    app::BoardData                        data{host};
    app::ThemeProvider                    theme{host};
    app::Stats                            stats{host};
    bbs::UserStore                        users;
    std::shared_ptr<const bbs::BbsConfig> cfgc = cfg;  // the const view Services holds a reference to
    app::Services                         svc{host, data, theme, stats, users, cfgc};
    std::vector<std::unique_ptr<door::ScriptStream>> streams;
    std::vector<std::unique_ptr<app::Session>>       sessions;

    Floor() {
        host.sd = (dir.p / "sd").string();
        host.flash = (dir.p / "flash").string();
        fs::create_directories(host.sd);
        fs::create_directories(host.flash);
        cfg->setupDone = true;  // callers get a Node, not the setup gate
        theme.config = [this]() -> const bbs::BbsConfig& { return *cfg; };
        svc.saveUsers = [] {};
        svc.rng = testRng;
        svc.sysInfo = [] { return bbs::SysInfo{}; };
        svc.nodeNumber = [](int slot) { return slot + 1; };
        svc.who = [this] {
            std::vector<bbs::Node::WhoLine> v;
            for (size_t i = 0; i < sessions.size(); i++) {
                app::Session* s = sessions[i].get();
                if (!s || !s->node) continue;
                v.push_back({(int)i + 1, s->node->handle(), "Online", s->node->linkShown()});
            }
            return v;
        };
        // A note, broadcast or split-chat key from one node reaches the others (the board's deliverChat shape).
        svc.nodeLink = [this](int slot, int kind, int to, const std::string& handle, const std::string& text) {
            for (size_t i = 0; i < sessions.size(); i++) {
                app::Session* s = sessions[i].get();
                if (!s || !s->node || (int)i == slot) continue;
                if (to == 0 || s->node->number() == to) s->node->nodeIn(kind, slot + 1, handle, text);
            }
        };
    }
    ~Floor() {
        sessions.clear();  // (the lines close while their ScriptStreams are still here)
        bbs::theme::setArt({});  // wireNode set these process-wide; drop the dangling ThemeProvider for later tests
        bbs::theme::setArtTop({});
        bbs::theme::setListTemplate({});
    }

    app::Session& add(int slot) {
        streams.push_back(std::make_unique<door::ScriptStream>());
        auto line = std::make_unique<ScriptLine>(*streams.back(), host.ms);
        sessions.push_back(std::make_unique<app::Session>(slot, std::move(line), svc));
        return *sessions.back();
    }

    // Drive one caller from the login screen to the main menu: handle, password, then a key past the oneliners.
    void login(size_t i, const std::string& handle) {
        app::Session&       s = *sessions[i];
        door::ScriptStream& ss = *streams[i];
        ss.push(handle + "\r" + "pw\r" + "\r");
        for (int t = 0; t < 8 && s.node && s.node->state() != bbs::Node::State::Menu; t++) {
            host.ms += 100;
            s.service(host.ms);
        }
        REQUIRE(s.node);
        REQUIRE(s.node->state() == bbs::Node::State::Menu);
    }
};

}  // namespace

TEST_CASE("ten callers get nodes 1 to 10 and see each other") {
    Floor f;
    for (int i = 0; i < 10; i++) f.users.add(validated("caller" + std::to_string(i + 1)));

    for (int i = 0; i < 10; i++) {
        app::Session& s = f.add(i);
        s.open(false);
        CHECK(s.nodeNumber() == i + 1);  // slot i answers as node i + 1
        f.login((size_t)i, "caller" + std::to_string(i + 1));
    }

    // Who's online lists all ten, each its own node and handle.
    std::vector<bbs::Node::WhoLine> who = f.svc.who();
    REQUIRE(who.size() == 10);
    for (int i = 0; i < 10; i++) {
        CHECK(who[(size_t)i].node == i + 1);
        CHECK(who[(size_t)i].handle == "caller" + std::to_string(i + 1));
    }

    // A chat line aimed at node 7 reaches that caller and nobody else.
    for (auto& ss : f.streams) ss->clearOutput();
    f.svc.nodeLink(0, bbs::Node::kNodeNote, 7, "caller1", "meet me in LORD");
    for (auto& s : f.sessions) {
        f.host.ms += 10;
        s->service(f.host.ms);  // each turn flushes the node's output to its line
    }
    CHECK(f.streams[6]->output().find("meet me in LORD") != std::string::npos);  // node 7 (slot 6)
    CHECK(f.streams[0]->output().find("meet me in LORD") == std::string::npos);  // the sender
    CHECK(f.streams[5]->output().find("meet me in LORD") == std::string::npos);  // node 6, not addressed
}

TEST_CASE("a stop saves users and stats") {
    TempDir  dir;
    TestHost host;
    host.flash = (dir.p / "flash").string();
    fs::create_directories(host.flash);

    bbs::UserStore users;
    app::Stats     stats(host);
    stats.statsLoad(users);  // no file yet: the lifetime total seeds from the accounts (none, so zero) and saves

    // The PC's saveUsers: users.dat written through UserStore::formatTo, as pchost does.
    auto saveUsers = [&] {
        std::string text;
        users.formatTo([&](const std::string& piece) { text += piece; });
        app::flashWrite(host, "/users.dat", text);
    };

    users.add(validated("NightOwl"));
    saveUsers();         // the new user kept on the way down
    stats.countCall();   // a call counted: the lifetime total is now 1, not the zero seed
    stats.statsSave();

    // Reload from the same files: both are there.
    bbs::UserStore users2;
    users2.parse(app::flashRead(host, "/users.dat"));
    app::Stats stats2(host);
    stats2.statsLoad(users2);
    CHECK(users2.find("NightOwl") != nullptr);
    CHECK(stats2.dayStats.total == 1);
}

TEST_CASE("a portable @ command answers the same on the board and the PC") {
    TempDir  dir;
    TestHost host;
    host.flash = (dir.p / "flash").string();
    fs::create_directories(host.flash);

    std::shared_ptr<const bbs::BbsConfig> cfg = std::make_shared<const bbs::BbsConfig>();
    app::BoardData                        data(host);
    app::ThemeProvider                    theme(host);
    app::Stats                            stats(host);
    bbs::UserStore                        users;
    users.add(validated("Rev", 255, 42));
    users.add(validated("ChipBreaker", 20, 3));
    app::Services svc(host, data, theme, stats, users, cfg);

    struct Cap : app::Out {
        std::string s;
        void        write(const char* p, size_t n) override { s.append(p, n); }
    } out;
    std::vector<app::Session*> sessions;  // nobody on a node: @USERS does not need one

    // @USERS 0 is one paged function, so the board and the PC send the same bytes.
    bool known = app::runPortableCommand("@USERS 0", svc, sessions, out);
    CHECK(known);
    CHECK(out.s.find("user 1|Rev|valid|255|42|") != std::string::npos);
    CHECK(out.s.find("user 2|ChipBreaker|valid|20|3|") != std::string::npos);
}
