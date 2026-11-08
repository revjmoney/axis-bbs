// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The waiting-for-caller screen: Renegade-style ANSI art with pipe codes (see docs/WFC-CODES.md) filled in with live values,
// plus where the hotkey letters and node lines landed so the CYD can make them tappable.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "bbs/ansiterm.h"
#include "bbs/pipecodes.h"

namespace axis::bbs {

struct WfcNode {
    int         number = 0;
    std::string handle, activity;  // handle "" = waiting
};

struct WfcSnapshot {
    std::string              bbsName, sysop, lastCaller, ip, time, date, page;
    std::vector<WfcNode>     nodes;   // cluster-wide, by number
    std::vector<std::string> events;  // newest first
    uint32_t                 callsToday = 0, postsToday = 0, uploadsToday = 0, sdFreeKb = 0, ramFreeKb = 0, uptimeS = 0;
    bool                     main = true;  // "main board" / "follower board"
};

// the |XX / @XX###@ value function for expandArt
ArtValue wfcValues(const WfcSnapshot& s);

struct WfcHotspot {
    int  row, col;
    char key;   // S U # B F L E A ! space; 'N' for a node line
    int  node;  // > 0 for a node line
};

// Expands WFC art into `term`, recording where the |K? and |N? codes landed.
std::vector<WfcHotspot> drawWfc(const std::string& art, const WfcSnapshot& s, AnsiTerm& term);

// The key tapped at (row, col), or 0 for none. A key hits on its cell or one cell either side; a node line hits anywhere on its
// row (and sets *node to its number, else 0).
char wfcKeyAt(const std::vector<WfcHotspot>& spots, int row, int col, int* node);

}  // namespace axis::bbs
