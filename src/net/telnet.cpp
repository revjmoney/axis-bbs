// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include "net/telnet.h"

namespace axis::net {

namespace {
constexpr uint8_t IAC = 255, DONT = 254, DO = 253, WONT = 252, WILL = 251, SB = 250, SE = 240;
constexpr uint8_t BINARY = 0, ECHO = 1, SGA = 3, NAWS = 31;
constexpr size_t  kMaxSubneg = 64;  // NAWS needs 5; nothing we use needs more

std::string cmd(uint8_t verb, uint8_t opt) { return {(char)IAC, (char)verb, (char)opt}; }
}  // namespace

// The server echoes and runs character at a time in 8-bit binary, so CP437 art passes untouched.
std::string TelnetFilter::initialNegotiation() const {
    return cmd(WILL, ECHO) + cmd(WILL, SGA) + cmd(DO, SGA) + cmd(WILL, BINARY) + cmd(DO, BINARY) + cmd(DO, NAWS);
}

void TelnetFilter::fromNet(const uint8_t* p, size_t n, std::string& toApp, std::string& replyToNet) {
    for (size_t i = 0; i < n; i++) {
        uint8_t c = p[i];
        switch (st_) {
            case St::Data:
                if (c == IAC) {
                    st_ = St::Iac;
                } else if (lastCr_ && !callerBinary_ && (c == 0 || c == '\n')) {
                    lastCr_ = false;  // second half of CR NUL / CR LF
                } else {
                    lastCr_ = c == '\r';
                    toApp += (char)c;
                }
                break;
            case St::Iac:
                if (c == IAC) {
                    toApp += (char)IAC;
                    lastCr_ = false;
                    st_ = St::Data;
                } else if (c >= WILL && c <= DONT) {
                    cmd_ = c;
                    st_ = St::Opt;
                } else if (c == SB) {
                    sb_.clear();
                    st_ = St::Sb;
                } else {
                    st_ = St::Data;  // NOP, GA, AYT and friends: ignore
                }
                break;
            case St::Opt: {
                // Options we offered or asked for need no answer; refuse everything else.
                if (c == BINARY && cmd_ == WILL) callerBinary_ = true;  // our DO BINARY was accepted
                if (c == BINARY && cmd_ == WONT) callerBinary_ = false;
                bool ours = c == ECHO || c == SGA || c == BINARY || c == NAWS;
                if (!ours) {
                    if (cmd_ == DO) replyToNet += cmd(WONT, c);
                    else if (cmd_ == WILL) replyToNet += cmd(DONT, c);
                }
                st_ = St::Data;
                break;
            }
            case St::Sb:
                if (c == IAC) st_ = St::SbIac;
                else if (sb_.size() < kMaxSubneg) sb_ += (char)c;  // a caller can't grow it without end
                break;
            case St::SbIac:
                if (c == SE) {
                    if (sb_.size() >= 5 && (uint8_t)sb_[0] == NAWS) {
                        auto b = [&](int k) { return (int)(uint8_t)sb_[k]; };
                        int w = b(1) << 8 | b(2), h = b(3) << 8 | b(4);
                        if (w > 0) width_ = w;
                        if (h > 0) height_ = h;
                    }
                    st_ = St::Data;
                } else {
                    if (sb_.size() < kMaxSubneg) sb_ += (char)c;  // IAC IAC inside a subnegotiation
                    st_ = St::Sb;
                }
                break;
        }
    }
}

std::string TelnetFilter::toNet(const uint8_t* p, size_t n) const {
    std::string s;
    s.reserve(n);
    for (size_t i = 0; i < n; i++) {
        s += (char)p[i];
        if (p[i] == IAC) s += (char)IAC;
    }
    return s;
}

}  // namespace axis::net
