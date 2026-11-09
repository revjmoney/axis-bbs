// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The Saylor poster (37 to 45 KB) streams out a piece at a time through Node::setPoster: one piece per poll(), so
// neither the file nor its ANSI ever sits in RAM whole. Then the pause, then the rest of the login sequence.
#include <algorithm>
#include <map>
#include <regex>
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
std::string plain(const std::string& s) { return std::regex_replace(s, std::regex("\x1b\\[[0-9;?]*[A-Za-z]"), ""); }

// a tall poster: numbered lines, with colour codes, about 40 KB
std::string bigPoster() {
    std::string s;
    for (int i = 0; s.size() < 40000; i++) s += "|12LINE " + std::to_string(i) + " |07of the poster, clocking in with Saylor\r\n";
    return s;
}

struct Rig {
    UserStore                          users;
    std::map<std::string, std::string> fs = {{"MAIN.MNU", "prompt1=MAIN ->\n"}, {"LOGIN.ANS", "LOGIN ART"},
                                             {"LOGIN.SEQ", "step=DISPLAYFILE|0|1|0|NEWS\n"}, {"NEWS.ANS", "NEWS ART"}};
    std::map<std::string, std::string> posters;  // what the theme folder has
    std::vector<size_t>                offsets;  // every piece asked for
    size_t                             biggestPiece = 0, biggestTake = 0;
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
        n.setArt([](const std::string& name) {  // the whole-file path must never be asked for the poster
            CHECK(name != "SAYLOR.ANS");
            CHECK(name != "SAYLORLG.ANS");
            return std::string();
        });
        n.setPoster([this](const std::string& file, size_t off, size_t want, bool* exists) {
            auto it = posters.find(file);
            *exists = it != posters.end();
            if (!*exists) return std::string();
            if (file == "SAYLOR.ANS" || off) offsets.push_back(off);
            std::string piece = off < it->second.size() ? it->second.substr(off, want) : std::string();
            biggestPiece = std::max(biggestPiece, piece.size());
            return piece;
        });
    }
    std::string take() {
        std::string s = n.takeOutput();
        biggestTake = std::max(biggestTake, s.size());
        return s;
    }
    std::string type(const std::string& s) {
        n.input((const uint8_t*)s.data(), s.size(), 1000);
        return take();
    }
    std::string login() {  // the LOGIN screen, handle, password: everything shown from the password on (raw)
        n.setMenus([this](const std::string& f) { return fs.count(f) ? fs[f] : std::string(); });
        n.begin(0);
        take();
        type("Rev\r");
        return type("pass1234\r");
    }
    std::string pump(int turns = 1000) {  // poll() until the poster is out, as the main loop does
        std::string all;
        for (int i = 0; i < turns && n.state() == Node::State::Poster; i++) {
            n.poll(1000);
            all += take();
        }
        return all;
    }
};
}  // namespace

TEST_CASE("the poster streams out a piece per poll, never whole, then pauses and the sequence goes on") {
    Rig r;
    std::string poster = bigPoster();
    r.posters["SAYLOR.ANS"] = poster + "\x1a" + "SAUCE00 not part of the picture";
    std::string first = r.login();
    CHECK(r.n.state() == Node::State::Poster);
    CHECK(first.find("\x1b[2J") != std::string::npos);  // the step clears first
    CHECK(first.find("press a key") == std::string::npos);
    std::string rest = r.pump();
    CHECK(r.n.state() == Node::State::SeqPause);
    std::string out = plain(first + rest);
    // every line, in order, once
    size_t at = 0;
    for (int i = 0;; i++) {
        std::string line = "LINE " + std::to_string(i) + " of the poster";
        if (poster.find(line) == std::string::npos) break;
        size_t found = out.find(line, at);
        REQUIRE(found != std::string::npos);
        at = found + line.size();
    }
    CHECK(out.find("|12") == std::string::npos);  // colour codes became ANSI, even where a piece cut through one
    CHECK(out.find("|07") == std::string::npos);
    CHECK(out.find("SAUCE00") == std::string::npos);
    CHECK(out.find("press a key") != std::string::npos);
    // pieces: small, in order, no gaps
    CHECK(r.biggestPiece <= Node::kPosterPiece);
    CHECK(r.offsets.size() > 10);
    for (size_t i = 1; i < r.offsets.size(); i++) CHECK(r.offsets[i] == r.offsets[i - 1] + Node::kPosterPiece);
    CHECK(r.biggestTake < 3 * Node::kPosterPiece);  // the caller's line never gets (or holds) the whole poster at once
    CHECK(r.type(" ").find("NEWS ART") != std::string::npos);
}

TEST_CASE("a code cut across two pieces still expands") {
    Rig r;
    std::string poster(Node::kPosterPiece - 1, 'x');  // no line break: the long-line path
    poster += "|12RED|07 and %BN done";
    r.posters["SAYLOR.ANS"] = poster;
    std::string out = r.login();
    out += r.pump();
    CHECK(out.find("|12") == std::string::npos);
    CHECK(out.find("\x1b[0;1;31mRED") != std::string::npos);
    CHECK(out.find("%BN") == std::string::npos);
    CHECK(plain(out).find(std::string(Node::kPosterPiece - 1, 'x')) != std::string::npos);
}

TEST_CASE("keys while the poster streams are ignored; the key after it moves on") {
    Rig r;
    r.posters["SAYLOR.ANS"] = bigPoster();
    r.login();
    CHECK(r.n.state() == Node::State::Poster);
    r.type("x");
    CHECK(r.n.state() == Node::State::Poster);
    r.n.poll(1000);
    r.type("\r");
    CHECK(r.n.state() == Node::State::Poster);
    r.pump();
    CHECK(r.n.state() == Node::State::SeqPause);
    CHECK(plain(r.type(" ")).find("NEWS ART") != std::string::npos);
}

TEST_CASE("a small poster comes out in one go, with its pause") {
    Rig r;
    r.posters["SAYLOR.ANS"] = "|14SMALL SAYLOR";
    std::string out = plain(r.login());
    CHECK(out.find("SMALL SAYLOR") != std::string::npos);
    CHECK(out.find("press a key") != std::string::npos);
    CHECK(r.n.state() == Node::State::SeqPause);
    CHECK(r.type(" ").find("NEWS ART") != std::string::npos);
}

TEST_CASE("no SAYLOR.ANS: the tall SAYLORLG.ANS instead") {
    Rig r;
    r.posters["SAYLORLG.ANS"] = bigPoster() + "THE TALL ONE\r\n";
    std::string out = r.login();
    out = plain(out + r.pump());
    CHECK(out.find("THE TALL ONE") != std::string::npos);
    CHECK(out.find("press a key") != std::string::npos);
}

TEST_CASE("a look with no poster skips it quietly: no clear, no pause") {
    Rig r;
    std::string out = r.login();
    CHECK(out.find("\x1b[2J") == std::string::npos);
    CHECK(r.n.state() != Node::State::Poster);
    CHECK(plain(out).find("NEWS ART") != std::string::npos);
}

TEST_CASE("the main going quiet mid-poster ends it where it got to") {
    Rig r;
    std::string poster = bigPoster();
    r.posters["SAYLOR.ANS"] = poster;
    std::string first = r.login();
    r.n.poll(1000);
    first += r.take();
    r.posters.clear();  // (a follower's request for the next piece was not answered)
    std::string rest = r.pump();
    CHECK(r.n.state() == Node::State::SeqPause);
    CHECK(plain(first + rest).find("press a key") != std::string::npos);
}
