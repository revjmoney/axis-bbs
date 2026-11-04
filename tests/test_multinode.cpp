// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include <regex>
#include <string>
#include <vector>

#include "doctest.h"
#include "bbs/node.h"

using namespace axis::bbs;

namespace {
SysInfo     info() { return SysInfo{}; }
std::string plain(const std::string& s) { return std::regex_replace(s, std::regex("\x1b\\[[0-9;?]*[A-Za-z]"), ""); }

// two (or three) callers on one BBS: what one node sends reaches the others like the cluster would
struct Floor {
    std::vector<Node*> nodes;
    void link(Node& n) {
        nodes.push_back(&n);
        n.setChat([](const std::string&, bool) {},
                  [this] {
                      std::vector<Node::WhoLine> v;
                      for (Node* o : nodes) v.push_back({o->number(), o->handle(), o->available() ? "Menu" : "Menu (DND)"});
                      return v;
                  });
        n.setNodeLink([this, &n](int kind, int to, const std::string& text) {
            for (Node* o : nodes)
                if (o != &n && (to == 0 || o->number() == to)) o->nodeIn(kind, n.number(), n.handle(), text);
        });
    }
};

struct Caller {
    Node n;
    Caller(int number, const char* handle, Floor& f, bool sysop = false) : n(number, info) {
        f.link(n);
        if (sysop) n.beginLocal(0);
        else {
            n.begin(0);
            type(std::string(handle) + "\r");
        }
        n.takeOutput();
    }
    std::string type(const std::string& s) {
        n.input((const uint8_t*)s.data(), s.size(), 1000);
        return n.takeOutput();
    }
    std::string out() { return n.takeOutput(); }
};
}  // namespace

TEST_CASE("[N] opens the MultiNode menu with the Oblivion/2 node commands") {
    Floor       f;
    Caller      a(1, "Kid", f);
    std::string out = plain(a.type("n"));
    for (const char* k : {"[M]", "[B]", "[S]", "[A]", "[W]"}) CHECK(out.find(k) != std::string::npos);
    CHECK(a.n.state() == Node::State::MultiNode);
    a.type("q");
    CHECK(a.n.state() == Node::State::Menu);
}

TEST_CASE("a caller sends one node a private message; only that node sees it") {
    Floor  f;
    Caller a(1, "Kid", f), b(2, "Spindle", f), c(3, "Chips", f);
    a.type("n");
    a.type("m");
    a.type("2\r");
    std::string sent = plain(a.type("meet me in LORD\r"));
    CHECK(sent.find("Sent to node 2") != std::string::npos);
    std::string got = plain(b.out());
    CHECK(got.find("Kid") != std::string::npos);
    CHECK(got.find("meet me in LORD") != std::string::npos);
    CHECK(plain(c.out()).find("meet me in LORD") == std::string::npos);
}

TEST_CASE("a broadcast reaches every node except one marked unavailable") {
    Floor  f;
    Caller a(1, "Kid", f), b(2, "Spindle", f), c(3, "Chips", f);
    c.type("n");
    std::string dnd = plain(c.type("a"));  // available for chat: off
    CHECK(dnd.find("not available") != std::string::npos);
    CHECK_FALSE(c.n.available());
    c.out();
    a.type("n");
    a.type("b");
    a.type("new ANSI pack up\r");
    CHECK(plain(b.out()).find("new ANSI pack up") != std::string::npos);
    CHECK(plain(c.out()).find("new ANSI pack up") == std::string::npos);
}

TEST_CASE("a node marked unavailable can't get a private message; the sender is told") {
    Floor  f;
    Caller a(1, "Kid", f), b(2, "Spindle", f);
    b.type("n");
    b.type("a");
    a.type("n");
    a.type("m");
    std::string out = plain(a.type("2\r"));
    CHECK(out.find("not available") != std::string::npos);
    CHECK(a.n.state() == Node::State::MultiNode);
}

TEST_CASE("the sysop can force a node into the teleconference; callers can't") {
    Floor  f;
    Caller sys(1, "Sysop", f, true), b(2, "Spindle", f), c(3, "Chips", f);
    c.type("n");
    std::string out = plain(c.type("f"));
    CHECK(out.find("sysop") != std::string::npos);  // only the sysop has [F]
    sys.type("n");
    sys.type("f");
    sys.type("2\r");
    CHECK(b.n.state() == Node::State::Chat);
}

TEST_CASE("split-screen chat: asked, accepted, keystrokes land in the other caller's top half, Esc ends it for both") {
    Floor  f;
    Caller a(1, "Kid", f), b(2, "Spindle", f);
    a.type("n");
    a.type("s");
    std::string wait = plain(a.type("2\r"));
    CHECK(wait.find("Waiting for node 2") != std::string::npos);
    std::string ask = plain(b.out());
    CHECK(ask.find("split") != std::string::npos);
    CHECK(ask.find("Kid") != std::string::npos);
    b.type("y");
    CHECK(a.n.state() == Node::State::Split);
    CHECK(b.n.state() == Node::State::Split);
    a.out();
    b.out();
    std::string mine = a.type("hi");
    CHECK(mine.find("\x1b[14;") != std::string::npos);  // my half: the bottom (row 14 down)
    std::string theirs = b.out();
    CHECK(theirs.find("\x1b[2;") != std::string::npos);  // their half: the top (row 2 down)
    CHECK(plain(theirs).find("h") != std::string::npos);
    CHECK(plain(theirs).find("i") != std::string::npos);
    a.type("\x1b");
    CHECK(a.n.state() == Node::State::Menu);
    CHECK(b.n.state() == Node::State::Menu);
}

TEST_CASE("a declined split chat says so and leaves both callers where they were") {
    Floor  f;
    Caller a(1, "Kid", f), b(2, "Spindle", f);
    a.type("n");
    a.type("s");
    a.type("2\r");
    b.out();
    b.type("n");  // no thanks
    CHECK(plain(a.out()).find("declined") != std::string::npos);
    CHECK(a.n.state() == Node::State::MultiNode);
    CHECK(b.n.state() == Node::State::Menu);
}

TEST_CASE("a note that arrives during a door waits and shows at the next menu") {
    Floor  f;
    Caller a(1, "Kid", f), b(2, "Spindle", f);
    b.n.nodeIn(Node::kNodeNote, 1, "Kid", "wake up");  // b at the menu: it shows at once
    CHECK(plain(b.out()).find("wake up") != std::string::npos);
}
