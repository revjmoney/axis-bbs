// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <set>
#include <cstdlib>
#include <utility>
#include <string>
#include <vector>

#include "bbs/ansiterm.h"
#include "bbs/wfc.h"
#include "bbs/wfcanim.h"
#include "platform/cyd/wfccyd_art.h"
#include "../third_party/ibm_font8x8/font8x8.h"

#include "doctest.h"

using namespace axis::bbs;

namespace {
// a palette of the 16 CGA colours: background at coverage 0, foreground at 4
struct Lut {
    uint8_t v[256][5];
    Lut() {
        for (int a = 0; a < 256; a++)
            for (int l = 0; l <= 4; l++) v[a][l] = (uint8_t)(l ? a & 15 : (a >> 4) & 7);
    }
};
const Lut kLut;

AnsiTerm wfcScreen() {
    AnsiTerm    t(45, 24);
    WfcSnapshot s;
    s.bbsName = "AXIS";
    s.callsToday = 3;
    s.nodes.push_back({1, "", ""});
    s.nodes.push_back({2, "Spindle Kid", "doors"});
    drawWfc(std::string_view(kWfcCydAns, sizeof kWfcCydAns - 1), s, t);
    return t;
}

// the still screen as AnsiScreen::cellRows draws it, for comparison
std::vector<uint8_t> stillFrame(const AnsiTerm& t) {
    std::vector<uint8_t> f(160 * 240, 0);
    for (int y = 0; y < 240; y++)
        for (int X = 0; X < 320; X++) {
            int     gx = X - 2, r = y / 10, ly = y % 10;
            uint8_t v = kLut.v[0][0];
            if (gx >= 0 && gx < 315) {
                const auto& c = t.cell(r, gx / 7);
                int         lx = gx % 7, gy = ly - 1;
                bool        join = c.ch >= 0xB0 && c.ch <= 0xDF;
                if (gy < 0) gy = join ? 0 : -1;
                if (gy > 7) gy = join ? 7 : -1;
                bool on = gy >= 0 && (ibm_font8x8[c.ch * 8 + gy] & (0x80 >> lx));
                v = kLut.v[c.attr][on ? 4 : 0];
            }
            uint8_t& p = f[y * 160 + X / 2];
            p = (X & 1) ? (uint8_t)((p & 0xF0) | v) : (uint8_t)((p & 0x0F) | (v << 4));
        }
    return f;
}

std::vector<uint8_t> frame(const WfcAnim& a, uint32_t now) {
    std::vector<uint8_t> f(160 * 240, 0);
    for (int b = 0; b < 6; b++) a.renderBand(f.data() + b * 160 * 40, b, now, kLut.v);
    return f;
}

void dumpPpm(const std::vector<uint8_t>& f, const std::string& path) {
    static const uint8_t kCga[16][3] = {{0, 0, 0},     {0, 0, 170},    {0, 170, 0},    {0, 170, 170},
                                        {170, 0, 0},   {170, 0, 170},  {170, 85, 0},   {170, 170, 170},
                                        {85, 85, 85},  {85, 85, 255},  {85, 255, 85},  {85, 255, 255},
                                        {255, 85, 85}, {255, 85, 255}, {255, 255, 85}, {255, 255, 255}};
    FILE*                 o = fopen(path.c_str(), "wb");
    if (!o) return;
    fprintf(o, "P6 320 240 255\n");
    for (int y = 0; y < 240; y++)
        for (int X = 0; X < 320; X++) {
            uint8_t b = f[y * 160 + X / 2], v = (X & 1) ? b & 15 : b >> 4;
            fwrite(kCga[v], 1, 3, o);
        }
    fclose(o);
}
}  // namespace

TEST_CASE("the panel WFC animation: the still screen is drawn the same as before") {
    AnsiTerm t = wfcScreen();
    WfcAnim  a;
    REQUIRE(a.load(t));
    CHECK(frame(a, 5000) == stillFrame(t));  // nothing started yet: exactly the still screen
    CHECK(a.parts() >= 4);                   // the logo and the boxes find themselves (tall groups cut in bands)
    CHECK(a.runs() >= 1);                    // and the box lines
    a.unload();
    CHECK_FALSE(a.loaded());
}

