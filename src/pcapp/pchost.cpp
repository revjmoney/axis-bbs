// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// axispc's host and BBS (pchost.h). The wiring follows the board's main.cpp (services(), answer, answerIn, deliverChat,
// loop), without the board: one Session per caller, any number of them, all on this one thread.
#include "pcapp/pchost.h"

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/utsname.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <thread>
#include <utility>

#include "bbs/features.h"
#include "bbs/setup_wizard.h"
#include "bbs/version.h"
#include "bbs/viewcache.h"
#include "lord/game.h"
#include "net/areas.h"
#include "pcapp/pcline.h"
#include "platform/linux/hostconf.h"
#include "platform/linux/modemline.h"
#include "platform/posix/posixhostfs.h"

namespace axis::pcapp {

namespace {
constexpr size_t   kParkedMax = 16;      // goodbyes still going out, at most (the oldest is closed for one more)
constexpr uint32_t kParkedMs = 60000;    // a parked goodbye is closed after this long in all
constexpr size_t   kReplyCap = 6 * 1024; // a sysop menu reply, at most (the board's cap)

std::string readFile(const std::string& path) {
    std::string s;
    FILE*       f = std::fopen(path.c_str(), "rb");
    if (!f) return s;
    char   buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) s.append(buf, n);
    std::fclose(f);
    return s;
}

// Like readFile, but tells a missing file apart from one that is there and will not read (permission denied, an I/O
// error). True with the text when the file read whole or is absent; false when it exists but could not be read (`why`
// says so). The users base must never be loaded as empty on a read error and then saved empty over the real file.
bool readFileChecked(const std::string& path, std::string& out, std::string& why) {
    out.clear();
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) {
        if (errno == ENOENT) return true;  // no file yet: an empty base is right
        why = "cannot open " + path + ": " + std::strerror(errno);
        return false;
    }
    char   buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) out.append(buf, n);
    bool bad = std::ferror(f) != 0;
    std::fclose(f);
    if (bad) {
        out.clear();
        why = "cannot read " + path + " (I/O error)";
        return false;
    }
    return true;
}

// The board's safe save: a temp file first, then renamed over the old one, so a crash never leaves half a file.
bool writeFileSafe(const std::string& path, const std::string& temp, const std::function<bool(FILE*)>& fill) {
    FILE* f = std::fopen(temp.c_str(), "wb");
    if (!f) return false;
    bool ok = fill(f);
    ok = (std::fclose(f) == 0) && ok;
    if (!ok) {
        std::remove(temp.c_str());  // the old file stays as it was
        return false;
    }
    return std::rename(temp.c_str(), path.c_str()) == 0;
}

uint32_t pcRandom() {  // Services::rng is a plain function
    static std::random_device rd;
    return (uint32_t)rd();
}

uint64_t memAvailable() {  // bytes (0 when /proc/meminfo has no answer)
    FILE* f = std::fopen("/proc/meminfo", "r");
    if (!f) return 0;
    char               line[128];
    unsigned long long kb = 0;
    while (std::fgets(line, sizeof line, f))
        if (std::sscanf(line, "MemAvailable: %llu kB", &kb) == 1) break;
    std::fclose(f);
    return (uint64_t)kb * 1024u;
}

// A locally administered address made from the machine id, as the Linux axis's WiFi.macAddress(): stable, never a real
// MAC, and nodePlatform() calls it "pc".
std::string machineId() {
    std::string id;
    for (const char* p : {"/etc/machine-id", "/var/lib/dbus/machine-id"}) {
        FILE* f = std::fopen(p, "r");
        if (!f) continue;
        char buf[128] = {0};
        if (std::fgets(buf, sizeof buf, f)) id = buf;
        std::fclose(f);
        if (!id.empty()) break;
    }
    if (id.empty()) {
        char host[256] = {0};
        ::gethostname(host, sizeof host - 1);
        id = host;
    }
    uint64_t h = 1469598103934665603ull;  // FNV-1a over "axis-mac:" + the id
    for (char c : std::string("axis-mac:") + id) {
        h ^= (uint8_t)c;
        h *= 1099511628211ull;
    }
    uint8_t b[6];
    for (int i = 0; i < 6; i++) b[i] = (uint8_t)(h >> (8 * i));
    b[0] = (uint8_t)((b[0] & 0xFC) | 0x02);  // locally administered, unicast
    char s[18];
    std::snprintf(s, sizeof s, "%02X:%02X:%02X:%02X:%02X:%02X", b[0], b[1], b[2], b[3], b[4], b[5]);
    return s;
}

PcLine* pcLine(app::Session& s) { return static_cast<PcLine*>(s.line.get()); }  // (every line here is a PcLine)
}  // namespace

// ------------------------------------------------------------------ PcHost
PcHost::PcHost(const std::string& dataDir)
    : sd_(dataDir + "/sd"), flash_(dataDir + "/flash"), start_(std::chrono::steady_clock::now()) {}

