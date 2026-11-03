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
