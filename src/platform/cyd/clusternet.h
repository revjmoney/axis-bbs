// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The cluster's network on an ESP32: UDP broadcast on port 2325 for discovery, a TCP server on
// port 2324 for followers' links, and WiFiClient links. Sends never block: bytes the socket won't take
// yet wait in the link's queue (WiFiClient::write would retry a dead peer for 10 s and stall the
// board, so every other node lost its heartbeat); a queue past kMaxQueue means the peer is gone.
#pragma once
#include <WiFi.h>
#include <WiFiUdp.h>

#include <map>

#include "cluster/cluster.h"

class Esp32ClusterNet : public axis::cluster::Net {
public:
    void begin();
    void     broadcast(const std::string& datagram) override;
    bool     recvDatagram(std::string& datagram, uint32_t& fromIp) override;
    int      connect(uint32_t ip) override;
    int      accept() override;
    bool     send(int link, const std::string& bytes) override;
    int      recv(int link, std::string& out) override;
    void     close(int link) override;
    uint32_t myIp() override { return (uint32_t)WiFi.localIP(); }

private:
    static constexpr size_t kMaxQueue = 8192;
    struct Link {
        WiFiClient  c;
        std::string out;  // waiting to go
        bool        dead = false;
    };
    void                       pump(Link& l);  // send what the socket takes now
    WiFiUDP                    udp_;
    WiFiServer                 server_{axis::cluster::kLinkPort};
    std::map<int, Link>        links_;
    int                        next_ = 1;
};