uint32_t PcHost::nowMs() {  // from the program's start, as the board's millis() (the WFC's and @STATUS's uptime)
    using namespace std::chrono;
    return (uint32_t)duration_cast<milliseconds>(steady_clock::now() - start_).count();
}

uint32_t PcHost::unixNow() {
    time_t t = std::time(nullptr);
    return t > 1700000000 ? (uint32_t)t : 0;
}

uint32_t PcHost::random32() { return (uint32_t)rd_(); }

uint64_t PcHost::sdFreeBytes() {
    struct statvfs v;
    if (::statvfs(sd_.c_str(), &v) != 0) return 0;
    return (uint64_t)v.f_bavail * v.f_frsize;
}

void PcHost::openEventFile() {
    file_.reset(new bbs::RotatingLog(flash_ + "/events.log", flash_ + "/events.old", 8192));  // 16 KB at most, as the board
    for (auto& l : file_->tail(40)) events_.add(l);
}

void PcHost::clearEvents() {
    events_ = bbs::EventLog();
    std::remove((flash_ + "/events.log").c_str());
    std::remove((flash_ + "/events.old").c_str());
}

void PcHost::event(const std::string& what) {
    time_t now = std::time(nullptr);
    char   stamp[8] = "??:??";  // the clock is not set yet (no double hyphen in log text)
    if (now > 1700000000) std::strftime(stamp, sizeof stamp, "%H:%M", std::localtime(&now));
    std::string line = std::string(stamp) + " " + what;
    events_.add(line);
    if (file_) file_->append(line);
    std::printf("axis: %s\n", what.c_str());
}

// ------------------------------------------------------------------ the message base
// The board main's store: counts a public post for the WFC's today box, and refuses a post into a Sysop Net or User Net
// area (sn_, un_), as the board does while its net is not running, so none is ever stored unsigned.
struct PcBbs::CountingMsgs : bbs::FileMessageStore {
    CountingMsgs(std::string root, app::Stats& st) : bbs::FileMessageStore(std::move(root)), stats(st) {}
    int post(const std::string& area, const bbs::Message& m) override {
        if (net::isNetAreaId(area)) return 0;
        int n = bbs::FileMessageStore::post(area, m);
        if (n && area != "email" && area != net::kNetFilesArea) stats.countPost();
        return n;
    }
    app::Stats& stats;
};

// ------------------------------------------------------------------ PcBbs
PcBbs::PcBbs(const std::string& dataDir)
    : dataDir_(dataDir),
      host_(dataDir),
      data_(host_),
      theme_(host_),
      stats_(host_),
      cfg_(std::make_shared<bbs::BbsConfig>()),
      services_(host_, data_, theme_, stats_, users_, cfg_) {
    theme_.config = [this]() -> const bbs::BbsConfig& { return *cfg_; };
    id_ = machineId();
    wireServices();
}

PcBbs::~PcBbs() = default;

bbs::BbsConfig& PcBbs::cfgEdit() {
    if (cfg_.use_count() > 1) cfg_ = std::make_shared<bbs::BbsConfig>(*cfg_);
    return const_cast<bbs::BbsConfig&>(*cfg_);  // (every copy is made here or in cfgSet: not const underneath)
}

void PcBbs::cfgSet(bbs::BbsConfig c) { cfg_ = std::make_shared<bbs::BbsConfig>(std::move(c)); }

// <data>/flash/axis.ini, as the board's configLoad. The board's telnet port 23 is lan_port here (as the Linux axis
// shows it): a stored 23 loads as lan_port and lan_port saves as 23, so the same file works for both programs.
bool PcBbs::loadConfig() {
    std::string ini = readFile(host_.flashRoot() + "/axis.ini");
    bbs::BbsConfig c = ini.empty() ? bbs::BbsConfig() : bbs::parseConfig(ini);
    uint16_t lan = (uint16_t)hostConfInt("lan_port", 2323, 1, 65535);
    if (c.telnetPort == 23) c.telnetPort = lan;
    c.publicPort = (int)hostConfInt("public_port", 0, 0, 65535);  // the advertised external port
    cfgSet(std::move(c));
    return !ini.empty();
}

bool PcBbs::saveConfig() {
    bbs::BbsConfig d = *cfg_;
    uint16_t       lan = (uint16_t)hostConfInt("lan_port", 2323, 1, 65535);
    if (lan != 23 && d.telnetPort == lan) d.telnetPort = 23;
    std::string s = bbs::formatConfig(d);
    return writeFileSafe(host_.flashRoot() + "/axis.ini", host_.flashRoot() + "/axis.tmp",
                         [&](FILE* f) { return std::fwrite(s.data(), 1, s.size(), f) == s.size(); });
}

// users.dat written a block at a time through UserStore::formatTo, as the board's writeUsersFile.
void PcBbs::saveUsers() {
    bool ok = writeFileSafe(host_.flashRoot() + "/users.dat", host_.flashRoot() + "/save.tmp", [this](FILE* f) {
        bool good = true;
        users_.formatTo([&](const std::string& piece) {
            if (good) good = std::fwrite(piece.data(), 1, piece.size(), f) == piece.size();
        });
        return good;
    });
    if (!ok) host_.event("users: SAVE FAILED");
    if (users_.pending() > pendingSeen_) host_.event("new user application waiting: the sysop menu's Users");
    pendingSeen_ = users_.pending();
}

