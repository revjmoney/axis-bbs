// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// AXIS UNDERGROUND CNC firmware entry: board, WiFi, the telnet listener and the sysop's
// waiting-for-caller screen. One node per board for now; callers beyond that hear "busy".
#include <Arduino.h>
#include <ESPmDNS.h>
#include <WiFi.h>
#include <time.h>

#include <memory>

#include "bbs/node.h"
#include "net/telnet.h"
#include "platform/cyd/board.h"
#include "platform/cyd/wifi.h"

static constexpr int      kNodeNumber = 1;
static constexpr uint16_t kTelnetPort = 23;

static WiFiServer server(kTelnetPort);
static WiFiClient caller;
static std::unique_ptr<axis::net::TelnetFilter> telnet;
static std::unique_ptr<axis::bbs::Node>         node;
static uint32_t callStartMs = 0, callsToday = 0;
static String   lastCaller = "none yet";
static bool     screenDirty = true;

static String timeText() {
    time_t now = time(nullptr);
    if (now < 1700000000) return "clock not set";
    char b[24];
    strftime(b, sizeof b, "%Y-%m-%d %H:%M", localtime(&now));
    return b;
}

static axis::bbs::SysInfo sysInfo() {
    axis::bbs::SysInfo s;
    s.ip = WiFi.localIP().toString().c_str();
    s.host = "axis.local";
    s.wifi = wifiSsid();
    s.rssi = WiFi.RSSI();
    s.uptimeSec = millis() / 1000;
    s.freeHeap = ESP.getFreeHeap();
    s.timeText = timeText().c_str();
    return s;
}

// Sysop screen (placeholder look until the AXIS screens are drawn).
static void drawScreen() {
    tft.fillScreen(TFT_BLACK);
    tft.setTextDatum(textdatum_t::top_left);
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.setTextSize(2);
    tft.drawString("AXIS UNDERGROUND", 8, 8);
    tft.drawString("CNC", 8, 28);
    tft.setTextSize(1);
    tft.setTextColor(TFT_ORANGE, TFT_BLACK);
    tft.drawString("telnet BBS  -  node 1", 8, 52);
    tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    tft.drawString(("ip     " + WiFi.localIP().toString() + ":" + String(kTelnetPort)).c_str(), 8, 76);
    tft.drawString("name   axis.local", 8, 90);
    tft.drawString(("wifi   " + String(wifiSsid())).c_str(), 8, 104);
    tft.drawString(("calls  " + String(callsToday) + "    last: " + lastCaller).c_str(), 8, 118);
    tft.setTextSize(2);
    if (node) {
        tft.setTextColor(TFT_GREEN, TFT_BLACK);
        tft.drawString("CALLER ONLINE", 8, 150);
        tft.setTextColor(TFT_WHITE, TFT_BLACK);
        tft.drawString(node->handle().empty() ? "(logging in)" : node->handle().c_str(), 8, 174);
    } else {
        tft.setTextColor(TFT_CYAN, TFT_BLACK);
        tft.drawString("WAITING FOR CALLER", 8, 150);
    }
    screenDirty = false;
}

static void sendApp(const std::string& s) {
    if (s.empty() || !caller.connected()) return;
    std::string w = telnet->toNet((const uint8_t*)s.data(), s.size());
    caller.write((const uint8_t*)w.data(), w.size());
}

static void hangUp() {
    caller.stop();
    Serial.printf("axis: logoff %s after %lus\n", node ? node->handle().c_str() : "",
                  (unsigned long)((millis() - callStartMs) / 1000));
    if (node && !node->handle().empty()) lastCaller = node->handle().c_str();
    node.reset();
    telnet.reset();
    screenDirty = true;
}

void setup() {
    Serial.begin(115200);
    delay(200);
    const char* panel = boardInit(1, 40000000);
    Serial.printf("\naxis: AXIS UNDERGROUND CNC booting, panel %s, heap %u\n", panel, ESP.getFreeHeap());
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_ORANGE, TFT_BLACK);
    tft.drawString("AXIS UNDERGROUND CNC: joining WiFi...", 8, 110);
    if (!wifiJoinSaved()) {
        Serial.println("axis: no saved WiFi network in range");
        tft.fillScreen(TFT_BLACK);
        tft.setTextColor(TFT_RED, TFT_BLACK);
        tft.drawString("No saved WiFi network in range.", 8, 110);
        return;  // loop() keeps the screen; WiFi setup comes with the sysop console
    }
    Serial.printf("axis: wifi %s ip %s time %s\n", wifiSsid(), WiFi.localIP().toString().c_str(),
                  timeText().c_str());
    if (MDNS.begin("axis")) MDNS.addService("telnet", "tcp", kTelnetPort);
    server.begin();
    server.setNoDelay(true);
    Serial.printf("axis: listening on port %u, heap %u\n", kTelnetPort, ESP.getFreeHeap());
    drawScreen();
}

void loop() {
    if (WiFi.status() != WL_CONNECTED) {
        delay(50);
        return;
    }
    if (server.hasClient()) {
        WiFiClient incoming = server.available();
        if (node) {  // one node per board for now
            std::string busy = axis::bbs::busyScreen();
            incoming.write((const uint8_t*)busy.data(), busy.size());
            incoming.stop();
            Serial.println("axis: busy, turned a caller away");
        } else {
            caller = incoming;
            caller.setNoDelay(true);
            telnet.reset(new axis::net::TelnetFilter());
            node.reset(new axis::bbs::Node(kNodeNumber, sysInfo));
            callStartMs = millis();
            callsToday++;
            Serial.printf("axis: call from %s\n", caller.remoteIP().toString().c_str());
            std::string hello = telnet->initialNegotiation();
            caller.write((const uint8_t*)hello.data(), hello.size());
            node->begin(millis());
            sendApp(node->takeOutput());
            screenDirty = true;
        }
    }
    if (node) {
        uint8_t buf[128];
        while (caller.connected() && caller.available()) {
            int n = caller.read(buf, sizeof buf);
            if (n <= 0) break;
            std::string app, reply;
            telnet->fromNet(buf, n, app, reply);
            if (!reply.empty()) caller.write((const uint8_t*)reply.data(), reply.size());
            std::string before = node->handle();
            node->input((const uint8_t*)app.data(), app.size(), millis());
            if (node->handle() != before) screenDirty = true;
        }
        node->poll(millis());
        sendApp(node->takeOutput());
        if (node->wantsHangup()) {
            caller.flush();
            delay(100);
            hangUp();
        } else if (!caller.connected()) {
            hangUp();
        }
    }
    if (screenDirty) drawScreen();
    delay(5);
}
