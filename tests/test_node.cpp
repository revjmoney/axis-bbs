// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include <regex>

#include "doctest.h"
#include "bbs/login_art.h"
#include "bbs/node.h"

using axis::bbs::Node;
using axis::bbs::SysInfo;

static std::string plain(const std::string& s) { return std::regex_replace(s, std::regex("\x1b\\[[0-9;]*m"), ""); }

static SysInfo info() {
    SysInfo s;
    s.ip = "192.168.1.77";
    s.host = "axis.local";
    s.wifi = "TestNet";
    s.rssi = -40;
    s.uptimeSec = 3725;
    s.freeHeap = 150000;
    s.timeText = "2026-11-02 22:30";
    s.storage = "SD card 7580 MB";
    return s;
}
static void type(Node& n, const std::string& s, uint32_t now = 1000) {
    n.input((const uint8_t*)s.data(), s.size(), now);
}

TEST_CASE("node greets with the AXIS welcome and asks for a handle") {
    Node n(1, info);
    n.begin(0);
    std::string out = plain(n.takeOutput());
    CHECK(out.find("Axis Micro BBS Firmware") != std::string::npos);
    CHECK(out.find("Handle:") != std::string::npos);
    CHECK(n.state() == Node::State::Login);
}

TEST_CASE("node echoes the handle, supports backspace, then shows the menu") {
    Node n(1, info);
    n.begin(0);
    plain(n.takeOutput());
    type(n, "Rew");
    CHECK(plain(n.takeOutput()) == "Rew");
    type(n, "\b");
    CHECK(plain(n.takeOutput()) == "\b \b");
    type(n, "v\r");
    std::string out = plain(n.takeOutput());
    CHECK(n.handle() == "Rev");
    CHECK(n.state() == Node::State::Menu);
    CHECK(out.find("Rev") != std::string::npos);
    CHECK(out.find("[C]") != std::string::npos);
    CHECK(out.find("[G]") != std::string::npos);
}

TEST_CASE("node asks again for an empty handle") {
    Node n(1, info);
    n.begin(0);
    plain(n.takeOutput());
    type(n, "   \r");
    CHECK(n.state() == Node::State::Login);
    CHECK(plain(n.takeOutput()).find("Handle:") != std::string::npos);
}

TEST_CASE("node caps the handle length at 30 characters") {
    Node n(1, info);
    n.begin(0);
    type(n, std::string(40, 'x') + "\r");
    CHECK(n.handle().size() == 30);
}

static Node loggedIn() {
    Node n(1, info);
    n.begin(0);
    type(n, "Rev\r");
    plain(n.takeOutput());
    return n;
}

TEST_CASE("credits name Rev J Money, rotatocam.com and jmscnc.com") {
    Node n = loggedIn();
    type(n, "c");
    std::string out = plain(n.takeOutput());
    CHECK(out.find("Rev. J. Money") != std::string::npos);
    CHECK(out.find("rotatocam.com") != std::string::npos);
    CHECK(out.find("jmscnc.com") != std::string::npos);
    CHECK(out.find("GPL") != std::string::npos);
    CHECK(out.find("Axis Micro BBS Firmware") != std::string::npos);
}

TEST_CASE("info shows the node's address and uptime") {
    Node n = loggedIn();
    type(n, "I");
    std::string out = plain(n.takeOutput());
    CHECK(out.find("192.168.1.77") != std::string::npos);
    CHECK(out.find("axis.local") != std::string::npos);
    CHECK(out.find("1h 2m") != std::string::npos);
    CHECK(out.find("SD card 7580 MB") != std::string::npos);
}

TEST_CASE("goodbye logs off and asks to hang up") {
    Node n = loggedIn();
    CHECK_FALSE(n.wantsHangup());
    type(n, "g");
    CHECK(plain(n.takeOutput()).find("Goodbye") != std::string::npos);
    CHECK(n.wantsHangup());
    CHECK(n.state() == Node::State::Done);
}

TEST_CASE("idle callers are logged off after 5 minutes") {
    Node n = loggedIn();
    n.poll(1000 + 299000);
    CHECK_FALSE(n.wantsHangup());
    n.poll(1000 + 300001);
    CHECK(n.wantsHangup());
    CHECK(plain(n.takeOutput()).find("idle") != std::string::npos);
}

TEST_CASE("unknown menu keys redraw the menu prompt") {
    Node n = loggedIn();
    type(n, "z");
    CHECK(plain(n.takeOutput()).find("Command") != std::string::npos);
}

