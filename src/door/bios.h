// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The door's BIOS: data area, video (into the B800 text page), keyboard, timer, clock, and the
// interrupt routing that lets doors hook vectors.
//
// Every vector points at a stub in ROM space (F000:v*8): "INT FBh, v, IRET". A door that hooks a
// vector and chains to the old one lands on the stub, and INT FBh hands vector v back to C++.
// The timer stub also does INT 1Ch, so a door's 1Ch hook runs 18.2 times a second.
#pragma once
#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "door/bytestream.h"
#include "door/cpu.h"
#include "door/guestmem.h"

namespace axis {
namespace door {

class Bios {
public:
    static constexpr uint16_t kStubSeg = 0xF000;
    static constexpr int      kChainVector = 0xFB;

    Bios(Cpu& cpu, GuestMemory& mem);
    void install();                          // stubs, vectors, BIOS data area, text page
    void setKeyboard(ByteStream* kb) { kb_ = kb; }
    void setServices(std::function<bool(int)> s) { services_ = std::move(s); }  // DOS, FOSSIL...
    bool route(int vector);                  // the Cpu interrupt callback
    bool dispatch(int vector);               // BIOS services in C++; false if not a BIOS vector
    int  tick(uint64_t nowMs);               // raises IRQ0 when a 18.2 Hz tick is due; returns 1 if it did
    bool consumeIdleHint();                  // true once after the door waited for a key or gave up its slice
    bool waitingForKey() const { return waiting_; }
    bool isStub(int vector) const;           // the vector still points at our stub
    std::array<std::string, 25> screenText() const;

private:
    uint8_t  curRow() const;
    uint8_t  curCol() const;
    void     setCursor(int row, int col);
    void     put(int row, int col, uint8_t ch, uint8_t attr);
    void     scroll(bool up, int lines, uint8_t attr, int r1, int c1, int r2, int c2);
    void     teletype(uint8_t ch);
    bool     service(int vector);
    bool     keyboard();
    uint16_t keyCode(int c) const;

    Cpu&                     cpu_;
    GuestMemory&             mem_;
    ByteStream*              kb_ = nullptr;
    std::function<bool(int)> services_;
    std::vector<uint8_t>     rom_;  // the stub page, served read-only
    bool                     idle_ = false, waiting_ = false;
    bool                     started_ = false;
    uint64_t                 startMs_ = 0, raised_ = 0;
};

}  // namespace door
}  // namespace axis
