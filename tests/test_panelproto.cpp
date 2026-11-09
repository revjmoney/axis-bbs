// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include "bbs/panelproto.h"

#include "doctest.h"
#include "bbs/password.h"

using namespace axis::bbs;

namespace {
uint32_t rng() { return 0x12345678u; }

struct Rig {
    BbsConfig cfg;
    UserStore users;
    Rig() {
        cfg.sysopHandle = "Rev";
        cfg.sysopSalt = makeSalt(rng);
        cfg.sysopHash = hashPassword(cfg.sysopSalt, "sysop1");
        User u;
        u.handle = "Kid";
        u.salt = makeSalt(rng);
        u.hash = hashPassword(u.salt, "kidpass");
        u.security = 255;
        users.all().push_back(u);
    }
};

std::string saltOf(const std::string& reply) { return reply.substr(5, reply.find(' ') - 5); }
std::string proof(const std::string& nonce, const std::string& salt, const std::string& pw) {
    return sha256Hex(nonce + hashPassword(salt, pw));
}
}  // namespace

TEST_CASE("panel advertises the hash scheme and accepts a PBKDF2 proof") {
    Rig          r;
    PanelSession s(r.cfg, r.users, "n0nce");
    CHECK(s.greeting() == "AXISPANEL 1 n0nce\n");
    std::string response = s.line("SALT Rev");
    CHECK(response == "SALT " + r.cfg.sysopSalt + " PBKDF2-SHA256-" + std::to_string(kPasswordRounds) + "\n");
    CHECK(s.line("AUTH Rev " + proof("n0nce", r.cfg.sysopSalt, "sysop1")) == "OK\n");
    CHECK(s.authed());
    CHECK(s.who() == "Rev");
}

TEST_CASE("panel names a stored hash's own round count (older 200000-round hashes still log in)") {
    Rig r;
    r.cfg.sysopSalt = "00112233445566778899aabbccddeeff";
    r.cfg.sysopHash = "pbkdf2-sha256$200000$8a9c52286c39cb1e25c31ef1abf35c3ffe17f4a5a21e19b8808801ccc95f6ef1";
    PanelSession s(r.cfg, r.users, "n2");
    CHECK(s.line("SALT Rev") == "SALT " + r.cfg.sysopSalt + " PBKDF2-SHA256-200000\n");
    CHECK(s.line("AUTH Rev " + sha256Hex("n2" + r.cfg.sysopHash)) == "OK\n");
}

TEST_CASE("panel advertises legacy SHA-256 for existing stored hashes") {
    Rig r;
    r.cfg.sysopSalt = "0123456789abcdef0123456789abcdef";
    r.cfg.sysopHash = sha256Hex(r.cfg.sysopSalt + "legacy-password");
    PanelSession s(r.cfg, r.users, "legacy-nonce");
    CHECK(s.line("SALT Rev") == "SALT " + r.cfg.sysopSalt + " SHA256\n");
    CHECK(s.line("AUTH Rev " + sha256Hex("legacy-nonce" + r.cfg.sysopHash)) == "OK\n");
}

TEST_CASE("a non-sysop user is refused") {
    Rig r;
    r.users.all()[0].security = 20;
    PanelSession s(r.cfg, r.users, "n");
    CHECK(s.line("SALT Kid") == "SALT -\n");
    CHECK(s.line("AUTH Kid " + proof("n", r.users.all()[0].salt, "kidpass")) == "NO\n");
    CHECK_FALSE(s.authed());
}

TEST_CASE("commands before login are refused") {
    Rig          r;
    PanelSession s(r.cfg, r.users, "n");
    bool         ran = false;
    s.run = [&](const std::string&) {
        ran = true;
        return std::string("x\n");
    };
    CHECK(s.line("CMD @NODES") == "NO\n");
    CHECK_FALSE(ran);
}

TEST_CASE("three bad panel logins close the link and are logged") {
    Rig          r;
    PanelSession s(r.cfg, r.users, "n");
    std::string  logged;
    s.onEvent = [&](const std::string& e) { logged += e; };
    s.line("AUTH Rev bad");
    s.line("AUTH Rev bad");
    CHECK_FALSE(s.closed());
    CHECK(s.line("AUTH Rev bad") == "NO\nBYE\n");
    CHECK(s.closed());
    CHECK(logged.find("panel: 3 bad logins") != std::string::npos);
}

TEST_CASE("long command output arrives whole, ended by the done marker") {
    Rig          r;
    PanelSession s(r.cfg, r.users, "n");
    s.run = [](const std::string& c) { return c == "@LOG" ? std::string(6000, 'x') + "\n" : std::string(); };
    s.line("AUTH Rev " + proof("n", r.cfg.sysopSalt, "sysop1"));
    std::string out = s.line("CMD @LOG");
    CHECK(out.size() == 6001 + 4);
    CHECK(out.substr(out.size() - 4) == "END\n");
}
