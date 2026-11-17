// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The Arduino and ESP-IDF calls the board's main.cpp makes, done on Linux: String, Print and Stream, the clock,
// the serial console on stdin and stdout, random numbers, the ESP object, base64. (Pins, heap figures and the
// subsystems later tasks bring, files and the network, are in the headers.)
#include <Arduino.h>
#include <base64.h>
#include <lwip/dns.h>
#include <mbedtls/base64.h>

#include <poll.h>
#include <sys/random.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cinttypes>
#include <ctime>
#include <string>

// ------------------------------------------------------------------ time
namespace {
uint64_t monoUs() {
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u;
}
uint64_t sinceStartUs() {  // the program's start (its first look at the clock): millis() counts from here, as the board's from its reset
    static const uint64_t start = monoUs();
    return monoUs() - start;
}
}  // namespace

int64_t  esp_timer_get_time(void) { return (int64_t)sinceStartUs(); }
uint32_t millis() { return (uint32_t)(sinceStartUs() / 1000u); }
uint32_t micros() { return (uint32_t)sinceStartUs(); }
void     yield() {}

void delayMicroseconds(uint32_t us) {
    timespec ts{(time_t)(us / 1000000u), (long)(us % 1000000u) * 1000L};
    while (nanosleep(&ts, &ts) != 0 && errno == EINTR) {
    }
}
void delay(uint32_t ms) {
    timespec ts{(time_t)(ms / 1000u), (long)(ms % 1000u) * 1000000L};
    while (nanosleep(&ts, &ts) != 0 && errno == EINTR) {
    }
}
void vTaskDelay(TickType_t ticks) { delay(ticks * portTICK_PERIOD_MS); }

// ------------------------------------------------------------------ the ESP object, random numbers, the ROM printf
EspClass ESP;

void EspClass::restart() {
    std::fflush(nullptr);  // every open stdio stream (the log and any file main.cpp left open), not only stdout
    _exit(3);  // code 3: "restart me" (the systemd unit restarts the program); no static destructors run mid-loop
}
void esp_restart(void) { ESP.restart(); }

extern "C" void esp_fill_random(void* buf, size_t len) {
    uint8_t* p = (uint8_t*)buf;
    while (len) {
        ssize_t n = getrandom(p, len, 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            std::abort();  // no randomness is not something to carry on without (keys and nonces come from here)
        }
        p += n;
        len -= (size_t)n;
    }
}
extern "C" uint32_t esp_random(void) {
    uint32_t v;
    esp_fill_random(&v, sizeof v);
    return v;
}

extern "C" int esp_rom_printf(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = std::vprintf(fmt, ap);
    va_end(ap);
    std::fflush(stdout);
    return n;
}

// ------------------------------------------------------------------ the serial console
HostSerial Serial;

void HostSerial::fill() {
    if (eof_ || head_ != tail_) return;
    pollfd p{STDIN_FILENO, POLLIN, 0};
    if (poll(&p, 1, 0) <= 0) return;
    if (p.revents & POLLNVAL) {  // stdin is not open at all: the same as its end
        eof_ = true;
        return;
    }
    if (!(p.revents & (POLLIN | POLLHUP | POLLERR))) return;
    ssize_t n = ::read(STDIN_FILENO, buf_, sizeof buf_);
    if (n > 0) {
        head_ = 0;
        tail_ = (int)n;
    } else if (n == 0 || (errno != EINTR && errno != EAGAIN)) {
        eof_ = true;  // end of input (or no stdin at all): no more keys, ever
    }
}
int HostSerial::available() {
    fill();
    return tail_ - head_;
}
int HostSerial::read() {
    fill();
    return head_ < tail_ ? buf_[head_++] : -1;
}
int HostSerial::peek() {
    fill();
    return head_ < tail_ ? buf_[head_] : -1;
}
size_t HostSerial::write(const uint8_t* buf, size_t n) {
    std::fflush(stdout);  // anything printf'd straight to stdout goes first
    size_t done = 0;
    while (done < n) {
        ssize_t w = ::write(STDOUT_FILENO, buf + done, n - done);
        if (w < 0 && errno == EINTR) continue;
        if (w <= 0) break;
        done += (size_t)w;
    }
    return done;
}

