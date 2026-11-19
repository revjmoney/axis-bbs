// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The card mirror's driver (clusterhost.h): as a Follower, keeps <data>/mirror a copy of the main's card through the
// card tree service (cluster/cardtree.h). The first full copy, the live follow of the change feed, the compare pass
// (noon, a gap in the feed, asked for) and the manifest in <data>/mirror.manifest. mirrorPoll runs from
// clusterHostPoll (never inside a request) and makes at most one request a turn, paced (cardmirror.h's Pace).
#include <errno.h>
#include <fcntl.h>
#include <ftw.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <time.h>
#include <unistd.h>

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <initializer_list>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"  // cluster.h's Link probe_{...}: the board's header
#include "cluster/cluster.h"
#pragma GCC diagnostic pop
#include "cluster/cardtree.h"
#include "platform/linux/cardmirror.h"
#include "platform/linux/clusterhost.h"
#include "platform/linux/hostconf.h"
#include "platform/linux/paths.h"

namespace {

using axis::cluster::Cluster;
using axis::cluster::Role;

constexpr char     kSep = '\x1f';
constexpr uint32_t kReqTimeoutMs = 4000;
constexpr uint32_t kFeedMs = 10000;         // 'I C' this often
constexpr uint32_t kQuietMs = 15000;        // a changed path is copied once it has been quiet this long
constexpr uint32_t kSaveMs = 5000;          // the manifest is written at most this often (and at the end of a pass)
constexpr uint32_t kFailBackoffMs = 10000;  // a request that failed: no request for this long
constexpr uint32_t kNoTreeBackoffMs = 60000;   // the main does not serve the card tree: asked again after this
constexpr uint32_t kFirstAgainMs = 60000;      // a first copy that left files behind runs again after this
constexpr uint32_t kDiskFullMs = 600000;       // after "disk full", copying resumes (with a compare pass) after this
constexpr uint32_t kLogMs = 600000;            // repeated lines at most this often
constexpr uint64_t kKeepFree = 64ull << 20;    // a copy must leave this much room on the mirror's disk
constexpr int      kTries = 3;                 // a file that changes while copied is tried this often a pass
constexpr uint32_t kMinCharge = 256;           // a request counts at least this many bytes
// The board notes a path when it is opened for writing, not when it is closed, so a live copy can catch a file still
// being written (a slow upload). Each live copy is checked again with 'I S' every kVerifyMs until two checks in a row
// agree with the manifest; a difference copies it again. At most kVerifyMax paths; more asks for a compare pass.
constexpr uint32_t kVerifyMs = 15000;
constexpr size_t   kVerifyMax = 64;
// A compare pass that would remove more than kMassMax files, or more than a quarter of the manifest (and more than
// kMassFloor), is a blank or swapped card more likely than the sysop's deletions: it changes nothing until FORCE.
constexpr size_t   kMassMax = 200;
constexpr size_t   kMassFloor = 5;
constexpr long     kFreshSaveS = 300;  // the manifest's #fresh= is written again once it is this much behind

enum class Pass { None, First, Check };

struct Copy {
    bool        active = false;
    bool        live = false;  // from the change feed (else from the pass)
    std::string path;
    enum Step { Stat1, Read, Stat2 } step = Stat1;
    uint64_t    size = 0, off = 0;
    int64_t     mtime = 0;
    int         fd = -1;
};

struct WalkDir {
    std::string dir;
    uint64_t    skip = 0;
};

bool        inPoll = false;
bool        loaded = false;
bool        confRead = false;
std::string mirrorDir, manPath, takePath;
Manifest    man;
bool        dirty = false;
uint32_t    savedAt = 0;
int         noonMin = 12 * 60;
std::unique_ptr<Pace> pace;

// the link
bool     ready = false;
uint32_t readyEpoch = 0, readyIp = 0;
bool     waiting = false;  // a back-off is running until waitUntil
uint32_t waitUntil = 0;
bool     noTreeLogged = false;
uint32_t noTreeAt = 0;

// the change feed
bool                            feedKnown = false;  // an 'I C' was answered in this run
uint32_t                        feedAt = 0, feedGen = 0, feedSeq = 0;
std::map<std::string, uint32_t> pending;  // changed paths, and when they were last seen in the feed
std::deque<std::string>         liveQ;
struct Verify {
    uint32_t at = 0;     // when it was last copied or checked
    int      agree = 0;  // checks in a row that agreed with the manifest
};
std::map<std::string, Verify> verify;

// the pass
Pass                       pass = Pass::None;
bool                       checkWanted = false;
const char*                checkWhy = "";
bool                       noonPass = false;
bool                       walking = false;
std::vector<WalkDir>       walkStack;
std::vector<CardEntry>     listing;
std::deque<std::string>    passQ;
std::map<std::string, int> tries;           // this pass's tries per path
unsigned                   copied = 0, removed = 0, matched = 0, failed = 0;
uint32_t                   passAgainAt = 0;   // a pass that could not finish: the next one waits until then
bool                       passWaiting = false;
bool                       walkFailed = false;  // the main could not list a folder: nothing of the walk is applied

bool     stopped = false;  // disk full
uint32_t stoppedAt = 0;

// the mass-removal guard
bool     forceRemove = false;  // @CARDMIRROR CHECK FORCE: the next walk's removals are made whatever their number
bool     massStop = false;     // walkDone found too many removals: endPass reports it
bool     massHeld = false;     // since then no pass has finished (the mirror is not called in step with the card)
size_t   massN = 0, massOf = 0;

// in step with a card main (the takeover's freshness): this run, by the monotonic clock
bool     followedCard = false;
uint32_t freshMono = 0;
uint32_t maxAgeMs = 60 * 60000;

Copy cur;

// for @CARDMIRROR: the last pass's end and what it found, and the last thing that went wrong
time_t      lastCheckAt = 0;
std::string lastCheck;
time_t      lastErrAt = 0;
std::string lastErr;

// a line in the log ("axis: mirror: ...") that @CARDMIRROR also shows as the last error
void errLine(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void errLine(const char* fmt, ...) {
    char    buf[512];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    std::printf("axis: mirror: %s\n", buf);
    lastErr = buf;
    lastErrAt = time(nullptr);
}

uint32_t monoMs() {
    timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint32_t)((uint64_t)t.tv_sec * 1000 + (uint64_t)t.tv_nsec / 1000000);
}

std::string local(const std::string& rel) { return mirrorDir + "/" + rel; }

bool writeWhole(const std::string& path, const std::string& text) {
    std::string tmp = path + ".tmp";
    int         fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (fd < 0) return false;
    bool ok = true;
    for (size_t done = 0; ok && done < text.size();) {
        ssize_t w = ::write(fd, text.data() + done, text.size() - done);
        if (w < 0 && errno == EINTR) continue;
        if (w <= 0) ok = false;
        else done += (size_t)w;
    }
    if (ok && ::fsync(fd) != 0) ok = false;
    if (::close(fd) != 0) ok = false;
    if (!ok || ::rename(tmp.c_str(), path.c_str()) != 0) {
        ::unlink(tmp.c_str());
        return false;
    }
    return true;
}

bool readWhole(const std::string& path, std::string& out) {
    out.clear();
    int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) return false;
    char buf[8192];
    bool ok = true;
    for (;;) {
        ssize_t n = ::read(fd, buf, sizeof buf);
        if (n < 0 && errno == EINTR) continue;
        if (n < 0) ok = false;
        if (n <= 0) break;
        out.append(buf, (size_t)n);
    }
    ::close(fd);
    return ok;
}

