// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include "app/commands.h"

#include <algorithm>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <utility>

#include "app/services.h"
#include "app/session.h"
#include "bbs/config.h"
#include "bbs/features.h"
#include "bbs/filelib.h"
#include "bbs/msgbase.h"
#include "bbs/node.h"
#include "bbs/password.h"
#include "bbs/users.h"
#include "bbs/votebooth.h"
#include "door/hostfs.h"
#include "platform/posix/posixhostfs.h"

// Size over speed, as main.cpp (after the includes: the door headers' inline functions keep -O2).
#pragma GCC optimize("Os")

namespace axis::app {

void Out::printf(const char* fmt, ...) {  // as Arduino's Print::printf: a small buffer, a bigger one when the line needs it
    char    loc[64];
    va_list ap, copy;
    va_start(ap, fmt);
    va_copy(copy, ap);
    int len = std::vsnprintf(loc, sizeof loc, fmt, copy);
    va_end(copy);
    if (len >= 0 && (size_t)len < sizeof loc) write(loc, (size_t)len);
    else if (len >= 0) {
        std::string big((size_t)len + 1, '\0');
        std::vsnprintf(&big[0], big.size(), fmt, ap);
        write(big.data(), (size_t)len);
    }
    va_end(ap);
}

namespace {

// Arduino String's edge cases (the board's commands were written against them): an index past the end finds nothing,
// substring swaps a backward pair and stops at the end, and a -1 from indexOf arrives as a huge unsigned.
int indexOf(const std::string& s, char c, unsigned from = 0) {
    if (from >= s.size()) return -1;
    size_t at = s.find(c, from);
    return at == std::string::npos ? -1 : (int)at;
}
int lastIndexOf(const std::string& s, char c, unsigned from) {
    if (from >= s.size()) return -1;
    size_t at = s.rfind(c, from);
    return at == std::string::npos ? -1 : (int)at;
}
int lastIndexOf(const std::string& s, char c) { return lastIndexOf(s, c, (unsigned)s.size() - 1); }
std::string sub(const std::string& s, unsigned left, unsigned right) {
    if (left > right) std::swap(left, right);
    if (left >= s.size()) return std::string();
    if (right > s.size()) right = (unsigned)s.size();
    return s.substr(left, right - left);
}
std::string sub(const std::string& s, unsigned left) { return sub(s, left, (unsigned)s.size()); }
long toInt(const std::string& s) { return std::strtol(s.c_str(), nullptr, 10); }
bool starts(const std::string& s, const char* p) { return s.rfind(p, 0) == 0; }
int constrain(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

int numberOf(Services& s, int slot) { return s.nodeNumber ? s.nodeNumber(slot) : slot + 1; }  // the node a slot answers as

bool run(const std::string& cmd, Services& s, std::vector<Session*>& sessions, Out& out, bool pagedRun);

// "@USERS 40", "@SETTINGS 40", "@FILES area|40", "@POSTS area|40": the list from line 40, about 4 KB of it, then
// "more <next>" while lines remain. A page is whole lines, and with its marker it stays under sysopRun's 6 KB cap (one
// line too long is cut).
struct Page : Out {
    const size_t kRoom = 6 * 1024 - 64;
    Out&         to;
    size_t       skip, line = 0, sent = 0, next = 0;  // next: where the next page starts (0: none)
    bool         drop = false;                       // the rest of a cut line
    std::string  held;                               // the line so far: sent whole, or left for the next page
    Page(Out& o, size_t s) : to(o), skip(s) {}
    void write(const char* p, size_t n) override {
        for (size_t i = 0; i < n; i++) put((uint8_t)p[i]);
    }
    void put(uint8_t b) {
        if (next) return;
        if (line < skip || drop) {
            if (b == '\n') line++, drop = false;
            return;
        }
        if (held.empty() && sent >= 4096) {
            next = line;
            return;
        }
        held += (char)b;
        bool end = b == '\n';
        if (!end && held.size() < kRoom) return;
        if (sent && sent + held.size() > kRoom) {  // it starts the next page
            next = line;
            return;
        }
        if (!end) held += '\n', drop = true;  // longer than a page: cut
        to.write(held.data(), held.size());
        sent += held.size();
        held.clear();
        if (end) line++;
    }
};

bool run(const std::string& cmd, Services& s, std::vector<Session*>& sessions, Out& out, bool pagedRun) {
    // Without the number: the whole list, as always.
    if (!pagedRun && (starts(cmd, "@USERS ") || starts(cmd, "@SETTINGS ") || starts(cmd, "@FILES ") || starts(cmd, "@POSTS "))) {
        int         at = std::max(lastIndexOf(cmd, ' '), lastIndexOf(cmd, '|'));
        std::string base = sub(cmd, 0, (unsigned)at), num = sub(cmd, (unsigned)at + 1);
        if (std::to_string(toInt(num)) == num && (base == "@USERS" || base == "@SETTINGS" || (cmd[(size_t)at] == '|' && base.length() > 7 && (cmd[1] == 'F' || cmd[1] == 'P')))) {
            Page page(out, (size_t)toInt(num));
            run(base, s, sessions, page, true);  // under the pager: @POSTS sends the newest first
            if (!page.next && !page.held.empty()) out.write(page.held.data(), page.held.size());
            if (page.next) out.printf("more %u\n", (unsigned)page.next);
            return true;
        }
    }
    const bool cfgw = s.cfgEdit && s.cfgReplace && s.cfgSave;  // the settings can be changed here
    auto       share = [&s] {
        if (s.shareSettings) s.shareSettings();
    };
#if !AXIS_FILES
    if (cmd == "@AREAS" || starts(cmd, "@FILES ") || starts(cmd, "@FDESC ") || starts(cmd, "@FREN ") || starts(cmd, "@FDEL ") ||
        starts(cmd, "@FMV ") ||
        ((starts(cmd, "@AREASET ") || starts(cmd, "@AREADEL ") || starts(cmd, "@AREAUP ")) && !starts(sub(cmd, (unsigned)indexOf(cmd, ' ') + 1), "msgs|"))) {
        out.println("axis: the file areas are not in this firmware");
    } else
#endif
    if (starts(cmd, "@KICK ") || starts(cmd, "@ADDTIME ") || starts(cmd, "@FORCETC ") || starts(cmd, "@NOTE ")) {
        // a node on this board ("@KICK 3", "@ADDTIME 3 15", "@FORCETC 3", "@NOTE 3 text"); the panel sends
        // each to the board holding that node
        std::string rest = sub(cmd, (unsigned)indexOf(cmd, ' ') + 1);
        int         sp = indexOf(rest, ' '), num = (int)toInt(rest), slot = -1;
        std::string arg = sp < 0 ? std::string() : sub(rest, (unsigned)sp + 1);
        for (size_t i = 0; i < sessions.size(); i++)
            if (numberOf(s, (int)i) == num) slot = (int)i;
        Session*   ses = slot >= 0 ? sessions[(size_t)slot] : nullptr;
        bbs::Node* n = ses ? ses->node.get() : nullptr;
        if (!n || !ses->line) out.printf("axis: nobody on node %d here\n", num);
        else if (starts(cmd, "@KICK ")) {
            s.host.event("sysop: kicked " + n->handle());
            ses->line->hangUp();  // (the line only: the session's turn sees the carrier gone and cleans up)
            out.printf("axis: kicked node %d\n", num);
        } else if (starts(cmd, "@ADDTIME ")) {
            n->addMinutes((int)toInt(arg));
            s.host.event("sysop: " + arg + " more minutes for " + n->handle());
            out.printf("axis: node %d gets %d more minutes\n", num, (int)toInt(arg));
        } else if (starts(cmd, "@FORCETC ")) {
            n->nodeIn(bbs::Node::kNodeForce, 0, s.cfg->sysopHandle, "");
            out.printf("axis: node %d sent to the teleconference\n", num);
        } else {
            n->nodeIn(bbs::Node::kNodeNote, 0, s.cfg->sysopHandle, arg);
            out.printf("axis: note sent to node %d\n", num);
        }
    } else if (starts(cmd, "@BCAST ")) {  // "@BCAST text": a broadcast to every caller on this board
        for (Session* ses : sessions)
            if (ses && ses->node && ses->line) ses->node->nodeIn(bbs::Node::kNodeBroadcast, 0, s.cfg->sysopHandle, sub(cmd, 7));
        out.println("axis: broadcast sent");
    } else if (cmd == "@USERS") {  // one line per user: id|handle|status|security|calls|flags
        for (auto& u : s.users.all())
            out.printf("user %d|%s|%s|%d|%u|%s%s%s%s%s\n", u.id, u.handle.c_str(),
                       u.status == bbs::User::Status::Pending ? "pending" : u.status == bbs::User::Status::Denied ? "denied" : "valid",
                       u.security, (unsigned)u.calls, u.permFiles ? "F" : "-", u.permMessages ? "M" : "-", u.permDoors ? "D" : "-",
                       u.freeDownloads ? "$" : "-", u.twit ? "T" : "-");
    } else if (starts(cmd, "@USER ")) {  // a user's details and application answers
        bbs::User* u = s.users.find(sub(cmd, 6));
        if (!u) out.println("axis: no user by that handle");
        else {
            out.printf("handle %s\nreal %s\nfrom %s\nemail %s\ntagline %s\nsecurity %d\ncalls %u\nup %u\ndown %u\n", u->handle.c_str(),
                       u->realName.c_str(), u->location.c_str(), u->email.c_str(), u->tagline.c_str(), u->security, (unsigned)u->calls,
                       (unsigned)u->uploads, (unsigned)u->downloads);
            for (auto& a : u->answers) out.printf("answer %s|%s\n", a.first.c_str(), a.second.c_str());
        }
    } else if (starts(cmd, "@VALIDATE ") || starts(cmd, "@DENY ") || starts(cmd, "@SEC ") || starts(cmd, "@PERM ") ||
               starts(cmd, "@TWIT ")) {
        // "@VALIDATE h", "@DENY h", "@SEC h 30", "@PERM h files|msgs|doors|freedl|nets on|off", "@TWIT h on|off"
        std::string rest = sub(cmd, (unsigned)indexOf(cmd, ' ') + 1), handle = rest, a1, a2;
        int         sp = indexOf(rest, ' ');
        if (sp > 0 && !starts(cmd, "@VALIDATE ") && !starts(cmd, "@DENY ")) {
            int last = lastIndexOf(rest, ' ');
            if (starts(cmd, "@PERM ")) {
                int mid = lastIndexOf(rest, ' ', (unsigned)last - 1);
                handle = sub(rest, 0, (unsigned)mid), a1 = sub(rest, (unsigned)mid + 1, (unsigned)last), a2 = sub(rest, (unsigned)last + 1);
            } else {
                handle = sub(rest, 0, (unsigned)last), a1 = sub(rest, (unsigned)last + 1);
            }
        }
        bbs::User* u = s.users.find(handle);
        if (!u) out.println("axis: no user by that handle");
        else {
            bool on = a2 == "on" || a1 == "on";
            if (starts(cmd, "@VALIDATE ")) {
                u->status = bbs::User::Status::Validated;
                if (s.warrior) s.warrior(u->handle);  // (empty: no LORD in this build)
            }
            else if (starts(cmd, "@DENY ")) u->status = bbs::User::Status::Denied;
            else if (starts(cmd, "@SEC ")) u->security = constrain((int)toInt(a1), 1, 255);
            else if (starts(cmd, "@TWIT ")) u->twit = on;
            else if (a1 == "files") u->permFiles = on;
            else if (a1 == "msgs") u->permMessages = on;
            else if (a1 == "doors") u->permDoors = on;
            else if (a1 == "freedl") u->freeDownloads = on;
            else if (a1 == "nets") u->permNets = on;
            s.saveUsers();
            s.host.event("sysop: " + sub(cmd, 1, (unsigned)indexOf(cmd, ' ')) + " " + u->handle);
            out.printf("axis: %s updated\n", u->handle.c_str());
        }
    } else if (cmd == "@SETTINGS") {  // axis.ini without any salt or hash line
        std::string t = bbs::formatConfig(*s.cfg), line;
        for (char ch : t) {
            if (ch != '\n') {
                line += ch;
                continue;
            }
            if (line.find("_salt=") == std::string::npos && line.find("_hash=") == std::string::npos) out.println(line.c_str());
            line.clear();
        }
    } else if (starts(cmd, "@VOTEDEL ")) {  // "@VOTEDEL n": booth question n (1-based, as listed) and its ballots
        if (s.follower && s.follower()) {
            out.println("@VOTEDEL the booth lives on the main: run it there");
        } else {
            std::vector<std::string> v = s.data.listLoad("votes"), b = s.data.listLoad("ballots");
            int                      q = (int)toInt(sub(cmd, 9));
            bool                     ok = bbs::voteDelete(v, b, q);
            if (ok) {
                s.data.listSave("votes", v);
                s.data.listSave("ballots", b);
                s.host.event("voting booth: question " + std::to_string(q) + " taken out");
            }
            out.println(ok ? "@VOTEDEL OK" : "@VOTEDEL no such question");
        }
    } else if (cmd == "@RUMORS" || cmd == "@BBSLIST" || cmd == "@VOTES" || cmd == "@LINERS") {
        std::vector<std::string> v = cmd == "@LINERS" ? s.data.oneLiners() : s.data.sharedList(cmd == "@RUMORS" ? "rumors" : cmd == "@BBSLIST" ? "bbslist" : "votes");
        for (auto& l : v) out.println(l.c_str());
    } else if (starts(cmd, "@UEDIT ")) {  // "@UEDIT handle|field|value": realname location email tagline security handle
        std::string a = sub(cmd, 7);
        int         b1 = indexOf(a, '|'), b2 = b1 < 0 ? -1 : indexOf(a, '|', (unsigned)b1 + 1);
        bbs::User*  u = b2 > 0 ? s.users.find(sub(a, 0, (unsigned)b1)) : nullptr;
        std::string f = b2 > 0 ? sub(a, (unsigned)b1 + 1, (unsigned)b2) : std::string(), v = b2 > 0 ? sub(a, (unsigned)b2 + 1) : std::string();
        if (!u) out.println("axis: @UEDIT handle|field|value");
        else {
            if (f == "realname") u->realName = v;
            else if (f == "location") u->location = v;
            else if (f == "email") u->email = v;
            else if (f == "tagline") u->tagline = v;
            else if (f == "security") u->security = constrain((int)toInt(v), 1, 255);
            else if (f == "handle" && v.length() && !s.users.find(v)) u->handle = v;
            else {
                out.println("axis: fields: realname location email tagline security handle");
                return true;
            }
            s.saveUsers();
            s.host.event("sysop: edited " + f + " of " + u->handle);
            out.printf("axis: %s updated\n", u->handle.c_str());
        }
    } else if (starts(cmd, "@UDEL ")) {  // "@UDEL handle": delete a caller's account
        bbs::User* u = s.users.find(sub(cmd, 6));
        if (!u) out.println("axis: no user by that handle");
        else {
            std::string h = u->handle;
            auto&       all = s.users.all();
            for (size_t i = 0; i < all.size(); i++)
                if (&all[i] == u) {
                    all.erase(all.begin() + (long)i);
                    break;
                }
            s.saveUsers();
            s.host.event("sysop: deleted user " + h);
            out.printf("axis: %s deleted\n", h.c_str());
        }
    } else if (cmd == "@AREAS" || cmd == "@MSGAREAS") {  // "N|id|name|level|m" per area
        auto& v = cmd == "@AREAS" ? s.cfg->areas : s.cfg->msgAreas;
        for (size_t i = 0; i < v.size(); i++)
            out.printf("%u|%s|%s|%d|%s\n", (unsigned)i + 1, v[i].id.c_str(), v[i].name.c_str(), v[i].minSecurity, v[i].mirror ? "m" : "");
    } else if (cfgw && (starts(cmd, "@AREASET ") || starts(cmd, "@AREADEL ") || starts(cmd, "@AREAUP "))) {
        // "@AREASET files|N|id|name|level" (N past the end adds), "@AREADEL msgs|N", "@AREAUP files|N" (moves it up one)
        std::string              a = sub(cmd, (unsigned)indexOf(cmd, ' ') + 1);
        std::vector<std::string> f;
        for (int from = 0;;) {
            int bar = indexOf(a, '|', (unsigned)from);
            f.push_back(bar < 0 ? sub(a, (unsigned)from) : sub(a, (unsigned)from, (unsigned)bar));
            if (bar < 0) break;
            from = bar + 1;
        }
        bbs::BbsConfig& e = s.cfgEdit();
        auto&           v = f[0] == "msgs" ? e.msgAreas : e.areas;
        int             n = f.size() > 1 ? (int)toInt(f[1]) : 0;
        bool            ok = n >= 1;
        if (ok && starts(cmd, "@AREASET ") && f.size() >= 4) {
            bbs::FileArea ar{f[2], f[3], f.size() > 4 ? (int)toInt(f[4]) : 0};
            if (n <= (int)v.size()) ar.mirror = v[(size_t)n - 1].mirror, v[(size_t)n - 1] = ar;
            else v.push_back(ar);
        } else if (ok && starts(cmd, "@AREADEL ") && n <= (int)v.size()) {
            v.erase(v.begin() + n - 1);
        } else if (ok && starts(cmd, "@AREAUP ") && n >= 2 && n <= (int)v.size()) {
            std::swap(v[(size_t)n - 1], v[(size_t)n - 2]);
        } else ok = false;
        if (!ok) out.println("axis: @AREASET files|N|id|name|level, @AREADEL msgs|N, @AREAUP files|N");
        else {
            s.cfgSave();
            share();
            if (s.areasChanged) s.areasChanged();
            s.host.event("sysop: " + sub(cmd, 1, (unsigned)indexOf(cmd, ' ')) + " " + a);
            out.println("axis: areas saved");
        }
#if AXIS_FILES
    } else if (starts(cmd, "@FILES ")) {  // "@FILES area": name|size|description per file
        if (s.files)
            for (auto& e : s.files().list(sub(cmd, 7))) out.printf("%s|%u|%s\n", e.name.c_str(), (unsigned)e.size, e.desc.c_str());
    } else if (starts(cmd, "@FDESC ") || starts(cmd, "@FREN ") || starts(cmd, "@FDEL ")) {
        // "@FDESC area|name|text", "@FREN area|old|new", "@FDEL area|name" (@FMV, a move on the board's card, is main.cpp's)
        std::string a = sub(cmd, (unsigned)indexOf(cmd, ' ') + 1);
        int         b1 = indexOf(a, '|'), b2 = b1 < 0 ? -1 : indexOf(a, '|', (unsigned)b1 + 1);
        std::string area = b1 > 0 ? sub(a, 0, (unsigned)b1) : "", name = b1 > 0 ? (b2 > 0 ? sub(a, (unsigned)b1 + 1, (unsigned)b2) : sub(a, (unsigned)b1 + 1)) : "",
                    third = b2 > 0 ? sub(a, (unsigned)b2 + 1) : "";
        bool ok = false;
        if (!s.files) ok = false;  // (no file areas)
        else if (starts(cmd, "@FDESC ")) ok = s.files().describe(area, name, third);
        else if (starts(cmd, "@FREN ")) ok = !third.empty() && s.files().rename(area, name, third);
        else ok = s.files().remove(area, name);
        s.host.event("sysop: " + sub(cmd, 1, (unsigned)indexOf(cmd, ' ')) + " " + area + " " + name + (ok ? "" : " (failed)"));
        out.println(ok ? "axis: done" : "axis: that didn't work");
#endif
    } else if (starts(cmd, "@POSTS ")) {  // "@POSTS area": num|from|subject|date per post (the newest 200; paged: newest first)
        if (s.messages) {
            auto v = s.messages().headers(sub(cmd, 7));
            if (pagedRun) std::reverse(v.begin(), v.end());
            for (auto& m : v) out.printf("%d|%s|%s|%s\n", m.num, m.from.c_str(), m.subject.c_str(), m.date.c_str());
        }
    } else if (starts(cmd, "@POSTDEL ") || starts(cmd, "@POSTMV ") || starts(cmd, "@PURGE ")) {
        // "@POSTDEL area|num", "@POSTMV area|num|toarea", "@PURGE area|days" (posts older than that)
        std::string a = sub(cmd, (unsigned)indexOf(cmd, ' ') + 1);
        int         b1 = indexOf(a, '|'), b2 = b1 < 0 ? -1 : indexOf(a, '|', (unsigned)b1 + 1);
        std::string area = b1 > 0 ? sub(a, 0, (unsigned)b1) : "";
        int         n = b1 > 0 ? (int)toInt(b2 > 0 ? sub(a, (unsigned)b1 + 1, (unsigned)b2) : sub(a, (unsigned)b1 + 1)) : 0;
        std::string to = b2 > 0 ? sub(a, (unsigned)b2 + 1) : "";
        int         gone = 0;
        if (!s.messages) {
            // (the message base is offline: nothing goes)
        } else if (starts(cmd, "@POSTMV ")) {
            bbs::Message m;
            if (!to.empty() && s.messages().read(area, n, m) && s.messages().post(to, m))
                gone = s.messages().drop(area, [n](const bbs::Message& x) { return x.num == n; });
        } else if (starts(cmd, "@POSTDEL ")) {
            gone = s.messages().drop(area, [n](const bbs::Message& x) { return x.num == n; });
        } else {
            time_t cut = time(nullptr) - (time_t)n * 86400;
            char   stamp[16];
            strftime(stamp, sizeof stamp, "%Y-%m-%d", localtime(&cut));
            std::string before = stamp;
            if (time(nullptr) > 1700000000 && n > 0)
                gone = s.messages().drop(area, [&](const bbs::Message& x) { return x.date.substr(0, 10) < before; });
        }
        s.host.event("sysop: " + sub(cmd, 1, (unsigned)indexOf(cmd, ' ')) + " " + a + " (" + std::to_string(gone) + ")");
        out.printf("axis: %d post%s\n", gone, gone == 1 ? "" : "s");
    } else if (cmd == "@MENU") {  // the main menu's items: N|key|name|command (empty: the built-in menu)
        for (size_t i = 0; i < s.cfg->mainMenu.size(); i++)
            out.printf("%u|%s|%s|%s\n", (unsigned)i + 1, s.cfg->mainMenu[i].key.c_str(), s.cfg->mainMenu[i].name.c_str(), s.cfg->mainMenu[i].cmd.c_str());
    } else if (cmd == "@MENUCMDS") {  // what a menu item can run
        for (auto& c : bbs::menuCommands()) out.printf("%s|%c\n", c.first.c_str(), c.second);
    } else if (cfgw && (starts(cmd, "@MENUSET ") || starts(cmd, "@MENUDEL ") || starts(cmd, "@MENUUP ") || cmd == "@MENURESET" || cmd == "@MENUSTART")) {
        // "@MENUSET N|key|name|command" (N past the end adds), "@MENUDEL N", "@MENUUP N", "@MENURESET" (built-in again),
        // "@MENUSTART" (an editable copy of the built-in menu)
        auto&       v = s.cfgEdit().mainMenu;
        std::string a = indexOf(cmd, ' ') > 0 ? sub(cmd, (unsigned)indexOf(cmd, ' ') + 1) : std::string();
        int         n = (int)toInt(a);
        bool        ok = true;
        if (cmd == "@MENURESET") v.clear();
        else if (cmd == "@MENUSTART") {
            static const char* const names[] = {"Message Bases", "File Areas", "Doors", "Teleconference", "Who's Online", "Page Sysop",
                                                "Your Account", "Oneliners", "MultiNode", "Last Callers", "Private Email", "Scan New Msgs",
                                                "Rumors", "BBS List", "Top Ten", "Voting Booth", "System Info", "Credits", "Goodbye", "Sysop Menu"};
            v.clear();
            size_t i = 0;
            for (auto& c : bbs::menuCommands()) v.push_back({std::string(1, c.second), names[i++ % 20], c.first});
        } else if (starts(cmd, "@MENUSET ")) {
            int b1 = indexOf(a, '|'), b2 = b1 < 0 ? -1 : indexOf(a, '|', (unsigned)b1 + 1), b3 = b2 < 0 ? -1 : indexOf(a, '|', (unsigned)b2 + 1);
            ok = n >= 1 && b3 > 0 && bbs::menuCommandKey(sub(a, (unsigned)b3 + 1));
            if (ok) {
                bbs::MenuItem m{sub(sub(a, (unsigned)b1 + 1, (unsigned)b2), 0, 1), sub(a, (unsigned)b2 + 1, (unsigned)b3), sub(a, (unsigned)b3 + 1)};
                if (n <= (int)v.size()) v[(size_t)n - 1] = m;
                else v.push_back(m);
            }
        } else if (starts(cmd, "@MENUDEL ")) {
            ok = n >= 1 && n <= (int)v.size();
            if (ok) v.erase(v.begin() + n - 1);
        } else {
            ok = n >= 2 && n <= (int)v.size();
            if (ok) std::swap(v[(size_t)n - 1], v[(size_t)n - 2]);
        }
        if (!ok) out.println("axis: @MENUSET N|key|name|command, @MENUDEL N, @MENUUP N, @MENURESET, @MENUSTART");
        else {
            s.cfgSave();
            share();
            s.host.event("sysop: " + sub(cmd, 1));
            out.println("axis: menu saved");
        }
    } else if (starts(cmd, "@SYSOP ")) {  // "@SYSOP handle": make a caller a sysop (validated, security 255)
        bbs::User* u = s.users.find(sub(cmd, 7));
        if (!u) out.println("axis: no user by that handle");
        else {
            u->security = bbs::kSysopSecurity;
            u->status = bbs::User::Status::Validated;
            s.host.event("sysop: " + u->handle + " is now a sysop (serial)");
            s.saveUsers();
            out.printf("axis: %s is now a sysop\n", u->handle.c_str());
        }
    } else if (starts(cmd, "@ADDUSER ")) {  // "@ADDUSER handle|location|password": a validated caller (security 20)
        std::string a = sub(cmd, 9);
        int         b1 = indexOf(a, '|'), b2 = b1 < 0 ? -1 : indexOf(a, '|', (unsigned)b1 + 1);
        if (b1 <= 0 || b2 < 0 || a.length() - b2 - 1 < 6) {
            out.println("axis: @ADDUSER handle|location|password (6+ characters)");
        } else if (s.users.find(sub(a, 0, (unsigned)b1))) {
            out.println("axis: that handle is taken");
        } else {
            bbs::User u;
            u.handle = sub(a, 0, (unsigned)b1);
            u.location = sub(a, (unsigned)b1 + 1, (unsigned)b2);
            u.salt = bbs::makeSalt(s.rng);
            u.hash = bbs::hashPassword(u.salt, sub(a, (unsigned)b2 + 1));
            u.status = bbs::User::Status::Validated;
            u.security = 20;
            u.created = s.unixNow ? s.unixNow() : s.host.unixNow();
            s.users.add(u);
            s.host.event("sysop: added " + u.handle + " (serial)");
            s.saveUsers();  // and on to every board
            if (s.warrior) s.warrior(u.handle);
            out.printf("axis: added %s\n", u.handle.c_str());
        }
    } else if (starts(cmd, "@DOORLS ")) {  // list a door's folder the way the door sees it
        std::string       dir = s.host.sdRoot() + "/doors/" + sub(cmd, 8);
        PosixHostFs       fs(dir);
        door::FindState   st;
        fs.find("*.*", 0x10, st);
        out.printf("axis: %s: %u entries\n", dir.c_str(), (unsigned)st.matches.size());
        for (auto& f : st.matches)
            out.printf("axis:   %-12s %8u%s\n", f.name83.c_str(), (unsigned)f.size, f.attr & 0x10 ? " <DIR>" : "");
    } else if (starts(cmd, "@DOORCHECK ")) {  // the flash cache belonged to the 8086 runtime, which is not linked
        out.println("axis: no 8086 in this firmware");
    } else if (cfgw && starts(cmd, "@SET ") && indexOf(cmd, '=') > 5) {  // "@SET location=Myrtle Beach, SC"
        std::string kv = sub(cmd, 5);
        s.cfgReplace(bbs::parseConfig(bbs::formatConfig(*s.cfg) + kv + "\n"));  // the later line wins
        bool ok = s.cfgSave();
        s.host.event("sysop: set " + sub(kv, 0, (unsigned)indexOf(kv, '=')) + (ok ? "" : " (SAVE FAILED)"));
        share();
        if (s.dirty) s.dirty();
    } else if (cfgw && starts(cmd, "@NUP")) {  // "@NUP secret": the new user password asked before Apply; alone, anyone can apply
        std::string     pw = cmd.length() > 5 ? sub(cmd, 5) : std::string();
        bbs::BbsConfig& e = s.cfgEdit();
        e.nupSalt = pw.length() ? bbs::makeSalt(s.rng) : std::string();
        e.nupHash = pw.length() ? bbs::hashPassword(e.nupSalt, pw) : std::string();
        bool ok = s.cfgSave();
        share();  // every board asks the same one
        s.host.event(std::string("sysop: new user password ") + (pw.length() ? "set" : "cleared") + (ok ? "" : " (SAVE FAILED)"));
    } else if (starts(cmd, "@RUMOR ")) {  // "@RUMOR text": add a rumor to the board's list (the main keeps them)
        s.data.sharedListAdd("rumors", sub(cmd, 7));
        out.println("axis: rumor added");
    } else if (starts(cmd, "@USERPASS ")) {  // "@USERPASS handle secret": the sysop sets a caller's password (4+ characters)
        std::string rest = sub(cmd, 10);
        int         sp = lastIndexOf(rest, ' ');
        bbs::User*  u = sp > 0 ? s.users.find(sub(rest, 0, (unsigned)sp)) : nullptr;
        std::string pw = sp > 0 ? sub(rest, (unsigned)sp + 1) : std::string();
        if (!u) out.println("axis: @USERPASS handle password");
        else if (pw.length() < 4) out.println("axis: a password needs 4 or more characters");
        else {
            u->salt = bbs::makeSalt(s.rng);
            u->hash = bbs::hashPassword(u->salt, pw);
            s.saveUsers();
            s.host.event("sysop: password changed for " + u->handle);
            out.printf("axis: password set for %s\n", u->handle.c_str());
        }
    } else if (cfgw && starts(cmd, "@SYSOPPASS ")) {  // "@SYSOPPASS secret" sets the sysop account's password (4+ characters, as the wizard)
        std::string pw = sub(cmd, 11);
        if (pw.length() < 4) {
            out.println("axis: @SYSOPPASS needs 4 or more characters");
        } else {
            bbs::BbsConfig& e = s.cfgEdit();
            e.sysopSalt = bbs::makeSalt(s.rng);
            e.sysopHash = bbs::hashPassword(e.sysopSalt, pw);
            bool ok = s.cfgSave();
            share();  // every board takes the same sysop login
            s.host.event(std::string("sysop: sysop password changed") + (ok ? "" : " (SAVE FAILED)"));
        }
    } else {
        return false;
    }
    return true;
}

}  // namespace

bool runPortableCommand(const std::string& cmd, Services& s, std::vector<Session*>& sessions, Out& out) {
    return run(cmd, s, sessions, out, false);
}

}  // namespace axis::app
