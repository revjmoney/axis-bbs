// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The window start mode: LovyanGFX's SDL panel as the board's screen (window.h). Built only with AXIS_WINDOW.
#include "platform/linux/window.h"

#include <pthread.h>

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include <Arduino.h>

#include "platform/cyd/board.h"
#include "platform/linux/startmode.h"

namespace {
constexpr int kWidth = 320, kHeight = 240;  // the CYD in landscape, as main.cpp's boardInit(1, ...) asks for

lgfx::Panel_sdl* panel = nullptr;
std::string      title;  // Panel_sdl keeps the pointer, so the text lives here
void (*axisMain)(const volatile bool*) = nullptr;

// The stop signals and the shutdown's alarm: the AXIS thread takes them, never an SDL thread.
sigset_t stopSignals() {
    sigset_t s;
    sigemptyset(&s);
    for (int sig : {SIGINT, SIGTERM, SIGHUP, SIGALRM}) sigaddset(&s, sig);
    return s;
}

// A window and a renderer as Panel_sdl will ask for them (it creates its own on the SDL thread and never checks
// either: a failure there would retry for ever). With no accelerated renderer (no GL: a remote desktop, the offscreen
// test driver) SDL's software renderer is named instead, which Panel_sdl's request then gets.
bool probe(std::string& why) {
    SDL_Window* w = SDL_CreateWindow("AXIS", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, kWidth, kHeight,
                                     SDL_WINDOW_HIDDEN);
    if (!w) {
        why = std::string("no window can open here (") + SDL_GetError() + ")";
        return false;
    }
    const Uint32  flags = SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC;  // Panel_sdl's
    SDL_Renderer* r = SDL_CreateRenderer(w, -1, flags);
    if (!r) {
        SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
        r = SDL_CreateRenderer(w, -1, flags);
    }
    if (!r) {
        why = std::string("no renderer for the window (") + SDL_GetError() + ")";
        SDL_DestroyWindow(w);
        return false;
    }
    SDL_RendererInfo info{};
    SDL_GetRendererInfo(r, &info);
    std::printf("axis: window: SDL video %s, renderer %s\n", SDL_GetCurrentVideoDriver(), info.name ? info.name : "?");
    SDL_DestroyRenderer(r);
    SDL_DestroyWindow(w);
    return true;
}

int axisThread(bool* running) {
    sigset_t s = stopSignals();
    pthread_sigmask(SIG_UNBLOCK, &s, nullptr);
    axisMain(running);
    return 0;  // (never: axisMain ends in the shutdown's _exit)
}
}  // namespace

bool windowOpen(const std::string& bbsName, int scale, std::string& why) {
    // Blocked from here on this thread and every thread SDL makes from it; the AXIS thread unblocks them.
    sigset_t s = stopSignals(), was;
    pthread_sigmask(SIG_BLOCK, &s, &was);
    SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");  // SIGINT and SIGTERM stay startmode.cpp's
    if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) {
        why = std::string("no display for the window (") + SDL_GetError() + ")";
        pthread_sigmask(SIG_SETMASK, &was, nullptr);
        return false;
    }
    if (!probe(why)) {
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        pthread_sigmask(SIG_SETMASK, &was, nullptr);
        return false;
    }
    panel = new lgfx::Panel_sdl;
    auto cfg = panel->config();
    cfg.panel_width = kWidth;   // the window's picture is the board's landscape screen, upright
    cfg.panel_height = kHeight;
    cfg.offset_rotation = 3;    // main.cpp's rotation 1 (the CYD's landscape) is this panel's 0
    cfg.readable = true;
    panel->config(cfg);
    title = "AXIS - " + bbsName;
    panel->setWindowTitle(title.c_str());
    panel->setScaling(scale, scale);
    // Panel_sdl's own keys (1 to 6: the scale, R and L: turn the picture) want Ctrl, so a stray key press does not
    // resize or turn the window.
    lgfx::Panel_sdl::setShortcutKeymod(KMOD_LCTRL);
    tft.setPanel(panel);
    // Here, before the AXIS thread exists: init adds the panel to the list the SDL thread walks. (Its drawing locks
    // see no semaphores until Panel_sdl::main() makes them, which only skips the hand-off to a thread not running.)
    tft.init();
    tft.setRotation(1);
    tft.setColorDepth(16);
    tft.fillScreen(TFT_BLACK);
    std::printf("axis: window: \"%s\", %dx%d at scale %d\n", title.c_str(), kWidth, kHeight, scale);
    return true;
}

void windowRun(void (*fn)(const volatile bool*)) {
    axisMain = fn;
    // SDL's events and the window's drawing on this thread until the window is gone, the AXIS code on a second one.
    // 0: LovyanGFX's debugger-step mode stays off (it would make a slow turn wait on every draw).
    int rc = lgfx::Panel_sdl::main(axisThread, 0);
    std::fflush(nullptr);
    _exit(rc == 0 ? 0 : 1);  // (not reached: the AXIS thread's shutdown exits first)
}

bool windowTouch(int& x, int& y) {
    if (!panel) return false;
    lgfx::touch_point_t tp;
    if (!tft.getTouch(&tp, 1)) return false;
    if (tp.x < 0 || tp.y < 0 || tp.x >= tft.width() || tp.y >= tft.height()) return false;  // dragged off the board
    x = tp.x;
    y = tp.y;
    return true;
}

void windowTestPoll() {
    static bool        first = true, shotDone = false, closeDone = false;
    static const char* shotFile = nullptr;
    static uint32_t    shotAt = 0, closeAt = 0, t0 = 0;
    if (first) {
        first = false;
        t0 = millis();
        shotFile = std::getenv("AXIS_SHOT");
        const char* ms = std::getenv("AXIS_SHOT_MS");
        shotAt = ms ? (uint32_t)std::strtoul(ms, nullptr, 10) : 8000;
        const char* c = std::getenv("AXIS_CLOSE_MS");
        closeAt = c ? (uint32_t)std::strtoul(c, nullptr, 10) : 0;
        shotDone = !shotFile || !*shotFile;
        closeDone = closeAt == 0;
    }
    uint32_t t = millis() - t0;
    if (!shotDone && t >= shotAt) {
        shotDone = true;
        int                  w = tft.width(), h = tft.height();
        std::vector<uint8_t> rgb((size_t)w * h * 3);
        tft.readRectRGB(0, 0, w, h, rgb.data());
        FILE* f = std::fopen(shotFile, "wb");
        if (f) {
            std::fprintf(f, "P6\n%d %d\n255\n", w, h);
            std::fwrite(rgb.data(), 1, rgb.size(), f);
            std::fclose(f);
            std::printf("axis: window: the screen (%dx%d) written to %s\n", w, h, shotFile);
        } else {
            std::printf("axis: window: cannot write %s\n", shotFile);
        }
    }
    if (!closeDone && t >= closeAt) {
        closeDone = true;
        std::printf("axis: window: closing the window (AXIS_CLOSE_MS)\n");
        SDL_Event e{};
        e.type = SDL_QUIT;  // as the window manager's close: Panel_sdl closes every window on it
        SDL_PushEvent(&e);
    }
}
