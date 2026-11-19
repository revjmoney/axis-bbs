// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The axis program's process: the board's setup() once, then its loop() for ever, as the ESP32's Arduino core
// does. ESP.restart() ends the process with exit code 3. Before setup(), the start mode is picked (startmode.h), the
// data folder made ready (paths.h), the profile decided (clusterhost.h) and axis.conf read (hostconf.h). A shutdown
// signal ends the loop: callers are hung up through the board's own path, the radio modem hangs up, the terminal is
// put back and the exit code is 0. hostRestart (host.h) does the same, then runs the program again in place.
// In the window start mode the board runs on a second thread, the AXIS thread, and SDL keeps this one (window.h).
#include <pthread.h>
#include <signal.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <Arduino.h>

#include "platform/cyd/config_store.h"
#include "platform/linux/clusterhost.h"
#include "platform/linux/host.h"
#include "platform/linux/hostconf.h"
#include "platform/linux/hostnet.h"
#include "platform/linux/modemline.h"
#include "platform/linux/paths.h"
#include "platform/linux/startmode.h"
#if AXIS_WINDOW
#include "platform/linux/window.h"
#endif

void setup();
void loop();

namespace axis::host {
StartMode   startMode = StartMode::Headless;
std::string dataDir;
bool        windowOn = false;
}  // namespace axis::host

namespace {
constexpr uint32_t kCallersWaitMs = 2000;  // shutdown: main.cpp's own hang-up of each caller, at most this long
constexpr uint32_t kRadioWaitMs = 4500;    // shutdown: the radio modem's guard, +++, guard, ATH, at most this long

std::string modemPath;            // --modem TTY
char**      startArgv = nullptr;  // the command line, for hostRestart's exec
bool        stopping = false;     // stopEverything has begun: the cluster host's work is over for this run

void turn() {
    loop();
    hostnetPoll();            // finished DNS lookups call back here, on the board's thread, between loop() turns
    modemLinePoll(millis());  // the radio line (also pumped from delay())
    if (!stopping) clusterHostPoll(millis());  // (never while stopping: it may be what asked for the restart)
}

// A board that is not set up shows its setup PIN only on its own screen, which this program has not got: the sysop
// sets it up from the console's @ commands instead.
const char kNotSetUp[] =
    "\r\naxis: this board is not set up yet. Its setup PIN is only ever shown on a board's own screen, so set it up\r\n"
    "axis: here with @ commands, one line each:\r\n"
    "axis:   @SET bbs_name=Your BBS Name\r\n"
    "axis:   @SET sysop_handle=YourHandle\r\n"
    "axis:   @SYSOPPASS yourpassword\r\n"
    "axis:   @SET setup_done=1\r\n"
    "axis: then type WFC for the waiting-for-caller screen (restart axis to take more than one caller at once).\r\n";

// Hangs up every caller and the radio line (waiting a moment for each), then puts the terminal back. For the
// shutdown and for a restart alike; the board does not run again in this process afterwards.
void stopEverything() {
    stopping = true;
    modemLineHangUp();       // first, so a radio caller's hang-up starts from a quiet line
    hostnetHangUpCallers();  // main.cpp finds each caller gone on its next turn and hangs it up as usual
    uint32_t t0 = millis();
    for (;;) {
        uint32_t t = millis() - t0;
        bool     callers = hostnetCallersOpen() > 0 && t < kCallersWaitMs;
        bool     radio = modemLineBusy() && t < kRadioWaitMs;
        if (!callers && !radio) break;
        turn();
    }
    modemLineFinish();
    size_t left = hostnetCallersOpen();
    if (left) std::printf("axis: %zu caller%s not hung up in time, closed\n", left, left == 1 ? "" : "s");
    hostTerminalRestore();
}

[[noreturn]] void shutdown() {
    std::printf("axis: stopping: hanging up callers\n");
    stopEverything();
    std::printf("axis: stopped\n");
    std::fflush(nullptr);
    _exit(0);  // not exit(): main.cpp's static objects are not torn down under a board that never stops; the
               // kernel closes every socket and file
}

// The board itself, from axis.conf to the shutdown: on the main thread, or on the AXIS thread in the window start
// mode (window.h). Either way setup(), every loop() turn, hostnetPoll(), modemLineStart() and modemLinePoll() all run
// on this one thread. `running` (the window's; nullptr with no window) turns false once the window is closed, which
// stops the board as SIGTERM does.
[[noreturn]] void runBoard(const volatile bool* running) {
    hostConfLoad(axisDataDir());  // <data>/axis.conf: ports and the accept gate's limits
    hostnetStart();
    modemLineStart(modemPath.empty() ? hostConfStr("modem", "") : modemPath);  // --modem wins over axis.conf's modem=
    setup();
    if (axis::host::startMode == StartMode::Terminal) {
        // The board's own serial "WFC" command opens the waiting-for-caller screen; main.cpp takes it only on a
        // set-up board. setup() has just loaded boardCfg from this same file through this same call, and only setup
        // and @SET change setup_done (both save it), so the file says what boardCfg says.
        if (configLoad().setupDone) Serial.inject("WFC\r");
        else if (axisProfile() == Profile::Solo) Serial.print(kNotSetUp);
        else clusterHostWaitSetup();  // a cluster board is set up by its main's settings: WFC once they come
    }
    while (!hostShutdownRequested()) {
        if (running && !*running) {  // the window is gone (its close box normally starts the shutdown itself)
            std::printf("axis: the window was closed\n");
            hostRequestShutdown();
            break;
        }
        turn();
#if AXIS_WINDOW
        if (running) windowTestPoll();
#endif
    }
    shutdown();
}
}  // namespace

