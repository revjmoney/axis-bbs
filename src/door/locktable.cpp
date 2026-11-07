// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// Byte-range locks shared by the native doors and the vestigial DOS runtime (dosemu/).
// The method bodies live here so the firmware still has the LockTable vtable after dos.cpp moved out.
#include "door/hostfs.h"

namespace axis {
namespace door {

bool LockTable::lock(int key, uint32_t off, uint32_t len, int owner) {
    (void)owner;
    for (auto& l : locks_)
        if (l.key == key && off < l.off + l.len && l.off < off + len) return false;
    locks_.push_back({key, off, len, owner});
    return true;
}

bool LockTable::unlock(int key, uint32_t off, uint32_t len, int owner) {
    for (size_t i = 0; i < locks_.size(); i++)
        if (locks_[i].key == key && locks_[i].off == off && locks_[i].len == len && locks_[i].owner == owner) {
            locks_.erase(locks_.begin() + i);
            return true;
        }
    return false;
}

void LockTable::releaseOwner(int owner) {
    for (size_t i = locks_.size(); i-- > 0;)
        if (locks_[i].owner == owner) locks_.erase(locks_.begin() + i);
}

}  // namespace door
}  // namespace axis
