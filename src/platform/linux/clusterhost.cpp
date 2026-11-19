// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The cluster state file, the cluster key file, the boot decision, the profiles' cluster rules, the solo watch and
// its drain, and the follower's watchdog (clusterhost.h).
#include "platform/linux/clusterhost.h"

#include <Arduino.h>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"  // cluster.h's Link probe_{...}: the board's header
#include "cluster/cluster.h"
#pragma GCC diagnostic pop
#include "cluster/protocol.h"
#include "platform/cyd/config_store.h"
#include "platform/linux/host.h"
#include "platform/linux/hostconf.h"
#include "platform/linux/hostnet.h"
#include "platform/linux/modemline.h"
#include "platform/linux/netgate.h"
#include "platform/linux/startmode.h"

void nativeRun(const char* cmd);  // main.cpp: one @ command, its output thrown away

namespace {

using axis::cluster::CardKind;
using axis::cluster::Cluster;
using axis::cluster::Role;

// Question 2: the card main is gone and no other board is left to be main. Solo (the default): restart, and the next
// decideProfile hears nothing and starts the solo BBS (it rejoins as a follower when the card board is back).
// TakeOver: save state main and restart as the mirror's main.
enum class NoMainAction { Solo, TakeOver };
constexpr NoMainAction kNoMainAction = NoMainAction::Solo;

constexpr uint32_t kMainQuietMs = 60000;    // the drain is called off when its main has not been heard this long
constexpr uint32_t kOlderLogMs = 600000;    // the "older firmware" line, at most this often
constexpr uint32_t kKeyCheckMs = 5000;      // Solo reads the key file again this often (@CLUSTERKEY may change it)
constexpr uint32_t kBroadcastMs = 60000;    // the drain tells the callers how long is left this often
constexpr uint32_t kSetupCheckMs = 2000;    // a board not set up yet: configLoad() looked at this often
constexpr uint32_t kYieldLogMs = 60000;     // takeoverWanted's line, at most this often
constexpr long     kSoloBackoffS = 600;     // after the watchdog's restart to solo: solo this long, mains ignored
constexpr uint32_t kYieldFreshMs = 40000;   // a takeover is called off when the main has not sent a Yield this long
constexpr uint32_t kHandedBackMs = 30 * 60000;  // state handedback with no card main followed this long: follower

Profile      started = Profile::Solo;  // clusterHostStart's profile
ClusterState saved;                    // the state file as it was at the start (state handedback changes once)
bool         polled = false;           // clusterHostPoll has read axis.conf's drain_minutes and no_main_seconds
uint32_t     drainMs = 10 * 60000, noMainMs = 60000;

// Solo's watch: the key (re-read every kKeyCheckMs), and what the cluster's port has heard
std::string soloKey;
uint32_t    keyAt = 0;
bool        heardMain = false;  // a version 2 main's announcement, last at heardAt
uint32_t    heardAt = 0;
bool        heardOlder = false;  // an older main's announcement since the last poll
bool        olderLogged = false;
uint32_t    olderAt = 0;

// the drain: callers told to finish, new ones held off, then a restart into the cluster
bool     draining = false;
uint32_t drainStart = 0, nextBroadcast = 0;

// the follower
uint32_t lastFollowing = 0;   // when role() was last Follower
uint32_t lastMainEpoch = 0;   // the main's epoch while following (TakeOver claims above it)

// terminal mode, a board not set up yet
bool     waitSetup = false;
uint32_t setupAt = 0;

bool     yieldLogged = false;
uint32_t yieldAt = 0;

// the takeover: started by takeoverWanted (inside the tick), finished by clusterHostPoll
bool     taking = false;
uint32_t takingAt = 0;
uint32_t takeoverWaitMs = 120000;
uint32_t yieldHeardAt = 0;              // the last Yield while taking
uint32_t yieldIp = 0, yieldEpoch = 0;  // the main that sent the first one

uint32_t handedBackAt = 0;  // when this run began in state handedback (clusterHostPoll's first turn)

// MirrorMain: card mains on older firmware heard (Cluster::HostHooks::onOlderCardMain and onOlderCardProved, inside
// the tick); clusterHostPoll steps down once the watch says so
OlderCardWatch olderCards;
bool           olderCardLogged = false, olderPendingLogged = false;
uint32_t       olderCardAt = 0, olderPendingAt = 0;

// the back-off after a failed join is still on (a solo_until more than kSoloBackoffS ahead is a clock that jumped)
bool soloBackoff(const ClusterState& s) {
    long now = (long)time(nullptr);
    return s.soloUntil > now && s.soloUntil - now <= kSoloBackoffS;
}

std::string stateFile() { return axisDataDir().empty() ? "" : axisDataDir() + "/cluster/state"; }
std::string keyFile() { return axisDataDir().empty() ? "" : axisDataDir() + "/cluster.key"; }

uint32_t nowMs() {
    timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint32_t)((uint64_t)t.tv_sec * 1000 + (uint64_t)t.tv_nsec / 1000000);
}

