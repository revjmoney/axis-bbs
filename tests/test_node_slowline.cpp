// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// A slow caller (a LoRa radio call at a few hundred bytes a second): every art screen goes through the one art drawer, a
// piece at a time as the caller's line takes it, so the board never waits on one caller and a screen never sits in RAM
// whole. While the line is backed up nothing new is drawn and the login sequence starts no next step; what the node writes
// after a screen starts (a prompt, a lightbar) comes out behind it.
#include <map>
#include <string>
#include <vector>

#include "doctest.h"
#include "bbs/node.h"
#include "bbs/password.h"
#include "bbs/users.h"

using namespace axis::bbs;

namespace {
SysInfo     info() { return SysInfo{}; }
uint32_t    rng() { return 0x5A5A5A5Au; }
std::string big(const char* name, char fill = '.') {  // a screen of 40 rows of 80 (3 KB), its name at the end
    std::string s;
    for (int i = 0; i < 40; i++) s += std::string(78, fill) + "\r\n";
    return s + name;
}

// The caller's line as main.cpp has it: the node's pieces go straight in (setDirect), the rest after the turn (sendApp),
// and it is backed up while 1 KB or more waits (TelnetStream::kHoldLow). The caller reading empties it.
struct Rig {
    UserStore                          users;
    std::map<std::string, std::string> fs = {{"MAIN.MNU", "clr=1\nacs=*\nprompt=1\nprompt1=MAIN PROMPT ->\ncmd=G|LOGOFF\n"},
                                             {"LOGIN.ANS", "LOGIN ART"},
                                             {"LOGIN.SEQ", "step=DISPLAYFILE|1|0|0|ONE\nstep=DISPLAYFILE|1|0|0|TWO\nstep=DISPLAYFILE|1|0|0|THREE\n"},
                                             {"ONE.ANS", big("SCREEN ONE", 'a')},
                                             {"TWO.ANS", big("SCREEN TWO", 'b')},
                                             {"THREE.ANS", big("SCREEN THREE", 'c')}};
    std::string                        line;  // sent to the caller, not read yet
    bool                               slow = true;
    std::vector<size_t>                menuOffsets;  // every piece of a menu screen asked for
    Node                               n;
    Rig() : n(1, info, BbsConfig()) {
        User u;
        u.handle = "Rev";
        u.salt = makeSalt(rng);
        u.hash = hashPassword(u.salt, "pass1234");
        u.status = User::Status::Validated;
        u.security = 20;
        users.add(u);
        n.setAccounts(&users, [] {}, rng, [] { return 1800000000u; });
        n.setMenus([this](const std::string& f) { return fs.count(f) ? fs[f] : std::string(); });
        n.setDirect([this](const char* p, size_t k) { line.append(p, k); });
        n.setBackedUp([this] { return slow && line.size() >= 1024; });
        n.begin(0);
        n.takeOutput();
        type("Rev\r");
        read();
    }
    void useMenuArt() {  // menu screens read by offset, as main.cpp's menuArtPiece does from the card
        n.setMenuArt([this](const std::string& file, size_t off, size_t want, bool* exists) {
            *exists = fs.count(file) > 0;
            if (!*exists) return std::string();
            if (file == "MAIN.ANS") menuOffsets.push_back(off);
            const std::string& s = fs[file];
            return off < s.size() ? s.substr(off, want) : std::string();
        });
    }
    void type(const std::string& s) {
        n.input((const uint8_t*)s.data(), s.size(), 1000);
        line += n.takeOutput();
    }
    void poll() {
        n.poll(1000);
        line += n.takeOutput();
    }
    std::string read() {  // the caller takes everything waiting
        std::string s;
        s.swap(line);
        return s;
    }
    // the caller reads, the board takes a turn, until nothing more comes; each turn's bytes in turns (when given)
    std::string drain(std::vector<size_t>* turns = nullptr) {
        std::string all;
        for (int quiet = 0, i = 0; quiet < 3 && i < 500; i++) {
            std::string got = read();
            all += got;
            if (turns && !got.empty()) turns->push_back(got.size());
            quiet = got.empty() ? quiet + 1 : 0;
            poll();
        }
        return all + read();
    }
};
bool has(const std::string& s, const char* what) { return s.find(what) != std::string::npos; }
}  // namespace

