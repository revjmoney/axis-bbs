// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The voting booth's lists: "votes" holds "question|choice|choice...", "ballots" holds "question#|handle|choice#"
// (question numbers count from 1, in the votes list's order).
#pragma once
#include <string>
#include <vector>

namespace axis::bbs {

// Takes question q (1-based) out of the booth with its ballots; later questions' ballots move down a number
// so they stay with their question. False if there is no question q.
bool voteDelete(std::vector<std::string>& votes, std::vector<std::string>& ballots, int q);

}  // namespace axis::bbs
