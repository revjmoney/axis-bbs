// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The waiting screen on the board's own panel keeps moving so it never burns in: parts of it melt, dissolve, explode, flap,
// rain away and come back (the LFD3K-SM idle show's transitions, done on the WFC's 7x10 cells), each part at its own time;
// border lines melt or turn into little bots that rock about; sometimes it rests, but one thing is always jiggling; and the
// whole screen drifts a pixel or two now and then. Telnet and serial WFCs never see any of it: this only draws the panel.
//
// Stateless like the idle show: every effect is a pure function of its part, its seed and the time since it began, so a
// 40 px band can be drawn on its own (no full frame anywhere). The only memory is a copy of the cells (2,160 B) while the
// screen animates, made by load() and given back by unload().
#pragma once
#include <stdint.h>

#include <memory>

namespace axis::bbs {
class AnsiTerm;

class WfcAnim {
public:
    static constexpr int kCols = 45, kRows = 24, kCw = 7, kCh = 10, kLeft = 2;
    static constexpr int kGridW = kCols * kCw, kGridH = kRows * kCh;  // 315 x 240
    static constexpr int kBands = 6, kBandH = 40, kOutW = 320;
    // the transitions: 0..12 take a part of the screen away and bring it back, 13 and 14 work on border lines, and Drain
    // takes the whole screen: it breaks into dust that swirls down a drain (the middle or a random spot) and back out
    enum Fx : uint8_t { Dissolve, Explode, Blocks, Sine, Flap, Type, Drip, Glitch, Rain, Warp, Crt, Blinds, Pixelate, Melt, Bots, Drain, kFxCount };
    static constexpr int kContentFx = Melt;
    // the one thing that is always moving
    enum Wig : uint8_t { Wiggle, Jostle, Shimmy, Bob, Tilt, kWigCount };
    static const char* fxName(int fx);
    static const char* wigName(int w);
    static int         fxByName(const char* name);  // -1: none

    struct Rect {  // grid pixels (the 315x240 grid before it is placed on the panel); x1, y1 are past the end
        int16_t x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        bool    empty() const { return x1 <= x0 || y1 <= y0; }
        bool    has(int x, int y) const { return x >= x0 && x < x1 && y >= y0 && y < y1; }
    };
    struct Act {
        uint8_t  fx = 0;
        bool     on = false;
        Rect     r;          // the part (cells, in pixels)
        uint32_t start = 0;  // may lie ahead (a cascade): nothing shows until then
        uint16_t dur = 0;
        uint32_t seed = 0;
    };
    static constexpr int kActs = 6, kParts = 12, kRuns = 16, kFlips = 3;
    // the little stick figure who lives on the screen: walks about, hangs out in empty spaces, plays hacky sack, and walks up
    // to letters to show them off like a game show hostess, flipping them upside down (they flip back later)
    enum Doing : uint8_t { Walk, Hang, Hacky, Vanna };
    struct Walker {
        bool     on = false, face = true, flipped = false;  // face: looking right
        uint8_t  doing = Hang, then = Hang;                  // then: what it does once the walk ends
        int16_t  x0 = 0, y0 = 0, x1 = 0, y1 = 0;            // feet: the figure's left column, the row under its feet
        int16_t  cell = -1;                                  // the letter it is showing
        uint32_t start = 0;
        uint16_t dur = 0;
    };

    bool load(const AnsiTerm& t);  // the screen to animate (again after it changed); false: no room, stay still
    void unload();
    bool loaded() const { return cells_ != nullptr; }

    void    step(uint32_t now);      // the schedule: starts and ends transitions, picks the next thing to jiggle
    uint8_t bandsDue(uint32_t now);  // bit b: band b must be drawn again for this frame (what moves now or moved last frame)
    // one 320x40 band of the panel (4 bits a pixel, 160 bytes a row, high nibble first), lut: palette number for each
    // attribute at coverage 0 (background) and 4 (foreground), as AnsiScreen builds it
    void renderBand(uint8_t* fb, int band, uint32_t now, const uint8_t (*lut)[5]) const;

