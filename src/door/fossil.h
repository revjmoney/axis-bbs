// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// A FOSSIL driver (revision 5) on INT 14h, wired to the caller's connection instead of a UART.
// Port 0 (COM1) is the caller. Functions 00h-1Bh; checked against the FOSSIL definitions in
// Synchronet's fossdefs.h (the FSC-0015 text itself could not be fetched).
#pragma once
#include <cstdint>
#include <deque>

#include "door/bytestream.h"
#include "door/cpu.h"
#include "door/guestmem.h"

namespace axis {
namespace door {

class Fossil {
public:
    static constexpr uint16_t kSignature = 0x1954;
    static constexpr uint8_t  kHighestFunction = 0x1B, kRevision = 5;

    Fossil(Cpu& cpu, GuestMemory& mem, ByteStream& line);
    bool int14();
    bool waitingForInput() const { return waiting_; }
    bool sentOutput() const { return sent_; }  // the door has written to the caller through FOSSIL

private:
    void     fill();  // pull what the caller has sent into our input buffer
    uint16_t status();
    Cpu&                cpu_;
    GuestMemory&        mem_;
    ByteStream&         line_;
    std::deque<uint8_t> in_;
    bool                waiting_ = false, sent_ = false;
};

}  // namespace door
}  // namespace axis