TEST_CASE("busy screen tells the caller the node is busy") {
    CHECK(axis::bbs::busyScreen().find("busy") != std::string::npos);
}

TEST_CASE("menu shows Doors") {
    Node n(1, info);
    n.begin(0);
    type(n, "Rev\r");
    CHECK(plain(n.takeOutput()).find("[D]") != std::string::npos);
}

TEST_CASE("D lists the configured doors") {
    Node n = loggedIn();
    type(n, "d");
    std::string out = plain(n.takeOutput());
    CHECK(out.find("Legend of the Red Dragon") != std::string::npos);
    CHECK(out.find("[1]") != std::string::npos);
    CHECK(n.state() == Node::State::Doors);
}

TEST_CASE("choosing LORD requests the door") {
    Node n = loggedIn();
    type(n, "d1");
    CHECK(n.state() == Node::State::InDoor);
    CHECK(n.doorRequested() == "lord");
    CHECK(n.door().exe == "LORD.EXE");
}

TEST_CASE("a key that is not a door goes back to the menu") {
    Node n = loggedIn();
    type(n, "dq");
    CHECK(n.state() == Node::State::Menu);
    CHECK(n.doorRequested().empty());
}

TEST_CASE("after the door the caller is back at the menu") {
    Node n = loggedIn();
    type(n, "d1");
    plain(n.takeOutput());
    n.resumeAfterDoor(Node::DoorEnd::Normal, 50000);
    CHECK(n.state() == Node::State::Menu);
    CHECK(n.doorRequested().empty());
    CHECK(plain(n.takeOutput()).find("Command:") != std::string::npos);
}

TEST_CASE("door out of memory returns the caller to the menu") {
    Node n = loggedIn();
    type(n, "d1");
    plain(n.takeOutput());
    n.resumeAfterDoor(Node::DoorEnd::OutOfMemory, 50000);
    std::string out = plain(n.takeOutput());
    CHECK(out.find("out of memory") != std::string::npos);
    CHECK(n.state() == Node::State::Menu);
}

TEST_CASE("a long door does not count as idle time") {
    Node n = loggedIn();
    type(n, "d1", 1000);
    n.poll(20 * 60000);  // 20 minutes in the door
    CHECK(n.state() == Node::State::InDoor);
    n.resumeAfterDoor(Node::DoorEnd::Normal, 20 * 60000);
    n.poll(20 * 60000 + 1000);
    CHECK(n.state() == Node::State::Menu);
}

static SysInfo fullInfo() {
    SysInfo s = info();
    s.callsToday = 7;
    s.nodesTotal = 1;
    s.nodes = {"Rev"};
    s.hardware = "ESP32 CYD 240 MHz";
    return s;
}

TEST_CASE("login screen shows the drippy logo and the board's details") {
    axis::bbs::BbsConfig cfg;
    cfg.sysopHandle = "Rev";
    cfg.location = "The Workshop";
    Node n(1, fullInfo, cfg);
    n.begin(0);
    std::string raw = n.takeOutput(), out = plain(raw);
    CHECK(raw.find(kLoginArt) != std::string::npos);  // the drippy logo from art/login.ans, as is
    CHECK(out.find("The Workshop") != std::string::npos);
    CHECK(out.find("axis.local") != std::string::npos);
    CHECK(out.find("ESP32 CYD") != std::string::npos);
    CHECK(out.find("calls today 7") != std::string::npos);
    CHECK(out.find(axis::bbs::kAxisVersion) != std::string::npos);
}

TEST_CASE("login screen lists the active nodes") {
    Node n(1, fullInfo);
    n.begin(0);
    std::string out = plain(n.takeOutput());
    CHECK(out.find("1 of 1") != std::string::npos);
    CHECK(out.find("you") != std::string::npos);
}

TEST_CASE("login screen credits jmscnc.com and rotatocam.com") {
    Node n(1, fullInfo);
    n.begin(0);
    std::string out = plain(n.takeOutput());
    CHECK(out.find("jmscnc.com") != std::string::npos);
    CHECK(out.find("rotatocam.com") != std::string::npos);
}

TEST_CASE("login screen fits a 25 line terminal") {
    Node n(1, fullInfo);
    n.begin(0);
    std::string out = plain(n.takeOutput());
    size_t lines = 0;
    for (size_t i = out.find("\x1b[2J"); i < out.size(); i++) lines += out[i] == '\n';
    CHECK(lines <= 24);
}

