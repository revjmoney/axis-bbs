// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// XMODEM (checksum and CRC) and XMODEM-1K, both directions. One file, no name or size on the wire:
// the last block is padded with 1Ah, which the host trims if it wants to.
#pragma once
#include <vector>

#include "net/transfer.h"

namespace axis::net {

class XmodemSender : public Transfer {
public:
    XmodemSender(door::ByteStream& line, ReadAt read, uint32_t size, bool oneK)
        : line_(line), read_(std::move(read)), size_(size), oneK_(oneK) {}
    bool step(uint32_t nowMs) override;

private:
    enum class St { WaitStart, WaitAck, WaitEotAck };
    void sendBlock();
    door::ByteStream& line_;
    ReadAt            read_;
    uint32_t          size_, pos_ = 0, blockLen_ = 0;
    bool              oneK_, crc_ = true;
    St                st_ = St::WaitStart;
    uint8_t           seq_ = 1;
    int               tries_ = 0, cans_ = 0;
    uint32_t          since_ = 0;
    bool              started_ = false;
};

class XmodemReceiver : public Transfer {
public:
    XmodemReceiver(door::ByteStream& line, Write write) : line_(line), write_(std::move(write)) {}
    bool step(uint32_t nowMs) override;

private:
    door::ByteStream&    line_;
    Write                write_;
    std::vector<uint8_t> buf_;
    bool                 crc_ = true, started_ = false, gotBlock_ = false;
    uint8_t              expect_ = 1;
    int                  pokes_ = 0, cans_ = 0, errors_ = 0;
    uint32_t             since_ = 0, lastByte_ = 0;
};

}  // namespace axis::net
