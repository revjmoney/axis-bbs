// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// One telnet caller as a ByteStream: negotiation and IAC handling through TelnetFilter, a peekable
// input queue, and writes that give up (dropping carrier) after a stretch with no progress, so a
// caller who stops reading can't hang the board. Output is batched (doors write a byte at a time,
// and a packet per byte costs the board a network buffer each). It goes out on flush(), when 1 KB
// is waiting, or when the door actually waits for a key. A status poll does not send: LORD checks
// the line between characters, and flushing there was one packet per character. Portable: a
// platform supplies the raw socket calls.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "door/bytestream.h"
#include "net/telnet.h"

namespace axis::net {

class TelnetStream : public door::ByteStream {
public:
    void start();  // send our option negotiation
    int  available() override;
    int  read() override;
    int  peek() override;
    void write(const uint8_t* p, size_t n) override;  // escaped for the wire, batched
    void flush() override;                             // send what is waiting
    bool carrier() override;
    void writeRaw(const std::string& s);  // already wire-ready (a busy screen, say); sent now
    std::string takeInput();               // every waiting key, for the BBS menus
    void        hangUp();
    const TelnetFilter& telnet() const { return telnet_; }
    // LFD3K-LINK (net/telnet.h): take the modem's link reports (answer its WILL 90 with DO 90). Set before start().
    void        acceptLink(bool on) { telnet_.acceptLink(on); }
    // the newest link report, once: true when one came in since the last call. At most one a second: reports that come
    // faster are parsed and replaced by the next, and the newest is taken a second after the last one.
    bool        takeLink(LinkInfo& out) {
        if (telnet_.linkReports() == linkTaken_) return false;
        uint32_t now = nowMs();
        if (linkTakenMs_ && now - linkTakenMs_ < 1000) return false;
        linkTaken_ = telnet_.linkReports();
        linkTakenMs_ = now ? now : 1;
        out = telnet_.link();
        return true;
    }
    uint32_t    bytesIn() const { return bytesIn_; }    // raw bytes from and to the caller this call
    uint32_t    bytesOut() const { return bytesOut_; }
    uint32_t    sendTimeoutMs = 10000;
    // Hold mode (off by default): flush() sends what the socket takes right now and keeps the rest for drain(), so a slow
    // caller (a LoRa radio call at a few hundred bytes a second) never stops the board's loop. What waits is kept in pieces of
    // about 1 KB (never one big block: the heap is fragmented). Above kHoldMax waiting, flush() waits it down to kHoldMax as
    // before. Off: flush() waits until everything is out (transfers need that).
    static constexpr size_t kHoldMax = 16384;
    static constexpr size_t kHoldLow = 1024;  // the board waits with the next screen or step until less than this is waiting
    void        setHold(bool on) { hold_ = on; }
    size_t      backlog() const { return queued_; }  // bytes waiting to go (batched or held)
    bool        drain();  // send what the socket takes now, never waits; false once the line is gone (10 s no progress: hung up)
    static size_t holdPeak();  // the most bytes any line has held so far (for @STATUS)

protected:
    virtual int      rawRead(uint8_t* buf, size_t n) = 0;         // bytes read, 0 if none waiting
    virtual int      rawWrite(const uint8_t* p, size_t n) = 0;    // bytes taken (0 = full), < 0 = gone
    virtual bool     rawConnected() = 0;
    virtual void     rawClose() = 0;
    virtual uint32_t nowMs() = 0;
    virtual void     idle() {}  // called while a write waits for room

private:
    void         pump();
    bool         push(size_t leave, bool wait);  // send until `leave` bytes wait; false: gone (hung up)
    void         queue(std::string piece);       // onto the end, joined to the last piece while that stays under 1 KB
    void         drop();                          // forget everything waiting and give the memory back
    TelnetFilter telnet_;
    std::string  in_;
    std::vector<std::string> out_;  // the bytes still to go, in pieces; the first one is sent from sent_
    size_t       inPos_ = 0;  // next unread byte of in_
    size_t       sent_ = 0, queued_ = 0;
    size_t       batch_ = 0;      // bytes written since the last flush (a flush per 1 KB, as before hold mode)
    uint32_t     waitSince_ = 0;  // when the socket started taking nothing (the 10 s rule)
    bool         up_ = true, hold_ = false, blocked_ = false;
    uint32_t     linkTaken_ = 0, linkTakenMs_ = 0, bytesIn_ = 0, bytesOut_ = 0;
};

}  // namespace axis::net
