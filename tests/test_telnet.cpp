// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include "doctest.h"
#include "net/telnet.h"

using axis::net::TelnetFilter;

static std::string bytes(std::initializer_list<int> v) {
    std::string s;
    for (int c : v) s += (char)c;
    return s;
}
static void feed(TelnetFilter& t, const std::string& in, std::string& app, std::string& reply) {
    t.fromNet((const uint8_t*)in.data(), in.size(), app, reply);
}

TEST_CASE("telnet initial negotiation offers echo, sga, binary and asks for naws") {
    TelnetFilter t;
    std::string n = t.initialNegotiation();
    CHECK(n.find(bytes({255, 251, 1})) != std::string::npos);   // WILL ECHO
    CHECK(n.find(bytes({255, 251, 3})) != std::string::npos);   // WILL SGA
    CHECK(n.find(bytes({255, 251, 0})) != std::string::npos);   // WILL BINARY
    CHECK(n.find(bytes({255, 253, 31})) != std::string::npos);  // DO NAWS
}

TEST_CASE("telnet strips option commands and refuses unknown options") {
    TelnetFilter t;
    std::string app, reply;
    feed(t, "a" + bytes({255, 253, 24}) + "b", app, reply);  // DO TTYPE in the middle
    CHECK(app == "ab");
    CHECK(reply == bytes({255, 252, 24}));                   // WONT TTYPE
}

TEST_CASE("telnet does not answer options it already agreed to") {
    TelnetFilter t;
    std::string app, reply;
    feed(t, bytes({255, 253, 1}), app, reply);  // DO ECHO: we offered WILL ECHO already
    CHECK(reply.empty());
}

TEST_CASE("telnet turns CR NUL and CR LF into CR") {
    TelnetFilter t;
    std::string app, reply;
    feed(t, "x\r" + bytes({0}) + "y\r\nz", app, reply);
    CHECK(app == "x\ry\rz");
}

TEST_CASE("telnet CR LF split across two reads still gives one CR") {
    TelnetFilter t;
    std::string app, reply;
    feed(t, "x\r", app, reply);
    feed(t, "\ny", app, reply);
    CHECK(app == "x\ry");
}

TEST_CASE("telnet IAC IAC becomes a literal 0xFF and 0xFF output is doubled") {
    TelnetFilter t;
    std::string app, reply;
    feed(t, bytes({255, 255}), app, reply);
    CHECK(app == bytes({255}));
    uint8_t out[] = {'a', 255, 'b'};
    CHECK(t.toNet(out, 3) == "a" + bytes({255, 255}) + "b");
}

TEST_CASE("telnet NAWS subnegotiation sets the window size, even split across reads") {
    TelnetFilter t;
    std::string app, reply;
    CHECK(t.width() == 80);
    CHECK(t.height() == 25);
    feed(t, bytes({255, 250, 31, 0, 132}), app, reply);
    feed(t, bytes({0, 50, 255, 240}) + "k", app, reply);
    CHECK(t.width() == 132);
    CHECK(t.height() == 50);
    CHECK(app == "k");
}

TEST_CASE("telnet an endless subnegotiation is capped and the stream recovers") {
    TelnetFilter t;
    std::string app, reply;
    feed(t, bytes({255, 250, 24}), app, reply);
    feed(t, std::string(100000, 'x'), app, reply);
    CHECK(t.pendingSubnegotiation() <= 64);
    feed(t, bytes({255, 240}) + "k", app, reply);
    CHECK(app == "k");
}