// writes `text` to `path` through a temp file beside it (fsync, then rename), mode 0600
bool writeFile(const std::string& path, const std::string& text) {
    if (path.empty()) return false;
    std::string tmp = path + ".tmp";
    int         fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (fd < 0) return false;
    bool ok = ::fchmod(fd, 0600) == 0;  // (a temp file left over from before keeps its old mode otherwise)
    for (size_t done = 0; ok && done < text.size();) {
        ssize_t w = ::write(fd, text.data() + done, text.size() - done);
        if (w < 0 && errno == EINTR) continue;
        if (w <= 0) ok = false;
        else done += (size_t)w;
    }
    if (ok && ::fsync(fd) != 0) ok = false;
    if (::close(fd) != 0) ok = false;
    if (!ok || ::rename(tmp.c_str(), path.c_str()) != 0) {
        ::unlink(tmp.c_str());
        return false;
    }
    size_t      slash = path.rfind('/');
    std::string dir = slash == std::string::npos ? "." : slash == 0 ? "/" : path.substr(0, slash);
    int         dfd = ::open(dir.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);  // the rename itself to the disk
    if (dfd >= 0) {
        ::fsync(dfd);
        ::close(dfd);
    }
    return true;
}

// the whole file, or false when it is not there or cannot be read
bool readFile(const std::string& path, std::string& out) {
    out.clear();
    if (path.empty()) return false;
    int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) return false;
    char buf[1024];
    bool ok = true;
    for (;;) {
        ssize_t n = ::read(fd, buf, sizeof buf);
        if (n < 0 && errno == EINTR) continue;
        if (n < 0) ok = false;
        if (n <= 0) break;
        out.append(buf, (size_t)n);
        if (out.size() > 64 * 1024) {  // neither file is ever this big
            ok = false;
            break;
        }
    }
    ::close(fd);
    return ok;
}

const char* standingName(Standing s) {
    return s == Standing::Main ? "main" : s == Standing::HandedBack ? "handedback" : "follower";
}

// "AXIS! ip epoch node sd [ver]" opened with the key: true, with its version (1 when it has none)
bool announcement(const std::string& key, std::string d, uint32_t& version) {
    if (key.empty()) return false;  // (an empty key would open what anyone on the LAN can make)
    if (!axis::cluster::openDatagram(key, d) || d.rfind("AXIS! ", 0) != 0) return false;
    std::vector<std::string> p;
    for (size_t pos = 6; pos <= d.size();) {
        size_t sp = d.find(' ', pos);
        p.push_back(d.substr(pos, sp == std::string::npos ? std::string::npos : sp - pos));
        if (sp == std::string::npos) break;
        pos = sp + 1;
    }
    if (p.size() < 3) return false;
    version = p.size() >= 5 ? (uint32_t)std::strtoul(p[4].c_str(), nullptr, 10) : 1;
    return true;
}

}  // namespace

