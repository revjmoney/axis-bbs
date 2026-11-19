// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// A list screen's look, read from a .LST file in a look's folder: where the ScrollList sits, its colours, and the
// templates for the caption, one row, the status line and the key line.
//   geom=x,y,width,rows   colours=hi,reg   head=KEY   caption=...   rule=0|1   row=...   status=...   keys=...
//   ';' starts a comment line; unknown keys are ignored (kept for a newer version, like the menu format).
// Row/status/caption text is art: |NN colours, |XX values, @XX####@ (see pipecodes.h), plus two sized fields resolved here:
//   @XXnn@ the field XX cut or padded with spaces (left aligned) to exactly nn columns, @XX>nn@ the same, right aligned.
#pragma once
#include <string>

#include "bbs/acs.h"
#include "bbs/pipecodes.h"

namespace axis::bbs {

struct ListTemplate {
    int         x = 3, y = 6, width = 74, rows = 14;  // the ScrollList geometry; defaults = today's file list
    int         hi = 111, reg = 15;                    // the list colours (DOS attrs), today's defaults
    std::string head;                                  // head art key (artFor), empty = the title banner
    std::string caption;                               // the column caption line, pipe codes (|07Name ...), empty = none
    bool        rule = true;                           // draw the ORANGE rule under the caption
    std::string row;                                   // the per-row template: value tokens and |codes for one list row
    std::string status;                                // the status line (list row kListStatusRow), pipe codes + value tokens
    std::string keys;                                  // the key line (kListKeyRow)
};

// false on a malformed file (no row=, or a geom= that is not four integers). Geometry out of range is clamped, not refused.
bool parseListTemplate(const std::string& text, ListTemplate& out);

// One row: t.row expanded with `field` as the value source, then clipped to t.width visible columns (ANSI escapes do not
// count, no CR/LF is ever emitted). A short result is not padded: the sized field tokens do the padding.
std::string renderRow(const ListTemplate& t, const AcsUser& who, const ArtValue& field);

}  // namespace axis::bbs
