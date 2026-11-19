// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The takeover change log and @HANDBACK (clusterhost.h), MirrorMain only.
//
// While the i5 is main its card is the mirror (<data>/mirror). Every card path the BBS writes is noted
// (takeoverNote, from fs_remap.cpp) in <data>/mirror.changes. @HANDBACK GO copies those paths back to the card
// board over its link (cluster/cardtree.h's write ops, MainReq), then 'I E' restarts the card board and this program
// restarts as a Follower in state handedback, so the card board becomes main from a clean boot.
//
// The card's state at the takeover is <data>/mirror.takeover (the manifest then, as the card reported each file).
// Each pass first asks 'I S' for every path it will touch and compares it with that state; any difference stops
// before anything is written (Question 3's default) and the i5 stays main. After each 'I F' or 'I X' that state is
// updated (and saved), so a hand-back that stopped part way can be started again.
//
// Up to kPasses passes, each over the paths noted since the one before began (the BBS keeps running meanwhile);
// what is noted after the last pass began is copied into <data>/handback-left/ and listed, not to the card.
//
// Every path comes from mirror.changes, the takeover state or a walk of the mirror, and goes through
// cardPathMirrored before use; local files are read with lstat and O_NOFOLLOW, and a symlink stops the hand-back.
// A folder on its own cannot be made on the card (there is no op for it): one is made when a file in it is copied.
#include <errno.h>
#include <fcntl.h>
#include <ftw.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <initializer_list>
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
#include "platform/linux/host.h"
#include "platform/linux/hostconf.h"
#include "platform/linux/paths.h"