bool clusterStateLoad(ClusterState& s) {
    std::string text;
    if (!readFile(stateFile(), text)) return false;
    ClusterState got;
    size_t       pos = 0;
    while (pos < text.size()) {
        size_t      nl = text.find('\n', pos);
        std::string line = text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
        pos = nl == std::string::npos ? text.size() : nl + 1;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        if (k == "state") got.state = v == "main" ? Standing::Main : v == "handedback" ? Standing::HandedBack : Standing::Follower;
        else if (k == "epoch") got.epoch = (uint32_t)std::strtoul(v.c_str(), nullptr, 10);
        else if (k == "since") got.since = std::strtol(v.c_str(), nullptr, 10);
        else if (k == "solo_until") got.soloUntil = std::strtol(v.c_str(), nullptr, 10);
    }
    s = got;
    return true;
}

bool clusterStateSave(const ClusterState& s) {
    std::string text = std::string("state=") + standingName(s.state) + "\nepoch=" + std::to_string(s.epoch) +
                       "\nsince=" + std::to_string(s.since) + "\nsolo_until=" + std::to_string(s.soloUntil) + "\n";
    return writeFile(stateFile(), text);
}

std::string clusterKeyRead() {
    std::string k;
    if (!readFile(keyFile(), k)) return "";
    while (!k.empty() && (k.back() == '\n' || k.back() == '\r' || k.back() == ' ' || k.back() == '\t'))
        k.pop_back();  // (a key written by hand)
    return k;
}

bool clusterKeyWrite(const std::string& key) { return writeFile(keyFile(), key); }

bool clusterKeyRemove() {
    std::string f = keyFile();
    return !f.empty() && ::unlink(f.c_str()) == 0;
}

Profile decideProfile(uint32_t listenMs) {
    ClusterState st;
    bool         haveState = clusterStateLoad(st);
    std::string  key = clusterKeyRead();
    if (haveState && st.state == Standing::Main) {
        if (!key.empty()) return Profile::MirrorMain;
        std::printf("axis: cluster: the saved state is main but there is no cluster key, so not taking main\n");
    }
    if (haveState && st.state == Standing::HandedBack) return Profile::Follower;
    if (haveState && soloBackoff(st)) {  // the watchdog gave up on a main a moment ago
        std::printf("axis: cluster: staying solo for %ld minutes after a failed join\n", kSoloBackoffS / 60);
        return Profile::Solo;
    }
    if (key.empty()) {
        std::printf("axis: cluster: no cluster key, so not looking for a main\n");
        return Profile::Solo;
    }
    int fd = ::socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        std::printf("axis: cluster: cannot make a socket to look for a main: %s\n", std::strerror(errno));
        return Profile::Solo;
    }
    int on = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on);
    ::setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &on, sizeof on);
    sockaddr_in sa{};
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = htonl(INADDR_ANY);
    sa.sin_port = htons(axis::cluster::kDiscoveryPort);
    if (::bind(fd, (sockaddr*)&sa, sizeof sa) != 0) {
        std::printf("axis: cluster: cannot listen on UDP %u to look for a main: %s\n", axis::cluster::kDiscoveryPort,
                    std::strerror(errno));
        ::close(fd);
        return Profile::Solo;
    }
    std::printf("axis: cluster: listening up to %u s for a main\n", (unsigned)((listenMs + 999) / 1000));
    Profile  got = Profile::Solo;
    bool     older = false;
    uint32_t t0 = nowMs();
    while (!hostShutdownRequested()) {
        uint32_t spent = nowMs() - t0;
        if (spent >= listenMs) break;
        pollfd p{fd, POLLIN, 0};
        uint32_t wait = listenMs - spent;
        if (::poll(&p, 1, (int)(wait < 100 ? wait : 100)) <= 0) continue;
        char        buf[512];
        sockaddr_in from{};
        socklen_t   len = sizeof from;
        ssize_t     n = ::recvfrom(fd, buf, sizeof buf, 0, (sockaddr*)&from, &len);
        if (n <= 0 || !NetGate::isLan(from.sin_addr.s_addr)) continue;
        uint32_t version = 0;
        if (!announcement(key, std::string(buf, (size_t)n), version)) continue;
        if (version >= axis::cluster::kProtocolVersion) {
            std::printf("axis: cluster: heard a main (protocol %u)\n", (unsigned)version);
            got = Profile::Follower;
            break;
        }
        older = true;  // (keeps listening: only a version 2 main is joined)
    }
    ::close(fd);
    if (got == Profile::Solo && older)
        std::printf("axis: cluster: the main runs older firmware: flash it to cluster with this computer\n");
    return got;
}

