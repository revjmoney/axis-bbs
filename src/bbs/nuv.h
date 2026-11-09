// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// New user voting (NUV), the Oblivion/2 and ViSiON/3 way: validated callers read a pending
// applicant's answers and vote yes or no. Enough yes votes let them in; enough no votes turn
// them down. The sysop sets both counts and who may vote; the sysop can still validate or deny
// anyone at once from the user editor.
#pragma once
#include <string>
#include <vector>

#include "bbs/users.h"

namespace axis::bbs {

struct NuvRules {
    int  yes = 3;     // "nuv_yes": yes votes that let an applicant in (0 = no voting: the sysop decides alone)
    int  no = 3;      // "nuv_no": no votes that turn one down (0 = only the sysop turns anyone down)
    int  level = 20;  // "nuv_level": the lowest security that may vote
    bool on() const { return yes > 0; }
};

enum class NuvOutcome { Counted, Validated, Denied, NotPending };

int            nuvYes(const User& u);
int            nuvNo(const User& u);
const NuvVote* nuvVoteBy(const User& u, const std::string& voter);  // any case; nullptr if they haven't voted
bool           nuvMayVote(const User& voter, const NuvRules& r);      // validated, at the level, not a twit
// the applicants this voter still has to see, oldest first (never themselves)
std::vector<User*> nuvWaiting(UserStore& users, const std::string& voter);
// counts the vote (a second vote from the same caller replaces the first), then lets the applicant
// in or turns them down when a count is reached. Validated: security at least 20, like the sysop's V.
NuvOutcome nuvCast(User& applicant, const std::string& voter, bool yes, const std::string& comment, const NuvRules& r);

}  // namespace axis::bbs
