// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// One telnet caller as a ByteStream: negotiation and IAC handling through TelnetFilter, a peekable
// input queue, and writes that give up (dropping carrier) after a stretch with no progress, so a
// caller who stops reading can't hang the board. Portable: a platform supplies the raw socket calls.
#pragma once
#include <cstdint>
#include <string>

#include "door/bytestream.h"
#include "net/telnet.h"

namespace axis::net {

class TelnetStream : public door::ByteStream {
public:
    void start();  // send our option negotiation
    int  available() override;
    int  read() override;
    int  peek() override;
    void write(const uint8_t* p, size_t n) override;  // escaped for the wire
    bool carrier() override;
    void writeRaw(const std::string& s);  // already wire-ready (a busy screen, say)
    std::string takeInput();               // every waiting key, for the BBS menus
    void        hangUp();
    const TelnetFilter& telnet() const { return telnet_; }
    uint32_t    sendTimeoutMs = 10000;

protected:
    virtual int      rawRead(uint8_t* buf, size_t n) = 0;         // bytes read, 0 if none waiting
    virtual int      rawWrite(const uint8_t* p, size_t n) = 0;    // bytes taken (0 = full), < 0 = gone
    virtual bool     rawConnected() = 0;
    virtual void     rawClose() = 0;
    virtual uint32_t nowMs() = 0;
    virtual void     idle() {}  // called while a write waits for room

private:
    void         pump();
    bool         sendAll(const char* p, size_t n);
    TelnetFilter telnet_;
    std::string  in_;
    bool         up_ = true;
};

}  // namespace axis::net
