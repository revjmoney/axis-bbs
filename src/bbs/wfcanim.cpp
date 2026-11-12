// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The panel WFC's anti burn-in show (wfcanim.h). The transitions are the LFD3K-SM idle show's (firmware/src/idle_show.c,
// Rev's own, GPLv3) moved from a 1-bit OLED word to coloured 7x10 text cells: each one takes its part away and brings it
// back as it was, so at its start and its end the part is exactly the still screen.
#ifdef ARDUINO
#pragma GCC optimize("Os")  // size first (frames are measured with @WFCFX: frame_us)
#endif
#include "bbs/wfcanim.h"

#include <string.h>

#include <new>

#include "bbs/ansiterm.h"
#include "../../third_party/ibm_font8x8/font8x8.h"  // IBM PC BIOS 8x8 by VileR, CC BY-SA 4.0 (the WFC's own font)

namespace axis::bbs {
namespace {

const int8_t kSin[64] = {0,    12,   25,   37,   49,   60,   71,   81,   90,   98,   106,  112,  117,  122,  125,  126,
                         127,  126,  125,  122,  117,  112,  106,  98,   90,   81,   71,   60,   49,   37,   25,   12,
                         0,    -12,  -25,  -37,  -49,  -60,  -71,  -81,  -90,  -98,  -106, -112, -117, -122, -125, -126,
                         -127, -126, -125, -122, -117, -112, -106, -98,  -90,  -81,  -71,  -60,  -49,  -37,  -25,  -12};

const char* const kFxNames[WfcAnim::kFxCount] = {"dissolve", "explode", "blocks", "sine",  "flap",   "type",     "drip", "glitch",
                                                 "rain",     "warp",    "crt",    "blinds", "pixelate", "melt", "bots"};
const char* const kWigNames[WfcAnim::kWigCount] = {"wiggle", "jostle", "shimmy", "bob", "tilt"};
const uint16_t    kDur[WfcAnim::kFxCount] = {2400, 4200, 2800, 3200, 2600, 3600, 3000, 1800, 3000, 3000, 2200, 2000, 2200, 5000, 6000};

// a little bot, 7x10 (bit 6 is the left column): the body, its eyes, and two leg poses
const uint8_t kBot[10] = {0x08, 0x3E, 0x2A, 0x3E, 0x1C, 0x7F, 0x3E, 0x3E, 0x22, 0x41};
const uint8_t kBotEyes[10] = {0, 0, 0x14, 0, 0, 0, 0, 0, 0, 0};
const uint8_t kBotStep[2] = {0x14, 0x22};  // rows 8 and 9 in the other pose

uint32_t mix(uint32_t v) {
    v ^= v >> 16;
    v *= 0x7FEB352Du;
    v ^= v >> 15;
    v *= 0x846CA68Bu;
    v ^= v >> 16;
    return v;
}
int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
int mini(int a, int b) { return a < b ? a : b; }
int maxi(int a, int b) { return a > b ? a : b; }
int smooth(int u) {  // smoothstep, 0..1024
    u = clampi(u, 0, 1024);
    return u * u / 1024 * (3072 - 2 * u) / 1024;
}
int tri(int t, int d) {  // 0 at both ends, 1024 in the middle
    int h = d / 2;
    if (h <= 0) return 0;
    return clampi(t < h ? t * 1024 / h : (d - t) * 1024 / h, 0, 1024);
}
int bounce(int startQ, int speedQ, int dt, int lo, int hi) {  // to and fro between lo and hi (positions in 1/256 px)
    int loQ = lo * 256, span = (hi - lo) * 256;
    if (span <= 0) return lo;
    int64_t raw = (int64_t)startQ - loQ + (int64_t)speedQ * dt, per = 2LL * span, p = raw % per;
    if (p < 0) p += per;
    if (p > span) p = per - p;
    return (int)((loQ + p) / 256);
}
int speedOf(uint32_t h) {
    int s = 24 + (int)(h % 40u);
    return (h & 0x100u) ? s : -s;
}
// the WFC's font in a 7x10 cell: font bits 7..1 across, a blank row above and below (box drawing and blocks repeat their
// edge rows instead, so lines join between cells): the same as AnsiScreen's still drawing
bool glyph(uint8_t ch, int lx, int ly) {
    if (lx < 0 || lx > 6 || ly < 0 || ly > 9) return false;
    int gy = ly - 1;
    bool join = ch >= 0xB0 && ch <= 0xDF;
    if (gy < 0) {
        if (!join) return false;
        gy = 0;
    }
    if (gy > 7) {
        if (!join) return false;
        gy = 7;
    }
    return (ibm_font8x8[ch * 8 + gy] & (0x80 >> lx)) != 0;
}
int botPx(int x, int y, int pose) {  // 0 nothing, 1 body, 2 eye
    if (x < 0 || x > 6 || y < 0 || y > 9) return 0;
    uint8_t row = pose && y >= 8 ? kBotStep[y - 8] : kBot[y];
    if (kBotEyes[y] & (0x40 >> x)) return 2;
    return (row & (0x40 >> x)) ? 1 : 0;
}
WfcAnim::Rect cellRect(int r0, int c0, int r1, int c1) {
    WfcAnim::Rect r;
    r.x0 = (int16_t)(c0 * WfcAnim::kCw), r.y0 = (int16_t)(r0 * WfcAnim::kCh);
    r.x1 = (int16_t)(c1 * WfcAnim::kCw), r.y1 = (int16_t)(r1 * WfcAnim::kCh);
    return r;
}
WfcAnim::Rect grow(WfcAnim::Rect r, int dx, int up, int down) {
    r.x0 = (int16_t)maxi(0, r.x0 - dx), r.x1 = (int16_t)mini(WfcAnim::kGridW, r.x1 + dx);
    r.y0 = (int16_t)maxi(0, r.y0 - up), r.y1 = (int16_t)mini(WfcAnim::kGridH, r.y1 + down);
    return r;
}
bool overlap(const WfcAnim::Rect& a, const WfcAnim::Rect& b) { return a.x0 < b.x1 && b.x0 < a.x1 && a.y0 < b.y1 && b.y0 < a.y1; }
uint8_t bandsOf(const WfcAnim::Rect& r) {
    if (r.empty()) return 0;
    uint8_t m = 0;
    for (int b = maxi(0, r.y0 / WfcAnim::kBandH); b < WfcAnim::kBands && b * WfcAnim::kBandH < r.y1; b++) m |= (uint8_t)(1u << b);
    return m;
}
}  // namespace

const char* WfcAnim::fxName(int fx) { return fx >= 0 && fx < kFxCount ? kFxNames[fx] : "?"; }
const char* WfcAnim::wigName(int w) { return w >= 0 && w < kWigCount ? kWigNames[w] : "?"; }
int         WfcAnim::fxByName(const char* name) {
    for (int i = 0; i < kFxCount; i++)
        if (!strcmp(name, kFxNames[i])) return i;
    return -1;
}

WfcAnim::Kind WfcAnim::kind(int r, int c) const {
    if (r < 0 || r >= kRows || c < 0 || c >= kCols) return Blank;
    const Cell& x = cells_[r * kCols + c];
    if (x.ch >= 0xB3 && x.ch <= 0xDA) return Line;
    if ((x.ch == ' ' || x.ch == 0 || x.ch == 0xFF) && ((x.attr >> 4) & 7) == 0) return Blank;
    return Art;
}

uint32_t WfcAnim::rnd() {
    rng_ = mix(rng_ + 0x9E3779B9u);
    return rng_;
}

// ---- the screen and its parts

bool WfcAnim::load(const AnsiTerm& t) {
    if (!cells_) cells_.reset(new (std::nothrow) Cell[kCols * kRows]);
    if (!cells_) return false;
    for (int r = 0; r < kRows; r++)
        for (int c = 0; c < kCols; c++) {
            Cell x{' ', 0x07};
            if (r < t.rows() && c < t.cols()) x = {t.cell(r, c).ch, t.cell(r, c).attr};
            cells_[r * kCols + c] = x;
        }
    // parts: groups of text and art (each cell joined to the ones around it, gaps of up to two blanks in a row bridged),
    // the biggest kParts of them; borders are not parts
    nParts_ = 0;
    int area[kParts] = {};
    std::unique_ptr<uint16_t[]> work(new (std::nothrow) uint16_t[kCols * kRows]);  // briefly: a flood fill's queue
    std::unique_ptr<uint8_t[]>  seen(new (std::nothrow) uint8_t[kCols * kRows]);
    if (work && seen) {
        memset(seen.get(), 0, kCols * kRows);
        for (int i = 0; i < kCols * kRows; i++) {
            if (seen[i] || kind(i / kCols, i % kCols) != Art) continue;
            int head = 0, tail = 0, n = 0, r0 = kRows, c0 = kCols, r1 = -1, c1 = -1;
            work[tail++] = (uint16_t)i, seen[i] = 1;
            while (head < tail) {
                int j = work[head++], r = j / kCols, c = j % kCols;
                n++;
                r0 = mini(r0, r), r1 = maxi(r1, r), c0 = mini(c0, c), c1 = maxi(c1, c);
                for (int dr = -1; dr <= 1; dr++)
                    for (int dc = -3; dc <= 3; dc++) {
                        int rr = r + dr, cc = c + dc;
                        if ((!dr && !dc) || (dr && (dc < -1 || dc > 1)) || rr < 0 || rr >= kRows || cc < 0 || cc >= kCols) continue;
                        int k = rr * kCols + cc;
                        if (seen[k] || kind(rr, cc) != Art) continue;
                        seen[k] = 1;
                        work[tail++] = (uint16_t)k;
                    }
            }
            if (n < 3 || c1 - c0 < 1) continue;
            int slot = nParts_ < kParts ? nParts_++ : -1;
            if (slot < 0) {  // full: the smallest goes, if this one is bigger
                slot = 0;
                for (int k = 1; k < kParts; k++)
                    if (area[k] < area[slot]) slot = k;
                if (area[slot] >= n) continue;
            }
            parts_[slot] = cellRect(r0, c0, r1 + 1, c1 + 1);
            area[slot] = n;
        }
    }
    // border runs: three or more line cells in a row or a column, the longest kRuns
    nRuns_ = 0;
    int len[kRuns] = {};
    auto addRun = [&](int r0, int c0, int r1, int c1, int n) {
        int slot = nRuns_ < kRuns ? nRuns_++ : -1;
        if (slot < 0) {
            slot = 0;
            for (int k = 1; k < kRuns; k++)
                if (len[k] < len[slot]) slot = k;
            if (len[slot] >= n) return;
        }
        runs_[slot] = cellRect(r0, c0, r1, c1);
        len[slot] = n;
    };
    for (int r = 0; r < kRows; r++)
        for (int c = 0; c < kCols;) {
            int e = c;
            while (e < kCols && kind(r, e) == Line) e++;
            if (e - c >= 3) addRun(r, c, r + 1, e, e - c);
            c = e > c ? e : c + 1;
        }
    for (int c = 0; c < kCols; c++)
        for (int r = 0; r < kRows;) {
            int e = r;
            while (e < kRows && kind(e, c) == Line) e++;
            if (e - r >= 3) addRun(r, c, e, c + 1, e - r);
            r = e > r ? e : r + 1;
        }
    // what was moving may sit on a part that changed: transitions carry on (they only read cells), a jiggle on a blank is left
    return true;
}

void WfcAnim::unload() {
    cells_.reset();
    for (auto& a : acts_) a.on = false;
    jig_.on = false;
    begun_ = held_ = false;
    forced_ = -1;
    lastMask_ = 0;
    drawnOx_ = ox_;
}

// ---- the schedule

bool WfcAnim::clash(const Rect& r, uint32_t now) const {
    for (auto& a : acts_)
        if (a.on && overlap(grow(area(a), 3, 3, 3), r)) return true;
    (void)now;
    return false;
}

WfcAnim::Rect WfcAnim::area(const Act& a) const {
    if (a.fx == Explode) return grow(a.r, 4 * kCw, 3 * kCh, 3 * kCh);
    if (a.fx == Melt) return grow(a.r, 0, 0, 4 * kCh);
    return a.r;
}

WfcAnim::Rect WfcAnim::wigArea() const { return grow(jig_.r, 3, 3, 3); }

void WfcAnim::start(int fx, const Rect& r, uint32_t when) {
    for (auto& a : acts_) {
        if (a.on) continue;
        a.fx = (uint8_t)fx, a.r = r, a.start = when, a.dur = kDur[fx], a.seed = rnd(), a.on = true;
        return;
    }
}

void WfcAnim::startSome(uint32_t now) {
    int live = 0;
    for (auto& a : acts_) live += a.on;
    nextStart_ = now + 1200 + rnd() % 4800;
    if (live >= 2) {
        nextStart_ = now + 700;
        return;
    }
    auto content = [this] {  // the next content effect: each one once before any comes round again
        if ((bag_ & ((1u << kContentFx) - 1)) == ((1u << kContentFx) - 1)) bag_ = 0;
        int f = (int)(rnd() % kContentFx);
        while (bag_ & (1u << f)) f = (f + 1) % kContentFx;
        bag_ |= (uint16_t)(1u << f);
        return f;
    };
    uint32_t pick = rnd() % 10;
    if (pick == 0 && live == 0 && nParts_ >= 2) {  // a cascade: every part goes, each its own way, one after another
        int n = mini(nParts_, kActs);
        for (int i = 0; i < n; i++) start(content(), parts_[i], now + (uint32_t)i * 650u);
        nextStart_ = now + (uint32_t)n * 650u + 7000u;
        return;
    }
    for (int tries = 0; tries < 6; tries++) {
        if (pick < 4 && nRuns_) {  // a border line melts away, or turns into bots
            const Rect& r = runs_[rnd() % nRuns_];
            if (clash(r, now)) continue;
            start((rnd() & 1) ? Melt : Bots, r, now);
            return;
        }
        if (!nParts_) return;
        Rect r = parts_[rnd() % nParts_];
        int  rows = (r.y1 - r.y0) / kCh;
        if (rows > 1 && rnd() % 10 < 6) {  // often only a line or three of it
            int n = 1 + (int)(rnd() % (uint32_t)mini(3, rows)), top = (int)(rnd() % (uint32_t)(rows - n + 1));
            r.y0 = (int16_t)(r.y0 + top * kCh), r.y1 = (int16_t)(r.y0 + n * kCh);
        }
        if (clash(r, now)) continue;
        start(content(), r, now);
        return;
    }
}

void WfcAnim::pickJiggle(uint32_t now) {
    jig_.on = false;
    for (int tries = 0; tries < 10; tries++) {
        Rect     t;
        uint32_t c = rnd() % 100;
        if (c < 20 && nParts_) {
            t = parts_[rnd() % nParts_];
        } else if (c < 40 && nRuns_) {
            t = runs_[rnd() % nRuns_];
        } else {  // a word, or one character on its own
            int i = (int)(rnd() % (kCols * kRows)), r = i / kCols, c0 = i % kCols;
            if (kind(r, c0) != Art) continue;
            int c1 = c0 + 1;
            if (c < 85) {
                while (c0 > 0 && kind(r, c0 - 1) == Art) c0--;
                while (c1 < kCols && kind(r, c1) == Art) c1++;
            }
            t = cellRect(r, c0, r + 1, c1);
        }
        if (t.empty() || clash(t, now)) continue;
        jig_.fx = (uint8_t)(rnd() % kWigCount), jig_.r = t, jig_.start = now, jig_.dur = (uint16_t)(2500 + rnd() % 4500);
        jig_.seed = rnd(), jig_.on = true;
        return;
    }
    // everything big is busy (a cascade): one character anywhere still jiggles (a transition over it wins where they meet)
    for (int tries = 0; tries < 200; tries++) {
        int i = (int)(rnd() % (kCols * kRows));
        if (kind(i / kCols, i % kCols) == Blank) continue;
        jig_.fx = (uint8_t)(rnd() % kWigCount), jig_.r = cellRect(i / kCols, i % kCols, i / kCols + 1, i % kCols + 1);
        jig_.start = now, jig_.dur = (uint16_t)(1500 + rnd() % 2500), jig_.seed = rnd(), jig_.on = true;
        return;
    }
}

void WfcAnim::step(uint32_t now) {
    if (!cells_ || held_) return;
    if (!begun_) {
        begun_ = true;
        rng_ ^= mix(now);
        nextStart_ = now + 3000;
        still_ = false;
        spellEnd_ = now + 30000 + rnd() % 30000;
        orbitAt_ = now + 60000;
        pickJiggle(now);
    }
    for (auto& a : acts_)
        if (a.on && (int32_t)(now - a.start) >= (int32_t)a.dur) a.on = false;
    if (!jig_.on || (int32_t)(now - jig_.start) >= (int32_t)jig_.dur) pickJiggle(now);
    if ((int32_t)(now - orbitAt_) >= 0) {  // the whole screen drifts a pixel or two now and then
        static const int8_t kOrbit[10] = {0, 1, 2, 3, 2, 1, 0, -1, -2, -1};
        orbitStep_ = (uint8_t)((orbitStep_ + 1) % 10);
        ox_ = kOrbit[orbitStep_];
        orbitAt_ = now + 60000 + rnd() % 30000;
    }
    if ((int32_t)(now - spellEnd_) >= 0) {  // busy for a while, now and then a rest (the jiggle goes on)
        if (still_) still_ = false;
        else still_ = rnd() % 10 < 7;
        spellEnd_ = now + (still_ ? 12000 + rnd() % 25000 : 25000 + rnd() % 35000);
        nextStart_ = now + 1500;
    }
    if (!still_ && (int32_t)(now - nextStart_) >= 0) startSome(now);
}

bool WfcAnim::busyAt(uint32_t now) const {
    now = at(now);
    for (auto& a : acts_)
        if (started(a, now)) return true;
    return false;
}

uint8_t WfcAnim::bandsDue(uint32_t now) {
    if (!cells_) return 0;
    now = at(now);
    uint8_t m = 0;
    for (auto& a : acts_)
        if (started(a, now)) m |= bandsOf(area(a));
    if (jig_.on) m |= bandsOf(wigArea());
    uint8_t due = (uint8_t)(m | lastMask_);  // and what moved last time, so it is put back
    lastMask_ = m;
    if (ox_ != drawnOx_) due = (1u << kBands) - 1, drawnOx_ = ox_;
    return due;
}

bool WfcAnim::force(int fx, uint32_t now, int part) {
    if (!cells_ || fx < 0 || fx >= kFxCount) return false;
    for (auto& a : acts_) a.on = false;
    Rect r;
    if (fx >= kContentFx) {
        if (!nRuns_) return false;
        r = runs_[part >= 0 && part < nRuns_ ? part : (int)(rnd() % nRuns_)];
    } else if (nParts_) {
        r = parts_[part >= 0 && part < nParts_ ? part : (int)(rnd() % nParts_)];
    } else {
        r = cellRect(0, 0, kRows, kCols);
    }
    start(fx, r, now);
    forced_ = 0;
    begun_ = true;
    still_ = false;
    nextStart_ = now + kDur[fx] + 2000u;
    if (jig_.on && overlap(wigArea(), area(acts_[0]))) jig_.on = false;
    return true;
}

void WfcAnim::hold(int32_t tau) {
    if (tau < 0 || forced_ < 0) {
        held_ = false;
        return;
    }
    held_ = true;
    heldNow_ = acts_[forced_].start + (uint32_t)tau;
}

void WfcAnim::calm(uint32_t now) {
    for (auto& a : acts_) a.on = false;
    held_ = false;
    still_ = true;
    spellEnd_ = now + 30000;
}

// ---- drawing

bool WfcAnim::lit(int gx, int gy) const {
    if (gx < 0 || gx >= kGridW || gy < 0 || gy >= kGridH) return false;
    const Cell& c = cells_[(gy / kCh) * kCols + gx / kCw];
    return glyph(c.ch, gx % kCw, gy % kCh);
}

uint8_t WfcAnim::base(int gx, int gy, const uint8_t (*lut)[5]) const {
    if (gx < 0 || gx >= kGridW || gy < 0 || gy >= kGridH) return lut[0][0];
    const Cell& c = cells_[(gy / kCh) * kCols + gx / kCw];
    return lut[c.attr][glyph(c.ch, gx % kCw, gy % kCh) ? 4 : 0];
}

uint8_t WfcAnim::bgAt(int gx, int gy, const uint8_t (*lut)[5]) const {
    if (gx < 0 || gx >= kGridW || gy < 0 || gy >= kGridH) return lut[0][0];
    return lut[cells_[(gy / kCh) * kCols + gx / kCw].attr][0];
}

uint8_t WfcAnim::fxPixel(const Act& a, int gx, int gy, int t, const uint8_t (*lut)[5]) const {
    const Rect& r = a.r;
    const int   D = a.dur, lx = gx - r.x0, ly = gy - r.y0, w = r.x1 - r.x0, h = r.y1 - r.y0;
    const int   cr = gy / kCh, cc = gx / kCw, cx = gx % kCw, cy = gy % kCh;
    const bool  inside = r.has(gx, gy);
    const Cell* cell = &cells_[cr * kCols + cc];
    switch (a.fx) {
        case Dissolve: {  // the pixels go in a random order, the last few glowing as they lift away, then come back
            int      p = tri(t, D);
            uint32_t hh = mix((uint32_t)gx * 0x9E3779B1u ^ (uint32_t)gy * 0x85EBCA77u ^ a.seed) & 1023u;
            if ((int)hh >= p) return base(gx, gy, lut);
            int d = p - (int)hh;
            if (d < 64 && r.has(gx, gy + d / 8) && lit(gx, gy + d / 8)) return lut[0x0E][4];
            return bgAt(gx, gy, lut);
        }
        case Blocks: {  // cells flash inverted and go dark in a random order, then flash back on in another
            if (kind(cr, cc) == Blank) return base(gx, gy, lut);
            uint32_t hh = mix((uint32_t)(cr * kCols + cc) * 0x9E3779B9u + a.seed), h2 = mix(hh ^ 0x51u);
            int      half = D / 2, off = 100 + (int)(hh % (uint32_t)(half - 220)), on = half + (int)(h2 % (uint32_t)(half - 200));
            if (t < off - 80 || t >= on + 80) return base(gx, gy, lut);
            if (t < off || t >= on) return lut[cell->attr][glyph(cell->ch, cx, cy) ? 0 : 4];
            return lut[cell->attr][0];
        }
        case Sine: {  // it scrolls round once on a moving sine, flattening as it settles
            int e = smooth(t * 1024 / D), s = w * e / 1024;
            int env = mini(1024, mini(t * 1024 / 300, (D - t) * 1024 / 300));
            int wob = kSin[(ly * 3 + t / 20) & 63] * 3 * env / (127 * 1024);
            int dy = kSin[(lx * 2 + t / 15) & 63] * 2 * env / (127 * 1024);
            int sx = (lx + s + wob) % w, sy = ly + dy;
            if (sx < 0) sx += w;
            if (sy < 0 || sy >= h) return bgAt(gx, gy, lut);
            return base(r.x0 + sx, r.y0 + sy, lut);
        }
        case Flap: {  // a split-flap board: each cell folds through a few letters and lands on itself again
            if (kind(cr, cc) == Blank) return base(gx, gy, lut);
            static const char kFaces[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
            const int F = 150;
            uint32_t  hh = mix((uint32_t)(cr * kCols + cc) * 0x2C1B3C6Du + a.seed);
            int       nflip = 2 + (int)(hh % 3u), wc = maxi(1, w / kCw);
            int       start = (cc - r.x0 / kCw) * (D * 45 / 100) / wc + (int)((hh >> 8) % 120u);
            int       j = t < start ? -1 : (t - start) / F;
            if (j < 0 || j >= nflip) return base(gx, gy, lut);
            auto face = [&](int k) { return k <= 0 || k >= nflip ? cell->ch : (uint8_t)kFaces[mix(hh + (uint32_t)k * 0x632BE5ABu) % 36u]; };
            uint8_t cur = face(j), nxt = face(j + 1);
            int     f = (t - start) % F * 1024 / F, bit;
            const int MID = 5, H = 10;
            if (f < 512) {  // the old top half folding down
                int k = 1024 - 2 * f;
                if (cy < MID) {
                    int src = MID - (MID - cy) * 1024 / maxi(k, 1);
                    bit = src >= 0 ? glyph(cur, cx, src) : glyph(nxt, cx, cy);
                } else {
                    bit = glyph(cur, cx, cy);
                }
            } else {  // the new bottom half unfolding
                int k = 2 * f - 1024;
                if (cy < MID) {
                    bit = glyph(nxt, cx, cy);
                } else {
                    int src = k > 0 ? MID + (cy - MID) * 1024 / k : H;
                    bit = src < H ? glyph(nxt, cx, src) : glyph(cur, cx, cy);
                }
            }
            return lut[cell->attr][bit ? 4 : 0];
        }
        case Type: {  // a block cursor eats it from the end, then types it back
            int wc = maxi(1, w / kCw), n = wc * maxi(1, h / kCh), ci = (cr - r.y0 / kCh) * wc + (cc - r.x0 / kCw);
            int tOut = D * 35 / 100, tIn0 = D / 2, tIn = D * 42 / 100, cur;
            bool vis;
            if (t < tIn0) {
                int k = mini(t, tOut) * (n + 1) / tOut;
                vis = ci < n - k;
                cur = (t < tOut || ((t - tOut) / 100) % 2 == 0) ? n - k : -1;
            } else {
                int k = mini(n, mini(t - tIn0, tIn) * (n + 1) / tIn);
                vis = ci < k;
                cur = k < n ? k : -1;
            }
            if (ci == cur && cy >= 1 && cy <= 8) return lut[0x07][4];
            return vis ? base(gx, gy, lut) : lut[cell->attr][0];
        }
        case Drip: {  // pairs of pixel columns slide down faster and faster and out; then they drop back in from the top
            uint32_t hc = mix((uint32_t)(lx / 2) * 0x2545F491u + a.seed);
            int      half = D / 2;
            if (t < half) {
                int st = (int)(hc % (uint32_t)(half / 3)), g = 40 + (int)((hc >> 8) % 40u), dt = maxi(0, t - st);
                int drop = dt * dt / 1000 * g / 1000, sy = gy - drop;
                return sy < r.y0 ? bgAt(gx, gy, lut) : base(gx, sy, lut);
            }
            int st = (int)((hc >> 12) % (uint32_t)(half / 3)), len = maxi(1, half - half / 3 - 100);
            int o = h * (1024 - smooth((t - half - st) * 1024 / len)) / 1024, sy = gy + o;
            return sy >= r.y1 ? bgAt(gx, gy, lut) : base(gx, sy, lut);
        }
        case Glitch: {  // slices torn sideways, some inverted or tinted, most at the middle
            if (t < D / 20 || t >= D - D / 20) return base(gx, gy, lut);
            uint32_t hf = mix((uint32_t)(t / 70) * 0x9E3779B9u + a.seed);
            int      sh = 2 + (int)(hf % 4u);
            uint32_t hh = mix(hf + (uint32_t)(ly / sh) * 0x85EBCA6Bu);
            if ((int)(hh % 1024u) >= tri(t, D)) return base(gx, gy, lut);
            int sx = (lx - ((int)((hh >> 12) % 25u) - 12)) % w;
            if (sx < 0) sx += w;
            sx += r.x0;
            const Cell& c = cells_[cr * kCols + sx / kCw];
            bool        on = glyph(c.ch, sx % kCw, cy) != ((hh >> 24) % 5u == 0);
            if ((hh >> 28) & 1) return on ? lut[(hh >> 29) & 1 ? 0x0D : 0x0B][4] : lut[c.attr][0];
            return lut[c.attr][on ? 4 : 0];
        }
        case Rain: {  // green streams fall down each column eating it, then fall again and leave it behind
            int      ri = cr - r.y0 / kCh, half = D / 2;
            uint32_t hl = mix((uint32_t)cc * 0x7FEB352Du + a.seed);
            bool     back = t >= half;
            int      vmin = (h / kCh + 4) * 1000 / maxi(1, half - half / 3 - 50);  // fast enough to clear the part in time
            int      tt = back ? t - half : t, st = (int)((hl >> 4) % (uint32_t)(half / 3)), v = vmin + (int)((hl >> 8) % (uint32_t)(vmin / 2 + 1));
            int      head = tt < st ? -100 : (tt - st) * v / 1000, d = head - ri;
            if (d >= 0 && d < 3) {
                uint8_t ch = (uint8_t)(0x21 + mix((uint32_t)(cc * 31 + cr * 17) + (uint32_t)(t / 90) * 0x9E37u) % 94u);
                return glyph(ch, cx, cy) ? lut[d == 0 ? 0x0F : d == 1 ? 0x0A : 0x02][4] : lut[cell->attr][0];
            }
            if (back) return d >= 3 ? base(gx, gy, lut) : lut[cell->attr][0];
            return d >= 3 ? lut[cell->attr][0] : base(gx, gy, lut);
        }
        case Explode:
        case Warp:  // the cells fly (sprites() draws them): where they were is empty
            return inside ? bgAt(gx, gy, lut) : base(gx, gy, lut);
        case Crt: {  // switched off like an old set: squashed to a bright line, the line to a dot, and back on
            int p = tri(t, D), my = (r.y0 + r.y1) / 2, mx = (r.x0 + r.x1) / 2;
            const uint8_t white = lut[0x0F][4];
            if (p < 650) {
                int v = maxi(40, 1024 - smooth(p * 1024 / 650));
                if (v < 160) return (gy == my || gy == my - 1) ? white : bgAt(gx, gy, lut);
                int sy = my + (gy - my) * 1024 / v;
                if (!r.has(gx, sy)) return bgAt(gx, gy, lut);
                const Cell& c = cells_[(sy / kCh) * kCols + cc];
                bool        on = glyph(c.ch, cx, sy % kCh);
                return on ? (v < 400 ? white : lut[c.attr][4]) : lut[c.attr][0];
            }
            int lw = w * (1024 - smooth((p - 650) * 1024 / 374)) / 2048;
            return (gy == my && gx >= mx - lw - 1 && gx <= mx + lw) ? white : bgAt(gx, gy, lut);
        }
        case Blinds: {  // venetian blinds: each row of cells closes slat by slat, then opens again
            int k = clampi(tri(t, D) * 12 / 1024 - ((ly / kCh) & 1), 0, 10);
            if (cy >= k) return base(gx, gy, lut);
            return cy == k - 1 ? lut[0x08][4] : lut[cell->attr][0];
        }
        case Pixelate: {  // coarser and coarser blocks, then sharp again
            int b = 1 + tri(t, D) * 6 / 1024;
            if (b <= 1) return base(gx, gy, lut);
            int sx = mini(w - 1, lx - lx % b + b / 2), sy = mini(h - 1, ly - ly % b + b / 2);
            return base(r.x0 + sx, r.y0 + sy, lut);
        }
        case Melt: {  // a border line's pixels drip down and fade away, then the line grows back along itself
            bool horiz = h == kCh;
            auto line = [&](int x, int y) { return r.has(x, y) && kind(y / kCh, x / kCw) == Line; };
            int  split = D * 55 / 100;
            if (t < split) {
                uint32_t hc = mix((uint32_t)(gx / 2) * 0x2545F491u + a.seed);
                int      st = (int)(hc % (uint32_t)(split / 3)), g = 30 + (int)((hc >> 8) % 40u), dt = maxi(0, t - st);
                int      drop = dt * dt / 1000 * g / 1000, sy = gy - drop;
                if (line(gx, sy) && lit(gx, sy)) {
                    uint8_t at = cells_[(sy / kCh) * kCols + cc].attr;
                    if (t > split * 6 / 10) at = (uint8_t)((at & 8) ? at & 0xF7 : (at & 0xF0) | 0x08);  // dimmer as it goes
                    return lut[at][4];
                }
                return line(gx, gy) ? bgAt(gx, gy, lut) : base(gx, gy, lut);
            }
            int e = smooth((t - split) * 1024 / maxi(1, D - split - 150));
            int reach = (horiz ? w : h) * e / 1024;
            if (line(gx, gy) && (horiz ? lx : ly) >= reach) return bgAt(gx, gy, lut);
            return base(gx, gy, lut);
        }
        case Bots: {  // a border line's cells turn into little bots that rock and hop, then back into the line
            if (kind(cr, cc) != Line) return base(gx, gy, lut);
            bool     horiz = h == kCh;
            int      n = horiz ? w / kCw : h / kCh, k = horiz ? cc - r.x0 / kCw : cr - r.y0 / kCh;
            uint32_t hb = mix((uint32_t)(cr * kCols + cc) * 0x632BE5ABu + a.seed);
            int      on = 60 + k * (D * 25 / 100) / maxi(1, n) + (int)(hb % 200u);
            int      off = D * 72 / 100 + k * (D * 18 / 100) / maxi(1, n) + (int)((hb >> 8) % 100u);
            if (t < on - 60 || t >= off + 60) return base(gx, gy, lut);
            if (t < on || t >= off) return lut[cell->attr][glyph(cell->ch, cx, cy) ? 0 : 4];  // a flash as it turns
            int ph = t + (int)(hb & 1023u);
            int sn = kSin[(ph / 24 + (int)(hb >> 16)) & 63] * 266 / 127, cs = 1024 - sn * sn / 2048;  // up to about 15 degrees
            int hop = (kSin[(ph / 16) & 63] > 100) ? 1 : 0;
            int dx = cx - 3, dy = cy - 5 + hop;
            int bx = ((dx * cs + dy * sn + 512) >> 10) + 3, by = ((-dx * sn + dy * cs + 512) >> 10) + 5;
            int v = botPx(bx, by, (ph / 220) & 1);
            if (v == 2 && (ph / 150) % 13 == 0) v = 1;  // a blink
            return v == 0 ? lut[cell->attr][0] : v == 1 ? lut[cell->attr][4] : lut[0x0F][4];
        }
    }
    return base(gx, gy, lut);
}

uint8_t WfcAnim::wigPixel(int gx, int gy, uint32_t now, const uint8_t (*lut)[5]) const {
    const Rect& T = jig_.r;
    int         t = (int)(now - jig_.start), D = jig_.dur;
    int         env = clampi(mini(t * 1024 / 300, (D - t) * 1024 / 300), 0, 1024), dx = 0, dy = 0;
    switch (jig_.fx) {
        case Wiggle: dx = kSin[(t * 64 / 420) & 63] * 2 * env / (1024 * 100); break;
        case Jostle:
            if (env > 512) {
                uint32_t hh = mix((uint32_t)(t / 110) + jig_.seed);
                dx = (int)(hh % 3u) - 1, dy = (int)((hh >> 4) % 3u) - 1;
            }
            break;
        case Shimmy:
            if (env > 512) dx = (((gy - T.y0) / 2 + t / 80) & 1) ? 1 : -1;
            break;
        case Bob: dy = kSin[(t * 64 / 640) & 63] * 2 * env / (1024 * 100); break;
        case Tilt: {
            int tilt = kSin[(t * 64 / 900) & 63] * 2 * env / (1024 * 100), half = (T.y1 - T.y0) / 2;
            dx = half ? (gy - (T.y0 + half)) * tilt / half : 0;
            break;
        }
    }
    int sx = gx - dx, sy = gy - dy;
    if (T.has(sx, sy)) return base(sx, sy, lut);
    if (T.has(gx, gy)) return bgAt(gx, gy, lut);
    return base(gx, gy, lut);
}

void WfcAnim::sprites(uint8_t* fb, int band, const Act& a, int t, const uint8_t (*lut)[5]) const {
    const int y0 = band * kBandH, D = a.dur;
    const Rect& r = a.r;
    auto put = [&](int gx, int gy, uint8_t v) {
        int X = gx + kLeft + ox_, y = gy - y0;
        if (gx < 0 || gx >= kGridW || gy < 0 || gy >= kGridH || y < 0 || y >= kBandH || X < 0 || X >= kOutW) return;
        uint8_t* p = fb + y * (kOutW / 2) + (X >> 1);
        *p = (X & 1) ? (uint8_t)((*p & 0xF0) | v) : (uint8_t)((*p & 0x0F) | (v << 4));
    };
    auto cellAt = [&](int r0, int c0, int px, int py) {
        if (py + kCh <= y0 || py >= y0 + kBandH) return;
        const Cell& c = cells_[r0 * kCols + c0];
        uint8_t     fg = lut[c.attr][4];
        for (int ly = 0; ly < kCh; ly++)
            for (int lx = 0; lx < kCw; lx++)
                if (glyph(c.ch, lx, ly)) put(px + lx, py + ly, fg);
    };
    int wc = maxi(1, (r.x1 - r.x0) / kCw);
    if (a.fx == Explode) {  // the cells break off left to right, fly about, and home again
        Rect box = area(a);
        for (int cr = r.y0 / kCh; cr < r.y1 / kCh; cr++)
            for (int cc = r.x0 / kCw; cc < r.x1 / kCw; cc++) {
                if (kind(cr, cc) == Blank) continue;
                uint32_t h1 = mix((uint32_t)(cr * kCols + cc) * 2654435761u + a.seed), h2 = mix(h1 ^ 0xA5A5A5A5u);
                int      hx = cc * kCw, hy = cr * kCh, px = hx, py = hy;
                int      brk = (cc - r.x0 / kCw) * (D * 22 / 100) / wc + (int)(h1 % 250u);
                int      back = D * 55 / 100 + (int)(h2 % (uint32_t)(D * 12 / 100)), len = D * 25 / 100;
                if (t >= brk && t < back + len) {
                    int dt = t - brk;
                    px = bounce(hx * 256, speedOf(h1), dt, box.x0, box.x1 - kCw);
                    py = bounce(hy * 256, speedOf(h2) * 2 / 3, dt, box.y0, box.y1 - kCh);
                    if (t >= back) {
                        int u = 1024 - (t - back) * 1024 / len, e = 1024 - u * u / 1024;
                        px += (hx - px) * e / 1024, py += (hy - py) * e / 1024;
                    }
                }
                cellAt(cr, cc, px, py);
            }
        return;
    }
    // Warp: the cells fall into the middle as stars stream out of it, then grow back out
    int mx = (r.x0 + r.x1) / 2, my = (r.y0 + r.y1) / 2, half = D / 2;
    int s = t < half ? 1024 - smooth(t * 1024 / half) : smooth((t - half) * 1024 / half);
    if (s >= 60)
        for (int cr = r.y0 / kCh; cr < r.y1 / kCh; cr++)
            for (int cc = r.x0 / kCw; cc < r.x1 / kCw; cc++) {
                if (kind(cr, cc) == Blank) continue;
                int hx = cc * kCw + kCw / 2, hy = cr * kCh + kCh / 2;
                cellAt(cr, cc, mx + (hx - mx) * s / 1024 - kCw / 2, my + (hy - my) * s / 1024 - kCh / 2);
            }
    if (s < 200)
        for (int i = 0; i < 4; i++) put(mx - 1 + (i & 1), my - 1 + i / 2, lut[0x0F][4]);
    if (t > D / 10 && t < D * 9 / 10) {
        int maxR = maxi(8, maxi(r.x1 - r.x0, r.y1 - r.y0) / 2);
        for (int k = 0; k < 28; k++) {
            uint32_t hk = mix((uint32_t)k * 40503u + a.seed);
            int      ang = (int)(hk & 63u), spd = 40 + (int)((hk >> 8) % 120u), ph = (int)((hk >> 16) % 1000u);
            int      dist = (int)((int64_t)(t + ph) * spd / 1000 % maxR);
            int      x = mx + kSin[(ang + 16) & 63] * dist / 127, y = my + kSin[ang] * dist / 127;
            if (r.has(x, y)) put(x, y, lut[dist > maxR / 2 ? 0x0F : 0x08][4]);
        }
    }
}

void WfcAnim::renderBand(uint8_t* fb, int band, uint32_t now, const uint8_t (*lut)[5]) const {
    if (!cells_ || band < 0 || band >= kBands) return;
    now = at(now);
    const Act* list[kActs];
    Rect       areas[kActs];
    int        n = 0;
    const int  y0 = band * kBandH, y1 = y0 + kBandH;
    for (auto& a : acts_)
        if (started(a, now)) {
            Rect ar = area(a);
            if (ar.y0 < y1 && ar.y1 > y0) list[n] = &a, areas[n++] = ar;
        }
    Rect       wa = wigArea();
    const bool wig = jig_.on && wa.y0 < y1 && wa.y1 > y0;
    const uint8_t black = lut[0][0];
    for (int y = 0; y < kBandH; y++) {
        int      gy = y0 + y;
        uint8_t* row = fb + y * (kOutW / 2);
        for (int X = 0; X < kOutW; X += 2) {
            uint8_t v[2];
            for (int k = 0; k < 2; k++) {
                int gx = X + k - kLeft - ox_;
                if (gx < 0 || gx >= kGridW) {
                    v[k] = black;
                    continue;
                }
                int i = 0;
                while (i < n && !areas[i].has(gx, gy)) i++;
                if (i < n) v[k] = fxPixel(*list[i], gx, gy, clampi((int)(now - list[i]->start), 0, list[i]->dur), lut);
                else if (wig && wa.has(gx, gy)) v[k] = wigPixel(gx, gy, now, lut);
                else v[k] = base(gx, gy, lut);
            }
            row[X >> 1] = (uint8_t)(v[0] << 4 | v[1]);
        }
    }
    for (int i = 0; i < n; i++)
        if (list[i]->fx == Explode || list[i]->fx == Warp)
            sprites(fb, band, *list[i], clampi((int)(now - list[i]->start), 0, list[i]->dur), lut);
}

}  // namespace axis::bbs
