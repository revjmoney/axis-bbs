// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// [U]ser settings: a caller's own account (Oblivion/2 called it Parameters). They see what the
// board knows about them and change their real name, location, email, tagline (it signs their
// posts) and password (the old one first, the new one twice). Every change is saved at once, which
// also sends the record to every board.
#pragma once
#include <functional>
#include <string>

#include "bbs/users.h"

namespace axis::bbs {

class UserSettings {
public:
    UserSettings(User& user, std::string& out, std::function<void()> save, uint32_t (*rng)())
        : u_(user), out_(out), save_(std::move(save)), rng_(rng) {}
    void begin();       // the account screen
    bool key(char c);   // false once the caller leaves for the main menu
    // The sysop's account lives in the board settings: the new password goes to this too (it
    // changes the sysop login on every board), after the record itself has the new hash.
    void setOnPassword(std::function<void(const std::string&)> f) { onPassword_ = std::move(f); }

private:
    enum class St { Menu, Edit, OldPw, NewPw, AgainPw };
    void show();
    void prompt();
    void ask(const char* what, std::string* field, size_t max);

    User&                 u_;
    std::string&          out_;
    std::function<void()> save_;
    std::function<void(const std::string&)> onPassword_;
    uint32_t (*rng_)();
    St                    st_ = St::Menu;
    std::string           line_, newPw_;
    std::string*          field_ = nullptr;
    size_t                max_ = 0;
};

}  // namespace axis::bbs
