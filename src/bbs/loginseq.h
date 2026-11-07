// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// ViSiON/3's login sequence: what a caller sees between logging in and MAIN, in order. One file,
// /menus/LOGIN.SEQ (";" comments), a step per line:  step=COMMAND|CLEAR(0/1)|PAUSE(0/1)|LEVEL|DATA
// (LEVEL: the lowest security level that sees it; DATA: e.g. the screen for DISPLAYFILE).
// No file, or no steps in it: ViSiON/3's shipped template, with the Saylor poster before fast login.
#pragma once
#include <string>
#include <vector>

namespace axis::bbs {

struct LoginStep {
    std::string cmd, data;
    bool        clear = false, pause = false;
    int         level = 0;
};

std::vector<LoginStep> parseLoginSeq(const std::string& text);

}  // namespace axis::bbs
