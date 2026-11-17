// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The ESP32 core's WiFiServer (a listening TCP socket). Listening comes with the network layer (plan task 3):
// until then nobody ever calls in.
#pragma once
#include <cstdint>

#include "WiFiClient.h"

class WiFiServer {
public:
    explicit WiFiServer(uint16_t port = 80, uint8_t maxClients = 4) : port_(port) { (void)maxClients; }
    void begin(uint16_t port = 0) {
        if (port) port_ = port;
    }
    void       end() {}
    void       close() {}
    void       stop() {}
    bool       hasClient() { return false; }
    WiFiClient available() { return WiFiClient(); }
    WiFiClient accept() { return WiFiClient(); }
    void       setNoDelay(bool) {}
    explicit   operator bool() { return false; }

private:
    uint16_t port_;
};