[[noreturn]] void hostRestart(const char* why) {
    static bool once = false;
    if (once) _exit(3);  // (asked again from inside the first one's hang-up: systemd starts it again)
    once = true;
    std::printf("axis: restarting: %s\n", why ? why : "");
    stopEverything();
    std::fflush(nullptr);
    // Terminal mode moved the log onto fd 1 and kept the console on another descriptor: the new program starts on
    // the terminal again, as the first one did.
    if (axis::host::startMode == StartMode::Terminal && axis::host::serialOut >= 0 &&
        axis::host::serialOut != STDOUT_FILENO)
        ::dup2(axis::host::serialOut, STDOUT_FILENO);
    alarm(0);  // a shutdown backstop is not carried into the new program
    sigset_t none;
    sigemptyset(&none);
    pthread_sigmask(SIG_SETMASK, &none, nullptr);  // (the window's AXIS thread or this one: the new program starts clean)
    if (startArgv) ::execv("/proc/self/exe", startArgv);  // every socket and file here is close-on-exec
    std::printf("axis: restart: cannot run the program again (%s), ending with code 3\n", std::strerror(errno));
    std::fflush(nullptr);
    _exit(3);  // code 3: systemd starts it again
}

int main(int argc, char** argv) {
    startArgv = argv;
    std::string seed;
    int         scale = 3;
    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "--headless") == 0 || std::strcmp(argv[i], "--terminal") == 0 ||
            std::strcmp(argv[i], "--window") == 0) {
            // detectStartMode() reads these
        } else if (std::strcmp(argv[i], "--data") == 0 && i + 1 < argc) {
            axis::host::dataDir = argv[++i];
        } else if (std::strcmp(argv[i], "--seed") == 0 && i + 1 < argc) {
            seed = argv[++i];  // a board card's folder, copied into <data>/sd on the first run
        } else if (std::strcmp(argv[i], "--modem") == 0 && i + 1 < argc) {
            modemPath = argv[++i];  // the LFD3K-SM's serial port (/dev/ttyUSB0): it answers radio calls
        } else if (std::strcmp(argv[i], "--scale") == 0 && i + 1 < argc) {
            char* end = nullptr;
            long  n = std::strtol(argv[++i], &end, 10);
            if (end == argv[i] || *end || n < 1 || n > 6) {
                std::fprintf(stderr, "axis: --scale takes 1 to 6 (screen pixels per board pixel in the window)\n");
                return 2;
            }
            scale = (int)n;
        } else {
            std::fprintf(stderr, "usage: axis [--terminal | --window | --headless] [--data DIR] [--seed CARD_FOLDER] "
                                 "[--modem TTY] [--scale 1-6]\n");
            return 2;
        }
    }
    // Root could write anywhere a stray path points; the service runs as its own user (axis), people as themselves.
    if (::geteuid() == 0) {
        std::fprintf(stderr, "axis: not starting as root: run it as an ordinary user (the service runs as user axis)\n");
        return 1;
    }
    StartMode mode = detectStartMode(argc, argv);
    std::setvbuf(stdout, nullptr, _IOLBF, 0);  // a line at a time: the log reaches a pipe or journal as it happens
    hostSignalsInstall();
    std::string why;
    if (!axisDataSetup(axis::host::dataDir, seed, why)) {
        std::fprintf(stderr, "axis: %s\n", why.c_str());
        return 1;
    }
    std::string logPath = axisDataDir() + "/log/axis.log";
    if (mode == StartMode::Terminal) {
        const char* bad = "";
        if (!hostTerminalStart(logPath.c_str(), bad)) {
            std::fprintf(stderr, "axis: terminal mode: %s; running headless\n", bad);
            mode = StartMode::Headless;
        }
    }
    // Which BBS this is (solo, or in the cluster), before anything reads the flash or the prefs: their folders follow it.
    Profile profile = decideProfile();
    axisSetProfile(profile);
    clusterHostStart(profile);
    std::printf("axis: profile %s\n", axisProfileName(profile));
#if AXIS_WINDOW
    if (mode == StartMode::Window) {
        // The title's BBS name, from the file setup() reads a moment later (nothing has to be mounted to read it).
        // The window has to be there before setup(): boardIsHeadless() is asked early and never changes.
        if (windowOpen(configLoad().bbsName, scale, why)) {
            axis::host::windowOn = true;
        } else {
            std::fprintf(stderr, "axis: window: %s; running headless\n", why.c_str());
            mode = StartMode::Headless;
        }
    }
#else
    (void)scale;
#endif
    axis::host::startMode = mode;
    if (mode != StartMode::Terminal) axis::host::serialIn = -1;  // headless and window: the console's input is ignored
    std::printf("axis: start mode %s\n",
                mode == StartMode::Terminal ? "terminal" : mode == StartMode::Window ? "window" : "headless");
    if (mode == StartMode::Terminal) Serial.printf("axis: terminal mode; the program's own log is %s\r\n", logPath.c_str());
    std::printf("axis: data folder %s\n", axisDataDir().c_str());
#if AXIS_WINDOW
    if (mode == StartMode::Window) windowRun(runBoard);  // the board on the AXIS thread; SDL keeps this one
#endif
    runBoard(nullptr);
}
