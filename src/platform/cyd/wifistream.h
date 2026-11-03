// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// A telnet caller on the board's WiFi: TelnetStream over an Arduino WiFiClient. The BBS menus
// and the doors share this one stream.
#pragma once
#include <WiFi.h>

#include "net/telnetstream.h"

class WifiStream : public axis::net::TelnetStream {
public:
    explicit WifiStream(const WiFiClient& c) : client_(c) { client_.setNoDelay(true); }
    ~WifiStream() override { hangUp(); }
    IPAddress remoteIP() { return client_.remoteIP(); }

protected:
    int rawRead(uint8_t* buf, size_t n) override {
        if (!client_.available()) return 0;
        int r = client_.read(buf, n);
        return r > 0 ? r : 0;
    }
    int rawWrite(const uint8_t* p, size_t n) override {
        if (!client_.connected()) return -1;
        return (int)client_.write(p, n);  // 0 when the socket is full for now
    }
    bool     rawConnected() override { return client_.connected(); }
    void     rawClose() override {
        delay(100);  // let the goodbye reach the caller
        client_.stop();
    }
    uint32_t nowMs() override { return millis(); }
    void     idle() override { delay(10); }

private:
    WiFiClient client_;
};
