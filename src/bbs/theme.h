// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The look of Axis Micro BBS Firmware, in one place: black, grey, orange and white (Rev's scheme).
// Plain text is light grey or white so it reads easily; headings fade white, light grey, grey;
// windows and art drip orange. In the 16 ANSI colours "orange" is brown (0;33), which BBS
// terminals show as VGA orange; the CYD panel uses real orange.
#pragma once
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace axis::bbs::theme {

constexpr const char* RESET = "\x1b[0m";
constexpr const char* CLS = "\x1b[0m\x1b[2J\x1b[H";
constexpr const char* TEXT = "\x1b[0;37m";    // plain text: light grey
constexpr const char* LGREY = "\x1b[0;37m";
constexpr const char* WHITE = "\x1b[1;37m";   // values, what you type
constexpr const char* DGREY = "\x1b[1;30m";   // dim grey: frames, rules
constexpr const char* ORANGE = "\x1b[0;33m";  // drips, keys, highlights
constexpr const char* HOT = "\x1b[1;33m";     // bright orange-yellow: sparks, the odd glint
constexpr const char* KEY = "\x1b[0;33m";     // menu hotkeys like [F]

// the part of `text` to show in `width` columns at time `ms`: as is if it fits, else scrolling
// through a step every 300 ms with a 1.5 s pause at each end
std::string marquee(const std::string& text, size_t width, uint32_t ms);
std::string fade(const std::string& text);  // white, light grey, grey, light grey, white ... per letter
std::string key(const std::string& letter, const std::string& rest);  // "[F]iles": orange key, fade text
// A framed window with an orange drip top, a faded title, "label  value" rows and drips under it.
std::string window(const std::string& title, const std::vector<std::pair<std::string, std::string>>& rows, int width = 60);
std::string bar(double done, int width);  // a progress bar: orange blocks, grey shade for the rest

}  // namespace axis::bbs::theme
