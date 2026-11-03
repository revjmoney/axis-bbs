// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include "door/guestmem.h"

#include <cstdlib>
#include <cstring>

namespace axis {
namespace door {

PagePool::PagePool(size_t budgetBytes) : budget_(budgetBytes / kPageSize) {}

PagePool::~PagePool() { releaseAll(); }

uint8_t* PagePool::alloc() {
    if (used_.size() >= budget_) return nullptr;
    uint8_t* p = (uint8_t*)std::malloc(kPageSize);
    if (!p) return nullptr;  // the heap ran out before the budget did
    used_.push_back(p);
    if (used_.size() > peak_) peak_ = used_.size();
    return p;
}

void PagePool::releaseAll() {
    for (uint8_t* p : used_) std::free(p);
    used_.clear();
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
    if (p.kind == ReadOnly) std::memcpy(r, p.ro, kPageSize);
    else std::memset(r, 0, kPageSize);
    p.ram = r;
    p.kind = Ram;
    int ram = 0;
    for (auto& q : pages_) ram += q.ram != nullptr;
    if (ram > peak_) peak_ = ram;
    return r;
}

uint8_t GuestMemory::read8(uint32_t a) {
    const Page& p = pages_[(a >> 12) & 0xFF];
    if (p.ram) return p.ram[a & 0xFFF];
    if (p.ro) return p.ro[a & 0xFFF];
    return p.kind == Zero ? 0 : 0xFF;
}

uint16_t GuestMemory::read16(uint32_t a) { return read8(a) | read8((a + 1) & 0xFFFFF) << 8; }

void GuestMemory::write8(uint32_t a, uint8_t v) {
    uint8_t* r = writable((a >> 12) & 0xFF);
    if (r) r[a & 0xFFF] = v;
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
