// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The board's internal flash filesystem. On Linux it becomes the data folder's flash/ (plan task 2): until then
// it does not mount.
#pragma once
#include <cstddef>

#include "FS.h"

namespace fs {
class LittleFSFS : public FS {
public:
    bool   begin(bool formatOnFail = false, const char* basePath = "/littlefs", uint8_t maxOpen = 10,
                 const char* label = "spiffs") {
        (void)formatOnFail, (void)basePath, (void)maxOpen, (void)label;
        return false;
    }
    void   end() {}
    bool   format() { return false; }
    size_t totalBytes() { return 0; }
    size_t usedBytes() { return 0; }
};
}  // namespace fs

inline fs::LittleFSFS LittleFS;
