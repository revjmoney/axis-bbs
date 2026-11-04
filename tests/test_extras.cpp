// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include <map>
#include <regex>
#include <string>
#include <vector>

#include "doctest.h"
#include "bbs/node.h"
#include "bbs/password.h"
#include "bbs/users.h"

using namespace axis::bbs;

namespace {
SysInfo     info() { return SysInfo{}; }
uint32_t    rng() { return 7; }
std::string plain(const std::string& s) { return std::regex_replace(s, std::regex("\x1b\\[[0-9;?]*[A-Za-z]"), ""); }

struct Rig {
    std::map<std::string, std::vector<std::string>> lists;  // the main's lists: rumors, bbslist
    UserStore                                       users;
    Node                                            n;
    explicit Rig(BbsConfig c = BbsConfig()) : n(1, info, c) {
        n.setLists([this](const std::string& l) { return lists[l]; }, [this](const std::string& l, const std::string& v) { lists[l].push_back(v); });
    }
    void login(const char* handle = "Rev") {
        n.begin(0);
        type(std::string(handle) + "\r");
    }
    std::string type(const std::string& s) {
        n.input((const uint8_t*)s.data(), s.size(), 1000);
        return plain(n.takeOutput());
    }
};
}  // namespace

TEST_CASE("[R]umors: add one anonymously, list them, and one shows over the main menu") {
    Rig r;
    r.lists["rumors"] = {"the sysop runs LORD on a chip the size of a stamp"};
    r.login();
    std::string menu = r.type("?");
    CHECK(menu.find("the sysop runs LORD") != std::string::npos);  // a rumor over the menu
    r.type("r");
    std::string out = r.type("a");
    CHECK(out.find("rumor") != std::string::npos);
    r.type("the ring board is haunted\r");
    REQUIRE(r.lists["rumors"].size() == 2);
    CHECK(r.lists["rumors"][1] == "the ring board is haunted");  // no name on it: rumors are anonymous
    out = r.type("l");
    CHECK(out.find("the ring board is haunted") != std::string::npos);
    r.type("q");
    CHECK(r.n.state() == Node::State::Menu);
}

TEST_CASE("[B]BS list: other boards to call, and callers can add one") {
    Rig r;
    r.lists["bbslist"] = {"Particles BBS|particlesbbs.dyndns.org:6400|Retro scene"};
    r.login();
    std::string out = r.type("b");
    CHECK(out.find("Particles BBS") != std::string::npos);
    CHECK(out.find("particlesbbs.dyndns.org:6400") != std::string::npos);
    r.type("a");
    r.type("Blackflag\r");
    r.type("blackflag.acid.org\r");
    r.type("ACiD's board\r");
    REQUIRE(r.lists["bbslist"].size() == 2);
    CHECK(r.lists["bbslist"][1] == "Blackflag|blackflag.acid.org|ACiD's board");
}

TEST_CASE("top ten: callers ranked by calls, by uploads and by posts") {
    Rig r;
    auto add = [&](const char* h, uint32_t calls, uint32_t up) {
        User u;
        u.handle = h;
        u.salt = makeSalt(rng);
        u.hash = hashPassword(u.salt, "pass1234");
        u.status = User::Status::Validated;
        u.calls = calls;
        u.uploads = up;
        r.users.add(u);
    };
    add("Kid Chips", 40, 0);
    add("Spindle", 90, 5 * 1024 * 1024);
    add("Lurker", 3, 0);
    r.n.setAccounts(&r.users, [] {}, rng, [] { return 1800000000u; });
    r.n.begin(0);
    r.type("Lurker\rpass1234\r");
    std::string out = r.type("k");
    size_t      sp = out.find("Spindle"), kid = out.find("Kid Chips");
    REQUIRE(sp != std::string::npos);
    REQUIRE(kid != std::string::npos);
    CHECK(sp < kid);  // most calls first
    CHECK(out.find("5120 KB") != std::string::npos);  // top uploader's bytes
}

TEST_CASE("a BULLETIN.ANS from the card shows after login, before the main menu") {
    BbsConfig c;
    Rig       r(c);
    r.n.setArt([](const std::string& name) { return name == "BULLETIN.ANS" ? std::string("SYSTEM NEWS: LORD is back") : std::string(); });
    r.login();
    std::string out = plain(r.n.takeOutput());
    (void)out;
    CHECK(r.n.state() == Node::State::Bulletin);
    std::string after = r.type(" ");
    CHECK(r.n.state() == Node::State::Menu);
    (void)after;
}

