// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The shipped menu set (art/menus, what goes on the card's /menus) end to end, read on a 80x25 screen: a caller's login
// through LOGIN.SEQ to MAIN, every screen held for a key and none blank, the lightbar, the rumor over the prompt, and the
// rumors menu and back.
#include <cstdio>
#include <map>
#include <regex>
#include <string>
#include <vector>

#include "doctest.h"
#include "bbs/ansiterm.h"
#include "bbs/node.h"
#include "bbs/password.h"
#include "bbs/users.h"

using namespace axis::bbs;

namespace {
SysInfo info() { return SysInfo{}; }

std::string shipped(const std::string& name) {  // art/menus/NAME, as the card has it
    std::string s;
    if (name.find('/') != std::string::npos) return s;
    if (FILE* f = std::fopen((std::string(AXIS_TEST_DATA) + "/../../art/menus/" + name).c_str(), "rb")) {
        char b[1024];
        for (size_t n; (n = std::fread(b, 1, sizeof b, f)) > 0;) s.append(b, n);
        std::fclose(f);
    }
    return s;
}

uint32_t rngFixed() { return 0x5A5A5A5Au; }

struct Real {
    UserStore                                       users;
    std::map<std::string, std::vector<std::string>> lists;
    Node                                            n;
    AnsiTerm                                        term;
    Real() : n(1, info, BbsConfig()) {
        lists["rumors"] = {"the sysop never sleeps"};
        n.setLists([this](const std::string& l) { return l == "rumor" ? lists["rumors"] : lists[l]; },  // (the board's "rumor": one)
                   [this](const std::string& l, const std::string& v) { lists[l].push_back(v); });
        n.setOneLiners([] { return std::vector<std::string>{"hello from a oneliner"}; }, [](const std::string&) {});
        n.setMenus([](const std::string& f) { return shipped(f); });
        n.setLastCallers([] { return std::vector<std::string>{"Spindle Kid|1|2026-11-12 17:47|Myrtle Beach"}; }, [](const std::string&) {});
        n.setArt([](const std::string& name) { return name == "BULLETIN.ANS" ? std::string("BOARD NEWS ART") : std::string(); });
        n.setPoster([](const std::string& file, size_t off, size_t want, bool* exists) {
            *exists = file == "SAYLOR.ANS";
            std::string all = "SAYLOR POSTER\r\n";
            return *exists && off < all.size() ? all.substr(off, want) : std::string();
        });
        User u;
        u.handle = "Rev";
        u.salt = makeSalt(rngFixed);
        u.hash = hashPassword(u.salt, "pass1234");
        u.status = User::Status::Validated;
        u.security = 20;
        users.add(u);
        n.setAccounts(&users, [] {}, rngFixed, [] { return 1800000000u; });
    }
    std::string type(const std::string& s) {
        n.input((const uint8_t*)s.data(), s.size(), 1000);
        std::string out = n.takeOutput();
        term.feed(out);
        return out;
    }
    std::string screen() const {  // what the caller sees now
        std::string s;
        for (int r = 0; r < term.rows(); r++) {
            std::string row;
            for (int c = 0; c < term.cols(); c++) row += (char)term.cell(r, c).ch;
            while (!row.empty() && row.back() == ' ') row.pop_back();
            s += row + "\n";
        }
        return s;
    }
    bool has(const std::string& t) const { return screen().find(t) != std::string::npos; }
    bool blankButPause() const {  // a cleared screen with only "(press a key)" on it
        std::string t = std::regex_replace(screen(), std::regex("\\(press a key\\)"), "");
        return t.find_first_not_of(" \n") == std::string::npos;
    }
    void toPassword() {  // the front door to the password, as a caller
        n.begin(0);
        term.feed(n.takeOutput());
        type("j");
        type(" ");
        type("Rev\r");
    }
    void toMain() {  // a whole login, a key at every pause, the normal (full) login
        toPassword();
        type("pass1234\r");
        for (int i = 0; i < 20 && !(n.state() == Node::State::Menu && has("Messages Menu")); i++) type(n.state() == Node::State::Menu ? "1" : " ");
    }
};
}  // namespace

