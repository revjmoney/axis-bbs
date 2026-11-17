// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// Start modes, the terminal's raw mode, and the shutdown signals (startmode.h).
#include "platform/linux/startmode.h"

#include <fcntl.h>
#include <termios.h>
#include <unistd.h>

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "platform/linux/host.h"

#ifndef AXIS_WINDOW
#define AXIS_WINDOW 0  // CMake sets it to 1 when the window (SDL) is built in
#endif

namespace {
constexpr unsigned kHardStopS = 5;  // a shutdown that has not finished by now (a stuck loop() turn) is forced

volatile sig_atomic_t stopAsked = 0;
volatile sig_atomic_t termOn = 0;  // the terminal is raw and on the alternate screen
termios               saved{};     // the terminal as it was
int                   termIn = -1, termOut = -1;

void putOut(const char* s) {  // async-signal-safe
    if (termOut < 0) return;
    size_t n = std::strlen(s);
    while (n) {
        ssize_t w = ::write(termOut, s, n);
        if (w <= 0) return;
        s += w;
        n -= (size_t)w;
    }
}

void say(const char* s) {  // stderr, async-signal-safe
    ssize_t w = ::write(STDERR_FILENO, s, std::strlen(s));
    (void)w;
}

void onStop(int sig) {
    // A second Ctrl-C means "now". A repeated SIGHUP or SIGTERM does not: a shell that loses its ssh session sends
    // SIGHUP more than once, and the callers' and the modem's hang-ups still deserve their turn (the alarm bounds it).
    if (stopAsked && sig == SIGINT) {
        hostTerminalRestore();
        say("axis: stopped at once (asked twice)\n");
        _exit(sig == SIGINT ? 130 : 1);
    }
    hostRequestShutdown();
}

void onHardStop(int) {  // the shutdown's own deadline: a loop() turn never came back
    hostTerminalRestore();
    say("axis: the shutdown took more than 5 s, stopping now\n");
    _exit(0);
}

void onCrash(int sig) {  // SA_RESETHAND: the default action (a core, the abort) follows once the terminal is back
    hostTerminalRestore();
    raise(sig);
}

bool isFlag(const char* a, StartMode& m) {
    if (std::strcmp(a, "--terminal") == 0) m = StartMode::Terminal;
    else if (std::strcmp(a, "--window") == 0) m = StartMode::Window;
    else if (std::strcmp(a, "--headless") == 0) m = StartMode::Headless;
    else return false;
    return true;
}
}  // namespace

StartMode detectStartMode(int argc, char** argv) {
    StartMode m = StartMode::Headless;
    bool      asked = false;
    for (int i = 1; i < argc; i++)
        if (isFlag(argv[i], m)) asked = true;
    bool tty = ::isatty(STDIN_FILENO) && ::isatty(STDOUT_FILENO);
    if (!asked) {
        if (tty) return StartMode::Terminal;
        if (AXIS_WINDOW && (std::getenv("DISPLAY") || std::getenv("WAYLAND_DISPLAY"))) m = StartMode::Window;
        else return StartMode::Headless;
    }
    if (m == StartMode::Terminal && !tty) {
        std::fprintf(stderr, "axis: --terminal needs a terminal on stdin and stdout; running headless\n");
        return StartMode::Headless;
    }
    if (m == StartMode::Window && !AXIS_WINDOW) {
        std::fprintf(stderr, "axis: this axis was built without the window (AXIS_WINDOW); running headless\n");
        return StartMode::Headless;
    }
    return m;
}

void hostSignalsInstall() {
    struct sigaction sa{};
    sigemptyset(&sa.sa_mask);
    sa.sa_handler = onStop;  // no SA_RESTART: a wait in progress sees EINTR and the loop gets on to the shutdown
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
    struct sigaction was{};
    if (sigaction(SIGHUP, nullptr, &was) == 0 && was.sa_handler != SIG_IGN)  // `nohup axis` keeps running on a hangup
        sigaction(SIGHUP, &sa, nullptr);
    sa.sa_handler = onHardStop;
    sigaction(SIGALRM, &sa, nullptr);
    sa.sa_handler = onCrash;
    sa.sa_flags = SA_RESETHAND;
    for (int s : {SIGSEGV, SIGABRT, SIGBUS, SIGFPE, SIGILL}) sigaction(s, &sa, nullptr);
    std::signal(SIGPIPE, SIG_IGN);  // a caller who hangs up mid-write is an error return, not the end of the BBS
}

bool hostTerminalStart(const char* logPath, const char*& why) {
    if (tcgetattr(STDIN_FILENO, &saved) != 0) {
        why = "cannot read the terminal's settings";
        return false;
    }
    int out = ::dup(STDOUT_FILENO);  // the console keeps the terminal; fd 1 (the program's log) moves to the file
    int log = ::open(logPath, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0600);
    if (out < 0 || log < 0) {
        if (out >= 0) ::close(out);
        if (log >= 0) ::close(log);
        why = "cannot open the log file";
        return false;
    }
    std::fflush(stdout);
    ::dup2(log, STDOUT_FILENO);
    ::close(log);
    ::fcntl(out, F_SETFD, FD_CLOEXEC);
    termIn = STDIN_FILENO;
    termOut = out;

    termios raw = saved;
    // keys straight in: no line editing, no echo, CR stays CR (the board's console wants CR), no flow control
    raw.c_iflag &= ~(tcflag_t)(BRKINT | ICRNL | INLCR | IGNCR | INPCK | ISTRIP | IXON | IXOFF);
    raw.c_lflag &= ~(tcflag_t)(ICANON | ECHO | ECHONL | IEXTEN);
    raw.c_cflag |= CS8;
    // ISIG stays, for Ctrl-C alone (it stops the program cleanly); Ctrl-\ and Ctrl-Z go to the board as keys
    raw.c_lflag |= ISIG;
    raw.c_cc[VQUIT] = _POSIX_VDISABLE;
    raw.c_cc[VSUSP] = _POSIX_VDISABLE;
#ifdef VDSUSP
    raw.c_cc[VDSUSP] = _POSIX_VDISABLE;
#endif
    // output processing stays (LF becomes CR LF): the board's own log lines end in a bare LF
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(termIn, TCSAFLUSH, &raw) != 0) {
        ::dup2(out, STDOUT_FILENO);
        ::close(out);
        termIn = termOut = -1;
        why = "cannot put the terminal in raw mode";
        return false;
    }
    termOn = 1;
    std::atexit(hostTerminalRestore);
    axis::host::serialIn = STDIN_FILENO;
    axis::host::serialOut = out;
    putOut("\x1b[?1049h\x1b[?25l\x1b[0m\x1b[2J\x1b[H");  // the alternate screen, cursor hidden
    return true;
}

void hostTerminalRestore() {
    if (!termOn) return;
    termOn = 0;
    putOut("\x1b[0m\x1b[?25h\x1b[?1049l");
    tcsetattr(termIn, TCSANOW, &saved);  // (not TCSAFLUSH: it waits for the output, and a gone terminal never takes it)
}

void hostRequestShutdown() {
    if (stopAsked) return;
    stopAsked = 1;
    alarm(kHardStopS);
}

bool hostShutdownRequested() { return stopAsked != 0; }
