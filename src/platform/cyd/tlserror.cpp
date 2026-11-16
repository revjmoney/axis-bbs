// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// mbedtls_strerror without mbedtls's table of every error's text (about 15 KB of flash). Only WiFiClientSecure's error
// log calls it; the number is enough to look the error up. The link swaps it in with -Wl,--wrap=mbedtls_strerror
// (platformio.ini).
#include <cstddef>
#include <cstdio>

extern "C" void __wrap_mbedtls_strerror(int ret, char* buf, size_t len) {
    if (buf && len) std::snprintf(buf, len, "TLS error -0x%04X", (unsigned)(ret < 0 ? -ret : ret));
}