TEST_CASE("with a direct line the login art goes straight out, never copied into the node's output") {
    Node        plainNode(1, info);
    plainNode.begin(0);
    std::string whole = plainNode.takeOutput();

    Node        n(1, info);
    std::string direct;
    n.setDirect([&](const char* p, size_t len) { direct.append(p, len); });
    n.begin(0);
    std::string rest = n.takeOutput();
    CHECK(direct + rest == whole);  // the same screen, in the same order
    CHECK(direct.find(kLoginArt) != std::string::npos);
    CHECK(rest.find(kLoginArt) == std::string::npos);
    CHECK(rest.size() < 4096);  // what the node holds stays small
}

namespace {
struct ChatRig {
    Node                                    n = loggedIn();
    std::vector<std::pair<std::string, bool>> said;
    ChatRig() {
        n.setChat([this](const std::string& text, bool page) { said.push_back({text, page}); },
                  [] {
                      return std::vector<Node::WhoLine>{{1, "Rev", "Chat"}, {2, "Spindle Kid", "Files"}, {3, "", ""}};
                  });
        n.takeOutput();
    }
};
}  // namespace

TEST_CASE("the menu offers the teleconference, who's online and a page for the sysop") {
    Node n = loggedIn();
    type(n, "c");  // credits, then the menu again
    std::string out = plain(n.takeOutput());
    CHECK(out.find("[T]") != std::string::npos);
    CHECK(out.find("[W]") != std::string::npos);
    CHECK(out.find("[P]") != std::string::npos);
}

TEST_CASE("in the teleconference a typed line goes to every node and lines from others show up") {
    ChatRig r;
    type(r.n, "t");
    CHECK(r.n.state() == Node::State::Chat);
    std::string out = plain(r.n.takeOutput());
    CHECK(out.find("/Q") != std::string::npos);
    type(r.n, "anyone cutting aluminum?\r");
    REQUIRE(r.said.size() == 1);
    CHECK(r.said[0].first == "anyone cutting aluminum?");
    CHECK_FALSE(r.said[0].second);
    r.n.takeOutput();
    type(r.n, "half a li");  // typing when a line arrives: the line shows, then what was typed
    r.n.takeOutput();
    r.n.chatIn(2, "Spindle Kid", "yes, 6061 on a rotary", false);
    out = plain(r.n.takeOutput());
    CHECK(out.find("Spindle Kid") != std::string::npos);
    CHECK(out.find("6061 on a rotary") != std::string::npos);
    CHECK(out.rfind("half a li") > out.find("6061"));
    type(r.n, "\b\b\b\b\b\b\b\b\b/q\r");
    CHECK(r.n.state() == Node::State::Menu);
    CHECK(r.said.size() == 1);  // /Q isn't said to anyone
}

TEST_CASE("lines arriving outside the teleconference don't interrupt the caller") {
    ChatRig r;
    r.n.chatIn(2, "Spindle Kid", "hello?", false);
    CHECK(plain(r.n.takeOutput()).find("hello?") == std::string::npos);
}

TEST_CASE("who's online lists every node with its caller and what they're doing") {
    ChatRig r;
    type(r.n, "w");
    std::string out = plain(r.n.takeOutput());
    CHECK(out.find("Spindle Kid") != std::string::npos);
    CHECK(out.find("Files") != std::string::npos);
    CHECK(out.find("waiting") != std::string::npos);  // node 3 has nobody
    CHECK(r.n.state() == Node::State::Menu);
}

TEST_CASE("a page asks for a message and sends it to the sysop") {
    ChatRig r;
    type(r.n, "p");
    CHECK(r.n.state() == Node::State::Paging);
    type(r.n, "need a file validated\r");
    REQUIRE(r.said.size() == 1);
    CHECK(r.said[0].first == "need a file validated");
    CHECK(r.said[0].second);
    CHECK(r.n.state() == Node::State::Menu);
    CHECK(plain(r.n.takeOutput()).find("sysop") != std::string::npos);
    type(r.n, "p\r");  // an empty page is no page
    CHECK(r.said.size() == 1);
}

