// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The network layer on Linux: WiFiClient and WiFiServer over real sockets, the accept gate, WiFi's addresses, lwIP's
// DNS lookup, and the mDNS calls (not used here). Everything runs on the main thread except the DNS lookups, whose
// answers wait in a queue for hostnetPoll().
#include "platform/linux/hostnet.h"

#include <Arduino.h>
#include <ESPmDNS.h>
#include <WiFi.h>
#include <ifaddrs.h>
#include <lwip/dns.h>
#include <lwip/sockets.h>
#include <mdns.h>
#include <net/if.h>
#include <netdb.h>
#include <poll.h>
#include <sys/resource.h>

#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

#include "platform/linux/hostconf.h"
#include "platform/linux/netgate.h"

namespace {
constexpr uint16_t kPanelPort = 2326;     // main.cpp's panelServer: the one server that is not the telnet one
constexpr uint32_t kWriteStallMs = 3000;  // write() gives up when the socket has taken nothing for this long
constexpr int      kAcceptBurst = 256;    // connections taken off one listener per hasClient()
constexpr int      kMaxLookups = 8;       // DNS lookups running at once
constexpr uint32_t kSilentMs = 30000;     // a caller from outside the LAN that sends nothing at all this long is hung up

uint16_t lanPort = 2323, publicPort = 0;
bool     closing = false;  // the program is stopping: no new callers, and every open one is hung up

void gateLog(const std::string& line) { std::printf("axis: %s\n", line.c_str()); }

std::deque<WiFiClient>& injected() {  // radio calls waiting for the telnet server (leaked: no exit-order trouble)
    static auto* q = new std::deque<WiFiClient>();
    return *q;
}

void tune(int fd) {  // every TCP connection: no Nagle, and a dead peer found within about two minutes
    int on = 1, idle = 60, intvl = 10, cnt = 6;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &on, sizeof on);
    setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &on, sizeof on);
    setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, &idle, sizeof idle);
    setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &intvl, sizeof intvl);
    setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, &cnt, sizeof cnt);
}

bool again(int e) { return e == EAGAIN || e == EWOULDBLOCK || e == EINTR; }

bool dotted(const char* host, uint32_t& out) {
    in_addr a;
    if (!host || inet_pton(AF_INET, host, &a) != 1) return false;
    out = a.s_addr;
    return true;
}

bool lookupV4(const std::string& host, uint32_t& out) {  // blocking: only ever on a lookup thread
    addrinfo hints{}, *res = nullptr;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host.c_str(), nullptr, &hints, &res) != 0 || !res) return false;
    bool ok = false;
    for (addrinfo* r = res; r && !ok; r = r->ai_next)
        if (r->ai_family == AF_INET) {
            out = ((sockaddr_in*)r->ai_addr)->sin_addr.s_addr;
            ok = true;
        }
    freeaddrinfo(res);
    return ok;
}

std::atomic<int> lookups{0};

// Runs fn on a detached thread, counted against kMaxLookups. False when it could not start.
template <class F>
bool startLookup(F fn) {
    if (lookups.fetch_add(1) >= kMaxLookups) {
        lookups.fetch_sub(1);
        return false;
    }
    try {
        std::thread([fn]() mutable {
            fn();
            lookups.fetch_sub(1);
        }).detach();
    } catch (...) {
        lookups.fetch_sub(1);
        return false;
    }
    return true;
}

// A lookup the caller waits for (WiFi.hostByName, WiFiClient::connect by name), at most timeoutMs. A lookup that
// takes longer finishes on its thread and is thrown away.
bool resolveWait(const char* host, int32_t timeoutMs, uint32_t& out) {
    if (dotted(host, out)) return true;
    if (!host || !*host || std::strlen(host) > 253) return false;
    struct Wait {
        std::mutex              m;
        std::condition_variable cv;
        bool                    done = false, ok = false;
        uint32_t                addr = 0;
    };
    auto        w = std::make_shared<Wait>();
    std::string h = host;
    if (!startLookup([w, h]() {
            uint32_t a = 0;
            bool     ok = lookupV4(h, a);
            std::lock_guard<std::mutex> l(w->m);
            w->done = true;
            w->ok = ok;
            w->addr = a;
            w->cv.notify_all();
        }))
        return false;
    std::unique_lock<std::mutex> l(w->m);
    w->cv.wait_for(l, std::chrono::milliseconds(timeoutMs > 0 ? timeoutMs : 3000), [&] { return w->done; });
    if (!w->done || !w->ok) return false;
    out = w->addr;
    return true;
}

