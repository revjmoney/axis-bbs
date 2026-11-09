// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#pragma GCC optimize("Os")  // menus and screens: size over speed (the door CPU and transfers stay -O2)
#include "bbs/usersettings.h"

#include "bbs/password.h"
#include "bbs/theme.h"

namespace axis::bbs {

void UserSettings::begin() {
    st_ = St::Menu;
    show();
}

void UserSettings::show() {
    auto kb = [](uint32_t b) { return std::to_string(b / 1024) + " KB"; };
    out_ += theme::CLS;
    out_ += "\r\n" + theme::drip("ACCOUNT");
    out_ += theme::window("YOUR ACCOUNT", {{"handle", u_.handle},
                                           {"real name", u_.realName},
                                           {"location", u_.location},
                                           {"email", u_.email},
                                           {"tagline", u_.tagline.empty() ? "(none: it signs your posts)" : u_.tagline},
                                           {"security", std::to_string(u_.security)},
                                           {"calls", std::to_string(u_.calls)},
                                           {"up / down", kb(u_.uploads) + " / " + kb(u_.downloads)}});
    out_ += "\r\n   " + theme::key("R", "eal name   ") + theme::key("L", "ocation   ") + theme::key("E", "mail   ") +
            theme::key("T", "agline   ") + theme::key("P", "assword   ") + theme::key("Q", "uit");
    prompt();
}

void UserSettings::prompt() { out_ += "\r\n   " + theme::fade("Change") + theme::ORANGE + ": " + theme::WHITE; }

void UserSettings::ask(const char* what, std::string* field, size_t max) {
    field_ = field;
    max_ = max;
    line_.clear();
    st_ = St::Edit;
    out_ += std::string("\r\n\r\n   ") + theme::TEXT + what + " (Enter keeps " + theme::WHITE + (field->empty() ? "it empty" : *field) +
            theme::TEXT + ")\r\n   " + theme::ORANGE + "> " + theme::WHITE;
}

bool UserSettings::key(char c) {
    if (st_ == St::Menu) {
        switch (c | 0x20) {
            case 'r': ask("Real name", &u_.realName, 40); break;
            case 'l': ask("Location", &u_.location, 40); break;
            case 'e': ask("Email", &u_.email, 60); break;
            case 't': ask("Tagline", &u_.tagline, 60); break;
            case 'p':
                st_ = St::OldPw;
                line_.clear();
                out_ += std::string("\r\n\r\n   ") + theme::TEXT + "Current password: " + theme::WHITE;
                break;
            case 'q': return false;
            default: break;
        }
        return true;
    }
    if (c == 8 || c == 127) {
        if (!line_.empty()) {
            line_.pop_back();
            out_ += "\b \b";
        }
        return true;
    }
    bool secret = st_ != St::Edit;
    if (c != '\r') {
        if ((unsigned char)c >= 0x20 && (unsigned char)c < 0x7f && line_.size() < (secret ? 40 : max_)) {
            line_ += c;
            out_ += secret ? '*' : c;
        }
        return true;
    }
    std::string got = line_;
    line_.clear();
    if (st_ == St::Edit) {
        if (!got.empty()) {
            *field_ = got;
            if (save_) save_();
        }
        st_ = St::Menu;
        show();
    } else if (st_ == St::OldPw) {
        if (!checkPassword(u_.salt, u_.hash, got)) {
            out_ += std::string("\r\n   ") + theme::ORANGE + "That's not your password." + theme::TEXT;
            st_ = St::Menu;
            prompt();
        } else {
            st_ = St::NewPw;
            out_ += std::string("\r\n   ") + theme::TEXT + "New password (6 or more): " + theme::WHITE;
        }
    } else if (st_ == St::NewPw) {
        if (got.size() < 6) {
            out_ += std::string("\r\n   ") + theme::ORANGE + "Too short." + theme::TEXT + " New password (6 or more): " + theme::WHITE;
            return true;
        }
        newPw_ = got;
        st_ = St::AgainPw;
        out_ += std::string("\r\n   ") + theme::TEXT + "Again: " + theme::WHITE;
    } else {
        st_ = St::Menu;
        if (got != newPw_) {
            out_ += std::string("\r\n   ") + theme::ORANGE + "They didn't match; nothing changed." + theme::TEXT;
        } else {
            u_.salt = makeSalt(rng_);
            u_.hash = hashPassword(u_.salt, got);
            if (onPassword_) onPassword_(got);
            if (save_) save_();
            out_ += std::string("\r\n   ") + theme::TEXT + "Password changed.";
        }
        newPw_.clear();
        prompt();
    }
    return true;
}

}  // namespace axis::bbs
