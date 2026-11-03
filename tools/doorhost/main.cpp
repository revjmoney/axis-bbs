// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// doorhost: run a DOS door in the AXIS door runtime on a PC.
//
//   doorhost --dir F:\doors\lord --exe LORD.EXE --args "0 NOEMS /DREW" --local
//       play it at the keyboard (the door's text screen is drawn in this console)
//   doorhost --dir ... --exe ... --args ... --run 5000 --keys "\r" --trace dos.log
//   doorhost --dir ... --exe ... --args ... --script steps.txt --shots DIR
//       each line "regex<TAB>keys<TAB>shot": wait for the regex on screen, save DIR/shot.b800, type the keys
//   doorhost --dir ... --exe LORD.EXE --args "1 /DREW" --telnet 2323 [--alias NAME] [--drop doorsys|dorinfo]
//       wait for one telnet caller and run the door for them (FOSSIL + drop file)
//       headless: type the keys (\r, \e, \xNN escapes; '|' waits 1 s), run, print the screen and stats
#include <chrono>
#include <fstream>
#include <memory>
#include <regex>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>

#include "door/session.h"
#include "platform/pc/consolestream.h"
#include "platform/pc/stdhostfs.h"
#include "platform/pc/tcpstream.h"

using namespace axis::door;

static std::string unescape(const std::string& s) {
    std::string o;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] != '\\' || i + 1 >= s.size()) {
            o += s[i];
            continue;
        }
        char c = s[++i];
        if (c == 'r') o += '\r';
        else if (c == 'n') o += '\n';
        else if (c == 'e') o += '\x1b';
        else if (c == 'x' && i + 2 < s.size()) {
            o += (char)std::stoi(s.substr(i + 1, 2), nullptr, 16);
            i += 2;
        } else o += c;
    }
    return o;
}