void PcBbs::makeAreaFolders() {
    if (!AXIS_FILES) return;
    std::string root = host_.sdRoot() + "/files";
    ::mkdir(root.c_str(), 0777);
    ::mkdir((root + "/" + bbs::viewcache::kArea).c_str(), 0777);  // the ANSI viewer's unpacked pieces
    for (auto& a : cfg_->areas) ::mkdir((root + "/" + a.id).c_str(), 0777);
}

std::string PcBbs::lordFolderId() const {
    for (auto& d : cfg_->doors)
        if (d.exe == "*LORD" && !d.id.empty()) return d.id;
    return "lord";
}

bool PcBbs::writeWarrior(const std::string& handle) {
    if (!AXIS_LORD) return true;
    // A LockTable owner well above any node number (max_callers can reach 4096), so registering a warrior never
    // shares a lock with a real caller's node (slot + 1). The board uses a small fixed value, safe with its few nodes.
    static const int kSysopWarriorOwner = 1000000;
    PosixHostFs fs(host_.sdRoot() + "/doors/" + lordFolderId());
    int         r = lord::ensureWarrior(fs, &doorLocks_, kSysopWarriorOwner, handle);
    if (r < 0) host_.event("warrior: could not write " + handle);
    else if (r == 1) host_.event("warrior: new " + handle);
    return r >= 0;
}

bbs::MessageStore& PcBbs::messages() {
    if (!msgs_) {
        std::string root = host_.sdRoot() + "/msgs";
        ::mkdir(root.c_str(), 0777);
        msgs_.reset(new CountingMsgs(root, stats_));
    }
    return *msgs_;
}

std::string PcBbs::timeText() const {
    time_t now = std::time(nullptr);
    if (now < 1700000000) return "clock not set";
    char b[24];
    std::strftime(b, sizeof b, "%Y-%m-%d %H:%M", std::localtime(&now));
    return b;
}

std::string PcBbs::lanAddress() {  // the first IPv4 address that is up and not loopback, looked up every 30 s
    uint32_t now = host_.nowMs();
    if (haveLanIp_ && now - lanIpMs_ < 30000u) return lanIp_;
    haveLanIp_ = true;
    lanIpMs_ = now;
    lanIp_.clear();
    ifaddrs* all = nullptr;
    if (::getifaddrs(&all) != 0) return lanIp_;
    for (ifaddrs* a = all; a; a = a->ifa_next) {
        if (!a->ifa_addr || a->ifa_addr->sa_family != AF_INET || !(a->ifa_flags & IFF_UP) || (a->ifa_flags & IFF_LOOPBACK)) continue;
        lanIp_ = NetGate::ipText(((sockaddr_in*)a->ifa_addr)->sin_addr.s_addr);
        break;
    }
    ::freeifaddrs(all);
    return lanIp_;
}

int PcBbs::freeSlot() const {
    for (size_t i = 0; i < sessions_.size(); i++)
        if (!sessions_[i]) return (int)i;
    return (int)sessions_.size();
}

int PcBbs::callersOnline() const {
    int n = 0;
    for (auto& s : sessions_) n += s ? 1 : 0;
    return n;
}

bbs::SysInfo PcBbs::sysInfo() {
    bbs::SysInfo s;
    s.ip = lanAddress();
    s.host = dialName();
    s.wifi = "none (a PC)";
    s.rssi = 0;
    s.uptimeSec = host_.nowMs() / 1000;
    uint64_t mem = memAvailable();
    s.freeHeap = mem > 0xFFFFFFFFull ? 0xFFFFFFFFu : (uint32_t)mem;
    s.timeText = timeText();
    s.storage = "data folder, " + std::to_string((unsigned long long)(host_.sdFreeBytes() >> 20)) + " MB free";
    utsname u{};
    s.hardware = std::string("PC ") + (::uname(&u) == 0 ? u.machine : "") + " (axispc)";
    s.callsToday = stats_.dayStats.calls;
    s.nodesTotal = (int)listener_.gate().limits().max_callers;
    for (auto& ses : sessions_) s.nodes.push_back(ses && ses->node ? ses->node->handle() : std::string());
    return s;
}

