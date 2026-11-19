// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The card tree service: how a card mirror (the i5 as a follower) lists, reads and, for a hand-back, writes the
// main's SD card over the cluster link, plus the ring of changed paths that lets it follow live.
//
// A request rides FileReq (follower to main, the read side) or MainReq (main to follower, the write side) under
// request letter 'I'. Its fields are split by 0x1F: "I" 1F op 1F args... (an 'I P' request's data is the last field,
// taken whole). A reply is '1' and its data, or '0'. Every path is relative to the card, checked by cardPathMirrored.
//   I V                      read   the tree service's version ("2")
//   I D dir skip             read   the folder's entries, sorted: "f" 1F name 1F size 1F mtime, "d" 1F name; from entry
//                                   skip, stopping before kTreeListMax; a last line "+" 1F next-skip when more
//   I S path                 both   "size" 1F "mtime", or '0' when missing
//   I R path off len         read   up to kTreePiece bytes at off (the file stays open for the next piece)
//   I C gen seq              read   "gen" 1F "seq" 1F "lost" '\n' then one changed path per line after seq
//   I O path                 write  opens path.ax_ truncated, making the parent folders
//   I P off data             write  appends data when off equals the bytes written so far
//   I F size mtime           write  closes, checks size, renames over path and sets its time; "size" 1F "mtime" as the card
//                                   now reports them; a wrong size deletes the temp and answers '0'
//   I X path                 write  removes it (missing counts as done)
//   I E                      write  ends a hand-back: finished() turns true
#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <string>
#include <utility>

namespace axis::cluster {

constexpr size_t kTreePiece = 4096;
constexpr size_t kTreeListMax = 7 * 1024;

// Relative to the card: not empty, at most 200 bytes, no leading '/', no '\', no ':', no byte below 0x20, no empty,
// "." or ".." component, components at most 64 bytes.
bool cardPathOk(const std::string& rel);
// cardPathOk, and not a log (.log) or a temp (.ax_), in any case, and nowhere under "System Volume Information".
bool cardPathMirrored(const std::string& rel);

// The last changed paths on the card, newest last. It records only while on (a card mirror is linked to the main).
// note() may be called from any task; answer() is the data of an 'I C' reply (without the leading '1').
class CardFeed {
public:
    static constexpr size_t kMax = 32, kPathMax = 96;

    void        setOn(bool on, uint32_t gen);  // from off to on starts generation gen with an empty ring
    bool        on() const { return on_.load(std::memory_order_relaxed); }
    void        note(const char* boardPath);  // only "/sd/...": the card path, noted when cardPathMirrored accepts it
    std::string answer(uint32_t gen, uint32_t seq) const;

private:
    struct Entry {
        uint32_t seq;
        char     path[kPathMax];
    };
    std::atomic<bool> on_{false};
    mutable std::mutex mu_;
    uint32_t           gen_ = 0, seq_ = 0;
    uint32_t           lostSeq_ = 0;  // the newest change that is not in the ring (overrun, or a path too long)
    size_t             n_ = 0;
    Entry              e_[kMax];
};
CardFeed& cardFeed();  // one per program

// Answers the 'I' requests over one card root ("/sd" on a board). One per program is plenty: it keeps one file
// open to read and one to write.
class CardTreeServer {
public:
    explicit CardTreeServer(std::string root) : root_(std::move(root)) {}
    ~CardTreeServer() { drop(); }
    CardTreeServer(const CardTreeServer&) = delete;
    CardTreeServer& operator=(const CardTreeServer&) = delete;

    std::string handleRead(const std::string& req);   // V D S R C (from any authenticated follower)
    std::string handleWrite(const std::string& req);  // S O P F X E (the main, when it is a card mirror)
    bool        finished() const { return finished_; }
    void        drop();  // the link is gone: closes the open files, deletes a half-written temp

private:
    std::string full(const std::string& rel) const { return rel.empty() ? root_ : root_ + "/" + rel; }
    std::string stat1(const std::string& rel) const;
    void        closeRead();
    void        closeWrite(bool removeTemp);

    std::string root_;
    std::FILE*  rd_ = nullptr;
    std::string rdPath_;
    std::FILE*  wr_ = nullptr;
    std::string wrPath_;  // the final path of the open temp
    size_t      written_ = 0;
    bool        finished_ = false;
};

}  // namespace axis::cluster
