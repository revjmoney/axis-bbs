// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include "pcapp/pcline.h"

#include <chrono>
#include <cstring>
#include <thread>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
using sock_t = SOCKET;
static void closeSock(intptr_t s) { closesocket((sock_t)s); }
static void nonBlocking(intptr_t s) {
    u_long on = 1;
    ioctlsocket((sock_t)s, FIONBIO, &on);
}
static bool wouldBlock() { return WSAGetLastError() == WSAEWOULDBLOCK || WSAGetLastError() == WSAEINTR; }
static void shutdownSend(intptr_t s) { ::shutdown((sock_t)s, SD_SEND); }
static void netStart() {
    static bool done = false;
    if (!done) {
        WSADATA w;
        WSAStartup(MAKEWORD(2, 2), &w);
        done = true;
    }
}
static constexpr int kSendFlags = 0;  // the socket is non-blocking already
static constexpr int kPeekFlags = MSG_PEEK;
#else
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cerrno>
using sock_t = int;
static void closeSock(intptr_t s) { ::close((int)s); }
static void nonBlocking(intptr_t s) { fcntl((int)s, F_SETFL, fcntl((int)s, F_GETFL) | O_NONBLOCK); }
static bool wouldBlock() { return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR; }  // EINTR: a signal is not a hang-up
static void shutdownSend(intptr_t s) { ::shutdown((int)s, SHUT_WR); }
#ifdef MSG_NOSIGNAL
static constexpr int kSendFlags = MSG_DONTWAIT | MSG_NOSIGNAL;  // never waits, never raises SIGPIPE
#else
static constexpr int kSendFlags = MSG_DONTWAIT;
#endif
static constexpr int kPeekFlags = MSG_PEEK | MSG_DONTWAIT;
#endif

namespace axis::pcapp {

void (*PcLine::tap)(PcLine*, const uint8_t*, size_t) = nullptr;

PcLine::PcLine(intptr_t fd, uint32_t ip) : fd_(fd), ip_(ip) {
#ifdef _WIN32
    netStart();
#endif
    nonBlocking(fd_);
    int one = 1;
    // TCP_NODELAY fails harmlessly on a socketpair
    setsockopt((sock_t)fd_, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&one), sizeof one);
}

PcLine::~PcLine() {
    hangUp();    // the goodbye and the close, while the virtuals are still ours
    rawClose();  // carrier() can drop the line without closing it: hangUp() then does nothing; this is idempotent
    if (closed) closed();
}

std::unique_ptr<PcLine> PcLine::fromFd(intptr_t fd, uint32_t ip) {
    return std::unique_ptr<PcLine>(new PcLine(fd, ip));
}

int PcLine::rawRead(uint8_t* buf, size_t n) {
    if (!injected_.empty()) {  // the sysop's keys first
        size_t k = injected_.size() < n ? injected_.size() : n;
        std::memcpy(buf, injected_.data(), k);
        injected_.erase(0, k);
        return (int)k;
    }
    if (gone_) return 0;
    int r = (int)::recv((sock_t)fd_, reinterpret_cast<char*>(buf), (int)n, 0);
    if (r > 0) return r;
    if (r == 0 || !wouldBlock()) gone_ = true;  // the caller hung up (or the line broke)
    return 0;
}

int PcLine::rawWrite(const uint8_t* p, size_t n) {
    // One send that never waits: 0 when the socket is full for now, as the board's line does.
    if (gone_) return -1;
    int w = (int)::send((sock_t)fd_, reinterpret_cast<const char*>(p), (int)n, kSendFlags);
    if (w < 0) {
        if (wouldBlock()) return 0;
        gone_ = true;
        return -1;
    }
    if (w > 0 && tap) tap(this, p, (size_t)w);
    return w;
}

bool PcLine::rawConnected() {
    if (gone_) return false;
    char c;
    int  r = (int)::recv((sock_t)fd_, &c, 1, kPeekFlags);  // 0 = orderly close; data stays queued for rawRead
    if (r == 0 || (r < 0 && !wouldBlock())) gone_ = true;
    return !gone_;
}

void PcLine::rawClose() {
    if (fd_ < 0) return;
    shutdownSend(fd_);  // the goodbye is delivered before the close, with no sleep on the BBS thread
    closeSock(fd_);
    fd_ = -1;
    gone_ = true;
}

uint32_t PcLine::nowMs() {
    using namespace std::chrono;
    return (uint32_t)duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

void PcLine::idle() { std::this_thread::sleep_for(std::chrono::milliseconds(10)); }

}  // namespace axis::pcapp
