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
    std::map<std::string, std::string> fs;
    UserStore users;
    BbsConfig c;
    std::unique_ptr<Node> n;
    explicit SysRig(bool telnet = true, bool local = false, bool menus = false, bool runner = true) {
        c.sysopHandle = "Rev";
        c.sysopSalt = makeSalt([] { return 0x5A5A5A5Au; });
        c.sysopHash = hashPassword(c.sysopSalt, "pw");
        c.sysopTelnet = telnet;
        n.reset(new Node(1, info, c));
        n->setAccounts(&users, [] {}, [] { return 0x5A5A5A5Au; }, [] { return 1800000000u; });
        if (runner) n->setSysopRun([](const std::string&) { return std::string(); });
        if (menus) {
            fs = {{"MAIN.MNU", "prompt1=AXIS MAIN ->\ncmd=/|RUN:BBSLIST|*|0|BBS List\n"}, {"MAIN.ANS", "MAIN ART"}};
            n->setMenus([this](const std::string& f) { return fs.count(f) ? fs[f] : std::string(); });
        }
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
    SysRig r(true, false, true);
    r.type("/");
    std::string out = r.type("x");
    CHECK(r.n->state() != Node::State::Sysop);
    CHECK(out.find("/") == std::string::npos);  // erased, not echoed again
    r.type("a");
    CHECK(r.type("b").find("/") == std::string::npos);  // no stray slash on later keys
    r.type("/");
    r.type("\r");  // a / then Enter reaches the menu: the / command (BBS list) runs
    CHECK(r.n->state() == Node::State::BbsList);
}

TEST_CASE("a slash then an arrow key is not lost") {
    SysRig r(true, false, true);
    r.type("/");
    std::string out = r.type("\x1b[A");
    CHECK(out.find("/") == std::string::npos);
    CHECK(r.n->state() == Node::State::BbsList);
}

TEST_CASE("the LF after //U CR does not pick a shell item") {
    SysRig a;
    a.type("//U\r");
    a.type("\n");
    CHECK(a.n->state() == Node::State::Sysop);
    SysRig b;
    CHECK(b.type("//U\r\n").find("USERS") != std::string::npos);
    CHECK(b.n->state() == Node::State::Sysop);
}

