// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The board's USB serial console on Linux: it writes axis::host::serialOut and reads axis::host::serialIn (host.h),
// never blocking; end of input is no input. The start mode (startmode.h) picks them: the terminal in terminal mode,
// stdout and no input headless.
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

    // The host's own keys, read before anything typed after them (terminal mode types "WFC\r" at start). False when
    // there is no room for them.
    bool inject(const char* keys);

private:
    void    fill();          // whatever the input has now, without waiting
    uint8_t buf_[256] = {};
    int     head_ = 0, tail_ = 0;
    bool    eof_ = false;
};

extern HostSerial Serial;
