// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include "bbs/node.h"

#include "bbs/login_art.h"

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

std::string uptime(uint32_t sec);

// text padded (or cut) to exactly n columns
std::string fit(std::string t, size_t n) {
    if (t.size() > n) t.resize(n);
    return t + std::string(n - t.size(), ' ');
}

// One line of the two boxes: label + value in each, 38 columns inside every box.
std::string boxLine(const char* l1, const std::string& v1, const char* l2, const std::string& v2) {
    const char* const FRAME = "\x1b[0;35m";
    const char* const LABEL = "\x1b[0;36m";
    const char* const VALUE = "\x1b[1;37m";
    std::string       s = FRAME;
    s += "\xba ";
    s += LABEL + fit(l1, 9) + VALUE + fit(v1, 28) + FRAME + "\xba\xba ";
    s += LABEL + fit(l2, 9) + VALUE + fit(v2, 28) + FRAME + "\xba\r\n";
    return s;
}

std::string boxEdge(bool top) {
    std::string s = "\x1b[0;35m";
    if (top) {
        s += "\xc9\xcd\xb5\x1b[1;33m THE BOARD \x1b[0;35m\xc6" + std::string(24, '\xcd') + "\xbb";
        s += "\xc9\xcd\xb5\x1b[1;33m THE SYSTEM \x1b[0;35m\xc6" + std::string(23, '\xcd') + "\xbb\r\n";
    } else {
        s += "\xc8" + std::string(38, '\xcd') + "\xbc\xc8" + std::string(38, '\xcd') + "\xbc\r\n";
    }
    return s;
}

// The caller's first screen: the drippy logo (art/login.ans), then the board and the system.
std::string welcome(int node, const SysInfo& s, const BbsConfig& cfg) {
    std::string w = CLS;
    w += kLoginArt;
    w += RST;
    w += "\r\n";
    std::string who = std::to_string(s.nodesTotal ? s.nodes.size() : 1) + " of " + std::to_string(s.nodesTotal) + "  ";
    for (size_t i = 0; i < s.nodes.size(); i++)
        who += "[" + std::to_string(i + 1) + "] " + ((int)i + 1 == node ? "you" : s.nodes[i].empty() ? "waiting" : s.nodes[i]) + " ";
    std::string dial = s.host + "  " + s.ip;
    w += boxEdge(true);
    w += boxLine("BBS", cfg.bbsName, "software", std::string("AXIS UNDERGROUND CNC ") + kAxisVersion);
    w += boxLine("sysop", cfg.sysopHandle.empty() ? "the sysop" : cfg.sysopHandle, "license", "GPL-3.0+ by Rev. J. Money");
    w += boxLine("from", cfg.location.empty() ? "the underground" : cfg.location, "hardware", s.hardware);
    w += boxLine("dial", dial, "storage", s.storage);
    w += boxLine("nodes", who, "up", uptime(s.uptimeSec) + "  calls today " + std::to_string(s.callsToday));
    w += boxEdge(false);
    w += "  \x1b[1;33mjmscnc.com\x1b[0;37m the JMSCNC store  \x1b[0;35m\xfa  \x1b[1;33mrotatocam.com\x1b[0;37m RotatoCAM  ";
    w += "\x1b[0;35m\xfa  \x1b[0;37mFTN " + cfg.ftnAddress + "\r\n";  // 72 columns: no wrap
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
    out_ += welcome(node_, info_(), cfg_);
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
    } else if (state_ == State::Doors) {
        out_ += DIM;
        out_ += "\r\n   Door number (Enter for the menu): ";
        out_ += HI;
    }
}

void Node::showDoors() {
    out_ += "\r\n\r\n   ";
    out_ += HI;
    out_ += "Doors";
    out_ += "\r\n";
    for (size_t i = 0; i < cfg_.doors.size() && i < 9; i++) {
        char b[96];
        std::snprintf(b, sizeof b, "   %s[%u]%s %s\r\n", CYAN, (unsigned)(i + 1), DIM, cfg_.doors[i].name.c_str());
        out_ += b;
    }
    state_ = State::Doors;
    prompt();
}

void Node::resumeAfterDoor(DoorEnd how, uint32_t nowMs) {
    if (state_ != State::InDoor) return;
    lastInput_ = nowMs;
    state_ = State::Menu;
    out_ += RST;
    out_ += "\x1b[2J\x1b[H\r\n   ";
    out_ += DIM;
    out_ += "Back from " + door().name + ".";
    const char* why = how == DoorEnd::OutOfMemory ? "The door ran out of memory and was closed; your game is saved up to its last write."
                      : how == DoorEnd::TimeLimit ? "Time is up for this call in the door."
                      : how == DoorEnd::LoadError ? "That door couldn't be started (its files may be missing)."
                                                  : nullptr;
    if (why) {
        out_ += "\r\n   ";
        out_ += AMBER;
        out_ += why;
    }
    showMenu();
}

void Node::showMenu() {
    out_ += "\r\n\r\n";
    out_ += DIM;
    out_ += "   ";
    for (const char* item : {"[D]", "Doors", "[I]", "Info", "[C]", "Credits", "[G]", "Goodbye"}) {
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
    if (state_ == State::Doors) {
        size_t pick = c >= '1' && c <= '9' ? (size_t)(c - '1') : cfg_.doors.size();
        if (pick < cfg_.doors.size()) {
            door_ = pick;
            state_ = State::InDoor;
            out_ += c;
            out_ += "\r\n\r\n   ";
            out_ += DIM;
            out_ += "Opening " + door().name + "...";
            out_ += RST;
            out_ += "\r\n";
        } else {
            state_ = State::Menu;
            showMenu();
        }
        return;
    }
    if (state_ != State::Menu) return;
    switch (c | 0x20) {  // either case
        case 'd':
            showDoors();
            break;
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
    if (state_ != State::Done && state_ != State::InDoor && nowMs - lastInput_ > (uint32_t)cfg_.idleMinutes * 60000u) {
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
