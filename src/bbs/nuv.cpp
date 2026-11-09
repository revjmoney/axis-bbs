// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include "bbs/nuv.h"

#include <cctype>

namespace axis::bbs {

namespace {
bool same(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); i++)
        if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i])) return false;
    return true;
}
}  // namespace

int nuvYes(const User& u) {
    int n = 0;
    for (const NuvVote& v : u.votes) n += v.yes;
    return n;
}

int nuvNo(const User& u) { return (int)u.votes.size() - nuvYes(u); }

const NuvVote* nuvVoteBy(const User& u, const std::string& voter) {
    for (const NuvVote& v : u.votes)
        if (same(v.voter, voter)) return &v;
    return nullptr;
}

bool nuvMayVote(const User& voter, const NuvRules& r) {
    return r.on() && voter.status == User::Status::Validated && voter.security >= r.level && !voter.twit;
}

std::vector<User*> nuvWaiting(UserStore& users, const std::string& voter) {
    std::vector<User*> out;
    for (User& u : users.all())
        if (u.status == User::Status::Pending && !same(u.handle, voter) && !nuvVoteBy(u, voter)) out.push_back(&u);
    return out;
}

NuvOutcome nuvCast(User& applicant, const std::string& voter, bool yes, const std::string& comment, const NuvRules& r) {
    if (applicant.status != User::Status::Pending) return NuvOutcome::NotPending;
    NuvVote v{voter, yes, comment.substr(0, 60)};
    bool    replaced = false;
    for (NuvVote& w : applicant.votes)
        if (same(w.voter, voter)) {
            w = v;
            replaced = true;
        }
    if (!replaced) applicant.votes.push_back(v);
    if (r.yes > 0 && nuvYes(applicant) >= r.yes) {
        applicant.status = User::Status::Validated;
        if (applicant.security < 20) applicant.security = 20;
        return NuvOutcome::Validated;
    }
    if (r.no > 0 && nuvNo(applicant) >= r.no) {
        applicant.status = User::Status::Denied;
        return NuvOutcome::Denied;
    }
    return NuvOutcome::Counted;
}

}  // namespace axis::bbs