// lwIP-style lookups: finished answers wait here for hostnetPoll() on the main thread.
struct DnsDone {
    std::string        name;
    bool               ok;
    uint32_t           addr;
    dns_found_callback cb;
    void*              arg;
};
std::mutex& dnsMutex() {
    static auto* m = new std::mutex();
    return *m;
}
std::vector<DnsDone>& dnsDone() {
    static auto* v = new std::vector<DnsDone>();
    return *v;
}

uint32_t ifAddr(bool mask) {  // the first non-loopback IPv4 address that is up (or its netmask), cached 5 s
    static uint32_t addr = 0, net = 0, at = 0;
    static bool     have = false;
    if (!have || millis() - at > 5000) {
        addr = net = 0;
        ifaddrs* ifs = nullptr;
        if (getifaddrs(&ifs) == 0) {
            for (ifaddrs* i = ifs; i; i = i->ifa_next) {
                if (!i->ifa_addr || i->ifa_addr->sa_family != AF_INET) continue;
                if ((i->ifa_flags & IFF_LOOPBACK) || !(i->ifa_flags & IFF_UP)) continue;
                addr = ((sockaddr_in*)i->ifa_addr)->sin_addr.s_addr;
                net = i->ifa_netmask ? ((sockaddr_in*)i->ifa_netmask)->sin_addr.s_addr : 0;
                break;
            }
            freeifaddrs(ifs);
        }
        have = true;
        at = millis();
    }
    return mask ? net : addr;
}

int listenOn(uint16_t port) {
    int fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        std::printf("axis: net: cannot make a socket for port %u: %s\n", port, std::strerror(errno));
        return -1;
    }
    int on = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on);
    sockaddr_in sa{};
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = htonl(INADDR_ANY);
    sa.sin_port = htons(port);
    if (bind(fd, (sockaddr*)&sa, sizeof sa) < 0 || listen(fd, 128) < 0) {
        std::printf("axis: net: cannot listen on port %u: %s\n", port, std::strerror(errno));
        ::close(fd);
        return -1;
    }
    return fd;
}
}  // namespace

// ------------------------------------------------------------------ the socket all copies of a WiFiClient share
struct HostSocket;
static std::unordered_set<HostSocket*>& callerSockets() {  // every caller's socket that is open (leaked, as hostGate)
    static auto* s = new std::unordered_set<HostSocket*>();
    return *s;
}

struct HostSocket {
    int      fd;
    uint32_t ip;     // the peer, network byte order (a radio call's made-up address)
    uint16_t port;   // the peer's (0 for a radio call)
    bool     gated;  // let in by the accept gate, which counts it until it closes
    bool     caller = false;  // a caller (accepted, or a radio call): hostnetHangUpCallers() hangs it up
    bool     heard = true;    // a gated caller from outside the LAN has sent a byte (false until it does)
    uint32_t acceptMs = 0;    // when it was accepted
    HostSocket(int f, uint32_t i, uint16_t p, bool g) : fd(f), ip(i), port(p), gated(g) {}
    ~HostSocket() {
        if (caller) callerSockets().erase(this);
        if (fd >= 0) ::close(fd);
        if (gated) hostGate().closed(ip);
    }
    HostSocket(const HostSocket&) = delete;
    HostSocket& operator=(const HostSocket&) = delete;
};

static std::shared_ptr<HostSocket> newCaller(int fd, uint32_t ip, uint16_t port, bool gated) {
    auto s = std::make_shared<HostSocket>(fd, ip, port, gated);
    s->caller = true;
    callerSockets().insert(s.get());
    return s;
}

NetGate& hostGate() {
    static auto* g = new NetGate();  // leaked: sockets closed during exit still find it
    return *g;
}

