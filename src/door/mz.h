// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// DOS .EXE (MZ) loading at a fixed segment, so the relocated image can be kept in flash
// and served read-only.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "door/dosmem.h"
#include "door/guestmem.h"

namespace axis {
namespace door {

// Fixed guest layout: environment below the PSP, PSP at 01F0h, program at 0200h (linear 2000h,
// page aligned so the image maps straight onto 4 KB pages), conventional memory ends at A000h.
constexpr uint16_t kEnvSeg = 0x0180, kPspSeg = 0x01F0, kLoadSeg = 0x0200, kTopSeg = 0xA000;

struct MzInfo {
    uint32_t imageSize, minAlloc, maxAlloc, headerSize;
    uint16_t relocCount, relocTable, cs, ip, ss, sp;
};

bool                 parseMz(const uint8_t* file, size_t len, MzInfo& out);
std::vector<uint8_t> relocate(const uint8_t* file, size_t len, const MzInfo& mz, uint16_t loadSeg);  // padded to whole pages

struct LoadedProgram {
    uint16_t psp, cs, ip, ss, sp;
};

// Maps the image read-only at kLoadSeg, the rest of conventional memory zero-on-write, builds the
// PSP and environment, and creates the DOS memory chain (returned in `dm`, owned by the caller).
// `image` must outlive the guest memory. False if the program doesn't fit.
bool loadProgram(GuestMemory& mem, DosMemory*& dm, const uint8_t* image, size_t imageLen, const MzInfo& mz,
                 const std::string& cmdTail, const std::string& progPath, LoadedProgram& out);
inline bool loadProgram(GuestMemory& mem, DosMemory*& dm, const std::vector<uint8_t>& image, const MzInfo& mz,
                        const std::string& cmdTail, const std::string& progPath, LoadedProgram& out) {
    return loadProgram(mem, dm, image.data(), image.size(), mz, cmdTail, progPath, out);
}

}  // namespace door
}  // namespace axis
