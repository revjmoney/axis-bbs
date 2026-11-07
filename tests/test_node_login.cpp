// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// ViSiON/3's front door and login sequence on menu files: the pre-login matrix, pre-logon art, the LOGIN
// screen, check access, the new user lightbar, then the login sequence and fast login before MAIN.
#include <map>
#include <regex>
#include <string>

#include "doctest.h"
#include "bbs/node.h"
#include "bbs/password.h"
#include "bbs/users.h"

using namespace axis::bbs;

namespace {
SysInfo     info() { return SysInfo{}; }
uint32_t    rng() { return 0x5A5A5A5Au; }
std::string plain(const std::string& s) { return std::regex_replace(s, std::regex("\x1b\\[[0-9;?]*[A-Za-z]"), ""); }

const char* kMatrix = "cmd=J|LOGIN|*|0|\ncmd=C|NEWUSER|*|0|\ncmd=A|CHECKACCESS|*|0|\ncmd=D|DISCONNECT|*|0|\n"
                      "bar=49,10,31,5,J, Journey onward.\nbar=49,11,31,5,C, Create an account.\n"
                      "bar=49,12,31,5,A, Check your access.\nbar=49,13,31,5,D, Disconnect.\n";

struct Rig {
    UserStore                          users;
    std::map<std::string, std::string> fs = {{"MAIN.MNU", "prompt1=MAIN ->\ncmd=M|GOTO:MSGMENU|*|0|\n"}, {"PDMATRIX.MNU", kMatrix},
                                             {"PRELOGON1.ANS", "PRE ART"}, {"LOGIN.ANS", "LOGIN ART"}, {"NEWUSER.ANS", "NEWUSER ART"}};
    Node                               n;
    std::string                        last;  // the raw output of the last type()
    Rig() : n(1, info, BbsConfig()) {
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
        n.setMenus([this](const std::string& f) { return fs.count(f) ? fs[f] : std::string(); });
        n.begin(0);
        last = n.takeOutput();
    }
    std::string type(const std::string& s) {
        n.input((const uint8_t*)s.data(), s.size(), 1000);
        last = n.takeOutput();
        return plain(last);
    }
};
}  // namespace

TEST_CASE("front door: the matrix lightbar, then pre-logon art, then the LOGIN screen") {
    Rig r;
    r.start();
    CHECK(r.last.find("Journey onward.") != std::string::npos);
    CHECK(r.n.state() == Node::State::Matrix);
    CHECK(r.type("j").find("PRE ART") != std::string::npos);
    CHECK(r.type(" ").find("LOGIN ART") != std::string::npos);
    CHECK(r.n.state() == Node::State::Login);
}

TEST_CASE("front door: check access reports a user's status and returns to the matrix") {
    Rig r;
    r.start();
    r.type("a");
    CHECK(r.type("Rev\r").find("validated") != std::string::npos);
    r.type(" ");  // a key after reading it
    CHECK(r.n.state() == Node::State::Matrix);
    r.type("a");
    CHECK(r.type("Nobody\r").find("no such user") != std::string::npos);
}

TEST_CASE("front door: ten wanders without logging in disconnect") {
    Rig r;
    r.start();
    for (int i = 0; i < 11 && r.n.state() != Node::State::Done; i++) {
        r.type("a");
        r.type("\r");
    }
    CHECK(r.n.state() == Node::State::Done);
}

TEST_CASE("LOGIN.ANS markers put the handle and password where the art says, and are blanked") {
    Rig r;
    r.fs["LOGIN.ANS"] = "\x1b[5;10HUser: |{P}\x1b[6;10HPass: |{O}";
    r.start();
    r.type("j");
    r.type(" ");
    CHECK(r.last.find("|{P}") == std::string::npos);
    CHECK(r.last.find("\x1b[5;16H") != std::string::npos);  // the handle goes where |{P} was
    r.type("Rev\r");
    CHECK(r.last.find("\x1b[6;16H") != std::string::npos);  // and the password where |{O} was
}

TEST_CASE("new user: NEWUSER.ANS and a yes/no lightbar; no goes back to the matrix") {
    Rig r;
    r.start();
    CHECK(r.type("c").find("NEWUSER ART") != std::string::npos);
    r.type("n");
    CHECK(r.n.state() == Node::State::Matrix);
    r.type("c");
    r.type("y");
    CHECK(r.n.state() == Node::State::Login);  // on to the application: the handle first
}

TEST_CASE("front door: D disconnects") {
    Rig r;
    r.start();
    r.type("d");
    CHECK(r.n.state() == Node::State::Done);
}

namespace {
std::string loginAll(Rig& r) {  // through the front door with Rev's password; everything shown from the password on
    r.start();
    r.type("j");
    r.type(" ");
    r.type("Rev\r");
    return r.type("pass1234\r");
}
}  // namespace

TEST_CASE("login sequence: steps run in order, a level-limited step is skipped, then MAIN") {
    Rig r;
    r.fs["LOGIN.SEQ"] = "step=DISPLAYFILE|0|1|0|NEWS\nstep=DISPLAYFILE|0|0|255|SECRET\n";
    r.fs["NEWS.ANS"] = "NEWS ART";
    r.fs["SECRET.ANS"] = "SECRET ART";
    std::string out = loginAll(r);
    CHECK(out.find("NEWS ART") != std::string::npos);
    CHECK(out.find("press a key") != std::string::npos);  // its pause
    std::string rest = r.type(" ");
    CHECK(rest.find("SECRET ART") == std::string::npos);  // level 255 only
    CHECK(rest.find("MAIN ->") != std::string::npos);
}

