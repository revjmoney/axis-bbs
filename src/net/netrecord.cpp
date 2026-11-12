// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The net record (net/netrecord.h): builds each line and hands it to the host to append.
#pragma GCC optimize("Os")
#include "net/netrecord.h"

#include <utility>

namespace axis::net {

namespace {
std::string clean(const std::string& s) {
    std::string o;
    o.reserve(s.size());
    for (char c : s) o.push_back((unsigned char)c < 0x20 || c == 0x7f ? ' ' : c);  // no control bytes reach a sysop's terminal
    return o;
}
}  // namespace

NetRecord::NetRecord(std::function<bool(const std::string&, const std::string&)> append, std::function<std::string()> yyyymm,
                     std::function<uint32_t()> now)
    : append_(std::move(append)), yyyymm_(std::move(yyyymm)), now_(std::move(now)) {}

bool NetRecord::note(const std::string& who, const std::string& what, const std::string& signedLine) {
    if (!append_) return false;
    std::string month = yyyymm_ ? yyyymm_() : std::string();
    if (month.empty()) month = "nodate";
    std::string line = std::to_string(now_ ? now_() : 0u) + " " + clean(who.empty() ? std::string("local") : who) + " " + clean(what);
    if (line.size() > 200) line.resize(200);
    line.push_back('\x1f');
    if (line.size() + signedLine.size() <= kNetRecordMaxLine && signedLine.find('\n') == std::string::npos &&
        signedLine.find('\r') == std::string::npos)
        line += signedLine;
    bool ok = append_("/net/record/" + month + ".log", line);
    if (!ok) failures_++;
    return ok;
}

std::string recordHead(const std::string& line) { return line.substr(0, line.find('\x1f')); }

}  // namespace axis::net
