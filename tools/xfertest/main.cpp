// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// Calls an Axis board over telnet like a caller and tries the file transfers end to end:
//   xfertest HOST PORT download AREA# FILE OUT [z|x|k]      download FILE from area AREA# to OUT
//   xfertest HOST PORT upload AREA# LOCALFILE "desc" [z|x|k] upload LOCALFILE into area AREA#
// The PC side runs Axis's own ZMODEM/XMODEM; a real terminal (SyncTERM) is the other check.
#include <cstdlib>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <regex>
#include <string>
#include <thread>
#include <vector>

#include "net/xmodem.h"
#include "net/zmodem.h"
#include "platform/pc/tcpstream.h"

using namespace axis;

namespace {
uint32_t nowMs() {
    return (uint32_t)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

std::string seen;  // what the board has shown, colour codes removed

bool waitFor(door::ByteStream& s, const std::string& pattern, int ms = 20000) {
    std::regex re(pattern);
    for (uint32_t end = nowMs() + ms; nowMs() < end;) {
        while (s.available() > 0) seen += (char)s.read();
        std::string plain = std::regex_replace(seen, std::regex("\x1b\\[[0-9;?]*[A-Za-z]"), "");
        if (std::regex_search(plain, re)) {
            seen.clear();
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    std::string plain = std::regex_replace(seen, std::regex("\x1b\\[[0-9;?]*[A-Za-z]"), "");
    std::fprintf(stderr, "xfertest: no \"%s\"; last seen: ...%s\n", pattern.c_str(),
                 plain.substr(plain.size() > 300 ? plain.size() - 300 : 0).c_str());
    return false;
}

void send(door::ByteStream& s, const std::string& keys) { s.write((const uint8_t*)keys.data(), keys.size()); }

bool runTransfer(net::Transfer& t) {
    uint32_t start = nowMs();
    while (t.step(nowMs())) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    uint32_t ms = nowMs() - start;
    std::printf("xfertest: %s, %u bytes in %u ms (%u bytes/s)%s%s\n", t.result() == net::Transfer::Result::Ok ? "OK" : "FAILED",
                (unsigned)t.bytes(), (unsigned)ms, (unsigned)(ms ? (uint64_t)t.bytes() * 1000 / ms : 0),
                t.error().empty() ? "" : ": ", t.error().c_str());
    return t.result() == net::Transfer::Result::Ok;
}
}  // namespace

int main(int argc, char** argv) {
    if (argc < 7) {
        std::fprintf(stderr, "usage: xfertest HOST PORT download AREA# FILE OUT [z|x|k]\n"
                             "       xfertest HOST PORT upload AREA# LOCALFILE \"description\" [z|x|k]\n");
        return 2;
    }
    std::string host = argv[1], mode = argv[3], area = argv[4];
    char        proto = argc > 7 ? argv[7][0] : 'z';
    auto        line = pc::TcpStream::dial(host.c_str(), (uint16_t)std::atoi(argv[2]));
    if (!line) {
        std::fprintf(stderr, "xfertest: can't connect\n");
        return 1;
    }
    if (!waitFor(*line, "Handle:")) return 1;
    // an account to log in with (boards with accounts): AXIS_HANDLE and AXIS_PASSWORD, kept off the command line
    const char* handle = std::getenv("AXIS_HANDLE");
    const char* pass = std::getenv("AXIS_PASSWORD");
    send(*line, std::string(handle ? handle : "XferTest") + "\r");
    if (pass) {
        if (!waitFor(*line, "assword")) return 1;
        send(*line, std::string(pass) + "\r");
    }
    if (!waitFor(*line, "Command:")) return 1;
    send(*line, "f");
    if (!waitFor(*line, "Area number")) return 1;
    send(*line, area + "\r");
    if (!waitFor(*line, "ownload")) return 1;

    bool ok = false;
    if (mode == "download") {
        std::string file = argv[5], out = argv[6];
        send(*line, "d" + file + "\r");
        if (!waitFor(*line, "MODEM")) return 1;
        send(*line, std::string(1, proto));
        if (!waitFor(*line, "download now")) return 1;
        std::vector<uint8_t> got;
        auto                 wr = [&](const uint8_t* p, size_t n) {
            got.insert(got.end(), p, p + n);
            return true;
        };
        if (proto == 'z') {
            net::ZmodemReceiver r(*line, [](const std::string&, uint32_t) { return true; }, wr, [](bool) {});
            ok = runTransfer(r);
        } else {
            net::XmodemReceiver r(*line, wr);
            ok = runTransfer(r);
        }
        std::ofstream(out, std::ios::binary).write((const char*)got.data(), (std::streamsize)got.size());
    } else {
        std::string          local = argv[5], desc = argv[6];
        std::ifstream        in(local, std::ios::binary);
        std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)), {});
        std::string          base = local.substr(local.find_last_of("/\\") + 1);
        send(*line, "u" + desc + "\r" + std::string(1, proto));
        if (proto != 'z') send(*line, base + "\r");
        if (!waitFor(*line, "upload in your terminal")) return 1;
        auto rd = [&](uint32_t off, uint8_t* buf, size_t n) {
            if (off + n > data.size()) return false;
            std::copy(data.begin() + off, data.begin() + off + n, buf);
            return true;
        };
        if (proto == 'z') {
            net::ZmodemSender s(*line, rd, (uint32_t)data.size(), base);
            ok = runTransfer(s);
        } else {
            net::XmodemSender s(*line, rd, (uint32_t)data.size(), proto == 'k');
            ok = runTransfer(s);
        }
    }
    bool shown = waitFor(*line, ok ? "C O M P L E T E" : "F A I L E D", 15000);
    std::printf("xfertest: the board showed the %s window: %s\n", ok ? "complete" : "failed", shown ? "yes" : "no");
    send(*line, "q\rg");
    return ok && shown ? 0 : 1;
}
