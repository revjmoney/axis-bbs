// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// DOS .EXE (MZ) loading at a fixed segment, so the relocated image can be kept in flash
// and served read-only.
#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
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

// The same relocation done a chunk at a time, for images too big for RAM. relocTargets turns `count`
// raw table entries into image offsets, dropping any whose word doesn't fit in `imageLen` (padded).
// relocateChunk patches the chunk at image offset `base`; byteAt(off) gives an unrelocated image
// byte (0 past the image) for a word that straddles two chunks.
std::vector<uint32_t> relocTargets(const uint8_t* table, uint32_t count, size_t imageLen);
void relocateChunk(uint8_t* chunk, uint32_t base, size_t chunkLen, const std::vector<uint32_t>& targets,
                   uint16_t loadSeg, const std::function<uint8_t(uint32_t)>& byteAt);

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
