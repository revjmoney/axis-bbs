#include "bbs/loginguard.h"

#include <algorithm>

#include "doctest.h"

using axis::bbs::LoginGuard;

TEST_CASE("three failed calls in ten minutes lock an address out for five minutes") {
    LoginGuard g;
    uint32_t   ip = 0x0A000002, t = 1000;
    CHECK(g.allowed(ip, t));
    g.failed(ip, t);
    g.failed(ip, t + 60000);
    CHECK(g.allowed(ip, t + 61000));
    g.failed(ip, t + 120000);
    CHECK_FALSE(g.allowed(ip, t + 121000));
    CHECK(g.secondsLeft(ip, t + 121000) > 290);
    CHECK(g.allowed(ip, t + 120000 + 5 * 60000 + 1));
}

TEST_CASE("failures spread out over more than ten minutes don't add up") {
    LoginGuard g;
    uint32_t   ip = 7;
    g.failed(ip, 0);
    g.failed(ip, 6 * 60000);
    g.failed(ip, 12 * 60000);  // the first one is too old by now
    CHECK(g.allowed(ip, 12 * 60000 + 1));
}

TEST_CASE("a repeat offender waits twice as long each time, up to an hour") {
    LoginGuard g;
    uint32_t   ip = 9, t = 0;
    for (int round = 0; round < 6; round++) {
        for (int i = 0; i < 3; i++) g.failed(ip, t);
        uint32_t left = g.secondsLeft(ip, t);
        CHECK(left == (uint32_t)std::min(300u << round, 3600u));
        t += left * 1000 + 1;
    }
}

TEST_CASE("a good login clears an address and other addresses are untouched") {
    LoginGuard g;
    g.failed(1, 0);
    g.failed(1, 1);
    g.succeeded(1);
    g.failed(1, 2);
    CHECK(g.allowed(1, 3));
    for (int i = 0; i < 3; i++) g.failed(2, 10);
    CHECK_FALSE(g.allowed(2, 11));
    CHECK(g.allowed(1, 11));
}

TEST_CASE("the guard stays small however many addresses try") {
    LoginGuard g;
    for (uint32_t ip = 0; ip < 1000; ip++) g.failed(ip, ip);
    CHECK(g.tracked() <= LoginGuard::kMaxTracked);
}
