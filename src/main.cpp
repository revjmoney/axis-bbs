// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// Axis Micro BBS Firmware entry: board, storage, settings, WiFi, the telnet listener
// and the sysop screen. On first boot it runs setup (tap: on this screen or over telnet).
// Several callers per board. Each can be in native LORD, an ANSI movie or pack, or a transfer.
#include <Arduino.h>
#include <ESPmDNS.h>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <lwip/sockets.h>
#include <soc/soc_memory_types.h>
#include <WiFi.h>
#include <time.h>

#include <dirent.h>
#include <sys/stat.h>

#include <Preferences.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>

#include <freertos/task.h>

#include "bbs/announce.h"
#include "bbs/axisnet.h"
#include "net/telnet.h"
#include "bbs/daystats.h"
#include "bbs/statsshare.h"
#include "bbs/diag.h"
#include "bbs/heapwin.h"
#include "bbs/votebooth.h"
#include "platform/cyd/web.h"
#include "platform/cyd/heapscope.h"
#include "bbs/ansiviewer.h"
#include "bbs/script.h"
#include "lord/game.h"
#include "bbs/ansiterm.h"
#include "bbs/ledsignal.h"
#include "bbs/msgbase.h"
#include "bbs/password.h"
#include "bbs/rotatelog.h"
#include "bbs/theme.h"
#include "bbs/setupportal.h"
#include "cluster/doorfs.h"
#include "bbs/panelproto.h"
#include "bbs/loginguard.h"
#include <base64.h>
#include <mbedtls/base64.h>
#include "cluster/mirrorjob.h"
#include "cluster/pooledlib.h"
#include "cluster/relay.h"
#include "cluster/remotefiles.h"
#include "bbs/config.h"
#include "bbs/eventlog.h"
#include "bbs/station.h"
#include "bbs/sysopshell.h"
#include "bbs/transferjob.h"
#include "bbs/users.h"
#include "bbs/wfc.h"
#include "bbs/wifipicker.h"
#include "bbs/node.h"
#include "bbs/setup_gate.h"
#include "bbs/setup_wizard.h"
#include "platform/cyd/ansi_screen.h"
#include "platform/cyd/board.h"
#include "platform/cyd/clusternet.h"
#include "platform/cyd/config_store.h"
#include "platform/cyd/screen.h"
#include "platform/cyd/sdfiles.h"
#include "platform/cyd/serialfiles.h"
#include "platform/cyd/storage.h"
#include "platform/cyd/wfccyd_art.h"
#include <SD.h>
#include <LittleFS.h>
#include "platform/cyd/wfc_art.h"
#include "platform/cyd/wifi.h"
#include "platform/cyd/wifistream.h"
#include "platform/posix/posixhostfs.h"

// Size over speed for this file's own functions (screens, menus, the loop). After the includes on
// purpose: inline functions from the door headers keep -O2 (a pragma above them slowed the door CPU).
#pragma GCC optimize("Os")

static constexpr int kNodeNumber = 1;

// Last door and the boot reason, for @STATUS. Updated when a door starts or returns. No flash writes.
static axis::bbs::DiagSample boardDiag_;

// What the board was busy with, kept in RTC memory through a crash reset (a power-on clears it): the boot after
// a panic or a watchdog names it in the event log. Added after an unexplained reboot during an upload (2026-11-09).
RTC_NOINIT_ATTR static char     doing_[64];
RTC_NOINIT_ATTR static uint32_t doingMagic_;
constexpr uint32_t              kDoingMagic = 0xA715D01Eu;
static void noteDoing(const String& what) {
    std::snprintf(doing_, sizeof doing_, "%s", what.c_str());
    doingMagic_ = kDoingMagic;
}
static const char* doingNow() { return doingMagic_ == kDoingMagic ? doing_ : ""; }  // the same text noteDoing keeps
static axis::bbs::HeapWindow heapWin;  // lowest heap since boot or the last @HEAPWIN
static size_t                artMax = 0;  // largest art string read from the SD (theme or menu file)

static void noteDoor(axis::bbs::DoorDiag how, uint32_t peakKb) {
    boardDiag_.door = how;
    boardDiag_.doorPeakKb = peakKb;
    boardDiag_.doorStackWords = (uint32_t)uxTaskGetStackHighWaterMark(nullptr);
}

enum class Mode { Normal, SetupChoice, SetupTelnet, SetupTouch, WifiPick };

static axis::bbs::BbsConfig cfg;
static Mode                 mode = Mode::Normal;
static String               setupPin;
static WiFiServer           server;
static axis::bbs::LoginGuard loginGuard;
static std::string sysopRun(const std::string& cmd);  // the in-BBS sysop menu runs its @ commands here  // an address with 3 failed calls in 10 min waits 5 min (more for repeats)
// The sysop control panel (tools/panel): one LAN connection on port 2326 (bbs/panelproto.h)
static WiFiServer           panelServer(2326);
static WiFiClient           panelClient;
static std::unique_ptr<axis::bbs::PanelSession> panelSession;
static String               panelLine;
static uint32_t             panelStatMs = 0;
static bool                 panelWall = false;  // WALL ON: every node's output goes to the panel
static int                  panelConNode = 0;   // the node the panel's console types into (0 = none)
static bool                 panelConJoined = false;  // broke in on a caller (else its own session)
static std::unique_ptr<WifiStream>              line;  // the caller, when one is online
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
static bool                                     ledReport = false;  // @LED: print the status light (bench)

// Bare board: GPIO 2, onboard blue LED, active high. Full duty is far too bright, so
// LEDC stays capped in ledDuty. CYD: RGB on GPIO 4, 16, 17, active low, same pattern
// at about 75 percent (ledCydDuty). GPIO 2 is the panel DC pin there, so it is never
// PWM'd on a screen. Backlight PWM already owns LEDC channel 7, so the RGB uses 0..2.
// The old breakout pins stay inputs with pulldowns, GPIO 12 included (it is a boot
// strap). Those pins overlap the CYD bus, so that list runs only on the ring.
static const uint8_t kRingOff[] = {23, 22, 19, 18, 12, 14, 25, 33, 26, 27, 32, 13, 15, 16, 17, 21, 4, 5};
static const int     kCydRgb[3] = {4, 16, 17};  // red, green, blue, active low

static void ledBegin() {
    if (!boardIsHeadless()) {
        // Hold the RGB off until the first loop, which is after the backlight claims
        // channel 7. Active low, so high is dark. Duty 0 on LEDC would be full on.
        for (int p : kCydRgb) {
            pinMode(p, OUTPUT);
            digitalWrite(p, HIGH);
        }
        return;
    }
    for (uint8_t p : kRingOff) pinMode(p, INPUT_PULLDOWN);
    ledcSetup(0, 5000, 8);
    ledcAttachPin(2, 0);
    ledcWrite(0, 0);
}