void clusterHostStart(Profile p) {
    started = p;
    if (!clusterStateLoad(saved)) saved = ClusterState();
    Cluster::HostHooks h;  // Solo: the boards' own rules
    if (p == Profile::Follower) {
        h.mirror = true;
        h.neverMain = true;
        h.minMainVersion = axis::cluster::kProtocolVersion;
        h.onYield = takeoverWanted;
    } else if (p == Profile::MirrorMain) {
        h.mirror = true;
        h.claimMain = true;
        h.claimEpoch = saved.epoch;
        h.onOlderCardMain = [](uint32_t ip) { return olderCards.heard(ip, millis()); };  // (clusterHostPoll steps down)
        h.onOlderCardProved = [](uint32_t ip) { olderCards.proved(ip, millis()); };
    } else {
        soloKey = clusterKeyRead();
    }
    Cluster::defaultHost = h;
}

bool takeoverWanted() {
    uint32_t now = millis();
    if (taking) {  // a Yield again while the callers here finish: still wanted (takeoverPoll checks it is the same main)
        yieldHeardAt = now;
        return false;
    }
    std::string why;
    if (saved.state == Standing::HandedBack) why = "just handed it back";
    else mirrorTakeoverOk(why);
    if (!why.empty()) {
        if (!yieldLogged || now - yieldAt >= kYieldLogMs) {
            std::printf("axis: mirror: not taking main: %s\n", why.c_str());
            yieldLogged = true;
            yieldAt = now;
        }
        return false;
    }
    Cluster* c = Cluster::instance();
    taking = true;  // clusterHostPoll does the rest
    takingAt = yieldHeardAt = now;
    yieldIp = c ? c->mainIp() : 0;
    yieldEpoch = c ? c->epoch() : 0;
    modemLineHold(true);
    hostnetHold(true);
    std::printf("axis: mirror: the main yields to the card mirror: taking main once nobody is on here (at most %u s; "
                "%s)\n",
                (unsigned)(takeoverWaitMs / 1000), mirrorAge().c_str());
    return false;
}

void soloWatchHeard(const std::string& d, uint32_t fromIp) {
    (void)fromIp;  // (NetGate::isLan already checked by the port)
    uint32_t version = 0;
    if (started != Profile::Solo || !announcement(soloKey, d, version)) return;
    if (version >= axis::cluster::kProtocolVersion) {
        if (soloBackoff(saved)) return;  // the back-off after a failed join: no drain yet
        heardMain = true;
        heardAt = millis();
    } else {
        heardOlder = true;
    }
}

void clusterHostWaitSetup() { waitSetup = true; }

