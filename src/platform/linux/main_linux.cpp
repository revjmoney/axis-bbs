// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The axis program's process: the board's setup() once, then its loop() for ever, as the ESP32's Arduino core
// does. ESP.restart() ends the process with exit code 3.
#include <csignal>
#include <cstdio>
#include <cstring>

#include "platform/linux/host.h"

void setup();
void loop();

namespace axis::host {
bool        headless = false;
std::string dataDir;
}  // namespace axis::host

int main(int argc, char** argv) {
    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "--headless") == 0) {
            axis::host::headless = true;
        } else if (std::strcmp(argv[i], "--data") == 0 && i + 1 < argc) {
            axis::host::dataDir = argv[++i];
        } else {
            std::fprintf(stderr, "usage: axis [--headless] [--data DIR]\n");
            return 2;
        }
    }
    std::setvbuf(stdout, nullptr, _IOLBF, 0);  // a line at a time: the log reaches a pipe or journal as it happens
    std::signal(SIGPIPE, SIG_IGN);             // a caller who hangs up mid-write is an error return, not the end of the BBS
    setup();
    for (;;) loop();
}
