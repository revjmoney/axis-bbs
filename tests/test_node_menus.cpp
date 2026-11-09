// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The BBS on ViSiON/3-style menu files: MAIN.MNU and friends drive the main menu, their RUN: functions run
// AXIS's own features, and anything missing falls back without stranding the caller.
#include <algorithm>
#include <map>
#include <vector>
#include <regex>
#include <string>

#include "doctest.h"
#include "bbs/node.h"
#include "bbs/password.h"
#include "bbs/sysopshell.h"

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

TEST_CASE("a screen picked from the menu files stays up until a key, then the menu comes back") {
    Rig r;
    r.fs = {{"MAIN.MNU", "prompt1=MAIN ->\ncmd=W|RUN:LASTCALLERS 20|*|0|\ncmd=S|RUN:SYSTEMSTATS|*|0|\n"}, {"MAIN.ANS", "MAIN ART"}};
    r.menus();
    r.login();
    for (const char* k : {"w", "s"}) {
        std::string out = r.type(k);
        CHECK(out.find(*k == 'w' ? "LAST CALLERS" : "STATS") != std::string::npos);  // W is last callers, not who's online
        CHECK(out.find("(press a key)") != std::string::npos);
        CHECK(out.find("MAIN ART") == std::string::npos);  // not cleared away by the menu
        CHECK(r.n.state() == Node::State::MenuHold);
        CHECK(r.type("\n").empty());  // the LF of a lightbar's Enter does not skip it
        CHECK(r.n.state() == Node::State::MenuHold);
        CHECK(r.type(" ").find("MAIN ART") != std::string::npos);
        CHECK(r.n.state() == Node::State::Menu);
    }
}

TEST_CASE("a menu note sits inside the theme's menu window, not over the art's left frame") {
    Rig r;
    r.fs = {{"MAIN.MNU", "prompt1=|XY05,19MAIN ->\ncmd=X|RUN:SPONSORMENU|*|0|\n"}};
    r.menus();
    r.login();
    r.n.input((const uint8_t*)"x", 1, 1000);
    std::string raw = r.n.takeOutput();
    size_t note = raw.find("Coming soon to AXIS.");
    REQUIRE(note != std::string::npos);
    CHECK(raw.rfind("\r\n\x1b[4C", note) != std::string::npos);  // moved to column 5 first
    CHECK(raw.find("\r\n   ") == std::string::npos);             // never the old three-space indent
    CHECK(raw.find("\x1b[19;5H", note) != std::string::npos);    // and the prompt goes back to its own spot
}

TEST_CASE("the rumor and BBS list menus' add commands open the screens that add") {
    Rig r;
    r.fs = {{"MAIN.MNU", "cmd=A|RUN:RUMORSADD|*|0|\ncmd=B|RUN:BBSLISTADD|*|0|\ncmd=Q|GOTO:MAIN|*|0|\n"}};
    r.menus();
    r.login();
    CHECK(r.type("a").find("Your rumor") != std::string::npos);  // straight to typing it, no second A
    CHECK(r.n.state() == Node::State::Rumors);
    r.type("the jar is full\r");
    CHECK(r.lists["rumors"].size() == 1);
    r.type("q");
    CHECK(r.type("b").find("Board name") != std::string::npos);
    CHECK(r.n.state() == Node::State::BbsList);
}

TEST_CASE("prompts picked from the menu files start on a cleared screen, not on top of the menu art") {
    Rig r;
    r.fs = {{"MAIN.MNU", "cmd=P|RUN:CHAT|*|0|\ncmd=R|RUN:RUMORSLIST|*|0|\ncmd=V|RUN:VOTE|*|0|\ncmd=Q|GOTO:MAIN|*|0|\n"}, {"MAIN.ANS", "MAIN ART"}};
    r.menus();
    r.login();
    for (const char* k : {"p", "r", "v"}) {
        r.n.input((const uint8_t*)k, 1, 1000);
        std::string raw = r.n.takeOutput();
        CHECK(raw.find("\x1b[2J") != std::string::npos);
        r.type(*k == 'p' ? "\r" : "q");  // back out (an empty page cancels)
        if (r.n.state() == Node::State::MenuHold) r.type(" ");
    }
}

