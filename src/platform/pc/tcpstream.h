// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// Telnet over TCP on the PC (Winsock or BSD sockets): a listener for callers, a caller's
// connection as a ByteStream (telnet handled inside), and a plain client for tests.
#pragma once
#include <cstdint>
#include <memory>
#include <string>

#include "door/bytestream.h"
#include "net/telnet.h"

namespace axis {
namespace pc {

class TcpStream : public door::ByteStream {
public:
    explicit TcpStream(intptr_t sock);
    ~TcpStream() override;
    int  available() override;
    int  read() override;
    void write(const uint8_t* p, size_t n) override;
    bool carrier() override;

private:
    void           pump();
    intptr_t       sock_;
    bool           up_ = true;
    net::TelnetFilter telnet_;
    std::string    in_;
};

class TcpListener {
public:
    explicit TcpListener(uint16_t port);  // 0 = any free port
    ~TcpListener();
    uint16_t                   port() const { return port_; }
    std::unique_ptr<TcpStream> accept(int timeoutMs);  // nullptr on timeout

private:
    intptr_t sock_;
    uint16_t port_ = 0;
};

class TcpClient {
public:
    TcpClient(const char* host, uint16_t port);
    ~TcpClient();
    std::string receive();  // whatever has arrived (no wait)
    void        send(const std::string& s);
    void        close();

private:
    intptr_t sock_;
};

}  // namespace pc
}  // namespace axis