// Every path here is a card path (cardPathMirrored: no "..", no leading '/'), so local() stays inside the mirror; and
// nothing below follows a symlink (lstat, O_NOFOLLOW, nftw with FTW_PHYS, utimensat with AT_SYMLINK_NOFOLLOW).

void forgetUnder(const std::string& rel) {  // rel and everything under it leave the manifest
    std::string under = rel + "/";
    for (auto it = man.files.lower_bound(rel); it != man.files.end();) {
        if (it->first == rel || it->first.compare(0, under.size(), under) == 0) {
            it = man.files.erase(it);
            dirty = true;
        } else if (it->first.compare(0, rel.size(), rel) != 0) {
            break;
        } else {
            ++it;
        }
    }
}

int removeOne(const char* p, const struct stat*, int, struct FTW*) {
    ::remove(p);  // (a symlink itself, never what it points at)
    return 0;
}

// rel on the card is a folder: a file or symlink in its place here goes, then the folder is made
void needDir(const std::string& rel) {
    std::string p = local(rel);
    struct stat st;
    if (::lstat(p.c_str(), &st) == 0) {
        if (S_ISDIR(st.st_mode)) return;
        ::unlink(p.c_str());
        forgetUnder(rel);
    }
    ::mkdir(p.c_str(), 0700);
}

// rel on the card is a file: every folder above it must be a folder here, and a folder (or symlink) in its place goes
void needFileRoom(const std::string& rel) {
    for (size_t i = 0; i < rel.size(); i++)
        if (rel[i] == '/') needDir(rel.substr(0, i));
    std::string p = local(rel);
    struct stat st;
    if (::lstat(p.c_str(), &st) != 0) return;
    if (S_ISDIR(st.st_mode)) {
        ::nftw(p.c_str(), removeOne, 16, FTW_DEPTH | FTW_PHYS);
        forgetUnder(rel);
        man.files.erase(rel);
    } else if (S_ISLNK(st.st_mode)) {
        ::unlink(p.c_str());
    }
}