// The waiting-for-caller screen's live values: every caller on now, then the next free node waiting, the newest events and
// today's counters.
bbs::WfcSnapshot PcBbs::wfcSnapshot() {
    bbs::WfcSnapshot w;
    w.bbsName = cfg_->bbsName;
    w.sysop = cfg_->sysopHandle;
    w.lastCaller = lastCaller_;
    w.ip = lanAddress();
    std::string t = timeText();
    if (t.size() >= 16) w.date = t.substr(0, 10), w.time = t.substr(11, 5);
    w.page = pageText_;
    std::string plat = bbs::nodePlatform(id_);
    for (size_t i = 0; i < sessions_.size(); i++) {
        app::Session* s = sessions_[i].get();
        if (!s) continue;
        bbs::Node* n = s->node.get();
        std::string act = n ? (i < activity_.size() && !activity_[i].empty() ? activity_[i].substr(activity_[i].find('|') + 1) : "Online")
                            : "Setup";
        w.nodes.push_back({(int)i + 1, n ? n->handle() : std::string("setup"), act, n ? n->linkShown() : "", plat});
    }
    if ((uint32_t)callersOnline() < listener_.gate().limits().max_callers)
        w.nodes.push_back({freeSlot() + 1, "", "", "", plat});  // the next caller's node
    std::sort(w.nodes.begin(), w.nodes.end(), [](const bbs::WfcNode& a, const bbs::WfcNode& b) { return a.number < b.number; });
#if AXIS_MODEMLINK
    w.radio = bbs::wfcRadio(lastRadio_, lastRadioWho_);
#endif
    const auto& ev = host_.events().lines();
    for (auto it = ev.rbegin(); it != ev.rend() && w.events.size() < 8; ++it) w.events.push_back(*it);
    stats_.rollDayCounters();
    w.callsToday = stats_.dayStats.calls;
    w.postsToday = stats_.dayStats.posts;
    w.uploadsToday = stats_.dayStats.uploads;
    w.totalCalls = stats_.dayStats.total;
    w.sdFreeKb = (uint32_t)(host_.sdFreeBytes() >> 10);
    uint64_t mem = memAvailable() >> 10;
    w.ramFreeKb = mem > 0xFFFFFFFFull ? 0xFFFFFFFFu : (uint32_t)mem;
    w.uptimeS = host_.nowMs() / 1000;
    w.main = true;
    return w;
}

// A chat line, a page or a node line, to the console and every caller (the board's deliverChat). Node lines never go back
// to the sender; a chat line does (the sender sees it in the conference).
void PcBbs::deliverChat(int from, const std::string& handle, const std::string& text, bool page, int kind, int to) {
    if (page) {
        pageText_ = handle + ": " + text;
        host_.event("PAGE from " + handle + " on node " + std::to_string(from) + ": " + text);
    }
    if (console_) {  // (its output goes out with the console's next pump)
        if (kind >= 2) {
            if (!to) console_->nodeIn(kind, from, handle, text);
        } else {
            console_->chatIn(from, handle, text, page);
        }
    }
    for (size_t i = 0; i < sessions_.size(); i++) {
        app::Session* s = sessions_[i].get();
        if (!s || !s->node || !s->line) continue;
        if (kind >= 2) {
            int me = (int)i + 1;
            if (me == from || (to && to != me)) continue;
            s->node->nodeIn(kind, from, handle, text);
        } else {
            s->node->chatIn(from, handle, text, page);
        }
        std::string out = s->node->takeOutput();
        s->line->write((const uint8_t*)out.data(), out.size());
        s->line->flush();
    }
}

// The callers already on keep the settings they called into; the switches and the door list reach them (the board's
// pushBoardToCallers).
void PcBbs::pushToCallers(const std::shared_ptr<const bbs::BbsConfig>& prev) {
    bool only = prev && bbs::Node::onlySwitches(*prev, *cfg_);
    if (console_) console_->applyBoard(cfg_, prev, only);
    for (auto& s : sessions_) {
        if (!s) continue;
        if (s->node) s->node->applyBoard(cfg_, prev, only);
#if AXIS_MODEMLINK
        if (s->line) s->line->acceptLink(cfg_->modemLink);  // modem_link off: calls already on stop taking link reports
#endif
    }
}

