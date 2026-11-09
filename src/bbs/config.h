// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// BBS settings from the first-boot setup, stored as key=value lines (axis.ini).
#pragma once
#include <string>
#include <utility>
#include <vector>

namespace axis::bbs {

// A door on the Doors menu. Its files live in /doors/<id>/ on the board's storage.
struct DoorEntry {
    std::string id, name, exe, args, drop = "doorsys";  // drop: "doorsys" (DOOR.SYS) or "dorinfo"
    bool        enabled = true;  // sixth field of doorN ("|0" hides it). A saved LORD.EXE with no field is off.
};

// LORD.EXE, any mix of case. LORD Fast is FAST.EXE and is not this.
inline bool lordGameExe(const std::string& exe) {
    if (exe.size() != 8) return false;
    for (size_t i = 0; i < 8; i++)
        if ((exe[i] | 0x20) != "lord.exe"[i]) return false;
    return true;
}

// A file area. Its files live in /files/<id>/ on the SD card.
struct FileArea;
std::string areaText(const FileArea& a);  // "id|name", "id|name|level" or "id|name|level|m", as the settings keep it

struct FileArea {
    std::string id, name;
    int         minSecurity = 0;  // callers below this level don't see the area ("areaN=id|name|level")
    bool        mirror = false;   // "areaN=id|name|level|m": every file kept on two SD cards (cluster/mirrorjob.h)
};

std::vector<FileArea> defaultFileAreas();  // Rev's list (design spec, file areas table)

// One item of an editable menu: the key callers press, what it's called, the BBS command it runs.
struct MenuItem {
    std::string key, name, cmd;
};
// The commands a menu item can run, with the board's own key for each ("messages" -> 'M').
const std::vector<std::pair<std::string, char>>& menuCommands();
char menuCommandKey(const std::string& cmd);  // 0 if there's no such command

struct BbsConfig {
    std::string sysopHandle, sysopSalt, sysopHash, sysopRealName;
    std::string sysopLocation, sysopEmail, sysopTagline;  // the sysop's own account ([U] while logged in); the tagline signs their posts
    std::string dosSalt, dosHash;  // old DOS Prompt password keys; still parsed so a saved ini loads. The command is gone.
    std::string nupSalt, nupHash;  // NUP: the new user password asked before Apply ("" = none; wizard, @NUP)
    std::string bbsName = "Axis Underground CNC", location, boardName = "axis", publicHost;
    int         telnetPort = 23;
    std::string timezone = "EASTERN";
    std::string ftnAddress = "99:1/0";
    int         newUserSecurity = 10, dailyMinutes = 60, idleMinutes = 5;
    int         sessions = 2;
    int         nuvYes = 3, nuvNo = 3, nuvLevel = 20;  // new user voting: yes votes in, no votes out (0 = sysop only), who votes ("nuv_yes", "nuv_no", "nuv_level")
    int         ratio = 0, freeKb = 0;  // downloads up to ratio x uploads + freeKb ("ratio", "free_kb"; 0 = no ratio)
    std::string theme = "axis";  // the one look ("theme"): axis, cyan, green, or a card screenset id
    std::string wfc;             // the waiting screen skin: a look id, or blank to follow the theme. Use wfcFolder().
    bool        loginSplash = false;  // Rev's RotatoCAM ANSI splash before the login screen ("login_splash", on unless 0)  // callers this board takes at once (1 to 4). Each can be in LORD, a movie, a pack, or a transfer. A deflated piece or a ZIP check needs its own inflate window and is refused when the heap is short.
    bool        sysopTelnet = true;  // "sysop_telnet": the sysop menu may be opened over telnet. Off: only the board's own screen and serial.
    bool        messagesOn = true, filesOn = true, doorsOn = true;  // board switches ("messages_on", "files_on", "doors_on"); on unless 0. A caller's own access is separate.
    bool        setupDone = false;
    // "doorN=id|name|exe|args|drop"; LORD unless the settings list doors
    // an exe starting with '*' is a door built into the firmware ("*ANSIVIEW": args = the area it shows)
    std::vector<DoorEntry> doors{{"lord", "Legend of the Red Dragon", "LORD.EXE", "%N NOEMS /DREW", "doorsys"},
                                 {"blackjack", "BBS Black Jack", "BLKJK.EXE", "BJ.CFG", "doorsys"},
                                 {"ansiview", "ANSI Pack Viewer", "*ANSIVIEW", "ansi", "none"},
                                 {"script", "Scripts", "*SCRIPT", "", "none"}};
    // "*SCRIPT": programs in /scripts on the card. A saved list that lacks it gets this row. *DOSSHELL is dropped.
    std::vector<FileArea>  areas = defaultFileAreas();
    std::vector<FileArea>  msgAreas{{"general", "General Chatter"}, {"cnc", "CNC Shop Talk"}, {"rotatocam", "RotatoCAM"},
                                   {"routers", "Routers & Gantries"}, {"mills", "Mills & Lathes"},
                                   {"rotary", "4th & 5th Axis"}, {"cam", "CAM & G-code"}, {"controls", "Controllers & Electronics"},
                                   {"tooling", "Tooling, Feeds & Speeds"}, {"workholding", "Workholding & Fixtures"},
                                   {"lasers", "Lasers, Plasma & 3D Printers"}, {"showtell", "Show & Tell"}, {"crashes", "Crashes & Fails"},
                                   {"trade", "Buy, Sell & Trade"}, {"scene", "BBS Scene & ANSI"}, {"sysop", "Ask the Sysop"}};  // "msgareaN=id|name"  // "areaN=id|name"; the defaults unless listed
    // the main menu as data ("menu.main.N=hotkey|name|command"); empty = the built-in menu
    std::vector<MenuItem> mainMenu;
    std::vector<std::pair<std::string, std::string>> unknown;  // keys from newer firmware, kept on save
};

BbsConfig   parseConfig(const std::string& ini);
std::string formatConfig(const BbsConfig& c);
std::string wfcFolder(const BbsConfig& c);  // c.wfc when it is a known look id, else c.theme
// A cluster follower's settings: the main's (one BBS), keeping this board's own name, session count
// and telnet port; set up, since the main was.
BbsConfig   mergeSettings(const BbsConfig& mine, const std::string& mainIni);
// Stock LORD tails from before NOEMS ("1 /DREW", "%N /DREW"). Other doors and any custom LORD
// tail are left as the sysop wrote them. True when args changed.
bool        migrateLordArgs(DoorEntry& d);
const char* tzRule(const std::string& name);  // POSIX TZ rule, "" if unknown
const std::vector<std::string>& tzNames();

}  // namespace axis::bbs
