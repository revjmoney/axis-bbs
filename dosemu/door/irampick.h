// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// Pick one block of pure instruction RAM for guest pages. ESP-IDF's EXEC allocator also offers the
// shared D/IRAM heap (byte memory, returned through its IRAM alias, and 4 bytes larger than asked).
// Those blocks are set aside so the next-largest block can be pure IRAM, then given back. A failed
// take is retried a page smaller instead of stopping the search.
#pragma once
#include <cstddef>

namespace axis::door {

struct ExecTake {
    void*  ptr = nullptr;  // nullptr: this size could not be taken
    size_t bytes = 0;      // usable bytes when ptr is set
    bool   shared = false; // also byte-addressable heap (do not keep)
};

struct IramPick {
    void*  ptr = nullptr;
    size_t bytes = 0;
    int    sharedHeld = 0;  // D/IRAM blocks borrowed and returned
    int    misses = 0;      // a size that could not be taken even one page smaller
};

// largest() is the biggest EXEC block in bytes. take(n) allocates n bytes. give(p) frees a block
// this search set aside. At most 8 shared blocks are held at once.
template <class Largest, class Take, class Give>
IramPick pickInstructionRam(Largest largest, Take take, Give give, int maxAside = 8) {
    if (maxAside > 8) maxAside = 8;
    if (maxAside < 1) maxAside = 1;
    void*  aside[8];
    int    held = 0;
    int    misses = 0;
    void*  keep = nullptr;
    size_t keepN = 0;
    while (held < maxAside && misses < 1) {
        size_t raw = largest();
        size_t b = raw / 4096u * 4096u;
        if (b < 4096u) break;
        ExecTake got = take(b);
        if (!got.ptr && b >= 8192u) {  // D/IRAM EXEC takes 4 extra bytes: one page smaller still grabs it
            b -= 4096u;
            got = take(b);
        }
        if (!got.ptr && b >= 8192u) {
            b -= 4096u;
            got = take(b);
        }
        if (!got.ptr) {
            misses++;
            break;
        }
        if (got.shared) {
            aside[held++] = got.ptr;
            continue;
        }
        keep = got.ptr;
        keepN = got.bytes ? got.bytes : b;
        break;
    }
    int shared = held;
    while (held) give(aside[--held]);
    return {keep, keepN, shared, misses};
}

}  // namespace axis::door
