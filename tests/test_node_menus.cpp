// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The BBS on ViSiON/3-style menu files: MAIN.MNU and friends drive the main menu, their RUN: functions run
// AXIS's own features, and anything missing falls back without stranding the caller.
#include <map>
#include <regex>
#include <string>

#include "doctest.h"
#include "bbs/node.h"

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