void needDirs(const std::string& rel) {  // rel and the folders above it are folders on the card
    for (size_t i = 0; i <= rel.size(); i++)
        if (i == rel.size() || rel[i] == '/') needDir(rel.substr(0, i));
}

bool isRegular(const std::string& rel, struct stat& st) {  // a plain file here (a symlink is not)
    return ::lstat(local(rel).c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

void setTime(const std::string& path, int64_t mtime) {
    timespec ts[2];
    ts[0].tv_sec = ts[1].tv_sec = (time_t)mtime;
    ts[0].tv_nsec = ts[1].tv_nsec = 0;
    ::utimensat(AT_FDCWD, path.c_str(), ts, AT_SYMLINK_NOFOLLOW);
}

void ensureLoaded() {
    if (loaded) return;
    loaded = true;
    if (axisDataDir().empty()) return;
    mirrorDir = axisDataDir() + "/mirror";
    manPath = axisDataDir() + "/mirror.manifest";
    takePath = axisDataDir() + "/mirror.takeover";
    ::mkdir(mirrorDir.c_str(), 0700);
    std::string text;
    if (!readWhole(manPath, text)) {
        std::printf("axis: mirror: no manifest yet, the first copy starts once the main is followed\n");
        return;
    }
    if (!parseManifest(text, man)) {
        man = Manifest();
        std::printf("axis: mirror: the manifest cannot be read, so the first copy runs again (files already there "
                    "at the main's size are kept)\n");
        return;
    }
    feedGen = man.gen;  // (the feed is asked from where the manifest left it)
    feedSeq = man.seq;
    std::printf("axis: mirror: %zu files in the manifest, first copy %s\n", man.files.size(),
                man.complete ? "finished" : "not finished");
}

bool saveManifest(uint32_t now) {
    if (manPath.empty()) return false;
    savedAt = now;
    if (!writeWhole(manPath, formatManifest(man))) {
        errLine("cannot write %s: %s", manPath.c_str(), std::strerror(errno));
        return false;
    }
    dirty = false;
    return true;
}

void readConf() {
    if (confRead) return;
    confRead = true;
    pace.reset(new Pace((uint32_t)hostConfInt("mirror_rate", 65536, 1024, 1000000000L)));
    maxAgeMs = (uint32_t)hostConfInt("mirror_max_age_minutes", 60, 1, 10000) * 60000;
    std::string n = hostConfStr("noon", "12:00");
    int         h = -1, m = -1;
    char        extra = 0;
    if (std::sscanf(n.c_str(), "%d:%d%c", &h, &m, &extra) == 2 && h >= 0 && h < 24 && m >= 0 && m < 60)
        noonMin = h * 60 + m;
    else
        std::printf("axis: conf: noon=%s is not a time HH:MM, using 12:00\n", n.c_str());
}

void dropTemp() {
    if (cur.fd >= 0) {
        ::close(cur.fd);
        ::unlink(local(cur.path + ".ax_").c_str());
    }
    cur = Copy();
}

// one request; false when it failed (a back-off starts)
bool ask(const std::string& req, std::string& reply, uint32_t now) {
    reply.clear();
    Cluster* c = Cluster::instance();
    if (!c || !c->request(req, reply, kReqTimeoutMs)) {
        waiting = true;
        waitUntil = now + kFailBackoffMs;
        pace->charge(kMinCharge);
        return false;
    }
    if (reply.size() > kMinCharge) pace->charge((uint32_t)(reply.size() - kMinCharge));
    return true;
}

std::string req(char op, std::initializer_list<std::string> args) {
    std::string r = std::string("I") + kSep + op;
    for (const std::string& a : args) r += kSep + a;
    return r;
}

std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> out;
    size_t                   pos = 0;
    for (;;) {
        size_t e = s.find(sep, pos);
        out.push_back(s.substr(pos, e == std::string::npos ? std::string::npos : e - pos));
        if (e == std::string::npos) break;
        pos = e + 1;
    }
    return out;
}

bool parseStat(const std::string& reply, int64_t& size, int64_t& mtime) {  // "1" size 1F mtime
    auto f = split(reply.substr(1), kSep);
    if (f.size() != 2) return false;
    char* end = nullptr;
    size = std::strtoll(f[0].c_str(), &end, 10);
    if (f[0].empty() || *end) return false;
    mtime = std::strtoll(f[1].c_str(), &end, 10);
    return !f[1].empty() && !*end;
}

void removeLocal(const std::string& rel) {
    std::string p = local(rel);
    if (::unlink(p.c_str()) != 0 && (errno == EISDIR || errno == EPERM)) ::rmdir(p.c_str());  // (a folder: only when empty)
    if (man.files.erase(rel)) dirty = true;
}

// the mirror is in step with the main's card (a pass finished, or the feed answered with nothing missed): noted only
// while following a card main, and #fresh= written again only once it is kFreshSaveS behind (not every feed answer)
void markFresh() {
    Cluster* c = Cluster::instance();
    if (!c || c->mainKind() != axis::cluster::CardKind::Card || massHeld) return;
    followedCard = true;
    freshMono = monoMs();
    int64_t t = (int64_t)time(nullptr);
    if (t - man.fresh >= kFreshSaveS || t < man.fresh) dirty = true;
    man.fresh = t;
}

void startPass(Pass p, const char* why, uint32_t now) {
    pass = p;
    walking = true;
    walkStack.assign(1, WalkDir{"", 0});
    listing.clear();
    passQ.clear();
    tries.clear();
    copied = removed = matched = failed = 0;
    stopped = false;
    (void)now;
    if (p == Pass::First)
        std::printf("axis: mirror: first copy of the main's card started (%zu files already in the manifest)\n",
                    man.files.size());
    else
        std::printf("axis: mirror: check started (%s)\n", why);
}

std::string today(int& minutes) {
    time_t t = time(nullptr);
    tm     lt{};
    localtime_r(&t, &lt);
    minutes = lt.tm_hour * 60 + lt.tm_min;
    char buf[32];
    std::snprintf(buf, sizeof buf, "%04d-%02d-%02d", lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday);
    return buf;
}

void endPass(uint32_t now) {
    char what[160];
    if (massStop) {  // (nothing copied or removed; asked again in 10 minutes, or at once with FORCE)
        passWaiting = true;
        passAgainAt = now + kLogMs;
        std::snprintf(what, sizeof what, "%s stopped: it would remove %zu of %zu files (@CARDMIRROR CHECK FORCE)",
                      pass == Pass::First ? "first copy" : "check", massN, massOf);
        if (pass == Pass::Check && !noonPass) checkWanted = true;
    } else if (walkFailed) {
        passWaiting = true;
        passAgainAt = now + kFirstAgainMs;
        if (pass == Pass::Check && !noonPass) checkWanted = true;  // (noon is still due, so it comes back by itself)
        errLine("the main could not list a folder of its card: %s stopped, trying again in a minute",
                pass == Pass::First ? "the first copy" : "the check");
        std::snprintf(what, sizeof what, "%s stopped: the main could not list a folder",
                      pass == Pass::First ? "first copy" : "check");
    } else if (pass == Pass::First) {
        if (!failed && !stopped) {
            man.complete = true;
            std::printf("axis: mirror: first copy finished: %zu files (copied %u, kept %u)\n", man.files.size(), copied,
                        matched);
            std::snprintf(what, sizeof what, "first copy finished: copied %u, kept %u", copied, matched);
        } else {
            passWaiting = true;
            passAgainAt = now + kFirstAgainMs;
            errLine("first copy not finished: %u files changed while copied or could not be copied, trying again in a "
                    "minute",
                    failed);
            std::snprintf(what, sizeof what, "first copy not finished: %u files left", failed);
        }
    } else {
        if (copied || removed || failed)
            std::printf("axis: mirror: check: copied %u, removed %u, matched %u%s\n", copied, removed, matched,
                        failed ? " (some files changed while copied: the next check takes them)" : "");
        else
            std::printf("axis: mirror: check: matches (%u files)\n", matched);
        std::snprintf(what, sizeof what, "check: copied %u, removed %u, matched %u%s", copied, removed, matched,
                      failed ? ", some left for the next check" : "");
        if (noonPass) {
            int m;
            man.noon = today(m);
        }
    }
    if (!massStop && !walkFailed && !failed && !stopped && man.complete) markFresh();
    lastCheckAt = time(nullptr);
    lastCheck = std::string(noonPass ? "noon " : "") + what;
    noonPass = false;
    walkFailed = false;
    massStop = false;
    pass = Pass::None;
    walking = false;
    walkStack.clear();
    listing.clear();
    passQ.clear();
    tries.clear();
    saveManifest(now);
}

// the walk is over: what to copy and what to remove
void walkDone(uint32_t now) {
    walking = false;
    MirrorDiff d = diffListing(man, listing);
    bool       force = forceRemove;
    forceRemove = false;  // (FORCE is good for one walk)
    size_t n = d.remove.size(), of = man.files.size();
    if (!force && (n > kMassMax || (n > kMassFloor && n * 4 > of))) {  // a blank or swapped card? nothing is changed
        errLine("check would remove %zu of %zu files, not removing: use @CARDMIRROR CHECK FORCE", n, of);
        massStop = massHeld = true;
        massN = n;
        massOf = of;
        listing.clear();
        endPass(now);
        return;
    }
    massHeld = false;
    for (const std::string& r : d.remove) {
        removeLocal(r);
        removed++;
    }
    matched = (unsigned)(listing.size() - d.copy.size());
    for (const CardEntry& e : d.copy) {
        if (pass == Pass::First && !man.files.count(e.path)) {  // Rev may have pre-loaded the mirror from a card copy
            struct stat st;
            if (isRegular(e.path, st) && (uint64_t)st.st_size == e.size) {  // (never through a symlink)
                setTime(local(e.path), e.mtime);
                man.files[e.path] = e;
                dirty = true;
                matched++;
                continue;
            }
        }
        passQ.push_back(e.path);
    }
    listing.clear();
    if (dirty && now - savedAt >= kSaveMs) saveManifest(now);
}

void diskFull(uint32_t now) {
    errLine("disk full, copy stopped");
    stopped = true;
    stoppedAt = now;
    dropTemp();
    liveQ.clear();
    pending.clear();
    verify.clear();  // (the compare pass after the wait catches what they were waiting for)
    if (pass != Pass::None) {
        walking = false;
        passQ.clear();
        endPass(now);
        stopped = true;  // (endPass keeps it; a new pass clears it)
    }
}

// the file in hand goes back to the front of its queue (the link went, or the takeover stops the copy)
void putBack() {
    if (cur.active) (cur.live ? liveQ : passQ).push_front(cur.path);
    dropTemp();
}

// a live path is checked again from now on (kVerifyMs); a full list asks for a compare pass instead
void toVerify(const std::string& path, uint32_t now) {
    auto it = verify.find(path);
    if (it == verify.end() && verify.size() >= kVerifyMax) {
        if (!checkWanted) {
            checkWanted = true;
            checkWhy = "more files changing than live follow checks";
        }
        return;
    }
    verify[path] = Verify{now, 0};
}

bool queuedLive(const std::string& path) {
    if (cur.active && cur.live && cur.path == path) return true;
    for (const std::string& q : liveQ)
        if (q == path) return true;
    return false;
}

// a file is copied: the pass counts it, and a live one is checked again (it may still be being written)
void copyDone(uint32_t now) {
    if (!cur.live) copied++;
    else toVerify(cur.path, now);
    cur = Copy();
}

void copyFailed(const std::string& path, bool live, uint32_t now) {  // changed while copied, or a request failed
    int n = ++tries[path];
    if (n < kTries) {
        (live ? liveQ : passQ).push_back(path);
    } else if (!live) {
        failed++;
    } else {
        tries.erase(path);  // (the verify list copies it again once it changes or settles)
        toVerify(path, now);
    }
}

// one 'I S' for the verify list's oldest due path: true when it made a request
bool verifyStep(uint32_t now) {
    auto due = verify.end();
    for (auto it = verify.begin(); it != verify.end(); ++it)
        if (now - it->second.at >= kVerifyMs && (due == verify.end() || (int32_t)(it->second.at - due->second.at) < 0))
            due = it;
    if (due == verify.end()) return false;
    std::string path = due->first;
    if (queuedLive(path)) {  // (a copy is coming: it puts the path back on the list)
        verify.erase(due);
        return false;
    }
    std::string reply;
    if (!ask(req('S', {path}), reply, now)) return true;  // (kept: checked again after the back-off)
    auto    it = man.files.find(path);
    int64_t size = 0, mtime = 0;
    bool    gone = reply == "0";
    bool    known = !gone && !reply.empty() && reply[0] == '1' && parseStat(reply, size, mtime);
    if (!gone && !known) return true;
    bool agrees = gone ? it == man.files.end() : size < 0 || (it != man.files.end() && it->second.size == (uint64_t)size &&
                                                              it->second.mtime == mtime);
    if (!agrees) {  // it changed after the copy (or the copy was given up): copy it again
        verify.erase(path);
        tries.erase(path);
        pending[path] = now;  // (copied once it has been quiet kQuietMs, as a feed path is)
        return true;
    }
    Verify& v = verify[path];
    v.at = now;
    if (++v.agree >= 2) verify.erase(path);  // two checks kVerifyMs apart agree: settled
    return true;
}

// the next step of the copy in hand: one request
void copyStep(uint32_t now) {
    std::string reply;
    if (cur.step == Copy::Stat1 || cur.step == Copy::Stat2) {
        if (!ask(req('S', {cur.path}), reply, now)) {
            std::string p = cur.path;
            bool        live = cur.live;
            dropTemp();
            copyFailed(p, live, now);
            return;
        }
        if (reply == "0") {  // gone from the card
            std::string p = cur.path;
            dropTemp();
            removeLocal(p);
            return;
        }
        int64_t size = 0, mtime = 0;
        if (reply.empty() || reply[0] != '1' || !parseStat(reply, size, mtime)) {
            std::string p = cur.path;
            bool        live = cur.live;
            dropTemp();
            copyFailed(p, live, now);
            return;
        }
        if (size < 0) {  // a folder
            std::string p = cur.path;
            dropTemp();
            needDirs(p);
            return;
        }
        if (cur.step == Copy::Stat1) {
            auto it = man.files.find(cur.path);
            struct stat st;
            if (it != man.files.end() && it->second.size == (uint64_t)size && it->second.mtime == mtime &&
                isRegular(cur.path, st) && (uint64_t)st.st_size == (uint64_t)size) {  // already as the card has it
                if (!cur.live) matched++;
                cur = Copy();
                return;
            }
            struct statvfs vs;
            if (::statvfs(mirrorDir.c_str(), &vs) == 0) {
                uint64_t freeB = (uint64_t)vs.f_bavail * vs.f_frsize;
                if (freeB < (uint64_t)size + kKeepFree) {
                    diskFull(now);
                    return;
                }
            }
            needFileRoom(cur.path);
            std::string tmp = local(cur.path + ".ax_");
            cur.fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW, 0600);
            if (cur.fd < 0 && (errno == EISDIR || errno == ELOOP)) {  // (a temp name is never the card's: clear it)
                ::nftw(tmp.c_str(), removeOne, 16, FTW_DEPTH | FTW_PHYS);
                cur.fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW, 0600);
            }
            if (cur.fd < 0) {
                errLine("cannot write %s: %s", local(cur.path + ".ax_").c_str(), std::strerror(errno));
                if (!cur.live) failed++;
                cur = Copy();
                return;
            }
            cur.size = (uint64_t)size;
            cur.mtime = mtime;
            cur.off = 0;
            cur.step = cur.size ? Copy::Read : Copy::Stat2;
            return;
        }
        // Stat2: unchanged since Stat1, so the copy is the card's file
        if ((uint64_t)size != cur.size || mtime != cur.mtime) {
            std::string p = cur.path;
            bool        live = cur.live;
            dropTemp();
            copyFailed(p, live, now);
            return;
        }
        bool        ok = ::fsync(cur.fd) == 0;
        ok = ::close(cur.fd) == 0 && ok;
        cur.fd = -1;
        std::string tmp = local(cur.path + ".ax_"), dst = local(cur.path);
        bool moved = ok && ::rename(tmp.c_str(), dst.c_str()) == 0;
        if (ok && !moved && (errno == EISDIR || errno == ENOTDIR || errno == ENOTEMPTY || errno == EEXIST)) {
            needFileRoom(cur.path);  // a folder here where the card now has a file (or the other way round above it)
            moved = ::rename(tmp.c_str(), dst.c_str()) == 0;
        }
        if (!moved) {
            errLine("cannot finish %s: %s", dst.c_str(), std::strerror(errno));
            ::unlink(tmp.c_str());
            if (!cur.live) failed++;
            cur = Copy();
            return;
        }
        setTime(dst, cur.mtime);
        man.files[cur.path] = CardEntry{cur.path, cur.size, cur.mtime};
        dirty = true;
        copyDone(now);
        return;
    }
    // Read: the next piece
    uint64_t want = cur.size - cur.off;
    if (want > axis::cluster::kTreePiece) want = axis::cluster::kTreePiece;
    bool got = ask(req('R', {cur.path, std::to_string(cur.off), std::to_string(want)}), reply, now);
    if (!got || reply.size() < 2 || reply[0] != '1') {  // failed, gone, or shorter than it said: changed
        std::string p = cur.path;
        bool        live = cur.live;
        dropTemp();
        copyFailed(p, live, now);
        return;
    }
    size_t n = reply.size() - 1;
    if (n > want) n = (size_t)want;
    for (size_t done = 0; done < n;) {
        ssize_t w = ::write(cur.fd, reply.data() + 1 + done, n - done);
        if (w < 0 && errno == EINTR) continue;
        if (w <= 0) {
            errLine("cannot write %s: %s", local(cur.path + ".ax_").c_str(), std::strerror(errno));
            bool live = cur.live;
            dropTemp();
            if (!live) failed++;
            return;
        }
        done += (size_t)w;
    }
    cur.off += n;
    if (cur.off >= cur.size) cur.step = Copy::Stat2;
}

