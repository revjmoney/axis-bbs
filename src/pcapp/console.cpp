// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// axispc's terminal mode (console.h).
#include "pcapp/console.h"

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "pcapp/pchost.h"
#include "platform/os/os.h"

namespace axis::pcapp {

namespace {
// CP437's upper half as Unicode, for a UTF-8 terminal (the art is CP437 bytes)
const uint16_t kCp437[128] = {
    0x00C7, 0x00FC, 0x00E9, 0x00E2, 0x00E4, 0x00E0, 0x00E5, 0x00E7, 0x00EA, 0x00EB, 0x00E8, 0x00EF, 0x00EE, 0x00EC, 0x00C4, 0x00C5,
    0x00C9, 0x00E6, 0x00C6, 0x00F4, 0x00F6, 0x00F2, 0x00FB, 0x00F9, 0x00FF, 0x00D6, 0x00DC, 0x00A2, 0x00A3, 0x00A5, 0x20A7, 0x0192,
    0x00E1, 0x00ED, 0x00F3, 0x00FA, 0x00F1, 0x00D1, 0x00AA, 0x00BA, 0x00BF, 0x2310, 0x00AC, 0x00BD, 0x00BC, 0x00A1, 0x00AB, 0x00BB,
    0x2591, 0x2592, 0x2593, 0x2502, 0x2524, 0x2561, 0x2562, 0x2556, 0x2555, 0x2563, 0x2551, 0x2557, 0x255D, 0x255C, 0x255B, 0x2510,
    0x2514, 0x2534, 0x252C, 0x251C, 0x2500, 0x253C, 0x255E, 0x255F, 0x255A, 0x2554, 0x2569, 0x2566, 0x2560, 0x2550, 0x256C, 0x2567,
    0x2568, 0x2564, 0x2565, 0x2559, 0x2558, 0x2552, 0x2553, 0x256B, 0x256A, 0x2518, 0x250C, 0x2588, 0x2584, 0x258C, 0x2590, 0x2580,
    0x03B1, 0x00DF, 0x0393, 0x03C0, 0x03A3, 0x03C3, 0x00B5, 0x03C4, 0x03A6, 0x0398, 0x03A9, 0x03B4, 0x221E, 0x03C6, 0x03B5, 0x2229,
    0x2261, 0x00B1, 0x2265, 0x2264, 0x2320, 0x2321, 0x00F7, 0x2248, 0x00B0, 0x2219, 0x00B7, 0x221A, 0x207F, 0x00B2, 0x25A0, 0x00A0};

void putOut(const char* s, size_t n) { os::termWrite(s, n); }  // the real terminal; signal-safe on Linux
void putOut(const std::string& s) { putOut(s.data(), s.size()); }

void putGlyph(std::string& out, uint8_t ch) {
    if (ch < 0x20 || ch == 0x7f) ch = ' ';  // (a control byte in a cell: nothing a terminal should get)
    if (ch < 0x80) {
        out += (char)ch;
        return;
    }
    uint16_t u = kCp437[ch - 0x80];
    if (u < 0x800) {
        out += (char)(0xC0 | (u >> 6));
        out += (char)(0x80 | (u & 0x3F));
    } else {
        out += (char)(0xE0 | (u >> 12));
        out += (char)(0x80 | ((u >> 6) & 0x3F));
        out += (char)(0x80 | (u & 0x3F));
    }
}

std::string sgr(uint8_t attr) {
    static const char kAnsi[8] = {0, 4, 2, 6, 1, 5, 3, 7};  // CGA colour number to ANSI colour number
    char b[28];
    std::snprintf(b, sizeof b, "\x1b[0;%s3%d;%dm", (attr & 8) ? "1;" : "", kAnsi[attr & 7], (attr & 0x80 ? 100 : 40) + kAnsi[(attr >> 4) & 7]);  // 100..107: the bright backgrounds
    return b;
}
}  // namespace

bool Console::start(const std::string& logPath, std::string& why) {
    if (!os::termStart(logPath, why)) return false;
    std::atexit(restore);
    return true;
}

void Console::restore() { os::termRestore(); }

void Console::tell(const std::string& line) { os::termTell(line); }

Console::Console(PcBbs& bbs) : bbs_(bbs), term_(80, 25) {}

Console::~Console() { bbs_.setConsoleNode(nullptr); }

void Console::setupScreen() {
    char b[640];
    std::snprintf(b, sizeof b,
                  "\x1b[2J\x1b[H\x1b[1;37m  AXIS PC\x1b[0;37m\r\n\r\n"
                  "  This BBS is not set up yet.\r\n\r\n"
                  "  Telnet to this computer on port \x1b[1;33m%u\x1b[0;37m and type the setup PIN when it asks:\r\n\r\n"
                  "      \x1b[1;32m%s\x1b[0;37m\r\n\r\n"
                  "  The waiting-for-caller screen opens here once setup is saved.\r\n"
                  "  Ctrl-C stops axispc.",
                  (unsigned)bbs_.lanPort(), bbs_.setupPin().c_str());
    term_.feed(b);
}

void Console::paint() {
    if (!term_.dirty() && painted_) return;
    std::string out;
    int         cur = -1;
    for (int r = 0; r < 25; r++) {
        bool same = painted_;
        for (int c = 0; c < 80 && same; c++) {
            const bbs::AnsiTerm::Cell& a = term_.cell(r, c);
            same = a.ch == shown_[r][c].ch && a.attr == shown_[r][c].attr;
        }
        if (same) continue;
        char at[16];
        std::snprintf(at, sizeof at, "\x1b[%d;1H", r + 1);
        out += at;
        for (int c = 0; c < 80; c++) {
            const bbs::AnsiTerm::Cell& a = term_.cell(r, c);
            if (a.attr != cur) {
                out += sgr(a.attr);
                cur = a.attr;
            }
            putGlyph(out, a.ch);
            shown_[r][c] = a;
        }
    }
    painted_ = true;
    term_.clean();
    bool typing = node_ && node_->state() != bbs::Node::State::Wfc;  // a cursor where the sysop types, not on the WFC
    char at[48];
    std::snprintf(at, sizeof at, "\x1b[0m\x1b[%d;%dH%s", term_.row() + 1, term_.col() + 1, typing ? "\x1b[?25h" : "\x1b[?25l");
    out += at;
    putOut(out);
}

bool Console::poll() {
    bool    moved = false;
    uint32_t now = bbs_.host().nowMs();
    bool    up = bbs_.setUp();
    if (!up) {
        if (node_) {  // (a sysop's @SET setup_done=0, say: the setup screen again)
            bbs_.setConsoleNode(nullptr);
            node_.reset();
        }
        if (setupShown_ != 1) {
            setupShown_ = 1;
            setupScreen();
            moved = true;
        }
    } else {
        setupShown_ = 0;
        if (!node_) {  // the WFC: the sysop logged in locally (no password), on the waiting-for-caller screen
            node_ = bbs_.makeLocalNode();
            bbs_.setConsoleNode(node_.get());
            node_->beginLocal(now);
            node_->openWfc(now);
            moved = true;
        }
    }
    char keys[256];
    int  n = 0;
    if (os::termRead(keys, (int)sizeof keys, n) && n > 0) {
        moved = true;
        if (node_) node_->input((const uint8_t*)keys, (size_t)n, now);
    }
    if (node_) {
        node_->poll(now);
        using S = bbs::Node::State;
        if (node_->state() == S::InDoor) node_->resumeAfterDoor(bbs::Node::DoorEnd::NotHere, now);  // no door at the console
        if (node_->state() == S::Transfer)
            node_->transferDone(false, 0, 0, "downloads need a terminal: call in over telnet", now);
        std::string o = node_->takeOutput();
        if (!o.empty()) {
            term_.feed(o);
            moved = true;
        }
        if (node_->wantsHangup()) {  // Q on the WFC, or the sysop logged off: the WFC afresh at the next poll
            bbs_.setConsoleNode(nullptr);
            node_.reset();
        }
    }
    paint();
    return moved;
}

}  // namespace axis::pcapp
