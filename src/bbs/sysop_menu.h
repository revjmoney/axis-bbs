// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The sysop menu on the board's touchscreen (a tap on the waiting-for-caller screen opens it):
// which screen is up, where its buttons are on the 320x240 panel, what a tap does, and the text
// each screen shows. Portable; the firmware only draws it and passes taps in.
#pragma once
#include <string>
#include <vector>

#include "bbs/config.h"
#include "bbs/eventlog.h"
#include "bbs/node.h"
#include "bbs/users.h"

namespace axis::bbs {

struct SysopData {  // what the screens show, gathered by the firmware when drawing
    BbsConfig        cfg;
    SysInfo          info;
    EventLog         log;
    std::vector<int> areaFiles;  // files in each of cfg.areas (-1 = no storage for files)
    UserStore        users;
};

class SysopMenu {
public:
    enum class Screen { Main, Setup, Users, UserDetail, Access, Files, Messages, Logs, System, ConfirmFormat, Theme, Station };
    enum class Action { None, Close, FormatSd, Restart, RerunSetup, ValidateUser, DenyUser, ToggleFiles, ToggleMessages, ToggleDoors, ToggleFreeDownloads, ToggleSysop, SetTheme, SetUserPassword, SetSysopPassword };
    struct Button {
        std::string label;
        int         x, y, w, h;
        bool        danger = false;  // drawn in red
    };
    static constexpr int kLinesPerPage = 10, kLineChars = 38;

    void                open();
    void                openAt(Screen s) { open(); go(s); }  // straight to a section (the WFC's menu)
    bool                isOpen() const { return open_; }
    Screen              screen() const { return screen_; }
    std::vector<Button> buttons() const;
    Action              tap(int x, int y);
    std::string         title() const;
    std::vector<std::string> lines(const SysopData& d) const;  // at most kLinesPerPage
    int selectedUser() const { return user_; }  // for ValidateUser / DenyUser
    const std::string& chosenTheme() const { return theme_; }  // after SetTheme: a look id

private:
    void   go(Screen s);
    bool   open_ = false;
    Screen screen_ = Screen::Main;
    int    page_ = 0;
    int    user_ = 0;
    std::string theme_;
    mutable std::vector<int> listed_;  // user ids on the Users page as last drawn, for row taps
};

}  // namespace axis::bbs