// one 'I D' of the walk
void walkStep(uint32_t now) {
    WalkDir     w = walkStack.back();
    std::string reply;
    if (!ask(req('D', {w.dir, std::to_string(w.skip)}), reply, now)) return;  // (retried after the back-off)
    walkStack.pop_back();
    if (reply.empty() || reply[0] != '1') {  // (a folder that went away while walked, or the card failed): try later
        walkFailed = true;
        endPass(now);
        return;
    }
    for (const std::string& line : split(reply.substr(1), '\n')) {
        if (line.empty()) continue;
        auto f = split(line, kSep);
        if (f[0] == "+" && f.size() == 2) {
            walkStack.push_back(WalkDir{w.dir, std::strtoull(f[1].c_str(), nullptr, 10)});
            continue;
        }
        if (f.size() < 2) continue;
        std::string rel = w.dir.empty() ? f[1] : w.dir + "/" + f[1];
        if (!axis::cluster::cardPathMirrored(rel)) continue;
        if (f[0] == "d") {
            needDirs(rel);
            walkStack.push_back(WalkDir{rel, 0});
        } else if (f[0] == "f" && f.size() == 4) {
            CardEntry e;
            e.path = rel;
            e.size = std::strtoull(f[2].c_str(), nullptr, 10);
            e.mtime = std::strtoll(f[3].c_str(), nullptr, 10);
            listing.push_back(e);
        }
    }
    if (walkStack.empty()) walkDone(now);
}

