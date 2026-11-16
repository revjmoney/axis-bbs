// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// Reading the net record (net/netrecord.h) back for the sysop: which kind a line is, a search over it, whether its
// signed line checks, and its record's fields one per line. The host reads the files; nothing here touches one.
#pragma once
#include <functional>
#include <string>

#include "net/state.h"

namespace axis::net {

// Votes: applications, votes and Rev's admit, deny, veto and votes notices. Members: cards and membership results.
// Actions: everything else (posts, sessions, other notices, local sysop actions).
enum class RecordKind { All, Votes, Members, Actions };

// The line is of kind k and, if text is not empty, holds text (any case) in its head or its signed line.
bool recordMatches(const std::string& line, RecordKind k, const std::string& text);

// One of "signed by <board>, checks", "signed by <board>, does NOT check", "signed by <board>, no card held to check
// it" or "local action, not signed". Posts check against the card held for their board, cards, applications and votes
// against their own pub, notices against Rev's keys now (a notice from before a key rotation shows as not checking).
// <board> is "Rev" for a notice and "?" for a line that is no record.
std::string recordCheck(const std::string& signedLine, const NetState& st);

// The decoded record's fields, one "<name> <value>" line each (a post's body and an application's blurb a line of
// theirs per line, indented). Every field is passed through netSafe. Nothing for an empty or unreadable line.
void recordFields(const std::string& signedLine, const std::function<void(const std::string&)>& line);

// A field from another board, safe for a terminal: control bytes become '?'.
std::string netSafe(std::string s);

}  // namespace axis::net
