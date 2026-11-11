// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#pragma GCC optimize("Os")  // menus and screens: size over speed (the door CPU and transfers stay -O2)
#include "bbs/ansiterm.h"

#include <cstdlib>
#include <vector>

namespace axis::bbs {

namespace {
const uint8_t kCga[8] = {0, 4, 2, 6, 1, 5, 3, 7};  // ANSI colour number to CGA's
}

void AnsiTerm::clear() {
    for (auto& r : cells_)
        for (auto& c : r) c = Cell();  // the whole array, whatever the size
    dirty_ = true;
}

void AnsiTerm::newline() {
    if (++y_ < rows_) return;
    y_ = rows_ - 1;  // scroll everything up a line
    for (int r = 1; r < rows_; r++)
        for (int c = 0; c < cols_; c++) cells_[r - 1][c] = cells_[r][c];
    for (int c = 0; c < cols_; c++) cells_[rows_ - 1][c] = Cell();
}

void AnsiTerm::put(uint8_t ch) {
    if (x_ >= cols_) {  // the last column was written: wrap now
        x_ = 0;
        newline();
    }
    Cell& c = cells_[y_][x_++];
    c.ch = ch;
    c.attr = (uint8_t)((bold_ ? fg_ | 8 : fg_) | bg_ << 4);
    dirty_ = true;
}

void AnsiTerm::csi(char f) {
    std::vector<int> p;
    for (size_t pos = 0; pos <= params_.size();) {
        size_t e = params_.find(';', pos);
        if (e == std::string::npos) e = params_.size();
        p.push_back(e > pos ? std::atoi(params_.substr(pos, e - pos).c_str()) : -1);
        pos = e + 1;
    }
    auto arg = [&](size_t i, int def) { return i < p.size() && p[i] >= 0 ? p[i] : def; };
    switch (f) {
        case 'm':
            for (size_t i = 0; i < p.size(); i++) {
                int v = p[i] < 0 ? 0 : p[i];
                if (v == 0) fg_ = 7, bg_ = 0, bold_ = false;
                else if (v == 1) bold_ = true;
                else if (v == 22) bold_ = false;
                else if (v >= 30 && v <= 37) fg_ = kCga[v - 30];
                else if (v >= 40 && v <= 47) bg_ = kCga[v - 40];
            }
            break;
        case 'H':
        case 'f':
            y_ = arg(0, 1) - 1;
            x_ = arg(1, 1) - 1;
            break;
        case 'A': y_ -= arg(0, 1); break;
        case 'B': y_ += arg(0, 1); break;
        case 'C': x_ += arg(0, 1); break;
        case 'D': x_ -= arg(0, 1); break;
        case 's': sx_ = x_, sy_ = y_; break;
        case 'u': x_ = sx_, y_ = sy_; break;
        case 'J':
            if (arg(0, 0) == 2) {
                clear();
                x_ = y_ = 0;
            } else {  // from the cursor to the end of the screen
                for (int c = x_; c < cols_; c++) cells_[y_][c] = Cell();
                for (int r = y_ + 1; r < rows_; r++)
                    for (int c = 0; c < cols_; c++) cells_[r][c] = Cell();
                dirty_ = true;
            }
            break;
        case 'K':
            for (int c = x_ < 0 ? 0 : x_; c < cols_; c++) cells_[y_][c] = Cell();
            dirty_ = true;
            break;
        default: break;
    }
    if (x_ < 0) x_ = 0;
    if (x_ > cols_ - 1 && f != 'm') x_ = cols_ - 1;
    if (y_ < 0) y_ = 0;
    if (y_ > rows_ - 1) y_ = rows_ - 1;
}

void AnsiTerm::feed(const std::string& bytes) { feed(bytes.data(), bytes.size()); }

void AnsiTerm::feed(const char* p, size_t n) {
    for (size_t i = 0; i < n; i++) {
        uint8_t ch = (uint8_t)p[i];
        if (esc_ == 1) {
            esc_ = ch == '[' ? 2 : 0;
            params_.clear();
            continue;
        }
        if (esc_ == 2) {
            if ((ch >= '0' && ch <= '9') || ch == ';' || ch == '?') {
                if (ch != '?') params_ += (char)ch;
            } else {
                csi((char)ch);
                esc_ = 0;
            }
            continue;
        }
        switch (ch) {
            case 27: esc_ = 1; break;
            case '\r': x_ = 0; break;
            case '\n': newline(); break;
            case '\b':
                if (x_ > 0) x_--;
                break;
            case 7: break;  // bell
            default:
                if (ch >= 32) put(ch);
                break;
        }
    }
}

}  // namespace axis::bbs