TEST_CASE("shipped menus: no login step is a blank screen waiting for a key") {
    Real r;
    r.toPassword();
    std::string keys[] = {"pass1234\r", " ", "1", " ", " ", " ", " ", " ", " "};
    for (const std::string& k : keys) {
        std::string out = r.type(k);
        if (out.find("press a key") != std::string::npos) CHECK_MESSAGE(!r.blankButPause(), "after [", k, "]:\n", r.screen());
    }
    CHECK(r.has("Messages Menu"));
}

TEST_CASE("shipped menus: the login screens each hold for a key: poster, news, last callers, oneliners, stats") {
    Real r;
    r.toPassword();
    r.type("pass1234\r");
    CHECK(r.has("SAYLOR POSTER"));  // nothing waiting from the sysop: no blank pause before it
    CHECK(r.has("press a key"));
    r.type(" ");
    CHECK(r.has("Normal Login"));  // the fast login lightbar
    r.type("1");
    CHECK(r.has("BOARD NEWS ART"));
    CHECK(r.has("press a key"));
    r.type(" ");
    CHECK(r.has("LAST CALLERS"));
    CHECK(r.has("Spindle Kid"));
    CHECK(r.has("press a key"));
    CHECK(r.n.state() == Node::State::SeqPause);
    r.type(" ");
    CHECK(r.has("hello from a oneliner"));  // the oneliners ask y/N: that is its own hold
    r.type(" ");
    CHECK(r.has("KB free"));  // the stats
    CHECK(r.has("press a key"));
    CHECK(r.n.state() == Node::State::SeqPause);
    r.type(" ");
    CHECK(r.has("Messages Menu"));
}

TEST_CASE("Enter (CR LF) at the poster opens the fast login lightbar; the LF does not pick Normal Login") {
    Real r;
    r.toPassword();
    r.type("pass1234\r");
    REQUIRE(r.has("SAYLOR POSTER"));
    r.type("\r\n");
    CHECK(r.has("Normal Login"));
    CHECK(!r.has("BOARD NEWS ART"));
    CHECK(r.n.state() == Node::State::Menu);
    r.type("\r\n");  // Enter on the lit bar: Normal Login, and the LF does not skip the news's pause
    CHECK(r.has("BOARD NEWS ART"));
    CHECK(r.n.state() == Node::State::SeqPause);
}

TEST_CASE("Enter on a lightbar item that opens another menu does not also pick that menu's first item") {
    Real r;
    r.toMain();
    for (int i = 0; i < 6; i++) r.type("\x1b[B");  // down to (R) Rumors
    r.type("\r\n");
    CHECK(r.has("List Rumors"));
    CHECK(r.n.state() == Node::State::Menu);  // the LF did not run (L) List Rumors
    CHECK(!r.has("the sysop never sleeps"));
}

TEST_CASE("shipped menus: MAIN has its lightbar, the first item lit") {
    Real r;
    r.toMain();
    // MAIN.MNU: bar=5,10,96,7,M, (M) Messages Menu : row 10, column 5, lit black on orange (DOS 96 = 0x60)
    CHECK(r.term.cell(9, 5).ch == '(');
    CHECK(r.term.cell(9, 5).attr == 0x60);
    CHECK(r.term.cell(10, 5).attr == 0x07);  // (T) Transfer Menu, plain
}

TEST_CASE("shipped menus: a rumor sits just above the main menu prompt, inside the menu window") {
    Real r;
    r.toMain();
    std::string row18;  // the prompt is |XY05,19: the rumor on row 18 from column 5
    for (int c = 0; c < 80; c++) row18 += (char)r.term.cell(17, c).ch;
    CHECK(row18.find("rumor: the sysop never sleeps") == 4);
    CHECK(row18.find_last_not_of(' ') < 44);  // never past the window's right edge (column 44)
    std::string row19;
    for (int c = 0; c < 80; c++) row19 += (char)r.term.cell(18, c).ch;
    CHECK(row19.find("Rev") != std::string::npos);  // the prompt still where it was
}

