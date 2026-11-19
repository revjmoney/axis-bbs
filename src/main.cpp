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
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <map>
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
#include "bbs/inflatelease.h"
#include "bbs/viewcache.h"
#include "bbs/features.h"
#include "bbs/script.h"
#include "bbs/sdnotice.h"
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
#include "cluster/cardtree.h"
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
#include "bbs/wfcanim.h"
#include "bbs/wifipicker.h"
#include "bbs/node.h"
#include "bbs/setup_gate.h"
#include "bbs/artfetch.h"
#include "bbs/listtrim.h"
#include "bbs/memlog.h"
#include "bbs/setup_wizard.h"
#include "platform/cyd/ansi_screen.h"
#include "platform/cyd/boardhost.h"
#include "app/boarddata.h"
#include "app/stats.h"
#include "app/themeprovider.h"
#include "app/session.h"
#include "app/services.h"
#include "app/commands.h"
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
#ifndef AXIS_NETS
#define AXIS_NETS 1  // Sysop Net on the main board; -DAXIS_NETS=0 leaves it out (src/net/*.cpp is then never linked)
#endif
#if AXIS_NETS
#include <lwip/dns.h>
#include "net/areas.h"
#include "net/crc.h"
#if AXIS_TRANSFERS
#include "net/fetch.h"
#endif
#include "net/outlog.h"
#include "net/netrecord.h"
#include "net/record.h"
#include "net/recordview.h"
#include "net/revkeys.h"
#include "net/session.h"
#include "net/sign.h"
#include "monocypher.h"
#endif

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
static axis::bbs::HeapWindow heapWin;  // lowest heap since boot, or since the last @HEAPWIN (bench builds only)
#if AXIS_HEAP_TRACE
// Bench build only (env cyd-trace): what is live, and who took it, each time the heap reaches a new low.
#include "esp_heap_trace.h"
#include "esp_rom_sys.h"
static heap_trace_record_t traceRecs_[250];
static uint32_t            traceDumpFree_ = UINT32_MAX;
static int                 traceDumps_ = 0;
#if AXIS_BENCH  // only @HEAPWIN restarts the trace, and @HEAPWIN is in bench builds only
static void heapTraceRestart() {
    heap_trace_stop();
    heap_trace_start(HEAP_TRACE_LEAKS);
    traceDumpFree_ = UINT32_MAX;
    traceDumps_ = 0;
}
#endif
static void heapTraceLow(uint32_t freeB, uint32_t largest) {
    if (freeB >= 64000 || freeB + 3072 > traceDumpFree_ || traceDumps_ >= 8) return;
    traceDumpFree_ = freeB;
    traceDumps_++;
    size_t n = heap_trace_get_count();
    uint32_t total = 0;
    heap_trace_record_t r;
    for (size_t i = 0; i < n; i++)
        if (heap_trace_get(i, &r) == ESP_OK && r.address) total += r.size;
    esp_rom_printf("@HT low free=%u largest=%u live=%u bytes=%u doing=%s\n", (unsigned)freeB, (unsigned)largest, (unsigned)n,
                   (unsigned)total, doingNow());
    for (size_t i = 0; i < n; i++) {
        if (heap_trace_get(i, &r) != ESP_OK || !r.address || r.size < 256) continue;
        esp_rom_printf("@HT %u", (unsigned)r.size);
        for (int d = 0; d < CONFIG_HEAP_TRACING_STACK_DEPTH; d++) esp_rom_printf(" %p", r.alloced_by[d]);
        esp_rom_printf("\n");
    }
    esp_rom_printf("@HT end\n");
}
#endif

static void noteDoor(axis::bbs::DoorDiag how, uint32_t peakKb) {
    boardDiag_.door = how;
    boardDiag_.doorPeakKb = peakKb;
    boardDiag_.doorStackWords = (uint32_t)uxTaskGetStackHighWaterMark(nullptr);
}

enum class Mode { Normal, SetupChoice, SetupTelnet, SetupTouch, WifiPick };

// The board's settings: one copy that main and every Node share (about 4 KB). A change makes a new copy while a Node
// still holds the current one, so a caller already on keeps the settings they called into, as before; pushBoardToCallers
// hands them the switches. Read with boardCfg->, change with cfgEdit() or cfgSet().
static std::shared_ptr<const axis::bbs::BbsConfig> boardCfg = std::make_shared<axis::bbs::BbsConfig>();
static axis::bbs::BbsConfig& cfgEdit() {
    if (boardCfg.use_count() > 1) boardCfg = std::make_shared<axis::bbs::BbsConfig>(*boardCfg);
    return const_cast<axis::bbs::BbsConfig&>(*boardCfg);  // (every copy is made here or in cfgSet: not const underneath)
}
static void cfgSet(axis::bbs::BbsConfig c) { boardCfg = std::make_shared<axis::bbs::BbsConfig>(std::move(c)); }
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
static axis::bbs::EventLog                      events;  // newest board events, for its Logs screen
static axis::bbs::RotatingLog                   eventFile("/littlefs/events.log", "/littlefs/events.old", 8192);  // 16 KB at most
static bool                                     eventFileReady = false;  // after LittleFS is mounted
#if AXIS_FILES
static SdFileLibrary                            sdFiles;  // the file areas on the SD card, for callers
#endif
static axis::bbs::UserStore                     users;    // callers' accounts, internal flash /users.dat
static axis::bbs::WifiPicker                    picker;   // the touch WiFi picker (no saved network, or @WIFIPICK)
#if AXIS_CLUSTER
static Esp32ClusterNet                          clusterNet;  // the boards on this LAN acting as one BBS
static std::unique_ptr<axis::cluster::Cluster>  cluster;
#else  // built without the cluster: the board runs alone, and every "if (cluster)" folds away
static constexpr axis::cluster::Cluster*        cluster = nullptr;
#endif
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
#if AXIS_FILES
static std::map<int, std::unique_ptr<axis::cluster::FileServer>> fileServers;  // main: one per follower
#endif
#if AXIS_CLUSTER
static std::map<int, std::unique_ptr<axis::cluster::DoorFsServer>> doorServers;
#endif
static bool servedRequest = false;  // the main answered a follower this loop turn
#if AXIS_BENCH
static uint32_t shotDoneMs = 0;      // the last @SHOT (it stalls the loop ~13 s on purpose)
#endif

// The board's shared data (app/boarddata.h): the oneliner wall, the last callers and the lists, in internal flash on
// the main; followers read and post over the link.
static void                 logEvent(const String& what);
static BoardHost            boardHost([](const std::string& line) { logEvent(line.c_str()); });
static axis::app::BoardData boardData(boardHost);
static bool asFollower();
static bool writeWarrior(const std::string& handle);
// A node's warrior hook: none when LORD is left out of the build, so registration says nothing about a warrior.
static bool (*const warriorHook)(const std::string&) = AXIS_LORD ? writeWarrior : nullptr;

// Scene news for Discord. TLS needs about 45 KB of heap, more than a busy CYD main has, so the
// posting is done by whichever board holds the webhook (set it on a headless node): a follower
// hands its news to the main, the main sends it to every board, and the board with the webhook posts.
constexpr int kAnnounceKind = 21, kAnnounceAllKind = 22;
#if AXIS_WEB  // (built without the web jobs there is no Discord: the news stays on the board)
static void announceOut(const std::string& line) {
    if (!cluster) web::announce(String("**") + boardCfg->bbsName.c_str() + "** " + line.c_str());
    else if (asFollower()) cluster->send("", kAnnounceKind, 0, line);
    else cluster->send("", kAnnounceAllKind, 0, line);
}
#endif

// The screens a caller sees (app/themeprovider.h): the theme's art, the menu files and the scripts, from this board's
// storage, or on a follower from the main over the link. The functions below hand them to each Node.
static axis::app::ThemeProvider themeProvider(boardHost);
static std::string themeArt(const std::string& name) { return themeProvider.themeArt(name); }
static std::string themeArtTop(const std::string& name, int rows) { return themeProvider.themeArtTop(name, rows); }
static std::shared_ptr<const std::string> wfcArtShared() { return themeProvider.wfcArtShared(); }
static std::string posterPiece(const std::string& name, size_t offset, size_t want, bool* exists) {
    return themeProvider.posterPiece(name, offset, want, exists);
}
static std::string menuFile(const std::string& name) { return themeProvider.menuFile(name); }
static bool        menuExists(const std::string& name) { return themeProvider.menuExists(name); }
static bool        menuArtPieces(const std::string& name, const axis::bbs::ArtEmit& piece) { return themeProvider.menuArtPieces(name, piece); }
static std::string menuArtPiece(const std::string& name, size_t offset, size_t want, bool* exists) {
    return themeProvider.menuArtPiece(name, offset, want, exists);
}
#if AXIS_SCRIPTS
static void wireScripts(axis::bbs::Node* n) {
    if (!n) return;
    n->setScripts([](const std::string& name) { return themeProvider.scriptFile(name); }, [] { return themeProvider.scriptNames(); });
}
#else
static void wireScripts(axis::bbs::Node*) {}  // scripts are left out of this build: no loader, nothing runs
#endif

// The shared lists for the local console (callers use the same over their own node setup).
static std::vector<std::string> sharedList(const std::string& n) { return boardData.sharedList(n); }
static void sharedListAdd(const std::string& n, const std::string& line) { boardData.sharedListAdd(n, line); }
static uint32_t lastServedMs = 0;  // main: a follower's door files
#if AXIS_SD_POOL && !AXIS_FILES
#error "AXIS_SD_POOL needs the file areas (AXIS_FILES)"
#endif
#if AXIS_SD_POOL && !AXIS_CLUSTER
#error "AXIS_SD_POOL needs the cluster (AXIS_CLUSTER): it pools the followers' cards"
#endif
#if AXIS_FILES && AXIS_CLUSTER  // the file areas (features.h): without them a follower has no file menu and the main answers "0"
static axis::cluster::RemoteFileLibrary         remoteFiles([](const std::string& req, std::string& reply) {
    return cluster && cluster->request(req, reply);
});

// SD card clustering (cluster/pooledlib.h, mirrorjob.h): on the main, this board's card and every
// card follower's as one library, and the mirrored areas copied onto a second card in the background.
// A follower with a card answers the main's requests for it (cardServer).
static std::map<std::string, std::unique_ptr<axis::cluster::RemoteFileLibrary>> cardFiles;  // main: one per card board
static std::unique_ptr<axis::cluster::FileServer> cardServer;  // follower with a card: the main's requests
#endif
static axis::cluster::CardTreeServer& cardTreeServer() {  // the card mirror's tree service (cluster/cardtree.h), made on first use
    static axis::cluster::CardTreeServer server(storageRoot());
    return server;
}
static std::string cardLabel(const std::string& id) {  // "node 2" (its lowest node), or its id when it is gone
    int low = 0;
    if (cluster)
        for (auto& n : cluster->nodes())
            if (n.id == id && (!low || n.number < low)) low = n.number;
    return low ? "node " + std::to_string(low) : "board " + id;
}
#if AXIS_FILES && AXIS_CLUSTER  // (the pool is the main's and its card followers')
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
    for (auto& a : boardCfg->areas)
        if (a.mirror) v.push_back(a.id);
    return v;
});
#endif
#endif  // AXIS_FILES && AXIS_CLUSTER
static Mode                                     afterPick = Mode::Normal;
static size_t                                   pendingSeen = 0;
// the door the caller is in, if any (the screen copy is freed meanwhile)
static std::unique_ptr<axis::door::HostFs>      doorFs;     // the door's folder: here, or the main's over the link
static std::unique_ptr<axis::door::LockTable>   doorRemoteLocks;  // a follower's door locks the main's records
static axis::door::LockTable                    doorLocks;
// Calls, posts and uploads today and the lifetime call total, in internal flash (/stats.txt) so a reboot keeps them
// (app/stats.h). The day counters roll at local midnight; until the clock is set they just keep counting.
// In a cluster the main owns them. A follower (or a board still finding the main) adds its calls and uploads to
// statsShare and sends them up (statsSync); it shows the main's numbers from the heartbeat plus what it has not had
// acknowledged, and never rolls, counts into, saves or overwrites its own dayStats and /stats.txt (the card board can be
// a follower for a moment, before a cardless main hands main back: its own numbers must survive that). A follower that
// stands in as main counts on its own file. Posts need no sending: a follower's caller posts straight into the main's
// message base (CountingMsgs), which counts them there.
static axis::app::Stats      stats(boardHost);
static axis::bbs::StatsShare statsShare;
static axis::bbs::StatsInbox statsInbox;  // main: the follower batches already applied
static bool statsOwner() {
    if (!cluster) return true;
    auto r = cluster->role();
    return r == axis::cluster::Role::Main || r == axis::cluster::Role::Off;
}
// What the screens and @STATS show: this board's own numbers on the main, the main's last ones on a follower (none
// before it has heard them), plus anything still waiting to be sent up.
static axis::bbs::DayStats statsShown() {
    if (statsOwner()) return statsShare.shown(stats.dayStats);
    return statsShare.shown();
}
static bool statsSendNow = false;  // a follower counted something: send it at the top of the next loop turn
// The shared services' ties to this board: its settings, its place in the cluster and the follower's link to the main.
static bool wireSharedServices() {
    auto askMain = [](const std::string& req, std::string& reply) { return cluster && cluster->request(req, reply, 3000); };
    boardData.follower = asFollower;
    boardData.remote = askMain;
    themeProvider.config = []() -> const axis::bbs::BbsConfig& { return *boardCfg; };
    themeProvider.follower = asFollower;
    themeProvider.remote = askMain;
    stats.owner = statsOwner;
    stats.share = [](axis::app::Stats::Count c) {
        if (c == axis::app::Stats::Count::Call) statsShare.call();
        else if (c == axis::app::Stats::Count::Upload) statsShare.upload();
        else statsShare.post();
        statsSendNow = true;
    };
    stats.console = [](const char* line) { Serial.println(line); };
    return true;
}
static const bool sharedServicesWired = wireSharedServices();

// Several callers per board, each an axis::app::Session (app/session.h): null while its slot is free. A Session is dropped
// at the end of the loop turn in which it hung up (slotStatus keeps the slot's last status line for the cluster).
static std::vector<std::unique_ptr<axis::app::Session>> sessions(1);
static std::vector<std::string>                         slotStatus;  // a free slot's last status line, as the Session had it
// The session whose turn it is (answer, its loop turn), for the board code that asks for "this caller" (the transfer window,
// the setup screen, a follower's upload description). Null between turns, as the old per-turn globals were.
static axis::app::Session* turn = nullptr;
struct TurnOf {  // `turn` for one scope, put back on the way out (an exception included)
    axis::app::Session* was;
    explicit TurnOf(axis::app::Session* s) : was(turn) { turn = s; }
    ~TurnOf() { turn = was; }
};
static bool boardHasLord() {
    for (auto& s : sessions)
        if (s && s->lord) return true;
    return false;
}
static bool boardHasViewer() {
    for (auto& s : sessions)
        if (s && s->viewer) return true;
    return false;
}
static bool boardHasJob() {
    for (auto& s : sessions)
        if (s && s->job) return true;
    return false;
}
static bool inflateHeld() { return axis::bbs::inflate::busy(); }  // a deflated piece is playing, or a ZIP upload is being checked
static void noteZipHeap() { web::setDoorRunning(inflateHeld()); }
// Every line on the board is a WifiStream (or the panel's PanelStream): main.cpp made them all (answerIn).
static WifiStream* wifiLine(axis::app::Session& s) { return static_cast<WifiStream*>(s.line.get()); }
static WifiStream* sessionLine(size_t i) { return sessions[i] ? static_cast<WifiStream*>(sessions[i]->line.get()) : nullptr; }
static axis::bbs::Node* sessionNode(size_t i) { return sessions[i] ? sessions[i]->node.get() : nullptr; }
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
#if AXIS_BENCH
static int      tapX = -1, tapY = -1;  // a tap from the @TAP serial command (bench)
#endif

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

static String dialName() { return String(boardCfg->boardName.c_str()) + ".local"; }

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
#if AXIS_NATIVE
    s.hardware = "PC";  // the native Linux build (About: what the board runs on)
#else
    s.hardware = "ESP32 CYD " + std::to_string(ESP.getCpuFreqMHz()) + " MHz " +
                 std::to_string(ESP.getFlashChipSize() >> 20) + " MB flash";
#endif
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
#if AXIS_WEB
    std::string a = axis::bbs::announceLine(what.c_str());  // scene news for the Discord channel
    if (!a.empty()) announceOut(a);
#endif
}

// users.dat written a block at a time (the whole file as one string is more than the main can spare)
static bool writeUsersFile() {
    return flashWriteStream("/users.dat", [](File& f) {
        std::string buf;
        bool        ok = true;
        auto        flush = [&] {
            if (ok && !buf.empty()) ok = f.write((const uint8_t*)buf.data(), buf.size()) == buf.size();
            buf.clear();
        };
        users.formatTo([&](const std::string& piece) {
            buf += piece;
            if (buf.size() >= 512) flush();
        });
        flush();
        return ok;
    });
}
static void saveUsers() {
    HeapScope scope("saveUsers");
    if (!writeUsersFile()) logEvent("users: SAVE FAILED");
    if (cluster)  // a follower tells the main; the main tells every follower
        for (auto& rec : userSync.changed(users)) cluster->sendUser(rec);
    if (users.pending() > pendingSeen) logEvent("new user application waiting: tap USERS");
    pendingSeen = users.pending();
    screenDirty = true;
}

static bool asFollower() { return cluster && cluster->role() == axis::cluster::Role::Follower; }

// The SD card notice (bbs/sdnotice.h): no card here, a feature that needs one built in, and not a follower of a main
// that has one. While its place is unknown it waits (a follower of a card main would only flash it): the cluster has
// not started yet but a cluster key is saved (it will look for a main), or it is still finding its main.
#if AXIS_CLUSTER
static int clusterKeySaved = -1;  // -1: not read yet from NVS "clu" (@CLUSTERKEY keeps it current)
static bool clusterKeyIsSaved() {
    if (clusterKeySaved < 0) {
        Preferences p;
        p.begin("clu", true);
        clusterKeySaved = p.getString("key", "").length() > 0 ? 1 : 0;
        p.end();
    }
    return clusterKeySaved == 1;
}
#endif
static bool sdNotice() {
#if AXIS_CLUSTER
    bool finding = cluster && (cluster->role() == axis::cluster::Role::Discovering || cluster->role() == axis::cluster::Role::Joining);
    if (axis::bbs::sdNoticeWaits(cluster != nullptr, cluster ? false : clusterKeyIsSaved(), finding)) return false;
#endif
    return axis::bbs::sdNoticeShown(storageHasSd(), asFollower() && cluster->mainHasCard());
}

// The message base: on the main (SD card, or internal flash), and the main's over the link on every
// follower, so the whole cluster is one conversation.
struct CountingMsgs : axis::bbs::FileMessageStore {  // counts a public post for the WFC's today box
    using axis::bbs::FileMessageStore::FileMessageStore;
    int post(const std::string& area, const axis::bbs::Message& m) override {
        int n = axis::bbs::FileMessageStore::post(area, m);
#if AXIS_NETS
        // net posts and Net Files listings are not this board's
        if (n && area != "email" && !axis::net::isNetAreaId(area) && area != axis::net::kNetFilesArea) stats.countPost();
#else
        if (n && area != "email") stats.countPost();
#endif
        return n;
    }
};
static std::unique_ptr<CountingMsgs> localMsgs;
static axis::bbs::RemoteMessageStore remoteMsgs([](const std::string& req, std::string& reply) {
    return cluster && cluster->request(req, reply, 4000);
});
#if AXIS_NETS
static axis::bbs::MessageStore* netMessages();  // the store with Sysop Net on top, once the net runs (main only)
// The board's store while the net is not on top of it: a post into a net area (sn_ or un_) is refused, so none is
// ever stored unsigned. NetCore itself writes to localMsgs, never through this.
struct NoNetMsgs : axis::bbs::MessageStore {
    std::vector<axis::bbs::Message> headers(const std::string& area) override { return localMsgs->headers(area); }
    bool read(const std::string& area, int num, axis::bbs::Message& out) override { return localMsgs->read(area, num, out); }
    int  post(const std::string& area, const axis::bbs::Message& m) override {
        return axis::net::isNetAreaId(area) ? 0 : localMsgs->post(area, m);
    }
    int drop(const std::string& area, const std::function<bool(const axis::bbs::Message&)>& gone) override {
        return localMsgs->drop(area, gone);
    }
    void scanHeaders(const std::string& area, const std::function<bool(const axis::bbs::Message&)>& each) override {
        localMsgs->scanHeaders(area, each);
    }
};
static NoNetMsgs noNetMsgs;
#endif
static axis::bbs::MessageStore& callerMessages() {
    if (!localMsgs) {
        std::string root = std::string(storageRoot()) + "/msgs";
        ::mkdir(root.c_str(), 0777);
        localMsgs.reset(new CountingMsgs(root));
    }
    if (asFollower()) return remoteMsgs;
#if AXIS_NETS
    if (axis::bbs::MessageStore* n = netMessages()) return *n;
    return noNetMsgs;
#else
    return *localMsgs;
#endif
}

// A follower (card or not) sees the main's library, which is every card pooled; the main pools its
// own card with its card followers'; a board on its own has just its card.
#if AXIS_FILES
static axis::bbs::FileLibrary& rawCallerFiles() {
#if AXIS_CLUSTER
    if (asFollower()) return remoteFiles;
#endif
#if AXIS_SD_POOL
    if (cluster && cluster->role() == axis::cluster::Role::Main && !cluster->cardBoards().empty()) return pooledFiles;
#endif
    return sdFiles;
}
#if AXIS_NETS
// Net Files (the Net Files section below): a file in an area shared on User Net was removed or renamed here.
static void netFilesRemoved(const std::string& area, const std::string& name);
static void netFilesRenamed(const std::string& area, const std::string& from, const std::string& to);
// The callers' file library with Net Files on top, as NetStore is for messages: a removal or rename in a shared area
// sends the removal of the old name's listing once it is done, if this board lists it (a rename lists the new name too). Everything else passes
// through to the library of the moment (rawCallerFiles), so a Node may keep it for the whole call.
struct NetSharingFiles : axis::bbs::FileLibrary {
    std::vector<axis::bbs::FileEntry> list(const std::string& a) override { return rawCallerFiles().list(a); }
    bool read(const std::string& a, const std::string& n, std::string& out, size_t max) override { return rawCallerFiles().read(a, n, out, max); }
    std::unique_ptr<axis::bbs::RandomFile> open(const std::string& a, const std::string& n) override { return rawCallerFiles().open(a, n); }
    std::unique_ptr<axis::bbs::WriteFile> create(const std::string& a, const std::string& n) override { return rawCallerFiles().create(a, n); }
    bool describe(const std::string& a, const std::string& n, const std::string& d) override { return rawCallerFiles().describe(a, n, d); }
    bool remove(const std::string& a, const std::string& n) override {
        bool ok = rawCallerFiles().remove(a, n);
        if (ok) netFilesRemoved(a, n);
        return ok;
    }
    bool rename(const std::string& a, const std::string& from, const std::string& to) override {
        bool ok = rawCallerFiles().rename(a, from, to);
        if (ok) netFilesRenamed(a, from, to);
        return ok;
    }
};
static NetSharingFiles netSharingFiles;
#endif
static axis::bbs::FileLibrary& callerFiles() {
#if AXIS_NETS
    if (!asFollower()) return netSharingFiles;  // (the main's net runs the hooks; a follower's go to the main's library)
#endif
    return rawCallerFiles();
}
static axis::bbs::FileLibrary* callerLib() { return &callerFiles(); }  // a new Node's file areas
#else
static axis::bbs::FileLibrary* callerLib() { return nullptr; }  // the file areas are not in this firmware
#endif

static uint32_t unixNow() {
    time_t t = time(nullptr);
    return t > 1700000000 ? (uint32_t)t : 0;
}

// File areas: one folder each under /files on the SD card (none without a card).
static void makeAreaFolders() {
    if (!AXIS_FILES || !storageHasSd()) return;  // (a build without the file areas makes none)
    ::mkdir("/sd/files", 0777);
    ::mkdir((std::string("/sd/files/") + axis::bbs::viewcache::kArea).c_str(), 0777);  // the ANSI viewer's unpacked pieces
    for (auto& a : boardCfg->areas) ::mkdir(("/sd/files/" + a.id).c_str(), 0777);
}

// ------------------------------------------------------------------ screens (placeholder look)
// Each screen draws into screenGfx() (the full copy, or the panel itself when there was no room for it) in pc() colours. The
// WFC draws through the band instead; the full copy goes when the WFC is back (drawScreen), never while a screen is drawing.
static uint32_t pc(uint8_t p) { return screenColor(p); }

// Cells written straight into a 4-bit buffer (BigText, AnsiScreen): into the full copy, or with none, through the band 40 pixel
// rows at a time straight to the panel. f(dst, y0) draws with its row 0 at y0 in dst.
template <typename F>
static void renderRows(int height, F f) {
    if (LGFX_Sprite* s = screenSprite()) return f(*s, 0);
    LGFX_Sprite* b = screenBand();
    if (!b) return;
    for (int top = 0; top < height; top += 40) {
        b->fillScreen(pal::BLACK);
        f(*b, -top);
        b->pushSprite(0, top);
        tft.waitDMA();  // the next band is drawn into the same buffer
    }
}

static void title(const char* sub) {
    auto& g = screenGfx();
    g.fillScreen(pc(pal::BLACK));
    g.setTextDatum(textdatum_t::top_left);
    g.setTextColor(pc(pal::ORANGE), pc(pal::BLACK));
    g.setTextSize(2);
    String t = boardCfg->bbsName.c_str();
    t.toUpperCase();
    g.drawString(t.c_str(), 8, 8);
    g.setTextSize(1);
    g.setTextColor(pc(pal::ORANGE), pc(pal::BLACK));
    g.drawString(sub, 8, 30);
}

static void bigButton(int y, const char* label, uint8_t colour) {
    auto&    g = screenGfx();
    uint32_t color = pc(colour);
    g.fillRoundRect(16, y, 288, 52, 8, color);
    g.setTextColor(pc(pal::WHITE), color);
    g.setTextSize(2);
    g.setTextDatum(textdatum_t::middle_center);
    g.drawString(label, 160, y + 26);
    g.setTextDatum(textdatum_t::top_left);
    g.setTextSize(1);
}

// The waiting-for-caller screen on the board's own screen: the WFC skin's WFCCYD.ANS (or the built-in art/wfccyd.ans) in 45x24
// cells of 7x10 (315x240, centred), the live values of wfcSnapshot(), its keys and node lines tappable. Expanded again every 5 s
// when the values changed, and whenever it comes back from another screen. The art is read from the card once and kept while
// the WFC is up; the terminal (~2.2 KB of cells) is only held while the art is drawn.
static int                                detailSession = -1;  // the node detail screen is up for this session (-1: the WFC)
static std::vector<axis::bbs::WfcHotspot> wfcSpots;            // where the keys and node lines landed
static axis::bbs::WfcSnapshot             wfcShown;            // the values on the screen
static uint32_t                           wfcDrawnMs = 0, wfcSoonUntil = 0;  // last expanded; a "coming soon" line until then
static bool                               wfcStale = true;     // draw it at the next turn (another screen was up, a key was tapped)
static bool                               wfcOnPanel = false;  // the panel shows the WFC (@SHOT draws it again band by band)
// The panel WFC moving against burn-in ("wfc_anim", bbs/wfcanim.h): it holds a 2,160 B copy of the cells only while it moves,
// and stands still (that copy given back) while a caller is on, when memory runs short, or when it is turned off.
static axis::bbs::WfcAnim wfcAnim;
static const char*        wfcAnimWhy = "";  // why it stands still ("" = it may move)
static uint32_t           wfcAnimFrameMs = 0, wfcAnimTryMs = 0;
#if AXIS_BENCH
static uint32_t wfcAnimFrameUs = 0, wfcAnimSlowUs = 0, wfcAnimDrawUs = 0, wfcAnimBands = 0;  // frame times for @WFCFX
#endif
static std::string                        wfcArt, wfcArtFolder;  // the theme's art while the WFC is up, and the skin folder it came from
static bool                               wfcArtLoaded = false;  // wfcArt is current (empty: the theme has none, the built-in art is drawn from flash)
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
// prev: the copy boardCfg replaced. Whether the two differ only in what a caller already on picks up is decided once here.
static std::shared_ptr<const axis::bbs::BbsConfig> nodeCfg();  // the board's settings as a Node gets them (below)
static void pushBoardToCallers(const std::shared_ptr<const axis::bbs::BbsConfig>& prev) {
    bool only = prev && axis::bbs::Node::onlySwitches(*prev, *boardCfg);
    const std::shared_ptr<const axis::bbs::BbsConfig> next = nodeCfg();  // with the Sysop Net areas while they are on
    auto one = [&](axis::bbs::Node* n) {
        if (n) n->applyBoard(next, prev, only);
    };
    one(localNode.get());
    for (size_t i = 0; i < sessions.size(); i++) one(sessionNode(i));
#if AXIS_MODEMLINK
    for (size_t i = 0; i < sessions.size(); i++)  // modem_link off: calls already on stop taking link reports
        if (WifiStream* l = sessionLine(i)) l->acceptLink(boardCfg->modemLink);
#endif
}
static std::unique_ptr<axis::bbs::AnsiTerm> localTerm;    // (only while the console is open: ~4 KB)
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
    auto& g = screenGfx();
    g.fillScreen(pc(pal::BLACK));
    g.setFont(&fonts::Font2);
    auto button = [&g](int x, int y, int w, int h, const char* label, bool lit) {
        uint32_t fill = lit ? pc(pal::ORANGE) : pc(pal::DIM);
        g.fillRoundRect(x, y, w, h, 5, fill);
        g.drawRoundRect(x, y, w, h, 5, lit ? pc(pal::WHITE) : pc(pal::ORANGE));
        g.setTextColor(lit ? pc(pal::BLACK) : pc(pal::WHITE), fill);
        g.setTextDatum(textdatum_t::middle_center);
        g.drawString(label, x + w / 2, y + h / 2);
        g.setTextDatum(textdatum_t::top_left);
    };
    if (localKeys) {  // typing: what's typed on top, the keys below
        g.setTextColor(pc(pal::ORANGE), pc(pal::BLACK));
        g.drawString("LOCAL: type, then OK", 8, 4);
        g.drawRect(4, 28, 312, 30, pc(pal::ORANGE));
        g.setTextColor(pc(pal::WHITE), pc(pal::BLACK));
        g.drawString((localKb.shown() + "_").c_str(), 10, 35);
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
            renderRows(localTerm->rows() * 8, [](LGFX_Sprite& d, int y0) {
                localScreen->renderZoom(d, localTerm->rows(), localPan, y0);  // 200 pixels, 40 columns
            });
        } else {
            renderRows(localTerm->rows() * 8, [](LGFX_Sprite& d, int y0) {
                localScreen->renderSharp(d, localTerm->rows(), y0);  // 200 pixels of the BBS, all 80 columns, crisp
            });
        }
        button(4, 206, 58, 30, "KEYS", false);
        button(66, 206, 58, 30, "ENTER", false);
        button(128, 206, 58, 30, localZoom ? "FULL" : "ZOOM", false);
        button(190, 206, 58, 30, localPan ? "<<" : ">>", false);
        button(256, 206, 60, 30, "EXIT", true);
    }
    g.setFont(&fonts::Font0);
    localTerm->clean();
}

// The WFC's terminal: the art expanded with these values (and the "coming soon" line), on the heap; nullptr when short of memory.
// Nothing on the screen changes here (drawWfc and @SHOT both build it): spots, if asked, gets where the keys landed.
static std::unique_ptr<axis::bbs::AnsiTerm> wfcTerm(const axis::bbs::WfcSnapshot& s, bool soon,
                                                    std::vector<axis::bbs::WfcHotspot>* spots = nullptr) {
    std::unique_ptr<axis::bbs::AnsiTerm> t;
    try {  // (the cells are a vector: a failed allocation throws std::bad_alloc)
        t.reset(new axis::bbs::AnsiTerm(45, 24));
    } catch (const std::bad_alloc&) {
    }
    if (!t) return t;
    std::string folder = axis::bbs::wfcFolder(*boardCfg);
    if (!wfcArtLoaded || folder != wfcArtFolder) {  // read once while the WFC is up (and again when the skin changes)
        std::string art = axis::bbs::cleanWfcArt(themeArt("WFCCYD.ANS"));
        wfcArt = std::move(art);  // empty: the theme has none, and the built-in art/wfccyd.ans is read from flash where it stands
        wfcArtLoaded = true;
        // a follower's empty answer may be a request that failed: the built-in for now, and ask again at the next redraw
        // (a theme that really has none is answered from the art cache then, without asking the main)
        if (!wfcArt.empty() || !asFollower()) wfcArtFolder = folder;
        else wfcArtLoaded = false;
    }
    const char* wfcBuiltin = kWfcCydAns;
    auto        at = axis::bbs::drawWfc(wfcArt.empty() ? std::string_view(wfcBuiltin, sizeof kWfcCydAns - 1) : std::string_view(wfcArt), s, *t);
    if (spots) *spots = std::move(at);
    if (soon) t->feed("\x1b[24;2H\x1b[0;1;33;44m That one is coming soon. \x1b[0m");
    else if (!s.notice.empty()) t->feed("\x1b[24;2H\x1b[0;1;37;41m " + s.notice + " \x1b[0m");  // the SD card notice (40 of 45 cells)
    return t;
}

static bool wfcSoon() { return wfcSoonUntil && (int32_t)(millis() - wfcSoonUntil) < 0; }

// The panel WFC may move: it is turned on, nobody is online on this board, and the heap's largest block is not short
// (below 40 KB it stops, above 56 KB it may go again).
static bool wfcAnimWanted() {
    static bool low = false;
    uint32_t    largest = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    if (largest < 40960u) low = true;
    else if (largest > 57344u) low = false;
    wfcAnimWhy = !boardHasPanel() ? "no panel" : !boardCfg->wfcAnim ? "turned off" : callersOnline() ? "a caller is on" : low ? "low memory" : "";
    return !*wfcAnimWhy;
}

// The WFC on the board: false when it is unchanged. The terminal is drawn through the band (320x40, 4 rows of cells) straight
// to the panel, top to bottom, and goes again, so the loop's stack only holds the snapshot.
static bool drawWfc() {
    HeapScope scope("drawWfc");
    uint32_t now = millis();
    bool     soon = wfcSoon();
    if (wfcSoonUntil && !soon) wfcSoonUntil = 0, wfcStale = true;  // the "coming soon" line goes
    if (!wfcStale && now - wfcDrawnMs < 5000) return false;
    wfcDrawnMs = now;
    axis::bbs::WfcSnapshot s = wfcSnapshot();
    if (!wfcStale && s == wfcShown) return false;
    std::vector<axis::bbs::WfcHotspot> spots;
    std::unique_ptr<axis::bbs::AnsiTerm> t;
    if (screenBandsInit()) t = wfcTerm(s, soon, &spots);  // (the band is made at boot: this only tries again if that failed)
    LGFX_Sprite* band = screenBand();
    if (!t || !band) {  // short on memory: say so (not whatever screen was up before), and try again at the next turn
        wfcAnim.unload();
        auto& g = screenGfx();
        g.fillScreen(pc(pal::BLACK));
        g.setFont(&fonts::Font0);
        g.setTextColor(pc(pal::ORANGE), pc(pal::BLACK));
        g.drawString("low memory: the waiting screen will be back", 8, 116);
        wfcOnPanel = false;
        return true;
    }
    bool anim = wfcAnimWanted() && wfcAnim.load(*t);  // moving: its cells again (what is moving carries on)
    if (!anim) wfcAnim.unload();
    tft.startWrite();  // (one bus hold for the six bands)
    for (int row = 0; row < 24; row += 4) {
        if (anim) AnsiScreen::renderAnimBand(*band, wfcAnim, row / 4, now);
        else AnsiScreen::renderCellsBand(*band, *t, row, 4);
        band->pushSprite(0, row * 10);
        tft.waitDMA();  // the next band is drawn into the same buffer
    }
    tft.endWrite();
    wfcSpots = std::move(spots);
    wfcShown = std::move(s);
    wfcStale = false;
    wfcOnPanel = true;
    return true;
}