TEST_CASE("a door that can't start here (a DOS .EXE) is left off a menu file, and the lines below move up") {
    BbsConfig c;
    c.doors = {{"lord", "LORD", "*LORD", "", "none"}, {"blackjack", "BBS Black Jack", "BLKJK.EXE", "BJ.CFG", "doorsys"},
               {"ansiview", "ANSI Pack Viewer", "*ANSIVIEW", "ansi", "none"}};
    std::map<std::string, std::string> fs = {
        {"MAIN.MNU", "cmd=L|DOOR:lord|*|0|\ncmd=B|DOOR:blackjack|*|0|\ncmd=A|DOOR:ansiview|*|0|\n"
                     "bar=5,10,96,7,L, (L) Red Dragon\nbar=5,11,96,7,B, (B) BBS Black Jack\nbar=5,12,96,7,A, (A) ANSI Pack Viewer\n"}};
    Node n(1, info, c);
    n.setMenus([&fs](const std::string& f) { return fs.count(f) ? fs[f] : std::string(); });
    n.begin(0);
    n.input((const uint8_t*)"Rev\r", 4, 1000);
    if (n.state() == Node::State::SeqPause) n.input((const uint8_t*)" ", 1, 1000);
    std::string out = plain(n.takeOutput());
    CHECK(out.find("Black Jack") == std::string::npos);
    CHECK(out.find("ANSI Pack Viewer") != std::string::npos);
    n.input((const uint8_t*)"b", 1, 1000);
    CHECK(n.state() != Node::State::InDoor);
    std::string raw;
    n.input((const uint8_t*)"?", 1, 1000);  // (a redraw) the viewer's bar sits on row 11 now, where Black Jack was
    raw = n.takeOutput();
    CHECK(raw.find("\x1b[11;5H") != std::string::npos);
}

TEST_CASE("List Rumors lists them at once") {
    Rig r;
    r.fs = {{"MAIN.MNU", "cmd=L|RUN:RUMORSLIST|*|0|\ncmd=Q|GOTO:MAIN|*|0|\n"}};
    r.menus();
    r.lists["rumors"] = {"the offcut you threw away was the exact size you needed"};
    r.login();
    CHECK(r.type("l").find("the offcut you threw away") != std::string::npos);
    CHECK(r.n.state() == Node::State::Rumors);
}

