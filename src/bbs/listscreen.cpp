// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#pragma GCC optimize("Os")  // menus and screens: size over speed (the door CPU and transfers stay -O2)
#include "bbs/listscreen.h"

#include "bbs/theme.h"

namespace axis::bbs {

namespace {
std::string clip(const std::string& s, size_t n) {
    std::string o;
    o.reserve(n);
    for (unsigned char c : s) {
        if (o.size() >= n) break;
        if (c >= 0x20 && c < 0x7f) o += (char)c;
        else if (c == '\t') o += ' ';
    }
    return o;
}

int rowAt(int r) { return r < 1 ? 1 : r > 24 ? 24 : r; }
}  // namespace

bool listScreenTemplate(const std::string& name, ListTemplate& out) { return theme::listTemplate(name, out); }

std::string listScreenHead(const std::string& artKey, const std::string& title, int rows) {
    std::string art = theme::artFor(artKey, rows);  // FILES.ANS / MSGS.ANS when the theme pack is on the card
    if (art.empty() && artKey != title) art = theme::artFor(title, rows);
    if (!art.empty()) return art;
    return theme::banner(title);
}

std::string listScreenTop(const std::string& title, const std::string& columns, const ListTemplate* tpl) {
    // The lightbar is pinned at row 6. A header .ANS is taller than that, so keep its top
    // (the block title) and park the column line on row 4.
    std::string key = title == "FILE AREAS" ? "FILES" : title == "MESSAGE AREAS" ? "MSGS" : title;
    // Only the art's top 3 rows: the rest was read, kept and sent (a theme's MSGS.ANS is 8 KB, 17 s for an SF10 radio caller)
    // just to be wiped by the clear below. 2026-11-16 board 1: the message areas screen took the heap 11 KB under the call's low.
    std::string s = theme::CLS;
    if (tpl) {
        s += listScreenHead(tpl->head.empty() ? key : tpl->head, title, 3);
        int capRow = tpl->y - 2 < 4 ? 4 : tpl->y - 2, ruleRow = tpl->y - 1;
        s += "\x1b[" + std::to_string(capRow) + ";1H\x1b[J";
        if (!tpl->caption.empty()) {  // static text: a plain caller and no values (the caption is not per row)
            ListTemplate c;
            c.row = tpl->caption, c.width = tpl->width;
            s += "\x1b[" + std::to_string(capRow) + ";" + std::to_string(tpl->x) + "H" +
                 renderRow(c, AcsUser{}, [](const std::string&) { return std::string(); }) + theme::RESET;
        }
        if (tpl->rule && ruleRow > capRow)
            s += "\x1b[" + std::to_string(ruleRow) + ";" + std::to_string(tpl->x) + "H" + theme::ORANGE +
                 std::string((size_t)tpl->width, 'Í') + theme::RESET;
        return s;
    }
    s += listScreenHead(key, title, 3);
    s += "\x1b[4;1H\x1b[J";
    s += std::string("  ") + theme::DGREY + columns + "\r\n";
    s += std::string("  ") + theme::ORANGE + std::string(74, '\xcd') + theme::RESET + "\r\n";
    return s;
}

std::string listScreenStatus(const std::string& line, const ListTemplate* tpl) {
    if (tpl)
        return "\x1b[" + std::to_string(rowAt(tpl->y + tpl->rows + 1)) + ";1H\x1b[2K" + theme::TEXT +
               std::string((size_t)(tpl->x - 1), ' ') + clipVisible(line, (size_t)tpl->width);
    return "\x1b[" + std::to_string(kListStatusRow) + ";1H\x1b[2K" + theme::TEXT + "  " + clip(line, 76);
}

std::string listScreenKeys(const std::string& keys, const ListTemplate* tpl) {
    if (tpl)
        return "\x1b[" + std::to_string(rowAt(tpl->y + tpl->rows + 3)) + ";1H\x1b[2K" +
               std::string((size_t)(tpl->x - 1), ' ') + clipVisible(keys, (size_t)tpl->width) + theme::RESET;
    return "\x1b[" + std::to_string(kListKeyRow) + ";1H\x1b[2K  " + keys + theme::RESET;
}

}  // namespace axis::bbs
