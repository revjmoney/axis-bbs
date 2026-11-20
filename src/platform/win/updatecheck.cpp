// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The once-a-day GitHub request on Windows. The comparison, the cache and the 24 hour gate live in
// updatecheck.cpp and are shared with Linux. This file is the side thread and the HTTPS GET. WinHTTP is part of
// Windows, so there is no separate TLS library to load and the .zip does not grow a DLL for it. A missing WinHTTP,
// a timeout, a bad certificate or a bad body is one log line and a stamp in <data>/update-check, then nothing
// until the next day. The system proxy and the system certificate store are used.
#include "platform/linux/updatecheck.h"

#include "platform/linux/hostconf.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstring>
#include <ctime>
#include <mutex>
#include <string>
#include <thread>

namespace axis::pcupdate {
namespace {

std::mutex              mu;
std::mutex              reqMu;
Cache                   cache;
std::string             reportText;
std::string             currentVer;
std::string             repoName;
std::string             dataDir;
std::string             loggedLatest;
std::string             pendingLog;
bool                    checkOn = false;
bool                    alert = false;
bool                    saidClock = false;
bool                    began = false;
HINTERNET               liveReq = nullptr;
std::atomic<bool>       stopFlag{false};
std::atomic<bool>       inflight{false};
std::atomic<bool>       resultReady{false};
std::atomic<bool>       logWaiting{false};
int64_t                 nextLookMs = 0;
std::thread             worker;
int64_t                 memoryChecked = 0;

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

void rebuild() {
    alert = blinks(checkOn, cache.newer && !cache.latest.empty());
    reportText = formatReport(cache, currentVer, checkOn);
}

void queueLog(const std::string& line) {
    if (pendingLog.empty()) pendingLog = line;
    logWaiting.store(true);
}

std::string takeLog() {
    std::lock_guard<std::mutex> lock(mu);
    std::string                 s;
    s.swap(pendingLog);
    return s;
}

std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    if (n <= 0) return {};
    std::wstring w((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}

bool armReq(HINTERNET h) {
    std::lock_guard<std::mutex> lock(reqMu);
    if (stopFlag.load()) {
        WinHttpCloseHandle(h);
        return false;
    }
    liveReq = h;
    return true;
}

void disarmReq(HINTERNET h) {
    std::lock_guard<std::mutex> lock(reqMu);
    if (liveReq == h) {
        liveReq = nullptr;
        WinHttpCloseHandle(h);
    }
}

void wakeReq() {
    std::lock_guard<std::mutex> lock(reqMu);
    if (liveReq) {
        WinHttpCloseHandle(liveReq);
        liveReq = nullptr;
    }
}

std::string loadFile(const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return {};
    std::string s;
    char        buf[1024];
    bool        tooBig = false;
    size_t      n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) {
        if (s.size() + n > 16000) {
            tooBig = true;
            break;
        }
        s.append(buf, n);
    }
    std::fclose(f);
    return tooBig ? std::string() : s;
}

bool saveFile(const std::string& path, const std::string& text) {
    std::string tmp = path + ".tmp";
    FILE*       f = std::fopen(tmp.c_str(), "wb");
    if (!f) return false;
    size_t n = std::fwrite(text.data(), 1, text.size(), f);
    if (n != text.size()) {
        std::fclose(f);
        std::remove(tmp.c_str());
        return false;
    }
    if (std::fclose(f) != 0) {
        std::remove(tmp.c_str());
        return false;
    }
    std::wstring from = widen(tmp), to = widen(path);
    if (from.empty() || to.empty() || !MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        std::remove(tmp.c_str());
        return false;
    }
    return true;
}

bool versionOk(const std::string& version) {
    if (version.empty() || version.size() > 32) return false;
    for (char c : version)
        if (c != '.' && (c < '0' || c > '9')) return false;
    return true;
}

FetchKind httpsGet(const std::string& repo, const std::string& version, std::string& bodyOut, int& codeOut) {
    bodyOut.clear();
    codeOut = 0;
    std::wstring ua = L"axispc";
    if (versionOk(version)) ua += L"/" + widen(version);
    HINTERNET ses = WinHttpOpen(ua.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!ses) return FetchKind::NoTls;
    // resolve, connect, send, receive. The whole attempt is also bounded below.
    WinHttpSetTimeouts(ses, 4000, 4000, 4000, 4000);
    if (stopFlag.load()) {
        WinHttpCloseHandle(ses);
        return FetchKind::NoAnswer;
    }
    HINTERNET con = WinHttpConnect(ses, L"api.github.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!con) {
        WinHttpCloseHandle(ses);
        return FetchKind::NoAnswer;
    }
    std::wstring path = L"/repos/" + widen(repo) + L"/releases?per_page=100";
    HINTERNET    req = WinHttpOpenRequest(con, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                          WINHTTP_FLAG_SECURE);
    if (!req) {
        WinHttpCloseHandle(con);
        WinHttpCloseHandle(ses);
        return FetchKind::NoAnswer;
    }
    const wchar_t* accept = L"Accept: application/vnd.github+json\r\n";
    WinHttpAddRequestHeaders(req, accept, (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD);
    if (!armReq(req)) {
        WinHttpCloseHandle(con);
        WinHttpCloseHandle(ses);
        return FetchKind::NoAnswer;
    }
    int64_t deadline = steadyMs() + 12000;
    BOOL    sent = WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
    if (sent) sent = WinHttpReceiveResponse(req, nullptr);
    std::string body;
    constexpr size_t kCap = 512u * 1024u;
    if (sent) {
        while (body.size() < kCap && !stopFlag.load() && steadyMs() < deadline) {
            DWORD avail = 0;
            if (!WinHttpQueryDataAvailable(req, &avail)) break;
            if (avail == 0) break;
            if (avail > 8192) avail = 8192;
            std::string chunk((size_t)avail, '\0');
            DWORD       got = 0;
            if (!WinHttpReadData(req, chunk.data(), avail, &got)) break;
            if (got == 0) break;
            body.append(chunk.data(), got);
        }
    }
    DWORD code = 0, sz = sizeof code;
    WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &code, &sz,
                        WINHTTP_NO_HEADER_INDEX);
    disarmReq(req);
    WinHttpCloseHandle(con);
    WinHttpCloseHandle(ses);
    if (stopFlag.load()) return FetchKind::NoAnswer;
    if (!sent && body.empty()) return FetchKind::NoAnswer;
    codeOut = (int)code;
    bodyOut = std::move(body);
    if (code == 429) return FetchKind::RateLimit;
    if (code == 403) {
        std::string low;
        low.reserve(bodyOut.size());
        for (unsigned char c : bodyOut) low.push_back((char)std::tolower(c));
        if (low.find("rate limit") != std::string::npos) return FetchKind::RateLimit;
        return FetchKind::BadHttp;
    }
    if (code != 200) return FetchKind::BadHttp;
    if (bodyOut.empty()) return FetchKind::NoAnswer;
    return FetchKind::Ok;
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
        if (kind == FetchKind::NoTls) line = "axis: update check: WinHTTP is not available; next try in a day";
        else if (kind == FetchKind::RateLimit) line = "axis: update check: GitHub rate limit; next try in a day";
        else if (kind == FetchKind::BadHttp) line = "axis: update check: GitHub refused the request; next try in a day";
        else if (kind == FetchKind::BadBody || (kind == FetchKind::Ok && !got.parsed))
            line = "axis: update check: the release list could not be read; next try in a day";
        else if (kind == FetchKind::NoAnswer) line = "axis: update check: no answer; next try in a day";
        std::lock_guard<std::mutex> lock(mu);
        if (got.remember) cache = got.cache;
        else memoryChecked = now > 0 ? now : memoryChecked;
        rebuild();
        if (!checkOn) line.clear();
        if (got.parsed && checkOn && cache.newer && cache.latest != loggedLatest) {
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
    checkOn = on;
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
    if (!checkOn) {
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
    if (!began || !checkOn || dataDir.empty()) return;
    if (inflight.load()) return;
    if (resultReady.exchange(false)) {
        if (worker.joinable()) worker.join();
    }
    int64_t ms = steadyMs();
    if (ms < nextLookMs) return;
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
    wakeReq();
    for (int i = 0; i < 20 && inflight.load(); i++) std::this_thread::sleep_for(std::chrono::milliseconds(100));
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

bool enabled() {
    std::lock_guard<std::mutex> lock(mu);
    return checkOn;
}

namespace {
std::string lowerArg(std::string_view raw) {
    while (!raw.empty() && (raw.front() == ' ' || raw.front() == '\t')) raw.remove_prefix(1);
    while (!raw.empty() && (raw.back() == ' ' || raw.back() == '\t' || raw.back() == '\r' || raw.back() == '\n')) raw.remove_suffix(1);
    std::string s(raw);
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

bool setEnabled(bool on) {
    if (!began) return false;
    if (!hostConfSet("update_check", on ? "1" : "0")) return false;
    {
        std::lock_guard<std::mutex> lock(mu);
        checkOn = on;
        if (!on) stopFlag.store(true);
        rebuild();
        if (on) nextLookMs = 0;
    }
    if (!on) wakeReq();
    return true;
}
}  // namespace

std::string command(std::string_view raw) {
    std::string arg = lowerArg(raw);
    if (arg.empty()) return report();
    if (arg == "state") return std::string(enabled() ? "on\n" : "off\n");
    if (arg == "on" || arg == "1") {
        if (!setEnabled(true)) return "axis: update check: could not save update_check=1 in axis.conf\n";
        return "axis: update check on\n";
    }
    if (arg == "off" || arg == "0") {
        if (!setEnabled(false)) return "axis: update check: could not save update_check=0 in axis.conf\n";
        return "axis: update check off\n";
    }
    return report();
}

}  // namespace axis::pcupdate
