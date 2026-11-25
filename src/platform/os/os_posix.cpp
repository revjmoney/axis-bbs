// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// Linux (and any other POSIX host) for axispc's OS layer. The calls are the ones axispc made inline before
// they moved here: sockets, termios, statvfs, getifaddrs, signals.
#include "platform/os/os.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/utsname.h>
#include <termios.h>
#include <unistd.h>

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstring>

namespace axis::os {
namespace {
termios saved{};
int     termOut = -1;
volatile sig_atomic_t termOn = 0;
}  // namespace

bool privileged() { return ::geteuid() == 0; }

bool interactive() { return ::isatty(STDIN_FILENO) && ::isatty(STDOUT_FILENO); }

void installStop(void (*onStop)(int), void (*onHard)(int), void (*onCrash)(int)) {
    struct sigaction sa{};
    sigemptyset(&sa.sa_mask);
    sa.sa_handler = onStop;
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
    struct sigaction was{};
    if (sigaction(SIGHUP, nullptr, &was) == 0 && was.sa_handler != SIG_IGN)  // `nohup axispc` keeps running
        sigaction(SIGHUP, &sa, nullptr);
    sa.sa_handler = onHard;
    sigaction(SIGALRM, &sa, nullptr);
    sa.sa_handler = onCrash;
    sa.sa_flags = SA_RESETHAND;
    for (int s : {SIGSEGV, SIGABRT, SIGBUS, SIGFPE, SIGILL}) sigaction(s, &sa, nullptr);
    std::signal(SIGPIPE, SIG_IGN);  // a caller who hangs up mid-write is an error return, not the end of the BBS
}

void armStopDeadline() { ::alarm(5); }

int makeDir(const std::string& path, int mode) {
    if (::mkdir(path.c_str(), (mode_t)mode) == 0) return 0;
    if (errno == EEXIST) {
        struct stat st;
        if (::stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) return 0;
    }
    return -1;
}

uint64_t diskFree(const std::string& path) {
    struct statvfs v;
    if (::statvfs(path.c_str(), &v) != 0) return 0;
    return (uint64_t)v.f_bavail * v.f_frsize;
}

uint64_t memAvailable() {
    FILE* f = std::fopen("/proc/meminfo", "r");
    if (!f) return 0;
    char               line[128];
    unsigned long long kb = 0;
    while (std::fgets(line, sizeof line, f))
        if (std::sscanf(line, "MemAvailable: %llu kB", &kb) == 1) break;
    std::fclose(f);
    return (uint64_t)kb * 1024u;
}

std::string lanIpv4() {
    ifaddrs* all = nullptr;
    if (::getifaddrs(&all) != 0) return {};
    std::string ip;
    for (ifaddrs* a = all; a; a = a->ifa_next) {
        if (!a->ifa_addr || a->ifa_addr->sa_family != AF_INET || !(a->ifa_flags & IFF_UP) || (a->ifa_flags & IFF_LOOPBACK))
            continue;
        uint32_t n = ((sockaddr_in*)a->ifa_addr)->sin_addr.s_addr;
        uint8_t  b[4];
        std::memcpy(b, &n, 4);
        char s[16];
        std::snprintf(s, sizeof s, "%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
        ip = s;
        break;
    }
    ::freeifaddrs(all);
    return ip;
}

std::string machineIdText() {
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
    return id;
}

std::string osMachine() {
    utsname u{};
    if (::uname(&u) != 0) return "Linux ";
    return std::string("Linux ") + u.machine;
}

std::string errorText(int err) { return std::strerror(err ? err : errno); }

bool netStart() { return true; }

ListenOut listenAny(uint16_t port) {
    ListenOut r;
    int       fd = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        r.err = errno;
        r.socketFailed = true;
        return r;
    }
    int on = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on);
    sockaddr_in sa{};
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = htonl(INADDR_ANY);
    sa.sin_port = htons(port);
    if (::bind(fd, (sockaddr*)&sa, sizeof sa) < 0 || ::listen(fd, 128) < 0) {
        r.err = errno;
        ::close(fd);
        return r;
    }
    r.fd = fd;
    return r;
}

