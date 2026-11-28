// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// radnet-bridge v1 bundles: boards and posts as JSON Lines, the format NecroTechs RadNet reads and writes
// (python -m radnet bridge export|import). This is the AXIS half: message-base areas out to a bundle, and a
// bundle's posts into one local area. PC-side only (tools/bridge); nothing here is in the board image.
//
// Every record carries an origin "<network>:<station>:<native id>". AXIS writes:
//   a Sysop Net / User Net post:  axis:<poster board>:<post id>   (the same post from any board is one origin)
//   its area:                     axis:net:<area>                (sn_support on every board is one board)
//   a local post:                 axis:<this board>:<area>.<num>
//   its area:                     axis:<this board>:<area>
// Imported posts are remembered in <area>.bridge beside <area>.msg (num, origin), so an import never repeats one and
// an export never sends one back. Nothing is signed: imported authors stay claims.
#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "bbs/msgbase.h"

namespace axis::bbs::bridge {

constexpr size_t kMaxBytes = 8u * 1024 * 1024;  // same caps as RadNet's importer
constexpr size_t kMaxLine = 16 * 1024;
constexpr size_t kMaxRecords = 5000;

// text
std::string cp437ToUtf8(const std::string& s);                 // CP437 bytes to UTF-8; control bytes but \n \t become spaces
std::string utf8ToCp437(const std::string& s);                 // UTF-8 to CP437 bytes; what CP437 lacks becomes '?'
std::string plainText(const std::string& s);                   // ANSI escapes and |00..|23 pipe colours removed
bool        originOk(const std::string& origin);               // network:station:id, RadNet's pattern
std::string idPart(const std::string& s);                      // a string made safe for an origin part ([A-Za-z0-9._-])

// minimal JSON for flat records: string, null and number values only
std::string jsonQuote(const std::string& utf8);
// parse one flat JSON object into key -> value; null and numbers come back as their text, strings unescaped.
// isString[key] tells which were strings. False on anything else (nesting, bad escapes, trailing junk).
bool jsonFlat(const std::string& line, std::map<std::string, std::string>& out, std::map<std::string, bool>& isString);

struct ExportArea {
    std::string id, title;  // area id (file name) and the title RadNet shows
};

struct ExportResult {
    int boards = 0, posts = 0, skipped = 0;  // skipped: posts that came from a bundle (never sent back)
    std::string error;
};

// Read the areas from store and write the bundle lines (header first) through out. station is this board's id
// (16 hex from @NET, or any [A-Za-z0-9._-] name). imported(area) gives the nums that came from a bundle.
ExportResult exportAreas(MessageStore& store, const std::string& station, const std::vector<ExportArea>& areas,
                         const std::function<std::vector<int>(const std::string& area)>& imported,
                         const std::function<void(const std::string& line)>& out);

struct ImportResult {
    int posts = 0, duplicates = 0, fromAxis = 0, rejected = 0;
    std::vector<std::string> errors;  // first 20, "line N: why"
    std::string fatal;                // the whole bundle refused (bad header, too big); nothing was posted
};

// Post a bundle's posts into one area. seen holds the origins already imported there (from <area>.bridge);
// remember(num, origin) is called for each new post so the caller can append it to that file. now is the date
// stamped on new posts ("YYYY-MM-DD HH:MM"). Posts whose origin is an axis: one are skipped (they are AXIS posts
// already). Each RadNet board's title goes in front of the subject: "[Field reports] LP-008".
ImportResult importBundle(const std::string& bundle, MessageStore& store, const std::string& area,
                          const std::map<std::string, int>& seen, const std::string& now,
                          const std::function<void(int num, const std::string& origin)>& remember);

// <area>.bridge lines: "<num> <origin>\n"
std::map<std::string, int> parseLedger(const std::string& text);

}  // namespace axis::bbs::bridge
