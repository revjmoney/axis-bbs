// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include "platform/cyd/clusternet.h"

#include <lwip/sockets.h>

void Esp32ClusterNet::begin() {
    udp_.begin(axis::cluster::kDiscoveryPort);
    server_.begin();
    server_.setNoDelay(true);
}

void Esp32ClusterNet::broadcast(const std::string& d) {
    IPAddress ip = WiFi.localIP(), mask = WiFi.subnetMask();
    IPAddress bcast((uint32_t)ip | ~(uint32_t)mask);  // this LAN's broadcast address
    udp_.beginPacket(bcast, axis::cluster::kDiscoveryPort);
    udp_.write((const uint8_t*)d.data(), d.size());
    udp_.endPacket();
}

bool Esp32ClusterNet::recvDatagram(std::string& d, uint32_t& from) {
    int n = udp_.parsePacket();
    if (n <= 0) return false;
    d.resize((size_t)n);
    int got = udp_.read((uint8_t*)&d[0], n);
    d.resize(got > 0 ? (size_t)got : 0);
    from = (uint32_t)udp_.remoteIP();
    return true;
}

int Esp32ClusterNet::connect(uint32_t ip) {
    WiFiClient c;
    if (!c.connect(IPAddress(ip), axis::cluster::kLinkPort, 1500)) return -1;
    c.setNoDelay(true);
    int id = next_++;
    links_[id].c = c;
    return id;
}

int Esp32ClusterNet::accept() {
    if (!server_.hasClient()) return -1;
    WiFiClient c = server_.available();
    c.setNoDelay(true);
    int id = next_++;
    links_[id].c = c;
    return id;
}

void Esp32ClusterNet::pump(Link& l) {
    while (!l.out.empty() && !l.dead) {
        int fd = l.c.fd();
        if (fd < 0) {
            l.dead = true;
            break;
        }
        int w = ::send(fd, l.out.data(), l.out.size(), MSG_DONTWAIT);
        if (w > 0) {
            l.out.erase(0, (size_t)w);
            if (l.out.empty()) std::string().swap(l.out);  // sent: give the memory back
        } else {
            if (w < 0 && errno != EAGAIN && errno != EWOULDBLOCK) l.dead = true;
            break;
        }
    }
}

bool Esp32ClusterNet::send(int link, const std::string& b) {
    auto it = links_.find(link);
    if (it == links_.end() || it->second.dead || !it->second.c.connected()) return false;
    Link& l = it->second;
    l.out += b;
    pump(l);
    if (l.out.size() > kMaxQueue) l.dead = true;  // the other board stopped reading
    return !l.dead;
}

int Esp32ClusterNet::recv(int link, std::string& out) {
    auto it = links_.find(link);
    if (it == links_.end()) return -1;
    Link& l = it->second;
    pump(l);
    if (l.dead) return -1;
    int     n = 0;
    uint8_t buf[512];
    while (l.c.available() > 0) {
        int r = l.c.read(buf, sizeof buf);
        if (r <= 0) break;
        out.append((const char*)buf, (size_t)r);
        n += r;
    }
    if (n == 0 && !l.c.connected()) return -1;
    return n;
}

void Esp32ClusterNet::close(int link) {
    auto it = links_.find(link);
    if (it == links_.end()) return;
    it->second.c.stop();
    links_.erase(it);
}