Accepted acceptOne(Socket listenFd) {
    Accepted    a;
    sockaddr_in sa{};
    socklen_t   len = sizeof sa;
    int         c = ::accept4((int)listenFd, (sockaddr*)&sa, &len, SOCK_NONBLOCK | SOCK_CLOEXEC);
    if (c < 0) {
        if (errno == EINTR || errno == ECONNABORTED) a.retry = true;
        else if (errno == EMFILE || errno == ENFILE) a.noFiles = true;
        else a.again = true;
        return a;
    }
    a.fd = c;
    a.ip = sa.sin_family == AF_INET ? sa.sin_addr.s_addr : 0;
    return a;
}

void closeSocket(Socket s) {
    if (s != kBadSocket) ::close((int)s);
}

void raiseNoFile(void (*warn)(unsigned long)) {
    rlimit r{};
    if (::getrlimit(RLIMIT_NOFILE, &r) != 0) return;
    rlim_t want = r.rlim_max == RLIM_INFINITY ? (rlim_t)1048576 : r.rlim_max;
    if (r.rlim_cur < want) {
        rlimit n = r;
        n.rlim_cur = want;
        if (::setrlimit(RLIMIT_NOFILE, &n) == 0) r = n;
    }
    if (r.rlim_cur < 256 && warn) warn((unsigned long)r.rlim_cur);
}

namespace {
void putOut(const char* s, size_t n) {
    if (termOut < 0) return;
    while (n) {
        ssize_t w = ::write(termOut, s, n);
        if (w <= 0) return;
        s += w;
        n -= (size_t)w;
    }
}
}  // namespace

bool termStart(const std::string& logPath, std::string& why) {
    if (::tcgetattr(STDIN_FILENO, &saved) != 0) {
        why = "cannot read the terminal's settings";
        return false;
    }
    int out = ::dup(STDOUT_FILENO);
    int log = ::open(logPath.c_str(), O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0600);
    if (out < 0 || log < 0) {
        if (out >= 0) ::close(out);
        if (log >= 0) ::close(log);
        why = "cannot open the log file " + logPath;
        return false;
    }
    std::fflush(stdout);
    std::fflush(stderr);
    ::dup2(log, STDOUT_FILENO);
    ::dup2(log, STDERR_FILENO);
    ::close(log);
    ::fcntl(out, F_SETFD, FD_CLOEXEC);
    termOut = out;
    termios raw = saved;
    raw.c_iflag &= ~(tcflag_t)(BRKINT | ICRNL | INLCR | IGNCR | INPCK | ISTRIP | IXON | IXOFF);
    raw.c_lflag &= ~(tcflag_t)(ICANON | ECHO | ECHONL | IEXTEN);
    raw.c_lflag |= ISIG;
    raw.c_cflag |= CS8;
    raw.c_cc[VQUIT] = _POSIX_VDISABLE;
    raw.c_cc[VSUSP] = _POSIX_VDISABLE;
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    if (::tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) != 0) {
        ::dup2(out, STDOUT_FILENO);
        ::dup2(out, STDERR_FILENO);
        ::close(out);
        termOut = -1;
        why = "cannot put the terminal in raw mode";
        return false;
    }
    termOn = 1;
    const char on[] = "\x1b[?1049h\x1b[?25l\x1b[0m\x1b[2J\x1b[H";
    putOut(on, sizeof on - 1);
    return true;
}

void termRestore() {
    if (!termOn) return;
    termOn = 0;
    const char off[] = "\x1b[0m\x1b[?25h\x1b[?1049l";
    putOut(off, sizeof off - 1);
    ::tcsetattr(STDIN_FILENO, TCSANOW, &saved);
}

void termWrite(const char* s, size_t n) { putOut(s, n); }

void termTell(const std::string& line) {
    if (termOut >= 0) putOut((line + "\n").c_str(), line.size() + 1);
    else std::fprintf(stderr, "%s\n", line.c_str());
}

bool termRead(char* buf, int cap, int& n) {
    n = 0;
    struct pollfd p = {STDIN_FILENO, POLLIN, 0};
    if (::poll(&p, 1, 0) <= 0 || !(p.revents & POLLIN)) return false;
    ssize_t got = ::read(STDIN_FILENO, buf, (size_t)cap);
    if (got <= 0) return false;
    n = (int)got;
    return true;
}

}  // namespace axis::os
