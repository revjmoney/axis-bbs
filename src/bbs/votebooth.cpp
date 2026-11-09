// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include "bbs/votebooth.h"

#include <cstdlib>

namespace axis::bbs {

bool voteDelete(std::vector<std::string>& votes, std::vector<std::string>& ballots, int q) {
    if (q < 1 || q > (int)votes.size()) return false;
    votes.erase(votes.begin() + (q - 1));
    std::vector<std::string> kept;
    for (const std::string& b : ballots) {
        size_t bar = b.find('|');
        int    n = std::atoi(b.substr(0, bar).c_str());
        if (bar == std::string::npos || n == q) continue;  // its ballots go with it (and a broken line goes too)
        kept.push_back(n > q ? std::to_string(n - 1) + b.substr(bar) : b);
    }
    ballots = std::move(kept);
    return true;
}

}  // namespace axis::bbs
