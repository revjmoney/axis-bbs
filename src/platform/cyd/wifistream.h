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
    // The sysop control panel: every byte sent to this caller can be copied out (the screen wall,
    // a console that broke in), and keys can be put in as if the caller typed them.
    static void (*tap)(WifiStream* s, const uint8_t* p, size_t n);
    int         tapNode = 0;  // the node number this line answers as
    void        inject(const std::string& keys) { injected_ += keys; }

protected:
    int rawRead(uint8_t* buf, size_t n) override {
        if (!injected_.empty()) {  // the sysop's keys first
            size_t k = injected_.size() < n ? injected_.size() : n;
            memcpy(buf, injected_.data(), k);
            injected_.erase(0, k);
            return (int)k;
        }
        if (!client_.available()) return 0;
        int r = client_.read(buf, n);
        return r > 0 ? r : 0;
    }
    int rawWrite(const uint8_t* p, size_t n) override {
        if (!client_.connected()) return -1;
        int w = (int)client_.write(p, n);  // 0 when the socket is full for now
        if (w > 0 && tap) tap(this, p, (size_t)w);
        return w;
    }
    bool     rawConnected() override { return client_.connected(); }
    void     rawClose() override {
        delay(100);  // let the goodbye reach the caller
        client_.stop();
    }
    uint32_t nowMs() override { return millis(); }
    void     idle() override { delay(10); }

    WiFiClient  client_;
    std::string injected_;
};

// A sysop session the control panel opened: no socket, keys come from the panel, output goes to it.
class PanelStream : public WifiStream {
public:
    PanelStream() : WifiStream(WiFiClient()) {}

protected:
    int rawRead(uint8_t* buf, size_t n) override {
        if (injected_.empty()) return 0;
        size_t k = injected_.size() < n ? injected_.size() : n;
        memcpy(buf, injected_.data(), k);
        injected_.erase(0, k);
        return (int)k;
    }
    int rawWrite(const uint8_t* p, size_t n) override {
        if (!open_) return -1;
        if (tap) tap(this, p, n);
        return (int)n;
    }
    bool rawConnected() override { return open_; }
    void rawClose() override { open_ = false; }

private:
    bool open_ = true;
};
