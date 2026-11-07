// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// Where a door's relocated program image comes from. On the PC the session relocates it into RAM;
// on the CYD it is cached in the flash `door` partition and served read-only from there.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

#include "door/hostfs.h"
#include "door/mz.h"

namespace axis {
namespace door {

struct ImageSource {
    // reads the program from fs itself (in chunks: it may be bigger than free RAM), fills mz from its
    // header, and returns the image relocated for loadSeg, valid for the whole session; nullptr on failure
    virtual const uint8_t* image(HostFs& fs, const std::string& exe, uint16_t loadSeg, MzInfo& mz, size_t& len) = 0;
    // true when image() returned nullptr because a host allocation failed (not a missing/bad program)
    virtual bool exhaustedHeap() const { return false; }
    virtual ~ImageSource() = default;
};

}  // namespace door
}  // namespace axis
