// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// Sysop diagnostics that stay a few words on the existing @STATUS line. No secrets, and the
// only allocation is the short string returned when a sysop asks. Numbers come from the board;
// this file only names them.
#pragma once
#include <cstdint>
#include <string>

namespace axis::bbs {

// Last DOS door this board ran. "none" until the first one, "running" while it is open.
enum class DoorDiag { None, Running, Quit, OutOfMemory, Hangup, TimeLimit, LoadError };

struct DiagSample {
    uint32_t heapFree = 0;       // byte-addressable heap, bytes
    uint32_t largest = 0;        // largest free byte block
    uint32_t minHeap = 0;        // lowest byte heap seen since boot
    uint32_t stackWords = 0;     // free words at the high-water mark of the task that answered
    uint32_t doorPeakKb = 0;     // guest RAM pages the last door touched, in KB
    uint32_t doorStackWords = 0; // that same task's high-water mark when the door returned
    DoorDiag door = DoorDiag::None;
    int      resetCode = 0;  // esp_reset_reason_t as a number (0 unknown ... 10 sdio)
};

const char* doorDiagToken(DoorDiag d);
const char* resetToken(int resetCode);  // a single word, no spaces

// Fields added to the @STATUS line. Starts with a space. Every value is one token.
std::string diagFields(const DiagSample& s);

}  // namespace axis::bbs
