// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The PC's caller line: a telnet caller over a non-blocking socket, what WifiStream is on the board. The backlog, hold
// mode, drain and the thresholds (kHoldMax, kHoldLow) are TelnetStream's own, so a slow caller behaves the same on both.
// A radio call arrives as a socketpair end from the modem line and is a PcLine too (its link label comes from the
// modem's LFD3K-LINK reports, as on the board).
#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "net/telnetstream.h"

namespace axis::pcapp {

class PcLine : public net::TelnetStream {
public:
    // Takes the socket (a TCP socket, or one end of a socketpair): makes it non-blocking and owns it. `ip` is the
    // caller's address in network order (0 when unknown).
    static std::unique_ptr<PcLine> fromFd(intptr_t fd, uint32_t ip);
    ~PcLine() override;
    PcLine(const PcLine&) = delete;
    PcLine& operator=(const PcLine&) = delete;

    uint32_t remoteIp() const { return ip_; }  // network order
    // The sysop control panel: every byte sent to this caller can be copied out, and keys put in as if typed.
    static void (*tap)(PcLine* s, const uint8_t* p, size_t n);
    int         tapNode = 0;  // the node number this line answers as
    void        inject(const std::string& keys) { injected_ += keys; }

protected:
    int      rawRead(uint8_t* buf, size_t n) override;
    int      rawWrite(const uint8_t* p, size_t n) override;
    bool     rawConnected() override;
    void     rawClose() override;
    uint32_t nowMs() override;
    void     idle() override;

private:
    PcLine(intptr_t fd, uint32_t ip);
    intptr_t    fd_;
    uint32_t    ip_;
    bool        gone_ = false;
    std::string injected_;
};

}  // namespace axis::pcapp