// Each loop turn: the moving WFC's next frame (about 15 a second), only the bands that move pushed. Stops at once (back to
// the still screen) when a caller comes on, memory runs short or it is turned off; starts again through drawWfc.
static void wfcAnimTick() {
    uint32_t now = millis();
    if (!wfcAnim.loaded()) {
        if (wfcOnPanel && mode == Mode::Normal && now - wfcAnimTryMs > 2000u) {
            wfcAnimTryMs = now;
            if (wfcAnimWanted()) wfcStale = true, screenDirty = true;  // the next drawWfc loads it (or, no room, tries later)
        }
        return;
    }
    if (!wfcOnPanel || mode != Mode::Normal) {  // another screen is up: it starts again when the WFC is back
        wfcAnim.unload();
        return;
    }
    if (!wfcAnimWanted()) {  // the still screen, now
        wfcAnim.unload();
        wfcStale = true;
        screenDirty = true;
        wfcAnimTryMs = now;
        return;
    }
    // a frame is drawn two bands a loop turn (so the loop keeps going), every band of it at the same moment (no tearing
    // between bands): the next frame starts once all of this one is out
    static uint8_t  pending = 0;
    static uint32_t frameAt = 0;
    if (!pending) {
        if (now - wfcAnimFrameMs < 66u) return;  // about 15 frames a second
        wfcAnimFrameMs = now;
        wfcAnim.step(now);
        pending = wfcAnim.bandsDue(now);
        frameAt = now;
    }
    now = frameAt;
    LGFX_Sprite* band = screenBand();
    if (!pending || !band) return;
    uint8_t due = 0;
    for (int b = 0, k = 0; b < axis::bbs::WfcAnim::kBands && k < 2; b++)
        if (pending & (1u << b)) due |= (uint8_t)(1u << b), k++;
    pending &= (uint8_t)~due;
#if AXIS_BENCH
    uint32_t t0 = micros();
    wfcAnimDrawUs = wfcAnimBands = 0;
#endif
    tft.startWrite();
    for (int b = 0; b < axis::bbs::WfcAnim::kBands; b++) {
        if (!(due & (1u << b))) continue;
#if AXIS_BENCH
        uint32_t d0 = micros();
#endif
        AnsiScreen::renderAnimBand(*band, wfcAnim, b, now);
#if AXIS_BENCH
        wfcAnimDrawUs += micros() - d0;
        wfcAnimBands++;
#endif
        band->pushSprite(0, b * axis::bbs::WfcAnim::kBandH);
        tft.waitDMA();
    }
    tft.endWrite();
#if AXIS_BENCH
    wfcAnimFrameUs = micros() - t0;
    if (wfcAnimFrameUs > wfcAnimSlowUs) wfcAnimSlowUs = wfcAnimFrameUs;
#endif
}

// A node's details (tap its line on the WFC): who, where, how long, and a KICK for the sysop.
static void drawNodeDetail(int i) {
    auto& g = screenGfx();
    g.fillScreen(pc(pal::BLACK));
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
        uint32_t since = sessions[(size_t)i]->startMs;
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
    renderRows(13 * 16, [&t](LGFX_Sprite& d, int y0) { t.render(d, y0, 13); });
    g.setFont(&fonts::Font2);
    g.setTextDatum(textdatum_t::middle_center);
    g.fillRoundRect(8, 204, 100, 30, 6, pc(pal::DIM));
    g.drawRoundRect(8, 204, 100, 30, 6, pc(pal::ORANGE));
    g.setTextColor(pc(pal::WHITE), pc(pal::DIM));
    g.drawString("BACK", 58, 219);
    if (n && l) {
        g.fillRoundRect(212, 204, 100, 30, 6, pc(pal::ORANGE));
        g.drawRoundRect(212, 204, 100, 30, 6, pc(pal::WHITE));
        g.setTextColor(pc(pal::BLACK), pc(pal::ORANGE));
        g.drawString("KICK", 262, 219);
    }
    g.setTextDatum(textdatum_t::top_left);
    g.setFont(&fonts::Font0);
}

// The sysop menu on the board's own screen (a key tapped on the WFC): the one SysopShell in 40 columns of 8x16 letters, a tap
// on a row picks it, KEYS / ENTER / BACK / EXIT under it, and the touch keyboard for its questions.
static void touchButton(int x, int y, int w, int h, const char* label, bool lit) {
    auto&    g = screenGfx();
    uint32_t fill = lit ? pc(pal::ORANGE) : pc(pal::DIM);
    g.fillRoundRect(x, y, w, h, 5, fill);
    g.drawRoundRect(x, y, w, h, 5, lit ? pc(pal::WHITE) : pc(pal::ORANGE));
    g.setTextColor(lit ? pc(pal::BLACK) : pc(pal::WHITE), fill);
    g.setTextDatum(textdatum_t::middle_center);
    g.drawString(label, x + w / 2, y + h / 2);
    g.setTextDatum(textdatum_t::top_left);
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
    try {
        shellTerm.reset(new axis::bbs::AnsiTerm(40, 15));
    } catch (const std::bad_alloc&) {
    }
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
    auto& g = screenGfx();
    g.fillScreen(pc(pal::BLACK));
    g.setFont(&fonts::Font2);
    if (shellKeys) {
        g.setTextColor(pc(pal::ORANGE), pc(pal::BLACK));
        g.drawString(localKb.title().c_str(), 8, 4);
        g.drawRect(4, 28, 312, 30, pc(pal::ORANGE));
        g.setTextColor(pc(pal::WHITE), pc(pal::BLACK));
        g.drawString((localKb.shown() + "_").c_str(), 10, 35);
        for (auto& k : localKb.keys()) touchButton(k.x, k.y, k.w, k.h, k.label.c_str(), k.lit);
    } else {
        bigText.clear(0x07);
        for (int r = 0; r < kShellRows; r++)
            for (int c = 0; c < 40; c++) {
                const auto& cell = shellTerm->cell(r, c);
                bigText.cells[r][c] = {cell.ch, cell.attr};
            }
        renderRows(kShellRows * 16, [](LGFX_Sprite& d, int y0) { bigText.render(d, y0, kShellRows); });
        bool yn = boardShell->yesNo();
        touchButton(4, 210, 74, 28, yn ? "YES" : "KEYS", yn);
        touchButton(82, 210, 74, 28, yn ? "NO" : "ENTER", false);
        touchButton(160, 210, 76, 28, "BACK", false);
        touchButton(242, 210, 74, 28, "EXIT", true);
    }
    g.setFont(&fonts::Font0);
}

// The sysop panel while a caller downloads or uploads: a drippy window with a live progress bar.
__attribute__((noinline)) static void drawTransfer(axis::app::Session& s) {
    wfcOnPanel = false;  // (called from the sessions too: the panel no longer shows the WFC, so @SHOT must not dump it)
    static BigText t;
    const auto&    r = s.job->request();
    uint32_t       done = s.job->bytes(), size = s.job->size(), secs = (millis() - s.jobStartMs) / 1000;
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
    std::string name = (r.upload ? s.job->savedName() : r.name);
    t.text(6, 1, "file", L);
    t.text(6, 10, (name.empty() ? std::string("(waiting)") : name).substr(0, 29).c_str(), W);
    t.text(7, 1, "caller", L);
    axis::bbs::Node* who = s.node.get();
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
    renderRows(240, [](LGFX_Sprite& d, int y0) { t.render(d, y0, 15); });
    screenPushIfSprite();
}

// The WiFi picker: networks in range, or the on-screen keyboard. Rev's scheme: orange, grey, white.
static void drawPicker() {
    auto& g = screenGfx();
    g.fillScreen(pc(pal::BLACK));
    g.setFont(&fonts::Font2);
    g.setTextDatum(textdatum_t::top_left);
    auto button = [&g](int x, int y, int w, int h, const char* label, bool lit) {
        uint32_t fill = lit ? pc(pal::ORANGE) : pc(pal::DIM);
        g.fillRoundRect(x, y, w, h, 5, fill);
        g.drawRoundRect(x, y, w, h, 5, lit ? pc(pal::WHITE) : pc(pal::ORANGE));
        g.setTextColor(lit ? pc(pal::BLACK) : pc(pal::WHITE), fill);
        g.setTextDatum(textdatum_t::middle_center);
        g.drawString(label, x + w / 2, y + h / 2);
        g.setTextDatum(textdatum_t::top_left);
    };
    if (picker.screen() == axis::bbs::WifiPicker::Screen::List) {
        g.setTextColor(pc(pal::ORANGE), pc(pal::BLACK));
        g.drawString("W I F I", 8, 4);
        g.setTextColor(pc(pal::LIGHTGREY), pc(pal::BLACK));
        g.drawString("tap your network", 90, 4);
        g.drawString(picker.message().substr(0, 40).c_str(), 8, 20);
        auto rows = picker.rows();
        for (size_t i = 0; i < rows.size(); i++) {
            int y = axis::bbs::WifiPicker::kRowTop + (int)i * axis::bbs::WifiPicker::kRowH;
            g.drawRect(4, y, 312, axis::bbs::WifiPicker::kRowH - 3, pc(pal::DARKGREY));
            g.setTextColor(pc(pal::WHITE), pc(pal::BLACK));
            g.drawString(rows[i].c_str(), 10, y + 6);
        }
        if (rows.empty()) g.drawString("no networks found: RESCAN", 10, 60);
        for (auto& b : picker.buttons()) button(b.x, b.y, b.w, b.h, b.label.c_str(), false);
    } else {
        auto& kb = picker.keyboard();
        g.setTextColor(pc(pal::ORANGE), pc(pal::BLACK));
        g.drawString(kb.title().substr(0, 26).c_str(), 70, 4);
        g.drawRect(4, 28, 312, 30, pc(pal::ORANGE));
        g.setTextColor(pc(pal::WHITE), pc(pal::BLACK));
        g.drawString((kb.shown() + "_").c_str(), 10, 35);
        for (auto& k : kb.keys()) button(k.x, k.y, k.w, k.h, k.label.c_str(), k.lit);
    }
    g.setFont(&fonts::Font0);
}

