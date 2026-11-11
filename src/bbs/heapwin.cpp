// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include "bbs/heapwin.h"

namespace axis::bbs {

void HeapWindow::sample(uint32_t freeB, uint32_t largest, const char* doing) {
    if (freeB < lowFree) {
        lowFree = freeB;
        lowDoing = doing ? doing : "";
    }
    if (largest < lowLargest) lowLargest = largest;
}

void HeapWindow::reset() {
    lowFree = lowLargest = UINT32_MAX;
    lowDoing.clear();
}

}  // namespace axis::bbs