TEST_CASE("fast login: 2 jumps to MAIN and skips the rest; 1 continues the sequence") {
    Rig r;
    r.fs["LOGIN.SEQ"] = "step=FASTLOGIN|0|0|0|\nstep=DISPLAYFILE|0|0|0|LATER\n";
    r.fs["FASTLOGN.MNU"] = "prompt1=FAST ->\ncmd=1|RUN:FULL_LOGIN_SEQUENCE|*|0|\ncmd=2|GOTO:MAIN|*|0|\n";
    r.fs["LATER.ANS"] = "LATER ART";
    CHECK(loginAll(r).find("FAST ->") != std::string::npos);
    std::string jump = r.type("2");
    CHECK(jump.find("MAIN ->") != std::string::npos);
    CHECK(jump.find("LATER ART") == std::string::npos);
    Rig s;
    s.fs = r.fs;
    loginAll(s);
    std::string full = s.type("1");
    CHECK(full.find("LATER ART") != std::string::npos);
    CHECK(full.find("MAIN ->") != std::string::npos);
}

TEST_CASE("login sequence: user stats and who's online are the existing screens") {
    Rig r;
    r.fs["LOGIN.SEQ"] = "step=USERSTATS|0|0|0|\nstep=WHOISONLINE|0|0|0|\nstep=DISPLAYFILE|0|0|0|AFTER\n";
    r.fs["AFTER.ANS"] = "AFTER ART";
    std::string out = loginAll(r);
    CHECK(out.find("free RAM") != std::string::npos);
    CHECK(out.find("WHO'S ONLINE") != std::string::npos);
    CHECK(out.find("AFTER ART") != std::string::npos);
    CHECK(out.find("MAIN ->") != std::string::npos);
}

TEST_CASE("the Saylor poster shows from the theme, then the rest of the sequence") {
    Rig r;
    r.fs["LOGIN.SEQ"] = "step=DISPLAYFILE|0|1|0|NEWS\n";
    r.fs["NEWS.ANS"] = "NEWS ART";
    r.n.setArt([](const std::string& name) { return name == "SAYLOR.ANS" ? std::string("SAYLOR POSTER") : std::string(); });
    std::string out = loginAll(r);
    CHECK(out.find("SAYLOR POSTER") != std::string::npos);
    CHECK(out.find("press a key") != std::string::npos);
    CHECK(r.type(" ").find("NEWS ART") != std::string::npos);
}

TEST_CASE("a missing Saylor poster is skipped") {
    Rig r;
    r.fs["LOGIN.SEQ"] = "step=DISPLAYFILE|0|0|0|NEWS\n";
    r.fs["NEWS.ANS"] = "NEWS ART";
    std::string out = loginAll(r);
    CHECK(out.find("SAYLOR") == std::string::npos);
    CHECK(out.find("NEWS ART") != std::string::npos);
}

TEST_CASE("sysop local logon runs the sequence, and fast login skips the bulletin") {
    Rig r;
    r.fs["LOGIN.SEQ"] = "step=FASTLOGIN|0|0|0|\nstep=PRINTNEWS|0|0|0|\nstep=DISPLAYFILE|0|0|0|LATER\n";
    r.fs["FASTLOGN.MNU"] = "prompt1=FAST ->\ncmd=1|RUN:FULL_LOGIN_SEQUENCE|*|0|\ncmd=2|GOTO:MAIN|*|0|\n";
    r.fs["LATER.ANS"] = "LATER ART";
    auto art = [](const std::string& name) {
        if (name == "SAYLOR.ANS") return std::string("SAYLOR POSTER");
        if (name == "BULLETIN.ANS") return std::string("BOARD NEWS");
        return std::string();
    };
    r.n.setArt(art);
    r.n.setMenus([&](const std::string& f) { return r.fs.count(f) ? r.fs[f] : std::string(); });
    r.n.beginLocalMenu(0);
    std::string out = plain(r.n.takeOutput());
    CHECK(r.n.isSysop());
    CHECK(out.find("Welcome back") != std::string::npos);
    CHECK(out.find("SAYLOR POSTER") != std::string::npos);
    CHECK(r.type(" ").find("FAST ->") != std::string::npos);
    std::string jump = r.type("2");
    CHECK(jump.find("BOARD NEWS") == std::string::npos);
    CHECK(jump.find("LATER ART") == std::string::npos);
    CHECK(jump.find("MAIN ->") != std::string::npos);

    Rig full;
    full.fs = r.fs;
    full.n.setArt(art);
    full.n.setMenus([&](const std::string& f) { return full.fs.count(f) ? full.fs[f] : std::string(); });
    full.n.beginLocalMenu(0);
    full.type(" ");
    std::string news = full.type("1");
    CHECK(news.find("BOARD NEWS") != std::string::npos);
    CHECK(news.find("press a key") != std::string::npos);
    std::string later = full.type(" ");
    CHECK(later.find("LATER ART") != std::string::npos);
    CHECK(later.find("MAIN ->") != std::string::npos);
}

TEST_CASE("an unknown login sequence step is skipped") {
    Rig r;
    r.fs["LOGIN.SEQ"] = "step=PRINTNEWS|0|0|0|\nstep=DISPLAYFILE|0|0|0|X\n";
    r.fs["X.ANS"] = "X ART";
    std::string out = loginAll(r);
    CHECK(out.find("X ART") != std::string::npos);
    CHECK(out.find("MAIN ->") != std::string::npos);
}
