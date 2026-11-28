// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// axis_bridge: AXIS message areas to and from NecroTechs RadNet board bundles (radnet-bridge v1).
//   axis_bridge export --msgs <dir> --station <board id> [--config <axis.ini>] <out.jsonl> <area>[=<title>]...
//   axis_bridge import --msgs <dir> [--area <area>] <in.jsonl>
// <dir> is a message-base folder: a copy of the card's /msgs, or AXIS Native's <data>/sd/msgs. Import while the BBS
// is stopped (or into a card copy you put back), since the BBS keeps its own place in each area file.
// Export never overwrites a file. Import appends to <area>.msg and records each post in <area>.bridge.
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "bbs/msgbase.h"
#include "bridgebundle.h"

namespace fs = std::filesystem;
using namespace axis::bbs;

namespace {
int usage() {
    std::fprintf(stderr,
                 "usage: axis_bridge export --msgs <dir> --station <board id> [--config <axis.ini>] <out.jsonl> <area>[=<title>]...\n"
                 "       axis_bridge import --msgs <dir> [--area <area>] <in.jsonl>\n"
                 "export: the areas named (titles from --config msgareaN=id|name lines when given) to a new bundle file.\n"
                 "import: the bundle's posts into one area (default radnet); add that area to the BBS's message areas.\n");
    return 2;
}

bool readFile(const fs::path& p, std::string& out, size_t cap) {
    std::error_code ec;
    auto            size = fs::file_size(p, ec);
    if (ec || size > cap) return false;
    std::ifstream in(p, std::ios::binary);
    if (!in) return false;
    std::ostringstream s;
    s << in.rdbuf();
    out = s.str();
    return true;
}

bool areaIdOk(const std::string& a) {
    if (a.empty() || a.size() > 32) return false;
    for (char c : a)
        if (!(std::isalnum((unsigned char)c) || c == '_' || c == '-')) return false;
    return true;
}

std::string nowStamp() {
    std::time_t now = std::time(nullptr);
    char        b[32];
    std::strftime(b, sizeof b, "%Y-%m-%d %H:%M", std::localtime(&now));
    return b;
}
}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) return usage();
    std::string              mode = argv[1], msgs, station, config, area = "radnet";
    std::vector<std::string> rest;
    for (int i = 2; i < argc; i++) {
        std::string a = argv[i];
        auto        val = [&](std::string& v) {
            if (i + 1 >= argc) return false;
            v = argv[++i];
            return true;
        };
        if (a == "--msgs") {
            if (!val(msgs)) return usage();
        } else if (a == "--station") {
            if (!val(station)) return usage();
        } else if (a == "--config") {
            if (!val(config)) return usage();
        } else if (a == "--area") {
            if (!val(area)) return usage();
        } else if (a.rfind("--", 0) == 0) {
            return usage();
        } else {
            rest.push_back(a);
        }
    }
    if (msgs.empty() || !fs::is_directory(msgs)) {
        std::fprintf(stderr, "axis_bridge: --msgs must be an existing message-base folder\n");
        return 2;
    }
    FileMessageStore store(msgs);

    if (mode == "export") {
        if (station.empty() || rest.size() < 2) return usage();
        std::map<std::string, std::string> titles;
        if (!config.empty()) {
            std::string text;
            if (!readFile(config, text, 1 << 20)) {
                std::fprintf(stderr, "axis_bridge: cannot read %s\n", config.c_str());
                return 1;
            }
            std::istringstream lines(text);
            for (std::string line; std::getline(lines, line);) {  // msgareaN=id|name[|level]
                if (line.rfind("msgarea", 0) != 0) continue;
                size_t eq = line.find('='), bar = line.find('|', eq);
                if (eq == std::string::npos || bar == std::string::npos) continue;
                size_t end = line.find('|', bar + 1);
                std::string name = line.substr(bar + 1, end == std::string::npos ? std::string::npos : end - bar - 1);
                if (!name.empty() && name.back() == '\r') name.pop_back();
                titles[line.substr(eq + 1, bar - eq - 1)] = name;
            }
        }
        std::vector<bridge::ExportArea> areas;
        for (size_t i = 1; i < rest.size(); i++) {
            std::string id = rest[i], title;
            if (size_t eq = id.find('='); eq != std::string::npos) {
                title = id.substr(eq + 1);
                id = id.substr(0, eq);
            }
            if (!areaIdOk(id)) {
                std::fprintf(stderr, "axis_bridge: bad area id %s\n", id.c_str());
                return 2;
            }
            if (title.empty() && titles.count(id)) title = titles[id];
            areas.push_back({id, title});
        }
        fs::path out = rest[0];
        if (fs::exists(out)) {
            std::fprintf(stderr, "axis_bridge: %s exists; export never overwrites\n", out.string().c_str());
            return 1;
        }
        std::string text;
        auto        r = bridge::exportAreas(
            store, station, areas,
            [&](const std::string& a) {
                std::vector<int> nums;
                std::string      ledger;
                if (readFile(fs::path(msgs) / (a + ".bridge"), ledger, 4 << 20))
                    for (auto& [origin, num] : bridge::parseLedger(ledger)) nums.push_back(num);
                return nums;
            },
            [&](const std::string& line) { text += line + "\n"; });
        if (!r.error.empty()) {
            std::fprintf(stderr, "axis_bridge: %s\n", r.error.c_str());
            return 2;
        }
        std::FILE* f = std::fopen(out.string().c_str(), "wb");  // checked absent just above
        if (!f || std::fwrite(text.data(), 1, text.size(), f) != text.size() || std::fclose(f) != 0) {
            std::fprintf(stderr, "axis_bridge: cannot write %s\n", out.string().c_str());
            return 1;
        }
        std::printf("exported %d boards, %d posts (%d imported posts not sent back)\n", r.boards, r.posts, r.skipped);
        return 0;
    }

    if (mode == "import") {
        if (rest.size() != 1 || !areaIdOk(area)) return usage();
        std::string bundle, ledger;
        if (!readFile(rest[0], bundle, bridge::kMaxBytes)) {
            std::fprintf(stderr, "axis_bridge: cannot read %s (or over 8 MB)\n", rest[0].c_str());
            return 1;
        }
        fs::path ledgerPath = fs::path(msgs) / (area + ".bridge");
        readFile(ledgerPath, ledger, 4 << 20);
        std::FILE* led = nullptr;
        auto       r = bridge::importBundle(bundle, store, area, bridge::parseLedger(ledger), nowStamp(),
                                            [&](int num, const std::string& origin) {
                                          if (!led) led = std::fopen(ledgerPath.string().c_str(), "ab");
                                          if (led) {
                                              std::fprintf(led, "%d %s\n", num, origin.c_str());
                                              std::fflush(led);
                                          }
                                      });
        if (led) std::fclose(led);
        if (!r.fatal.empty()) {
            std::fprintf(stderr, "axis_bridge: %s\n", r.fatal.c_str());
            return 1;
        }
        std::printf("imported %d posts into %s (%d duplicates, %d AXIS posts skipped, %d rejected)\n", r.posts,
                    area.c_str(), r.duplicates, r.fromAxis, r.rejected);
        for (const std::string& e : r.errors) std::printf("  %s\n", e.c_str());
        return r.rejected ? 3 : 0;
    }
    return usage();
}
