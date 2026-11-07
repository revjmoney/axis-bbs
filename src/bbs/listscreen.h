// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The list screens used by file and message browsing: the same shape as ViSiON/2, Oblivion/2,
// Mystic and Renegade. A title bar, a column caption, a scrolling lightbar, one status line for
// the highlighted row, and a key line. The lightbar itself is ScrollList; this is only the frame.
#pragma once
#include <string>

namespace axis::bbs {

// Cursor rows the frame leaves for the bar (ScrollList is placed at row 6, 14 rows tall).
constexpr int kListRow = 6, kListRows = 14, kListStatusRow = 21, kListKeyRow = 23;

std::string listScreenHead(const std::string& artKey, const std::string& title);  // theme .ANS, else banner
std::string listScreenTop(const std::string& title, const std::string& columns);
std::string listScreenStatus(const std::string& line);  // one line, positioned, clipped
std::string listScreenKeys(const std::string& keys);

}  // namespace axis::bbs