// ------------------------------------------------------------------ WiFiClient
WiFiClient::WiFiClient(std::shared_ptr<HostSocket> s) : s_(std::move(s)), connected_(s_ && s_->fd >= 0) {}

void WiFiClient::fail() {
    s_.reset();
    connected_ = false;
}
void WiFiClient::stop() { fail(); }

int WiFiClient::fd() const { return s_ ? s_->fd : -1; }

int WiFiClient::connect(IPAddress ip, uint16_t port, int32_t timeoutMs) {
    stop();
    int fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd < 0) return 0;
    sockaddr_in sa{};
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    sa.sin_addr.s_addr = (uint32_t)ip;
    if (::connect(fd, (sockaddr*)&sa, sizeof sa) < 0) {
        if (errno != EINPROGRESS) {
            ::close(fd);
            return 0;
        }
        pollfd   p{fd, POLLOUT, 0};
        uint32_t t0 = millis();
        int      r;
        for (;;) {
            int left = timeoutMs < 0 ? -1 : (int)(timeoutMs - (int32_t)(millis() - t0));
            if (timeoutMs >= 0 && left < 0) left = 0;
            r = poll(&p, 1, left);
            if (r < 0 && errno == EINTR) continue;
            break;
        }
        int err = 0;
        socklen_t len = sizeof err;
        if (r <= 0 || getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) < 0 || err != 0) {
            ::close(fd);
            return 0;
        }
    }
    tune(fd);
    s_ = std::make_shared<HostSocket>(fd, (uint32_t)ip, port, false);
    connected_ = true;
    return 1;
}

int WiFiClient::connect(const char* host, uint16_t port, int32_t timeoutMs) {
    uint32_t a;
    if (!resolveWait(host, timeoutMs, a)) return 0;
    return connect(IPAddress(a), port, timeoutMs);
}

