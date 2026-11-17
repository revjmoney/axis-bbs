// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The ESP object on Linux. The heap figures are fixed and large (64 MiB): a PC has room, and the board's
// memory guards (which refuse work below a few KB) must never trip here.
#pragma once
#include <cstdint>

constexpr uint32_t kHostHeapBytes = 64u << 20;

class EspClass {
public:
    [[noreturn]] void restart();  // the process ends with exit code 3, so systemd starts it again
    uint32_t getFreeHeap() { return kHostHeapBytes; }
    uint32_t getMinFreeHeap() { return kHostHeapBytes; }
    uint32_t getMaxAllocHeap() { return kHostHeapBytes; }
    uint32_t getHeapSize() { return kHostHeapBytes; }
    uint32_t getFlashChipSize() { return 0; }  // no flash chip on a PC
    uint32_t getCpuFreqMHz() { return 0; }     // not looked up (yet)
    const char* getChipModel() { return "Linux"; }
};

extern EspClass ESP;