// one 'I C': the paths changed since feedSeq
void feedStep(uint32_t now) {
    feedAt = now;
    std::string reply;
    if (!ask(req('C', {std::to_string(feedGen), std::to_string(feedSeq)}), reply, now)) return;
    if (reply.empty() || reply[0] != '1') {  // (not asked again every turn)
        waiting = true;
        waitUntil = now + kFailBackoffMs;
        return;
    }
    auto   lines = split(reply.substr(1), '\n');
    auto   head = split(lines[0], kSep);
    if (head.size() != 3) {
        waiting = true;
        waitUntil = now + kFailBackoffMs;
        return;
    }
    uint32_t gen = (uint32_t)std::strtoul(head[0].c_str(), nullptr, 10);
    uint32_t seq = (uint32_t)std::strtoul(head[1].c_str(), nullptr, 10);
    bool     lost = head[2] == "1";
    bool     gap = lost || gen != feedGen;
    bool     firstKnown = !feedKnown;
    feedKnown = true;
    feedGen = gen;
    feedSeq = seq;
    if (gap) {
        // a first copy that has not started walks everything after this point anyway
        if (man.complete || pass == Pass::First) {
            checkWanted = true;
            checkWhy = firstKnown ? "following a main again" : "a gap in the change feed";
        }
        if (man.gen != gen || man.seq != seq) {
            man.gen = gen;
            man.seq = seq;
            dirty = true;
        }
        return;
    }
    for (size_t i = 1; i < lines.size(); i++)
        if (!lines[i].empty() && axis::cluster::cardPathMirrored(lines[i])) pending[lines[i]] = now;
    if (man.complete && pass == Pass::None && !checkWanted && !stopped) markFresh();  // (nothing missed, nothing owed)
}