// ------------------------------------------------------------------ Print and Stream
size_t Print::write(const uint8_t* buf, size_t n) {
    size_t done = 0;
    while (done < n && write(buf[done])) done++;
    return done;
}

size_t Print::vprintf(const char* fmt, va_list ap) {
    char    small[256];
    va_list copy;
    va_copy(copy, ap);
    int len = std::vsnprintf(small, sizeof small, fmt, copy);
    va_end(copy);
    if (len < 0) return 0;
    if ((size_t)len < sizeof small) return write((const uint8_t*)small, (size_t)len);
    std::string big((size_t)len + 1, '\0');
    std::vsnprintf(&big[0], big.size(), fmt, ap);
    return write((const uint8_t*)big.data(), (size_t)len);
}

size_t Print::printf(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    size_t n = vprintf(fmt, ap);
    va_end(ap);
    return n;
}

size_t Print::print(long v, int base) {
    if (base == DEC) return printf("%ld", v);
    return print((unsigned long)v, base);  // other bases: the bits, unsigned (as the core prints them)
}
size_t Print::print(unsigned long v, int base) { return print((unsigned long long)v, base); }
size_t Print::print(long long v, int base) {
    if (base == DEC) return printf("%lld", v);
    return print((unsigned long long)v, base);
}
size_t Print::print(unsigned long long v, int base) {
    if (base < 2 || base > 36) base = DEC;
    char  buf[65];
    char* p = buf + sizeof buf;
    *--p = 0;
    do {
        int d = (int)(v % (unsigned)base);
        *--p = (char)(d < 10 ? '0' + d : 'A' + d - 10);
        v /= (unsigned)base;
    } while (v);
    return write(p);
}
size_t Print::print(double v, int digits) { return printf("%.*f", digits, v); }

int Stream::timedRead() {
    uint32_t start = millis();
    do {
        int c = read();
        if (c >= 0) return c;
        delay(1);
    } while (millis() - start < timeout_);
    return -1;
}
size_t Stream::readBytes(char* buf, size_t n) {
    size_t got = 0;
    while (got < n) {
        int c = timedRead();
        if (c < 0) break;
        buf[got++] = (char)c;
    }
    return got;
}
String Stream::readString() {
    String s;
    for (int c; (c = timedRead()) >= 0;) s += (char)c;
    return s;
}
String Stream::readStringUntil(char terminator) {
    String s;
    for (int c; (c = timedRead()) >= 0 && c != terminator;) s += (char)c;
    return s;
}

// ------------------------------------------------------------------ String
namespace {
std::string numText(unsigned long long v, int base, bool neg) {
    if (base < 2 || base > 36) base = 10;
    std::string s;
    do {
        int d = (int)(v % (unsigned)base);
        s += (char)(d < 10 ? '0' + d : 'a' + d - 10);  // the core writes hex in lower case
        v /= (unsigned)base;
    } while (v);
    if (neg) s += '-';
    std::reverse(s.begin(), s.end());
    return s;
}
std::string signedText(long long v, int base) {
    if (base == 10 && v < 0) return numText((unsigned long long)-(v + 1) + 1u, 10, true);
    // the core prints other bases of a negative number as its unsigned bits
    return numText((unsigned long long)v, base, false);
}
std::string floatText(double v, unsigned decimals) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.*f", (int)decimals, v);
    return buf;
}
}  // namespace

String::String(unsigned char v, unsigned char base) : s_(numText(v, base, false)) {}
String::String(int v, unsigned char base) : s_(base == 10 ? signedText(v, base) : numText((unsigned int)v, base, false)) {}
String::String(unsigned int v, unsigned char base) : s_(numText(v, base, false)) {}
String::String(long v, unsigned char base) : s_(base == 10 ? signedText(v, base) : numText((unsigned long)v, base, false)) {}
String::String(unsigned long v, unsigned char base) : s_(numText(v, base, false)) {}
String::String(long long v, unsigned char base) : s_(signedText(v, base)) {}
String::String(unsigned long long v, unsigned char base) : s_(numText(v, base, false)) {}
String::String(float v, unsigned int decimals) : s_(floatText(v, decimals)) {}
String::String(double v, unsigned int decimals) : s_(floatText(v, decimals)) {}

