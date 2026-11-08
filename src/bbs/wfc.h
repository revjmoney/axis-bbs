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
    bool        operator==(const WfcNode& o) const { return number == o.number && handle == o.handle && activity == o.activity; }
};

struct WfcSnapshot {
    std::string              bbsName, sysop, lastCaller, ip, time, date, page;
    std::vector<WfcNode>     nodes;   // cluster-wide, by number
    std::vector<std::string> events;  // newest first
    uint32_t                 callsToday = 0, postsToday = 0, uploadsToday = 0, sdFreeKb = 0, ramFreeKb = 0, uptimeS = 0;
    bool                     main = true;  // "main board" / "follower board"
    bool                     operator==(const WfcSnapshot& o) const {
        return bbsName == o.bbsName && sysop == o.sysop && lastCaller == o.lastCaller && ip == o.ip && time == o.time && date == o.date &&
               page == o.page && nodes == o.nodes && events == o.events && callsToday == o.callsToday && postsToday == o.postsToday &&
               uploadsToday == o.uploadsToday && sdFreeKb == o.sdFreeKb && ramFreeKb == o.ramFreeKb && uptimeS == o.uptimeS && main == o.main;
    }
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

// A theme's WFC art made safe to draw or send: cut at kWfcArtCap and at a SAUCE/EOF mark, no escape left open at the end, bare LF
// made CR LF.
constexpr size_t kWfcArtCap = 12288;  // a theme's WFC.ANS past this is cut, as the board's theme loader does
std::string      cleanWfcArt(std::string a);

// The key tapped at (row, col), or 0 for none. A key hits on its cell or one cell either side; a node line hits anywhere on its
// row (and sets *node to its number, else 0).
char wfcKeyAt(const std::vector<WfcHotspot>& spots, int row, int col, int* node);

}  // namespace axis::bbs
