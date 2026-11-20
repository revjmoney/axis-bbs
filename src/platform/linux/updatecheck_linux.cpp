// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The once-a-day GitHub request. It runs on a side thread so the BBS loop never waits on DNS, TCP or TLS.
// libssl is loaded at the moment of the check (dlopen), not linked, so a box without it still starts and the
// .deb / AppImage dependency list does not change. A missing library, a timeout, a bad certificate or a bad
// body is one log line and a stamp in <data>/update-check, then nothing until the next day.
#include "platform/linux/updatecheck.h"

#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <ctime>
#include <mutex>
#include <thread>

namespace axis::pcupdate {
namespace {

std::mutex              mu;
std::mutex              fdMu;
Cache                   cache;
std::string             reportText;
std::string             currentVer;
std::string             repoName;
std::string             dataDir;
std::string             loggedLatest;
std::string             pendingLog;
bool                    enabled = false;
bool                    alert = false;
bool                    saidClock = false;
bool                    began = false;
int                     liveFd = -1;
std::atomic<bool>       stopFlag{false};
std::atomic<bool>       inflight{false};
std::atomic<bool>       resultReady{false};
std::atomic<bool>       logWaiting{false};
int64_t                 nextLookMs = 0;  // poll() reads the clock once a minute, not every loop turn
std::thread             worker;
int64_t                 memoryChecked = 0;  // a try that was not written (no tls), so this process does not spin

int64_t steadyMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

int64_t unixNow() { return (int64_t)time(nullptr); }

bool repoOk(const std::string& s) {
    auto slash = s.find('/');
    if (slash == std::string::npos || slash == 0 || slash + 1 >= s.size()) return false;
    if (s.find('/', slash + 1) != std::string::npos) return false;
    for (unsigned char c : s) {
        if (c == '/') continue;
        bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
        if (!ok) return false;
    }
    return true;
}

void rebuild() {  // mu held
    alert = enabled && cache.newer && !cache.latest.empty();
    reportText = formatReport(cache, currentVer, enabled);
}

void queueLog(const std::string& line) {  // mu held
    if (pendingLog.empty()) pendingLog = line;
    logWaiting.store(true);
}

std::string takeLog() {
    std::lock_guard<std::mutex> lock(mu);
    std::string                 s;
    s.swap(pendingLog);
    return s;
}

void rememberFd(int fd) {
    std::lock_guard<std::mutex> lock(fdMu);
    liveFd = fd;
}

void closeFd(int& fd) {
    std::lock_guard<std::mutex> lock(fdMu);
    if (liveFd == fd) liveFd = -1;
    if (fd >= 0) {
        ::close(fd);
        fd = -1;
    }
}

void wakeFd() {
    std::lock_guard<std::mutex> lock(fdMu);
    if (liveFd >= 0) ::shutdown(liveFd, SHUT_RDWR);
}

bool waitFd(int fd, bool write, int64_t deadline) {
    while (!stopFlag.load()) {
        int64_t now = steadyMs();
        if (now >= deadline) return false;
        int    ms = (int)std::min<int64_t>(200, deadline - now);
        pollfd p{};
        p.fd = fd;
        p.events = write ? POLLOUT : POLLIN;
        int r = ::poll(&p, 1, ms);
        if (r > 0) return true;
        if (r < 0 && errno != EINTR) return false;
    }
    return false;
}

std::string loadFile(const std::string& path) {
    int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) return {};
    std::string s;
    char        buf[1024];
    bool        tooBig = false;
    for (;;) {
        ssize_t n = ::read(fd, buf, sizeof buf);
        if (n < 0) {
            if (errno == EINTR) continue;
            ::close(fd);
            return {};
        }
        if (n == 0) break;
        if (s.size() + (size_t)n > 16000) {
            tooBig = true;
            break;
        }
        s.append(buf, (size_t)n);
    }
    ::close(fd);
    return tooBig ? std::string() : s;
}

bool saveFile(const std::string& path, const std::string& text) {
    std::string tmp = path + ".tmp";
    int         fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (fd < 0) return false;
    size_t off = 0;
    while (off < text.size()) {
        ssize_t n = ::write(fd, text.data() + off, text.size() - off);
        if (n < 0) {
            if (errno == EINTR) continue;
            ::close(fd);
            ::unlink(tmp.c_str());
            return false;
        }
        off += (size_t)n;
    }
    if (::close(fd) != 0) {
        ::unlink(tmp.c_str());
        return false;
    }
    if (::rename(tmp.c_str(), path.c_str()) != 0) {
        ::unlink(tmp.c_str());
        return false;
    }
    return true;
}

template <class T>
T loadSym(void* lib, const char* name) {
    void* p = dlsym(lib, name);
    T     fn = nullptr;
    static_assert(sizeof(fn) == sizeof(p), "function pointer");
    std::memcpy(&fn, &p, sizeof fn);
    return fn;
}

struct SslFns {
    void* lib = nullptr;
    int (*init)(uint64_t, const void*) = nullptr;
    const void* (*method)() = nullptr;
    void* (*ctxNew)(const void*) = nullptr;
    void (*ctxVerify)(void*, int, void*) = nullptr;
    int (*ctxPaths)(void*) = nullptr;
    void (*ctxFree)(void*) = nullptr;
    void* (*sslNew)(void*) = nullptr;
    void (*sslFree)(void*) = nullptr;
    int (*setFd)(void*, int) = nullptr;
    long (*ctrl)(void*, int, long, void*) = nullptr;
    int (*setHost)(void*, const char*) = nullptr;
    int (*connect)(void*) = nullptr;
    int (*read)(void*, void*, int) = nullptr;
    int (*write)(void*, const void*, int) = nullptr;
    int (*getError)(const void*, int) = nullptr;
    int (*shutdown)(void*) = nullptr;
};

SslFns& sslFns() {
    static SslFns f;
    return f;
}

bool loadSsl() {
    SslFns& f = sslFns();
    if (f.lib) return f.connect != nullptr;
    f.lib = dlopen("libssl.so.3", RTLD_NOW | RTLD_LOCAL);
    if (!f.lib) f.lib = dlopen("libssl.so.1.1", RTLD_NOW | RTLD_LOCAL);
    if (!f.lib) return false;
    f.init = loadSym<int (*)(uint64_t, const void*)>(f.lib, "OPENSSL_init_ssl");
    f.method = loadSym<const void* (*)()>(f.lib, "TLS_client_method");
    f.ctxNew = loadSym<void* (*)(const void*)>(f.lib, "SSL_CTX_new");
    f.ctxVerify = loadSym<void (*)(void*, int, void*)>(f.lib, "SSL_CTX_set_verify");
    f.ctxPaths = loadSym<int (*)(void*)>(f.lib, "SSL_CTX_set_default_verify_paths");
    f.ctxFree = loadSym<void (*)(void*)>(f.lib, "SSL_CTX_free");
    f.sslNew = loadSym<void* (*)(void*)>(f.lib, "SSL_new");
    f.sslFree = loadSym<void (*)(void*)>(f.lib, "SSL_free");
    f.setFd = loadSym<int (*)(void*, int)>(f.lib, "SSL_set_fd");
    f.ctrl = loadSym<long (*)(void*, int, long, void*)>(f.lib, "SSL_ctrl");
    f.setHost = loadSym<int (*)(void*, const char*)>(f.lib, "SSL_set1_host");
    f.connect = loadSym<int (*)(void*)>(f.lib, "SSL_connect");
    f.read = loadSym<int (*)(void*, void*, int)>(f.lib, "SSL_read");
    f.write = loadSym<int (*)(void*, const void*, int)>(f.lib, "SSL_write");
    f.getError = loadSym<int (*)(const void*, int)>(f.lib, "SSL_get_error");
    f.shutdown = loadSym<int (*)(void*)>(f.lib, "SSL_shutdown");
    if (!f.method || !f.ctxNew || !f.ctxVerify || !f.ctxPaths || !f.ctxFree || !f.sslNew || !f.sslFree || !f.setFd || !f.ctrl ||
        !f.setHost || !f.connect || !f.read || !f.write || !f.getError) {
        f.connect = nullptr;
        return false;
    }
    if (f.init) f.init(0, nullptr);
    return true;
}

constexpr int kWantRead = 2;
constexpr int kWantWrite = 3;
constexpr int kSslCtrlHostname = 55;  // SSL_CTRL_SET_TLSEXT_HOSTNAME
constexpr int kVerifyPeer = 1;

bool sslWait(void* ssl, int fd, int rc, int64_t deadline) {
    SslFns& f = sslFns();
    int     err = f.getError(ssl, rc);
    if (err == kWantRead) return waitFd(fd, false, deadline);
    if (err == kWantWrite) return waitFd(fd, true, deadline);
    return false;
}

FetchKind httpsGet(const std::string& repo, const std::string& version, std::string& bodyOut, int& codeOut) {
    bodyOut.clear();
    codeOut = 0;
    if (!loadSsl()) return FetchKind::NoTls;
    if (stopFlag.load()) return FetchKind::NoAnswer;
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* res = nullptr;
    if (getaddrinfo("api.github.com", "443", &hints, &res) != 0 || !res) return FetchKind::NoAnswer;
    int fd = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        freeaddrinfo(res);
        return FetchKind::NoAnswer;
    }
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0) fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    rememberFd(fd);
    int64_t deadline = steadyMs() + 12000;
    int     cr = ::connect(fd, res->ai_addr, res->ai_addrlen);
    freeaddrinfo(res);
    if (cr < 0 && errno != EINPROGRESS) {
        closeFd(fd);
        return FetchKind::NoAnswer;
    }
    if (cr < 0 && !waitFd(fd, true, std::min(deadline, steadyMs() + 5000))) {
        closeFd(fd);
        return FetchKind::NoAnswer;
    }
    int       soerr = 0;
    socklen_t sl = sizeof soerr;
    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &soerr, &sl) < 0 || soerr) {
        closeFd(fd);
        return FetchKind::NoAnswer;
    }
    SslFns& f = sslFns();
    void*   ctx = f.ctxNew(f.method());
    if (!ctx) {
        closeFd(fd);
        return FetchKind::NoAnswer;
    }
    f.ctxVerify(ctx, kVerifyPeer, nullptr);
    f.ctxPaths(ctx);
    void* ssl = f.sslNew(ctx);
    if (!ssl || f.setFd(ssl, fd) != 1 || f.setHost(ssl, "api.github.com") != 1 || f.ctrl(ssl, kSslCtrlHostname, 0, (void*)"api.github.com") != 1) {
        if (ssl) f.sslFree(ssl);
        f.ctxFree(ctx);
        closeFd(fd);
        return FetchKind::NoAnswer;
    }
    for (;;) {
        if (stopFlag.load() || steadyMs() >= deadline) {
            f.sslFree(ssl);
            f.ctxFree(ctx);
            closeFd(fd);
            return FetchKind::NoAnswer;
        }
        int rc = f.connect(ssl);
        if (rc == 1) break;
        if (!sslWait(ssl, fd, rc, deadline)) {
            f.sslFree(ssl);
            f.ctxFree(ctx);
            closeFd(fd);
            return FetchKind::NoAnswer;
        }
    }
    std::string req = buildRequest(repo, version);
    size_t      off = 0;
    while (off < req.size()) {
        if (stopFlag.load() || steadyMs() >= deadline) {
            f.sslFree(ssl);
            f.ctxFree(ctx);
            closeFd(fd);
            return FetchKind::NoAnswer;
        }
        int n = f.write(ssl, req.data() + off, (int)(req.size() - off));
        if (n > 0) {
            off += (size_t)n;
            continue;
        }
        if (!sslWait(ssl, fd, n, deadline)) {
            f.sslFree(ssl);
            f.ctxFree(ctx);
            closeFd(fd);
            return FetchKind::NoAnswer;
        }
    }
    std::string raw;
    constexpr size_t kCap = 512u * 1024u;
    while (raw.size() < kCap) {
        if (stopFlag.load() || steadyMs() >= deadline) break;
        char buf[4096];
        int  n = f.read(ssl, buf, (int)sizeof buf);
        if (n > 0) {
            raw.append(buf, (size_t)n);
            continue;
        }
        int err = f.getError(ssl, n);
        if (err == kWantRead || err == kWantWrite) {
            if (!sslWait(ssl, fd, n, deadline)) break;
            continue;
        }
        break;
    }
    if (f.shutdown) f.shutdown(ssl);
    f.sslFree(ssl);
    f.ctxFree(ctx);
    closeFd(fd);
    if (stopFlag.load()) return FetchKind::NoAnswer;
    HttpMessage msg = parseHttp(raw);
    codeOut = msg.code;
    bodyOut = std::move(msg.body);
    FetchKind kind = classifyHttp(msg);
    if (kind == FetchKind::BadBody && raw.empty()) return FetchKind::NoAnswer;
    return kind;
}

