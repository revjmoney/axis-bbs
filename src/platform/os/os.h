// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// What axispc asks of the operating system: the telnet listener's sockets, the sysop console, the data-folder
// helpers the host uses, and how a stop is signalled. Linux is os_posix.cpp (the same calls the program made
// before this split). Windows is os_win.cpp (Winsock, the console, WinHTTP is separate). The ESP32 build does
// not compile this directory.
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace axis::os {

// Root on Linux. An elevated administrator on Windows.
bool privileged();
// stdin and stdout are both terminals (a pipe or a service is not).
bool interactive();

// SIGINT, SIGTERM and SIGHUP, or the Windows console equivalents, call onStop. A stop still running after 5 s
// calls onHard (SIGALRM on Linux, a timer thread on Windows). A fatal signal calls onCrash; on Linux the default
// action follows (SA_RESETHAND). SIGPIPE is ignored where that signal exists.
void installStop(void (*onStop)(int), void (*onHard)(int), void (*onCrash)(int));
// The 5 s stop deadline. Called once from the stop handler. alarm(5) on Linux.
void armStopDeadline();

// One directory. 0 when it was made or was already a directory. `mode` is the Unix mode (0777); Windows creates
// the directory with the user's default ACL and ignores `mode`.
int makeDir(const std::string& path, int mode);

uint64_t    diskFree(const std::string& path);  // bytes free for us, 0 when unknown
uint64_t    memAvailable();                     // bytes, 0 when unknown
std::string lanIpv4();                          // first IPv4 that is up and not loopback, or ""
std::string machineIdText();                    // stable text the host hashes into a fake MAC
std::string osMachine();                        // "Linux x86_64", "Windows x64", ...

using Socket = intptr_t;
constexpr Socket kBadSocket = -1;

struct ListenOut {
    Socket fd = kBadSocket;
    int    err = 0;             // errno or WSAGetLastError
    bool   socketFailed = false;  // true: no socket; false with a bad fd: bind or listen failed
};

struct Accepted {
    Socket   fd = kBadSocket;
    uint32_t ip = 0;  // network order
    bool     again = false;    // nothing waiting
    bool     retry = false;    // interrupted, or the connection was aborted
    bool     noFiles = false;  // out of sockets or file handles
};

std::string errorText(int err);  // strerror, or Windows' system message for a Winsock error

bool     netStart();
ListenOut listenAny(uint16_t port);  // 0.0.0.0, non-blocking, reuse address
Accepted acceptOne(Socket listenFd);
void     closeSocket(Socket s);
// Raise the open-file limit when the OS has one. `warn` is called when the resulting limit is under 256.
void     raiseNoFile(void (*warn)(unsigned long n));

// "a.b.c.d" in network order, the same bytes inet_addr stores. 0 when it is not four numbers 0..255.
inline uint32_t ipv4(const char* text) {
    unsigned b[4] = {};
    if (!text || std::sscanf(text, "%u.%u.%u.%u", &b[0], &b[1], &b[2], &b[3]) != 4) return 0;
    for (unsigned v : b)
        if (v > 255) return 0;
    uint8_t  bytes[4] = {(uint8_t)b[0], (uint8_t)b[1], (uint8_t)b[2], (uint8_t)b[3]};
    uint32_t ip = 0;
    std::memcpy(&ip, bytes, 4);
    return ip;
}

// The sysop's terminal: raw keys, the log moved off stdout, the alternate screen. termRestore is safe twice
// and from the stop handler.
bool termStart(const std::string& logPath, std::string& why);
void termRestore();
void termWrite(const char* s, size_t n);
void termTell(const std::string& line);
// Non-blocking. True with n > 0 when keys were waiting.
bool termRead(char* buf, int cap, int& n);

}  // namespace axis::os