TEST_CASE("a lightbar item is drawn at its own spot in the menu window") {
    // (tools/art/check_menus.py checks every shipped bar against every theme's window and has a cmd= behind it)
    Rig r;
    r.fs = {{"MAIN.MNU", "prompt1=|XY05,19MAIN ->\ncmd=M|GOTO:MAIN|*|0|\nbar=5,10,96,7,M, (M) Messages Menu \n"}};
    r.menus();
    r.login();
    std::string raw;
    r.n.input((const uint8_t*)"?", 1, 1000);
    raw = r.n.takeOutput();
    CHECK(raw.find("\x1b[10;5H") != std::string::npos);  // the bar drawn at its window spot
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

TEST_CASE("the sysop edits their own account with U, saved through the sysop commands") {
    SysRig r;
    std::vector<std::string> ran;
    r.n->setSysopRun([&ran](const std::string& c) {
        ran.push_back(c);
        return std::string();
    });
    std::string out = r.type("u");
    CHECK(out.find("BBS SETUP") == std::string::npos);
    CHECK(out.find("real name") != std::string::npos);  // the caller's account screen
    CHECK(out.find("tagline") != std::string::npos);
    CHECK(out.find("Rev") != std::string::npos);
    CHECK(r.n->state() == Node::State::Settings);

    r.type("tMachines and monkey business\r");
    r.type("lMyrtle Beach\r");
    r.type("rRev J\r");
    r.type("eme@example.com\r");
    CHECK(std::find(ran.begin(), ran.end(), "@SET sysop_tagline=Machines and monkey business") != ran.end());
    CHECK(std::find(ran.begin(), ran.end(), "@SET sysop_location=Myrtle Beach") != ran.end());
    CHECK(std::find(ran.begin(), ran.end(), "@SET sysop_real_name=Rev J") != ran.end());
    CHECK(std::find(ran.begin(), ran.end(), "@SET sysop_email=me@example.com") != ran.end());
    CHECK(r.n->tagline() == "Machines and monkey business");  // it signs the sysop's posts now

    out = r.type("pnope\r");
    CHECK(out.find("not your password") != std::string::npos);
    r.type("ppw\rnewpass1\rnewpass1\r");
    CHECK(std::find(ran.begin(), ran.end(), "@SYSOPPASS newpass1") != ran.end());
    out = r.type("ppw\r");  // the old one is gone in this session too
    CHECK(out.find("not your password") != std::string::npos);

    r.type("q");
    CHECK(r.n->state() == Node::State::Menu);
}

TEST_CASE("a sysop line can't smuggle a second setting") {
    SysRig r;
    std::vector<std::string> ran;
    r.n->setSysopRun([&ran](const std::string& c) {
        ran.push_back(c);
        return std::string();
    });
    r.type("u");
    r.type("lA\nsysop_hash=x\r");  // control bytes never reach the line
    for (const std::string& c : ran) CHECK(c.find('\n') == std::string::npos);
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
    CHECK(renegadeKey('L', s, soon));
    CHECK(s == SysopSection::System);  // logs
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
// over telnet the WFC is the sysop menu's item 9 (// alone opens that menu)
std::string toWfc(SysRig& r) {
    r.type("//\r");
    return r.type("9");
}
}  // namespace

TEST_CASE("the wfc draws the theme's art with live values") {
    SysRig r;
    r.n->setArt([](const std::string& f) { return f == "WFC.ANS" ? std::string("|BN calls |CT") : std::string(); });
    r.n->setWfcSnapshot(snap3);
    std::string out = toWfc(r);
    CHECK(out.find("AXIS calls 3") != std::string::npos);
    CHECK(r.n->state() == Node::State::Wfc);
}

TEST_CASE("no WFC.ANS: the built-in screen") {
    SysRig r;
    r.n->setArt([](const std::string&) { return std::string(); });
    r.n->setWfcSnapshot(snap3);
    std::string out = toWfc(r);
    CHECK(out.find("W A I T I N G   F O R   C A L L E R") != std::string::npos);
    CHECK(out.find("AXIS") != std::string::npos);
    CHECK(out.find("[S]ettings") != std::string::npos);
    CHECK(out.find("[A]rchive") != std::string::npos);
    CHECK(out.find("|") == std::string::npos);  // every code was filled in
    SysRig none;  // and without a loader at all
    CHECK(toWfc(none).find("W A I T I N G") != std::string::npos);
}

TEST_CASE("a broken WFC file still draws") {
    for (std::string bad : {std::string(20000, 'x'), std::string("ESC[1;"), std::string("hello \x1b[1;"), std::string("hello \x1b"), std::string("A\x1a" "SAUCE00")}) {
        if (bad == "ESC[1;") bad = "\x1b[1;";
        SysRig r;
        r.n->setArt([bad](const std::string&) { return bad; });
        r.n->setWfcSnapshot(snap3);
        r.type("//\r");
        r.n->input((const uint8_t*)"9", 1, 1000);
        std::string raw = r.n->takeOutput();
        CHECK_FALSE(raw.empty());
        CHECK(raw.find("\x1b[1;\x1b") == std::string::npos);  // no escape left open for what follows
        CHECK(r.n->state() == Node::State::Wfc);
        CHECK(r.type("u").find("USERS") != std::string::npos);  // and the keys still answer
    }
    SysRig z;  // 0 bytes
    z.n->setArt([](const std::string&) { return std::string(); });
    CHECK_FALSE(toWfc(z).empty());
}

TEST_CASE("U from the wfc and back") {
    SysRig r;
    r.n->setArt([](const std::string&) { return std::string("|BN calls |CT"); });
    r.n->setWfcSnapshot(snap3);
    toWfc(r);
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
    toWfc(r);
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
    toWfc(r);
    CHECK(r.n->state() == Node::State::Wfc);
    r.type(" ");
    CHECK(r.n->state() == Node::State::Menu);
}

TEST_CASE("Q on the wfc at the board leaves like a logoff") {
    SysRig r(true, true);
    r.n->openWfc(1000);
    r.type("q");
    CHECK(r.n->state() == Node::State::Done);
    CHECK(r.n->wantsHangup());
}

TEST_CASE("over telnet // alone opens the sysop menu, never the wfc") {
    SysRig r;
    std::string out = r.type("//\r");
    CHECK(r.n->state() == Node::State::Sysop);
    CHECK(out.find("Waiting for caller screen") != std::string::npos);
    CHECK(out.find("W A I T I N G") == std::string::npos);
}

TEST_CASE("the wfc item is in the telnet sysop menu only (at the board the wfc is home)") {
    std::string out;
    SysopShell remote(out, [](const std::string&) { return std::string(); }, 80, false);
    remote.begin();
    CHECK(out.find("Waiting for caller screen") != std::string::npos);
    out.clear();
    SysopShell here(out, [](const std::string&) { return std::string(); }, 80, true);
    here.begin();
    CHECK(out.find("Waiting for caller screen") == std::string::npos);
}

TEST_CASE("telnet wfc: Q goes back to the sysop menu it was picked from, and it never hangs up") {
    SysRig r;
    toWfc(r);
    REQUIRE(r.n->state() == Node::State::Wfc);
    r.type("q");
    CHECK(r.n->state() == Node::State::Sysop);
    CHECK_FALSE(r.n->wantsHangup());
    r.type("9");  // and in again, then Esc
    REQUIRE(r.n->state() == Node::State::Wfc);
    r.type("\x1b");
    r.n->poll(1000 + 500);  // nothing followed the Esc: a lone Esc
    CHECK(r.n->state() == Node::State::Sysop);
}

TEST_CASE("telnet wfc: Esc then a key backs out too, an arrow key does not") {
    SysRig r;
    toWfc(r);
    r.type("\x1b[B");
    CHECK(r.n->state() == Node::State::Wfc);
    r.type("\x1bx");
    CHECK(r.n->state() == Node::State::Sysop);
}

TEST_CASE("telnet wfc: a section from it and Esc comes back to the wfc, then Q to the sysop menu") {
    SysRig r;
    toWfc(r);
    CHECK(r.type("U").find("USERS") != std::string::npos);
    for (int i = 0; i < 4 && r.n->state() == Node::State::Sysop; i++) r.type("\x1b");
    CHECK(r.n->state() == Node::State::Wfc);
    r.type("q");
    CHECK(r.n->state() == Node::State::Sysop);
}

TEST_CASE("the wfc over telnet idles out like any screen; at the board it stays up") {
    SysRig r;  // a telnet sysop
    toWfc(r);
    REQUIRE(r.n->state() == Node::State::Wfc);
    uint32_t late = 1000 + (uint32_t)(r.c.idleMinutes + 1) * 60000u;
    r.n->poll(late);
    CHECK(r.n->state() == Node::State::Done);
    SysRig s(true, true);  // the sysop at the board (the touchscreen, serial or the panel's console)
    s.n->openWfc(1000);
    REQUIRE(s.n->state() == Node::State::Wfc);
    s.n->poll(late);
    CHECK(s.n->state() == Node::State::Wfc);
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
    toWfc(r);
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
    r.n->poll(1000 + 60u * 60000u);  // an hour with no key: over telnet the wfc idles out like any screen
    CHECK(r.n->state() == Node::State::Done);
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

TEST_CASE("an idle wfc is the same screen until the minute turns: uptime by the minute, free RAM in 8 KB steps") {
    WfcSnapshot a = snap3(), b = snap3();
    a.uptimeS = 600, b.uptimeS = 655;  // the same minute
    a.ramFreeKb = 80, b.ramFreeKb = 86;  // the same 8 KB step
    CHECK(a == b);
    b.uptimeS = 660;
    CHECK_FALSE(a == b);
    b.uptimeS = 600, b.ramFreeKb = 88;
    CHECK_FALSE(a == b);
}

// ---- the look preview (sysop menu > Look): the screen itself, arrows for the others, Enter uses it
namespace {
struct PreviewRig {
    SysRig                   r;
    std::vector<std::string> ran;
    PreviewRig() {
        r.n->setSysopRun([this](const std::string& c) {
            ran.push_back(c);
            return std::string("axis: ok\n");
        });
        r.n->setArt([](const std::string& name) { return name.find("skulls:") == 0 ? std::string() : "PREVIEW " + name; });
    }
    bool ranCmd(const std::string& c) const { return std::find(ran.begin(), ran.end(), c) != ran.end(); }
};
}  // namespace

TEST_CASE("look preview: the theme's main menu screen, arrows to the next look, Enter uses it") {
    PreviewRig p;
    p.r.type("//\r");
    p.r.type("8");  // Look
    p.r.type("1");  // Theme
    std::string out = p.r.type("4");  // Crt
    CHECK(out.find("PREVIEW crt:MAIN.ANS") != std::string::npos);
    CHECK(out.find("Enter") != std::string::npos);  // the key line
    CHECK(p.r.n->state() == Node::State::LookPreview);
    CHECK(p.r.type("\x1b[C").find("PREVIEW sea:MAIN.ANS") != std::string::npos);
    p.r.type("\x1b[D");
    CHECK(p.r.type("\x1b[D").find("PREVIEW green:MAIN.ANS") != std::string::npos);
    CHECK_FALSE(p.ranCmd("@SET theme=green"));
    p.r.type("\r");
    CHECK(p.ranCmd("@SET theme=green"));
    CHECK(p.r.n->state() == Node::State::Sysop);
}

TEST_CASE("look preview: Esc goes back to the list and sets nothing") {
    PreviewRig p;
    p.r.type("//\r");
    p.r.type("8");
    p.r.type("1");
    p.r.type("4");
    REQUIRE(p.r.n->state() == Node::State::LookPreview);
    p.r.type("\x1b");
    p.r.n->poll(1000 + 600);  // nothing followed it: a lone Esc
    CHECK(p.r.n->state() == Node::State::Sysop);
    CHECK(plain(p.r.n->takeOutput()).find("Crt, hardcore crt") != std::string::npos);
    for (const std::string& c : p.ran) CHECK(c.rfind("@SET theme=", 0) != 0);
    p.r.type("4");
    p.r.type("q");  // Q backs out too
    CHECK(p.r.n->state() == Node::State::Sysop);
}

TEST_CASE("look preview: the waiting screen skins, the classic built-in screen first, and a look with no screens") {
    PreviewRig p;
    p.r.n->setWfcSnapshot(snap3);
    p.r.type("//\r");
    p.r.type("8");
    p.r.type("2");  // Waiting screen skin
    std::string out = p.r.type("2");  // Classic AXIS (built in)
    CHECK(out.find("W A I T I N G") != std::string::npos);  // the built-in art
    CHECK(out.find("PREVIEW") == std::string::npos);
    CHECK(p.r.type("\x1b[C").find("PREVIEW axis:WFC.ANS") != std::string::npos);
    p.r.type("\x1b[D");
    p.r.type("\r");
    CHECK(p.ranCmd("@SET wfc=classic"));
    p.r.type("2");  // the skin picker again
    out = p.r.type("a");  // the tenth item: skulls (the stub has no screens for it)
    CHECK(out.find("No screens for this look") != std::string::npos);
    CHECK(p.r.n->state() == Node::State::LookPreview);  // still previewing: arrows go on to the next
}

TEST_CASE("a lone Esc in the sysop menu goes back after a pause, without waiting for (and eating) the next key") {
    SysRig r;
    r.type("//\r");
    r.type("8");  // Look
    REQUIRE(r.n->state() == Node::State::Sysop);
    r.type("\x1b");
    r.n->poll(1000 + 500);
    std::string top = plain(r.n->takeOutput());
    CHECK(top.find("Waiting for caller screen") != std::string::npos);  // back on the top layer
    r.type("8");  // this key counts: Look again
    CHECK(r.type("1").find("Crt, hardcore crt") != std::string::npos);
    SysRig s;  // and from the top layer, out of the sysop menu
    s.type("//\r");
    s.type("\x1b");
    s.n->poll(1000 + 500);
    CHECK(s.n->state() == Node::State::Menu);
}

TEST_CASE("the autosig and password menu commands open the account screen (the tagline is the autosig)") {
    for (const char* cmd : {"CFG_AUTOSIG", "CFG_PASSWORD", "USERCONFIG"}) {
        SysRig r(true, false, false);
        r.fs = {{"MAIN.MNU", std::string("prompt1=MAIN ->\ncmd=U|RUN:") + cmd + "|*|0|\n"}};
        r.n->setMenus([&r](const std::string& f) { return r.fs.count(f) ? r.fs[f] : std::string(); });
        r.n->begin(0);
        r.type("Rev\rpw\r");
        if (r.n->state() == Node::State::SeqPause) r.type(" ");
        std::string out = r.type("u");
        CHECK(out.find("Coming soon") == std::string::npos);
        CHECK(out.find("tagline") != std::string::npos);
        CHECK(r.n->state() == Node::State::Settings);
    }
}