TEST_CASE("every transition starts and ends on the still screen, shows in between, and draws the same twice") {
    AnsiTerm                   t = wfcScreen();
    const std::vector<uint8_t> still = stillFrame(t);
    for (int fx = 0; fx < WfcAnim::kFxCount; fx++) {
        CAPTURE(WfcAnim::fxName(fx));
        WfcAnim a;
        REQUIRE(a.load(t));
        int parts = fx == WfcAnim::Drain ? 1 : fx >= WfcAnim::kContentFx ? a.runs() : a.parts();
        for (int p = 0; p < parts; p++) {
            CAPTURE(p);
            REQUIRE(a.force(fx, 1000, p));
            int d = a.act(0).dur;
            a.hold(0);
            CHECK(frame(a, 0) == still);
            a.hold(d);
            CHECK(frame(a, 0) == still);
            bool moved = false;
            for (int tau = d / 8; tau < d && !moved; tau += d / 8) {
                a.hold(tau);
                moved = frame(a, 0) != still;
            }
            CHECK(moved);
            a.hold(d / 2 - 37);
            CHECK(frame(a, 0) == frame(a, 0));
        }
    }
}

TEST_CASE("the animation never draws outside its band") {
    AnsiTerm t = wfcScreen();
    WfcAnim  a;
    REQUIRE(a.load(t));
    std::vector<uint8_t> buf(160 * 40 + 64, 0xA5);
    for (int fx = 0; fx < WfcAnim::kFxCount; fx++) {
        REQUIRE(a.force(fx, 1000));
        for (int tau = 0; tau <= a.act(0).dur; tau += 97) {
            a.hold(tau);
            for (int b = 0; b < 6; b++) {
                a.renderBand(buf.data() + 32, b, 0, kLut.v);
                for (int i = 0; i < 32; i++) {
                    REQUIRE(buf[i] == 0xA5);
                    REQUIRE(buf[160 * 40 + 32 + i] == 0xA5);
                }
            }
        }
    }
}

TEST_CASE("the schedule: one thing always jiggles, parts take turns, the screen rests now and then, every effect comes up") {
    AnsiTerm t = wfcScreen();
    WfcAnim  a;
    REQUIRE(a.load(t));
    std::set<int> seen;
    uint32_t      lastBusy = 0, longestRest = 0;
    int           maxLive = 0;
    bool          stillSeen = false;
    for (uint32_t now = 10; now < 60u * 60u * 1000u; now += 50) {
        a.step(now);
        REQUIRE(a.jiggle().on);
        int live = 0;
        for (int i = 0; i < WfcAnim::kActs; i++)
            if (a.act(i).on) live++, seen.insert(a.act(i).fx);
        maxLive = live > maxLive ? live : maxLive;
        stillSeen = stillSeen || a.still();
        if (a.busyAt(now)) lastBusy = now;
        else if (now - lastBusy > longestRest) longestRest = now - lastBusy;
        CHECK(a.ox() >= -2);
        CHECK(a.ox() <= 3);
    }
    CHECK(stillSeen);
    CHECK(longestRest >= 12000);  // a real rest: still but for the one jiggling thing
    CHECK(maxLive >= 2);          // parts go at different times, sometimes together
    CHECK(seen.size() == (size_t)WfcAnim::kFxCount);
}

TEST_CASE("only the bands that move are drawn again, and once more after they stop") {
    AnsiTerm t = wfcScreen();
    WfcAnim  a;
    REQUIRE(a.load(t));
    CHECK(a.bandsDue(0) == 0);
    REQUIRE(a.force(WfcAnim::Crt, 100, 0));
    uint8_t m = a.bandsDue(200);
    CHECK(m != 0);
    CHECK(m != 0x3F);
    a.calm(300);  // stopped: the same bands once more, so they are put back
    CHECK(a.bandsDue(300) == m);
    CHECK(a.bandsDue(350) == 0);
}

