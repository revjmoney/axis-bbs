// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// Windows for axispc's OS layer. Sockets are Winsock. The sysop screen is the console, with virtual-terminal
// processing so the same ANSI the Linux build writes is drawn. There is no SIGALRM: the stop deadline is a
// thread. File modes are not Unix modes; makeDir uses the user's default ACL.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "platform/os/os.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>

#include <direct.h>
#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

namespace axis::os {
namespace {
HANDLE                termIn = nullptr;
HANDLE                termOutH = nullptr;
DWORD                 savedIn = 0, savedOut = 0;
UINT                  savedInCp = 0, savedOutCp = 0;
int                   termFd = -1;
volatile LONG         termOn = 0;
bool                  haveTerm = false;
void (*stopFn)(int) = nullptr;
void (*hardFn)(int) = nullptr;
void (*crashFn)(int) = nullptr;
std::atomic<bool>     deadlineArmed{false};

std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    if (n <= 0) return {};
    std::wstring w((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}

std::string narrow(const wchar_t* s) {
    if (!s || !*s) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, s, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 1) return {};
    std::string o((size_t)n - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s, -1, o.data(), n, nullptr, nullptr);
    return o;
}

void putOut(const char* s, size_t n) {
    if (!termOutH) return;
    while (n) {
        DWORD w = 0;
        if (!WriteFile(termOutH, s, (DWORD)n, &w, nullptr) || w == 0) return;
        s += w;
        n -= w;
    }
}

BOOL WINAPI onConsole(DWORD kind) {
    int sig = (kind == CTRL_C_EVENT) ? SIGINT : SIGTERM;
    if (stopFn) stopFn(sig);
    // Closing the window ends the process when this returns. Ctrl-C does not: the main thread stops the BBS.
    if (kind == CTRL_CLOSE_EVENT || kind == CTRL_LOGOFF_EVENT || kind == CTRL_SHUTDOWN_EVENT) {
        for (int i = 0; i < 60; i++) Sleep(100);
    }
    return TRUE;
}

void onCrashSignal(int sig) {
    std::signal(sig, SIG_DFL);
    if (crashFn) crashFn(sig);
}
}  // namespace

bool privileged() {
    HANDLE tok = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok)) return false;
    TOKEN_ELEVATION elev{};
    DWORD           n = 0;
    bool            yes = GetTokenInformation(tok, TokenElevation, &elev, sizeof elev, &n) && elev.TokenIsElevated;
    CloseHandle(tok);
    return yes;
}

bool interactive() { return _isatty(0) && _isatty(1); }

void installStop(void (*onStop)(int), void (*onHard)(int), void (*onCrash)(int)) {
    stopFn = onStop;
    hardFn = onHard;
    crashFn = onCrash;
    SetConsoleCtrlHandler(onConsole, TRUE);
    for (int s : {SIGSEGV, SIGABRT, SIGFPE, SIGILL}) std::signal(s, onCrashSignal);
#ifdef SIGPIPE
    std::signal(SIGPIPE, SIG_IGN);
#endif
}

void armStopDeadline() {
    if (deadlineArmed.exchange(true)) return;
    std::thread([] {
        std::this_thread::sleep_for(std::chrono::seconds(5));
        if (hardFn) hardFn(0);
    }).detach();
}

int makeDir(const std::string& path, int) {
    if (_mkdir(path.c_str()) == 0) return 0;
    if (errno == EEXIST) {
        struct stat st;
        if (::stat(path.c_str(), &st) == 0 && (st.st_mode & _S_IFDIR)) return 0;
    }
    return -1;
}

uint64_t diskFree(const std::string& path) {
    ULARGE_INTEGER free{};
    std::wstring   w = widen(path);
    if (w.empty() || !GetDiskFreeSpaceExW(w.c_str(), &free, nullptr, nullptr)) return 0;
    return (uint64_t)free.QuadPart;
}

uint64_t memAvailable() {
    MEMORYSTATUSEX m{};
    m.dwLength = sizeof m;
    if (!GlobalMemoryStatusEx(&m)) return 0;
    return (uint64_t)m.ullAvailPhys;
}

