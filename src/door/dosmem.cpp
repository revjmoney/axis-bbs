// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include "door/dosmem.h"

namespace axis {
namespace door {

DosMemory::DosMemory(GuestMemory& mem, uint16_t firstMcb, uint16_t topSeg) : mem_(mem), first_(firstMcb) {
    set(first_, 'Z', 0, (uint16_t)(topSeg - firstMcb - 1));
}

uint8_t  DosMemory::type(uint16_t m) { return mem_.read8(m * 16u); }
uint16_t DosMemory::owner(uint16_t m) { return mem_.read16(m * 16u + 1); }
uint16_t DosMemory::size(uint16_t m) { return mem_.read16(m * 16u + 3); }

void DosMemory::set(uint16_t m, uint8_t t, uint16_t o, uint16_t s) {
    mem_.write8(m * 16u, t);
    mem_.write16(m * 16u + 1, o);
    mem_.write16(m * 16u + 3, s);
}

void DosMemory::merge(uint16_t m) {
    while (type(m) == 'M') {
        uint16_t next = m + size(m) + 1;
        if (owner(next) != 0) break;
        set(m, type(next), 0, size(m) + size(next) + 1);
    }
}

bool DosMemory::alloc(uint16_t paras, uint16_t own, uint16_t& seg, uint16_t& largest) {
    largest = 0;
    for (uint16_t m = first_;; m = m + size(m) + 1) {
        uint8_t t = type(m);
        if (t != 'M' && t != 'Z') return false;  // chain damaged
        if (owner(m) == 0) {
            merge(m);
            uint16_t s = size(m);
            if (s >= paras) {
                if (s > paras) {  // split off the rest as a free block
                    set(m + paras + 1, type(m), 0, s - paras - 1);
                    set(m, 'M', own, paras);
                } else {
                    set(m, type(m), own, paras);
                }
                seg = m + 1;
                return true;
            }
            if (s > largest) largest = s;
        }
        if (type(m) == 'Z') return false;
    }
}

bool DosMemory::free(uint16_t seg) {
    for (uint16_t m = first_;; m = m + size(m) + 1) {
        uint8_t t = type(m);
        if (t != 'M' && t != 'Z') return false;
        if (m + 1 == seg) {
            set(m, t, 0, size(m));
            merge(m);
            return true;
        }
        if (t == 'Z' || m + 1 > seg) return false;
    }
}

bool DosMemory::resize(uint16_t seg, uint16_t paras, uint16_t& largest) {
    uint16_t m = seg - 1;
    uint8_t  t = type(m);
    if (t != 'M' && t != 'Z') return false;
    uint16_t s = size(m), own = owner(m);
    if (paras <= s) {
        if (paras < s) {
            set(m + paras + 1, t, 0, s - paras - 1);
            set(m, 'M', own, paras);
            merge(m + paras + 1);
        }
        return true;
    }
    // grow into the free blocks that follow, if they are big enough
    uint16_t avail = s;
    if (t == 'M') {
        uint16_t next = m + s + 1;
        if (owner(next) == 0) {
            merge(next);
            avail = s + size(next) + 1;
            if (avail >= paras) {
                uint8_t nt = type(next);
                set(m, nt, own, avail);
                if (avail > paras) {
                    set(m + paras + 1, nt, 0, avail - paras - 1);
                    set(m, 'M', own, paras);
                }
                return true;
            }
        }
    }
    largest = avail;
    return false;
}

}  // namespace door
}  // namespace axis
