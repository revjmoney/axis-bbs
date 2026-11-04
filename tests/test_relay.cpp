// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include <string>
#include <vector>

#include "doctest.h"
#include "cluster/relay.h"

using namespace axis::cluster;

namespace {
struct FakePipe : Pipe {
    std::string in, out;  // in: bytes waiting to be read from this side; out: bytes written to it
    bool        open = true;
    size_t      takePerWrite = 1 << 20;
    int         read(uint8_t* buf, size_t n) override {
        if (in.empty()) return open ? 0 : -1;
        size_t k = in.size() < n ? in.size() : n;
        in.copy((char*)buf, k);
        in.erase(0, k);
        return (int)k;
    }
    int write(const uint8_t* p, size_t n) override {
        if (!open) return -1;
        size_t k = n < takePerWrite ? n : takePerWrite;
        out.append((const char*)p, k);
        return (int)k;
    }
};
NodeInfo node(int number, uint32_t ip, const char* handle = "", const char* activity = "") {
    NodeInfo n;
    n.number = number;
    n.ip = ip;
    n.handle = handle;
    n.activity = activity;
    return n;
}
}  // namespace

TEST_CASE("a relay carries the caller's keys to the node and the node's screens back") {
    FakePipe caller, board;
    Relay    r(caller, board);
    caller.in = "Rev\r";
    board.in = std::string("\xff\xfb\x01", 3) + "Welcome to AXIS";
    CHECK(r.pump());
    CHECK(board.out == "Rev\r");
    CHECK(caller.out == std::string("\xff\xfb\x01", 3) + "Welcome to AXIS");
}

TEST_CASE("a relay keeps what a slow side hasn't taken yet and stops reading until it has") {
    FakePipe caller, board;
    Relay    r(caller, board);
    caller.takePerWrite = 100;
    board.in = std::string(5000, 'x');
    for (int i = 0; i < 100 && caller.out.size() < 5000; i++) r.pump();
    CHECK(caller.out == std::string(5000, 'x'));
    CHECK(r.waiting() <= Relay::kMaxWaiting);
}

TEST_CASE("a relay ends when either side hangs up, after passing on what was left") {
    FakePipe caller, board;
    Relay    r(caller, board);
    board.in = "goodbye";
    board.open = false;
    bool more = true;
    for (int i = 0; i < 5 && more; i++) more = r.pump();
    CHECK_FALSE(more);
    CHECK(caller.out == "goodbye");
}

TEST_CASE("a busy main picks the lowest free node, never itself, a busy one or one just given a caller") {
    std::vector<NodeInfo> nodes = {node(1, 101, "Rev", "Menu"), node(2, 102, "", "Logging in"), node(3, 103), node(4, 104)};
    CHECK(pickFreeNode(nodes, 1, {}) == 103);
    CHECK(pickFreeNode(nodes, 1, {103}) == 104);
    CHECK(pickFreeNode(nodes, 3, {104}) == 0);  // node 3 is the main itself
    CHECK(pickFreeNode({node(1, 101, "Rev", "Menu")}, 1, {}) == 0);
}
