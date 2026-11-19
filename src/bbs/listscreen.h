// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The list screens used by file and message browsing: the same shape as ViSiON/2, Oblivion/2,
// Mystic and Renegade. A title bar, a column caption, a scrolling lightbar, one status line for
// the highlighted row, and a key line. The lightbar itself is ScrollList; this is only the frame.
#pragma once
#include <string>

#include "bbs/listtemplate.h"

namespace axis::bbs {

// Cursor rows the frame leaves for the bar (ScrollList is placed at row 6, 14 rows tall).
constexpr int kListRow = 6, kListRows = 14, kListStatusRow = 21, kListKeyRow = 23;

std::string listScreenHead(const std::string& artKey, const std::string& title, int rows = 0);  // theme .ANS (rows > 0: its top rows), else banner
// The active look's list template (NAME.LST) into `out`; false when the look has none. Load it once when a list opens, into a
// local, and pass &local (or nullptr) below: no cache, so a live look swap or a sysop edit is seen by the next list.
bool listScreenTemplate(const std::string& name, ListTemplate& out);

// With a template: the head art (tpl->head, else the title's), the caption (expanded art) at row y-2 and the rule (when
// tpl->rule) at row y-1 from column x, width wide; the status line at row y+rows+1 and the keys two rows below it (all
// kept on the 24-row screen). `line`/`keys` arrive already expanded by the caller. nullptr: today's fixed layout.
std::string listScreenTop(const std::string& title, const std::string& columns, const ListTemplate* tpl = nullptr);
std::string listScreenStatus(const std::string& line, const ListTemplate* tpl = nullptr);  // one line, positioned, clipped
std::string listScreenKeys(const std::string& keys, const ListTemplate* tpl = nullptr);

}  // namespace axis::bbs
