// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// Axis Micro BBS Firmware entry: board, storage, settings, WiFi, the telnet listener
// and the sysop screen. On first boot it runs setup (tap: on this screen or over telnet).
// One node per board for now; callers beyond that hear "busy".
#include <Arduino.h>
#include <ESPmDNS.h>
#include <esp_heap_caps.h>
#include <lwip/sockets.h>
#include <soc/soc_memory_types.h>
#include <WiFi.h>
#include <time.h>

#include <dirent.h>
#include <sys/stat.h>

#include <Preferences.h>

#include <memory>

#include "bbs/announce.h"
#include "platform/cyd/web.h"
#include "bbs/ansiviewer.h"
#include "bbs/dosshell.h"
#include "bbs/ansiterm.h"
#include "bbs/ledsignal.h"
#include "bbs/msgbase.h"
#include "bbs/password.h"
#include "bbs/rotatelog.h"
#include "bbs/theme.h"
#include "bbs/setupportal.h"
#include "cluster/doorfs.h"
#include "door/screenmirror.h"
#include "cluster/mirrorjob.h"
#include "cluster/pooledlib.h"
#include "cluster/relay.h"
#include "cluster/remotefiles.h"
#include "bbs/config.h"
#include "bbs/eventlog.h"
#include "bbs/sysop_menu.h"
#include "bbs/transferjob.h"
#include "bbs/users.h"
#include "bbs/wifipicker.h"
#include "door/session.h"
#include "bbs/node.h"
#include "bbs/setup_gate.h"
#include "bbs/setup_wizard.h"
#include "platform/cyd/ansi_screen.h"
#include "platform/cyd/board.h"
#include "platform/cyd/clusternet.h"
#include "platform/cyd/config_store.h"
#include "platform/cyd/flashimage.h"
#include "platform/cyd/screen.h"
#include "platform/cyd/sdfiles.h"
#include "platform/cyd/serialfiles.h"
#include "platform/cyd/storage.h"
#include <SD.h>
#include "platform/cyd/wfc_art.h"
#include "platform/cyd/wifi.h"
#include "platform/cyd/wifistream.h"
#include "platform/posix/posixhostfs.h"

// Size over speed for this file's own functions (screens, menus, the loop). After the includes on
// purpose: inline functions from the door headers keep -O2 (a pragma above them slowed the door CPU).
#pragma GCC optimize("Os")

static constexpr int kNodeNumber = 1;

enum class Mode { Normal, SetupChoice, SetupTelnet, SetupTouch, WifiPick };

static axis::bbs::BbsConfig cfg;
static Mode                 mode = Mode::Normal;
static String               setupPin;
static WiFiServer           server;
static std::unique_ptr<WifiStream>              line;  // the caller, when one is online
static axis::bbs::SysopMenu                     sysop;   // the touchscreen sysop menu (a tap on the WFC)
static axis::bbs::EventLog                      events;  // newest board events, for its Logs screen
static axis::bbs::RotatingLog                   eventFile("/littlefs/events.log", "/littlefs/events.old", 8192);  // 16 KB at most
static bool                                     eventFileReady = false;  // after LittleFS is mounted
static SdFileLibrary                            sdFiles;  // the file areas on the SD card, for callers
static axis::bbs::UserStore                     users;    // callers' accounts, internal flash /users.dat
static axis::bbs::WifiPicker                    picker;   // the touch WiFi picker (no saved network, or @WIFIPICK)
static Esp32ClusterNet                          clusterNet;  // the boards on this LAN acting as one BBS
static std::unique_ptr<axis::cluster::Cluster>  cluster;
static std::string                              lastStatus;
static String                                   pageText;   // the last page for the sysop ("handle: text")
static uint32_t                                 pageAtMs = 0;
static uint32_t                                 mainLostAtMs = 0;  // a failover, for the LED
static bool                                     ledReport = false;  // @LED: print what the LED shows

// The RGB LED (GPIO 4 red, 16 green, 17 blue, lit when low) on LEDC channels the backlight
// (channel 7) doesn't share a timer with.
#ifdef AXIS_LEDRING
// Rev's LED board (his esp32-led-controller: nine 10 mm LEDs on a terminal breakout, each through
// 470 ohms to ground, lit when high). Rev wants them all dark: every output pin that could carry one
// is held low, the board's blue LED (GPIO 2) included. GPIO 12 is a boot strap pin; low is safe.
static const uint8_t kDarkPins[] = {23, 22, 19, 18, 12, 14, 25, 33, 26, 27, 32, 13, 15, 16, 17, 21, 4, 5, 2};
#endif

static void ledBegin() {
#ifdef AXIS_LEDRING
    for (uint8_t p : kDarkPins) {
        pinMode(p, OUTPUT);
        digitalWrite(p, LOW);
    }
    return;
#endif
#if defined(AXIS_HEADLESS)
    ledcSetup(8, 5000, 8);  // a plain ESP32 board: one blue LED on GPIO 2, lit when high
    ledcAttachPin(2, 8);
    ledcWrite(8, 0);
    return;
#endif
    const int pins[3] = {4, 16, 17};
    for (int i = 0; i < 3; i++) {
        ledcSetup(8 + i, 5000, 8);
        ledcAttachPin(pins[i], 8 + i);
        ledcWrite(8 + i, 255);  // off
    }
}

static axis::bbs::UserSync                      userSync;     // which users still need to go over the link
static std::map<int, std::unique_ptr<axis::cluster::FileServer>> fileServers;  // main: one per follower
static std::map<int, std::unique_ptr<axis::cluster::DoorFsServer>> doorServers;
static bool servedRequest = false;  // the main answered a follower this loop turn
static uint32_t shotDoneMs = 0;      // the last @SHOT (it stalls the loop ~13 s on purpose)

// The oneliner wall: the newest 20, in internal flash on the main; followers read and post over the link.
static std::vector<std::string> oneLiners;
static void linersLoad() {
    std::string text = flashRead("/oneliners.txt");
    oneLiners.clear();
    size_t pos = 0;
    while (pos < text.size()) {
        size_t e = text.find('\n', pos);
        if (e == std::string::npos) e = text.size();
        if (e > pos) oneLiners.push_back(text.substr(pos, e - pos));
        pos = e + 1;
    }
}
static void linersAddLocal(const std::string& line) {
    oneLiners.push_back(line.substr(0, 100));
    while (oneLiners.size() > 20) oneLiners.erase(oneLiners.begin());
    std::string text;
    for (auto& l : oneLiners) text += l + "\n";
    flashWrite("/oneliners.txt", text);
}
// The last callers: the newest 20 logins ("handle|node|time|location"), kept like the oneliners.
static std::vector<std::string> lastCallers;
static void callersLoad() {
    std::string text = flashRead("/callers.txt");
    lastCallers.clear();
    for (size_t pos = 0; pos < text.size();) {
        size_t e = text.find('\n', pos);
        if (e == std::string::npos) e = text.size();
        if (e > pos) lastCallers.push_back(text.substr(pos, e - pos));
        pos = e + 1;
    }
}
static void callersAddLocal(const std::string& line) {
    lastCallers.push_back(line.substr(0, 120));
    while (lastCallers.size() > 20) lastCallers.erase(lastCallers.begin());
    std::string text;
    for (auto& l : lastCallers) text += l + "\n";
    flashWrite("/callers.txt", text);
}
static bool asFollower();

// The board's small shared lists (rumors, the BBS list): 50 lines each in internal flash on the main.
static bool listName(const std::string& n) { return n == "rumors" || n == "bbslist" || n == "votes" || n == "ballots"; }
static std::vector<std::string> listLoad(const std::string& n) {
    std::vector<std::string> v;
    if (!listName(n)) return v;
    std::string text = flashRead(("/list_" + n + ".txt").c_str());
    for (size_t pos = 0; pos < text.size();) {
        size_t e = text.find('\n', pos);
        if (e == std::string::npos) e = text.size();
        if (e > pos) v.push_back(text.substr(pos, e - pos));
        pos = e + 1;
    }
    return v;
}
static void listAddLocal(const std::string& n, const std::string& line) {
    if (!listName(n)) return;
    std::vector<std::string> v = listLoad(n);
    v.push_back(line.substr(0, 120));
    while (v.size() > (n == "ballots" ? 500u : n == "rumors" ? 300u : 50u)) v.erase(v.begin());
    std::string text;
    for (auto& l : v) text += l + "\n";
    flashWrite(("/list_" + n + ".txt").c_str(), text);
}

// The sysop's own screens for the theme (MATRIX.ANS, MAIN.ANS): from the card on the main, 7 KB at
// most, cut at the SAUCE record; a board without the card asks the main once and keeps the answer,
// so every node looks and keys the same. The settings arriving (a theme change) forget them.
// Scene news for Discord. TLS needs about 45 KB of heap, more than a busy CYD main has, so the
// posting is done by whichever board holds the webhook (set it on a headless node): a follower
// hands its news to the main, the main sends it to every board, and the board with the webhook posts.
constexpr int kAnnounceKind = 21, kAnnounceAllKind = 22;
static void announceOut(const std::string& line) {
    if (!cluster) web::announce(String("**") + cfg.bbsName.c_str() + "** " + line.c_str());
    else if (asFollower()) cluster->send("", kAnnounceKind, 0, line);
    else cluster->send("", kAnnounceAllKind, 0, line);
}

static std::map<std::string, std::string> artCache;
static std::string themeArtLocal(const std::string& name) {
    std::string s;
    if (!storageHasSd() || name.find('/') != std::string::npos) return s;
    if (FILE* f = fopen(("/sd/themes/" + cfg.theme + "/" + name).c_str(), "rb")) {
        char b[512];
        for (size_t n; s.size() < 7168 && (n = fread(b, 1, sizeof b, f)) > 0;) s.append(b, n);
        fclose(f);
    }
    if (size_t sauce = s.find('\x1a'); sauce != std::string::npos) s.resize(sauce);
    if (s.size() > 7168) s.resize(7168);
    return s;
}
static std::string themeArt(const std::string& name) {
    if (!asFollower()) return themeArtLocal(name);
    auto it = artCache.find(name);
    if (it != artCache.end()) return it->second;
    std::string reply;
    std::string art = cluster->request(std::string("T\x1f") + name, reply, 3000) && !reply.empty() && reply[0] == '1' ? reply.substr(1) : "";
    artCache[name] = art;
    return art;
}
// The shared lists for the local console (callers use the same over their own node setup).
static std::vector<std::string> sharedList(const std::string& n) {
    if (!asFollower()) return n == "callers" ? lastCallers : listLoad(n);
    std::vector<std::string> v;
    std::string              reply;
    if (cluster->request(n == "callers" ? std::string("K") : std::string("Y\x1f") + n, reply, 3000) && !reply.empty() && reply[0] == '1')
        for (size_t pos = 1; pos < reply.size();) {
            size_t e = reply.find('\n', pos);
            if (e == std::string::npos) e = reply.size();
            if (e > pos) v.push_back(reply.substr(pos, e - pos));
            pos = e + 1;
        }
    return v;
}
static void sharedListAdd(const std::string& n, const std::string& line) {
    if (!asFollower()) listAddLocal(n, line);
    else {
        std::string reply;
        cluster->request(std::string("Z\x1f") + n + "\x1f" + line, reply, 3000);
    }
}
static uint32_t lastServedMs = 0;  // main: a follower's door files
static axis::cluster::RemoteFileLibrary         remoteFiles([](const std::string& req, std::string& reply) {
    return cluster && cluster->request(req, reply);
});

// SD card clustering (cluster/pooledlib.h, mirrorjob.h): on the main, this board's card and every
// card follower's as one library, and the mirrored areas copied onto a second card in the background.
// A follower with a card answers the main's requests for it (cardServer).
static std::map<std::string, std::unique_ptr<axis::cluster::RemoteFileLibrary>> cardFiles;  // main: one per card board
static std::unique_ptr<axis::cluster::FileServer> cardServer;  // follower with a card: the main's requests
static std::string cardLabel(const std::string& id) {  // "node 2" (its lowest node), or its id when it is gone
    int low = 0;
    if (cluster)
        for (auto& n : cluster->nodes())
            if (n.id == id && (!low || n.number < low)) low = n.number;
    return low ? "node " + std::to_string(low) : "board " + id;
}
static std::vector<axis::cluster::PoolCard> poolCards() {
    std::vector<axis::cluster::PoolCard> v;
    if (storageHasSd()) v.push_back({"local", "this board", &sdFiles, storageFreeBytes(), true});
    if (!cluster || cluster->role() != axis::cluster::Role::Main) return v;
    for (auto& c : cluster->cardBoards()) {
        auto& lib = cardFiles[c.id];
        if (!lib) {
            std::string id = c.id;
            lib.reset(new axis::cluster::RemoteFileLibrary([id](const std::string& req, std::string& reply) {
                return cluster && cluster->requestTo(id, req, reply, 2500);
            }));
        }
        v.push_back({c.id, cardLabel(c.id), lib.get(), c.freeBytes, c.alive});
    }
    return v;
}
#if AXIS_SD_POOL
static axis::cluster::PooledFileLibrary pooledFiles(poolCards);
static axis::cluster::MirrorJob         mirrorJob(pooledFiles, [] {
    std::vector<std::string> v;
    for (auto& a : cfg.areas)
        if (a.mirror) v.push_back(a.id);
    return v;
});
#endif
static Mode                                     afterPick = Mode::Normal;
static size_t                                   pendingSeen = 0;
// the door the caller is in, if any (the screen copy is freed meanwhile)
static std::unique_ptr<axis::door::HostFs>      doorFs;     // the door's folder: here, or the main's over the link
static std::unique_ptr<axis::door::LockTable>   doorRemoteLocks;  // a follower's door locks the main's records
static std::unique_ptr<FlashImageSource>        doorImage;
static std::unique_ptr<axis::door::DoorSession> door;
static std::unique_ptr<axis::door::ScreenMirror> doorMirror;  // a door that draws on the PC screen (LORDCFG): sent as ANSI
static axis::door::LockTable                    doorLocks;
static std::unique_ptr<axis::bbs::AnsiViewer>   viewer;  // a door built into the firmware, when running
static std::unique_ptr<axis::bbs::DosShell>     dosShell;  // the DOS Prompt door, when running
static std::unique_ptr<axis::PosixHostFs>       dosFs;     // its sandbox: <storage>/dosbox, nothing else
static axis::bbs::DosGate                       dosGate;   // wrong-password count and lockout, board-wide
static std::unique_ptr<axis::bbs::TransferJob>  job;     // a download or upload, when one is running
static uint32_t                                 jobStartMs = 0, jobDrawnMs = 0;
static bool                                     jobLent = false;  // the screen copy is the upload checker's workspace
static uint32_t                                 doorHeapBefore = 0, doorStartMs = 0;
static uint8_t*                                 doorIram = nullptr;  // instruction RAM lent to the door
static std::unique_ptr<axis::bbs::Node>         node;
static std::unique_ptr<axis::bbs::SetupGate>    gate;
static std::unique_ptr<axis::bbs::SetupWizard>  wizard;
static uint32_t callStartMs = 0, callsToday = 0;

