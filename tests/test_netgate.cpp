// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The accept gate (src/platform/linux/netgate.h): its limits and its ban, on a fake clock.
#include "platform/linux/netgate.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "doctest.h"

namespace {
// Network order, the same bytes inet_addr stores, so this test does not need a socket library.
uint32_t ip(const char* s) {
    unsigned b[4] = {};
    if (std::sscanf(s, "%u.%u.%u.%u", &b[0], &b[1], &b[2], &b[3]) != 4) return 0;
    uint8_t  bytes[4] = {(uint8_t)b[0], (uint8_t)b[1], (uint8_t)b[2], (uint8_t)b[3]};
    uint32_t n = 0;
    std::memcpy(&n, bytes, 4);
    return n;
}
}  // namespace

TEST_CASE("netgate: LAN addresses are told apart") {
    CHECK(NetGate::isLan(ip("10.1.2.3")));
    CHECK(NetGate::isLan(ip("127.0.0.1")));
    CHECK(NetGate::isLan(ip("172.16.0.1")));
    CHECK(NetGate::isLan(ip("172.31.255.1")));
    CHECK_FALSE(NetGate::isLan(ip("172.32.0.1")));
    CHECK(NetGate::isLan(ip("192.168.0.9")));
    CHECK(NetGate::isLan(ip("169.254.1.1")));
    CHECK_FALSE(NetGate::isLan(ip("203.0.113.5")));
    CHECK(NetGate::ipText(ip("203.0.113.5")) == "203.0.113.5");
}

TEST_CASE("netgate: open callers per address, outside and on the LAN") {
    NetGate  g;  // the defaults: 3 per outside address, 8 per LAN address, 16 in all
    uint32_t far = ip("203.0.113.5"), near = ip("192.168.0.20"), t = 1000;
    for (int i = 0; i < 3; i++) CHECK(g.admit(far, false, t + i * 1000));
    CHECK_FALSE(g.admit(far, false, t + 3000));
    CHECK(g.openFrom(far) == 3);
    g.closed(far);
    CHECK(g.admit(far, false, t + 4000));  // one left, so one more may come in

    for (int i = 0; i < 8; i++) CHECK(g.admit(near, true, t + i * 1000));
    CHECK_FALSE(g.admit(near, true, t + 9000));
    CHECK(g.open() == 11);
}

TEST_CASE("netgate: the board's total limit refuses without a ban") {
    NetGate::Limits l;
    l.max_callers = 2;
    NetGate g(l);
    uint32_t a = ip("198.51.100.1"), b = ip("198.51.100.2"), c = ip("198.51.100.3"), t = 1000;
    CHECK(g.admit(a, false, t));
    CHECK(g.admit(b, false, t));
    for (uint32_t s = 0; s <= 120; s += 5) CHECK_FALSE(g.admit(c, false, t + s * 1000));  // two minutes of "full"
    CHECK_FALSE(g.banned(c, t + 120000));
    g.closed(a);
    CHECK(g.admit(c, false, t + 121000));
}

TEST_CASE("netgate: new connections a minute, counted refused or not") {
    NetGate  g;  // 20 a minute
    uint32_t a = ip("203.0.113.9"), t = 1000;
    for (int i = 0; i < 20; i++) {
        REQUIRE(g.admit(a, false, t + i * 100));
        g.closed(a);
    }
    CHECK_FALSE(g.admit(a, false, t + 2100));  // the 21st inside the minute
    CHECK_FALSE(g.banned(a, t + 2100));
    // A pause well past the minute (and past the run gap): the count has moved on.
    CHECK(g.admit(a, false, t + 2100 + NetGate::kWindowMs + 1000));
}

TEST_CASE("netgate: a minute of refusals bans for ten minutes, then a clean slate") {
    NetGate  g;
    uint32_t a = ip("203.0.113.66"), other = ip("203.0.113.67"), t = 1000;
    for (int i = 0; i < 20; i++) {
        REQUIRE(g.admit(a, false, t + i * 10));
        g.closed(a);
    }
    uint32_t start = t + 200;  // the first refusal starts the run
    CHECK_FALSE(g.admit(a, false, start));
    uint32_t now = start;
    while (now - start < NetGate::kWindowMs) {  // keep knocking every second: always over the rate
        now += 1000;
        CHECK_FALSE(g.admit(a, false, now));
    }
    CHECK(g.banned(a, now));
    uint32_t banAt = now;

    CHECK(g.admit(other, false, now + 1000));  // another address still gets in
    g.closed(other);

    CHECK_FALSE(g.admit(a, false, banAt + 60000));  // a ban refuses even a slow, polite caller
    CHECK(g.banned(a, banAt + NetGate::kBanMs - 1));
    CHECK_FALSE(g.admit(a, false, banAt + NetGate::kBanMs - 1));
    CHECK_FALSE(g.banned(a, banAt + NetGate::kBanMs));
    CHECK(g.admit(a, false, banAt + NetGate::kBanMs));  // the ban is over
}

TEST_CASE("netgate: refusals with a long pause between them start over and never ban") {
    NetGate  g;
    uint32_t a = ip("203.0.113.70"), t = 1000;
    for (int i = 0; i < 3; i++) REQUIRE(g.admit(a, false, t));  // three held open: the per-address limit
    uint32_t now = t;
    for (int i = 0; i < 10; i++) {  // a refusal every 20 s for 3 minutes: each one is a new run
        now += 20000;
        CHECK_FALSE(g.admit(a, false, now));
    }
    CHECK_FALSE(g.banned(a, now));
}

TEST_CASE("netgate: the clock wrapping round does no harm") {
    NetGate  g;
    uint32_t a = ip("203.0.113.80");
    uint32_t t = 0xFFFFFFFFu - 30000;  // 30 s before the 49-day wrap
    for (int i = 0; i < 20; i++) {
        REQUIRE(g.admit(a, false, t + i * 100));
        g.closed(a);
    }
    CHECK_FALSE(g.admit(a, false, t + 40000));  // past the wrap, still inside the minute
    CHECK(g.admit(a, false, t + 40000 + NetGate::kWindowMs + 20000));
}
