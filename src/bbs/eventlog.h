// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The newest board events (calls, doors, hang-ups, sysop actions) for the sysop's Logs screen,
// at most `keep` lines and `maxBytes` bytes (the board keeps the full history in a RotatingLog).
#pragma once
#include <deque>
#include <string>

namespace axis::bbs {

class EventLog {
public:
    explicit EventLog(size_t keep = 40, size_t maxBytes = 2048) : keep_(keep), maxBytes_(maxBytes) {}
    void add(const std::string& line) {
        lines_.push_back(line);
        bytes_ += line.size();
        while (lines_.size() > 1 && (lines_.size() > keep_ || bytes_ > maxBytes_)) {
            bytes_ -= lines_.front().size();
            lines_.pop_front();
        }
    }
    const std::deque<std::string>& lines() const { return lines_; }

private:
    size_t                  keep_, maxBytes_, bytes_ = 0;
    std::deque<std::string> lines_;
};

}  // namespace axis::bbs
