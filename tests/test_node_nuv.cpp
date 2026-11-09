// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// New user voting on the node: [A] on the menu, CHECKNUV in the login sequence, the pending
// applicant's tally, and the vote letting someone in or turning them down.
#include <map>
#include <regex>
#include <string>
#include <vector>

#include "doctest.h"
#include "bbs/node.h"
#include "bbs/nuv.h"
#include "bbs/password.h"
#include "bbs/users.h"

using namespace axis::bbs;

namespace {
SysInfo     info() { return SysInfo{}; }
uint32_t    rng() { return 0x5A5A5A5Au; }
std::string plain(const std::string& s) { return std::regex_replace(s, std::regex("\x1b\\[[0-9;?]*[A-Za-z]"), ""); }
bool        has(const std::string& s, const char* what) { return s.find(what) != std::string::npos; }

User person(const char* handle, User::Status st, int security = 20) {
    User u;
    u.handle = handle;
    u.salt = makeSalt(rng);
    u.hash = hashPassword(u.salt, "secret99");
    u.status = st;
    u.security = security;
    return u;
}

User applicant() {
    User a = person("cnc4life", User::Status::Pending, 10);
    a.location = "Dothan, AL";
    a.answers = {{"First name", "Dale"}, {"Email", "dale@example.com"}, {"Who has one", "my uncle, a Shapeoko 4"}, {"Why let them in", "rotary carving"}};
    return a;
}

struct Rig {
    UserStore                          users;
    BbsConfig                          cfg;
    std::map<std::string, std::string> fs;
    std::vector<std::string>           events;
    int                                saves = 0;
    std::vector<std::string>           warriors;
    std::unique_ptr<Node>              n;
    explicit Rig(int yes = 3, int no = 3, int level = 20) {
        cfg.nuvYes = yes;
        cfg.nuvNo = no;
        cfg.nuvLevel = level;
    }
    void start(bool menuFiles = false) {
        n.reset(new Node(1, info, cfg));
        n->setAccounts(&users, [this] { saves++; }, rng, [] { return 1800000000u; });
        n->setWarrior([this](const std::string& h) {
            warriors.push_back(h);
            return true;
        });
        n->onEvent = [this](const std::string& e) { events.push_back(e); };
        if (menuFiles) n->setMenus([this](const std::string& f) { return fs.count(f) ? fs[f] : std::string(); });
        n->begin(0);
        n->takeOutput();
    }
    std::string type(const std::string& s) {
        n->input((const uint8_t*)s.data(), s.size(), 1000);
        return plain(n->takeOutput());
    }
    std::string login(const char* handle) {
        std::string out = type(std::string(handle) + "\rsecret99\r");
        while (n->state() == Node::State::SeqPause) out += type(" ");
        return out;
    }
};
}  // namespace

TEST_CASE("a voter sees who's waiting at login, reads the answers with [A], and the third yes lets them in") {
    Rig r;
    r.users.add(person("ChipBreaker", User::Status::Validated));
    r.users.add(applicant());
    User& a = *r.users.find("cnc4life");
    a.votes = {{"EndmillEd", true, ""}, {"RotaryRick", true, "good guy"}};
    r.start();
    CHECK(has(r.login("ChipBreaker"), "1 new user is waiting on the vote: [A] votes."));
    std::string screen = r.type("a");
    CHECK(r.n->state() == Node::State::Nuv);
    CHECK(has(screen, "NEW USER VOTING"));
    CHECK(has(screen, "cnc4life"));
    CHECK(has(screen, "Dothan, AL"));
    CHECK(has(screen, "2 yes, 0 no"));
    CHECK(has(screen, "3 yes gets them in"));
    CHECK(has(screen, "my uncle, a Shapeoko 4"));
    CHECK_FALSE(has(screen, "Dale"));  // the name and email are the sysop's alone
    CHECK_FALSE(has(screen, "dale@example.com"));
    CHECK(has(r.type("y"), "A word for the other voters"));
    CHECK(has(r.type("seems legit\r"), "That's 3 yes. cnc4life is in."));
    CHECK(a.status == User::Status::Validated);
    CHECK(a.security == 20);
    CHECK(nuvVoteBy(a, "chipbreaker")->comment == "seems legit");
    CHECK(r.saves > 0);
    REQUIRE(r.warriors.size() == 1);
    CHECK(r.warriors[0] == "cnc4life");
    REQUIRE_FALSE(r.events.empty());
    CHECK(r.events.back() == "nuv: ChipBreaker voted yes on cnc4life: in by the vote");
    r.type(" ");
    CHECK(r.n->state() == Node::State::Menu);  // nobody else waiting: back to the menu
}

TEST_CASE("a no vote is counted, skip and quit leave the applicant waiting") {
    Rig r;
    r.users.add(person("ChipBreaker", User::Status::Validated));
    r.users.add(applicant());
    User b = person("Second", User::Status::Pending, 10);
    r.users.add(b);
    r.start();
    r.login("ChipBreaker");
    r.type("a");
    r.type("n");
    CHECK(has(r.type("\r"), "Counted. cnc4life has 0 yes, 1 no."));
    CHECK(has(r.type(" "), "Second"));  // on to the next
    r.type("s");
    CHECK(r.n->state() == Node::State::Menu);  // skipped the last one
    CHECK(r.users.find("Second")->votes.empty());
    CHECK(r.users.find("cnc4life")->status == User::Status::Pending);
    CHECK(has(r.type("a"), "Second"));  // the skipped one is still there; cnc4life is not (already voted)
    r.type("q");
    CHECK(r.n->state() == Node::State::Menu);
}

