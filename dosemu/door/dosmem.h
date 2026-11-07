// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// DOS memory blocks (INT 21h 48h/49h/4Ah) as a real MCB chain in guest memory, like DOS keeps it:
// each block is preceded by a 16-byte header: 'M' or 'Z' (last), owner PSP, size in paragraphs.
#pragma once
#include <cstdint>

#include "door/guestmem.h"

namespace axis {
namespace door {

class DosMemory {
public:
    DosMemory(GuestMemory& mem, uint16_t firstMcb, uint16_t topSeg);  // one free block from firstMcb to topSeg
    bool alloc(uint16_t paras, uint16_t owner, uint16_t& seg, uint16_t& largest);
    bool free(uint16_t seg);
    bool resize(uint16_t seg, uint16_t paras, uint16_t& largest);

private:
    uint8_t  type(uint16_t mcb);
    uint16_t owner(uint16_t mcb);
    uint16_t size(uint16_t mcb);
    void     set(uint16_t mcb, uint8_t type, uint16_t owner, uint16_t size);
    void     merge(uint16_t mcb);  // join a free block with the free blocks after it
    GuestMemory& mem_;
    uint16_t     first_;
};

}  // namespace door
}  // namespace axis