static void drawScreen() {
    if (!boardHasPanel()) {  // headless: no panel, no copies, nothing to draw
        screenDirty = false;
        return;
    }
    static bool fullAsked = false;  // this stay away from the WFC asked for the full copy (no room: straight to the panel until the WFC)
    if (mode != Mode::Normal || (turn && turn->job) || localNode || boardShell || detailSession >= 0) {  // the WFC is drawn afresh when it is back
        wfcStale = true;
        wfcOnPanel = false;
        if (wfcArtLoaded) std::string().swap(wfcArt), wfcArtLoaded = false;  // and its art read again then (the skin may change meanwhile)
        if (!fullAsked) fullAsked = true, screenFull();
    } else {  // every way back to the WFC comes through here: the full copy goes before the WFC is drawn
        screenFullRelease();
        fullAsked = false;
    }
    auto& g = screenGfx();  // (nothing below releases the full copy)
    if (mode == Mode::WifiPick) {
        drawPicker();
        screenPushIfSprite();
        screenDirty = false;
        return;
    }
    if (turn && turn->job) {
        drawTransfer(*turn);
        screenDirty = false;
        return;
    }
    if (mode == Mode::SetupChoice) {
        title("first-boot setup");
        g.setTextColor(pc(pal::LIGHTGREY), pc(pal::BLACK));
        g.drawString(("WiFi: " + String(wifiSsid()) + "   " + WiFi.localIP().toString()).c_str(), 8, 50);
        g.drawString("How do you want to set up your BBS?", 8, 70);
        bigButton(92, "ON THIS SCREEN", pal::DIM);
        bigButton(156, "OVER TELNET", pal::BLUE);
        g.setTextColor(pc(pal::DARKGREY), pc(pal::BLACK));
        g.drawString("tap a button", 8, 222);
    } else if (mode == Mode::SetupTelnet) {
        title("first-boot setup");
        g.setTextColor(pc(pal::LIGHTGREY), pc(pal::BLACK));
        g.drawString("From a computer on this network:", 8, 56);
        g.setTextSize(2);
        g.setTextColor(pc(pal::LIGHTCYAN), pc(pal::BLACK));
        g.drawString(("telnet " + dialName()).c_str(), 8, 76);
        g.setTextSize(1);
        g.setTextColor(pc(pal::LIGHTGREY), pc(pal::BLACK));
        g.drawString(("or telnet " + WiFi.localIP().toString() + " " + String(boardCfg->telnetPort)).c_str(), 8, 100);
        g.drawString("Setup PIN:", 8, 128);
        g.setTextSize(4);
        g.setTextColor(pc(pal::YELLOW), pc(pal::BLACK));
        g.drawString(setupPin.c_str(), 8, 144);
        g.setTextSize(1);
        axis::app::Session* s0 = sessions.empty() ? nullptr : sessions[0].get();  // (setup takes one caller, in slot 0)
        bool setupWiz = s0 && s0->wizard, setupGate = s0 && s0->gate;
        g.setTextColor(setupWiz ? pc(pal::LIGHTGREEN) : pc(pal::DARKGREY), pc(pal::BLACK));
        g.drawString(setupWiz ? "setup in progress over telnet..."
                              : setupGate ? "caller connected, waiting for the PIN" : "waiting for the sysop to call",
                       8, 200);
    } else if (mode == Mode::SetupTouch) {
        title("first-boot setup");
        g.setTextColor(pc(pal::LIGHTGREY), pc(pal::BLACK));
        g.drawString("On-screen setup is coming in the next build.", 8, 70);
        bigButton(156, "BACK", pal::DIM);
    } else if (localNode) {
        drawLocal();
    } else if (boardShell) {
        drawShell();
    } else if (detailSession >= 0) {
        drawNodeDetail(detailSession);
    } else if (!drawWfc()) {  // the WFC: nothing changed (drawn, it went straight to the panel band by band)
        screenDirty = false;
        return;
    }
    screenPushIfSprite();
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
    if (MDNS.begin(boardCfg->boardName.c_str())) MDNS.addService("telnet", "tcp", boardCfg->telnetPort);
    server.end();
    server.begin(boardCfg->telnetPort);
    server.setNoDelay(true);
    panelServer.begin();
    panelServer.setNoDelay(true);
    Serial.printf("axis: listening on %s:%d, byte heap %u, largest %u\n", dialName().c_str(), boardCfg->telnetPort,
                  heap_caps_get_free_size(MALLOC_CAP_8BIT), heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
}

// The clock zone at once; mDNS and the listeners now, or (an mDNS search for modems is running) once it has ended or
// 3.5 s have passed: applyNetworkDue() in the loop. Callers and the panel keep running meanwhile.
static void applyNetwork() {
    setenv("TZ", axis::bbs::tzRule(boardCfg->timezone), 1);
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

// The LORA FLOWTRON DATAMASTER 3000 SERIAL MODEM - LFD3K-SM: the newest link report on this board (the WFC's |R? codes),
// the last 10 radio calls (newest first) and the modems found on the LAN by mDNS (the sysop menu's System > Modem, @MODEM).
#if AXIS_MODEMLINK
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
#else  // built without the modem link: no radio calls kept and no mDNS search for modems
static bool modemSearchRunning() { return false; }
static void modemSearchForget() {}
static void modemSearchPoll() {}
#endif

// Lines hung up with bytes still waiting (a slow caller's goodbye): drained a little each loop turn, closed once empty, after
// 10 s with no progress (TelnetStream's rule) or after 60 s in all. At most 4; the oldest is closed for a fifth.
struct ParkedLine {
    std::unique_ptr<axis::net::TelnetStream> line;
    uint32_t                    sinceMs = 0;
};
static std::vector<ParkedLine> parkedLines;
static void parkLine(std::unique_ptr<axis::net::TelnetStream> l) {
    if (parkedLines.size() >= 4) parkedLines.erase(parkedLines.begin());
    parkedLines.push_back({std::move(l), millis()});
}
static void drainParkedLines() {
    for (size_t i = 0; i < parkedLines.size();) {
        ParkedLine& p = parkedLines[i];
        if (!p.line->drain() || !p.line->backlog() || millis() - p.sinceMs > 60000u) parkedLines.erase(parkedLines.begin() + (long)i);
        else i++;
    }
}

#if AXIS_NETS && AXIS_TRANSFERS
static void netFetchRelease(const void* owner);  // the Net Files fetch slot, if this caller holds it
#endif
// The board's part of a hang up (Session::hangUp calls it while the line and node are still there): wrong passwords count
// against the caller's address, and a radio call goes into the Modem page's last 10.
static void lineEnding(axis::app::Session& s) {
    WifiStream* line = wifiLine(s);
    axis::bbs::Node* node = s.node.get();
    if (node->failedLogin()) {  // wrong passwords: count it against the caller's address
        uint32_t ip = (uint32_t)line->remoteIP();
        loginGuard.failed(ip, millis());
        if (!loginGuard.allowed(ip, millis()))
            logEvent("login: " + line->remoteIP().toString() + " locked out for " + String(loginGuard.secondsLeft(ip, millis()) / 60) + " min");
    }
#if AXIS_MODEMLINK
    if (node->viaModem()) {  // a radio call: into the Modem page's last 10
        std::string who = node->handle();
        for (char& c : who)
            if (c == '|') c = ' ';
        radioCalls.insert(radioCalls.begin(), {who, node->linkTiers(), (millis() - s.startMs) / 1000, line->bytesIn() + line->bytesOut(),
                                               node->link().retrains});
        if (radioCalls.size() > 10) radioCalls.resize(10);
    }
#endif
}
static void callerGone(const std::string& handle) {
    lastCaller = handle.c_str();
    if (!statsOwner()) statsShare.caller(handle), statsSendNow = true;  // the main's LC is the cluster's
}

// A caller passed through to another node (the main's own node was busy).
#if AXIS_CLUSTER
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

#if AXIS_NATIVE
uint16_t hostnetPassPort(uint16_t port);  // (platform/linux/hostnet.h)
#endif
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
#if AXIS_NATIVE
    if (!out.connect(IPAddress(ip), hostnetPassPort((uint16_t)boardCfg->telnetPort), 1500)) return false;  // (23, not lan_port)
#else
    if (!out.connect(IPAddress(ip), (uint16_t)boardCfg->telnetPort, 1500)) return false;
#endif
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
#else  // built without the cluster: no other node to pass a caller to
static bool passCaller(WiFiClient&) { return false; }
static void pumpPassing() {}
#endif

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
static void viewCacheDrop();

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
    me.name = boardCfg->bbsName;
    // The public address only: no publicHost means no advertised host (never the LAN IP, which peer boards would
    // then show to their own callers). The board stays reachable on its own LAN by mDNS, just not advertised.
    me.host = boardCfg->publicHost;
    me.port = boardCfg->advertisedPort();
    me.location = boardCfg->location;
    me.sysop = boardCfg->sysopHandle;
    me.blurb = std::string(axis::bbs::kAxisSoftware) + " " + axis::bbs::kAxisVersion;
    int on = 0;
    for (size_t i = 0; i < sessions.size(); i++) on += sessionLine(i) ? 1 : 0;
    me.online = on;
    me.seen = unixNow();
    return me;
}

static std::vector<std::string> axisNotesFor(const axis::bbs::AxisCard& peer) {
    std::vector<std::string> out;
    for (const std::string& n : boardData.listLoad("netnotes")) {
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
    std::vector<std::string> lines = boardData.listLoad("axisnet"), before = lines;
    axis::bbs::AxisHeard     heard = axis::bbs::axisTake(reply, lines, axisSelfCard().host);
    if (!heard.ok) return;
    if (lines != before) {
        boardData.listSave("axisnet", lines);
        axisDirty = true;
        logEvent("axis net: directory updated");
    }
    uint32_t now = unixNow();
    for (const std::string& n : heard.notes) {
        boardData.listAdd("netnotes", std::to_string(now) + "|" + n);
        axisDirty = true;
    }
}

#if AXIS_NETS
// ------------------------------------------------------------------ Sysop Net (net/areas.h, net/session.h)
// This board's share of the board to board net. Only the main (or a board on its own) runs it: it starts from the
// loop once the cluster has settled and WiFi is up, and nothing of it exists on a follower or with net_sysop=0.
// Followers post through the main's message store, which is the NetPostingStore below. One session dialed in
// (AXISNET 2 on the telnet port, handed over by axisServe) and one dialed out at a time; the loop pumps both
// without blocking. Its files live under /net on the main's storage (the SD card; internal flash without one).
static constexpr uint32_t kNetHeapFloor = 40 * 1024;  // the largest free block a session needs to make progress
static constexpr uint32_t kNetSaveMs = 30000;         // the /net files are written at most this often
static constexpr uint32_t kNetPostDialMs = 10000;     // a local net post is dialed out this long after
static constexpr uint32_t kNetMinuteMs = 60000;       // card refresh, housekeeping, retrying the log
static constexpr uint32_t kNetDnsMs = 15000;          // a name lookup given up after this long
static constexpr size_t   kNetFeedPerTurn = 2048;     // bytes read from one session's socket per loop turn
static constexpr size_t   kNetDialOthers = 3;         // boards from the cards dialed besides the first peer

static std::string netPath(const std::string& name) { return std::string(storageRoot()) + "/net/" + name; }
static std::string netTmpPath(const std::string& name) {
    std::string p = netPath(name);
    size_t      dot = p.rfind('.');
    return (dot == std::string::npos ? p : p.substr(0, dot)) + ".tmp";
}

// A whole /net file (at most max bytes). A board stopped between removing the old file and the rename leaves only
// the temp file: that is taken as the file.
static std::string netFileRead(const std::string& name, size_t max = 64 * 1024) {
    std::string path = netPath(name), tmp = netTmpPath(name);
    struct stat st;
    if (::stat(path.c_str(), &st) != 0 && ::stat(tmp.c_str(), &st) == 0) ::rename(tmp.c_str(), path.c_str());
    std::string s;
    FILE*       f = std::fopen(path.c_str(), "rb");
    if (!f) return s;
    char   buf[256];
    size_t r;
    while (s.size() < max && (r = std::fread(buf, 1, sizeof buf, f)) > 0) s.append(buf, r);
    std::fclose(f);
    return s;
}

// Written whole to a temp file, then put in place of the old one (FAT's rename will not replace a file).
static bool netFileWrite(const std::string& name, const std::string& text) {
    std::string path = netPath(name), tmp = netTmpPath(name);
    FILE*       f = std::fopen(tmp.c_str(), "wb");
    if (!f) return false;
    bool ok = std::fwrite(text.data(), 1, text.size(), f) == text.size();
    ok = std::fclose(f) == 0 && ok;
    if (!ok) {
        ::unlink(tmp.c_str());
        return false;
    }
    ::unlink(path.c_str());
    return ::rename(tmp.c_str(), path.c_str()) == 0;
}

// /net/apps.txt, the signed User Net applications and votes (net/members.h holds only their compact fields and each
// line's byte offset): one line appended per record taken, read back a line at a time and never whole (it can reach
// about 140 KB).
static bool netAppsAppend(const std::string& line, uint32_t& off) {
    FILE* f = std::fopen(netPath("apps.txt").c_str(), "a+b");
    if (!f) return false;
    long at = std::fseek(f, 0, SEEK_END) == 0 ? std::ftell(f) : -1;
    bool ok = at >= 0;
    if (ok && at > 0 && std::fseek(f, -1, SEEK_END) == 0 && std::fgetc(f) != '\n') {  // a torn append: end it first
        ok = std::fseek(f, 0, SEEK_END) == 0 && std::fputc('\n', f) != EOF;
        at++;
    }
    ok = ok && std::fseek(f, 0, SEEK_END) == 0 && std::fwrite(line.data(), 1, line.size(), f) == line.size() && std::fputc('\n', f) != EOF;
    ok = std::fclose(f) == 0 && ok;
    if (ok) off = (uint32_t)at;
    return ok;
}

// The one line starting at byte off (without its line break); false if it cannot be read or is longer than a frame.
static bool netAppsReadAt(uint32_t off, std::string& line) {
    line.clear();
    FILE* f = std::fopen(netPath("apps.txt").c_str(), "rb");
    if (!f) return false;
    bool   ok = std::fseek(f, (long)off, SEEK_SET) == 0, end = false;
    char   buf[256];
    size_t r;
    while (ok && !end && (r = std::fread(buf, 1, sizeof buf, f)) > 0) {
        const char* nl = (const char*)std::memchr(buf, '\n', r);
        size_t      n = nl ? (size_t)(nl - buf) : r;
        if (line.size() + n > axis::net::kNetMaxFrame + 1) ok = false;
        else line.append(buf, n);
        end = nl != nullptr;
    }
    std::fclose(f);
    if (!line.empty() && line.back() == '\r') line.pop_back();
    return ok && !line.empty();
}

// The line starting at pos in f (no line break, no '\r' at its end), pos moved past it. A line longer than a frame
// comes back empty, like an empty one. False at the end of the file.
static bool netAppsNext(FILE* f, uint32_t& pos, std::string& line) {
    line.clear();
    bool any = false, over = false;
    for (int c; (c = std::fgetc(f)) != EOF;) {
        any = true;
        pos++;
        if (c == '\n') break;
        if (line.size() > axis::net::kNetMaxFrame) over = true;
        if (!over) line.push_back((char)c);
    }
    if (over) std::string().swap(line);
    if (!line.empty() && line.back() == '\r') line.pop_back();
    return any;
}

// Each line in order (no line break, no empty lines) until each returns false; a line longer than a frame is passed
// over, so the same file always gives the same lines at the same places.
static void netAppsScan(const axis::net::NetCore::AppsLineFn& each) {
    FILE* f = std::fopen(netPath("apps.txt").c_str(), "rb");
    if (!f) return;
    uint32_t    pos = 0;
    std::string line;
    while (netAppsNext(f, pos, line))
        if (!line.empty() && !each(line)) break;
    std::fclose(f);
}

// /net/files.idx, the Net Files listing index (NetCore::setListingIndex): byte ranges read and written in place. A
// take reads it in many pieces, so one handle stays open between calls (an open on the card each time cost more than
// the reads). It is closed after a failed read or write (the next call opens it again) and by netIdxClose wherever the
// file may change or go under it: a restart, the card looked at again or formatted, a serial file command.
static FILE* netIdxF = nullptr;
static void  netIdxClose() {
    if (netIdxF) std::fclose(netIdxF);
    netIdxF = nullptr;
}
static bool netIdxDone(bool ok) {
    if (!ok) netIdxClose();
    return ok;
}
static bool netIdxOpen(bool make) {  // a read never makes the file
    if (!netIdxF) netIdxF = std::fopen(netPath("files.idx").c_str(), make ? "w+b" : "r+b");
    return netIdxF != nullptr;
}
static bool netIdxRead(uint32_t off, void* p, size_t n) {
    return netIdxDone(netIdxOpen(false) && std::fseek(netIdxF, (long)off, SEEK_SET) == 0 && std::fread(p, 1, n, netIdxF) == n);
}
// A head write (offset 0) is synced to the card with the records before it: the head marks a change under way and
// ends it, so it must be there before the area changes and after the records are. Records alone are only flushed.
static bool netIdxWrite(uint32_t off, const void* p, size_t n) {
    if (!netIdxOpen(false) && !netIdxOpen(true)) return false;  // first use
    return netIdxDone(std::fseek(netIdxF, (long)off, SEEK_SET) == 0 && std::fwrite(p, 1, n, netIdxF) == n &&
                      std::fflush(netIdxF) == 0 && (off || ::fsync(fileno(netIdxF)) == 0));
}

// /net/outlog.txt for FileOutLog (net/outlog.h). Reads are mostly in order, so the line and byte offset the last
// read ended at are kept and the next read starts there. No file stays open between calls.
struct NetLogFile {
    std::string path, tmp;
    uint32_t    lines = 0;
    bool        counted = false;  // lines is the file's line count
    bool        broken = false;   // an append failed: no more until the log is opened again
    uint32_t    atLine = 0;       // the line that starts at byte atPos
    long        atPos = 0;

    uint32_t lineCount() {
        if (counted) return lines;
        lines = 0;
        atLine = 0;
        atPos = 0;
        struct stat st;
        if (::stat(path.c_str(), &st) != 0 && ::stat(tmp.c_str(), &st) == 0) ::rename(tmp.c_str(), path.c_str());
        if (::stat(path.c_str(), &st) != 0) {  // no log yet
            counted = true;
            return 0;
        }
        FILE* f = std::fopen(path.c_str(), "rb");
        if (!f) return 1;  // there but unreadable: open() then fails reading the header and tries again later
        char   buf[256], last = '\n';
        size_t r;
        while ((r = std::fread(buf, 1, sizeof buf, f)) > 0) {
            for (size_t i = 0; i < r; i++) lines += buf[i] == '\n';
            last = buf[r - 1];
        }
        bool err = std::ferror(f) != 0;
        std::fclose(f);
        if (err) return 1;
        if (last != '\n') {  // a line cut short by a reset: end it, so it reads as one junk entry
            FILE* a = std::fopen(path.c_str(), "ab");
            bool  ok = a && std::fputc('\n', a) != EOF;
            if (a) ok = std::fclose(a) == 0 && ok;
            if (!ok) return 1;
            lines++;
        }
        counted = true;
        return lines;
    }

    bool readLine(uint32_t n, std::string& out) {
        out.clear();
        if (!counted || n >= lines) return false;
        FILE* f = std::fopen(path.c_str(), "rb");
        if (!f) return false;
        if (n < atLine || std::fseek(f, atPos, SEEK_SET) != 0) {
            atLine = 0;
            atPos = 0;
            std::rewind(f);
        }
        int c = 0;
        while (atLine < n && (c = std::fgetc(f)) != EOF)
            if (c == '\n') atLine++;
        bool got = false;
        if (atLine == n)
            while ((c = std::fgetc(f)) != EOF) {
                if (c == '\n') {
                    got = true;
                    break;
                }
                if (out.size() <= axis::net::kSyncMaxLine) out.push_back((char)c);
            }
        if (got) {
            atLine = n + 1;
            atPos = std::ftell(f);
        } else {
            atLine = 0;
            atPos = 0;
            out.clear();
        }
        std::fclose(f);
        return got;
    }

    bool append(const std::string& l) {
        if (broken) return false;
        FILE* f = std::fopen(path.c_str(), "ab");
        if (!f) return false;  // nothing written: the next append may still work
        bool ok = std::fwrite(l.data(), 1, l.size(), f) == l.size() && std::fputc('\n', f) != EOF;
        ok = std::fclose(f) == 0 && ok;
        if (ok) lines++;
        else broken = true;  // maybe half a line: the loop opens the log again, which counts and ends it
        return ok;
    }

    bool replace(const std::string& header, uint32_t from) {
        FILE* out = std::fopen(tmp.c_str(), "wb");
        if (!out) return false;
        bool     ok = std::fputs(header.c_str(), out) >= 0 && std::fputc('\n', out) != EOF;
        uint32_t line = 0, kept = 1;
        if (FILE* in = std::fopen(path.c_str(), "rb")) {
            char   buf[256];
            size_t r;
            while (ok && (r = std::fread(buf, 1, sizeof buf, in)) > 0) {
                size_t i = 0;
                while (i < r && line < from)
                    if (buf[i++] == '\n') line++;
                if (i == r) continue;
                for (size_t k = i; k < r; k++) kept += buf[k] == '\n';
                ok = std::fwrite(buf + i, 1, r - i, out) == r - i;
            }
            if (std::ferror(in)) ok = false;
            std::fclose(in);
        }
        ok = std::fclose(out) == 0 && ok;
        if (!ok) {
            ::unlink(tmp.c_str());
            return false;
        }
        ::unlink(path.c_str());
        if (::rename(tmp.c_str(), path.c_str()) != 0) return false;  // the temp is taken as the log when it is opened
        lines = kept;
        counted = true;
        atLine = 0;
        atPos = 0;
        return true;
    }
};

enum class NetPhase { Idle, Looking, Live };
#if AXIS_FILES
// Net Files sharing: what waits to be listed or removed (an upload, a removal or rename here, @NET LISTALL), done one at a
// time from netTick. A listing first reads its file once, in kFetchChunk pieces across loop turns, for the crc.
struct NetShareItem {
    std::string area, name, desc, uploader;
    bool        gone = false, lookDesc = false;  // lookDesc: the description comes from the area's list
};
struct NetShare {
    std::vector<NetShareItem>              q;     // oldest first
    NetShareItem                           cur;   // the listing whose crc is being read
    std::unique_ptr<axis::bbs::RandomFile> file;  // its file while it is
    uint32_t                               size = 0, pos = 0, crc = 0xFFFFFFFFu;
    std::string                            allArea;     // @NET LISTALL: the area walked, "" when none
    size_t                                 allIx = 0;   // the next file in its list
    std::vector<uint32_t>                  allHave;     // crc32 of the names this board lists there already (skipped)
    uint32_t                               listed = 0, listedMs = 0;  // @NET status: live listings, counted at most once a minute
    bool                                   counted = false;
    bool                                   goneSent = false;  // the last removal was sent (a rename's new name goes with it)
};
#endif
#if AXIS_TRANSFERS
// The Net Files serve slot: one inbound fetch call (AXISFILE 1, handed over by axisServe), apart from the sync slots.
struct NetServe {
    WiFiClient                               c;
    std::unique_ptr<axis::net::FetchServer>  s;
    std::unique_ptr<axis::bbs::RandomFile>   file;  // the file being served (check opens it)
    std::string                              pend, id, who;  // who: "<handle>@<board>" once the GET is read
    std::string                              from;  // the calling board
    uint32_t                                 ip = 0, closeMs = 0, closeAt = 0;  // closeMs: the half-close, or the last byte heard after it; closeAt: the half-close
    bool                                     proven = false, served = false;  // the GET's signature checked; check passed
    bool                                     closing = false;  // half-closed: what comes is dropped until the dialer closes
    // noexcept (WiFiClient() cannot throw): NetHost's constructor then needs no unwinding code for the members before it.
    NetServe() noexcept {}
};
// The Net Files fetch slot: one outbound fetch call at a time for a caller here (or a follower's, or @NET FETCHTEST).
enum class FetchPh : uint8_t { Idle, Looking, Live, Done, Failed };
struct NetFetchSlot {
    WiFiClient                               c;
    std::unique_ptr<axis::net::FetchClient>  f;
    axis::net::NetListing                    l;
    std::string                              homePub, handle, host, home, pend;  // home: the home board's name, for messages
    int                                      port = 23;
    FetchPh                                  ph = FetchPh::Idle;
    const void*                              owner = nullptr;
    const void*                              job = nullptr;  // the TransferJob reading it (netFetchJob)
    uint32_t                                 gen = 0, dnsGen = 0, ip = 0, at = 0;  // at: the offset this call asked from
    uint32_t                                 touchMs = 0, tickMs = 0, slowMs = 0, slowFed = 0, fed = 0, fedNow = 0;
    uint32_t                                 redialAt = 0;
    int                                      retries = 0;
    bool                                     redial = false, forward = false;
    const char*                              why = "";
    NetFetchSlot() noexcept {}  // (as NetServe)
};
#endif
struct NetSlot {
    WiFiClient                              c;
    std::unique_ptr<axis::net::NetSession>  s;
    std::string                             pend;  // taken from the session, not yet taken by the socket
    NetPhase                                phase = NetPhase::Idle;
    std::string                             host, expect;  // dialing: where, and the board meant
    int                                     port = 23;
    bool                                    first = false;  // dialing the first peer
    String                                  who;            // for the event log until the peer is known
    uint32_t                                dnsGen = 0, startMs = 0;
    uint32_t                                ip = 0;  // inbound: the caller's address (the proof back-off)
};
struct NetHost {
    NetLogFile                              file;
    std::unique_ptr<axis::net::FileOutLog>  log;
    std::unique_ptr<axis::net::NetCore>     core;
    std::unique_ptr<axis::net::NetPostingStore> store;  // the board's message store with the net on top
    axis::net::KeyPair                      key;   // this board's key, or Rev's from /netrev.key
    axis::net::NetCard                      card;  // this board's card, signed, as held in the state
    uint32_t                                cardSeq = 0;
    NetSlot                                 in, out;
    std::string                             firstKey, firstBoard;  // /net/firstpeer.txt: "host:port", its board
    bool                                    logSaid = false, dirty = false, postDue = false;
    uint32_t                                logTryMs = 0, minuteMs = 0, saveMs = 0, dialMs = 0, postMs = 0;
    uint32_t                                postSeq = 0, appendFails = 0, appsFails = 0, houseDay = 0;
    bool                                    seqFloorDue = false;  // no counters found and no clock: floored once it is set
    bool                                    cardSeqFree = false;  // cardSeq was saved but no card went out with it
    uint32_t                                crc[3] = {0, 0, 0};  // what state, seen and cursors last wrote
    size_t                                  dialIx = 0;
    std::unique_ptr<axis::net::NetRecord>   record;         // the net record (/net/record)
    std::vector<std::string>                hides, blocks;  // /net/local.txt: this board's own moderation
    uint32_t                                trimMs = 0;     // no clock: the last trim by count (once per 24 h of uptime)
    // User Net: the next vote and application seqs (/netseq.txt); the boot load of /net/apps.txt (read from appsPos a
    // few lines a pass, appsLines lines of appsBytes bytes); appsHeld, about the bytes of held lines (compaction).
    uint32_t                                voteSeq = 1, appSeq = 1, appsPos = 0, appsLines = 0, appsBytes = 0, appsHeld = 0;
    bool                                    appsLoaded = false;
    bool                                    unDirty = false, unMember = false, sealSaid = false;  // an app, vote, card or notice taken; this board a member
    bool                                    cfgUser = false;  // net_user, net_user_areas and net_user_level as last applied
    uint8_t                                 cfgAreas = 0, carried = 0;  // (masks: userNetMask) the setting, what core carries
    int                                     cfgLevel = 0;
#if AXIS_FILES
    NetShare                                share;
#endif
#if AXIS_TRANSFERS
    NetServe                                serve;
    NetFetchSlot                            fetch;
#endif
    NetHost() = default;
    NetHost(const NetHost&) = delete;
    NetHost& operator=(const NetHost&) = delete;
    ~NetHost() {  // the sessions and the core go first (they hold copies of the key and wipe them), then this one
        in.s.reset();
        out.s.reset();
#if AXIS_TRANSFERS
        serve.s.reset();
        fetch.f.reset();
#endif
        store.reset();
        core.reset();
        axis::net::wipe(key);
    }
};
static std::unique_ptr<NetHost> netHost;
static uint32_t                 netStartMs = 0;  // the last start attempt (a failed one is tried again in 10 min)
static bool                     netStartSaid = false;

static axis::bbs::MessageStore* netMessages() { return netHost ? netHost->store.get() : nullptr; }
static bool netMainUp = false;  // a follower: the main's settings frame said its net is up ("net_up=1")
static bool netMainFiles = false;  // a follower: the main answers Net Files for its callers ("net_files=1")
static bool netSharedUp = false;  // the main: what the last settings frame said
// The User Net areas callers get now: on the main while this board is a member with net_user on, on a
// follower what the main's settings frame said ("net_un="). netAreasGen moves whenever they or net_user_level may have.
static uint8_t     netUnMask = 0;  // (bbs/config.h userNetMask)
static uint32_t    netAreasGen = 0;

// Proof back-off: an address whose AXISNET 2 session failed before it proved who it is is refused for 60 s,
// doubling up to 30 min for repeats. Cooldown: an address whose session came from a board not trusted at its hello
// is refused for 10 min after it (that session had 2 min, 10 with a history pull), so one stranger cannot keep the inbound slot.
// 8 addresses; the one touched longest ago makes room.
static constexpr uint32_t kNetCooldownSecs = 600;
struct NetBackoff {
    uint32_t ip = 0, atMs = 0, secs = 0;
    bool     said = false;  // its refusal logged in this back-off
    bool     cool = false;  // a cooldown, not a proof back-off
};
static NetBackoff netBackoff[8];
static NetBackoff* netBackoffFind(uint32_t ip, bool make) {
    NetBackoff* old = &netBackoff[0];
    for (NetBackoff& b : netBackoff) {
        if (b.secs && b.ip == ip) return &b;
        if (!b.secs) old = &b;
        else if (old->secs && millis() - b.atMs > millis() - old->atMs) old = &b;
    }
    if (!make) return nullptr;
    *old = NetBackoff();
    old->ip = ip;
    return old;
}
static bool netBackedOff(uint32_t ip) {
    NetBackoff* b = netBackoffFind(ip, false);
    if (!b || millis() - b->atMs >= b->secs * 1000u) return false;
    if (!b->said)
        logEvent("net: " + IPAddress(ip).toString() + " refused for " + String(b->secs) + " s" +
                 (b->cool ? " (new board, cooling down)" : " (no proof last time)"));
    b->said = true;
    return true;
}
static void netStrike(uint32_t ip) {
    NetBackoff* b = netBackoffFind(ip, true);
    b->secs = b->secs && !b->cool ? std::min<uint32_t>(b->secs * 2, 1800) : 60;
    b->atMs = millis();
    b->said = false;
    b->cool = false;
}
static void netCooldown(uint32_t ip) {
    NetBackoff* b = netBackoffFind(ip, true);
    b->secs = kNetCooldownSecs;
    b->atMs = millis();
    b->said = false;
    b->cool = true;
}
static void netProven(uint32_t ip) {
    if (NetBackoff* b = netBackoffFind(ip, false)) *b = NetBackoff();
}

// Boards the sysop blocked here (@NETBLOCK). They are never dialed, their sessions are refused and their posts rejected.
static bool netBlocked(const std::string& board) {
    return netHost && std::find(netHost->blocks.begin(), netHost->blocks.end(), board) != netHost->blocks.end();
}

static bool netHeapOk() {
    if (heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) < kNetHeapFloor) return false;
    using S = axis::bbs::Node::State;
    for (size_t i = 0; i < sessions.size(); i++)
        if (axis::bbs::Node* n = sessionNode(i))
            if (n->state() == S::Transfer) return false;
    if (localNode && localNode->state() == S::Transfer) return false;
    if (serialWfc && serialWfc->state() == S::Transfer) return false;
    return true;
}

static std::string netNonce() {
    uint8_t b[8];
    for (size_t i = 0; i < sizeof b; i += 4) {
        uint32_t r = esp_random();
        std::memcpy(b + i, &r, 4);
    }
    return axis::net::toHex(b, sizeof b);
}

// 64 hex characters at path into seed. The text goes through a buffer wiped after.
static bool netSeedRead(const char* path, uint8_t seed[32]) {
    char hex[64];
    File f = LittleFS.open(path, "r");
    if (!f) return false;
    bool ok = f.read((uint8_t*)hex, sizeof hex) == sizeof hex;
    f.close();
    for (size_t i = 0; ok && i < 32; i++) {
        int v = 0;
        for (int j = 0; ok && j < 2; j++) {
            char c = hex[2 * i + j];
            int  d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
            ok = d >= 0;
            v = v * 16 + d;
        }
        seed[i] = (uint8_t)v;
    }
    crypto_wipe(hex, sizeof hex);
    if (!ok) crypto_wipe(seed, 32);
    return ok;
}

// A new seed written as hex to dest, /netkey.txt or /netrev.key (a temp file, then renamed), through a buffer wiped after.
static bool netSeedWrite(const uint8_t seed[32], const char* dest = "/netkey.txt") {
    static const char kHex[] = "0123456789abcdef";
    char              hex[65];
    for (size_t i = 0; i < 32; i++) {
        hex[2 * i] = kHex[seed[i] >> 4];
        hex[2 * i + 1] = kHex[seed[i] & 15];
    }
    hex[64] = '\n';
    File f = LittleFS.open("/netkey.tmp", "w");
    bool ok = f && f.write((const uint8_t*)hex, sizeof hex) == sizeof hex;
    if (f) f.close();
    crypto_wipe(hex, sizeof hex);
    if (ok && !LittleFS.rename("/netkey.tmp", dest)) ok = LittleFS.remove(dest) && LittleFS.rename("/netkey.tmp", dest);
    if (!ok) LittleFS.remove("/netkey.tmp");
    return ok;
}

// The board's key. /netrev.key (Rev's seed, written by a sysop command) wins; else /netkey.txt, made on first boot
// from esp_random(). Both hold 64 hex characters in internal flash, which survives reflashes. Never printed.
static bool netKeyLoad(axis::net::KeyPair& k) {
    uint8_t seed[32];
    bool    have = false;
    if (LittleFS.exists("/netrev.key")) have = netSeedRead("/netrev.key", seed);  // a bad one keeps the net off: no fallback
    else if (LittleFS.exists("/netkey.txt")) have = netSeedRead("/netkey.txt", seed);  // likewise, never replaced
    else {  // first boot
        for (size_t i = 0; i < sizeof seed; i += 4) {
            uint32_t r = esp_random();
            std::memcpy(seed + i, &r, 4);
        }
        have = netSeedWrite(seed);
    }
    if (have) k = axis::net::keyFromSeed(seed);
    crypto_wipe(seed, sizeof seed);
    return have;
}

// /netseq.txt in internal flash, beside the key: "<next post seq> <card seq> <next vote seq> <next app seq>", then " f"
// while the counters still wait to be floored from the clock (none were found and there was no clock), so a restart
// before the clock is set still floors them. An older file has only the first two (n says how many were read). It lives
// there, not under /net, so a board that boots without its SD card keeps its counters with its key (peers drop a
// reused post id as seen and refuse a card whose seq is not newer). Malformed reads as missing.
static bool netSeqRead(uint32_t v[4], int& n, bool& floorDue) {
    File f = LittleFS.open("/netseq.txt", "r");
    if (!f) return false;
    char   b[64];
    size_t got = f.read((uint8_t*)b, sizeof b - 1);
    f.close();
    b[got] = 0;
    char* p = b;
    for (n = 0; n < 4 && std::isdigit((unsigned char)*p); n++) {
        v[n] = (uint32_t)std::strtoul(p, &p, 10);
        if (*p == ' ' && std::isdigit((unsigned char)p[1])) p++;
    }
    floorDue = p[0] == ' ' && p[1] == 'f';
    if (floorDue) p += 2;
    return n >= 2 && v[0] && (!*p || *p == '\n');
}
// Written to a temp file, then renamed over the old one (LittleFS replaces it in one step).
static bool netSeqWrite(uint32_t post, uint32_t card, uint32_t vote, uint32_t app, bool floorDue) {
    std::string t = std::to_string(post) + " " + std::to_string(card) + " " + std::to_string(vote) + " " + std::to_string(app) +
                    (floorDue ? " f\n" : "\n");
    File        f = LittleFS.open("/netseq.tmp", "w");
    bool        ok = f && f.write((const uint8_t*)t.data(), t.size()) == t.size();
    if (f) f.close();
    if (ok && !LittleFS.rename("/netseq.tmp", "/netseq.txt")) ok = LittleFS.remove("/netseq.txt") && LittleFS.rename("/netseq.tmp", "/netseq.txt");
    if (!ok) {
        LittleFS.remove("/netseq.tmp");
        static bool said = false;
        if (!said) logEvent("net: /netseq.txt could not be written, no post or card change until it can");
        said = true;
    }
    return ok;
}

// A card field: printable, at most max bytes (record.h refuses control characters and long fields).
static std::string netField(const std::string& s, size_t max) {
    std::string o;
    for (char c : s)
        if ((unsigned char)c >= 0x20 && c != 0x7f && o.size() < max) o.push_back(c);
    return o;
}

static axis::net::NetCard netCardNow(const NetHost& h) {
    axis::bbs::AxisCard self = axisSelfCard();  // the public host and port Axis Net already gives out
    axis::net::NetCard  c;
    c.pub = axis::net::toHex(h.key.pub, 32);
    c.board = axis::net::boardId(c.pub);
    c.name = netField(boardCfg->netName.empty() ? boardCfg->bbsName : boardCfg->netName, 40);
    c.host = netField(self.host, 64);
    c.port = self.port;
    c.mac = netField(WiFi.macAddress().c_str(), 17);
    c.sysop = netField(boardCfg->netSysopName.empty() ? boardCfg->sysopHandle : boardCfg->netSysopName, 40);
    c.location = netField(boardCfg->netLocation, 40);
    c.links = axis::net::kNetCardLinks;
    return c;
}

static bool netSameCard(const axis::net::NetCard& a, const axis::net::NetCard& b) {
    return a.board == b.board && a.pub == b.pub && a.name == b.name && a.host == b.host && a.port == b.port &&
           a.mac == b.mac && a.sysop == b.sysop && a.location == b.location && a.links == b.links;
}

// The card held in the state when nothing changed; else a new one, its seq one past any used before, signed.
static bool netCardUpdate(NetHost& h) {
    axis::net::NetCard        c = netCardNow(h);
    const axis::net::NetCard* held = h.core->state().card(c.board);
    // Unchanged and at the saved seq: kept. A held card below the saved seq (the counter was floored from the clock,
    // or the state was lost) is made again past it, so peers that hold a newer one take it.
    if (held && netSameCard(*held, c) && held->seq >= h.cardSeq) {
        if (h.card.seq != held->seq) h.card = *held;
        return true;
    }
    if (h.core->state().banned(c.board)) return false;  // no card is taken for a banned board: nothing to save
    // The seq: one saved earlier that no card went out with is used again; else one past any used before.
    uint32_t out = std::max(h.card.seq, held ? held->seq : 0u);
    c.seq = h.cardSeqFree && h.cardSeq > out ? h.cardSeq : std::max(h.cardSeq, out) + 1;
    static bool said = false;
    if (!axis::net::signCard(c, h.key)) {  // fields it refuses: nothing saved, nothing sent
        if (!said) logEvent("net: this board's card could not be made (check net_name, net_sysop_name, net_location)");
        said = true;
        return false;
    }
    if (c.seq != h.cardSeq) {  // saved before the card goes anywhere
        if (!netSeqWrite(h.core->postSeq(), c.seq, h.voteSeq, h.appSeq, h.seqFloorDue)) return false;
        h.cardSeq = c.seq;
    }
    h.cardSeqFree = true;
    if (!h.core->setOwnCard(c)) {
        if (!said) logEvent("net: this board's card could not be made (check net_name, net_sysop_name, net_location)");
        said = true;
        return false;
    }
    h.cardSeqFree = false;
    h.card = c;
    h.dirty = true;
    return true;
}

// No /netseq.txt at start and no clock then: once the clock is set, both counters go on from it at least (a lost
// file never brings back a used seq). Before any post or card is made.
static void netSeqFloor(NetHost& h) {
    uint32_t now = unixNow();
    if (!h.seqFloorDue || now <= axis::net::kNetClockSet) return;
    uint32_t post = std::max(h.core->postSeq(), now), card = std::max(h.cardSeq, now), vote = std::max(h.voteSeq, now), app = std::max(h.appSeq, now);
    if (!netSeqWrite(post, card, vote, app, false)) return;
    h.core->setPostSeq(post);
    h.cardSeq = card;
    h.voteSeq = vote;
    h.appSeq = app;
    h.cardSeqFree = false;
    h.seqFloorDue = false;
}

// net_first_peer: "host:port" or "host:port@<board>". The board is the @ part, else the one remembered from the
// first session with that host:port, else "" (trust on first use).
struct NetTarget {
    std::string host, board;
    int         port = 23;
    bool        first = false;
};
static bool netFirstTarget(const NetHost& h, NetTarget& t) {
    auto trim = [](std::string x) {
        while (!x.empty() && (x.back() == ' ' || x.back() == '\t')) x.pop_back();
        size_t b = x.find_first_not_of(" \t");
        return b == std::string::npos ? std::string() : x.substr(b);
    };
    std::string v = boardCfg->netFirstPeer, at;
    size_t      a = v.find('@');
    if (a != std::string::npos) {
        at = trim(v.substr(a + 1));
        v.resize(a);
        for (char& c : at) c = (char)std::tolower((unsigned char)c);
    }
    v = trim(v);
    if (v.empty()) return false;
    size_t colon = v.rfind(':');
    t.host = trim(colon == std::string::npos ? v : v.substr(0, colon));
    t.port = colon == std::string::npos ? 23 : std::atoi(v.c_str() + colon + 1);
    if (t.host.empty() || t.port <= 0 || t.port > 65535) return false;
    t.first = true;
    t.board.clear();
    if (at.size() == 16 && at.find_first_not_of("0123456789abcdef") == std::string::npos) t.board = at;
    else if (h.firstKey == t.host + ":" + std::to_string(t.port)) t.board = h.firstBoard;
    return true;
}

// Who to dial: the first peer always, and up to three other boards whose cards give a host (link peers first).
// Never this board, a banned board or one blocked here.
static std::vector<NetTarget> netTargets(const NetHost& h) {
    std::vector<NetTarget> v;
    NetTarget              f;
    bool                   haveFirst = netFirstTarget(h, f);
    if (haveFirst && !netBlocked(f.board)) v.push_back(f);
    const axis::net::NetState& st = h.core->state();
    for (int pass = 0; pass < 2; pass++)
        st.forEachCard([&](const axis::net::NetCard& c) {
            if (v.size() >= (haveFirst ? 1u : 0u) + kNetDialOthers) return;
            if (c.host.empty() || c.port <= 0 || c.board == h.core->board() || (haveFirst && c.board == f.board)) return;
            if (st.banned(c.board) || netBlocked(c.board) || h.core->isLinkPeer(c.board) != (pass == 0)) return;
            NetTarget t;
            t.host = c.host;
            t.port = c.port;
            t.board = c.board;
            v.push_back(t);
        });
    return v;
}

// A card's name for the event log. Names come from other boards, so one that would read as scene news
// (bbs/announce.h, " uploaded ") is left out.
static String netCardName(const axis::net::NetCard* c) {
    if (!c || c->name.empty() || c->name.find(" uploaded ") != std::string::npos) return String();
    return String(c->name.c_str());
}

// Who a session is with, for the event log: the peer's card name and board id, else what was dialed.
static String netWho(const NetHost& h, const NetSlot& s) {
    std::string peer = s.s ? s.s->peer() : std::string();
    if (peer.empty() && !s.expect.empty()) peer = s.expect;
    if (peer.empty()) return s.who;
    const axis::net::NetCard* c = h.core->state().card(peer);
    String                    name = netCardName(c);
    return name.length() ? name + " (" + peer.c_str() + ")" : String(peer.c_str());
}

// What the socket takes now, never waiting. False when the socket is gone.
static bool netSendRaw(WiFiClient& c, std::string& pend) {
    while (!pend.empty()) {
        int fd = c.fd();
        if (fd < 0) return false;
        int w = ::send(fd, pend.data(), pend.size(), MSG_DONTWAIT);
        if (w < 0) return errno == EAGAIN || errno == EWOULDBLOCK;
        if (w == 0) return true;
        pend.erase(0, (size_t)w);
    }
    return true;
}
static bool netSendSome(NetSlot& s) { return netSendRaw(s.c, s.pend); }

static void netSaveFirstPeer(NetHost& h, const std::string& board) {
    NetTarget t;
    if (!netFirstTarget(h, t)) return;
    std::string key = t.host + ":" + std::to_string(t.port);
    if (h.firstKey == key && h.firstBoard == board) return;
    h.firstKey = key;
    h.firstBoard = board;
    h.core->setFirstPeer(board);
    if (!netFileWrite("firstpeer.txt", key + " " + board + "\n")) logEvent("net: firstpeer.txt SAVE FAILED");
}

// Ends a session: what it still has to say goes out first (briefly), then the socket closes.
static void netClose(NetHost& h, NetSlot& s, const char* why) {
    if (s.s) s.pend += s.s->takeOut();
    for (uint32_t t0 = millis(); !s.pend.empty() && s.c.connected() && millis() - t0 < 200;) {
        if (!netSendSome(s)) break;
        if (!s.pend.empty()) delay(2);
    }
    s.c.stop();
    if (s.s) {
        bool   ok = s.s->done();
        String what = why ? why : s.s->why();
        logEvent(String("net: ") + (ok ? "synced with " : "sync failed with ") + netWho(h, s) +
                 (what.length() ? String(" (") + what + ")" : String()) + ", " + String(s.s->postsSent()) + " out, " +
                 String(s.s->postsTaken()) + " in" + heapText());
        if (ok && s.first && !s.s->peer().empty()) netSaveFirstPeer(h, s.s->peer());
        if (h.record) h.record->note(s.s->peer().empty() ? std::string("-") : s.s->peer(), std::string(ok ? "session ok " : "session failed ") + what.c_str(), std::string());
        bool blocked = what == "blocked";  // refused by this board's own block list: no back-off for that
        if (&s == &h.in && s.s->proven() && !s.s->wasTrusted()) netCooldown(s.ip);  // a new board: 10 min before the next
        else if (&s == &h.in && s.s->proven()) netProven(s.ip);
        else if (&s == &h.in && !blocked && !(why && !std::strcmp(why, "net off"))) netStrike(s.ip);  // never proved who it is
    }
    s.s.reset();
    s.pend.clear();
    s.pend.shrink_to_fit();
    s.c = WiFiClient();
    s.phase = NetPhase::Idle;
    s.host.clear();
    s.expect.clear();
    s.first = false;
    h.dirty = true;
}

// One turn of a session: read what is there and feed it, send what it says, then tick it.
static void netPump(NetHost& h, NetSlot& s, bool heapOk) {
    if (s.phase != NetPhase::Live || !s.s) return;
    char   buf[512];
    size_t fed = 0;
    // Short of heap nothing is read: a received line costs a few copies of itself (up to 9 KB each), so it waits
    // in the socket and TCP holds the peer back. The session's silence timer stands still meanwhile.
    while (heapOk && !s.s->parked() && fed < kNetFeedPerTurn) {  // (parked: a listing line waits for the index)
        int a = s.c.available();
        if (a <= 0) break;
        int r = s.c.read((uint8_t*)buf, (size_t)a < sizeof buf ? (size_t)a : sizeof buf);
        if (r <= 0) break;
        s.s->feed(buf, (size_t)r);
        fed += (size_t)r;
    }
    bool sent;
    if (s.pend.empty()) s.pend = s.s->takeOut();  // while the socket lags, the rest waits in the session (its own limit)
    sent = netSendSome(s);
    s.s->tick(millis(), heapOk);
    if (sent && s.pend.empty()) {
        s.pend = s.s->takeOut();
        sent = netSendSome(s);
    }
    if (s.s->done() || s.s->failed()) netClose(h, s, nullptr);
    else if (s.s->parked() && !s.c.connected()) return;  // the peer closed: what it sent, parked, is handled first
    else if (!sent || (!s.c.connected() && s.c.available() <= 0)) netClose(h, s, "connection closed");
}

static volatile uint32_t netDnsGen = 0, netDnsDone = 0, netDnsAddr = 0;
static void netDnsFound(const char*, const ip_addr_t* a, void* arg) {  // lwIP's thread: only a stale lookup is ignored
    uint32_t gen = (uint32_t)(uintptr_t)arg;
    if (gen != netDnsGen) return;
    netDnsAddr = a && IP_IS_V4(a) ? ip_2_ip4(a)->addr : 0;
    netDnsDone = gen;
}

// Dials the outbound slot's host at ip (the connect waits 1.2 s at most), and starts the session.
static void netConnect(NetHost& h, NetSlot& s, IPAddress ip) {
    if (!s.c.connect(ip, (uint16_t)s.port, 1200)) {
        logEvent(String("net: no answer from ") + s.who);
        s.phase = NetPhase::Idle;
        s.c = WiFiClient();
        return;
    }
    s.c.setNoDelay(true);
    s.s.reset(new (std::nothrow) axis::net::NetSession(*h.core, h.card, h.key, netNonce(), true, s.expect));
    if (!s.s) {
        s.c.stop();
        s.c = WiFiClient();
        s.phase = NetPhase::Idle;
        return;
    }
    s.startMs = millis();
    s.phase = NetPhase::Live;
    s.pend = s.s->takeOut();  // the hello goes now: the other board's axisServe waits only 40 ms for a first byte
    netSendSome(s);
}

// Starts dialing a board: an address connects now; a name is looked up first, without waiting (netDialStep).
static void netDial(NetHost& h, const NetTarget& t) {
    NetSlot& s = h.out;
    s.host = t.host;
    s.port = t.port;
    s.expect = t.board;
    s.first = t.first;
    const axis::net::NetCard* c = t.board.empty() ? nullptr : h.core->state().card(t.board);
    s.who = netCardName(c);
    if (!s.who.length()) s.who = t.first ? String("the first peer") : String(t.board.c_str());
    IPAddress ip;
    if (ip.fromString(t.host.c_str())) {
        netConnect(h, s, ip);
        return;
    }
    ip_addr_t a;
    uint32_t  gen = ++netDnsGen;
    err_t     e = dns_gethostbyname(t.host.c_str(), &a, netDnsFound, (void*)(uintptr_t)gen);  // as WiFi.hostByName does
    if (e == ERR_OK && IP_IS_V4(&a)) {
        netConnect(h, s, IPAddress(ip_2_ip4(&a)->addr));
        return;
    }
    if (e != ERR_INPROGRESS) {
        logEvent(String("net: could not look up ") + s.who);
        s.phase = NetPhase::Idle;
        return;
    }
    s.dnsGen = gen;
    s.startMs = millis();
    s.phase = NetPhase::Looking;
}

static void netDialStep(NetHost& h) {
    NetSlot& s = h.out;
    if (s.phase != NetPhase::Looking) return;
    if (netDnsDone == s.dnsGen) {
        uint32_t addr = netDnsAddr;
        if (addr) netConnect(h, s, IPAddress(addr));
        else {
            logEvent(String("net: could not look up ") + s.who);
            s.phase = NetPhase::Idle;
        }
    } else if (millis() - s.startMs > kNetDnsMs) {
        netDnsGen++;  // a late answer is ignored
        logEvent(String("net: could not look up ") + s.who);
        s.phase = NetPhase::Idle;
    }
}

// axisServe read "AXISNET 2 ": the rest of the hello comes from the socket. One inbound session at a time.
static void netAccept(WiFiClient& c, const std::string& got) {
    NetHost* h = netHost.get();
    uint32_t ip = (uint32_t)c.remoteIP();
    if (netBackedOff(ip) || !h || !h->appsLoaded || !boardCfg->netSysop || asFollower() || h->in.phase != NetPhase::Idle || h->out.phase != NetPhase::Idle ||
        !netHeapOk()) {  // one session at a time, in or out
        c.print("BYE busy\n");
        delay(10);
        c.stop();
        return;
    }
    NetSlot& s = h->in;
    s.c = c;
    s.c.setNoDelay(true);
    s.s.reset(new (std::nothrow) axis::net::NetSession(*h->core, h->card, h->key, netNonce(), false, std::string()));
    if (!s.s) {
        s.c.print("BYE busy\n");
        s.c.stop();
        s.c = WiFiClient();
        return;
    }
    s.who = c.remoteIP().toString();
    s.ip = ip;
    s.first = false;
    s.startMs = millis();
    s.phase = NetPhase::Live;
    s.s->feed(got.data(), got.size());
}

// A post from here (its counter was saved before it was stored, NetCore::setSeqReserve): a dial is due in 10 s.
static void netPosted(NetHost& h) {
    if (h.core->postSeq() == h.postSeq) return;
    h.postSeq = h.core->postSeq();
    h.postDue = true;
    h.postMs = millis();
    h.dirty = true;
}
// The board's store with the net on top; the counter is saved as the post is made.
struct NetStore : axis::net::NetPostingStore {
    using axis::net::NetPostingStore::NetPostingStore;
    int post(const std::string& area, const axis::bbs::Message& m) override {
        if (netHost && axis::net::isNetAreaId(area)) {
            // A caller with net posts off, or a twit, is refused before anything is signed. The board's own sysop is
            // no account in the user store: let through by handle, only when no account has that handle.
            const std::string& sysop = boardCfg->sysopHandle.empty() ? std::string("Sysop") : boardCfg->sysopHandle;
            const axis::bbs::User* u = users.find(m.from);
            if (u ? !axis::net::mayPostToNets(u) : strcasecmp(m.from.c_str(), sysop.c_str()) != 0) return 0;
            netSeqFloor(*netHost);
        }
        int n = axis::net::NetPostingStore::post(area, m);
        if (netHost) netPosted(*netHost);
        return n;
    }
};

// The FileOutLog on /net/outlog.txt; until it opens, nothing new is passed on (tried again every minute).
static void netLogOpen(NetHost& h) {
    h.logTryMs = millis();
    h.file.counted = false;
    h.file.broken = false;
    bool ok = h.log->open();
    if (!ok && !h.logSaid) logEvent("net: the outbound log (/net/outlog.txt) could not be read, trying every minute");
    else if (ok && h.logSaid) logEvent("net: the outbound log is open again");
    h.logSaid = !ok;
}

// The /net files that changed since they were last written (state, seen posts, link cursors). The counters are not
// here: they go to /netseq.txt before they are used.
static bool netSave(NetHost& h, bool force = false) {
    if (!force && heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) < kNetHeapFloor) return false;  // next time
    static const char* const kNames[3] = {"state.txt", "seen.txt", "cursors.txt"};
    bool                     ok = true;
    for (int i = 0; i < 3; i++) {
        std::string text = i == 0 ? h.core->state().save() : i == 1 ? h.core->seen().save() : h.core->saveCursors();
        uint32_t crc = axis::net::crc32((const uint8_t*)text.data(), text.size());
        if (crc == h.crc[i]) continue;
        if (netFileWrite(kNames[i], text)) h.crc[i] = crc;
        else ok = false;
    }
    if (!ok) {
        static bool said = false;
        if (!said) logEvent("net: the /net files could not be written");
        said = true;
    }
    return ok;
}

// Before a clean restart.
static void netSaveNow() {
    if (netHost) netSave(*netHost, true);
    netIdxClose();
}
// /net/files.idx may change or go under the board (a card swap, a serial file command, a format): the handle is
// closed and NetCore forgets the index, so its next look checks the head and the area's tail again.
static void netIdxForget() {
    netIdxClose();
    if (netHost && netHost->core) netHost->core->listingIndexForget();
}

// ---- the net record, local moderation and the @NET commands
static constexpr size_t kNetRecordCap = 64 * 1024;  // a main without a card: one record file, the newest half kept
static constexpr size_t kNetMaxHides = 200, kNetMaxBlocks = 64;

static bool netHas(const std::vector<std::string>& v, const std::string& s) { return std::find(v.begin(), v.end(), s) != v.end(); }
static bool netIsBoard(const std::string& s) { return s.size() == 16 && s.find_first_not_of("0123456789abcdef") == std::string::npos; }
static bool netIsPostId(const std::string& s) {
    if (s.size() < 18 || s.size() > 27 || s[16] != '-' || !netIsBoard(s.substr(0, 16)) || s[17] == '0') return false;
    return s.find_first_not_of("0123456789", 17) == std::string::npos;
}

static std::string netYyyymm() {
    uint32_t now = unixNow();
    if (now <= axis::net::kNetClockSet) return "nodate";
    time_t    t = (time_t)now;
    struct tm tm;
    localtime_r(&t, &tm);
    char b[12];
    snprintf(b, sizeof b, "%04d%02d", tm.tm_year + 1900, tm.tm_mon + 1);
    return b;
}

// The record file being written now: /net/record/<yyyymm>.log on the SD card, else the one capped /net/record.log.
static std::string netRecordFile() {
    return std::string(storageRoot()) + (storageHasSd() ? "/net/record/" + netYyyymm() + ".log" : std::string("/net/record.log"));
}

// Internal flash only: the file cut to its newest half (from a line start), through a temp file. A failed cut removes it.
static bool netRecordHalve(const std::string& file, long size) {
    std::string tmp = std::string(storageRoot()) + "/net/record.tmp";
    FILE*       in = std::fopen(file.c_str(), "rb");
    FILE*       out = in ? std::fopen(tmp.c_str(), "wb") : nullptr;
    bool        ok = out && std::fseek(in, size / 2, SEEK_SET) == 0;
    int         c;
    while (ok && (c = std::fgetc(in)) != EOF && c != '\n') {}
    char   buf[256];
    size_t r;
    while (ok && (r = std::fread(buf, 1, sizeof buf, in)) > 0) ok = std::fwrite(buf, 1, r, out) == r;
    if (in) std::fclose(in);
    if (out) ok = std::fclose(out) == 0 && ok;
    // LittleFS rename replaces the target atomically (esp_littlefs calls lfs_rename, no remove first), so a reset
    // here leaves either the old file or the new one, never neither. A failed cut removes the file: never past the cap.
    if (ok && ::rename(tmp.c_str(), file.c_str()) == 0) return true;
    ::unlink(tmp.c_str());
    ::unlink(file.c_str());
    return false;
}

static bool netRecordAppend(const std::string& path, const std::string& line) {
    std::string file = std::string(storageRoot()) + (storageHasSd() ? path : std::string("/net/record.log"));
    struct stat st;
    if (storageHasSd()) ::mkdir((std::string(storageRoot()) + "/net/record").c_str(), 0777);
    else if (::stat(file.c_str(), &st) == 0 && (size_t)st.st_size + line.size() + 1 > kNetRecordCap && !netRecordHalve(file, (long)st.st_size))
        return false;
    FILE* f = std::fopen(file.c_str(), "ab");
    if (!f) return false;
    bool ok = std::fwrite(line.data(), 1, line.size(), f) == line.size() && std::fputc('\n', f) != EOF;
    return std::fclose(f) == 0 && ok;
}

// The record's newest lines, newest first, skip lines in: their head only ("<unix> <who> <what>", at most 120
// characters). Read backwards in small blocks; a month's file is never loaded whole.
static void netRecordTail(size_t skip, size_t count, std::vector<std::string>& out) {
    std::string path = netRecordFile();
    FILE*       f = std::fopen(path.c_str(), "rb");
    if (!f) return;
    std::fseek(f, 0, SEEK_END);
    long lineEnd = std::ftell(f), p;
    if (lineEnd > 0 && std::fseek(f, lineEnd - 1, SEEK_SET) == 0 && std::fgetc(f) == '\n') lineEnd--;
    size_t seen = 0;
    auto   emit = [&](long s, long e) {  // the line [s, e); false once there are enough
        if (e <= s || seen++ < skip) return true;
        char   h[120];
        size_t n = (size_t)std::min<long>(e - s, (long)sizeof h);
        std::fseek(f, s, SEEK_SET);
        n = std::fread(h, 1, n, f);
        out.push_back(axis::net::recordHead(std::string(h, n)));
        return out.size() < count;
    };
    char buf[128];
    bool more = count > 0;
    for (p = lineEnd; more && p > 0;) {
        long from = p > (long)sizeof buf ? p - (long)sizeof buf : 0;
        if (std::fseek(f, from, SEEK_SET) != 0 || std::fread(buf, 1, (size_t)(p - from), f) != (size_t)(p - from)) break;
        for (long i = p - 1; more && i >= from; i--)
            if (buf[i - from] == '\n') {
                more = emit(i + 1, lineEnd);
                lineEnd = i;
            }
        p = from;
    }
    if (more && p == 0) emit(0, lineEnd);
    std::fclose(f);
}

