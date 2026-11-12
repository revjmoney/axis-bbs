// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The net record: an append-only log of everything this board saw on the net and every local sysop
// action, one line each: "<unix> <who> <what>", then 0x1F and the signed line it was about (empty
// for local actions). The host supplies the file appends and the month name; nothing here trims it.
#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

namespace axis::net {

constexpr size_t kNetRecordMaxLine = 10 * 1024 - 1;  // a record line stays under 10 KB (newline included)

class NetRecord {
public:
    // append adds one line (the host adds the newline) to path, "/net/record/<yyyymm>.log".
    // yyyymm gives the month ("nodate" with the clock unset); now gives unix time (0 if null).
    NetRecord(std::function<bool(const std::string& path, const std::string& line)> append, std::function<std::string()> yyyymm,
              std::function<uint32_t()> now = nullptr);
    // who and what lose newlines and 0x1F; a signed line that would push the record past
    // kNetRecordMaxLine is left out. False if the append failed.
    bool note(const std::string& who, const std::string& what, const std::string& signedLine);
    uint32_t failures() const { return failures_; }

private:
    std::function<bool(const std::string&, const std::string&)> append_;
    std::function<std::string()> yyyymm_;
    std::function<uint32_t()> now_;
    uint32_t failures_ = 0;
};

// The part of a record line before its first 0x1F ("<unix> <who> <what>").
std::string recordHead(const std::string& line);

}  // namespace axis::net
