// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The LFD3K-SM radio line (modemline.h). One state machine, stepped by modemLinePoll(); every read and write is
// non-blocking, and the waits (the +++ guard time, ATH's answer, the 5 s reopen) are clock checks, never sleeps.
#include "platform/linux/modemline.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <termios.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>

#include "platform/linux/host.h"
#include "platform/linux/hostconf.h"
#include "platform/linux/hostnet.h"

uint32_t millis();

namespace {
constexpr const char* kModemName = "LRA FLOWTRON DATAMASTER 3000 SERIAL MODEM - LFD3K-SM";
constexpr uint32_t    kRetryMs = 5000;       // a missing or failed tty is tried again this often
constexpr uint32_t    kSettleMs = 500;       // after opening: the modem's boot chatter is thrown away
constexpr uint32_t    kProbeMs = 3000;       // AT&V's answer (the station name), at most this long
constexpr uint32_t    kLinkEveryMs = 10000;  // between LFD3K-LINK reports
constexpr uint32_t    kAthMs = 3000;         // ATH's OK, at most this long
constexpr uint32_t    kDrainMs = 15000;      // AXIS's last screen going out before +++, at most this long
constexpr uint32_t    kAnswerMs = 5000;      // a call AXIS has not answered by now is logged
constexpr size_t      kToAxisMax = 8192;     // the caller's bytes waiting for AXIS: past this the tty is not read
constexpr size_t      kLineMax = 512;        // one modem result line, at most

constexpr uint8_t IAC = 255, DONT = 254, DO = 253, WONT = 252, WILL = 251, SB = 250, SE = 240;
constexpr uint8_t kOptLink = 90;  // LFD3K-LINK

enum class St {
    Off,     // no modem asked for
    Closed,  // the tty is not open: tried again every kRetryMs
    Settle,  // just opened: input thrown away for kSettleMs
    Probe,   // AT&V sent: the station name read, RING still answered
    Idle,    // result lines: RING answered with ATA, CONNECT starts a call
    Call,    // bytes pumped between the tty and AXIS's socket
    Drain,   // AXIS hung up: its last bytes go out to the caller
    Guard1,  // quiet before +++
    Guard2,  // +++ sent: quiet after it
    Ath,     // ATH sent: its answer awaited
};

struct ModemLine {
    std::string path;
    int         fd = -1;
    St          st = St::Off;
    uint32_t    since = 0;  // when st began
    uint32_t    lastTry = 0;
    bool        tried = false;
    std::string lastWhy;  // the last open failure (logged once until it changes)
    uint32_t    guardMs = 1200;
    std::string station;  // AT&V's NAME
    std::string lines;    // modem output not yet split into lines (Probe, Idle)
    std::string toTty;    // bytes waiting for the tty
    std::string reply;    // ATH's answer so far
    // the call
    int         sock = -1;  // this end of the socketpair; AXIS has the other
    std::string toAxis;     // bytes waiting for AXIS (caller keys, telnet answers, link reports)
    std::string held;       // the caller's last bytes, when they could be the start of NO CARRIER (scanCarrier)
    uint32_t    heldMs = 0;
    uint64_t    up = 0, down = 0;
    uint32_t    callMs = 0;
    bool        heard = false, waitLogged = false;
    // the telnet side, as lfd3k_bridge.py's Telnet class
    int         tn = 0;  // 0 data, 1 IAC, 2 option byte, 3 in SB, 4 IAC in SB
    uint8_t     cmd = 0;
    std::string report;  // the LFD3K-LINK payload, "" for none (no SF tier: option 90 never offered)
    bool        offered = false, linkOn = false, negotiated = false, sentOnce = false;
    uint32_t    lastSent = 0;
    uint32_t    reports = 0;  // sent this call
};
ModemLine         M;
std::thread::id   mainThread;
bool              busy = false;

void say(const std::string& s) { std::printf("axis: modem: %s\n", s.c_str()); }

std::string printable(const std::string& s) {
    std::string o;
    for (char c : s) o.push_back(c >= 32 && c < 127 ? c : ' ');
    return o;
}

void enter(St s, uint32_t now) {
    M.st = s;
    M.since = now;
}

void endCall() {
    if (M.sock >= 0) ::close(M.sock);  // AXIS reads the end of its socket and hangs its session up
    M.sock = -1;
    M.toAxis.clear();
}

void closeTty() {
    if (M.fd >= 0) ::close(M.fd);
    M.fd = -1;
    M.toTty.clear();
    M.lines.clear();
}

void gone(uint32_t now, const char* why) {
    bool inCall = M.sock >= 0;
    std::printf("axis: modem gone (%s)%s\n", why, inCall ? ", the radio caller is hung up" : "");
    endCall();
    closeTty();
    enter(St::Closed, now);
    M.lastTry = now;
    M.tried = true;
    M.lastWhy.clear();
}

// The LFD3K-LINK report for a CONNECT line like "CONNECT LORA SF12 BW500 1172/ARQ/LZ" (lfd3k_bridge.py's
// link_payload), or "" when the line names no SF5..SF12 tier.
bool digits(const std::string& s) {
    if (s.empty()) return false;
    for (char c : s)
        if (c < '0' || c > '9') return false;
    return true;
}

std::string linkPayload(const std::string& modem, const std::string& connect) {
    std::vector<std::string> words;
    for (size_t i = 0; i < connect.size();) {
        while (i < connect.size() && connect[i] == ' ') i++;
        size_t e = connect.find(' ', i);
        if (e == std::string::npos) e = connect.size();
        if (e > i) words.push_back(connect.substr(i, e - i));
        i = e;
    }
    std::string tier;
    for (auto& w : words)
        if (w.size() > 2 && w.compare(0, 2, "SF") == 0 && digits(w.substr(2)) && w.size() <= 4) {
            int n = std::atoi(w.c_str() + 2);
            if (n >= 5 && n <= 12) {
                tier = w;
                break;
            }
        }
    if (tier.empty()) return "";
    std::string safe;
    for (char c : modem) safe.push_back(std::isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.' ? c : '_');
    if (safe.size() > 24) safe.resize(24);
    if (safe.empty()) safe = "-";
    std::string out = "v=1 modem=" + safe + " kind=radio caller=- tier=" + tier;
    for (auto& w : words) {
        size_t slash = w.find('/');
        if (!digits(w.substr(0, slash))) continue;
        out += " bps=" + w.substr(0, slash);
        if (slash != std::string::npos) {
            bool lz = false;
            for (size_t p = slash; p != std::string::npos;) {
                size_t q = w.find('/', p + 1);
                if (w.substr(p + 1, q == std::string::npos ? std::string::npos : q - p - 1) == "LZ") lz = true;
                p = q;
            }
            if (!lz) out += " lz=0";
        }
        break;
    }
    return out;
}

// ------------------------------------------------------------------ the tty
void tryOpen(uint32_t now) {
    M.lastTry = now;
    M.tried = true;
    std::string why;
    int         fd = ::open(M.path.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        why = std::string("cannot open: ") + std::strerror(errno);
    } else if (!isatty(fd)) {
        why = "not a serial port";
    } else if (flock(fd, LOCK_EX | LOCK_NB) < 0) {
        why = "another axis has it open";
    } else {
        termios t{};
        if (tcgetattr(fd, &t) < 0) {
            why = std::string("cannot read its settings: ") + std::strerror(errno);
        } else {
            cfmakeraw(&t);
            t.c_cflag &= ~(CSIZE | PARENB | CSTOPB | CRTSCTS | HUPCL);  // HUPCL off: closing never drops DTR
            t.c_cflag |= CS8 | CREAD | CLOCAL;                          // CLOCAL: no carrier line, no SIGHUP
            t.c_cc[VMIN] = 1;  // with O_NONBLOCK, "nothing yet" is EAGAIN and a 0 read is a hangup
            t.c_cc[VTIME] = 0;
            cfsetispeed(&t, B115200);
            cfsetospeed(&t, B115200);
            if (tcsetattr(fd, TCSANOW, &t) < 0) why = std::string("cannot set 115200 8N1: ") + std::strerror(errno);
        }
    }
    if (!why.empty()) {
        if (why != M.lastWhy) std::printf("axis: modem: %s %s, trying again every 5 s\n", M.path.c_str(), why.c_str());
        M.lastWhy = why;
        if (fd >= 0) ::close(fd);
        return;
    }
    int bits = TIOCM_DTR | TIOCM_RTS;  // raised together and left raised (a pty has no lines: that fails, quietly)
    ioctl(fd, TIOCMBIS, &bits);
    M.fd = fd;
    M.lastWhy.clear();
    M.station.clear();
    say(std::string(kModemName) + " on " + M.path + " (115200 8N1, DTR and RTS raised)");
    enter(St::Settle, now);
}

// Reads what the tty has, at most `room` bytes. False when the tty is gone (gone() has been called).
bool ttyRead(uint32_t now, std::string& got, size_t room) {
    pollfd p{M.fd, POLLIN, 0};
    if (::poll(&p, 1, 0) > 0 && (p.revents & (POLLERR | POLLNVAL | POLLHUP)) && !(p.revents & POLLIN)) {
        gone(now, "the port hung up");
        return false;
    }
    char buf[512];
    while (got.size() < room) {
        ssize_t n = ::read(M.fd, buf, std::min(sizeof buf, room - got.size()));
        if (n > 0) {
            got.append(buf, (size_t)n);
            continue;
        }
        if (n == 0) {
            gone(now, "the port hung up");
            return false;
        }
        if (errno == EINTR) continue;
        if (errno == EAGAIN || errno == EWOULDBLOCK) break;
        gone(now, std::strerror(errno));
        return false;
    }
    return true;
}

bool ttyFlush(uint32_t now) {
    while (!M.toTty.empty()) {
        ssize_t n = ::write(M.fd, M.toTty.data(), M.toTty.size());
        if (n > 0) {
            M.toTty.erase(0, (size_t)n);
            continue;
        }
        if (n < 0 && errno == EINTR) continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
        gone(now, n < 0 ? std::strerror(errno) : "write failed");
        return false;
    }
    return true;
}

bool ttyQuiet() {  // nothing of ours still waits in the driver (a pty may not say: then it counts as quiet)
    int n = 0;
    return !M.toTty.empty() ? false : ioctl(M.fd, TIOCOUTQ, &n) < 0 || n == 0;
}

// The modem's NO CARRIER result code in data mode is framed "\r\nNO CARRIER\r\n" (lfd3k-sm core/src/at.rs
// format_result, verbose results); a caller's terminal sends its text with a bare CR, so "NO CARRIER" typed or
// uploaded in a message never matches. (The V3's CP2102 carries only TX and RX and the firmware drives no DCD, so
// TIOCMGET cannot confirm a hangup.) The code can span reads: a tail of `data` that could be its start is held back
// in M.held (at most 13 bytes) until the next read settles it, or kHoldMs pass (releaseHeld), so a caller's lone CR
// waits at most that long. `fwd` gets the bytes that are surely the caller's; true when the code has come.
const std::string  kNoCarrier = "\r\nNO CARRIER\r\n";
constexpr uint32_t kHoldMs = 100;
bool scanCarrier(const std::string& data, uint32_t now, std::string& fwd) {
    std::string all = M.held + data;
    size_t      at = all.find(kNoCarrier);
    if (at != std::string::npos) {
        fwd = all.substr(0, at);
        M.held.clear();
        return true;
    }
    size_t k = std::min(all.size(), kNoCarrier.size() - 1);
    while (k > 0 && all.compare(all.size() - k, k, kNoCarrier, 0, k) != 0) k--;
    fwd = all.substr(0, all.size() - k);
    M.held = all.substr(all.size() - k);
    if (!data.empty()) M.heldMs = now;
    return false;
}

// ------------------------------------------------------------------ the call's telnet side
std::string frame() {
    std::string f{(char)IAC, (char)SB, (char)kOptLink};
    return f + M.report + std::string{(char)IAC, (char)SE};
}

void afterFeed(uint32_t now) {  // the offer once AXIS has negotiated; a report when one is due
    if (M.report.empty()) return;
    if (M.negotiated && !M.offered) {
        M.offered = true;
        M.toAxis += std::string{(char)IAC, (char)WILL, (char)kOptLink};
    }
    if (M.linkOn && (!M.sentOnce || now - M.lastSent >= kLinkEveryMs)) {
        M.sentOnce = true;
        M.lastSent = now;
        M.reports++;
        M.toAxis += frame();
    }
}

std::string feed(const uint8_t* p, size_t n) {  // AXIS's bytes: telnet commands answered and taken out
    std::string out;
    for (size_t i = 0; i < n; i++) {
        uint8_t b = p[i];
        switch (M.tn) {
        case 0:
            if (b == IAC) M.tn = 1;
            else out.push_back((char)b);
            break;
        case 1:
            if (b == IAC) {
                out.push_back((char)IAC);
                M.tn = 0;
            } else if (b == DO || b == DONT || b == WILL || b == WONT) {
                M.cmd = b;
                M.tn = 2;
            } else {
                M.tn = b == SB ? 3 : 0;
            }
            break;
        case 2:
            M.negotiated = true;
            if (b == kOptLink && M.offered && (M.cmd == DO || M.cmd == DONT)) {
                bool on = M.cmd == DO;  // the answer to our offer: no reply
                if (on && !M.linkOn) say("AXIS said DO 90: radio reports on (" + M.report + ")");
                if (!on) say("AXIS said DONT 90: no radio reports for this call");
                M.linkOn = on;
                if (on) M.sentOnce = false;  // the first report right away
            } else if (M.cmd == DO) {
                M.toAxis += std::string{(char)IAC, (char)WONT, (char)b};
            } else if (M.cmd == WILL) {
                M.toAxis += std::string{(char)IAC, (char)DONT, (char)b};
            }
            M.tn = 0;
            break;
        case 3:
            if (b == IAC) M.tn = 4;
            break;
        default:
            M.tn = b == SE ? 0 : 3;
            break;
        }
    }
    return out;
}

void toAxis(const std::string& keys) {  // the caller's bytes, IAC doubled
    for (char c : keys) {
        M.toAxis.push_back(c);
        if ((uint8_t)c == IAC) M.toAxis.push_back(c);
    }
    M.up += keys.size();
}

std::string counts() {
    std::string s = "(" + std::to_string(M.down) + " bytes to caller, " + std::to_string(M.up) + " from caller";
    if (M.linkOn) s += ", " + std::to_string(M.reports) + " radio report" + (M.reports == 1 ? "" : "s");
    return s + ")";
}

void hangUp(uint32_t now, const std::string& why) {  // AXIS ended the call: drain, guard, +++, guard, ATH
    say(why + " " + counts() + ", hanging up");
    endCall();
    enter(St::Drain, now);
}

void noCarrier(uint32_t now) {  // the caller hung up (or the link dropped): AXIS's socket closes, back to result lines
    say("NO CARRIER " + counts());
    endCall();
    M.toTty.clear();  // AXIS's unsent bytes would reach the modem as commands
    M.lines.clear();
    enter(St::Idle, now);
}

void startCall(uint32_t now, const std::string& connect, const std::string& rest) {
    int sv[2];
    if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0, sv) < 0) {
        say(std::string("no socket for the call (") + std::strerror(errno) + "), hanging up");
        enter(St::Guard1, now);
        return;
    }
    M.sock = sv[0];
    hostnetInjectClient(sv[1], inet_addr("127.0.0.2"));  // the telnet server's next caller, from 127.0.0.2
    M.toAxis.clear();
    M.held.clear();
    M.up = M.down = 0;
    M.callMs = now;
    M.heard = M.waitLogged = false;
    M.tn = 0;
    M.cmd = 0;
    M.report = linkPayload(M.station, connect);
    M.offered = M.linkOn = M.negotiated = M.sentOnce = false;
    M.reports = 0;
    say(printable(connect) + ": a radio caller for the telnet server" +
        (M.report.empty() ? " (no SF tier in it: no radio reports)" : ", radio reports offered (option 90)"));
    enter(St::Call, now);
    if (rest.empty()) return;
    std::string fwd;
    bool        nc = scanCarrier(rest, now, fwd);
    toAxis(fwd);
    if (nc) noCarrier(now);  // the call ended in the same read as its CONNECT
}

