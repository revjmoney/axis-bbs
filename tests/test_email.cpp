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
namespace fs = std::filesystem;

namespace {
SysInfo     info() { return SysInfo{}; }
uint32_t    rng() { return 0x1234u; }
std::string plain(const std::string& s) { return std::regex_replace(s, std::regex("\x1b\\[[0-9;?]*[A-Za-z]"), ""); }

struct Board {
    fs::path         dir = fs::temp_directory_path() / "axis_email";
    UserStore        users;
    FileMessageStore* st = nullptr;
    Board() {
        fs::remove_all(dir);
        fs::create_directories(dir);
        st = new FileMessageStore(dir.string());
        for (const char* h : {"Kid Chips", "Spindle", "Lurker"}) {
            User u;
            u.handle = h;
            u.salt = makeSalt(rng);
            u.hash = hashPassword(u.salt, "pass1234");
            u.status = User::Status::Validated;
            users.add(u);
        }
    }
    ~Board() {
        delete st;
        fs::remove_all(dir);
    }
};

struct Caller {
    Node n;
    Caller(Board& b, const char* handle, int num = 1) : n(num, info) {
        n.setAccounts(&b.users, [] {}, rng, [] { return 1800000000u; });
        n.setMessages(b.st);
        n.begin(0);
        n.takeOutput();
        login = type(std::string(handle) + "\rpass1234\r");
    }
    std::string login;
    std::string type(const std::string& s) {
        n.input((const uint8_t*)s.data(), s.size(), 1000);
        return plain(n.takeOutput());
    }
};
}  // namespace

TEST_CASE("[E]mail: a caller writes private mail to another user") {
    Board b;
    {
        Caller      kid(b, "Kid Chips");
        std::string box = kid.type("e");
        CHECK(box.find("PRIVATE MAIL") != std::string::npos);
        CHECK(kid.n.state() == Node::State::Email);
        kid.type("s");
        std::string bad = kid.type("Nobody Here\r");
        CHECK(bad.find("No user") != std::string::npos);
        kid.type("s");
        kid.type("spindle\r");  // any case
        kid.type("lathe for sale\r");
        kid.type("still have the Sherline?\r");
        std::string sent = kid.type("/s\r");
        CHECK(sent.find("Sent to Spindle") != std::string::npos);
    }
    Caller spindle(b, "Spindle", 2);
    CHECK(spindle.login.find("1 new") != std::string::npos);  // the notice at login
    std::string box = spindle.type("e");
    CHECK(box.find("lathe for sale") != std::string::npos);
    CHECK(box.find("Kid Chips") != std::string::npos);
    std::string msg = spindle.type("1\r");
    CHECK(msg.find("still have the Sherline?") != std::string::npos);
    Caller lurker(b, "Lurker", 3);
    CHECK(lurker.type("e").find("lathe for sale") == std::string::npos);  // private
    CHECK(lurker.type("1\r").find("Sherline") == std::string::npos);
}

TEST_CASE("mail read once isn't new at the next login, and Q leaves the mailbox") {
    Board b;
    {
        Caller kid(b, "Kid Chips");
        kid.type("e");
        kid.type("s");
        kid.type("Spindle\r");
        kid.type("hi\r");
        kid.type("yo\r");
        kid.type("/s\r");
    }
    {
        Caller s(b, "Spindle", 2);
        s.type("e");
        s.type("1\r");
        s.type("q");
        CHECK(s.n.state() == Node::State::Menu);
    }
    Caller again(b, "Spindle", 2);
    CHECK(again.login.find("new mail") == std::string::npos);
}
