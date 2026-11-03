// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include <regex>
#include <string>

#include "doctest.h"
#include "bbs/node.h"
#include "bbs/password.h"
#include "bbs/users.h"

using namespace axis::bbs;

namespace {
SysInfo  info() { return SysInfo{}; }
uint32_t rng() { return 0x5A5A5A5Au; }
std::string plain(const std::string& s) { return std::regex_replace(s, std::regex("\x1b\\[[0-9;]*m"), ""); }

struct Rig {
    UserStore users;
    int       saves = 0;
    BbsConfig cfg;
    Node      node;
    explicit Rig(BbsConfig c = BbsConfig()) : cfg(c), node(1, info, c) {
        node.setAccounts(&users, [this] { saves++; }, rng, [] { return 1800000000u; });
        node.begin(0);
        node.takeOutput();
    }
    std::string type(const std::string& s) {
        node.input((const uint8_t*)s.data(), s.size(), 1000);
        return plain(node.takeOutput());
    }
};

User validUser(const char* handle, const char* pw, User::Status st) {
    User u;
    u.handle = handle;
    u.salt = makeSalt(rng);
    u.hash = hashPassword(u.salt, pw);
    u.status = st;
    return u;
}

// every answer for an applicant who owns a CNC
const char* kFullApplication =
    "secret99\rsecret99\r"   // password twice
    "Jesse\r"                // first name
    "Conway, SC\r"           // location
    "\r"                     // email: skipped
    "a flyer at the shop\r"  // heard about the board
    "y"                      // own a CNC
    "Shapeoko 4, grbl\r"     // machine and controller
    "signs and gun grips\r"  // what they cut
    "y"                      // rotary axis
    "y"                      // know RotatoCAM
    "n"                      // RotatoCAM Pro user
    "y"                      // been to rotatocam.com
    "n"                      // been to jmscnc.com
    "Fusion\r"               // CAM
    "Night Owl BBS\r"        // first BBS
    "ACiD\r"                 // best ANSI group
    "SyncTERM\r"             // terminal
    "toolpaths\r"            // uploads
    "I cut chips daily\r"    // why
    "y"                      // agree to the rules
    "y";                     // submit
}  // namespace

TEST_CASE("an unknown handle is offered the new user application") {
    Rig         r;
    std::string out = r.type("Chip Breaker\r");
    CHECK(out.find("Apply") != std::string::npos);
    r.type("n");
    CHECK(r.node.state() == Node::State::Login);
}

// an applicant with no machine who isn't a RotatoCAM Pro user: the sysop decides
const char* kNoMachineApplication =
    "secret99\rsecret99\rSam\rMyrtle Beach\r\rfriend\r"
    "n"           // own a CNC (router, mill, lathe, laser, 3D printer)
    "n"           // know anyone with one
    "n"           // rotary axis
    "n"           // know RotatoCAM (so no Pro question)
    "n"           // been to rotatocam.com
    "n"           // been to jmscnc.com
    "none\rnone\rACiD\rSyncTERM\rnothing yet\rI want to learn\r"
    "y"           // agree
    "y";          // submit

TEST_CASE("someone with no machine who isn't a Pro user waits for the sysop") {
    Rig r;
    r.type("Sam\ry");
    std::string out = r.type(kNoMachineApplication);
    CHECK(out.find("PENDING VERIFICATION") != std::string::npos);
    REQUIRE(r.users.all().size() == 1);
    CHECK(r.users.all()[0].status == User::Status::Pending);
    CHECK(r.node.state() == Node::State::Pending);
}

TEST_CASE("owning a machine gets you in right away") {
    Rig r;
    r.type("Chip Breaker\ry");
    std::string out = r.type(kFullApplication);
    CHECK(out.find("ACCESS GRANTED") != std::string::npos);
    REQUIRE(r.users.all().size() == 1);
    CHECK(r.users.all()[0].status == User::Status::Validated);
    CHECK(r.node.state() == Node::State::Menu);
}

