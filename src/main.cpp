// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// AXIS UNDERGROUND CNC firmware entry: board, storage, settings, WiFi, the telnet listener
// and the sysop screen. On first boot it runs setup (tap: on this screen or over telnet).
// One node per board for now; callers beyond that hear "busy".
#include <Arduino.h>
#include <ESPmDNS.h>
#include <WiFi.h>
#include <time.h>

#include <memory>

#include "bbs/config.h"
#include "bbs/node.h"
#include "bbs/setup_gate.h"
#include "bbs/setup_wizard.h"
#include "platform/cyd/ansi_screen.h"
#include "platform/cyd/board.h"
#include "platform/cyd/config_store.h"
#include "platform/cyd/screen.h"
#include "platform/cyd/storage.h"
#include <SD.h>
#include "platform/cyd/wfc_art.h"
#include "platform/cyd/wifi.h"
#include "platform/cyd/wifistream.h"
#include "platform/posix/posixhostfs.h"

static constexpr int kNodeNumber = 1;

enum class Mode { Normal, SetupChoice, SetupTelnet, SetupTouch };

static axis::bbs::BbsConfig cfg;
static Mode                 mode = Mode::Normal;
static String               setupPin;
static WiFiServer           server;
static std::unique_ptr<WifiStream>              line;  // the caller, when one is online
static std::unique_ptr<axis::bbs::Node>         node;
static std::unique_ptr<axis::bbs::SetupGate>    gate;
static std::unique_ptr<axis::bbs::SetupWizard>  wizard;
static uint32_t callStartMs = 0, callsToday = 0;
static String   lastCaller = "none yet";
static bool     screenDirty = true;
static int      tapX = -1, tapY = -1;  // a tap from the @TAP serial command

static uint32_t hwRandom() { return esp_random(); }

static String timeText() {
    time_t now = time(nullptr);
    if (now < 1700000000) return "clock not set";
    char b[24];
    strftime(b, sizeof b, "%Y-%m-%d %H:%M", localtime(&now));
    return b;
}

static String dialName() { return String(cfg.boardName.c_str()) + ".local"; }

static axis::bbs::SysInfo sysInfo() {
    axis::bbs::SysInfo s;
    s.ip = WiFi.localIP().toString().c_str();
    s.host = dialName().c_str();
    s.wifi = wifiSsid();
    s.rssi = WiFi.RSSI();
    s.uptimeSec = millis() / 1000;
    s.freeHeap = ESP.getFreeHeap();
    s.timeText = timeText().c_str();
    s.storage = storageSummary();
    return s;
}

// ------------------------------------------------------------------ screens (placeholder look)
static void title(const char* sub) {
    scr.fillScreen(TFT_BLACK);
    scr.setTextDatum(textdatum_t::top_left);
    scr.setTextColor(TFT_RED, TFT_BLACK);
    scr.setTextSize(2);
    String t = cfg.bbsName.c_str();
    t.toUpperCase();
    scr.drawString(t.c_str(), 8, 8);
    scr.setTextSize(1);
    scr.setTextColor(TFT_ORANGE, TFT_BLACK);
    scr.drawString(sub, 8, 30);
}

static void bigButton(int y, const char* label, uint16_t color) {
    scr.fillRoundRect(16, y, 288, 52, 8, color);
    scr.setTextColor(TFT_WHITE, color);
    scr.setTextSize(2);
    scr.setTextDatum(textdatum_t::middle_center);
    scr.drawString(label, 160, y + 26);
    scr.setTextDatum(textdatum_t::top_left);
    scr.setTextSize(1);
}