void PcBbs::wireServices() {
    app::Services& s = services_;
    s.saveUsers = [this] { saveUsers(); };
    s.sysopRun = [this](const std::string& cmd) { return sysopRun(cmd); };
    s.chat = [this](int slot, const std::string& handle, const std::string& text, bool page) {
        deliverChat(slot + 1, handle, text, page, 0, 0);
    };
    s.who = [this] {
        std::vector<bbs::Node::WhoLine> v;
        for (size_t i = 0; i < sessions_.size(); i++) {
            app::Session* ses = sessions_[i].get();
            if (!ses || !ses->node) continue;
            std::string act = i < activity_.size() && !activity_[i].empty() ? activity_[i].substr(activity_[i].find('|') + 1) : "Online";
            v.push_back({(int)i + 1, ses->node->handle(), act, ses->node->linkShown()});
        }
        return v;
    };
    s.messages = [this]() -> bbs::MessageStore& { return messages(); };
#if AXIS_FILES
    s.files = [this]() -> bbs::FileLibrary& {
        if (!files_) files_.reset(new SdFileLibrary(host_.sdRoot() + "/files", [] { return true; }));
        return *files_;
    };
#endif
    s.rng = pcRandom;
    s.unixNow = [this] { return host_.unixNow(); };
    s.sysInfo = [this] { return sysInfo(); };
    s.wfcSnapshot = [this] { return wfcSnapshot(); };
    if (AXIS_LORD) s.warrior = [this](const std::string& h) { return writeWarrior(h); };
    s.nodeLink = [this](int slot, int kind, int to, const std::string& handle, const std::string& text) {
        deliverChat(slot + 1, handle, text, kind == 1, kind, to);
    };
    s.nodeNumber = [](int slot) { return slot + 1; };  // the lowest free node, from 1
    s.cfgEdit = [this]() -> bbs::BbsConfig& { return cfgEdit(); };
    s.cfgReplace = [this](bbs::BbsConfig c) {
        std::shared_ptr<const bbs::BbsConfig> prev = cfg_;
        cfgSet(std::move(c));
        pushToCallers(prev);
    };
    s.cfgSave = [this] { return saveConfig(); };
    s.shareSettings = [] {};  // (standalone: no cluster to share them with)
    s.areasChanged = [this] { makeAreaFolders(); };
    s.ready = [this] { return setUp(); };
    s.setupPin = [this] { return setupPin_; };
    s.setupChosen = [this] { inSetup_ = true; };
    s.setupSave = [this](bbs::SetupWizard& w) {
        cfgSet(w.result());  // (the No-IP login the wizard may ask for is not kept: axispc has no web jobs)
        bool ok = saveConfig();
        host_.event(std::string("setup ") + (ok ? "saved" : "SAVE FAILED") + " over telnet");
    };
    s.setupFinished = [this] {
        inSetup_ = false;
        makeAreaFolders();
    };
    s.dialName = [this] { return dialName(); };
    s.doorFiles = [this](const std::string& id, std::unique_ptr<door::HostFs>& fs,
                         std::unique_ptr<door::LockTable>&) -> door::LockTable* {
        fs.reset(new PosixHostFs(host_.sdRoot() + "/doors/" + id));
        return &doorLocks_;  // one lock table for every caller's game, as the board's
    };
    s.releaseDoor = [this](int num) { doorLocks_.releaseOwner(num); };
    s.doorNote = [this](bbs::DoorDiag d) { lastDoor_ = d; };
    s.lineEnding = [this](app::Session& ses) {
        if (!ses.node || !ses.node->failedLogin()) return;  // wrong passwords: count it against the caller's address
        uint32_t ip = pcLine(ses)->remoteIp(), now = host_.nowMs();
        loginGuard_.failed(ip, now);
        if (!loginGuard_.allowed(ip, now))
            host_.event("login: " + NetGate::ipText(ip) + " locked out for " + std::to_string(loginGuard_.secondsLeft(ip, now) / 60) + " min");
    };
    s.park = [this](std::unique_ptr<net::TelnetStream> l) {
        if (parked_.size() >= kParkedMax) parked_.erase(parked_.begin());
        parked_.push_back({std::move(l), host_.nowMs()});
    };
    s.callerGone = [this](const std::string& handle) { lastCaller_ = handle; };
#if AXIS_MODEMLINK
    s.radio = [this](app::Session& ses, const net::LinkInfo& li, bool first) {  // the board's radioReport
        uint32_t now = host_.nowMs();
        if (first || ses.node->handle() == lastRadioWho_ || li.tier != lastRadio_.tier || now - lastRadioMs_ > 60000u) {
            lastRadio_ = li;
            lastRadioWho_ = ses.node->handle();
            lastRadioMs_ = now;
        }
        if (first)
            host_.event("radio caller on node " + std::to_string(ses.slot + 1) + ": " + net::linkLabel(li) +
                        (li.modem.empty() ? std::string() : " via " + li.modem));
    };
#endif
    // Each caller's activity ("Messages", "Doors" ...) for the WFC, @NODES and who's online: the Session's status line.
    s.clustered = [] { return true; };
    s.status = [this](int slot, const std::string& handle, const std::string& activity) {
        if ((size_t)slot >= activity_.size()) activity_.resize((size_t)slot + 1);
        activity_[(size_t)slot] = handle + "|" + activity;
    };
    s.romLog = [](const char* line) { std::fputs(line, stdout); };
    // (nap stays empty: one caller's door never stops the others; the loop naps only when nothing moved)
}

