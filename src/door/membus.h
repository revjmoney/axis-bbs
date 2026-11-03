// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// How the 8086 core reaches guest memory. Addresses are already masked to 20 bits.
#pragma once
#include <stdint.h>

namespace axis {
namespace door {

struct MemoryBus {
    virtual uint8_t  read8(uint32_t a) = 0;
    virtual uint16_t read16(uint32_t a) = 0;
    virtual void     write8(uint32_t a, uint8_t v) = 0;
    virtual void     write16(uint32_t a, uint16_t v) = 0;
    virtual ~MemoryBus() = default;
};

}  // namespace door
}  // namespace axis