int main(int argc, char** argv) {
    DoorConfig  cfg;
    bool        local = false;
    int         runMs = 0;
    std::string keys, tracePath, scriptPath, shotsDir;
    int         telnetPort = -1;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto        next = [&]() { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
        if (a == "--dir") cfg.dir = next();
        else if (a == "--exe") cfg.exe = next();
        else if (a == "--args") cfg.args = next();
        else if (a == "--local") local = true;
        else if (a == "--run") runMs = std::stoi(next());
        else if (a == "--keys") keys = unescape(next());
        else if (a == "--trace") tracePath = next();
        else if (a == "--script") scriptPath = next();
        else if (a == "--shots") shotsDir = next();
        else if (a == "--budget") cfg.ramBudget = (size_t)std::stoi(next()) * 1024;
        else if (a == "--telnet") telnetPort = std::stoi(next());
        else if (a == "--alias") cfg.caller.alias = cfg.caller.realName = next();
        else if (a == "--drop") {
            std::string d = next();
            cfg.dropFile = d == "dorinfo" ? DropFile::Dorinfo : DropFile::DoorSys;
        } else {
            std::fprintf(stderr, "unknown option %s\n", a.c_str());
            return 2;
        }
    }
    if (cfg.dir.empty() || cfg.exe.empty()) {
        std::fprintf(stderr, "usage: doorhost --dir DIR --exe PROG.EXE [--args ARGS] (--local | --run MS [--keys K]) [--trace F] [--budget KB]\n");
        return 2;
    }
    if (!tracePath.empty()) cfg.trace = std::fopen(tracePath.c_str(), "w");
    axis::pc::StdHostFs fs(cfg.dir);
    LockTable           locks;
    cfg.localMode = true;

    if (telnetPort >= 0) {  // wait for one caller, run the door for them over FOSSIL, then exit
        axis::pc::TcpListener listener((uint16_t)telnetPort);
        std::printf("doorhost: waiting for a telnet caller on port %u\n", listener.port());
        std::fflush(stdout);
        std::unique_ptr<axis::pc::TcpStream> line;
        while (!(line = listener.accept(1000))) {
        }
        std::printf("doorhost: caller connected, starting %s\n", cfg.exe.c_str());
        cfg.localMode = false;
        if (cfg.dropFile == DropFile::None) cfg.dropFile = DropFile::DoorSys;
        DoorSession s(cfg, fs, locks, *line, 1);
        s.run();
        std::printf("doorhost: door finished: result %d, exit code %d, peak RAM %d KB\n", (int)s.result(), s.exitCode(),
                    s.memStats().peakRamPages * 4);
        return 0;
    }

    if (local) {
        axis::pc::ConsoleStream con;
        DoorSession             s(cfg, fs, locks, con, 1);
        while (s.step(30)) con.draw(s.screen());
        con.draw(s.screen());
        std::printf("\n[door finished: result %d, exit code %d]\n", (int)s.result(), s.exitCode());
        return 0;
    }

    if (!scriptPath.empty()) {
        ScriptStream keysIn;
        DoorSession  s(cfg, fs, locks, keysIn, 1);
        std::ifstream in(scriptPath);
        std::string   line;
        auto text = [&]() {
            std::string t;
            for (auto& l : s.screen()) t += l + "\n";
            return t;
        };
        while (std::getline(in, line)) {
            if (line.empty() || line[0] == '#') continue;
            size_t t1 = line.find('\t'), t2 = t1 == std::string::npos ? t1 : line.find('\t', t1 + 1);
            std::string pat = line.substr(0, t1);
            std::string k = t1 == std::string::npos ? "" : unescape(line.substr(t1 + 1, t2 == std::string::npos ? std::string::npos : t2 - t1 - 1));
            std::string shot = t2 == std::string::npos ? "" : line.substr(t2 + 1);
            std::regex re(pat, std::regex::icase);
            bool found = false;
            for (int ms = 0; ms < 15000 && !found; ms += 50) {
                found = std::regex_search(text(), re);
                if (!found && !s.step(50)) break;
            }
            for (int ms = 0; ms < 300; ms += 50) s.step(50);  // let the screen settle
            std::printf("%s %s\n", found ? "ok  " : "MISS", pat.c_str());
            if (!shot.empty() && !shotsDir.empty()) {
                auto page = s.textPage();
                std::ofstream((shotsDir + "/" + shot + ".b800").c_str(), std::ios::binary).write((const char*)page.data(), page.size());
            }
            keysIn.push(k);
        }
        while (s.step(100)) {
        }
        std::printf("[result %d, exit %d, peak RAM %d KB]\n", (int)s.result(), s.exitCode(), s.memStats().peakRamPages * 4);
        return 0;
    }

    ScriptStream keysIn;
    DoorSession  s(cfg, fs, locks, keysIn, 1);
    auto         t0 = std::chrono::steady_clock::now();
    size_t       k = 0;
    while (s.step(50)) {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
        static long long nextKeyAt = 1000;
        if (k < keys.size() && elapsed >= nextKeyAt) {
            if (keys[k] == '|') nextKeyAt = elapsed + 1000;
            else {
                keysIn.push(std::string(1, keys[k]));
                nextKeyAt = elapsed + 300;
            }
            k++;
        }
        if (elapsed >= runMs) break;
    }
    for (auto& line : s.screen()) {
        std::string l = line;
        for (char& c : l)
            if ((unsigned char)c < 32 || (unsigned char)c > 126) c = (unsigned char)c >= 0xB0 && (unsigned char)c <= 0xDF ? '#' : ' ';
        while (!l.empty() && l.back() == ' ') l.pop_back();
        std::printf("|%s\n", l.c_str());
    }
    auto st = s.memStats();
    std::printf("[result %d, exit %d, %u instructions, RAM pages %d (peak %d), read-only pages %d]\n", (int)s.result(),
                s.exitCode(), s.instructions(), st.ramPages, st.peakRamPages, st.roPages);
    std::printf("[%s]\n", s.debugState().c_str());
    if (cfg.trace) std::fclose(cfg.trace);
    return 0;
}
