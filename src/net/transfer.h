// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// File transfers over the caller's line (XMODEM, ZMODEM), run a slice at a time like a door.
// A sender reads the file through `ReadAt`; a receiver hands what arrives to the host.
#pragma once
#include <cstdint>
#include <functional>
#include <string>

#include "bbs/features.h"
#include "door/bytestream.h"

namespace axis::net {

class Transfer {
public:
    enum class Result { Running, Ok, Cancelled, Failed };
    using ReadAt = std::function<bool(uint32_t offset, uint8_t* buf, size_t n)>;
    // a receiver: a file is starting (size 0 = unknown); false skips it
    using OnFile = std::function<bool(const std::string& name, uint32_t size)>;
    using Write = std::function<bool(const uint8_t* p, size_t n)>;  // file data, in order; false aborts
    using OnEnd = std::function<void(bool complete)>;               // the current file is finished
    // A sender's data that arrives from elsewhere (a Net Files fetch). Optional: when set, a sender asks
    // before each block and, if false, sends nothing this step and asks again next step. While it waits,
    // its own timers for the receiver's answers do not run (the fetch's stall timer ends a dead fetch).
    // Once the data can never come, it answers true so the read fails and the sender cancels the transfer.
    using ReadyAt = std::function<bool(uint32_t offset, size_t n)>;

    virtual bool step(uint32_t nowMs) = 0;  // false once finished
    Result       result() const { return result_; }
    uint32_t     bytes() const { return bytes_; }  // file bytes moved so far
    const std::string& error() const { return error_; }
    virtual ~Transfer() = default;
#if AXIS_NETS
    void setReady(ReadyAt r) { ready_ = std::move(r); }  // senders only (receivers never ask)
#endif

protected:
#if AXIS_NETS
    bool    dataReady(uint32_t off, size_t n) const { return !ready_ || ready_(off, n); }
    ReadyAt ready_;
#else
    bool dataReady(uint32_t, size_t) const { return true; }
#endif
    bool finish(Result r, const char* why = "") {
        result_ = r;
        error_ = why;
        return false;
    }
    Result      result_ = Result::Running;
    uint32_t    bytes_ = 0;
    std::string error_;
};

}  // namespace axis::net
