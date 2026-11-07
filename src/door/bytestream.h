// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// A byte pipe to the caller (or the local keyboard and screen). DOS character I/O, the BIOS
// keyboard, FOSSIL and the telnet bridge all talk through this.
#pragma once
#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>

namespace axis {
namespace door {

class ByteStream {
public:
    virtual int  available() = 0;  // bytes ready to read
    virtual int  read() = 0;       // -1 if none
    virtual int  peek() = 0;       // the next byte without taking it, -1 if none
    virtual void write(const uint8_t* p, size_t n) = 0;
    virtual void flush() {}  // send bytes already queued; a poll must not need this
    virtual bool carrier() = 0;
    virtual ~ByteStream() = default;
};

// For tests: queued input, captured output.
class ScriptStream : public ByteStream {
public:
    void push(const std::string& keys) {
        for (char c : keys) in_.push_back((uint8_t)c);
    }
    std::string output() const { return out_; }
    void        clearOutput() { out_.clear(); }
    void        hangUp() { up_ = false; }

    int available() override { return (int)in_.size(); }
    int peek() override { return in_.empty() ? -1 : in_.front(); }
    int read() override {
        if (in_.empty()) return -1;
        int c = in_.front();
        in_.pop_front();
        return c;
    }
    void write(const uint8_t* p, size_t n) override { out_.append((const char*)p, n); }
    bool carrier() override { return up_; }

private:
    std::deque<uint8_t> in_;
    std::string         out_;
    bool                up_ = true;
};

}  // namespace door
}  // namespace axis
