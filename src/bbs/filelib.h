// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// What the BBS knows about file areas: the files in an area and a way to read a text file.
// The board reads its SD card (/sd/files/<id>/); tests use a pretend store.
#pragma once
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace axis::bbs {

struct FileEntry {
    std::string name;
    uint32_t    size = 0;
    std::string desc;  // from the area's FILES.BBS
};

class FileLibrary {
public:
    virtual std::vector<FileEntry> list(const std::string& areaId) = 0;
    // up to `max` bytes of a file; false if it isn't there
    virtual bool read(const std::string& areaId, const std::string& name, std::string& out, size_t max) = 0;
    virtual ~FileLibrary() = default;
};

// An area listing: files in FILES.BBS order with their descriptions ("NAME.EXT  description" per
// line), then any other files in the folder. Names match in any case and keep the folder's spelling;
// FILES.BBS itself and described files that are gone are left out.
std::vector<FileEntry> mergeFilesBbs(const std::string& filesBbs,
                                     const std::vector<std::pair<std::string, uint32_t>>& folder);

std::string fileSizeText(uint32_t bytes);  // "512", "48K", "21.0M"
bool        isTextFile(const std::string& name);  // .TXT .NFO .DIZ .ANS .ASC .BBS .DOC .MD .1ST

}  // namespace axis::bbs
