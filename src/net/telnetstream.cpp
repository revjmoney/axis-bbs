// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include "net/telnetstream.h"

namespace axis::net {

namespace {
constexpr size_t kMaxQueued = 4096;  // stop reading the socket while this many keys wait
}

void TelnetStream::start() { writeRaw(telnet_.initialNegotiation()); }

void TelnetStream::pump() {
    uint8_t buf[128];
    while (up_ && in_.size() < kMaxQueued) {
        int n = rawRead(buf, sizeof buf);
        if (n <= 0) break;
        std::string reply;
        telnet_.fromNet(buf, (size_t)n, in_, reply);
        writeRaw(reply);
    }
}

int TelnetStream::available() {
    pump();
    return (int)in_.size();
}

int TelnetStream::peek() {
    pump();
    return in_.empty() ? -1 : (uint8_t)in_[0];
}

int TelnetStream::read() {
    int c = peek();
    if (c >= 0) in_.erase(0, 1);
    return c;
}

std::string TelnetStream::takeInput() {
    pump();
    std::string s;
    s.swap(in_);
    return s;
}

bool TelnetStream::sendAll(const char* p, size_t n) {
    uint32_t lastProgress = nowMs();
    while (n > 0) {
        int w = rawWrite((const uint8_t*)p, n);
        if (w < 0) return false;
        if (w > 0) {
            p += w;
            n -= (size_t)w;
            lastProgress = nowMs();
        } else if (nowMs() - lastProgress >= sendTimeoutMs) {
            return false;  // the caller stopped reading
        } else {
            idle();
        }
    }
    return true;
}

void TelnetStream::writeRaw(const std::string& s) {
    if (!up_ || s.empty()) return;
    if (!sendAll(s.data(), s.size())) hangUp();
}

void TelnetStream::write(const uint8_t* p, size_t n) {
    if (n) writeRaw(telnet_.toNet(p, n));
}

bool TelnetStream::carrier() {
    if (up_ && !rawConnected()) up_ = false;
    return up_;
}

void TelnetStream::hangUp() {
    if (!up_) return;
    up_ = false;
    rawClose();
}

}  // namespace axis::net