bool someoneOn() {
    Cluster* c = Cluster::instance();
    if (!c) return false;
    for (const auto& n : c->nodes())
        if (!n.handle.empty()) return true;
    return false;
}

}  // namespace

bool mirrorComplete() {
    ensureLoaded();
    return man.complete;
}

void mirrorCheckSoon(bool force) {
    checkWanted = true;
    checkWhy = force ? "asked for, with FORCE" : "asked for";
    if (force) {
        forceRemove = true;
        passWaiting = false;  // (a pass held by the guard starts again at once)
    }
}

bool mirrorTakeoverOk(std::string& why) {
    ensureLoaded();
    readConf();
    if (!man.complete) why = "the first copy is not finished";
    else if (massHeld) why = "the last check found most of the card gone (@CARDMIRROR CHECK FORCE)";
    else if (!followedCard) why = "this run has not been in step with the card board yet";
    else if (monoMs() - freshMono > maxAgeMs) why = mirrorAge() + ", over mirror_max_age_minutes";
    else return true;
    return false;
}

std::string mirrorAge() {
    ensureLoaded();
    if (!man.fresh) return "the mirror has never been in step with the card board";
    long m = ((long)time(nullptr) - (long)man.fresh) / 60;
    if (m < 0) m = 0;
    return "the mirror was last in step with the card board " + std::to_string(m) + (m == 1 ? " minute ago" : " minutes ago");
}