// Result lines while no call is up (lfd3k_bridge.py's main loop).
void resultLines(uint32_t now, const std::string& data) {
    M.lines += data;
    for (;;) {
        size_t e = M.lines.find_first_of("\r\n");
        if (e == std::string::npos) break;
        std::string text = M.lines.substr(0, e);
        M.lines.erase(0, e + 1);
        size_t a = text.find_first_not_of(" \t");
        size_t b = text.find_last_not_of(" \t");
        text = a == std::string::npos ? "" : text.substr(a, b - a + 1);
        if (text.empty()) continue;
        if (M.st == St::Probe) {
            std::vector<std::string> w;
            for (size_t i = 0; i < text.size();) {
                size_t j = text.find(' ', i);
                if (j == std::string::npos) j = text.size();
                if (j > i) w.push_back(text.substr(i, j - i));
                i = j + 1;
            }
            for (size_t i = 0; i + 1 < w.size(); i++)
                if (w[i] == "NAME") M.station = w[i + 1];
            if (text == "OK" || text == "ERROR") {
                say("station " + (M.station.empty() ? std::string("unknown") : printable(M.station)) +
                    ", answering radio calls");
                enter(St::Idle, now);
            }
        }
        if (text.compare(0, 4, "RING") == 0) {
            say("RING, answering");
            M.toTty += "ATA\r";
        } else if (text.compare(0, 7, "CONNECT") == 0) {
            if (M.st == St::Probe) say("station " + (M.station.empty() ? std::string("unknown") : printable(M.station)));
            std::string rest = M.lines;
            M.lines.clear();
            while (!rest.empty() && (rest[0] == '\r' || rest[0] == '\n')) rest.erase(0, 1);
            startCall(now, text, rest);
            return;
        } else if (text == "NO CARRIER" || text == "NO ANSWER" || text == "BUSY") {
            say(text);
        }
    }
    if (M.lines.size() > kLineMax) M.lines.erase(0, M.lines.size() - kLineMax);
}