// Waiting-for-caller screen: the drippy ANSI from art/wfc.ans with live status in its panel.
static AnsiScreen wfc;
static BigText    panel;
static void drawWfc() {
    wfc.load(kWfcArt);
    wfc.render(scr, 12);  // the drippy logo and name in small cells: the top 96 pixels
    // the live panel in full-size 8x16 text: 9 rows fill the bottom 144 pixels
    const uint8_t F = 0x04, K = 0x07, V = 0x0F;  // frame in the drips' red
    const int     last = BigText::kRows - 1;
    char          b[48];
    panel.clear(K);
    panel.fill(0, 0, 40, 0xCD, F);
    panel.fill(last, 0, 40, 0xCD, F);
    panel.text(0, 0, "\xc9", F);
    panel.text(0, 39, "\xbb", F);
    panel.text(last, 0, "\xc8", F);
    panel.text(last, 39, "\xbc", F);
    for (int r = 1; r < last; r++) {
        panel.text(r, 0, "\xba", F);
        panel.text(r, 39, "\xba", F);
    }
    if (node) panel.text(0, 2, "\xb5 CALLER ONLINE \xc6", 0x0A);
    else panel.text(0, 2, "\xb5 WAITING FOR CALLER \xc6", 0x0B);
    panel.text(0, 30, "\xb5 node 1 \xc6", F);
    auto line = [&](int r, const char* label, const String& value) {
        panel.text(r, 2, label, K);
        panel.text(r, 9, value.substring(0, 29).c_str(), V);
    };
    uint32_t up = millis() / 1000;
    snprintf(b, sizeof b, "%lud %luh %lum", (unsigned long)(up / 86400), (unsigned long)(up / 3600 % 24),
             (unsigned long)(up / 60 % 60));
    line(1, "dial", dialName() + ":" + String(cfg.telnetPort));
    line(2, "ip", WiFi.localIP().toString());
    line(3, "wifi", String(wifiSsid()) + " " + String(WiFi.RSSI()) + " dBm");
    line(4, "store", storageHasSd() ? "SD card" : "internal flash");
    line(5, "calls", String(callsToday) + "  last " + lastCaller);
    line(6, "up", String(b) + "  " + String(ESP.getFreeHeap() / 1024) + " KB free");
    if (node) {
        snprintf(b, sizeof b, "%s  %lus", node->handle().empty() ? "(logging in)" : node->handle().c_str(),
                 (unsigned long)((millis() - callStartMs) / 1000));
        panel.text(7, 2, "online", K);
        panel.text(7, 9, b, 0x0E);
    } else {
        line(7, "sysop", String(cfg.sysopHandle.c_str()) + "   " + timeText().substring(11));
    }
    panel.render(scr, 96, BigText::kRows);
}

static void drawScreen() {
    if (mode == Mode::SetupChoice) {
        title("first-boot setup");
        scr.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
        scr.drawString(("WiFi: " + String(wifiSsid()) + "   " + WiFi.localIP().toString()).c_str(), 8, 50);
        scr.drawString("How do you want to set up your BBS?", 8, 70);
        bigButton(92, "ON THIS SCREEN", 0x2945);
        bigButton(156, "OVER TELNET", 0x0210);
        scr.setTextColor(TFT_DARKGREY, TFT_BLACK);
        scr.drawString("tap a button", 8, 222);
    } else if (mode == Mode::SetupTelnet) {
        title("first-boot setup");
        scr.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
        scr.drawString("From a computer on this network:", 8, 56);
        scr.setTextSize(2);
        scr.setTextColor(TFT_CYAN, TFT_BLACK);
        scr.drawString(("telnet " + dialName()).c_str(), 8, 76);
        scr.setTextSize(1);
        scr.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
        scr.drawString(("or telnet " + WiFi.localIP().toString() + " " + String(cfg.telnetPort)).c_str(), 8, 100);
        scr.drawString("Setup PIN:", 8, 128);
        scr.setTextSize(4);
        scr.setTextColor(TFT_YELLOW, TFT_BLACK);
        scr.drawString(setupPin.c_str(), 8, 144);
        scr.setTextSize(1);
        scr.setTextColor(wizard ? TFT_GREEN : TFT_DARKGREY, TFT_BLACK);
        scr.drawString(wizard ? "setup in progress over telnet..."
                              : gate ? "caller connected, waiting for the PIN" : "waiting for the sysop to call",
                       8, 200);
    } else if (mode == Mode::SetupTouch) {
        title("first-boot setup");
        scr.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
        scr.drawString("On-screen setup is coming in the next build.", 8, 70);
        bigButton(156, "BACK", 0x2945);
    } else {
        drawWfc();
    }
    screenPush();
    screenDirty = false;
}

