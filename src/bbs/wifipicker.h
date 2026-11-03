// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// Choosing a WiFi network on the touch panel: the networks in range (strongest first), a password
// on the on-screen keyboard, or a hidden network by name. Portable; the firmware scans, draws and
// joins (and saves the network only once the join works).
#pragma once
#include <string>
#include <vector>

#include "bbs/touchkeyboard.h"

namespace axis::bbs {

class WifiPicker {
public:
    struct Network {
        std::string ssid;
        int         rssi = 0;
        bool        secured = true;
    };
    struct Button {
        std::string label;
        int         x, y, w, h;
    };
    enum class Screen { List, Name, Password };
    static constexpr int kRowTop = 40, kRowH = 30, kRows = 5;

    void                     setNetworks(std::vector<Network> nets);  // after a scan
    Screen                   screen() const { return screen_; }
    std::vector<std::string> rows() const;  // the current page of networks
    std::vector<Button>      buttons() const;
    void                     tap(int x, int y);
    TouchKeyboard&           keyboard() { return kb_; }
    bool                     wantsJoin() const { return join_; }
    bool                     wantsRescan() const { return rescan_; }
    void                     rescanStarted() { rescan_ = false; }
    const std::string&       ssid() const { return ssid_; }
    const std::string&       password() const { return password_; }
    const std::string&       message() const { return message_; }
    void                     joining() { join_ = false; }  // the firmware has taken the request
    void                     joinFailed();
    void                     setCanLeave(bool can) { canLeave_ = can; }  // already online: offer BACK
    bool                     wantsLeave() const { return leave_; }

private:
    std::vector<Network> nets_;
    Screen               screen_ = Screen::List;
    TouchKeyboard        kb_{"", false};
    std::string          ssid_, password_, message_;
    bool                 join_ = false, rescan_ = false, canLeave_ = false, leave_ = false;
    int                  page_ = 0;
};

}  // namespace axis::bbs
