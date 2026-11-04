// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// [E]mail: private messages between callers. They live in the message store's hidden "email" area
// (so every board shares them through the main), and a caller only ever sees mail addressed to
// them. Their newest-read number is kept with their account, so new mail can be announced at login.
#pragma once
#include <functional>
#include <string>
#include <vector>

#include "bbs/msgbase.h"

namespace axis::bbs {

class EmailBox {
public:
    static constexpr const char* kArea = "email";
    // findUser: the handle as the board spells it, or false when nobody goes by that name
    EmailBox(MessageStore& store, std::string& out, std::string me, int& lastRead,
             std::function<bool(const std::string&, std::string&)> findUser, std::function<std::string()> now, std::string tagline)
        : st_(store), out_(out), me_(std::move(me)), lastRead_(lastRead), findUser_(std::move(findUser)), now_(std::move(now)),
          tagline_(std::move(tagline)) {}
    void       begin();   // the inbox
    bool       key(char c);  // false once the caller leaves for the main menu
    static int countNew(MessageStore& store, const std::string& me, int lastRead);

private:
    enum class St { Box, Num, To, Subject, Body };
    void show();
    void prompt();
    void read(int num);
    bool lineKey(char c, size_t max);  // true on Enter

    MessageStore&                                         st_;
    std::string&                                          out_;
    std::string                                           me_;
    int&                                                  lastRead_;
    std::function<bool(const std::string&, std::string&)> findUser_;
    std::function<std::string()>                          now_;
    std::string                                           tagline_, line_;
    St                                                    s_ = St::Box;
    std::vector<Message>                                  mine_;  // headers addressed to this caller
    Message                                               draft_;
    int                                                   lines_ = 0;
};

}  // namespace axis::bbs
