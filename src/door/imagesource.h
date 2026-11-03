// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// Where a door's relocated program image comes from. On the PC the session relocates it into RAM;
// on the CYD it is cached in the flash `door` partition and served read-only from there.
#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

#include "door/mz.h"

namespace axis {
namespace door {

struct ImageSource {
    // the image relocated for loadSeg, valid for the whole session; nullptr if it can't be provided
    virtual const uint8_t* image(const std::vector<uint8_t>& exe, const MzInfo& mz, uint16_t loadSeg, size_t& len) = 0;
    virtual ~ImageSource() = default;
};

}  // namespace door
}  // namespace axis
