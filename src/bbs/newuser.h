// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The new user application, underground style: a password, who you are, your shop (CNC machine,
// rotary axis), whether you know RotatoCAM, scene questions, the rules, then a review and submit.
// The account it makes is pending until the sysop validates it. The Node hands it every key.
#pragma once
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "bbs/config.h"
#include "bbs/users.h"

namespace axis::bbs {

class NewUserApp {
public:
    enum class Outcome { Running, Submitted, Cancelled };
    NewUserApp(std::string handle, std::string& out, const BbsConfig& cfg, uint32_t (*rng)());
    void    begin();
    Outcome key(char c);
    User    user() const { return user_; }  // after Submitted: pending, password hashed, answers filled in

private:
    enum class Kind { Text, Optional, YesNo, Password, Confirm, Agree, Submit };
    struct Step {
        std::string section, key, prompt;
        Kind        kind;
        std::function<bool(const std::map<std::string, std::string>&)> when;  // empty: always asked
    };
    void ask();
    void next();
    void section(const std::string& title);
    std::vector<Step>                  steps_;
    std::map<std::string, std::string> answers_;
    std::string                        handle_, line_, password_, lastSection_;
    std::string&                       out_;
    const BbsConfig&                   cfg_;
    uint32_t (*rng_)();
    size_t at_ = 0;
    User   user_;
};

}  // namespace axis::bbs
