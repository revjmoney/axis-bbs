// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include "bbs/sysop_menu.h"

#include <cstdio>

#include "bbs/version.h"

namespace axis::bbs {

namespace {
const char* const kSections[] = {"BBS SETUP", "USERS", "FILES", "MESSAGES", "LOGS", "SYSTEM"};
const SysopMenu::Screen kSectionScreens[] = {SysopMenu::Screen::Setup, SysopMenu::Screen::Users, SysopMenu::Screen::Files,
                                             SysopMenu::Screen::Messages, SysopMenu::Screen::Logs, SysopMenu::Screen::System};

std::string row(const char* label, const std::string& value) {  // "label    value", cut to fit
    char b[80];
    std::snprintf(b, sizeof b, "%-9s%s", label, value.c_str());
    std::string s = b;
    if (s.size() > (size_t)SysopMenu::kLineChars) s.resize(SysopMenu::kLineChars);
    return s;
}

std::string upTime(uint32_t sec) {
    char b[32];
    std::snprintf(b, sizeof b, "%ud %uh %um", (unsigned)(sec / 86400), (unsigned)(sec / 3600 % 24), (unsigned)(sec / 60 % 60));
    return b;
}
}  // namespace

void SysopMenu::open() {
    open_ = true;
    go(Screen::Main);
}

void SysopMenu::go(Screen s) {
    screen_ = s;
    page_ = 0;
}

std::vector<SysopMenu::Button> SysopMenu::buttons() const {
    std::vector<Button> b;
    switch (screen_) {
        case Screen::Main:
            for (int i = 0; i < 6; i++) b.push_back({kSections[i], 8 + (i % 2) * 156, 36 + (i / 2) * 56, 148, 48});
            b.push_back({"EXIT", 8, 204, 100, 30});
            break;
        case Screen::ConfirmFormat:
            b.push_back({"YES, ERASE IT", 8, 150, 148, 48, true});
            b.push_back({"CANCEL", 164, 150, 148, 48});
            break;
        default:
            b.push_back({"BACK", 8, 204, 90, 30});
            if (screen_ == Screen::Files || screen_ == Screen::Logs) b.push_back({"MORE", 222, 204, 90, 30});
            if (screen_ == Screen::Setup) b.push_back({"RUN SETUP", 192, 204, 120, 30, true});
            if (screen_ == Screen::System) {
                b.push_back({"FORMAT SD", 106, 204, 100, 30, true});
                b.push_back({"RESTART", 214, 204, 98, 30});
            }
            break;
    }
    return b;
}

SysopMenu::Action SysopMenu::tap(int x, int y) {
    if (!open_) return Action::None;
    for (const Button& b : buttons()) {
        if (x < b.x || x >= b.x + b.w || y < b.y || y >= b.y + b.h) continue;
        if (screen_ == Screen::Main) {
            if (b.label == "EXIT") {
                open_ = false;
                return Action::Close;
            }
            for (int i = 0; i < 6; i++)
                if (b.label == kSections[i]) go(kSectionScreens[i]);
            return Action::None;
        }
        if (b.label == "BACK") go(Screen::Main);
        else if (b.label == "MORE") page_++;
        else if (b.label == "FORMAT SD") go(Screen::ConfirmFormat);
        else if (b.label == "CANCEL") go(Screen::System);
        else if (b.label == "YES, ERASE IT") {
            go(Screen::System);
            return Action::FormatSd;
        } else if (b.label == "RESTART") return Action::Restart;
        else if (b.label == "RUN SETUP") return Action::RerunSetup;
        return Action::None;
    }
    return Action::None;
}

std::string SysopMenu::title() const {
    switch (screen_) {
        case Screen::Main: return "SYSOP MENU";
        case Screen::Setup: return "BBS SETUP";
        case Screen::Users: return "USERS";
        case Screen::Files: return "FILE AREAS";
        case Screen::Messages: return "MESSAGES";
        case Screen::Logs: return "LOGS";
        case Screen::System: return "SYSTEM";
        case Screen::ConfirmFormat: return "FORMAT SD CARD?";
    }
    return "";
}

std::vector<std::string> SysopMenu::lines(const SysopData& d) const {
    std::vector<std::string> all;  // every line of the screen; one page is returned
    const BbsConfig&         c = d.cfg;
    switch (screen_) {
        case Screen::Main:
            break;
        case Screen::Setup:
            all.push_back(row("BBS", c.bbsName));
            all.push_back(row("sysop", c.sysopHandle + (c.sysopRealName.empty() ? "" : " (" + c.sysopRealName + ")")));
            all.push_back(row("from", c.location));
            all.push_back(row("dial", c.boardName + ".local:" + std::to_string(c.telnetPort)));
            all.push_back(row("time", c.timezone));
            all.push_back(row("FTN", c.ftnAddress));
            all.push_back(row("new user", "security " + std::to_string(c.newUserSecurity) + ", " +
                                              std::to_string(c.dailyMinutes) + " min/day"));
            all.push_back(row("idle", std::to_string(c.idleMinutes) + " min"));
            all.push_back(row("doors", std::to_string(c.doors.size()) + (c.doors.empty() ? "" : " (" + c.doors[0].name + ")")));
            all.push_back("RUN SETUP redoes it (over telnet/here)");
            break;
        case Screen::Users:
            all.push_back(row("sysop", c.sysopHandle + (c.sysopHash.empty() ? "" : ", password set")));
            all.push_back("");
            all.push_back("Caller accounts, security levels and");
            all.push_back("the user editor arrive with the BBS");
            all.push_back("core (accounts). Callers sign in by");
            all.push_back("handle for now.");
            break;
        case Screen::Files:
            for (size_t i = 0; i < c.areas.size(); i++) {
                int         n = i < d.areaFiles.size() ? d.areaFiles[i] : -1;
                std::string count = n < 0 ? "no SD" : std::to_string(n);
                std::string name = c.areas[i].name;
                if (name.size() > 30) name.resize(30);
                all.push_back(name + std::string(32 - name.size(), ' ') + count);
            }
            break;
        case Screen::Messages:
            all.push_back(row("network", "FidoNet style, " + c.ftnAddress));
            all.push_back("");
            all.push_back("Message areas, the JAM message base");
            all.push_back("and PKT import/export arrive with the");
            all.push_back("message base work.");
            break;
        case Screen::Logs:
            if (d.log.lines().empty()) all.push_back("(nothing yet)");
            for (auto& l : d.log.lines()) all.push_back(l.size() > (size_t)kLineChars ? l.substr(0, kLineChars) : l);
            break;
        case Screen::System:
            all.push_back(row("software", std::string(kAxisSoftware) + " " + kAxisVersion));
            all.push_back(row("hardware", d.info.hardware));
            all.push_back(row("storage", d.info.storage));
            all.push_back(row("free RAM", std::to_string(d.info.freeHeap) + " bytes"));
            all.push_back(row("up", upTime(d.info.uptimeSec)));
            all.push_back(row("calls", std::to_string(d.info.callsToday) + " today"));
            all.push_back(row("wifi", d.info.wifi + ", " + std::to_string(d.info.rssi) + " dBm"));
            all.push_back(row("address", d.info.ip + "  " + d.info.host));
            break;
        case Screen::ConfirmFormat:
            all.push_back("Erase the whole SD card and make a");
            all.push_back("fresh FAT32 volume? Doors, files and");
            all.push_back("logs on the card are gone for good.");
            break;
    }
    if (screen_ == Screen::Logs) {  // newest page first, MORE goes further back
        int pages = ((int)all.size() + kLinesPerPage - 1) / kLinesPerPage;
        int p = pages ? page_ % pages : 0;
        int end = (int)all.size() - p * kLinesPerPage, start = end - kLinesPerPage < 0 ? 0 : end - kLinesPerPage;
        return std::vector<std::string>(all.begin() + start, all.begin() + end);
    }
    int pages = ((int)all.size() + kLinesPerPage - 1) / kLinesPerPage;
    int p = pages ? page_ % pages : 0;
    size_t start = (size_t)p * kLinesPerPage, end = start + kLinesPerPage < all.size() ? start + kLinesPerPage : all.size();
    return std::vector<std::string>(all.begin() + start, all.begin() + end);
}

}  // namespace axis::bbs
