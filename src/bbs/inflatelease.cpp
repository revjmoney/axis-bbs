// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#pragma GCC optimize("Os")  // menus and screens: size over speed (the door CPU and transfers stay -O2)
#include "bbs/inflatelease.h"

#include <cstdlib>

#include "bbs/zipreader.h"

namespace axis::bbs::inflate {

namespace {
const void* owner_ = nullptr;
uint8_t*    window_ = nullptr;
uint8_t*    state_ = nullptr;
}  // namespace

bool take(const void* owner, uint8_t*& window, uint8_t*& state) {
    if (owner_ && owner_ != owner) return false;
    if (!owner_) {
        window_ = (uint8_t*)std::malloc(kWindow);
        state_ = window_ ? (uint8_t*)std::malloc(ZipReader::kStateBytes) : nullptr;
        if (!state_) {  // short: nothing kept
            std::free(window_);
            window_ = nullptr;
            return false;
        }
        owner_ = owner;
    }
    window = window_;
    state = state_;
    return true;
}

void give(const void* owner) {
    if (!owner_ || owner_ != owner) return;
    std::free(window_);
    std::free(state_);
    window_ = state_ = nullptr;
    owner_ = nullptr;
}

bool busy() { return owner_ != nullptr; }

}  // namespace axis::bbs::inflate
