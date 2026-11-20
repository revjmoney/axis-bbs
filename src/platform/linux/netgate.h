// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The accept gate: every TCP caller passes here before main.cpp sees it. A refused caller is closed at once, so a
// flood from one address never reaches the board's code and never holds a node. Plain C++17 (no Arduino, no host
// globals): the clock is passed in, so the tests can drive it.
//
// Refused when: open callers in all >= max_callers; open from this address >= max_per_ip (max_per_ip_lan for a LAN
// address); or this address made >= rate_per_ip new connections in the last 60 s (every attempt counts, refused or
// not). An address refused for its own reasons (not a full board) for 60 s straight is banned for 10 minutes.
// The board's LoginGuard (bad passwords) still runs afterwards, unchanged.
#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

class NetGate {
public:
    struct Limits {
        uint32_t max_callers = 16;
        uint32_t max_per_ip = 3;
        uint32_t max_per_ip_lan = 8;
        uint32_t rate_per_ip = 20;  // new connections per 60 s
    };
    static constexpr uint32_t kWindowMs = 60000;      // the rate window, and how long "over the limit" must last
    static constexpr uint32_t kBanMs = 600000;        // 10 minutes
    static constexpr uint32_t kStreakGapMs = 15000;   // a pause this long between refusals starts the run afresh
    static constexpr size_t   kMaxTracked = 16384;    // addresses remembered at once (a spread-out flood is refused)

    NetGate() = default;
    explicit NetGate(const Limits& l) : lim_(l) {}
    void          setLimits(const Limits& l) { lim_ = l; }
    const Limits& limits() const { return lim_; }

    // ip in network byte order (an s_addr). True: the caller is let in and counted until closed(ip).
    bool admit(uint32_t ip, bool lan, uint32_t nowMs);
    // A caller admit() let in has gone (its socket is closed).
    void closed(uint32_t ip);

    uint32_t open() const { return total_; }
    uint32_t openFrom(uint32_t ip) const;
    bool     banned(uint32_t ip, uint32_t nowMs) const;

    // RFC 1918, link-local (169.254/16) and loopback (127/8). ip in network byte order.
    static bool        isLan(uint32_t ip);
    static std::string ipText(uint32_t ip);

    // Where the gate's log lines go ("gate: ban 192.0.2.1 10 min"); nullptr: nowhere.
    void (*log)(const std::string& line) = nullptr;

private:
    struct Entry {
        uint32_t              open = 0;
        std::vector<uint32_t> ring;  // the last rate_per_ip attempt times
        size_t                next = 0;
        uint32_t              lastSeen = 0;
        bool                  over = false;  // refused for its own reasons, in a run
        uint32_t              overSince = 0, lastRefuse = 0;
        bool                  isBanned = false;
        uint32_t              banAt = 0;
    };
    bool rateOver(const Entry& e, uint32_t now) const;
    void note(Entry& e, uint32_t now);
    void prune(uint32_t now);
    void say(const std::string& s) {
        if (log) log(s);
    }

    Limits                              lim_;
    std::unordered_map<uint32_t, Entry> table_;
    uint32_t                            total_ = 0;
    uint32_t                            lastPrune_ = 0, lastFullLog_ = 0;
    bool                                fullLogged_ = false;
};