namespace {

using axis::cluster::Cluster;
using axis::cluster::Role;
using axis::cluster::cardPathMirrored;

constexpr char     kSep = '\x1f';
constexpr uint32_t kReqTimeoutMs = 5000;  // a card's rename and utime can be slow
constexpr int      kPasses = 3;
constexpr int      kTries = 3;            // a file that changes while it is sent is sent again this often a pass
constexpr uint32_t kMinCharge = 256;      // a request counts at least this many bytes against the pace
constexpr uint32_t kSaveMs = 5000;        // the takeover state is written at most this often while it changes
constexpr size_t   kShowMax = 30;         // @HANDBACK lists at most this many paths

// ---- the change log ----

std::mutex            mu;  // the log's sets (takeoverNote may come from any thread that touches the card)
bool                  logLoaded = false;
std::set<std::string> changes;  // every card path written since the takeover (mirror.changes)
std::set<std::string> fresh;    // noted since the running pass began (only while a hand-back runs)
bool                  collecting = false;
bool                  appendFailLogged = false;

std::string dataPath(const char* name) { return axisDataDir().empty() ? "" : axisDataDir() + "/" + name; }
std::string mirrorDir() { return dataPath("mirror"); }
std::string local(const std::string& rel) { return mirrorDir() + "/" + rel; }

bool readWhole(const std::string& path, std::string& out) {
    out.clear();
    if (path.empty()) return false;
    int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
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

bool writeWhole(const std::string& path, const std::string& text) {
    if (path.empty()) return false;
    std::string tmp = path + ".tmp";
    int         fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW, 0600);
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

void loadLog() {  // (mu held)
    if (logLoaded) return;
    logLoaded = true;
    std::string text;
    if (!readWhole(dataPath("mirror.changes"), text)) return;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t      nl = text.find('\n', pos);
        std::string line = text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
        pos = nl == std::string::npos ? text.size() : nl + 1;
        if (cardPathMirrored(line)) changes.insert(line);
    }
}

std::set<std::string> logSnapshot() {
    std::lock_guard<std::mutex> lk(mu);
    loadLog();
    return changes;
}

std::set<std::string> takeFresh() {  // what was noted since the last call, and a new count from now
    std::lock_guard<std::mutex> lk(mu);
    std::set<std::string> f;
    f.swap(fresh);
    return f;
}

void markFresh(const std::string& rel) {  // sent while it changed: the next pass takes it
    std::lock_guard<std::mutex> lk(mu);
    fresh.insert(rel);
}

// ---- the card's state as this program knows it (mirror.takeover, kept up to date) ----

Manifest known;  // .files: each file as the card last reported it
bool     knownLoaded = false;
bool     knownDirty = false;
uint32_t knownSavedAt = 0;

bool loadKnown(std::string& why) {
    if (knownLoaded) return true;
    std::string text;
    if (!readWhole(dataPath("mirror.takeover"), text)) {
        why = "there is no mirror.takeover (the card's state at the takeover)";
        return false;
    }
    if (!parseManifest(text, known)) {
        why = "mirror.takeover cannot be read";
        return false;
    }
    knownLoaded = true;
    return true;
}

bool saveKnown(uint32_t now) {
    knownSavedAt = now;
    if (!writeWhole(dataPath("mirror.takeover"), formatManifest(known))) {
        std::printf("axis: handback: cannot write mirror.takeover: %s\n", std::strerror(errno));
        return false;
    }
    knownDirty = false;
    return true;
}

bool knownUnder(const std::string& rel, const std::string& path) {  // path is under folder rel
    return path.size() > rel.size() && path.compare(0, rel.size(), rel) == 0 && path[rel.size()] == '/';
}

// ---- what a pass does ----

struct Plan {
    std::set<std::string>    check;    // 'I S' for each before anything is written
    std::vector<std::string> remove;   // gone from the mirror: 'I X', deepest first
    std::vector<std::string> dirs;     // folders in the mirror: 'I X' only where the card has a file instead
    std::vector<std::string> copy;     // files in the mirror
    uint64_t                 bytes = 0;
};

std::vector<std::string>* walkOut = nullptr;
std::string               walkRoot, walkBad;
int walkOne(const char* p, const struct stat* st, int type, struct FTW*) {
    std::string rel = std::string(p).substr(mirrorDir().size() + 1);
    if (type == FTW_SL || type == FTW_SLN || (type == FTW_F && !S_ISREG(st->st_mode))) {
        if (cardPathMirrored(rel)) walkBad = rel;
        return 0;
    }
    if (type == FTW_F && cardPathMirrored(rel)) walkOut->push_back(rel);
    return 0;
}

size_t depth(const std::string& p) { return (size_t)std::count(p.begin(), p.end(), '/'); }

// The work for the noted paths `base`: what to check, remove and copy. False (and why) on a mirror entry that is
// neither a plain file nor a folder (a symlink): the hand-back never follows or copies one.
bool makePlan(const std::set<std::string>& base, Plan& plan, std::string& why) {
    plan = Plan();
    std::set<std::string> remove, copy, dirs;
    auto goneUnder = [&](const std::string& p) {  // the card's files under p that the mirror no longer has
        for (auto it = known.files.lower_bound(p + "/"); it != known.files.end() && knownUnder(p, it->first); ++it) {
            struct stat st;
            const std::string& q = it->first;
            if (::lstat(local(q).c_str(), &st) == 0) continue;  // (a file there now: it is noted itself, or walked)
            remove.insert(q);
            plan.check.insert(q);
            for (size_t i = p.size() + 1; i < q.size(); i++)  // and the folders between, where the mirror has none
                if (q[i] == '/') {
                    std::string d = q.substr(0, i);
                    if (::lstat(local(d).c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) remove.insert(d);
                }
        }
    };
    for (const std::string& p : base) {
        if (!cardPathMirrored(p)) continue;
        struct stat st;
        if (::lstat(local(p).c_str(), &st) != 0) {
            remove.insert(p);
            plan.check.insert(p);
            goneUnder(p);
        } else if (S_ISREG(st.st_mode)) {
            copy.insert(p);
            plan.check.insert(p);
            goneUnder(p);
        } else if (S_ISDIR(st.st_mode)) {
            dirs.insert(p);
            plan.check.insert(p);
            goneUnder(p);
            std::vector<std::string> files;  // a folder renamed or made here: what is in it
            walkOut = &files;
            walkBad.clear();
            ::nftw(local(p).c_str(), walkOne, 16, FTW_PHYS);
            walkOut = nullptr;
            if (!walkBad.empty()) {
                why = walkBad + " in the mirror is not a plain file or folder";
                return false;
            }
            for (const std::string& q : files) {
                struct stat qs;
                auto        k = known.files.find(q);
                if (::lstat(local(q).c_str(), &qs) == 0 && k != known.files.end() &&
                    k->second.size == (uint64_t)qs.st_size && k->second.mtime == (int64_t)qs.st_mtime && !base.count(q))
                    continue;  // as the card has it already
                copy.insert(q);
                plan.check.insert(q);
            }
        } else {
            why = p + " in the mirror is not a plain file or folder";
            return false;
        }
    }
    for (const std::string& c : copy) remove.erase(c);
    plan.remove.assign(remove.begin(), remove.end());
    std::stable_sort(plan.remove.begin(), plan.remove.end(), [](const std::string& a, const std::string& b) {
        return depth(a) > depth(b) || (depth(a) == depth(b) && a > b);
    });
    plan.dirs.assign(dirs.begin(), dirs.end());
    plan.copy.assign(copy.begin(), copy.end());
    for (const std::string& c : plan.copy) {
        struct stat st;
        if (::lstat(local(c).c_str(), &st) == 0) plan.bytes += (uint64_t)st.st_size;
    }
    return true;
}

// ---- the hand-back ----

enum class Phase { Plan, Check, Work, Finish };
enum class CardIs { Missing, File, Folder };

struct Sent {
    uint64_t size = 0;
    int64_t  mtime = 0;
};

struct Job {
    bool        running = false;
    bool        stopAsked = false;
    std::string board;  // the card board's id (never shown: it is its MAC)
    int         node = 0;
    int         passNo = 0;
    Phase       phase = Phase::Plan;
    std::set<std::string>         base;  // this pass's noted paths
    Plan                          plan;
    std::vector<std::string>      checkList;
    size_t                        checkAt = 0;
    std::vector<std::string>      differ;
    std::map<std::string, CardIs> cardIs;
    // the work: removals, then folders over files, then copies
    size_t   removeAt = 0, dirAt = 0, copyAt = 0;
    std::map<std::string, Sent> sent;  // what was sent, as the mirror had it (checked again before 'I E')
    unsigned copiedN = 0, removedN = 0;
    uint64_t bytesSent = 0;
};
Job job;

struct Cur {  // the file being sent
    bool        active = false;
    std::string path;
    bool        xDone = false;  // the card's folder in its place is removed
    bool        opened = false;
    int         fd = -1;
    uint64_t    size = 0, off = 0;
    int64_t     mtime = 0;
    int         tries = 0;
};
Cur cur;

bool                  inPoll = false;
std::unique_ptr<Pace> pace;
std::vector<std::string> lastResult;  // the last hand-back's end, for @HANDBACK

void closeCur() {
    if (cur.fd >= 0) ::close(cur.fd);
    cur = Cur();
}

int nodeOf(const std::string& board) {
    Cluster* c = Cluster::instance();
    if (!c) return 0;
    int n = 0;
    for (const auto& i : c->nodes())
        if (i.id == board && (n == 0 || i.number < n)) n = i.number;
    return n;
}

std::vector<axis::cluster::CardBoard> liveCards() {
    std::vector<axis::cluster::CardBoard> v;
    Cluster* c = Cluster::instance();
    if (!c || c->role() != Role::Main) return v;
    for (const auto& b : c->cardBoards())
        if (b.alive) v.push_back(b);
    return v;
}

int callersOn() {
    Cluster* c = Cluster::instance();
    int      n = 0;
    if (c)
        for (const auto& i : c->nodes())
            if (!i.handle.empty()) n++;
    return n;
}

std::string req(char op, std::initializer_list<std::string> args) {
    std::string r = std::string("I") + kSep + op;
    for (const std::string& a : args) r += kSep + a;
    return r;
}

bool parseStat(const std::string& reply, int64_t& size, int64_t& mtime) {  // "1" size 1F mtime
    if (reply.size() < 2 || reply[0] != '1') return false;
    size_t sep = reply.find(kSep, 1);
    if (sep == std::string::npos) return false;
    std::string a = reply.substr(1, sep - 1), b = reply.substr(sep + 1);
    char*       end = nullptr;
    size = std::strtoll(a.c_str(), &end, 10);
    if (a.empty() || *end) return false;
    mtime = std::strtoll(b.c_str(), &end, 10);
    return !b.empty() && !*end;
}

void result(const std::string& line) {
    std::printf("axis: handback: %s\n", line.c_str());
    lastResult.push_back(line);
}

// the hand-back ends here without handing main back: the i5 stays main, mirror.changes is kept
void stop(const std::string& path, const std::string& why, uint32_t now) {
    closeCur();
    lastResult.clear();
    std::printf("axis: handback stopped at %s: %s\n", path.c_str(), why.c_str());
    lastResult.push_back("stopped at " + path + ": " + why);
    if (knownDirty) saveKnown(now);
    job.running = false;
    std::lock_guard<std::mutex> lk(mu);
    collecting = false;
    fresh.clear();
}

bool ask(const std::string& r, std::string& reply, uint32_t) {
    reply.clear();
    Cluster* c = Cluster::instance();
    bool     ok = c && c->requestTo(job.board, r, reply, kReqTimeoutMs);
    pace->charge(reply.size() > kMinCharge ? (uint32_t)(reply.size() - kMinCharge) : 0);
    return ok;
}

void startPass(uint32_t now) {
    job.passNo++;
    job.phase = Phase::Check;
    std::string why;
    if (!makePlan(job.base, job.plan, why)) {
        stop("pass " + std::to_string(job.passNo), why, now);
        return;
    }
    job.checkList.assign(job.plan.check.begin(), job.plan.check.end());
    job.checkAt = 0;
    job.differ.clear();
    job.cardIs.clear();
    job.removeAt = job.dirAt = job.copyAt = 0;
    std::printf("axis: handback: pass %d of %d: %zu paths noted, %zu files to copy (%llu bytes), %zu to remove, "
                "checking the card first\n",
                job.passNo, kPasses, job.base.size(), job.plan.copy.size(), (unsigned long long)job.plan.bytes,
                job.plan.remove.size());
}

// one 'I S' of the check
void checkStep(uint32_t now) {
    if (job.checkAt >= job.checkList.size()) {
        if (!job.differ.empty()) {  // Question 3: changed on the card after the takeover, nothing written
            closeCur();
            lastResult.clear();
            std::printf("axis: handback stopped at the check: %zu files on the card changed after the takeover, "
                        "nothing written, this computer stays main\n",
                        job.differ.size());
            lastResult.push_back("stopped at the check: " + std::to_string(job.differ.size()) +
                                 " files on the card changed after the takeover, nothing written in this pass:");
            for (const std::string& d : job.differ) {
                std::printf("axis: handback: changed on the card: %s\n", d.c_str());
                lastResult.push_back("  " + d);
            }
            if (knownDirty) saveKnown(now);
            job.running = false;
            std::lock_guard<std::mutex> lk(mu);
            collecting = false;
            fresh.clear();
            return;
        }
        job.phase = Phase::Work;
        return;
    }
    const std::string& p = job.checkList[job.checkAt];
    std::string        reply;
    if (!ask(req('S', {p}), reply, now)) {
        stop(p, "the card board did not answer", now);
        return;
    }
    auto    k = known.files.find(p);
    int64_t size = 0, mtime = 0;
    bool    same;
    if (reply == "0") {
        job.cardIs[p] = CardIs::Missing;
        same = k == known.files.end();
    } else if (parseStat(reply, size, mtime)) {
        job.cardIs[p] = size < 0 ? CardIs::Folder : CardIs::File;
        same = size < 0 ? k == known.files.end()
                        : k != known.files.end() && k->second.size == (uint64_t)size && k->second.mtime == mtime;
    } else {
        stop(p, "the card board's answer to the check cannot be read", now);
        return;
    }
    if (!same) job.differ.push_back(p);
    job.checkAt++;
}

void gone(const std::string& p) {  // removed from the card
    if (known.files.erase(p)) knownDirty = true;
    job.removedN++;
}

// one request of the work; false when the pass's work is done
bool workStep(uint32_t now) {
    std::string reply;
    if (!cur.active && job.stopAsked) {
        std::string at = job.removeAt < job.plan.remove.size() ? job.plan.remove[job.removeAt]
                         : job.copyAt < job.plan.copy.size()   ? job.plan.copy[job.copyAt]
                                                               : "the end of pass " + std::to_string(job.passNo);
        return stop(at, "stopped by the sysop", now), true;
    }
    if (!cur.active && job.removeAt < job.plan.remove.size()) {
        const std::string& p = job.plan.remove[job.removeAt];
        if (!ask(req('X', {p}), reply, now)) return stop(p, "the card board did not answer", now), true;
        if (reply != "1") return stop(p, "the card board would not remove it (a folder with files only the card has?)", now), true;
        gone(p);
        job.removeAt++;
        return true;
    }
    while (!cur.active && job.dirAt < job.plan.dirs.size()) {  // a folder here where the card has a file
        const std::string& p = job.plan.dirs[job.dirAt];
        auto               it = job.cardIs.find(p);
        if (it == job.cardIs.end() || it->second != CardIs::File) {
            job.dirAt++;
            continue;
        }
        if (!ask(req('X', {p}), reply, now)) return stop(p, "the card board did not answer", now), true;
        if (reply != "1") return stop(p, "the card board would not remove the file where the mirror has a folder", now), true;
        gone(p);
        job.dirAt++;
        return true;
    }
    if (!cur.active) {
        if (job.copyAt >= job.plan.copy.size()) return false;
        cur = Cur();
        cur.active = true;
        cur.path = job.plan.copy[job.copyAt++];
    }
    const std::string p = cur.path;
    auto              ci = job.cardIs.find(p);
    if (!cur.xDone && ci != job.cardIs.end() && ci->second == CardIs::Folder) {  // a folder on the card in its place
        if (!ask(req('X', {p}), reply, now)) return stop(p, "the card board did not answer", now), true;
        if (reply != "1") return stop(p, "the card board would not remove its folder where the mirror has a file", now), true;
        cur.xDone = true;
        return true;
    }
    if (!cur.opened) {
        cur.fd = ::open(local(p).c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
        struct stat st;
        if (cur.fd < 0 || ::fstat(cur.fd, &st) != 0 || !S_ISREG(st.st_mode)) {
            bool vanished = cur.fd < 0 && errno == ENOENT;
            closeCur();
            if (vanished) {  // removed here since the plan: it was noted, the next pass removes it
                markFresh(p);
                return true;
            }
            return stop(p, "the mirror's file cannot be read (or is not a plain file)", now), true;
        }
        cur.size = (uint64_t)st.st_size;
        cur.mtime = (int64_t)st.st_mtime;
        cur.off = 0;
        if (!ask(req('O', {p}), reply, now)) return stop(p, "the card board did not answer", now), true;
        if (reply != "1") return stop(p, "the card board would not open it for writing", now), true;
        cur.opened = true;
        return true;
    }
    if (cur.off < cur.size) {
        uint64_t want = cur.size - cur.off;
        if (want > axis::cluster::kTreePiece) want = axis::cluster::kTreePiece;
        std::string data((size_t)want, '\0');
        ssize_t     n;
        do n = ::pread(cur.fd, &data[0], (size_t)want, (off_t)cur.off);
        while (n < 0 && errno == EINTR);
        if (n < 0) return stop(p, std::string("the mirror's file cannot be read: ") + std::strerror(errno), now), true;
        if ((uint64_t)n < want) {  // it shrank while sent: from the top (its temp on the card is dropped by 'I O')
            if (++cur.tries >= kTries) {
                markFresh(p);
                closeCur();
            } else {
                ::close(cur.fd);
                cur.fd = -1;
                cur.opened = false;
            }
            return true;
        }
        if (!ask(req('P', {std::to_string(cur.off), data}), reply, now))
            return stop(p, "the card board did not answer", now), true;
        if (reply != "1") return stop(p, "the card board would not write a piece", now), true;
        pace->charge((uint32_t)want > kMinCharge ? (uint32_t)want - kMinCharge : 0);
        cur.off += want;
        job.bytesSent += want;
        return true;
    }
    struct stat st;
    if (::fstat(cur.fd, &st) != 0 || (uint64_t)st.st_size != cur.size || (int64_t)st.st_mtime != cur.mtime) {
        if (++cur.tries >= kTries) {  // still changing: the next pass (or handback-left) takes it
            markFresh(p);
            closeCur();
        } else {
            ::close(cur.fd);
            cur.fd = -1;
            cur.opened = false;
        }
        return true;
    }
    if (!ask(req('F', {std::to_string(cur.size), std::to_string(cur.mtime)}), reply, now))
        return stop(p, "the card board did not answer", now), true;
    int64_t size = 0, mtime = 0;
    if (!parseStat(reply, size, mtime) || size < 0 || (uint64_t)size != cur.size)
        return stop(p, "the card board's copy came out the wrong size", now), true;
    known.files[p] = CardEntry{p, (uint64_t)size, mtime};
    knownDirty = true;
    job.sent[p] = Sent{cur.size, cur.mtime};
    job.copiedN++;
    closeCur();
    return true;
}

// the pass's work is done: files changed since they were sent are noted for the next pass (a file opened before it
// was sent and written later through that handle gets no new note)
void passDone(uint32_t now) {
    for (auto it = job.sent.begin(); it != job.sent.end();) {
        struct stat st;
        bool        same = ::lstat(local(it->first).c_str(), &st) == 0 && S_ISREG(st.st_mode) &&
                    (uint64_t)st.st_size == it->second.size && (int64_t)st.st_mtime == it->second.mtime;
        if (!same) markFresh(it->first);
        ++it;
    }
    if (knownDirty) saveKnown(now);
    std::set<std::string> next = takeFresh();
    for (const std::string& p : next) job.sent.erase(p);  // (sent again, or left)
    if (next.empty() || job.passNo >= kPasses) {
        job.base = next;  // the paths left behind (none when the passes caught up)
        job.phase = Phase::Finish;
        return;
    }
    job.base = next;
    job.phase = Phase::Plan;
}

// copies `rel` from the mirror into <data>/handback-left/ (a plain file; a folder is made; nothing when gone)
bool keepLeft(const std::string& rel, std::string& how) {
    std::string root = dataPath("handback-left");
    ::mkdir(root.c_str(), 0700);
    struct stat st;
    if (::lstat(local(rel).c_str(), &st) != 0) {
        how = "removed here";
        return true;
    }
    for (size_t i = 0; i <= rel.size(); i++)
        if (i == rel.size() || rel[i] == '/') {
            if (i == rel.size() && !S_ISDIR(st.st_mode)) break;
            std::string d = root + "/" + rel.substr(0, i);
            struct stat ds;
            if (::lstat(d.c_str(), &ds) == 0 && !S_ISDIR(ds.st_mode)) ::unlink(d.c_str());
            ::mkdir(d.c_str(), 0700);
        }
    if (S_ISDIR(st.st_mode)) {
        how = "a folder";
        return true;
    }
    if (!S_ISREG(st.st_mode)) {
        how = "not a plain file, not kept";
        return true;
    }
    std::string text;
    if (!readWhole(local(rel), text)) return false;
    std::string dst = root + "/" + rel;
    struct stat ds;
    if (::lstat(dst.c_str(), &ds) == 0 && !S_ISREG(ds.st_mode)) return false;
    if (!writeWhole(dst, text)) return false;
    how = std::to_string(text.size()) + " bytes";
    return true;
}

// the end: what is left is kept, 'I E', the mirror's manifest, the log cleared, state handedback, a restart
void finish(uint32_t now) {
    std::set<std::string> more = takeFresh();  // noted in the turns since the last pass ended
    if (!more.empty() && job.passNo < kPasses) {  // (the passes had caught up: one more)
        job.base = more;
        job.phase = Phase::Plan;
        return;
    }
    for (const std::string& p : more) {
        job.base.insert(p);
        job.sent.erase(p);
    }
    std::vector<std::string> left(job.base.begin(), job.base.end());
    std::vector<std::string> leftLines;
    for (const std::string& p : left) {
        std::string how;
        if (!keepLeft(p, how)) {
            stop(p, "cannot copy it into handback-left: " + std::string(std::strerror(errno)), now);
            return;
        }
        leftLines.push_back(p + " (" + how + ")");
    }
    std::string reply;
    if (!ask(req('E', {}), reply, now) || reply != "1") {
        stop("the end", "the card board did not take the end of the hand-back", now);
        return;
    }
    // the card restarts as it is now: the mirror's manifest is the card's state, without what was left behind (a
    // compare pass copies those from the card again; this computer's copies are in handback-left)
    Manifest    m = known;
    std::string text;
    Manifest    old;
    if (readWhole(dataPath("mirror.manifest"), text) && parseManifest(text, old)) {
        m.gen = old.gen;
        m.seq = old.seq;
        m.complete = old.complete;
        m.noon = old.noon;
    }
    for (const std::string& p : left) {
        m.files.erase(p);
        for (auto it = m.files.lower_bound(p + "/"); it != m.files.end() && knownUnder(p, it->first);) it = m.files.erase(it);
        struct stat st;
        if (!known.files.count(p) && ::lstat(local(p).c_str(), &st) == 0 && S_ISREG(st.st_mode))
            ::unlink(local(p).c_str());  // (not on the card: kept in handback-left, gone from the mirror)
    }
    if (!writeWhole(dataPath("mirror.manifest"), formatManifest(m)))
        std::printf("axis: handback: cannot write mirror.manifest: %s (the next check copies the card again)\n",
                    std::strerror(errno));
    saveKnown(now);
    {
        std::lock_guard<std::mutex> lk(mu);
        changes.clear();
        fresh.clear();
        collecting = false;
    }
    if (::unlink(dataPath("mirror.changes").c_str()) != 0 && errno != ENOENT)
        std::printf("axis: handback: cannot clear mirror.changes: %s\n", std::strerror(errno));
    lastResult.clear();
    result("finished: " + std::to_string(job.copiedN) + " files copied (" + std::to_string(job.bytesSent) +
           " bytes), " + std::to_string(job.removedN) + " removed, in " + std::to_string(job.passNo) + " passes");
    if (!leftLines.empty()) {
        result(std::to_string(leftLines.size()) + " paths changed during the last pass, kept in " +
               dataPath("handback-left") + " and not on the card:");
        for (const std::string& l : leftLines) result("  " + l);
    }
    job.running = false;
    ClusterState st;
    clusterStateLoad(st);
    st.state = Standing::HandedBack;
    st.since = (long)time(nullptr);
    st.soloUntil = 0;
    if (!clusterStateSave(st)) {
        result("cannot save the cluster state: this computer stays main (the card board is restarting and follows it)");
        return;
    }
    hostRestart("main handed back");
}

}  // namespace

void takeoverNote(const char* boardPath) {
    if (!boardPath || std::strncmp(boardPath, "/sd/", 4) != 0) return;
    std::string rel(boardPath + 4);
    if (!cardPathMirrored(rel)) return;
    std::lock_guard<std::mutex> lk(mu);
    loadLog();
    if (collecting) fresh.insert(rel);
    if (!changes.insert(rel).second) return;
    std::string path = dataPath("mirror.changes");
    std::string line = rel + "\n";
    int         fd = path.empty() ? -1 : ::open(path.c_str(), O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC | O_NOFOLLOW, 0600);
    bool        ok = fd >= 0;
    for (size_t done = 0; ok && done < line.size();) {
        ssize_t w = ::write(fd, line.data() + done, line.size() - done);
        if (w < 0 && errno == EINTR) continue;
        if (w <= 0) ok = false;
        else done += (size_t)w;
    }
    if (ok && ::fdatasync(fd) != 0) ok = false;
    if (fd >= 0) ::close(fd);
    if (!ok && !appendFailLogged) {  // (kept in memory: a hand-back in this run still has it)
        appendFailLogged = true;
        std::printf("axis: handback: cannot add to mirror.changes: %s\n", std::strerror(errno));
    }
}

size_t handbackChangeCount() { return logSnapshot().size(); }

void handbackPoll(uint32_t now) {
    if (inPoll || !job.running) return;  // (never inside itself: a request's waits may pump other work)
    struct Guard {
        Guard() { inPoll = true; }
        ~Guard() { inPoll = false; }
    } guard;
    bool alive = false;
    for (const auto& b : liveCards())
        if (b.id == job.board) alive = true;
    if (!alive) {
        stop(cur.active ? cur.path : "pass " + std::to_string(job.passNo), "the card board is not linked", now);
        return;
    }
    if (job.phase == Phase::Plan) {  // (no request)
        startPass(now);
        return;
    }
    if (job.stopAsked && (job.phase == Phase::Check || job.phase == Phase::Finish)) {
        stop(job.phase == Phase::Check ? "the check" : "the end", "stopped by the sysop", now);
        return;
    }
    if (knownDirty && now - knownSavedAt >= kSaveMs) saveKnown(now);
    Cluster* c = Cluster::instance();
    bool     busy = false;
    if (c)
        for (const auto& i : c->nodes())
            if (!i.handle.empty()) busy = true;
    pace->setBusy(busy);
    if (!pace->take(kMinCharge, now)) return;
    if (job.phase == Phase::Check) {
        checkStep(now);
    } else if (job.phase == Phase::Work) {
        if (!workStep(now)) passDone(now);
    } else if (job.phase == Phase::Finish) {
        finish(now);
    }
}

void handbackShow(std::string& out) {
    auto line = [&](const std::string& s) { out += "axis: handback: " + s + "\n"; };
    if (axisProfile() != Profile::MirrorMain) {
        line(std::string("nothing to hand back: this computer is ") +
             (axisProfile() == Profile::Follower ? "a follower" : "the solo BBS") + ", not main from a takeover");
        for (const std::string& r : lastResult) line(r);
        return;
    }
    ClusterState st;
    if (clusterStateLoad(st) && st.since) {
        time_t    t = (time_t)st.since;
        struct tm lt{};
        localtime_r(&t, &lt);
        char buf[40];
        std::strftime(buf, sizeof buf, "%Y-%m-%d %H:%M", &lt);
        line(std::string("this computer is main from its card mirror since ") + buf);
    }
    std::set<std::string> log = logSnapshot();
    std::string           why;
    if (!loadKnown(why)) {
        line(std::to_string(log.size()) + " paths noted since the takeover, but " + why + ", so it cannot be handed back");
    } else {
        Plan plan;
        if (!makePlan(log, plan, why)) {
            line(std::to_string(log.size()) + " paths noted since the takeover; " + why);
        } else {
            line(std::to_string(log.size()) + " paths noted since the takeover: " + std::to_string(plan.copy.size()) +
                 " files to copy (" + std::to_string(plan.bytes) + " bytes), " + std::to_string(plan.remove.size()) +
                 " to remove");
            size_t shown = 0;
            for (const std::string& p : plan.copy)
                if (shown++ < kShowMax) line("  copy " + p);
            for (const std::string& p : plan.remove)
                if (shown++ < kShowMax) line("  remove " + p);
            if (shown > kShowMax) line("  and " + std::to_string(shown - kShowMax) + " more");
        }
    }
    auto cards = liveCards();
    if (cards.empty()) line("target: no card board is linked");
    else if (cards.size() == 1) line("target: the card board on node " + std::to_string(nodeOf(cards[0].id)));
    else {
        std::string nodes;
        for (const auto& b : cards) nodes += (nodes.empty() ? "" : ", ") + std::to_string(nodeOf(b.id));
        line("target: card boards on nodes " + nodes + " (@HANDBACK GO <node> names one)");
    }
    line("callers online: " + std::to_string(callersOn()));
    if (job.running) {
        std::string at = job.phase == Phase::Check
                             ? "checking the card, " + std::to_string(job.checkAt) + " of " + std::to_string(job.checkList.size())
                         : job.phase == Phase::Work
                             ? (cur.active ? "copying " + cur.path + ", " + std::to_string(cur.off) + " of " +
                                                 std::to_string(cur.size) + " bytes"
                                           : "removing and copying")
                             : "finishing";
        line("running to node " + std::to_string(job.node) + ": pass " + std::to_string(job.passNo) + " of " +
             std::to_string(kPasses) + ", " + at + "; " + std::to_string(job.copiedN) + " copied, " +
             std::to_string(job.removedN) + " removed" + (job.stopAsked ? " (stopping)" : ""));
    }
    for (const std::string& r : lastResult) line(r);
}

bool handbackGo(const std::string& nodeArg, std::string& out) {
    auto no = [&](const std::string& why) {
        out += "axis: handback refused: " + why + "\n";
        return false;
    };
    if (axisProfile() != Profile::MirrorMain) return no("this computer is not main from a takeover");
    if (job.running) return no("a hand-back is already running (@HANDBACK STOP stops it)");
    Cluster* c = Cluster::instance();
    if (!c || c->role() != Role::Main) return no("this computer is not the cluster's main right now");
    auto cards = liveCards();
    if (cards.empty()) return no("no card board is linked");
    std::string board;
    if (!nodeArg.empty()) {
        char* end = nullptr;
        long  n = std::strtol(nodeArg.c_str(), &end, 10);
        if (nodeArg.empty() || *end || n <= 0) return no("@HANDBACK GO takes a node number");
        for (const auto& b : cards)
            if (nodeOf(b.id) == (int)n) board = b.id;
        if (board.empty()) return no("node " + nodeArg + " is not a linked card board");
    } else if (cards.size() > 1) {
        return no("more than one card board is linked: name its node (@HANDBACK GO <node>)");
    } else {
        board = cards[0].id;
    }
    std::string why;
    knownLoaded = false;  // (read again: it may have been put right by hand since)
    if (!loadKnown(why)) return no(why);
    if (!pace) pace.reset(new Pace((uint32_t)hostConfInt("mirror_rate", 65536, 1024, 1000000000L)));
    job = Job();
    job.running = true;
    job.board = board;
    job.node = nodeOf(board);
    job.base = logSnapshot();
    job.phase = Phase::Plan;
    closeCur();
    lastResult.clear();
    {
        std::lock_guard<std::mutex> lk(mu);
        fresh.clear();
        collecting = true;
    }
    std::printf("axis: handback: started to the card board on node %d: %zu paths noted since the takeover\n", job.node,
                job.base.size());
    out += "axis: handback started to the card board on node " + std::to_string(job.node) + ": " +
           std::to_string(job.base.size()) + " paths; every file is checked on the card first (@HANDBACK shows how it goes)\n";
    return true;
}

bool handbackStop(std::string& out) {
    if (!job.running) {
        out += "axis: handback: none is running\n";
        return false;
    }
    job.stopAsked = true;
    out += "axis: handback: stopping after the file in hand (this computer stays main)\n";
    return true;
}
