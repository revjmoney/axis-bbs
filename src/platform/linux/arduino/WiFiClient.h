// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The ESP32 core's WiFiClient: a TCP connection whose copies share one socket (the core holds it in a shared_ptr).
// As on the ESP32: stop() lets go of this copy only, and the socket closes when the last copy lets go; connected()
// is true until the peer has closed and every byte it sent has been read, even with nothing to read now; read()
// is -1 when nothing waits; write() waits (a few seconds at most) until the socket takes every byte. The sockets
// are non-blocking with TCP_NODELAY and keepalive, so main.cpp's ::send(fd, ..., MSG_DONTWAIT) works as on the
// board. Bodies in hostnet.cpp.
#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>

#include "IPAddress.h"
#include "Print.h"

struct HostSocket;  // hostnet.cpp: the fd, the peer's address, and whether the accept gate counts it

class WiFiClient : public Stream {
public:
    WiFiClient() = default;
    explicit WiFiClient(std::shared_ptr<HostSocket> s);

    int connect(IPAddress ip, uint16_t port, int32_t timeoutMs = 3000);
    int connect(const char* host, uint16_t port, int32_t timeoutMs = 3000);
    size_t write(uint8_t c) override { return write(&c, 1); }
    size_t write(const uint8_t* buf, size_t n) override;
    using Print::write;
    int       read() override;
    int       read(uint8_t* buf, size_t n);
    int       available() override;
    int       peek() override;
    void      flush() override;  // as the ESP32's: drops input waiting to be read (up to one buffer)
    void      stop();
    uint8_t   connected();
    explicit  operator bool() { return connected(); }
    int       fd() const;
    int       setNoDelay(bool on);
    IPAddress remoteIP() const;
    uint16_t  remotePort() const;
    IPAddress localIP() const;
    uint16_t  localPort() const;

private:
    void                        fail();  // a hard socket error: let go, as the ESP32's stop() on a failed read
    std::shared_ptr<HostSocket> s_;
    bool                        connected_ = false;
};