// ---- The record browser (@NET RECORD FIND, KIND, MONTHS, SHOW; the sysop menu's Traffic and logs)

using axis::net::netSafe;  // a field from another board, safe for a terminal (net/recordview.h)
static constexpr size_t kNetRecordScan = 2000;  // lines looked at by one command, so a search never holds the loop long

// One line of output, for net/recordview.h's field lists (ctx is the Print).
static void netOutLine(void* out, const std::string& line) { static_cast<Print*>(out)->printf("%s\n", line.c_str()); }

// A record file's short name: "202611", "nodate", or "record" (the one file without a card).
static std::string netRecordMonth(const std::string& path) {
    size_t sl = path.rfind('/') + 1;
    return path.substr(sl, path.size() - sl - 4);
}

// a comes before b, newest first: the file being written, then the months newest to oldest, nodate.log last
static bool netRecordBefore(const std::string& a, const std::string& b, const std::string& cur) {
    int ra = a == cur ? 0 : a.compare(a.size() - 10, 6, "nodate") == 0 ? 2 : 1, rb = b == cur ? 0 : b.compare(b.size() - 10, 6, "nodate") == 0 ? 2 : 1;
    return ra != rb ? ra < rb : a > b;
}

// The record files, newest first: on the SD card the month being written, then /net/record/<yyyymm>.log newest to
// oldest, nodate.log (lines from before the clock was set) last; without a card the one /net/record.log.
static std::vector<std::string> netRecordFiles() {
    std::vector<std::string> v;
    std::string              cur = netRecordFile(), dir = std::string(storageRoot()) + "/net/record";
    if (!storageHasSd()) v.push_back(cur);
    else if (DIR* d = opendir(dir.c_str())) {
        while (dirent* e = readdir(d)) {
            std::string n = e->d_name;
            if (n == "nodate.log" || (n.size() == 10 && n.find_first_not_of("0123456789") == 6 && n.compare(6, 4, ".log") == 0)) v.push_back(dir + "/" + n);
        }
        closedir(d);
    }
    for (size_t i = 1; i < v.size(); i++)
        for (size_t j = i; j > 0 && netRecordBefore(v[j], v[j - 1], cur); j--) std::swap(v[j], v[j - 1]);
    return v;
}

// Walks the record newest first across every file, from index skip on (index 0 is the newest line): each(index, file,
// line start, line) gets the line, only its first 255 bytes (the head) unless whole. Each file is read from its end in
// 512-byte pieces and one line is held at a time; a line over kNetRecordMaxLine is passed over but keeps its index.
// Stops when each says false or after kNetRecordScan lines; returns the next index to look at then, or -1 when no
// line is left.
static long netRecordWalk(size_t skip, bool whole, const std::function<bool(size_t, const std::string&, long, std::string&)>& each) {
    std::unique_ptr<char[]>  buf(new (std::nothrow) char[512]);
    std::vector<std::string> files = netRecordFiles();
    std::string              line;
    size_t                   idx = 0, looked = 0;
    if (!buf) return -1;
    for (size_t fi = 0; fi < files.size(); fi++) {
        FILE* f = std::fopen(files[fi].c_str(), "rb");
        if (!f) continue;
        bool stop = false, left = false;  // left: lines remain before the one it stopped at
        auto emit = [&](long s, long e) {  // the line [s, e); false to stop
            if (e <= s) return true;
            if (idx < skip || (size_t)(e - s) > axis::net::kNetRecordMaxLine) return ++idx, true;
            if (looked++ >= kNetRecordScan) return !(stop = left = true);
            size_t n = whole ? (size_t)(e - s) : std::min<size_t>((size_t)(e - s), 255);
            line.resize(n);
            if (std::fseek(f, s, SEEK_SET) != 0 || std::fread(&line[0], 1, n, f) != n) line.clear();
            stop = !each(idx++, files[fi], s, line);
            left = s > 0;
            return !stop;
        };
        std::fseek(f, 0, SEEK_END);
        long lineEnd = std::ftell(f), p;
        if (lineEnd > 0 && std::fseek(f, lineEnd - 1, SEEK_SET) == 0 && std::fgetc(f) == '\n') lineEnd--;
        bool more = true;
        for (p = lineEnd; more && p > 0;) {
            long from = p > 512 ? p - 512 : 0;
            if (std::fseek(f, from, SEEK_SET) != 0 || std::fread(buf.get(), 1, (size_t)(p - from), f) != (size_t)(p - from)) break;
            for (long i = p - 1; more && i >= from; i--)
                if (buf[i - from] == '\n') {
                    more = emit(i + 1, lineEnd);
                    lineEnd = i;
                }
            p = from;
        }
        if (more && p == 0) emit(0, lineEnd);
        std::fclose(f);
        if (!stop) continue;
        for (size_t k = fi + 1; !left && k < files.size(); k++) {  // stopped on a file's first line: any older file?
            struct stat st;
            left = ::stat(files[k].c_str(), &st) == 0 && st.st_size > 0;
        }
        return left ? (long)idx : -1;
    }
    return -1;
}

// The record line starting at pos in the file, if a line starts there (none over kNetRecordMaxLine).
static bool netRecordLineAt(const std::string& path, long pos, std::string& line) {
    FILE* f = std::fopen(path.c_str(), "rb");
    bool  ok = f && pos >= 0 && (pos ? std::fseek(f, pos - 1, SEEK_SET) == 0 && std::fgetc(f) == '\n' : true);
    char  b[128];
    line.clear();
    for (size_t r; ok && (r = std::fread(b, 1, sizeof b, f)) > 0;) {
        const char* nl = (const char*)std::memchr(b, '\n', r);
        size_t      n = nl ? (size_t)(nl - b) : r;
        ok = line.size() + n <= axis::net::kNetRecordMaxLine;
        if (ok) line.append(b, n);
        if (nl) break;
    }
    if (f) std::fclose(f);
    return ok && !line.empty();
}

// "@NET RECORD FIND <text> [n [skip]]", "KIND votes|members|actions|all [n [skip]]", "MONTHS", "SHOW <index>" or
// "SHOW <file>:<offset>:<unix>" (args lowercase, after RECORD). FIND and KIND print "<index> <file>:<offset>:<unix> <date>
// <who> <what>" newest first, at most n (20, up to 100) from the record's index skip on, then "older from <index>" if
// lines are left. SHOW by index takes the line at that index now; by file, offset and time the very line listed (its
// place does not move as the record grows; the time is checked, since internal flash cuts its file). Trailing numbers
// after FIND's text are n and skip. False if args are none of these (the plain RECORD [n [skip]]).
static bool netRecordBrowse(const NetHost& h, const std::string& args, Print& out) {
    std::vector<std::string> w;
    for (size_t at = 0, e; at < args.size(); at = e + 1) {
        e = std::min(args.find(' ', at), args.size());
        if (e > at) w.push_back(args.substr(at, e - at));
    }
    auto digits = [](const std::string& s) { return s.find_first_not_of("0123456789") == std::string::npos; };
    if (w.empty()) return false;
    const std::string& sub = w[0];
    bool               find = sub == "find", show = sub == "show";
    if (sub == "months") {
        bool any = false;
        for (const std::string& p : netRecordFiles()) {
            struct stat st;
            if (::stat(p.c_str(), &st) == 0) out.printf("%s %ld bytes\n", netRecordMonth(p).c_str(), (long)st.st_size), any = true;
        }
        if (!any) out.println("axis: the record is empty");
        return true;
    }
    if (!find && !show && sub != "kind") return false;
    if (heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) < kNetHeapFloor) {
        out.println("@ERR low on memory just now, try again");
        return true;
    }
    if (show) {
        std::string line;
        size_t      c1 = w.size() == 2 ? w[1].find(':') : std::string::npos, c2 = c1 == std::string::npos ? c1 : w[1].find(':', c1 + 1);
        if (w.size() == 2 && digits(w[1])) {
            size_t want = (size_t)std::strtoul(w[1].c_str(), nullptr, 10);
            netRecordWalk(want, true, [&line, want](size_t i, const std::string&, long, std::string& l) {
                if (i == want) line.swap(l);
                return false;
            });
        } else if (c2 != std::string::npos) {
            for (const std::string& p : netRecordFiles())
                if (netRecordMonth(p) == w[1].substr(0, c1) && netRecordLineAt(p, std::strtol(w[1].c_str() + c1 + 1, nullptr, 10), line) &&
                    std::strtoul(line.c_str(), nullptr, 10) != std::strtoul(w[1].c_str() + c2 + 1, nullptr, 10))
                    line.clear();  // another line is there now
        } else {
            out.println("@ERR @NET RECORD SHOW <index> or <file>:<offset>:<time>");
            return true;
        }
        if (line.empty()) {
            out.println("axis: that line is not in the record now");
            return true;
        }
        size_t      us = std::min(line.find('\x1f'), line.size());
        std::string head = line.substr(0, us);
        line.erase(0, us + 1);  // the signed line
        size_t s1 = std::min(head.find(' '), head.size()), s2 = std::min(head.find(' ', s1 + 1), head.size());
        out.printf("date %s\n", axis::net::netDateText((uint32_t)std::strtoul(head.c_str(), nullptr, 10)).c_str());
        out.printf("who %s\n", netSafe(head.substr(std::min(s1 + 1, s2), s2 - std::min(s1 + 1, s2))).c_str());
        out.printf("what %s\n", netSafe(head.substr(std::min(s2 + 1, head.size()))).c_str());
        axis::net::recordFields(line, netOutLine, &out);
        out.printf("check %s\n", axis::net::recordCheck(line, h.core->state()).c_str());
        return true;
    }
    size_t nums = 0;  // after the word and the text or kind
    while (nums < 2 && w.size() > 2 + nums && digits(w[w.size() - 1 - nums])) nums++;
    using K = axis::net::RecordKind;
    std::string kind = w.size() > 1 ? w[1] : "", text;
    K           k = kind == "votes" ? K::Votes : kind == "members" ? K::Members : kind == "actions" ? K::Actions : K::All;
    for (size_t i = 1; find && i + nums < w.size(); i++) text += (text.empty() ? "" : " ") + w[i];
    if (find ? text.empty() : k == K::All && kind != "all") {
        out.println("@ERR @NET RECORD FIND <text> [n [skip]], KIND votes|members|actions [n [skip]]");
        return true;
    }
    unsigned long n = nums ? std::strtoul(w[w.size() - nums].c_str(), nullptr, 10) : 20, shown = 0;
    n = std::min<unsigned long>(n ? n : 20, 100);
    long more = netRecordWalk(nums == 2 ? (size_t)std::strtoul(w.back().c_str(), nullptr, 10) : 0, find,
                              [&](size_t i, const std::string& file, long pos, std::string& l) {
                                  if (!axis::net::recordMatches(l, find ? K::All : k, text)) return true;
                                  std::string head = axis::net::recordHead(l);
                                  size_t      sp = std::min(head.find(' '), head.size());
                                  uint32_t    t = (uint32_t)std::strtoul(head.c_str(), nullptr, 10);
                                  out.printf("%u %s:%ld:%u %s %s\n", (unsigned)i, netRecordMonth(file).c_str(), pos, (unsigned)t,
                                             axis::net::netDateText(t).c_str(), netSafe(head.substr(std::min(sp + 1, head.size()))).c_str());
                                  return ++shown < n;
                              });
    if (!shown) out.println("axis: nothing found");
    if (more >= 0) out.printf("axis: older from %ld\n", more);
    return true;
}

static void netNote(const std::string& what) {
    if (netHost && netHost->record) netHost->record->note("sysop", what, "");
}

// /net/local.txt: "hide <postId>" and "block <board>" lines. This board's own choices, never sent anywhere.
static void netLocalLoad(NetHost& h) {
    std::string t = netFileRead("local.txt", 16 * 1024), l;
    for (char c : t + "\n") {
        if (c != '\n') {
            l += c;
            continue;
        }
        if (l.compare(0, 5, "hide ") == 0 && netIsPostId(l.substr(5)) && h.hides.size() < kNetMaxHides) h.hides.push_back(l.substr(5));
        else if (l.compare(0, 6, "block ") == 0 && netIsBoard(l.substr(6)) && h.blocks.size() < kNetMaxBlocks) h.blocks.push_back(l.substr(6));
        l.clear();
    }
}
static bool netLocalSave(const NetHost& h) {
    std::string t;
    for (const std::string& s : h.hides) t += "hide " + s + "\n";
    for (const std::string& s : h.blocks) t += "block " + s + "\n";
    return netFileWrite("local.txt", t);
}

// Which of Rev's two keys this board signs with (0 or 1), or -1 when it is not Rev's board.
static int netRevSlot(const NetHost& h) {
    std::string pub = axis::net::toHex(h.key.pub, 32);
    return pub == axis::net::revKey(0) ? 0 : pub == axis::net::revKey(1) ? 1 : -1;
}

// Something made here goes out with the next sync, dialed soon.
static void netFlood(NetHost& h) {
    h.dirty = true;
    h.postDue = true;
    h.postMs = millis();
}

// A notice from Rev's board: signed, applied here and logged to flood. out: what to say (none for the auto seal).
static bool netNotice(NetHost& h, const char* kind, const std::string& target, const std::string& value, Print* out) {
    axis::net::NetNotice n;
    n.kind = kind;
    n.target = target;
    n.value = value;
    n.seq = h.core->state().usedSeq() + 1;  // never a seq used before, even one since pruned or from before a restart
    n.key = netRevSlot(h);
    bool ok = n.key >= 0 && axis::net::signNotice(n, h.key) && h.core->originateNotice(n);
    if (ok) {
        netFlood(h);
        logEvent(String(out ? "sysop: net " : "net: sealed ") + kind + " " + target.c_str());
    }
    if (out) out->printf(ok ? "@OK %s %s, seq %u\n" : "@ERR %s %s not taken\n", kind, target.c_str(), (unsigned)n.seq);
    return ok;
}

// ---- Net Files (listings: net/areas.h; fetch calls: net/fetch.h): sharing, serving and fetching

// An event log line "net files: ..." (one formatted line instead of a chain of String additions).
static void netFilesLog(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
static void netFilesLog(const char* fmt, ...) {
    char    b[192];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    logEvent(String("net files: ") + b);
}

#if AXIS_TRANSFERS
// The same line in the event log and in the net record, about board.
static void netFilesNote(NetHost& h, const std::string& board, const char* fmt, ...) __attribute__((format(printf, 3, 4)));
static void netFilesNote(NetHost& h, const std::string& board, const char* fmt, ...) {
    char    b[256];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    netFilesLog("%s", b);
    if (h.record) h.record->note(board, b, std::string());
}
#endif

// Closes a socket after reading what is left in it, so lwIP closes it cleanly rather than answering with a reset.
static void netDrainStop(WiFiClient& c) {
    uint8_t b[64];
    for (int i = 0; i < 32 && c.available() > 0; i++)
        if (c.read(b, sizeof b) <= 0) break;
    c.stop();
}

static constexpr uint32_t kNetFilesTurnMs = 20;  // file reads in one loop turn stop past this (a pooled read can be a cluster round trip)

#if AXIS_FILES
// net_files_areas: the local file area ids shared on User Net, a comma list (spaces around an id are dropped). Only an
// id a listing can carry (1 to 16 of a-z 0-9 _) counts.
static bool netFilesShared(const std::string& area) {
    if (area.empty() || area.size() > 16 || area.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_") != std::string::npos) return false;
    const std::string& v = boardCfg->netFilesAreas;
    for (size_t at = 0; at <= v.size();) {
        size_t e = v.find(',', at);
        if (e == std::string::npos) e = v.size();
        size_t b = at, z = e;
        while (b < z && v[b] == ' ') b++;
        while (z > b && v[z - 1] == ' ') z--;
        if (v.compare(b, z - b, area) == 0) return true;
        at = e + 1;
    }
    return false;
}

// This board lists its files: User Net is loaded and this board is a member carrying un_files (as originateListing wants).
static bool netFilesOn(const NetHost& h) {
    return h.appsLoaded && boardCfg->netSysop && !asFollower() && h.core->carries(axis::net::kNetFilesUserArea) && h.core->selfMember();
}

static std::string netSysopName() { return boardCfg->sysopHandle.empty() ? std::string("Sysop") : boardCfg->sysopHandle; }

static constexpr size_t   kNetShareQueue = 32;     // listings waiting at most; more are dropped (logged)
static constexpr size_t   kNetShareSpare = 8;      // places LISTALL leaves free for live changes; removals may use them past the cap
static constexpr size_t   kNetShareChunk = 2048;   // a listing's crc is read in pieces this size (kFetchChunk)
static constexpr uint32_t kNetSharePerTurn = 8;    // pieces per loop turn at most (16 KB), and only for kNetFilesTurnMs

static void netShareAdd(NetShareItem it) {
    NetHost* h = netHost.get();
    if (!h || !netFilesOn(*h) || !netFilesShared(it.area)) return;
    if (h->share.q.size() >= kNetShareQueue + (it.gone ? kNetShareSpare : 0)) {  // a removal is kept before a listing
        netFilesLog("too many waiting, %s not %s", it.name.c_str(), it.gone ? "unlisted" : "listed");
        return;
    }
    h->share.q.push_back(std::move(it));
}
static void netFilesRemoved(const std::string& area, const std::string& name) {
    NetShareItem it;
    it.area = area;
    it.name = name;
    it.gone = true;
    netShareAdd(std::move(it));
}
// When the old name was listed (netShareStep), the new name is listed with the area's description, the sysop as the
// uploader (no uploader is kept for a file).
static void netFilesRenamed(const std::string& area, const std::string& from, const std::string& to) {
    netFilesRemoved(area, from);
    NetShareItem it;
    it.area = area;
    it.name = to;
    it.uploader = netSysopName();
    it.lookDesc = true;
    netShareAdd(std::move(it));
}
// A caller's upload landed (serviceJob): listed once its crc is read.
static void netFilesUploaded(const std::string& area, const std::string& name, const std::string& desc, const std::string& who) {
    if (asFollower()) return;  // (a follower's uploads land on the main through its file server)
    NetShareItem it;
    it.area = area;
    it.name = name;
    it.desc = desc;
    it.uploader = who.empty() ? netSysopName() : who;
    netShareAdd(std::move(it));
}

// Signs and sends a listing or a removal (the post seq, saved before and after as for a post: NetStore::post).
static void netShareSend(NetHost& h, const NetShareItem& it, uint32_t size, uint32_t crc) {
    axis::net::NetListing l;
    l.area = it.area;
    l.name = it.name;
    l.desc = it.desc;
    l.uploader = it.uploader;
    l.reach = "i";  // Phase 3 has only the internet; a home card with no host shows unreachable (netFetchReach)
    l.size = size;
    l.crc = crc;
    l.gone = it.gone;
    netSeqFloor(h);
    std::string id = h.core->originateListing(l);
    netPosted(h);
    h.share.counted = false;
    netFilesLog("%s %s/%s %s%s", it.gone ? "unlisted" : "listed", it.area.c_str(), it.name.c_str(), id.empty() ? "FAILED" : "as ",
                id.c_str());
}

// @NET LISTALL: the next files of the area walked fill the queue up to kNetShareQueue - kNetShareSpare (one list of the area
// per 24 files or so), so removals and renames still find room; names this board lists there already are skipped.
static void netListAllNext(NetHost& h) {
    NetShare& sh = h.share;
    std::vector<axis::bbs::FileEntry> v = callerFiles().list(sh.allArea);
    while (sh.allIx < v.size() && sh.q.size() < kNetShareQueue - kNetShareSpare) {
        const axis::bbs::FileEntry& e = v[sh.allIx++];
        uint32_t k = axis::net::crc32((const uint8_t*)e.name.data(), e.name.size());
        if (std::find(sh.allHave.begin(), sh.allHave.end(), k) != sh.allHave.end()) continue;
        NetShareItem it;
        it.area = sh.allArea;
        it.name = e.name;
        it.desc = e.desc;
        it.uploader = netSysopName();
        sh.q.push_back(std::move(it));
    }
    if (sh.allIx < v.size()) return;
    netFilesLog("every file in %s is listed", sh.allArea.c_str());
    sh.allArea.clear();
    std::vector<uint32_t>().swap(sh.allHave);
}

// One piece of sharing work from netTick: a removal sent, or a piece of a listing's crc (then the listing sent).
static void netShareStep(NetHost& h) {
    NetShare& sh = h.share;
    if (!sh.file) {
        if (sh.q.empty() && !sh.allArea.empty()) netListAllNext(h);
        if (sh.q.empty() || !h.core->listingIndexReady()) return;  // (originateListing would spend a seq for nothing)
        NetShareItem it = std::move(sh.q.front());
        sh.q.erase(sh.q.begin());
        if (!netFilesOn(h)) return;  // left User Net meanwhile
        // A removal only of a name this board lists (an upload's temp name, or a ZIP its rezip replaced, never was; the
        // upload itself is listed by netFilesUploaded). A rename's new name (lookDesc, next in the queue) goes only with it.
        if (it.gone) {
            if ((sh.goneSent = h.core->listedHere(it.area, it.name))) netShareSend(h, it, 0, 0);
            return;
        }
        if ((it.lookDesc && !sh.goneSent) || !netFilesShared(it.area) || !(sh.file = callerFiles().open(it.area, it.name)))
            return;  // a rename of a name never listed, unshared or gone meanwhile
        if (it.lookDesc)
            for (const axis::bbs::FileEntry& e : callerFiles().list(it.area))
                if (e.name == it.name) {
                    it.desc = e.desc;
                    break;
                }
        sh.cur = std::move(it);
        sh.size = sh.file->size();
        sh.pos = 0;
        sh.crc = 0xFFFFFFFFu;
    }
    if (sh.pos < sh.size) {
        std::unique_ptr<uint8_t[]> buf(new (std::nothrow) uint8_t[kNetShareChunk]);
        uint32_t t0 = millis();
        for (uint32_t i = 0; buf && i < kNetSharePerTurn && sh.pos < sh.size && (!i || millis() - t0 <= kNetFilesTurnMs); i++) {
            size_t n = std::min<size_t>(kNetShareChunk, sh.size - sh.pos);
            if (!sh.file->readAt(sh.pos, buf.get(), n)) {
                netFilesLog("%s could not be read, not listed", sh.cur.name.c_str());
                sh.file.reset();
                return;
            }
            sh.crc = axis::net::crc32Update(sh.crc, buf.get(), n);
            sh.pos += (uint32_t)n;
        }
    }
    if (sh.pos < sh.size || !h.core->listingIndexReady()) return;
    sh.file.reset();
    netShareSend(h, sh.cur, sh.size, ~sh.crc);
}

// "@NET LISTALL <area>": every file already in a shared area, listed once (Decision 2), the sysop as uploader.
static void netListAll(NetHost& h, const std::string& area, Print& out) {
    NetShare& sh = h.share;
    bool known = false;
    for (const axis::bbs::FileArea& a : boardCfg->areas) known = known || a.id == area;
    if (!known || !netFilesShared(area)) out.println("@ERR not a shared file area (net_files_areas)");
    else if (!netFilesOn(h)) out.println("@ERR this board is not a User Net member carrying un_files");
    else if (!sh.allArea.empty()) out.printf("@ERR still listing %s\n", sh.allArea.c_str());
    else if (heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) < kNetHeapFloor) out.println("@ERR short of memory, try again");
    else {
        std::string me = h.core->board();
        localMsgs->scanHeaders(axis::net::kNetFilesArea, [&](const axis::bbs::Message& m) {
            axis::net::NetListing l;
            if (axis::net::decode(m.net, l) && !l.gone && l.board == me && l.area == area)
                sh.allHave.push_back(axis::net::crc32((const uint8_t*)l.name.data(), l.name.size()));
            return true;
        });
        sh.allArea = area;
        sh.allIx = 0;
        out.printf("@OK listing the files in %s (%u listed already)\n", area.c_str(), (unsigned)sh.allHave.size());
    }
}

// Live listings stored here (a header count, at most once a minute; held or killed ones count too).
static uint32_t netFilesListed(NetHost& h) {
    NetShare& sh = h.share;
    if (sh.counted && millis() - sh.listedMs < 60000u) return sh.listed;
    uint32_t n = 0;
    localMsgs->scanHeaders(axis::net::kNetFilesArea, [&n](const axis::bbs::Message& m) {
        axis::net::NetListing l;
        n += axis::net::decode(m.net, l) && !l.gone;
        return true;
    });
    sh.listed = n;
    sh.listedMs = millis();
    sh.counted = true;
    return n;
}
static size_t netFilesSharedCount() {
    size_t n = 0;
    for (const axis::bbs::FileArea& a : boardCfg->areas) n += netFilesShared(a.id);
    return n;
}
#endif  // AXIS_FILES

// Text from another board for the event log and the console: no control bytes, nothing that reads as scene news.
static std::string netLogText(const std::string& s) {
    std::string t = netSafe(s);
    return t.find(" uploaded ") == std::string::npos ? t : std::string("?");
}

// "@NET FILES [skip]": live listings, newest first, 20 a page: "<id> <home name> <area> <name> <size>".
static void netFilesPage(NetHost& h, int skip, Print& out) {
    if (heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) < kNetHeapFloor) {
        out.println("@ERR short of memory, try again");
        return;
    }
    const axis::net::NetState& st = h.core->state();
    int n = 0, next = h.core->listingsPage(skip, 20, [&](const axis::net::NetListing& l) {
        std::string home = netLogText(netCardName(st.card(l.board)).c_str());
        if (home.empty()) home = l.board;
        for (char& c : home)
            if (c == ' ') c = '_';
        std::string name = netLogText(l.name);
        out.printf("%s %s %s %s %u\n", axis::net::listingId(l).c_str(), home.c_str(), l.area.c_str(), name.c_str(), (unsigned)l.size);
        n++;
    });
    if (next >= 0) out.printf("axis: more with @NET FILES %d\n", next);
    else if (!n) out.println("axis: no Net Files listed");
}

#if AXIS_TRANSFERS
static constexpr int      kNetServeMax = 1;            // fetch calls served at once (the one serve slot)
static_assert(kNetServeMax == 1, "NetHost has one serve slot");
// The trickle bound (a home board that sends too little can hold the caller's transfer): while the client has room for more
// (time spent with a full window is the caller's own pace and is not counted), at least 1 KB (or what is left of the file)
// must arrive in every 120 s, else the fetch ends "slow". A healthy home board keeps the window full; this only cuts one
// that sends under about 8 bytes a second.
static constexpr uint32_t kNetFetchSlowMs = 120000;
static constexpr uint32_t kNetFetchSlowBytes = 1024;
// netFetchBegin's answers.
// level: the board's own listing, in an area above the caller's security level.
enum NetFetchBegin { kNetFetchStarted = 0, kNetFetchBusy, kNetFetchGone, kNetFetchUnreachable, kNetFetchOff, kNetFetchOwn, kNetFetchLevel };
static const char* const kNetFetchWords[] = {"started", "busy", "gone", "unreachable", "off", "own", "level"};

// ---- the serve slot (an AXISFILE 1 call from another board)

// check (fetch.h): the caller's board proves who it is first, then everything about it and the file.
static axis::net::ServeFile netServeCheck(NetHost& h, const axis::net::NetFetch& f) {
    axis::net::ServeFile r;
    NetServe&            v = h.serve;
    axis::net::NetCore&  core = *h.core;
    v.id = f.listing;
    v.from = f.board;
    v.who = netLogText(f.handle);
    v.who += '@';
    v.who += f.board;
    const axis::net::NetCard* card = core.state().card(f.board);
    if (!card || !axis::net::verifyFetch(f, card->pub)) {
        r.no = "sig";
        return r;
    }
    v.proven = true;
    axis::net::NetListing l;
    if (core.state().banned(f.board) || netBlocked(f.board)) r.no = "banned";
    else if (core.members().status(f.board) != axis::net::NetMembers::Status::Member) r.no = "member";
    else if (!netFilesOn(h) || !core.findListing(f.listing, l) || l.board != core.board() || l.gone || !netFilesShared(l.area) ||
             !(v.file = callerFiles().open(l.area, l.name)))
        r.no = core.listingIndexReady() ? "gone" : "busy";  // (the index is built again after a drop: try later)
    else if (v.file->size() != l.size) {
        v.file.reset();
        r.no = "size";
    } else {
        r.ok = true;
        r.size = l.size;
        axis::bbs::RandomFile* file = v.file.get();  // (the slot drops the server before the file)
        r.read = [file](uint32_t off, uint8_t* buf, size_t n) { return file->readAt(off, buf, n); };
        v.served = true;
    }
    return r;
}

static void netServePump(NetHost& h);
// axisServe read "AXISFILE 1 ": one fetch call served at a time, apart from the sync sessions.
static void netServeAccept(WiFiClient& c, const std::string& got) {
    NetHost* h = netHost.get();
    if (h && h->serve.closing) netServePump(*h);  // a half-closed call whose dialer closed frees the slot (a redial can come first)
    uint32_t ip = (uint32_t)c.remoteIP();
    bool     ok = h && h->appsLoaded && boardCfg->netSysop && !asFollower() && !h->serve.s && h->core->listingIndexReady() && netHeapOk() &&
              !netBackedOff(ip);
    NetServe* v = h ? &h->serve : nullptr;
    if (ok) {
        v->s.reset(new (std::nothrow) axis::net::FetchServer(h->core->board(), h->key, netNonce(), [h](const axis::net::NetFetch& f) {
            return netServeCheck(*h, f);
        }));
        ok = v->s != nullptr;
    }
    if (!ok) {  // the slot is taken, short of memory, or not serving: the caller's board says "busy"
        c.print("NO busy\r\n");
        delay(10);
        netDrainStop(c);
        return;
    }
    v->c = c;
    v->c.setNoDelay(true);
    v->ip = ip;
    v->proven = v->served = false;
    v->id.clear();
    v->who = c.remoteIP().toString().c_str();
    v->from.clear();
    v->s->feed(got.data(), got.size());
}

// The call is over and logged. A served one is half-closed (lwIP sends its FIN after the queued bytes) with the slot kept
// while netServePump drops what the dialer still sends, until it closes or kFetchStallMs passes with nothing heard: a
// dialer's late keepalive on a fully closed socket would be answered with a reset that throws away bytes it has not read
// yet. Any other call closes at once (a stranger cannot hold the slot with stray bytes). Called again while closing (that
// wait over, or the net turned off): the socket closes and the slot is free.
static void netServeClose(NetHost& h, const char* why) {
    NetServe& v = h.serve;
    if (!v.closing) {
        if (v.s) v.pend += v.s->takeOut(256);  // a NO line still to go
        for (uint32_t t0 = millis(); !v.pend.empty() && v.c.connected() && millis() - t0 < 200;) {
            if (!netSendRaw(v.c, v.pend)) break;
            if (!v.pend.empty()) delay(2);
        }
        bool        done = v.s && v.s->finished() && !v.s->failed() && v.served;
        std::string word = why ? why : v.s ? v.s->why() : "";
        if (done) netFilesNote(h, v.from, "fetch served %s to %s", v.id.c_str(), v.who.c_str());
        else
            netFilesLog("fetch %s from %s %s (%s)", v.id.empty() ? "call" : v.id.c_str(), v.who.c_str(), v.served ? "stopped" : "refused", word.c_str());
        if (!v.proven && (word == "sig" || word == "bad" || word == "timeout")) netStrike(v.ip);  // never proved who it is
        else if (v.proven) netProven(v.ip);
        int fd = v.c.fd();
        if (done && !why && fd >= 0 && v.c.connected() && shutdown(fd, SHUT_WR) == 0) {  // served (a proven member) only
            v.closing = true;
            v.closeMs = v.closeAt = millis();
            return;
        }
    }
    netDrainStop(v.c);
    v.closing = false;
    v.s.reset();  // before the file it reads
    v.file.reset();
    v.pend.clear();
    v.pend.shrink_to_fit();
    v.c = WiFiClient();
}

static void netServePump(NetHost& h) {
    NetServe& v = h.serve;
    if (!v.s) return;
    char   buf[256];
    size_t fed = 0;
    if (v.closing) {  // half-closed: drop what comes (a keepalive) until the dialer closes
        for (int i = 0; i < 8 && v.c.available() > 0 && v.c.read((uint8_t*)buf, sizeof buf) > 0; i++) v.closeMs = millis();
        // (a byte heard keeps a slow dialer reading its tail, but never past kFetchServeIdleMs from the half-close)
        if (!v.c.connected() || millis() - v.closeMs >= axis::net::kFetchStallMs || millis() - v.closeAt >= axis::net::kFetchServeIdleMs)
            netServeClose(h, nullptr);
        return;
    }
    while (fed < 2048) {  // the GET line, then only the dialer's keepalives
        int a = v.c.available();
        if (a <= 0) break;
        int r = v.c.read((uint8_t*)buf, (size_t)a < sizeof buf ? (size_t)a : sizeof buf);
        if (r <= 0) break;
        v.s->feed(buf, (size_t)r);
        fed += (size_t)r;
    }
    bool     sent = true;
    uint32_t t0 = millis();
    for (int i = 0; i < 4 && sent && v.pend.empty() && (!i || millis() - t0 <= kNetFilesTurnMs); i++) {  // up to 4 pieces a turn
        v.pend = v.s->takeOut(axis::net::kFetchChunk + 64);
        if (v.pend.empty()) break;
        sent = netSendRaw(v.c, v.pend);
    }
    if (!v.pend.empty() && sent) sent = netSendRaw(v.c, v.pend);
    v.s->tick(millis());
    if (v.s->failed()) netServeClose(h, nullptr);
    else if (v.s->finished() && v.pend.empty()) netServeClose(h, nullptr);
    else if (!sent || (!v.c.connected() && v.c.available() <= 0)) netServeClose(h, "closed");
}

// ---- the fetch slot (this board dials a home board for a caller here)

static volatile uint32_t netFetchDnsGen = 0, netFetchDnsDone = 0, netFetchDnsAddr = 0;  // its own lookup: never a sync's
static void netFetchDnsFound(const char*, const ip_addr_t* a, void* arg) {
    uint32_t gen = (uint32_t)(uintptr_t)arg;
    if (gen != netFetchDnsGen) return;
    netFetchDnsAddr = a && IP_IS_V4(a) ? ip_2_ip4(a)->addr : 0;
    netFetchDnsDone = gen;
}

static NetFetchSlot* netFetchFor(uint32_t gen) {
    NetHost* h = netHost.get();
    return h && h->fetch.ph != FetchPh::Idle && h->fetch.gen == gen ? &h->fetch : nullptr;
}

// The call's socket closed and its unsent text dropped (the client and its window stay unless the caller drops them).
static void netFetchDrop(NetFetchSlot& s) {
    netDrainStop(s.c);
    s.c = WiFiClient();
    s.pend.clear();
}

