// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The host's own @ commands, asked by main.cpp's runCommand after every board command (its AXIS_NATIVE line). Both
// are in boardOnlyCommand, so they run where they are typed. They run inside loop(), so they only report or set
// things up: the mirror's pass and the hand-back's requests and restart run from clusterHostPoll.
//   @CARDMIRROR          the card mirror: profile, standing, first copy, manifest, last check, live follow, last error
//   @CARDMIRROR CHECK    a compare pass at the next chance (Follower)
//   @CARDMIRROR CHECK FORCE  the same, and its removals are made however many (once: the mass-removal guard)
//   @HANDBACK            what a hand-back would copy, to which card board, callers online, how one is going
//   @HANDBACK GO [node] [NOW]  starts it (MirrorMain, with a linked card board; NOW: even with callers on)
//   @HANDBACK STOP       stops it after the file in hand
#include <Arduino.h>

#include <time.h>

#include <cctype>
#include <ctime>
#include <string>
#include <vector>

#include "platform/linux/clusterhost.h"
#include "platform/linux/paths.h"

namespace {

// "1 file", "2 files"
std::string many(uint64_t n, const char* one) { return std::to_string(n) + " " + one + (n == 1 ? "" : "s"); }

std::string upper(std::string s) {
    for (char& c : s) c = (char)std::toupper((unsigned char)c);
    return s;
}

// the command's words: "@HANDBACK GO 2" is {"@HANDBACK", "GO", "2"}
std::vector<std::string> words(const std::string& s) {
    std::vector<std::string> w;
    size_t                   pos = 0;
    while (pos < s.size()) {
        while (pos < s.size() && s[pos] == ' ') pos++;
        size_t end = s.find(' ', pos);
        if (end == std::string::npos) end = s.size();
        if (end > pos) w.push_back(s.substr(pos, end - pos));
        pos = end;
    }
    return w;
}

std::string when(long t) {
    if (!t) return "";
    time_t    tt = (time_t)t;
    struct tm lt{};
    localtime_r(&tt, &lt);
    char buf[40];
    std::strftime(buf, sizeof buf, "%Y-%m-%d %H:%M", &lt);
    return buf;
}

const char* standing() {
    ClusterState st;
    if (!clusterStateLoad(st)) return "none saved";
    return st.state == Standing::Main ? "main" : st.state == Standing::HandedBack ? "handed back" : "follower";
}

void cardMirror(const std::vector<std::string>& w, std::string& out) {
    auto line = [&](const std::string& s) { out += "axis: card mirror: " + s + "\n"; };
    bool force = w.size() == 3 && upper(w[2]) == "FORCE";
    if (w.size() >= 2 && upper(w[1]) == "CHECK" && (w.size() == 2 || force)) {
        if (axisProfile() != Profile::Follower) {
            line("refused: only a follower checks its mirror against the main's card");
            return;
        }
        mirrorCheckSoon(force);
        line(force ? "a check of the whole card starts at the next chance, and removes from the mirror whatever is "
                     "gone from the card, however many files"
                   : "a check of the whole card starts at the next chance");
        return;
    }
    if (w.size() >= 2) {
        line("unknown: @CARDMIRROR, @CARDMIRROR CHECK or @CARDMIRROR CHECK FORCE");
        return;
    }
    MirrorStatus s = mirrorStatus();
    line(std::string("profile ") + axisProfileName(axisProfile()) + ", standing " + standing());
    line(std::string("first copy ") + (s.complete ? "complete" : "not complete"));
    line(many(s.files, "file") + ", " + many(s.bytes, "byte") + " in the manifest");
    if (!s.pass.empty()) line("running now: " + s.pass);
    if (s.lastCheckAt) line("last check " + when(s.lastCheckAt) + ": " + s.lastCheck);
    else line("last check: none in this run");
    line("last noon check: " + (s.noon.empty() ? std::string("none yet") : s.noon));
    line("live follow: " + many(s.pending, "path") + " waiting, " + std::to_string(s.verifying) +
         " being checked until they settle");
    if (s.stopped) line("copying waits: the disk was full");
    if (s.held) line("checks wait: the last one would have removed most of the mirror (@CARDMIRROR CHECK FORCE)");
    line(s.freshAt ? "last in step with the card board " + when(s.freshAt) + " (" + mirrorAge() + ")"
                   : std::string("never in step with the card board yet"));
    line("last error: " + (s.lastErr.empty() ? std::string("none in this run") : when(s.lastErrAt) + " " + s.lastErr));
    if (axisProfile() == Profile::MirrorMain)
        line(many(handbackChangeCount(), "path") + " changed here since the takeover (@HANDBACK)");
}

void handBack(const std::vector<std::string>& w, std::string& out) {
    std::string sub = w.size() >= 2 ? upper(w[1]) : "";
    bool        now = w.size() >= 3 && upper(w.back()) == "NOW";
    size_t      args = w.size() - (now ? 1 : 0);  // (the words before NOW)
    if (sub.empty()) handbackShow(out);
    else if (sub == "GO" && args <= 3) handbackGo(args == 3 ? w[2] : "", now, out);
    else if (sub == "STOP" && w.size() == 2) handbackStop(out);
    else out += "axis: handback: unknown: @HANDBACK, @HANDBACK GO [node] [NOW] or @HANDBACK STOP\n";
}

}  // namespace

bool nativeCommand(const String& cmd, Print& out) {
    std::vector<std::string> w = words(cmd.c_str());
    if (w.empty()) return false;
    std::string head = upper(w[0]), text;
    if (head == "@CARDMIRROR") cardMirror(w, text);
    else if (head == "@HANDBACK") handBack(w, text);
    else return false;
    out.write((const uint8_t*)text.data(), text.size());
    return true;
}