TEST_CASE("a RotatoCAM Pro user gets in right away, machine or not") {
    Rig         r;
    std::string keys = kNoMachineApplication;
    keys.replace(keys.find("nnnn"), 4, "nnny" "y");  // knows RotatoCAM: yes, then Pro user: yes
    r.type("Pro User\ry");
    std::string out = r.type(keys);
    CHECK(out.find("ACCESS GRANTED") != std::string::npos);
    REQUIRE(r.users.all().size() == 1);
    CHECK(r.users.all()[0].status == User::Status::Validated);
}

TEST_CASE("the application asks about CNC, RotatoCAM and the scene") {
    Rig         r;
    std::string all = r.type("Chip Breaker\r");  // one call per statement: the order of a + b + c is unspecified
    all += r.type("y");
    all += r.type(kFullApplication);
    CHECK(all.find("N E W   U S E R") != std::string::npos);
    CHECK(all.find("own a CNC machine") != std::string::npos);
    CHECK(all.find("RotatoCAM") != std::string::npos);
    CHECK(all.find("rotatocam.com") != std::string::npos);
    REQUIRE(r.users.all().size() == 1);
    const User& u = r.users.all()[0];
    CHECK(u.handle == "Chip Breaker");
    CHECK(u.status == User::Status::Validated);  // owns a machine
    CHECK(u.realName == "Jesse");
    CHECK(u.location == "Conway, SC");
    CHECK(checkPassword(u.salt, u.hash, "secret99"));
    bool machine = false, rotato = false;
    for (auto& a : u.answers) {
        machine = machine || a.second == "Shapeoko 4, grbl";
        rotato = rotato || (a.first.find("rotatocam.com") != std::string::npos && a.second == "yes");
    }
    CHECK(machine);
    CHECK(rotato);
    CHECK(r.saves >= 1);
}

TEST_CASE("someone without a CNC is asked whether they know someone who has one") {
    Rig         r;
    std::string all = r.type("Newbie\ry");
    all += r.type("secret99\rsecret99\rSam\rMyrtle Beach\r\rfriend\r");
    all += r.type("n");
    CHECK(all.find("know anyone") != std::string::npos);
}

TEST_CASE("passwords are never echoed and must match") {
    Rig         r;
    std::string all = r.type("Echo Test\ry");
    all += r.type("secret99\rdifferent\r");
    CHECK(all.find("secret99") == std::string::npos);
    CHECK(all.find("didn't match") != std::string::npos);
}

TEST_CASE("a pending caller gets the pending screen and no files or doors") {
    Rig r;
    r.users.add(validUser("Waiting", "secret99", User::Status::Pending));
    std::string out = r.type("waiting\rsecret99\r");
    CHECK(out.find("PENDING VERIFICATION") != std::string::npos);
    CHECK(r.node.state() == Node::State::Pending);
    r.type("f");
    CHECK(r.node.state() == Node::State::Pending);
}

TEST_CASE("a validated caller reaches the main menu, and the call is counted") {
    Rig r;
    r.users.add(validUser("Machinist", "secret99", User::Status::Validated));
    r.type("MACHINIST\rsecret99\r");
    CHECK(r.node.state() == Node::State::Menu);
    CHECK(r.users.find("machinist")->calls == 1);
}

TEST_CASE("three wrong passwords hang up") {
    Rig r;
    r.users.add(validUser("Machinist", "secret99", User::Status::Validated));
    r.type("Machinist\rnope1\rnope2\rnope3\r");
    CHECK(r.node.wantsHangup());
}

TEST_CASE("a denied caller is told so and dropped") {
    Rig r;
    r.users.add(validUser("Lamer", "secret99", User::Status::Denied));
    std::string out = r.type("Lamer\rsecret99\r");
    CHECK(out.find("denied") != std::string::npos);
    CHECK(r.node.wantsHangup());
}

TEST_CASE("the sysop logs in with the sysop password from setup") {
    BbsConfig c;
    c.sysopHandle = "Rev";
    c.sysopSalt = makeSalt(rng);
    c.sysopHash = hashPassword(c.sysopSalt, "sysop123");
    Rig r(c);
    r.type("rev\rsysop123\r");
    CHECK(r.node.state() == Node::State::Menu);
    CHECK(r.node.isSysop());
}
