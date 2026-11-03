// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The PC console as a door's local keyboard and screen (doorhost --local).
#pragma once
#include <array>
#include <string>

#include "door/bytestream.h"

namespace axis {
namespace pc {

class ConsoleStream : public door::ByteStream {
public:
    ConsoleStream();
    ~ConsoleStream() override;
    int  available() override;
    int  read() override;
    void write(const uint8_t* p, size_t n) override;
    bool carrier() override { return true; }
    void draw(const std::array<std::string, 25>& screen);  // redraw only when the text changed

private:
    std::array<std::string, 25> last_;
    int                         pending_ = -1;
};

}  // namespace pc
}  // namespace axis