TEST_CASE("a door that's busy for another caller on this board sends the caller back politely") {
    Node n = loggedIn();
    type(n, "d1\r");
    REQUIRE(n.state() == Node::State::InDoor);
    n.takeOutput();
    n.resumeAfterDoor(Node::DoorEnd::Busy, 2000);
    std::string out = plain(n.takeOutput());
    CHECK(n.state() == Node::State::Menu);
    CHECK(out.find("another caller") != std::string::npos);
    CHECK(out.find("Back from") == std::string::npos);  // they never went in
}

TEST_CASE("with the login splash on, the RotatoCAM splash plays before the welcome, and a key skips it") {
    axis::bbs::BbsConfig c;
    c.loginSplash = true;
    Node n(1, info, c);
    n.begin(0);
    CHECK(n.state() == Node::State::Splash);
    std::string out = n.takeOutput();
    for (uint32_t t = 20; t < 1500; t += 20) n.poll(t);
    out += n.takeOutput();
    CHECK(out.find("Handle:") == std::string::npos);  // still playing
    n.input((const uint8_t*)" ", 1, 1600);  // impatient caller
    n.poll(1620);
    CHECK(n.state() == Node::State::Login);
    CHECK(plain(n.takeOutput()).find("Handle:") != std::string::npos);
}

TEST_CASE("the splash hands over to the welcome by itself when it's done") {
    axis::bbs::BbsConfig c;
    c.loginSplash = true;
    Node n(1, info, c);
    n.begin(0);
    for (uint32_t t = 0; t < 9000; t += 50) n.poll(t);
    CHECK(n.state() == Node::State::Login);
    CHECK(plain(n.takeOutput()).find("Handle:") != std::string::npos);
}

namespace {
struct LinerRig {
    std::vector<std::string> wall = {"Rev: welcome to the underground", "Spindle Kid: 6061 or bust"};
    Node                     n = loggedInWithLiners();
    Node loggedInWithLiners() {
        Node n(1, info);
        n.setOneLiners([this] { return wall; }, [this](const std::string& l) { wall.push_back(l); });
        n.begin(0);
        type(n, "Rev\r");
        return n;
    }
};
}  // namespace

TEST_CASE("after login the oneliners show and the caller may add one") {
    LinerRig    r;
    std::string out = plain(r.n.takeOutput());
    CHECK(out.find("ONELINERS") != std::string::npos);
    CHECK(out.find("6061 or bust") != std::string::npos);
    CHECK(r.n.state() == Node::State::OneLinerAsk);
    type(r.n, "y");
    CHECK(r.n.state() == Node::State::OneLiner);
    type(r.n, "first post from the shop\r");
    CHECK(r.wall.back() == "Rev: first post from the shop");
    CHECK(r.n.state() == Node::State::Menu);
}

TEST_CASE("a caller can skip the oneliners, and an empty line adds nothing") {
    LinerRig r;
    r.n.takeOutput();
    type(r.n, "n");
    CHECK(r.n.state() == Node::State::Menu);
    LinerRig e;
    type(e.n, "y\r");
    CHECK(e.wall.size() == 2);
    CHECK(e.n.state() == Node::State::Menu);
}

TEST_CASE("without a oneliner wall the caller goes straight to the menu") {
    Node n = loggedIn();
    CHECK(n.state() == Node::State::Menu);
}

TEST_CASE("activity text follows the caller from login to a door and chat") {
    Node n(1, info);
    n.begin(0);
    CHECK(n.activityText() == "logging in");
    type(n, "Rev\r");
    CHECK(n.activityText() == "main menu");
    type(n, "d1\r");
    CHECK(n.activityText().rfind("in Legend of the Red Dragon", 0) == 0);
}

TEST_CASE("a local sysop login starts at the menu as sysop, no handle or password asked") {
    axis::bbs::BbsConfig c;
    c.sysopHandle = "Rev";
    c.loginSplash = true;  // (not for the local console)
    Node n(1, info, c);
    n.beginLocal(0);
    CHECK(n.isSysop());
    CHECK(n.handle() == "Rev");
    CHECK(n.state() == Node::State::Menu);
    CHECK(plain(n.takeOutput()).find("Handle:") == std::string::npos);
}

TEST_CASE("a door asked for at the board's own screen says to call in for doors") {
    Node n = loggedIn();
    type(n, "d1\r");
    REQUIRE(n.state() == Node::State::InDoor);
    n.takeOutput();
    n.resumeAfterDoor(Node::DoorEnd::NotHere, 2000);
    CHECK(n.state() == Node::State::Menu);
    CHECK(plain(n.takeOutput()).find("telnet") != std::string::npos);
}