// Several callers per board. Each loop turn serves one session at a time: its line and node are
// moved into the globals above (so the one-caller code works unchanged), then moved back. Doors
// and transfers use the lent screen memory, so one session at a time holds that slot (busySlot).
struct Session {
    std::unique_ptr<WifiStream>      line;
    std::unique_ptr<axis::bbs::Node> node;
    uint32_t                         startMs = 0;
    std::string                      lastStatus;
};
static std::vector<Session> sessions(1);
static int                  curSession = -1;  // whose line and node the globals hold, -1 = none
static int                  busySlot = -1;    // the session using the door/transfer slot
static void bindSession(int i) {
    curSession = i;
    line = std::move(sessions[(size_t)i].line);
    node = std::move(sessions[(size_t)i].node);
    callStartMs = sessions[(size_t)i].startMs;
}
static void unbindSession() {
    if (curSession < 0) return;
    sessions[(size_t)curSession].line = std::move(line);
    sessions[(size_t)curSession].node = std::move(node);
    sessions[(size_t)curSession].startMs = callStartMs;
    curSession = -1;
}
static WifiStream* sessionLine(size_t i) { return (int)i == curSession ? line.get() : sessions[i].line.get(); }
static axis::bbs::Node* sessionNode(size_t i) { return (int)i == curSession ? node.get() : sessions[i].node.get(); }
static int callersOnline() {
    int n = 0;
    for (size_t i = 0; i < sessions.size(); i++) n += sessionLine(i) != nullptr;
    return n;
}
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
    s.freeHeap = heap_caps_get_free_size(MALLOC_CAP_8BIT);  // byte-addressable (getFreeHeap counts IRAM too)
    s.timeText = timeText().c_str();
    s.storage = storageSummary();
    s.hardware = "ESP32 CYD " + std::to_string(ESP.getCpuFreqMHz()) + " MHz " +
                 std::to_string(ESP.getFlashChipSize() >> 20) + " MB flash";
    s.callsToday = callsToday;
    s.nodesTotal = (int)sessions.size();
    s.nodes.clear();
    for (size_t i = 0; i < sessions.size(); i++) s.nodes.push_back(sessionNode(i) ? sessionNode(i)->handle() : std::string());
    if (cluster && cluster->nodes().size() > 1) {  // every board's node, in number order
        auto all = cluster->nodes();
        std::sort(all.begin(), all.end(), [](const axis::cluster::NodeInfo& a, const axis::cluster::NodeInfo& b) { return a.number < b.number; });
        s.nodes.clear();
        for (auto& n : all) s.nodes.push_back(n.handle);
        s.nodesTotal = (int)all.size();
    }
    return s;
}

// A board event: on the serial log and the sysop's Logs screen, stamped with the time.
// byte heap free and its largest block, in KB: what every string and buffer competes for
static String heapText() {
    return " [" + String(heap_caps_get_free_size(MALLOC_CAP_8BIT) / 1024) + "/" +
           String(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) / 1024) + " KB]";
}

static void announceOut(const std::string& line);
static void logEvent(const String& what) {
    time_t now = time(nullptr);
    char   stamp[8] = "--:--";
    if (now > 1700000000) strftime(stamp, sizeof stamp, "%H:%M", localtime(&now));
    events.add(std::string(stamp) + " " + what.c_str());
    if (eventFileReady) eventFile.append(std::string(stamp) + " " + what.c_str());
    Serial.printf("axis: %s\n", what.c_str());
    std::string a = axis::bbs::announceLine(what.c_str());  // scene news for the Discord channel
    if (!a.empty()) announceOut(a);
}

static void saveUsers() {
    if (!flashWrite("/users.dat", users.format())) logEvent("users: SAVE FAILED");
    if (cluster)  // a follower tells the main; the main tells every follower
        for (auto& rec : userSync.changed(users)) cluster->sendUser(rec);
    if (users.pending() > pendingSeen) logEvent("new user application waiting: tap USERS");
    pendingSeen = users.pending();
    screenDirty = true;
}

static bool asFollower() { return cluster && cluster->role() == axis::cluster::Role::Follower; }

// The message base: on the main (SD card, or internal flash), and the main's over the link on every
// follower, so the whole cluster is one conversation.
static std::unique_ptr<axis::bbs::FileMessageStore> localMsgs;
static axis::bbs::RemoteMessageStore remoteMsgs([](const std::string& req, std::string& reply) {
    return cluster && cluster->request(req, reply, 4000);
});
static axis::bbs::MessageStore& callerMessages() {
    if (!localMsgs) {
        std::string root = std::string(storageRoot()) + "/msgs";
        ::mkdir(root.c_str(), 0777);
        localMsgs.reset(new axis::bbs::FileMessageStore(root));
    }
    if (asFollower()) return remoteMsgs;
    return *localMsgs;
}

// A follower (card or not) sees the main's library, which is every card pooled; the main pools its
// own card with its card followers'; a board on its own has just its card.
static axis::bbs::FileLibrary& callerFiles() {
    if (asFollower()) return remoteFiles;
#if AXIS_SD_POOL
    if (cluster && cluster->role() == axis::cluster::Role::Main && !cluster->cardBoards().empty()) return pooledFiles;
#endif
    return sdFiles;
}

static uint32_t unixNow() {
    time_t t = time(nullptr);
    return t > 1700000000 ? (uint32_t)t : 0;
}

// File areas: one folder each under /files on the SD card (none without a card).
static void makeAreaFolders() {
    if (!storageHasSd()) return;
    ::mkdir("/sd/files", 0777);
    for (auto& a : cfg.areas) ::mkdir(("/sd/files/" + a.id).c_str(), 0777);
}

static std::vector<int> areaFileCounts() {
    std::vector<int> n;
    for (auto& a : cfg.areas) {
        int count = -1;
        if (storageHasSd())
            if (DIR* d = opendir(("/sd/files/" + a.id).c_str())) {
                count = 0;
                while (dirent* e = readdir(d))  // FILES.BBS is the area's description list, not a file
                    count += e->d_name[0] != '.' && strcasecmp(e->d_name, "FILES.BBS") != 0;
                closedir(d);
            }
        n.push_back(count);
    }
    return n;
}

// ------------------------------------------------------------------ screens (placeholder look)
static void title(const char* sub) {
    scr.fillScreen(pal::BLACK);
    scr.setTextDatum(textdatum_t::top_left);
    scr.setTextColor(pal::ORANGE, pal::BLACK);
    scr.setTextSize(2);
    String t = cfg.bbsName.c_str();
    t.toUpperCase();
    scr.drawString(t.c_str(), 8, 8);
    scr.setTextSize(1);
    scr.setTextColor(pal::ORANGE, pal::BLACK);
    scr.drawString(sub, 8, 30);
}

static void bigButton(int y, const char* label, uint16_t color) {
    scr.fillRoundRect(16, y, 288, 52, 8, color);
    scr.setTextColor(pal::WHITE, color);
    scr.setTextSize(2);
    scr.setTextDatum(textdatum_t::middle_center);
    scr.drawString(label, 160, y + 26);
    scr.setTextDatum(textdatum_t::top_left);
    scr.setTextSize(1);
}

// Waiting-for-caller screen: the drippy ANSI from art/wfc.ans with live status in its panel.
static BigText    panel;
// The waiting-for-caller screen: the board's activity at a glance. Blinking status lights, one
// line per node on this board (tap one for its details), the newest events, a battery spot.
static int detailSession = -1;  // the node detail screen is up for this session (-1: the WFC)

static String sessionLineText(size_t i) {  // "Spindle Kid  files: ANSI Packs  12m", or waiting
    axis::bbs::Node* n = sessionNode(i);
    if (!n) return "waiting for a caller";
    uint32_t since = (int)i == curSession ? callStartMs : sessions[i].startMs;
    uint32_t m = (millis() - since) / 60000;
    String   who = n->handle().empty() ? String("(new caller)") : String(n->handle().c_str());
    return who + "  " + n->activityText().c_str() + "  " + String(m) + "m";
}

static void drawBattery() {  // top right: a battery outline; "?" until a sensor (a divider to GPIO35) is fitted
    const int x = 288, y = 2;
    scr.drawRect(x, y, 26, 12, pal::DARKGREY);
    scr.fillRect(x + 26, y + 3, 3, 6, pal::DARKGREY);
    scr.setFont(&fonts::Font0);
    scr.setTextDatum(textdatum_t::top_left);
    scr.setTextColor(pal::DARKGREY, pal::BLACK);
    scr.drawString("?", x + 10, y + 2);
    // the local sysop login, under the battery
    scr.fillRoundRect(272, 18, 46, 18, 4, pal::DIM);
    scr.drawRoundRect(272, 18, 46, 18, 4, pal::ORANGE);
    scr.setTextColor(pal::WHITE, pal::DIM);
    scr.setTextDatum(textdatum_t::middle_center);
    scr.drawString("LOCAL", 295, 27);
    scr.setTextDatum(textdatum_t::top_left);
}

// The local sysop login: the BBS on the board's own screen, the sysop logged in, an 80x25
// terminal in 4x8 cells, a touch keyboard for typing (a line at a time, sent with Enter).
static std::unique_ptr<axis::bbs::Node> localNode;
static std::unique_ptr<axis::bbs::AnsiTerm> localTerm;    // (only while the console is open: ~9 KB)
static std::unique_ptr<AnsiScreen>          localScreen;
static bool                              localKeys = false;  // the keyboard is up
static bool                              localZoom = true;   // 40 columns in 8x8 cells (readable), or all 80 in 4x8
static int                               localPan = 0;       // zoomed: which 40 columns (0 or 40)
static axis::bbs::TouchKeyboard          localKb("type, then OK", false, 70);
static void localFeed() {
    if (!localNode) return;
    std::string out = localNode->takeOutput();
    if (!out.empty()) {
        localTerm->feed(out);
        screenDirty = true;
    }
}
static void endLocal() {
    localNode.reset();
    localTerm.reset();
    localScreen.reset();
    localKeys = false;
    logEvent("local sysop logged off");
    screenDirty = true;
}
static void drawLocal() {
    scr.fillScreen(pal::BLACK);
    scr.setFont(&fonts::Font2);
    auto button = [](int x, int y, int w, int h, const char* label, bool lit) {
        uint16_t fill = lit ? pal::ORANGE : pal::DIM;
        scr.fillRoundRect(x, y, w, h, 5, fill);
        scr.drawRoundRect(x, y, w, h, 5, lit ? pal::WHITE : pal::ORANGE);
        scr.setTextColor(lit ? pal::BLACK : pal::WHITE, fill);
        scr.setTextDatum(textdatum_t::middle_center);
        scr.drawString(label, x + w / 2, y + h / 2);
        scr.setTextDatum(textdatum_t::top_left);
    };
    if (localKeys) {  // typing: what's typed on top, the keys below
        scr.setTextColor(pal::ORANGE, pal::BLACK);
        scr.drawString("LOCAL: type, then OK", 8, 4);
        scr.drawRect(4, 28, 312, 30, pal::ORANGE);
        scr.setTextColor(pal::WHITE, pal::BLACK);
        scr.drawString((localKb.shown() + "_").c_str(), 10, 35);
        for (auto& k : localKb.keys()) button(k.x, k.y, k.w, k.h, k.label.c_str(), k.lit);
    } else {
        for (int r = 0; r < axis::bbs::AnsiTerm::kRows; r++)
            for (int c = 0; c < axis::bbs::AnsiTerm::kCols; c++) {
                const auto& cell = localTerm->cell(r, c);
                localScreen->cells[r][c] = {cell.ch, cell.attr};
            }
        if (localZoom) {  // follow the cursor across the two halves
            if (localTerm->col() >= localPan + 40) localPan = 40;
            else if (localTerm->col() < localPan) localPan = 0;
            localScreen->renderZoom(scr, axis::bbs::AnsiTerm::kRows, localPan);  // 200 pixels, 40 columns
        } else {
            localScreen->renderSharp(scr, axis::bbs::AnsiTerm::kRows);  // 200 pixels of the BBS, all 80 columns, crisp
        }
        button(4, 206, 58, 30, "KEYS", false);
        button(66, 206, 58, 30, "ENTER", false);
        button(128, 206, 58, 30, localZoom ? "FULL" : "ZOOM", false);
        button(190, 206, 58, 30, localPan ? "<<" : ">>", false);
        button(256, 206, 60, 30, "EXIT", true);
    }
    scr.setFont(&fonts::Font0);
    localTerm->clean();
}

