// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The look of Axis Micro BBS Firmware, in one place: black, grey, orange and white (Rev's scheme).
// Plain text is light grey or white so it reads easily; headings fade white, light grey, grey;
// windows and art drip orange. In the 16 ANSI colours "orange" is brown (0;33), which BBS
// terminals show as VGA orange; the CYD panel uses real orange.
#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace axis::bbs::theme {

constexpr const char* RESET = "\x1b[0m";
constexpr const char* CLS = "\x1b[0m\x1b[2J\x1b[H";
// The board's look (setting "theme"): Axis (above) or Oblivion, a dark grey and cyan rendition of
// the stock Oblivion/2 screens: double-line boxes, spaced title bars, bright cyan hotkeys. One look
// for the whole board; the colours below follow it.
enum class Look { Axis, Oblivion, Iniquity };
void        use(Look look);
Look        look();
Look        lookFrom(const std::string& name);  // "oblivion", else Axis
const char* lookName(Look look);

extern const char* TEXT;    // plain text: light grey
extern const char* LGREY;
extern const char* WHITE;   // values, what you type
extern const char* DGREY;   // dim grey: frames, rules
extern const char* ORANGE;  // the accent: drips, keys, highlights (cyan in the Oblivion look)
extern const char* HOT;     // bright accent: sparks, the odd glint
extern const char* KEY;     // menu hotkeys like [F]

// Oblivion/2 pipe colours: |00 to |15 set the text colour (resetting the background), |16 to |23
// the background. Anything else is left as it is.
std::string pipes(const std::string& text);

// a menu title as big drippy block letters (several lines, centred in 80 columns)
std::string drip(const std::string& text);
// The theme's header screens from the card: drip("DOORS") shows DOORS.ANS when the theme folder has it
void setArt(std::function<std::string(const std::string&)> art);
std::string artFor(const std::string& title);  // "" when there is none
// fills a header screen's codes (%BN, %NN, %UN...) for the caller being served; a Node sets it while it works
void setMci(std::function<std::string(const std::string&)> fill);
std::string titleBar(const std::string& text);  // the Oblivion look's title: a fading rule around "M E N U"
// the part of `text` to show in `width` columns at time `ms`: as is if it fits, else scrolling
// through a step every 300 ms with a 1.5 s pause at each end
std::string marquee(const std::string& text, size_t width, uint32_t ms);
std::string fade(const std::string& text);  // white, light grey, grey, light grey, white ... per letter
std::string key(const std::string& letter, const std::string& rest);  // "[F]iles": orange key, fade text
// A framed window with an orange drip top, a faded title, "label  value" rows and drips under it.
std::string window(const std::string& title, const std::vector<std::pair<std::string, std::string>>& rows, int width = 60);
std::string bar(double done, int width);  // a progress bar: orange blocks, grey shade for the rest

}  // namespace axis::bbs::theme
