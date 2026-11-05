// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include "bbs/menurunner.h"

#include <map>

#include "doctest.h"

using namespace axis::bbs;

namespace {
struct Rig {
    std::map<std::string, std::string> fs;
    std::string                        out;
    MenuRunner                         r{[this](const std::string& f) { return fs.count(f) ? fs[f] : std::string(); },
                     [this](const char* d, size_t n) { out.append(d, n); }, [](const std::string&) { return std::string(); }};
};
}  // namespace

TEST_CASE("runner: shows art and prompt, a hotkey runs its command, GOTO returns a Goto") {
    Rig g;
    g.fs = {{"MAIN.MNU", "title=Main\nprompt1=|07Command ->\ncmd=M|GOTO:MSGMENU|*|0|\ncmd=R|RUN:READMSGS|*|0|Reading\n"}, {"MAIN.ANS", "ART"}};
    REQUIRE(g.r.enter("MAIN", AcsUser{10, ""}));
    CHECK(g.out.find("ART") != std::string::npos);
    CHECK(g.out.find("Command ->") != std::string::npos);
    MenuAction a = g.r.key('m');
    CHECK(a.kind == MenuAction::Goto);
    CHECK(a.arg == "MSGMENU");
    a = g.r.key('R');
    CHECK(a.kind == MenuAction::Run);
    CHECK(a.arg == "READMSGS");
    CHECK(g.r.activity() == "Reading");
}

TEST_CASE("runner: /SYSOP is typed then Enter; a hidden sysop command is refused below its ACS") {
    Rig g;
    g.fs = {{"MAIN.MNU", "cmd=/SYSOP|GOTO:ADMIN|s255|1|\ncmd=M|GOTO:MSGMENU|*|0|\n"}};
    REQUIRE(g.r.enter("MAIN", AcsUser{10, ""}));
    for (char c : std::string("/SYSOP")) g.r.key(c);
    CHECK(g.r.key('\r').kind == MenuAction::None);  // not a sysop
    Rig s;
    s.fs = g.fs;
    REQUIRE(s.r.enter("MAIN", AcsUser{255, ""}));
    for (char c : std::string("/SYSOP")) s.r.key(c);
    MenuAction a = s.r.key('\r');
    CHECK(a.kind == MenuAction::Goto);
    CHECK(a.arg == "ADMIN");
}

TEST_CASE("runner: a lightbar pick runs the item's key") {
    Rig g;
    g.fs = {{"MAIN.MNU", "cmd=M|GOTO:MSGMENU|*|0|\ncmd=F|GOTO:FILEM|*|0|\nbar=5,5,31,7,M,Messages\nbar=5,6,31,7,F,Files\n"}};
    REQUIRE(g.r.enter("MAIN", AcsUser{10, ""}));
    g.r.key('\x1b'), g.r.key('['), g.r.key('B');
    MenuAction a = g.r.key('\r');
    CHECK(a.kind == MenuAction::Goto);
    CHECK(a.arg == "FILEM");
}

TEST_CASE("runner: a missing or empty menu is refused (the caller falls back)") {
    Rig g;
    CHECK_FALSE(g.r.enter("NOPE", AcsUser{10, ""}));
    g.fs = {{"EMPTY.MNU", "title=Empty\n"}};
    CHECK_FALSE(g.r.enter("EMPTY", AcsUser{10, ""}));
}

TEST_CASE("runner: G asks to log off, /G hangs up, DOOR: and auto-run commands") {
    Rig g;
    g.fs = {{"MAIN.MNU", "cmd=//|RUN:PENDINGVALIDATIONNOTICE|*|0|\ncmd=L|DOOR:LORD|*|0|Playing LORD\n"}};
    REQUIRE(g.r.enter("MAIN", AcsUser{10, ""}));
    MenuAction a = g.r.key(0);  // the auto-run, before any input
    CHECK(a.kind == MenuAction::Run);
    CHECK(a.arg == "PENDINGVALIDATIONNOTICE");
    CHECK(g.r.key(0).kind == MenuAction::None);  // once only
    a = g.r.key('l');
    CHECK(a.kind == MenuAction::Door);
    CHECK(a.arg == "LORD");
    CHECK(g.r.key('g').kind == MenuAction::Logoff);
    g.r.key('/');
    CHECK(g.r.key('G').kind == MenuAction::Quit);
}

TEST_CASE("runner: an arrow key on a menu with no lightbar runs nothing (it is not the A command)") {
    Rig g;
    g.fs = {{"MAIN.MNU", "cmd=A|GOTO:AREAS|*|0|\ncmd=B|GOTO:BBS|*|0|\n"}};
    REQUIRE(g.r.enter("MAIN", AcsUser{10, ""}));
    for (char c : std::string("\x1b[A\x1b[B\x1b[5~\x1bOA")) CHECK(g.r.key(c).kind == MenuAction::None);
    CHECK(g.r.key('a').kind == MenuAction::Goto);  // the real key still works
}
