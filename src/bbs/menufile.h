// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// A menu as ViSiON/3 defines it (its MNU fields, CFG commands and BAR lightbar items), kept in one lean
// text file per menu, /menus/NAME.MNU (";" starts a comment):
//   title=  clr=1  prompt=1  prompt1=  prompt2=  fallback=  acs=  pass=  help=  hotkey=1
//   cmd=KEYS|CMD|ACS|HIDDEN(0/1)|NODE ACTIVITY        (KEYS: several, space separated; "//" "~~" auto-run)
//   bar=X,Y,HICOLOUR,COLOUR,KEY,TEXT                    (colours are DOS attributes, bg*16+fg)
#pragma once
#include <string>
#include <vector>

namespace axis::bbs {

struct MenuCmd {
    std::string keys, cmd, acs, activity;
    bool        hidden = false;
};
struct BarItem {
    int         x = 1, y = 1, hi = 31, reg = 7;
    std::string key, text;
};
struct MenuDef {
    std::string          title, prompt1, prompt2, fallback, acs = "*", pass, help;
    bool                 clr = true, usePrompt = true, forceHotkey = false;
    std::vector<MenuCmd> cmds;
    std::vector<BarItem> bars;
};

bool        parseMenu(const std::string& text, MenuDef& out);  // false on a file with no commands
std::string formatMenu(const MenuDef& m);

}  // namespace axis::bbs
