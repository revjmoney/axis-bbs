// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The window start mode (built with AXIS_WINDOW): the board's 320x240 screen in a desktop window, drawn by
// LovyanGFX's SDL panel (lgfx::Panel_sdl), with the mouse as the touch screen.
//
// Threads. LovyanGFX's SDL panel owns the process's main thread: Panel_sdl::main() runs SDL's events and the drawing
// of the window there, and runs the user's code on a second thread. That second thread is "the AXIS thread": it runs
// everything that is the main thread in the terminal and headless modes (setup(), every loop() turn, hostnetPoll()
// with its DNS callbacks, modemLineStart() and modemLinePoll(), the shutdown). Nothing of AXIS runs on the SDL
// thread. The shutdown signals are blocked on the SDL thread (and every SDL thread made after windowOpen()), so they
// arrive on the AXIS thread as they would on the main thread, and a delay() in progress sees EINTR there.
//
// Closing the window is SIGTERM: an SDL event watch calls hostRequestShutdown() the moment the close arrives (which
// arms the 5 s backstop), then the AXIS thread runs the usual shutdown.
#pragma once
#include <string>

// Main thread, once, before setup(): starts SDL video and makes `tft` a 320x240 SDL panel titled
// "AXIS - <bbsName>", each board pixel `scale` (1 to 6) screen pixels. False (with `why`) when no window can open
// here; nothing is left running then, and the program runs headless.
bool windowOpen(const std::string& bbsName, int scale, std::string& why);

// Main thread, after windowOpen(): runs `axisMain` on the AXIS thread and SDL on this one. `axisMain` must not
// return (it ends in the shutdown's _exit); its `running` turns false once the window is gone.
[[noreturn]] void windowRun(void (*axisMain)(const volatile bool* running));

// AXIS thread: the mouse on the window, in the board's screen coordinates. False when the button is up.
bool windowTouch(int& x, int& y);

// AXIS thread, between loop() turns: the test hooks for running with no display (SDL_VIDEODRIVER=offscreen).
//   AXIS_SHOT=FILE.ppm  writes the board's screen as a PPM image AXIS_SHOT_MS (default 8000) ms after the start.
//   AXIS_CLOSE_MS=MS    closes the window that long after the start, as its close box does.
void windowTestPoll();