std::unique_ptr<bbs::Node> PcBbs::makeLocalNode() {
    std::unique_ptr<bbs::Node> n(new bbs::Node(1, [this] { return sysInfo(); }, cfg_,
                                               services_.files ? &services_.files() : nullptr));
    n->setAccounts(&users_, services_.saveUsers, services_.rng, services_.unixNow);
    n->setWarrior(services_.warrior);
    n->onEvent = [this](const std::string& e) { host_.event(e); };
    n->setSysopRun(services_.sysopRun);
    n->setWfcSnapshot(services_.wfcSnapshot);
    n->setChat([this](const std::string& text, bool page) {
                   if (console_) deliverChat(0, console_->handle(), text, page, 0, 0);  // (the console chats as node 0)
               },
               services_.who);
    n->setMessages(&messages());
    app::ThemeProvider* t = &theme_;
    app::BoardData*     d = &data_;
    n->setArt([t](const std::string& name) { return t->themeArt(name); });
    n->setWfcArt([t] { return t->wfcArtShared(); });
    n->setPoster([t](const std::string& name, size_t off, size_t want, bool* exists) { return t->posterPiece(name, off, want, exists); });
    n->setMenus([t](const std::string& name) { return t->menuFile(name); },
                [t](const std::string& name, const bbs::ArtEmit& piece) { return t->menuArtPieces(name, piece); });
    n->setMenuArt([t](const std::string& name, size_t off, size_t want, bool* exists) { return t->menuArtPiece(name, off, want, exists); });
    n->setExists([t](const std::string& name) { return t->menuExists(name); });
#if AXIS_SCRIPTS
    n->setScripts([t](const std::string& name) { return t->scriptFile(name); }, [t] { return t->scriptNames(); });
#endif
    n->setOneLiners([d] { return d->sharedOneLiners(); }, [d](const std::string& line) { d->sharedAddOneLiner(line); });
    n->setLists([d](const std::string& name) { return d->sharedListLoad(name); },
                [d](const std::string& name, const std::string& line) { d->sharedListAdd(name, line); });
    n->setLastCallers([d] { return d->sharedLastCallers(); }, [](const std::string&) {});  // (the sysop is not a caller)
    return n;
}

bool PcBbs::start(const std::string& modemPath, std::string& why) {
    host_.openEventFile();
    // axis.ini present but unreadable (a 0600 settings file this program's user cannot read) must not load as
    // defaults and then be saved over the real one. Refuse to start; an absent file is a fresh board.
    std::string iniCheck;
    if (!readFileChecked(host_.flashRoot() + "/axis.ini", iniCheck, why)) {
        host_.event("settings: " + why + " (refusing to start so defaults are never saved over your axis.ini)");
        return false;
    }
    std::string statsCheck;
    if (!readFileChecked(host_.flashRoot() + "/stats.txt", statsCheck, why)) {
        host_.event("stats: " + why + " (refusing to start so empty stats are never saved over the real file)");
        return false;
    }
    bool had = loadConfig();
    bool lordArgs = false;  // stock LORD tails gain NOEMS, as the board's setup does
    if (AXIS_LORD)
        for (auto& d : cfgEdit().doors) lordArgs = bbs::migrateLordArgs(d) || lordArgs;
    if (had && lordArgs) saveConfig();
    // users.dat: a file that is there but will not read (a copy from /var/lib/axis with 0600 files, say, that this
    // program's own user cannot read) must never load as an empty base and then be saved empty over the real file at
    // the first clean stop. Refuse to start instead; an absent file is fine (a fresh board).
    std::string usersText;
    if (!readFileChecked(host_.flashRoot() + "/users.dat", usersText, why)) {
        host_.event("users: " + why + " (refusing to start so an empty base is never saved over it)");
        return false;
    }
    users_.parse(usersText);
    data_.load();  // the oneliners and the last callers
    if (!data_.lastCallers().empty()) {  // the WFC's last caller: the newest saved login ("handle|node|time|location")
        std::string h = data_.lastCallers().back().substr(0, data_.lastCallers().back().find('|'));
        if (!h.empty()) lastCaller_ = h;
    }
    stats_.statsLoad(users_);
    pendingSeen_ = users_.pending();
    host_.event(std::string("boot: axispc, ") + bbs::kAxisSoftware + " " + bbs::kAxisVersion);
    makeAreaFolders();
    if (!cfg_->setupDone) {
        char b[8];
        std::snprintf(b, sizeof b, "%06lu", (unsigned long)(host_.random32() % 1000000));
        setupPin_ = b;
        std::printf("axis: setup PIN %s (no screen to show it: type it when a caller is asked for it)\n", setupPin_.c_str());
    }
    std::printf("axis: settings %s\n", cfg_->setupDone ? "loaded" : "not set up yet, first-boot setup over telnet");
    if (!listener_.start(host_.nowMs())) {
        why = "no telnet port could be opened (lan_port and public_port in axis.conf)";
        return false;
    }
    ModemLineHooks mh;  // a radio call: a caller of its own (a PcLine on the socketpair), answered at the next turn
    mh.inject = [this](int fd) { radioFds_.push_back(fd); };
    mh.nowMs = [this] { return host_.nowMs(); };
    modemLineSetHooks(std::move(mh));
    modemLineStart(modemPath.empty() ? hostConfStr("modem", "") : modemPath);
    return true;
}