// ------------------------------------------------------------------ network
static void applyNetwork() {
    setenv("TZ", axis::bbs::tzRule(cfg.timezone), 1);
    tzset();
    MDNS.end();
    if (MDNS.begin(cfg.boardName.c_str())) MDNS.addService("telnet", "tcp", cfg.telnetPort);
    server.end();
    server.begin(cfg.telnetPort);
    server.setNoDelay(true);
    Serial.printf("axis: listening on %s:%d, heap %u\n", dialName().c_str(), cfg.telnetPort, ESP.getFreeHeap());
}

static void sendApp(const std::string& s) {
    if (!s.empty() && line) line->write((const uint8_t*)s.data(), s.size());
}

static void hangUp() {
    line.reset();  // closes the connection
    Serial.printf("axis: hang up after %lus\n", (unsigned long)((millis() - callStartMs) / 1000));
    if (node && !node->handle().empty()) lastCaller = node->handle().c_str();
    node.reset();
    gate.reset();
    wizard.reset();
    screenDirty = true;
}

static void answer(WiFiClient incoming) {
    if (line) {  // one node per board for now
        std::string busy = axis::bbs::busyScreen();
        incoming.write((const uint8_t*)busy.data(), busy.size());
        incoming.stop();
        Serial.println("axis: busy, turned a caller away");
        return;
    }
    line.reset(new WifiStream(incoming));
    callStartMs = millis();
    Serial.printf("axis: call from %s\n", line->remoteIP().toString().c_str());
    line->start();
    if (cfg.setupDone) {
        callsToday++;
        node.reset(new axis::bbs::Node(kNodeNumber, sysInfo, cfg));
        node->begin(millis());
        sendApp(node->takeOutput());
    } else {
        gate.reset(new axis::bbs::SetupGate(setupPin.c_str()));
        gate->begin();
        sendApp(gate->takeOutput());
    }
    screenDirty = true;
}

// keystrokes from the caller, already through the telnet filter
static void callerInput(const std::string& app) {
    const uint8_t* p = (const uint8_t*)app.data();
    if (node) {
        std::string before = node->handle();
        node->input(p, app.size(), millis());
        if (node->handle() != before) screenDirty = true;
        return;
    }
    if (wizard) {
        wizard->input(p, app.size());
        sendApp(wizard->takeOutput());
        if (wizard->finished()) {
            cfg = wizard->result();
            bool ok = configSave(cfg);
            Serial.printf("axis: setup %s over telnet\n", ok ? "saved" : "SAVE FAILED");
            char b[220];
            snprintf(b, sizeof b,
                     "\r\n   \x1b[1;32mSettings saved.\x1b[0;37m Your BBS is open: call %s port %d.\r\n"
                     "   Log in as %s for the sysop menu (coming soon).\x1b[0m\r\n",
                     dialName().c_str(), cfg.telnetPort, cfg.sysopHandle.c_str());
            sendApp(b);
            hangUp();
            mode = Mode::Normal;
            applyNetwork();
        } else if (wizard->cancelled()) {
            sendApp("\r\n   Nothing was saved. Call again to start over.\x1b[0m\r\n");
            hangUp();
        }
        return;
    }
    if (gate) {
        gate->input(p, app.size());
        sendApp(gate->takeOutput());
        if (gate->passed()) {
            mode = Mode::SetupTelnet;  // the right PIN means the sysop chose telnet
            gate.reset();
            wizard.reset(new axis::bbs::SetupWizard(cfg, hwRandom));
            wizard->begin();
            sendApp(wizard->takeOutput());
            screenDirty = true;
        } else if (gate->rejected()) {
            hangUp();
        }
    }
}

// ------------------------------------------------------------------ taps and serial commands
static void handleTap(int x, int y) {
    bool top = y >= 92 && y < 144, bottom = y >= 156 && y < 208;
    if (x < 16 || x >= 304) return;
    if (mode == Mode::SetupChoice) {
        if (top) mode = Mode::SetupTouch;
        else if (bottom) mode = Mode::SetupTelnet;
        screenDirty = true;
    } else if (mode == Mode::SetupTouch && bottom) {
        mode = Mode::SetupChoice;
        screenDirty = true;
    }
}

