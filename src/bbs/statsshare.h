// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// One set of WFC stats for the whole cluster. The main owns them (DayStats, /stats.txt). A follower that answers a
// call or finishes an upload adds it here (pending) and hands it to the main as a batch over the link:
//   "B" 0x1F <batch id> 0x1F <calls> 0x1F <posts> 0x1F <uploads> 0x1F <newest caller>
// The main applies a batch once (it remembers the last few ids, so a resend after a lost answer counts nothing) and
// answers "1" plus its numbers. Every heartbeat carries the same numbers as one extra field:
//   v1 day=<key> calls=<n> posts=<n> uploads=<n> total=<n> LF <last caller>
// A follower shows the main's last numbers plus what it has not had acknowledged yet. Posts need no batch: a
// follower's caller posts straight into the main's message base, which counts them there.
#pragma once
#include <cstdint>
#include <string>

#include "bbs/daystats.h"

namespace axis::bbs {

struct StatsCounts {
    uint32_t    calls = 0, posts = 0, uploads = 0;
    std::string who;  // the newest caller who hung up ("" = none)
    bool        empty() const { return !calls && !posts && !uploads && who.empty(); }
};

// The main's numbers as one heartbeat field (or a "B" answer after its "1"), and back. Garbage reads as false.
std::string encodeStatsSnapshot(const DayStats& s, const std::string& lastCaller);
bool        decodeStatsSnapshot(const std::string& text, DayStats& s, std::string& lastCaller);
// A follower's batch as a request, and back (false: not a well formed batch, id 0 or a count over kMaxBatch).
std::string encodeStatsBatch(uint32_t id, const StatsCounts& c);
bool        decodeStatsBatch(const std::string& req, uint32_t& id, StatsCounts& c);
constexpr uint32_t kMaxBatch = 100000;
constexpr size_t   kMaxCallerName = 40;

// The follower's side.
class StatsShare {
public:
    void call() { pend_.calls++; }
    void post() { pend_.posts++; }
    void upload() { pend_.uploads++; }
    void caller(const std::string& handle);

    bool waiting() const { return !pend_.empty() || batch_; }  // something the main has not acknowledged
    // The request to send now: the batch already out (same id, so the main can tell a resend), or the pending counts
    // as a new batch numbered freshId (0 becomes 1).
    std::string request(uint32_t freshId);
    // The main's answer to request() (the link pairs each answer with its request). True: a good answer, the batch out
    // is cleared and the numbers in it taken. False ("0" or garbage): nothing is lost, the same batch goes again later.
    // No answer at all: just don't call this; the batch stays out.
    bool answer(const std::string& reply);
    // A heartbeat's extra field. True when it held the main's numbers.
    bool heard(const std::string& field);

    bool               known() const { return known_; }  // the main has told its numbers at least once
    const DayStats&    main() const { return main_; }
    const std::string& mainLastCaller() const { return mainLast_; }
    // What the screens show: base (the main's numbers, or nothing yet) plus every count not acknowledged.
    DayStats    shown(const DayStats& base) const;
    DayStats    shown() const { return shown(known_ ? main_ : DayStats()); }
    std::string lastCaller() const;  // the newest unsent caller, else the main's, else ""
    const StatsCounts& pending() const { return pend_; }
    const StatsCounts& outstanding() const { return out_; }

private:
    StatsCounts pend_, out_;  // counted since the last batch; the batch out (id batch_, 0 = none)
    uint32_t    batch_ = 0;
    DayStats    main_;
    std::string mainLast_;
    bool        known_ = false;
};

// The main's side: a follower's batch applied once.
class StatsInbox {
public:
    enum class Result { Bad, Applied, Again };
    // req is the whole "B" request. Applied: s (and lastCaller when the batch names one) changed. Again: a resend of a
    // batch already counted. Bad: not a well formed batch (answer it anyway, so the follower lets it go).
    Result apply(const std::string& req, DayStats& s, std::string& lastCaller);

private:
    uint32_t seen_[8] = {};
    unsigned next_ = 0;
};

}  // namespace axis::bbs
