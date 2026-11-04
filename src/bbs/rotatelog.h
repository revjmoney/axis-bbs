// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// A log file kept small: lines go to <file> until it would pass the limit, then it becomes <old>
// (replacing the last one) and a fresh file starts. The two together never pass twice the limit.
#pragma once
#include <string>
#include <vector>

namespace axis::bbs {

class RotatingLog {
public:
    RotatingLog(std::string file, std::string old, size_t limit) : file_(std::move(file)), old_(std::move(old)), limit_(limit) {}
    void                     append(const std::string& line);
    std::vector<std::string> tail(size_t n) const;  // the newest n lines, oldest first

private:
    std::string file_, old_;
    size_t      limit_;
};

}  // namespace axis::bbs
