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

// One piece of art read from a file: added to `carry`, and what is ready goes out, up to the last line break so no code is
// cut in two (a run of 1 KB with no line break goes out but for its last 32 bytes). `last`: the file ends here, all goes out.
// Every art screen (menus, theme screens, the poster) is drawn through this, a piece at a time.
void artPiece(std::string& carry, const char* raw, size_t n, bool last, const AcsUser& who, const ArtValue& value, const ArtEmit& emit);

}  // namespace axis::bbs
