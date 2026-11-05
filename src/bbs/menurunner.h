// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// ViSiON/3's menu loop on a menu file: auto-run commands ("//" once a call, "~~" on every load), clear,
// the menu's art, its lightbar, the prompt; then each key: a one-key command runs at once, a longer one
// ("/SYSOP") is typed and Enter runs it, the lightbar's arrows move and Enter picks its item. "G" (log off)
// and "/G" (hang up) work on every menu. What a command does is the caller's business: it gets an action.
#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <set>
#include <string>

#include "bbs/acs.h"
#include "bbs/lightbar.h"
#include "bbs/menufile.h"
#include "bbs/pipecodes.h"

namespace axis::bbs {

struct MenuAction {
    enum Kind { None, Goto, Run, Door, Logoff, Quit } kind = None;
    std::string arg, args;  // Goto: the menu; Run: the function and its arguments; Door: its id
};

class MenuRunner {
public:
    using Loader = std::function<std::string(const std::string& file)>;  // "MAIN.MNU", "MAIN.ANS": "" if none
    MenuRunner(Loader load, ArtEmit emit, ArtValue value);
    bool       enter(const std::string& name, const AcsUser& who);  // false: no such menu, or not allowed
    MenuAction key(char c, uint32_t nowMs = 0);                     // key(0): the next auto-run, if any
    void       tick(uint32_t nowMs);                                // a lone ESC becomes Esc after a pause
    void       redraw();                                            // the menu again (after a command's screen)
    void       prompt();                                            // just the prompt again
    const std::string& current() const { return name_; }
    const std::string& activity() const { return activity_; }
    const MenuDef&     def() const { return def_; }

private:
    MenuAction  run(const MenuCmd& c);
    MenuAction  typed(bool enter);
    void        emit(const std::string& s) { emit_(s.data(), s.size()); }
    Loader                    load_;
    ArtEmit                   emit_;
    ArtValue                  value_;
    MenuDef                   def_;
    AcsUser                   who_;
    std::string               name_, activity_, typed_;
    std::unique_ptr<Lightbar> bar_;
    std::vector<size_t>       autos_;  // commands to auto-run before input
    std::set<std::string>     ranOnce_;
    uint32_t                  lastKeyMs_ = 0;
    int                       esc_ = 0;  // inside a terminal key sequence that no lightbar takes
};

}  // namespace axis::bbs
