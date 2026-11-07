// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include <regex>
#include <string>

#include "doctest.h"
#include "bbs/node.h"
#include "bbs/password.h"
#include "bbs/theme.h"
#include "bbs/users.h"

using namespace axis::bbs;

namespace {
SysInfo     info() { return SysInfo{}; }
std::string plain(const std::string& s) { return std::regex_replace(s, std::regex("\x1b\\[[0-9;?]*[A-Za-z]"), ""); }
const char* kBar = "\x1b[0;1;37;44m";  // the lightbar: bright white on blue

struct O2 {
    BbsConfig   cfg;
    Node        n;
    std::string first;
    explicit O2(bool oblivion = true) : cfg(make(oblivion)), n(1, info, cfg) {
        n.begin(0);
        first = n.takeOutput();
    }
    static BbsConfig make(bool oblivion) {
        BbsConfig c;
        c.bbsName = "AXIS UNDERGROUND CNC";
        c.loginSplash = false;
        c.theme = oblivion ? "oblivion" : "axis";
        return c;
    }
    std::string type(const std::string& s) {
        n.input((const uint8_t*)s.data(), s.size(), 1000);
        return n.takeOutput();
    }
    ~O2() { theme::use(theme::Look::Axis); }
};
}  // namespace

TEST_CASE("the Oblivion look opens on a matrix: a lightbar over Login, Apply, Who and Goodbye") {
    O2 o;
    CHECK(o.n.state() == Node::State::Matrix);
    std::string p = plain(o.first);
    for (const char* item : {"Login", "Apply", "Who's Online", "Goodbye"}) CHECK(p.find(item) != std::string::npos);
    CHECK(o.first.find(kBar) != std::string::npos);
    CHECK(p.find("A X I S") != std::string::npos);  // the board's name in the title bar
}

TEST_CASE("arrow keys move the lightbar and Enter picks; hotkeys work too") {
    O2          o;
    std::string out = o.type("\x1b[B");  // down: Apply
    CHECK(out.find(kBar) != std::string::npos);
    CHECK(out.find("\x1b[") != std::string::npos);  // only the two items redrawn, by cursor position
    CHECK(out.find("\x1b[2J") == std::string::npos);
    out = plain(o.type("\r"));
    CHECK(o.n.state() == Node::State::Login);
    CHECK(out.find("application") != std::string::npos);
    O2 g;
    g.type("\x1bOA");  // up from Login wraps to Goodbye (the other arrow form)
    g.type("\r");
    CHECK(g.n.state() == Node::State::Done);
    O2 l;
    l.type("l");
    CHECK(l.n.state() == Node::State::Login);
}

TEST_CASE("after login the Oblivion main menu: a boxed two-column list and the bracket prompt") {
    O2 o;
    o.type("l");
    std::string out = plain(o.type("Rev\r"));
    CHECK(out.find("M A I N") != std::string::npos);
    CHECK(out.find("File Vaults") != std::string::npos);
    CHECK(out.find("Message Bases") != std::string::npos);
    CHECK(out.find("User Settings") != std::string::npos);
    CHECK(out.find("[Main Menu]") != std::string::npos);
    out = plain(o.type("i"));  // a command, then just the prompt again (O/2 style), ? for the menu
    CHECK(out.find("[Main Menu]") != std::string::npos);
    CHECK(out.find("File Vaults") == std::string::npos);
    CHECK(plain(o.type("?")).find("File Vaults") != std::string::npos);
}

TEST_CASE("the Axis look still opens on the drippy login screen") {
    O2 o(false);
    CHECK(o.n.state() == Node::State::Login);
    CHECK(o.first.find(kBar) == std::string::npos);
}

TEST_CASE("an Oblivion art file from the card replaces the stock screen, its pipe and MCI codes filled in") {
    BbsConfig c = O2::make(true);
    Node      n(3, info, c);
    n.setArt([](const std::string& name) { return name == "MATRIX.ANS" ? std::string("|11Welcome to %BN, node %NN|07") : std::string(); });
    n.begin(0);
    std::string out = n.takeOutput();
    CHECK(out.find("\x1b[0;1;36mWelcome to AXIS UNDERGROUND CNC, node 3\x1b[0;37m") != std::string::npos);
    CHECK(out.find(kBar) != std::string::npos);  // the lightbar still goes on top
    theme::use(theme::Look::Axis);
}

TEST_CASE("a matrix screen with O/2 pulldown marks gets the lightbar at those marks, the marks blanked") {
    BbsConfig c = O2::make(true);
    Node      n(1, info, c);
    n.setArt([](const std::string& name) {
        return name == "MATRIX.ANS" ? std::string("\x1b[2J\x1b[16;10H|01%01\x1b[16;20H|02%02\x1b[16;30H|03%03\x1b[16;40H|04%04\x1b[16;50H|05%05")
                                    : std::string();
    });
    n.begin(0);
    std::string out = n.takeOutput();
    CHECK(out.find("|01") == std::string::npos);
    CHECK(out.find("%02") == std::string::npos);
    CHECK(out.find("\x1b[16;10H\x1b[0;1;37;44m") != std::string::npos);  // Login, lit, where |01 was
    CHECK(out.find("\x1b[16;20H") != std::string::npos);                 // Apply where |02 was
    std::string mv = n.takeOutput();
    n.input((const uint8_t*)"\x1b[C", 3, 0);  // right: Apply
    mv = n.takeOutput();
    CHECK(mv.find("\x1b[16;20H\x1b[0;1;37;44m") != std::string::npos);
    theme::use(theme::Look::Axis);
}

