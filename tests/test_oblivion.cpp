// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include <regex>
#include <string>

#include "doctest.h"
#include "bbs/node.h"
#include "bbs/theme.h"

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