bool String::equalsIgnoreCase(const String& o) const {
    if (s_.size() != o.s_.size()) return false;
    for (size_t i = 0; i < s_.size(); i++)
        if (std::tolower((unsigned char)s_[i]) != std::tolower((unsigned char)o.s_[i])) return false;
    return true;
}
bool String::startsWith(const String& p, unsigned int offset) const {
    if (offset > s_.size() || p.s_.size() > s_.size() - offset) return false;
    return s_.compare(offset, p.s_.size(), p.s_) == 0;
}
bool String::endsWith(const String& p) const {
    return p.s_.size() <= s_.size() && s_.compare(s_.size() - p.s_.size(), p.s_.size(), p.s_) == 0;
}

char& String::operator[](unsigned int i) {
    static char dummy;
    if (i >= s_.size()) {
        dummy = 0;
        return dummy;
    }
    return s_[i];
}
void String::getBytes(unsigned char* buf, unsigned int size, unsigned int index) const {
    if (!buf || !size) return;
    if (index >= s_.size()) {
        buf[0] = 0;
        return;
    }
    size_t n = std::min((size_t)size - 1, s_.size() - index);
    std::memcpy(buf, s_.data() + index, n);
    buf[n] = 0;
}

int String::indexOf(char c, unsigned int from) const {
    if (from >= s_.size()) return -1;
    size_t at = s_.find(c, from);
    return at == std::string::npos ? -1 : (int)at;
}
int String::indexOf(const String& s, unsigned int from) const {
    if (from >= s_.size()) return -1;
    size_t at = s_.find(s.s_, from);
    return at == std::string::npos ? -1 : (int)at;
}
int String::lastIndexOf(char c, unsigned int from) const {
    if (from >= s_.size()) return -1;
    size_t at = s_.rfind(c, from);
    return at == std::string::npos ? -1 : (int)at;
}
int String::lastIndexOf(const String& s, unsigned int from) const {
    if (s.s_.empty() || s.s_.size() > s_.size() || from >= s_.size()) return -1;
    size_t at = s_.rfind(s.s_, from);
    return at == std::string::npos ? -1 : (int)at;
}
String String::substring(unsigned int left, unsigned int right) const {
    if (left > right) std::swap(left, right);
    if (left >= s_.size()) return String();
    if (right > s_.size()) right = (unsigned int)s_.size();
    return String(s_.data() + left, right - left);
}

void String::replace(char find, char with) { std::replace(s_.begin(), s_.end(), find, with); }
void String::replace(const String& find, const String& with) {
    if (find.s_.empty()) return;
    std::string out;
    size_t      from = 0;
    for (size_t at; (at = s_.find(find.s_, from)) != std::string::npos; from = at + find.s_.size()) {
        out.append(s_, from, at - from);
        out += with.s_;
    }
    out.append(s_, from, std::string::npos);
    s_.swap(out);
}
void String::remove(unsigned int index, unsigned int count) {
    if (index >= s_.size() || !count) return;
    s_.erase(index, std::min((size_t)count, s_.size() - index));
}
void String::toLowerCase() {
    for (char& c : s_) c = (char)std::tolower((unsigned char)c);
}
void String::toUpperCase() {
    for (char& c : s_) c = (char)std::toupper((unsigned char)c);
}
void String::trim() {
    size_t b = 0, e = s_.size();
    while (b < e && std::isspace((unsigned char)s_[b])) b++;
    while (e > b && std::isspace((unsigned char)s_[e - 1])) e--;
    s_ = s_.substr(b, e - b);
}
long   String::toInt() const { return std::strtol(s_.c_str(), nullptr, 10); }
float  String::toFloat() const { return std::strtof(s_.c_str(), nullptr); }
double String::toDouble() const { return std::strtod(s_.c_str(), nullptr); }

