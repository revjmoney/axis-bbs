// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The axis program's process: the board's setup() once, then its loop() for ever, as the ESP32's Arduino core
// does. ESP.restart() ends the process with exit code 3. Before setup(), the data folder is made ready (paths.h)
// and axis.conf read (hostconf.h).
#include <unistd.h>

#include <csignal>
#include <cstdio>
#include <cstring>

#include "platform/linux/host.h"
#include "platform/linux/hostconf.h"
#include "platform/linux/hostnet.h"
#include "platform/linux/paths.h"

void setup();
void loop();

namespace axis::host {
bool        headless = false;
std::string dataDir;
}  // namespace axis::host

int main(int argc, char** argv) {
    std::string seed;
    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "--headless") == 0) {
            axis::host::headless = true;
        } else if (std::strcmp(argv[i], "--data") == 0 && i + 1 < argc) {
            axis::host::dataDir = argv[++i];
        } else if (std::strcmp(argv[i], "--seed") == 0 && i + 1 < argc) {
            seed = argv[++i];  // a board card's folder, copied into <data>/sd on the first run
        } else {
            std::fprintf(stderr, "usage: axis [--headless] [--data DIR] [--seed CARD_FOLDER]\n");
            return 2;
        }
    }
    // Root could write anywhere a stray path points; the service runs as its own user (axis), people as themselves.
    if (::geteuid() == 0) {
        std::fprintf(stderr, "axis: not starting as root: run it as an ordinary user (the service runs as user axis)\n");
        return 1;
    }
    std::setvbuf(stdout, nullptr, _IOLBF, 0);  // a line at a time: the log reaches a pipe or journal as it happens
    std::signal(SIGPIPE, SIG_IGN);             // a caller who hangs up mid-write is an error return, not the end of the BBS
    std::string why;
    if (!axisDataSetup(axis::host::dataDir, seed, why)) {
        std::fprintf(stderr, "axis: %s\n", why.c_str());
        return 1;
    }
    std::printf("axis: data folder %s\n", axisDataDir().c_str());
    hostConfLoad(axisDataDir());  // <data>/axis.conf: ports and the accept gate's limits
    hostnetStart();
    setup();
    for (;;) {
        loop();
        hostnetPoll();  // finished DNS lookups call back here, on the main thread, between loop() turns
    }
}
