// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// While the BBS is unconfigured, a telnet caller must type the setup PIN shown on the
// board's screen before the setup wizard opens. Three wrong tries end the call.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

namespace axis::bbs {

class SetupGate {
public:
    static constexpr int kTries = 3;
    explicit SetupGate(std::string pin);
    void        begin();
    void        input(const uint8_t* p, size_t n);
    std::string takeOutput();
    bool        passed() const { return passed_; }
    bool        rejected() const { return tries_ >= kTries && !passed_; }

private:
    void        prompt();
    std::string pin_, line_, out_;
    int         tries_ = 0;
    bool        passed_ = false;
};

}  // namespace axis::bbs
