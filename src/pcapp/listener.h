// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// axispc's telnet ports: lan_port (axis.conf, default 2323) and public_port (default 0, off), both on every address, as
// the Linux axis listens. Every caller passes the accept gate (platform/linux/netgate.h) before the BBS sees it: a refused
// one is closed at once. Non-blocking: poll() takes what is waiting and never waits.
#pragma once
#include <cstdint>
#include <vector>

#include "platform/linux/netgate.h"

namespace axis::pcapp {

class Listener {
public:
    struct Caller {
        intptr_t fd;  // a socket (Winsock's SOCKET does not fit in an int)
        uint32_t ip;  // network order
    };

    Listener() = default;
    ~Listener();
    Listener(const Listener&) = delete;
    Listener& operator=(const Listener&) = delete;

    // Reads the ports and the gate's limits from axis.conf (hostConfLoad first) and opens the ports. False when neither opened.
    bool start(uint32_t nowMs);
    // The callers let in since the last call (each counted by the gate until closed(ip)).
    std::vector<Caller> poll(uint32_t nowMs);
    // A caller poll() handed out is gone (its socket closed).
    void closed(uint32_t ip) { gate_.closed(ip); }
    // No new caller from now on: the ports close.
    void stop();

    uint16_t lanPort() const { return lanPort_; }
    NetGate& gate() { return gate_; }

private:
    void drain(intptr_t fd, uint16_t port, uint32_t nowMs, std::vector<Caller>& out);

    NetGate  gate_;
    intptr_t fd_[2] = {-1, -1};
    uint16_t port_[2] = {0, 0};
    uint16_t lanPort_ = 2323;
    uint32_t lastFdLog_ = 0;
};

}  // namespace axis::pcapp
