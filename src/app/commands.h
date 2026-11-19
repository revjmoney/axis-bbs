// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The sysop's @ commands that need no board hardware: users, posts, files, lists, settings, menus, and the callers on
// this program's nodes (kick, more time, notes, broadcasts). The serial console, the control panel and the in-BBS sysop
// menu (bbs/sysopshell.cpp) all reach them; the board's own commands (the screen, Wi-Fi, the SD card, restarts, the
// cluster, Sysop Net) stay in main.cpp, which asks here for the rest.
#pragma once
#include <cstddef>
#include <string>
#include <vector>

namespace axis::app {

struct Services;
class Session;

// Where a command's reply goes. println ends a line with "\r\n" and print/printf add nothing, as Arduino's Print does
// (the replies are byte for byte what the board always sent).
struct Out {
    virtual void write(const char* p, size_t n) = 0;
    virtual ~Out() = default;

    void print(const std::string& s) { write(s.data(), s.size()); }
    void print(const char* s) { write(s, std::char_traits<char>::length(s)); }
    void print(char c) { write(&c, 1); }
    void println(const char* s) {
        print(s);
        println();
    }
    void println() { write("\r\n", 2); }
    void printf(const char* fmt, ...) __attribute__((format(printf, 2, 3)));
};

// One @ command. sessions: every slot, null for a free one (the index is the slot). False: not a command known here.
bool runPortableCommand(const std::string& cmd, Services& s, std::vector<Session*>& sessions, Out& out);

}  // namespace axis::app
