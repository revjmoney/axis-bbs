// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The Arduino String for the native Linux program, backed by std::string: the part of the ESP32 core's
// API the board's code uses, with the same edge cases (substring past the end, indexOf from past the end).
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

class String {
public:
    String() = default;
    String(const char* s) : s_(s ? s : "") {}
    String(const char* s, size_t n) : s_(s ? std::string(s, n) : std::string()) {}
    String(const String&) = default;
    String(String&&) noexcept = default;
    explicit String(char c) : s_(1, c) {}
    explicit String(unsigned char v, unsigned char base = 10);
    explicit String(int v, unsigned char base = 10);
    explicit String(unsigned int v, unsigned char base = 10);
    explicit String(long v, unsigned char base = 10);
    explicit String(unsigned long v, unsigned char base = 10);
    explicit String(long long v, unsigned char base = 10);
    explicit String(unsigned long long v, unsigned char base = 10);
    explicit String(float v, unsigned int decimals = 2);
    explicit String(double v, unsigned int decimals = 2);

    String& operator=(const String&) = default;
    String& operator=(String&&) noexcept = default;
    String& operator=(const char* s) {
        s_ = s ? s : "";
        return *this;
    }

    const char*  c_str() const { return s_.c_str(); }
    unsigned int length() const { return (unsigned int)s_.size(); }
    bool         isEmpty() const { return s_.empty(); }
    bool         reserve(unsigned int n) {
        s_.reserve(n);
        return true;
    }
    void clear() { s_.clear(); }

    bool concat(const String& o) { return s_ += o.s_, true; }
    bool concat(const char* s) { return s ? (s_ += s, true) : false; }
    bool concat(const char* s, unsigned int n) { return s ? (s_.append(s, n), true) : false; }
    bool concat(char c) { return s_ += c, true; }
    bool concat(unsigned char v) { return concat(String(v)); }
    bool concat(int v) { return concat(String(v)); }
    bool concat(unsigned int v) { return concat(String(v)); }
    bool concat(long v) { return concat(String(v)); }
    bool concat(unsigned long v) { return concat(String(v)); }
    bool concat(long long v) { return concat(String(v)); }
    bool concat(unsigned long long v) { return concat(String(v)); }
    bool concat(float v) { return concat(String(v)); }
    bool concat(double v) { return concat(String(v)); }

    template <typename T>
    String& operator+=(const T& v) {
        concat(v);
        return *this;
    }

    int  compareTo(const String& o) const { return s_.compare(o.s_); }
    bool equals(const String& o) const { return s_ == o.s_; }
    bool equals(const char* s) const { return s_ == (s ? s : ""); }
    bool equalsIgnoreCase(const String& o) const;
    bool startsWith(const String& p) const { return startsWith(p, 0); }
    bool startsWith(const String& p, unsigned int offset) const;
    bool endsWith(const String& p) const;

    char  charAt(unsigned int i) const { return i < s_.size() ? s_[i] : 0; }
    void  setCharAt(unsigned int i, char c) {
        if (i < s_.size()) s_[i] = c;
    }
    char  operator[](unsigned int i) const { return charAt(i); }
    char& operator[](unsigned int i);
    void  getBytes(unsigned char* buf, unsigned int size, unsigned int index = 0) const;
    void  toCharArray(char* buf, unsigned int size, unsigned int index = 0) const { getBytes((unsigned char*)buf, size, index); }

    int    indexOf(char c, unsigned int from = 0) const;
    int    indexOf(const String& s, unsigned int from = 0) const;
    int    lastIndexOf(char c) const { return lastIndexOf(c, length() - 1); }
    int    lastIndexOf(char c, unsigned int from) const;
    int    lastIndexOf(const String& s) const { return lastIndexOf(s, length() - s.length()); }
    int    lastIndexOf(const String& s, unsigned int from) const;
    String substring(unsigned int left) const { return substring(left, length()); }
    String substring(unsigned int left, unsigned int right) const;

    void replace(char find, char with);
    void replace(const String& find, const String& with);
    void remove(unsigned int index) { remove(index, (unsigned int)-1); }
    void remove(unsigned int index, unsigned int count);
    void toLowerCase();
    void toUpperCase();
    void trim();

    long   toInt() const;
    float  toFloat() const;
    double toDouble() const;

    friend bool operator==(const String& a, const String& b) { return a.s_ == b.s_; }
    friend bool operator==(const String& a, const char* b) { return a.equals(b); }
    friend bool operator==(const char* a, const String& b) { return b.equals(a); }
    friend bool operator!=(const String& a, const String& b) { return !(a == b); }
    friend bool operator!=(const String& a, const char* b) { return !(a == b); }
    friend bool operator!=(const char* a, const String& b) { return !(a == b); }
    friend bool operator<(const String& a, const String& b) { return a.s_ < b.s_; }
    friend bool operator>(const String& a, const String& b) { return a.s_ > b.s_; }
    friend bool operator<=(const String& a, const String& b) { return a.s_ <= b.s_; }
    friend bool operator>=(const String& a, const String& b) { return a.s_ >= b.s_; }

private:
    std::string s_;
};

// Concatenation: "text" + String(n), s + 'c', s + 12 (a number is written out, as on the board).
inline String operator+(const String& a, const String& b) {
    String r(a);
    r.concat(b);
    return r;
}
inline String operator+(const String& a, const char* b) {
    String r(a);
    r.concat(b);
    return r;
}
inline String operator+(const char* a, const String& b) {
    String r(a);
    r.concat(b);
    return r;
}
inline String operator+(const String& a, char b) {
    String r(a);
    r.concat(b);
    return r;
}
#define AXIS_STRING_PLUS(T)                         \
    inline String operator+(const String& a, T b) { \
        String r(a);                                \
        r.concat(b);                                \
        return r;                                   \
    }
AXIS_STRING_PLUS(unsigned char)
AXIS_STRING_PLUS(int)
AXIS_STRING_PLUS(unsigned int)
AXIS_STRING_PLUS(long)
AXIS_STRING_PLUS(unsigned long)
AXIS_STRING_PLUS(long long)
AXIS_STRING_PLUS(unsigned long long)
AXIS_STRING_PLUS(float)
AXIS_STRING_PLUS(double)
#undef AXIS_STRING_PLUS
