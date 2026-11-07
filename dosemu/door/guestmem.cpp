// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include "door/guestmem.h"

#include <cstdlib>
#include <cstring>

namespace axis {
namespace door {

PagePool::PagePool(size_t budgetBytes) : budget_(budgetBytes / kPageSize) {
    try {
        used_.reserve(budget_);  // recording a page must not need another heap block
    } catch (...) {
        // a tiny vector can still grow one pointer at a time; alloc() catches that
    }
}

PagePool::~PagePool() { freeUsed(); }

void PagePool::addArena(uint8_t* arena, size_t bytes) {
    if (!arena || bytes < kPageSize) return;
    arenas_.emplace_back(arena, bytes / kPageSize * kPageSize);
    for (size_t off = bytes / kPageSize * kPageSize; off > 0; off -= kPageSize) spare_.push_back(arena + off - kPageSize);
}

bool PagePool::lent(const uint8_t* p) const {
    for (auto& a : arenas_)
        if (p >= a.first && p < a.first + a.second) return true;
    return false;
}

uint8_t* PagePool::alloc() {
    if (used_.size() >= budget_) return nullptr;
    uint8_t* p = nullptr;
    bool     heap = false;
    if (!spare_.empty()) {
        p = spare_.back();
        spare_.pop_back();
    } else {
        p = (uint8_t*)std::malloc(kPageSize);
        if (!p) return nullptr;  // the heap ran out before the budget did
        heap = true;
    }
    if (failTrack_) {  // the page was had, then recording it failed: give it back
        failTrack_ = false;
        if (heap) std::free(p);
        else spare_.push_back(p);
        return nullptr;
    }
    try {
        used_.push_back(p);
    } catch (...) {
        if (heap) std::free(p);
        else {
            try {
                spare_.push_back(p);
            } catch (...) {
                // the lent page stays inside its arena; releaseAll rebuilds the spare list
            }
        }
        return nullptr;
    }
    if (used_.size() > peak_) peak_ = used_.size();
    return p;
}

void PagePool::freeUsed() {
    for (uint8_t* p : used_)
        if (!lent(p)) std::free(p);
    used_.clear();
    spare_.clear();
}

void PagePool::releaseAll() {
    freeUsed();
    try {
        for (auto& a : arenas_)
            for (size_t off = a.second; off > 0; off -= kPageSize) spare_.push_back(a.first + off - kPageSize);
    } catch (...) {
        spare_.clear();
    }
}

GuestMemory::GuestMemory(PagePool& pool) : pool_(pool) {}

void GuestMemory::mapReadOnly(uint32_t addr, const uint8_t* src, uint32_t len) {
    for (uint32_t off = 0; off < len; off += kPageSize) {
        Page& p = pages_[((addr + off) >> 12) & 0xFF];
        p.ro = src + off;
        p.ram = nullptr;
        p.kind = ReadOnly;
    }
}

void GuestMemory::mapZero(uint32_t addr, uint32_t len) {
    for (uint32_t off = 0; off < len; off += kPageSize) {
        Page& p = pages_[((addr + off) >> 12) & 0xFF];
        p.ro = nullptr;
        p.ram = nullptr;
        p.kind = Zero;
    }
}

void GuestMemory::mapRam(uint32_t addr, uint32_t len) {
    for (uint32_t off = 0; off < len; off += kPageSize) {
        int page = ((addr + off) >> 12) & 0xFF;
        pages_[page].kind = Zero;
        writable(page);
    }
}

uint8_t* GuestMemory::writable(int page) {
    Page& p = pages_[page];
    if (p.ram) return p.ram;
    if (p.kind == Unmapped) return nullptr;
    uint8_t* r = pool_.alloc();
    if (!r) {
        if (!oom_ && onOom_) onOom_();
        oom_ = true;
        return nullptr;
    }
    volatile uint32_t* w = (volatile uint32_t*)r;  // word stores only (see the header)
    if (p.kind == ReadOnly)
        for (int i = 0; i < kPageSize / 4; i++)
            w[i] = p.ro[i * 4] | p.ro[i * 4 + 1] << 8 | p.ro[i * 4 + 2] << 16 | (uint32_t)p.ro[i * 4 + 3] << 24;
    else
        for (int i = 0; i < kPageSize / 4; i++) w[i] = 0;
    p.ram = r;
    p.kind = Ram;
    int ram = 0;
    for (auto& q : pages_) ram += q.ram != nullptr;
    if (ram > peak_) peak_ = ram;
    return r;
}

uint8_t GuestMemory::read8(uint32_t a) {
    const Page& p = pages_[(a >> 12) & 0xFF];
    if (p.ram) return (uint8_t)(((const volatile uint32_t*)p.ram)[(a & 0xFFF) >> 2] >> ((a & 3) * 8));
    if (p.ro) return p.ro[a & 0xFFF];
    return p.kind == Zero ? 0 : 0xFF;
}

uint16_t GuestMemory::read16(uint32_t a) { return read8(a) | read8((a + 1) & 0xFFFFF) << 8; }

void GuestMemory::write8(uint32_t a, uint8_t v) {
    uint8_t* r = writable((a >> 12) & 0xFF);
    if (!r) return;
    volatile uint32_t& w = ((volatile uint32_t*)r)[(a & 0xFFF) >> 2];
    unsigned           shift = (a & 3) * 8;
    w = (w & ~(0xFFu << shift)) | (uint32_t)v << shift;
}

void GuestMemory::write16(uint32_t a, uint16_t v) {
    write8(a, (uint8_t)v);
    write8((a + 1) & 0xFFFFF, v >> 8);
}

void GuestMemory::readBlock(uint32_t addr, void* dst, size_t n) {
    uint8_t* d = (uint8_t*)dst;
    for (size_t i = 0; i < n; i++) d[i] = read8((addr + i) & 0xFFFFF);
}

void GuestMemory::writeBlock(uint32_t addr, const void* src, size_t n) {
    const uint8_t* s = (const uint8_t*)src;
    for (size_t i = 0; i < n; i++) write8((addr + i) & 0xFFFFF, s[i]);
}

GuestMemory::Stats GuestMemory::stats() const {
    Stats s{0, 0, peak_};
    for (auto& p : pages_) {
        s.ramPages += p.ram != nullptr;
        s.roPages += p.kind == ReadOnly;
    }
    return s;
}

}  // namespace door
}  // namespace axis