// ------------------------------------------------------------------ IPAddress
bool IPAddress::fromString(const char* s) {
    if (!s) return false;
    unsigned parts[4], n = 0, v = 0;
    bool     digit = false;
    for (const char* p = s;; p++) {
        if (*p >= '0' && *p <= '9') {
            v = v * 10 + (unsigned)(*p - '0');
            if (v > 255) return false;
            digit = true;
        } else if (*p == '.' || *p == 0) {
            if (!digit || n >= 4) return false;
            parts[n++] = v;
            v = 0;
            digit = false;
            if (*p == 0) break;
        } else {
            return false;
        }
    }
    if (n != 4) return false;
    for (int i = 0; i < 4; i++) bytes_[i] = (uint8_t)parts[i];
    return true;
}

// ------------------------------------------------------------------ base64 (mbedtls's calls and return codes)
namespace {
const char kB64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
int        b64Value(unsigned char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}
}  // namespace

int mbedtls_base64_encode(unsigned char* dst, size_t dlen, size_t* olen, const unsigned char* src, size_t slen) {
    size_t need = (slen + 2) / 3 * 4 + 1;  // with the NUL
    if (!dst || dlen < need) {
        *olen = need;
        return MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL;
    }
    size_t o = 0;
    for (size_t i = 0; i < slen; i += 3) {
        uint32_t v = (uint32_t)src[i] << 16 | (i + 1 < slen ? (uint32_t)src[i + 1] << 8 : 0) | (i + 2 < slen ? src[i + 2] : 0);
        dst[o++] = (unsigned char)kB64[(v >> 18) & 63];
        dst[o++] = (unsigned char)kB64[(v >> 12) & 63];
        dst[o++] = i + 1 < slen ? (unsigned char)kB64[(v >> 6) & 63] : '=';
        dst[o++] = i + 2 < slen ? (unsigned char)kB64[v & 63] : '=';
    }
    dst[o] = 0;
    *olen = o;
    return 0;
}

int mbedtls_base64_decode(unsigned char* dst, size_t dlen, size_t* olen, const unsigned char* src, size_t slen) {
    // first pass: check every character and count the bytes (spaces and line breaks are skipped, as mbedtls does)
    size_t chars = 0, pads = 0;
    for (size_t i = 0; i < slen; i++) {
        unsigned char c = src[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        if (c == '=') {
            if (++pads > 2) return MBEDTLS_ERR_BASE64_INVALID_CHARACTER;
        } else if (pads || b64Value(c) < 0) {
            return MBEDTLS_ERR_BASE64_INVALID_CHARACTER;
        }
        chars++;
    }
    if (chars == 0) {
        *olen = 0;
        return 0;
    }
    if (chars % 4) return MBEDTLS_ERR_BASE64_INVALID_CHARACTER;
    size_t need = chars / 4 * 3 - pads;
    if (!dst || dlen < need) {
        *olen = need;
        return MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL;
    }
    uint32_t acc = 0;
    size_t   n = 0, o = 0;
    for (size_t i = 0; i < slen; i++) {
        unsigned char c = src[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        acc = acc << 6 | (c == '=' ? 0u : (uint32_t)b64Value(c));
        if (++n == 4) {
            unsigned char b[3] = {(unsigned char)(acc >> 16), (unsigned char)(acc >> 8), (unsigned char)acc};
            for (int k = 0; k < 3 && o < need; k++) dst[o++] = b[k];
            acc = 0;
            n = 0;
        }
    }
    *olen = o;
    return 0;
}

String base64::encode(const uint8_t* data, size_t length) {
    size_t      need = 0;
    mbedtls_base64_encode(nullptr, 0, &need, data, length);
    std::string out(need, '\0');
    size_t      got = 0;
    if (mbedtls_base64_encode((unsigned char*)&out[0], out.size(), &got, data, length) != 0) return String();
    return String(out.data(), got);
}
