// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The net's outbound log, in RAM and in a file.
#include "net/outlog.h"

#include <cstdlib>

namespace axis::net {

namespace {
bool oneLine(const std::string& s) { return s.find_first_of("\r\n") == std::string::npos; }
}  // namespace

// ---- MemOutLog ----

uint32_t MemOutLog::append(const std::string& entry) {
    if (!oneLine(entry) || next() == kOutLogFail) return kOutLogFail;
    entries_.push_back(entry);
    return next() - 1;
}

bool MemOutLog::read(uint32_t index, std::string& entry) {
    if (index < first_ || index >= next()) return false;
    entry = entries_[index - first_];
    return true;
}

void MemOutLog::trimTo(uint32_t keepLast) {
    while (entries_.size() > keepLast) {
        entries_.pop_front();
        first_++;
    }
}

// ---- FileOutLog ----

bool FileOutLog::open() {
    base_ = 0;
    count_ = 0;
    header_ = false;
    ok_ = false;
    uint32_t n = io_.lineCount ? io_.lineCount() : 0;
    if (n == 0) {  // empty or missing: the header is written with the first entry
        ok_ = true;
        return true;
    }
    std::string h;
    if (!io_.readLine || !io_.readLine(0, h)) return false;  // could not read: leave the file alone
    if (!h.empty() && h[0] == '#') {
        base_ = (uint32_t)std::strtoul(h.c_str() + 1, nullptr, 10);
        count_ = n - 1;
    } else {  // read fine but no header: the whole file is entries from 0
        if (!io_.replace || !io_.replace("#0", 0)) return false;
        count_ = n;
    }
    header_ = true;
    ok_ = true;
    return true;
}

uint32_t FileOutLog::append(const std::string& entry) {
    if (!ok_ || !oneLine(entry) || !io_.append || next() == kOutLogFail) return kOutLogFail;
    if (!header_) {
        if (count_ != 0 || !io_.append("#" + std::to_string(base_))) return kOutLogFail;
        header_ = true;
    }
    if (!io_.append(entry)) return kOutLogFail;
    count_++;
    return next() - 1;
}

bool FileOutLog::read(uint32_t index, std::string& entry) {
    if (!ok_ || !header_ || index < base_ || index >= next() || !io_.readLine) return false;
    return io_.readLine(index - base_ + 1, entry);
}

void FileOutLog::trimTo(uint32_t keepLast) {
    if (!ok_ || !header_ || count_ <= keepLast || !io_.replace) return;
    uint32_t skip = count_ - keepLast;
    // One file swap carries both the new base (in its header) and the kept entries, so the two
    // never disagree. If the swap fails, nothing changes.
    if (!io_.replace("#" + std::to_string(base_ + skip), skip + 1)) return;
    base_ += skip;
    count_ = keepLast;
}

}  // namespace axis::net
