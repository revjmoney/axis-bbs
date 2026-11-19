// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The card mirror's bookkeeping, pure C++17 (no Arduino, no host globals, so the tests can link it): the manifest
// of what <data>/mirror holds (each file as the main's card reported it), the difference between it and a listing
// of the card, when the daily noon check is due, and the pace of the copy. mirrorhost.cpp drives it.
#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>

struct CardEntry {
    std::string path;  // relative to the card
    uint64_t    size = 0;
    int64_t     mtime = 0;  // seconds since 1970, as the card reports it
};

struct Manifest {
    std::map<std::string, CardEntry> files;
    uint32_t                         gen = 0, seq = 0;  // the main's change feed: how far this mirror has read it
    bool                             complete = false;  // the first full copy finished
    std::string                      noon;              // "YYYY-MM-DD" of the last noon check (other compare passes leave it)
};

// "#gen=", "#seq=", "#complete=", "#noon=" lines, then one file a line: path 0x1F size 0x1F mtime
std::string formatManifest(const Manifest& m);
// false on anything it cannot read (the caller then treats the manifest as missing); `out` is only set on true
bool parseManifest(const std::string& text, Manifest& out);

struct MirrorDiff {
    std::vector<CardEntry>   copy;    // on the card, and missing from the manifest or at another size or time
    std::vector<std::string> remove;  // in the manifest, not on the card
};
MirrorDiff diffListing(const Manifest& m, const std::vector<CardEntry>& card);

// The noon check is due: today is after the last one ("" is before any day) and it is noon or later.
bool noonDue(const std::string& lastNoon, const std::string& today, int nowMinutes, int noonMinutes);

// The copy's pace: bytesPerSec while nobody is on a call in the cluster, a quarter of it while someone is. take()
// is true when the budget is not overdrawn, and then spends `bytes` (it may go below zero: one request at most
// overdraws it); charge() spends more after the fact (a reply bigger than what take() was given).
class Pace {
public:
    explicit Pace(uint32_t bytesPerSec);
    void setBusy(bool someoneOn) { busy_ = someoneOn; }
    bool take(uint32_t bytes, uint32_t nowMs);
    void charge(uint32_t bytes) { budget_ -= (int64_t)bytes; }

private:
    void     refill(uint32_t nowMs);
    uint32_t rate_;
    bool     busy_ = false;
    bool     started_ = false;
    uint32_t last_ = 0;
    int64_t  budget_ = 0;
    uint32_t carry_ = 0;  // milli-bytes not yet whole (a low rate over short turns still adds up)
};
