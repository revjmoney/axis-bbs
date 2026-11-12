// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// Signed records for the nets: posts, board cards and Rev's notices. One record per line,
// fields joined by 0x1F with the kind letter first. Hex fields are lowercase.
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

enum class NetKind : char { Post = 'P', Card = 'C', Notice = 'N' };

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
    std::string kind, target, value;  // hold kill ban lift limits rotate
    uint32_t seq = 0;
    int key = 0;  // which of Rev's two keys signed it
    std::string sig;
};

// The exact text that is signed (kind letter included; path and sig excluded).
std::string signedPart(const NetPost& p);
std::string signedPart(const NetCard& c);
std::string signedPart(const NetNotice& n);

// Full wire line. Returns an empty string if the record cannot be represented validly.
std::string encode(const NetPost& p);
std::string encode(const NetCard& c);
std::string encode(const NetNotice& n);

// Parsed in place: the fields are copied once, straight into the record (a post's body is unescaped
// into it), and nothing else of the line is copied. The record is left as it was on failure.
bool decode(std::string_view line, NetPost& p);
bool decode(std::string_view line, NetCard& c);
bool decode(std::string_view line, NetNotice& n);

// The post's fields are valid and it carries a signature of the right form (no line is built).
bool validPost(const NetPost& p);

bool signPost(NetPost& p, const KeyPair& k);
bool verifyPost(const NetPost& p, const std::string& pubHex);
bool signCard(NetCard& c, const KeyPair& k);
bool verifyCard(const NetCard& c);  // checks against the card's own pub
bool verifyCard(const NetCard& c, const std::string& pubHex);  // pubHex must equal c.pub
bool signNotice(NetNotice& n, const KeyPair& k);  // for Rev's tooling and tests
bool verifyNotice(const NetNotice& n);

}  // namespace axis::net
