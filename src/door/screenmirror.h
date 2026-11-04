// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// A door that draws straight to the PC screen (a setup program like LORDCFG) shown to a caller:
// the 80x25 text page (char, attribute pairs) turned into ANSI, the first frame whole, then only
// the cells that changed. Characters go out as CP437 bytes, as the BBS sends everything else.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace axis::door {

class ScreenMirror {
public:
    std::string update(const std::vector<uint8_t>& page);  // what to send for this frame ("" = no change)
    void        reset() { last_.clear(); }                 // the next frame is drawn whole

private:
    std::vector<uint8_t> last_;
};

}  // namespace axis::door
