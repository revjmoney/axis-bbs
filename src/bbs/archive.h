// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// What's inside an archive, for [I]nfo in the file areas: ZIP, RAR (1.5 to 4, and 5), ARJ, TAR and
// GZIP, told apart by their first bytes. Listing only reads headers: nothing is unpacked, so it
// works for formats this board can't unpack (RAR's dictionaries are bigger than its RAM).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "bbs/filelib.h"

namespace axis::bbs {

struct ArchiveEntry {
    std::string name, method;  // method: "stored", "deflate", "rar m3", "arj 1" ...
    uint32_t    size = 0, packed = 0, crc = 0;
    uint16_t    date = 0;  // DOS date, 0 = unknown
    bool        dir = false;
};

struct ArchiveInfo {
    std::string               kind;  // "ZIP", "RAR", "RAR5", "ARJ", "TAR", "GZIP"; "" = not an archive
    std::vector<ArchiveEntry> entries;
};

ArchiveInfo listArchive(const std::string& name, RandomFile& file);

}  // namespace axis::bbs