void step(uint32_t now) {
    if (M.st == St::Off) return;
    if (M.st == St::Closed) {
        if (!M.tried || now - M.lastTry >= kRetryMs) tryOpen(now);
        return;
    }
    std::string got;
    switch (M.st) {
    case St::Settle:
        if (!ttyRead(now, got, 65536)) return;
        if (now - M.since >= kSettleMs) {
            M.toTty += "AT&V\r";  // reads the settings, changes none
            M.lines.clear();
            enter(St::Probe, now);
        }
        break;
    case St::Probe:
    case St::Idle:
        if (!ttyRead(now, got, 4096)) return;
        if (!got.empty()) resultLines(now, got);
        if (M.st == St::Probe && now - M.since >= kProbeMs) {
            say("station " + (M.station.empty() ? std::string("unknown (no AT&V answer)") : printable(M.station)) +
                ", answering radio calls");
            enter(St::Idle, now);
        }
        break;
    case St::Call: {
        // caller -> AXIS (not while AXIS is behind: its socket fills and the tty waits in the kernel)
        if (M.toAxis.size() < kToAxisMax) {
            if (!ttyRead(now, got, kToAxisMax - M.toAxis.size())) return;
            if (!got.empty()) {
                std::string fwd;
                bool        nc = scanCarrier(got, now, fwd);
                toAxis(fwd);  // the caller's bytes before a NO CARRIER still count
                if (nc) {
                    if (M.sock >= 0) ::send(M.sock, M.toAxis.data(), M.toAxis.size(), MSG_DONTWAIT | MSG_NOSIGNAL);
                    noCarrier(now);
                    break;
                }
            }
        }
        if (!M.held.empty() && now - M.heldMs >= kHoldMs) {  // it was not NO CARRIER after all: the caller's
            toAxis(M.held);
            M.held.clear();
        }
        // AXIS -> caller (only when the tty has taken the last lot: a slow radio holds AXIS's line, not this loop)
        while (M.toTty.empty() && M.sock >= 0) {
            uint8_t buf[1024];
            ssize_t n = ::recv(M.sock, buf, sizeof buf, MSG_DONTWAIT);
            if (n > 0) {
                M.heard = true;
                std::string clean = feed(buf, (size_t)n);
                M.down += clean.size();
                M.toTty += clean;
                afterFeed(now);
                continue;
            }
            if (n == 0) {
                hangUp(now, "AXIS closed the call");
                break;
            }
            if (errno == EINTR) continue;
            if (errno != EAGAIN && errno != EWOULDBLOCK) hangUp(now, std::string("AXIS's socket failed (") + std::strerror(errno) + ")");
            break;
        }
        if (M.st != St::Call) break;
        afterFeed(now);  // a quiet line: the reports keep coming
        while (!M.toAxis.empty()) {
            ssize_t n = ::send(M.sock, M.toAxis.data(), M.toAxis.size(), MSG_DONTWAIT | MSG_NOSIGNAL);
            if (n > 0) {
                M.toAxis.erase(0, (size_t)n);
                continue;
            }
            if (n < 0 && errno == EINTR) continue;
            if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
            hangUp(now, std::string("AXIS's socket failed (") + std::strerror(errno) + ")");
            break;
        }
        if (M.st == St::Call && !M.heard && !M.waitLogged && now - M.callMs >= kAnswerMs) {
            M.waitLogged = true;
            say("the radio caller has waited 5 s and AXIS has not answered yet (still waiting)");
        }
        break;
    }
    case St::Drain:
        if (!ttyRead(now, got, 4096)) return;
        if (std::string fwd; scanCarrier(got, now, fwd)) {  // the caller went first: +++ and ATH still go (harmless)
            say("NO CARRIER while hanging up (still sending +++ and ATH)");
            M.toTty.clear();
        }
        if (now - M.since >= kDrainMs) M.toTty.clear();  // the radio has stopped taking bytes: give up on them
        if (!ttyFlush(now)) return;
        if (ttyQuiet()) enter(St::Guard1, now);
        break;
    case St::Guard1:
        if (!ttyRead(now, got, 4096)) return;
        if (std::string fwd; scanCarrier(got, now, fwd)) say("NO CARRIER while hanging up (still sending +++ and ATH)");
        if (now - M.since >= M.guardMs) {
            M.toTty += "+++";
            enter(St::Guard2, now);
        }
        break;
    case St::Guard2:
        if (!ttyRead(now, got, 4096)) return;
        if (now - M.since >= M.guardMs) {
            M.toTty += "ATH\r";
            M.reply.clear();
            enter(St::Ath, now);
        }
        break;
    case St::Ath: {
        if (!ttyRead(now, got, 4096)) return;
        M.reply += got;
        if (M.reply.size() > kLineMax) M.reply.erase(0, M.reply.size() - kLineMax);
        size_t code = std::min({M.reply.find("OK"), M.reply.find("ERROR"), M.reply.find("NO CARRIER")});
        if (code != std::string::npos || now - M.since >= kAthMs) {
            size_t      cut = code == std::string::npos ? M.reply.size() : M.reply.find('\n', code);
            cut = cut == std::string::npos ? M.reply.size() : cut + 1;
            std::string r = printable(M.reply.substr(0, cut)), after = M.reply.substr(cut);
            while (r.find("  ") != std::string::npos) r.erase(r.find("  "), 1);
            size_t a = r.find_first_not_of(' '), b = r.find_last_not_of(' ');
            say("hang up: " + (a == std::string::npos ? std::string("no answer") : r.substr(a, b - a + 1)));
            M.lines.clear();
            M.held.clear();
            enter(St::Idle, now);
            if (!after.empty()) resultLines(now, after);  // a RING right behind the OK is answered, not lost
        }
        break;
    }
    default:
        break;
    }
    if (M.fd >= 0) ttyFlush(now);
}

void delayHook() { modemLinePoll(millis()); }
}  // namespace

void modemLineStart(const std::string& path) {
    mainThread = std::this_thread::get_id();
    M.guardMs = (uint32_t)hostConfInt("modem_guard_ms", 1200, 100, 10000);
    if (path.empty()) {
        say("none (give --modem /dev/ttyUSB0 or modem= in axis.conf for the " + std::string(kModemName) + ")");
        return;
    }
    M.path = path;
    enter(St::Closed, 0);
    axis::host::delayHook = delayHook;
    modemLinePoll(millis());
}

void modemLinePoll(uint32_t nowMs) {
    if (std::this_thread::get_id() != mainThread || M.st == St::Off || busy) return;  // main thread only: nothing is locked
    busy = true;
    step(nowMs);
    busy = false;
}
