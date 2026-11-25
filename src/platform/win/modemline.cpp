// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The LFD3K-SM radio line on Windows. The Linux build drives the modem's tty (platform/linux/modemline.cpp).
// That port is termios, a lock file and a socket pair; none of that is wired here yet. A path is logged and
// ignored, so axispc still starts and still answers telnet.
#include "platform/linux/modemline.h"

#include <cstdio>
#include <utility>

namespace {
ModemLineHooks hooks;
bool           noted = false;
}  // namespace

void modemLineSetHooks(ModemLineHooks h) { hooks = std::move(h); }

void modemLineStart(const std::string& path) {
    if (path.empty() || noted) return;
    noted = true;
    std::printf("axis: modem: the radio line is not available on Windows yet (%s is not opened)\n", path.c_str());
}

void modemLinePoll(uint32_t) {}

void modemLineHangUp() {}

void modemLineHold(bool) {}

bool modemLineBusy() { return false; }

void modemLineFinish() {}