static void ledCydAttach() {
    for (int i = 0; i < 3; i++) {
        ledcSetup((uint8_t)i, 5000, 8);
        ledcWrite((uint8_t)i, 255);
        ledcAttachPin(kCydRgb[i], i);
        ledcWrite((uint8_t)i, 255);
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
static bool writeWarrior(const std::string& handle);

// The board's small shared lists (rumors, the BBS list): 50 lines each in internal flash on the main.
static bool listName(const std::string& n) {
    return n == "rumors" || n == "bbslist" || n == "votes" || n == "ballots" || n == "axisnet" || n == "netnotes";
}
// One random line of a list, read a line at a time (the main menu's rumor: the whole list is more than the main can spare then)
static std::vector<std::string> listOne(const std::string& n) {
    std::vector<std::string> v;
    if (!listName(n)) return v;
    File     f = LittleFS.open(("/list_" + n + ".txt").c_str(), "r");
    uint32_t seen = 0;
    while (f && f.available()) {
        String l = f.readStringUntil('\n');
        if (l.length() && esp_random() % ++seen == 0) v.assign(1, l.c_str());
    }
    if (f) f.close();
    return v;
}
static std::vector<std::string> listLoad(const std::string& n) {
    std::vector<std::string> v;
    if (n == "rumor") return listOne("rumors");  // the main menu's one rumor
    if (!listName(n)) return v;
    File f = LittleFS.open(("/list_" + n + ".txt").c_str(), "r");  // a line at a time: one 8 KB block is more than the main can spare
    while (f && f.available()) {
        String l = f.readStringUntil('\n');
        if (l.length()) v.push_back(l.c_str());
    }
    if (f) f.close();
    return v;
}
static void listStore(const std::string& n, const std::vector<std::string>& v) {
    if (!listName(n)) return;
    size_t      cap = n == "axisnet" || n == "netnotes" ? 180u : 120u;
    std::string text;
    for (auto& l : v) {
        text += l.substr(0, cap);
        text += '\n';
    }
    flashWrite(("/list_" + n + ".txt").c_str(), text);
}
static void listAddLocal(const std::string& n, const std::string& line) {
    if (!listName(n)) return;
    if (n == "axisnet") {
        axis::bbs::AxisCard c;
        if (!axis::bbs::axisParse(line, c) || c.host.empty()) return;
        std::vector<std::string> v = listLoad(n);
        axis::bbs::axisMerge(v, c, 24);
        listStore(n, v);
        return;
    }
    if (n == "netnotes") {
        auto tail = [](const std::string& s) {
            size_t p = s.find('|');
            return p == std::string::npos ? s : s.substr(p + 1);
        };
        std::vector<std::string> v = listLoad(n);
        for (auto& e : v)
            if (tail(e) == tail(line)) return;
        v.push_back(line.substr(0, 180));
        while (v.size() > 40) v.erase(v.begin());
        listStore(n, v);
        return;
    }
    std::vector<std::string> v = listLoad(n);
    v.push_back(line.substr(0, 120));
    while (v.size() > (n == "ballots" ? 500u : n == "rumors" ? 300u : 50u)) v.erase(v.begin());
    listStore(n, v);
}

// The sysop's own screens for the theme (MATRIX.ANS, MAIN.ANS): from the card on the main.
// The Downloads screensets run 6 to 11 KB, so the main keeps 12 KB and cuts at the SAUCE record.
// A board without the card asks the main once. The cluster frame is 8 KB, so a follower can
// lose the bottom of a screen that is bigger than that. The settings arriving forget the cache.
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
constexpr size_t kThemeArtCap = 12288;
constexpr size_t kPosterCap = 49152;  // SAYLOR.ANS is 37 KB, SAYLORLG.ANS is 45 KB. Not cached.
constexpr size_t kPosterChunk = 7000;  // a cluster frame is 8 KB, so a follower pulls the poster in pieces
static bool posterName(const std::string& name) { return name == "SAYLOR.ANS" || name == "SAYLORLG.ANS"; }
// Append at most min(fileSize - offset, cap) bytes of f (from offset) to out, cut at the first 0x1a. The file's size is asked
// first so out grows once (a string read in 512-byte pieces doubles its block and costs ~16 KB per screen). False: the
// seek to offset failed (nothing appended).
static bool readFileAppend(FILE* f, size_t cap, size_t offset, std::string& out) {
    const size_t old = out.size();
    long         end = -1;
    if (fseek(f, 0, SEEK_END) == 0) end = ftell(f);
    if (end >= 0) {
        if (fseek(f, (long)offset, SEEK_SET) != 0) return false;
        size_t n = (size_t)end > offset ? (size_t)end - offset : 0;
        if (n > cap) n = cap;
        out.resize(old + n);
        n = n ? fread(&out[old], 1, n, f) : 0;
        out.resize(old + n);
    } else {  // (no size: the old piece-by-piece read)
        if (fseek(f, (long)offset, SEEK_SET) != 0) return false;
        char b[512];
        for (size_t n; out.size() - old < cap && (n = fread(b, 1, sizeof b, f)) > 0;) out.append(b, n);
        if (out.size() - old > cap) out.resize(old + cap);
    }
    if (size_t sauce = out.find('', old); sauce != std::string::npos) out.resize(sauce);
    return true;
}
static bool readThemeFileAppend(const char* folder, const std::string& name, size_t cap, size_t offset, std::string& out) {
    if (!folder || !folder[0]) return false;
    std::string path = std::string("/sd/themes/") + folder + "/" + name;
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    const size_t old = out.size();
    readFileAppend(f, cap, offset, out);
    fclose(f);
    if (out.size() - old > artMax) artMax = out.size() - old;
    return true;
}
static std::string readThemeFile(const char* folder, const std::string& name, size_t cap, size_t offset, bool* opened) {
    std::string s;
    bool        ok = readThemeFileAppend(folder, name, cap, offset, s);
    if (opened) *opened = ok;
    return s;
}
static std::string readThemeFile(const char* folder, const std::string& name) {
    return readThemeFile(folder, name, kThemeArtCap, 0, nullptr);
}
// One piece of the Saylor poster, from the theme's folder (`want` bytes at most, never more than a cluster frame takes).
// It is never read whole: Node streams it a piece at a time (Node::setPoster). A short read at a later offset means
// the end of the file, not "try the other folder".
static std::string posterPieceLocal(const std::string& name, size_t offset, size_t want, bool* opened) {
    *opened = false;
    if (!storageHasSd() || !posterName(name) || offset >= kPosterCap) return "";
    if (want > kPosterChunk) want = kPosterChunk;
    if (want > kPosterCap - offset) want = kPosterCap - offset;
    std::string s = readThemeFile(cfg.theme.c_str(), name, want, offset, opened);
    if (!*opened) s = readThemeFile(axis::bbs::theme::legacyThemeFolder(cfg.theme), name, want, offset, opened);
    if (!*opened) s = readThemeFile("axis", name, want, offset, opened);  // a look without its own poster shows the axis one
    return s;
}
// A follower asks the main for the same piece ("T name offset want"); *exists false when the main has none (or did not answer).
static std::string posterPiece(const std::string& name, size_t offset, size_t want, bool* exists) {
    *exists = false;
    if (!asFollower()) return posterPieceLocal(name, offset, want, exists);
    std::string reply;
    if (!cluster->request(std::string("T\x1f") + name + "\x1f" + std::to_string(offset) + "\x1f" + std::to_string(want), reply, 3000) ||
        reply.empty() || reply[0] != '1')
        return "";
    *exists = true;
    reply.erase(0, 1);  // (in place: no second copy)
    if (reply.size() > want) reply.resize(want);
    return reply;
}
static bool wfcScreen(const std::string& name) {  // a waiting screen (or its lite one): from the WFC skin's folder
    return name == "WFC.ANS" || name == "WFCCYD.ANS" || name == "WFC.LITE.ANS" || name == "WFCCYD.LITE.ANS";
}
static std::string themeArtLocal(const std::string& name) {
    if (!storageHasSd() || name.find('/') != std::string::npos) return "";
    std::string look, file;
    if (axis::bbs::theme::splitLookFile(name, look, file)) {  // "crt:MAIN.ANS": that look's screen (the sysop's preview)
        std::string s = readThemeFile(look.c_str(), file);
        if (s.empty()) s = readThemeFile(axis::bbs::theme::legacyThemeFolder(look), file);
        return s;
    }
    if (posterName(name)) return "";  // the poster streams in pieces (posterPiece), never read whole
    // The waiting screens come from the WFC skin's folder (the theme's, unless the sysop picked another); the rest from the theme.
    const std::string folder = wfcScreen(name) ? axis::bbs::wfcFolder(cfg) : cfg.theme;
    std::string s = readThemeFile(folder.c_str(), name);
    if (s.empty()) s = readThemeFile(axis::bbs::theme::legacyThemeFolder(folder), name);
    return s;
}
// The main's answer to "T name offset": one piece of a theme file, at most kPosterChunk bytes, so the reply stays under the
// cluster's 8 KB frame (a bigger one drops the link). *opened: the file is there.
static std::string themeArtPiece(const std::string& name, size_t off, size_t want, bool* opened) {
    *opened = false;
    if (!storageHasSd() || name.empty() || name.find('/') != std::string::npos || name.find("..") != std::string::npos) return "";
    if (posterName(name)) return posterPieceLocal(name, off, want, opened);
    size_t cap = kThemeArtCap;
    if (off >= cap) return "";
    if (!want || want > kPosterChunk) want = kPosterChunk;
    if (want > cap - off) want = cap - off;
    // the waiting screens come from the WFC skin's folder (the theme's, unless the sysop picked another); the rest from the theme;
    // "crt:MAIN.ANS" from that look's folder (the sysop's preview on a follower)
    std::string folder = wfcScreen(name) ? axis::bbs::wfcFolder(cfg) : cfg.theme, file = name, look;
    if (axis::bbs::theme::splitLookFile(name, look, file)) folder = look;
    std::string s = readThemeFile(folder.c_str(), file, want, off, opened);
    if (!*opened) s = readThemeFile(axis::bbs::theme::legacyThemeFolder(folder), file, want, off, opened);
    return s;
}
// A follower pulls a theme file from the main in pieces. *answered: every request was answered (an empty file is a real "none").
static std::string themeArtRemote(const std::string& name, bool* answered) {
    size_t      cap = posterName(name) ? kPosterCap : kThemeArtCap;
    std::string all;
    if (!posterName(name)) all.reserve(cap);  // (one block: the pieces do not double it)
    *answered = false;
    for (size_t off = 0; all.size() < cap;) {
        std::string reply;
        if (!cluster->request(std::string("T\x1f") + name + "\x1f" + std::to_string(off), reply, 3000) || reply.empty()) {
            *answered = false;  // (a piece went missing: not a whole answer)
            return all;
        }
        *answered = true;
        if (reply[0] != '1') break;
        std::string chunk = reply.substr(1);
        if (chunk.empty()) break;
        all += chunk;
        if (chunk.size() < kPosterChunk) break;
        off += chunk.size();
    }
    if (all.size() > cap) all.resize(cap);
    return all;
}
static std::string themeArt(const std::string& name) {
    if (!asFollower()) return themeArtLocal(name);
    if (posterName(name)) return "";  // the poster streams in pieces (posterPiece), never fetched whole
    bool poster = posterName(name) || name.find(':') != std::string::npos;  // (a preview is seen once: never cached either)
    if (!poster) {
        auto it = artCache.find(name);
        if (it != artCache.end()) return it->second;
    }
    bool        answered = false;
    std::string art = themeArtRemote(name, &answered);
    if (!poster && answered) artCache[name] = art;  // (the poster is one login screen, then it can go; a failed ask is asked again)
    return art;
}
// ViSiON/3-style menu files (MAIN.MNU, MAIN.ANS...): this board's own /menus first (its SD card or flash),
// then the main's over the cluster. Only the small .MNU files are cached; art is fetched each time it shows.
static std::map<std::string, std::string> menuCache;
// A menu screen the theme draws (MAIN.ANS, PDMATRIX.ANS as MATRIX.ANS ...): the theme's file, so switching the theme
// switches the menus too. Null when the theme has none (or the name is a menu file): the menu set's own then.
static FILE* openThemeScreen(const std::string& name) {
    const char* file = axis::bbs::theme::menuScreenFile(name);
    if (!file || !storageHasSd() || cfg.theme.empty()) return nullptr;
    FILE* f = fopen((std::string("/sd/themes/") + cfg.theme + "/" + file).c_str(), "rb");
    if (!f)
        if (const char* old = axis::bbs::theme::legacyThemeFolder(cfg.theme)) f = fopen((std::string("/sd/themes/") + old + "/" + file).c_str(), "rb");
    return f;
}
// theme: a menu screen the theme draws comes from the theme (off for the main's "U" answer to a follower: theme
// screens run past the 8 KB cluster frame, so a follower pulls those through "T" in pieces instead)
static std::string menuFileLocal(const std::string& name, bool theme = true) {
    std::string s;
    if (name.empty() || name.find('/') != std::string::npos || name.find("..") != std::string::npos) return s;
    size_t cap = 8192;
    FILE*  f = theme ? openThemeScreen(name) : nullptr;
    if (f) cap = kThemeArtCap;  // (theme screens run to 11 KB)
    else f = fopen((std::string(storageRoot()) + "/menus/" + name).c_str(), "rb");
    if (f) {
        readFileAppend(f, cap, 0, s);
        fclose(f);
    }
    if (s.size() > artMax) artMax = s.size();
    return s;
}
static std::string menuFile(const std::string& name) {
    if (asFollower() && cluster)
        if (const char* screen = axis::bbs::theme::menuScreenFile(name)) {  // the theme's screen from the main, in pieces (not cached)
            bool        answered = false;
            std::string art = themeArtRemote(screen, &answered);
            if (!art.empty()) return art;
        }
    std::string s = menuFileLocal(name);
    if (!s.empty() || !asFollower()) return s;
    bool mnu = name.size() > 4 && name.compare(name.size() - 4, 4, ".MNU") == 0;
    if (mnu)
        if (auto it = menuCache.find(name); it != menuCache.end()) return it->second;
    std::string reply;
    if (cluster->request(std::string("U\x1f") + name, reply, 3000) && !reply.empty() && reply[0] == '1') {
        reply.erase(0, 1);  // (in place: art is several KB, no second copy)
        s = std::move(reply);
    }
    if (mnu) menuCache[name] = s;
    return s;
}

// /scripts/NAME.axs. 8000 bytes so a follower can carry the file in one 8 KB cluster frame.
static std::string scriptRead(const std::string& path) {
    std::string s;
    if (FILE* f = fopen(path.c_str(), "rb")) {
        char b[512];
        for (size_t n; s.size() < 8000 && (n = fread(b, 1, sizeof b, f)) > 0;) s.append(b, n);
        fclose(f);
    }
    return s;
}
static std::vector<std::string> scriptNamesLocal() {
    std::vector<std::string> v;
    if (!storageHasSd()) return v;
    std::string dirp = std::string(storageRoot()) + "/scripts";
    if (DIR* d = opendir(dirp.c_str())) {
        while (dirent* e = readdir(d)) {
            std::string n = e->d_name;
            if (n.size() <= 4) continue;
            std::string ext = n.substr(n.size() - 4);
            for (char& c : ext) c = (char)std::tolower((unsigned char)c);
            if (ext != ".axs") continue;
            std::string base = n.substr(0, n.size() - 4);
            if (axis::bbs::scriptNameOk(base)) v.push_back(base);
        }
        closedir(d);
    }
    std::sort(v.begin(), v.end());
    if (v.size() > 35) v.resize(35);
    return v;
}
static std::string scriptFile(const std::string& name) {
    if (!axis::bbs::scriptNameOk(name)) return {};
    if (storageHasSd()) return scriptRead(std::string(storageRoot()) + "/scripts/" + name + ".axs");
    if (!asFollower() || !cluster) return {};
    std::string reply;
    if (cluster->request(std::string("J\x1f") + name, reply, 3000) && !reply.empty() && reply[0] == '1') reply.erase(0, 1);
    else reply.clear();
    return reply;
}
static std::vector<std::string> scriptNames() {
    if (storageHasSd() || !asFollower() || !cluster) return scriptNamesLocal();
    std::vector<std::string> v;
    std::string reply;
    if (cluster->request(std::string("J\x1f"), reply, 3000) && !reply.empty() && reply[0] == '1') {
        for (size_t pos = 1; pos < reply.size();) {
            size_t e = reply.find('\n', pos);
            if (e == std::string::npos) e = reply.size();
            std::string n = reply.substr(pos, e - pos);
            if (!n.empty() && n.back() == '\r') n.pop_back();
            if (axis::bbs::scriptNameOk(n)) v.push_back(n);
            pos = e + 1;
        }
    }
    return v;
}
static void wireScripts(axis::bbs::Node* n) {
    if (!n) return;
    n->setScripts([](const std::string& name) { return scriptFile(name); }, [] { return scriptNames(); });
}

// a menu's art from this board's own storage a piece at a time (false when it isn't here: the whole-file path)
static bool menuArtPieces(const std::string& name, const axis::bbs::ArtEmit& piece) {
    if (name.empty() || name.find('/') != std::string::npos || name.find("..") != std::string::npos) return false;
    if (asFollower() && axis::bbs::theme::menuScreenFile(name)) return false;  // a follower: the theme's screen comes from the main (menuFile)
    FILE* f = openThemeScreen(name);  // the theme's screen first
    if (!f) f = fopen((std::string(storageRoot()) + "/menus/" + name).c_str(), "rb");
    if (!f) return false;
    char b[512];
    for (size_t n, total = 0; total < 16384 && (n = fread(b, 1, sizeof b, f)) > 0; total += n) {
        const void* sauce = memchr(b, 0x1a, n);  // the SAUCE record: not part of the picture
        piece(b, sauce ? (size_t)((const char*)sauce - b) : n);
        if (sauce) break;
    }
    fclose(f);
    return true;
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
static axis::door::LockTable                    doorLocks;
static std::unique_ptr<axis::bbs::AnsiViewer>   viewer;  // this session's movie or pack door
static std::unique_ptr<axis::lord::NativeLord>  nativeLord;  // this session's LORD, parked with the session
static std::unique_ptr<axis::door::HostFs>      lordFs;
static std::unique_ptr<axis::door::LockTable>   lordLocks;
static std::unique_ptr<axis::bbs::TransferJob>  job;     // this session's download or upload
static uint32_t                                 jobStartMs = 0, jobDrawnMs = 0;
static uint32_t                                 doorStartMs = 0;

// The inflate window for a ZIP upload check. Movies allocate their own inside the viewer.
// The panel sprite stays the waiting screen.
struct ZipBuf {
    uint8_t* window = nullptr;
    uint8_t* state  = nullptr;
    void     release() {
        heap_caps_free(window);
        heap_caps_free(state);
        window = state = nullptr;
    }
    ~ZipBuf() { release(); }
    ZipBuf() = default;
    ZipBuf(ZipBuf&& o) noexcept : window(o.window), state(o.state) { o.window = o.state = nullptr; }
    ZipBuf& operator=(ZipBuf&& o) noexcept {
        if (this != &o) {
            release();
            window   = o.window;
            state    = o.state;
            o.window = o.state = nullptr;
        }
        return *this;
    }
    ZipBuf(const ZipBuf&)            = delete;
    ZipBuf& operator=(const ZipBuf&) = delete;
    bool held() const { return window || state; }
    bool ensure(size_t windowBytes, size_t stateBytes) {
        if (!window) window = (uint8_t*)heap_caps_malloc(windowBytes, MALLOC_CAP_8BIT);
        if (!state) state = (uint8_t*)heap_caps_malloc(stateBytes, MALLOC_CAP_8BIT);
        if (window && state) return true;
        release();
        return false;
    }
};
static ZipBuf               zipRam;
static constexpr size_t     kZipWindow = 32768;
static std::unique_ptr<axis::bbs::Node>         node;
static std::unique_ptr<axis::bbs::SetupGate>    gate;
static std::unique_ptr<axis::bbs::SetupWizard>  wizard;
static uint32_t callStartMs = 0;
// Calls, posts and uploads today and the lifetime call total, in internal flash (/stats.txt) so a reboot keeps them.
// The day counters roll at local midnight; until the clock is set they just keep counting.
static axis::bbs::DayStats dayStats;
// In a cluster the main owns them. A follower (or a board still finding the main) adds its calls and uploads to
// statsShare and sends them up (statsSync); it shows the main's numbers from the heartbeat plus what it has not had
// acknowledged, and never rolls, counts into, saves or overwrites its own dayStats and /stats.txt (the card board can be
// a follower for a moment, before a cardless main hands main back: its own numbers must survive that). A follower that
// stands in as main counts on its own file. Posts need no sending: a follower's caller posts straight into the main's
// message base (CountingMsgs), which counts them there.
static axis::bbs::StatsShare statsShare;
static axis::bbs::StatsInbox statsInbox;  // main: the follower batches already applied
static bool statsOwner() {
    if (!cluster) return true;
    auto r = cluster->role();
    return r == axis::cluster::Role::Main || r == axis::cluster::Role::Off;
}
static void statsSave() {
    if (!flashWrite("/stats.txt", dayStats.format())) Serial.println("axis: stats save failed");
}
static void rollDayCounters() {
    if (!statsOwner()) return;  // only the main's midnight counts
    time_t now = time(nullptr);
    if (now < 1700000000) return;
    struct tm* t = localtime(&now);
    if (dayStats.roll((t->tm_year + 1900) * 400 + t->tm_yday)) statsSave();
}
// At boot, after the users: the saved counters, or (no file yet) a total seeded from every account's call count.
static void statsLoad() {
    if (!LittleFS.exists("/stats.txt")) {
        for (auto& u : users.all()) dayStats.total += u.calls;
        statsSave();
    } else if (!dayStats.parse(flashRead("/stats.txt"))) {
        Serial.println("axis: /stats.txt unreadable, stats start from zero");
    }
}
// What the screens and @STATS show: this board's own numbers on the main, the main's last ones on a follower (none
// before it has heard them), plus anything still waiting to be sent up.
static axis::bbs::DayStats statsShown() {
    if (statsOwner()) return statsShare.shown(dayStats);
    return statsShare.shown();
}
static bool statsSendNow = false;  // a follower counted something: send it at the top of the next loop turn
static void countCall() {
    if (statsOwner()) {
        rollDayCounters();
        dayStats.call();
        statsSave();
    } else statsShare.call(), statsSendNow = true;
}
static void countUpload() {
    if (statsOwner()) {
        rollDayCounters();
        dayStats.upload();
        statsSave();
    } else statsShare.upload(), statsSendNow = true;
}
static void countPost() {
    if (statsOwner()) {
        rollDayCounters();
        dayStats.post();
        statsSave();
    } else statsShare.post(), statsSendNow = true;
}

// Several callers per board. Each loop turn serves one session at a time: its line, node, game,
// movie, and transfer are moved into the globals above, then moved back.
struct Session {
    std::unique_ptr<WifiStream>             line;
    std::unique_ptr<axis::bbs::Node>        node;
    uint32_t                                startMs = 0;
    std::string                             lastStatus;
    std::unique_ptr<axis::lord::NativeLord> lord;
    std::unique_ptr<axis::door::HostFs>     lordFs;
    std::unique_ptr<axis::door::LockTable>  lordLocks;
    ZipBuf                                  zip;  // freed after viewer and job (members destruct in reverse)
    std::unique_ptr<axis::bbs::AnsiViewer>  viewer;
    std::unique_ptr<axis::bbs::TransferJob> job;
    uint32_t                                doorStartMs = 0;
    uint32_t                                jobStartMs = 0, jobDrawnMs = 0;
};
static std::vector<Session> sessions(1);
static int                  curSession = -1;  // whose line and node the globals hold, -1 = none
static void bindSession(int i) {
    curSession = i;
    line = std::move(sessions[(size_t)i].line);
    node = std::move(sessions[(size_t)i].node);
    nativeLord = std::move(sessions[(size_t)i].lord);
    lordFs = std::move(sessions[(size_t)i].lordFs);
    lordLocks = std::move(sessions[(size_t)i].lordLocks);
    zipRam = std::move(sessions[(size_t)i].zip);
    viewer = std::move(sessions[(size_t)i].viewer);
    job = std::move(sessions[(size_t)i].job);
    callStartMs = sessions[(size_t)i].startMs;
    doorStartMs = sessions[(size_t)i].doorStartMs;
    jobStartMs = sessions[(size_t)i].jobStartMs;
    jobDrawnMs = sessions[(size_t)i].jobDrawnMs;
}
static void unbindSession() {
    if (curSession < 0) return;
    sessions[(size_t)curSession].line = std::move(line);
    sessions[(size_t)curSession].node = std::move(node);
    sessions[(size_t)curSession].lord = std::move(nativeLord);
    sessions[(size_t)curSession].lordFs = std::move(lordFs);
    sessions[(size_t)curSession].lordLocks = std::move(lordLocks);
    sessions[(size_t)curSession].zip = std::move(zipRam);
    sessions[(size_t)curSession].viewer = std::move(viewer);
    sessions[(size_t)curSession].job = std::move(job);
    sessions[(size_t)curSession].startMs = callStartMs;
    sessions[(size_t)curSession].doorStartMs = doorStartMs;
    sessions[(size_t)curSession].jobStartMs = jobStartMs;
    sessions[(size_t)curSession].jobDrawnMs = jobDrawnMs;
    doorStartMs = jobStartMs = jobDrawnMs = 0;
    curSession = -1;
}
static bool boardHasLord() {
    if (nativeLord) return true;
    for (auto& s : sessions)
        if (s.lord) return true;
    return false;
}
static bool boardHasViewer() {
    if (viewer) return true;
    for (auto& s : sessions)
        if (s.viewer) return true;
    return false;
}
static bool boardHasJob() {
    if (job) return true;
    for (auto& s : sessions)
        if (s.job) return true;
    return false;
}
static bool inflateHeld() {  // a movie is playing, or a ZIP upload is being checked
    if (zipRam.held() || (viewer && viewer->holdsRam())) return true;
    for (auto& s : sessions)
        if (s.zip.held() || (s.viewer && s.viewer->holdsRam())) return true;
    return false;
}
static void noteZipHeap() { web::setDoorRunning(inflateHeld()); }
static void dropLord() {
    nativeLord.reset();
    lordLocks.reset();
    lordFs.reset();
}
struct LordMail {
    std::string          me;
    axis::door::HostFs*  fs = nullptr;
    int                  seen[48] = {};
    int                  nseen = 0;
    bool                 loaded = false;
};
static LordMail lordMailBoxes[4];
static WifiStream* sessionLine(size_t i) { return (int)i == curSession ? line.get() : sessions[i].line.get(); }
static axis::bbs::Node* sessionNode(size_t i) { return (int)i == curSession ? node.get() : sessions[i].node.get(); }
static int callersOnline() {
    int n = 0;
    for (size_t i = 0; i < sessions.size(); i++) n += sessionLine(i) != nullptr;
    return n;
}
static String   lastCaller = "none yet";
// LC: a follower shows the main's last caller (or its own newest one not yet sent), its own only before it has heard.
static std::string statsLastCaller() {
    std::string h = statsOwner() ? std::string() : statsShare.lastCaller();
    return h.empty() ? std::string(lastCaller.c_str()) : h;
}
static bool     screenDirty = true;
static int      tapX = -1, tapY = -1;  // a tap from the @TAP serial command

static uint32_t hwRandom() { return esp_random(); }

// A follower heard the main's WFC numbers (a heartbeat, or the answer to a batch): redraw when they changed. They are
// only shown (statsShown), never copied into this board's own dayStats.
static void statsHeard(bool ok) {
    if (!ok || statsOwner()) return;
    const axis::bbs::DayStats& m = statsShare.main();
    static axis::bbs::DayStats seen;
    static std::string         seenLc;
    bool same = m.day == seen.day && m.calls == seen.calls && m.posts == seen.posts && m.uploads == seen.uploads && m.total == seen.total &&
                seenLc == statsShare.mainLastCaller();
    if (same) return;  // (every heartbeat brings them: redraw only on a change)
    seen = m;
    seenLc = statsShare.mainLastCaller();
    screenDirty = true;
}
// Follower: send what it counted to the main, right after counting and then every 5 s until acknowledged. Only from the
// top of loop(), never inside a caller's turn; 1.5 s at most (the main answers in milliseconds). A link that is down is
// skipped at once and the counts wait.
static void statsSync() {
    static uint32_t nextTry = 0;
    if (!statsShare.waiting() || !asFollower()) return;
    if (!statsSendNow && (int32_t)(millis() - nextTry) < 0) return;
    statsSendNow = false;
    std::string reply;
    bool        ok = cluster->request(statsShare.request(hwRandom()), reply, 1500) && statsShare.answer(reply);
    statsHeard(ok);
    nextTry = millis() + (ok ? 0 : 5000);
}

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
    s.callsToday = statsShown().calls;
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
static std::unique_ptr<axis::bbs::Node> serialWfc;  // the serial port's WFC terminal, while it is open
static void logEvent(const String& what) {
    time_t now = time(nullptr);
    char   stamp[8] = "--:--";
    if (now > 1700000000) strftime(stamp, sizeof stamp, "%H:%M", localtime(&now));
    events.add(std::string(stamp) + " " + what.c_str());
    if (eventFileReady) eventFile.append(std::string(stamp) + " " + what.c_str());
    if (!serialWfc) Serial.printf("axis: %s\n", what.c_str());  // (not while the serial WFC owns the screen)
    if (panelSession && panelSession->authed() && panelClient.connected())  // the control panel's event feed
        panelClient.print(String("EVT ") + stamp + " " + what + "\n");
    std::string a = axis::bbs::announceLine(what.c_str());  // scene news for the Discord channel
    if (!a.empty()) announceOut(a);
}

static void saveUsers() {
    HeapScope scope("saveUsers");
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
struct CountingMsgs : axis::bbs::FileMessageStore {  // counts a public post for the WFC's today box
    using axis::bbs::FileMessageStore::FileMessageStore;
    int post(const std::string& area, const axis::bbs::Message& m) override {
        int n = axis::bbs::FileMessageStore::post(area, m);
        if (n && area != "email") countPost();
        return n;
    }
};
static std::unique_ptr<CountingMsgs> localMsgs;
static axis::bbs::RemoteMessageStore remoteMsgs([](const std::string& req, std::string& reply) {
    return cluster && cluster->request(req, reply, 4000);
});
static axis::bbs::MessageStore& callerMessages() {
    if (!localMsgs) {
        std::string root = std::string(storageRoot()) + "/msgs";
        ::mkdir(root.c_str(), 0777);
        localMsgs.reset(new CountingMsgs(root));
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

// The waiting-for-caller screen on the board's own screen: the WFC skin's WFCCYD.ANS (or the built-in art/wfccyd.ans) in 45x24
// cells of 7x10 (315x240, centred), the live values of wfcSnapshot(), its keys and node lines tappable. Expanded again every 5 s
// when the values changed, and whenever it comes back from another screen. The art is read from the card once and kept while
// the WFC is up; the terminal (~5 KB) is only held while the art is drawn.
static int                                detailSession = -1;  // the node detail screen is up for this session (-1: the WFC)
static std::vector<axis::bbs::WfcHotspot> wfcSpots;            // where the keys and node lines landed
static axis::bbs::WfcSnapshot             wfcShown;            // the values on the screen
static uint32_t                           wfcDrawnMs = 0, wfcSoonUntil = 0;  // last expanded; a "coming soon" line until then
static bool                               wfcStale = true;     // draw it at the next turn (another screen was up, a key was tapped)
static std::string                        wfcArt, wfcArtFolder;  // the art while the WFC is up, and the skin folder it came from
static const int                          kWfcLeft = 2;        // 45 cells of 7 pixels: 2 spare pixels on the left, 3 on the right
static axis::bbs::WfcSnapshot             wfcSnapshot();
// The sysop menu on the board's own screen: the one SysopShell, 40 columns in 8x16 letters, a tap picks the row.
static std::unique_ptr<axis::bbs::SysopShell> boardShell;
static std::unique_ptr<axis::bbs::AnsiTerm>   shellTerm;  // 40x15: the footer can wrap without scrolling the list
static std::string                            shellOut;
static bool                                   shellKeys = false;  // the touch keyboard is up (a question, or keys for the list)
static BigText                                bigText;            // the shell's letters, copied from shellTerm
static const int                              kShellRows = 13;    // 208 pixels of shell, the buttons under it

// The local sysop login: the BBS on the board's own screen, the sysop logged in, an 80x25
// terminal in 4x8 cells, a touch keyboard for typing (a line at a time, sent with Enter).
static std::unique_ptr<axis::bbs::Node> localNode;

// A caller already online keeps a copy of the settings from when they logged in.
// Section switches and the door list have to reach that copy or the menu stays stale.
static void pushBoardToCallers() {
    auto one = [](axis::bbs::Node* n) {
        if (n) n->applyBoard(cfg);
    };
    one(node.get());
    one(localNode.get());
    for (size_t i = 0; i < sessions.size(); i++) one(sessions[i].node.get());
    for (size_t i = 0; i < sessions.size(); i++)  // modem_link off: calls already on stop taking link reports
        if (WifiStream* l = sessionLine(i)) l->acceptLink(cfg.modemLink);
}
static std::unique_ptr<axis::bbs::AnsiTerm> localTerm;    // (only while the console is open: ~9 KB)
static std::unique_ptr<AnsiScreen>          localScreen;
static bool                              localKeys = false;  // the keyboard is up
static bool                              localZoom = true;   // 40 columns in 8x8 cells (readable), or all 80 in 4x8
static int                               localPan = 0;       // zoomed: which 40 columns (0 or 40)
static axis::bbs::TouchKeyboard          localKb("type, then OK", false, 70);
static void                              runCommand(const String& cmd, Print& out);
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
        for (int r = 0; r < localTerm->rows(); r++)
            for (int c = 0; c < localTerm->cols(); c++) {
                const auto& cell = localTerm->cell(r, c);
                localScreen->cells[r][c] = {cell.ch, cell.attr};
            }
        if (localZoom) {  // follow the cursor across the two halves (once it moves: << and >> stick until then)
            static int lastCol = -1;
            if (localTerm->col() != lastCol) {
                lastCol = localTerm->col();
                if (lastCol >= localPan + 40) localPan = 40;
                else if (lastCol < localPan) localPan = 0;
            }
            localScreen->renderZoom(scr, localTerm->rows(), localPan);  // 200 pixels, 40 columns
        } else {
            localScreen->renderSharp(scr, localTerm->rows());  // 200 pixels of the BBS, all 80 columns, crisp
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

// The WFC on the board: false when it is unchanged (nothing to push). The art is expanded into a heap terminal that goes again
// once the screen copy has it, so the loop's stack only holds the snapshot.
static bool drawWfc() {
    HeapScope scope("drawWfc");
    uint32_t now = millis();
    bool     soon = wfcSoonUntil && (int32_t)(now - wfcSoonUntil) < 0;
    if (wfcSoonUntil && !soon) wfcSoonUntil = 0, wfcStale = true;  // the "coming soon" line goes
    if (!wfcStale && now - wfcDrawnMs < 5000) return false;
    wfcDrawnMs = now;
    axis::bbs::WfcSnapshot s = wfcSnapshot();
    if (!wfcStale && s == wfcShown) return false;
    std::unique_ptr<axis::bbs::AnsiTerm> t(new (std::nothrow) axis::bbs::AnsiTerm(45, 24));
    if (!t) {  // short on memory: say so (not whatever screen was up before), and try again at the next turn
        scr.fillScreen(pal::BLACK);
        scr.setFont(&fonts::Font0);
        scr.setTextColor(pal::ORANGE, pal::BLACK);
        scr.drawString("low memory: the waiting screen will be back", 8, 116);
        return true;
    }
    std::string folder = axis::bbs::wfcFolder(cfg);
    if (wfcArt.empty() || folder != wfcArtFolder) {  // read once while the WFC is up (and again when the skin changes)
        std::string art = axis::bbs::cleanWfcArt(themeArt("WFCCYD.ANS"));
        wfcArt = art.empty() ? std::string(kWfcCydAns) : art;  // the theme has none: the built-in art/wfccyd.ans
        // a follower's empty answer may be a request that failed: the built-in for now, and ask again at the next redraw
        // (a theme that really has none is answered from the art cache then, without asking the main)
        if (!art.empty() || !asFollower()) wfcArtFolder = folder;
    }
    wfcSpots = axis::bbs::drawWfc(wfcArt, s, *t);
    if (soon) t->feed("\x1b[24;2H\x1b[0;1;33;44m That one is coming soon. \x1b[0m");
    AnsiScreen::renderCells(scr, *t);
    wfcShown = std::move(s);
    wfcStale = false;
    return true;
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
        std::string from = l->remoteIP().toString().c_str(), radio = axis::net::linkLabel(n->link(), false);
        if (!radio.empty()) from += "  " + radio;  // "RADIO SF7" for a caller on the LFD3K-SM modem
        row(5, "from", from.c_str());
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

// The sysop menu on the board's own screen (a key tapped on the WFC): the one SysopShell in 40 columns of 8x16 letters, a tap
// on a row picks it, KEYS / ENTER / BACK / EXIT under it, and the touch keyboard for its questions.
static void touchButton(int x, int y, int w, int h, const char* label, bool lit) {
    uint16_t fill = lit ? pal::ORANGE : pal::DIM;
    scr.fillRoundRect(x, y, w, h, 5, fill);
    scr.drawRoundRect(x, y, w, h, 5, lit ? pal::WHITE : pal::ORANGE);
    scr.setTextColor(lit ? pal::BLACK : pal::WHITE, fill);
    scr.setTextDatum(textdatum_t::middle_center);
    scr.drawString(label, x + w / 2, y + h / 2);
    scr.setTextDatum(textdatum_t::top_left);
}
static void closeShell() {
    boardShell.reset();
    shellTerm.reset();
    std::string().swap(shellOut);
    shellKeys = false;
    screenDirty = true;
}
static void shellFeed() {  // what the shell wrote, onto its terminal; a question brings up the keyboard
    if (shellTerm && !shellOut.empty()) shellTerm->feed(shellOut);
    shellOut.clear();
    screenDirty = true;
    if (!boardShell || !boardShell->open() || mode != Mode::Normal) {  // left, or Rerun setup took the screen
        closeShell();
        return;
    }
    if (boardShell->asking() && !boardShell->yesNo() && !shellKeys) {
        localKb.reset(boardShell->question().substr(0, 40), boardShell->secret());
        shellKeys = true;
    }
}
static void openShell(axis::bbs::SysopSection s) {
    shellTerm.reset(new (std::nothrow) axis::bbs::AnsiTerm(40, 15));
    boardShell.reset(new (std::nothrow) axis::bbs::SysopShell(shellOut, sysopRun, 40, true));  // (at the board: every item)
    if (!shellTerm || !boardShell) {
        closeShell();
        logEvent("sysop menu: the board is short on memory, try again in a moment");
        return;
    }
    shellKeys = false;
    boardShell->beginAt(s);
    shellFeed();
}
static void shellKey(char c) {
    if (boardShell) boardShell->key(c);
    shellFeed();
}
static void shellTap(int x, int y) {
    if (shellKeys) {
        localKb.tap(x, y);
        if (!localKb.done() && !localKb.cancelled()) return;
        bool        asked = boardShell->asking(), ok = localKb.done();
        std::string typed = localKb.text();
        localKb.reset("type, then OK", false);
        shellKeys = false;
        if (!ok) {
            if (asked) shellKey(0x1b);  // the question is dropped
            return;
        }
        for (char c : typed)
            if (boardShell) boardShell->key(c);
        if (boardShell && asked) boardShell->key('\r');  // the answer; keys for the list go as typed
        shellFeed();
        return;
    }
    if (y < kShellRows * 16) {
        boardShell->tapRow(y / 16);
        shellFeed();
        return;
    }
    if (y < 206) return;
    bool yn = boardShell->yesNo();
    if (x < 80) {
        if (yn) {
            shellKey('y');
        } else {
            localKb.reset("keys, then OK", false);
            shellKeys = true;
        }
    } else if (x < 158) {
        shellKey(yn ? 'n' : '\r');
    } else if (x < 238) {
        shellKey(boardShell->asking() ? 0x1b : 'q');  // back a layer (or drop the question); off the top layer: the WFC
    } else {
        closeShell();
    }
}
static void drawShell() {
    scr.fillScreen(pal::BLACK);
    scr.setFont(&fonts::Font2);
    if (shellKeys) {
        scr.setTextColor(pal::ORANGE, pal::BLACK);
        scr.drawString(localKb.title().c_str(), 8, 4);
        scr.drawRect(4, 28, 312, 30, pal::ORANGE);
        scr.setTextColor(pal::WHITE, pal::BLACK);
        scr.drawString((localKb.shown() + "_").c_str(), 10, 35);
        for (auto& k : localKb.keys()) touchButton(k.x, k.y, k.w, k.h, k.label.c_str(), k.lit);
    } else {
        bigText.clear(0x07);
        for (int r = 0; r < kShellRows; r++)
            for (int c = 0; c < 40; c++) {
                const auto& cell = shellTerm->cell(r, c);
                bigText.cells[r][c] = {cell.ch, cell.attr};
            }
        bigText.render(scr, 0, kShellRows);
        bool yn = boardShell->yesNo();
        touchButton(4, 210, 74, 28, yn ? "YES" : "KEYS", yn);
        touchButton(82, 210, 74, 28, yn ? "NO" : "ENTER", false);
        touchButton(160, 210, 76, 28, "BACK", false);
        touchButton(242, 210, 74, 28, "EXIT", true);
    }
    scr.setFont(&fonts::Font0);
}

// The sysop panel while a caller downloads or uploads: a drippy window with a live progress bar.
static void drawTransfer() {
    if (!screenReady()) return;
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
    axis::bbs::Node* who = node.get();
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
    if (!screenReady()) {  // a door is running
        wfcStale = true;
        if (!wfcArt.empty()) std::string().swap(wfcArt);
        return;
    }
    if (mode != Mode::Normal || job || localNode || boardShell || detailSession >= 0) {  // the WFC is drawn afresh when it is back
        wfcStale = true;
        if (!wfcArt.empty()) std::string().swap(wfcArt);  // and its art read again then (the skin may change meanwhile)
    }
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
    } else if (boardShell) {
        drawShell();
    } else if (detailSession >= 0) {
        drawNodeDetail(detailSession);
    } else if (!drawWfc()) {  // the WFC: nothing changed, nothing to push
        screenDirty = false;
        return;
    }
    screenPush();
    screenDirty = false;
}

// ------------------------------------------------------------------ network
static void modemSearchForget();
static bool modemSearchRunning();
static bool     networkPending = false;  // applyNetwork() waits for an mDNS modem search to end (the loop keeps running)
static uint32_t networkPendingMs = 0;
static void applyNetworkNow() {
    modemSearchForget();  // MDNS.end() must never meet a search: one that is done is freed here
    MDNS.end();
    if (MDNS.begin(cfg.boardName.c_str())) MDNS.addService("telnet", "tcp", cfg.telnetPort);
    server.end();
    server.begin(cfg.telnetPort);
    server.setNoDelay(true);
    panelServer.begin();
    panelServer.setNoDelay(true);
    Serial.printf("axis: listening on %s:%d, byte heap %u, largest %u\n", dialName().c_str(), cfg.telnetPort,
                  heap_caps_get_free_size(MALLOC_CAP_8BIT), heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
}

// The clock zone at once; mDNS and the listeners now, or (an mDNS search for modems is running) once it has ended or
// 3.5 s have passed: applyNetworkDue() in the loop. Callers and the panel keep running meanwhile.
static void applyNetwork() {
    setenv("TZ", axis::bbs::tzRule(cfg.timezone), 1);
    tzset();
    if (!modemSearchRunning()) {
        networkPending = false;
        applyNetworkNow();
        return;
    }
    if (!networkPending) networkPendingMs = millis();
    networkPending = true;
}
static void applyNetworkDue() {  // each loop turn, after modemSearchPoll()
    if (!networkPending || (modemSearchRunning() && millis() - networkPendingMs < 3500u)) return;
    networkPending = false;
    applyNetworkNow();  // (a search still running after 3.5 s is left to MDNS.end(), which frees it)
}
static void sendApp(const std::string& s) {
    if (s.empty() || !line) return;
    line->write((const uint8_t*)s.data(), s.size());
    line->flush();
}

// The LORA FLOWTRON DATAMASTER 3000 SERIAL MODEM - LFD3K-SM: the newest link report on this board (the WFC's |R? codes),
// the last 10 radio calls (newest first) and the modems found on the LAN by mDNS (the sysop menu's System > Modem, @MODEM).
struct RadioCall {
    std::string who, tiers;
    uint32_t    secs = 0, bytes = 0;
    int32_t     retrains = -1;
};
static std::vector<RadioCall> radioCalls;
static axis::net::LinkInfo    lastRadio;
static std::string            lastRadioWho;
struct FoundModem {
    std::string name, ip;
};
static std::vector<FoundModem> foundModems;
static mdns_search_once_t*     modemSearch = nullptr;  // an mDNS search for _lfd3k._tcp running in the background
static uint32_t                modemSearchMs = 0;
static bool modemSearchRunning() { return modemSearch != nullptr; }
static void modemSearchForget() {  // before MDNS.end(): a finished search is freed here (never waits)
    if (!modemSearch) return;
    mdns_result_t* res = nullptr;
    if (mdns_query_async_get_results(modemSearch, 0, &res)) {
        if (res) mdns_query_results_free(res);
        mdns_query_async_delete(modemSearch);
    }  // (still running: MDNS.end() frees it with the rest)
    modemSearch = nullptr;
}
static void modemSearchStart() {
    if (modemSearch || networkPending || WiFi.status() != WL_CONNECTED) return;
    modemSearch = mdns_query_async_new(nullptr, "_lfd3k", "_tcp", MDNS_TYPE_PTR, 3000, 8, nullptr);
    modemSearchMs = millis();
}
static std::string safeName(const char* s, size_t n) {  // letters, digits, space and - _ . only, 24 at most
    std::string v;
    for (size_t i = 0; s && i < n && s[i] && v.size() < 24; i++)
        if (std::isalnum((unsigned char)s[i]) || s[i] == ' ' || s[i] == '-' || s[i] == '_' || s[i] == '.') v += s[i];
    return v;
}
static void modemSearchPoll() {  // never waits: the results once the search is done
    mdns_result_t* res = nullptr;
    if (!modemSearch || !mdns_query_async_get_results(modemSearch, 0, &res)) return;
    foundModems.clear();
    for (mdns_result_t* r = res; r && foundModems.size() < 8; r = r->next) {
        std::string name = safeName(r->instance_name ? r->instance_name : r->hostname, 64);
        for (size_t t = 0; t < r->txt_count; t++)  // TXT name= is the modem's own name
            if (r->txt[t].key && std::strcmp(r->txt[t].key, "name") == 0 && r->txt[t].value)
                name = safeName(r->txt[t].value, r->txt_value_len ? r->txt_value_len[t] : 64);
        std::string ip;
        for (mdns_ip_addr_t* a = r->addr; a && ip.empty(); a = a->next)
            if (a->addr.type == ESP_IPADDR_TYPE_V4) ip = IPAddress(a->addr.u_addr.ip4.addr).toString().c_str();
        foundModems.push_back({name.empty() ? std::string("(no name)") : name, ip.empty() ? std::string("-") : ip});
    }
    if (res) mdns_query_results_free(res);
    mdns_query_async_delete(modemSearch);
    modemSearch = nullptr;
    for (size_t i = 0; i < sessions.size(); i++)  // a Modem page that is open shows them now
        if (axis::bbs::Node* n = sessionNode(i)) n->sysopReload("MODEM");
    if (localNode) localNode->sysopReload("MODEM");
    if (boardShell && boardShell->reloadIf("MODEM")) screenDirty = true;
}

static void hangUp() {
    noteDoing("idle");
    viewer.reset();
    job.reset();
    zipRam.release();
    noteZipHeap();
    dropLord();
    if (line && node && node->failedLogin()) {  // wrong passwords: count it against the caller's address
        uint32_t ip = (uint32_t)line->remoteIP();
        loginGuard.failed(ip, millis());
        if (!loginGuard.allowed(ip, millis()))
            logEvent("login: " + line->remoteIP().toString() + " locked out for " + String(loginGuard.secondsLeft(ip, millis()) / 60) + " min");
    }
    if (line && node && node->viaModem()) {  // a radio call: into the Modem page's last 10
        std::string who = node->handle();
        for (char& c : who)
            if (c == '|') c = ' ';
        radioCalls.insert(radioCalls.begin(), {who, node->linkTiers(), (millis() - callStartMs) / 1000, line->bytesIn() + line->bytesOut(),
                                               node->link().retrains});
        if (radioCalls.size() > 10) radioCalls.resize(10);
    }
    line.reset();  // closes the connection
    logEvent("hang up" + heapText() + " " + String(node && !node->handle().empty() ? node->handle().c_str() : "") + " after " +
             String((millis() - callStartMs) / 1000) + "s");
    if (node && !node->handle().empty()) {
        lastCaller = node->handle().c_str();
        if (!statsOwner()) statsShare.caller(node->handle()), statsSendNow = true;  // the main's LC is the cluster's
    }
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

static bool        axisProbeSoon = false;
static uint32_t   axisProbeAt = 0;
static size_t     axisProbeIx = 0;
static bool       axisDirty = false;

struct AxisBridge {
    WiFiClient                 remote;
    axis::net::TelnetFilter    telnet;
    int                        session = -1;
    std::string                held;
    uint32_t                   start = 0;
    bool                       on = false;
};
static AxisBridge axisBridge;

static void answer(WiFiClient incoming);

static void axisAccept() {
    if (server.hasClient()) answer(server.available());
}

static std::string axisRead(WiFiClient& c, uint32_t budget) {
    std::string in;
    uint32_t    t0 = millis(), idle = t0;
    while (c.connected() && millis() - t0 < budget && in.size() < 3500) {
        while (c.available() && in.size() < 3500) {
            in.push_back((char)c.read());
            idle = millis();
        }
        if (in.size() >= 4 && in.compare(in.size() - 4, 4, "END\n") == 0) break;
        if (!in.empty() && millis() - idle > 350) break;
        axisAccept();
        if (cluster) cluster->tick(millis());
        delay(2);
    }
    return in;
}

static bool axisSame(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); i++)
        if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i])) return false;
    return true;
}

static axis::bbs::AxisCard axisSelfCard() {
    axis::bbs::AxisCard me;
    me.name = cfg.bbsName;
    me.host = cfg.publicHost.empty() ? std::string(WiFi.localIP().toString().c_str()) : cfg.publicHost;
    me.port = cfg.telnetPort > 0 ? cfg.telnetPort : 23;
    me.location = cfg.location;
    me.sysop = cfg.sysopHandle;
    me.blurb = std::string(axis::bbs::kAxisSoftware) + " " + axis::bbs::kAxisVersion;
    int on = 0;
    for (size_t i = 0; i < sessions.size(); i++) on += sessionLine(i) ? 1 : 0;
    me.online = on;
    me.seen = unixNow();
    return me;
}

static std::vector<std::string> axisNotesFor(const axis::bbs::AxisCard& peer) {
    std::vector<std::string> out;
    for (const std::string& n : listLoad("netnotes")) {
        size_t bar = n.find('|');
        if (bar == std::string::npos) continue;
        std::string rest = n.substr(bar + 1);
        size_t      b = rest.find('|'), c = b == std::string::npos ? b : rest.find('|', b + 1);
        if (b == std::string::npos) continue;
        std::string to = c == std::string::npos ? rest.substr(b + 1) : rest.substr(b + 1, c - (b + 1));
        if (axisSame(to, peer.name) || axisSame(to, peer.host)) out.push_back(rest);
        if (out.size() >= 6) break;
    }
    return out;
}

static void axisApply(const std::string& reply) {
    if (asFollower()) return;
    std::vector<std::string> lines = listLoad("axisnet"), before = lines;
    axis::bbs::AxisHeard     heard = axis::bbs::axisTake(reply, lines, axisSelfCard().host);
    if (!heard.ok) return;
    if (lines != before) {
        listStore("axisnet", lines);
        axisDirty = true;
        logEvent("axis net: directory updated");
    }
    uint32_t now = unixNow();
    for (const std::string& n : heard.notes) {
        listAddLocal("netnotes", std::to_string(now) + "|" + n);
        axisDirty = true;
    }
}

// A peer opens with "AXISNET 1" on the telnet port. Anything else is a caller, and a
// line we already read is handed back to the login. Returns true when this socket was a peer.
static bool axisServe(WiFiClient& c, std::string& giveBack) {
    giveBack.clear();
    uint32_t t0 = millis();
    while (c.connected() && !c.available() && millis() - t0 < 40) delay(1);
    if (!c.connected() || !c.available() || c.peek() != 'A') return false;
    std::string first;
    uint32_t    idle = millis();
    while (c.connected() && millis() - idle < 250 && first.size() < 40) {
        if (!c.available()) {
            delay(1);
            continue;
        }
        char ch = (char)c.read();
        idle = millis();
        if (ch == '\n') break;
        if (ch != '\r') first.push_back(ch);
    }
    if (first != "AXISNET 1") {
        giveBack = first;
        if (!giveBack.empty() && giveBack.back() != '\n') giveBack.push_back('\n');
        return false;
    }
    std::string all = "AXISNET 1\n" + axisRead(c, 1000);
    axisApply(all);
    axis::bbs::AxisCard them;
    size_t              hello = all.find("HELLO ");
    if (hello != std::string::npos) {
        size_t e = all.find('\n', hello);
        axis::bbs::axisParse(all.substr(hello + 6, e == std::string::npos ? std::string::npos : e - (hello + 6)), them);
    }
    std::string greet = axis::bbs::axisGreet(axisSelfCard(), listLoad("axisnet"), axisNotesFor(them));
    c.print(greet.c_str());
    c.flush();
    delay(20);
    c.stop();
    logEvent(String("axis net: spoke with ") + (them.name.empty() ? them.host.c_str() : them.name.c_str()));
    return true;
}

static void axisProbeOne() {
    if (asFollower() || axisBridge.on) return;
    std::vector<std::string> lines = listLoad("axisnet");
    if (lines.empty()) return;
    if (axisProbeIx >= lines.size()) axisProbeIx = 0;
    axis::bbs::AxisCard peer;
    if (!axis::bbs::axisParse(lines[axisProbeIx++], peer) || peer.host.empty()) return;
    WiFiClient out;
    if (!out.connect(peer.host.c_str(), (uint16_t)peer.port, 1200)) {
        logEvent(String("axis net: no answer from ") + peer.host.c_str());
        return;
    }
    out.setNoDelay(true);
    out.print(axis::bbs::axisGreet(axisSelfCard(), listLoad("axisnet"), axisNotesFor(peer)).c_str());
    out.flush();
    std::string reply = axisRead(out, 1200);
    out.stop();
    axisApply(reply);
}

static bool axisEscape(std::string& held, char c, std::string& forward) {
    if (c == '\x1c') return true;
    held.push_back(c);
    if (held == "~.\r" || held == "~.\n") return true;
    if (held == "~" || held == "~.") return false;
    forward.append(held);
    held.clear();
    return false;
}

// One slice of a call to another board. True: still on that call, the BBS should not take keys.
static bool axisPump(int i) {
    if (!axisBridge.on || axisBridge.session != i || !line || !node) return false;
    bool        timed = millis() - axisBridge.start > 600000;
    bool        gone = !line->carrier() || !axisBridge.remote.connected() || timed;
    if (gone) {
        axisBridge.remote.stop();
        axisBridge.on = false;
        const char* why = !line->carrier() ? nullptr : timed ? "That call went on for 10 minutes." : "Back from that board.";
        node->axisCallDone(line->carrier(), why);
        return false;
    }
    uint8_t buf[256];
    int     n = axisBridge.remote.available();
    if (n > 0) {
        if (n > (int)sizeof buf) n = (int)sizeof buf;
        int r = axisBridge.remote.read(buf, (size_t)n);
        if (r > 0) {
            std::string app, reply;
            axisBridge.telnet.fromNet(buf, (size_t)r, app, reply);
            if (!reply.empty()) axisBridge.remote.write((const uint8_t*)reply.data(), reply.size());
            if (!app.empty()) line->write((const uint8_t*)app.data(), app.size());
        }
    }
    std::string keys = line->takeInput(), forward;
    bool        leave = false;
    for (char c : keys) leave = axisEscape(axisBridge.held, c, forward) || leave;
    if (!forward.empty()) {
        std::string wire = axisBridge.telnet.toNet((const uint8_t*)forward.data(), forward.size());
        if (!wire.empty()) axisBridge.remote.write((const uint8_t*)wire.data(), wire.size());
    }
    if (leave) {
        axisBridge.remote.stop();
        axisBridge.on = false;
        node->axisCallDone(true, "Back from that board.");
        return false;
    }
    line->flush();
    delay(2);
    return true;
}

static void axisStart(int i) {
    if (!node) return;
    if (axisBridge.on) {
        node->axisCallDone(false, "This board is already on a call.");
        return;
    }
    std::string host = node->axisDialHost();
    int         port = node->axisDialPort();
    if (host.empty()) {
        node->axisCallDone(false, "That board has no address.");
        return;
    }
    WiFiClient out;
    if (!out.connect(host.c_str(), (uint16_t)port, 1200)) {
        logEvent(String("axis net: could not call ") + host.c_str());
        node->axisCallDone(false, "Could not reach that board.");
        return;
    }
    out.setNoDelay(true);
    axisBridge.remote = out;
    axisBridge.telnet = axis::net::TelnetFilter();
    axisBridge.session = i;
    axisBridge.held.clear();
    axisBridge.start = millis();
    axisBridge.on = true;
    logEvent(String("axis net: calling ") + host.c_str());
}

static void answerIn(WiFiClient incoming, const std::string& giveBack = {});

static void answer(WiFiClient incoming) {
    std::string giveBack;
    if (axisServe(incoming, giveBack)) return;
    if (!loginGuard.allowed((uint32_t)incoming.remoteIP(), millis())) {  // too many failed logins from there lately
        uint32_t m = (loginGuard.secondsLeft((uint32_t)incoming.remoteIP(), millis()) + 59) / 60;
        incoming.print("\r\nToo many failed logins from your address. Try again in " + String(m) + " minute" + (m == 1 ? "" : "s") + ".\r\n");
        delay(50);
        incoming.stop();
        return;
    }
    int free = -1;  // setup takes one caller; a set-up board as many as its sessions
    for (size_t i = 0; i < sessions.size() && free < 0; i++)
        if (!sessionLine(i) && (i == 0 || (cfg.setupDone && mode == Mode::Normal))) free = (int)i;
    if (free >= 0) {
        bindSession(free);
        answerIn(incoming, giveBack);
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

// The waiting-for-caller screen's live values: every node in the cluster, the newest events, today's counters.
static axis::bbs::WfcSnapshot wfcSnapshot() {
    axis::bbs::WfcSnapshot w;
    w.bbsName = cfg.bbsName;
    w.sysop = cfg.sysopHandle;
    w.lastCaller = statsLastCaller();
    w.ip = WiFi.localIP().toString().c_str();
    std::string t = timeText().c_str();  // "YYYY-MM-DD HH:MM"
    if (t.size() >= 16) w.date = t.substr(0, 10), w.time = t.substr(11, 5);
    w.page = pageText.c_str();
    if (cluster && cluster->nodes().size() > 1) {
        for (auto& n : cluster->nodes()) w.nodes.push_back({n.number, n.handle, n.activity});
        std::sort(w.nodes.begin(), w.nodes.end(), [](const axis::bbs::WfcNode& a, const axis::bbs::WfcNode& b) { return a.number < b.number; });
    } else {
        for (size_t i = 0; i < sessions.size(); i++) {
            axis::bbs::Node* n = sessionNode(i);
            w.nodes.push_back({sessionNumber((int)i), n ? n->handle() : std::string(), n ? "Online" : "", n ? n->linkShown() : ""});
        }
    }  // (in a cluster the label rides in each board's activity: serviceSession)
    w.radio = axis::bbs::wfcRadio(lastRadio, lastRadioWho);
    const auto& ev = events.lines();
    for (auto it = ev.rbegin(); it != ev.rend() && w.events.size() < 8; ++it) w.events.push_back(*it);
    rollDayCounters();
    axis::bbs::DayStats st = statsShown();
    w.callsToday = st.calls;
    w.postsToday = st.posts;
    w.uploadsToday = st.uploads;
    w.totalCalls = st.total;
    w.sdFreeKb = (uint32_t)(storageFreeBytes() >> 10);
    w.ramFreeKb = heap_caps_get_free_size(MALLOC_CAP_8BIT) / 1024;
    w.uptimeS = millis() / 1000;
    w.main = !asFollower();
    return w;
}

static bool panelConsoleNew = false;  // answerIn: the next session is the panel's own sysop console
static void answerIn(WiFiClient incoming, const std::string& giveBack) {
    if (panelConsoleNew) line.reset(new PanelStream());
    else line.reset(new WifiStream(incoming));
    line->tapNode = sessionNumber(curSession);
    line->acceptLink(cfg.modemLink);  // the LFD3K-SM modem's link reports (LFD3K-LINK, telnet option 90)
    callStartMs = millis();
    logEvent((panelConsoleNew ? String("sysop console from the panel") : "call from " + line->remoteIP().toString()) + " (heap " + String(heap_caps_get_free_size(MALLOC_CAP_8BIT) / 1024) +
             " KB, largest " + String(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) / 1024) + " KB)");
    line->start();
    if (!giveBack.empty()) line->inject(giveBack);
    if (cfg.setupDone && mode == Mode::Normal) {
        countCall();
        node.reset(new axis::bbs::Node(sessionNumber(curSession), sysInfo, cfg, &callerFiles()));
        int slot = curSession;
        node->setAccounts(&users, saveUsers, hwRandom, unixNow);
        node->setWarrior(writeWarrior);
        node->onEvent = [](const std::string& e) { logEvent(String(e.c_str())); };
        node->setSysopRun(sysopRun);
        node->setWfcSnapshot(wfcSnapshot);
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
                    for (size_t i = 0; i < sessions.size(); i++) {
                        axis::bbs::Node* n = sessionNode(i);
                        v.push_back({sessionNumber((int)i), n ? n->handle() : "", n ? "Online" : "", n ? n->linkShown() : ""});
                    }
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
        node->setPoster(posterPiece);
        node->setMenus([](const std::string& name) { return menuFile(name); }, menuArtPieces);
        wireScripts(node.get());
        axis::bbs::theme::setArt([](const std::string& name) { return themeArt(name); });  // section header screens
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
        if (panelConsoleNew) {  // the sysop, from the panel: no password asked, and the WFC is the first screen
            node->beginLocal(millis());
            node->openWfc(millis());
        }
        else node->begin(millis());
        sendApp(node->takeOutput());
    } else {
        gate.reset(new axis::bbs::SetupGate(setupPin.c_str()));
        gate->begin();
        sendApp(gate->takeOutput());
    }
    screenDirty = true;
}

// ------------------------------------------------------------------ doors
// Give back this session's door. Safe to call twice, and it does not allocate.
static void releaseDoorSlot() {
    doorFs.reset();
    if (curSession >= 0) (doorRemoteLocks ? *doorRemoteLocks : doorLocks).releaseOwner(sessionNumber(curSession));
    doorRemoteLocks.reset();
    viewer.reset();
    zipRam.release();
    noteZipHeap();
    nativeLord.reset();
    screenDirty = true;
}

static void abortDoor(axis::bbs::Node::DoorEnd how) {
    using E = axis::bbs::Node::DoorEnd;
    noteDoor(how == E::OutOfMemory ? axis::bbs::DoorDiag::OutOfMemory
             : how == E::TimeLimit  ? axis::bbs::DoorDiag::TimeLimit
             : how == E::LoadError  ? axis::bbs::DoorDiag::LoadError
             : how == E::CarrierLost ? axis::bbs::DoorDiag::Hangup
                                    : axis::bbs::DoorDiag::Quit,
             0);
    releaseDoorSlot();
    if (!node) return;
    try {
        node->resumeAfterDoor(how, millis());
        sendApp(node->takeOutput());
    } catch (...) {
        esp_rom_printf("axis: door menu recover failed\n");
    }
}

static std::string areaName(const std::string& id) {
    for (auto& a : cfg.areas)
        if (a.id == id) return a.name;
    return id;
}

static bool lordSame(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); i++)
        if ((a[i] | 0x20) != (b[i] | 0x20)) return false;
    return true;
}
static bool lordCoded(const std::string& body) {
    size_t i = 0;
    while (i < body.size()) {
        if (body[i] == '`') return true;
        size_t n = body.find('\n', i);
        if (n == std::string::npos) break;
        i = n + 1;
    }
    return false;
}
static void lordLoadSeen(LordMail* box) {
    box->loaded = true;
    box->nseen = 0;
    if (!box->fs) return;
    int h = box->fs->open("MAILGOT", 0, false, false);
    if (h < 0) return;
    int32_t nums[48];
    int n = box->fs->read(h, nums, (int)sizeof nums);
    box->fs->close(h);
    if (n < 4) return;
    int count = n / 4;
    if (count > 48) count = 48;
    box->nseen = count;
    for (int i = 0; i < count; i++) box->seen[i] = (int)nums[i];
}
static void lordSaveSeen(LordMail* box) {
    if (!box->fs) return;
    int32_t nums[48];
    for (int i = 0; i < box->nseen; i++) nums[i] = box->seen[i];
    int h = box->fs->open("MAILGOT", 2, true, true);
    if (h < 0) return;
    if (box->nseen > 0) box->fs->write(h, nums, box->nseen * 4);
    box->fs->close(h);
}
static int lordPull(void* ctx, char* body, int bodyN, char* from, int fromN) {
    auto* box = (LordMail*)ctx;
    if (!box || bodyN < 2 || fromN < 2) return 0;
    if (!box->loaded) lordLoadSeen(box);
    body[0] = from[0] = 0;
    for (const auto& h : callerMessages().headers("email")) {
        if (!lordSame(h.to, box->me)) continue;
        bool saw = false;
        for (int i = 0; i < box->nseen; i++)
            if (box->seen[i] == h.num) saw = true;
        if (saw) continue;
        axis::bbs::Message m;
        if (!callerMessages().read("email", h.num, m) || !lordCoded(m.body)) continue;
        if (box->nseen == 48) {
            std::memmove(box->seen, box->seen + 1, sizeof(int) * 47);
            box->nseen = 47;
        }
        box->seen[box->nseen++] = h.num;
        lordSaveSeen(box);
        std::snprintf(from, (size_t)fromN, "%s", m.from.c_str());
        std::snprintf(body, (size_t)bodyN, "%s", m.body.c_str());
        return h.num;
    }
    return 0;
}
static void lordPost(void* ctx, const char* from, const char* to, const char* body) {
    (void)ctx;
    axis::bbs::Message m;
    m.from = from ? from : "";
    m.to = to ? to : "";
    m.subject = "LORD";
    m.date = std::string(timeText().c_str());
    m.body = body ? body : "";
    callerMessages().post("email", m);
}

static std::string lordFolderId() {
    for (auto& d : cfg.doors)
        if (d.exe == "*LORD" && !d.id.empty()) return d.id;
    return "lord";
}

// Registration, and a later validation, write one warrior. A follower writes the main's file.
static bool writeWarrior(const std::string& handle) {
    std::string id = lordFolderId();
    bool remote = !storageHasSd() && asFollower();
    std::unique_ptr<axis::door::HostFs> fs;
    std::unique_ptr<axis::door::LockTable> remoteLocks;
    axis::door::LockTable* locks = &doorLocks;
    if (remote) {
        auto ask = [](const std::string& req, std::string& reply) { return cluster && cluster->request(req, reply, 8000); };
        auto* rfs = new axis::cluster::RemoteHostFs(ask, id);
        fs.reset(rfs);
        remoteLocks.reset(new axis::cluster::RemoteLockTable(ask, rfs));
        locks = remoteLocks.get();
    } else {
        fs.reset(new axis::PosixHostFs(std::string(storageRoot()) + "/doors/" + id));
    }
    int r = axis::lord::ensureWarrior(*fs, locks, 64, handle);
    if (r < 0) logEvent(String("warrior: could not write ") + handle.c_str());
    else if (r == 1) logEvent(String("warrior: new ") + handle.c_str());
    return r >= 0;
}

static void startDoorInner() {
    const axis::bbs::DoorEntry& d = node->door();
    sendApp(node->takeOutput());
    if (d.exe == "*LORD") {  // one game per caller. The EXE slot stays free.
        logEvent(String(node->handle().c_str()) + " opened " + d.name.c_str());
        doorStartMs = millis();
        dropLord();
        std::string dir = std::string(storageRoot()) + "/doors/" + d.id;
        int me = sessionNumber(curSession);
        bool remote = !storageHasSd() && cluster && cluster->role() == axis::cluster::Role::Follower;
        axis::door::LockTable* locks = &doorLocks;
        if (remote) {
            auto ask = [](const std::string& req, std::string& reply) { return cluster && cluster->request(req, reply, 8000); };
            auto* rfs = new axis::cluster::RemoteHostFs(ask, d.id);
            lordFs.reset(rfs);
            lordLocks.reset(new axis::cluster::RemoteLockTable(ask, rfs));
            locks = lordLocks.get();
        } else {
            lordFs.reset(new axis::PosixHostFs(dir));
        }
        int idx = curSession;
        if (idx < 0) idx = 0;
        if (idx > 3) idx = 3;
        LordMail& box = lordMailBoxes[idx];
        if (box.me != node->handle()) {
            box.me = node->handle();
            box.nseen = 0;
            box.loaded = false;
        }
        box.fs = lordFs.get();
        nativeLord.reset(new axis::lord::NativeLord(*lordFs, *line, node->handle(), locks, me));
        nativeLord->setRng(hwRandom);
        nativeLord->setMailbox(&box, lordPull, lordPost);
        nativeLord->begin();
        if (line) line->flush();
        return;
    }
    if (d.exe == "*DOSSHELL" || d.exe == "*SCRIPT") {
        // *SCRIPT runs inside the session. *DOSSHELL is not in this firmware.
        logEvent(String(node->handle().c_str()) + " opened " + d.name.c_str() + " (not a slot door)");
        abortDoor(axis::bbs::Node::DoorEnd::LoadError);
        return;
    }
    doorStartMs = millis();
    if (!d.exe.empty() && d.exe[0] == '*') {  // one viewer per caller. It allocates an inflate window only while a piece plays.
        logEvent(String(node->handle().c_str()) + " opened " + d.name.c_str());
        std::vector<std::pair<std::string, std::string>> va;  // "ansi,scene-acid,...": several areas, a picker first
        for (size_t from = 0; from <= d.args.size();) {
            size_t comma = d.args.find(',', from);
            std::string id = d.args.substr(from, comma == std::string::npos ? std::string::npos : comma - from);
            if (!id.empty()) va.push_back({id, areaName(id)});
            if (comma == std::string::npos) break;
            from = comma + 1;
        }
        viewer.reset(new axis::bbs::AnsiViewer(callerFiles(), va.empty() ? d.args : va[0].first, va.empty() ? areaName(d.args) : va[0].second,
                                               nullptr, 0, *line));
        viewer->setAreas(va);
        if (!node->lite()) viewer->setClock([] { return (uint32_t)millis(); });  // ANSI movies play at modem speed (a lite caller: unpaced)
        viewer->begin();
        line->flush();
        return;
    }
    // An .EXE needs the 8086 runtime. That runtime is dosemu/ in the repo, and it is not linked here.
    logEvent(String(node->handle().c_str()) + " opened " + d.name.c_str() + " (no DOS runtime)");
    abortDoor(axis::bbs::Node::DoorEnd::LoadError);
}

static void startDoor() {
    bool lord = node && node->door().exe == "*LORD";
    if (node) noteDoing(String("door ") + node->door().name.c_str() + " by " + node->handle().c_str());
    noteDoor(axis::bbs::DoorDiag::Running, 0);
    try {
        startDoorInner();
    } catch (const std::bad_alloc&) {
        esp_rom_printf("axis: ALLOC FAILED during door start (uncaught)\n");
        if (lord) {
            dropLord();
            if (node) node->resumeAfterDoor(axis::bbs::Node::DoorEnd::OutOfMemory, millis());
        } else abortDoor(axis::bbs::Node::DoorEnd::OutOfMemory);
    } catch (...) {
        esp_rom_printf("axis: door start failed\n");
        if (lord) {
            dropLord();
            if (node) node->resumeAfterDoor(axis::bbs::Node::DoorEnd::LoadError, millis());
        } else abortDoor(axis::bbs::Node::DoorEnd::LoadError);
    }
    noteZipHeap();
}

static void finishLord() {
    bool drop = !line || !line->carrier();
    noteDoor(drop ? axis::bbs::DoorDiag::Hangup : axis::bbs::DoorDiag::Quit, 0);
    dropLord();
    logEvent(String("door lord ended after ") + String((millis() - doorStartMs) / 1000) + "s");
    if (drop) {
        hangUp();
        return;
    }
    if (!node) return;
    node->resumeAfterDoor(axis::bbs::Node::DoorEnd::Normal, millis());
    sendApp(node->takeOutput());
}

static void finishViewer() {
    noteDoor(line && line->carrier() ? axis::bbs::DoorDiag::Quit : axis::bbs::DoorDiag::Hangup, 0);
    releaseDoorSlot();
    logEvent(String("door ") + node->door().id.c_str() + " ended after " + String((millis() - doorStartMs) / 1000) + "s");
    if (!line || !line->carrier()) {
        hangUp();
        return;
    }
    node->resumeAfterDoor(axis::bbs::Node::DoorEnd::Normal, millis());
    sendApp(node->takeOutput());
}

// keystrokes from the caller, already through the telnet filter
static void callerInput(const std::string& app) {
    const uint8_t* p = (const uint8_t*)app.data();
    if (node) {
        std::string before = node->handle();
        {
            HeapScope scope("node");
            node->input(p, app.size(), millis());
        }
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

// Callers on this board, not counting the panel's own sysop console (the sysop doing the restart or format).
static int callersBesideSysop() {
    int n = 0;
    for (size_t i = 0; i < sessions.size(); i++)
        if (sessionLine(i) && !(panelConNode && !panelConJoined && sessionNumber((int)i) == panelConNode)) n++;
    return n;
}

// Erase and re-make the SD card: the touch menu's YES button and "@SDFORMAT FORMAT" (the sysop menu) share this.
// False when it was refused because a caller is online; `result` says what happened either way. force: the
// serial console's "@SDFORMAT FORMAT force" goes ahead anyway.
static bool sdFormatNow(String& result, bool& ok, bool force = false) {
    if (!force && callersBesideSysop()) {
        logEvent("sysop: SD format refused, a caller is online");
        return false;
    }
    ok = storageFormatSd(result);
    logEvent(String("sysop: SD format ") + (ok ? "done" : "FAILED") + ": " + result);
    makeAreaFolders();
    return true;
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
        pushBoardToCallers();
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
    // a joining board's copy of the users, one record at a time in id order (ids only grow; @UDEL can remove one
    // mid-copy, which only means it is not sent). Never a list of every record at once.
    cluster->nextUser = [](int after, int& id, std::string& record) {
        const axis::bbs::User* best = nullptr;
        for (const auto& u : users.all())
            if (u.id > after && (!best || u.id < best->id)) best = &u;
        if (!best) return false;
        id = best->id;
        record = axis::bbs::formatUser(*best);
        return true;
    };
    cluster->heapLargest = [] { return (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT); };
    cluster->beatExtra = [] {  // the main's WFC stats ride every heartbeat (about 60 bytes)
        return axis::bbs::encodeStatsSnapshot(dayStats, lastCaller.c_str());
    };
    cluster->onBeatExtra = [](const std::string& x) { statsHeard(statsShare.heard(x)); };
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
    // A follower's request is told apart by its first letter. The file server (cluster/remotefiles.cpp) owns
    // L R S A C W F D X N; the rest here are M O T U J V Y Z Q P K H B and "AN" (B: a follower's WFC stats). Scripts were "S" and @ commands "C"
    // until 2026-11-09: that took every file open (packs, views, downloads) and upload away from a follower.
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
        if (req.rfind("T\x1f", 0) == 0) {  // a theme screen, "T name offset": every file in pieces, so the frame stays under 8 KB
            HeapScope scope("T");
            std::string rest = req.substr(2);
            size_t      sep = rest.find('\x1f');
            std::string name = sep == std::string::npos ? rest : rest.substr(0, sep);
            size_t      off = 0, want = 0;  // "T name offset [want]": want 0 (an older follower) is a whole 7000 byte piece
            if (sep != std::string::npos) {
                size_t* to = &off;
                for (char ch : rest.substr(sep + 1)) {
                    if (ch == '\x1f' && to == &off) {
                        to = &want;
                        continue;
                    }
                    if (ch < '0' || ch > '9' || *to > kPosterCap) break;
                    *to = *to * 10 + (size_t)(ch - '0');
                }
            }
            if (off > (posterName(name) ? kPosterCap : kThemeArtCap)) return std::string("0");
            bool        opened = false;
            std::string piece = themeArtPiece(name, off, want ? want : kPosterChunk, &opened);
            if (!opened && posterName(name)) return std::string("0");
            return "1" + piece;
        }
        if (req.rfind("U\x1f", 0) == 0) {
            HeapScope scope("U");
            return "1" + menuFileLocal(req.substr(2), false);
        }  // a menu file or its art (/menus only: under 8 KB)
        if (req.rfind("J\x1f", 0) == 0) {  // a script program, or the folder when the name is empty
            std::string name = req.substr(2);
            if (name.empty()) {
                std::string out = "1";
                for (const std::string& n : scriptNamesLocal()) out += n + "\n";
                return out;
            }
            return std::string("1") + scriptFile(name);
        }
        if (req.rfind("V\x1f", 0) == 0) return "1" + axis::bbs::followerCommand(req.substr(2), sysopRun);  // a follower's @ command (never a board-only one)
        if (req == "AN") {  // a follower asked the main to speak to the Axis Net
            axisProbeSoon = true;
            return std::string("1");
        }
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
        if (req.rfind("B\x1f", 0) == 0) {  // a follower's calls and uploads for the WFC stats: applied once, answered with ours
            rollDayCounters();
            std::string last = lastCaller.c_str();
            if (statsInbox.apply(req, dayStats, last) == axis::bbs::StatsInbox::Result::Applied) {
                statsSave();
                lastCaller = last.c_str();
                screenDirty = true;
            }
            return "1" + axis::bbs::encodeStatsSnapshot(dayStats, lastCaller.c_str());
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
    // the cluster key lives in this board's flash, never in axis.ini: settings travel to every board
    String key = p.getString("key", "");
    p.end();
    cluster->setRandom(hwRandom);
    cluster->setKey(key.c_str());
    cluster->start(millis(), previous);
}

// No screen to pick WiFi on: a hotspot, AXIS-SETUP, with one web page (setupportal.h). The
// password goes from the form straight to wifiJoin and the board's saved networks, never logged.
// Compiled into the one image. Only the ring starts it.
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

static void startPicker(Mode after) {
    afterPick = after;
    mode = Mode::WifiPick;
    picker = axis::bbs::WifiPicker();
    picker.setNetworks(wifiScan());
    picker.setCanLeave(WiFi.status() == WL_CONNECTED);
    screenDirty = true;
}

// Space on the WFC: the sysop logs on at the board's own screen.
static void startLocal() {
    // the local console needs ~35 KB; while the cluster reshuffles (a board restarting) the main can have far less
    if (ESP.getFreeHeap() < 40000 || heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) < 30000) {
        logEvent("local logon: the board is busy, try again in a moment");
        return;
    }
    localTerm.reset(new axis::bbs::AnsiTerm());
    localScreen.reset(new AnsiScreen());
    localNode.reset(new axis::bbs::Node(cluster && !cluster->numbers().empty() ? cluster->numbers()[0] : 1, sysInfo, cfg,
                                        &callerFiles()));
    localNode->setAccounts(&users, saveUsers, hwRandom, unixNow);
    localNode->setWarrior(writeWarrior);
    localNode->onEvent = [](const std::string& e) { logEvent(String(e.c_str())); };
    localNode->setSysopRun(sysopRun);
    localNode->setWfcSnapshot(wfcSnapshot);
    localNode->setMessages(&callerMessages());
    localNode->setArt([](const std::string& name) { return themeArt(name); });  // the theme's screens, like a caller gets
    localNode->setPoster(posterPiece);
    localNode->setMenus([](const std::string& name) { return menuFile(name); }, menuArtPieces);
    wireScripts(localNode.get());
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
    localNode->beginLocalMenu(millis());
    logEvent(String("local sysop login: ") + localNode->handle().c_str());
    localFeed();
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
            } else if (y < 200) {
                localNode->tapRow(y * localTerm->rows() / 200);
                localFeed();
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
        if (boardShell) {  // the sysop menu: a row, the buttons, or the keyboard
            shellTap(x, y);
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
        // the WFC: a key opens the sysop menu at its section, a node line its details, Space the local logon
        int  num = 0;
        char k = axis::bbs::wfcKeyAt(wfcSpots, y / 10, x < kWfcLeft ? 0 : (x - kWfcLeft) / 7, &num);
        if (k == 'N') {  // its details, if the node is on this board
            for (size_t i = 0; i < sessions.size(); i++)
                if (sessionNumber((int)i) == num) detailSession = (int)i;
            return;
        }
        if (k == ' ') {
            startLocal();
            return;
        }
        axis::bbs::SysopSection sec = axis::bbs::SysopSection::Top;
        bool                    soon = false;
        if (k && axis::bbs::renegadeKey(k, sec, soon)) {
            if (!soon) {
                openShell(sec);
                return;
            }
            wfcSoonUntil = millis() + 2500;  // "That one is coming soon." for a moment
            if (!wfcSoonUntil) wfcSoonUntil = 1;
            wfcStale = true;
        }
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
static void runCommand(const String& cmd, Print& out);

// The in-BBS sysop menu's commands: run on the main (so a change is shared like @SET); a follower asks it,
// except for a board-only command (its cluster key, a restart, its card, its relays), which runs right here.
static std::string sysopRun(const std::string& cmd) {
    if (cluster && cluster->role() == axis::cluster::Role::Follower && !axis::bbs::boardOnlyCommand(cmd)) {
        std::string reply;
        return cluster->request("V\x1f" + cmd, reply, 8000) && !reply.empty() ? reply.substr(1) : std::string("axis: the main didn't answer\n");
    }
    String out;
    struct Sink : Print {
        String& s;
        explicit Sink(String& r) : s(r) {}
        size_t write(uint8_t b) override { s += (char)b; return 1; }
    } sink(out);
    runCommand(String(cmd.c_str()), sink);
    return std::string(out.c_str());
}

// Copies of what callers are sent, for the panel's screen wall and console ("SCR <node> <base64>").
static void panelTap(WifiStream* s, const uint8_t* p, size_t n) {
    if (!panelSession || !panelSession->authed() || !panelClient.connected()) return;
    if (!panelWall && s->tapNode != panelConNode) return;
    static uint32_t second = 0, sent = 0;  // at most 32 KB a second to the PC: a slow PC skips frames, callers never wait
    if (millis() - second > 1000) second = millis(), sent = 0;
    if ((sent += n) > 32768) return;
    panelClient.print("SCR " + String(s->tapNode) + " " + base64::encode(p, n) + "\n");
}
void (*WifiStream::tap)(WifiStream*, const uint8_t*, size_t) = panelTap;

static int slotOfNode(int num) {
    for (size_t i = 0; i < sessions.size(); i++)
        if (sessionNumber((int)i) == num) return (int)i;
    return -1;
}

static void panelSay(int slot, const std::string& text) {  // a line to a caller, from the BBS
    WifiStream* l = (int)slot == curSession ? line.get() : sessions[(size_t)slot].line.get();
    if (l) l->write((const uint8_t*)text.data(), text.size()), l->flush();
}

// WALL ON/OFF, CON NEW, CON JOIN <node>, CON OFF, KEY <base64>: the screen wall and console login
static std::string panelExtra(const std::string& in) {
    if (in == "WALL ON" || in == "WALL OFF") {
        panelWall = in == "WALL ON";
        return "WALL " + std::string(panelWall ? "ON" : "OFF") + "\n";
    }
    if (in == "CON OFF") {
        int slot = slotOfNode(panelConNode);
        if (slot >= 0 && panelConJoined) panelSay(slot, "\r\n\x1b[1;33m*** The sysop has left. ***\x1b[0m\r\n");
        else if (slot >= 0) {  // its own session: hang it up
            WifiStream* l = slot == curSession ? line.get() : sessions[(size_t)slot].line.get();
            if (l) l->hangUp();
        }
        if (panelConNode) logEvent(String("panel console off node ") + panelConNode);
        panelConNode = 0;
        return "CON 0\n";
    }
    if (in.rfind("CON JOIN ", 0) == 0) {
        int num = atoi(in.c_str() + 9), slot = slotOfNode(num);
        if (slot < 0 || !sessionLine((size_t)slot)) return "CON NO nobody on that node here\n";
        panelConNode = num, panelConJoined = true;
        panelSay(slot, "\r\n\x1b[1;33m*** The sysop has broken in. ***\x1b[0m\r\n");
        logEvent(String("panel: sysop broke in on node ") + num);
        return "CON " + std::to_string(num) + "\n";
    }
    if (in == "CON NEW") {
        int free = -1;
        for (size_t i = 0; i < sessions.size() && free < 0; i++)
            if (!sessionLine(i) && cfg.setupDone && mode == Mode::Normal) free = (int)i;
        if (free < 0) return "CON NO every node on this board is busy\n";
        if (curSession >= 0) unbindSession();
        panelConNode = sessionNumber(free), panelConJoined = false;  // (before its first screen goes out)
        panelConsoleNew = true;
        bindSession(free);
        answerIn(WiFiClient());
        panelConsoleNew = false;
        unbindSession();
        panelConNode = sessionNumber(free), panelConJoined = false;
        return "CON " + std::to_string(panelConNode) + "\n";
    }
    if (in.rfind("KEY ", 0) == 0 && panelConNode) {
        int slot = slotOfNode(panelConNode);
        WifiStream* l = slot < 0 ? nullptr : slot == curSession ? line.get() : sessions[(size_t)slot].line.get();
        if (!l) return "CON 0\n";
        String  b64 = String(in.c_str() + 4);
        size_t  len = 0;
        uint8_t buf[256];
        if (mbedtls_base64_decode(buf, sizeof buf, &len, (const uint8_t*)b64.c_str(), b64.length()) == 0)
            l->inject(std::string((const char*)buf, len));
        return "";
    }
    return "?\n";
}

// The control panel's connection: login, commands through runCommand, STAT every 5 s.
static void pumpPanel() {
    if (panelServer.hasClient()) {
        WiFiClient c = panelServer.available();
        if (!loginGuard.allowed((uint32_t)c.remoteIP(), millis())) {  // the panel port counts too
            c.print("BYE locked\n");
            c.stop();
            return;
        }
        {  // one panel at a time: a new one replaces the old (a restarted app's socket can look alive for minutes)
            if (panelClient.connected()) {
                if (panelConNode) panelExtra("CON OFF");
                panelClient.stop();
            }
            panelClient = c;
            char nonce[17];
            snprintf(nonce, sizeof nonce, "%08lx%08lx", (unsigned long)hwRandom(), (unsigned long)hwRandom());
            panelSession.reset(new axis::bbs::PanelSession(cfg, users, nonce));
            panelSession->onEvent = [](const std::string& e) { logEvent(String(e.c_str())); };
            panelSession->extra = panelExtra;
            panelWall = false;
            panelConNode = 0;
            panelSession->run = [](const std::string& c) {
                String out;
                struct Sink : Print {
                    String& s;
                    explicit Sink(String& r) : s(r) {}
                    size_t write(uint8_t b) override { s += (char)b; return 1; }
                } sink(out);
                runCommand(String(c.c_str()), sink);
                return std::string(out.c_str());
            };
            panelLine = "";
            panelClient.print(panelSession->greeting().c_str());
        }
    }
    if (!panelSession) return;
    if (!panelClient.connected()) {
        if (panelConNode) panelExtra("CON OFF");  // the panel went away: its console goes too
        panelSession.reset();
        return;
    }
    while (panelClient.available()) {
        char ch = (char)panelClient.read();
        if (ch == '\r') continue;
        if (ch != '\n') {
            if (panelLine.length() < 400) panelLine += ch;
            continue;
        }
        if (panelSession->authed() && panelLine.startsWith("CMD ")) {  // straight to the socket: no copy of a long
            runCommand(panelLine.substring(4), panelClient);           // reply in the main's small heap
            panelClient.print("END\n");
            panelLine = "";
            continue;
        }
        std::string reply = panelSession->line(panelLine.c_str());
        panelLine = "";
        if (!reply.empty()) panelClient.write((const uint8_t*)reply.data(), reply.size());
        if (panelSession->closed()) {  // three bad panel logins
            loginGuard.failed((uint32_t)panelClient.remoteIP(), millis());
            panelClient.stop();
            panelSession.reset();
            return;
        }
    }
    if (panelSession->authed() && millis() - panelStatMs > 5000) {
        panelStatMs = millis();
        panelClient.print("STAT ");
        runCommand("@STATUS", panelClient);  // (one line, ends with its own newline)
    }
}

static void runCommand(const String& cmd, Print& out) {
    if (cmd.startsWith("@TAP ")) {
        int sp = cmd.indexOf(' ', 5);
        tapX = cmd.substring(5, sp).toInt();
        tapY = cmd.substring(sp + 1).toInt();
    } else if (cmd.startsWith("@LKEY ")) {  // bench: keys into the local console ("\e" Esc, "\r" Enter, "\\" backslash)
        if (!localNode) {
            out.println("axis: no local console open");
        } else {
            std::string k;
            for (int i = 6; i < (int)cmd.length(); i++) {
                char c = cmd[i];
                if (c == '\\' && i + 1 < (int)cmd.length()) {
                    char n = cmd[++i];
                    c = n == 'e' ? '\x1b' : n == 'r' ? '\r' : n;
                }
                k += c;
            }
            localNode->input((const uint8_t*)k.data(), k.size(), millis());
            localFeed();
            out.println("axis: done");
        }
    } else if (cmd == "@LSCREEN") {  // bench: the local console's 80x25 screen as text, one line per row
        if (!localTerm) {
            out.println("axis: no local console open");
        } else {
            for (int r = 0; r < localTerm->rows(); r++) {
                char row[axis::bbs::AnsiTerm::kMaxCols + 1];
                for (int c = 0; c < localTerm->cols(); c++) row[c] = (char)localTerm->cell(r, c).ch;
                row[localTerm->cols()] = 0;
                out.printf("%02d|%s\n", r + 1, row);
            }
        }
    } else if (cmd == "@DOORSTATE") {  // the 8086 runtime is dosemu/ and is not in this firmware
        out.println("axis: no 8086 in this firmware");
    } else if (cmd == "@NODES") {  // the cluster as this board sees it (bench)
        if (!cluster) out.println("axis: no cluster");
        else
            for (auto& n : cluster->nodes())
                out.printf("axis: node %d ip %s handle '%s' activity '%s'%s\n", n.number, IPAddress(n.ip).toString().c_str(),
                              n.handle.c_str(), n.activity.c_str(), n.number == cluster->nodeNumber() ? " (this board)" : "");
    } else if (cmd == "@MODEM" || cmd == "@MODEM scan") {  // LFD3K-SM radio callers now, the last 10, the modems on the LAN
        if (cmd == "@MODEM scan" || (!modemSearch && millis() - modemSearchMs > 60000u) || !modemSearchMs) modemSearchStart();
        for (size_t i = 0; i < sessions.size(); i++) {
            axis::bbs::Node* n = sessionNode(i);
            if (!n || !n->viaModem()) continue;
            std::string who = n->handle();
            for (char& c : who)
                if (c == '|') c = ' ';
            out.printf("now|%d|%s|%s|%s|%s\n", sessionNumber((int)i), who.c_str(), axis::net::linkLabel(n->link()).c_str(),
                       n->link().bps >= 0 ? std::to_string(n->link().bps).c_str() : "", n->linkTiers().c_str());
        }
        for (auto& c : radioCalls)
            out.printf("call|%s|%s|%u|%u|%d\n", c.who.c_str(), c.tiers.c_str(), (unsigned)c.secs, (unsigned)c.bytes, (int)c.retrains);
        for (auto& m : foundModems) out.printf("found|%s|%s\n", m.name.c_str(), m.ip.c_str());
        if (modemSearch) out.println("search|on");
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
    } else if (cmd.startsWith("@KICK ") || cmd.startsWith("@ADDTIME ") || cmd.startsWith("@FORCETC ") || cmd.startsWith("@NOTE ")) {
        // a node on this board ("@KICK 3", "@ADDTIME 3 15", "@FORCETC 3", "@NOTE 3 text"); the panel sends
        // each to the board holding that node
        String rest = cmd.substring(cmd.indexOf(' ') + 1);
        int    sp = rest.indexOf(' '), num = rest.toInt(), slot = -1;
        String arg = sp < 0 ? String() : rest.substring(sp + 1);
        for (size_t i = 0; i < sessions.size(); i++)
            if (sessionNumber((int)i) == num) slot = (int)i;
        axis::bbs::Node* n = slot >= 0 ? sessionNode((size_t)slot) : nullptr;
        if (!n || !sessionLine((size_t)slot)) out.printf("axis: nobody on node %d here\n", num);
        else if (cmd.startsWith("@KICK ")) {
            logEvent(String("sysop: kicked ") + n->handle().c_str());
            sessionLine((size_t)slot)->hangUp();
            out.printf("axis: kicked node %d\n", num);
        } else if (cmd.startsWith("@ADDTIME ")) {
            n->addMinutes(arg.toInt());
            logEvent(String("sysop: ") + arg + " more minutes for " + n->handle().c_str());
            out.printf("axis: node %d gets %d more minutes\n", num, (int)arg.toInt());
        } else if (cmd.startsWith("@FORCETC ")) {
            n->nodeIn(axis::bbs::Node::kNodeForce, 0, cfg.sysopHandle, "");
            out.printf("axis: node %d sent to the teleconference\n", num);
        } else {
            n->nodeIn(axis::bbs::Node::kNodeNote, 0, cfg.sysopHandle, arg.c_str());
            out.printf("axis: note sent to node %d\n", num);
        }
    } else if (cmd.startsWith("@BCAST ")) {  // "@BCAST text": a broadcast to every caller on this board
        for (size_t i = 0; i < sessions.size(); i++)
            if (sessionNode(i) && sessionLine(i)) sessionNode(i)->nodeIn(axis::bbs::Node::kNodeBroadcast, 0, cfg.sysopHandle, cmd.substring(7).c_str());
        out.println("axis: broadcast sent");
    } else if (cmd == "@USERS") {  // one line per user: id|handle|status|security|calls|flags
        for (auto& u : users.all())
            out.printf("user %d|%s|%s|%d|%u|%s%s%s%s%s\n", u.id, u.handle.c_str(),
                       u.status == axis::bbs::User::Status::Pending ? "pending" : u.status == axis::bbs::User::Status::Denied ? "denied" : "valid",
                       u.security, (unsigned)u.calls, u.permFiles ? "F" : "-", u.permMessages ? "M" : "-", u.permDoors ? "D" : "-",
                       u.freeDownloads ? "$" : "-", u.twit ? "T" : "-");
    } else if (cmd.startsWith("@USER ")) {  // a user's details and application answers
        axis::bbs::User* u = users.find(cmd.substring(6).c_str());
        if (!u) out.println("axis: no user by that handle");
        else {
            out.printf("handle %s\nreal %s\nfrom %s\nemail %s\ntagline %s\nsecurity %d\ncalls %u\nup %u\ndown %u\n", u->handle.c_str(),
                       u->realName.c_str(), u->location.c_str(), u->email.c_str(), u->tagline.c_str(), u->security, (unsigned)u->calls,
                       (unsigned)u->uploads, (unsigned)u->downloads);
            for (auto& a : u->answers) out.printf("answer %s|%s\n", a.first.c_str(), a.second.c_str());
        }
    } else if (cmd.startsWith("@VALIDATE ") || cmd.startsWith("@DENY ") || cmd.startsWith("@SEC ") || cmd.startsWith("@PERM ") ||
               cmd.startsWith("@TWIT ")) {
        // "@VALIDATE h", "@DENY h", "@SEC h 30", "@PERM h files|msgs|doors|freedl on|off", "@TWIT h on|off"
        String rest = cmd.substring(cmd.indexOf(' ') + 1), handle = rest, a1, a2;
        int    sp = rest.indexOf(' ');
        if (sp > 0 && !cmd.startsWith("@VALIDATE ") && !cmd.startsWith("@DENY ")) {
            int last = rest.lastIndexOf(' ');
            if (cmd.startsWith("@PERM ")) {
                int mid = rest.lastIndexOf(' ', last - 1);
                handle = rest.substring(0, mid), a1 = rest.substring(mid + 1, last), a2 = rest.substring(last + 1);
            } else {
                handle = rest.substring(0, last), a1 = rest.substring(last + 1);
            }
        }
        axis::bbs::User* u = users.find(handle.c_str());
        if (!u) out.println("axis: no user by that handle");
        else {
            bool on = a2 == "on" || a1 == "on";
            if (cmd.startsWith("@VALIDATE ")) {
                u->status = axis::bbs::User::Status::Validated;
                writeWarrior(u->handle.c_str());
            }
            else if (cmd.startsWith("@DENY ")) u->status = axis::bbs::User::Status::Denied;
            else if (cmd.startsWith("@SEC ")) u->security = constrain((int)a1.toInt(), 1, 255);
            else if (cmd.startsWith("@TWIT ")) u->twit = on;
            else if (a1 == "files") u->permFiles = on;
            else if (a1 == "msgs") u->permMessages = on;
            else if (a1 == "doors") u->permDoors = on;
            else if (a1 == "freedl") u->freeDownloads = on;
            saveUsers();
            logEvent(String("sysop: ") + cmd.substring(1, cmd.indexOf(' ')) + " " + u->handle.c_str());
            out.printf("axis: %s updated\n", u->handle.c_str());
        }
    } else if (cmd == "@SETTINGS") {  // axis.ini without any salt or hash line
        std::string t = axis::bbs::formatConfig(cfg), line;
        for (char ch : t) {
            if (ch != '\n') {
                line += ch;
                continue;
            }
            if (line.find("_salt=") == std::string::npos && line.find("_hash=") == std::string::npos) out.println(line.c_str());
            line.clear();
        }
    } else if (cmd.startsWith("@VOTEDEL ")) {  // "@VOTEDEL n": booth question n (1-based, as listed) and its ballots
        if (asFollower()) {
            out.println("@VOTEDEL the booth lives on the main: run it there");
        } else {
            std::vector<std::string> v = listLoad("votes"), b = listLoad("ballots");
            int                      q = cmd.substring(9).toInt();
            bool                     ok = axis::bbs::voteDelete(v, b, q);
            if (ok) {
                listStore("votes", v);
                listStore("ballots", b);
                logEvent("voting booth: question " + String(q) + " taken out");
            }
            out.println(ok ? "@VOTEDEL OK" : "@VOTEDEL no such question");
        }
    } else if (cmd == "@RUMORS" || cmd == "@BBSLIST" || cmd == "@VOTES" || cmd == "@LINERS") {
        std::vector<std::string> v = cmd == "@LINERS" ? oneLiners : sharedList(cmd == "@RUMORS" ? "rumors" : cmd == "@BBSLIST" ? "bbslist" : "votes");
        for (auto& l : v) out.println(l.c_str());
    } else if ((cmd == "@STATS" || cmd.startsWith("@STATS ")) && !statsOwner()) {
        // a follower: the main owns the counters; this is the main's last word plus what is still to be sent up
        if (cmd.length() > 7) {
            out.println("@STATS total is kept on the main: run @STATS total=<n> there");
        } else {
            axis::bbs::DayStats s = statsShown();
            const auto&         p = statsShare.pending();
            const auto&         o = statsShare.outstanding();
            out.printf("@STATS %s day=%d calls=%u posts=%u uploads=%u total=%u unsent=%u/%u/%u\n",
                       statsShare.known() ? "from the main:" : "main not heard yet, this board's unsent only:", s.day, (unsigned)s.calls,
                       (unsigned)s.posts, (unsigned)s.uploads, (unsigned)s.total, (unsigned)(p.calls + o.calls), (unsigned)(p.posts + o.posts),
                       (unsigned)(p.uploads + o.uploads));
        }
    } else if (cmd == "@STATS" || cmd.startsWith("@STATS ")) {
        // this board's WFC counters: "@STATS" lists them, "@STATS total=<n>" sets the lifetime call total
        uint32_t n = 0;
        if (cmd.length() > 7 && !(cmd.substring(7).startsWith("total=") && axis::bbs::parseStatTotal(cmd.substring(13).c_str(), n))) {
            out.println("@STATS use @STATS or @STATS total=<n> (n from 0 to 4294967295)");
        } else {
            if (cmd.length() > 7) {
                dayStats.total = n;
                statsSave();
                screenDirty = true;
                logEvent("sysop: lifetime calls set to " + String(n));
            }
            axis::bbs::DayStats s = statsShown();  // (plus any counts left from when this board was a follower)
            out.printf("@STATS day=%d calls=%u posts=%u uploads=%u total=%u\n", s.day, (unsigned)s.calls, (unsigned)s.posts,
                       (unsigned)s.uploads, (unsigned)s.total);
        }
    } else if (cmd == "@HEAPWIN") {  // restart the lowest-heap window (the numbers on @STATUS: wmin wlarge wdoing)
        heapWin.reset();
        out.println("@HEAPWIN reset");
    } else if (cmd == "@STATUS") {  // one line for the panel
        int on = 0;
        for (size_t i = 0; i < sessions.size(); i++) on += sessionLine(i) ? 1 : 0;
        axis::bbs::DiagSample sample = boardDiag_;
        sample.heapFree = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_8BIT);
        sample.largest = (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
        sample.minHeap = (uint32_t)heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT);
        sample.stackWords = (uint32_t)uxTaskGetStackHighWaterMark(nullptr);
        sample.winFree = heapWin.lowFree == UINT32_MAX ? sample.heapFree : heapWin.lowFree;
        sample.winLargest = heapWin.lowLargest == UINT32_MAX ? sample.largest : heapWin.lowLargest;
        sample.winDoing = heapWin.lowDoing;
        auto stk = [](const char* n) { TaskHandle_t h = xTaskGetHandle(n); return h ? (uint32_t)uxTaskGetStackHighWaterMark(h) : 0u; };
        sample.stkWeb = stk("web");
        sample.stkMdns = stk("mdns");
        sample.stkTimer = stk("esp_timer");
        sample.outMax = (uint32_t)axis::bbs::Node::outMax();
        sample.artMax = (uint32_t)artMax;
        char line[384];
        std::snprintf(line, sizeof line, "id=%s role=%s nodes=%d on=%d heap=%u largest=%u up=%lu rssi=%d sd_mb=%u ip=%s",
                      WiFi.macAddress().c_str(),
                      !cluster ? "solo" : cluster->role() == axis::cluster::Role::Main ? "main" : "follower", (int)sessions.size(), on,
                      sample.heapFree, sample.largest, (unsigned long)(millis() / 1000), (int)WiFi.RSSI(),
                      (unsigned)(storageFreeBytes() >> 20), WiFi.localIP().toString().c_str());
        out.print(line);
        out.print(axis::bbs::diagFields(sample).c_str());
        out.print('\n');
    } else if (cmd.startsWith("@UEDIT ")) {  // "@UEDIT handle|field|value": realname location email tagline security handle
        String a = cmd.substring(7);
        int    b1 = a.indexOf('|'), b2 = b1 < 0 ? -1 : a.indexOf('|', b1 + 1);
        axis::bbs::User* u = b2 > 0 ? users.find(a.substring(0, b1).c_str()) : nullptr;
        String f = b2 > 0 ? a.substring(b1 + 1, b2) : String(), v = b2 > 0 ? a.substring(b2 + 1) : String();
        if (!u) out.println("axis: @UEDIT handle|field|value");
        else {
            if (f == "realname") u->realName = v.c_str();
            else if (f == "location") u->location = v.c_str();
            else if (f == "email") u->email = v.c_str();
            else if (f == "tagline") u->tagline = v.c_str();
            else if (f == "security") u->security = constrain((int)v.toInt(), 1, 255);
            else if (f == "handle" && v.length() && !users.find(v.c_str())) u->handle = v.c_str();
            else {
                out.println("axis: fields: realname location email tagline security handle");
                return;
            }
            saveUsers();
            logEvent(String("sysop: edited ") + f + " of " + u->handle.c_str());
            out.printf("axis: %s updated\n", u->handle.c_str());
        }
    } else if (cmd.startsWith("@UDEL ")) {  // "@UDEL handle": delete a caller's account
        axis::bbs::User* u = users.find(cmd.substring(6).c_str());
        if (!u) out.println("axis: no user by that handle");
        else {
            std::string h = u->handle;
            auto& all = users.all();
            for (size_t i = 0; i < all.size(); i++)
                if (&all[i] == u) {
                    all.erase(all.begin() + (long)i);
                    break;
                }
            saveUsers();
            logEvent(String("sysop: deleted user ") + h.c_str());
            out.printf("axis: %s deleted\n", h.c_str());
        }
    } else if (cmd == "@AREAS" || cmd == "@MSGAREAS") {  // "N|id|name|level|m" per area
        auto& v = cmd == "@AREAS" ? cfg.areas : cfg.msgAreas;
        for (size_t i = 0; i < v.size(); i++)
            out.printf("%u|%s|%s|%d|%s\n", (unsigned)i + 1, v[i].id.c_str(), v[i].name.c_str(), v[i].minSecurity, v[i].mirror ? "m" : "");
    } else if (cmd.startsWith("@AREASET ") || cmd.startsWith("@AREADEL ") || cmd.startsWith("@AREAUP ")) {
        // "@AREASET files|N|id|name|level" (N past the end adds), "@AREADEL msgs|N", "@AREAUP files|N" (moves it up one)
        String a = cmd.substring(cmd.indexOf(' ') + 1);
        std::vector<String> f;
        for (int from = 0;;) {
            int bar = a.indexOf('|', from);
            f.push_back(bar < 0 ? a.substring(from) : a.substring(from, bar));
            if (bar < 0) break;
            from = bar + 1;
        }
        auto& v = f[0] == "msgs" ? cfg.msgAreas : cfg.areas;
        int   n = f.size() > 1 ? (int)f[1].toInt() : 0;
        bool  ok = n >= 1;
        if (ok && cmd.startsWith("@AREASET ") && f.size() >= 4) {
            axis::bbs::FileArea ar{f[2].c_str(), f[3].c_str(), f.size() > 4 ? (int)f[4].toInt() : 0};
            if (n <= (int)v.size()) ar.mirror = v[(size_t)n - 1].mirror, v[(size_t)n - 1] = ar;
            else v.push_back(ar);
        } else if (ok && cmd.startsWith("@AREADEL ") && n <= (int)v.size()) {
            v.erase(v.begin() + n - 1);
        } else if (ok && cmd.startsWith("@AREAUP ") && n >= 2 && n <= (int)v.size()) {
            std::swap(v[(size_t)n - 1], v[(size_t)n - 2]);
        } else ok = false;
        if (!ok) out.println("axis: @AREASET files|N|id|name|level, @AREADEL msgs|N, @AREAUP files|N");
        else {
            configSave(cfg);
            if (cluster) cluster->shareSettings();
            makeAreaFolders();
            logEvent(String("sysop: ") + cmd.substring(1, cmd.indexOf(' ')) + " " + a);
            out.println("axis: areas saved");
        }
    } else if (cmd.startsWith("@FILES ")) {  // "@FILES area": name|size|description per file
        for (auto& e : callerFiles().list(cmd.substring(7).c_str())) out.printf("%s|%u|%s\n", e.name.c_str(), (unsigned)e.size, e.desc.c_str());
    } else if (cmd.startsWith("@FDESC ") || cmd.startsWith("@FREN ") || cmd.startsWith("@FDEL ") || cmd.startsWith("@FMV ")) {
        // "@FDESC area|name|text", "@FREN area|old|new", "@FDEL area|name", "@FMV area|name|toarea"
        String a = cmd.substring(cmd.indexOf(' ') + 1);
        int    b1 = a.indexOf('|'), b2 = b1 < 0 ? -1 : a.indexOf('|', b1 + 1);
        std::string area = b1 > 0 ? a.substring(0, b1).c_str() : "", name = b1 > 0 ? (b2 > 0 ? a.substring(b1 + 1, b2) : a.substring(b1 + 1)).c_str() : "",
                    third = b2 > 0 ? a.substring(b2 + 1).c_str() : "";
        bool ok = false;
        if (cmd.startsWith("@FDESC ")) ok = callerFiles().describe(area, name, third);
        else if (cmd.startsWith("@FREN ")) ok = !third.empty() && callerFiles().rename(area, name, third);
        else if (cmd.startsWith("@FDEL ")) ok = callerFiles().remove(area, name);
        else if (storageHasSd() && axis::bbs::safeFileName(area) && axis::bbs::safeFileName(name) && axis::bbs::safeFileName(third)) {  // a move: same card, another folder
            ok = ::rename(("/sd/files/" + area + "/" + name).c_str(), ("/sd/files/" + third + "/" + name).c_str()) == 0;
            if (ok) {  // carry its description over: scan the old FILES.BBS a line at a time (a big area's list costs the main too much RAM)
                File   fb = SD.open(("/files/" + area + "/FILES.BBS").c_str());
                String desc;
                while (fb && fb.available() && desc.isEmpty()) {
                    String l = fb.readStringUntil('\n');
                    int    sp = l.indexOf(' ');
                    if (sp > 0 && l.substring(0, sp).equalsIgnoreCase(name.c_str())) {
                        desc = l.substring(sp);
                        desc.trim();
                    }
                }
                if (fb) fb.close();
                if (!desc.isEmpty()) callerFiles().describe(third, name, desc.c_str());
            }
        }
        logEvent(String("sysop: ") + cmd.substring(1, cmd.indexOf(' ')) + " " + area.c_str() + " " + name.c_str() + (ok ? "" : " (failed)"));
        out.println(ok ? "axis: done" : "axis: that didn't work");
    } else if (cmd.startsWith("@POSTS ")) {  // "@POSTS area": num|from|subject|date per post (the newest 200)
        for (auto& m : callerMessages().headers(cmd.substring(7).c_str()))
            out.printf("%d|%s|%s|%s\n", m.num, m.from.c_str(), m.subject.c_str(), m.date.c_str());
    } else if (cmd.startsWith("@POSTDEL ") || cmd.startsWith("@POSTMV ") || cmd.startsWith("@PURGE ")) {
        // "@POSTDEL area|num", "@POSTMV area|num|toarea", "@PURGE area|days" (posts older than that)
        String a = cmd.substring(cmd.indexOf(' ') + 1);
        int    b1 = a.indexOf('|'), b2 = b1 < 0 ? -1 : a.indexOf('|', b1 + 1);
        std::string area = b1 > 0 ? a.substring(0, b1).c_str() : "";
        int         n = b1 > 0 ? (int)(b2 > 0 ? a.substring(b1 + 1, b2) : a.substring(b1 + 1)).toInt() : 0;
        std::string to = b2 > 0 ? a.substring(b2 + 1).c_str() : "";
        int         gone = 0;
        if (cmd.startsWith("@POSTMV ")) {
            axis::bbs::Message m;
            if (!to.empty() && callerMessages().read(area, n, m) && callerMessages().post(to, m))
                gone = callerMessages().drop(area, [n](const axis::bbs::Message& x) { return x.num == n; });
        } else if (cmd.startsWith("@POSTDEL ")) {
            gone = callerMessages().drop(area, [n](const axis::bbs::Message& x) { return x.num == n; });
        } else {
            time_t cut = time(nullptr) - (time_t)n * 86400;
            char   stamp[16];
            strftime(stamp, sizeof stamp, "%Y-%m-%d", localtime(&cut));
            std::string before = stamp;
            if (time(nullptr) > 1700000000 && n > 0)
                gone = callerMessages().drop(area, [&](const axis::bbs::Message& x) { return x.date.substr(0, 10) < before; });
        }
        logEvent(String("sysop: ") + cmd.substring(1, cmd.indexOf(' ')) + " " + a + " (" + gone + ")");
        out.printf("axis: %d post%s\n", gone, gone == 1 ? "" : "s");
    } else if (cmd == "@MENU") {  // the main menu's items: N|key|name|command (empty: the built-in menu)
        for (size_t i = 0; i < cfg.mainMenu.size(); i++)
            out.printf("%u|%s|%s|%s\n", (unsigned)i + 1, cfg.mainMenu[i].key.c_str(), cfg.mainMenu[i].name.c_str(), cfg.mainMenu[i].cmd.c_str());
    } else if (cmd == "@MENUCMDS") {  // what a menu item can run
        for (auto& c : axis::bbs::menuCommands()) out.printf("%s|%c\n", c.first.c_str(), c.second);
    } else if (cmd.startsWith("@MENUSET ") || cmd.startsWith("@MENUDEL ") || cmd.startsWith("@MENUUP ") || cmd == "@MENURESET" || cmd == "@MENUSTART") {
        // "@MENUSET N|key|name|command" (N past the end adds), "@MENUDEL N", "@MENUUP N", "@MENURESET" (built-in again),
        // "@MENUSTART" (an editable copy of the built-in menu)
        auto&  v = cfg.mainMenu;
        String a = cmd.indexOf(' ') > 0 ? cmd.substring(cmd.indexOf(' ') + 1) : String();
        int    n = (int)a.toInt();
        bool   ok = true;
        if (cmd == "@MENURESET") v.clear();
        else if (cmd == "@MENUSTART") {
            static const char* const names[] = {"Message Bases", "File Areas", "Doors", "Teleconference", "Who's Online", "Page Sysop",
                                                "Your Account", "Oneliners", "MultiNode", "Last Callers", "Private Email", "Scan New Msgs",
                                                "Rumors", "BBS List", "Top Ten", "Voting Booth", "System Info", "Credits", "Goodbye", "Sysop Menu"};
            v.clear();
            size_t i = 0;
            for (auto& c : axis::bbs::menuCommands()) v.push_back({std::string(1, c.second), names[i++ % 20], c.first});
        } else if (cmd.startsWith("@MENUSET ")) {
            int b1 = a.indexOf('|'), b2 = b1 < 0 ? -1 : a.indexOf('|', b1 + 1), b3 = b2 < 0 ? -1 : a.indexOf('|', b2 + 1);
            ok = n >= 1 && b3 > 0 && axis::bbs::menuCommandKey(a.substring(b3 + 1).c_str());
            if (ok) {
                axis::bbs::MenuItem m{a.substring(b1 + 1, b2).substring(0, 1).c_str(), a.substring(b2 + 1, b3).c_str(), a.substring(b3 + 1).c_str()};
                if (n <= (int)v.size()) v[(size_t)n - 1] = m;
                else v.push_back(m);
            }
        } else if (cmd.startsWith("@MENUDEL ")) {
            ok = n >= 1 && n <= (int)v.size();
            if (ok) v.erase(v.begin() + n - 1);
        } else {
            ok = n >= 2 && n <= (int)v.size();
            if (ok) std::swap(v[(size_t)n - 1], v[(size_t)n - 2]);
        }
        if (!ok) out.println("axis: @MENUSET N|key|name|command, @MENUDEL N, @MENUUP N, @MENURESET, @MENUSTART");
        else {
            configSave(cfg);
            if (cluster) cluster->shareSettings();
            logEvent(String("sysop: ") + cmd.substring(1));
            out.println("axis: menu saved");
        }
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
            writeWarrior(u.handle);
            out.printf("axis: added %s\n", u.handle.c_str());
        }
    } else if (cmd.startsWith("@DISCORD")) {  // "@DISCORD https://discord.com/api/webhooks/...": the channel webhook; alone: off
        out.println(web::setDiscord(cmd.length() > 9 ? cmd.substring(9) : String()) ? "axis: discord webhook saved" : "axis: that isn't a Discord webhook URL");
    } else if (cmd == "@DDNS now") {  // bench: make the web task's DNS check due on its next turn
        web::ddnsSoon();
        out.println("axis: ddns check due");
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
    } else if (cmd == "@LED") {  // status light (bench): blue LED on the ring, RGB on a CYD
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
    } else if (cmd == "@SDFORMAT FORMAT" || (cmd == "@SDFORMAT FORMAT force" && &out == &Serial)) {
        // the sysop menu's Format SD card (it asks for FORMAT to be typed first); "force", from the serial console only,
        // formats with callers on
        String result;
        bool   ok = false;
        if (!sdFormatNow(result, ok, cmd.endsWith(" force"))) {
            out.println("axis: sd format refused, a caller is online (serial: @SDFORMAT FORMAT force)");
        } else {
            out.printf("axis: sd format %s: %s\n", ok ? "ok" : "FAILED", result.c_str());
            screenDirty = true;
        }
    } else if (cmd == "@STATION") {  // the sysop menu's Station layer: "sensor|<line>" and "relay|n|0/1"
        for (const char* n : {"temp", "water", "light", "humid"}) out.printf("sensor|%s\n", axis::bbs::station::line(n).c_str());
        for (int i = 1; i <= axis::bbs::station::kRelays; i++) out.printf("relay|%d|%d\n", i, axis::bbs::station::relayState(i));
    } else if (cmd.startsWith("@RELAY ")) {  // "@RELAY 2 on"
        int n = cmd.substring(7).toInt();
        if (axis::bbs::station::relay(n, cmd.endsWith(" on") ? 1 : 0)) out.printf("axis: relay %d %s\n", n, cmd.endsWith(" on") ? "on" : "off");
        else out.println("axis: no such relay");
    } else if (cmd == "@BEEP") {
        axis::bbs::station::beep(880, 80);
        out.println("axis: beep");
    } else if (cmd.startsWith("@DOORLS ")) {  // list a door's folder the way the door sees it
        std::string           dir = std::string(storageRoot()) + "/doors/" + cmd.substring(8).c_str();
        axis::PosixHostFs     fs(dir);
        axis::door::FindState st;
        fs.find("*.*", 0x10, st);
        out.printf("axis: %s: %u entries\n", dir.c_str(), (unsigned)st.matches.size());
        for (auto& f : st.matches)
            out.printf("axis:   %-12s %8u%s\n", f.name83.c_str(), (unsigned)f.size, f.attr & 0x10 ? " <DIR>" : "");
    } else if (cmd.startsWith("@DOORCHECK ")) {  // the flash cache belonged to the 8086 runtime, which is not linked
        out.println("axis: no 8086 in this firmware");
    } else if (cmd.startsWith("@SET ") && cmd.indexOf('=') > 5) {  // "@SET location=Myrtle Beach, SC"
        String kv = cmd.substring(5);
        cfg = axis::bbs::parseConfig(axis::bbs::formatConfig(cfg) + kv.c_str() + "\n");  // the later line wins
        pushBoardToCallers();
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
    } else if (cmd == "@HEADLESS" || cmd == "@HEADLESS on" || cmd == "@HEADLESS off") {
        // "@HEADLESS on": this board is a bare ESP32 (no screen, one LED on GPIO 2); "off": a CYD.
        // Per board, in NVS, so it survives every reflash; the board restarts to take it up.
        if (cmd == "@HEADLESS") {
            out.printf("axis: this board is %s (saved: %s)\n", boardIsHeadless() ? "headless" : "a screen board",
                       boardHeadlessSaved() ? "headless" : "screen");
        } else {
            bool on = cmd.endsWith("on"), ok = boardSetHeadless(on);
            logEvent(String("sysop: headless ") + (on ? "on" : "off") + (ok ? "" : " (SAVE FAILED)"));
            out.println(ok ? "axis: saved, restarting" : "axis: headless SAVE FAILED");
            if (ok && on != boardIsHeadless()) {
                delay(300);
                ESP.restart();
            }
        }
    } else if (cmd == "@CLUSTERKEY" || cmd.startsWith("@CLUSTERKEY ")) {
        // "@CLUSTERKEY secret": the key every board of this BBS shares (8+ characters, set the same on
        // each board); "@CLUSTERKEY off" forgets it (the board runs alone); alone, says whether one is set.
        // Per board, in flash: it never goes out with the settings and is never shown or logged.
        String key = cmd.length() > 12 ? cmd.substring(12) : String();
        key.trim();
        Preferences p;
        if (!key.length()) {
            p.begin("clu", true);
            bool set = p.getString("key", "").length() > 0;
            p.end();
            out.println(set ? "axis: cluster key is set" : "axis: no cluster key: this board runs alone");
        } else if (key != "off" && key.length() < 8) {
            out.println("axis: a cluster key needs 8 or more characters");
        } else {
            p.begin("clu", false);
            bool ok = key == "off" ? p.remove("key") : p.putString("key", key) > 0;
            p.end();
            // start over with the new key: drop every link, find the others again. Never here: the cluster does it at
            // the top of its next tick (loop()), so no command can clear its links while tick() is looping over them.
            if (cluster) cluster->restartSoon(key == "off" ? "" : key.c_str());
            logEvent(String("sysop: cluster key ") + (key == "off" ? "cleared" : "set") + (ok ? "" : " (SAVE FAILED)"));
            out.println(ok ? "axis: cluster key saved" : "axis: cluster key SAVE FAILED");
        }
    } else if (cmd == "@AXIS" || cmd.startsWith("@AXIS ")) {  // "@AXIS host:port" adds a board, "@AXIS" lists, "@AXIS probe" asks one
        String rest = cmd.length() > 6 ? cmd.substring(6) : String();
        rest.trim();
        if (rest == "probe") {
            axisProbeSoon = true;
            out.println("axis: axis net will ask the next board");
        } else if (rest.length()) {
            std::string host;
            int         port = 23;
            if (!axis::bbs::axisHostPort(rest.c_str(), host, port)) out.println("axis: axis net wants host or host:port");
            else {
                axis::bbs::AxisCard c;
                c.name = host;
                c.host = host;
                c.port = port;
                c.blurb = "added at the console";
                sharedListAdd("axisnet", axis::bbs::axisLine(c));
                axisProbeSoon = true;
                out.printf("axis: axis net added %s:%d\n", host.c_str(), port);
            }
        } else {
            for (const std::string& l : sharedList("axisnet")) out.println(l.c_str());
            if (sharedList("axisnet").empty()) out.println("axis: axis net is empty");
        }
    } else if (cmd.startsWith("@RUMOR ")) {  // "@RUMOR text": add a rumor to the board's list (the main keeps them)
        sharedListAdd("rumors", cmd.substring(7).c_str());
        out.println("axis: rumor added");
    } else if (cmd.startsWith("@USERPASS ")) {  // "@USERPASS handle secret": the sysop sets a caller's password (4+ characters)
        String rest = cmd.substring(10);
        int    sp = rest.lastIndexOf(' ');
        axis::bbs::User* u = sp > 0 ? users.find(rest.substring(0, sp).c_str()) : nullptr;
        String pw = sp > 0 ? rest.substring(sp + 1) : String();
        if (!u) out.println("axis: @USERPASS handle password");
        else if (pw.length() < 4) out.println("axis: a password needs 4 or more characters");
        else {
            u->salt = axis::bbs::makeSalt(hwRandom);
            u->hash = axis::bbs::hashPassword(u->salt, pw.c_str());
            saveUsers();
            logEvent(String("sysop: password changed for ") + u->handle.c_str());
            out.printf("axis: password set for %s\n", u->handle.c_str());
        }
    } else if (cmd.startsWith("@SYSOPPASS ")) {  // "@SYSOPPASS secret" sets the sysop account's password (4+ characters, as the wizard)
        String pw = cmd.substring(11);
        if (pw.length() < 4) {
            out.println("axis: @SYSOPPASS needs 4 or more characters");
        } else {
            cfg.sysopSalt = axis::bbs::makeSalt(hwRandom);
            cfg.sysopHash = axis::bbs::hashPassword(cfg.sysopSalt, pw.c_str());
            bool ok = configSave(cfg);
            if (cluster) cluster->shareSettings();  // every board takes the same sysop login
            logEvent(String("sysop: sysop password changed") + (ok ? "" : " (SAVE FAILED)"));
        }
    } else if (cmd == "@RESTART" && callersBesideSysop()) {
        out.println("axis: restart refused, a caller is online (serial: @RESTART force)");
    } else if (cmd == "@RESTART" || (cmd == "@RESTART force" && &out == &Serial)) {  // restart this board (the sysop menu's System layer)
        logEvent("sysop: restart");
        out.println("axis: restarting");
        delay(300);
        ESP.restart();
    } else if (cmd == "@WIFIPICK") {  // the touch WiFi picker on demand (bench)
        startPicker(mode == Mode::WifiPick ? afterPick : mode);
    } else if (&out == &Serial && serialFileCommand(cmd)) {  // @PUT @LS @TYPE @RM @MKDIR @BAUD (tools/sdput.py)
    } else if (cmd == "@RERUNSETUP" && boardIsHeadless()) {  // the wizard is on a screen this board hasn't got
        out.println("axis: setup refused: this board is headless (no screen to run setup on)");
    } else if (cmd == "@RERUNSETUP" && callersBesideSysop()) {
        out.println("axis: setup refused, a caller is online");
    } else if (cmd == "@RERUNSETUP") {  // the sysop menu's Rerun setup: the wizard again; nothing changes until it saves
        logEvent("sysop: setup again");
        newSetupPin();
        mode = Mode::SetupChoice;
        screenDirty = true;
        out.println("axis: setup again on the board's screen (the settings stay until it saves)");
    } else if (cmd == "@RESETSETUP") {
        configErase();
        out.println("axis: settings erased, restarting into setup");
        delay(100);
        ESP.restart();
    }
}

// "WFC" on the serial port: a terminal on the waiting-for-caller screen, until Q; then the @ commands are back.
static void serialWfcClose() {
    serialWfc.reset();
    Serial.print("\r\naxis: back to @ commands\r\n");
}
static void serialWfcPump() {
    serialWfc->poll(millis());
    std::string o = serialWfc->takeOutput();
    if (!o.empty()) Serial.write((const uint8_t*)o.data(), o.size());
    if (serialWfc->state() == axis::bbs::Node::State::Done || serialWfc->wantsHangup()) serialWfcClose();
}

// "WFC" (any case) opens it. An "@" as the first character of a line on the WFC screen, or 10 minutes without input, leaves it.
static void handleSerial() {
    static String   cmd;
    static bool     lineStart = true;
    static uint32_t lastIn = 0;
    while (Serial.available()) {
        char c = Serial.read();
        if (serialWfc) {
            lastIn = millis();
            // a tool or script talking @ commands: hand the line back. Only on the WFC screen itself: in the sysop menu an
            // @ is part of an answer (a password, FORMAT ...)
            if (c == '@' && lineStart && serialWfc->state() == axis::bbs::Node::State::Wfc) {
                serialWfcClose();
                cmd = "@";
                continue;
            }
            lineStart = (c == '\r' || c == '\n');
            uint8_t byte = (uint8_t)c;
            serialWfc->input(&byte, 1, millis());
            serialWfcPump();
            continue;
        }
        if ((c == 0x0A || c == 0x0D) && cmd.equalsIgnoreCase("WFC")) {
            cmd = "";
            if (mode != Mode::Normal || !cfg.setupDone) {
                Serial.println(cfg.setupDone ? "axis: WFC is not available right now (the board is not at its normal screen)"
                                             : "axis: WFC is not available until setup is done");
                continue;
            }
            serialWfc.reset(new axis::bbs::Node(cluster && !cluster->numbers().empty() ? cluster->numbers()[0] : 1, sysInfo, cfg, &callerFiles()));
            serialWfc->setAccounts(&users, saveUsers, hwRandom, unixNow);
            serialWfc->setWarrior(writeWarrior);
            serialWfc->onEvent = [](const std::string& e) { logEvent(String(e.c_str())); };
            serialWfc->setSysopRun(sysopRun);
            serialWfc->setWfcSnapshot(wfcSnapshot);
            serialWfc->setMessages(&callerMessages());
            serialWfc->setArt([](const std::string& name) { return themeArt(name); });
            serialWfc->setPoster(posterPiece);
            serialWfc->setMenus([](const std::string& name) { return menuFile(name); }, menuArtPieces);
            wireScripts(serialWfc.get());
            serialWfc->setLists([](const std::string& n) { return sharedList(n); }, [](const std::string& n, const std::string& l) { sharedListAdd(n, l); });
            serialWfc->setLastCallers([] { return sharedList("callers"); }, [](const std::string&) {});
            serialWfc->beginLocal(millis());
            serialWfc->openWfc(millis());
            lineStart = true;
            lastIn = millis();
            serialWfcPump();
            continue;
        }
        if (c != '\n' && c != '\r') {
            if (cmd.length() < 160) cmd += c;
            continue;
        }
        runCommand(cmd, Serial);
        cmd = "";
    }
    if (serialWfc) {
        if (millis() - lastIn > 600000u) serialWfcClose();
        else serialWfcPump();
    }
}

// ------------------------------------------------------------------ main
// Out of memory: say what was asked for and what was left, before the abort (diagnostics).
static void allocFailed(size_t size, uint32_t caps, const char* fn) {
    esp_rom_printf("axis: ALLOC FAILED %u bytes (caps %x) in %s: byte heap %u, largest %u\n", (unsigned)size, (unsigned)caps, fn,
                   (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT), (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
}

void setup() {
    heapScopeHook([](const std::string& l) { logEvent(String(l.c_str())); });
    boardDiag_.resetCode = (int)esp_reset_reason();
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
    bool lordArgs = false;  // stock LORD tails gain NOEMS; a sysop's own arguments stay
    for (auto& d : cfg.doors) lordArgs = axis::bbs::migrateLordArgs(d) || lordArgs;
    if (lordArgs) configSave(cfg);
    users.parse(flashRead("/users.dat"));
    linersLoad();
    callersLoad();
    if (!lastCallers.empty()) {  // the WFC's last caller: the newest saved login ("handle|node|time|location")
        std::string h = lastCallers.back().substr(0, lastCallers.back().find('|'));
        if (!h.empty()) lastCaller = h.c_str();
    }
    statsLoad();
    userSync.changed(users);  // what we have counts as sent: only new changes go over the link
    for (auto& l : eventFile.tail(40)) events.add(l);  // the Logs screen picks up where it left off
    eventFileReady = true;
    logEvent("boot: Axis Micro BBS Firmware " + String(axis::bbs::kAxisVersion) + " (reset=" + axis::bbs::resetToken(boardDiag_.resetCode) + ")");
    {
        int r = boardDiag_.resetCode;  // panic, interrupt/task/other watchdog, brownout: say what was running then
        if ((r == 4 || r == 5 || r == 6 || r == 7 || r == 9) && doingMagic_ == kDoingMagic) {
            doing_[sizeof doing_ - 1] = 0;
            logEvent(String("CRASH RESET (") + axis::bbs::resetToken(r) + ") while: " + doing_);
        }
        noteDoing("idle");
    }
    pendingSeen = users.pending();
    makeAreaFolders();
    if (!cfg.setupDone) {
        if (boardIsHeadless()) {  // nothing to set up on the ring: the main's settings arrive over the link
            String mac = WiFi.macAddress();
            mac.replace(":", "");
            cfg.boardName = ("axis-" + mac.substring(8)).c_str();
            cfg.sessions = 3;  // no screen to draw: room for a third caller
            newSetupPin();     // (only used if a caller arrives before the settings do)
        } else {
            mode = Mode::SetupChoice;
            newSetupPin();
        }
    }
    Serial.printf("axis: settings %s\n", cfg.setupDone ? "loaded" : "not set up yet, first-boot setup");
    if (!wifiJoinSaved()) {
        if (boardIsHeadless()) {
            startHotspot();
        } else {
            Serial.println("axis: no saved WiFi network in range: touch picker");
            startPicker(mode);
            drawScreen();
        }
        return;
    }
    Serial.printf("axis: wifi %s ip %s\n", wifiSsid(), WiFi.localIP().toString().c_str());
    applyNetwork();
    Serial.printf("axis: time %s\n", timeText().c_str());
    startCluster();
    drawScreen();
}

// This board's status light. Callers here, or a door on this board. See ledsignal.h.
static void ledUpdate() {
    static uint32_t last = 0;
    if (millis() - last < 20 && !ledReport) return;
    last = millis();
    axis::bbs::LedInputs in;
    for (size_t i = 0; i < sessions.size(); i++)
        if (axis::bbs::Node* n = sessionNode(i)) {
            in.nodes += !n->handle().empty();
            in.door = in.door || n->state() == axis::bbs::Node::State::InDoor;
        }
    in.door = in.door || boardHasViewer() || boardHasLord();
    uint32_t now = millis();
    uint8_t bare = axis::bbs::ledDuty(in, now);
    if (!boardIsHeadless()) {
        static bool pwm = false;
        if (!pwm) {
            ledCydAttach();
            pwm = true;
        }
        axis::bbs::LedColour c = axis::bbs::ledColour(in, now);
        uint8_t level = axis::bbs::ledCydDuty(bare);
        uint8_t r = (uint8_t)((uint16_t)level * c.r / 255);
        uint8_t g = (uint8_t)((uint16_t)level * c.g / 255);
        uint8_t b = (uint8_t)((uint16_t)level * c.b / 255);
        ledcWrite(0, 255 - r);  // active low
        ledcWrite(1, 255 - g);
        ledcWrite(2, 255 - b);
        if (ledReport) {
            ledReport = false;
            Serial.printf("axis: led nodes %d door %d duty %u rgb %u %u %u\n", in.nodes, in.door ? 1 : 0,
                          (unsigned)level, r, g, b);
        }
        return;
    }
    if (ledReport) {
        ledReport = false;
        Serial.printf("axis: led nodes %d door %d duty %u\n", in.nodes, in.door ? 1 : 0, (unsigned)bare);
    }
    ledcWrite(0, bare);
}

// The LFD3K-SM modem's newest link report for this caller, into its node (before any door, movie or transfer takes the
// turn, so a retrain during one still counts).
static uint32_t lastRadioMs = 0;
static void takeLinkReport() {
    axis::net::LinkInfo li;
    if (line && node && cfg.modemLink && line->takeLink(li)) {  // (at most one a second per line: TelnetStream::takeLink)
        bool first = !node->viaModem();
        node->setLink(li);
        // the WFC's |R? codes: this caller's link when it is the one shown, a new call, a new tier, or the shown one is a minute old
        // (two radio callers do not take turns at every report)
        if (first || node->handle() == lastRadioWho || li.tier != lastRadio.tier || millis() - lastRadioMs > 60000u) {
            lastRadio = li;
            lastRadioWho = node->handle();
            lastRadioMs = millis();
        }
        if (first)
            logEvent(String("radio caller on node ") + String(sessionNumber(curSession)) + ": " + axis::net::linkLabel(li).c_str() +
                     (li.modem.empty() ? "" : (String(" via ") + li.modem.c_str()).c_str()));
    }
    if (node && node->linkRedrawDue(millis())) screenDirty = true;  // the tier changed (at most every 5 s), not every report
}

// One session's turn: its caller, and its own movie or transfer when it has one.
static void serviceSession(int i) {
    takeLinkReport();
    if (cluster) {
        std::string handle = node ? node->handle() : "", activity;
        if (node) {
            using S = axis::bbs::Node::State;
            S st = node->state();
            activity = (nativeLord || viewer || st == S::Script) ? "Doors" : job ? "Transfer" : st == S::Files ? "Files"
                     : st == S::Applying ? "Applying" : st == S::Chat ? "Chat" : st == S::Messages ? "Messages" : st == S::Menu ? "Menu"
                     : st == S::Splash || st == S::Login || st == S::Password ? "Logging in" : st == S::Split ? "Split chat"
                     : st == S::AxisNet ? "Axis Net" : st == S::AxisCall ? "Remote board" : "Online";
            if (!node->available()) activity += " (DND)";
            if (!node->linkShown().empty()) activity += "  " + node->linkShown();  // (tier only, at most every 5 s: not a status per report)
        }
        Session& me = sessions[(size_t)i];
        if (handle + "|" + activity != me.lastStatus) {
            me.lastStatus = handle + "|" + activity;
            cluster->setStatus(i, handle, activity);
        }
    }
    if (job) {  // this caller's download or upload
        if (job->needsWorkspace() && !zipRam.held()) {  // a ZIP upload: its own inflate window, not the panel
            drawTransfer();
            if (!zipRam.ensure(kZipWindow, axis::bbs::ZipReader::kStateBytes))
                logEvent("upload: short on memory to check the ZIP");
            job->beginProcessing(zipRam.window, zipRam.window ? kZipWindow : 0, zipRam.state);
            noteZipHeap();
        }
        bool running = job->step(millis());
        if (line) line->flush();
        if (millis() - jobDrawnMs > 500) {
            jobDrawnMs = millis();
            drawTransfer();
        }
        if (!running) {
            const auto& r = job->request();
            if (r.upload && job->ok()) countUpload();
            logEvent(String(node->handle().c_str()) + (r.upload ? " uploaded " : " downloaded ") +
                     (r.upload ? job->savedName() : r.name).c_str() + (job->ok() ? "" : String(" (failed: ") + job->note().c_str() + ")"));
            noteDoing("idle");
            if (line && line->carrier()) {
                node->transferDone(job->ok(), job->bytes(), millis() - jobStartMs, job->note(), millis());
                job.reset();
                zipRam.release();
                noteZipHeap();
                sendApp(node->takeOutput());
            } else {
                hangUp();
            }
            screenDirty = true;
        }
        return;
    }
    if (nativeLord) {  // this caller is in LORD
        bool running;
        {
            HeapScope scope("door");
            running = nativeLord->step();
        }
        if (line) line->flush();
        if (!running) finishLord();
        delay(5);
        return;
    }
    if (viewer) {  // this caller's movie or pack
        bool running = viewer->step();
        noteZipHeap();
        if (line) line->flush();
        if (!running) finishViewer();
        delay(5);
        return;
    }
    if (line && axisPump(i)) return;  // a call to another Axis board owns the keys
    if (line) {
        if (axisDirty && node && node->state() == axis::bbs::Node::State::AxisNet) node->axisRedraw();
        std::string app = line->takeInput();
        if (!app.empty()) callerInput(app);
        if (line && node && node->axisTakeProbe()) {
            if (asFollower() && cluster) {
                std::string reply;
                cluster->request("AN", reply, 1500);
            } else axisProbeSoon = true;
        }
        if (line && node && node->state() == axis::bbs::Node::State::AxisCall && !axisBridge.on) {
            sendApp(node->takeOutput());  // "Calling ..." before the connect, which can take a second
            axisStart(i);
            sendApp(node->takeOutput());  // the list again when the call did not open
            return;
        }
        if (line && node) {
            {
                HeapScope scope("node");
                node->poll(millis());
            }
            if (node->state() == axis::bbs::Node::State::InDoor) {
                startDoor();
                return;
            }
            if (node->state() == axis::bbs::Node::State::Transfer && !job) {
                sendApp(node->takeOutput());  // the transfer window, before the protocol starts
                jobStartMs = jobDrawnMs = millis();
                axis::bbs::UploadInfo info{cfg.bbsName, dialName().c_str(), cfg.location,
                                           std::string(axis::bbs::kAxisSoftware) + " " + axis::bbs::kAxisVersion};
                noteDoing(String(node->transfer().upload ? "upload " : "download ") + node->transfer().name.c_str() + " by " +
                          node->handle().c_str());
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
    heapWin.sample((uint32_t)heap_caps_get_free_size(MALLOC_CAP_8BIT), (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT), doingNow());
    heapScopeFlush();  // a line the web task (core 0) held
    handleSerial();
    modemSearchPoll();  // (before any early return: a network re-apply may be waiting on it)
    applyNetworkDue();
    if (boardIsHeadless() && portal) {  // the AXIS-SETUP hotspot until a network is picked
        portal->handleClient();
        delay(2);
        return;
    }
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
    statsSync();
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
        if (cluster->role() == axis::cluster::Role::Main && !inflateHeld() && !uploading) mirrorJob.step(millis());
#endif
    }
    ledUpdate();
    if (server.hasClient()) answer(server.available());
    if (!asFollower() && !axisBridge.on && !inflateHeld() && (axisProbeSoon || millis() - axisProbeAt > 120000)) {
        axisProbeSoon = false;
        axisProbeAt = millis();
        axisProbeOne();
    }
    pumpPanel();
    for (size_t i = 0; i < sessions.size(); i++) {
        bindSession((int)i);
        serviceSession((int)i);
        unbindSession();
    }
    axisDirty = false;
    static uint32_t lastMemLog = 0, seenDropped = 0, seenGated = 0;  // low-memory web notes, at most hourly
    static bool     memLogged = false;
    if (!memLogged || millis() - lastMemLog > 3600000u) {
        uint32_t d = web::droppedLines(), g = web::gatedChecks();
        if (d != seenDropped || g != seenGated) {
            if (d != seenDropped) logEvent("discord: " + String(d - seenDropped) + " lines dropped (low memory)");
            if (g != seenGated) logEvent("ddns: waiting for memory (largest " + String(web::gatedLargestKb()) + " KB)");
            seenDropped = d, seenGated = g;
            lastMemLog = millis(), memLogged = true;
        }
    }
    static uint32_t lastTick = 0;  // keep the clock and the time-online count moving
    if (millis() - lastTick > 300u) {  // the status lights blink, long lines scroll, the times tick
        lastTick = millis();
        if (mode == Mode::Normal) screenDirty = true;
    }
    if (screenDirty) drawScreen();
    if (!servedRequest) delay(5);  // a follower's request is answered before the nap
    servedRequest = false;
}
