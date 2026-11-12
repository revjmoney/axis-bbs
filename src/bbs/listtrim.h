// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The board's plain lists (rumors, ballots ...) are appended a line at a time and trimmed lazily: the file may hold up to
// cap + cap/4 lines, readers see the newest cap. These are the two decisions, portable so the PC tests check the same rule.
#pragma once
#include <cstddef>

namespace axis::bbs {

// After an append left `lines` in the file: how many of the oldest to drop now so the newest cap remain (0: not yet).
inline size_t listTrimSkip(size_t lines, size_t cap) { return cap && lines > cap + cap / 4 ? lines - cap : 0; }
// A reader of a file holding `lines`: how many of the oldest to pass over so it sees the newest cap (cap 0: all of them).
inline size_t listReadSkip(size_t lines, size_t cap) { return cap && lines > cap ? lines - cap : 0; }

}  // namespace axis::bbs