    // bench (@WFCFX): start an effect now (part -1: any), hold the time tau ms into it (-1: let it run), stop everything
    bool        force(int fx, uint32_t now, int part = -1);
    void        hold(int32_t tau);
    void        calm(uint32_t now);  // a still spell starts now (the jiggle goes on)
    bool        held() const { return held_; }
    int         ox() const { return ox_; }
    bool        still() const { return still_; }
    int         parts() const { return nParts_; }
    int         runs() const { return nRuns_; }
    const Act&  act(int i) const { return acts_[i]; }
    const Act&  jiggle() const { return jig_; }
    bool        busyAt(uint32_t now) const;  // any transition showing
    bool        walkerDo(int doing, uint32_t now);  // bench: Hang, Hacky or Vanna next (it walks there first)
    const Walker& walker() const { return walker_; }
    int         flipped() const;  // letters upside down now
    static void drainSpot(uint32_t seed, int& x, int& y);  // where a drain with this seed swirls (somewhere new each time)

private:
    struct Cell {
        uint8_t ch, attr;
    };
    enum Kind : uint8_t { Blank, Line, Art };
    Kind     kind(int r, int c) const;
    uint32_t rnd();
    uint32_t at(uint32_t now) const { return held_ ? heldNow_ : now; }
    bool     started(const Act& a, uint32_t now) const { return a.on && (int32_t)(now - a.start) >= 0; }
    Rect     area(const Act& a) const;  // where it draws (a part, more for the ones that fly or drip)
    Rect     wigArea() const;
    void     start(int fx, const Rect& r, uint32_t when);
    void     startSome(uint32_t now);
    void     pickJiggle(uint32_t now);
    bool     clash(const Rect& r, uint32_t now) const;
    // drawing: palette numbers
    bool    lit(int gx, int gy) const;                       // the still screen's pixel is foreground
    uint8_t base(int gx, int gy, const uint8_t (*lut)[5]) const;  // the still screen
    uint8_t bgAt(int gx, int gy, const uint8_t (*lut)[5]) const;  // the cell's background there (its glyph gone)
    uint8_t pixel(int gx, int gy, uint32_t now, const uint8_t (*lut)[5], const Act* const* acts, int n) const;
    uint8_t fxPixel(const Act& a, int gx, int gy, int t, const uint8_t (*lut)[5]) const;
    uint8_t wigPixel(int gx, int gy, uint32_t now, const uint8_t (*lut)[5]) const;
    void    sprites(uint8_t* fb, int band, const Act& a, int t, const uint8_t (*lut)[5]) const;
    void    drawWalker(uint8_t* fb, int band, uint32_t now, const uint8_t (*lut)[5]) const;
    void    walkerAt(uint32_t now, int& x, int& y) const;
    Rect    walkerArea(uint32_t now) const;
    void    walkTo(uint32_t now, int x, int y, int then, int cell);
    void    nextWalker(uint32_t now, int want);
    void    flipCell(int cell, uint32_t now);
    int     flipK(int cell) const;  // 1024 upright .. -1024 upside down, between: turning over
    bool    draining(uint32_t now) const;

    std::unique_ptr<Cell[]> cells_;  // 45x24, row by row
    Rect                    parts_[kParts];
    Rect                    runs_[kRuns];  // border lines (one row tall or one column wide)
    int                     nParts_ = 0, nRuns_ = 0;
    Act                     acts_[kActs];
    Act                     jig_;  // fx: a Wig
    uint32_t                rng_ = 0x2545F491u;
    bool                    begun_ = false, still_ = false, held_ = false;
    uint32_t                heldNow_ = 0, nextStart_ = 0, spellEnd_ = 0, orbitAt_ = 0;
    int8_t                  ox_ = 0, drawnOx_ = 0;
    uint8_t                 orbitStep_ = 0, lastMask_ = 0;
    int8_t                  forced_ = -1;  // the act @WFCFX started (hold times it)
    uint16_t                bag_ = 0;  // content effects used this round (each one once before any again)
    Walker                  walker_;
    int16_t                 lastDrainX_ = -1000, lastDrainY_ = -1000;
    uint32_t                lastDrainMs_ = 0;  // a drain at most every 15 minutes
    struct Flip {
        int16_t  cell = -1;
        bool     up = false;  // turning (or turned) upside down; false: turning back, then gone
        uint32_t at = 0, until = 0;
    } flips_[kFlips];
    mutable uint32_t drawNow_ = 0;  // the time of the band being drawn (letters part way through a flip)
};

}  // namespace axis::bbs
