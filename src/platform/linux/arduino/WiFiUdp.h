// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The ESP32 core's WiFiUDP on Linux. Only the cluster uses it (clusternet.cpp, port 2325). begin(port) binds a
// non-blocking socket on every address with SO_BROADCAST and SO_REUSEADDR; beginPacket/write/endPacket send one
// datagram; parsePacket takes one, dropping any from outside the LAN (NetGate::isLan). This computer's own broadcasts
// come back too: the cluster ignores an announcement with its own address, and only a main answers an "AXIS?", which
// a main never sends. In the Solo profile port 2325 sends nothing and what
// it hears goes to the solo watch (clusterhost.h's soloWatchHeard), never to parsePacket, so main.cpp's cluster is a
// main of one that nobody hears. Bodies in hostnet.cpp.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

#include "IPAddress.h"

class WiFiUDP {
public:
    WiFiUDP() = default;
    ~WiFiUDP() { stop(); }
    WiFiUDP(const WiFiUDP&) = delete;
    WiFiUDP& operator=(const WiFiUDP&) = delete;

    uint8_t   begin(uint16_t port);  // 1 when bound
    void      stop();
    int       beginPacket(IPAddress ip, uint16_t port);
    size_t    write(const uint8_t* buf, size_t n);
    size_t    write(uint8_t c) { return write(&c, 1); }
    int       endPacket();  // 1 when sent; 0 (nothing sent) when the packet outgrew one datagram
    int       parsePacket();  // the next datagram's size, 0 when none waits
    int       available() { return (int)(rx_.size() - rxPos_); }
    int       read(uint8_t* buf, size_t n);
    int       read();
    IPAddress remoteIP() { return IPAddress(rxIp_); }
    uint16_t  remotePort() { return rxPort_; }

private:
    bool        silent() const;  // the Solo profile's port 2325: nothing goes out, what comes in is the solo watch's
    int         fd_ = -1;
    uint16_t    port_ = 0;
    std::string rx_, tx_;
    size_t      rxPos_ = 0;
    uint32_t    rxIp_ = 0, txIp_ = 0;
    uint16_t    rxPort_ = 0, txPort_ = 0;
    bool        txOpen_ = false;
    bool        txOver_ = false;  // write() went past one datagram: endPacket sends nothing
};
