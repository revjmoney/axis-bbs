// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// ViSiON/3's art and prompt codes, turned into ANSI as the text streams out:
//   |00..|15 foreground (DOS colours), |B0..|B15 background, |CL clear screen, |XYcc,rr move the cursor,
//   |XX a two-letter placeholder (value("XX")), @XX####@ a placeholder padded to the field's width,
//   %XX AXIS's own codes (the same values as |XX), {{ACS}}...{{/}} shown only to callers the access string allows, else blanked to the same width.
#pragma once
#include <cstddef>
#include <functional>
#include <string>

#include "bbs/acs.h"

namespace axis::bbs {

using ArtValue = std::function<std::string(const std::string&)>;
using ArtEmit = std::function<void(const char*, size_t)>;

// handed out in pieces of at most 512 bytes (a whole screen never sits in RAM twice)
void        expandArt(const std::string& in, const AcsUser& who, const ArtValue& value, const ArtEmit& emit);
std::string expandArt(const std::string& in, const AcsUser& who, const ArtValue& value);  // short text, tests

}  // namespace axis::bbs
