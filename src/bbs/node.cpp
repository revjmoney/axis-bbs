// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include "bbs/node.h"

#include <cctype>
#include <cstdio>

namespace axis::bbs {

namespace {
// ANSI colors (CP437 terminal)
const char* const CLS = "\x1b[0m\x1b[2J\x1b[H";
const char* const RST = "\x1b[0m";
const char* const DIM = "\x1b[0;37m";
const char* const HI = "\x1b[1;37m";
const char* const CYAN = "\x1b[1;36m";
const char* const AMBER = "\x1b[0;33m";
const char* const RED = "\x1b[1;31m";

// Placeholder title until the real AXIS ANSI screens are drawn (roadmap stage 9).
std::string welcome(int node, const SysInfo& s, const std::string& bbsName) {
    std::string w = CLS;
    w += AMBER;
    w += " \xb0\xb1\xb2\xdb\xdb\xdb\xdb\xdb\xdb\xdb\xdb\xdb\xdb\xdb\xdb\xdb\xdb\xdb\xdb\xdb\xdb\xdb\xdb\xdb\xdb\xdb\xdb\xdb\xdb\xdb\xdb\xdb\xdb\xdb\xdb\xdb\xdb\xdb\xdb\xdb\xdb\xdb\xdb\xdb\xdb\xdb\xdb\xdb\xdb\xb2\xb1\xb0\r\n";
    w += RED;
    std::string title = bbsName;
    for (char& ch : title) ch = (char)std::toupper((unsigned char)ch);
    w += "         " + title + "\r\n";
    w += AMBER;
    w += " \xb0\xb1\xb2\xdb\xdb\xdb\xdb\xdb\xdb\xdb\xdb\xdb\xdb\xdb\xdb\xdb\xdb\xdb\xdb\xdb\xdb\xdb\xdb\xdb\xdb\xdb\xdb\xdb\xdb\xdb\xdb\xdb\xdb\xdb\xdb\xdb\xdb\xdb\xdb\xdb\xdb\xdb\xdb\xdb\xdb\xdb\xdb\xdb\xdb\xb2\xb1\xb0\r\n";
    w += DIM;
    w += "   rotary axes \xfa toolpaths \xfa G-code \xfa the underground workshop\r\n\r\n";
    char line[96];
    std::snprintf(line, sizeof line, "   %snode %d%s  \xb3  %s  \xb3  %s\r\n\r\n", HI, node, DIM, s.timeText.c_str(),
                  s.host.c_str());
    w += line;
    w += "   Preliminary build: accounts, message bases and doors are on the way.\r\n\r\n";
    return w;
}

std::string uptime(uint32_t sec) {
    char b[32];
    unsigned d = sec / 86400, h = sec / 3600 % 24, m = sec / 60 % 60;
    if (d) std::snprintf(b, sizeof b, "%ud %uh %um", d, h, m);
    else std::snprintf(b, sizeof b, "%uh %um", h, m);
    return b;
}
}  // namespace

Node::Node(int nodeNumber, std::function<SysInfo()> info, BbsConfig cfg)
    : node_(nodeNumber), info_(std::move(info)), cfg_(std::move(cfg)) {}

void Node::begin(uint32_t nowMs) {
    lastInput_ = nowMs;
    out_ += welcome(node_, info_(), cfg_.bbsName);
    prompt();
}

void Node::prompt() {
    if (state_ == State::Login) {
        out_ += CYAN;
        out_ += "   Handle: ";
        out_ += HI;
    } else if (state_ == State::Menu) {
        out_ += DIM;
        out_ += "\r\n   Command: ";
        out_ += HI;
    }
}

void Node::showMenu() {
    out_ += "\r\n\r\n";
    out_ += DIM;
    out_ += "   ";
    for (const char* item : {"[I]", "Info", "[C]", "Credits", "[G]", "Goodbye"}) {
        bool isKey = item[0] == '[';
        out_ += isKey ? CYAN : DIM;
        out_ += item;
        out_ += isKey ? " " : "   ";
    }
    out_ += "\r\n";
    prompt();
}

void Node::input(const uint8_t* p, size_t n, uint32_t nowMs) {
    lastInput_ = nowMs;
    for (size_t i = 0; i < n && state_ != State::Done; i++) key((char)p[i]);
}

void Node::key(char c) {
    if (state_ == State::Login) {
        if (c == '\r') {
            size_t a = line_.find_first_not_of(' '), b = line_.find_last_not_of(' ');
            std::string h = a == std::string::npos ? "" : line_.substr(a, b - a + 1);
            line_.clear();
            if (h.empty()) {
                out_ += "\r\n";
                prompt();
                return;
            }
            handle_ = h;
            state_ = State::Menu;
            out_ += "\r\n\r\n";
            out_ += DIM;
            out_ += "   Welcome, ";
            out_ += HI + handle_ + DIM + ".";
            showMenu();
        } else if (c == '\b' || c == 0x7f) {
            if (!line_.empty()) {
                line_.pop_back();
                out_ += "\b \b";
            }
        } else if ((unsigned char)c >= 0x20 && (unsigned char)c < 0x7f && line_.size() < kMaxHandle) {
            line_ += c;
            out_ += c;
        }
        return;
    }
    if (state_ != State::Menu) return;
    switch (c | 0x20) {  // either case
        case 'i': {
            SysInfo s = info_();
            char b[320];
            std::snprintf(b, sizeof b,
                          "\r\n\r\n   %sSystem%s\r\n   node      %d\r\n   address   %s  (%s)\r\n"
                          "   wifi      %s, %d dBm\r\n   uptime    %s\r\n   free RAM  %u bytes\r\n"
                          "   storage   %s\r\n",
                          HI, DIM, node_, s.ip.c_str(), s.host.c_str(), s.wifi.c_str(), s.rssi,
                          uptime(s.uptimeSec).c_str(), (unsigned)s.freeHeap, s.storage.c_str());
            out_ += b;
            showMenu();
            break;
        }
        case 'c':
            out_ += "\r\n\r\n   ";
            out_ += RED;
            out_ += "AXIS UNDERGROUND CNC";
            out_ += DIM;
            out_ += " by ";
            out_ += HI;
            out_ += "Rev. J. Money";
            out_ += DIM;
            out_ += "\r\n   rotatocam.com   jmscnc.com\r\n\r\n";
            out_ += "   Copyright (C) 2026 Rev. J. Money. Free software under the GNU GPL v3\r\n";
            out_ += "   or later, with an added term: keep this credit (see the README).\r\n";
            showMenu();
            break;
        case 'g':
            out_ += "\r\n\r\n   ";
            out_ += AMBER;
            out_ += "Goodbye from the underground. Keep your tools sharp.";
            out_ += RST;
            out_ += "\r\n";
            state_ = State::Done;
            break;
        default:
            prompt();
            break;
    }
}

void Node::poll(uint32_t nowMs) {
    if (state_ != State::Done && nowMs - lastInput_ > (uint32_t)cfg_.idleMinutes * 60000u) {
        out_ += "\r\n\r\n   ";
        out_ += AMBER;
        out_ += "Logged off: idle too long.";
        out_ += RST;
        out_ += "\r\n";
        state_ = State::Done;
    }
}

std::string Node::takeOutput() {
    std::string s;
    s.swap(out_);
    return s;
}

std::string busyScreen() {
    std::string s = "\x1b[0m\r\n\x1b[1;31m   AXIS UNDERGROUND CNC\x1b[0;37m: all nodes are busy right now.\r\n";
    s += "   Try again in a few minutes.\x1b[0m\r\n";
    return s;
}

}  // namespace axis::bbs
