// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// A lightbar over ANSI art, ViSiON/3 style: each item sits at its own screen spot in its own colours (DOS
// attributes, bg*16+fg), the current one in its highlight. Up/down (and left/right) move it with wrap, 1-9
// jump, an item's hotkey picks it, Enter picks the current one. Terminal arrow keys arrive as ESC [ A even
// when split across packets; a lone ESC counts once the caller pauses (keyTimeout).
#pragma once
#include <string>
#include <vector>

#include "bbs/menufile.h"

namespace axis::bbs {

std::string dosColour(int attr);  // a DOS attribute as an ANSI SGR sequence

class Lightbar {
public:
    explicit Lightbar(std::vector<BarItem> items);
    std::string draw();          // every item, the current one highlighted
    int         key(char c);     // the picked item's index once picked, -1 otherwise
    bool        escaped() const { return escaped_; }
    void        keyTimeout();    // the caller paused after ESC: it was Esc
    bool        midKey() const { return esc_ != 0; }
    std::string takeRedraw();    // only the rows that changed since draw()/takeRedraw()
    int         current() const { return at_; }
    size_t      size() const { return items_.size(); }
    const BarItem& item(size_t i) const { return items_[i]; }

private:
    std::string row(size_t i) const;
    std::vector<BarItem> items_;
    int                  at_ = 0, shown_ = 0, esc_ = 0;
    bool                 escaped_ = false;
};

// a Yes / No lightbar at (x,y); key() gives 1 for yes, 0 for no, -1 while undecided
class YesNoBar {
public:
    YesNoBar(int x, int y, bool defaultYes);
    std::string draw() { return bar_.draw(); }
    std::string takeRedraw() { return bar_.takeRedraw(); }
    int         key(char c);

private:
    Lightbar bar_;
};

}  // namespace axis::bbs