TEST_CASE("// without a sysop runner says so") {
    SysRig r(true, false, false, false);
    std::string out = r.type("//\r");
    CHECK(out.find("unavailable") != std::string::npos);
    CHECK(r.n->state() == Node::State::Menu);
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

// ---- the terminal WFC (// then Enter)
namespace {
WfcSnapshot snap3() {
    WfcSnapshot s;
    s.bbsName = "AXIS";
    s.callsToday = 3;
    return s;
}
}  // namespace

TEST_CASE("the wfc draws the theme's art with live values") {
    SysRig r;
    r.n->setArt([](const std::string& f) { return f == "WFC.ANS" ? std::string("|BN calls |CT") : std::string(); });
    r.n->setWfcSnapshot(snap3);
    std::string out = r.type("//\r");
    CHECK(out.find("AXIS calls 3") != std::string::npos);
    CHECK(r.n->state() == Node::State::Wfc);
}

TEST_CASE("no WFC.ANS: the built-in screen") {
    SysRig r;
    r.n->setArt([](const std::string&) { return std::string(); });
    r.n->setWfcSnapshot(snap3);
    std::string out = r.type("//\r");
    CHECK(out.find("W A I T I N G   F O R   C A L L E R") != std::string::npos);
    CHECK(out.find("AXIS") != std::string::npos);
    CHECK(out.find("[S]ettings") != std::string::npos);
    CHECK(out.find("[A]rchive") != std::string::npos);
    CHECK(out.find("|") == std::string::npos);  // every code was filled in
    SysRig none;  // and without a loader at all
    CHECK(none.type("//\r").find("W A I T I N G") != std::string::npos);
}

TEST_CASE("a broken WFC file still draws") {
    for (std::string bad : {std::string(20000, 'x'), std::string("ESC[1;"), std::string("hello \x1b[1;"), std::string("hello \x1b"), std::string("A\x1a" "SAUCE00")}) {
        if (bad == "ESC[1;") bad = "\x1b[1;";
        SysRig r;
        r.n->setArt([bad](const std::string&) { return bad; });
        r.n->setWfcSnapshot(snap3);
        r.type("//");
        r.n->input((const uint8_t*)"\r", 1, 1000);
        std::string raw = r.n->takeOutput();
        CHECK_FALSE(raw.empty());
        CHECK(raw.find("\x1b[1;\x1b") == std::string::npos);  // no escape left open for what follows
        CHECK(r.n->state() == Node::State::Wfc);
        CHECK(r.type("u").find("USERS") != std::string::npos);  // and the keys still answer
    }
    SysRig z;  // 0 bytes
    z.n->setArt([](const std::string&) { return std::string(); });
    CHECK_FALSE(z.type("//\r").empty());
}

TEST_CASE("U from the wfc and back") {
    SysRig r;
    r.n->setArt([](const std::string&) { return std::string("|BN calls |CT"); });
    r.n->setWfcSnapshot(snap3);
    r.type("//\r");
    CHECK(r.type("U").find("USERS") != std::string::npos);
    CHECK(r.n->state() == Node::State::Sysop);
    std::string back;
    for (int i = 0; i < 4 && r.n->state() == Node::State::Sysop; i++) back += r.type("\x1b");
    CHECK(r.n->state() == Node::State::Wfc);
    CHECK(back.find("AXIS calls 3") != std::string::npos);
}

TEST_CASE("E A and ! are coming soon on the wfc and stay there") {
    SysRig r;
    r.n->setWfcSnapshot(snap3);
    r.type("//\r");
    CHECK(r.type("e").find("coming soon") != std::string::npos);
    CHECK(r.n->state() == Node::State::Wfc);
    CHECK(r.type("!").find("coming soon") != std::string::npos);
    CHECK(r.n->state() == Node::State::Wfc);
    r.type("\x1b[A");  // an arrow key is nothing
    CHECK(r.n->state() == Node::State::Wfc);
}

TEST_CASE("space logs the sysop on") {
    SysRig r;
    r.n->setWfcSnapshot(snap3);
    r.type("//\r");
    CHECK(r.n->state() == Node::State::Wfc);
    r.type(" ");
    CHECK(r.n->state() == Node::State::Menu);
}

TEST_CASE("Q on the wfc leaves like a logoff") {
    SysRig r;
    r.type("//\r");
    r.type("q");
    CHECK(r.n->state() == Node::State::Done);
    CHECK(r.n->wantsHangup());
}

TEST_CASE("hanging up on the wfc frees the node") {
    SysRig r;
    r.type("//\r");
    CHECK(r.n->state() == Node::State::Wfc);
    r.n->carrierLost();
    CHECK(r.n->state() == Node::State::Done);
    CHECK(r.n->wantsHangup());
    SysRig s;  // and from inside a section opened from the wfc
    s.type("//\r");
    s.type("u");
    s.n->carrierLost();
    CHECK(s.n->wantsHangup());
}

TEST_CASE("the wfc redraws every 5 s only when the snapshot changed") {
    SysRig r;
    int calls = 3;
    r.n->setArt([](const std::string&) { return std::string("|BN calls |CT"); });
    r.n->setWfcSnapshot([&] {
        WfcSnapshot s = snap3();
        s.callsToday = calls;
        return s;
    });
    r.type("//\r");
    r.n->poll(3000);
    CHECK(r.n->takeOutput().empty());
    r.n->poll(7000);  // 5 s on, nothing changed
    CHECK(r.n->takeOutput().empty());
    calls = 4;
    r.n->poll(8000);  // changed, but not 5 s yet
    CHECK(r.n->takeOutput().empty());
    r.n->poll(12500);
    std::string again = r.n->takeOutput();
    CHECK(plain(again).find("AXIS calls 4") != std::string::npos);
    CHECK(again.find("[2J") == std::string::npos);  // no clear screen: it homes and overwrites
    CHECK(again.find("[H") != std::string::npos);
    r.n->poll(18000);
    CHECK(r.n->takeOutput().empty());
    r.n->poll(1000 + 60u * 60000u);  // an hour with no key: the wfc does not time out
    CHECK(r.n->state() == Node::State::Wfc);
}

TEST_CASE("a snapshot compares by value") {
    WfcSnapshot a = snap3(), b = snap3();
    CHECK(a == b);
    b.nodes.push_back({1, "x", "y"});
    CHECK_FALSE(a == b);
    a = b;
    b.events.push_back("e");
    CHECK_FALSE(a == b);
}
