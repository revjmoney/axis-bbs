// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The board's SD card. On Linux it becomes the data folder's sd/ (plan task 2): until then there is no card.
#pragma once
#include <cstdint>

#include "FS.h"

typedef enum { CARD_NONE, CARD_MMC, CARD_SD, CARD_SDHC, CARD_UNKNOWN } sdcard_type_t;

namespace fs {
class SDFS : public FS {
public:
    void     end() {}
    uint8_t  cardType() { return CARD_NONE; }
    uint64_t cardSize() { return 0; }
    uint64_t totalBytes() { return 0; }
    uint64_t usedBytes() { return 0; }
};
}  // namespace fs

inline fs::SDFS SD;