namespace {

void broadcastLeft(uint32_t now) {  // "@BCAST This BBS joins its cluster in N minutes. Please finish up." ("1 minute")
    uint32_t spent = now - drainStart;
    uint32_t left = spent >= drainMs ? 1 : (drainMs - spent + 59999) / 60000;
    std::string cmd = "@BCAST This BBS joins its cluster in " + std::to_string(left) +
                      (left == 1 ? " minute" : " minutes") + ". Please finish up.";
    nativeRun(cmd.c_str());
}

void soloPoll(uint32_t now) {
    if (now - keyAt >= kKeyCheckMs) {
        soloKey = clusterKeyRead();
        keyAt = now;
    }
    if (heardOlder) {
        heardOlder = false;
        if (!olderLogged || now - olderAt >= kOlderLogMs) {
            std::printf("axis: cluster: the main runs older firmware: flash it to cluster with this computer\n");
            olderLogged = true;
            olderAt = now;
        }
    }
    bool recent = heardMain && now - heardAt < kMainQuietMs;
    if (!draining) {
        if (!recent) return;
        draining = true;
        drainStart = now;
        nextBroadcast = now;
        std::printf("axis: cluster: heard a main (protocol %u): closing the public port and draining the callers to "
                    "join it (at most %u min)\n",
                    (unsigned)axis::cluster::kProtocolVersion, (unsigned)(drainMs / 60000));
        hostnetClosePublic();
        hostnetHold(true);
        modemLineHold(true);
    }
    if (!recent) {  // the main went quiet (or it was a recording): stay solo, the public port stays closed
        draining = false;
        heardMain = false;
        hostnetHold(false);
        modemLineHold(false);
        std::printf("axis: cluster: the main has not been heard for %u s, staying solo (the public port stays closed "
                    "until the next start)\n",
                    (unsigned)(kMainQuietMs / 1000));
        return;
    }
    if (hostnetBbsCallersOpen() == 0 && !modemLineBusy()) hostRestart("joining the cluster");
    if (now - drainStart >= drainMs) hostRestart("joining the cluster");
    if ((int32_t)(now - nextBroadcast) >= 0) {
        broadcastLeft(now);
        nextBroadcast += kBroadcastMs;
    }
}

// Every takeover, the Yield's and noMain's: the mirror's gates (mirrorTakeoverOk), the card's state kept for the
// hand-back (mirrorSaveForTakeover puts a half-copied file back), state main, and a restart. False (and the callers
// let in again) when any of it says no.
bool takeMain(uint32_t epoch, const char* why) {
    std::string no;
    bool        ok = mirrorTakeoverOk(no);
    if (!ok) std::printf("axis: mirror: not taking main: %s\n", no.c_str());
    else if (!(ok = mirrorSaveForTakeover()))
        std::printf("axis: mirror: cannot keep the card's state for the hand-back, so not taking main\n");
    ClusterState st = saved;
    st.state = Standing::Main;
    st.epoch = epoch;
    st.since = (long)time(nullptr);
    st.soloUntil = 0;
    if (ok && !(ok = clusterStateSave(st))) std::printf("axis: cluster: cannot save the cluster state, so not taking main\n");
    if (!ok) {
        hostnetHold(false);
        modemLineHold(false);
        return false;
    }
    std::printf("axis: mirror: taking main: %s\n", mirrorAge().c_str());
    hostRestart(why);
}

void noMain() {
    if (kNoMainAction == NoMainAction::TakeOver) takeMain(lastMainEpoch + 1, "no main on the LAN: taking main");
    ClusterState st = saved;  // the back-off: decideProfile and the solo watch leave mains alone for a while
    st.soloUntil = (long)time(nullptr) + kSoloBackoffS;
    if (!clusterStateSave(st)) std::printf("axis: cluster: cannot save the cluster state\n");
    hostRestart("no main on the LAN: solo");
}

void followerPoll(uint32_t now) {
    Cluster* c = Cluster::instance();
    bool     following = c && c->role() == Role::Follower;
    if (following) {
        lastFollowing = now;
        lastMainEpoch = c->epoch();
    }
    if (saved.state == Standing::HandedBack) {  // after @HANDBACK: no watchdog until a card main has been followed
        if (!following || c->mainKind() != CardKind::Card) {
            if (now - handedBackAt < kHandedBackMs) return;
            saved.state = Standing::Follower;  // (the card board never came back as main: the usual rules again)
            if (!clusterStateSave(saved)) std::printf("axis: cluster: cannot save the cluster state\n");
            std::printf("axis: cluster: no card board main followed in %u minutes since the hand-back: following the "
                        "usual rules again\n",
                        (unsigned)(kHandedBackMs / 60000));
            return;
        }
        saved.state = Standing::Follower;
        saved.epoch = c->epoch();
        saved.since = (long)time(nullptr);
        if (!clusterStateSave(saved)) std::printf("axis: cluster: cannot save the cluster state\n");
        std::printf("axis: cluster: following the card board again: the hand-back is done\n");
        return;
    }
    if (!following && now - lastFollowing >= noMainMs && !taking) noMain();  // (a takeover under way finishes first)
}

// the takeover takeoverWanted started: once nobody is on here (or the wait is up), state main and a restart
// (called off when, meanwhile, this is no longer a follower of the same cardless main that keeps asking: say the card
// board came back, the cardless main yielded to it and this program now follows the card board)
void takeoverPoll(uint32_t now) {
    if (!taking) return;
    Cluster*    c = Cluster::instance();
    const char* off = !c || c->role() != Role::Follower           ? "this computer no longer follows that main"
                      : c->mainKind() != CardKind::None              ? "the main now holds a card"
                      : c->mainIp() != yieldIp || c->epoch() != yieldEpoch ? "another main"
                      : now - yieldHeardAt >= kYieldFreshMs          ? "the main stopped asking"
                                                                     : nullptr;
    if (off) {
        taking = false;
        hostnetHold(false);
        modemLineHold(false);
        std::printf("axis: mirror: the takeover is called off: %s\n", off);
        return;
    }
    if ((hostnetBbsCallersOpen() || modemLineBusy()) && now - takingAt < takeoverWaitMs) return;
    taking = false;
    takeMain(c->epoch() + 1, "taking over as main from the mirror");
}

// MirrorMain heard a card main on older firmware: it reads this program's 'm' as no card, so both would stay main.
// This program steps down: state follower and a restart (the next start hears an older main and stays solo). Never
// on one announcement (it may be a recording): only once OlderCardWatch says it is heard over time and proved the key.
void olderCardMainPoll(uint32_t now) {
    if (!olderCards.stepDown(now)) {
        if (olderCards.pending(now) && (!olderPendingLogged || now - olderPendingAt >= kOlderLogMs)) {
            olderPendingLogged = true;
            olderPendingAt = now;
            std::printf("axis: cluster: a card board on older firmware announces it is main: this computer stays main "
                        "until that board is heard for 10 s and proves the cluster key\n");
        }
        return;
    }
    if (olderCardLogged && now - olderCardAt < kOlderLogMs) return;
    olderCardLogged = true;
    olderCardAt = now;
    std::printf("axis: cluster: A CARD BOARD ON OLDER FIRMWARE IS ALSO MAIN: this computer stops being main and "
                "restarts as a follower; flash that board to cluster with this computer (%zu changed paths not handed "
                "back stay in mirror.changes and the mirror)\n",
                handbackChangeCount());
    ClusterState st = saved;
    st.state = Standing::Follower;
    st.since = (long)time(nullptr);
    if (!clusterStateSave(st)) {
        std::printf("axis: cluster: cannot save the cluster state, so staying main\n");
        return;
    }
    hostRestart("a card board on older firmware is main");
}

void setupPoll(uint32_t now) {
    if (!waitSetup || now - setupAt < kSetupCheckMs) return;
    setupAt = now;
    if (!configLoad().setupDone) return;
    waitSetup = false;
    Serial.inject("WFC\r");  // the main's settings have set this board up: the waiting-for-caller screen
}

}  // namespace

void clusterHostPoll(uint32_t now) {
    if (!polled) {  // axis.conf is read after clusterHostStart, so its keys are taken here
        polled = true;
        drainMs = (uint32_t)hostConfInt("drain_minutes", 10, 1, 1440) * 60000;
        noMainMs = (uint32_t)hostConfInt("no_main_seconds", 60, 10, 86400) * 1000;
        takeoverWaitMs = (uint32_t)hostConfInt("takeover_wait_seconds", 120, 0, 3600) * 1000;
        lastFollowing = now;
        keyAt = now;
        setupAt = now;
        handedBackAt = now;
    }
    if (started == Profile::Solo) soloPoll(now);
    else if (started == Profile::Follower) {
        takeoverPoll(now);
        followerPoll(now);
        mirrorPoll(now);
    } else if (started == Profile::MirrorMain) {
        olderCardMainPoll(now);
        handbackPoll(now);  // (its end restarts from here, never from inside loop())
    }
    setupPoll(now);
}
