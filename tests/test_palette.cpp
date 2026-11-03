// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// Rev's scheme: black, grey, orange and white everywhere. Every screen a caller can reach is checked
// for colour codes outside it (pieces in the ANSI Pack Viewer are other people's art, so not those).
#include <map>
#include <regex>
#include <string>
#include <vector>

#include "doctest.h"
#include "bbs/login_art.h"
#include "bbs/node.h"
#include "bbs/setup_gate.h"
#include "bbs/setup_wizard.h"
#include "bbs/theme.h"

using namespace axis::bbs;

namespace {
// SGR numbers allowed: reset, bold/normal, black/brown(orange)/white foregrounds, black/brown/white backgrounds
bool allowed(int n) {
    for (int a : {0, 1, 5, 22, 25, 30, 33, 37, 39, 40, 43, 47, 49})
        if (n == a) return true;
    return false;
}

std::vector<std::string> offColour(const std::string& s) {
    std::vector<std::string> bad;
    std::regex               sgr("\x1b\\[([0-9;]*)m");
    for (auto it = std::sregex_iterator(s.begin(), s.end(), sgr); it != std::sregex_iterator(); ++it) {
        std::string codes = (*it)[1];
        size_t      pos = 0;
        while (pos <= codes.size()) {
            size_t      semi = codes.find(';', pos);
            std::string part = codes.substr(pos, semi == std::string::npos ? std::string::npos : semi - pos);
            if (!part.empty() && !allowed(std::stoi(part))) bad.push_back("ESC[" + codes + "m");
            if (semi == std::string::npos) break;
            pos = semi + 1;
        }
    }
    return bad;
}

struct Lib : FileLibrary {
    std::vector<FileEntry> list(const std::string&) override { return {{"README.TXT", 900, "Read me"}, {"PACK.ZIP", 90000, "A pack"}}; }
    bool read(const std::string&, const std::string&, std::string& out, size_t) override {
        out = "hello\nworld\n";
        return true;
    }
};

SysInfo info() {
    SysInfo s;
    s.host = "axis.local";
    s.ip = "192.168.1.164";
    s.nodes = {""};
    return s;
}

void type(Node& n, const std::string& s) { n.input((const uint8_t*)s.data(), s.size(), 1000); }
}  // namespace

TEST_CASE("the login logo uses only the theme colours") { CHECK(offColour(kLoginArt).empty()); }

TEST_CASE("every BBS screen a caller can reach uses only the theme colours") {
    Lib         lib;
    BbsConfig   cfg;
    cfg.areas = {{"axis", "Axis Micro BBS Firmware"}};
    Node        n(1, info, cfg, &lib);
    std::string all;
    n.begin(0);
    all += n.takeOutput();
    for (const char* keys : {"Rev\r", "i", "c", "d", "\r", "f", "1\r", "n", "p", "vREADME.TXT\r", "\r", "dPACK.ZIP\r", "z"}) {
        type(n, keys);
        all += n.takeOutput();
    }
    n.transferDone(true, 90000, 3000, "", 2000);
    all += n.takeOutput();
    type(n, "q\rg");
    all += n.takeOutput() + busyScreen();
    auto bad = offColour(all);
    INFO("off-palette codes: " << (bad.empty() ? std::string("none") : bad[0]) << " (" << bad.size() << " in all)");
    CHECK(bad.empty());
}

TEST_CASE("the first-boot setup screens use only the theme colours") {
    SetupGate g("123456");
    g.begin();
    std::string all = g.takeOutput();
    g.input((const uint8_t*)"123456\r", 7);
    all += g.takeOutput();
    BbsConfig   cfg;
    SetupWizard w(cfg, [] { return 4u; });
    w.begin();
    all += w.takeOutput();
    for (const char* line : {"Rev\r", "pw12345678\r", "pw12345678\r", "\r", "\r", "\r", "\r"}) {
        w.input((const uint8_t*)line, std::string(line).size());
        all += w.takeOutput();
    }
    auto bad = offColour(all);
    INFO("off-palette codes: " << (bad.empty() ? std::string("none") : bad[0]));
    CHECK(bad.empty());
}
