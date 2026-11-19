// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The cluster state file, the cluster key file and the boot decision (clusterhost.h).
#include "platform/linux/clusterhost.h"

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

#include "cluster/protocol.h"
#include "platform/linux/netgate.h"
#include "platform/linux/startmode.h"

namespace {

Profile started = Profile::Solo;  // clusterHostStart's profile

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
    // The profile's cluster rules (Cluster::defaultHost) come with the cluster network; the Solo defaults until then.
    started = p;
}

void clusterHostPoll(uint32_t now) {
    (void)now;
    (void)started;
}
