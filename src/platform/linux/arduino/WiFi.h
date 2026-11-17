// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The ESP32 core's WiFi object on Linux. The PC's own network is already up when the program starts, so the
// station is always "connected"; there is no radio to scan or hotspot to open. The addresses come with the
// network layer (plan task 3).
#pragma once
#include <cstdint>

#include "IPAddress.h"
#include "WString.h"
#include "WiFiClient.h"
#include "WiFiServer.h"

typedef enum {
    WL_NO_SHIELD = 255,
    WL_IDLE_STATUS = 0,
    WL_NO_SSID_AVAIL = 1,
    WL_SCAN_COMPLETED = 2,
    WL_CONNECTED = 3,
    WL_CONNECT_FAILED = 4,
    WL_CONNECTION_LOST = 5,
    WL_DISCONNECTED = 6
} wl_status_t;

typedef enum { WIFI_OFF = 0, WIFI_STA = 1, WIFI_AP = 2, WIFI_AP_STA = 3 } wifi_mode_t;

typedef enum {
    WIFI_AUTH_OPEN = 0,
    WIFI_AUTH_WEP,
    WIFI_AUTH_WPA_PSK,
    WIFI_AUTH_WPA2_PSK,
    WIFI_AUTH_WPA_WPA2_PSK,
    WIFI_AUTH_WPA2_ENTERPRISE,
    WIFI_AUTH_WPA3_PSK,
    WIFI_AUTH_WPA2_WPA3_PSK
} wifi_auth_mode_t;

class WiFiClass {
public:
    bool        mode(wifi_mode_t) { return true; }
    wifi_mode_t getMode() { return WIFI_STA; }
    wl_status_t status() { return WL_CONNECTED; }
    wl_status_t begin(const char*, const char* = nullptr, int32_t = 0, const uint8_t* = nullptr, bool = true) {
        return WL_CONNECTED;
    }
    bool disconnect(bool = false, bool = false) { return true; }
    bool setSleep(bool) { return true; }
    bool setHostname(const char*) { return true; }

    IPAddress localIP() { return IPAddress(); }
    IPAddress gatewayIP() { return IPAddress(); }
    IPAddress subnetMask() { return IPAddress(); }
    IPAddress dnsIP(uint8_t = 0) { return IPAddress(); }
    IPAddress softAPIP() { return IPAddress(); }
    String    macAddress() { return String("00:00:00:00:00:00"); }  // a placeholder until the network layer (task 3)
    String    SSID() { return String(); }
    int8_t    RSSI() { return 0; }

    int16_t          scanNetworks(bool = false, bool = false) { return 0; }  // no radio
    void             scanDelete() {}
    String           SSID(uint8_t) { return String(); }
    int32_t          RSSI(uint8_t) { return 0; }
    int32_t          channel(uint8_t) { return 0; }
    uint8_t*         BSSID(uint8_t) { return nullptr; }
    wifi_auth_mode_t encryptionType(uint8_t) { return WIFI_AUTH_OPEN; }

    bool softAP(const char*, const char* = nullptr, int = 1, int = 0, int = 4) { return false; }  // no hotspot
    int  hostByName(const char*, IPAddress&) { return 0; }  // lookups come with task 3
};

inline WiFiClass WiFi;
