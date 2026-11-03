// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The CRCs the file transfer protocols use (and ZIP: CRC-32 is the same one).
#pragma once
#include <cstddef>
#include <cstdint>

namespace axis::net {

uint16_t crc16Xmodem(uint16_t crc, const uint8_t* p, size_t n);  // CCITT polynomial 1021h, start 0
uint32_t crc32Update(uint32_t crc, const uint8_t* p, size_t n);  // start with 0xFFFFFFFF, finish with ~
inline uint32_t crc32(const uint8_t* p, size_t n) { return ~crc32Update(0xFFFFFFFFu, p, n); }

}  // namespace axis::net