// A call: through the login lockout and (on a board not set up) the one setup caller, onto the lowest free node.
void PcBbs::answer(int fd, uint32_t ip, bool gated) {
    std::unique_ptr<PcLine> l = PcLine::fromFd(fd, ip);
    if (gated) l->closed = [this, ip] { listener_.closed(ip); };
    uint32_t now = host_.nowMs();
    if (!loginGuard_.allowed(ip, now)) {  // too many failed logins from there lately
        uint32_t    m = (loginGuard_.secondsLeft(ip, now) + 59) / 60;
        std::string t = "\r\nToo many failed logins from your address. Try again in " + std::to_string(m) + " minute" +
                        (m == 1 ? "" : "s") + ".\r\n";
        l->write((const uint8_t*)t.data(), t.size());
        l->flush();
        return;  // (the line closes as it goes)
    }
    if (!setUp() && callersOnline() > 0) {  // setup takes one caller
        std::string busy = bbs::busyScreen();
        l->write((const uint8_t*)busy.data(), busy.size());
        l->flush();
        std::printf("axis: busy, turned a caller away\n");
        return;
    }
    int slot = freeSlot();
    if ((size_t)slot >= sessions_.size()) sessions_.resize((size_t)slot + 1);
    if ((size_t)slot < activity_.size()) activity_[(size_t)slot].clear();
    l->tapNode = slot + 1;
    host_.event("call from " + NetGate::ipText(ip) + (gated ? "" : " (radio)") + " on node " + std::to_string(slot + 1));
    std::unique_ptr<net::TelnetStream> owned(l.release());
    sessions_[(size_t)slot].reset(new app::Session(slot, std::move(owned), services_));
    sessions_[(size_t)slot]->open(false);
}

void PcBbs::drainParked(uint32_t now) {
    for (size_t i = 0; i < parked_.size();) {
        Parked& p = parked_[i];
        if (!p.line->drain() || !p.line->backlog() || now - p.sinceMs > kParkedMs) parked_.erase(parked_.begin() + (long)i);
        else i++;
    }
}

bool PcBbs::moved() {
    uint64_t sum = 0;
    for (auto& s : sessions_)
        if (s && s->line) sum += (uint64_t)s->line->bytesIn() + s->line->bytesOut();
    for (auto& p : parked_) sum += (uint64_t)p.line->bytesOut();
    bool m = sum != bytesSeen_;
    bytesSeen_ = sum;
    return m;
}

// One event loop turn, every caller served on this one thread. Caution: a caller's turn can block the whole thread.
// TelnetStream's blocking flush (push with wait, used while hold is off: a transfer, and on the board the protocol
// blocks per block) spins on idle() until the socket takes the bytes or sendTimeoutMs (about 10 s) passes and the line
// is hung up. On the board each caller has more room; here one frozen reader mid-transfer holds every other caller for
// up to that timeout. This is shared-core behaviour (src/net/telnetstream.cpp) and a real cap would be a core change
// beyond this task; a normal caller's turn keeps hold on, so its flush is non-blocking, and the stop path (above) keeps
// its goodbye best effort too.
bool PcBbs::turn() {
    uint32_t now = host_.nowMs();
    bool     changed = false;
    for (auto& c : listener_.poll(now)) {
        answer(c.fd, c.ip, true);
        changed = true;
    }
    modemLinePoll(now);
    if (!radioFds_.empty()) {
        std::vector<int> fds;
        fds.swap(radioFds_);
        for (int fd : fds) answer(fd, inet_addr("127.0.0.2"), false);  // (never gated: a radio caller is not refused)
        changed = true;
    }
    for (size_t i = 0; i < sessions_.size(); i++) {
        app::Session* s = sessions_[i].get();
        if (!s) continue;
        uint32_t t0 = host_.nowMs();
        s->service(t0);
        uint32_t took = host_.nowMs() - t0;
        if (took > 1000)  // one caller held the loop: say what it was doing
            host_.event("slow turn: node " + std::to_string(i + 1) + " " + std::to_string(took) + " ms, state " +
                        std::to_string(s->node ? (int)s->node->state() : -1) + ", held " +
                        std::to_string(s->line ? (unsigned)s->line->backlog() : 0u));
        if (s->done()) {
            sessions_[i].reset();
            if (i < activity_.size()) activity_[i].clear();
            changed = true;
        }
    }
    while (!sessions_.empty() && !sessions_.back()) sessions_.pop_back();
    drainParked(host_.nowMs());
    if (now - lastRollMs_ > 5000u) {  // the day counters roll at midnight, whoever is on
        lastRollMs_ = now;
        stats_.rollDayCounters();
    }
    return moved() || changed;
}