TEST_CASE("a caller whose line keeps up gets the whole login sequence and the menu in one go, as before") {
    Rig r;
    r.slow = false;
    r.type("pass1234\r");
    std::string out = r.read();
    CHECK(has(out, "SCREEN ONE"));
    CHECK(has(out, "SCREEN TWO"));
    CHECK(has(out, "SCREEN THREE"));
    CHECK(has(out, "MAIN PROMPT ->"));
    CHECK(r.n.state() == Node::State::Menu);
}

TEST_CASE("a slow caller's screens come a piece a turn, in order, and nothing more while its line is full") {
    Rig r;
    r.type("pass1234\r");
    std::string first = r.read();
    CHECK_FALSE(first.empty());
    CHECK(first.size() < 4096);  // a piece or two of screen one, not the screen, not the sequence
    CHECK_FALSE(has(first, "SCREEN ONE"));
    r.line = std::string(2000, '~');  // the caller is slow: the line is still full
    r.poll();
    r.poll();
    CHECK(r.line == std::string(2000, '~'));  // nothing drawn while it is
    r.read();
    std::vector<size_t> turns;
    std::string         all = first + r.drain(&turns);
    for (size_t t : turns) CHECK(t < 4096);
    CHECK(all.find("SCREEN ONE") < all.find("SCREEN TWO"));
    CHECK(all.find("SCREEN TWO") < all.find("SCREEN THREE"));
    CHECK(all.find("SCREEN THREE") < all.find("MAIN PROMPT ->"));
    CHECK(r.n.state() == Node::State::Menu);
    // every row of every screen, once
    for (char c : std::string("abc")) {
        size_t rows = 0;
        for (size_t at = all.find(std::string(78, c)); at != std::string::npos; at = all.find(std::string(78, c), at + 78)) rows++;
        CHECK(rows == 40);
    }
}

TEST_CASE("a slow caller's whole sequence matches what a fast caller gets, byte for byte") {
    Rig fast;
    fast.slow = false;
    fast.type("pass1234\r");
    std::string want = fast.read();
    Rig slow;
    slow.type("pass1234\r");
    std::string got = slow.read() + slow.drain();
    CHECK(got == want);
}

TEST_CASE("keys typed while a slow caller's next login step waits are dropped, not fed to a half-done screen") {
    Rig r;
    r.type("pass1234\r");
    std::string all = r.read();
    for (int i = 0; i < 50 && !has(all, "SCREEN ONE"); i++) {  // screen one out, the line full again behind it
        r.poll();
        all += r.read();
    }
    r.line = std::string(2000, '~');
    r.type("gggg");  // (G is goodbye on most menus)
    CHECK(r.n.state() != Node::State::Done);
    r.read();
    all += r.drain();
    CHECK(has(all, "SCREEN THREE"));
    CHECK(has(all, "MAIN PROMPT ->"));
    CHECK(r.n.state() == Node::State::Menu);
}

TEST_CASE("a menu screen is read from the card a piece at a time, and the prompt comes out behind its art") {
    Rig r;
    r.useMenuArt();
    r.fs["MAIN.ANS"] = big("MENU ART END", 'm');
    r.type("pass1234\r");
    std::string all = r.read() + r.drain();
    REQUIRE(has(all, "MENU ART END"));
    CHECK(all.find("MENU ART END") < all.find("MAIN PROMPT ->"));
    REQUIRE(r.menuOffsets.size() >= 2);
    for (size_t i = 1; i < r.menuOffsets.size(); i++) CHECK(r.menuOffsets[i] == r.menuOffsets[i - 1] + Node::kPosterPiece);
}
