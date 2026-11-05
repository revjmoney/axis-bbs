// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include "bbs/menufile.h"

#include <cstdlib>

namespace axis::bbs {

namespace {
std::vector<std::string> split(const std::string& s, char sep, size_t max) {  // the last field keeps the rest
    std::vector<std::string> v;
    size_t                   from = 0;
    while (v.size() + 1 < max) {
        size_t at = s.find(sep, from);
        if (at == std::string::npos) break;
        v.push_back(s.substr(from, at - from));
        from = at + 1;
    }
    v.push_back(s.substr(from));
    return v;
}
}  // namespace

bool parseMenu(const std::string& text, MenuDef& out) {
    out = MenuDef();
    for (size_t from = 0; from < text.size();) {
        size_t      nl = text.find('\n', from);
        std::string line = text.substr(from, nl == std::string::npos ? std::string::npos : nl - from);
        from = nl == std::string::npos ? text.size() : nl + 1;
        while (!line.empty() && line.back() == '\r') line.pop_back();  // (trailing spaces stay: prompts end with one)
        size_t eq = line.find('=');
        if (line.empty() || line[0] == ';' || eq == std::string::npos) continue;
        std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        if (k == "title") out.title = v;
        else if (k == "clr") out.clr = v != "0";
        else if (k == "prompt") out.usePrompt = v != "0";
        else if (k == "prompt1") out.prompt1 = v;
        else if (k == "prompt2") out.prompt2 = v;
        else if (k == "fallback") out.fallback = v;
        else if (k == "acs") out.acs = v;
        else if (k == "pass") out.pass = v;
        else if (k == "help") out.help = v;
        else if (k == "hotkey") out.forceHotkey = v != "0";
        else if (k == "cmd") {
            auto    f = split(v, '|', 5);
            MenuCmd c;
            c.keys = f[0];
            if (f.size() > 1) c.cmd = f[1];
            if (f.size() > 2) c.acs = f[2];
            if (f.size() > 3) c.hidden = f[3] == "1";
            if (f.size() > 4) c.activity = f[4];
            if (!c.keys.empty() && !c.cmd.empty()) out.cmds.push_back(c);
        } else if (k == "bar") {
            auto f = split(v, ',', 6);
            if (f.size() < 6) continue;
            BarItem b;
            b.x = std::atoi(f[0].c_str()), b.y = std::atoi(f[1].c_str());
            b.hi = std::atoi(f[2].c_str()), b.reg = std::atoi(f[3].c_str());
            b.key = f[4], b.text = f[5];
            out.bars.push_back(b);
        }
    }
    return !out.cmds.empty();
}

std::string formatMenu(const MenuDef& m) {
    std::string s = "title=" + m.title + "\nclr=" + (m.clr ? "1" : "0") + "\nprompt=" + (m.usePrompt ? "1" : "0") +
                    "\nprompt1=" + m.prompt1 + "\nprompt2=" + m.prompt2 + "\nfallback=" + m.fallback + "\nacs=" + m.acs +
                    "\npass=" + m.pass + "\nhelp=" + m.help + "\nhotkey=" + (m.forceHotkey ? "1" : "0") + "\n";
    for (const MenuCmd& c : m.cmds) s += "cmd=" + c.keys + "|" + c.cmd + "|" + c.acs + "|" + (c.hidden ? "1" : "0") + "|" + c.activity + "\n";
    for (const BarItem& b : m.bars)
        s += "bar=" + std::to_string(b.x) + "," + std::to_string(b.y) + "," + std::to_string(b.hi) + "," + std::to_string(b.reg) + "," + b.key + "," + b.text + "\n";
    return s;
}

}  // namespace axis::bbs
