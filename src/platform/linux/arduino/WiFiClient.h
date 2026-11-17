// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The ESP32 core's WiFiClient (a TCP connection; copies share one socket). The sockets come with the network
// layer (plan task 3): until then a client is never connected and every connect fails.
#pragma once
#include <cstddef>
#include <cstdint>

#include "IPAddress.h"
#include "Print.h"

class WiFiClient : public Stream {
public:
    WiFiClient() = default;

    int connect(IPAddress ip, uint16_t port, int32_t timeoutMs = 3000) {
        (void)ip, (void)port, (void)timeoutMs;
        return 0;
    }
    int connect(const char* host, uint16_t port, int32_t timeoutMs = 3000) {
        (void)host, (void)port, (void)timeoutMs;
        return 0;
    }
    size_t write(uint8_t c) override { return write(&c, 1); }
    size_t write(const uint8_t* buf, size_t n) override {
        (void)buf, (void)n;
        return 0;
    }
    using Print::write;
    int read() override { return -1; }
    int read(uint8_t* buf, size_t n) {
        (void)buf, (void)n;
        return -1;
    }
    int       available() override { return 0; }
    int       peek() override { return -1; }
    void      flush() override {}
    void      stop() {}
    uint8_t   connected() { return 0; }
    explicit  operator bool() { return connected(); }
    int       fd() const { return -1; }
    int       setNoDelay(bool) { return 0; }
    IPAddress remoteIP() const { return IPAddress(); }
    uint16_t  remotePort() const { return 0; }
    IPAddress localIP() const { return IPAddress(); }
    uint16_t  localPort() const { return 0; }
};