static void drawWfc() {
    AnsiScreen::renderArt(scr, kWfcArt, kWfcRows);  // the compact drippy logo in small cells, from flash: the top 64 pixels
    drawBattery();
    const uint8_t F = 0x06, K = 0x07, V = 0x0F, DIMC = 0x08;
    const int     rows = 11, last = rows - 1;
    char          b[64];
    bool          blink = (millis() / 500) % 2;
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
    panel.text(0, 2, callersOnline() ? "\xb5 ACTIVITY \xc6" : "\xb5 WAITING FOR CALLER \xc6", V);
    {
        bool   main = !cluster || cluster->role() == axis::cluster::Role::Main;
        String nums;  // "node 2+3": every node (session) this board serves
        if (cluster && !cluster->numbers().empty())
            for (size_t i = 0; i < cluster->numbers().size(); i++) nums += (i ? "+" : "") + String(cluster->numbers()[i]);
        else
            for (size_t i = 0; i < sessions.size(); i++) nums += (i ? "+" : "") + String((int)i + 1);
        snprintf(b, sizeof b, "\xb5 node %s \xc6", nums.c_str());
        panel.text(0, 39 - (int)strlen(b), b, main ? F : DIMC);
    }
    // the status lights: steady = on, blinking = busy, dim = off
    bool anyDoor = door || viewer || dosShell, anyXfer = job != nullptr;
    if (cluster && cluster->nodes().size() > 1)
        for (auto& n : cluster->nodes()) {
            anyDoor = anyDoor || n.activity == "Doors";
            anyXfer = anyXfer || n.activity == "Transfer";
        }
    bool paged = pageText.length() && millis() - pageAtMs < 60000;
    struct Light {
        const char* name;
        uint8_t     on;
        bool        lit, busy;
    } lights[] = {
        {"NET", 0x0A, WiFi.status() == WL_CONNECTED, callersOnline() > 0},
        {"LINK", 0x0B, cluster && cluster->nodes().size() > 1, cluster && cluster->role() == axis::cluster::Role::Main},
        {"SD", 0x06, storageHasSd(), anyXfer},
        {"CALL", 0x0F, callersOnline() > 0, callersOnline() > 0},
        {"DOOR", 0x04, anyDoor, anyDoor},
        {"PAGE", 0x0D, paged, paged},
    };
    int col = 2;
    for (auto& l : lights) {
        bool show = l.lit && (!l.busy || blink);
        panel.text(1, col, "\xfe", show ? l.on : (l.lit ? DIMC : 0x08));
        panel.text(1, col + 1, l.name, l.lit ? K : DIMC);
        col += 2 + (int)strlen(l.name) + 1;
    }
    panel.fill(2, 1, 38, 0xC4, DIMC);
    // one line per node on this board
    for (size_t i = 0; i < sessions.size() && i < 4; i++) {
        int    r = 3 + (int)i;
        String t = sessionLineText(i);
        int num = cluster && i < cluster->numbers().size() ? cluster->numbers()[i] : (int)i + 1;
        snprintf(b, sizeof b, "%2d", num);
        panel.text(r, 2, b, F);
        panel.text(r, 5, axis::bbs::theme::marquee(t.c_str(), 33, millis()).c_str(), sessionNode(i) ? V : DIMC);
    }
    if (sessions.size() < 4) {  // a free row: the system ticker (what used to fill this screen), scrolling
        uint32_t up = millis() / 1000;
        snprintf(b, sizeof b, "up %lud %luh %lum", (unsigned long)(up / 86400), (unsigned long)(up / 3600 % 24), (unsigned long)(up / 60 % 60));
        String others;  // the other boards' nodes (headless ones too): who's on, where
        if (cluster)
            for (auto& n : cluster->nodes()) {
                bool mine = false;
                for (int k : cluster->numbers()) mine = mine || k == n.number;
                if (mine) continue;
                others += "n" + String(n.number) + " " + (n.handle.empty() ? String("free") : String(n.handle.c_str()) + " " + n.activity.c_str()) + "  ";
            }
        String tick = (others.length() ? "other nodes: " + others + "\xb3 " : String()) + dialName() + ":" + String(cfg.telnetPort) + "  " +
                      WiFi.localIP().toString() + "  " + String(wifiSsid()) + " " +
                      String(WiFi.RSSI()) + " dBm  " + (storageHasSd() ? "SD " + String(cfg.areas.size()) + " areas" : String("no SD")) + "  " +
                      b + "  " + String(heap_caps_get_free_size(MALLOC_CAP_8BIT) / 1024) + " KB free  calls " + String(callsToday) +
                      " last " + lastCaller + "  " + cfg.location.c_str();
        panel.text(6, 2, axis::bbs::theme::marquee(tick.c_str(), 37, millis()).c_str(), DIMC);
    }
    // the newest events
    panel.fill(7, 1, 38, 0xC4, DIMC);
    const auto& ev = events.lines();
    for (int k = 0; k < 2; k++) {
        if ((int)ev.size() - 1 - k < 0) break;
        const std::string& e = ev[ev.size() - 1 - k];
        panel.text(9 - k, 2, axis::bbs::theme::marquee(e, 37, millis()).c_str(), k == 0 ? K : DIMC);
    }
    if (paged) {  // a page for the sysop takes the event lines' place
        panel.text(8, 2, "PAGE ", 0x0C);
        panel.text(8, 7, axis::bbs::theme::marquee(pageText.c_str(), 31, millis()).c_str(), V);
    }
    if (users.pending()) {  // applications waiting for the sysop
        snprintf(b, sizeof b, "\xb5 %u NEW USER%s \xc6", (unsigned)users.pending(), users.pending() == 1 ? "" : "S");
        panel.text(last, 39 - (int)strlen(b), b, blink ? 0x0A : F);
    } else {
        String now = timeText();
        snprintf(b, sizeof b, "\xb5 %s \xc6", now[4] == '-' ? now.substring(11).c_str() : "tap: sysop");
        panel.text(last, 39 - (int)strlen(b), b, DIMC);
    }
    panel.render(scr, 64, rows);
}

// A node's details (tap its line on the WFC): who, where, how long, and a KICK for the sysop.
static void drawNodeDetail(int i) {
    scr.fillScreen(pal::BLACK);
    axis::bbs::Node* n = sessionNode((size_t)i);
    WifiStream*      l = sessionLine((size_t)i);
    int num = cluster && (size_t)i < cluster->numbers().size() ? cluster->numbers()[(size_t)i] : i + 1;
    BigText t;
    t.clear(0x07);
    char b[64];
    snprintf(b, sizeof b, "NODE %d", num);
    t.text(0, 1, b, 0x0F);
    t.fill(1, 1, 38, 0xC4, 0x06);
    auto row = [&](int r, const char* k, const String& v) {
        t.text(r, 1, k, 0x07);
        t.text(r, 10, axis::bbs::theme::marquee(v.c_str(), 29, millis()).c_str(), 0x0F);
    };
    if (!n || !l) {
        row(3, "caller", "nobody: waiting for a call");
    } else {
        uint32_t since = i == curSession ? callStartMs : sessions[(size_t)i].startMs;
        uint32_t secs = (millis() - since) / 1000;
        row(2, "caller", n->handle().empty() ? String("(new caller)") : String(n->handle().c_str()));
        row(3, "doing", n->activityText().c_str());
        snprintf(b, sizeof b, "%lum %02lus", (unsigned long)(secs / 60), (unsigned long)(secs % 60));
        row(4, "online", b);
        row(5, "from", l->remoteIP().toString());
        row(6, "sysop", n->isSysop() ? "yes" : "no");
        if (axis::bbs::User* u = users.find(n->handle())) {
            row(7, "calls", String(u->calls));
            row(8, "up/down", String(u->uploads / 1024) + " KB / " + String(u->downloads / 1024) + " KB");
        }
    }
    t.render(scr, 0, 13);
    scr.setFont(&fonts::Font2);
    scr.setTextDatum(textdatum_t::middle_center);
    scr.fillRoundRect(8, 204, 100, 30, 6, pal::DIM);
    scr.drawRoundRect(8, 204, 100, 30, 6, pal::ORANGE);
    scr.setTextColor(pal::WHITE, pal::DIM);
    scr.drawString("BACK", 58, 219);
    if (n && l) {
        scr.fillRoundRect(212, 204, 100, 30, 6, pal::ORANGE);
        scr.drawRoundRect(212, 204, 100, 30, 6, pal::WHITE);
        scr.setTextColor(pal::BLACK, pal::ORANGE);
        scr.drawString("KICK", 262, 219);
    }
    scr.setTextDatum(textdatum_t::top_left);
    scr.setFont(&fonts::Font0);
}

// The sysop menu: drippy title, the screen's text in big VGA cells, then the touch buttons.
static void drawSysop() {
    axis::bbs::SysopData d;
    d.cfg = cfg;
    d.info = sysInfo();
    d.log = events;
    d.users = users;
    if (sysop.screen() == axis::bbs::SysopMenu::Screen::Files) d.areaFiles = areaFileCounts();
    static BigText t;
    t.clear(0x07);
    std::string ttl = sysop.title();
    t.text(0, 1, ttl.c_str(), 0x0F);
    t.text(0, 38 - 10, cfg.boardName.substr(0, 10).c_str(), 0x08);
    for (int c = 1; c < 1 + (int)ttl.size(); c++)  // paint drips under the title
        if ((c * 7 + ttl[c - 1]) % 3 == 0) t.text(1, c, (c % 2) ? "\xdb" : "\xdf", 0x06);
    t.fill(1, 1 + (int)ttl.size() + 1, 38 - (int)ttl.size(), 0xc4, 0x08);
    auto lines = sysop.lines(d);
    for (size_t i = 0; i < lines.size(); i++) t.text(2 + (int)i, 1, lines[i].c_str(), 0x0F);
    t.render(scr, 0, 13);
    scr.fillRect(0, 208, 320, 32, pal::BLACK);
    scr.setFont(&fonts::Font2);
    scr.setTextDatum(textdatum_t::middle_center);
    for (auto& b : sysop.buttons()) {
        // Rev's scheme: dark grey buttons outlined in orange; erase/redo buttons solid orange
        uint16_t fill = b.danger ? pal::ORANGE : pal::DIM, ink = b.danger ? pal::BLACK : pal::WHITE;
        scr.fillRoundRect(b.x, b.y, b.w, b.h, 6, fill);
        scr.drawRoundRect(b.x, b.y, b.w, b.h, 6, b.danger ? pal::WHITE : pal::ORANGE);
        scr.setTextColor(ink, fill);
        scr.drawString(b.label.c_str(), b.x + b.w / 2, b.y + b.h / 2);
    }
    scr.setTextDatum(textdatum_t::top_left);
    scr.setFont(&fonts::Font0);
}

// The sysop panel while a caller downloads or uploads: a drippy window with a live progress bar.
static void drawTransfer() {
    if (!screenReady()) return;  // lent to the upload checker
    static BigText t;
    const auto&    r = job->request();
    uint32_t       done = job->bytes(), size = job->size(), secs = (millis() - jobStartMs) / 1000;
    char           b[48];
    t.clear(0x07);
    const uint8_t O = 0x06, G = 0x08, W = 0x0F, L = 0x07;  // orange, dark grey, white, light grey
    t.fill(0, 0, 40, 0xDC, O);
    t.text(1, 0, "\xdb", O);
    t.text(1, 39, "\xdb", O);
    t.text(1, 2, r.upload ? "U P L O A D" : "D O W N L O A D", W);
    t.text(1, 30, r.proto == 'Z' ? "ZMODEM" : r.proto == 'K' ? "XMODEM-1K" : "XMODEM", G);
    t.fill(2, 0, 40, 0xDB, O);
    static const char drips[] = "# ## # ### ## # #### # ## ### # ## ## #";
    for (int c = 0; c < 39; c++)
        if (drips[c] == '#') t.text(3, c, (c * 7 % 3) ? "\xdb" : "\xdf", c % 7 == 3 ? G : O);
    for (int c = 0; c < 39; c++)
        if (drips[c] == '#' && c % 3 == 0) t.text(4, c, "\xdf", O);
    std::string name = (r.upload ? job->savedName() : r.name);
    t.text(6, 1, "file", L);
    t.text(6, 10, (name.empty() ? std::string("(waiting)") : name).substr(0, 29).c_str(), W);
    t.text(7, 1, "caller", L);
    axis::bbs::Node* who = busySlot >= 0 ? sessionNode((size_t)busySlot) : nullptr;
    t.text(7, 10, who ? who->handle().substr(0, 29).c_str() : "", W);
    snprintf(b, sizeof b, "%lu of %lu bytes", (unsigned long)done, (unsigned long)size);
    t.text(8, 1, "moved", L);
    t.text(8, 10, size ? b : (std::to_string(done) + " bytes").c_str(), W);
    snprintf(b, sizeof b, "%lus  %lu bytes/s", (unsigned long)secs, (unsigned long)(secs ? done / secs : 0));
    t.text(9, 1, "time", L);
    t.text(9, 10, b, W);
    int fill = size ? (int)((uint64_t)done * 38 / size) : (int)(secs % 38);
    t.fill(11, 1, 38, 0xB0, G);
    t.fill(11, 1, fill, 0xDB, O);
    t.render(scr, 0, 15);
    screenPush();
}

