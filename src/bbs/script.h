// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// A program loaded from the card. The engine stays in flash. The file is the program.
//
// /scripts/NAME.axs (8 KB, one cluster frame). The Scripts door lists the folder.
// A menu command SCRIPT:name runs that file and returns to the menu.
// A login sequence step SCRIPT with the name in the data field does the same, then continues.
//
// Lines, one command each. ; starts a comment.
//   #label   or  :label
//   say text          @HANDLE @NODE @BBS @TIME and @VAR, and |00..|15 colours
//   cls
//   pause             wait for a key
//   ask VAR prompt
//   goto label
//   gosub label
//   return            exit when there is no gosub to go back to
//   if VAR = text     also == and !  (the rest of the line is the text)
//   else
//   end
//   menu              following lines are: KEY LABEL text
//   end               closes menu, or closes if
//   exit
//   sensor NAME       say a stub reading (temp, water, light, humid)
//   sensor NAME VAR   store that reading in VAR
//   relay N           say whether relay N is on (1 to 4)
//   relay N on|off
//   beep [HZ MS]      stub speaker, nothing is driven
#pragma once
#include <string>
#include <utility>
#include <vector>

namespace axis::bbs {

inline bool scriptNameOk(const std::string& name) {
    if (name.empty() || name.size() > 16) return false;
    for (unsigned char c : name)
        if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_' || c == '-')) return false;
    return true;
}

class ScriptProg {
public:
    struct World {
        std::string handle, bbs, node, time;
    };

    bool               load(const std::string& text, const World& world);
    bool               offer(const std::vector<std::string>& names);  // the caller picks a file
    bool               running() const { return on_; }
    bool               waiting() const { return wait_ != Wait::None; }
    const std::string& picked() const { return picked_; }
    // key < 0: no key this turn. False once the program has ended.
    bool               step(int key, std::string& out);

private:
    enum class Wait { None, Pause, Ask, Menu, Pick };
    struct Item {
        std::string key, label, text;
    };
    struct If {
        bool active = false, took = false, hadElse = false;
    };

    bool        live() const;
    void        exec(const std::string& line, std::string& out);
    void        takeKey(int key, std::string& out);
    void        paintPick(std::string& out) const;
    std::string expand(const std::string& in) const;
    std::string value(const std::string& key) const;
    void        setVar(const std::string& key, const std::string& val);
    int         findLabel(const std::string& name) const;
    bool        cond(const std::string& rest) const;
    void        doElse();
    void        reset();

    World                            world_;
    std::vector<std::string>         lines_;
    std::vector<std::pair<std::string, std::string>> vars_;
    std::vector<int>                 stack_;
    std::vector<If>                  ifs_;
    std::vector<Item>                items_;
    std::vector<std::string>         picks_;
    std::string                      picked_, askVar_, askLine_;
    size_t                           pc_ = 0;
    int                              steps_ = 0;
    int                              pending_ = -1;
    Wait                             wait_ = Wait::None;
    bool                             on_ = false, shown_ = false, skipMenu_ = false;
};

}  // namespace axis::bbs
