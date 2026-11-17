// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// How the axis program starts, and how it stops.
//   Terminal: the board's serial console on this terminal. Keys go straight in (raw), the screen is the terminal's
//     alternate screen with the cursor hidden, and the waiting-for-caller screen (the board's serial "WFC") opens by
//     itself on a set-up board. The program's own log lines go to <data>/log/axis.log so they never draw over it.
//   Window: the board's screen in a window (plan task 6; until then it starts headless).
//   Headless: the console's output is the log (stdout, the journal); its input is ignored.
// Stopping: SIGINT, SIGTERM and SIGHUP (unless it was ignored, as under nohup; and later the window's close box) ask
// for a shutdown. The main loop then hangs up every caller through the board's own hang-up path, hangs the radio modem
// up, restores the terminal and exits 0, within 5 s (a timer forces the exit if a loop() turn never ends). A second
// SIGINT exits at once. A crash restores the terminal too.
#pragma once

enum class StartMode { Terminal, Window, Headless };

// --terminal, --window and --headless win (the last one given); otherwise Terminal when stdin and stdout are both a
// terminal, else Window when the window is built in and DISPLAY or WAYLAND_DISPLAY is set, else Headless. A mode that
// cannot run here (--terminal with no terminal, --window for now) says so on stderr and falls back to Headless.
StartMode detectStartMode(int argc, char** argv);

// The signal handlers (shutdown, and the crash ones that restore the terminal). Once, early.
void hostSignalsInstall();

// Terminal mode: raw keys, the alternate screen, the hidden cursor, and the program's log moved to `logPath`
// (the console, Serial, keeps the terminal). False (with `why`) when it could not; the program then runs headless.
bool hostTerminalStart(const char* logPath, const char*& why);

// Puts the terminal back as it was (safe to call more than once, and from a signal handler).
void hostTerminalRestore();

// The one shutdown path: signals call it, and the window's close box will. Async-signal-safe.
void hostRequestShutdown();
bool hostShutdownRequested();
