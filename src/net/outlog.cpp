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

void FileOutLog::open() {
    base_ = 0;
    count_ = 0;
    if (io_.loadBase) {
        std::string b = io_.loadBase();
        base_ = (uint32_t)std::strtoul(b.c_str(), nullptr, 10);
    }
    if (io_.lineCount) count_ = io_.lineCount();
}

uint32_t FileOutLog::append(const std::string& entry) {
    if (!oneLine(entry) || !io_.append || next() == kOutLogFail) return kOutLogFail;
    if (!io_.append(entry)) return kOutLogFail;
    count_++;
    return next() - 1;
}

bool FileOutLog::read(uint32_t index, std::string& entry) {
    if (index < base_ || index >= next() || !io_.readLine) return false;
    return io_.readLine(index - base_, entry);
}

void FileOutLog::trimTo(uint32_t keepLast) {
    if (count_ <= keepLast || !io_.dropFront) return;
    uint32_t skip = count_ - keepLast;
    // The base is saved first: a crash in between makes a few indexes point one entry off, which
    // costs at most a resend or a skip (dedupe makes resends harmless).
    if (io_.saveBase) io_.saveBase(std::to_string(base_ + skip));
    if (!io_.dropFront(skip)) {
        if (io_.saveBase) io_.saveBase(std::to_string(base_));
        return;
    }
    base_ += skip;
    count_ = keepLast;
}

}  // namespace axis::net