static void netFetchClear(NetHost& h) {
    NetFetchSlot& s = h.fetch;
    netFetchDrop(s);
    s.f.reset();
    s.pend.shrink_to_fit();
    s.ph = FetchPh::Idle;
    s.owner = s.job = nullptr;
    s.redial = false;
    s.gen++;  // a NetFetchFile or ready hook left from it is stale now
}
#if AXIS_FILES && AXIS_CLUSTER
static void netRemoteRelease(const void* owner);
static void netRemoteNote(const void* j, std::string& note);
#endif
static void netFetchRelease(const void* owner) {
#if AXIS_FILES && AXIS_CLUSTER
    netRemoteRelease(owner);  // (a follower's, held through the main; also cleared here after this board became main)
#endif
    if (netHost && owner && netHost->fetch.ph != FetchPh::Idle && netHost->fetch.owner == owner) netFetchClear(*netHost);
}

// The fetch can never deliver now: the socket closes and the window goes; reads fail, and the caller's sender cancels.
static void netFetchFail(NetHost& h, const char* why) {
    NetFetchSlot& s = h.fetch;
    if (s.ph == FetchPh::Idle || s.ph == FetchPh::Failed) return;
    netFetchDrop(s);
    s.f.reset();
    s.ph = FetchPh::Failed;
    s.why = why && *why ? why : "bad";
    netFilesLog("fetch %s for %s from %s stopped (%s)%s", axis::net::listingId(s.l).c_str(), s.handle.c_str(), s.home.c_str(), s.why,
                heapText().c_str());
}

// The home board can be dialed: its card is held with a host, and the listing says internet.
static bool netFetchReach(const NetHost& h, const axis::net::NetListing& l) {
    const axis::net::NetCard* c = h.core->state().card(l.board);
    return c && !c->host.empty() && c->port > 0 && l.reach.find('i') != std::string::npos;
}

static void netFetchConnect(NetHost& h, IPAddress ip) {
    NetFetchSlot& s = h.fetch;
    s.ip = (uint32_t)ip;
    if (!s.c.connect(ip, (uint16_t)s.port, 1200)) {
        netFetchFail(h, "unreachable");
        return;
    }
    s.c.setNoDelay(true);
    s.ph = FetchPh::Live;
    s.pend += s.f->takeOut();  // the hello goes now: the home board's axisServe waits only 40 ms for a first byte
    netSendRaw(s.c, s.pend);
}

// As netDial: an address (or the one found before) connects now; a name is looked up first, without waiting.
static void netFetchDial(NetHost& h) {
    NetFetchSlot& s = h.fetch;
    IPAddress     ip;
    if (s.ip) {
        netFetchConnect(h, IPAddress(s.ip));
        return;
    }
    if (ip.fromString(s.host.c_str())) {
        netFetchConnect(h, ip);
        return;
    }
    ip_addr_t a;
    uint32_t  gen = ++netFetchDnsGen;
    err_t     e = dns_gethostbyname(s.host.c_str(), &a, netFetchDnsFound, (void*)(uintptr_t)gen);
    if (e == ERR_OK && IP_IS_V4(&a)) netFetchConnect(h, IPAddress(ip_2_ip4(&a)->addr));
    else if (e != ERR_INPROGRESS) netFetchFail(h, "unreachable");
    else {
        s.dnsGen = gen;
        s.ph = FetchPh::Looking;
    }
}


// A fetch's crc checkpoint table (FetchClient::CrcMarks), counted with its window in the heap checks.
constexpr size_t kNetFetchMarksBytes = axis::net::kFetchCrcMarks * sizeof(uint32_t);

// A new client from a new offset (the caller's sender asked outside the window). While the crc is on, it asks again from
// the crc checkpoint at or below that offset (for a seek ahead, the last one reached), with its state and the checkpoints,
// so every byte from there is checked in order and the sender reads from its offset; with the crc off (a caller resuming
// its own partial file) it starts at the offset, unchecked.
static void netFetchRedial(NetHost& h) {
    NetFetchSlot& s = h.fetch;
    uint32_t      at = s.redialAt, cs = 0xFFFFFFFFu, cp = s.f ? s.f->crcPos() : axis::net::kFetchNoCrc;
    axis::net::FetchClient::CrcMarks marks;
    s.redial = false;
    if (cp != axis::net::kFetchNoCrc && s.f->checkpoint(s.redialAt, at, cs)) marks = s.f->takeMarks();
    else s.forward = true;
    cp = s.forward ? axis::net::kFetchNoCrc : at;
    netFetchDrop(s);
    s.f.reset();  // (its window first, then the new one)
    if (heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) >= kNetHeapFloor + axis::net::kFetchWindow + kNetFetchMarksBytes)
        s.f.reset(new (std::nothrow) axis::net::FetchClient(s.l, s.homePub, s.handle, h.key, h.core->board(), netNonce(), at, cs, cp));
    if (!s.f) {
        netFetchFail(h, "memory");
        return;
    }
    s.f->giveMarks(std::move(marks));
    s.f->aim(s.redialAt);
    netFilesLog("fetch %s asked again from %u (at %u)", axis::net::listingId(s.l).c_str(), (unsigned)s.redialAt, (unsigned)at);
    s.at = at;
    s.fedNow = 0;
    s.slowMs = 0;
    s.slowFed = s.fed;
    s.f->tick(millis());
    s.ph = FetchPh::Looking;
    netFetchDial(h);
}

// Every byte arrived. A Done that the crc did not check counts only for a forward resume (the caller's program resuming its
// own partial file), noted as such; any other is a failure. This runs in the pump that fed the last byte, before the
// caller's sender can ask for it.
static void netFetchDone(NetHost& h) {
    NetFetchSlot& s = h.fetch;
    if (!s.f->verified()) {
        if (!s.forward) {
            netFetchFail(h, "crc");
            return;
        }
        netFilesNote(h, s.l.board, "fetch %s for %s unverified (resumed)", axis::net::listingId(s.l).c_str(), s.handle.c_str());
    }
    netFetchDrop(s);
    s.ph = FetchPh::Done;
}

static void netFetchPump(NetHost& h) {
    NetFetchSlot& s = h.fetch;
    if (s.ph == FetchPh::Idle) return;
    uint32_t now = millis();
    // begun and never taken up (its caller or follower is gone); a job is not, nor a follower's transfer (job = its
    // token) until the fetch ended (its "e" lost)
    if (axis::net::fetchLetGo(s.job, s.owner, s.ph == FetchPh::Done || s.ph == FetchPh::Failed, now - s.touchMs)) {
        if (s.ph != FetchPh::Failed) netFilesLog("fetch %s let go (nobody asked for it)", axis::net::listingId(s.l).c_str());
        netFetchClear(h);
        return;
    }
    if (s.redial && s.ph != FetchPh::Failed) netFetchRedial(h);
    if (s.ph == FetchPh::Done || s.ph == FetchPh::Failed) return;
    if (s.ph == FetchPh::Looking) {
        s.f->tick(now);
        if (s.f->state() == axis::net::FetchClient::St::Failed) netFetchFail(h, s.f->why());
        else if (netFetchDnsDone == s.dnsGen) {
            uint32_t addr = netFetchDnsAddr;
            if (addr) netFetchConnect(h, IPAddress(addr));
            else netFetchFail(h, "unreachable");
        }
        s.tickMs = now;
        return;
    }
    uint8_t buf[1024];
    size_t  got = 0;
    while (got < 4096) {  // never more than the client has room for (a full window holds the home board back)
        size_t room = std::min(s.f->room(), sizeof buf);
        int    a = s.c.available();
        if (!room || a <= 0) break;
        int r = s.c.read(buf, std::min(room, (size_t)a));
        if (r <= 0) break;
        size_t t = s.f->feed((const char*)buf, (size_t)r);
        s.fed += (uint32_t)t;
        s.fedNow += (uint32_t)t;
        got += (size_t)r;
        if (t < (size_t)r) {
            netFetchFail(h, "bad");
            return;
        }
    }
    s.pend += s.f->takeOut();
    bool sent = netSendRaw(s.c, s.pend);
    s.f->tick(now);
    using St = axis::net::FetchClient::St;
    St st = s.f->state();
    if (st == St::Failed) netFetchFail(h, s.f->why());
    else if (st == St::Done) netFetchDone(h);
    else if (!sent || (!s.c.connected() && s.c.available() <= 0)) {  // gone before every byte came
        netFilesLog("fetch %s closed at %u of %u", axis::net::listingId(s.l).c_str(), (unsigned)s.f->reached(), (unsigned)s.l.size);
        if (st != St::Streaming || s.retries >= axis::net::kFetchRetries) netFetchFail(h, "closed");
        else {  // asked again from where the sender reads (as for a seek outside the window)
            s.retries++;
            s.redial = true;
            s.redialAt = s.f->aimed();
        }
    }
    else if (st == St::Streaming) {
        if (s.f->room() > 0) s.slowMs += now - s.tickMs;
        if (s.slowMs >= kNetFetchSlowMs) {
            uint32_t have = s.at + s.fedNow, left = s.l.size > have ? s.l.size - have : 0;
            if (s.fed - s.slowFed < std::min(kNetFetchSlowBytes, left)) {
                netFetchFail(h, "slow");
                return;
            }
            s.slowMs = 0;
            s.slowFed = s.fed;
        }
    }
    s.tickMs = now;
}

// The caller's sender asks (Transfer::ReadyAt). True once the fetch can never deliver, so its read fails.
static bool netFetchReadyAt(uint32_t gen, uint32_t off, size_t n) {
    NetFetchSlot* s = netFetchFor(gen);
    if (!s || s->ph == FetchPh::Failed) return true;
    s->touchMs = millis();
    if (s->redial || s->ph == FetchPh::Looking || !s->f) return false;
    if (off < s->l.size && s->f->behind(off)) {  // outside the window: a new call from there
        if (s->retries >= axis::net::kFetchRetries) {
            netFetchFail(*netHost, "retries");
            return true;
        }
        s->retries++;
        s->redial = true;
        s->redialAt = off;
        return false;
    }
    return s->f->ready(off, n);
}
static bool netFetchRead(uint32_t gen, uint32_t off, uint8_t* buf, size_t n) {
    NetFetchSlot* s = netFetchFor(gen);
    if (!s || s->ph == FetchPh::Failed || s->redial || !s->f) return false;
    s->touchMs = millis();
    return s->f->read(off, buf, n);
}

// The fetched file as the caller's TransferJob reads it (size: the listing's). Dropping it lets the slot go.
class NetFetchFile : public axis::bbs::RandomFile {
public:
    NetFetchFile(uint32_t gen, uint32_t size) : gen_(gen), size_(size) {}
    ~NetFetchFile() override {
        if (netFetchFor(gen_)) netFetchClear(*netHost);
    }
    uint32_t size() const override { return size_; }
    bool     readAt(uint32_t off, uint8_t* buf, size_t n) override { return netFetchRead(gen_, off, buf, n); }

private:
    uint32_t gen_, size_;
};

// ---- the host API for the caller's Net Files list (Task 5) and a follower's fetch through the main (Task 6). owner is
// whatever stays the same for the one asking (the caller's Node, a follower's token); the slot is one per board.

// "" or why the listing's home board cannot be dialed now.
static std::string netFetchUnreachable(const std::string& id) {
    NetHost*              h = netHost.get();
    axis::net::NetListing l;
    if (!h || !h->core->findListing(id, l) || l.board == h->core->board() || netFetchReach(*h, l)) return std::string();
    return "that board can't be reached right now";
}

// Starts a fetch of listing id for handle (the caller's, never empty) from offset: kNetFetchStarted, or why not (busy: the
// slot is taken or memory is short; gone: no such live listing; unreachable; off: the net does not run here or no handle;
// own: the listing is this board's, so the file is downloaded from its area here).
static int netFetchBegin(const void* owner, const std::string& id, const std::string& handle, uint32_t offset) {
    NetHost* hp = netHost.get();
    std::string who;
    for (char ch : handle)
        if ((unsigned char)ch >= 0x20 && ch != 0x7f && who.size() < 30) who.push_back(ch);
    if (!hp || !owner || who.empty() || !hp->appsLoaded || !boardCfg->netSysop || asFollower()) return kNetFetchOff;
    NetHost&      h = *hp;
    NetFetchSlot& s = h.fetch;
    if (s.ph != FetchPh::Idle && s.owner == owner && !s.job) netFetchClear(h);  // the same caller again
    if (s.ph != FetchPh::Idle) return kNetFetchBusy;
    if (h.core->blockedHere(id.substr(0, 16))) return kNetFetchUnreachable;  // never dialed (its listings are not shown)
    axis::net::NetListing l;
    if (!h.core->findListing(id, l)) return h.core->listingIndexReady() ? kNetFetchGone : kNetFetchBusy;
    if (l.board == h.core->board()) return kNetFetchOwn;
    if (!netFetchReach(h, l)) return kNetFetchUnreachable;
    if (heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) < kNetHeapFloor + axis::net::kFetchWindow + kNetFetchMarksBytes) return kNetFetchBusy;
    const axis::net::NetCard& card = *h.core->state().card(l.board);
    s.f.reset(new (std::nothrow) axis::net::FetchClient(l, card.pub, who, h.key, h.core->board(), netNonce(), offset));
    if (!s.f) return kNetFetchBusy;
    s.l = l;
    s.homePub = card.pub;
    s.handle = who;
    s.host = card.host;
    s.port = card.port;
    s.home = netLogText(netCardName(&card).c_str());
    if (s.home.empty()) s.home = l.board;
    s.owner = owner;
    s.job = nullptr;
    s.gen++;
    s.ip = 0;
    s.at = offset;
    s.retries = 0;
    s.forward = offset > 0;  // the caller's program resumes its own file: the crc cannot cover it
    s.redial = false;
    s.fed = s.fedNow = s.slowMs = s.slowFed = 0;
    s.tickMs = s.touchMs = millis();
    s.why = "";
    s.ph = FetchPh::Looking;
    s.f->tick(millis());  // its connect clock starts now (it covers the lookup too)
    netFilesNote(h, l.board, "fetch %s for %s", id.c_str(), who.c_str());
    netFetchDial(h);
    return kNetFetchStarted;
}

// Where the owner's fetch is: 0 connecting; 1 ready, the bytes are coming (size: the whole file); -1 failed (word: the
// home board's NO word, or "stall", "sig", "size", "crc", "bad", "memory", "unreachable", "closed", "slow", "retries",
// "stopped" when the slot is not this owner's any more).
static int netFetchPoll(const void* owner, uint32_t& size, const char*& word) {
    NetHost* h = netHost.get();
    if (!h || h->fetch.ph == FetchPh::Idle || h->fetch.owner != owner) {
        word = "stopped";
        return -1;
    }
    NetFetchSlot& s = h->fetch;
    s.touchMs = millis();
    if (s.ph == FetchPh::Failed) {
        word = s.why;
        return -1;
    }
    using St = axis::net::FetchClient::St;
    if (s.ph == FetchPh::Done || (s.ph == FetchPh::Live && s.f && s.f->state() == St::Streaming)) {
        size = s.l.size;
        return 1;
    }
    return 0;
}

// Once netFetchPoll says ready: the caller's download job over the fetch (nullptr when it is not ready or already has one).
// It can also be nullptr right after a ready poll (no memory for the job): the browser treats that as "stopped".
// It reads through a NetFetchFile and holds its sender with the slot's ready hook; the job ending lets the slot go.
static axis::bbs::TransferJob* netFetchJob(const void* owner, axis::bbs::FileLibrary& lib, const axis::bbs::TransferRequest& req,
                                           axis::door::ByteStream& line) {
    uint32_t    size = 0;
    const char* word = "";
    if (netFetchPoll(owner, size, word) != 1 || netHost->fetch.job) return nullptr;
    NetFetchSlot&                          s = netHost->fetch;
    uint32_t                               gen = s.gen;
    std::unique_ptr<axis::bbs::RandomFile> file(new (std::nothrow) NetFetchFile(gen, size));
    if (!file) return nullptr;
    axis::bbs::TransferJob* j = new (std::nothrow) axis::bbs::TransferJob(
        lib, req, line, std::move(file), size, s.l.name, [gen](uint32_t off, size_t n) { return netFetchReadyAt(gen, off, n); });
    if (j) s.job = j;
    return j;
}

// serviceJob: a Net Files download that ended because the fetch stopped gets a clear message, not the sender's "read error".
static void netFetchJobNote(const void* j, std::string& note) {
#if AXIS_FILES && AXIS_CLUSTER
    netRemoteNote(j, note);
#endif
    if (netHost && netHost->fetch.ph == FetchPh::Failed && netHost->fetch.job == j) note = "the file transfer from " + netHost->fetch.home + " stopped";
}

#if AXIS_FILES
static_assert(kNetFetchStarted == axis::bbs::NetFilesView::kStarted && kNetFetchBusy == axis::bbs::NetFilesView::kBusy &&
                  kNetFetchGone == axis::bbs::NetFilesView::kGone && kNetFetchUnreachable == axis::bbs::NetFilesView::kUnreachable &&
                  kNetFetchOff == axis::bbs::NetFilesView::kOff && kNetFetchOwn == axis::bbs::NetFilesView::kOwn &&
                  kNetFetchLevel == axis::bbs::NetFilesView::kLevel,
              "NetFilesView mirrors NetFetchBegin");

// The caller's Net Files list (NetFilesView::page): every text in it comes from another board, made safe here. A row
// whose home board cannot be dialed (no host on its card) says so in place of its description.
static std::string netPageReach;  // the last page's rows: '1' reachable, '0' not (a follower's G reply)
static int netFilesView(int skip, int max, std::vector<axis::bbs::FileEntry>& out, std::vector<std::string>& ids,
                        std::vector<std::string>& homes) {
    NetHost* h = netHost.get();
    netPageReach.clear();
    if (!h) return -1;
    if (heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) < kNetHeapFloor) return axis::bbs::NetFilesView::kShort;
    const axis::net::NetState& st = h->core->state();
    return h->core->listingsPage(skip, max, [&](const axis::net::NetListing& l) {
        bool reach = l.board == h->core->board() || netFetchReach(*h, l);
        netPageReach += reach ? '1' : '0';
        out.push_back({netLogText(l.name), l.size, reach ? netSafe(l.desc) : std::string("(can't be reached)"), l.date});
        ids.push_back(axis::net::listingId(l));
        homes.push_back(netSafe(netCardName(st.card(l.board)).c_str()));
        if (homes.back().empty()) homes.back() = l.board;
    });
}

// NetFilesView::callerRadioBps: -1 for a caller not on radio, else the modem's reported bps, else (no report yet) the
// rate of the caller's LoRa tier, else the slowest tier's (SF12), so a radio caller is always told the time. Tier rates
// are sf * 400000 / 2^sf: the LoRa raw rate (SF * BW / 2^SF, times the coding rate) fitted to the LFD3K modem's own
// reports on the 2026-11-12 radio bench, SF5 62500 and SF10 3906 bps (the SF10 call is in docs/FEATURES.md); SF12
// gives 1171.
static int32_t netCallerBps(const axis::net::LinkInfo& k) {
    if (!k.radio()) return -1;
    if (k.bps > 0) return k.bps;
    int sf = k.sf() ? k.sf() : 12;
    return (int32_t)(400000u * (uint32_t)sf >> sf);
}

// NetFilesView::begin for a caller here (owner: their Node). This board's own listing is downloaded from its area, so
// it is answered before the slot is asked (a busy slot never refuses a local file); only while the area is still
// shared, as the serve check asks (unsharing does not take the listings back).
// This board's own listing for a caller of security level `level`: own while its area is still shared and open to that
// level (Node::openTo's rule), else gone or level.
static int netFilesOwn(const axis::net::NetListing& l, int level) {
    return axis::bbs::NetFilesView::ownAnswer(boardCfg->areas, netFilesShared(l.area), l.area, level);
}
// NetFilesView::begin on this board, for a caller here (owner: their Node) or a follower's (owner: its file server).
static int netFilesBegin(const void* owner, const std::string& id, const std::string& handle, int level, std::string& area,
                         std::string& name) {
    axis::net::NetListing l;
    if (netHost && netHost->core->findListing(id, l) && l.board == netHost->core->board()) {
        area = l.area;
        name = l.name;
        return netFilesOwn(l, level);
    }
    return netFetchBegin(owner, id, handle, 0);
}

#if AXIS_CLUSTER
static std::string netNo(const char* word) {  // a refusal or failure for a follower: "0<word>"
    std::string r("0");
    r += word;
    return r;
}

// The main's answer to a follower's caller (FileServer: G page, b begin or poll, t take, e end; f has 8 fields). owner is
// that follower's file server; the follower lets one of its callers at a time hold the fetch. Every answer comes from
// what is here now: t reads the fetch's window only, so a follower never holds this loop longer than its own request.
static std::string netFollowerOp(const void* owner, char op, const std::vector<std::string>& f) {
    uint32_t a = (uint32_t)std::strtoul(f[0].c_str(), nullptr, 10), b = (uint32_t)std::strtoul(f[1].c_str(), nullptr, 10);
    char     num[16];
    if (op == 'G') {  // "G skip max": "1<next skip>", then a line per row: id name size desc home reach
        std::vector<axis::bbs::FileEntry> v;
        std::vector<std::string>          ids, homes;
        int         next = netFilesView((int)a, b && b <= 15 ? (int)b : 15, v, ids, homes);
        // (15 rows fit one link frame, about 4 KB at most; the guard is there as L has it)
        std::string r("1");
        r += std::to_string(next);
        size_t head = r.size();
        for (size_t i = 0; i < v.size(); i++) {
            size_t at = r.size();
            std::snprintf(num, sizeof num, "\x1f%u\x1f", (unsigned)v[i].size);
            ((((((r += '\n') += ids[i]) += '\x1f') += v[i].name) += num) += v[i].desc) += '\x1f';
            ((r += homes[i]) += '\x1f') += i < netPageReach.size() ? netPageReach[i] : '1';
            if (r.size() > axis::cluster::FileServer::kListMax) {  // the rest on the next page
                r.resize(at);
                r.replace(1, head - 1, std::to_string((int)a + (int)i));
                break;
            }
        }
        return r;
    }
    NetHost* h = netHost.get();
    if (!h) return netNo("off");
    NetFetchSlot& s = h->fetch;
    bool          mine = s.ph != FetchPh::Idle && s.owner == owner;
    if (op == 'b') {  // "b id handle offset level": begin; the same id again (or no handle) asks how it goes
        if (!mine || axis::net::listingId(s.l) != f[0]) {
            if (f[1].empty()) return netNo("stopped");
            std::string area, name;  // (the main's own file: the follower downloads it from that area; no level: refused)
            int         r = netFilesBegin(owner, f[0], f[1], f[3].empty() ? -1 : (int)std::strtol(f[3].c_str(), nullptr, 10), area, name);
            return r == kNetFetchOwn ? "3" + area + '\x1f' + name : r == kNetFetchStarted ? std::string("2") : netNo(kNetFetchWords[r]);
        }
        uint32_t    size = 0;
        const char* word = "stopped";
        int         r = netFetchPoll(owner, size, word);
        if (r <= 0) return r ? netNo(word) : std::string("2");
        s.job = owner;  // its transfer starts: held like a caller's job here (e, onNodeGone, slow, stall end it; fetchLetGo once ended)
        std::snprintf(num, sizeof num, "1%u\x1f", (unsigned)size);
        return num + s.home;
    }
    if (op == 't') {  // "t offset length": "1<bytes>", "2" not here yet, "0<word>"
        if (!mine) return netNo("stopped");
        if (b > axis::cluster::FileServer::kChunk || (uint64_t)a + b > s.l.size) return netNo("bad");
        if (s.ph != FetchPh::Failed && !netFetchReadyAt(s.gen, a, b)) return std::string("2");
        std::string r(1 + b, '1');
        if (s.ph != FetchPh::Failed && netFetchRead(s.gen, a, (uint8_t*)&r[1], b)) return r;
        return netNo(s.ph == FetchPh::Failed ? s.why : "closed");
    }
    if (op == 'e') netFetchRelease(owner);
    return std::string("1");
}

// A follower's side: the main's fetch, held by one of this board's callers at a time.
struct NetRemoteFetch {
    const void*                           owner = nullptr;
    const axis::cluster::RemoteFetchFile* file = nullptr;  // once the transfer runs
    const void*                           job = nullptr;
    std::string                           id, home;
    uint32_t                              size = 0, askMs = 0;
    bool                                  ready = false;  // the main said ready (size may be 0)
};
static NetRemoteFetch netRemote;
static std::string    netRemoteWord;     // why the last one failed (kept after release: the browser shows it after)
static constexpr uint32_t kNetRemoteAskMs = 250;  // how often a connecting fetch is asked about (a round trip each)

static void netRemoteDrop() {  // this board holds nothing on the main now
    netRemote.owner = netRemote.job = nullptr;  // (id and home are only read while it is held)
    netRemote.file = nullptr;
    netRemote.ready = false;
}
static void netRemoteRelease(const void* owner) {
    if (!owner || netRemote.owner != owner) return;
    std::string ignored;
    remoteFiles.raw("e", ignored);
    netRemoteDrop();
}

static void netRemoteNote(const void* j, std::string& note) {
    if (j && netRemote.job == j && netRemote.file && !netRemote.file->word().empty()) note = "the file transfer from " + netRemote.home + " stopped";
}

// Once the poll says ready: the caller's download job over the main's fetch (nullptr when not ready or no memory). The
// fetch is ended (netRemoteRelease) once the job is gone: serviceJob and hangUp release this caller's.
static axis::bbs::TransferJob* netRemoteJob(const void* owner, const axis::bbs::TransferRequest& req, axis::door::ByteStream& line) {
    if (!owner || netRemote.owner != owner || netRemote.file || !netRemote.ready) return nullptr;
    uint32_t       size = netRemote.size;
    auto*    f = new (std::nothrow) axis::cluster::RemoteFetchFile(remoteFiles, size, [] { return (uint32_t)millis(); });
    if (!f) return nullptr;
    netRemote.file = f;
    std::unique_ptr<axis::bbs::RandomFile> file(f);
    axis::bbs::TransferJob*                j = new (std::nothrow)
        axis::bbs::TransferJob(remoteFiles, req, line, std::move(file), size, req.name, [f](uint32_t off, size_t n) { return f->ready(off, n); });
    if (j) netRemote.job = j;
    return j;
}
#endif

// NetFilesView for a caller here: this board's fetch, or on a follower the main's (through the file link).
static int netViewPage(int skip, int max, std::vector<axis::bbs::FileEntry>& out, std::vector<std::string>& ids,
                       std::vector<std::string>& homes) {
#if AXIS_CLUSTER
    if (asFollower()) return remoteFiles.netPage(skip, max, out, ids, homes);  // (its rows marked as here)
#endif
    return netFilesView(skip, max, out, ids, homes);
}
static int netViewBegin(const axis::bbs::Node* n, const std::string& id, std::string& area, std::string& name) {
#if AXIS_CLUSTER
    if (asFollower()) {
        // another caller here has the main's fetch, unless the main already said it holds nothing for it (a t "0stopped")
        if (netRemote.file && netRemote.file->word() == "stopped") netRemoteDrop();
        if (netRemote.owner && netRemote.owner != n) return kNetFetchBusy;
        netRemoteRelease(n);
        std::string a;
        char        r = remoteFiles.netFetch(id, n->handle(), n->securityLevel(), a);
        if (r == '1' || r == '2') {
            netRemote.owner = n;
            netRemote.id = id;
            netRemote.askMs = millis();
            return kNetFetchStarted;
        }
        if (r == '3') {  // "area<1F>name"
            size_t k = a.find('\x1f');
            area = a.substr(0, k);
            name = k == std::string::npos ? std::string() : a.substr(k + 1);
            return kNetFetchOwn;
        }
        for (int i = kNetFetchBusy; r == '0' && i <= kNetFetchLevel; i++)
            if (a == kNetFetchWords[i]) return i;
        return kNetFetchUnreachable;
    }
#endif
    return netFilesBegin(n, id, n->handle(), n->securityLevel(), area, name);
}
static int netViewPoll(const void* owner, uint32_t& size, const char*& word) {
#if AXIS_CLUSTER
    if (asFollower()) {
        word = "stopped";
        if (!owner || netRemote.owner != owner) return -1;
        if (millis() - netRemote.askMs < kNetRemoteAskMs) return 0;
        netRemote.askMs = millis();
        std::string a;
        char        r = remoteFiles.netFetch(netRemote.id, "", 0, a);  // (no handle: only asks)
        if (r == '2') return 0;
        if (r == '1') {  // "size<1F>home"
            size = netRemote.size = (uint32_t)std::strtoul(a.c_str(), nullptr, 10);
            netRemote.home = a.substr(a.find('\x1f') + 1);
            netRemote.ready = true;
            return 1;
        }
        netRemoteWord = r == '0' ? a : "closed";
        if (netRemoteWord == "stopped") netRemoteDrop();  // the main holds nothing for it (nothing to end there)
        word = netRemoteWord.c_str();
        return -1;
    }
#endif
    return netFetchPoll(owner, size, word);
}
#endif

// "@NET FETCHTEST <id>" (the bench): the listing fetched to nowhere, read in order as a sender would, a few KB a loop turn
// (netFetchTestStep, from netTick) so the board goes on meanwhile; at most 10 minutes. The result goes to the event log
// (and so to Serial); "@NET FETCHTEST" alone prints the last one.
struct NetFetchTest {
    bool        on = false;
    uint32_t    gen = 0, off = 0, size = 0, t0 = 0;
    std::string last;
};
static NetFetchTest netFetchTestRun;
static const char   kNetFetchTestOwner = 0;

static void netFetchTest(NetHost& h, const std::string& id, Print& out) {
    NetFetchTest& t = netFetchTestRun;
    if (id.empty()) {
        out.println(t.on ? "axis: a fetch test is running" : t.last.empty() ? "axis: no fetch test yet" : t.last.c_str());
        return;
    }
    if (t.on) {
        out.println("@ERR a fetch test is running");
        return;
    }
    int r = netFetchBegin(&kNetFetchTestOwner, id, netSysopName(), 0);
    if (r != kNetFetchStarted) {
        out.printf("@ERR %s\n", kNetFetchWords[r]);
        return;
    }
    h.fetch.job = &kNetFetchTestOwner;  // (no idle let-go while it runs)
    t.on = true;
    t.gen = h.fetch.gen;
    t.off = 0;
    t.size = h.fetch.l.size;
    t.t0 = millis();
    out.printf("@OK fetching %s\n", id.c_str());
}

static void netFetchTestStep(NetHost& h) {
    NetFetchTest& t = netFetchTestRun;
    if (!t.on) return;
    NetFetchSlot* s = netFetchFor(t.gen);
    static uint8_t buf[1024];  // (off the loop task's stack)
    for (int i = 0; s && i < 4 && t.off < t.size && s->ph != FetchPh::Failed; i++) {
        size_t n = std::min<size_t>(sizeof buf, t.size - t.off);
        if (!netFetchReadyAt(t.gen, t.off, n) || !netFetchRead(t.gen, t.off, buf, n)) break;
        t.off += (uint32_t)n;
    }
    if (s && s->ph != FetchPh::Failed && millis() - t.t0 > 600000u) netFetchFail(h, "timeout");
    char res[64];
    if (!s) std::snprintf(res, sizeof res, "@ERR stopped");
    else if (s->ph == FetchPh::Done && t.off >= t.size) std::snprintf(res, sizeof res, "@OK %u crc ok", (unsigned)t.off);
    else if (s->ph != FetchPh::Failed) return;  // still going
    else if (!std::strcmp(s->why, "crc")) std::snprintf(res, sizeof res, "@OK %u crc bad", (unsigned)t.size);
    else std::snprintf(res, sizeof res, "@ERR %s", s->why);
    t.on = false;
    t.last = res;
    netFilesLog("fetch test: %s", res);
    if (s) netFetchClear(h);
}
#endif  // AXIS_TRANSFERS

// @NET FILES, LISTALL and FETCHTEST. False when the words are none of them.
static bool netFilesCommand(NetHost& h, const std::string& up, const std::string& a, Print& out) {
    if (up == "FILES" || up.compare(0, 6, "FILES ") == 0) {
        netFilesPage(h, up.size() > 6 ? std::atoi(up.c_str() + 6) : 0, out);
        return true;
    }
#if AXIS_FILES
    if (up.compare(0, 8, "LISTALL ") == 0) {
        netListAll(h, a.substr(a.find_first_not_of(' ', 8) == std::string::npos ? a.size() : a.find_first_not_of(' ', 8)), out);
        return true;
    }
#endif
#if AXIS_TRANSFERS
    if (up == "FETCHTEST" || up.compare(0, 10, "FETCHTEST ") == 0) {
        netFetchTest(h, a.size() > 10 ? a.substr(10) : std::string(), out);
        return true;
    }
#endif
    return false;
}

static std::string netAgo(uint32_t t) { return t ? axis::net::netDateText(t) : std::string("never"); }

static const char* netSlotText(const NetSlot& s) { return s.phase == NetPhase::Idle ? "idle" : s.phase == NetPhase::Looking ? "looking up" : "live"; }

// The rest of a NetCore::saveCursors line after its peer, "<index> <last sync> <out|in> <sessions>": the numbers, then the pool.
// (No sscanf: it would pull about 24 KB of scanf into the image.)
static std::string netCursor(const char* p, uint32_t& idx, uint32_t& last) {
    char* e = nullptr;
    idx = (uint32_t)std::strtoul(p, &e, 10);
    last = (uint32_t)std::strtoul(e, &e, 10);
    while (*e == ' ') e++;
    const char* w = e;
    while (*e && *e != ' ') e++;  // the pool word only (the completed sessions follow it)
    return std::string(w, (size_t)(e - w));
}

static void netStatus(NetHost& h, Print& out) {
    axis::net::NetCore& core = *h.core;
    const axis::net::NetCard& c = h.card;
    out.printf("net %s\n", boardCfg->netSysop ? "on" : "off (net_sysop=0)");
    out.printf("board %s\n", core.board().c_str());
    out.printf("rev board %s\n", netRevSlot(h) >= 0 ? "yes" : "no");
    out.printf("name %s\nmac %s\ncontact %s\nlocation %s\n", c.name.c_str(), c.mac.c_str(), c.sysop.c_str(), c.location.c_str());
    out.printf("origin %s (%s:%d)\n", c.name.c_str(), c.host.c_str(), c.port);
    out.printf("first peer %s\n", boardCfg->netFirstPeer.empty() ? "none" : "set");
    out.printf("high seq %u, cards %u, outlog %u..%u\n", (unsigned)core.state().highSeq(), (unsigned)core.state().cardCount(),
               (unsigned)h.log->first(), (unsigned)h.log->next());
    out.printf("sessions in %s, out %s\n", netSlotText(h.in), netSlotText(h.out));
    uint32_t    next = h.log->next(), idx, last;
    std::string t = core.saveCursors();
    for (size_t at = 0, nl; (nl = t.find('\n', at)) != std::string::npos; at = nl + 1) {
        std::string l = t.substr(at, nl - at);
        size_t      sp = l.find(' ');
        if (sp != 16) continue;  // peer lines only (not "history <0|1>")
        std::string pool = netCursor(l.c_str() + sp, idx, last);
        out.printf("peer %.16s %s queue %u, synced %s\n", l.c_str(), pool.c_str(), (unsigned)(next > idx ? next - idx : 0), netAgo(last).c_str());
    }
    int hist = core.historyState();
    out.printf("history %s\n", hist > 0 ? "done" : hist == 0 ? "not done" : "not asked yet");
    using S = axis::net::NetMembers::Status;
    S us = h.appsLoaded ? core.members().status(core.board()) : S::None;
    // With net_user off: "off", then what the tally still holds for this board in brackets.
    const char* held = us == S::Member ? "member" : us == S::Pending ? "application pending" : us == S::Denied ? "denied" : us == S::Vetoed ? "vetoed" : nullptr;
    if (!h.appsLoaded) out.print("user net loading");
    else if (!boardCfg->netUser) out.printf("user net off%s%s%s", held ? " (" : "", held ? held : "", held ? ")" : "");
    else out.printf("user net %s", us == S::Pending ? "pending" : held ? held : "not applied");
    if (h.appsLoaded && boardCfg->netUser && us == S::Pending) {
        axis::net::NetMembers::Tally t = core.members().tally(core.board());
        out.printf(", yes %u of %u, no %u of %u", (unsigned)t.yes, (unsigned)core.state().votesYes(), (unsigned)t.no, (unsigned)core.state().votesNo());
    }
    out.print("\n");
#if AXIS_FILES
    out.printf("files: %u listed, %u shared areas\n", (unsigned)netFilesListed(h), (unsigned)netFilesSharedCount());
#endif
    if (h.record && h.record->failures()) out.printf("record writes failed %u\n", (unsigned)h.record->failures());
}