// The WiFi picker: networks in range, or the on-screen keyboard. Rev's scheme: orange, grey, white.
static void drawPicker() {
    scr.fillScreen(pal::BLACK);
    scr.setFont(&fonts::Font2);
    scr.setTextDatum(textdatum_t::top_left);
    auto button = [](int x, int y, int w, int h, const char* label, bool lit) {
        uint16_t fill = lit ? pal::ORANGE : pal::DIM;
        scr.fillRoundRect(x, y, w, h, 5, fill);
        scr.drawRoundRect(x, y, w, h, 5, lit ? pal::WHITE : pal::ORANGE);
        scr.setTextColor(lit ? pal::BLACK : pal::WHITE, fill);
        scr.setTextDatum(textdatum_t::middle_center);
        scr.drawString(label, x + w / 2, y + h / 2);
        scr.setTextDatum(textdatum_t::top_left);
    };
    if (picker.screen() == axis::bbs::WifiPicker::Screen::List) {
        scr.setTextColor(pal::ORANGE, pal::BLACK);
        scr.drawString("W I F I", 8, 4);
        scr.setTextColor(pal::LIGHTGREY, pal::BLACK);
        scr.drawString("tap your network", 90, 4);
        scr.drawString(picker.message().substr(0, 40).c_str(), 8, 20);
        auto rows = picker.rows();
        for (size_t i = 0; i < rows.size(); i++) {
            int y = axis::bbs::WifiPicker::kRowTop + (int)i * axis::bbs::WifiPicker::kRowH;
            scr.drawRect(4, y, 312, axis::bbs::WifiPicker::kRowH - 3, pal::DARKGREY);
            scr.setTextColor(pal::WHITE, pal::BLACK);
            scr.drawString(rows[i].c_str(), 10, y + 6);
        }
        if (rows.empty()) scr.drawString("no networks found: RESCAN", 10, 60);
        for (auto& b : picker.buttons()) button(b.x, b.y, b.w, b.h, b.label.c_str(), false);
    } else {
        auto& kb = picker.keyboard();
        scr.setTextColor(pal::ORANGE, pal::BLACK);
        scr.drawString(kb.title().substr(0, 26).c_str(), 70, 4);
        scr.drawRect(4, 28, 312, 30, pal::ORANGE);
        scr.setTextColor(pal::WHITE, pal::BLACK);
        scr.drawString((kb.shown() + "_").c_str(), 10, 35);
        for (auto& k : kb.keys()) button(k.x, k.y, k.w, k.h, k.label.c_str(), k.lit);
    }
    scr.setFont(&fonts::Font0);
}

