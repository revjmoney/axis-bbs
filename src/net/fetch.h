// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// Net Files fetch calls: a caller on this board downloads a file that lives on its home board. The
// caller's board dials the home board's telnet port and the file streams straight into the caller's
// ZMODEM or XMODEM sender; nothing is stored on the way. Both ends are pure state machines: the host
// feeds what arrives and sends what takeOut() gives.
//
// The wire (each line ends with CR LF; after OK the bytes are raw, then the home board closes):
//   dialer:  AXISFILE 1 <dialNonce16>
//   server:  HI <serverBoard16> <servNonce16> <sig128>    sig over "axisfile|<dialNonce>|<servNonce>|<serverBoard>"
//            or NO busy                                   (the host, when it has no free slot)
//   dialer:  GET <encoded NetFetch>                       (only once HI checks against the home board's pub)
//   server:  OK <bytes from offset to end>, then the bytes
//            or NO <word>   word: busy gone size member banned sig bad
#pragma once
#include "bbs/features.h"
#if AXIS_NETS && AXIS_TRANSFERS

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "net/record.h"

namespace axis::net {

constexpr size_t   kFetchWindow = 8 * 1024;          // the client's replay window (a power of two)
constexpr size_t   kFetchChunk = 2048;               // the server's read size
constexpr uint32_t kFetchStallMs = 20000;            // no bytes for this long ends a fetch (either end)
constexpr uint32_t kFetchConnectMs = 15000;          // connect plus handshake
constexpr int      kFetchRetries = 3;                // re-requests at a new offset per download
constexpr uint32_t kFetchNoCrc = 0xFFFFFFFFu;        // crcPos() when the crc can no longer be checked
constexpr size_t   kFetchMaxLine = 1024;             // a longer line fails the call (HI is 165, GET about 260)
static_assert((kFetchWindow & (kFetchWindow - 1)) == 0, "the ring indexes by offset");

// What the host found for a request.
struct ServeFile {
    bool        ok = false;
    const char* no = nullptr;  // the NO word when !ok (a static string)
    uint32_t    size = 0;      // the whole file
    std::function<bool(uint32_t off, uint8_t* buf, size_t n)> read;
};

// One inbound fetch call. The host's first-line dispatch consumes "AXISFILE 1 ": it feeds that text
// back in first, then the rest. A host with no free slot answers "NO busy\r\n" itself, with no server.
//
// Once serving, what the dialer sends (its keepalives) is dropped but counts as life: kFetchStallMs with
// nothing taken out and nothing heard means the dialer is gone.
//
// What is checked where (the server never streams before all of it passes):
//   - the server: the AXISFILE line, GET's decode (field rules and signature form), both nonces equal
//     this call's (a GET from an earlier call is refused "sig"), and offset <= size;
//   - check (the host, which holds the cards): verifyFetch(f, <the calling board's card pub>), then the
//     listing, area, size, membership, ban and block. check runs only after the nonces match, and must
//     answer no="sig" when verifyFetch fails. It should do nothing lasting beyond opening the file.
class FetchServer {
public:
    FetchServer(const std::string& myBoard, const KeyPair& key, const std::string& servNonce16,
                std::function<ServeFile(const NetFetch&)> check);
    ~FetchServer();  // wipes its copy of the key (already wiped once HI is signed)
    void        feed(const char* p, size_t n);
    std::string takeOut(size_t room);  // at most room bytes: pending lines, then at most kFetchChunk read from the file
    void        tick(uint32_t nowMs);  // handshake deadline kFetchConnectMs; kFetchStallMs with nothing taken out
    bool        finished() const;      // close the call (everything to send has been taken out)
    bool        failed() const;
    const char* why() const { return why_; }  // the NO word sent, or "timeout", "stall", "read"; "" when served

private:
    enum class St { Hello, Get, Serving, Done, Failed };
    void        line(const std::string& l);
    void        refuse(const char* word);
    std::string board_, serv_, dial_, in_, out_;
    KeyPair     key_;
    std::function<ServeFile(const NetFetch&)> check_;
    ServeFile   file_;
    St          st_ = St::Hello;
    const char* why_ = "";
    uint32_t    pos_ = 0, startMs_ = 0, lastMs_ = 0;
    bool        started_ = false, moved_ = false;
};

// One outbound fetch call. The host constructs it when it starts to dial and ticks it from then on
// (kFetchConnectMs covers the connect and the handshake). It feeds at most room() bytes at a time.
// While streaming it sends a one-byte keepalive (a newline) every kFetchStallMs / 2, so the home board
// can tell a slow caller (a radio link holding TCP back) from a dialer that is gone.
//
// The listing's signed crc (CRC-32 of the whole file) is checked: the client keeps a running CRC over
// the bytes it takes, and when the last byte arrives a mismatch fails the call "crc" before the last
// block is readable, so the caller's program never gets the end of a changed file. A re-request (a seek
// outside the window) passes the old client's crcState() and crcPos() to the new one: the new client
// ignores bytes before crcPos for the CRC and carries on from there, so a rewound download is still
// checked end to end. If the bytes ever skip past crcPos (a forward seek, say a receiver resuming its
// own partial file), the CRC cannot be completed: crcPos() then reads kFetchNoCrc and the end is not
// checked (verified() says which).
// Once streaming, the newest bytes stay in a kFetchWindow ring: the client takes bytes only up to
// kFetchWindow past where the sender last asked (ready or read), so TCP holds the home board back
// while the caller's sender is behind, and the sender's own block is never dropped.
class FetchClient {
public:
    FetchClient(const NetListing& l, const std::string& homePub, const std::string& handle, const KeyPair& me,
                const std::string& myBoard, const std::string& dialNonce16, uint32_t offset,
                uint32_t crcState = 0xFFFFFFFFu, uint32_t crcPos = 0);
    ~FetchClient();
    size_t      room() const;                  // bytes the host may feed now (0 while the ring is full, or done)
    size_t      feed(const char* p, size_t n);  // returns the bytes taken (at most room())
    std::string takeOut();
    void        tick(uint32_t nowMs);
    enum class St { Hello, Asked, Streaming, Done, Failed };  // Done: every byte has arrived (still readable)
    St          state() const { return st_; }
    const char* why() const { return why_; }  // the server's NO word, or "stall", "sig", "size", "crc", "bad", "memory"
    uint32_t    crcState() const { return crc_; }  // for the next client of a re-request
    uint32_t    crcPos() const { return crcOn_ ? crcPos_ : kFetchNoCrc; }
    bool        verified() const { return st_ == St::Done && crcOn_; }  // Done and the crc matched
    // [off, off+n) is in the window (or off is at or past the end of the file). Also notes off as where
    // the sender is, which is what lets the ring take more.
    bool ready(uint32_t off, size_t n) const;
    bool read(uint32_t off, uint8_t* buf, size_t n);  // copies from the window; false if not in it
    bool behind(uint32_t off) const;                  // before the window, or beyond what has arrived + window

private:
    void     line(const std::string& l);
    void     fail(const char* why);
    void     arrived();  // every byte is in: check the crc, then Done
    uint32_t low() const { return hi_ - start_ > kFetchWindow ? hi_ - (uint32_t)kFetchWindow : start_; }
    KeyPair  me_;
    std::string board_, homePub_, handle_, myBoard_, dial_, id_, in_, out_;
    std::unique_ptr<uint8_t[]> ring_;
    uint32_t size_, start_, hi_;  // the file's size, the first offset asked for, the end of what has arrived
    uint32_t listCrc_, crc_, crcPos_;  // the listing's crc; the running CRC-32 state over [0, crcPos_)
    bool     crcOn_;
    mutable uint32_t want_;       // where the sender last asked
    St          st_ = St::Hello;
    const char* why_ = "";
    uint32_t    startMs_ = 0, lastMs_ = 0, keepMs_ = 0;
    bool        started_ = false, heard_ = false;
};

}  // namespace axis::net

#endif  // AXIS_NETS && AXIS_TRANSFERS
