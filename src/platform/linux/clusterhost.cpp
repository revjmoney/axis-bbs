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
    }
    s = got;
    return true;
}

bool clusterStateSave(const ClusterState& s) {
    std::string text = std::string("state=") + standingName(s.state) + "\nepoch=" + std::to_string(s.epoch) +
                       "\nsince=" + std::to_string(s.since) + "\n";
    return writeFile(stateFile(), text);
}

std::string clusterKeyRead() {
    std::string k;
    if (!readFile(keyFile(), k)) return "";
    while (!k.empty() && (k.back() == '\n' || k.back() == '\r')) k.pop_back();  // (a key written by hand)
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
    } else {
        soloKey = clusterKeyRead();
    }
    Cluster::defaultHost = h;
}

bool takeoverWanted() {
    uint32_t now = millis();
    if (!yieldLogged || now - yieldAt >= kYieldLogMs) {
        std::printf("axis: cluster: a Yield came, not taking main yet\n");
        yieldLogged = true;
        yieldAt = now;
    }
    return false;
}

void soloWatchHeard(const std::string& d, uint32_t fromIp) {
    (void)fromIp;  // (NetGate::isLan already checked by the port)
    uint32_t version = 0;
    if (started != Profile::Solo || !announcement(soloKey, d, version)) return;
    if (version >= axis::cluster::kProtocolVersion) {
        heardMain = true;
        heardAt = millis();
    } else {
        heardOlder = true;
    }
}

void clusterHostWaitSetup() { waitSetup = true; }

namespace {

void broadcastLeft(uint32_t now) {  // "@BCAST This BBS joins its cluster in N minutes. Please finish up."
    uint32_t spent = now - drainStart;
    uint32_t left = spent >= drainMs ? 1 : (drainMs - spent + 59999) / 60000;
    std::string cmd = "@BCAST This BBS joins its cluster in " + std::to_string(left) +
                      " minutes. Please finish up.";
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

void noMain() {
    if (kNoMainAction == NoMainAction::TakeOver) {
        ClusterState st;
        st.state = Standing::Main;
        st.epoch = lastMainEpoch + 1;
        st.since = (long)time(nullptr);
        if (clusterStateSave(st)) hostRestart("no main on the LAN: taking main");
        std::printf("axis: cluster: cannot save the cluster state, so not taking main\n");
    }
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
        if (!following || c->mainKind() != CardKind::Card) return;
        saved.state = Standing::Follower;
        saved.epoch = c->epoch();
        saved.since = (long)time(nullptr);
        if (!clusterStateSave(saved)) std::printf("axis: cluster: cannot save the cluster state\n");
        std::printf("axis: cluster: following the card board again: the hand-back is done\n");
        return;
    }
    if (!following && now - lastFollowing >= noMainMs) noMain();
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
        lastFollowing = now;
        keyAt = now;
        setupAt = now;
    }
    if (started == Profile::Solo) soloPoll(now);
    else if (started == Profile::Follower) followerPoll(now);
    setupPoll(now);
}
