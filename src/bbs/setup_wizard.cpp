// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#pragma GCC optimize("Os")  // menus and screens: size over speed (the door CPU and transfers stay -O2)
#include "bbs/setup_wizard.h"

#include <cctype>
#include <cstdlib>

#include "bbs/password.h"

namespace axis::bbs {

namespace {
const char* const DIM = "\x1b[0;37m";
const char* const HI = "\x1b[1;37m";
const char* const CYAN = "\x1b[0;33m";
const char* const RED = "\x1b[0;33m";
const char* const AMBER = "\x1b[0;33m";

bool allDigits(const std::string& s) {
    if (s.empty()) return false;
    for (char c : s)
        if (!std::isdigit((unsigned char)c)) return false;
    return true;
}
std::string inRange(const std::string& v, int lo, int hi) {
    if (!allDigits(v) || v.size() > 6 || std::atoi(v.c_str()) < lo || std::atoi(v.c_str()) > hi)
        return "Enter a number from " + std::to_string(lo) + " to " + std::to_string(hi) + ".";
    return "";
}
// zone:net/node or zone:net/node.point, all numbers
bool ftnOk(const std::string& s) {
    size_t c = s.find(':'), sl = s.find('/');
    if (c == std::string::npos || sl == std::string::npos || sl < c) return false;
    size_t dot = s.find('.', sl);
    std::string node = s.substr(sl + 1, dot == std::string::npos ? std::string::npos : dot - sl - 1);
    return allDigits(s.substr(0, c)) && allDigits(s.substr(c + 1, sl - c - 1)) && allDigits(node) &&
           (dot == std::string::npos || allDigits(s.substr(dot + 1)));
}
}  // namespace

SetupWizard::SetupWizard(BbsConfig start, uint32_t (*rng)()) : cfg_(std::move(start)), rng_(rng) {
    cfg_.setupDone = false;
}

const std::vector<SetupWizard::Field>& SetupWizard::fields() const {
    static const std::vector<Field> f = {
        {"sysop_handle", "Sysop handle", "the name you log in with", false, false},
        {"sysop_password", "Sysop password", "at least 4 characters", true, false},
        {"sysop_password2", "Password again", "", true, false},
        {"sysop_real_name", "Your real name", "optional, sysop only", false, true},
        {"bbs_name", "BBS name", "shown to every caller", false, false},
        {"location", "Location", "city, state", false, true},
        {"board_name", "Board name on the LAN", "callers dial <name>.local", false, false},
        {"public_host", "Public hostname", "optional, for later internet access", false, true},
        {"noip_host", "No-IP update hostname", "optional: your host, or all.ddnskey.com for a DDNS key", false, true},
        {"noip_user", "No-IP username", "your noip.com login or DDNS key username", false, true},
        {"noip_pass", "No-IP password", "your noip.com password or DDNS key password", true, true},
        {"telnet_port", "Telnet port", "23 is standard", false, false},
        {"timezone", "Timezone", "number from the list", false, false},
        {"ftn_address", "FidoNet address", "99:1/0 is a private test zone", false, false},
        {"new_user_security", "New user security level", "1 to 255", false, false},
        {"daily_minutes", "Daily minutes per caller", "1 to 1440", false, false},
        {"idle_minutes", "Idle logoff, minutes", "1 to 60", false, false},
        {"nup", "New user password", "optional: callers must know it to apply (Enter keeps the current one)", true, true},
    };
    return f;
}

std::string SetupWizard::defaultFor(const std::string& k) const {
    if (k == "sysop_handle") return cfg_.sysopHandle;
    if (k == "sysop_real_name") return cfg_.sysopRealName;
    if (k == "bbs_name") return cfg_.bbsName;
    if (k == "location") return cfg_.location;
    if (k == "board_name") return cfg_.boardName;
    if (k == "public_host") return cfg_.publicHost;
    if (k == "telnet_port") return std::to_string(cfg_.telnetPort);
    if (k == "timezone") return cfg_.timezone;
    if (k == "ftn_address") return cfg_.ftnAddress;
    if (k == "new_user_security") return std::to_string(cfg_.newUserSecurity);
    if (k == "daily_minutes") return std::to_string(cfg_.dailyMinutes);
    if (k == "idle_minutes") return std::to_string(cfg_.idleMinutes);
    return "";
}

std::string SetupWizard::validate(const std::string& k, const std::string& v) const {
    if (k == "sysop_handle") {
        if (v.empty()) return "The sysop needs a handle.";
        if (v.size() > 30) return "Keep it to 30 characters.";
    } else if (k == "sysop_password") {
        if (v.size() < 4) return "Use at least 4 characters.";
    } else if (k == "bbs_name") {
        if (v.empty()) return "Your BBS needs a name.";
        if (v.size() > 40) return "Keep it to 40 characters.";
    } else if (k == "board_name") {
        if (v.empty() || v.size() > 30) return "Use 1 to 30 characters.";
        for (char c : v)
            if (!(std::islower((unsigned char)c) || std::isdigit((unsigned char)c) || c == '-'))
                return "Use lowercase letters, digits and dashes only (it becomes <name>.local).";
    } else if (k == "telnet_port") {
        return inRange(v, 1, 65535);
    } else if (k == "timezone") {
        const auto& names = tzNames();
        if (allDigits(v) && std::atoi(v.c_str()) >= 1 && std::atoi(v.c_str()) <= (int)names.size()) return "";
        for (auto& n : names)
            if (n == v) return "";
        return "Pick a number from the list.";
    } else if (k == "ftn_address") {
        if (!ftnOk(v)) return "Use zone:net/node, like 99:1/0.";
    } else if (k == "new_user_security") {
        return inRange(v, 1, 255);
    } else if (k == "daily_minutes") {
        return inRange(v, 1, 1440);
    } else if (k == "idle_minutes") {
        return inRange(v, 1, 60);
    } else if (v.size() > 60) {
        return "Keep it to 60 characters.";
    }
    return "";
}

void SetupWizard::set(const std::string& k, const std::string& v) {
    int n = std::atoi(v.c_str());
    if (k == "sysop_handle") cfg_.sysopHandle = v;
    else if (k == "sysop_real_name") cfg_.sysopRealName = v;
    else if (k == "bbs_name") cfg_.bbsName = v;
    else if (k == "location") cfg_.location = v;
    else if (k == "board_name") cfg_.boardName = v;
    else if (k == "public_host") cfg_.publicHost = v;
    else if (k == "noip_host") {
        noip_.host = v;
        // callers dial the No-IP name, unless it is a DDNS key's "update every host" name
        bool keyName = v.size() >= 11 && v.compare(v.size() - 11, 11, "ddnskey.com") == 0;
        if (cfg_.publicHost.empty() && !keyName) cfg_.publicHost = v;
    } else if (k == "noip_user") noip_.user = v;
    else if (k == "noip_pass") noip_.pass = v;
    else if (k == "telnet_port") cfg_.telnetPort = n;
    else if (k == "timezone") cfg_.timezone = allDigits(v) ? tzNames()[n - 1] : v;
    else if (k == "ftn_address") cfg_.ftnAddress = v;
    else if (k == "new_user_security") cfg_.newUserSecurity = n;
    else if (k == "daily_minutes") cfg_.dailyMinutes = n;
    else if (k == "idle_minutes") cfg_.idleMinutes = n;
    else if (k == "nup" && !v.empty()) {  // Enter keeps whatever NUP there was
        cfg_.nupSalt = makeSalt(rng_);
        cfg_.nupHash = hashPassword(cfg_.nupSalt, v);
    }
}

void SetupWizard::setPassword(const std::string& password) {
    cfg_.sysopSalt = makeSalt(rng_);
    cfg_.sysopHash = hashPassword(cfg_.sysopSalt, password);
}

void SetupWizard::confirm(bool save) {
    if (save) {
        cfg_.setupDone = true;
        finished_ = true;
    } else {
        cancelled_ = true;
    }
}

void SetupWizard::begin() {
    out_ += "\x1b[0m\x1b[2J\x1b[H\r\n   ";
    out_ += RED;
    out_ += "AXIS UNDERGROUND CNC";
    out_ += DIM;
    out_ += "  first-boot setup\r\n\r\n";
    out_ += "   Answer each question. Press Enter to keep the value in [brackets].\r\n";
    out_ += "   Everything can be changed later from the sysop menu.\r\n";
    ask();
}

void SetupWizard::ask() {
    const Field& f = fields()[step_];
    if (f.key == "timezone") {
        out_ += "\r\n";
        const auto& names = tzNames();
        for (size_t i = 0; i < names.size(); i++)
            out_ += std::string("   ") + DIM + std::to_string(i + 1) + ". " + names[i] + "\r\n";
    }
    std::string def = f.secret ? "" : defaultFor(f.key);
    out_ += "\r\n   ";
    out_ += CYAN;
    out_ += f.label;
    if (!def.empty()) out_ += std::string(DIM) + " [" + def + "]";
    else if (!f.hint.empty()) out_ += std::string(DIM) + " (" + f.hint + ")";
    out_ += std::string(CYAN) + ": " + HI;
}

void SetupWizard::submit() {
    const Field& f = fields()[step_];
    std::string v = line_;
    line_.clear();
    if (v.empty() && !f.secret) v = defaultFor(f.key);
    if (f.key == "sysop_password2") {
        if (v != pendingPassword_) {
            out_ += std::string("\r\n   ") + AMBER + "Passwords did not match, try again.";
            pendingPassword_.clear();
            step_--;  // back to the first password question
            ask();
            return;
        }
        setPassword(v);
        pendingPassword_.clear();
    } else {
        std::string why = (v.empty() && f.optional) ? "" : validate(f.key, v);
        if (!why.empty()) {
            out_ += std::string("\r\n   ") + AMBER + why;
            ask();
            return;
        }
        if (f.key == "sysop_password") pendingPassword_ = v;
        else set(f.key, v);
    }
    if (++step_ < fields().size()) ask();
    else summary();
}

void SetupWizard::summary() {
    atSummary_ = true;
    out_ += std::string("\r\n\r\n   ") + HI + "Your new BBS\r\n";
    auto row = [&](const char* label, const std::string& v) {
        out_ += std::string("   ") + DIM + label + HI + v + "\r\n";
    };
    row("Sysop       ", cfg_.sysopHandle + (cfg_.sysopRealName.empty() ? "" : " (" + cfg_.sysopRealName + ")"));
    row("Password    ", "********");
    row("BBS name    ", cfg_.bbsName);
    row("Location    ", cfg_.location);
    row("Callers dial ", cfg_.boardName + ".local:" + std::to_string(cfg_.telnetPort) +
                             (cfg_.publicHost.empty() ? "" : "  or " + cfg_.publicHost));
    if (!noip_.host.empty()) row("Dynamic DNS ", noip_.host + " (No-IP, " + (noip_.user.empty() ? "no login yet" : noip_.user) + ")");
    row("Timezone    ", cfg_.timezone);
    row("FidoNet     ", cfg_.ftnAddress);
    row("New users   ", "security " + std::to_string(cfg_.newUserSecurity) + ", " +
                           std::to_string(cfg_.dailyMinutes) + " min/day, idle logoff " +
                           std::to_string(cfg_.idleMinutes) + " min");
    out_ += std::string("\r\n   ") + CYAN + "Save these settings? (Y/N): " + HI;
}

void SetupWizard::input(const uint8_t* p, size_t n) {
    for (size_t i = 0; i < n && !finished_ && !cancelled_; i++) {
        char c = (char)p[i];
        if (atSummary_) {
            if ((c | 0x20) == 'y') {
                out_ += "Y\r\n";
                confirm(true);
            } else if ((c | 0x20) == 'n') {
                out_ += "N\r\n";
                confirm(false);
            }
            continue;
        }
        const Field& f = fields()[step_];
        if (c == '\r') {
            submit();
        } else if (c == '\b' || c == 0x7f) {
            if (!line_.empty()) {
                line_.pop_back();
                out_ += "\b \b";
            }
        } else if ((unsigned char)c >= 0x20 && (unsigned char)c < 0x7f && line_.size() < 60) {
            line_ += c;
            out_ += f.secret ? '*' : c;
        }
    }
}

std::string SetupWizard::takeOutput() {
    std::string s;
    s.swap(out_);
    return s;
}

}  // namespace axis::bbs
