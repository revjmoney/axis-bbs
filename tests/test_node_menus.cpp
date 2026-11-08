// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The BBS on ViSiON/3-style menu files: MAIN.MNU and friends drive the main menu, their RUN: functions run
// AXIS's own features, and anything missing falls back without stranding the caller.
#include <map>
#include <regex>
#include <string>

#include "doctest.h"
#include "bbs/node.h"
#include "bbs/password.h"

using namespace axis::bbs;

namespace {
SysInfo     info() { return SysInfo{}; }
std::string plain(const std::string& s) { return std::regex_replace(s, std::regex("\x1b\\[[0-9;?]*[A-Za-z]"), ""); }

struct Rig {
    std::map<std::string, std::vector<std::string>> lists;
    std::map<std::string, std::string>              fs;
    Node                                            n;
    Rig() : n(1, info, BbsConfig()) {
        n.setLists([this](const std::string& l) { return lists[l]; }, [this](const std::string& l, const std::string& v) { lists[l].push_back(v); });
    }
    void menus() { n.setMenus([this](const std::string& f) { return fs.count(f) ? fs[f] : std::string(); }); }
    void login() {
        n.begin(0);
        type("Rev\r");
        if (n.state() == Node::State::SeqPause) type(" ");  // the default login sequence's one pause, then MAIN
    }
    std::string type(const std::string& s) {
        n.input((const uint8_t*)s.data(), s.size(), 1000);
        return plain(n.takeOutput());
    }
};
}  // namespace

TEST_CASE("with menu files the caller lands on MAIN.MNU and its keys run AXIS features") {
    Rig r;
    r.fs = {{"MAIN.MNU", "prompt1=AXIS MAIN ->\ncmd=B|RUN:BBSLIST|*|0|BBS List\n"}, {"MAIN.ANS", "MAIN ART"}};
    r.menus();
    r.login();
    std::string menu = r.type("?");
    CHECK(menu.find("MAIN ART") != std::string::npos);
    CHECK(menu.find("AXIS MAIN ->") != std::string::npos);
    r.type("b");
    CHECK(r.n.state() == Node::State::BbsList);  // the existing BBS list
}

TEST_CASE("a GOTO to a missing menu lands back on the menu's fallback (MAIN), not stranded") {
    Rig r;
    r.fs = {{"MAIN.MNU", "prompt1=MAIN ->\nfallback=MAIN\ncmd=X|GOTO:NOPE|*|0|\n"}};
    r.menus();
    r.login();
    CHECK(r.type("x").find("MAIN ->") != std::string::npos);
    CHECK(r.n.state() == Node::State::Menu);
}

TEST_CASE("if MAIN.MNU itself stops loading, the built-in menu takes over for the call") {
    Rig r;
    r.fs = {{"MAIN.MNU", "fallback=MAIN\ncmd=X|GOTO:NOPE|*|0|\n"}};
    r.menus();
    r.login();
    r.fs.clear();  // the card went away
    r.type("x");
    CHECK(r.n.state() == Node::State::Menu);
    CHECK(r.type("?").find("essages") != std::string::npos);  // the built-in menu's items
}

TEST_CASE("an unknown RUN: function says coming soon and keeps the caller on the menu") {
    Rig r;
    r.fs = {{"MAIN.MNU", "cmd=X|RUN:SPONSORMENU|*|0|\n"}};
    r.menus();
    r.login();
    CHECK(r.type("x").find("Coming soon to AXIS.") != std::string::npos);
    CHECK(r.n.state() == Node::State::Menu);
}

TEST_CASE("a GOTO to another menu file shows that menu, and Q style GOTO:MAIN comes back") {
    Rig r;
    r.fs = {{"MAIN.MNU", "prompt1=MAIN ->\ncmd=M|GOTO:MSGMENU|*|0|\n"}, {"MSGMENU.MNU", "prompt1=MSG ->\ncmd=Q|GOTO:MAIN|*|0|\n"}};
    r.menus();
    r.login();
    CHECK(r.type("m").find("MSG ->") != std::string::npos);
    CHECK(r.type("q").find("MAIN ->") != std::string::npos);
}

namespace {
struct SysRig {
    UserStore users;
    BbsConfig c;
    std::unique_ptr<Node> n;
    explicit SysRig(bool telnet = true, bool local = false) {
        c.sysopHandle = "Rev";
        c.sysopSalt = makeSalt([] { return 0x5A5A5A5Au; });
        c.sysopHash = hashPassword(c.sysopSalt, "pw");
        c.sysopTelnet = telnet;
        n.reset(new Node(1, info, c));
        n->setAccounts(&users, [] {}, [] { return 0x5A5A5A5Au; }, [] { return 1800000000u; });
        n->setSysopRun([](const std::string&) { return std::string(); });
        n->begin(0);
        if (local) n->beginLocal(0);
        else {
            type("Rev\rpw\r");
            if (n->state() == Node::State::SeqPause) type(" ");
        }
    }
    std::string type(const std::string& s) {
        n->input((const uint8_t*)s.data(), s.size(), 1000);
        return plain(n->takeOutput());
    }
};
}  // namespace

TEST_CASE("a sysop's //U opens the user section") {
    SysRig r;
    CHECK(r.n->isSysop());
    std::string out = r.type("//U\r");
    CHECK(out.find("USERS") != std::string::npos);
    CHECK(r.n->state() == Node::State::Sysop);
}

TEST_CASE("// is nothing special for a caller") {
    Rig r;
    r.login();
    std::string out = r.type("//\r");
    CHECK(out.find("USERS") == std::string::npos);
    CHECK(out.find("WFC") == std::string::npos);
    CHECK(r.n.state() == Node::State::Menu);
    out = r.type("//U\r");
    CHECK(out.find("USERS") == std::string::npos);
    CHECK(r.n.state() == Node::State::Menu);
}

TEST_CASE("telnet sysop access off") {
    SysRig off(false);
    CHECK(off.n->isSysop());
    std::string out = off.type("//U\r");
    CHECK(out.find("USERS") == std::string::npos);
    CHECK(off.n->state() == Node::State::Menu);
    SysRig loc(false, true);
    out = loc.type("//U\r");
    CHECK(out.find("USERS") != std::string::npos);
    CHECK(loc.n->state() == Node::State::Sysop);
}

TEST_CASE("E A and ! are coming soon") {
    SysRig r;
    CHECK(r.type("//E\r").find("coming soon") != std::string::npos);
    CHECK(r.n->state() == Node::State::Menu);
    CHECK(r.type("//!\r").find("coming soon") != std::string::npos);
}

TEST_CASE("a lone slash still reaches the menu for a sysop") {
    SysRig r;
    r.type("/");
    r.type("l");
    CHECK(r.n->state() != Node::State::Sysop);
}

TEST_CASE("renegadeKey maps the section keys") {
    SysopSection s = SysopSection::Top;
    bool         soon = true;
    CHECK(renegadeKey('b', s, soon));
    CHECK(s == SysopSection::MsgBases);
    CHECK_FALSE(soon);
    CHECK(renegadeKey('!', s, soon));
    CHECK(soon);
    CHECK_FALSE(renegadeKey('z', s, soon));
}
