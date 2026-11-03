// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// Telnet protocol handling for one caller: option negotiation in, clean bytes out.
// Portable (no sockets); the firmware and the PC host feed it raw network bytes.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

namespace axis::net {

class TelnetFilter {
public:
    // What the server sends right after accepting a caller.
    std::string initialNegotiation() const;
    // Raw bytes from the caller: clean keystrokes go to toApp, protocol answers to replyToNet.
    void fromNet(const uint8_t* p, size_t n, std::string& toApp, std::string& replyToNet);
    // Bytes from the BBS or a door, escaped for the wire (0xFF doubled).
    std::string toNet(const uint8_t* p, size_t n) const;
    int width() const { return width_; }
    int height() const { return height_; }

private:
    enum class St { Data, Iac, Opt, Sb, SbIac } st_ = St::Data;
    uint8_t     cmd_ = 0;      // WILL/WONT/DO/DONT waiting for its option byte
    std::string sb_;           // subnegotiation payload
    bool        lastCr_ = false;
    int         width_ = 80, height_ = 25;
};

}  // namespace axis::net