std::string newerLine(const std::string& latest) {
    return "axis: update: pc-v" + latest +
           " is available. The version on the waiting screen blinks; V, or System then Update, shows the notes.";
}

void threadMain() {
    try {
        Cache       prev;
        std::string current, repo, path;
        {
            std::lock_guard<std::mutex> lock(mu);
            prev = cache;
            current = currentVer;
            repo = repoName;
            path = dataDir + "/update-check";
        }
        std::string body;
        int         code = 0;
        FetchKind   kind = httpsGet(repo, current, body, code);
        if (stopFlag.load()) {
            inflight.store(false);
            return;
        }
        int64_t now = unixNow();
        Applied got = applyFetch(prev, now, current, repo, kind, code, body);
        bool    saved = true;
        if (got.remember) saved = saveFile(path, writeCache(got.cache));
        std::string line;
        if (kind == FetchKind::NoTls) line = "axis: update check: libssl is not available; next try in a day";
        else if (kind == FetchKind::RateLimit) line = "axis: update check: GitHub rate limit; next try in a day";
        else if (kind == FetchKind::BadHttp) line = "axis: update check: GitHub refused the request; next try in a day";
        else if (kind == FetchKind::BadBody || (kind == FetchKind::Ok && !got.parsed))
            line = "axis: update check: the release list could not be read; next try in a day";
        else if (kind == FetchKind::NoAnswer) line = "axis: update check: no answer; next try in a day";
        std::lock_guard<std::mutex> lock(mu);
        if (got.remember) cache = got.cache;
        else memoryChecked = now > 0 ? now : memoryChecked;
        rebuild();
        if (got.parsed && cache.newer && cache.latest != loggedLatest) {
            line = newerLine(cache.latest);
            loggedLatest = cache.latest;
        } else if (got.remember && !saved && line.empty()) {
            line = "axis: update check: could not write " + path;
        }
        if (!line.empty()) queueLog(line);
    } catch (...) {
        std::lock_guard<std::mutex> lock(mu);
        memoryChecked = unixNow();
        queueLog("axis: update check: no answer; next try in a day");
    }
    inflight.store(false);
    resultReady.store(true);
}

}  // namespace

