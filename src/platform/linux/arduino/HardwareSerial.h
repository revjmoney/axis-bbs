// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The board's USB serial console on Linux: stdout for output, stdin for input (never blocks; end of input is
// no input). How the terminal is set up (raw keys, the WFC in the terminal) is the start mode's job.
#pragma once
#include "Print.h"

class HostSerial : public Stream {
public:
    constexpr HostSerial() = default;
    void   begin(unsigned long baud) { (void)baud; }
    void   end() {}
    void   updateBaudRate(unsigned long baud) { (void)baud; }
    int    available() override;
    int    read() override;
    int    peek() override;
    size_t write(uint8_t c) override { return write(&c, 1); }
    size_t write(const uint8_t* buf, size_t n) override;
    using Print::write;
    void   flush() override {}
    int    availableForWrite() override { return 4096; }
    explicit operator bool() const { return true; }

private:
    void    fill();          // whatever stdin has now, without waiting
    uint8_t buf_[256] = {};
    int     head_ = 0, tail_ = 0;
    bool    eof_ = false;
};

extern HostSerial Serial;
