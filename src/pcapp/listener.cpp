// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// axispc's telnet ports (listener.h).
#include "pcapp/listener.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <string>

#include "platform/linux/hostconf.h"

namespace axis::pcapp {

namespace {
constexpr int kAcceptBurst = 32;  // callers taken from one port in one poll, at most

void gateLog(const std::string& line) { std::printf("axis: net: %s\n", line.c_str()); }

int listenOn(uint16_t port) {
    int fd = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        std::printf("axis: net: cannot make a socket for port %u: %s\n", port, std::strerror(errno));
        return -1;
    }
    int on = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on);
    sockaddr_in sa{};
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = htonl(INADDR_ANY);
    sa.sin_port = htons(port);
    if (::bind(fd, (sockaddr*)&sa, sizeof sa) < 0 || ::listen(fd, 128) < 0) {
        std::printf("axis: net: cannot listen on port %u: %s\n", port, std::strerror(errno));
        ::close(fd);
        return -1;
    }
    return fd;
}

void raiseFileLimit() {  // as the Linux axis: every caller is a socket
    rlimit r{};
    if (::getrlimit(RLIMIT_NOFILE, &r) != 0) return;
    rlim_t want = r.rlim_max == RLIM_INFINITY ? (rlim_t)1048576 : r.rlim_max;
    if (r.rlim_cur < want) {
        rlimit n = r;
        n.rlim_cur = want;
        if (::setrlimit(RLIMIT_NOFILE, &n) == 0) r = n;
    }
    if (r.rlim_cur < 256)
        std::printf("axis: net: WARNING: only %lu file handles allowed (ulimit -n), callers may be refused\n",
                    (unsigned long)r.rlim_cur);
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
    raiseFileLimit();
    uint16_t want[2] = {lanPort_, pub};
    for (int i = 0; i < 2; i++) {
        if (!want[i]) continue;
        fd_[i] = listenOn(want[i]);
        port_[i] = want[i];
        if (fd_[i] >= 0) std::printf("axis: net: listening on 0.0.0.0:%u (%s)\n", want[i], i ? "public port" : "LAN port");
    }
    return fd_[0] >= 0 || fd_[1] >= 0;
}

void Listener::stop() {
    for (int i = 0; i < 2; i++) {
        if (fd_[i] >= 0) ::close(fd_[i]);
        fd_[i] = -1;
    }
}

std::vector<Listener::Caller> Listener::poll(uint32_t nowMs) {
    std::vector<Caller> out;
    for (int i = 0; i < 2; i++)
        if (fd_[i] >= 0) drain(fd_[i], port_[i], nowMs, out);
    return out;
}

void Listener::drain(int fd, uint16_t /*port*/, uint32_t nowMs, std::vector<Caller>& out) {
    for (int k = 0; k < kAcceptBurst; k++) {
        sockaddr_in sa{};
        socklen_t   len = sizeof sa;
        int         c = ::accept4(fd, (sockaddr*)&sa, &len, SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (c < 0) {
            if (errno == EINTR || errno == ECONNABORTED) continue;
            if ((errno == EMFILE || errno == ENFILE) && nowMs - lastFdLog_ > 10000) {
                std::printf("axis: net: out of file handles, callers wait in the queue\n");
                lastFdLog_ = nowMs;
            }
            return;  // (EAGAIN: nobody else waiting)
        }
        uint32_t ip = sa.sin_family == AF_INET ? sa.sin_addr.s_addr : 0;
        if (!gate_.admit(ip, NetGate::isLan(ip), nowMs)) {
            ::close(c);  // refused: closed before the BBS ever sees it
            continue;
        }
        out.push_back({c, ip});
    }
}

}  // namespace axis::pcapp