static void drawScreen() {
    if (!screenReady()) return;  // a door is running
    if (mode == Mode::WifiPick) {
        drawPicker();
        screenPush();
        screenDirty = false;
        return;
    }
    if (job) {
        drawTransfer();
        screenDirty = false;
        return;
    }
    if (mode == Mode::SetupChoice) {
        title("first-boot setup");
        scr.setTextColor(pal::LIGHTGREY, pal::BLACK);
        scr.drawString(("WiFi: " + String(wifiSsid()) + "   " + WiFi.localIP().toString()).c_str(), 8, 50);
        scr.drawString("How do you want to set up your BBS?", 8, 70);
        bigButton(92, "ON THIS SCREEN", pal::DIM);
        bigButton(156, "OVER TELNET", pal::BLUE);
        scr.setTextColor(pal::DARKGREY, pal::BLACK);
        scr.drawString("tap a button", 8, 222);
    } else if (mode == Mode::SetupTelnet) {
        title("first-boot setup");
        scr.setTextColor(pal::LIGHTGREY, pal::BLACK);
        scr.drawString("From a computer on this network:", 8, 56);
        scr.setTextSize(2);
        scr.setTextColor(pal::LIGHTCYAN, pal::BLACK);
        scr.drawString(("telnet " + dialName()).c_str(), 8, 76);
        scr.setTextSize(1);
        scr.setTextColor(pal::LIGHTGREY, pal::BLACK);
        scr.drawString(("or telnet " + WiFi.localIP().toString() + " " + String(cfg.telnetPort)).c_str(), 8, 100);
        scr.drawString("Setup PIN:", 8, 128);
        scr.setTextSize(4);
        scr.setTextColor(pal::YELLOW, pal::BLACK);
        scr.drawString(setupPin.c_str(), 8, 144);
        scr.setTextSize(1);
        scr.setTextColor(wizard ? pal::LIGHTGREEN : pal::DARKGREY, pal::BLACK);
        scr.drawString(wizard ? "setup in progress over telnet..."
                              : gate ? "caller connected, waiting for the PIN" : "waiting for the sysop to call",
                       8, 200);
    } else if (mode == Mode::SetupTouch) {
        title("first-boot setup");
        scr.setTextColor(pal::LIGHTGREY, pal::BLACK);
        scr.drawString("On-screen setup is coming in the next build.", 8, 70);
        bigButton(156, "BACK", pal::DIM);
    } else if (localNode) {
        drawLocal();
    } else if (sysop.isOpen()) {
        drawSysop();
    } else if (detailSession >= 0) {
        drawNodeDetail(detailSession);
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
    Serial.printf("axis: listening on %s:%d, byte heap %u, largest %u\n", dialName().c_str(), cfg.telnetPort,
                  heap_caps_get_free_size(MALLOC_CAP_8BIT), heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
}

static void sendApp(const std::string& s) {
    if (s.empty() || !line) return;
    line->write((const uint8_t*)s.data(), s.size());
    line->flush();
}

static void hangUp() {
    line.reset();  // closes the connection
    logEvent("hang up" + heapText() + " " + String(node && !node->handle().empty() ? node->handle().c_str() : "") + " after " +
             String((millis() - callStartMs) / 1000) + "s");
    if (node && !node->handle().empty()) lastCaller = node->handle().c_str();
    node.reset();
    gate.reset();
    wizard.reset();
    screenDirty = true;
}

// A caller passed through to another node (the main's own node was busy).
class ClientPipe : public axis::cluster::Pipe {
public:
    explicit ClientPipe(WiFiClient c) : c_(c) {}
    int read(uint8_t* buf, size_t n) override {
        int a = c_.available();
        if (a <= 0) return c_.connected() ? 0 : -1;
        int r = c_.read(buf, (size_t)a < n ? (size_t)a : n);
        return r < 0 ? 0 : r;
    }
    int write(const uint8_t* p, size_t n) override {  // never blocks: what the socket won't take waits in the relay
        int fd = c_.fd();
        if (fd < 0 || !c_.connected()) return -1;
        int w = ::send(fd, p, n, MSG_DONTWAIT);
        if (w < 0) return (errno == EAGAIN || errno == EWOULDBLOCK) ? 0 : -1;
        return w;
    }
    void stop() { c_.stop(); }

private:
    WiFiClient c_;
};
struct Passing {
    std::unique_ptr<ClientPipe>        caller, board;
    std::unique_ptr<axis::cluster::Relay> relay;
    uint32_t                           ip = 0, sinceMs = 0;
};
static std::vector<std::unique_ptr<Passing>> passing;     // callers on other nodes through this main
static std::vector<std::pair<uint32_t, uint32_t>> reservedNodes;  // node ip, when it was given a caller

// The main's node is busy: hand the caller to a free node, if the cluster has one.
static bool passCaller(WiFiClient& incoming) {
    if (!cluster || cluster->role() != axis::cluster::Role::Main || passing.size() >= 4) return false;
    std::vector<uint32_t> reserved;
    for (auto it = reservedNodes.begin(); it != reservedNodes.end();)
        if (millis() - it->second > 10000) it = reservedNodes.erase(it);
        else reserved.push_back((it++)->first);
    uint32_t ip = axis::cluster::pickFreeNode(cluster->nodes(), cluster->nodeNumber(), reserved);
    if (!ip) return false;
    WiFiClient out;
    if (!out.connect(IPAddress(ip), (uint16_t)cfg.telnetPort, 1500)) return false;
    out.setNoDelay(true);
    incoming.setNoDelay(true);
    std::unique_ptr<Passing> p(new Passing);
    p->caller.reset(new ClientPipe(incoming));
    p->board.reset(new ClientPipe(out));
    p->relay.reset(new axis::cluster::Relay(*p->caller, *p->board));
    p->ip = ip;
    p->sinceMs = millis();
    reservedNodes.push_back({ip, millis()});
    logEvent("call from " + incoming.remoteIP().toString() + " passed to " + IPAddress(ip).toString() + heapText());
    passing.push_back(std::move(p));
    return true;
}

static void pumpPassing() {
    for (size_t i = 0; i < passing.size();) {
        if (passing[i]->relay->pump()) {
            i++;
            continue;
        }
        passing[i]->caller->stop();
        passing[i]->board->stop();
        logEvent("passed call to " + IPAddress(passing[i]->ip).toString() + " ended after " +
                 String((millis() - passing[i]->sinceMs) / 1000) + "s");
        passing.erase(passing.begin() + (long)i);
    }
}

static void answerIn(WiFiClient incoming);

static void answer(WiFiClient incoming) {
    int free = -1;  // setup takes one caller; a set-up board as many as its sessions
    for (size_t i = 0; i < sessions.size() && free < 0; i++)
        if (!sessionLine(i) && (i == 0 || (cfg.setupDone && mode == Mode::Normal))) free = (int)i;
    if (free >= 0) {
        bindSession(free);
        answerIn(incoming);
        unbindSession();
        return;
    }
    if (passCaller(incoming)) return;  // every session here is busy: another node takes the call
    {  // every node is busy
        std::string busy = axis::bbs::busyScreen();
        incoming.write((const uint8_t*)busy.data(), busy.size());
        incoming.stop();
        Serial.println("axis: busy, turned a caller away");
        return;
    }
    return;
}

static int sessionNumber(int i) {  // the node number a session answers as
    if (cluster && i >= 0 && i < (int)cluster->numbers().size()) return cluster->numbers()[(size_t)i];
    return i + 1;
}

static void answerIn(WiFiClient incoming) {
    line.reset(new WifiStream(incoming));
    callStartMs = millis();
    logEvent("call from " + line->remoteIP().toString() + " (heap " + String(heap_caps_get_free_size(MALLOC_CAP_8BIT) / 1024) +
             " KB, largest " + String(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) / 1024) + " KB)");
    line->start();
    if (cfg.setupDone && mode == Mode::Normal) {
        callsToday++;
        node.reset(new axis::bbs::Node(sessionNumber(curSession), sysInfo, cfg, &callerFiles()));
        int slot = curSession;
        node->setAccounts(&users, saveUsers, hwRandom, unixNow);
        node->onEvent = [](const std::string& e) { logEvent(String(e.c_str())); };
        node->setChat(
            [slot](const std::string& text, bool page) {  // through the main to every node (or just this board)
                if (!node) return;
                if (cluster) page ? cluster->page(node->handle(), text, slot) : cluster->chat(node->handle(), text, slot);
                else
                    for (size_t i = 0; i < sessions.size(); i++)
                        if (sessionNode(i)) sessionNode(i)->chatIn(sessionNumber(slot), node->handle(), text, page);
            },
            [] {
                std::vector<axis::bbs::Node::WhoLine> v;
                if (cluster && cluster->nodes().size() > 1) {
                    for (auto& n : cluster->nodes()) v.push_back({n.number, n.handle, n.activity});
                    std::sort(v.begin(), v.end(), [](const axis::bbs::Node::WhoLine& a, const axis::bbs::Node::WhoLine& b) {
                        return a.node < b.node;
                    });
                } else {
                    for (size_t i = 0; i < sessions.size(); i++)
                        v.push_back({sessionNumber((int)i), sessionNode(i) ? sessionNode(i)->handle() : "", sessionNode(i) ? "Online" : ""});
                }
                return v;
            });
        node->setNodeLink([slot](int kind, int to, const std::string& text) {  // notes, broadcasts, split chat keys
            if (!node) return;
            if (cluster) {
                cluster->send(node->handle(), kind, to, text, slot);
                return;
            }
            for (size_t i = 0; i < sessions.size(); i++)
                if ((int)i != slot && sessionNode(i) && (to == 0 || sessionNumber((int)i) == to))
                    sessionNode(i)->nodeIn(kind, sessionNumber(slot), node->handle(), text);
        });
        node->setMessages(&callerMessages());
        node->setArt([](const std::string& name) { return themeArt(name); });
        node->setOneLiners(
            [] {  // the wall: here on the main, over the link on a follower
                if (!asFollower()) return oneLiners;
                std::vector<std::string> v;
                std::string              reply;
                if (cluster->request("O", reply, 3000) && !reply.empty() && reply[0] == '1') {
                    size_t pos = 1;
                    while (pos < reply.size()) {
                        size_t e = reply.find('\n', pos);
                        if (e == std::string::npos) e = reply.size();
                        if (e > pos) v.push_back(reply.substr(pos, e - pos));
                        pos = e + 1;
                    }
                }
                return v;
            },
            [](const std::string& line) {
                logEvent(("oneliner: " + line).c_str());
                if (!asFollower()) linersAddLocal(line);
                else {
                    std::string reply;
                    cluster->request(std::string("P\x1f") + line, reply, 3000);
                }
            });
        node->setLists(
            [](const std::string& n) {
                if (!asFollower()) return listLoad(n);
                std::vector<std::string> v;
                std::string              reply;
                if (cluster->request(std::string("Y\x1f") + n, reply, 3000) && !reply.empty() && reply[0] == '1')
                    for (size_t pos = 1; pos < reply.size();) {
                        size_t e = reply.find('\n', pos);
                        if (e == std::string::npos) e = reply.size();
                        if (e > pos) v.push_back(reply.substr(pos, e - pos));
                        pos = e + 1;
                    }
                return v;
            },
            [](const std::string& n, const std::string& line) {
                if (n == "rumors") logEvent(("rumor: " + line).c_str());
                if (!asFollower()) listAddLocal(n, line);
                else {
                    std::string reply;
                    cluster->request(std::string("Z\x1f") + n + "\x1f" + line, reply, 3000);
                }
            });
        node->setLastCallers(
            [] {
                if (!asFollower()) return lastCallers;
                std::vector<std::string> v;
                std::string              reply;
                if (cluster->request("K", reply, 3000) && !reply.empty() && reply[0] == '1')
                    for (size_t pos = 1; pos < reply.size();) {
                        size_t e = reply.find('\n', pos);
                        if (e == std::string::npos) e = reply.size();
                        if (e > pos) v.push_back(reply.substr(pos, e - pos));
                        pos = e + 1;
                    }
                return v;
            },
            [](const std::string& line) {
                if (!asFollower()) callersAddLocal(line);
                else {
                    std::string reply;
                    cluster->request(std::string("Q\x1f") + line, reply, 3000);
                }
            });
        node->setDirect([](const char* p, size_t n) {  // the login art, from flash in 1 KB pieces
            if (line) line->write((const uint8_t*)p, n);
        });
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
#ifdef AXIS_HEADLESS
    return;  // no panel
#endif
    tft.fillScreen(TFT_BLACK);
    tft.setTextDatum(textdatum_t::top_left);
    tft.setTextSize(2);
    tft.setTextColor(TFT_ORANGE, TFT_BLACK);
    tft.drawString(("IN DOOR: " + node->door().name).substr(0, 26).c_str(), 6, 100);
    tft.setTextColor(TFT_ORANGE, TFT_BLACK);
    tft.drawString(node->handle().substr(0, 26).c_str(), 6, 124);
}

static std::string areaName(const std::string& id) {
    for (auto& a : cfg.areas)
        if (a.id == id) return a.name;
    return id;
}

static uint8_t* zipStateTake();
static void     zipStateFree();

static void startDoor() {
    const axis::bbs::DoorEntry& d = node->door();
    sendApp(node->takeOutput());
    doorHeapBefore = ESP.getFreeHeap();
    size_t   lentBytes = 0;
    uint8_t* lentRam = screenLend(lentBytes);
    doorStatusLine();
    doorStartMs = millis();
    if (d.exe == "*DOSSHELL") {  // the DOS Prompt: its own sandbox folder, its own password
        logEvent(String(node->handle().c_str()) + " opened " + d.name.c_str());
        std::string box = std::string(storageRoot()) + "/" + (d.args.empty() ? std::string("dosbox") : d.args);
        ::mkdir(box.c_str(), 0777);  // first visit: an empty sandbox
        dosFs.reset(new axis::PosixHostFs(box));
        axis::bbs::DosShellConfig c;
        c.handle = node->handle();
        c.salt = cfg.dosSalt;
        c.hash = cfg.dosHash;
        c.clockMs = [] { return (uint32_t)millis(); };
        c.timeText = [] { return sysInfo().timeText; };
        dosShell.reset(new axis::bbs::DosShell(*dosFs, *line, dosGate, c));
        dosShell->begin();
        line->flush();
        return;
    }
    if (!d.exe.empty() && d.exe[0] == '*') {  // built in: the screen copy is its unzip workspace
        logEvent(String(node->handle().c_str()) + " opened " + d.name.c_str());
        viewer.reset(new axis::bbs::AnsiViewer(callerFiles(), d.args, areaName(d.args), lentRam, lentBytes, *line));
        viewer->setZipState(zipStateTake());
        viewer->setClock([] { return (uint32_t)millis(); });  // ANSI movies play at modem speed
        viewer->begin();
        line->flush();
        return;
    }
    std::string dir = std::string(storageRoot()) + "/doors/" + d.id;
    int         me = sessionNumber(curSession);
    bool        remote = !storageHasSd() && cluster && cluster->role() == axis::cluster::Role::Follower;
    axis::door::LockTable* locks = &doorLocks;
    if (remote) {  // no door files here: the main's, over the link (cluster/doorfs.h)
        auto ask = [](const std::string& req, std::string& reply) { return cluster && cluster->request(req, reply, 8000); };
        auto* rfs = new axis::cluster::RemoteHostFs(ask, d.id);
        doorFs.reset(rfs);
        doorRemoteLocks.reset(new axis::cluster::RemoteLockTable(ask, rfs));  // a lock drops cached reads
        locks = doorRemoteLocks.get();
        dir = "main:" + dir;
    } else {
        doorFs.reset(new axis::PosixHostFs(dir));
    }
    bool setup = d.drop == "local";  // a setup program (LORDCFG): the PC screen and keyboard, mirrored to the caller
    doorImage.reset(new FlashImageSource(d.id.c_str()));
    axis::door::DoorConfig c;
    c.dir = dir;
    c.exe = d.exe;
    c.args = axis::cluster::doorArgs(d.args, me);
    if (!setup && d.args.find("%N") != std::string::npos && me != 1) {  // a multinode door (LORD): this node's own drop folder
        c.dropDir = "NODE" + std::to_string(me);
        axis::door::FileInfo fi;
        std::string          nodeDat = "NODE" + std::to_string(me) + ".DAT";
        if (!doorFs->stat(nodeDat, fi) && doorFs->stat("NODE1.DAT", fi) && fi.size < 4096) {  // LORD's node file, made once
            std::string one(fi.size, '\0');
            int         h = doorFs->open("NODE1.DAT", 0, false, false);
            if (h >= 0 && doorFs->read(h, &one[0], (int)one.size()) == (int)one.size()) {
                doorFs->close(h);
                std::string n = axis::cluster::lordNodeFile(one, me);
                int         w = doorFs->open(nodeDat, 1, true, true);
                if (w >= 0) {
                    doorFs->write(w, n.data(), (int)n.size());
                    doorFs->close(w);
                }
            } else if (h >= 0) {
                doorFs->close(h);
            }
        }
    }
    c.localMode = setup;
    doorMirror.reset(setup ? new axis::door::ScreenMirror : nullptr);
    c.imageSource = doorImage.get();
    c.arenas.emplace_back(lentRam, lentBytes);  // the screen copy becomes the door's first pages
    // Spare instruction RAM: malloc never hands it out (it can't be read a byte at a time), but
    // guest pages are only touched as words, so a door can use it. About 40 KB on this board.
    // Only pure instruction RAM: a block in the shared D/IRAM region is byte heap too (taking it
    // once left the door 10 KB of heap), so such blocks are held aside while we look, then freed.
    size_t iramBytes = 0;
    {
        void* aside[4];
        int   held = 0;
        while (held < 4) {
            size_t b = heap_caps_get_largest_free_block(MALLOC_CAP_EXEC) / 4096 * 4096;
            void*  p = b ? heap_caps_malloc(b, MALLOC_CAP_EXEC) : nullptr;
            if (!p) break;
            if (esp_ptr_in_diram_iram(p)) {
                aside[held++] = p;
                continue;
            }
            doorIram = (uint8_t*)p;
            iramBytes = b;
            break;
        }
        while (held) heap_caps_free(aside[--held]);
    }
    if (doorIram) c.arenas.emplace_back(doorIram, iramBytes);
    c.dropFile = setup ? axis::door::DropFile::None : d.drop == "dorinfo" ? axis::door::DropFile::Dorinfo : axis::door::DropFile::DoorSys;
    c.caller.alias = c.caller.realName = node->handle();
    c.caller.node = sessionNumber(curSession);
    busySlot = curSession;
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
    logEvent(String(node->handle().c_str()) + " opened " + d.name.c_str());
    door.reset(new axis::door::DoorSession(c, *doorFs, *locks, *line, me));
}

// The unzipper's working state (~12 KB), from the heap only while a ZIP is open; its 32 KB window
// is the lent screen copy (38.4 KB, too small for both since the copy went 4-bit).
static uint8_t* zipState = nullptr;
static uint8_t* zipStateTake() {
    if (!zipState) zipState = (uint8_t*)heap_caps_malloc(axis::bbs::ZipReader::kStateBytes, MALLOC_CAP_8BIT);
    return zipState;
}
static void zipStateFree() {
    heap_caps_free(zipState);
    zipState = nullptr;
}

static void finishViewer() {
    busySlot = -1;
    viewer.reset();
    zipStateFree();
    dosShell.reset();
    dosFs.reset();
    screenReturn();
    screenDirty = true;
    logEvent(String("door ") + node->door().id.c_str() + " ended after " + String((millis() - doorStartMs) / 1000) + "s");
    if (!line || !line->carrier()) {
        hangUp();
        return;
    }
    node->resumeAfterDoor(axis::bbs::Node::DoorEnd::Normal, millis());
    sendApp(node->takeOutput());
}

static void finishDoor() {
    using R = axis::door::DoorResult;
    using E = axis::bbs::Node::DoorEnd;
    R   r = door->result();
    int peakKb = (int)door->memStats().peakRamPages * 4;
    door.reset();
    if (doorMirror && line) line->write((const uint8_t*)"[0m[2J", 8);  // out of the setup screen
    doorMirror.reset();
    doorImage.reset();
    doorFs.reset();
    (doorRemoteLocks ? *doorRemoteLocks : doorLocks).releaseOwner(sessionNumber(curSession));
    doorRemoteLocks.reset();
    busySlot = -1;
    heap_caps_free(doorIram);
    doorIram = nullptr;
    screenReturn();
    logEvent(String("door ") + node->door().id.c_str() + " ended (" +
             (r == R::Exited ? "quit" : r == R::OutOfMemory ? "out of memory" : r == R::CarrierLost ? "hung up"
              : r == R::TimeLimit ? "time limit" : "couldn't start") + ")");
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
            if (!wizard->noip().host.empty())  // the No-IP login stays in this board's own storage
                web::setNoip(String(wizard->noip().host.c_str()) + "|" + wizard->noip().user.c_str() + "|" + wizard->noip().pass.c_str());
            logEvent(String("setup ") + (ok ? "saved" : "SAVE FAILED") + " over telnet");
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
static void newSetupPin() {
    char b[8];
    snprintf(b, sizeof b, "%06lu", (unsigned long)(esp_random() % 1000000));
    setupPin = b;
}

static void sysopAction(axis::bbs::SysopMenu::Action a) {
    using A = axis::bbs::SysopMenu::Action;
    if (a == A::FormatSd) {
        if (callersOnline()) {
            logEvent("sysop: SD format refused, a caller is online");
            return;
        }
        String result;
        bool   ok = storageFormatSd(result);
        logEvent(String("sysop: SD format ") + (ok ? "done" : "FAILED") + ": " + result);
        makeAreaFolders();
    } else if (a == A::Restart) {
        logEvent("sysop: restart");
        delay(200);
        ESP.restart();
    } else if (a == A::ValidateUser || a == A::DenyUser) {
        axis::bbs::User* u = users.byId(sysop.selectedUser());
        if (!u) return;
        bool ok = a == A::ValidateUser;
        u->status = ok ? axis::bbs::User::Status::Validated : axis::bbs::User::Status::Denied;
        if (ok && u->security < 20) u->security = 20;
        logEvent(String("sysop: ") + (ok ? "validated " : "denied ") + u->handle.c_str());
        saveUsers();
    } else if (a == A::ToggleSysop) {
        axis::bbs::User* u = users.byId(sysop.selectedUser());
        if (!u) return;
        bool up = u->security < axis::bbs::kSysopSecurity;
        u->security = up ? axis::bbs::kSysopSecurity : 20;
        if (up) u->status = axis::bbs::User::Status::Validated;
        logEvent(String("sysop: ") + u->handle.c_str() + (up ? " is now a sysop" : " is no longer a sysop"));
        saveUsers();
    } else if (a == A::ToggleFiles || a == A::ToggleMessages || a == A::ToggleDoors || a == A::ToggleFreeDownloads) {
        axis::bbs::User* u = users.byId(sysop.selectedUser());
        if (!u) return;
        bool& f = a == A::ToggleFiles ? u->permFiles : a == A::ToggleMessages ? u->permMessages : a == A::ToggleDoors ? u->permDoors
                                                                                                         : u->freeDownloads;
        f = !f;
        logEvent(String("sysop: ") + u->handle.c_str() + (a == A::ToggleFiles ? " files " : a == A::ToggleMessages ? " messages "
                                                           : a == A::ToggleDoors ? " doors " : " free downloads ") + (f ? "on" : "off"));
        saveUsers();  // and on to every board
    } else if (a == A::CycleTheme) {  // the next caller sees it; every board gets it
        cfg.theme = cfg.theme == "oblivion" ? "axis" : "oblivion";
        bool ok = configSave(cfg);
        logEvent(String("sysop: theme ") + cfg.theme.c_str() + (ok ? "" : " (SAVE FAILED)"));
        if (cluster) cluster->shareSettings();
        screenDirty = true;
    } else if (a == A::RerunSetup) {  // nothing changes until the wizard saves
        logEvent("sysop: setup again");
        newSetupPin();
        mode = Mode::SetupChoice;
        sysop = axis::bbs::SysopMenu();
    }
}

// The cluster: find the other AXIS boards on this LAN (or become main), keep our node number.
static void startCluster() {
    if (cluster) return;
    clusterNet.begin();
    cluster.reset(new axis::cluster::Cluster(clusterNet, WiFi.macAddress().c_str()));
#if AXIS_SD_POOL
    mirrorJob.onEvent = [](const std::string& e) { logEvent(String(e.c_str())); };  // "mirror: copied X to node 2"
#endif
    cluster->onEvent = [](const std::string& e) {
        logEvent(String("cluster: ") + e.c_str() + heapText());
        if (e.rfind("MAIN NODE LOST", 0) == 0) mainLostAtMs = millis();
        screenDirty = true;
    };
    cluster->onChat = [](const axis::cluster::ChatLine& l) {
        if (l.kind == kAnnounceKind) {  // a follower's scene news: the main passes it to every board
            if (!asFollower()) cluster->send("", kAnnounceAllKind, 0, l.text);
            return;
        }
        if (l.kind == kAnnounceAllKind) {  // the board holding the webhook posts it
            web::announce(String("**") + cfg.bbsName.c_str() + "** " + l.text.c_str());
            return;
        }
        if (l.page) {
            pageText = String(l.handle.c_str()) + ": " + l.text.c_str();
            pageAtMs = millis();
            logEvent("PAGE from " + String(l.handle.c_str()) + " on node " + String(l.node) + ": " + l.text.c_str());
            screenDirty = true;
        }
        if (localNode) {
            if (l.kind >= 2) {
                if (!l.to) localNode->nodeIn(l.kind, l.node, l.handle, l.text);  // the console gets broadcasts
            } else {
                localNode->chatIn(l.node, l.handle, l.text, l.page);
            }
            localFeed();
        }
        for (size_t i = 0; i < sessions.size(); i++)
            if (sessionNode(i) && sessionLine(i)) {
                if (l.kind >= 2) {  // node to node: only the node it's for (or all), never back to the sender
                    int me = sessionNumber((int)i);
                    if (me == l.node || (l.to && l.to != me)) continue;
                    sessionNode(i)->nodeIn(l.kind, l.node, l.handle, l.text);
                } else {
                    sessionNode(i)->chatIn(l.node, l.handle, l.text, l.page);
                }
                std::string out = sessionNode(i)->takeOutput();
                sessionLine(i)->write((const uint8_t*)out.data(), out.size());
                sessionLine(i)->flush();
            }
    };
    cluster->settings = [] { return axis::bbs::formatConfig(cfg); };
    cluster->onSettings = [](const std::string& ini) {  // one BBS: the main's settings, this board's name
        artCache.clear();  // the theme may have changed
        if (storageHasSd()) {  // the SD board keeps the BBS's settings: a stand-in main's older copy must not replace them
            Serial.println("axis: settings from a main without the card: kept our own");
            return;
        }
        Serial.printf("axis: settings from the main: %u bytes\n", (unsigned)ini.size());
        axis::bbs::BbsConfig m = axis::bbs::mergeSettings(cfg, ini);
        if (axis::bbs::formatConfig(m) == axis::bbs::formatConfig(cfg)) return;
        bool wasSetUp = cfg.setupDone;
        cfg = m;
        bool ok = configSave(cfg);
        logEvent(String("settings from the main") + (ok ? "" : " (SAVE FAILED)"));
        if (!wasSetUp) {  // a fresh (headless) node: ready for callers now
            mode = Mode::Normal;
            sessions.resize((size_t)cfg.sessions);
        }
        screenDirty = true;
    };
    cluster->setSlots((int)sessions.size());
    cluster->setHasFiles(storageHasSd());  // the board with the SD card should be main
    cluster->setFreeBytes(storageFreeBytes(true));  // and the main pools every card by its room
    cluster->clockMs = [] { return (uint32_t)millis(); };
    cluster->sleepMs = [](uint32_t ms) { delay(ms); };
    cluster->allUsers = [] {
        std::vector<std::string> v;
        for (auto& u : users.all()) v.push_back(axis::bbs::formatUser(u));
        return v;
    };
    cluster->onUser = [](const std::string& rec) {
        axis::bbs::User u;
        if (!axis::bbs::parseUser(rec, u)) return;
        axis::bbs::User& here = users.upsert(u);
        if (cluster->role() == axis::cluster::Role::Main) {
            saveUsers();  // and on to the other followers
        } else {
            userSync.received(axis::bbs::formatUser(here));
            if (!flashWrite("/users.dat", users.format())) logEvent("users: SAVE FAILED");
            pendingSeen = users.pending();
            screenDirty = true;
        }
    };
    cluster->onRequest = [](int nodeNum, const std::string& req) {
        servedRequest = true;  // a follower waits on this: no nap this turn
        lastServedMs = millis();
        if (req.size() > 2 && req[0] == 'M') {  // the message base
            callerMessages();  // (makes the local store)
            return axis::bbs::serveMessages(*localMsgs, req);
        }
        if (req == "O") {  // the oneliner wall
            std::string out = "1";
            for (auto& l : oneLiners) out += l + "\n";
            return out;
        }
        if (req.rfind("T\x1f", 0) == 0) return "1" + themeArtLocal(req.substr(2));  // a theme screen
        if (req.rfind("Y\x1f", 0) == 0) {  // a shared list
            std::string out = "1";
            for (auto& l : listLoad(req.substr(2))) out += l + "\n";
            return out;
        }
        if (req.rfind("Z\x1f", 0) == 0) {
            size_t sep = req.find('\x1f', 2);
            if (sep != std::string::npos) listAddLocal(req.substr(2, sep - 2), req.substr(sep + 1));
            return std::string("1");
        }
        if (req == "K") {  // the last callers
            std::string out = "1";
            for (auto& l : lastCallers) out += l + "\n";
            return out;
        }
        if (req.rfind("Q\x1f", 0) == 0) {
            callersAddLocal(req.substr(2));
            return std::string("1");
        }
        if (req.rfind("P\x1f", 0) == 0) {
            linersAddLocal(req.substr(2));
            return std::string("1");
        }
        if (!req.empty() && req[0] == 'H') {  // a follower's door, on this board's door folders
            auto& ds = doorServers[nodeNum];
            if (!ds)
                ds.reset(new axis::cluster::DoorFsServer(
                    [](const std::string& id) {
                        return std::unique_ptr<axis::door::HostFs>(new axis::PosixHostFs(std::string(storageRoot()) + "/doors/" + id));
                    },
                    doorLocks));
            return ds->handle(req);
        }
        auto& srv = fileServers[nodeNum];
#if AXIS_SD_POOL
        if (!srv) srv.reset(new axis::cluster::FileServer(pooledFiles));  // (just this card when it is the only one)
#else
        if (!srv) srv.reset(new axis::cluster::FileServer(sdFiles));
#endif
        return srv->handle(req);
    };
    cluster->onMainRequest = [](const std::string& req) {  // a follower with a card: the main asks for its files
        servedRequest = true;
        if (!storageHasSd()) return std::string("0");
        if (!cardServer) cardServer.reset(new axis::cluster::FileServer(sdFiles));
        std::string reply = cardServer->handle(req);
        if (!req.empty() && (req[0] == 'F' || req[0] == 'X')) cluster->setFreeBytes(storageFreeBytes(true));  // the main picks cards by room
        return reply;
    };
    cluster->onMainGone = [] { cardServer.reset(); };  // its half-done uploads and mirror copies are thrown away
    cluster->onNodeGone = [](int nodeNum) {  // its unfinished uploads go, its door's files close, its locks free
        fileServers.erase(nodeNum);
        auto it = doorServers.find(nodeNum);
        if (it != doorServers.end()) {
            it->second->drop();
            doorServers.erase(it);
        }
    };  // its unfinished uploads go too
    cluster->busyHere = [] { return localNode != nullptr; };  // the sysop at the board's own screen
    cluster->onNumber = [](int n) {
        Preferences p;
        p.begin("clu", false);
        p.putInt("num", n);
        p.end();
        screenDirty = true;
    };
    Preferences p;
    p.begin("clu", true);
    int previous = p.getInt("num", 0);
    p.end();
    cluster->start(millis(), previous);
}

#ifdef AXIS_HEADLESS
// No screen to pick WiFi on: a hotspot, AXIS-SETUP, with one web page (setupportal.h). The
// password goes from the form straight to wifiJoin and the board's saved networks, never logged.
#include <WebServer.h>

static std::unique_ptr<WebServer> portal;
static std::vector<std::string>   portalNets;
static void portalShow(const char* message) {
    portal->send(200, "text/html", axis::bbs::portalPage(portalNets, message).c_str());
}
static void startHotspot() {
    portalNets.clear();
    WiFi.mode(WIFI_AP_STA);
    int n = WiFi.scanNetworks();
    for (int i = 0; i < n && portalNets.size() < 20; i++)
        if (WiFi.SSID(i).length()) portalNets.push_back(WiFi.SSID(i).c_str());
    WiFi.scanDelete();
    WiFi.softAP("AXIS-SETUP");
    portal.reset(new WebServer(80));
    portal->on("/", HTTP_GET, [] { portalShow(""); });
    portal->on("/join", HTTP_POST, [] {
        String ssid = portal->arg("ssid"), pass = portal->arg("pass");
        portal->send(200, "text/html", "<body style=\"background:#000;color:#ccc\">Joining... if it works the node restarts on your network.</body>");
        delay(300);
        if (ssid.length() && wifiJoin(ssid, pass)) {
            wifiRemember(ssid, pass);
            logEvent("wifi: joined " + ssid + " from the hotspot page, restarting");
            delay(200);
            ESP.restart();
        }
        logEvent("wifi: couldn't join " + ssid + " from the hotspot page");
        WiFi.mode(WIFI_AP_STA);  // the join dropped the hotspot: bring it back
        WiFi.softAP("AXIS-SETUP");
    });
    portal->onNotFound([] { portalShow(""); });  // any address: the page (phones look for one)
    portal->begin();
    logEvent("wifi: no saved network: join the AXIS-SETUP hotspot and open http://192.168.4.1/");
}
#endif

static void startPicker(Mode after) {
    afterPick = after;
    mode = Mode::WifiPick;
    picker = axis::bbs::WifiPicker();
    picker.setNetworks(wifiScan());
    picker.setCanLeave(WiFi.status() == WL_CONNECTED);
    screenDirty = true;
}

static void handleTap(int x, int y) {
    if (!screenReady()) return;  // a door has the screen's memory
    if (mode == Mode::WifiPick) {
        picker.tap(x, y);
        screenDirty = true;
        return;
    }
    if (mode == Mode::Normal) {
        screenDirty = true;
        if (localNode) {  // the local console: the keyboard, or KEYS / ENTER / EXIT
            if (localKeys) {
                localKb.tap(x, y);
                if (localKb.done()) {
                    std::string line = localKb.text() + "\r";
                    localNode->input((const uint8_t*)line.data(), line.size(), millis());
                    localKb.reset("type, then OK", false);
                    localKeys = false;
                } else if (localKb.cancelled()) {
                    localKb.reset("type, then OK", false);
                    localKeys = false;
                }
            } else if (y >= 206 && x < 64) {
                localKeys = true;
            } else if (y >= 206 && x < 126) {
                localNode->input((const uint8_t*)"\r", 1, millis());
            } else if (y >= 206 && x < 188) {
                localZoom = !localZoom;
            } else if (y >= 206 && x < 250) {
                localPan = localPan ? 0 : 40;
                localZoom = true;
            } else if (y >= 206) {
                endLocal();
            }
            screenDirty = true;
            return;
        }
        if (sysop.isOpen()) {
            sysopAction(sysop.tap(x, y));
            return;
        }
        if (detailSession < 0 && x >= 272 && y >= 16 && y < 38) {  // LOCAL: the sysop logs in at the board
            localTerm.reset(new axis::bbs::AnsiTerm());
            localScreen.reset(new AnsiScreen());
            localNode.reset(new axis::bbs::Node(cluster && !cluster->numbers().empty() ? cluster->numbers()[0] : 1, sysInfo, cfg,
                                                &callerFiles()));
            localNode->setAccounts(&users, saveUsers, hwRandom, unixNow);
            localNode->onEvent = [](const std::string& e) { logEvent(String(e.c_str())); };
            localNode->setMessages(&callerMessages());
            localNode->setArt([](const std::string& name) { return themeArt(name); });  // the theme's screens, like a caller gets
            localNode->setLists([](const std::string& n) { return sharedList(n); }, [](const std::string& n, const std::string& line) { sharedListAdd(n, line); });
            localNode->setLastCallers([] { return sharedList("callers"); }, [](const std::string&) {});
            localPan = 0;
            localZoom = false;  // start with the whole screen (crisp); ZOOM for big letters
            localNode->setChat(
                [](const std::string& text, bool page) {
                    if (!localNode) return;
                    if (cluster) page ? cluster->page(localNode->handle(), text) : cluster->chat(localNode->handle(), text);
                    else localNode->chatIn(1, localNode->handle(), text, page);
                },
                [] {
                    std::vector<axis::bbs::Node::WhoLine> v;
                    if (cluster)
                        for (auto& n : cluster->nodes()) v.push_back({n.number, n.handle, n.activity});
                    return v;
                });
            localNode->beginLocal(millis());
            logEvent(String("local sysop login: ") + localNode->handle().c_str());
            localFeed();
            return;
        }
        if (detailSession >= 0) {  // the node detail screen: BACK, or KICK the caller
            if (y >= 204 && x < 108) detailSession = -1;
            else if (y >= 204 && x >= 212 && sessionLine((size_t)detailSession)) {
                axis::bbs::Node* n = sessionNode((size_t)detailSession);
                logEvent(String("sysop: kicked ") + (n && !n->handle().empty() ? n->handle().c_str() : "a caller"));
                sessionLine((size_t)detailSession)->hangUp();  // the session's turn sees the carrier gone and cleans up
                detailSession = -1;
            }
            return;
        }
        int row = (y - 64) / 16;  // a node's line on the WFC (panel rows 3..6 start at y = 64)
        if (y >= 64 && row >= 3 && row < 3 + (int)sessions.size() && row < 7) {
            detailSession = row - 3;
            return;
        }
        sysop.open();  // anywhere else: the sysop menu
        return;
    }
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

// Every @ command, for the serial console and the sysop control panel alike (one code path, so
// the same command always does the same thing). Replies go to `out`.
static void runCommand(const String& cmd, Print& out) {
    if (cmd.startsWith("@TAP ")) {
        int sp = cmd.indexOf(' ', 5);
        tapX = cmd.substring(5, sp).toInt();
        tapY = cmd.substring(sp + 1).toInt();
    } else if (cmd == "@DOORSTATE") {  // the running door, for a door that seems stuck (bench)
        if (!door) out.println("axis: no door running");
        else
            out.printf("axis: door result %d, %lu instructions, carrier %d, pool %u/%u pages, %s\n", (int)door->result(),
                          (unsigned long)door->instructions(), busySlot >= 0 && sessionLine((size_t)busySlot) ? (int)sessionLine((size_t)busySlot)->carrier() : -1, (unsigned)door->poolPages(),
                          (unsigned)door->poolBudget(), door->debugState().c_str());
    } else if (cmd == "@NODES") {  // the cluster as this board sees it (bench)
        if (!cluster) out.println("axis: no cluster");
        else
            for (auto& n : cluster->nodes())
                out.printf("axis: node %d ip %s handle '%s' activity '%s'%s\n", n.number, IPAddress(n.ip).toString().c_str(),
                              n.handle.c_str(), n.activity.c_str(), n.number == cluster->nodeNumber() ? " (this board)" : "");
    } else if (cmd == "@CARDS") {  // the SD cards in the pool: free space, up or down (bench)
        if (storageHasSd()) out.printf("axis: card on this board, %u MB free\n", (unsigned)(storageFreeBytes(true) >> 20));
        else out.println("axis: no SD card on this board");
        if (cluster && cluster->role() == axis::cluster::Role::Main)
            for (auto& c : cluster->cardBoards())
                out.printf("axis: card on %s (%s, ip %s), %u MB free, %s\n", cardLabel(c.id).c_str(), c.id.c_str(),
                              IPAddress(c.ip).toString().c_str(), (unsigned)(c.freeBytes >> 20), c.alive ? "up" : "DOWN");
        else if (cluster)
            out.println("axis: only the main pools the cards (@CARDS there)");
    } else if (cmd == "@MIRROR") {  // the mirror job: what it is copying, what it has done (bench)
        std::string areas;
        for (auto& a : cfg.areas)
            if (a.mirror) areas += (areas.empty() ? "" : " ") + a.id;
        out.printf("axis: mirrored areas: %s\n", areas.empty() ? "none (add |m to an areaN line)" : areas.c_str());
#if AXIS_SD_POOL
        if (!cluster || cluster->role() != axis::cluster::Role::Main) out.println("axis: only the main runs the mirror");
        else out.printf("axis: mirror %s\n", mirrorJob.state().c_str());
#else
        out.println("axis: pool and mirror are not in this build (AXIS_SD_POOL)");
#endif
    } else if (cmd.startsWith("@SYSOP ")) {  // "@SYSOP handle": make a caller a sysop (validated, security 255)
        axis::bbs::User* u = users.find(cmd.substring(7).c_str());
        if (!u) out.println("axis: no user by that handle");
        else {
            u->security = axis::bbs::kSysopSecurity;
            u->status = axis::bbs::User::Status::Validated;
            logEvent(String("sysop: ") + u->handle.c_str() + " is now a sysop (serial)");
            saveUsers();
            out.printf("axis: %s is now a sysop\n", u->handle.c_str());
        }
    } else if (cmd.startsWith("@ADDUSER ")) {  // "@ADDUSER handle|location|password": a validated caller (security 20)
        String a = cmd.substring(9);
        int    b1 = a.indexOf('|'), b2 = b1 < 0 ? -1 : a.indexOf('|', b1 + 1);
        if (b1 <= 0 || b2 < 0 || a.length() - b2 - 1 < 6) {
            out.println("axis: @ADDUSER handle|location|password (6+ characters)");
        } else if (users.find(a.substring(0, b1).c_str())) {
            out.println("axis: that handle is taken");
        } else {
            axis::bbs::User u;
            u.handle = a.substring(0, b1).c_str();
            u.location = a.substring(b1 + 1, b2).c_str();
            u.salt = axis::bbs::makeSalt(hwRandom);
            u.hash = axis::bbs::hashPassword(u.salt, a.substring(b2 + 1).c_str());
            u.status = axis::bbs::User::Status::Validated;
            u.security = 20;
            u.created = unixNow();
            users.add(u);
            logEvent(String("sysop: added ") + u.handle.c_str() + " (serial)");
            saveUsers();  // and on to every board
            out.printf("axis: added %s\n", u.handle.c_str());
        }
    } else if (cmd.startsWith("@DISCORD")) {  // "@DISCORD https://discord.com/api/webhooks/...": the channel webhook; alone: off
        out.println(web::setDiscord(cmd.length() > 9 ? cmd.substring(9) : String()) ? "axis: discord webhook saved" : "axis: that isn't a Discord webhook URL");
    } else if (cmd.startsWith("@DDNS")) {  // "@DDNS domain|host|key|secret": GoDaddy dynamic DNS; alone: off
        out.println(web::setDdns(cmd.length() > 6 ? cmd.substring(6) : String()) ? "axis: ddns saved" : "axis: @DDNS domain|host|key|secret");
    } else if (cmd == "@NOIPNOW") {  // bench: update No-IP right now
        web::noipNow();
        out.printf("axis: web: %s\n", web::status().c_str());
    } else if (cmd.startsWith("@NOIP")) {  // "@NOIP host|username|password": No-IP dynamic DNS; alone: off
        out.println(web::setNoip(cmd.length() > 6 ? cmd.substring(6) : String()) ? "axis: noip saved" : "axis: @NOIP host|username|password");
    } else if (cmd == "@LOGCLEAR") {  // wipe the event log (screen, flash file, its old copy) and the page shown on the WFC
        events = axis::bbs::EventLog();
        ::remove("/littlefs/events.log");
        ::remove("/littlefs/events.old");
        pageText = "";
        screenDirty = true;
        out.println("axis: event log cleared");
    } else if (cmd == "@WEBPING") {
        out.printf("axis: webping %s\n", web::ping().c_str());
    } else if (cmd == "@WEB") {
        out.printf("axis: web: %s\n", web::status().c_str());
    } else if (cmd == "@WEBTEST") {
        web::announce(String("**") + cfg.bbsName.c_str() + "** is online. Telnet in and feed the scene.");
        out.println("axis: test announcement queued");
    } else if (cmd == "@LED") {  // what the LED is showing and why (bench)
        ledReport = true;
    } else if (cmd == "@LOG") {  // the event log, newest last (bench)
        for (auto& l : events.lines()) out.printf("@LOG %s\n", l.c_str());
        out.println("@LOG END");
    } else if (cmd == "@SHOT") {
        shotDoneMs = millis();
        screenShot([] {  // the cluster keeps its heartbeat while the screen streams out
            if (cluster) cluster->tick(millis());
        });
        shotDoneMs = millis();  // (the loop's stall check comes next: this one was on purpose)
    } else if (cmd == "@SDCHECK") {  // look for the card again (cards swapped while running)
        SD.end();
        storageInit();
        screenDirty = true;
    } else if (cmd == "@SDFORMAT FORMAT") {  // bench trigger for the sysop SD tool
        if (callersOnline()) {
            out.println("axis: sd format refused, a caller is online");
        } else {
            String result;
            bool   ok = storageFormatSd(result);
            out.printf("axis: sd format %s: %s\n", ok ? "ok" : "FAILED", result.c_str());
            screenDirty = true;
        }
    } else if (cmd.startsWith("@DOORLS ")) {  // list a door's folder the way the door sees it
        std::string           dir = std::string(storageRoot()) + "/doors/" + cmd.substring(8).c_str();
        axis::PosixHostFs     fs(dir);
        axis::door::FindState st;
        fs.find("*.*", 0x10, st);
        out.printf("axis: %s: %u entries\n", dir.c_str(), (unsigned)st.matches.size());
        for (auto& f : st.matches)
            out.printf("axis:   %-12s %8u%s\n", f.name83.c_str(), (unsigned)f.size, f.attr & 0x10 ? " <DIR>" : "");
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
            out.printf("axis: door %s ok: %u bytes %s in %lu ms, key %08lx, heap %u\n", id.c_str(), (unsigned)len,
                          src.rewrote() ? "written" : "already cached", (unsigned long)(millis() - t0),
                          (unsigned long)src.key(), ESP.getFreeHeap(), ESP.getMaxAllocHeap());
        else
            out.printf("axis: door %s FAILED: %s\n", id.c_str(), src.error());
    } else if (cmd.startsWith("@SET ") && cmd.indexOf('=') > 5) {  // "@SET location=Myrtle Beach, SC"
        String kv = cmd.substring(5);
        cfg = axis::bbs::parseConfig(axis::bbs::formatConfig(cfg) + kv.c_str() + "\n");  // the later line wins
        bool ok = configSave(cfg);
        logEvent("sysop: set " + kv.substring(0, kv.indexOf('=')) + (ok ? "" : " (SAVE FAILED)"));
        if (cluster) cluster->shareSettings();
        screenDirty = true;
    } else if (cmd.startsWith("@NUP")) {  // "@NUP secret": the new user password asked before Apply; alone, anyone can apply
        String pw = cmd.length() > 5 ? cmd.substring(5) : String();
        cfg.nupSalt = pw.length() ? axis::bbs::makeSalt(hwRandom) : std::string();
        cfg.nupHash = pw.length() ? axis::bbs::hashPassword(cfg.nupSalt, pw.c_str()) : std::string();
        bool ok = configSave(cfg);
        if (cluster) cluster->shareSettings();  // every board asks the same one
        logEvent(String("sysop: new user password ") + (pw.length() ? "set" : "cleared") + (ok ? "" : " (SAVE FAILED)"));
    } else if (cmd.startsWith("@DOSPASS")) {  // "@DOSPASS secret" sets the DOS Prompt password; alone, disables it
        String pw = cmd.length() > 9 ? cmd.substring(9) : String();
        cfg.dosSalt = pw.length() ? axis::bbs::makeSalt(hwRandom) : std::string();
        cfg.dosHash = pw.length() ? axis::bbs::hashPassword(cfg.dosSalt, pw.c_str()) : std::string();
        bool ok = configSave(cfg);
        logEvent(String("sysop: DOS prompt password ") + (pw.length() ? "set" : "cleared") + (ok ? "" : " (SAVE FAILED)"));
    } else if (cmd.startsWith("@RUMOR ")) {  // "@RUMOR text": add a rumor to the board's list (the main keeps them)
        sharedListAdd("rumors", cmd.substring(7).c_str());
        out.println("axis: rumor added");
    } else if (cmd.startsWith("@SYSOPPASS ")) {  // "@SYSOPPASS secret" sets the sysop account's password (6+ characters)
        String pw = cmd.substring(11);
        if (pw.length() < 6) {
            out.println("axis: @SYSOPPASS needs 6 or more characters");
        } else {
            cfg.sysopSalt = axis::bbs::makeSalt(hwRandom);
            cfg.sysopHash = axis::bbs::hashPassword(cfg.sysopSalt, pw.c_str());
            bool ok = configSave(cfg);
            if (cluster) cluster->shareSettings();  // every board takes the same sysop login
            logEvent(String("sysop: sysop password changed") + (ok ? "" : " (SAVE FAILED)"));
        }
    } else if (cmd == "@WIFIPICK") {  // the touch WiFi picker on demand (bench)
        startPicker(mode == Mode::WifiPick ? afterPick : mode);
    } else if (&out == &Serial && serialFileCommand(cmd)) {  // @PUT @LS @RM @MKDIR @BAUD (tools/sdput.py)
    } else if (cmd == "@RESETSETUP") {
        configErase();
        out.println("axis: settings erased, restarting into setup");
        delay(100);
        ESP.restart();
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
        runCommand(cmd, Serial);
        cmd = "";
    }
}

// ------------------------------------------------------------------ main
// Out of memory: say what was asked for and what was left, before the abort (diagnostics).
static void allocFailed(size_t size, uint32_t caps, const char* fn) {
    esp_rom_printf("axis: ALLOC FAILED %u bytes (caps %x) in %s: byte heap %u, largest %u\n", (unsigned)size, (unsigned)caps, fn,
                   (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT), (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
}

void setup() {
    heap_caps_register_failed_alloc_callback(allocFailed);
    ledBegin();
    Serial.begin(115200);
    delay(200);
    const char* panel = boardInit(1, 40000000);
    if (!screenInit()) Serial.println("axis: no memory for the screen copy");
    Serial.printf("\naxis: Axis Micro BBS Firmware %s booting, panel %s, heap %u\n", axis::bbs::kAxisVersion, panel,
                  ESP.getFreeHeap());
    title("starting...");
    screenPush();
    storageInit();
    if (!configMount()) Serial.println("axis: internal flash filesystem failed to mount");
    web::begin();
    cfg = configLoad();
    sessions.resize(cfg.setupDone ? (size_t)cfg.sessions : 1);  // callers at once (setup takes one)
    for (auto& d : cfg.doors)  // LORD as a multinode door: each node its own node file and drop folder
        if (d.id == "lord" && d.args == "1 /DREW") {
            d.args = "%N /DREW";
            configSave(cfg);
        }
    users.parse(flashRead("/users.dat"));
    linersLoad();
    callersLoad();
    userSync.changed(users);  // what we have counts as sent: only new changes go over the link
    for (auto& l : eventFile.tail(40)) events.add(l);  // the Logs screen picks up where it left off
    eventFileReady = true;
    logEvent("boot: Axis Micro BBS Firmware " + String(axis::bbs::kAxisVersion));
    pendingSeen = users.pending();
    makeAreaFolders();
#ifdef AXIS_HEADLESS
    if (!cfg.setupDone) {  // nothing to set up on a node with no screen: the main's settings arrive over the link
        String mac = WiFi.macAddress();
        mac.replace(":", "");
        cfg.boardName = ("axis-" + mac.substring(8)).c_str();
        cfg.sessions = 3;  // no screen to draw: room for a third caller
        newSetupPin();     // (only used if a caller arrives before the settings do)
    }
#else
    if (!cfg.setupDone) {
        mode = Mode::SetupChoice;
        newSetupPin();
    }
#endif
    Serial.printf("axis: settings %s\n", cfg.setupDone ? "loaded" : "not set up yet, first-boot setup");
    if (!wifiJoinSaved()) {
#ifdef AXIS_HEADLESS
        startHotspot();
#else
        Serial.println("axis: no saved WiFi network in range: touch picker");
        startPicker(mode);
        drawScreen();
#endif
        return;
    }
    Serial.printf("axis: wifi %s ip %s\n", wifiSsid(), WiFi.localIP().toString().c_str());
    applyNetwork();
    Serial.printf("axis: time %s\n", timeText().c_str());
    startCluster();
    drawScreen();
}

// What the whole cluster is doing, shown on this board's LED (see ledsignal.h for the patterns).
static void ledUpdate() {
    static uint32_t last = 0;
    if (millis() - last < 20) return;
    last = millis();
    axis::bbs::LedInputs in;
    if (cluster && cluster->nodes().size() > 1) {
        for (auto& n : cluster->nodes()) {
            in.callers += !n.handle.empty();
            in.doorRunning = in.doorRunning || n.activity == "Doors";
            in.transfer = in.transfer || n.activity == "Transfer";
            in.chat = in.chat || n.activity == "Chat";
        }
    } else {
        for (size_t i = 0; i < sessions.size(); i++)
            if (axis::bbs::Node* n = sessionNode(i)) {
                in.callers += !n->handle().empty();
                in.chat = in.chat || n->state() == axis::bbs::Node::State::Chat;
            }
        in.doorRunning = door || viewer || dosShell;
        in.transfer = job != nullptr;
    }
    in.pendingUsers = (int)users.pending();
    in.paged = pageText.length() && millis() - pageAtMs < 60000;
    in.mainLost = mainLostAtMs && millis() - mainLostAtMs < 30000;
    axis::bbs::Rgb c = axis::bbs::ledColor(in, millis());
    if (ledReport) {
        ledReport = false;
        Serial.printf("axis: led callers %d pending %d door %d transfer %d chat %d paged %d lost %d -> rgb %u %u %u\n", in.callers,
                      in.pendingUsers, in.doorRunning, in.transfer, in.chat, in.paged, in.mainLost, c.r, c.g, c.b);
    }
#ifdef AXIS_LEDRING
    return;  // Rev: no lights on the ring board (every LED pin held low in ledBegin)
#endif
#ifdef AXIS_HEADLESS
    ledcWrite(8, c.r > c.g ? (c.r > c.b ? c.r : c.b) : (c.g > c.b ? c.g : c.b));  // one LED: the pattern, as brightness
#else
    ledcWrite(8, 255 - c.r);
    ledcWrite(9, 255 - c.g);
    ledcWrite(10, 255 - c.b);
#endif
}

// One session's turn: its caller, its door or transfer if it holds the slot.
static void serviceSession(int i) {
    if (cluster) {
        std::string handle = node ? node->handle() : "", activity;
        if (node) {
            using S = axis::bbs::Node::State;
            S    st = node->state();
            bool mine = busySlot == i;
            activity = mine && (door || viewer || dosShell) ? "Doors" : mine && job ? "Transfer" : st == S::Files ? "Files"
                     : st == S::Applying ? "Applying" : st == S::Chat ? "Chat" : st == S::Messages ? "Messages" : st == S::Menu ? "Menu"
                     : st == S::Splash || st == S::Login || st == S::Password ? "Logging in" : st == S::Split ? "Split chat" : "Online";
            if (!node->available()) activity += " (DND)";
        }
        Session& me = sessions[(size_t)i];
        if (handle + "|" + activity != me.lastStatus) {
            me.lastStatus = handle + "|" + activity;
            cluster->setStatus(i, handle, activity);
        }
    }
    if (job && busySlot == i) {  // a download or upload owns the line
        if (job->needsWorkspace()) {  // a ZIP upload arrived: lend the screen copy to check and rezip it
            drawTransfer();
            size_t   n = 0;
            uint8_t* ws = screenLend(n);
            jobLent = true;
            job->beginProcessing(ws, n, zipStateTake());
        }
        bool running = job->step(millis());
        if (line) line->flush();
        if (millis() - jobDrawnMs > 500) {
            jobDrawnMs = millis();
            drawTransfer();
        }
        if (!running) {
            if (jobLent) {
                screenReturn();
                jobLent = false;
            }
            zipStateFree();
            busySlot = -1;
            const auto& r = job->request();
            logEvent(String(node->handle().c_str()) + (r.upload ? " uploaded " : " downloaded ") +
                     (r.upload ? job->savedName() : r.name).c_str() + (job->ok() ? "" : String(" (failed: ") + job->note().c_str() + ")"));
            if (line && line->carrier()) {
                node->transferDone(job->ok(), job->bytes(), millis() - jobStartMs, job->note(), millis());
                job.reset();
                sendApp(node->takeOutput());
            } else {
                job.reset();
                hangUp();
            }
            screenDirty = true;
        }
        return;
    }
    if (dosShell && busySlot == i) {  // the DOS Prompt owns the line
        bool running = dosShell->step();
        for (auto& e : dosShell->events()) logEvent(e.c_str());
        dosShell->events().clear();
        if (line) line->flush();
        if (!running) finishViewer();
        delay(5);
        return;
    }
    if (viewer && busySlot == i) {  // a built-in door owns the line
        bool running = viewer->step();
        if (line) line->flush();
        if (!running) finishViewer();
        delay(5);
        return;
    }
    if (door && busySlot == i) {  // the door owns the caller's line; the BBS waits
        bool running = door->step(millis() - lastServedMs < 100 ? 8 : 30);  // short slices while a follower's door is asking us
        if (doorMirror && line) {  // a setup program: its screen, as ANSI
            std::string a = doorMirror->update(door->textPage());
            if (!a.empty()) line->write((const uint8_t*)a.data(), a.size());
        }
        if (line) line->flush();  // whatever the door drew this slice
        if (!running) finishDoor();
        return;
    }
    if (line) {
        std::string app = line->takeInput();
        if (!app.empty()) callerInput(app);
        if (line && node) {
            node->poll(millis());
            if (node->state() == axis::bbs::Node::State::InDoor && busySlot >= 0 && busySlot != i) {
                node->resumeAfterDoor(axis::bbs::Node::DoorEnd::Busy, millis());  // another caller here has the slot
                sendApp(node->takeOutput());
                return;
            }
            if (node->state() == axis::bbs::Node::State::InDoor) {
                busySlot = i;
                startDoor();
                return;
            }
            if (node->state() == axis::bbs::Node::State::Transfer && busySlot >= 0 && busySlot != i) {
                node->transferDone(false, 0, 0, "another caller on this board is using the door/transfer slot: try again in a minute",
                                   millis());
                sendApp(node->takeOutput());
                return;
            }
            if (node->state() == axis::bbs::Node::State::Transfer && !job) {
                busySlot = i;
                sendApp(node->takeOutput());  // the transfer window, before the protocol starts
                jobStartMs = jobDrawnMs = millis();
                axis::bbs::UploadInfo info{cfg.bbsName, dialName().c_str(), cfg.location,
                                           std::string(axis::bbs::kAxisSoftware) + " " + axis::bbs::kAxisVersion};
                job.reset(new axis::bbs::TransferJob(callerFiles(), node->transfer(), *line, info));
                return;
            }
            sendApp(node->takeOutput());
            if (node->wantsHangup()) hangUp();
        }
        if (line && !line->carrier()) hangUp();
    }
}

void loop() {
    static uint32_t lastLoop = millis();
    if (millis() - lastLoop > 2000 && millis() - shotDoneMs > 1000)  // (a bench screenshot stalls on purpose)
        logEvent("loop stalled " + String(millis() - lastLoop) + " ms");
    lastLoop = millis();
    handleSerial();
#ifdef AXIS_HEADLESS
    if (portal) {  // the AXIS-SETUP hotspot until a network is picked
        portal->handleClient();
        delay(2);
        return;
    }
#endif
    int         tx, ty;
    static bool wasDown = false;
    bool        down = boardTouch(tx, ty);
    if (down && !wasDown) handleTap(tx, ty);
    wasDown = down;
    if (tapX >= 0) {
        handleTap(tapX, tapY);
        tapX = tapY = -1;
    }
    if (mode == Mode::WifiPick) {  // the touch WiFi picker owns the screen
        if (picker.wantsLeave()) {
            mode = afterPick;
            screenDirty = true;
            return;
        }
        if (picker.wantsRescan()) {
            picker.rescanStarted();
            picker.setNetworks(wifiScan());
            screenDirty = true;
        }
        if (picker.wantsJoin()) {
            picker.joining();
            drawScreen();  // "joining ..."
            String ssid = picker.ssid().c_str(), pass = picker.password().c_str();
            if (wifiJoin(ssid, pass)) {
                wifiRemember(ssid, pass);  // saved only once it works
                logEvent("wifi: joined " + ssid);
                mode = afterPick;
                applyNetwork();
                startCluster();
            } else {
                picker.joinFailed();
                if (afterPick == Mode::Normal || cfg.setupDone) wifiJoinSaved();  // never strand a working board
            }
            screenDirty = true;
        }
        if (screenDirty) drawScreen();
        delay(10);
        return;
    }
    if (WiFi.status() != WL_CONNECTED) {
        if (screenDirty) drawScreen();
        delay(20);
        return;
    }
    if (cluster) cluster->tick(millis());
    if (localNode) {  // the sysop at the board's own screen
        localNode->poll(millis());
        using S = axis::bbs::Node::State;
        if (localNode->state() == S::InDoor) localNode->resumeAfterDoor(axis::bbs::Node::DoorEnd::NotHere, millis());
        if (localNode->state() == S::Transfer)
            localNode->transferDone(false, 0, 0, "downloads need a terminal: call in over telnet", millis());
        localFeed();
        if (localNode->wantsHangup()) endLocal();
    }
    pumpPassing();  // callers passed to other nodes, every turn (doors and transfers included)
    if (cluster) {
        static uint32_t lastFree = 0;  // a card follower keeps the main told of its room
        if (storageHasSd() && millis() - lastFree > 30000u) {
            lastFree = millis();
            cluster->setFreeBytes(storageFreeBytes());
        }
#if AXIS_SD_POOL
        bool uploading = false;  // a follower's caller mid-upload: the mirror waits (an upload may be rezipped)
        for (auto& f : fileServers) uploading = uploading || (f.second && f.second->openUploads());
        if (cluster->role() == axis::cluster::Role::Main && busySlot < 0 && !uploading) mirrorJob.step(millis());
#endif
    }
    ledUpdate();
    if (server.hasClient()) answer(server.available());
    for (size_t i = 0; i < sessions.size(); i++) {
        bindSession((int)i);
        serviceSession((int)i);
        unbindSession();
    }
    static uint32_t lastTick = 0;  // keep the clock and the time-online count moving
    if (millis() - lastTick > 300u) {  // the status lights blink, long lines scroll, the times tick
        lastTick = millis();
        if (mode == Mode::Normal) screenDirty = true;
    }
    if (screenDirty) drawScreen();
    if (!servedRequest && !door) delay(5);  // no nap while a door runs (it sleeps itself when idle) or a follower waits
    servedRequest = false;
}
