// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// A slow caller (a LoRa radio call at a few hundred bytes a second): while its line is backed up, the login sequence starts
// no next step and no menu, so one turn draws at most one screen and the caller's screens never pile up in the board's RAM.
#include <map>
#include <string>

#include "doctest.h"
#include "bbs/node.h"
#include "bbs/password.h"
#include "bbs/users.h"

using namespace axis::bbs;

namespace {
SysInfo     info() { return SysInfo{}; }
uint32_t    rng() { return 0x5A5A5A5Au; }
std::string big(const char* name) { return std::string(3000, '.') + name; }  // a screen that leaves in 2 KB pieces

// The caller's line as main.cpp has it: the node's 2 KB pieces go straight in (setDirect), the rest after the turn
// (sendApp), and it is backed up while 1 KB or more waits (TelnetStream::kHoldLow). The caller reading empties it.
struct Rig {
    UserStore                          users;
    std::map<std::string, std::string> fs = {{"MAIN.MNU", "prompt1=MAIN ->\n"},
                                             {"LOGIN.ANS", "LOGIN ART"},
                                             {"LOGIN.SEQ", "step=DISPLAYFILE|1|0|0|ONE\nstep=DISPLAYFILE|1|0|0|TWO\nstep=DISPLAYFILE|1|0|0|THREE\n"},
                                             {"ONE.ANS", big("SCREEN ONE")},
                                             {"TWO.ANS", big("SCREEN TWO")},
                                             {"THREE.ANS", big("SCREEN THREE")}};
    std::string                        line;  // sent to the caller, not read yet
    bool                               slow = true;
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
    CHECK(r.n.state() == Node::State::Menu);
}

TEST_CASE("a slow caller gets one login screen a turn, each once its line has room, then the menu") {
    Rig r;
    r.type("pass1234\r");
    std::string all, out = r.read();
    CHECK(has(out, "SCREEN ONE"));
    CHECK_FALSE(has(out, "SCREEN TWO"));
    all += out;
    r.line = std::string(2000, '~');  // the caller is slow: the line is still full
    r.poll();
    r.poll();
    CHECK(r.line == std::string(2000, '~'));  // nothing more while it is
    r.read();
    for (const char* want : {"SCREEN TWO", "SCREEN THREE"}) {
        r.poll();
        out = r.read();
        CHECK(has(out, want));
        CHECK(out.size() < 4000);  // one screen, not two
        all += out;
    }
    r.poll();  // the menu after the last step waited for room too: it comes now
    out = r.read();
    CHECK_FALSE(out.empty());
    CHECK_FALSE(has(out, "SCREEN"));
    r.poll();
    CHECK(r.read().empty());  // and nothing after it
    CHECK(all.find("SCREEN ONE") < all.find("SCREEN TWO"));
    CHECK(all.find("SCREEN TWO") < all.find("SCREEN THREE"));
}

TEST_CASE("keys typed while a slow caller's next login step waits are dropped, not fed to a half-done screen") {
    Rig r;
    r.type("pass1234\r");
    CHECK(has(r.line, "SCREEN ONE"));
    r.type("gggg");  // (G is goodbye on most menus)
    CHECK_FALSE(has(r.line, "SCREEN TWO"));
    CHECK(r.n.state() != Node::State::Done);
    r.slow = false;
    r.read();
    r.poll();
    CHECK(has(r.read(), "SCREEN TWO"));
    CHECK(r.n.state() == Node::State::Menu);
}
