// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include "platform/pc/consolestream.h"

#include <cstdio>

#ifdef _WIN32
#include <conio.h>
#include <windows.h>
#else
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>
static termios g_saved;
#endif

namespace axis {
namespace pc {

ConsoleStream::ConsoleStream() {
#ifdef _WIN32
    SetConsoleOutputCP(437);
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD  mode = 0;
    GetConsoleMode(h, &mode);
    SetConsoleMode(h, mode | 0x0004);  // ENABLE_VIRTUAL_TERMINAL_PROCESSING
#else
    termios t;
    tcgetattr(0, &g_saved);
    t = g_saved;
    t.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(0, TCSANOW, &t);
#endif
    std::printf("\x1b[2J");
}

ConsoleStream::~ConsoleStream() {
#ifndef _WIN32
    tcsetattr(0, TCSANOW, &g_saved);
#endif
    std::printf("\x1b[0m\n");
}

int ConsoleStream::available() {
    if (pending_ >= 0) return 1;
#ifdef _WIN32
    if (_kbhit()) {
        int c = _getch();
        if (c == 0 || c == 0xE0) {  // extended key: drop the second byte for now
            _getch();
            return 0;
        }
        pending_ = c;
    }
#else
    fd_set  f;
    timeval tv = {0, 0};
    FD_ZERO(&f);
    FD_SET(0, &f);
    if (select(1, &f, nullptr, nullptr, &tv) > 0) {
        unsigned char c;
        if (::read(0, &c, 1) == 1) pending_ = c == '\n' ? '\r' : c;
    }
#endif
    return pending_ >= 0 ? 1 : 0;
}

int ConsoleStream::read() {
    if (!available()) return -1;
    int c = pending_;
    pending_ = -1;
    return c;
}

void ConsoleStream::write(const uint8_t* p, size_t n) { std::fwrite(p, 1, n, stdout); }

void ConsoleStream::draw(const std::array<std::string, 25>& screen) {
    if (screen == last_) return;
    for (int r = 0; r < 25; r++) {
        if (screen[r] == last_[r]) continue;
        std::printf("\x1b[%d;1H", r + 1);
        std::fwrite(screen[r].data(), 1, screen[r].size(), stdout);
    }
    std::fflush(stdout);
    last_ = screen;
}

}  // namespace pc
}  // namespace axis
