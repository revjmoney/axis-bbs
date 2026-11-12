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
        bytesIn_ += (uint32_t)n;
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

namespace {
size_t gHoldPeak = 0;
}

size_t TelnetStream::holdPeak() { return gHoldPeak; }

void TelnetStream::queue(std::string piece) {
    if (piece.empty()) return;
    queued_ += piece.size();
    if (!out_.empty() && out_.back().size() + piece.size() <= kBatch) out_.back() += piece;
    else out_.push_back(std::move(piece));
}

void TelnetStream::drop() {
    std::vector<std::string>().swap(out_);
    sent_ = queued_ = 0;
}

bool TelnetStream::push(size_t leave, bool wait) {
    while (up_ && queued_ > leave) {
        const std::string& first = out_.front();
        int w = rawWrite((const uint8_t*)first.data() + sent_, first.size() - sent_);
        if (w < 0) {
            hangUp();
            return false;
        }
        if (w > 0) {
            sent_ += (size_t)w;
            queued_ -= (size_t)w;
            bytesOut_ += (uint32_t)w;
            blocked_ = false;
            if (sent_ == first.size()) {
                out_.erase(out_.begin());
                sent_ = 0;
            }
        } else if (!blocked_) {
            blocked_ = true;  // the 10 s start when the socket first takes nothing
            waitSince_ = nowMs();
        } else if (nowMs() - waitSince_ >= sendTimeoutMs) {
            hangUp();  // the caller stopped reading
            return false;
        } else if (!wait) {
            break;
        } else {
            idle();
        }
    }
    if (out_.empty() && out_.capacity() > 4) drop();  // all of a held screen went: give the list back too
    return up_;
}

bool TelnetStream::drain() {
    if (!up_) return false;
    if (queued_) push(0, false);
    return up_;
}

void TelnetStream::writeRaw(const std::string& s) {
    queue(s);
    flush();
}

void TelnetStream::flush() {
    batch_ = 0;
    if (!queued_ || !up_) return;
    if (!hold_) {
        push(0, true);
        return;
    }
    if (push(0, false) && queued_ > kHoldMax) push(kHoldMax, true);  // a burst bigger than the hold: wait it down
    if (queued_ > gHoldPeak) gHoldPeak = queued_;
}

void TelnetStream::write(const uint8_t* p, size_t n) {
    while (n && up_) {  // a piece at a time: a whole screen never needs one big buffer (the heap is fragmented)
        size_t      k = n < kBatch ? n : kBatch;
        std::string piece = telnet_.toNet(p, k);
        batch_ += piece.size();
        queue(std::move(piece));
        p += k;
        n -= k;
        if (batch_ >= kBatch) flush();
    }
}

bool TelnetStream::carrier() {
    if (up_ && !rawConnected()) up_ = false;
    return up_;
}

void TelnetStream::hangUp() {
    if (!up_) return;
    up_ = false;
    drop();
    rawClose();
}

}  // namespace axis::net