TEST_CASE("with an O/2 MAIN.ANS on the card, the keys follow stock Oblivion/2 letters") {
    BbsConfig c = O2::make(true);
    c.doors = {{"lord", "LORD", "LORD.EXE", "", ""}};
    Node n(1, info, c);
    n.setArt([](const std::string& name) { return name == "MAIN.ANS" ? std::string("o open doors  = multinode") : std::string(); });
    n.begin(0);
    n.input((const uint8_t*)"lRev\r", 5, 0);
    n.takeOutput();
    n.input((const uint8_t*)"o", 1, 0);  // O/2: open doors
    CHECK(n.state() == Node::State::Doors);
    n.input((const uint8_t*)"\r", 1, 0);
    n.input((const uint8_t*)"=", 1, 0);  // O/2: multinode
    CHECK(n.state() == Node::State::MultiNode);
    theme::use(theme::Look::Axis);
}

TEST_CASE("every stock O/2 letter lands on the right command") {
    struct Want {
        char        key;
        Node::State state;
    } wants[] = {{'c', Node::State::Chat}, {'f', Node::State::Paging}, {'o', Node::State::Doors}, {'=', Node::State::MultiNode},
                 {'a', Node::State::Menu} /* no accounts: stays at the menu */};
    for (const Want& w : wants) {
        BbsConfig c = O2::make(true);
        c.doors = {{"lord", "LORD", "LORD.EXE", "", ""}};
        Node n(1, info, c);
        n.setArt([](const std::string& name) { return name == "MAIN.ANS" ? std::string("art") : std::string(); });
        n.setChat([](const std::string&, bool) {}, [] { return std::vector<Node::WhoLine>{}; });
        n.begin(0);
        n.input((const uint8_t*)"lRev\r", 5, 0);
        n.takeOutput();
        n.input((const uint8_t*)&w.key, 1, 0);
        CHECK_MESSAGE(n.state() == w.state, "key " << w.key);
    }
    theme::use(theme::Look::Axis);
}

TEST_CASE("Oblivion L lists callers, S draws the stats screen, and the prompt shows ?") {
    uint32_t (*rng)() = []() -> uint32_t { return 0x5A5A5A5Au; };
    UserStore users;
    auto add = [&](const char* handle, User::Status st, bool twit) {
        User u;
        u.handle = handle;
        u.location = "Myrtle Beach";
        u.tagline = "rotary chips";
        u.calls = 14;
        u.salt = makeSalt(rng);
        u.hash = hashPassword(u.salt, "pass1234");
        u.status = st;
        u.twit = twit;
        users.add(u);
    };
    add("RevJMoney", User::Status::Validated, false);
    add("Waiting", User::Status::Pending, false);
    add("TwitCaller", User::Status::Validated, true);
    BbsConfig c = O2::make(true);
    SysInfo   live;
    live.ip = "192.168.1.77";
    live.host = "axis.local";
    live.wifi = "TestNet";
    live.rssi = -40;
    live.uptimeSec = 3725;
    live.storage = "SD card 7580 MB";
    live.freeHeap = 150000;
    Node n(1, [live] { return live; }, c);
    n.setAccounts(&users, [] {}, rng, [] { return 1800000000u; });
    n.setArt([](const std::string& name) { return name == "MAIN.ANS" ? std::string("art") : std::string(); });
    n.begin(0);
    std::string login = "lRevJMoney\rpass1234\r";
    n.input((const uint8_t*)login.data(), login.size(), 1000);
    std::string menu = plain(n.takeOutput());
    CHECK(menu.find("?") != std::string::npos);
    n.input((const uint8_t*)"l", 1, 1000);
    std::string listed = plain(n.takeOutput());
    CHECK(listed.find("RevJMoney") != std::string::npos);
    CHECK(listed.find("Myrtle Beach") != std::string::npos);
    CHECK(listed.find("Waiting") == std::string::npos);
    CHECK(listed.find("TwitCaller") == std::string::npos);
    CHECK(listed.find("?") != std::string::npos);
    n.input((const uint8_t*)"w", 1, 1000);
    CHECK(plain(n.takeOutput()).find("(nobody yet)") != std::string::npos);
    n.input((const uint8_t*)"s", 1, 1000);
    std::string stats = plain(n.takeOutput());
    CHECK(stats.find("192.168.1.77") != std::string::npos);
    CHECK(stats.find("1h 2m") != std::string::npos);
    CHECK(stats.find("SD card 7580 MB") != std::string::npos);
    CHECK(stats.find("RevJMoney") != std::string::npos);
    CHECK(stats.find("STATS") != std::string::npos);
    theme::use(theme::Look::Axis);
}