TEST_CASE("a long rumor is cut at a word to the menu window, and the list shows it whole") {
    Real r;
    r.lists["rumors"] = {"somebody heard the sysop runs this whole board on a pocket calculator and two potatoes"};
    r.toMain();
    std::string row18;
    for (int c = 0; c < 80; c++) row18 += (char)r.term.cell(17, c).ch;
    CHECK(row18.find("rumor: somebody heard") == 4);
    CHECK(row18.find("..") != std::string::npos);
    CHECK(row18.find_last_not_of(' ') < 44);
    r.type("r");
    r.type("l");
    CHECK(r.has("potatoes"));  // (the end of it, past the menu window)
}

TEST_CASE("no rumors yet: the main menu has no rumor line") {
    Real r;
    r.lists["rumors"].clear();
    r.toMain();
    CHECK(!r.has("rumor:"));
    CHECK(r.has("Messages Menu"));
}

TEST_CASE("shipped menus: Q on the rumors menu, and after its list, comes back to the main menu") {
    Real r;
    r.toMain();
    r.type("r");
    CHECK(r.has("List Rumors"));
    r.type("q");
    CHECK(r.n.state() == Node::State::Menu);
    CHECK(r.has("Messages Menu"));
    r.type("r");
    r.type("l");
    CHECK(r.has("the sysop never sleeps"));
    r.type("q");  // the list's own Q: back to the rumors menu
    CHECK(r.has("List Rumors"));
    r.type("q");
    CHECK(r.n.state() == Node::State::Menu);
    CHECK(r.has("Messages Menu"));
}

TEST_CASE("shipped menus: * on the rumors menu prints one rumor under the prompt and stays on the menu") {
    Real r;
    r.toMain();
    r.type("r");
    std::string out = r.type("*");
    CHECK(out.find("the sysop never sleeps") != std::string::npos);
    CHECK(out.find("\x1b[2J") == std::string::npos);  // not a screen of its own
    CHECK(r.n.state() == Node::State::Menu);
    CHECK(r.has("List Rumors"));
    r.type("q");
    CHECK(r.has("Messages Menu"));
}

TEST_CASE("an older card LOGIN.SEQ (no pauses after last callers and stats) still holds each screen") {
    Real r;
    std::string old = shipped("LOGIN.SEQ");
    for (const char* s : {"LASTCALLS|1|1|0|", "USERSTATS|1|1|0|"}) {
        size_t at = old.find(s);
        REQUIRE(at != std::string::npos);
        old.replace(at + std::string(s).find('|'), 5, "|0|0|");
    }
    REQUIRE(old.find("LASTCALLS|0|0|0|") != std::string::npos);
    r.n.setMenus([old](const std::string& f) { return f == "LOGIN.SEQ" ? old : shipped(f); });
    r.toPassword();
    r.type("pass1234\r");
    r.type(" ");   // the poster
    r.type("1");   // normal login
    r.type(" ");   // the news
    CHECK(r.has("Spindle Kid"));  // last callers, held though ONELINERS clears the screen itself
    CHECK(r.n.state() == Node::State::SeqPause);
    r.type(" ");
    CHECK(r.has("hello from a oneliner"));
    r.type(" ");
    CHECK(r.has("KB free"));  // the stats, held though MAIN clears
    CHECK(r.n.state() == Node::State::SeqPause);
    r.type(" ");
    CHECK(r.has("Messages Menu"));
}

TEST_CASE("the WFC logon (beginLocal, then the WFC up): Space shows the poster, not skipped behind the WFC") {
    Real r;
    r.n.beginLocal(0);
    r.n.openWfc(0);
    r.term.feed(r.n.takeOutput());
    r.type(" ");
    CHECK(r.has("SAYLOR POSTER"));
    CHECK(r.has("press a key"));
    r.type(" ");
    CHECK(r.has("Normal Login"));
}

TEST_CASE("a rumor stored with \"rumor: \" on it is not shown as \"rumor: rumor:\"") {
    Real r;
    r.lists["rumors"] = {"rumor: the jar is full"};
    r.toMain();
    CHECK(r.has("rumor: the jar is full"));
    CHECK(!r.has("rumor: rumor:"));
}
