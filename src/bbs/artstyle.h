// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The art dialect a look draws in, and its colour slots (THEME.CFG): which extra codes the art expander reads.
#pragma once
#include <cstdint>
#include <string>

namespace axis::bbs {

enum class Dialect : uint8_t { Axis, Vision3, Renegade };

struct ArtStyle {
    Dialect dialect = Dialect::Axis;
    uint8_t c[7] = {7, 15, 7, 15, 8, 11, 31};              // ViSiON/3 |C1..|C7
    uint8_t r[10] = {7, 11, 14, 15, 9, 13, 12, 10, 8, 3};  // Renegade ^0..^9
};

extern const char kDosToAnsi[8];  // DOS colour order to ANSI's, as the digit of an SGR code

const char* dialectName(Dialect d);                              // "axis", "vision3", "renegade"
bool        dialectFrom(const std::string& name, Dialect& out);  // case-insensitive; false when unknown
std::string attrSgr(uint8_t attr);                               // a packed DOS attribute as one SGR sequence
// key=value lines (dialect=, c1..c7=, r0..r9=; ';' starts a comment; other keys are ignored). Any bad line: false, `out` = ArtStyle{}.
// `why` (optional) hears what was wrong, in a few words.
bool parseThemeCfg(const std::string& text, ArtStyle& out, std::string* why = nullptr);

}  // namespace axis::bbs
