// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include <filesystem>
#include <regex>
#include <string>

#include "doctest.h"
#include "bbs/msgbase.h"
#include "bbs/node.h"
#include "bbs/password.h"
#include "bbs/users.h"

using namespace axis::bbs;

namespace {
SysInfo     info() { return SysInfo{}; }
uint32_t    rng() { return 0x5A5A5A5Au; }
std::string plain(const std::string& s) { return std::regex_replace(s, std::regex("\x1b\\[[0-9;]*[A-Za-z]"), ""); }

struct NoFiles : FileLibrary {
    std::vector<FileEntry> list(const std::string&) override { return {}; }
    bool read(const std::string&, const std::string&, std::string&, size_t) override { return false; }
};

struct Rig {
    NoFiles   files;
    UserStore users;
    int       saves = 0;
    Node      node;
    explicit Rig(BbsConfig c = BbsConfig()) : node(1, info, c, &files) {
        User u;
        u.handle = "Kid Chips";
        u.salt = makeSalt(rng);
        u.hash = hashPassword(u.salt, "oldpass1");
        u.status = User::Status::Validated;
        u.realName = "Jesse";
        u.location = "Conway, SC";
        users.add(u);
        node.setAccounts(&users, [this] { saves++; }, rng, [] { return 1800000000u; });
        node.begin(0);
        type("Kid Chips\roldpass1\r");
    }
    std::string type(const std::string& s) {
        node.input((const uint8_t*)s.data(), s.size(), 1000);
        return plain(node.takeOutput());
    }
    User& me() { return *users.find("Kid Chips"); }
};
}  // namespace

TEST_CASE("[U] opens the caller's own account with what the board knows about them") {
    Rig         r;
    std::string out = r.type("u");
    CHECK(out.find("Kid Chips") != std::string::npos);
    CHECK(out.find("Jesse") != std::string::npos);
    CHECK(out.find("Conway, SC") != std::string::npos);
    CHECK(out.find("[T]") != std::string::npos);  // tagline
    CHECK(out.find("[P]") != std::string::npos);  // password
    CHECK(r.node.state() == Node::State::Settings);
}

TEST_CASE("a caller sets their tagline and location; Enter alone keeps a value") {
    Rig r;
    r.type("u");
    int before = r.saves;
    r.type("t");
    std::string out = r.type("Cutting chips since '92\r");
    CHECK(r.me().tagline == "Cutting chips since '92");
    CHECK(out.find("Cutting chips since '92") != std::string::npos);  // the screen shows it
    CHECK(r.saves > before);                                          // saved, and on to every board
    r.type("l");
    r.type("\r");
    CHECK(r.me().location == "Conway, SC");
    r.type("l");
    r.type("Myrtle Beach, SC\r");
    CHECK(r.me().location == "Myrtle Beach, SC");
}

TEST_CASE("a new password needs the old one and the same new one twice") {
    Rig r;
    r.type("u");
    r.type("p");
    std::string out = r.type("wrongpw\r");
    CHECK(out.find("not your password") != std::string::npos);
    CHECK(checkPassword(r.me().salt, r.me().hash, "oldpass1"));
    r.type("p");
    r.type("oldpass1\r");
    r.type("newpass9\r");
    out = r.type("typo9999\r");
    CHECK(out.find("didn't match") != std::string::npos);
    CHECK(checkPassword(r.me().salt, r.me().hash, "oldpass1"));
    r.type("p");
    r.type("oldpass1\r");
    r.type("newpass9\r");
    r.type("newpass9\r");
    CHECK(checkPassword(r.me().salt, r.me().hash, "newpass9"));
    CHECK_FALSE(checkPassword(r.me().salt, r.me().hash, "oldpass1"));
}

TEST_CASE("Q leaves the account screen for the main menu") {
    Rig r;
    r.type("u");
    r.type("q");
    CHECK(r.node.state() == Node::State::Menu);
}

TEST_CASE("the tagline travels with the user record and older records without one still read") {
    User u;
    u.handle = "Kid Chips";
    u.tagline = "chips fly";
    User back;
    REQUIRE(parseUser(formatUser(u), back));
    CHECK(back.tagline == "chips fly");
    User old, plainOld;
    plainOld.handle = "Old Timer";
    REQUIRE(parseUser(formatUser(plainOld), old));
    CHECK(old.tagline.empty());
}

TEST_CASE("the tagline signs the caller's posts") {
    std::filesystem::path d = std::filesystem::temp_directory_path() / "axis_tagline";
    std::filesystem::remove_all(d);
    std::filesystem::create_directories(d);
    {
        FileMessageStore st(d.string());
        Rig              r;
        r.me().tagline = "chips fly";
        r.node.setMessages(&st);
        r.type("m");
        r.type("1\r");
        r.type("p");
        r.type("\r");
        r.type("hello\r");
        r.type("first post\r");
        r.type("/s\r");
        Message m;
        REQUIRE(st.read("general", 1, m));
        CHECK(m.body.find("first post") != std::string::npos);
        CHECK(m.body.find("chips fly") != std::string::npos);
    }
    std::filesystem::remove_all(d);
}

TEST_CASE("areas above a caller's security level don't show up for them, but do for the sysop") {
    BbsConfig c;
    c.areas = {{"axis", "Axis Firmware"}, {"zero", "Zero Day", 150}};
    c.msgAreas = {{"general", "General Chatter"}, {"elite", "Elite Talk", 100}};
    Rig         r(c);  // Kid Chips: security 10
    std::string files = r.type("f");
    CHECK(files.find("Axis Firmware") != std::string::npos);
    CHECK(files.find("Zero Day") == std::string::npos);
    r.me().security = 200;
    r.type("\r");
    CHECK(r.type("f").find("Zero Day") != std::string::npos);
}

TEST_CASE("each login goes on the last callers list, and [L] shows the newest first") {
    std::vector<std::string> calls = {"Old Timer|2|2026-11-03 21:10|Conway, SC"};
    BbsConfig                c;
    UserStore                users;
    User                     u;
    u.handle = "Kid Chips";
    u.salt = makeSalt(rng);
    u.hash = hashPassword(u.salt, "oldpass1");
    u.status = User::Status::Validated;
    u.location = "Myrtle Beach, SC";
    users.add(u);
    Node n(3, info, c);
    n.setAccounts(&users, [] {}, rng, [] { return 1800000000u; });
    n.setLastCallers([&] { return calls; }, [&](const std::string& l) { calls.push_back(l); });
    n.begin(0);
    n.input((const uint8_t*)"Kid Chips\roldpass1\r", 19, 1000);
    REQUIRE(calls.size() == 2);
    CHECK(calls[1].find("Kid Chips|3|") == 0);
    CHECK(calls[1].find("Myrtle Beach, SC") != std::string::npos);
    n.takeOutput();
    n.input((const uint8_t*)"l", 1, 1000);
    std::string out = plain(n.takeOutput());
    size_t      kid = out.find("Kid Chips"), old = out.find("Old Timer");
    REQUIRE(kid != std::string::npos);
    REQUIRE(old != std::string::npos);
    CHECK(kid < old);  // newest first
    CHECK(out.find("Conway, SC") != std::string::npos);
}
