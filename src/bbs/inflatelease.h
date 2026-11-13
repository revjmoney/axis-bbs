// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The board's one ZIP inflate window (32 KB plus ZipReader::kStateBytes), lent to one owner at a
// time: an ANSI viewer while a deflated piece plays, or a TransferJob while it checks a ZIP upload.
// Allocated on the first take and freed on give, so nothing is held at idle. Loop task only.
#pragma once
#include <cstddef>
#include <cstdint>

#include "bbs/features.h"

namespace axis::bbs::inflate {

constexpr size_t kWindow = 32768;

#if AXIS_ANSIVIEW || AXIS_TRANSFERS
// The window and state for owner: true when lent (again, to the owner holding it). False when another
// owner holds them (busy() is then true) or the heap is short (nothing is kept).
bool take(const void* owner, uint8_t*& window, uint8_t*& state);
void give(const void* owner);  // frees both; a give by anyone but the owner does nothing
bool busy();                   // lent right now
#else
// Neither the ANSI viewer nor transfers are in this build (features.h): nothing ever borrows the window.
inline bool take(const void*, uint8_t*&, uint8_t*&) { return false; }
inline void give(const void*) {}
inline bool busy() { return false; }
#endif

}  // namespace axis::bbs::inflate
