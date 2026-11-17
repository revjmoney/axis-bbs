// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The ESP32 core's WiFiServer on Linux. The board's telnet port 23 becomes axis.conf's lan_port (2323) and, when it
// is not 0, public_port too; any other port (the panel's 2326) is itself and takes LAN addresses only. hasClient()
// accepts everything waiting, puts each caller through the accept gate (netgate.h), closes the refused ones at once
// and keeps the rest for available(). Calls the radio modem answers are queued by hostnetInjectClient() (hostnet.h)
// and come out of the telnet server's available() the same way, without the gate. Bodies in hostnet.cpp.
#pragma once
#include <cstdint>
#include <deque>

#include "WiFiClient.h"

class WiFiServer {
public:
    explicit WiFiServer(uint16_t port = 80, uint8_t maxClients = 4) : port_(port) { (void)maxClients; }
    ~WiFiServer() { end(); }
    WiFiServer(const WiFiServer&) = delete;
    WiFiServer& operator=(const WiFiServer&) = delete;

    void       begin(uint16_t port = 0);  // again with the same port: nothing changes
    void       end();
    void       close() { end(); }
    void       stop() { end(); }
    bool       hasClient();
    WiFiClient available();
    WiFiClient accept() { return available(); }
    void       setNoDelay(bool) {}  // every accepted socket gets TCP_NODELAY anyway
    explicit   operator bool() { return fd_[0] >= 0 || fd_[1] >= 0; }

private:
    void drain(int i);

    uint16_t               port_;
    int                    fd_[2] = {-1, -1};  // [0] the LAN (or only) listener, [1] the public one
    uint16_t               bound_[2] = {0, 0};
    bool                   lanOnly_[2] = {false, false};
    bool                   telnet_ = false;  // the board's telnet server: radio calls come out here
    std::deque<WiFiClient> waiting_;
};