std::string lanIpv4() {
    ULONG sz = 16 * 1024;
    auto  bytes = [sz] { return std::vector<IP_ADAPTER_ADDRESSES>((sz / sizeof(IP_ADAPTER_ADDRESSES)) + 2); };
    auto  raw = bytes();
    ULONG cap = (ULONG)(raw.size() * sizeof(IP_ADAPTER_ADDRESSES));
    sz = cap;
    auto* addrs = raw.data();
    ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;
    ULONG rc = GetAdaptersAddresses(AF_INET, flags, nullptr, addrs, &sz);
    if (rc == ERROR_BUFFER_OVERFLOW) {
        raw.assign((sz / sizeof(IP_ADAPTER_ADDRESSES)) + 2, {});
        cap = (ULONG)(raw.size() * sizeof(IP_ADAPTER_ADDRESSES));
        sz = cap;
        addrs = raw.data();
        rc = GetAdaptersAddresses(AF_INET, flags, nullptr, addrs, &sz);
    }
    if (rc != NO_ERROR) return {};
    for (auto* a = addrs; a; a = a->Next) {
        if (a->OperStatus != IfOperStatusUp || a->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;
        for (auto* u = a->FirstUnicastAddress; u; u = u->Next) {
            if (!u->Address.lpSockaddr || u->Address.lpSockaddr->sa_family != AF_INET) continue;
            auto*    in = reinterpret_cast<sockaddr_in*>(u->Address.lpSockaddr);
            uint8_t  b[4];
            std::memcpy(b, &in->sin_addr.s_addr, 4);
            if (b[0] == 127) continue;
            char s[16];
            std::snprintf(s, sizeof s, "%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
            return s;
        }
    }
    return {};
}

std::string machineIdText() {
    wchar_t buf[128] = {};
    DWORD   n = sizeof buf;
    if (RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Cryptography", L"MachineGuid", RRF_RT_REG_SZ, nullptr, buf,
                     &n) == ERROR_SUCCESS && buf[0])
        return narrow(buf);
    wchar_t host[256] = {};
    DWORD   hn = 256;
    if (GetComputerNameW(host, &hn) && host[0]) return narrow(host);
    return "windows";
}

std::string osMachine() {
    SYSTEM_INFO si{};
    GetNativeSystemInfo(&si);
    const char* m = "x86";
    if (si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64) m = "x64";
    else if (si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_ARM64) m = "arm64";
    return std::string("Windows ") + m;
}

std::string errorText(int err) {
    wchar_t* buf = nullptr;
    DWORD    n = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr,
                                (DWORD)err, 0, reinterpret_cast<wchar_t*>(&buf), 0, nullptr);
    if (!n || !buf) return "error " + std::to_string(err);
    int need = WideCharToMultiByte(CP_UTF8, 0, buf, -1, nullptr, 0, nullptr, nullptr);
    std::string s;
    if (need > 1) {
        s.resize((size_t)need - 1);
        WideCharToMultiByte(CP_UTF8, 0, buf, -1, s.data(), need, nullptr, nullptr);
    }
    LocalFree(buf);
    while (!s.empty() && (s.back() == '\r' || s.back() == '\n' || s.back() == ' ')) s.pop_back();
    return s.empty() ? "error " + std::to_string(err) : s;
}

bool netStart() {
    static bool done = false;
    if (done) return true;
    WSADATA w;
    if (WSAStartup(MAKEWORD(2, 2), &w) != 0) return false;
    done = true;
    return true;
}

ListenOut listenAny(uint16_t port) {
    ListenOut r;
    if (!netStart()) {
        r.err = WSAGetLastError();
        r.socketFailed = true;
        return r;
    }
    SOCKET fd = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd == INVALID_SOCKET) {
        r.err = WSAGetLastError();
        r.socketFailed = true;
        return r;
    }
    u_long nb = 1;
    ioctlsocket(fd, FIONBIO, &nb);
    SetHandleInformation((HANDLE)fd, HANDLE_FLAG_INHERIT, 0);
    int on = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&on), sizeof on);
    sockaddr_in sa{};
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = htonl(INADDR_ANY);
    sa.sin_port = htons(port);
    if (::bind(fd, (sockaddr*)&sa, sizeof sa) != 0 || ::listen(fd, 128) != 0) {
        r.err = WSAGetLastError();
        ::closesocket(fd);
        return r;
    }
    r.fd = (Socket)fd;
    return r;
}

