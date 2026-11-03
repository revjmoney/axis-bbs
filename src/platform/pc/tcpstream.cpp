// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include "platform/pc/tcpstream.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
using socklen_t = int;
static void closeSock(intptr_t s) { closesocket((SOCKET)s); }
static void nonBlocking(intptr_t s) {
    u_long on = 1;
    ioctlsocket((SOCKET)s, FIONBIO, &on);
}
static bool wouldBlock() { return WSAGetLastError() == WSAEWOULDBLOCK; }
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cerrno>
static void closeSock(intptr_t s) { ::close((int)s); }
static void nonBlocking(intptr_t s) { fcntl((int)s, F_SETFL, fcntl((int)s, F_GETFL) | O_NONBLOCK); }
static bool wouldBlock() { return errno == EAGAIN || errno == EWOULDBLOCK; }
#endif

namespace axis {
namespace pc {

namespace {
void netInit() {
#ifdef _WIN32
    static bool done = false;
    if (!done) {
        WSADATA w;
        WSAStartup(MAKEWORD(2, 2), &w);
        done = true;
    }
#endif
}
// false if the peer stopped taking data for timeoutMs (or the connection broke)
bool sendAll(intptr_t s, const char* p, size_t n, int timeoutMs = 10000) {
    int waited = 0;
    while (n) {
        int w = ::send((decltype(socket(0, 0, 0)))s, p, (int)n, 0);
        if (w > 0) {
            p += w;
            n -= w;
            waited = 0;
        } else if (w < 0 && wouldBlock()) {
            if (waited >= timeoutMs) return false;
            waited += 100;
            fd_set f;
            FD_ZERO(&f);
            FD_SET((decltype(socket(0, 0, 0)))s, &f);
            timeval tv = {0, 100000};
            select((int)s + 1, nullptr, &f, nullptr, &tv);
        } else {
            return false;
        }
    }
    return true;
}
}  // namespace

TcpStream::TcpStream(intptr_t sock) : sock_(sock) {
    nonBlocking(sock_);
    int one = 1;
    setsockopt((decltype(socket(0, 0, 0)))sock_, IPPROTO_TCP, TCP_NODELAY, (const char*)&one, sizeof one);
    setsockopt((decltype(socket(0, 0, 0)))sock_, SOL_SOCKET, SO_KEEPALIVE, (const char*)&one, sizeof one);
    std::string hello = telnet_.initialNegotiation();
    sendAll(sock_, hello.data(), hello.size());
}

TcpStream::~TcpStream() { closeSock(sock_); }

std::unique_ptr<TcpStream> TcpStream::dial(const char* host, uint16_t port) {
    netInit();
    intptr_t    sock = (intptr_t)socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in a = {};
    a.sin_family = AF_INET;
    a.sin_port = htons(port);
    inet_pton(AF_INET, host, &a.sin_addr);
    if (connect((decltype(socket(0, 0, 0)))sock, (sockaddr*)&a, sizeof a) != 0) {
        closeSock(sock);
        return nullptr;
    }
    return std::unique_ptr<TcpStream>(new TcpStream(sock));
}

void TcpStream::pump() {
    if (!up_) return;
    char buf[1024];
    while (true) {
        int n = ::recv((decltype(socket(0, 0, 0)))sock_, buf, sizeof buf, 0);
        if (n > 0) {
            std::string reply;
            telnet_.fromNet((const uint8_t*)buf, n, in_, reply);
            if (!reply.empty()) sendAll(sock_, reply.data(), reply.size());
        } else if (n == 0 || !wouldBlock()) {
            up_ = false;  // the caller hung up
            return;
        } else {
            return;
        }
    }
}

int TcpStream::available() {
    pump();
    return (int)in_.size();
}

int TcpStream::read() {
    pump();
    if (in_.empty()) return -1;
    int c = (uint8_t)in_[0];
    in_.erase(0, 1);
    return c;
}

int TcpStream::peek() {
    pump();
    return in_.empty() ? -1 : (uint8_t)in_[0];
}

void TcpStream::write(const uint8_t* p, size_t n) {
    if (!up_) return;
    std::string w = telnet_.toNet(p, n);
    if (!sendAll(sock_, w.data(), w.size(), sendTimeoutMs_)) up_ = false;  // the caller stopped reading
}

bool TcpStream::carrier() {
    pump();
    return up_;
}

TcpListener::TcpListener(uint16_t port) {
    netInit();
    sock_ = (intptr_t)socket(AF_INET, SOCK_STREAM, 0);
    int one = 1;
    setsockopt((decltype(socket(0, 0, 0)))sock_, SOL_SOCKET, SO_REUSEADDR, (const char*)&one, sizeof one);
    sockaddr_in a = {};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    a.sin_port = htons(port);
    bind((decltype(socket(0, 0, 0)))sock_, (sockaddr*)&a, sizeof a);
    listen((decltype(socket(0, 0, 0)))sock_, 4);
    socklen_t len = sizeof a;
    getsockname((decltype(socket(0, 0, 0)))sock_, (sockaddr*)&a, &len);
    port_ = ntohs(a.sin_port);
}

TcpListener::~TcpListener() { closeSock(sock_); }

std::unique_ptr<TcpStream> TcpListener::accept(int timeoutMs) {
    fd_set f;
    FD_ZERO(&f);
    FD_SET((decltype(socket(0, 0, 0)))sock_, &f);
    timeval tv = {timeoutMs / 1000, (timeoutMs % 1000) * 1000};
    if (select((int)sock_ + 1, &f, nullptr, nullptr, &tv) <= 0) return nullptr;
    intptr_t c = (intptr_t)::accept((decltype(socket(0, 0, 0)))sock_, nullptr, nullptr);
    return std::unique_ptr<TcpStream>(new TcpStream(c));
}

TcpClient::TcpClient(const char* host, uint16_t port) {
    netInit();
    sock_ = (intptr_t)socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in a = {};
    a.sin_family = AF_INET;
    a.sin_port = htons(port);
    inet_pton(AF_INET, host, &a.sin_addr);
    connect((decltype(socket(0, 0, 0)))sock_, (sockaddr*)&a, sizeof a);
    nonBlocking(sock_);
}

TcpClient::~TcpClient() { close(); }

std::string TcpClient::receive() {
    std::string s;
    if (sock_ < 0) return s;
    char buf[4096];
    int  n;
    while ((n = ::recv((decltype(socket(0, 0, 0)))sock_, buf, sizeof buf, 0)) > 0) s.append(buf, n);
    return s;
}

void TcpClient::send(const std::string& s) {
    if (sock_ >= 0) sendAll(sock_, s.data(), s.size());
}

void TcpClient::close() {
    if (sock_ >= 0) closeSock(sock_);
    sock_ = -1;
}

}  // namespace pc
}  // namespace axis