void start(const std::string& dir, bool on, const std::string& repo, const std::string& currentVersion) {
    std::lock_guard<std::mutex> lock(mu);
    if (began) return;
    began = true;
    enabled = on;
    dataDir = dir;
    currentVer = currentVersion;
    if (!repoOk(repo)) {
        repoName = kDefaultRepo;
        if (!repo.empty()) std::printf("axis: update check: update_repo is not owner/name, using %s\n", kDefaultRepo);
    } else {
        repoName = repo;
    }
    if (!dir.empty()) cache = readCache(loadFile(dir + "/update-check"));
    rebuild();
    if (!enabled) {
        std::printf("axis: update check: off (update_check=0)\n");
        return;
    }
    if (alert) {
        std::printf("%s\n", newerLine(cache.latest).c_str());
        loggedLatest = cache.latest;
    }
}

void poll() {
    if (logWaiting.exchange(false)) {
        std::string line = takeLog();
        if (!line.empty()) std::printf("%s\n", line.c_str());
    }
    if (!began || !enabled || dataDir.empty()) return;
    if (inflight.load()) return;
    if (resultReady.exchange(false)) {
        if (worker.joinable()) worker.join();
    }
    int64_t ms = steadyMs();
    if (ms < nextLookMs) return;  // the loop turns constantly; the day gate only needs a look once a minute
    nextLookMs = ms + 60000;
    int64_t now = unixNow();
    if (now < 1700000000) {
        if (!saidClock) {
            saidClock = true;
            std::printf("axis: update check: the clock is not set yet\n");
        }
        return;
    }
    int64_t checked;
    {
        std::lock_guard<std::mutex> lock(mu);
        checked = cache.checked;
        if (memoryChecked > checked) checked = memoryChecked;
    }
    if (!due(checked, now)) return;
    stopFlag.store(false);
    inflight.store(true);
    try {
        if (worker.joinable()) worker.join();
        worker = std::thread(threadMain);
    } catch (...) {
        inflight.store(false);
        std::lock_guard<std::mutex> lock(mu);
        memoryChecked = now;
        queueLog("axis: update check: no answer; next try in a day");
    }
}

void stop() {
    stopFlag.store(true);
    wakeFd();
    for (int i = 0; i < 20 && inflight.load(); i++) ::usleep(100000);
    if (worker.joinable()) {
        if (!inflight.load()) worker.join();
        else worker.detach();
    }
}

View view() {
    try {
        std::lock_guard<std::mutex> lock(mu);
        View                        v;
        v.alert = alert;
        v.url = cache.url;
        v.report = reportText;
        return v;
    } catch (...) {
        return {};
    }
}

std::string report() {
    try {
        std::lock_guard<std::mutex> lock(mu);
        if (!reportText.empty()) return reportText;
    } catch (...) {
    }
    return "No release check yet.\n";
}

}  // namespace axis::pcupdate
