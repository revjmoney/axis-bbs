// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include "doctest.h"
#include "bbs/config.h"
#include "bbs/node.h"
#include "bbs/script.h"
#include "bbs/station.h"

#include <string>

using axis::bbs::Node;
using axis::bbs::ScriptProg;
namespace station = axis::bbs::station;
using axis::bbs::SysInfo;

static std::string plain(const std::string& s) {  // a clear screen is ESC [ 2 J, so stop on the command letter
    std::string o;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '\x1b' && i + 1 < s.size() && s[i + 1] == '[') {
            i += 2;
            while (i < s.size() && ((s[i] >= '0' && s[i] <= '9') || s[i] == ';' || s[i] == '?')) i++;
            continue;
        }
        o += s[i];
    }
    return o;
}

static SysInfo info() {
    SysInfo s;
    s.timeText = "2026-11-07 12:00";
    s.storage = "SD";
    return s;
}

static std::string hello() { return "say hi @HANDLE\nexit\n"; }

TEST_CASE("script say, goto, gosub, and a variable") {
    ScriptProg p;
    ScriptProg::World w{"Rev", "Axis", "1", "now"};
    REQUIRE(p.load("goto skip\nsay no\n#skip\nsay yes @HANDLE\ngosub hi\nsay back\nexit\n#hi\nsay there\nreturn\n", w));
    std::string o;
    CHECK_FALSE(p.step(-1, o));
    CHECK(o.find("no") == std::string::npos);
    CHECK(o.find("yes Rev") != std::string::npos);
    CHECK(o.find("there") != std::string::npos);
    CHECK(o.find("back") != std::string::npos);
}

TEST_CASE("script if else and ask") {
    ScriptProg p;
    REQUIRE(p.load("ask NAME Who\nif NAME = Ada\nsay yes\nelse\nsay no\nend\nexit\n", {}));
    std::string o;
    CHECK(p.step(-1, o));
    CHECK(p.waiting());
    for (char c : std::string("Ada")) CHECK(p.step(c, o));
    CHECK_FALSE(p.step('\r', o));
    CHECK(o.find("yes") != std::string::npos);
    CHECK(o.find("no") == std::string::npos);
}

TEST_CASE("script menu jumps to the chosen label") {
    ScriptProg p;
    REQUIRE(p.load("menu\n1 shop The shop\n2 quit Leave\nend\n#shop\nsay in\nexit\n#quit\nsay out\nexit\n", {}));
    std::string o;
    CHECK(p.step(-1, o));
    CHECK(o.find("The shop") != std::string::npos);
    CHECK_FALSE(p.step('1', o));
    CHECK(o.find("in") != std::string::npos);
}

TEST_CASE("script list pick returns the file name") {
    ScriptProg p;
    REQUIRE(p.offer({"hello", "shop"}));
    std::string o;
    CHECK(p.step(-1, o));
    CHECK(o.find("shop") != std::string::npos);
    CHECK_FALSE(p.step('2', o));
    CHECK(p.picked() == "shop");
}

TEST_CASE("a tight goto is stopped") {
    ScriptProg p;
    REQUIRE(p.load("#a\ngoto a\n", {}));
    std::string o;
    CHECK_FALSE(p.step(-1, o));
    CHECK(o.find("too long") != std::string::npos);
}

TEST_CASE("the scripts door runs a card file and returns to the door list") {
    auto c = axis::bbs::parseConfig("");
    c.loginSplash = false;  // an empty file turns the splash on; this test starts at the handle prompt
    Node n(1, info, c);
    n.setScripts([](const std::string& name) { return name == "hello" ? hello() : std::string(); },
                 [] { return std::vector<std::string>{"hello"}; });
    n.begin(0);
    n.input((const uint8_t*)"Rev\r", 4, 1000);
    n.takeOutput();
    n.input((const uint8_t*)"d", 1, 1000);
    std::string doors = plain(n.takeOutput());
    CHECK(doors.find("Scripts") != std::string::npos);
    n.input((const uint8_t*)"3", 1, 1000);  // 1 LORD, 2 ANSI Movies, 3 Scripts
    std::string list = plain(n.takeOutput());
    CHECK(n.state() == Node::State::Script);
    CHECK(list.find("hello") != std::string::npos);
    n.input((const uint8_t*)"1", 1, 1000);
    std::string ran = plain(n.takeOutput());
    CHECK(ran.find("hi Rev") != std::string::npos);
    CHECK(n.state() == Node::State::Doors);
}

TEST_CASE("a menu command runs the same engine") {
    Node n(1, info);
    n.setMenus([](const std::string& name) {
        return name == "MAIN.MNU" ? std::string("title=Main\ncmd=S|SCRIPT:hello\n") : std::string();
    });
    n.setScripts([](const std::string& name) { return name == "hello" ? hello() : std::string(); },
                 [] { return std::vector<std::string>{"hello"}; });
    n.beginLocalMenu(0);
    n.takeOutput();
    n.input((const uint8_t*)" ", 1, 1000);  // the login sequence's mail pause, then the menu
    n.takeOutput();
    n.input((const uint8_t*)"S", 1, 1000);
    std::string o = plain(n.takeOutput());
    CHECK(o.find("hi Sysop") != std::string::npos);
    CHECK(n.state() == Node::State::Menu);
}

TEST_CASE("a script can read a stub sensor, flip a relay, and beep") {
    station::relay(1, 0);
    ScriptProg p;
    REQUIRE(p.load("sensor temp T\nsay @T\nsensor nope\nrelay 1 on\nrelay 1\nbeep 440 200\nexit\n", {}));
    std::string o;
    CHECK_FALSE(p.step(-1, o));
    CHECK(o.find("72") != std::string::npos);
    CHECK(o.find("no such sensor") != std::string::npos);
    CHECK(o.find("relay 1 on") != std::string::npos);
    CHECK(station::relayState(1) == 1);
    CHECK(station::lastBeepHz() == 440);
    CHECK(station::lastBeepMs() == 200);
    station::relay(1, 0);
}

TEST_CASE("a login sequence step can be a script") {
    Node n(1, info);
    n.setMenus([](const std::string& name) {
        if (name == "MAIN.MNU") return std::string("title=Main\ncmd=G|LOGOFF\n");
        if (name == "LOGIN.SEQ") return std::string("step=SCRIPT|0|0|0|hello\n");
        return std::string();
    });
    n.setScripts([](const std::string& name) { return name == "hello" ? hello() : std::string(); },
                 [] { return std::vector<std::string>{}; });
    n.begin(0);
    n.input((const uint8_t*)"Rev\r", 4, 1000);
    std::string o = plain(n.takeOutput());
    CHECK(o.find("hi Rev") != std::string::npos);
    CHECK(n.state() == Node::State::Menu);
}
