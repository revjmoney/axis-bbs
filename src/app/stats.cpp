// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include "app/stats.h"

#include <ctime>

#include "app/boarddata.h"

// Size over speed, as main.cpp builds the code that was moved here.
#pragma GCC optimize("Os")

namespace axis::app {

void Stats::say(const char* line) {
    if (console) console(line);
    else host_.event(line);
}

void Stats::statsSave() {
    if (!flashWrite(host_, "/stats.txt", dayStats.format())) say("axis: stats save failed");
}

void Stats::rollDayCounters() {
    if (!isOwner()) return;  // only the main's midnight counts
    time_t now = (time_t)host_.unixNow();
    if (now < 1700000000) return;
    struct tm* t = localtime(&now);
    if (dayStats.roll((t->tm_year + 1900) * 400 + t->tm_yday)) statsSave();
}

void Stats::statsLoad(const axis::bbs::UserStore& users) {
    if (!flashExists(host_, "/stats.txt")) {
        for (auto& u : users.all()) dayStats.total += u.calls;
        statsSave();
    } else if (!dayStats.parse(flashRead(host_, "/stats.txt"))) {
        say("axis: /stats.txt unreadable, stats start from zero");
    }
}

void Stats::countCall() {
    if (isOwner()) {
        rollDayCounters();
        dayStats.call();
        statsSave();
    } else if (share) share(Count::Call);
}
AXIS_NOINLINE void Stats::countUpload() {
    if (isOwner()) {
        rollDayCounters();
        dayStats.upload();
        statsSave();
    } else if (share) share(Count::Upload);
}
void Stats::countPost() {
    if (isOwner()) {
        rollDayCounters();
        dayStats.post();
        statsSave();
    } else if (share) share(Count::Post);
}

}  // namespace axis::app
