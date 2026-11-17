// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The board's internal flash filesystem: the data folder's flash/ (made by axisDataSetup before setup() runs).
#pragma once
#include <cstddef>
#include <cstdint>

#include "FS.h"

namespace fs {
class LittleFSFS : public FS {
public:
    constexpr LittleFSFS() : FS("/littlefs") {}
    // mounted when the data folder's flash/ is there (it always is once the program has started)
    bool   begin(bool formatOnFail = false, const char* basePath = "/littlefs", uint8_t maxOpen = 10,
                 const char* label = "spiffs");
    void   end() {}
    bool   format() { return false; }  // never empties a folder on the PC
    size_t totalBytes() { return (size_t)total(); }
    size_t usedBytes() { return (size_t)used(); }
};
}  // namespace fs

inline fs::LittleFSFS LittleFS;