void PcBbs::stop() {
    std::printf("axis: stopping: hanging up callers\n");
    listener_.stop();
    modemLineHangUp();  // first, so a radio caller's hang-up starts from a quiet line
    for (auto& s : sessions_) {
        if (!s) continue;
        // setHold(true) first so the goodbye is best effort: a caller mid-transfer had hold off (service), and a
        // blocking flush to a frozen reader would spin up to sendTimeoutMs (about 10 s) here on the one thread. With
        // hold on the goodbye goes out only as far as the socket takes now; the rest is parked and drained below.
        if (s->line) s->line->setHold(true);
        if (s->line && s->line->carrier()) s->sendApp("\r\n\r\n   The BBS is stopping. Please call back soon.\x1b[0m\r\n");
        s->hangUp("the BBS is stopping");
        s.reset();
    }
    sessions_.clear();
    // The users and the stats are saved now, before the radio and parked waits: the 5 s alarm backstop (main.cpp's
    // onHardStop) can _exit at any point in those waits, and it saves nothing. A save that comes after them could be
    // lost to a slow radio hang-up.
    saveUsers();
    stats_.statsSave();
    uint32_t t0 = host_.nowMs();
    for (;;) {  // the goodbyes (1.5 s at most) and the radio line's hang-up (4.5 s at most, inside the 5 s backstop)
        uint32_t t = host_.nowMs() - t0;
        drainParked(host_.nowMs());
        if (t > 1500u) parked_.clear();
        bool radio = modemLineBusy() && t < 4500u;
        if (parked_.empty() && !radio) break;
        modemLinePoll(host_.nowMs());
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    modemLineFinish();
    std::printf("axis: stopped\n");
}

// ------------------------------------------------------------------ @ commands
std::string PcBbs::sysopRun(const std::string& cmd) {
    struct Sink : app::Out {
        std::string& s;
        bool         cut = false;
        explicit Sink(std::string& r) : s(r) {}
        void write(const char* p, size_t n) override {
            size_t room = kReplyCap - s.size();
            if (n > room) cut = true;
            s.append(p, n > room ? room : n);
        }
    };
    std::string out;
    Sink        sink(out);
    runCommand(cmd, sink);
    if (sink.cut) {
        out.resize(out.rfind('\n') + 1);  // (no newline at all: npos + 1 is 0)
        out += "axis: reply cut at 6 KB\n";
    }
    return out;
}

void PcBbs::runCommand(const std::string& cmd, app::Out& out) {
    if (cmd == "@NODES") {  // the callers on this program, then the next free node waiting (the board's lists its cluster)
        for (size_t i = 0; i < sessions_.size(); i++) {
            app::Session* s = sessions_[i].get();
            if (!s) continue;
            std::string act = i < activity_.size() && !activity_[i].empty() ? activity_[i].substr(activity_[i].find('|') + 1)
                              : s->node ? "Online" : "Setup";
            out.printf("axis: node %d ip %s handle '%s' activity '%s'%s\n", (int)i + 1,
                       s->line ? NetGate::ipText(pcLine(*s)->remoteIp()).c_str() : "-", s->node ? s->node->handle().c_str() : "",
                       act.c_str(), s->node && s->node->viaModem() ? " (radio)" : "");
        }
        if ((uint32_t)callersOnline() < listener_.gate().limits().max_callers)
            out.printf("axis: node %d ip - handle '' activity ''\n", freeSlot() + 1);  // (the sysop menu shows it "waiting")
    } else if (cmd == "@STATS" || cmd.rfind("@STATS ", 0) == 0) {
        // the WFC counters: "@STATS" lists them, "@STATS total=<n>" sets the lifetime call total
        uint32_t n = 0;
        if (cmd.size() > 7 && !(cmd.compare(7, 6, "total=") == 0 && bbs::parseStatTotal(cmd.substr(13), n))) {
            out.println("@STATS use @STATS or @STATS total=<n> (n from 0 to 4294967295)");
        } else {
            if (cmd.size() > 7) {
                stats_.dayStats.total = n;
                stats_.statsSave();
                host_.event("sysop: lifetime calls set to " + std::to_string(n));
            }
            const bbs::DayStats& s = stats_.dayStats;
            out.printf("@STATS day=%d calls=%u posts=%u uploads=%u total=%u\n", s.day, (unsigned)s.calls, (unsigned)s.posts,
                       (unsigned)s.uploads, (unsigned)s.total);
        }
    } else if (cmd == "@STATUS") {  // one line for the panel: the board's fields, as far as a PC has them
        uint64_t mem = memAvailable();
        bbs::DiagSample d;
        d.heapFree = d.largest = d.minHeap = d.winFree = d.winLargest = mem > 0xFFFFFFFFull ? 0xFFFFFFFFu : (uint32_t)mem;
        d.door = lastDoor_;
        d.outMax = (uint32_t)bbs::Node::outMax();
        d.holdMax = (uint32_t)net::TelnetStream::holdPeak();
        d.artMax = (uint32_t)theme_.artMax();
        char line[384];
        std::snprintf(line, sizeof line, "id=%s role=solo nodes=%u on=%d heap=%u largest=%u up=%lu rssi=0 sd_mb=%u ip=%s",
                      id_.c_str(), (unsigned)listener_.gate().limits().max_callers, callersOnline(), d.heapFree, d.largest,
                      (unsigned long)(host_.nowMs() / 1000), (unsigned)(host_.sdFreeBytes() >> 20), lanAddress().c_str());
        out.print(line);
        out.print(bbs::diagFields(d));
        out.print('\n');
    } else if (cmd == "@LOGCLEAR") {  // the event log (screen and files) and the page shown on the WFC
        host_.clearEvents();
        pageText_.clear();
        out.println("axis: event log cleared");
    } else if (cmd == "@LOG") {  // the event log, newest last
        for (auto& l : host_.events().lines()) out.printf("@LOG %s\n", l.c_str());
        out.println("@LOG END");
    } else {
        std::vector<app::Session*> v;  // every slot, null for a free one
        v.reserve(sessions_.size());
        for (auto& s : sessions_) v.push_back(s.get());
        app::runPortableCommand(cmd, services_, v, out);
    }
}

}  // namespace axis::pcapp