static void netPeers(NetHost& h, Print& out) {
    const axis::net::NetState& st = h.core->state();
    std::string                t = "\n" + h.core->saveCursors();
    st.forEachCard([&](const axis::net::NetCard& c) {
        std::string link = "-", seen = "-";
        size_t      at = t.find("\n" + c.board + " ");
        if (at != std::string::npos) {
            uint32_t    idx, last;
            std::string l = t.substr(at + 18, t.find('\n', at + 1) - at - 18);
            link = netCursor(l.c_str(), idx, last);
            seen = netAgo(last);
        }
        const char* flags = c.board == h.core->board() ? "this board" : st.banned(c.board) ? "banned" : netHas(h.blocks, c.board) ? "blocked" : "";
        out.printf("%s | %s | %s\n  %s:%d | %s | %s | %s\n", netSafe(netCardName(&c).c_str()).c_str(), c.board.c_str(), netSafe(c.mac).c_str(),
                   netSafe(c.host).c_str(), c.port, link.c_str(), seen.c_str(), flags);
    });
}

// ---- User Net (net/members.h): applying, voting, the lists, and Rev's admit, deny, veto and votes

// The seq of the board's application held now, as admit and deny name it ("1" when none is held; @NETADMIT and
// @NETDENY refuse that case first, and the seals due always have one).
static std::string netAppSeq(const NetHost& h, const std::string& board) {
    uint32_t off, seq = 1;
    h.core->members().heldApp(board, off, &seq);
    return std::to_string(seq);
}

// "@NET APPS": one line per held application, the status from one tally for them all (members(), denied()). The
// fields come from other boards: control bytes are shown as '?'.
static void netApps(const NetHost& h, Print& out) {
    const axis::net::NetMembers& mem = h.core->members();
    const axis::net::NetState&   st = h.core->state();
    std::vector<std::string>     in = mem.members(), dn = mem.denied();
    std::string                  line;
    uint32_t                     vo;
    bool                         any = false;
    for (uint32_t off : mem.offsets()) {
        axis::net::NetApp  a;
        axis::net::NetVote v;
        if (!netAppsReadAt(off, line)) continue;
        if (line[0] != 'A') break;  // the votes come after the applications
        if (!axis::net::decode(line, a)) continue;
        const axis::net::NetNotice*  n = st.memberNotice(a.board);
        axis::net::NetMembers::Tally t = mem.tally(a.board);
        const char* is = st.banned(a.board) ? "banned" : netHas(in, a.board) ? "member" : netHas(dn, a.board) ? "denied" : n && n->kind == "veto" ? "vetoed" : "pending";
        const char* mine = mem.heldVote(h.core->board(), a.board, vo) && netAppsReadAt(vo, line) && axis::net::decode(line, v) && v.appSeq == a.seq ? (v.yes ? "yes" : "no") : "-";
        out.printf("%s | %s | %s | %s | %s | %s | yes %u no %u | you %s\n", a.board.c_str(), netSafe(a.name).c_str(), netSafe(a.sysop).c_str(),
                   netSafe(a.location).c_str(), netSafe(a.mac).c_str(), is, (unsigned)t.yes, (unsigned)t.no, mine);
        any = true;
    }
    if (!any) out.println("axis: no applications held");
}

// "@NET WAITING": the applicants this board has not voted on (waitingFor), one a line: "<board> <yes> <no> <name>, <location>".
static void netWaiting(const NetHost& h, Print& out) {
    const axis::net::NetMembers& mem = h.core->members();
    std::string                  line;
    for (const std::string& b : mem.waitingFor(h.core->board())) {
        axis::net::NetApp            a;
        axis::net::NetMembers::Tally t = mem.tally(b);
        uint32_t                     off;
        if (mem.heldApp(b, off) && netAppsReadAt(off, line)) axis::net::decode(line, a);
        out.printf("%s %u %u %s, %s\n", b.c_str(), (unsigned)t.yes, (unsigned)t.no, a.name.empty() ? "?" : netSafe(a.name).c_str(), netSafe(a.location).c_str());
    }
}

// "@NET APP <board>": the board's application held now, field by field, its counts, then each member's vote on it:
// "vote yes|no, <name or board>: <comment>".
static void netAppShow(const NetHost& h, const std::string& board, Print& out) {
    const axis::net::NetMembers& mem = h.core->members();
    const axis::net::NetState&   st = h.core->state();
    uint32_t                     off, seq = 0;
    std::string                  line;
    if (!mem.heldApp(board, off, &seq) || !netAppsReadAt(off, line) || line[0] != 'A') {
        out.println("axis: no application held from that board");
        return;
    }
    axis::net::recordFields(line, netOutLine, &out);
    axis::net::NetMembers::Tally t = mem.tally(board);
    out.printf("counts yes %u of %u, no %u of %u\n", (unsigned)t.yes, (unsigned)st.votesYes(), (unsigned)t.no, (unsigned)st.votesNo());
    for (const std::string& m : mem.members()) {
        axis::net::NetVote        v;
        const axis::net::NetCard* c = st.card(m);
        if (mem.heldVote(m, board, off) && netAppsReadAt(off, line) && axis::net::decode(line, v) && v.appSeq == seq)
            out.printf("vote %s, %s: %s\n", v.yes ? "yes" : "no", c ? netSafe(c->name).c_str() : m.c_str(), netSafe(v.comment).c_str());
    }
}

// APPS, MEMBERS (from @NET), @NETAPPLY, @NETVOTE (this board), @NETADMIT, @NETDENY, @NETVETO, @NETVOTES (Rev's board).
// t is the first word of the arguments (lowercase), rest what follows it as typed. False if word is none of them.
static bool netUserCommand(NetHost& h, const std::string& word, const std::string& arg, const std::string& a, const std::string& t,
                           const std::string& rest, Print& out) {
    bool apply = word == "@NETAPPLY", vote = word == "@NETVOTE", veto = word == "@NETVETO", list = word == "APPS" || word == "MEMBERS";
    bool rev = word == "@NETADMIT" || word == "@NETDENY" || veto, votes = word == "@NETVOTES";
    if (!apply && !vote && !rev && !votes && !list) return false;
    axis::net::NetCore&    core = *h.core;
    axis::net::NetMembers& mem = core.members();
    using S = axis::net::NetMembers::Status;
    uint32_t    now = unixNow(), off, appSeq = 0;
    const char* no = !h.appsLoaded ? "User Net is still loading" : (rev || votes) && netRevSlot(h) < 0 ? "not Rev's board" : nullptr;
    bool        ok = false;
    if (!no && word == "APPS") netApps(h, out);
    else if (!no && list)
        for (const std::string& b : mem.members()) {
            const axis::net::NetCard* c = core.state().card(b);
            out.printf("%s %s%s\n", b.c_str(), c ? netSafe(c->name).c_str() : "-", core.isRevBoard(b) ? " (Rev)" : "");
        }
    if (!no && list) return true;
    if (!no && votes) {  // "yes=<n>,no=<n>", each 1 to 20: NetState refuses any other value, and then no seq is used
        if (a.compare(0, 4, "yes=") != 0 || !netNotice(h, "votes", "net", a, &out)) out.println("axis: @NETVOTES yes=<1-20>,no=<1-20>");
        return true;
    }
    if (!no && rev) {
        if (!netIsBoard(t) || t == core.board() || core.isRevBoard(t)) no = "bad board id (never this board or Rev's)";
        else if (core.state().memberNoticeCount() >= axis::net::kMaxMemberNotices && !core.state().memberNotice(t)) no = "the member table is full (64 boards)";
        else if (!veto && !mem.heldApp(t, off)) no = "no application held from that board (admit and deny name the application they decide)";
        else if (veto && rest != "YES") {
            out.printf("axis: a veto keeps %s out of User Net for good; to send: @NETVETO %s YES\n", t.c_str(), t.c_str());
            return true;
        } else {  // admit and deny name the application they decide, the one held now
            netNotice(h, veto ? "veto" : word == "@NETADMIT" ? "admit" : "deny", t, veto ? std::string() : netAppSeq(h, t), &out);
            return true;
        }
    } else if (!no && apply) {
        S st = mem.status(core.board());
        no = !boardCfg->netUser ? "net_user is off" : st == S::Member ? "already a member" : st == S::Pending ? "already applied" :
             st == S::Vetoed ? "vetoed" : h.seqFloorDue ? "waiting for the clock" : nullptr;
        // 30 days after a deny: from the day Rev's deny was applied here, or, denied by the count with no deny of Rev's,
        // from the held application's own signed date. A local gate only: the tally does not change.
        static char wait[64];
        if (!no && st == S::Denied && now > axis::net::kNetClockSet) {
            const axis::net::NetNotice* dn = core.state().memberNotice(core.board());
            uint32_t heldSeq = 0;
            bool     hasApp = mem.heldApp(core.board(), off, &heldSeq);
            // As the tally: a deny stands unless the held application is newer than the one it names.
            if (!dn || dn->kind != "deny" || (hasApp && heldSeq > (uint32_t)std::strtoul(dn->value.c_str(), nullptr, 10))) {
                std::string       line;
                axis::net::NetApp held;
                // (an application dated before the clock was set, or one that cannot be read, does not hold it back)
                if (hasApp && netAppsReadAt(off, line) && axis::net::decode(line, held) && held.date > axis::net::kNetClockSet &&
                    now / 86400 < held.date / 86400 + 30) {
                    unsigned left = (unsigned)(held.date / 86400 + 30 - now / 86400);
                    std::snprintf(wait, sizeof wait, "denied by the vote count; may apply again in %u day%s", left, left == 1 ? "" : "s");
                    no = wait;
                }
            } else {
                uint32_t d = core.state().memberNoticeDay(core.board());
                if (d < axis::net::kNetClockDay) d = now / 86400;  // a deny from before the clock
                if (now / 86400 < d + 30) no = "denied less than 30 days ago";
            }
        }
        if (!no) {
            axis::net::NetApp ap;
            ap.pub = axis::net::toHex(h.key.pub, 32);
            ap.board = core.board();
            ap.name = netField(boardCfg->netName.empty() ? boardCfg->bbsName : boardCfg->netName, 36);
            ap.sysop = netField(boardCfg->netSysopName.empty() ? boardCfg->sysopHandle : boardCfg->netSysopName, 36);
            ap.location = netField(boardCfg->netLocation, 36);
            ap.mac = WiFi.macAddress().c_str();
            for (char& ch : ap.mac) ch = (char)std::tolower((unsigned char)ch);
            ap.blurb = netField(arg, axis::net::kNetMaxBlurb);
            ap.date = now;
            ap.seq = h.appSeq;  // the next one is saved first: a reset never hands this seq out again
            ok = netSeqWrite(core.postSeq(), h.cardSeq, h.voteSeq, ap.seq + 1, h.seqFloorDue);
            if (ok) h.appSeq = ap.seq + 1;
            ok = ok && axis::net::signApp(ap, h.key) && core.takeApp(ap, "");
        }
    } else if (!no) {  // @NETVOTE <board> Y|N [comment]
        std::string yn = rest.substr(0, rest.find(' ')), comment = yn.size() < rest.size() ? rest.substr(yn.size() + 1) : "";
        for (char& ch : yn) ch = (char)std::toupper((unsigned char)ch);
        no = !h.unMember ? "this board is not a member" : !netIsBoard(t) || (yn != "Y" && yn != "N") ? "@NETVOTE <board id> Y|N [comment]" :
             t == core.board() ? "not on this board's own application" : !mem.heldApp(t, off, &appSeq) ? "no application held from that board" :
             h.seqFloorDue ? "waiting for the clock" : nullptr;
        if (!no) {
            axis::net::NetVote v;
            v.voter = core.board();
            v.pub = axis::net::toHex(h.key.pub, 32);
            v.app = t;
            v.appSeq = appSeq;
            v.seq = h.voteSeq;
            v.date = now;
            v.yes = yn == "Y";
            v.comment = netField(comment, axis::net::kNetMaxComment);
            ok = netSeqWrite(core.postSeq(), h.cardSeq, v.seq + 1, h.appSeq, h.seqFloorDue);
            if (ok) h.voteSeq = v.seq + 1;
            ok = ok && axis::net::signVote(v, h.key) && core.takeVote(v, "");
        }
    }
    if (no) out.printf("@ERR %s\n", no);
    else if (ok) {
        netFlood(h);
        logEvent(String("sysop: net ") + (apply ? "apply" : "vote ") + (apply ? "" : t.c_str()));
        out.println(apply ? "@OK applied" : "@OK voted");
    } else out.println("@ERR not taken");
    return true;
}

// "@NETKEY SET <64 hex>" / "@NETKEY CLEAR", typed at the serial console (handleSerial gives the line straight here and
// wipes it after; runCommand refuses it from anywhere else). Rev's daily key goes to internal flash as /netrev.key and
// is used from the next start. The seed is never echoed, logged or kept: it goes from the line into a wiped buffer.
static bool netKeyLine(const String& cmd) {
    static const char kWord[] = "@NETKEY";
    if (cmd.length() < 7) return false;
    for (int i = 0; i < 7; i++)
        if (std::toupper((unsigned char)cmd[i]) != kWord[i]) return false;
    return cmd.length() == 7 || cmd[7] == ' ';
}
static void netKeyCommand(const String& cmd) {
    const char* s = cmd.c_str() + 7;
    while (*s == ' ') s++;
    auto said = [&s](const char* w) {
        size_t n = std::strlen(w);
        if (strncasecmp(s, w, n) != 0 || (s[n] && s[n] != ' ')) return false;
        s += n;
        while (*s == ' ') s++;
        return true;
    };
    if (asFollower()) {
        Serial.println("@ERR the net key is set on the main board");
    } else if (said("CLEAR")) {
        bool had = LittleFS.exists("/netrev.key");
        bool ok = !had || LittleFS.remove("/netrev.key");
        if (ok && had) {
            logEvent("sysop: net key cleared");
            netNote("netkey cleared");
        }
        Serial.println(!ok ? "@ERR net key not cleared" : had ? "@OK net key cleared, @RESTART to use the board's own key" : "@OK no net key was set");
    } else if (said("SET")) {
        uint8_t seed[32];
        bool    ok = true;
        for (int i = 0; i < 64 && ok; i++) {
            char c = s[i];
            int  d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
            ok = d >= 0;
            if (i % 2 == 0) seed[i / 2] = (uint8_t)(d << 4);
            else seed[i / 2] |= (uint8_t)d;
        }
        for (int i = 64; ok && s[i]; i++) ok = s[i] == ' ';
        axis::net::KeyPair k;
        if (ok) {
            k = axis::net::keyFromSeed(seed);
            std::string pub = axis::net::toHex(k.pub, 32);
            ok = pub == axis::net::revKey(0) || pub == axis::net::revKey(1);
            crypto_wipe(&k, sizeof k);
            if (!ok) Serial.println("@ERR that is not one of Rev's keys");
        } else {
            Serial.println("@ERR @NETKEY SET wants 64 hex characters");
        }
        if (ok && !netSeedWrite(seed, "/netrev.key")) {
            ok = false;
            Serial.println("@ERR net key not saved");
        }
        crypto_wipe(seed, sizeof seed);
        if (ok) {
            logEvent("sysop: net key set");
            netNote("netkey set");
            Serial.println("@OK net key saved, @RESTART to use it");
        }
    } else {
        Serial.println("@ERR @NETKEY SET <64 hex> or @NETKEY CLEAR");
    }
}

// Every other @NET command (serial, the sysop menu, the PC panel). The main runs them; a follower forwards them.
static void netCommand(const String& cmd, Print& out) {
    if (netKeyLine(cmd)) {  // never from here (only handleSerial takes it): refused before any copy, and the line wiped
        std::memset((void*)cmd.c_str(), 0, cmd.length());
        out.println("@ERR serial console only");
        return;
    }
    std::string c = cmd.c_str(), arg;
    size_t      sp = c.find(' ');
    std::string word = c.substr(0, sp);
    if (sp != std::string::npos) arg = c.substr(sp + 1);
    while (!arg.empty() && arg.back() == ' ') arg.pop_back();
    while (!arg.empty() && arg[0] == ' ') arg.erase(0, 1);
    if (asFollower()) {
        out.println("axis: Sysop Net runs on the main board");
        return;
    }
    if (!netHost) {
        out.println("axis: Sysop Net is not running");
        return;
    }
    NetHost& h = *netHost;
    std::string a = arg;
    for (char& ch : a) ch = (char)std::tolower((unsigned char)ch);
    std::string t = a.substr(0, a.find(' ')), rest;  // the first word (lowercase), then what follows it as typed
    if (t.size() < arg.size()) rest = arg.substr(arg.find_first_not_of(' ', t.size()));
    if (word == "@NET") {
        std::string up = arg;
        for (char& ch : up) ch = (char)std::toupper((unsigned char)ch);
        if (up.empty()) netStatus(h, out);
        else if (up == "SYNC") {
            h.postDue = true;
            h.postMs = millis() - kNetPostDialMs;
            out.println("@OK dialing soon");
        } else if (up == "PEERS") netPeers(h, out);
        else if (up == "LOCAL") {
            for (const std::string& s : h.hides) out.printf("hide %s\n", s.c_str());
            for (const std::string& s : h.blocks) out.printf("block %s\n", s.c_str());
        } else if (up.compare(0, 6, "RECORD") == 0) {
            if (netRecordBrowse(h, a.substr(6), out)) return;  // FIND, KIND, MONTHS, SHOW
            char*         e = nullptr;
            unsigned long n = std::strtoul(up.c_str() + 6, &e, 10), skip = std::strtoul(e, nullptr, 10);
            if (!n) n = 20;
            std::vector<std::string> v;
            netRecordTail((size_t)skip, (size_t)std::min<unsigned long>(n, 100), v);
            for (const std::string& l : v) {
                size_t s1 = l.find(' ');
                out.printf("%s %s\n", axis::net::netDateText((uint32_t)std::strtoul(l.c_str(), nullptr, 10)).c_str(),
                           s1 == std::string::npos ? "" : l.c_str() + s1 + 1);
            }
            if (v.empty()) out.println("axis: the record is empty");
        } else if (up == "APPS" || up == "MEMBERS") netUserCommand(h, up, arg, a, t, rest, out);  // (only its own words: never "@NET @NETAPPLY")
        else if ((up == "WAITING" || t == "app") && !h.appsLoaded) out.println("axis: User Net is still loading");
        else if (up == "WAITING") netWaiting(h, out);
        else if (t == "app") netAppShow(h, a.size() > 4 ? a.substr(4) : std::string(), out);
        else if (!netFilesCommand(h, up, a, out))
            out.println("axis: @NET, SYNC, PEERS, LOCAL, APPS, MEMBERS, WAITING, APP <board>, RECORD [n [skip]], RECORD FIND|KIND|MONTHS|SHOW, "
                        "FILES [skip], LISTALL <area>, FETCHTEST <id>");
        return;
    }
    // local moderation: this board only
    bool hide = word == "@NETHIDE", unhide = word == "@NETUNHIDE", block = word == "@NETBLOCK", unblock = word == "@NETUNBLOCK";
    if (hide || unhide || block || unblock) {
        std::vector<std::string>& v = hide || unhide ? h.hides : h.blocks;
        bool                      ok = hide || unhide ? netIsPostId(a) : netIsBoard(a) && a != h.core->board();
        if (!ok) {
            out.println("@ERR bad board or post id");
            return;
        }
        auto it = std::find(v.begin(), v.end(), a);
        if (hide || block) {
            if (it == v.end() && v.size() >= (hide ? kNetMaxHides : kNetMaxBlocks)) {
                out.println("@ERR the list is full");
                return;
            }
            if (it == v.end()) v.push_back(a);
            if (block)
                for (NetSlot* s : {&h.in, &h.out})
                    if (s->s && s->s->peer() == a) netClose(h, *s, "blocked");
        } else if (it != v.end()) {
            v.erase(it);
        }
        h.core->moderationChanged();  // (Net Files: what is shown changed)
        ok = netLocalSave(h);
        for (char& ch : word) ch = (char)std::tolower((unsigned char)ch);
        netNote(word.substr(4) + " " + a);
        out.printf(ok ? "@OK %s %s\n" : "@ERR %s %s not saved\n", word.c_str() + 4, a.c_str());
        return;
    }
    if (netUserCommand(h, word, arg, a, t, rest, out)) return;
    // Rev's notices: only on Rev's board
    if (netRevSlot(h) < 0) {
        out.println("@ERR not Rev's board");
        return;
    }
    static const char* const kRev[4][2] = {{"@NETBAN", "ban"}, {"@NETHOLD", "hold"}, {"@NETKILL", "kill"}, {"@NETLIFT", "lift"}};
    for (const auto& r : kRev)
        if (word == r[0]) {  // ban: a board; hold, kill: a post; lift: either (the target's shape picks the group)
            bool        board = netIsBoard(t), post = netIsPostId(t), kill = r[1][0] == 'k';
            if (r[1][0] == 'b' && board && (t == h.core->board() || h.core->isRevBoard(t))) out.println("@ERR never this board or Rev's");
            else if (!(r[1][0] == 'b' ? board : r[1][0] == 'l' ? board || post : post)) out.println("@ERR bad board or post id");
            else if (kill && (arg.size() < 4 || arg.compare(arg.size() - 4, 4, " YES") != 0))
                out.printf("axis: kill drops %s on every board; to send: @NETKILL %s YES\n", t.c_str(), t.c_str());
            else netNotice(h, r[1], t, "", &out);
            return;
        }
    if (word == "@NETLIMITS") {
        unsigned long posts = 0, bytes = 0;
        char*         e = nullptr;
        bool          ok = a.compare(0, 6, "posts=") == 0 && std::isdigit((unsigned char)a[6]);
        if (ok) posts = std::strtoul(a.c_str() + 6, &e, 10);
        ok = ok && std::strncmp(e, ",bytes=", 7) == 0 && std::isdigit((unsigned char)e[7]);
        if (ok) bytes = std::strtoul(e + 7, &e, 10);
        if (ok && !*e && posts && bytes && posts < 100000 && bytes < 100000000)
            netNotice(h, "limits", "net", "posts=" + std::to_string(posts) + ",bytes=" + std::to_string(bytes), &out);
        else out.println("@ERR @NETLIMITS posts=<n>,bytes=<n>");
    } else if (word == "@NETROTATE") {
        std::string hex = a.substr(0, a.find(' ')), yes = a.size() > 65 ? arg.substr(65) : "";
        if (&out != &Serial) out.println("@ERR serial console only");
        else if (hex.size() != 64 || hex.find_first_not_of("0123456789abcdef") != std::string::npos) out.println("@ERR @NETROTATE <64 hex public key> [YES]");
        else if (yes != "YES") out.printf("axis: replaces Rev's key %d net-wide; to send: @NETROTATE %s YES\n", 1 - netRevSlot(h), hex.c_str());
        else netNotice(h, "rotate", netRevSlot(h) == 0 ? "1" : "0", hex, &out);
    } else {
        out.println("axis: unknown @NET command");
    }
}

// The User Net areas this board carries (a mask): all four on Rev's board, else net_user_areas while net_user is on.
static uint8_t netCarried(const NetHost& h) { return netRevSlot(h) >= 0 ? 15 : boardCfg->netUser ? boardCfg->netUserAreas : 0; }
static void netCarry(NetHost& h, uint8_t m) {
    std::vector<std::string> v;
    for (size_t i = 0; i < 4; i++)
        if (m >> i & 1) v.push_back(axis::net::userNetAreas()[i].id);
    h.core->setUserAreas(std::move(v));
    h.carried = m;
}

// The boot load of /net/apps.txt, two lines a pass (each a signature check) and only with heap to spare; sessions
// and the seals wait for its end. Then what this board carries and whether it is a member are taken as they are (a
// restart resets no User Net pull), and the areas and seals are worked out on the next pass.
static void netAppsLoadStep(NetHost& h) {
    if (heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) < kNetHeapFloor) return;
    axis::net::NetMembers& mem = h.core->members();
    FILE*                  f = std::fopen(netPath("apps.txt").c_str(), "rb");
    bool                   more = f && std::fseek(f, (long)h.appsPos, SEEK_SET) == 0;
    std::string            line;
    for (int i = 0; more && i < 2; i++) {
        uint32_t at = h.appsPos;
        more = netAppsNext(f, h.appsPos, line);
        if (line.empty()) continue;
        mem.loadLine(line, at);
        h.appsLines++;
        h.appsBytes += line.size() + 1;
    }
    if (f) std::fclose(f);
    if (more) return;
    mem.endLoad();
    h.appsLoaded = true;
    h.appsHeld = h.appsLines ? (uint32_t)((uint64_t)h.appsBytes * mem.offsets().size() / h.appsLines) : 0;
    h.cfgUser = boardCfg->netUser;
    h.cfgAreas = boardCfg->netUserAreas;
    h.cfgLevel = boardCfg->netUserLevel;
    netCarry(h, netCarried(h));
    h.unMember = h.core->selfMember();
    h.unDirty = true;
}

// /net/apps.txt rewritten with only the lines held now, once it is past twice their size (64 KB at least): streamed
// into a temp file put in place of it, then each held record's offset is taken from the file in place (the old one if
// the rewrite failed). Only with no session open.
static void netAppsCompact(NetHost& h) {
    std::string path = netPath("apps.txt"), tmp = netTmpPath("apps.txt"), line;
    struct stat st;
    if (::stat(path.c_str(), &st) != 0 || (uint32_t)st.st_size <= std::max<uint32_t>(2 * h.appsHeld, 64 * 1024)) return;
    axis::net::NetMembers& mem = h.core->members();
    FILE*                  in = std::fopen(path.c_str(), "rb");
    FILE*                  out = in ? std::fopen(tmp.c_str(), "wb") : nullptr;
    bool                   ok = out != nullptr;
    uint32_t               pos = 0, size = 0;
    while (ok && netAppsNext(in, pos, line))
        if (!line.empty() && mem.holds(line)) {
            ok = std::fwrite(line.data(), 1, line.size(), out) == line.size() && std::fputc('\n', out) != EOF;
            size += line.size() + 1;
        }
    if (in) std::fclose(in);
    if (out) ok = std::fclose(out) == 0 && ok;
    if (ok) {  // the old file steps aside as apps.old until the new one is in place (netStart restores it if not)
        std::string old = netPath("apps.old");
        ::unlink(old.c_str());
        ok = ::rename(path.c_str(), old.c_str()) == 0;
        if (ok && ::rename(tmp.c_str(), path.c_str()) != 0) {
            ::rename(old.c_str(), path.c_str());
            ok = false;
        }
        if (ok) ::unlink(old.c_str());
    }
    if (!ok) ::unlink(tmp.c_str());
    if ((in = std::fopen(path.c_str(), "rb")) != nullptr) {
        pos = 0;
        for (uint32_t at = 0; netAppsNext(in, pos, line); at = pos)
            if (!line.empty()) mem.setOffset(line, at);
        std::fclose(in);
    }
    h.appsHeld = ok ? size : (uint32_t)st.st_size;  // a failed one waits until the file is twice this
}

// User Net upkeep, at most once a pass and only when something changed (net_user, net_user_areas, net_user_level, or
// an app, vote, card or notice taken): the areas carried, whether this board is a member (one tally), the areas callers
// get, a new User Net pull when this board became a member or carries a new area, and on Rev's board the admits due
// (four notices a pass at most; each one taken brings this back next pass).
static void netUserTick(NetHost& h) {
    const axis::bbs::BbsConfig& c = *boardCfg;
    bool cfg = c.netUser != h.cfgUser || c.netUserAreas != h.cfgAreas || c.netUserLevel != h.cfgLevel;
    if ((!cfg && !h.unDirty) || heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) < kNetHeapFloor) return;
    axis::net::NetCore& core = *h.core;
    bool                reset = false;
    if (cfg) {
        h.cfgUser = c.netUser;
        h.cfgAreas = c.netUserAreas;
        h.cfgLevel = c.netUserLevel;
        uint8_t m = netCarried(h);
        reset = (m & ~h.carried) != 0;  // an area more
        netCarry(h, m);
        netAreasGen++;  // (the level may have changed)
    }
    h.unDirty = false;
    bool member = core.selfMember();
    reset = reset || (member && !h.unMember);
    h.unMember = member;
    if (reset) {
        core.resetUserHistory();
        h.dirty = true;
    }
    uint8_t un = member && c.netUser ? h.carried : 0;
    if (un != netUnMask) {
        netUnMask = un;
        netAreasGen++;
        if (cluster && cluster->role() == axis::cluster::Role::Main) cluster->shareSettings();  // the followers' callers too
    }
    if (netRevSlot(h) < 0) return;
    // Only admits are signed: a deny by the count is already a tally fact on every board, and a signed deny would
    // use one of the 64 member notice slots for each applicant turned down. Rev denies by hand (@NETDENY).
    int n = 0;
    for (const auto& d : core.members().sealsDue()) {
        if (d.second != axis::net::NetMembers::Status::Member) continue;
        if (n < 4 && netNotice(h, "admit", d.first, netAppSeq(h, d.first), nullptr)) {
            n++;
            h.sealSaid = false;  // a later failure is logged again
        } else if (n < 4 && !h.sealSaid) {  // (the next change tries again)
            logEvent(String("net: seal for ") + d.first.c_str() + " not taken");
            h.sealSaid = true;
        }
    }
}

static bool netStart() {
    netStartMs = millis();
    callerMessages();  // makes the board's own store
    if (!localMsgs) return false;
    ::mkdir((std::string(storageRoot()) + "/net").c_str(), 0777);
    std::unique_ptr<NetHost> h(new (std::nothrow) NetHost);
    if (!h || !netKeyLoad(h->key)) {
        if (!netStartSaid) logEvent("net: the board key (/netrev.key or /netkey.txt) could not be read or made, Sysop Net is off, trying every 10 min");
        netStartSaid = true;
        return false;
    }
    h->file.path = netPath("outlog.txt");
    h->file.tmp = netTmpPath("outlog.txt");
    NetLogFile* f = &h->file;
    axis::net::FileOutLog::Io io;
    io.append = [f](const std::string& l) { return f->append(l); };
    io.readLine = [f](uint32_t n, std::string& l) { return f->readLine(n, l); };
    io.lineCount = [f] { return f->lineCount(); };
    io.replace = [f](const std::string& header, uint32_t from) { return f->replace(header, from); };
    h->log.reset(new (std::nothrow) axis::net::FileOutLog(std::move(io)));
    if (h->log) h->core.reset(new (std::nothrow) axis::net::NetCore(*localMsgs, h->key, unixNow, *h->log));
    if (h->core) h->store.reset(new (std::nothrow) NetStore(*localMsgs, *h->core));
    if (!h->store) {
        if (!netStartSaid) logEvent("net: not enough memory, Sysop Net is off" + heapText());
        netStartSaid = true;
        return false;
    }
    axis::net::NetCore& core = *h->core;
    uint32_t            day = unixNow() / 86400;
    ::unlink((std::string(storageRoot()) + "/net/record.tmp").c_str());  // a cut stopped by a reset
    h->record.reset(new (std::nothrow) axis::net::NetRecord(netRecordAppend, netYyyymm, unixNow));
    netLocalLoad(*h);
    {
        NetHost*              hp = h.get();  // (the host outlives the core: both live in netHost from here on)
        axis::net::NetRecord* rec = h->record.get();
        core.setOnEvent([rec, hp](const std::string& who, const std::string& what, const std::string& line) {
            // an app, vote, card or notice: User Net looks again (netUserTick); not for a post or a listing
            if (what[0] != 'p' && what[0] != 'l') hp->unDirty = true;
            if (!rec || (!storageHasSd() && what.compare(0, 14, "post duplicate") == 0)) return;  // internal flash: spare it the wear
            rec->note(who, what, line);
        });
        core.setLocalFilter([hp](const std::string& b) { return netHas(hp->blocks, b); },
                            [hp](const std::string& id) { return netHas(hp->hides, id); });
        core.setAppsFile(netAppsAppend, netAppsScan, netAppsReadAt);
        netIdxClose();  // (a handle from before is not trusted)
        core.setListingIndex(netIdxRead, netIdxWrite, [](void* p, size_t n) {
            for (size_t i = 0; i < n; i += 4) {
                uint32_t r = esp_random();
                std::memcpy((uint8_t*)p + i, &r, std::min<size_t>(4, n - i));
            }
        });
        if (!storageHasSd()) core.setListingCaps(100, 100);  // internal flash only: 100 live listings, 100 from one board
    }
    core.state().load(netFileRead("state.txt"), day);
    {  // /net/apps.txt is read a few lines a pass from the loop (netAppsLoadStep). A compaction stopped by a reset: with no
        // apps.txt the old file (apps.old) comes back, else the temp file; the others go only once apps.txt is there.
        std::string p = netPath("apps.txt"), t = netTmpPath("apps.txt"), o = netPath("apps.old");
        struct stat st;
        if (::stat(p.c_str(), &st) != 0 && ::rename(o.c_str(), p.c_str()) != 0) ::rename(t.c_str(), p.c_str());
        if (::stat(p.c_str(), &st) == 0) {
            ::unlink(t.c_str());
            ::unlink(o.c_str());
        }
        core.members().beginLoad();
    }
    core.seen().load(netFileRead("seen.txt"));
    core.loadCursors(netFileRead("cursors.txt"));  // (clears the link peers: the first peer goes back after)
    if (core.historyState() < 0) {  // no history flag saved yet: decided now, before a session adds posts
        core.needHistory();
        h->dirty = true;
        netSave(*h, true);  // the decision is on flash now, not up to 30 s later (posts may land first)
    }
    {
        std::string fp = netFileRead("firstpeer.txt", 256);
        size_t      sp = fp.find(' ');
        if (sp != std::string::npos) {
            h->firstKey = fp.substr(0, sp);
            h->firstBoard = fp.substr(sp + 1, 16);
            if (h->firstBoard.size() != 16) {
                h->firstKey.clear();
                h->firstBoard.clear();
            }
        }
        NetTarget t;
        if (netFirstTarget(*h, t) && !t.board.empty()) core.setFirstPeer(t.board);
    }
    {  // the counters: /netseq.txt in internal flash; the old /net/postseq.txt and /net/card.txt are a floor
        uint32_t    v[4] = {0, 0, 0, 0};
        int         n = 0;
        bool        floorDue = false;
        bool        found = netSeqRead(v, n, floorDue) && !floorDue;  // still waiting for the clock: as not found
        uint32_t    post = v[0], card = v[1];
        std::string oldPost = netFileRead("postseq.txt", 32), oldCard = netFileRead("card.txt", 32);
        post = std::max(post, (uint32_t)std::strtoul(oldPost.c_str(), nullptr, 10));
        card = std::max(card, (uint32_t)std::strtoul(oldCard.c_str(), nullptr, 10));
        found = found || !oldPost.empty() || !oldCard.empty();
        uint32_t now = unixNow();
        post = axis::net::netSeqStart(post, found, now);
        if (!found && now > axis::net::kNetClockSet) card = std::max(card, now);
        h->seqFloorDue = !found && now <= axis::net::kNetClockSet;
        h->voteSeq = axis::net::netSeqStart(v[2], found && n > 2, now);  // (a board from before User Net: from the clock)
        h->appSeq = axis::net::netSeqStart(v[3], found && n > 3, now);
        if (!netSeqWrite(post, card, h->voteSeq, h->appSeq, h->seqFloorDue)) {
            if (!netStartSaid) logEvent("net: /netseq.txt could not be written, Sysop Net is off, trying every 10 min");
            netStartSaid = true;
            return false;
        }
        core.setPostSeq(post);
        h->cardSeq = card;
        NetHost* hp = h.get();
        core.setSeqReserve([hp](uint32_t next) { return netSeqWrite(next, hp->cardSeq, hp->voteSeq, hp->appSeq, hp->seqFloorDue); });
    }
    h->postSeq = core.postSeq();
    {  // the card held in the state; a new one only on first boot (a changed one is made once the log is open)
        axis::net::NetCard        c = netCardNow(*h);
        const axis::net::NetCard* held = core.state().card(c.board);
        if (held) {
            h->card = *held;
            core.setOwnCard(*held);
        } else if (!netCardUpdate(*h)) {
            if (!netStartSaid) logEvent("net: no card for this board, Sysop Net is off");
            netStartSaid = true;
            return false;
        }
    }
    netLogOpen(*h);
    core.setRetention((uint32_t)boardCfg->netKeepPosts, (uint32_t)boardCfg->netKeepDays);
    netCardUpdate(*h);
    h->appendFails = core.appendFailures();
    h->appsFails = core.appsFailures();
    h->minuteMs = h->saveMs = h->trimMs = millis();
    h->dialMs = millis() - (uint32_t)boardCfg->netSyncMin * 60000u + 30000u;  // the first dial 30 s from now
    netHost = std::move(h);
    logEvent(String("net: Sysop Net on, board ") + netHost->core->board().c_str() + heapText());
    return true;
}

