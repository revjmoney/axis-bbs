// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The User Net membership tally: applications (A) and votes (V) held as compact fields, and the
// membership rule over them plus Rev's notices in NetState. status() depends only on the set of
// records held and the NetState, never on arrival order or a clock.
// The full signed lines live on the storage root (/net/apps.txt); RAM holds only the fields below.
// takeApp and takeVote hand the line back for the host to append, holds() says whether a line is the
// record held now, and save(oldText) keeps only those lines, so the host can compact the file.
#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "net/record.h"
#include "net/state.h"

namespace axis::net {

constexpr size_t kMaxOpenApps = 16;       // applications, the latest one per board
constexpr size_t kMaxVotes = 256;         // votes from boards that were members when taken
constexpr size_t kMaxOutsiderVotes = 32;  // votes from boards that were not, the oldest goes first

class NetMembers {
public:
    enum class Status { None, Pending, Member, Denied, Vetoed };
    struct Tally {
        uint32_t yes, no;
    };

    // isRev says whether a board id is one of Rev's boards (its card carries one of Rev's keys).
    NetMembers(const NetState& st, std::function<bool(const std::string& board)> isRev);

    // Verified first. Keeps only the latest app per board (equal seqs: the higher line CRC wins).
    // Refused for a Rev board, a Rev admitted board or a vetoed one. A full table drops the oldest app
    // Rev has sealed, with its votes; with none sealed the new app is refused. line gets the encoded
    // record. False if nothing changed.
    bool takeApp(const NetApp& a, std::string& line);
    // Verified against its own pub. Keeps the highest seq per (voter, app, appSeq) (equal seqs: the
    // higher line CRC wins). A vote for an app not held yet is kept; one for an app older than the
    // held one is refused. A voter may not vote on itself. False if nothing changed.
    bool takeVote(const NetVote& v, std::string& line);

    Status status(const std::string& board) const;
    // Votes on the board's latest held app, counting only voters that are members by the tally.
    Tally tally(const std::string& board) const;
    // Rev's boards (from cards), Rev admitted boards and boards admitted by the tally: Rev's first,
    // each group in board id order.
    std::vector<std::string> members() const;
    // Pending applicants this voter has not voted on (latest app), oldest app seq first.
    std::vector<std::string> waitingFor(const std::string& voter) const;
    // For Rev's board: applicants the tally says Member or Denied with no notice sealing that yet.
    std::vector<std::pair<std::string, Status>> sealsDue() const;

    // The line (an A or V record) is exactly the app or vote held now.
    bool holds(const std::string& line) const;
    // The lines of oldText that holds() approves, each once, one per line.
    std::string save(const std::string& oldText) const;
    // Replaces the tables from the lines, each verified again. Order in the text does not matter.
    // Load the NetState first: seals and membership decide what fits.
    void load(const std::string& text);

    size_t appCount() const { return apps_.size(); }
    size_t voteCount() const { return votes_.size() + outsiders_.size(); }

private:
    struct App {
        uint64_t board;
        uint32_t seq, date, crc;  // crc: CRC-32 of the encoded line
    };
    struct Vote {
        uint64_t voter, app;
        uint32_t appSeq, seq, date;
        uint32_t crc : 31;  // low 31 bits of the encoded line's CRC-32
        uint32_t yes : 1;
    };
    const App* findApp(uint64_t board) const;
    bool sealed(uint64_t board, uint32_t appSeq) const;
    bool seedMember(uint64_t board) const;
    bool deniedByNotice(uint64_t board) const;
    // Every known board (applicant or voter) in M, sorted.
    std::vector<uint64_t> grow() const;
    bool inM(const std::vector<uint64_t>& m, uint64_t board) const;
    Tally count(const std::vector<uint64_t>& m, const App& a) const;
    Status statusIn(const std::vector<uint64_t>& m, uint64_t board) const;
    bool locate(const std::string& line, int& pool, size_t& index) const;

    const NetState& st_;
    std::function<bool(const std::string&)> isRev_;
    std::vector<App> apps_;
    std::vector<Vote> votes_;      // voters that were members when taken
    std::vector<Vote> outsiders_;  // voters that were not
};

}  // namespace axis::net
