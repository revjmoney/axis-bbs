// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The door's 1 MB real-mode address space as 256 pages of 4 KB. A page is either served
// read-only from a buffer (the door's code in flash), backed by RAM, zero until first written,
// or unmapped. RAM pages come one at a time from a budgeted pool (no big buffer, so a
// fragmented heap is fine). This is the hook where SD swap plugs in later.
#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

#include "door/membus.h"

namespace axis {
namespace door {

constexpr uint32_t kPageSize = 4096;
constexpr int      kPageCount = 256;

class PagePool {
public:
    explicit PagePool(size_t budgetBytes);
    // Memory lent by the host (the CYD's screen copy, its spare instruction RAM): used for pages
    // before the heap, never freed here. Must be 4-byte aligned; pages are only touched as words.
    void addArena(uint8_t* arena, size_t bytes);
    ~PagePool();
    uint8_t* alloc();  // nullptr when the budget or the heap is used up (never throws)
    void     releaseAll();
    // Test hook: the next alloc takes a page, then fails while recording it and gives that page back.
    void     failNextTrack() { failTrack_ = true; }
    size_t   usedPages() const { return used_.size(); }
    size_t   peakPages() const { return peak_; }
    size_t   budgetPages() const { return budget_; }

private:
    void                  freeUsed();
    bool                  lent(const uint8_t* p) const;
    size_t                budget_, peak_ = 0;
    bool                  failTrack_ = false;
    std::vector<uint8_t*> used_, spare_;  // spare_: lent pages not handed out yet
    std::vector<std::pair<uint8_t*, size_t>> arenas_;
};

// Guest RAM pages are read and written only as aligned 32-bit words, so they can live in the
// ESP32's instruction RAM (which faults on byte access) as well as in ordinary RAM.
class GuestMemory : public MemoryBus {
public:
    explicit GuestMemory(PagePool& pool);
    void mapReadOnly(uint32_t addr, const uint8_t* src, uint32_t len);  // page-aligned addr
    void mapZero(uint32_t addr, uint32_t len);
    void mapRam(uint32_t addr, uint32_t len);
    void readBlock(uint32_t addr, void* dst, size_t n);
    void writeBlock(uint32_t addr, const void* src, size_t n);
    bool outOfMemory() const { return oom_; }
    void onOutOfMemory(std::function<void()> f) { onOom_ = std::move(f); }  // e.g. stop the CPU at once

    struct Stats {
        int ramPages, roPages, peakRamPages;
    };
    Stats stats() const;

    uint8_t  read8(uint32_t a) override;
    uint16_t read16(uint32_t a) override;
    void     write8(uint32_t a, uint8_t v) override;
    void     write16(uint32_t a, uint16_t v) override;

private:
    enum Kind : uint8_t { Unmapped, ReadOnly, Ram, Zero };
    struct Page {
        const uint8_t* ro = nullptr;
        uint8_t*       ram = nullptr;
        Kind           kind = Unmapped;
    };
    uint8_t* writable(int page);  // RAM for this page, allocating or copying as needed; nullptr on OOM/unmapped
    PagePool& pool_;
    Page      pages_[kPageCount];
    int       peak_ = 0;
    bool      oom_ = false;
    std::function<void()> onOom_;
};

}  // namespace door
}  // namespace axis
