// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include "net/telnetstream.h"

namespace axis::net {

namespace {
constexpr size_t kMaxQueued = 1024;  // stop reading the socket while this many keys wait (a busy main has ~8 KB blocks)
constexpr size_t kBatch = 1024;      // send once this much output is waiting
}

void TelnetStream::start() {
    // (no reserve: on a busy main a call starts with little heap; buffers grow only as needed)
    writeRaw(telnet_.initialNegotiation());
}

// Refill from the socket only once everything already read is used up: asking the socket for
// every byte (and shifting the buffer by one) made uploads crawl at 3 KB/s.
void TelnetStream::pump() {
    if (inPos_ < in_.size()) return;
    in_.clear();
    inPos_ = 0;
    uint8_t buf[1024];
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
    return (int)(in_.size() - inPos_);
}

int TelnetStream::peek() {
    pump();
    return inPos_ < in_.size() ? (uint8_t)in_[inPos_] : -1;
}

int TelnetStream::read() {
    int c = peek();
    if (c >= 0) inPos_++;
    return c;
}

std::string TelnetStream::takeInput() {
    pump();
    std::string s = in_.substr(inPos_);
    in_.clear();
    inPos_ = 0;
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
    out_ += s;
    flush();
}

void TelnetStream::flush() {
    if (out_.empty()) return;
    std::string s;
    s.swap(out_);
    if (up_ && !sendAll(s.data(), s.size())) hangUp();
}

void TelnetStream::write(const uint8_t* p, size_t n) {
    while (n && up_) {  // a piece at a time: a whole screen never needs one big buffer (the heap is fragmented)
        size_t k = n < kBatch ? n : kBatch;
        out_ += telnet_.toNet(p, k);
        p += k;
        n -= k;
        if (out_.size() >= kBatch) flush();
    }
}

bool TelnetStream::carrier() {
    if (up_ && !rawConnected()) up_ = false;
    return up_;
}

void TelnetStream::hangUp() {
    if (!up_) return;
    up_ = false;
    out_.clear();
    rawClose();
}

}  // namespace axis::net
