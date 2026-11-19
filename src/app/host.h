// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// What the shared BBS code (src/app) asks of the program it runs in: the clock, random numbers, the event log and the
// storage folders. The board answers through platform/cyd/boardhost.h; the PC program through its own host. No Arduino
// header and no ESP API here or anywhere in src/app.
#pragma once
#include <cstdint>
#include <string>

// The board's loop stack depends on some session functions staying out of their callers: noinline there, nothing on the PC.
#if defined(AXIS_BOARD) || defined(ESP_PLATFORM)
#define AXIS_NOINLINE __attribute__((noinline))
#else
#define AXIS_NOINLINE
#endif

namespace axis::app {

struct Host {
    virtual uint32_t    nowMs() = 0;                          // millis()
    virtual uint32_t    unixNow() = 0;                        // 0 when the clock is not set
    virtual uint32_t    random32() = 0;                       // hwRandom()
    virtual void        event(const std::string& line) = 0;   // logEvent: the Logs screen and the event file
    virtual std::string sdRoot() = 0;                         // storageRoot(): "/sd" on the board, <data>/sd on the PC
    virtual bool        hasSd() = 0;
    virtual uint64_t    sdFreeBytes() = 0;
    virtual std::string flashRoot() = 0;                      // "/littlefs" on the board, <data>/flash on the PC
    virtual bool        roomFor(uint32_t /*minFree*/, uint32_t /*minLargest*/) { return true; }  // heap floors (board only)
    // The board's "heap low in ..." notes (platform/cyd/heapscope.h): the lowest free heap so far, and a line when it went
    // lower than `mark` inside the named code. Nothing on the PC.
    virtual uint32_t    heapMark() { return 0; }
    virtual void        heapNote(const char* /*where*/, uint32_t /*mark*/) {}
    virtual ~Host() = default;
};

// HeapScope for the shared code: names the code that pushed the board's heap to a new low.
class HeapWatch {
public:
    HeapWatch(Host& h, const char* where) : host_(h), where_(where), mark_(h.heapMark()) {}
    ~HeapWatch() { host_.heapNote(where_, mark_); }
    HeapWatch(const HeapWatch&) = delete;
    HeapWatch& operator=(const HeapWatch&) = delete;

private:
    Host&       host_;
    const char* where_;
    uint32_t    mark_;
};

}  // namespace axis::app
