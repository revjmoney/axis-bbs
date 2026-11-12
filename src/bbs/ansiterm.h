// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// An ANSI-BBS terminal in memory (80x25 by default, any size up to 80x30): what a caller's terminal would show, for the sysop's
// local login on the CYD. Text, SGR colours (bold = bright), cursor moves (H f A B C D), save and
// restore (s u), erase (J K), and the immediate wrap after the last column that BBS terminals do.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace axis::bbs {

class AnsiTerm {
public:
    static constexpr int kMaxCols = 80, kMaxRows = 30;
    struct Cell {
        uint8_t ch = ' ', attr = 7;  // attr: CGA foreground | background << 4
    };
    explicit AnsiTerm(int cols = 80, int rows = 25)
        : cols_(cols < 1 ? 1 : cols > kMaxCols ? kMaxCols : cols), rows_(rows < 1 ? 1 : rows > kMaxRows ? kMaxRows : rows),
          cells_((size_t)cols_ * rows_) {  // only the cells of its own screen (a 45x24 panel is 2160, not 80x30)
        clear();
    }
    int         cols() const { return cols_; }
    int         rows() const { return rows_; }
    void        feed(const std::string& bytes);
    void        feed(const char* p, size_t n);  // in pieces: an escape may span two calls
    const Cell& cell(int r, int c) const { return cells_[(size_t)r * cols_ + c]; }
    int         row() const { return y_; }
    int         col() const { return x_; }
    bool        dirty() const { return dirty_; }
    void        clean() { dirty_ = false; }

private:
    void clear();
    void put(uint8_t ch);
    void newline();
    void csi(char final);
    Cell&       at(int r, int c) { return cells_[(size_t)r * cols_ + c]; }
    int         cols_, rows_;
    std::vector<Cell> cells_;  // rows_ * cols_, row by row (declared after cols_ and rows_: it is sized from them)
    int         x_ = 0, y_ = 0, sx_ = 0, sy_ = 0;
    uint8_t     fg_ = 7, bg_ = 0;
    bool        bold_ = false, dirty_ = true;
    int         esc_ = 0;  // 0 text, 1 after ESC, 2 in CSI
    std::string params_;
};

}  // namespace axis::bbs
