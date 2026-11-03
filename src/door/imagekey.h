// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// Fingerprint of a door program as it would be cached in flash: FNV-1a 32-bit over the EXE bytes,
// then the two bytes of the load segment. A different EXE or layout means a re-flash.
#pragma once
#include <cstddef>
#include <cstdint>

namespace axis {
namespace door {

uint32_t imageKey(const uint8_t* exe, size_t len, uint16_t loadSeg);
// the same key fed a piece at a time (the board reads the EXE in chunks)
uint32_t imageKeyStart();
uint32_t imageKeyAdd(uint32_t h, const uint8_t* p, size_t n);
uint32_t imageKeyFinish(uint32_t h, uint16_t loadSeg);

}  // namespace door
}  // namespace axis
