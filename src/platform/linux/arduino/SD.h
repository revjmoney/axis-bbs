// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The board's SD card: the data folder's sd/. It is always there, so it reads as a mounted SDHC card whose size
// is the size of the filesystem the data folder is on.
#pragma once
#include <cstdint>

#include "FS.h"

typedef enum { CARD_NONE, CARD_MMC, CARD_SD, CARD_SDHC, CARD_UNKNOWN } sdcard_type_t;

namespace fs {
class SDFS : public FS {
public:
    constexpr SDFS() : FS("/sd") {}
    void     end() {}
    uint8_t  cardType() { return CARD_SDHC; }
    uint64_t cardSize() { return total(); }
    uint64_t totalBytes() { return total(); }
    uint64_t usedBytes() { return used(); }
};
}  // namespace fs

inline fs::SDFS SD;