// From the loop: starts the net on the main, pumps both sessions, dials on schedule, keeps the /net files.
static void netTick() {
#if AXIS_CLUSTER
    bool mainBoard = cluster && (cluster->role() == axis::cluster::Role::Main || cluster->role() == axis::cluster::Role::Off);
#else
    bool mainBoard = true;  // built without the cluster: the board runs alone, so it is its own main
#endif
    bool up = netHost && boardCfg->netSysop && mainBoard;
    if (up != netSharedUp && cluster && cluster->role() == axis::cluster::Role::Main) {  // followers list the areas by it
        netSharedUp = up;
        cluster->shareSettings();
    }
    if (!netHost) {
        if (boardCfg->setupDone && boardCfg->netSysop && mainBoard && millis() > 20000 && (!netStartMs || millis() - netStartMs > 600000u))
            netStart();
        return;
    }
    NetHost& h = *netHost;
    if (!boardCfg->netSysop || !mainBoard) {  // turned off, or no longer the main: the objects stay (nodes hold the store)
        if (h.in.phase != NetPhase::Idle) netClose(h, h.in, "net off");
        if (h.out.phase == NetPhase::Live) netClose(h, h.out, "net off");
        h.out.phase = NetPhase::Idle;
#if AXIS_TRANSFERS
        if (h.serve.s) netServeClose(h, "net off");
        netFetchFail(h, "off");  // (its caller's transfer ends)
        netFetchPump(h);         // a failed fetch nobody takes up is still let go
        netFetchTestStep(h);
#endif
        return;
    }
    if (!h.appsLoaded) {  // no session until the User Net records are in
        netAppsLoadStep(h);
        return;
    }
#if AXIS_TRANSFERS
    netServePump(h);  // Net Files calls, apart from the sync sessions (neither waits for the other)
    netFetchPump(h);
    netFetchTestStep(h);  // @NET FETCHTEST, a few KB a turn
#endif
#if AXIS_FILES
    if (heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) >= kNetHeapFloor) netShareStep(h);
#endif
    bool heapOk = netHeapOk();
    netPump(h, h.in, heapOk);
    netDialStep(h);
    netPump(h, h.out, heapOk);
    axis::net::NetCore& core = *h.core;
    netPosted(h);  // (a post that went around the store)
    netUserTick(h);
    if (heapOk) core.listingIndexStep();  // the listing index: a look at it, or a piece of its rebuild
    if (h.file.broken || (!h.log->ok() && millis() - h.logTryMs >= kNetMinuteMs)) netLogOpen(h);
    if (millis() - h.minuteMs >= kNetMinuteMs) {
        h.minuteMs = millis();
        core.setRetention((uint32_t)boardCfg->netKeepPosts, (uint32_t)boardCfg->netKeepDays);
        netSeqFloor(h);
        netCardUpdate(h);
        if (h.in.phase == NetPhase::Idle && h.out.phase == NetPhase::Idle && heapOk) netAppsCompact(h);
        uint32_t now = unixNow();
        if (now && now / 86400 != h.houseDay && heapOk) {  // once a day: retention, pruning, the log trimmed
            h.houseDay = now / 86400;
            core.trim(now);
            h.dirty = true;
        } else if (!now && heapOk && millis() - h.trimMs >= 24u * 3600u * 1000u) {  // no clock: by count, once a day of uptime
            h.trimMs = millis();
            core.trim(0);
            h.dirty = true;
        }
    }
    if (millis() - h.saveMs >= kNetSaveMs) {
        h.saveMs = millis();
        if (core.appendFailures() > h.appendFails) {
            logEvent(String("net: ") + String(core.appendFailures() - h.appendFails) + " outbound log entries could not be written");
            h.appendFails = core.appendFailures();
        }
        if (core.appsFailures() > h.appsFails) {
            logEvent(String("net: ") + String(core.appsFailures() - h.appsFails) + " application or vote lines could not be written to apps.txt");
            h.appsFails = core.appsFailures();
        }
        if ((h.dirty || h.in.s || h.out.s) && netSave(h)) h.dirty = h.in.s || h.out.s;
    }
    uint32_t every = (uint32_t)(boardCfg->netSyncMin < 1 ? 1 : boardCfg->netSyncMin) * 60000u;
    bool     due = millis() - h.dialMs >= every || (h.postDue && millis() - h.postMs >= kNetPostDialMs);
    if (due && h.out.phase == NetPhase::Idle && h.in.phase == NetPhase::Idle && heapOk && !inflateHeld()) {
        h.dialMs = millis();
        h.postDue = false;
        std::vector<NetTarget> v = netTargets(h);
        if (!v.empty()) netDial(h, v[h.dialIx++ % v.size()]);
    }
}

// True when this board's callers get the Sysop Net areas: on the main, once its NetCore (and so the NetPostingStore)
// is up and net_sysop is on; on a follower, when the main's last settings frame said so (never a request: no wait).
static bool netAreasOn() { return asFollower() ? netMainUp : netHost && boardCfg->netSysop; }

// The settings a new Node gets: the board's, plus the four Sysop Net areas for the sysop alone while the net runs, then
// the User Net areas callers get now (netUnMask) at net_user_level. Never saved: they are not in boardCfg->msgAreas.
static std::shared_ptr<const axis::bbs::BbsConfig> nodeCfg() {
    if (!netAreasOn()) return boardCfg;
    // one shared copy per board settings, rebuilt when boardCfg is replaced or the User Net areas may have changed
    static std::shared_ptr<const axis::bbs::BbsConfig> base, withNet;
    static uint32_t                                    gen = 0;
    if (!withNet || base != boardCfg || gen != netAreasGen) {
        auto c = std::make_shared<axis::bbs::BbsConfig>(*boardCfg);
        for (const axis::net::NetArea& a : axis::net::sysopNetAreas()) {
            axis::bbs::FileArea f;
            f.id = a.id;
            f.name = a.name;
            f.minSecurity = axis::bbs::kSysopSecurity;
            c->msgAreas.push_back(f);
        }
        for (size_t i = 0; i < 4; i++)  // (their names say "User Net: ")
            if (netUnMask >> i & 1) {
                axis::bbs::FileArea f;
                f.id = axis::net::userNetAreas()[i].id;
                f.name = axis::net::userNetAreas()[i].name;
                f.minSecurity = boardCfg->netUserLevel;
                c->msgAreas.push_back(f);
            }
        gen = netAreasGen;
        base = boardCfg;
        withNet = std::move(c);
    }
    return withNet;
}
#else
static std::shared_ptr<const axis::bbs::BbsConfig> nodeCfg() { return boardCfg; }
#endif

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
#if AXIS_NETS && AXIS_TRANSFERS
        if (first == "AXISFILE 1 ") {  // Net Files: a fetch call; the rest of its line is the server's to read
            netServeAccept(c, first);
            return true;
        }
#endif
#if AXIS_NETS
        if (first == "AXISNET 2 ") {  // Sysop Net: the rest of the hello (a whole card) is the session's to read
            netAccept(c, first);
            return true;
        }
#endif
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
    std::string greet = axis::bbs::axisGreet(axisSelfCard(), boardData.listLoad("axisnet"), axisNotesFor(them));
    c.print(greet.c_str());
    c.flush();
    delay(20);
    c.stop();
    logEvent(String("axis net: spoke with ") + (them.name.empty() ? them.host.c_str() : them.name.c_str()));
    return true;
}

static void axisProbeOne() {
    if (asFollower() || axisBridge.on) return;
    std::vector<std::string> lines = boardData.listLoad("axisnet");
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
    out.print(axis::bbs::axisGreet(axisSelfCard(), boardData.listLoad("axisnet"), axisNotesFor(peer)).c_str());
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
__attribute__((noinline)) static bool axisPump(axis::app::Session& s) {
    axis::net::TelnetStream* line = s.line.get();
    axis::bbs::Node* node = s.node.get();
    if (!axisBridge.on || axisBridge.session != s.slot || !s.line || !s.node) return false;
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

__attribute__((noinline)) static void axisStart(axis::app::Session& s) {
    axis::bbs::Node* node = s.node.get();
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
    axisBridge.session = s.slot;
    axisBridge.held.clear();
    axisBridge.start = millis();
    axisBridge.on = true;
    logEvent(String("axis net: calling ") + host.c_str());
}

static void answerIn(int slot, WiFiClient incoming, const std::string& giveBack = {});

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
        if (!sessionLine(i) && (i == 0 || (boardCfg->setupDone && mode == Mode::Normal))) free = (int)i;
    if (free >= 0) {
        viewCacheDrop();  // a caller gets a session here: the pass steps aside first (not for board probes or net peers above)
        answerIn(free, incoming, giveBack);
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
    w.bbsName = boardCfg->bbsName;
    w.sysop = boardCfg->sysopHandle;
    w.lastCaller = statsLastCaller();
    w.ip = WiFi.localIP().toString().c_str();
    std::string t = timeText().c_str();  // "YYYY-MM-DD HH:MM"
    if (t.size() >= 16) w.date = t.substr(0, 10), w.time = t.substr(11, 5);
    w.page = pageText.c_str();
    if (cluster && cluster->nodes().size() > 1) {
        for (auto& n : cluster->nodes()) w.nodes.push_back({n.number, n.handle, n.activity, "", axis::bbs::nodePlatform(n.id)});
        std::sort(w.nodes.begin(), w.nodes.end(), [](const axis::bbs::WfcNode& a, const axis::bbs::WfcNode& b) { return a.number < b.number; });
    } else {
        for (size_t i = 0; i < sessions.size(); i++) {
            axis::bbs::Node* n = sessionNode(i);
            w.nodes.push_back({sessionNumber((int)i), n ? n->handle() : std::string(), n ? "Online" : "", n ? n->linkShown() : "",
                               axis::bbs::nodePlatform(WiFi.macAddress().c_str())});
        }
    }  // (in a cluster the label rides in each board's activity: serviceSession)
#if AXIS_MODEMLINK
    w.radio = axis::bbs::wfcRadio(lastRadio, lastRadioWho);
#endif
    const auto& ev = events.lines();
    for (auto it = ev.rbegin(); it != ev.rend() && w.events.size() < 8; ++it) w.events.push_back(*it);
    stats.rollDayCounters();
    axis::bbs::DayStats st = statsShown();
    w.callsToday = st.calls;
    w.postsToday = st.posts;
    w.uploadsToday = st.uploads;
    w.totalCalls = st.total;
    w.sdFreeKb = (uint32_t)(storageFreeBytes() >> 10);
    w.ramFreeKb = heap_caps_get_free_size(MALLOC_CAP_8BIT) / 1024;
    w.uptimeS = millis() / 1000;
    w.main = !asFollower();
    if (sdNotice()) w.notice = axis::bbs::kSdNotice;
    return w;
}

// A chat line, a sysop page or a node line for this board's console and sessions: the cluster's onChat calls it for
// every line that reaches this board, and a board built without the cluster calls it directly. Node lines never go
// back to the sender.
static void deliverChat(const axis::cluster::ChatLine& l) {
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
            if (l.kind >= 2) {
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
}

static bool panelConsoleNew = false;  // answerIn: the next session is the panel's own sysop console
static axis::app::Services& services();
// A call into session `slot`: the board's line (or the panel's own console), then the Session's first screen.
static void answerIn(int slot, WiFiClient incoming, const std::string& giveBack) {
    WifiStream* l = panelConsoleNew ? new PanelStream() : new WifiStream(incoming);
    l->tapNode = sessionNumber(slot);
    logEvent((panelConsoleNew ? String("sysop console from the panel") : "call from " + l->remoteIP().toString()) + " (heap " + String(heap_caps_get_free_size(MALLOC_CAP_8BIT) / 1024) +
             " KB, largest " + String(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) / 1024) + " KB)");
    std::unique_ptr<axis::net::TelnetStream> owned(l);
    sessions[(size_t)slot].reset(new axis::app::Session(slot, std::move(owned), services()));
    axis::app::Session& s = *sessions[(size_t)slot];
    if ((size_t)slot < slotStatus.size()) s.lastStatus = slotStatus[(size_t)slot];  // the slot's last status line, as before
    if (!giveBack.empty()) l->inject(giveBack);
    TurnOf on(&s);
    s.open(panelConsoleNew);
}
// ------------------------------------------------------------------ doors
// (A caller's door, LORD's mail and the transfer are the Session's: app/session.cpp. The board's door folders are here.)
static std::string lordFolderId() {
    for (auto& d : boardCfg->doors)
        if (d.exe == "*LORD" && !d.id.empty()) return d.id;
    return "lord";
}

// Registration, and a later validation, write one warrior. A follower writes the main's file.
static bool writeWarrior(const std::string& handle) {
    if (!AXIS_LORD) return true;  // no LORD in this build: no warrior to write
    std::string id = lordFolderId();
    bool remote = asFollower() && (!storageHasSd() || cluster->mainKind() == axis::cluster::CardKind::Mirror);  // (a mirror main: its copy is the game)
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

// @PUT @LS @TYPE @RM @MKDIR @BAUD from the serial console (tools/sdput.py). @PUT and @RM may replace or remove
// /net/files.idx or nf_list: the index is forgotten first (not for the others, which would restart a rebuild).
static bool serialFiles(const String& cmd) {
#if AXIS_NETS
    if (cmd.startsWith("@PUT ") || cmd.startsWith("@RM ")) netIdxForget();
#endif
    return serialFileCommand(cmd);
}

// Erase and re-make the SD card: the touch menu's YES button and "@SDFORMAT FORMAT" (the sysop menu) share this.
// False when it was refused because a caller is online; `result` says what happened either way. force: the
// serial console's "@SDFORMAT FORMAT force" goes ahead anyway.
static bool sdFormatNow(String& result, bool& ok, bool force = false) {
    if (!force && callersBesideSysop()) {
        logEvent("sysop: SD format refused, a caller is online");
        return false;
    }
#if AXIS_NETS
    netIdxForget();
#endif
    ok = storageFormatSd(result);
    logEvent(String("sysop: SD format ") + (ok ? "done" : "FAILED") + ": " + result);
    makeAreaFolders();
    return true;
}

// The cluster: find the other AXIS boards on this LAN (or become main), keep our node number.
#if !AXIS_CLUSTER
static void startCluster() {}  // built without the cluster: the board runs alone, it never looks for or answers another
#else
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
            web::announce(String("**") + boardCfg->bbsName.c_str() + "** " + l.text.c_str());
            return;
        }
        deliverChat(l);  // a chat line, a page or a node line: this board's console and sessions
    };
#if AXIS_NETS
    cluster->settings = [] {  // plus "net_up=1" while Sysop Net runs here and the User Net areas callers get (an old follower keeps them as unknown keys)
        std::string s = axis::bbs::formatConfig(*boardCfg);
        if (netHost && boardCfg->netSysop) {
            s += "net_up=1\nnet_un=" + axis::bbs::userNetList(netUnMask) + "\n";
#if AXIS_FILES && AXIS_TRANSFERS
            s += "net_files=1\n";  // followers' callers get Net Files through this board (FileServer G b t e)
#endif
        }
        return s;
    };
#else
    cluster->settings = [] { return axis::bbs::formatConfig(*boardCfg); };
#endif
    cluster->onSettings = [](const std::string& ini) {  // one BBS: the main's settings, this board's name
#if AXIS_NETS
        netMainUp = ini.find("\nnet_up=1\n") != std::string::npos;  // an old main never says it: off
        netMainFiles = ini.find("\nnet_files=1\n") != std::string::npos;
        size_t      un = ini.find("\nnet_un=");  // an old main never says it: no User Net areas
        uint8_t     got = un == std::string::npos ? 0 : axis::bbs::userNetMask(ini.substr(un + 8, ini.find('\n', un + 8) - (un + 8)));
        if (got != netUnMask) {
            netUnMask = got;
            netAreasGen++;
        }
#endif
        themeProvider.forgetRemote();  // the theme may have changed (and so may the waiting screen and the menu screens)
        if (storageHasSd()) {  // the SD board keeps the BBS's settings: a stand-in main's older copy must not replace them
            Serial.println("axis: settings from a main without the card: kept our own");
            return;
        }
        Serial.printf("axis: settings from the main: %u bytes\n", (unsigned)ini.size());
        axis::bbs::BbsConfig m = axis::bbs::mergeSettings(*boardCfg, ini);
        if (axis::bbs::formatConfig(m) == axis::bbs::formatConfig(*boardCfg)) return;
        bool wasSetUp = boardCfg->setupDone;
        std::shared_ptr<const axis::bbs::BbsConfig> prev = boardCfg;
        cfgSet(std::move(m));
        pushBoardToCallers(prev);
        bool ok = configSave(*boardCfg);
        logEvent(String("settings from the main") + (ok ? "" : " (SAVE FAILED)"));
        if (!wasSetUp) {  // a fresh (headless) node: ready for callers now
            mode = Mode::Normal;
            sessions.resize((size_t)boardCfg->sessions);
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
        return axis::bbs::encodeStatsSnapshot(stats.dayStats, lastCaller.c_str());
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
            if (!writeUsersFile()) logEvent("users: SAVE FAILED");
            pendingSeen = users.pending();
            screenDirty = true;
        }
    };
    // A follower's request is told apart by its first letter. The file server (cluster/remotefiles.cpp) owns
    // L R S A C W F D X N, and with the nets G b t e (Net Files: lower case, as B T E are taken here); the rest here are
    // M O T U J V Y Z Q P K H B E and "AN" (B: a follower's WFC stats). Scripts were "S" and @ commands "C"
    // until 2026-11-09: that took every file open (packs, views, downloads) and upload away from a follower.
    cluster->onRequest = [](int nodeNum, const std::string& req) {
        servedRequest = true;  // a follower waits on this: no nap this turn
        lastServedMs = millis();
        if (req.size() > 2 && req[0] == 'M') {  // the message base
            callerMessages();  // (makes the local store)
            return axis::bbs::serveMessages(callerMessages(), req);  // (with Sysop Net on top, once it runs)
        }
        if (req == "O") {  // the oneliner wall
            std::string out = "1";
            for (auto& l : boardData.oneLiners()) out += l + "\n";
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
                    if (ch < '0' || ch > '9' || *to > axis::app::ThemeProvider::kPosterCap) break;
                    *to = *to * 10 + (size_t)(ch - '0');
                }
            }
            if (off > (axis::app::ThemeProvider::posterName(name) ? axis::app::ThemeProvider::kPosterCap : axis::app::ThemeProvider::kThemeArtCap))
                return std::string("0");
            // built once, in place: '1' then the piece (reserved up front, with room for the cluster to put its frame header and tag round it)
            size_t w = want ? want : axis::app::ThemeProvider::kPosterChunk;
            if (w > axis::app::ThemeProvider::kPosterChunk) w = axis::app::ThemeProvider::kPosterChunk;
            std::string r;
            r.reserve(1 + w + 64);
            r = '1';
            bool opened = themeProvider.themeArtPieceAppend(name, off, w, r);
            if (!opened && axis::app::ThemeProvider::posterName(name)) return std::string("0");
            return r;
        }
        if (req.rfind("U\x1f", 0) == 0) {
            HeapScope scope("U");
            return "1" + themeProvider.menuFileLocal(req.substr(2), false);
        }  // a menu file or its art (/menus only: under 8 KB)
        if (req.rfind("E\x1f", 0) == 0) return std::string(themeProvider.menuExistsLocal(req.substr(2)) ? "1" : "0");  // does a menu screen exist: no read
#if AXIS_SCRIPTS
        if (req.rfind("J\x1f", 0) == 0) {  // a script program, or the folder when the name is empty
            std::string name = req.substr(2);
            if (name.empty()) {
                std::string out = "1";
                for (const std::string& n : themeProvider.scriptNamesLocal()) out += n + "\n";
                return out;
            }
            return std::string("1") + themeProvider.scriptFile(name);
        }
#endif
        if (req.rfind("V\x1f", 0) == 0) return "1" + axis::bbs::followerCommand(req.substr(2), sysopRun);  // a follower's @ command (never a board-only one)
        if (req == "AN") {  // a follower asked the main to speak to the Axis Net
            axisProbeSoon = true;
            return std::string("1");
        }
        if (req.rfind("Y\x1f", 0) == 0) {  // a shared list
            std::string out = "1";
            for (auto& l : boardData.listLoad(req.substr(2))) out += l + "\n";
            return out;
        }
        if (req.rfind("Z\x1f", 0) == 0) {
            size_t sep = req.find('\x1f', 2);
            if (sep != std::string::npos) boardData.listAdd(req.substr(2, sep - 2), req.substr(sep + 1));
            return std::string("1");
        }
        if (req.rfind("B\x1f", 0) == 0) {  // a follower's calls and uploads for the WFC stats: applied once, answered with ours
            stats.rollDayCounters();
            std::string last = lastCaller.c_str();
            if (statsInbox.apply(req, stats.dayStats, last) == axis::bbs::StatsInbox::Result::Applied) {
                stats.statsSave();
                lastCaller = last.c_str();
                screenDirty = true;
            }
            return "1" + axis::bbs::encodeStatsSnapshot(stats.dayStats, lastCaller.c_str());
        }
        if (req == "K") {  // the last callers
            std::string out = "1";
            for (auto& l : boardData.lastCallers()) out += l + "\n";
            return out;
        }
        if (req.rfind("Q\x1f", 0) == 0) {
            boardData.noteCaller(req.substr(2));
            return std::string("1");
        }
        if (req.rfind("P\x1f", 0) == 0) {
            boardData.addOneLiner(req.substr(2));
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
        if (req.size() > 1 && req[0] == 'I' && req[1] == '\x1f') return storageHasSd() ? cardTreeServer().handleRead(req) : std::string("0");  // a card mirror reads the tree
#if AXIS_FILES
        auto& srv = fileServers[nodeNum];
#if AXIS_NETS
        // Through the callers' library with Net Files on top (rawCallerFiles: the pool while card followers are joined, else
        // this card), so a follower's removal or rename in a shared area unlists it too.
        if (!srv) srv.reset(new axis::cluster::FileServer(netSharingFiles));
#elif AXIS_SD_POOL
        if (!srv) srv.reset(new axis::cluster::FileServer(pooledFiles));  // (just this card when it is the only one)
#else
        if (!srv) srv.reset(new axis::cluster::FileServer(sdFiles));
#endif
        return srv->handle(req);
#else
        return std::string("0");  // a file request: the file areas are not in this firmware (the follower lists nothing)
#endif
    };
    cluster->onMainRequest = [](const std::string& req) {  // a follower with a card: the main asks for its files
        servedRequest = true;
        if (req.size() > 1 && req[0] == 'I' && req[1] == '\x1f') return storageHasSd() && cluster->mainKind() == axis::cluster::CardKind::Mirror ? cardTreeServer().handleWrite(req) : std::string("0");  // a hand-back
#if AXIS_FILES
        if (!storageHasSd()) return std::string("0");
        if (!cardServer) cardServer.reset(new axis::cluster::FileServer(sdFiles));
        std::string reply = cardServer->handle(req);
        if (!req.empty() && (req[0] == 'F' || req[0] == 'X')) cluster->setFreeBytes(storageFreeBytes(true));  // the main picks cards by room
        return reply;
#else
        return std::string("0");  // the file areas are not in this firmware
#endif
    };
#if AXIS_FILES
    cluster->onMainGone = [] { cardServer.reset(); cardTreeServer().drop(); };  // its half-done uploads and mirror copies are thrown away
#endif
#if AXIS_NETS && AXIS_FILES
    axis::cluster::FileServer::uploaded = netFilesUploaded;  // a follower's upload into a shared area is listed
    remoteFiles.who = [] { return turn && turn->node ? turn->node->handle() : std::string(); };  // (a follower: the caller describing an upload)
#if AXIS_TRANSFERS
    axis::cluster::FileServer::netOp = netFollowerOp;
#endif
#endif
    cluster->onNodeGone = [](int nodeNum) {  // its unfinished uploads go, its door's files close, its locks free
#if AXIS_FILES
#if AXIS_NETS && AXIS_TRANSFERS
        auto fs = fileServers.find(nodeNum);
        if (fs != fileServers.end()) netFetchRelease(fs->second.get());  // its caller's Net Files fetch, if it had one
#endif
        fileServers.erase(nodeNum);
#endif
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
#endif  // AXIS_CLUSTER

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
#if AXIS_NETS
            netSaveNow();
#endif
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
    localNode.reset(new axis::bbs::Node(cluster && !cluster->numbers().empty() ? cluster->numbers()[0] : 1, sysInfo, nodeCfg(),
                                        callerLib()));
    localNode->setAccounts(&users, saveUsers, hwRandom, unixNow);
    localNode->setWarrior(warriorHook);
    localNode->onEvent = [](const std::string& e) { logEvent(String(e.c_str())); };
    localNode->setSysopRun(sysopRun);
    localNode->setWfcSnapshot(wfcSnapshot);
    localNode->setMessages(&callerMessages());
    localNode->setArt([](const std::string& name) { return themeArt(name); });  // the theme's screens, like a caller gets
    localNode->setWfcArt(wfcArtShared);
    localNode->setPoster(posterPiece);
    localNode->setMenus([](const std::string& name) { return menuFile(name); }, menuArtPieces);
    localNode->setMenuArt(menuArtPiece);
    localNode->setExists(menuExists);
    wireScripts(localNode.get());
    localNode->setLists([](const std::string& n) { return sharedList(n); }, [](const std::string& n, const std::string& line) { sharedListAdd(n, line); });
    localNode->setLastCallers([] { return sharedList("callers"); }, [](const std::string&) {});
    localPan = 0;
    localZoom = false;  // start with the whole screen (crisp); ZOOM for big letters
    localNode->setChat(
        [](const std::string& text, bool page) {
            if (!localNode) return;
#if !AXIS_CLUSTER
            deliverChat({localNode->number(), localNode->handle(), text, page});
            return;
#endif
            if (cluster) page ? cluster->page(localNode->handle(), text) : cluster->chat(localNode->handle(), text);
            else localNode->chatIn(1, localNode->handle(), text, page);
        },
        [] {
            std::vector<axis::bbs::Node::WhoLine> v;
            if (cluster)
                for (auto& n : cluster->nodes()) v.push_back({n.number, n.handle, n.activity});
#if !AXIS_CLUSTER  // (built without the cluster: this board's own sessions)
            for (size_t i = 0; i < sessions.size(); i++) {
                axis::bbs::Node* n = sessionNode(i);
                v.push_back({sessionNumber((int)i), n ? n->handle() : "", n ? "Online" : "", n ? n->linkShown() : ""});
            }
#endif
            return v;
        });
    localNode->beginLocalMenu(millis());
    logEvent(String("local sysop login: ") + localNode->handle().c_str());
    localFeed();
}

