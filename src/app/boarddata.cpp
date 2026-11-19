// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include "app/boarddata.h"

#include <sys/stat.h>

#include <cstdio>

#include "bbs/axisnet.h"
#include "bbs/listtrim.h"

// Size over speed, as main.cpp builds the code that was moved here.
#pragma GCC optimize("Os")

namespace axis::app {

namespace {
std::string inFlash(Host& host, const std::string& path) { return host.flashRoot() + path; }

// One line of f without its '\n' (a '\r' stays, as the board's readStringUntil kept it); false at the end of the file.
bool readLine(FILE* f, std::string& l) {
    l.clear();
    bool any = false;
    for (int c; (c = std::getc(f)) != EOF;) {
        any = true;
        if (c == '\n') return true;
        l += (char)c;
    }
    return any;
}

// The text's lines, the empty ones left out.
void splitLines(const std::string& text, std::vector<std::string>& v) {
    v.clear();
    for (size_t pos = 0; pos < text.size();) {
        size_t e = text.find('\n', pos);
        if (e == std::string::npos) e = text.size();
        if (e > pos) v.push_back(text.substr(pos, e - pos));
        pos = e + 1;
    }
}
}  // namespace

std::string flashRead(Host& host, const char* path) {
    FILE* f = std::fopen(inFlash(host, path).c_str(), "rb");
    if (!f) return "";
    std::string s;
    long        size = std::fseek(f, 0, SEEK_END) == 0 ? std::ftell(f) : -1;
    if (size > 0 && std::fseek(f, 0, SEEK_SET) == 0) {
        s.resize((size_t)size);
        s.resize(std::fread(&s[0], 1, s.size(), f));
    }
    std::fclose(f);
    return s;
}

bool flashWrite(Host& host, const char* path, const std::string& text) {
    const std::string tmp = inFlash(host, "/save.tmp");
    FILE*             f = std::fopen(tmp.c_str(), "wb");
    if (!f) return false;
    bool ok = std::fwrite(text.data(), 1, text.size(), f) == text.size();
    std::fclose(f);
    if (!ok) return false;
    const std::string to = inFlash(host, path);
    std::remove(to.c_str());
    return std::rename(tmp.c_str(), to.c_str()) == 0;
}

bool flashExists(Host& host, const char* path) {
    struct stat st;
    return ::stat(inFlash(host, path).c_str(), &st) == 0;
}

void BoardData::load() {
    splitLines(flashRead(host_, "/oneliners.txt"), oneLiners_);
    splitLines(flashRead(host_, "/callers.txt"), lastCallers_);
}

void BoardData::addOneLiner(const std::string& line) {
    oneLiners_.push_back(line.substr(0, 100));
    while (oneLiners_.size() > 20) oneLiners_.erase(oneLiners_.begin());
    std::string text;
    for (auto& l : oneLiners_) text += l + "\n";
    flashWrite(host_, "/oneliners.txt", text);
}

void BoardData::noteCaller(const std::string& line) {
    lastCallers_.push_back(line.substr(0, 120));
    while (lastCallers_.size() > 20) lastCallers_.erase(lastCallers_.begin());
    std::string text;
    for (auto& l : lastCallers_) text += l + "\n";
    flashWrite(host_, "/callers.txt", text);
}

// The board's small shared lists (rumors, the BBS list): 50 lines each in internal flash on the main.
bool BoardData::listName(const std::string& n) {
    return n == "rumors" || n == "bbslist" || n == "votes" || n == "ballots" || n == "axisnet" || n == "netnotes";
}
// How many lines a plain list keeps (0: axisnet and netnotes, which keep their own merge and size rules).
size_t BoardData::listCap(const std::string& n) {
    return n == "ballots" ? 500u : n == "rumors" ? 300u : n == "bbslist" || n == "votes" ? 50u : 0u;
}
static int listIdx(const std::string& n) { return n == "rumors" ? 0 : n == "bbslist" ? 1 : n == "votes" ? 2 : n == "ballots" ? 3 : -1; }
std::string BoardData::listPath(const std::string& n) { return "/list_" + n + ".txt"; }
void        BoardData::forgetCounts() {
    for (int32_t& c : listCount_) c = -1;
}
size_t BoardData::listCountOf(const std::string& n) {
    int i = listIdx(n);
    if (i < 0) return 0;
    if (listCount_[i] >= 0) return (size_t)listCount_[i];
    size_t      c = 0;
    std::string l;
    if (FILE* f = std::fopen(inFlash(host_, listPath(n)).c_str(), "r")) {
        while (readLine(f, l))
            if (l.length()) c++;
        std::fclose(f);
    }
    listCount_[i] = (int32_t)c;
    return c;
}
// A plain list is trimmed lazily (listAppendLocal), so the file can hold up to cap + cap/4 lines: readers see the newest cap.
size_t BoardData::listSkip(const std::string& n) {
    size_t cap = listCap(n);
    if (!cap) return 0;
    return axis::bbs::listReadSkip(listCountOf(n), cap);
}
// One random line of a list, read a line at a time (the main menu's rumor: the whole list is more than the main can spare then)
std::vector<std::string> BoardData::listOne(const std::string& n) {
    std::vector<std::string> v;
    if (!listName(n)) return v;
    size_t      skip = listSkip(n);
    uint32_t    seen = 0;
    std::string l;
    if (FILE* f = std::fopen(inFlash(host_, listPath(n)).c_str(), "r")) {
        while (readLine(f, l)) {
            if (!l.length()) continue;
            if (skip) {
                skip--;
                continue;
            }
            if (host_.random32() % ++seen == 0) v.assign(1, l);
        }
        std::fclose(f);
    }
    return v;
}
std::vector<std::string> BoardData::listLoad(const std::string& n) {
    std::vector<std::string> v;
    if (n == "rumor") return listOne("rumors");  // the main menu's one rumor
    if (!listName(n)) return v;
    size_t      skip = listSkip(n);
    std::string l;
    if (FILE* f = std::fopen(inFlash(host_, listPath(n)).c_str(), "r")) {  // a line at a time: one 8 KB block is more than the main can spare
        while (readLine(f, l)) {
            if (!l.length()) continue;
            if (skip) {
                skip--;
                continue;
            }
            v.push_back(l);
        }
        std::fclose(f);
    }
    return v;
}
void BoardData::listSave(const std::string& n, const std::vector<std::string>& v) {
    if (!listName(n)) return;
    if (listIdx(n) >= 0) listCount_[listIdx(n)] = -1;  // the file is rewritten: count it again when next needed
    size_t      cap = n == "axisnet" || n == "netnotes" ? 180u : 120u;
    std::string text;
    for (auto& l : v) {
        text += l.substr(0, cap);
        text += '\n';
    }
    flashWrite(host_, listPath(n).c_str(), text);
}
// A plain list gets its line appended to the file; once it is cap/4 over, the newest cap lines are streamed to a
// temp file that replaces it (a failure leaves the original). No copy of the list is held in RAM.
bool BoardData::listAppendLocal(const std::string& n, const std::string& line, size_t cap) {
    if (!listName(n) || !cap) return false;
    std::string out = line.substr(0, 120) + '\n';
    size_t      add = 0;  // non-empty segments written (a follower's text may hold newlines)
    for (size_t p = 0; p < out.size();) {
        size_t e = out.find('\n', p);
        if (e > p) add++;
        p = e + 1;
    }
    if (!add) return false;  // an empty line is never shown: write nothing
    size_t            had = listCountOf(n);  // before the append, so a first count does not include the new line
    const std::string path = inFlash(host_, listPath(n));
    FILE*             f = std::fopen(path.c_str(), "a");
    if (!f) return false;
    bool ok = std::fwrite(out.data(), 1, out.size(), f) == out.size();
    std::fclose(f);
    if (!ok) {
        listCount_[listIdx(n)] = -1;  // maybe half a line landed: recount
        return false;
    }
    size_t now = had + add;
    listCount_[listIdx(n)] = (int32_t)now;
    const size_t skipOld = axis::bbs::listTrimSkip(now, cap);  // (the newest cap stay once it is cap/4 over)
    if (!skipOld) return true;
    const std::string tmp = inFlash(host_, "/list_" + n + ".tmp");
    FILE*             in = std::fopen(path.c_str(), "r");
    FILE*             t = std::fopen(tmp.c_str(), "w");
    if (!in || !t) {
        if (in) std::fclose(in);
        if (t) {
            std::fclose(t);
            std::remove(tmp.c_str());
        }
        return true;  // the line is in; the trim waits for the next one
    }
    size_t      skip = skipOld, kept = 0;
    bool        wrote = true;
    std::string l;
    while (wrote && readLine(in, l)) {
        if (!l.length()) continue;
        if (skip) {
            skip--;
            continue;
        }
        std::string w = l.length() > 120 ? l.substr(0, 120) : l;
        w += '\n';
        wrote = std::fwrite(w.data(), 1, w.size(), t) == w.size();
        kept++;
    }
    std::fclose(in);
    std::fclose(t);
    if (!wrote || (std::rename(tmp.c_str(), path.c_str()) != 0 &&
                   !(std::remove(path.c_str()) == 0 && std::rename(tmp.c_str(), path.c_str()) == 0))) {
        std::remove(tmp.c_str());
        return true;
    }
    listCount_[listIdx(n)] = (int32_t)kept;
    return true;
}
void BoardData::listAdd(const std::string& n, const std::string& line) {
    if (!listName(n)) return;
    if (n == "axisnet") {
        axis::bbs::AxisCard c;
        if (!axis::bbs::axisParse(line, c) || c.host.empty()) return;
        std::vector<std::string> v = listLoad(n);
        axis::bbs::axisMerge(v, c, 24);
        listSave(n, v);
        return;
    }
    if (n == "netnotes") {
        auto tail = [](const std::string& s) {
            size_t p = s.find('|');
            return p == std::string::npos ? s : s.substr(p + 1);
        };
        std::vector<std::string> v = listLoad(n);
        for (auto& e : v)
            if (tail(e) == tail(line)) return;
        v.push_back(line.substr(0, 180));
        while (v.size() > 40) v.erase(v.begin());
        listSave(n, v);
        return;
    }
    listAppendLocal(n, line, listCap(n));
}

// A follower's answer from the main: '1' then one line each (none when it did not answer).
std::vector<std::string> BoardData::remoteLines(const std::string& req) {
    std::vector<std::string> v;
    std::string              reply;
    if (remote && remote(req, reply) && !reply.empty() && reply[0] == '1')
        for (size_t pos = 1; pos < reply.size();) {
            size_t e = reply.find('\n', pos);
            if (e == std::string::npos) e = reply.size();
            if (e > pos) v.push_back(reply.substr(pos, e - pos));
            pos = e + 1;
        }
    return v;
}
void BoardData::remoteSend(const std::string& req) {
    std::string reply;
    if (remote) remote(req, reply);
}

std::vector<std::string> BoardData::sharedOneLiners() {
    if (!isFollower()) return oneLiners_;
    return remoteLines("O");
}
void BoardData::sharedAddOneLiner(const std::string& line) {
    if (!isFollower()) addOneLiner(line);
    else remoteSend(std::string("P\x1f") + line);
}
std::vector<std::string> BoardData::sharedLastCallers() {
    if (!isFollower()) return lastCallers_;
    return remoteLines("K");
}
void BoardData::sharedNoteCaller(const std::string& line) {
    if (!isFollower()) noteCaller(line);
    else remoteSend(std::string("Q\x1f") + line);
}
std::vector<std::string> BoardData::sharedListLoad(const std::string& n) {
    if (!isFollower()) return listLoad(n);
    return remoteLines(std::string("Y\x1f") + n);
}
// The shared lists for the local console (callers use the same over their own node setup).
std::vector<std::string> BoardData::sharedList(const std::string& n) {
    return n == "callers" ? sharedLastCallers() : sharedListLoad(n);
}
void BoardData::sharedListAdd(const std::string& n, const std::string& line) {
    if (!isFollower()) listAdd(n, line);
    else remoteSend(std::string("Z\x1f") + n + "\x1f" + line);
}

}  // namespace axis::app
