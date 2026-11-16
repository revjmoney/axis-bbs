// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// Signed records for the nets: posts, board cards, Rev's notices, User Net applications and votes.
// One record per line, fields joined by 0x1F with the kind letter first. Hex fields are lowercase.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "net/sign.h"

namespace axis::net {

constexpr size_t kNetMaxBody = 8 * 1024;
constexpr size_t kNetMaxSubject = 72;
constexpr size_t kNetMaxFrame = 9 * 1024;  // longest encoded line, without the newline
constexpr size_t kNetMaxBlurb = 240;        // an application's blurb, before escaping
constexpr size_t kNetMaxComment = 72;       // a vote's comment, before escaping

enum class NetKind : char { Post = 'P', Card = 'C', Notice = 'N', App = 'A', Vote = 'V' };

struct NetPost {
    std::string id, area, board, from, to, subject, body, path;  // id = <board>-<seq>; path not signed
    uint32_t date = 0;
    std::string sig;
};

struct NetCard {
    std::string board, pub, name, host, mac, sysop, location, links;
    int port = 23;
    uint32_t seq = 0;  // a newer seq replaces an older card
    std::string sig;
};

struct NetNotice {
    std::string kind, target, value;  // hold kill ban lift limits rotate admit deny veto votes
    uint32_t seq = 0;
    int key = 0;  // which of Rev's two keys signed it
    std::string sig;
};

// A board's application to join User Net. pub lets a board check it before it holds the applicant's
// card (boardId(pub) must equal board). seq is the applicant's application counter, from 1. The mac is
// "xx:xx:xx:xx:xx:xx" (hex digits in either case) or empty. The blurb may hold line breaks.
struct NetApp {
    std::string board, pub, name, sysop, location, mac, blurb;
    uint32_t seq = 0, date = 0;
    std::string sig;
};

// A member board's vote on an application. pub is the voter's key (boardId(pub) must equal voter), so
// a vote checks by itself with no card. app is the applicant's board id and appSeq its application
// seq. seq is the voter's vote counter: a higher one replaces a lower one for (voter, app, appSeq).
struct NetVote {
    std::string voter, pub, app;
    uint32_t appSeq = 0, seq = 0, date = 0;
    bool yes = false;
    std::string comment;
    std::string sig;
};

// "<board>-a<seq>", the application's id.
std::string appId(const NetApp& a);

// The exact text that is signed (kind letter included; path and sig excluded).
std::string signedPart(const NetPost& p);
std::string signedPart(const NetCard& c);
std::string signedPart(const NetNotice& n);
std::string signedPart(const NetApp& a);
std::string signedPart(const NetVote& v);

// Full wire line. Returns an empty string if the record cannot be represented validly.
std::string encode(const NetPost& p);
std::string encode(const NetCard& c);
std::string encode(const NetNotice& n);
std::string encode(const NetApp& a);
std::string encode(const NetVote& v);

// Parsed in place: the fields are copied once, straight into the record (a post's body is unescaped
// into it), and nothing else of the line is copied. The record is left as it was on failure.
bool decode(std::string_view line, NetPost& p);
bool decode(std::string_view line, NetCard& c);
bool decode(std::string_view line, NetNotice& n);
bool decode(std::string_view line, NetApp& a);
bool decode(std::string_view line, NetVote& v);

// The post's fields are valid and it carries a signature of the right form (no line is built).
bool validPost(const NetPost& p);

bool signPost(NetPost& p, const KeyPair& k);
bool verifyPost(const NetPost& p, const std::string& pubHex);
bool signCard(NetCard& c, const KeyPair& k);
bool verifyCard(const NetCard& c);  // checks against the card's own pub
bool verifyCard(const NetCard& c, const std::string& pubHex);  // pubHex must equal c.pub
bool signNotice(NetNotice& n, const KeyPair& k);  // for Rev's tooling and tests
bool verifyNotice(const NetNotice& n);
bool signApp(NetApp& a, const KeyPair& k);  // k must be the applicant's key (a.pub)
bool verifyApp(const NetApp& a);            // checks against the application's own pub
bool signVote(NetVote& v, const KeyPair& k);  // k must be the voter's key (v.pub)
bool verifyVote(const NetVote& v);            // checks against the vote's own pub

}  // namespace axis::net
