// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The ESP32 core's WebServer. Only the headless board's AXIS-SETUP hotspot page uses it, and a PC never opens
// that hotspot (its network is up before the program starts): a server here never answers.
#pragma once
#include <cstdint>
#include <functional>

#include "WString.h"

enum HTTPMethod { HTTP_ANY = 0, HTTP_DELETE = 0, HTTP_GET = 1, HTTP_HEAD = 2, HTTP_POST = 3, HTTP_PUT = 4 };

class WebServer {
public:
    typedef std::function<void(void)> THandlerFunction;

    explicit WebServer(int port = 80) { (void)port; }
    void   begin() {}
    void   stop() {}
    void   handleClient() {}
    void   on(const String&, THandlerFunction) {}
    void   on(const String&, HTTPMethod, THandlerFunction) {}
    void   onNotFound(THandlerFunction) {}
    void   send(int code, const char* type = nullptr, const String& content = String()) { (void)code, (void)type, (void)content; }
    void   send(int code, const String& type, const String& content) { (void)code, (void)type, (void)content; }
    String arg(const String&) { return String(); }
    bool   hasArg(const String&) { return false; }
};
