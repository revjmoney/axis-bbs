// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The newest board events (calls, doors, hang-ups, sysop actions) for the sysop's Logs screen.
#pragma once
#include <deque>
#include <string>

namespace axis::bbs {

class EventLog {
public:
    explicit EventLog(size_t keep = 40) : keep_(keep) {}
    void add(const std::string& line) {
        lines_.push_back(line);
        while (lines_.size() > keep_) lines_.pop_front();
    }
    const std::deque<std::string>& lines() const { return lines_; }

private:
    size_t                  keep_;
    std::deque<std::string> lines_;
};

}  // namespace axis::bbs