TEST_CASE("the no count turns an applicant down, and they hear it was the vote") {
    Rig r(3, 1);
    r.users.add(person("ChipBreaker", User::Status::Validated));
    r.users.add(applicant());
    r.start();
    r.login("ChipBreaker");
    r.type("a");
    r.type("n");
    CHECK(has(r.type("lamer\r"), "That's 1 no. cnc4life is turned down."));
    CHECK(r.users.find("cnc4life")->status == User::Status::Denied);
    Rig back(3, 1);
    back.users = r.users;
    back.start();
    CHECK(has(back.login("cnc4life"), "The new user vote turned you down."));
    CHECK(back.n->wantsHangup());
}

TEST_CASE("the applicant's pending screen shows their tally") {
    Rig r;
    r.users.add(applicant());
    r.users.find("cnc4life")->votes = {{"x", true, ""}};
    r.start();
    std::string out = r.login("cnc4life");
    CHECK(has(out, "PENDING VERIFICATION"));
    CHECK(has(out, "1 yes, 0 no (3 yes gets you in)"));
    CHECK(has(out, "the callers vote on you"));
}

TEST_CASE("who can't vote is told why, and with voting off the menu has no [A]") {
    Rig r(3, 3, 50);  // only security 50 and up vote
    r.users.add(person("Newbie", User::Status::Validated, 20));
    r.users.add(applicant());
    r.start();
    CHECK_FALSE(has(r.login("Newbie"), "waiting on the vote"));
    CHECK(has(r.type("a"), "Your account can't vote on new users yet."));
    CHECK(r.users.find("cnc4life")->votes.empty());

    Rig off(0);
    off.users.add(person("ChipBreaker", User::Status::Validated));
    off.users.add(applicant());
    off.start();
    CHECK_FALSE(has(off.login("ChipBreaker"), "[A]"));
    CHECK(has(off.type("a"), "New user voting is off here"));

    Rig empty;
    empty.users.add(person("ChipBreaker", User::Status::Validated));
    empty.start();
    empty.login("ChipBreaker");
    CHECK(has(empty.type("a"), "Nobody is waiting on the vote."));
}

TEST_CASE("menu files: CHECKNUV in the login sequence asks first, and no goes on with the login") {
    for (bool vote : {true, false}) {
        Rig r;
        r.fs = {{"MAIN.MNU", "prompt1=MAIN ->\ncmd=A|RUN:NUV|*|0|\n"}, {"MAIN.ANS", "MAIN ART"}};
        r.users.add(person("ChipBreaker", User::Status::Validated));
        r.users.add(applicant());
        r.start(true);
        std::string out = r.type("ChipBreaker\rsecret99\r");
        while (r.n->state() == Node::State::SeqPause) out += r.type(" ");
        CHECK(has(out, "1 new user is waiting on the vote. Vote now?"));
        CHECK(r.n->state() == Node::State::Nuv);
        if (vote) {
            CHECK(has(r.type("y"), "cnc4life"));
            r.type("y");
            r.type("\r");
            out = r.type(" ");
        } else {
            out = r.type("n");
        }
        while (r.n->state() == Node::State::SeqPause) out += r.type(" ");
        CHECK(has(out, "MAIN ART"));  // the login went on to the menu
        CHECK(r.n->state() == Node::State::Menu);
        CHECK(r.users.find("cnc4life")->votes.size() == (vote ? 1u : 0u));
    }
}

TEST_CASE("menu files: RUN:NUV on a menu key opens the voting") {
    Rig r;
    r.fs = {{"MAIN.MNU", "prompt1=MAIN ->\ncmd=X|RUN:NUV|*|0|\n"}, {"MAIN.ANS", "MAIN ART"}};
    r.users.add(person("ChipBreaker", User::Status::Validated));
    r.users.add(applicant());
    r.start(true);
    r.type("ChipBreaker\rsecret99\r");
    while (r.n->state() != Node::State::Menu && r.n->state() != Node::State::Done) r.type(r.n->state() == Node::State::Nuv ? "n" : " ");
    CHECK(has(r.type("x"), "cnc4life"));
    CHECK(r.n->state() == Node::State::Nuv);
}

TEST_CASE("the sysop votes too") {
    Rig r(1);
    r.cfg.sysopHandle = "Rev";
    r.cfg.sysopSalt = makeSalt(rng);
    r.cfg.sysopHash = hashPassword(r.cfg.sysopSalt, "secret99");
    r.users.add(applicant());
    r.start();
    r.login("Rev");
    r.type("a");
    r.type("y");
    CHECK(has(r.type("\r"), "cnc4life is in."));
    CHECK(nuvVoteBy(*r.users.find("cnc4life"), "Rev"));
}
