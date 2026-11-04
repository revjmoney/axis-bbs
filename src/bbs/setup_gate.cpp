// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#pragma GCC optimize("Os")  // menus and screens: size over speed (the door CPU and transfers stay -O2)
#include "bbs/setup_gate.h"

namespace axis::bbs {

SetupGate::SetupGate(std::string pin) : pin_(std::move(pin)) {}

void SetupGate::begin() {
    out_ += "\x1b[0m\x1b[2J\x1b[H\r\n   \x1b[0;33mAXIS UNDERGROUND CNC\x1b[0;37m\r\n\r\n";
    out_ += "   This BBS is being set up. If you are the sysop, type the setup PIN\r\n";
    out_ += "   shown on the board's screen.\r\n";
    prompt();
}

void SetupGate::prompt() { out_ += "\r\n   \x1b[0;33mSetup PIN: \x1b[1;37m"; }

void SetupGate::input(const uint8_t* p, size_t n) {
    for (size_t i = 0; i < n && !passed_ && !rejected(); i++) {
        char c = (char)p[i];
        if (c == '\r') {
            if (line_ == pin_) {
                passed_ = true;
            } else if (++tries_ < kTries) {
                out_ += "\r\n   \x1b[0;33mThat PIN is not right.";
                prompt();
            } else {
                out_ += "\r\n   \x1b[0;33mToo many tries. Goodbye.\x1b[0m\r\n";
            }
            line_.clear();
        } else if (c == '\b' || c == 0x7f) {
            if (!line_.empty()) {
                line_.pop_back();
                out_ += "\b \b";
            }
        } else if (c >= '0' && c <= '9' && line_.size() < 12) {
            line_ += c;
            out_ += '*';
        }
    }
}

std::string SetupGate::takeOutput() {
    std::string s;
    s.swap(out_);
    return s;
}

}  // namespace axis::bbs
