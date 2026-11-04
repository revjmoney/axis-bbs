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
        u.security = 20;
        users.all().push_back(u);
    }
};

std::string saltOf(const std::string& reply) { return reply.substr(5, reply.find('\n') - 5); }

std::string proof(const std::string& nonce, const std::string& salt, const std::string& pw) {
    return sha256Hex(nonce + hashPassword(salt, pw));
}
}  // namespace

TEST_CASE("a sysop gets the salt and logs in with the proof") {
    Rig          r;
    PanelSession s(r.cfg, r.users, "n0nce");
    CHECK(s.greeting() == "AXISPANEL 1 n0nce\n");
    std::string salt = saltOf(s.line("SALT Rev"));
    CHECK(salt == r.cfg.sysopSalt);
    CHECK(s.line("AUTH Rev " + proof("n0nce", salt, "sysop1")) == "OK\n");
    CHECK(s.authed());
    CHECK(s.who() == "Rev");
}

TEST_CASE("a non-sysop user is refused") {
    Rig          r;
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