static void handleSerial() {
    static String cmd;
    while (Serial.available()) {
        char c = Serial.read();
        if (c != '\n' && c != '\r') {
            if (cmd.length() < 80) cmd += c;
            continue;
        }
        if (cmd.startsWith("@TAP ")) {
            int sp = cmd.indexOf(' ', 5);
            tapX = cmd.substring(5, sp).toInt();
            tapY = cmd.substring(sp + 1).toInt();
        } else if (cmd == "@SHOT") {
            screenShot();
        } else if (cmd == "@SDCHECK") {  // look for the card again (cards swapped while running)
            SD.end();
            storageInit();
            screenDirty = true;
        } else if (cmd == "@SDFORMAT FORMAT") {  // bench trigger for the sysop SD tool
            if (line) {
                Serial.println("axis: sd format refused, a caller is online");
            } else {
                String result;
                bool   ok = storageFormatSd(result);
                Serial.printf("axis: sd format %s: %s\n", ok ? "ok" : "FAILED", result.c_str());
                screenDirty = true;
            }
        } else if (cmd.startsWith("@DOORLS ")) {  // list a door's folder the way the door sees it
            std::string           dir = std::string(storageRoot()) + "/doors/" + cmd.substring(8).c_str();
            axis::PosixHostFs     fs(dir);
            axis::door::FindState st;
            fs.find("*.*", 0x10, st);
            Serial.printf("axis: %s: %u entries\n", dir.c_str(), (unsigned)st.matches.size());
            for (auto& f : st.matches)
                Serial.printf("axis:   %-12s %8u%s\n", f.name83.c_str(), (unsigned)f.size, f.attr & 0x10 ? " <DIR>" : "");
        } else if (cmd == "@RESETSETUP") {
            configErase();
            Serial.println("axis: settings erased, restarting into setup");
            delay(100);
            ESP.restart();
        }
        cmd = "";
    }
}

// ------------------------------------------------------------------ main
void setup() {
    Serial.begin(115200);
    delay(200);
    const char* panel = boardInit(1, 40000000);
    if (!screenInit()) Serial.println("axis: no memory for the screen copy");
    Serial.printf("\naxis: AXIS UNDERGROUND CNC booting, panel %s, heap %u\n", panel, ESP.getFreeHeap());
    title("starting...");
    screenPush();
    storageInit();
    if (!configMount()) Serial.println("axis: internal flash filesystem failed to mount");
    cfg = configLoad();
    if (!cfg.setupDone) {
        mode = Mode::SetupChoice;
        char b[8];
        snprintf(b, sizeof b, "%06lu", (unsigned long)(esp_random() % 1000000));
        setupPin = b;
    }
    Serial.printf("axis: settings %s\n", cfg.setupDone ? "loaded" : "not set up yet, first-boot setup");
    if (!wifiJoinSaved()) {
        Serial.println("axis: no saved WiFi network in range");
        title("no WiFi");
        scr.setTextColor(TFT_RED, TFT_BLACK);
        scr.drawString("No saved WiFi network in range.", 8, 110);
        screenPush();
        return;  // the touch WiFi picker comes with the next build
    }
    Serial.printf("axis: wifi %s ip %s\n", wifiSsid(), WiFi.localIP().toString().c_str());
    applyNetwork();
    Serial.printf("axis: time %s\n", timeText().c_str());
    drawScreen();
}

void loop() {
    handleSerial();
    int         tx, ty;
    static bool wasDown = false;
    bool        down = boardTouch(tx, ty);
    if (down && !wasDown) handleTap(tx, ty);
    wasDown = down;
    if (tapX >= 0) {
        handleTap(tapX, tapY);
        tapX = tapY = -1;
    }
    if (WiFi.status() != WL_CONNECTED) {
        if (screenDirty) drawScreen();
        delay(20);
        return;
    }
    if (server.hasClient()) answer(server.available());
    if (line) {
        std::string app = line->takeInput();
        if (!app.empty()) callerInput(app);
        if (line && node) {
            node->poll(millis());
            sendApp(node->takeOutput());
            if (node->wantsHangup()) hangUp();
        }
        if (line && !line->carrier()) hangUp();
    }
    static uint32_t lastTick = 0;  // keep the clock and the time-online count moving
    if (millis() - lastTick > (node ? 1000u : 15000u)) {
        lastTick = millis();
        if (mode == Mode::Normal) screenDirty = true;
    }
    if (screenDirty) drawScreen();
    delay(5);
}
