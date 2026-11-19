// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// Calls, posts and uploads today and the lifetime call total, in the flash folder (/stats.txt) so a restart keeps them.
// The day counters roll at local midnight; until the clock is set they just keep counting. In a cluster the main owns
// them: a follower hands its counts to `share` (the board's StatsShare, which sends them up) and never rolls, counts
// into or saves its own.
#pragma once
#include <functional>

#include "app/host.h"
#include "bbs/daystats.h"
#include "bbs/users.h"

namespace axis::app {

class Stats {
public:
    explicit Stats(Host& h) : host_(h) {}

    enum class Count { Call, Post, Upload };

    axis::bbs::DayStats dayStats;  // this board's own counters (the main's, in a cluster)

    // This board keeps the counts (not a cluster follower). Empty: always, as on the PC.
    std::function<bool()> owner;
    // A follower's count, for the main (empty: dropped).
    std::function<void(Count)> share;
    // A line for the board's serial console (the save and load notes). Empty: the event log.
    std::function<void(const char*)> console;

    void statsSave();
    void rollDayCounters();
    // At boot, after the users: the saved counters, or (no file yet) a total seeded from every account's call count.
    void statsLoad(const axis::bbs::UserStore& users);
    void countCall();
    AXIS_NOINLINE void countUpload();
    void countPost();

private:
    bool isOwner() { return !owner || owner(); }
    void say(const char* line);

    Host& host_;
};

}  // namespace axis::app
