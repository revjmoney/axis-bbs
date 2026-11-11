// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The lowest heap seen since the sysop last reset the window, and what the board was doing when the
// free bytes were lowest. Portable so the PC tests can drive it.
#pragma once
#include <cstdint>
#include <string>

namespace axis::bbs {

struct HeapWindow {
    uint32_t    lowFree = UINT32_MAX, lowLargest = UINT32_MAX;
    std::string lowDoing;  // `doing` at the moment lowFree was set

    void sample(uint32_t freeB, uint32_t largest, const char* doing);
    void reset();
};

}  // namespace axis::bbs
