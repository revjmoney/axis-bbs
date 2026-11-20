// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// axispc's telnet ports (listener.h). The sockets themselves are platform/os (POSIX on Linux, Winsock on Windows).
#include "pcapp/listener.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <string>

#include "platform/linux/hostconf.h"
#include "platform/os/os.h"

namespace axis::pcapp {

namespace {
constexpr int kAcceptBurst = 32;  // callers taken from one port in one poll, at most

void gateLog(const std::string& line) { std::printf("axis: net: %s\n", line.c_str()); }

void warnFiles(unsigned long n) {
    std::printf("axis: net: WARNING: only %lu file handles allowed (ulimit -n), callers may be refused\n", n);
}

os::Socket listenOn(uint16_t port) {
    os::ListenOut r = os::listenAny(port);
    if (r.fd == os::kBadSocket) {
        const char* what = r.socketFailed ? "make a socket for" : "listen on";
        std::printf("axis: net: cannot %s port %u: %s\n", what, port, os::errorText(r.err).c_str());
        return os::kBadSocket;
    }
    return r.fd;
}
}  // namespace

Listener::~Listener() { stop(); }

bool Listener::start(uint32_t /*nowMs*/) {
    lanPort_ = (uint16_t)hostConfInt("lan_port", 2323, 1, 65535);
    uint16_t pub = (uint16_t)hostConfInt("public_port", 0, 0, 65535);
    if (pub == lanPort_) pub = 0;
    NetGate::Limits l;
    l.max_callers = (uint32_t)hostConfInt("max_callers", 16, 1, 4096);
    l.max_per_ip = (uint32_t)hostConfInt("max_per_ip", 3, 1, 1024);
    l.max_per_ip_lan = (uint32_t)hostConfInt("max_per_ip_lan", 8, 1, 1024);
    l.rate_per_ip = (uint32_t)hostConfInt("rate_per_ip", 20, 1, 100000);
    gate_.setLimits(l);
    gate_.log = gateLog;
    std::printf("axis: net: telnet on LAN port %u, public port %s; gate: %u callers, %u per address (%u LAN), %u new a minute\n",
                lanPort_, pub ? std::to_string(pub).c_str() : "off", l.max_callers, l.max_per_ip, l.max_per_ip_lan, l.rate_per_ip);
    os::netStart();
    os::raiseNoFile(warnFiles);
    uint16_t want[2] = {lanPort_, pub};
    for (int i = 0; i < 2; i++) {
        if (!want[i]) continue;
        fd_[i] = listenOn(want[i]);
        port_[i] = want[i];
        if (fd_[i] != os::kBadSocket) std::printf("axis: net: listening on 0.0.0.0:%u (%s)\n", want[i], i ? "public port" : "LAN port");
    }
    return fd_[0] != os::kBadSocket || fd_[1] != os::kBadSocket;
}

void Listener::stop() {
    for (int i = 0; i < 2; i++) {
        if (fd_[i] != os::kBadSocket) os::closeSocket(fd_[i]);
        fd_[i] = os::kBadSocket;
    }
}

std::vector<Listener::Caller> Listener::poll(uint32_t nowMs) {
    std::vector<Caller> out;
    for (int i = 0; i < 2; i++)
        if (fd_[i] != os::kBadSocket) drain(fd_[i], port_[i], nowMs, out);
    return out;
}

void Listener::drain(intptr_t fd, uint16_t /*port*/, uint32_t nowMs, std::vector<Caller>& out) {
    for (int k = 0; k < kAcceptBurst; k++) {
        os::Accepted c = os::acceptOne(fd);
        if (c.retry) continue;
        if (c.fd == os::kBadSocket) {
            if (c.noFiles && nowMs - lastFdLog_ > 10000) {
                std::printf("axis: net: out of file handles, callers wait in the queue\n");
                lastFdLog_ = nowMs;
            }
            return;
        }
        if (!gate_.admit(c.ip, NetGate::isLan(c.ip), nowMs)) {
            os::closeSocket(c.fd);  // refused: closed before the BBS ever sees it
            continue;
        }
        out.push_back({c.fd, c.ip});
    }
}

}  // namespace axis::pcapp
