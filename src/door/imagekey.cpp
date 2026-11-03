// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include "door/imagekey.h"

namespace axis {
namespace door {

uint32_t imageKeyStart() { return 0x811C9DC5u; }  // FNV-1a 32-bit offset basis

uint32_t imageKeyAdd(uint32_t h, const uint8_t* p, size_t n) {
    for (size_t i = 0; i < n; i++) h = (h ^ p[i]) * 16777619u;
    return h;
}

uint32_t imageKeyFinish(uint32_t h, uint16_t loadSeg) {
    h = (h ^ (uint8_t)loadSeg) * 16777619u;  // the segment, low byte first
    return (h ^ (uint8_t)(loadSeg >> 8)) * 16777619u;
}

uint32_t imageKey(const uint8_t* exe, size_t len, uint16_t loadSeg) {
    return imageKeyFinish(imageKeyAdd(imageKeyStart(), exe, len), loadSeg);
}

}  // namespace door
}  // namespace axis
