// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// axispc: the AXIS BBS as a PC program, the same shared core as the board (src/app, src/bbs) on one event loop thread.
//   axispc [--terminal | --headless] [--data DIR] [--seed DIR] [--modem /dev/ttyUSB0]
// --terminal (the default on a terminal): the sysop's WFC screen here (console.h). --headless (the default otherwise): no
// screen, the log on stdout. --data: the data folder (plan 1's layout: sd/, flash/, prefs/, axis.conf; the default is
// /var/lib/axis for user axis, else ~/.axis), so a folder the Linux axis made works as it is. --seed: a board card's
// folder, copied into an empty <data>/sd. --modem: the LRA FLOWTRON DATAMASTER 3000 SERIAL MODEM's tty (else axis.conf's
// modem=). It refuses to run as root. SIGTERM, SIGINT and SIGHUP stop it: every caller hung up, the users and the stats
// saved, exit 0 (a stop that takes more than 5 s is forced; a second Ctrl-C stops at once).
#include <signal.h>
#include <unistd.h>

#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <thread>

#include "bbs/sysopshell.h"
#include "bbs/version.h"
#include "pcapp/console.h"
#include "pcapp/pchost.h"
#include "platform/linux/hostconf.h"
#include "platform/linux/paths.h"
#include "platform/linux/updatecheck.h"

namespace {
volatile sig_atomic_t stopAsked = 0;

void say(const char* s) {  // stderr, async-signal-safe
    ssize_t w = ::write(STDERR_FILENO, s, std::strlen(s));
    (void)w;
}

void onStop(int sig) {
    if (stopAsked && sig == SIGINT) {  // a second Ctrl-C: now
        axis::pcapp::Console::restore();
        say("axis: stopped at once (asked twice)\n");
        _exit(130);
    }
    if (!stopAsked) alarm(5);  // the stop's own deadline
    stopAsked = 1;
}

void onHardStop(int) {
    axis::pcapp::Console::restore();
    say("axis: the stop took more than 5 s, stopping now\n");
    _exit(0);
}

void onCrash(int sig) {  // SA_RESETHAND: the default action follows once the terminal is back
    axis::pcapp::Console::restore();
    raise(sig);
}

void signalsInstall() {
    struct sigaction sa{};
    sigemptyset(&sa.sa_mask);
    sa.sa_handler = onStop;
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
    struct sigaction was{};
    if (sigaction(SIGHUP, nullptr, &was) == 0 && was.sa_handler != SIG_IGN)  // (`nohup axispc` keeps running)
        sigaction(SIGHUP, &sa, nullptr);
    sa.sa_handler = onHardStop;
    sigaction(SIGALRM, &sa, nullptr);
    sa.sa_handler = onCrash;
    sa.sa_flags = SA_RESETHAND;
    for (int s : {SIGSEGV, SIGABRT, SIGBUS, SIGFPE, SIGILL}) sigaction(s, &sa, nullptr);
    std::signal(SIGPIPE, SIG_IGN);  // a caller who hangs up mid-write is an error return, not the end of the BBS
}

int usage() {
    std::fprintf(stderr, "usage: axispc [--terminal | --headless] [--data DIR] [--seed CARD_FOLDER] [--modem TTY]\n");
    return 2;
}
}  // namespace

int main(int argc, char** argv) {
    std::string data, seed, modem;
    int         asked = 0;  // 1 terminal, 2 headless (the last one given)
    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "--terminal") == 0) asked = 1;
        else if (std::strcmp(argv[i], "--headless") == 0) asked = 2;
        else if (std::strcmp(argv[i], "--data") == 0 && i + 1 < argc) data = argv[++i];
        else if (std::strcmp(argv[i], "--seed") == 0 && i + 1 < argc) seed = argv[++i];
        else if (std::strcmp(argv[i], "--modem") == 0 && i + 1 < argc) modem = argv[++i];
        else return usage();
    }
    // Root could write anywhere a stray path points; the service runs as its own user, people as themselves.
    if (::geteuid() == 0) {
        std::fprintf(stderr, "axis: not starting as root: run axispc as an ordinary user\n");
        return 1;
    }
    bool tty = ::isatty(STDIN_FILENO) && ::isatty(STDOUT_FILENO);
    bool terminal = asked ? asked == 1 : tty;
    if (terminal && !tty) {
        std::fprintf(stderr, "axis: --terminal needs a terminal on stdin and stdout; running headless\n");
        terminal = false;
    }
    std::setvbuf(stdout, nullptr, _IOLBF, 0);  // a line at a time: the log reaches a pipe or journal as it happens
    signalsInstall();
    std::string why;
    if (!axisDataSetup(data, seed, why)) {
        std::fprintf(stderr, "axis: %s\n", why.c_str());
        return 1;
    }
    std::string logPath = axisDataDir() + "/log/axispc.log";
    if (terminal && !axis::pcapp::Console::start(logPath, why)) {
        std::fprintf(stderr, "axis: terminal mode: %s; running headless\n", why.c_str());
        terminal = false;
    }
    std::printf("axis: axispc starting, %s mode, data folder %s\n", terminal ? "terminal" : "headless", axisDataDir().c_str());
    hostConfLoad(axisDataDir());  // <data>/axis.conf: the ports, the gate's limits, the modem
    // once a day, off this thread: update_check=0 in axis.conf turns it off. The firmware does not call this.
    axis::pcupdate::start(axisDataDir(), hostConfInt("update_check", 1, 0, 1) != 0, hostConfStr("update_repo", axis::pcupdate::kDefaultRepo),
                          axis::bbs::kAxisVersion);
    axis::bbs::setPcUpdateRow(true);  // System lists Update; the host tests never set this, so their menus stay put
    {
        axis::pcapp::PcBbs bbs(axisDataDir());
        if (!bbs.start(modem, why)) {
            axis::pcupdate::stop();
            std::printf("axis: %s\n", why.c_str());
            axis::pcapp::Console::restore();
            // The reason can hold the data folder's path (from $HOME), so the terminal gets a pointer to the log, which has it.
            axis::pcapp::Console::tell("axis: axispc could not start; the reason is in log/axispc.log in the data folder");
            return 1;
        }
        std::unique_ptr<axis::pcapp::Console> console;
        if (terminal) console.reset(new axis::pcapp::Console(bbs));
        while (!stopAsked) {
            bool moved = bbs.turn();
            if (console) moved = console->poll() || moved;
            axis::pcupdate::poll();  // the release check's result, if its thread has one; it never waits
            if (!moved) std::this_thread::sleep_for(std::chrono::milliseconds(5));  // nothing moved: a 5 ms nap
        }
        console.reset();
        bbs.stop();
    }
    axis::pcapp::Console::restore();
    std::fflush(nullptr);
    return 0;
}