size_t WiFiClient::write(const uint8_t* buf, size_t n) {
    if (!s_ || !connected_ || !buf) return 0;
    size_t   sent = 0;
    uint32_t last = millis();
    while (sent < n && s_) {
        ssize_t w = ::send(s_->fd, buf + sent, n - sent, MSG_DONTWAIT | MSG_NOSIGNAL);
        if (w > 0) {
            sent += (size_t)w;
            last = millis();
            continue;
        }
        if (w < 0 && errno == EINTR) continue;
        if (w < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {  // full for now: wait for room, a few seconds at most
            uint32_t waited = millis() - last;
            if (waited >= kWriteStallMs) break;
            pollfd p{s_->fd, POLLOUT, 0};
            poll(&p, 1, (int)(kWriteStallMs - waited));
            continue;
        }
        fail();  // the peer is gone
    }
    return sent;
}

int WiFiClient::read(uint8_t* buf, size_t n) {
    if (!s_) return -1;
    if (!buf || !n) return 0;
    ssize_t r = recv(s_->fd, buf, n, MSG_DONTWAIT);
    if (r > 0) {
        s_->heard = true;
        return (int)r;
    }
    if (r == 0 || again(errno)) return 0;  // nothing now, or the peer's end (connected() says which)
    fail();
    return -1;
}

int WiFiClient::read() {
    uint8_t b;
    int     r = read(&b, 1);
    if (r < 0) return r;
    return r == 0 ? -1 : b;
}

int WiFiClient::available() {
    if (!s_) return 0;
    int n = 0;
    if (ioctl(s_->fd, FIONREAD, &n) < 0) {
        fail();
        return 0;
    }
    return n;
}

int WiFiClient::peek() {
    if (!s_) return -1;
    uint8_t b;
    ssize_t r = recv(s_->fd, &b, 1, MSG_PEEK | MSG_DONTWAIT);
    if (r == 1) return b;
    if (r < 0 && !again(errno)) fail();
    return -1;
}

void WiFiClient::flush() {
    int a = available();
    if (a <= 0 || !s_) return;
    uint8_t junk[1436];  // the ESP32 core's receive buffer
    recv(s_->fd, junk, (size_t)a < sizeof junk ? (size_t)a : sizeof junk, MSG_DONTWAIT);
}

uint8_t WiFiClient::connected() {
    if (!s_ || !connected_) return 0;
    uint8_t b;
    ssize_t r = recv(s_->fd, &b, 1, MSG_PEEK | MSG_DONTWAIT);
    if (r > 0 || (r < 0 && again(errno))) return 1;  // bytes waiting, or open with nothing to read
    connected_ = false;                              // 0: the peer closed and everything it sent has been read
    return 0;
}

int WiFiClient::setNoDelay(bool on) {
    if (!s_) return -1;
    int f = on ? 1 : 0;
    return setsockopt(s_->fd, IPPROTO_TCP, TCP_NODELAY, &f, sizeof f);
}

IPAddress WiFiClient::remoteIP() const { return s_ ? IPAddress(s_->ip) : IPAddress(); }
uint16_t  WiFiClient::remotePort() const { return s_ ? s_->port : 0; }

IPAddress WiFiClient::localIP() const {
    sockaddr_in sa{};
    socklen_t   len = sizeof sa;
    if (!s_ || getsockname(s_->fd, (sockaddr*)&sa, &len) < 0 || sa.sin_family != AF_INET) return IPAddress();
    return IPAddress((uint32_t)sa.sin_addr.s_addr);
}
uint16_t WiFiClient::localPort() const {
    sockaddr_in sa{};
    socklen_t   len = sizeof sa;
    if (!s_ || getsockname(s_->fd, (sockaddr*)&sa, &len) < 0 || sa.sin_family != AF_INET) return 0;
    return ntohs(sa.sin_port);
}

// ------------------------------------------------------------------ WiFiServer
void WiFiServer::begin(uint16_t port) {
    if (port) port_ = port;
    uint16_t want[2] = {port_, 0};
    bool     lanOnly[2] = {true, false};
    if (port_ == 23) {  // the board's telnet port: the LAN port, and the public one when it is set
        want[0] = lanPort;
        want[1] = publicPort == lanPort ? 0 : publicPort;
        lanOnly[0] = false;  // any address may call the LAN port; the firewall decides who reaches it
    }
    if ((fd_[0] >= 0 || !want[0]) && (fd_[1] >= 0 || !want[1]) && bound_[0] == want[0] && bound_[1] == want[1])
        return;  // already listening there (applyNetwork() calls begin() again with nothing changed)
    end();
    telnet_ = port_ != kPanelPort;
    for (int i = 0; i < 2; i++) {
        bound_[i] = want[i];
        lanOnly_[i] = lanOnly[i];
        if (!want[i]) continue;
        fd_[i] = listenOn(want[i]);
        if (fd_[i] >= 0)
            std::printf("axis: net: listening on 0.0.0.0:%u (%s)\n", want[i],
                        i == 1 ? "public port" : lanOnly[i] ? "LAN addresses only" : "LAN port");
    }
}

void WiFiServer::end() {
    for (int i = 0; i < 2; i++) {
        if (fd_[i] >= 0) ::close(fd_[i]);
        fd_[i] = -1;
        bound_[i] = 0;
    }
    waiting_.clear();  // callers accepted but not yet answered are hung up (and leave the gate's count)
}

void WiFiServer::drain(int i) {
    static uint32_t lastFdLog = 0, lastLanLog = 0;
    for (int k = 0; k < kAcceptBurst && fd_[i] >= 0; k++) {
        sockaddr_in sa{};
        socklen_t   len = sizeof sa;
        int         c = accept4(fd_[i], (sockaddr*)&sa, &len, SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (c < 0) {
            if (errno == EINTR || errno == ECONNABORTED) continue;
            if ((errno == EMFILE || errno == ENFILE) && millis() - lastFdLog > 10000) {
                std::printf("axis: net: out of file handles, callers wait in the queue\n");
                lastFdLog = millis();
            }
            return;
        }
        if (closing) {  // stopping: nobody new
            ::close(c);
            continue;
        }
        uint32_t ip = sa.sin_family == AF_INET ? sa.sin_addr.s_addr : 0;
        bool     lan = NetGate::isLan(ip);
        if (lanOnly_[i] && !lan) {
            if (millis() - lastLanLog > 10000) {
                std::printf("axis: net: refused %s on port %u (LAN addresses only)\n", NetGate::ipText(ip).c_str(),
                            bound_[i]);
                lastLanLog = millis();
            }
            ::close(c);
            continue;
        }
        if (!hostGate().admit(ip, lan, millis())) {
            ::close(c);  // refused: closed before main.cpp ever sees it
            continue;
        }
        tune(c);
        auto s = newCaller(c, ip, ntohs(sa.sin_port), true);
        s->heard = lan;  // the LAN is spared the silent-caller timeout (hostnetPoll)
        s->acceptMs = millis();
        waiting_.push_back(WiFiClient(s));
    }
}

bool WiFiServer::hasClient() {
    if (telnet_) {  // radio calls: whenever the telnet server has begun, listening or not (its port may be taken)
        auto& q = injected();
        while (!q.empty()) {
            waiting_.push_back(q.front());
            q.pop_front();
        }
    }
    for (int i = 0; i < 2; i++)
        if (fd_[i] >= 0) drain(i);
    return !waiting_.empty();
}

WiFiClient WiFiServer::available() {
    if (waiting_.empty() && !hasClient()) return WiFiClient();
    WiFiClient c = waiting_.front();
    waiting_.pop_front();
    return c;
}

// ------------------------------------------------------------------ the host layer's own calls
void hostnetInjectClient(int fd, uint32_t fakeIp) {
    if (fd < 0) return;
    if (closing) {
        ::close(fd);
        return;
    }
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
    fcntl(fd, F_SETFD, fcntl(fd, F_GETFD, 0) | FD_CLOEXEC);
    injected().push_back(WiFiClient(newCaller(fd, fakeIp, 0, false)));
}

void hostnetHangUpCallers() {
    closing = true;
    injected().clear();  // radio calls the telnet server has not taken yet
    // Both ways shut, the descriptor kept: main.cpp reads the caller's end on its next turn and hangs the session up
    // through its own path (the log line, the last caller, the radio call list); its WiFiClient closes the socket.
    for (HostSocket* s : callerSockets())
        if (s->fd >= 0) ::shutdown(s->fd, SHUT_RDWR);
}

size_t hostnetCallersOpen() { return callerSockets().size(); }

void hostnetStart() {
    lanPort = (uint16_t)hostConfInt("lan_port", 2323, 1, 65535);
    publicPort = (uint16_t)hostConfInt("public_port", 0, 0, 65535);
    NetGate::Limits l;
    l.max_callers = (uint32_t)hostConfInt("max_callers", 16, 1, 4096);
    l.max_per_ip = (uint32_t)hostConfInt("max_per_ip", 3, 1, 1024);
    l.max_per_ip_lan = (uint32_t)hostConfInt("max_per_ip_lan", 8, 1, 1024);
    l.rate_per_ip = (uint32_t)hostConfInt("rate_per_ip", 20, 1, 100000);
    hostGate().setLimits(l);
    hostGate().log = gateLog;
    std::printf("axis: net: telnet on LAN port %u, public port %s; gate: %u callers, %u per address (%u LAN), %u new a minute\n",
                lanPort, publicPort ? std::to_string(publicPort).c_str() : "off", l.max_callers, l.max_per_ip,
                l.max_per_ip_lan, l.rate_per_ip);

    rlimit r{};
    if (getrlimit(RLIMIT_NOFILE, &r) == 0) {
        rlim_t want = r.rlim_max == RLIM_INFINITY ? (rlim_t)1048576 : r.rlim_max;
        if (r.rlim_cur < want) {
            rlimit n = r;
            n.rlim_cur = want;
            if (setrlimit(RLIMIT_NOFILE, &n) == 0) r = n;
        }
        if (r.rlim_cur < 256)
            std::printf("axis: net: WARNING: only %lu file handles allowed (ulimit -n), callers may be refused\n",
                        (unsigned long)r.rlim_cur);
    }
}

// A gated caller from outside the LAN that has sent no byte at all within kSilentMs of being accepted is hung up (both
// ways shut, as hostnetHangUpCallers), so connections that never speak cannot hold every node. The LAN and the radio
// calls (not gated) are spared; any byte at all, read or waiting, counts.
static void hangUpSilent() {
    static uint32_t last = 0, lastLog = 0;
    uint32_t        now = millis();
    if (now - last < 1000) return;
    last = now;
    for (HostSocket* s : callerSockets()) {
        if (s->heard || s->fd < 0) continue;
        int n = 0;
        if (ioctl(s->fd, FIONREAD, &n) == 0 && n > 0) {
            s->heard = true;
            continue;
        }
        if (now - s->acceptMs < kSilentMs) continue;
        s->heard = true;  // once
        ::shutdown(s->fd, SHUT_RDWR);
        if (now - lastLog > 10000) {
            std::printf("axis: net: hung up %s: nothing sent in %u s\n", NetGate::ipText(s->ip).c_str(), kSilentMs / 1000);
            lastLog = now;
        }
    }
}

void hostnetPoll() {
    hangUpSilent();
    std::vector<DnsDone> done;
    {
        std::lock_guard<std::mutex> l(dnsMutex());
        done.swap(dnsDone());
    }
    for (auto& d : done) {
        if (!d.cb) continue;
        ip_addr_t a{};
        a.type = IPADDR_TYPE_V4;
        a.u_addr.ip4.addr = d.addr;
        d.cb(d.name.c_str(), d.ok ? &a : nullptr, d.arg);
    }
}

// ------------------------------------------------------------------ lwIP DNS
err_t dns_gethostbyname(const char* hostname, ip_addr_t* addr, dns_found_callback found, void* callback_arg) {
    if (!hostname || !addr) return ERR_ARG;
    uint32_t a;
    if (dotted(hostname, a)) {
        std::memset(addr, 0, sizeof *addr);
        addr->type = IPADDR_TYPE_V4;
        addr->u_addr.ip4.addr = a;
        return ERR_OK;
    }
    if (!found || !*hostname || std::strlen(hostname) > 253) return ERR_ARG;
    std::string h = hostname;
    bool        started = startLookup([h, found, callback_arg]() {
        uint32_t ip = 0;
        bool     ok = lookupV4(h, ip);
        std::lock_guard<std::mutex> l(dnsMutex());
        dnsDone().push_back(DnsDone{h, ok, ip, found, callback_arg});
    });
    return started ? ERR_INPROGRESS : ERR_MEM;
}

// ------------------------------------------------------------------ WiFi's addresses
IPAddress WiFiClass::localIP() { return IPAddress(ifAddr(false)); }
IPAddress WiFiClass::subnetMask() { return IPAddress(ifAddr(true)); }

IPAddress WiFiClass::gatewayIP() {
    FILE* f = std::fopen("/proc/net/route", "r");
    if (!f) return IPAddress();
    char     line[256];
    uint32_t gw = 0;
    while (std::fgets(line, sizeof line, f)) {
        char          ifname[64];
        unsigned long dest, gate, flags;
        if (std::sscanf(line, "%63s %lx %lx %lx", ifname, &dest, &gate, &flags) == 4 && dest == 0 && (flags & 2)) {
            gw = (uint32_t)gate;  // printed as the address's bytes in memory: already network order
            break;
        }
    }
    std::fclose(f);
    return IPAddress(gw);
}

String WiFiClass::macAddress() {  // a locally administered address made from the machine id: stable, never a real MAC
    static String mac;
    if (mac.length()) return mac;
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
        gethostname(host, sizeof host - 1);
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
    mac = s;
    return mac;
}

int WiFiClass::hostByName(const char* host, IPAddress& out) {
    uint32_t a;
    if (!resolveWait(host, 4000, a)) return 0;
    out = IPAddress(a);
    return 1;
}

// ------------------------------------------------------------------ mDNS: not used on this computer
bool MDNSResponder::begin(const String&) {
    static bool said = false;
    if (!said) std::printf("axis: mdns: not used on this computer (callers dial the address and port)\n");
    said = true;
    return false;
}

mdns_search_once_t* mdns_query_async_new(const char*, const char*, const char*, uint16_t, uint32_t, size_t,
                                         mdns_query_notify_t) {
    static bool said = false;
    if (!said) std::printf("axis: mdns: no modem search on this computer (the modem is on a serial port)\n");
    said = true;
    return nullptr;
}

