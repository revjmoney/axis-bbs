// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// An 80x25 ANSI-BBS terminal in memory: what a caller's terminal would show, for the sysop's
// local login on the CYD. Text, SGR colours (bold = bright), cursor moves (H f A B C D), save and
// restore (s u), erase (J K), and the immediate wrap after column 80 that BBS terminals do.
#pragma once
#include <cstdint>
#include <string>

namespace axis::bbs {

class AnsiTerm {
public:
    static constexpr int kCols = 80, kRows = 25;
    struct Cell {
        uint8_t ch = ' ', attr = 7;  // attr: CGA foreground | background << 4
    };
    AnsiTerm() { clear(); }
    void        feed(const std::string& bytes);
    const Cell& cell(int r, int c) const { return cells_[r][c]; }
    int         row() const { return y_; }
    int         col() const { return x_; }
    bool        dirty() const { return dirty_; }
    void        clean() { dirty_ = false; }

private:
    void clear();
    void put(uint8_t ch);
    void newline();
    void csi(char final);
    Cell        cells_[kRows][kCols];
    int         x_ = 0, y_ = 0, sx_ = 0, sy_ = 0;
    uint8_t     fg_ = 7, bg_ = 0;
    bool        bold_ = false, dirty_ = true;
    int         esc_ = 0;  // 0 text, 1 after ESC, 2 in CSI
    std::string params_;
};

}  // namespace axis::bbs
