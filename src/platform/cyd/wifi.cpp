// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// Join logic adapted from the CYD Demoscene firmware's net.cpp (same author).
#include "platform/cyd/wifi.h"

#include <Preferences.h>
#include <WiFi.h>
#include <time.h>

static String joined;

const char* wifiSsid() { return joined.c_str(); }
bool        wifiHasTime() { return time(nullptr) > 1700000000; }

// open networks that belong to a gadget (TV cast setup, printers, cameras): never the LAN we want
static bool deviceHotspot(String s) {
    s.toLowerCase();
    for (const char* k : {"cast", "direct-", "setup", "vizio", "roku", "printer", "hp-", "epson", "canon", "camera", "_nomap"})
        if (s.indexOf(k) >= 0) return true;
    return false;
}

// giving the exact channel and access point from the scan is far more reliable than a plain begin()
static bool connect(int idx, const String& ssid, const String& pass) {
    const char* pw = pass.length() ? pass.c_str() : nullptr;
    for (int attempt = 0; attempt < 3; attempt++) {
        WiFi.disconnect();
        delay(100);
        if (attempt < 2) WiFi.begin(ssid.c_str(), pw, WiFi.channel(idx), WiFi.BSSID(idx));
        else WiFi.begin(ssid.c_str(), pw);
        for (int i = 0; i < 80 && WiFi.status() != WL_CONNECTED; i++) delay(100);
        if (WiFi.status() == WL_CONNECTED) return true;
        Serial.printf("axis: join %s try %d failed (status %d)\n", ssid.c_str(), attempt + 1, WiFi.status());
    }
    return false;
}

bool wifiJoinSaved() {
    String s[5], p[5];
    {
        Preferences prefs;
        prefs.begin("net", true);
        for (int i = 0; i < 5; i++) {
            s[i] = prefs.getString(("s" + String(i)).c_str(), "");
            p[i] = prefs.getString(("p" + String(i)).c_str(), "");
        }
        prefs.end();
    }
    WiFi.mode(WIFI_STA);
    WiFi.setHostname("axis");
    int found = WiFi.scanNetworks();
    Serial.printf("axis: scan found %d networks\n", found);
    // saved networks in range, strongest first, gadget hotspots last
    for (int pass = 0; pass < 2; pass++) {
        for (int idx = 0; idx < found; idx++) {
            String ssid = WiFi.SSID(idx);
            if (deviceHotspot(ssid) != (pass == 1)) continue;
            for (int k = 0; k < 5; k++) {
                if (!s[k].length() || s[k] != ssid) continue;
                Serial.printf("axis: joining %s (%d dBm)\n", ssid.c_str(), WiFi.RSSI(idx));
                if (connect(idx, ssid, p[k])) {
                    joined = ssid;
                    WiFi.scanDelete();
                    configTzTime("EST5EDT,M3.2.0,M11.1.0", "pool.ntp.org", "time.google.com", "216.239.35.0");
                    for (int i = 0; i < 80 && !wifiHasTime(); i++) delay(100);
                    return true;
                }
            }
        }
    }
    WiFi.scanDelete();
    return false;
}