Accepted acceptOne(Socket listenFd) {
    Accepted    a;
    sockaddr_in sa{};
    int         len = sizeof sa;
    SOCKET      c = ::accept((SOCKET)listenFd, (sockaddr*)&sa, &len);
    if (c == INVALID_SOCKET) {
        int e = WSAGetLastError();
        if (e == WSAEINTR || e == WSAECONNABORTED) a.retry = true;
        else if (e == WSAEMFILE || e == WSAENOBUFS) a.noFiles = true;
        else a.again = true;  // WSAEWOULDBLOCK: nobody else waiting
        return a;
    }
    u_long nb = 1;
    ioctlsocket(c, FIONBIO, &nb);
    SetHandleInformation((HANDLE)c, HANDLE_FLAG_INHERIT, 0);
    a.fd = (Socket)c;
    a.ip = sa.sin_family == AF_INET ? sa.sin_addr.s_addr : 0;
    return a;
}

void closeSocket(Socket s) {
    if (s != kBadSocket) ::closesocket((SOCKET)s);
}

void raiseNoFile(void (*)(unsigned long)) {
    // Winsock does not take its sockets from the C runtime's file-handle table. The default is enough.
}

bool termStart(const std::string& logPath, std::string& why) {
    HANDLE in = GetStdHandle(STD_INPUT_HANDLE);
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (!GetConsoleMode(in, &savedIn) || !GetConsoleMode(out, &savedOut)) {
        why = "cannot read the console's settings";
        return false;
    }
    DWORD om = savedOut | ENABLE_PROCESSED_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    // Processed input keeps Ctrl-C as a stop signal. Virtual-terminal input turns arrows into the same escapes a Linux tty sends.
    DWORD im = ENABLE_PROCESSED_INPUT | ENABLE_VIRTUAL_TERMINAL_INPUT;
    if (!SetConsoleMode(out, om) || !SetConsoleMode(in, im)) {
        SetConsoleMode(out, savedOut);
        SetConsoleMode(in, savedIn);
        why = "cannot put the console in raw mode";
        return false;
    }
    int dupOut = _dup(1);
    int log = _open(logPath.c_str(), _O_WRONLY | _O_CREAT | _O_APPEND | _O_BINARY, _S_IREAD | _S_IWRITE);
    if (dupOut < 0 || log < 0) {
        if (dupOut >= 0) _close(dupOut);
        if (log >= 0) _close(log);
        SetConsoleMode(out, savedOut);
        SetConsoleMode(in, savedIn);
        why = "cannot open the log file " + logPath;
        return false;
    }
    std::fflush(stdout);
    std::fflush(stderr);
    if (_dup2(log, 1) != 0 || _dup2(log, 2) != 0) {
        _close(log);
        _dup2(dupOut, 1);
        _dup2(dupOut, 2);
        _close(dupOut);
        SetConsoleMode(out, savedOut);
        SetConsoleMode(in, savedIn);
        why = "cannot open the log file " + logPath;
        return false;
    }
    _close(log);
    termFd = dupOut;
    termOutH = (HANDLE)_get_osfhandle(dupOut);
    termIn = in;
    savedOutCp = GetConsoleOutputCP();
    savedInCp = GetConsoleCP();
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    haveTerm = true;
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
    HANDLE in = termIn ? termIn : GetStdHandle(STD_INPUT_HANDLE);
    HANDLE out = termOutH ? termOutH : GetStdHandle(STD_OUTPUT_HANDLE);
    if (out) SetConsoleMode(out, savedOut);
    if (in) SetConsoleMode(in, savedIn);
    if (savedOutCp) SetConsoleOutputCP(savedOutCp);
    if (savedInCp) SetConsoleCP(savedInCp);
}

void termWrite(const char* s, size_t n) { putOut(s, n); }

void termTell(const std::string& line) {
    if (haveTerm) {
        std::string s = line + "\n";
        putOut(s.data(), s.size());
    } else {
        std::fprintf(stderr, "%s\n", line.c_str());
    }
}

bool termRead(char* buf, int cap, int& n) {
    n = 0;
    if (!termOn || !termIn || cap <= 0) return false;
    for (;;) {
        DWORD pending = 0;
        if (!GetNumberOfConsoleInputEvents(termIn, &pending) || pending == 0) return false;
        INPUT_RECORD rec{};
        DWORD        got = 0;
        if (!PeekConsoleInputW(termIn, &rec, 1, &got) || got == 0) return false;
        if (rec.EventType == KEY_EVENT && rec.Event.KeyEvent.bKeyDown) break;
        ReadConsoleInputW(termIn, &rec, 1, &got);  // a resize or a focus change is not a key
    }
    DWORD got = 0;
    if (!ReadFile(termIn, buf, (DWORD)cap, &got, nullptr) || got == 0) return false;
    n = (int)got;
    return true;
}

}  // namespace axis::os
