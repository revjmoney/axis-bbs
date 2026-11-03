// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// First-boot setup questions. Text mode drives it over telnet; the touch screen uses
// fields(), defaultFor(), validate() and set() with its own keyboard.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "bbs/config.h"

namespace axis::bbs {

class SetupWizard {
public:
    struct Field {
        std::string key, label, hint;
        bool        secret;
        bool        optional;
    };

    SetupWizard(BbsConfig start, uint32_t (*rng)());
    void        begin();
    void        input(const uint8_t* p, size_t n);
    std::string takeOutput();
    bool        finished() const { return finished_; }
    bool        cancelled() const { return cancelled_; }
    const BbsConfig& result() const { return cfg_; }

    const std::vector<Field>& fields() const;
    std::string defaultFor(const std::string& key) const;
    std::string validate(const std::string& key, const std::string& value) const;  // "" = ok
    void        set(const std::string& key, const std::string& value);
    // touch path: store the password (hashes it) and confirm
    void        setPassword(const std::string& password);
    void        confirm(bool save);

private:
    void ask();
    void submit();
    void summary();
    BbsConfig   cfg_;
    uint32_t (*rng_)();
    size_t      step_ = 0;
    bool        atSummary_ = false, finished_ = false, cancelled_ = false;
    std::string out_, line_, pendingPassword_;
};

}  // namespace axis::bbs
