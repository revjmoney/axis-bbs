// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// Arduino's Print and Stream for the native Linux program (the ESP32 core's API, as the board's code uses it).
#pragma once
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "WString.h"

#define DEC 10
#define HEX 16
#define OCT 8
#define BIN 2

class Print {
public:
    constexpr Print() = default;
    virtual ~Print() = default;

    virtual size_t write(uint8_t c) = 0;
    virtual size_t write(const uint8_t* buf, size_t n);
    size_t         write(const char* s) { return s ? write((const uint8_t*)s, std::strlen(s)) : 0; }
    size_t         write(const char* buf, size_t n) { return write((const uint8_t*)buf, n); }
    virtual int    availableForWrite() { return 0; }
    virtual void   flush() {}

    size_t printf(const char* fmt, ...) __attribute__((format(printf, 2, 3)));
    size_t vprintf(const char* fmt, va_list ap);

    size_t print(const String& s) { return write((const uint8_t*)s.c_str(), s.length()); }
    size_t print(const char* s) { return write(s); }
    size_t print(char c) { return write((uint8_t)c); }
    size_t print(unsigned char v, int base = DEC) { return print((unsigned long)v, base); }
    size_t print(int v, int base = DEC) { return print((long)v, base); }
    size_t print(unsigned int v, int base = DEC) { return print((unsigned long)v, base); }
    size_t print(long v, int base = DEC);
    size_t print(unsigned long v, int base = DEC);
    size_t print(long long v, int base = DEC);
    size_t print(unsigned long long v, int base = DEC);
    size_t print(double v, int digits = 2);

    size_t println() { return write("\r\n"); }
    template <typename T>
    size_t println(const T& v) {
        size_t n = print(v);
        return n + println();
    }
    template <typename T>
    size_t println(const T& v, int fmt) {
        size_t n = print(v, fmt);
        return n + println();
    }
};

class Stream : public Print {
public:
    constexpr Stream() = default;
    virtual int available() = 0;
    virtual int read() = 0;
    virtual int peek() = 0;

    void          setTimeout(unsigned long ms) { timeout_ = ms; }
    unsigned long getTimeout() const { return timeout_; }
    size_t        readBytes(char* buf, size_t n);
    size_t        readBytes(uint8_t* buf, size_t n) { return readBytes((char*)buf, n); }
    String        readString();
    String        readStringUntil(char terminator);

protected:
    int           timedRead();  // the next byte, waiting up to the timeout; -1 when none came
    unsigned long timeout_ = 1000;
};
