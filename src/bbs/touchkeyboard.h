// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// An on-screen keyboard for the CYD's 320x240 touch panel: a number row and letters (shift for
// capitals, one at a time), a symbols page, space, delete and OK. A password stays masked until SHOW
// is tapped. Portable: the firmware draws keys() and passes taps in.
#pragma once
#include <string>
#include <vector>

namespace axis::bbs {

class TouchKeyboard {
public:
    struct Key {
        std::string label;
        int         x, y, w, h;
        bool        lit = false;  // a toggle that is on (SHIFT, SHOW, #+=)
    };
    TouchKeyboard(std::string title, bool secret, size_t maxLen = 63)
        : title_(std::move(title)), secret_(secret), max_(maxLen) {}
    std::vector<Key>   keys() const;
    void               tap(int x, int y);
    const std::string& text() const { return text_; }
    std::string        shown() const;  // what the text field shows (masked for a password)
    const std::string& title() const { return title_; }
    bool               done() const { return done_; }
    bool               cancelled() const { return cancelled_; }
    bool               shifted() const { return shift_; }
    void               reset(const std::string& title, bool secret);

private:
    std::string title_, text_;
    bool        secret_, show_ = false, shift_ = false, symbols_ = false, done_ = false, cancelled_ = false;
    size_t      max_;
};

}  // namespace axis::bbs
