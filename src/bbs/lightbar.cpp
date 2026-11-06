// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include "bbs/lightbar.h"

#include <cctype>
#include <cstdlib>

namespace axis::bbs {

namespace {
const char kAnsi[8] = {'0', '4', '2', '6', '1', '5', '3', '7'};  // DOS colour order to ANSI's
}

std::string dosColour(int attr) {
    int fg = attr & 15, bg = (attr >> 4) & 7;
    return std::string("\x1b[0;") + (fg & 8 ? "1;" : "") + (attr & 0x80 ? "5;" : "") + "3" + kAnsi[fg & 7] + ";4" + kAnsi[bg] + "m";
}

Lightbar::Lightbar(std::vector<BarItem> items) : items_(std::move(items)) {}

std::string Lightbar::row(size_t i) const {
    const BarItem& b = items_[i];
    return "\x1b[" + std::to_string(b.y) + ";" + std::to_string(b.x) + "H" + dosColour((int)i == at_ ? b.hi : b.reg) + b.text + "\x1b[0m";
}

std::string Lightbar::draw() {
    std::string s;
    for (size_t i = 0; i < items_.size(); i++) s += row(i);
    shown_ = at_;
    return s;
}

std::string Lightbar::takeRedraw() {
    if (shown_ == at_ || items_.empty()) return "";
    std::string s = row((size_t)shown_) + row((size_t)at_);  // the old bar back to plain, the new one lit
    shown_ = at_;
    return s;
}

void Lightbar::keyTimeout() {
    if (esc_ == 1) escaped_ = true;
    esc_ = 0;
}

int Lightbar::key(char c) {
    if (items_.empty()) return -1;
    int n = (int)items_.size();
    if (esc_ == 1) {
        if (c == '[' || c == 'O') {
            esc_ = 2;
            return -1;
        }
        esc_ = 0, escaped_ = true;  // a lone ESC, then something else: handle that key too
    } else if (esc_ == 2) {
        esc_ = 0;
        if (c == 'A' || c == 'D') at_ = (at_ + n - 1) % n;
        else if (c == 'B' || c == 'C') at_ = (at_ + 1) % n;
        else if (c == 'H') at_ = 0;
        else if (c == 'F') at_ = n - 1;
        return -1;
    }
    if (c == '\x1b') {
        esc_ = 1;
        return -1;
    }
    if (c == '\r' || c == '\n') return at_;
    if (c >= '1' && c <= '9' && c - '1' < n) {
        bool isHotkey = false;  // a digit that is some item's own hotkey picks that item instead
        for (const BarItem& b : items_)
            if (b.key.size() == 1 && b.key[0] == c) isHotkey = true;
        if (!isHotkey) return at_ = c - '1';
    }
    for (int i = 0; i < n; i++)
        if (items_[(size_t)i].key.size() == 1 && std::toupper((unsigned char)items_[(size_t)i].key[0]) == std::toupper((unsigned char)c)) return at_ = i;
    return -1;
}

ScrollList::ScrollList(int x, int y, int width, int height, int hi, int reg) : x_(x), y_(y), w_(width), h_(height < 1 ? 1 : height), hi_(hi), reg_(reg) {}

void ScrollList::setItems(std::vector<std::string> items, size_t at) {
    items_ = std::move(items);
    at_ = top_ = 0;
    typed_.clear();
    escaped_ = false;
    move((int)at);
}

void ScrollList::move(int to) {
    int n = (int)items_.size();
    clamped_ = 0;
    if (n == 0) return;
    if (to < 0) clamped_ = -1;
    else if (to >= n) clamped_ = 1;
    at_ = to < 0 ? 0 : to >= n ? n - 1 : to;
    if (at_ < top_) top_ = at_;
    if (at_ >= top_ + h_) top_ = at_ - h_ + 1;
}

std::string ScrollList::row(int i) const {
    std::string t = i < (int)items_.size() ? items_[(size_t)i] : "";
    if ((int)t.size() > w_) t.resize((size_t)w_);
    t.resize((size_t)w_, ' ');
    return "\x1b[" + std::to_string(y_ + i - top_) + ";" + std::to_string(x_) + "H" + dosColour(i == at_ ? hi_ : reg_) + t + "\x1b[0m";
}

std::string ScrollList::draw() {
    std::string s;
    for (int i = top_; i < top_ + h_; i++) s += row(i);
    shownAt_ = at_, shownTop_ = top_;
    return s;
}

std::string ScrollList::takeRedraw() {
    if (top_ != shownTop_) return draw();
    if (at_ == shownAt_) return "";
    std::string s = row(shownAt_) + row(at_);
    shownAt_ = at_;
    return s;
}

void ScrollList::keyTimeout() {
    if (esc_ == 1) escaped_ = true;
    esc_ = 0;
}

int ScrollList::key(char c) {
    clamped_ = 0;
    int n = (int)items_.size();
    if (esc_ == 1) {
        if (c == '[' || c == 'O') {
            esc_ = 2;
            return -1;
        }
        esc_ = 0, escaped_ = true;
    } else if (esc_ == 2) {
        esc_ = 0;
        if (c >= '1' && c <= '6') {
            esc_ = 3, escNum_ = c - '0';
            return -1;
        }
        if (c == 'A') move(at_ - 1);
        else if (c == 'B') move(at_ + 1);
        else if (c == 'H') move(0);
        else if (c == 'F') move(n - 1);
        typed_.clear();
        return -1;
    } else if (esc_ == 3) {  // ESC [ n ~
        esc_ = 0;
        if (escNum_ == 5) move(at_ - h_);
        else if (escNum_ == 6) move(at_ + h_);
        else if (escNum_ == 1) move(0);
        else if (escNum_ == 4) move(n - 1);
        typed_.clear();
        return -1;
    }
    if (c == '\x1b') {
        esc_ = 1;
        return -1;
    }
    if (n == 0) return -1;
    if (c == '\r' || c == '\n') {
        if (!typed_.empty()) {
            int pick = std::atoi(typed_.c_str()) - 1;
            typed_.clear();
            if (pick < 0 || pick >= n) return -1;
            move(pick);
        }
        return at_;
    }
    if (c >= '0' && c <= '9') {
        typed_ += c;
        int to = std::atoi(typed_.c_str()) - 1;
        if (to >= 0 && to < n) move(to);
        return -1;
    }
    if (c == 8 || c == 127) {
        if (!typed_.empty()) typed_.pop_back();
        return -1;
    }
    if (std::isalpha((unsigned char)c)) {  // the next item starting with that letter
        typed_.clear();
        for (int k = 1; k <= n; k++) {
            int i = (at_ + k) % n;
            if (!items_[(size_t)i].empty() && std::toupper((unsigned char)items_[(size_t)i][0]) == std::toupper((unsigned char)c)) {
                move(i);
                break;
            }
        }
    }
    return -1;
}

YesNoBar::YesNoBar(int x, int y, bool defaultYes) : bar_({{x, y, 31, 7, "Y", " Yes "}, {x + 6, y, 31, 7, "N", " No "}}) {
    if (!defaultYes) bar_.key('2');
}

int YesNoBar::key(char c) {
    int r = bar_.key(c);
    return r < 0 ? -1 : r == 0 ? 1 : 0;
}

}  // namespace axis::bbs