TEST_CASE("[V]oting booth: the sysop asks, callers vote once, everyone sees the tally") {
    std::map<std::string, std::vector<std::string>> lists;
    auto wire = [&](Node& n) {
        n.setLists([&](const std::string& l) { return lists[l]; }, [&](const std::string& l, const std::string& v) { lists[l].push_back(v); });
    };
    {
        Node sys(1, info);
        wire(sys);
        sys.beginLocal(0);  // the sysop
        sys.input((const uint8_t*)"v", 1, 0);
        std::string keys = "aBest 4th axis?\rSherline\rHomemade\r\r";
        sys.input((const uint8_t*)keys.data(), keys.size(), 0);
    }
    REQUIRE(lists["votes"].size() == 1);
    CHECK(lists["votes"][0] == "Best 4th axis?|Sherline|Homemade");
    Rig r;
    r.lists = lists;
    r.login("Kid");
    std::string out = r.type("v");
    CHECK(out.find("Best 4th axis?") != std::string::npos);
    out = r.type("1");
    CHECK(out.find("Sherline") != std::string::npos);
    r.type("2");  // Homemade
    REQUIRE(r.lists["ballots"].size() == 1);
    CHECK(r.lists["ballots"][0] == "1|Kid|2");
    out = r.type("1");  // look again: voted already, the tally shows
    CHECK(out.find("you voted") != std::string::npos);
    CHECK(out.find("1 vote") != std::string::npos);
    r.type("2");  // can't vote twice
    CHECK(r.lists["ballots"].size() == 1);
    out = r.type("a");
    CHECK(out.find("sysop") != std::string::npos);  // callers can't add questions
}

namespace {
UserStore threeUsers() {
    UserStore s;
    for (const char* h : {"Kid Chips", "Spindle", "Troll"}) {
        User u;
        u.handle = h;
        u.salt = makeSalt(rng);
        u.hash = hashPassword(u.salt, "pass1234");
        u.status = User::Status::Validated;
        u.security = 20;
        s.add(u);
    }
    return s;
}
}  // namespace

TEST_CASE("* opens the sysop's user editor: pick a caller, set their level, flag a twit") {
    UserStore users = threeUsers();
    int       saves = 0;
    Node      sys(1, info);
    sys.setAccounts(&users, [&] { saves++; }, rng, [] { return 1800000000u; });
    sys.beginLocal(0);
    sys.takeOutput();
    sys.input((const uint8_t*)"*", 1, 0);
    std::string out = plain(sys.takeOutput());
    CHECK(out.find("Spindle") != std::string::npos);
    CHECK(sys.state() == Node::State::UserEdit);
    std::string keys = "3\r";  // Troll
    sys.input((const uint8_t*)keys.data(), keys.size(), 0);
    out = plain(sys.takeOutput());
    CHECK(out.find("Troll") != std::string::npos);
    sys.input((const uint8_t*)"t", 1, 0);
    CHECK(users.find("Troll")->twit);
    keys = "s50\r";
    sys.input((const uint8_t*)keys.data(), keys.size(), 0);
    CHECK(users.find("Troll")->security == 50);
    CHECK(saves >= 2);
    sys.input((const uint8_t*)"q", 1, 0);
    sys.input((const uint8_t*)"q", 1, 0);
    CHECK(sys.state() == Node::State::Menu);
}

TEST_CASE("callers can't open the user editor") {
    UserStore users = threeUsers();
    Node      n(1, info);
    n.setAccounts(&users, [] {}, rng, [] { return 1800000000u; });
    n.begin(0);
    n.input((const uint8_t*)"Kid Chips\rpass1234\r", 19, 0);
    n.takeOutput();
    n.input((const uint8_t*)"*", 1, 0);
    CHECK(n.state() == Node::State::Menu);
}

TEST_CASE("a twit thinks they posted a oneliner and a rumor, but nobody else ever sees them") {
    UserStore users = threeUsers();
    users.find("Troll")->twit = true;
    std::vector<std::string>                        liners;
    std::map<std::string, std::vector<std::string>> lists;
    Node                                            n(1, info);
    n.setAccounts(&users, [] {}, rng, [] { return 1800000000u; });
    n.setOneLiners([&] { return liners; }, [&](const std::string& l) { liners.push_back(l); });
    n.setLists([&](const std::string& l) { return lists[l]; }, [&](const std::string& l, const std::string& v) { lists[l].push_back(v); });
    n.begin(0);
    n.input((const uint8_t*)"Troll\rpass1234\r", 15, 0);
    std::string keys = "ylol trolled\r";
    n.input((const uint8_t*)keys.data(), keys.size(), 0);
    CHECK(liners.empty());
    keys = "ranobody reads this\r";
    n.input((const uint8_t*)keys.data(), keys.size(), 0);
    CHECK(lists["rumors"].empty());
    CHECK(plain(n.takeOutput()).find("out there") != std::string::npos);  // as far as they know, it worked
}
