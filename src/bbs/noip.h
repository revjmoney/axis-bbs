// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// No-IP dynamic DNS, the part that needs no network: the update URL and what No-IP's reply means.
// No-IP asks clients to stop retrying after a refusal (bad login, unknown host, abuse) until the
// settings change, or it may block the account, so a reply says whether to stop.
#pragma once
#include <string>

namespace axis::bbs {

struct NoIpReply {
    bool        ok = false;    // good / nochg on every line
    bool        stop = false;  // a refusal: don't try again until the settings change
    std::string text;          // the reply, trimmed, for the status line
};

NoIpReply   noipReply(const std::string& body);
std::string noipUrl(const std::string& host, const std::string& ip);

}  // namespace axis::bbs