static void handleTap(int x, int y) {
    if (!boardHasPanel()) return;  // headless: no screen to tap (and nothing drawn to say where)
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
#if AXIS_NATIVE
bool nativeCommand(const String& cmd, Print& out);  // the host's own @ commands (native/)
#endif

// The in-BBS sysop menu's commands: run on the main (so a change is shared like @SET); a follower asks it,
// except for a board-only command (its cluster key, a restart, its card, its relays), which runs right here.
static std::string sysopRun(const std::string& cmd) {
    if (cluster && cluster->role() == axis::cluster::Role::Follower && !axis::bbs::boardOnlyCommand(cmd)) {
        std::string reply;
        return cluster->request("V\x1f" + cmd, reply, 8000) && !reply.empty() ? reply.substr(1) : std::string("axis: the main didn't answer\n");
    }
    // one reply stays under 6 KB (the link's queue dies at 8 KB); the long lists come a page at a time (app/commands.cpp)
    static constexpr size_t kCap = 6 * 1024;
    std::string out;
    struct Sink : Print {
        std::string& s;
        bool         cut = false;
        explicit Sink(std::string& r) : s(r) {}
        size_t write(uint8_t b) override { return write(&b, 1); }
        size_t write(const uint8_t* p, size_t n) override {
            size_t room = kCap - s.size();
            if (n > room) cut = true;
            s.append((const char*)p, n > room ? room : n);
            return n;
        }
    } sink(out);
    runCommand(String(cmd.c_str()), sink);
    if (sink.cut) {
        out.resize(out.rfind('\n') + 1);  // (no newline at all: npos + 1 is 0)
        out += "axis: reply cut at 6 KB\n";
    }
    return out;
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
    WifiStream* l = sessionLine((size_t)slot);
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
            WifiStream* l = sessionLine((size_t)slot);
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
            if (!sessionLine(i) && boardCfg->setupDone && mode == Mode::Normal) free = (int)i;
        if (free < 0) return "CON NO every node on this board is busy\n";
        panelConNode = sessionNumber(free), panelConJoined = false;  // (before its first screen goes out)
        panelConsoleNew = true;
        answerIn(free, WiFiClient(), {});
        panelConsoleNew = false;
        panelConNode = sessionNumber(free), panelConJoined = false;
        return "CON " + std::to_string(panelConNode) + "\n";
    }
    if (in.rfind("KEY ", 0) == 0 && panelConNode) {
        int slot = slotOfNode(panelConNode);
        WifiStream* l = slot < 0 ? nullptr : sessionLine((size_t)slot);
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
            panelSession.reset(new axis::bbs::PanelSession(boardCfg, users, nonce));
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
#if AXIS_NETS
            if (panelLine.length() >= 11 && strncasecmp(panelLine.c_str() + 4, "@NETKEY", 7) == 0) std::memset((void*)panelLine.c_str(), 0, panelLine.length());  // refused; no seed kept
#endif
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

// The @ commands that need no board hardware (app/commands.cpp), their replies through `out`.
static bool portableCommand(const String& cmd, Print& out) {
    struct PrintOut : axis::app::Out {
        Print& p;
        explicit PrintOut(Print& o) : p(o) {}
        void write(const char* s, size_t n) override { p.write((const uint8_t*)s, n); }
    } o(out);
    std::vector<axis::app::Session*> v;  // every slot, null for a free one
    v.reserve(sessions.size());
    for (auto& s : sessions) v.push_back(s.get());
    return axis::app::runPortableCommand(std::string(cmd.c_str(), cmd.length()), services(), v, o);
}

static void runCommand(const String& cmd, Print& out) {
    if (cmd == "@DOORSTATE") {  // the last door's memory (noteDoor), and the heap now
        if (boardDiag_.door == axis::bbs::DoorDiag::None) out.println("axis: no door has run since boot");
        else
            out.printf("last door: %s\nloop stack low: %u bytes\n", axis::bbs::doorDiagToken(boardDiag_.door),
                       (unsigned)boardDiag_.doorStackWords);  // (bytes: the Xtensa port's StackType_t is uint8_t)
        out.printf("heap free: %u\nlargest block: %u\n", (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
                   (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
#if AXIS_BENCH  // bench and test tooling only (bbs/features.h): in a release image these are unknown commands
    } else if (cmd.startsWith("@TAP ")) {  // bench: a tap on the touchscreen (tools/screenshots/cyd_shot.py)
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
#endif
#if AXIS_NETS
    } else if (cmd.startsWith("@NET") || netKeyLine(cmd)) {  // Sysop Net: @NET, @NET SYNC|PEERS|RECORD|LOCAL, local and Rev's moderation
        netCommand(cmd, out);
#endif
    } else if (cmd == "@NODES") {  // the cluster as this board sees it (bench)
        if (!cluster) out.println("axis: no cluster");
        else
            for (auto& n : cluster->nodes())
                out.printf("axis: node %d ip %s handle '%s' activity '%s'%s\n", n.number, IPAddress(n.ip).toString().c_str(),
                              n.handle.c_str(), n.activity.c_str(), n.number == cluster->nodeNumber() ? " (this board)" : "");
    } else if (cmd == "@MODEM" || cmd == "@MODEM scan") {  // LFD3K-SM radio callers now, the last 10, the modems on the LAN
#if !AXIS_MODEMLINK
        out.println("axis: the radio modem link is not in this firmware");
#else
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
#endif
    } else if (cmd == "@CARDS") {  // the SD cards in the pool: free space, up or down (bench)
        if (storageHasSd()) out.printf("axis: card on this board, %u MB free\n", (unsigned)(storageFreeBytes(true) >> 20));
        else out.println("axis: no SD card on this board");
        if (cluster && cluster->role() == axis::cluster::Role::Main)
            for (auto& c : cluster->cardBoards())
                out.printf("axis: card on %s (%s, ip %s), %u MB free, %s\n", cardLabel(c.id).c_str(), c.id.c_str(),
                              IPAddress(c.ip).toString().c_str(), (unsigned)(c.freeBytes >> 20), c.alive ? "up" : "DOWN");
        else if (cluster)
            out.println("axis: only the main pools the cards (@CARDS there)");
#if AXIS_SD_POOL  // the pool and its mirror (platformio.ini): without them @MIRROR is an unknown command
    } else if (cmd == "@MIRROR") {  // the mirror job: what it is copying, what it has done (bench)
        std::string areas;
        for (auto& a : boardCfg->areas)
            if (a.mirror) areas += (areas.empty() ? "" : " ") + a.id;
        out.printf("axis: mirrored areas: %s\n", areas.empty() ? "none (add |m to an areaN line)" : areas.c_str());
        if (!cluster || cluster->role() != axis::cluster::Role::Main) out.println("axis: only the main runs the mirror");
        else out.printf("axis: mirror %s\n", mirrorJob.state().c_str());
#endif
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
                stats.dayStats.total = n;
                stats.statsSave();
                screenDirty = true;
                logEvent("sysop: lifetime calls set to " + String(n));
            }
            axis::bbs::DayStats s = statsShown();  // (plus any counts left from when this board was a follower)
            out.printf("@STATS day=%d calls=%u posts=%u uploads=%u total=%u\n", s.day, (unsigned)s.calls, (unsigned)s.posts,
                       (unsigned)s.uploads, (unsigned)s.total);
        }
#if AXIS_BENCH
    } else if (cmd == "@HEAPWIN") {  // bench: restart the lowest-heap window (the numbers on @STATUS: wmin wlarge wdoing)
        heapWin.reset();               // (without it the window runs from boot: @STATUS still shows it)
#if AXIS_HEAP_TRACE
        heapTraceRestart();
#endif
        out.println("@HEAPWIN reset");
#endif
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
        sample.stkWeb = web::stackMark();  // (the task comes and goes: its own lowest mark, no foreign handle)
        sample.stkMdns = stk("mdns");
        sample.stkTimer = stk("esp_timer");
        sample.outMax = (uint32_t)axis::bbs::Node::outMax();
        sample.holdMax = (uint32_t)axis::net::TelnetStream::holdPeak();
        sample.artMax = (uint32_t)themeProvider.artMax();
        char line[384];
        std::snprintf(line, sizeof line, "id=%s role=%s nodes=%d on=%d heap=%u largest=%u up=%lu rssi=%d sd_mb=%u ip=%s",
                      WiFi.macAddress().c_str(),
                      !cluster ? "solo" : cluster->role() == axis::cluster::Role::Main ? "main" : "follower", (int)sessions.size(), on,
                      sample.heapFree, sample.largest, (unsigned long)(millis() / 1000), (int)WiFi.RSSI(),
                      (unsigned)(storageFreeBytes() >> 20), WiFi.localIP().toString().c_str());
        out.print(line);
        out.print(axis::bbs::diagFields(sample).c_str());
        if (sdNotice()) out.print(" sd=missing");  // the SD card notice (bbs/sdnotice.h)
        out.print('\n');
#if AXIS_FILES
    } else if (cmd.startsWith("@FMV ")) {  // "@FMV area|name|toarea": a move on this board's card (@FDESC @FREN @FDEL: app/commands.cpp)
        String a = cmd.substring(cmd.indexOf(' ') + 1);
        int    b1 = a.indexOf('|'), b2 = b1 < 0 ? -1 : a.indexOf('|', b1 + 1);
        std::string area = b1 > 0 ? a.substring(0, b1).c_str() : "", name = b1 > 0 ? (b2 > 0 ? a.substring(b1 + 1, b2) : a.substring(b1 + 1)).c_str() : "",
                    third = b2 > 0 ? a.substring(b2 + 1).c_str() : "";
        bool ok = false;
        if (storageHasSd() && axis::bbs::safeFileName(area) && axis::bbs::safeFileName(name) && axis::bbs::safeFileName(third)) {  // a move: same card, another folder
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
#endif
#if AXIS_WEB
    } else if (cmd.startsWith("@DISCORD")) {  // "@DISCORD https://discord.com/api/webhooks/...": the channel webhook; alone: off
        out.println(web::setDiscord(cmd.length() > 9 ? cmd.substring(9) : String()) ? "axis: discord webhook saved" : "axis: that isn't a Discord webhook URL");
#if AXIS_BENCH
    } else if (cmd == "@DDNS now") {  // bench (tools/busytest.py): make the web task's DNS check due on its next turn
        web::ddnsSoon();
        out.println("axis: ddns check due");
#endif
    } else if (cmd.startsWith("@DDNS")) {  // "@DDNS domain|host|key|secret": GoDaddy dynamic DNS; alone: off
        out.println(web::setDdns(cmd.length() > 6 ? cmd.substring(6) : String()) ? "axis: ddns saved" : "axis: @DDNS domain|host|key|secret");
    } else if (cmd == "@NOIPNOW") {  // bench: update No-IP right now
        web::noipNow();
        out.printf("axis: web: %s\n", web::status().c_str());
    } else if (cmd.startsWith("@NOIP")) {  // "@NOIP host|username|password": No-IP dynamic DNS; alone: off
        out.println(web::setNoip(cmd.length() > 6 ? cmd.substring(6) : String()) ? "axis: noip saved" : "axis: @NOIP host|username|password");
#else  // built without the web jobs: their settings stay saved as they are, unused
    } else if (cmd.startsWith("@DISCORD") || cmd.startsWith("@DDNS") || cmd.startsWith("@NOIP") || cmd.startsWith("@WEB")) {
        out.println("axis: the web jobs (Discord, No-IP, dynamic DNS) are not in this firmware");
#endif
    } else if (cmd == "@LOGCLEAR") {  // wipe the event log (screen, flash file, its old copy) and the page shown on the WFC
        events = axis::bbs::EventLog();
        ::remove("/littlefs/events.log");
        ::remove("/littlefs/events.old");
        pageText = "";
        screenDirty = true;
        out.println("axis: event log cleared");
#if AXIS_WEB
    } else if (cmd == "@WEBPING") {
        out.printf("axis: webping %s\n", web::ping().c_str());
    } else if (cmd == "@WEB") {
        out.printf("axis: web: %s\n", web::status().c_str());
    } else if (cmd == "@WEBTEST") {
        web::announce(String("**") + boardCfg->bbsName.c_str() + "** is online. Telnet in and feed the scene.");
        out.println("axis: test announcement queued");
#endif
    } else if (cmd == "@LED") {  // status light (bench): blue LED on the ring, RGB on a CYD
        ledReport = true;
    } else if (cmd == "@LOG") {  // the event log, newest last (bench)
        for (auto& l : events.lines()) out.printf("@LOG %s\n", l.c_str());
        out.println("@LOG END");
#if AXIS_BENCH
    } else if (cmd == "@SHOT") {  // bench: the screen over serial (tools/screenshots/cyd_shot.py)
        shotDoneMs = millis();
        std::unique_ptr<axis::bbs::AnsiTerm> wfc;  // the WFC on the panel: its terminal again, dumped band by band
        if (wfcOnPanel && !screenSprite()) wfc = wfcTerm(wfcShown, wfcSoon());
        screenShot([] {  // the cluster keeps its heartbeat while the screen streams out
            if (cluster) cluster->tick(millis());
        }, wfc.get(), wfc && wfcAnim.loaded() ? &wfcAnim : nullptr);
        shotDoneMs = millis();  // (the loop's stall check comes next: this one was on purpose)
    } else if (cmd == "@WFCFX" || cmd.startsWith("@WFCFX ")) {  // bench: the panel WFC's animation (wfc_anim)
        // @WFCFX: what it is doing. @WFCFX <effect> [part] [hold=<ms>]: start one now (held that far in), @WFCFX run: let a
        // held one go, @WFCFX calm: a still spell now, @WFCFX hang|hacky|vanna: what the stick figure does next
        String arg = cmd.substring(6);
        arg.trim();
        arg.toLowerCase();
        uint32_t now = millis();
        if (arg == "run") wfcAnim.hold(-1);
        else if (arg == "calm") wfcAnim.calm(now);
        else if (arg == "hang" || arg == "hacky" || arg == "vanna")  // the stick figure: what it does next (it walks there first)
            wfcAnim.walkerDo(arg == "hang" ? axis::bbs::WfcAnim::Hang : arg == "hacky" ? axis::bbs::WfcAnim::Hacky : axis::bbs::WfcAnim::Vanna, now);
        else if (arg.length()) {
            int    sp = arg.indexOf(' ');
            String name = sp < 0 ? arg : arg.substring(0, sp), rest = sp < 0 ? String() : arg.substring(sp + 1);
            int    fx = axis::bbs::WfcAnim::fxByName(name.c_str()), part = -1, hold = -1, h = rest.indexOf("hold=");
            if (h >= 0) hold = rest.substring(h + 5).toInt(), rest = rest.substring(0, h);
            rest.trim();
            if (rest.length()) part = rest.toInt();
            if (fx < 0) {
                out.print("axis: wfcfx: effects are");
                for (int i = 0; i < axis::bbs::WfcAnim::kFxCount; i++) out.printf(" %s", axis::bbs::WfcAnim::fxName(i));
                out.println();
            } else if (!wfcAnim.loaded()) {
                out.printf("axis: wfcfx: the WFC is still (%s)\n", *wfcAnimWhy ? wfcAnimWhy : "not on the panel");
            } else if (!wfcAnim.force(fx, now, part)) {
                out.println("axis: wfcfx: nothing on the screen for that one");
            } else {
                wfcAnim.hold(hold);
            }
        }
        out.printf("axis: wfcfx %s parts=%d runs=%d still=%d ox=%d held=%d frame_us=%u draw_us=%u bands=%u slowest_us=%u",
                   wfcAnim.loaded() ? "moving" : *wfcAnimWhy ? wfcAnimWhy : "still", wfcAnim.parts(), wfcAnim.runs(), (int)wfcAnim.still(),
                   wfcAnim.ox(), (int)wfcAnim.held(), (unsigned)wfcAnimFrameUs, (unsigned)wfcAnimDrawUs, (unsigned)wfcAnimBands, (unsigned)wfcAnimSlowUs);
        for (int i = 0; i < axis::bbs::WfcAnim::kActs; i++) {
            const auto& a = wfcAnim.act(i);
            if (a.on) out.printf(" %s@%d/%u", axis::bbs::WfcAnim::fxName(a.fx), (int)(now - a.start), (unsigned)a.dur);
        }
        if (wfcAnim.jiggle().on) out.printf(" jiggle=%s", axis::bbs::WfcAnim::wigName(wfcAnim.jiggle().fx));
        static const char* const kDoing[] = {"walking", "hanging out", "hacky sack", "showing a letter"};
        if (wfcAnim.walker().on) out.printf(" figure=%s flipped=%d", kDoing[wfcAnim.walker().doing & 3], wfcAnim.flipped());
        out.println();
    } else if (cmd == "@SPRITEFAIL") {  // bench: the next screen away from the WFC finds no room for the full copy
        screenFailNext();
        out.println("axis: the next full screen copy fails");
#endif
    } else if (cmd == "@SDCHECK") {  // look for the card again (cards swapped while running)
#if AXIS_NETS
        netIdxForget();
#endif
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
#if AXIS_NETS
                netSaveNow();
#endif
                delay(300);
                ESP.restart();
            }
        }
    } else if (cmd == "@CLUSTERKEY" || cmd.startsWith("@CLUSTERKEY ")) {
        // "@CLUSTERKEY secret": the key every board of this BBS shares (8+ characters, set the same on
        // each board); "@CLUSTERKEY off" forgets it (the board runs alone); alone, says whether one is set.
        // Per board, in flash: it never goes out with the settings and is never shown or logged.
#if !AXIS_CLUSTER
        out.println("axis: the cluster is not in this firmware: this board runs alone");  // (a saved key stays as it is)
#else
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
            if (ok) clusterKeySaved = key == "off" ? 0 : 1;  // (the SD card notice's "will it look for a main")
            // start over with the new key: drop every link, find the others again. Never here: the cluster does it at
            // the top of its next tick (loop()), so no command can clear its links while tick() is looping over them.
            if (cluster) cluster->restartSoon(key == "off" ? "" : key.c_str());
            logEvent(String("sysop: cluster key ") + (key == "off" ? "cleared" : "set") + (ok ? "" : " (SAVE FAILED)"));
            out.println(ok ? "axis: cluster key saved" : "axis: cluster key SAVE FAILED");
        }
#endif
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
    } else if (cmd == "@RESTART" && callersBesideSysop()) {
        out.println("axis: restart refused, a caller is online (serial: @RESTART force)");
    } else if (cmd == "@RESTART" || (cmd == "@RESTART force" && &out == &Serial)) {  // restart this board (the sysop menu's System layer)
        logEvent("sysop: restart");
        out.println("axis: restarting");
#if AXIS_NETS
        netSaveNow();
#endif
        delay(300);
        ESP.restart();
    } else if (cmd == "@WIFIPICK") {  // the touch WiFi picker on demand (bench)
        startPicker(mode == Mode::WifiPick ? afterPick : mode);
    } else if (&out == &Serial && serialFiles(cmd)) {
        boardData.forgetCounts();  // @PUT @LS @TYPE @RM @MKDIR @BAUD (tools/sdput.py) (a list file may have changed)
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
#if AXIS_NETS
        netSaveNow();
#endif
        delay(100);
        ESP.restart();
    }
    else if (portableCommand(cmd, out)) {}  // users, posts, files, lists, settings, menus, the callers here (app/commands.cpp)
#if AXIS_NATIVE
    else if (nativeCommand(cmd, out)) {}
#endif
}
#if AXIS_NATIVE
void nativeRun(const char* cmd) {  // one @ command, its output thrown away (the host's drain, @BCAST)
    struct Null : Print {
        size_t write(uint8_t) override { return 1; }
        size_t write(const uint8_t*, size_t n) override { return n; }
    } sink;
    runCommand(String(cmd), sink);
}
#endif

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
            if (mode != Mode::Normal || !boardCfg->setupDone) {
                Serial.println(boardCfg->setupDone ? "axis: WFC is not available right now (the board is not at its normal screen)"
                                             : "axis: WFC is not available until setup is done");
                continue;
            }
            serialWfc.reset(new axis::bbs::Node(cluster && !cluster->numbers().empty() ? cluster->numbers()[0] : 1, sysInfo, nodeCfg(), callerLib()));
            serialWfc->setAccounts(&users, saveUsers, hwRandom, unixNow);
            serialWfc->setWarrior(warriorHook);
            serialWfc->onEvent = [](const std::string& e) { logEvent(String(e.c_str())); };
            serialWfc->setSysopRun(sysopRun);
            serialWfc->setWfcSnapshot(wfcSnapshot);
            serialWfc->setMessages(&callerMessages());
            serialWfc->setArt([](const std::string& name) { return themeArt(name); });
            serialWfc->setWfcArt(wfcArtShared);
            serialWfc->setPoster(posterPiece);
            serialWfc->setMenus([](const std::string& name) { return menuFile(name); }, menuArtPieces);
            serialWfc->setMenuArt(menuArtPiece);
            serialWfc->setExists(menuExists);
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
#if AXIS_NETS
            cmd.reserve(168);  // one buffer, never moved: what is typed (a key) is wiped from it below, not left in a freed copy
#endif
            if (cmd.length() < 160) cmd += c;
            continue;
        }
#if AXIS_NETS
        if (netKeyLine(cmd)) netKeyCommand(cmd);  // Rev's net key: never echoed, logged or passed on
        else
#endif
            runCommand(cmd, Serial);
#if AXIS_NETS
        if (cmd.length()) std::memset((void*)cmd.c_str(), 0, cmd.length());
#endif
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
#if AXIS_HEAP_TRACE
    heap_trace_init_standalone(traceRecs_, sizeof traceRecs_ / sizeof traceRecs_[0]);
#endif
    heapScopeHook([](const std::string& l) { logEvent(String(l.c_str())); });
    boardDiag_.resetCode = (int)esp_reset_reason();
    heap_caps_register_failed_alloc_callback(allocFailed);
    ledBegin();
    Serial.begin(115200);
    delay(200);
    const char* panel = boardInit(1, 40000000);
    if (boardHasPanel() && !screenBandsInit()) Serial.println("axis: no memory for the screen band");  // (headless: none)
    Serial.printf("\naxis: Axis Micro BBS Firmware %s booting, panel %s, heap %u\n", axis::bbs::kAxisVersion, panel,
                  ESP.getFreeHeap());
    if (boardHasPanel()) title("starting...");  // straight to the panel: the full copy is only made for a screen other than the WFC
    storageInit();
    if (!configMount()) Serial.println("axis: internal flash filesystem failed to mount");
    web::begin();
    cfgSet(configLoad());
    sessions.resize(boardCfg->setupDone ? (size_t)boardCfg->sessions : 1);  // callers at once (setup takes one)
    bool lordArgs = false;  // stock LORD tails gain NOEMS; a sysop's own arguments stay (a build without LORD keeps every line)
    if (AXIS_LORD)
        for (auto& d : cfgEdit().doors) lordArgs = axis::bbs::migrateLordArgs(d) || lordArgs;
    if (lordArgs) configSave(*boardCfg);
    users.parse(flashRead("/users.dat"));
    boardData.load();  // the oneliners and the last callers
    if (!boardData.lastCallers().empty()) {  // the WFC's last caller: the newest saved login ("handle|node|time|location")
        std::string h = boardData.lastCallers().back().substr(0, boardData.lastCallers().back().find('|'));
        if (!h.empty()) lastCaller = h.c_str();
    }
    stats.statsLoad(users);
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
    if (!boardCfg->setupDone) {
        if (boardIsHeadless()) {  // nothing to set up on the ring: the main's settings arrive over the link
            String mac = WiFi.macAddress();
            mac.replace(":", "");
            cfgEdit().boardName = ("axis-" + mac.substring(8)).c_str();
            cfgEdit().sessions = 3;  // no screen to draw: room for a third caller
            newSetupPin();     // (only used if a caller arrives before the settings do)
            Serial.printf("axis: setup PIN %s (no screen to show it: type it when a caller is asked for it)\n", setupPin.c_str());
        } else {
            mode = Mode::SetupChoice;
            newSetupPin();
        }
    }
    Serial.printf("axis: settings %s\n", boardCfg->setupDone ? "loaded" : "not set up yet, first-boot setup");
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

// The LFD3K-SM modem's newest link report for a caller (Session::takeLinkReport): the WFC's |R? codes follow this caller's
// link when it is the one shown, a new call, a new tier, or the shown one is a minute old (two radio callers do not take
// turns at every report). The first report of a call is an event line.
#if AXIS_MODEMLINK
static uint32_t lastRadioMs = 0;
static void radioReport(axis::app::Session& s, const axis::net::LinkInfo& li, bool first) {
    if (first || s.node->handle() == lastRadioWho || li.tier != lastRadio.tier || millis() - lastRadioMs > 60000u) {
        lastRadio = li;
        lastRadioWho = s.node->handle();
        lastRadioMs = millis();
    }
    if (first)
        logEvent(String("radio caller on node ") + String(sessionNumber(s.slot)) + ": " + axis::net::linkLabel(li).c_str() +
                 (li.modem.empty() ? "" : (String(" via ") + li.modem.c_str()).c_str()));
}
#endif

// What each caller's Session uses from this board (app/services.h): the shared stores, the cluster-aware chat and the
// board-only seams (the screen, the door note, the login lockouts, the radio report, the Axis Net bridge, the Net Files
// fetch, the setup over telnet).
static axis::app::Services& services() {
    static axis::app::Services s(boardHost, boardData, themeProvider, stats, users, boardCfg);
    static bool wired = false;
    if (wired) return s;
    wired = true;
    s.saveUsers = saveUsers;
    s.sysopRun = sysopRun;
    s.chat = [](int slot, const std::string& handle, const std::string& text, bool page) {  // through the main to every node
#if !AXIS_CLUSTER
        deliverChat({sessionNumber(slot), handle, text, page});
        return;
#endif
        if (cluster) page ? cluster->page(handle, text, slot) : cluster->chat(handle, text, slot);
        else
            for (size_t i = 0; i < sessions.size(); i++)
                if (sessionNode(i)) sessionNode(i)->chatIn(sessionNumber(slot), handle, text, page);
    };
    s.who = [] {
        std::vector<axis::bbs::Node::WhoLine> v;
        if (cluster && cluster->nodes().size() > 1) {
            for (auto& n : cluster->nodes()) v.push_back({n.number, n.handle, n.activity});
            std::sort(v.begin(), v.end(), [](const axis::bbs::Node::WhoLine& a, const axis::bbs::Node::WhoLine& b) { return a.node < b.node; });
        } else {
            for (size_t i = 0; i < sessions.size(); i++) {
                axis::bbs::Node* n = sessionNode(i);
                v.push_back({sessionNumber((int)i), n ? n->handle() : "", n ? "Online" : "", n ? n->linkShown() : ""});
            }
        }
        return v;
    };
    s.messages = []() -> axis::bbs::MessageStore& { return callerMessages(); };
#if AXIS_FILES
    s.files = []() -> axis::bbs::FileLibrary& { return callerFiles(); };
#endif
    s.rng = hwRandom;
    s.unixNow = unixNow;
    s.nodeCfg = nodeCfg;
    s.sysInfo = sysInfo;
    s.wfcSnapshot = wfcSnapshot;
    s.dayStats = statsShown;  // (a cluster follower shows the main's counts)
    s.warrior = warriorHook;
    s.nodeLink = [](int slot, int kind, int to, const std::string& handle, const std::string& text) {
#if !AXIS_CLUSTER
        deliverChat({sessionNumber(slot), handle, text, kind == 1, kind, to});
        return;
#endif
        if (cluster) {
            cluster->send(handle, kind, to, text, slot);
            return;
        }
        for (size_t i = 0; i < sessions.size(); i++)
            if ((int)i != slot && sessionNode(i) && (to == 0 || sessionNumber((int)i) == to))
                sessionNode(i)->nodeIn(kind, sessionNumber(slot), handle, text);
    };
    s.nodeNumber = sessionNumber;
#if AXIS_NETS && AXIS_FILES && AXIS_TRANSFERS
    s.netFiles = [](axis::app::Session& ses) {
        if (!asFollower() || netMainFiles) {  // Net Files: this board's fetch, or on a follower the main's (an older main never says so)
            const axis::bbs::Node*  n = ses.node.get();
            axis::bbs::NetFilesView v;
            v.page = netViewPage;
            v.unreachable = netFetchUnreachable;  // (a follower: "", the main says it when the fetch begins)
            v.callerRadioBps = [n] { return netCallerBps(n->link()); };  // (this board's own caller link)
            v.begin = [n](const std::string& id, std::string& area, std::string& name) { return netViewBegin(n, id, area, name); };
            v.poll = [n](uint32_t& size, const char*& word) { return netViewPoll(n, size, word); };
            v.release = [n] { netFetchRelease(n); };
            ses.node->setNetFiles(std::move(v));
        }
    };
#endif
    s.follower = asFollower;
    s.cfgEdit = []() -> axis::bbs::BbsConfig& { return cfgEdit(); };
    s.cfgReplace = [](axis::bbs::BbsConfig c) {
        std::shared_ptr<const axis::bbs::BbsConfig> prev = boardCfg;
        cfgSet(std::move(c));
        pushBoardToCallers(prev);
    };
    s.cfgSave = [] { return configSave(*boardCfg); };
    s.shareSettings = [] {
        if (cluster) cluster->shareSettings();
    };
    s.areasChanged = makeAreaFolders;
    s.ready = [] { return boardCfg->setupDone && mode == Mode::Normal; };
    s.setupPin = [] { return std::string(setupPin.c_str()); };
    s.setupChosen = [] { mode = Mode::SetupTelnet; };
    s.setupSave = [](axis::bbs::SetupWizard& w) {
        cfgSet(w.result());
        bool ok = configSave(*boardCfg);
#if AXIS_WEB  // (built without the web jobs the wizard does not ask for No-IP)
        if (!w.noip().host.empty())  // the No-IP login stays in this board's own storage
            web::setNoip(String(w.noip().host.c_str()) + "|" + w.noip().user.c_str() + "|" + w.noip().pass.c_str());
#endif
        logEvent(String("setup ") + (ok ? "saved" : "SAVE FAILED") + " over telnet");
    };
    s.setupFinished = [] {
        mode = Mode::Normal;
        applyNetwork();
    };
    s.dialName = [] { return std::string(dialName().c_str()); };
    s.doorFiles = [](const std::string& id, std::unique_ptr<axis::door::HostFs>& fs,
                     std::unique_ptr<axis::door::LockTable>& own) -> axis::door::LockTable* {
        bool remote = asFollower() && (!storageHasSd() || cluster->mainKind() == axis::cluster::CardKind::Mirror);  // (a mirror main: its copy is the game)
        if (remote) {
            auto ask = [](const std::string& req, std::string& reply) { return cluster && cluster->request(req, reply, 8000); };
            auto* rfs = new axis::cluster::RemoteHostFs(ask, id);
            fs.reset(rfs);
            own.reset(new axis::cluster::RemoteLockTable(ask, rfs));
            return own.get();
        }
        fs.reset(new axis::PosixHostFs(std::string(storageRoot()) + "/doors/" + id));
        return &doorLocks;
    };
    s.releaseDoor = [](int num) {
        doorFs.reset();
        (doorRemoteLocks ? *doorRemoteLocks : doorLocks).releaseOwner(num);
        doorRemoteLocks.reset();
    };
    s.doorNote = [](axis::bbs::DoorDiag d) { noteDoor(d, 0); };
    s.zipHeap = noteZipHeap;
    s.drawTransfer = drawTransfer;
#if AXIS_NETS && AXIS_TRANSFERS
    s.netJob = [](axis::app::Session& ses) -> axis::bbs::TransferJob* {
#if AXIS_FILES && AXIS_CLUSTER
        if (asFollower()) return netRemoteJob(ses.node.get(), ses.node->transfer(), *ses.line);  // (the main's fetch)
#endif
        return netFetchJob(ses.node.get(), callerFiles(), ses.node->transfer(), *ses.line);
    };
    s.netRelease = netFetchRelease;
    s.netJobNote = netFetchJobNote;
#endif
#if AXIS_NETS && AXIS_FILES
    s.netUploaded = netFilesUploaded;
#endif
    s.lineEnding = lineEnding;
    s.park = parkLine;
    s.callerGone = callerGone;
#if AXIS_MODEMLINK
    s.radio = radioReport;
#endif
    s.clustered = [] { return cluster != nullptr; };
    s.status = [](int slot, const std::string& handle, const std::string& activity) { cluster->setStatus(slot, handle, activity); };
    s.dirty = [] { screenDirty = true; };
    s.doing = [](const std::string& what) { noteDoing(String(what.c_str())); };
    s.heapText = [] { return std::string(heapText().c_str()); };
    s.romLog = [](const char* line) { esp_rom_printf("%s", line); };
    s.nap = [](uint32_t ms) { delay(ms); };
    s.axisPump = axisPump;
    s.axisCall = axisStart;
    s.axisBusy = [] { return axisBridge.on; };
    s.axisDirty = [] { return axisDirty; };
    s.axisProbe = [] {
        if (asFollower() && cluster) {
            std::string reply;
            cluster->request("AN", reply, 1500);
        } else axisProbeSoon = true;
    };
    return s;
}

// The SD view cache filled ahead of callers: on the main with a card, only while nobody is online on any board (one small step a
// loop turn: a pack looked at, or 16 KB of one copied; the inflate window only for a deflated piece and only above the fill floor).
// A pass goes through every *ANSIVIEW door's areas, then deletes copies no pack matches; the next starts 30 minutes after one ends,
// so new uploads get copied too. A caller arriving drops the pass (and a copy part way); the next idle restarts it.
#if AXIS_ANSIVIEW
static std::unique_ptr<axis::bbs::viewcache::Filler> viewFill;
static uint32_t viewFillNext = 300000;  // the first pass five minutes after boot (after the boot DNS check, which needs TLS room)
#endif
// A caller is coming in: the pass gives its memory back now, before the session is made (not a loop turn later), and waits
// until the board has been idle a while again.
static void viewCacheDrop() {
#if AXIS_ANSIVIEW
    viewFill.reset();
    viewFillNext = axis::bbs::viewcache::holdForCallers(viewFillNext, millis());
#endif
}
static void viewCacheTick() {
#if AXIS_ANSIVIEW  // (a build without the viewer has no cache to fill)
    std::unique_ptr<axis::bbs::viewcache::Filler>& fill = viewFill;
    uint32_t&       nextMs = viewFillNext;
    uint32_t        now = millis();
    bool            anyone = false;
    for (size_t i = 0; i < sessions.size() && !anyone; i++) anyone = sessionLine(i) != nullptr;
    if (cluster)  // a caller on a follower counts too: the main serves its screens and files
        for (auto& n : cluster->nodes()) anyone = anyone || !n.handle.empty();
    // The web task (a DNS check, a Discord line) needs the heap the window would hold: the pass steps aside for it too.
    bool webWork = web::working();
    if (anyone || webWork || asFollower() || !storageHasSd() || !boardCfg) {
        if (fill || anyone) {  // dropped (a copy part way is thrown away, the window given back); not back for two minutes,
            fill.reset();      // and never the moment the last caller hangs up
            nextMs = axis::bbs::viewcache::holdForCallers(nextMs, now);
        }
        return;
    }
    // (The first try, a file per piece in /sd/files/viewcache, is left alone: deleting thousands of files one by one in a FAT
    // folder that big stalls the loop for seconds a file. It is only card space; the sysop can delete it from a PC.)
    if (!fill && (int32_t)(now - nextMs) < 0) return;
    HeapScope scope("viewcache");  // (its pack directories and the inflate window: the deepest idle use of the heap)
    try {
        if (!fill) {
            std::vector<std::string> areas;
            for (auto& d : boardCfg->doors) {
                std::string exe = d.exe;
                for (char& c : exe) c = (char)std::toupper((unsigned char)c);
                if (exe != "*ANSIVIEW") continue;
                for (size_t from = 0; from <= d.args.size();) {
                    size_t      comma = d.args.find(',', from);
                    std::string id = d.args.substr(from, comma == std::string::npos ? std::string::npos : comma - from);
                    if (!id.empty() && std::find(areas.begin(), areas.end(), id) == areas.end()) areas.push_back(id);
                    if (comma == std::string::npos) break;
                    from = comma + 1;
                }
            }
            fill.reset(new axis::bbs::viewcache::Filler(
                callerFiles(), std::move(areas),
                [](uint32_t minFree, uint32_t minLargest) {
                    return heap_caps_get_free_size(MALLOC_CAP_8BIT) >= minFree &&
                           heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) >= minLargest;
                },
                [](uint64_t bytes) { return storageFreeBytes(true) >= bytes + (256ull << 20); }));  // fresh, and 256 MB left for uploads
        }
        if (!fill->step()) {
            if (fill->copied() || fill->removed())
                logEvent("view cache: " + String(fill->copied()) + " packs copied, " + String(fill->removed()) + " old copies removed");
            fill.reset();
            nextMs = now + 30u * 60u * 1000u;
        }
    } catch (const std::bad_alloc&) {  // the heap could not spare a listing or a directory: drop the pass, try again later
        fill.reset();
        nextMs = now + 10u * 60u * 1000u;
        logEvent("view cache: short of memory, the pass waits 10 minutes");
    }
#endif
}

// A card mirror ended its hand-back (I E): restart like @RESTART, from here so the reply has left first.
static void cardTreeRestartTick() {
    static uint32_t at = 0;
    if (!cardTreeServer().finished()) return;
    if (!at) {
        at = millis() | 1;
        logEvent("cluster: the card was handed back, restarting");
    }
    if (millis() - at < 500) return;
#if AXIS_NETS
    netSaveNow();
#endif
    ESP.restart();
}

void loop() {
    static uint32_t lastLoop = millis();
#if AXIS_BENCH
    if (millis() - lastLoop > 2000 && millis() - shotDoneMs > 1000)  // (a bench screenshot stalls on purpose)
#else
    if (millis() - lastLoop > 2000)
#endif
        logEvent("loop stalled " + String(millis() - lastLoop) + " ms");
    lastLoop = millis();
    heapWin.sample((uint32_t)heap_caps_get_free_size(MALLOC_CAP_8BIT), (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT), doingNow());
#if AXIS_HEAP_TRACE
    heapTraceLow((uint32_t)heap_caps_get_free_size(MALLOC_CAP_8BIT), (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
#endif
    heapScopeFlush();  // a line the web task (core 0) held
    web::tick();       // the web task exists only while a DNS check or a Discord line waits
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
#if AXIS_BENCH
    if (tapX >= 0) {
        handleTap(tapX, tapY);
        tapX = tapY = -1;
    }
#endif
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
                if (afterPick == Mode::Normal || boardCfg->setupDone) wifiJoinSaved();  // never strand a working board
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
    if (cluster) { bool mf = cluster->role() == axis::cluster::Role::Main && cluster->hasMirrorFollower(); axis::cluster::cardFeed().setOn(mf, hwRandom()); if (!mf) cardTreeServer().dropRead(); }
    cardTreeRestartTick();
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
#if AXIS_NETS
    netTick();  // Sysop Net: both sessions pumped, dialing on schedule (the main only)
#endif
    if (!asFollower() && !axisBridge.on && !inflateHeld() && (axisProbeSoon || millis() - axisProbeAt > 120000)) {
        axisProbeSoon = false;
        axisProbeAt = millis();
        axisProbeOne();
    }
    pumpPanel();
    for (size_t i = 0; i < sessions.size(); i++) {
        axis::app::Session* s = sessions[i].get();
        if (slotStatus.size() <= i) slotStatus.resize(i + 1);
        if (!s) {  // nobody on: the slot's status line goes empty once (the cluster's node list), as a turn with nobody on did
            if (cluster && slotStatus[i] != "|") {
                slotStatus[i] = "|";
                cluster->setStatus((int)i, "", "");
            }
            continue;
        }
        {
            TurnOf on(s);
            uint32_t turnMs = millis();
            s->service(turnMs);
            turnMs = millis() - turnMs;
            if (turnMs > 1000)  // one caller held the loop: say what it was doing (node state number, bytes its line still holds)
                logEvent("slow turn: node " + String(sessionNumber((int)i)) + " " + String(turnMs) + " ms, state " +
                         String(s->node ? (int)s->node->state() : -1) + ", held " + String(s->line ? (unsigned)s->line->backlog() : 0u) + " " +
                         String(s->lastStatus.c_str()));
        }
        if (s->done()) {  // hung up this turn: the slot is free, its last status line kept for the next turn
            slotStatus[i] = s->lastStatus;
            sessions[i].reset();
        }
    }
    drainParkedLines();
    axisDirty = false;
    static uint32_t lastMemLog = 0, seenDropped = 0, seenGated = 0, seenStarts = 0;  // low-memory web notes, at most hourly
    static bool     memLogged = false;
    if (axis::bbs::gateLogDue(millis(), lastMemLog, memLogged)) {
        uint32_t d = web::droppedLines(), g = web::gatedChecks(), f = web::startFails();
        if (d != seenDropped || g != seenGated || f != seenStarts) {
            if (d != seenDropped) logEvent("discord: " + String(d - seenDropped) + " lines dropped (low memory)");
            if (g != seenGated) logEvent("ddns: waiting for memory (largest " + String(web::gatedLargestKb()) + " KB)");
            if (f != seenStarts) logEvent("web: task waiting for memory (largest " + String(web::startLargestKb()) + " KB)");
            seenDropped = d, seenGated = g, seenStarts = f;
            lastMemLog = millis(), memLogged = true;
        }
    }
    viewCacheTick();
    static uint32_t lastRoll = 0;  // the day counters roll at midnight on every board, screen or not (the WFC used to be the only one)
    if (millis() - lastRoll > 5000u) {
        lastRoll = millis();
        stats.rollDayCounters();
    }
    static uint32_t lastTick = 0;  // keep the clock and the time-online count moving
    if (millis() - lastTick > 300u) {  // the status lights blink, long lines scroll, the times tick
        lastTick = millis();
        if (mode == Mode::Normal) screenDirty = true;
    }
    if (screenDirty) drawScreen();
    wfcAnimTick();  // the panel WFC's next frame (stands still while anyone is online)
    if (!servedRequest) delay(5);  // a follower's request is answered before the nap
    servedRequest = false;
}