bool mirrorSaveForTakeover() {
    ensureLoaded();
    if (manPath.empty()) return false;
    putBack();  // (a half-copied file is not the card's; it is copied again if the takeover is called off)
    uint32_t now = monoMs();
    if (!saveManifest(now)) return false;
    if (!writeWhole(takePath, formatManifest(man))) {
        errLine("cannot write %s: %s", takePath.c_str(), std::strerror(errno));
        return false;
    }
    // a new takeover starts a new change log (one left from an earlier takeover was handed back, or the mirror has
    // been copied from the card since)
    std::string changes = axisDataDir() + "/mirror.changes";
    if (::unlink(changes.c_str()) != 0 && errno != ENOENT) {
        errLine("cannot clear %s: %s", changes.c_str(), std::strerror(errno));
        return false;
    }
    return true;
}

MirrorStatus mirrorStatus() {
    ensureLoaded();
    MirrorStatus s;
    s.complete = man.complete;
    s.files = man.files.size();
    for (const auto& f : man.files) s.bytes += f.second.size;
    s.noon = man.noon;
    s.lastCheckAt = (long)lastCheckAt;
    s.lastCheck = lastCheck;
    s.pass = pass == Pass::First ? "first copy" : pass == Pass::Check ? (noonPass ? "noon check" : "check") : "";
    s.pending = pending.size() + liveQ.size() + (cur.active && cur.live ? 1 : 0);
    s.verifying = verify.size();
    s.lastErrAt = (long)lastErrAt;
    s.lastErr = lastErr;
    s.stopped = stopped;
    s.freshAt = (long)man.fresh;
    s.held = massHeld;
    return s;
}

