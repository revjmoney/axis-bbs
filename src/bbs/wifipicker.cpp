// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#pragma GCC optimize("Os")  // menus and screens: size over speed (the door CPU and transfers stay -O2)
#include "bbs/wifipicker.h"

#include <algorithm>
#include <cstdio>

namespace axis::bbs {

void WifiPicker::setNetworks(std::vector<Network> nets) {
    std::sort(nets.begin(), nets.end(), [](const Network& a, const Network& b) { return a.rssi > b.rssi; });
    nets_.clear();
    for (auto& n : nets) {  // one row per name (several access points share one)
        if (n.ssid.empty()) continue;
        bool seen = false;
        for (auto& m : nets_) seen = seen || m.ssid == n.ssid;
        if (!seen) nets_.push_back(n);
    }
    page_ = 0;
}

std::vector<std::string> WifiPicker::rows() const {
    std::vector<std::string> r;
    for (size_t i = (size_t)page_ * kRows; i < nets_.size() && i < (size_t)(page_ + 1) * kRows; i++) {
        char b[64];
        std::string name = nets_[i].ssid.substr(0, 22);
        std::snprintf(b, sizeof b, "%-23s%4d dBm %s", name.c_str(), nets_[i].rssi, nets_[i].secured ? "" : "open");
        r.push_back(b);
    }
    return r;
}

std::vector<WifiPicker::Button> WifiPicker::buttons() const {
    if (screen_ != Screen::List) return {};
    if (canLeave_) return {{"BACK", 4, 204, 74, 30}, {"RESCAN", 82, 204, 76, 30}, {"HIDDEN", 162, 204, 76, 30}, {"MORE", 242, 204, 74, 30}};
    return {{"RESCAN", 8, 204, 96, 30}, {"HIDDEN", 112, 204, 96, 30}, {"MORE", 216, 204, 96, 30}};
}

void WifiPicker::joinFailed() {
    join_ = false;
    screen_ = Screen::List;
    message_ = "couldn't join " + ssid_ + ": check the password";
}

void WifiPicker::tap(int x, int y) {
    if (screen_ != Screen::List) {
        kb_.tap(x, y);
        if (kb_.cancelled()) {
            screen_ = Screen::List;
            return;
        }
        if (!kb_.done()) return;
        if (screen_ == Screen::Name) {
            if (kb_.text().empty()) {
                screen_ = Screen::List;
                return;
            }
            ssid_ = kb_.text();
            kb_.reset("Password for " + ssid_, true);
            screen_ = Screen::Password;
        } else {
            password_ = kb_.text();
            message_ = "joining " + ssid_ + "...";
            join_ = true;
            screen_ = Screen::List;
        }
        return;
    }
    for (const Button& b : buttons()) {
        if (x < b.x || x >= b.x + b.w || y < b.y || y >= b.y + b.h) continue;
        if (b.label == "BACK") leave_ = true;
        else if (b.label == "RESCAN") rescan_ = true;
        else if (b.label == "MORE") page_ = nets_.empty() ? 0 : (page_ + 1) % (int)((nets_.size() + kRows - 1) / kRows);
        else if (b.label == "HIDDEN") {
            kb_.reset("Hidden network name", false);
            screen_ = Screen::Name;
        }
        return;
    }
    if (y < kRowTop || y >= kRowTop + kRows * kRowH) return;
    size_t i = (size_t)page_ * kRows + (size_t)((y - kRowTop) / kRowH);
    if (i >= nets_.size()) return;
    ssid_ = nets_[i].ssid;
    password_.clear();
    if (!nets_[i].secured) {
        message_ = "joining " + ssid_ + "...";
        join_ = true;
        return;
    }
    kb_.reset("Password for " + ssid_, true);
    screen_ = Screen::Password;
}

}  // namespace axis::bbs
