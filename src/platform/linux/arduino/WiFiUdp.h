// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The ESP32 core's WiFiUDP. Only the cluster uses it (clusternet.cpp, port 2325). Here it is silent for now:
// it sends nothing and hears nothing, so the cluster in main.cpp is a main of one that nobody hears.
#pragma once
#include <cstddef>
#include <cstdint>

#include "IPAddress.h"

class WiFiUDP {
public:
    uint8_t   begin(uint16_t) { return 0; }
    void      stop() {}
    int       beginPacket(IPAddress, uint16_t) { return 0; }
    size_t    write(const uint8_t*, size_t) { return 0; }
    int       endPacket() { return 0; }
    int       parsePacket() { return 0; }
    int       available() { return 0; }
    int       read(uint8_t*, size_t) { return -1; }
    IPAddress remoteIP() { return IPAddress(); }
    uint16_t  remotePort() { return 0; }
};