void mirrorPoll(uint32_t now) {
    if (inPoll) return;  // (never inside itself: a request's waits may pump other work)
    struct Guard {
        Guard() { inPoll = true; }
        ~Guard() { inPoll = false; }
    } guard;
    ensureLoaded();
    readConf();
    if (mirrorDir.empty()) return;

    Cluster* c = Cluster::instance();
    if (!c || c->role() != Role::Follower || c->mainVersion() < axis::cluster::kProtocolVersion) {
        if (ready) putBack();
        ready = false;
        return;
    }
    if (ready && (c->epoch() != readyEpoch || c->mainIp() != readyIp)) {  // another main (or the same, again)
        ready = false;
        putBack();
    }

    // the feed's position is written once nothing from it is waiting
    if (feedKnown && pending.empty() && liveQ.empty() && verify.empty() && !(cur.active && cur.live) && (man.gen != feedGen || man.seq != feedSeq)) {
        man.gen = feedGen;
        man.seq = feedSeq;
        dirty = true;
    }
    if (dirty && now - savedAt >= kSaveMs) saveManifest(now);
    if (stopped && now - stoppedAt >= kDiskFullMs) {  // try again, and catch up on what was missed
        stopped = false;
        checkWanted = true;
        checkWhy = "after the disk was full";
    }
    for (auto it = pending.begin(); it != pending.end();) {  // quiet long enough: copy it
        if (now - it->second >= kQuietMs) {
            if (!queuedLive(it->first)) liveQ.push_back(it->first);
            it = pending.erase(it);
        } else {
            ++it;
        }
    }

    if (waiting && (int32_t)(now - waitUntil) < 0) return;
    waiting = false;
    pace->setBusy(someoneOn());
    if (!pace->take(kMinCharge, now)) return;

    std::string reply;
    if (!ready) {  // the main serves the card tree?
        if (!ask(req('V', {}), reply, now)) return;
        if (reply != "12") {
            waiting = true;
            waitUntil = now + kNoTreeBackoffMs;
            if (!noTreeLogged || now - noTreeAt >= kLogMs) {
                errLine("the main does not serve its card tree yet (no card, or older firmware)");
                noTreeLogged = true;
                noTreeAt = now;
            }
            return;
        }
        ready = true;
        readyEpoch = c->epoch();
        readyIp = c->mainIp();
        feedAt = now - kFeedMs;  // the feed first
        return;
    }
    if (!feedKnown || now - feedAt >= kFeedMs) {
        feedStep(now);
        return;
    }
    if (cur.active) {
        copyStep(now);
        return;
    }
    if (!liveQ.empty() && !stopped) {
        cur = Copy();
        cur.active = true;
        cur.live = true;
        cur.path = liveQ.front();
        liveQ.pop_front();
        copyStep(now);
        return;
    }
    if (!stopped && verifyStep(now)) return;
    if (passWaiting && (int32_t)(now - passAgainAt) >= 0) passWaiting = false;
    if (pass == Pass::None && !stopped && !passWaiting) {  // a pass to start? (it starts with its first request)
        if (!man.complete) {
            checkWanted = false;
            startPass(Pass::First, "", now);
        } else if (checkWanted) {
            checkWanted = false;
            startPass(Pass::Check, checkWhy, now);
        } else {
            int         minutes = 0;
            std::string day = today(minutes);
            if (noonDue(man.noon, day, minutes, noonMin)) {
                noonPass = true;
                startPass(Pass::Check, "noon", now);
            }
        }
    }
    if (pass == Pass::None) return;
    if (walking) {
        walkStep(now);
        return;
    }
    if (!passQ.empty()) {
        cur = Copy();
        cur.active = true;
        cur.live = false;
        cur.path = passQ.front();
        passQ.pop_front();
        copyStep(now);
        return;
    }
    endPass(now);
}
