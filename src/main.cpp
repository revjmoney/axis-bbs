// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// AXIS UNDERGROUND CNC firmware entry: board, storage, settings, WiFi, the telnet listener
// and the sysop screen. On first boot it runs setup (tap: on this screen or over telnet).
// One node per board for now; callers beyond that hear "busy".
#include <Arduino.h>
#include <ESPmDNS.h>
#include <esp_heap_caps.h>
#include <WiFi.h>
#include <time.h>

#include <memory>

#include "bbs/config.h"
#include "door/session.h"
#include "bbs/node.h"
#include "bbs/setup_gate.h"
#include "bbs/setup_wizard.h"
#include "platform/cyd/ansi_screen.h"
#include "platform/cyd/board.h"
#include "platform/cyd/config_store.h"
#include "platform/cyd/flashimage.h"
#include "platform/cyd/screen.h"
#include "platform/cyd/serialfiles.h"
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
// the door the caller is in, if any (the screen copy is freed meanwhile)
static std::unique_ptr<axis::PosixHostFs>       doorFs;
static std::unique_ptr<FlashImageSource>        doorImage;
static std::unique_ptr<axis::door::DoorSession> door;
static axis::door::LockTable                    doorLocks;
static uint32_t                                 doorHeapBefore = 0, doorStartMs = 0;
static uint8_t*                                 doorIram = nullptr;  // instruction RAM lent to the door
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
    if (!screenReady()) return;  // a door is running
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
    if (s.empty() || !line) return;
    line->write((const uint8_t*)s.data(), s.size());
    line->flush();
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

// ------------------------------------------------------------------ doors
static void doorStatusLine() {  // straight to the panel: there is no screen copy during a door
    tft.fillScreen(TFT_BLACK);
    tft.setTextDatum(textdatum_t::top_left);
    tft.setTextSize(2);
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.drawString(("IN DOOR: " + node->door().name).substr(0, 26).c_str(), 6, 100);
    tft.setTextColor(TFT_ORANGE, TFT_BLACK);
    tft.drawString(node->handle().substr(0, 26).c_str(), 6, 124);
}

static void startDoor() {
    const axis::bbs::DoorEntry& d = node->door();
    sendApp(node->takeOutput());
    doorHeapBefore = ESP.getFreeHeap();
    size_t   lentBytes = 0;
    uint8_t* lentRam = screenLend(lentBytes);
    doorStatusLine();
    std::string dir = std::string(storageRoot()) + "/doors/" + d.id;
    doorFs.reset(new axis::PosixHostFs(dir));
    doorImage.reset(new FlashImageSource(d.id.c_str()));
    axis::door::DoorConfig c;
    c.dir = dir;
    c.exe = d.exe;
    c.args = d.args;
    c.localMode = false;
    c.imageSource = doorImage.get();
    c.arenas.emplace_back(lentRam, lentBytes);  // the screen copy becomes the door's first pages
    // Spare instruction RAM: malloc never hands it out (it can't be read a byte at a time), but
    // guest pages are only touched as words, so a door can use it. About 40 KB on this board.
    size_t iramBytes = heap_caps_get_largest_free_block(MALLOC_CAP_EXEC) / 4096 * 4096;
    doorIram = iramBytes ? (uint8_t*)heap_caps_malloc(iramBytes, MALLOC_CAP_EXEC) : nullptr;
    if (doorIram) c.arenas.emplace_back(doorIram, iramBytes);
    else iramBytes = 0;
    c.dropFile = d.drop == "dorinfo" ? axis::door::DropFile::Dorinfo : axis::door::DropFile::DoorSys;
    c.caller.alias = c.caller.realName = node->handle();
    c.caller.node = kNodeNumber;
    c.caller.minutesLeft = cfg.dailyMinutes;
    c.timeLimitSec = (uint32_t)cfg.dailyMinutes * 60u;
    c.clockMs = [] { return (uint64_t)millis(); };
    c.sleepMs = [](uint32_t ms) { delay(ms); };
    c.onOutOfMemory = [] {  // byte-addressable heap is what pages and everything else need
        Serial.printf("axis: door out of memory: byte heap %u, largest %u, pool %u of %u pages\n",
                      heap_caps_get_free_size(MALLOC_CAP_8BIT), heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
                      door ? (unsigned)door->poolPages() : 0u, door ? (unsigned)door->poolBudget() : 0u);
    };
    doorStartMs = millis();
    Serial.printf("axis: door %s for %s, byte heap %u + %u lent from the screen + %u instruction RAM\n", d.id.c_str(),
                  node->handle().c_str(), heap_caps_get_free_size(MALLOC_CAP_8BIT), (unsigned)lentBytes, (unsigned)iramBytes);
    door.reset(new axis::door::DoorSession(c, *doorFs, doorLocks, *line, kNodeNumber));
}

