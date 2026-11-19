// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// axispc's terminal mode: the sysop's own screen, what the CYD's panel is to the board. The terminal goes raw on its
// alternate screen, and the program's log (stdout, stderr) moves to <data>/log/axispc.log so it never draws over it.
// On a set-up BBS the screen is a local Node on the 80x25 waiting-for-caller screen (Node::openWfc: the theme's WFC.ANS or
// the built-in art/wfc80.ans, filled from PcBbs's wfcSnapshot with the node list, pc tags and the newest events). Its keys,
// as the board's WFC: Space logs the sysop on locally (the main menu), S U # B F L open the in-BBS sysop menu
// (SysopShell) at that section, Esc in there comes back, Q draws the WFC afresh. Ctrl-C stops axispc. Not set up yet: a
// screen with the port and the setup PIN to type over telnet. What the Node draws goes through an 80x25 AnsiTerm and is
// painted cell by cell (CP437 as UTF-8), so a terminal wider than 80 columns shows it right too.
#pragma once
#include <cstdint>
#include <memory>
#include <string>

#include "bbs/ansiterm.h"
#include "bbs/node.h"

namespace axis::pcapp {

class PcBbs;

class Console {
public:
    // Raw keys, the alternate screen and the log moved to `logPath`. False (with why): the program runs headless.
    static bool start(const std::string& logPath, std::string& why);
    // The terminal as it was (safe twice, and from a signal handler).
    static void restore();
    // A line for the sysop on the terminal itself (after restore(): a reason the program stopped).
    static void tell(const std::string& line);

    explicit Console(PcBbs& bbs);
    ~Console();
    Console(const Console&) = delete;
    Console& operator=(const Console&) = delete;

    // Keys in, the screen out. True when anything moved.
    bool poll();

private:
    void paint();
    void setupScreen();

    PcBbs&                     bbs_;
    std::unique_ptr<bbs::Node> node_;
    bbs::AnsiTerm              term_;
    bbs::AnsiTerm::Cell        shown_[25][80];
    bool                       painted_ = false;
    int                        setupShown_ = -1;  // the not-set-up screen is up (1) or not (0); -1 before the first poll
};

}  // namespace axis::pcapp
