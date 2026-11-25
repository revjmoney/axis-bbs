// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// ViSiON/3's art and prompt codes, turned into ANSI as the text streams out:
//   |00..|15 foreground (DOS colours), |B0..|B15 background, |CL clear screen, |XYcc,rr move the cursor,
//   |XX a two-letter placeholder (value("XX")), @XX####@ a placeholder padded to the field's width,
//   (a look's dialect, ArtStyle, adds |C1..|C7 and || for vision3; ^0..^9, |16..|31 backgrounds, |CS and || for renegade.
//    Renegade also fills %ND, the node number, in the value callback.)
//   %XX AXIS's own codes (the same values as |XX), {{ACS}}...{{/}} shown only to callers the access string allows, else blanked to the same width.
#pragma once
#include <cstddef>
#include <functional>
#include <string>

#include "bbs/acs.h"
#include "bbs/artstyle.h"

namespace axis::bbs {

using ArtValue = std::function<std::string(const std::string&)>;
using ArtEmit = std::function<void(const char*, size_t)>;

// handed out in pieces of at most 512 bytes (a whole screen never sits in RAM twice)
void        expandArt(const std::string& in, const AcsUser& who, const ArtValue& value, const ArtEmit& emit,
                const ArtStyle& style = ArtStyle{});
std::string expandArt(const std::string& in, const AcsUser& who, const ArtValue& value,
                      const ArtStyle& style = ArtStyle{});  // short text, tests

// One piece of art read from a file: added to `carry`, and what is ready goes out, up to the last line break so no code is
// cut in two (a run of 1 KB with no line break goes out but for its last 32 bytes). `last`: the file ends here, all goes out.
// Every art screen (menus, theme screens, the poster) is drawn through this, a piece at a time.
// Ctrl-Z (0x1A) ends the art in every dialect: it and all after it are dropped (the SAUCE record is never drawn). With `ended`
// (one flag per screen, false at its start) the cut also drops the pieces that follow; without, only the rest of this piece.
void artPiece(std::string& carry, const char* raw, size_t n, bool last, const AcsUser& who, const ArtValue& value, const ArtEmit& emit,
              const ArtStyle& style = ArtStyle{}, bool* ended = nullptr);

}  // namespace axis::bbs