static void finishDoor() {
    using R = axis::door::DoorResult;
    using E = axis::bbs::Node::DoorEnd;
    R   r = door->result();
    int peakKb = (int)door->memStats().peakRamPages * 4;
    door.reset();
    doorImage.reset();
    doorFs.reset();
    doorLocks.releaseOwner(kNodeNumber);
    heap_caps_free(doorIram);
    doorIram = nullptr;
    screenReturn();
    Serial.printf("axis: door %s done result %d, peak RAM %d KB, %lus, heap before %u after %u, lowest %u\n", node->door().id.c_str(),
                  (int)r, peakKb, (unsigned long)((millis() - doorStartMs) / 1000), doorHeapBefore, ESP.getFreeHeap(), ESP.getMinFreeHeap());
    screenDirty = true;
    if (!line || !line->carrier() || r == R::CarrierLost) {
        hangUp();
        return;
    }
    E how = r == R::OutOfMemory ? E::OutOfMemory : r == R::TimeLimit ? E::TimeLimit : r == R::LoadError ? E::LoadError : E::Normal;
    node->resumeAfterDoor(how, millis());
    sendApp(node->takeOutput());
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
            if (cmd.length() < 160) cmd += c;
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
        } else if (cmd.startsWith("@DOORCHECK ")) {  // "@DOORCHECK lord [force]": cache LORD.EXE in flash
            String id = cmd.substring(11);
            bool   force = id.endsWith(" force");
            if (force) id = id.substring(0, id.length() - 6);
            String exe = id + ".EXE";
            exe.toUpperCase();
            axis::PosixHostFs fs(std::string(storageRoot()) + "/doors/" + id.c_str());
            FlashImageSource  src(id.c_str(), force);
            axis::door::MzInfo mz;
            size_t             len = 0;
            uint32_t           t0 = millis();
            const uint8_t*     img = src.image(fs, exe.c_str(), axis::door::kLoadSeg, mz, len);
            if (img)
                Serial.printf("axis: door %s ok: %u bytes %s in %lu ms, key %08lx, heap %u\n", id.c_str(), (unsigned)len,
                              src.rewrote() ? "written" : "already cached", (unsigned long)(millis() - t0),
                              (unsigned long)src.key(), ESP.getFreeHeap(), ESP.getMaxAllocHeap());
            else
                Serial.printf("axis: door %s FAILED: %s\n", id.c_str(), src.error());
        } else if (serialFileCommand(cmd)) {  // @PUT @LS @RM @MKDIR @BAUD (tools/sdput.py)
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
    if (door) {  // the door owns the caller's line; the BBS waits
        bool running = door->step(30);
        if (line) line->flush();  // whatever the door drew this slice
        if (!running) finishDoor();
        return;
    }
    if (line) {
        std::string app = line->takeInput();
        if (!app.empty()) callerInput(app);
        if (line && node) {
            node->poll(millis());
            if (node->state() == axis::bbs::Node::State::InDoor) {
                startDoor();
                return;
            }
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