TEST_CASE("the stick figure walks up to a letter and turns it upside down, and it is drawn upside down") {
    AnsiTerm t = wfcScreen();
    WfcAnim  a;
    REQUIRE(a.load(t));
    const std::vector<uint8_t> still = stillFrame(t);
    a.step(10);
    CHECK(a.walker().on);
    a.calm(10);
    REQUIRE(a.walkerDo(WfcAnim::Vanna, 20));
    int cell = a.walker().cell;
    REQUIRE(cell >= 0);
    uint32_t now = 20;
    while (now < 60000 && !a.flipped()) a.step(now += 50);
    REQUIRE(a.flipped() == 1);
    for (int i = 0; i < 12; i++) a.step(now += 50);  // the flip is done
    std::vector<uint8_t> f = frame(a, now);
    auto px = [](const std::vector<uint8_t>& b, int X, int y) { return (X & 1) ? b[y * 160 + X / 2] & 15 : b[y * 160 + X / 2] >> 4; };
    const WfcAnim::Rect& j = a.jiggle().r;
    int cx = (cell % 45) * 7, cy = (cell / 45) * 10;
    bool jiggled = j.x0 < cx + 7 && cx < j.x1 + 3 && j.y0 < cy + 10 && cy < j.y1 + 3;
    if (!jiggled)
        for (int ly = 0; ly < 10; ly++)
            for (int lx = 0; lx < 7; lx++) CHECK(px(f, cx + lx + 2, cy + ly) == px(still, cx + lx + 2, cy + 9 - ly));
    CHECK(f != still);  // the figure is on the screen too
    while (now < 200000 && a.flipped()) a.step(now += 50);
    CHECK(a.flipped() == 0);  // they all turn back sooner or later
}

TEST_CASE("the drain is somewhere new every time") {
    AnsiTerm t = wfcScreen();
    WfcAnim  a;
    REQUIRE(a.load(t));
    std::set<std::pair<int, int>> spots;
    int lastX = -1000, lastY = -1000;
    for (int i = 0; i < 20; i++) {
        REQUIRE(a.force(WfcAnim::Drain, 1000u + (uint32_t)i));
        int x, y;
        WfcAnim::drainSpot(a.act(0).seed, x, y);
        CHECK(std::abs(x - lastX) + std::abs(y - lastY) >= 80);
        CHECK(x >= 16);
        CHECK(x < WfcAnim::kGridW - 16);
        CHECK(y >= 16);
        CHECK(y < WfcAnim::kGridH - 16);
        spots.insert({x, y});
        lastX = x, lastY = y;
    }
    CHECK(spots.size() == 20);
}

TEST_CASE("effects have names for the bench command") {
    for (int fx = 0; fx < WfcAnim::kFxCount; fx++) CHECK(WfcAnim::fxByName(WfcAnim::fxName(fx)) == fx);
    CHECK(WfcAnim::fxByName("nope") == -1);
}

// AXIS_WFCANIM_DUMP=<folder>: frames of every effect as PPM (tools make GIFs of them), for looking at on the PC
TEST_CASE("frames for looking at (only with AXIS_WFCANIM_DUMP)") {
    const char* dir = getenv("AXIS_WFCANIM_DUMP");
    if (!dir) return;
    AnsiTerm t = wfcScreen();
    WfcAnim  a;
    REQUIRE(a.load(t));
    for (int fx = 0; fx < WfcAnim::kFxCount; fx++) {
        REQUIRE(a.force(fx, 1000));
        int d = a.act(0).dur;
        for (int tau = 0, i = 0; tau <= d; tau += 80, i++) {
            a.hold(tau);
            char name[64];
            snprintf(name, sizeof name, "/%s_%03d.ppm", WfcAnim::fxName(fx), i);
            dumpPpm(frame(a, 0), std::string(dir) + name);
        }
    }
    a.hold(-1);
    a.calm(0);
    for (uint32_t now = 0, i = 0; now < 120000; now += 80, i++) {  // two minutes of the real schedule
        a.step(now + 40000);
        char name[64];
        snprintf(name, sizeof name, "/show_%04u.ppm", (unsigned)i);
        dumpPpm(frame(a, now + 40000), std::string(dir) + name);
    }
}
