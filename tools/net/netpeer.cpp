// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// axis_netpeer: a Sysop Net node on the PC, for testing a board against something other than a board.
// It is the board's own NetCore and NetSession over a raw TCP socket, so it speaks exactly the board's
// protocol, with a FileMessageStore and a file outbound log in its folder.
//
//   axis_netpeer listen <port> --dir <folder> [--seed <file>] [--name <name>] [--host <host>]
//   axis_netpeer dial <host:port[@board]> --dir <folder> [--seed <file>] [--name <name>]
//
// Commands (typed or piped on stdin), one per line:
//   post <area> <subject> <body>   area is sn_support or just support; a "quoted subject" may hold spaces;
//                                  \n in the body is a line break
//   peers                          known boards and link peers
//   sync                           dial now (dial mode)
//   list <area>                    posts in an area
//   user on|off                    carry the four User Net areas (and say net2 on the card), or stop
//   apply <blurb>                  apply to User Net (needs user on); \n in the blurb is a line break
//   vote <board> y|n [comment]     vote on an applicant (this node must be a member)
//   apps                           the applications held, with their counts
//   members                        the User Net members this node counts
//   share <folder>                 serve the files in a folder as this node's Net Files area "pc" (kept in share.txt)
//   list <file> [desc]             list a file of the shared folder (needs user on and a member). A first word that
//                                  is an area (support, announce, updates, bugs) still lists that area's posts
//   unlist <file>                  remove this node's listing of a file
//   files                          every live Net Files listing this node holds (its own marked)
//   fetch <id> <outfile>           fetch a listed file from its home board into a file (one fetch at a time)
//   quit
//
// Without --seed the node makes its own seed in <dir>/seed.txt the first time and keeps it. The seed is
// never printed. Everything else lives in <dir> too: msgs/, outlog.txt, state.txt, seen.txt,
// cursors.txt, card.txt, postseq.txt, appseq.txt, voteseq.txt, user.txt, apps.txt (the signed User Net
// applications and votes), files.idx (the Net Files listing index), share.txt (the shared folder) and (dial
// mode) firstpeer.txt.
//
// Net Files: while it runs it answers AXISFILE 1 fetch calls for files it listed (the board's FetchServer);
// fetch dials a home board's card host and port with the board's FetchClient.
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "bbs/filelib.h"
#include "bbs/msgbase.h"
#include "net/areas.h"
#include "net/crc.h"
#include "net/fetch.h"
#include "net/outlog.h"
#include "net/record.h"
#include "net/session.h"
#include "net/sign.h"
#include "net/state.h"
#include "platform/pc/tcpstream.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>
#else
#include <fcntl.h>
#include <poll.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;
using namespace axis::net;

namespace {

void say(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    std::vfprintf(stdout, fmt, ap);
    va_end(ap);
    std::fputc('\n', stdout);
    std::fflush(stdout);  // piped stdout is fully buffered: every event goes out now
}

void wipe(void* p, size_t n) {
    volatile uint8_t* v = static_cast<volatile uint8_t*>(p);
    while (n--) *v++ = 0;
}

bool randomBytes(uint8_t* out, size_t n) {
#ifdef _WIN32
    return BCryptGenRandom(nullptr, out, (ULONG)n, BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0;
#else
    FILE* f = std::fopen("/dev/urandom", "rb");
    if (!f) return false;
    bool ok = std::fread(out, 1, n, f) == n;
    std::fclose(f);
    return ok;
#endif
}

// Creates path only if it does not exist and writes the bytes (owner-only where the OS has modes).
bool writeNew(const std::string& path, const char* data, size_t len) {
#ifdef _WIN32
    int fd = _open(path.c_str(), _O_WRONLY | _O_CREAT | _O_EXCL | _O_BINARY, _S_IREAD | _S_IWRITE);
    if (fd < 0) return false;
    bool ok = _write(fd, data, (unsigned)len) == (int)len;
    return _close(fd) == 0 && ok;
#else
    int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (fd < 0) return false;
    bool ok = write(fd, data, len) == (ssize_t)len;
    return close(fd) == 0 && ok;
#endif
}

std::string readFile(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return "";
    std::ostringstream s;
    s << f.rdbuf();
    return s.str();
}

// Writes a temp file, then puts it in place of the old one.
bool writeFile(const fs::path& p, const std::string& text) {
    fs::path tmp = p;
    tmp += ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f << text;
        if (!f.flush()) return false;
    }
    std::error_code ec;
    fs::rename(tmp, p, ec);
    return !ec;
}

// The seed from a file of 64 hex characters. False (and nothing kept) if it is not one.
bool loadSeed(const std::string& path, uint8_t seed[32]) {
    std::string t = readFile(path);
    while (!t.empty() && (t.back() == '\n' || t.back() == '\r' || t.back() == ' ')) t.pop_back();
    bool ok = t.size() == 64 && fromHex(t, seed, 32);
    wipe(&t[0], t.size());
    return ok;
}

// The node's own seed in <dir>/seed.txt, made the first time.
bool ownSeed(const fs::path& dir, uint8_t seed[32]) {
    std::string path = (dir / "seed.txt").string();
    if (fs::exists(path)) return loadSeed(path, seed);
    if (!randomBytes(seed, 32)) return false;
    std::string h = toHex(seed, 32) + "\n";
    bool ok = writeNew(path, h.data(), h.size());
    wipe(&h[0], h.size());
    if (ok) say("made a new seed in %s", path.c_str());
    return ok;
}

// The outbound log's file, held in RAM as lines too (the peer's log is small).
struct LogFile {
    fs::path path;
    std::vector<std::string> lines;
    void load() {
        lines.clear();
        std::istringstream in(readFile(path));
        std::string l;
        while (std::getline(in, l)) {
            if (!l.empty() && l.back() == '\r') l.pop_back();
            lines.push_back(l);
        }
    }
    FileOutLog::Io io() {
        FileOutLog::Io io;
        io.append = [this](const std::string& line) {
            std::ofstream f(path, std::ios::binary | std::ios::app);
            if (!(f << line << '\n')) return false;
            lines.push_back(line);
            return true;
        };
        io.readLine = [this](uint32_t n, std::string& line) {
            if (n >= lines.size()) return false;
            line = lines[n];
            return true;
        };
        io.lineCount = [this] { return (uint32_t)lines.size(); };
        io.replace = [this](const std::string& header, uint32_t from) {
            std::vector<std::string> next{header};
            for (size_t i = from; i < lines.size(); i++) next.push_back(lines[i]);
            std::string text;
            for (const auto& l : next) text += l + "\n";
            if (!writeFile(path, text)) return false;
            lines.swap(next);
            return true;
        };
        return io;
    }
};

uint32_t unixNow() { return (uint32_t)std::time(nullptr); }
uint32_t msNow() {
    static const auto t0 = std::chrono::steady_clock::now();
    return (uint32_t)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
}

std::string nonceHex() {
    uint8_t b[8];
    if (!randomBytes(b, sizeof b)) {
        uint64_t t = (uint64_t)std::chrono::high_resolution_clock::now().time_since_epoch().count();
        std::memcpy(b, &t, 8);
    }
    return toHex(b, sizeof b);
}

std::string areaId(const std::string& a) {
    std::string id = a.compare(0, 3, "sn_") == 0 ? a : "sn_" + a;
    return sysopNetArea(id) ? id : "";
}

// Splits off the first word, or a "quoted phrase".
std::string word(std::string& rest) {
    size_t i = rest.find_first_not_of(' ');
    if (i == std::string::npos) {
        rest.clear();
        return "";
    }
    rest.erase(0, i);
    std::string w;
    if (rest[0] == '"') {
        size_t e = rest.find('"', 1);
        if (e == std::string::npos) e = rest.size();
        w = rest.substr(1, e - 1);
        rest.erase(0, e < rest.size() ? e + 1 : e);
    } else {
        size_t e = rest.find(' ');
        w = rest.substr(0, e);
        rest.erase(0, e == std::string::npos ? rest.size() : e);
    }
    size_t j = rest.find_first_not_of(' ');
    rest.erase(0, j == std::string::npos ? rest.size() : j);
    return w;
}

bool isBoardId(const std::string& s) {
    return s.size() == 16 && s.find_first_not_of("0123456789abcdef") == std::string::npos;
}

// A field from another board, shown with control bytes as '?'.
std::string safe(const std::string& s) {
    std::string o = s;
    for (char& c : o)
        if ((unsigned char)c < 0x20 || c == 0x7f) c = '?';
    return o;
}

// Printable text, at most max bytes (line breaks kept only if keepNl).
std::string field(const std::string& s, size_t max, bool keepNl) {
    std::string o;
    for (char c : s)
        if (((unsigned char)c >= 0x20 && c != 0x7f) || (keepNl && c == '\n'))
            if (o.size() < max) o.push_back(c);
    return o;
}

// The line starting at pos (no line break), pos moved past it. A line over a frame comes back empty. False at the end.
bool nextLine(std::istream& f, uint32_t& pos, std::string& line) {
    line.clear();
    bool any = false, over = false;
    for (int c; (c = f.get()) != EOF;) {
        any = true;
        pos++;
        if (c == '\n') break;
        if (line.size() > kNetMaxFrame) over = true;
        if (!over) line.push_back((char)c);
    }
    if (over) std::string().swap(line);
    if (!line.empty() && line.back() == '\r') line.pop_back();
    return any;
}

constexpr uint32_t kDenyWaitDays = 30;  // as on the board: a denied board waits before applying again
constexpr const char* kShareArea = "pc";  // the Net Files area this node lists its shared folder under
constexpr const char* kFetchHandle = "netpeer";

// What one inbound fetch call turned out to be (for the log line when it ends).
struct ServeNote {
    std::string id, from;
    bool proven = false, served = false;
};

class Peer {
public:
    Peer(const fs::path& dir, const KeyPair& key, std::string name, std::string host, int port)
        : dir_(dir), files_((dir / "msgs").string()), key_(key), name_(std::move(name)), host_(std::move(host)),
          port_(port), logFile_{dir / "outlog.txt", {}}, log_(logFile_.io()),
          core_(files_, key_, unixNow, log_) {}

    bool start() {
        fs::create_directories(dir_ / "msgs");
        logFile_.load();
        if (!log_.open()) {
            say("outlog.txt could not be read");
            return false;
        }
        uint32_t day = unixNow() / 86400;
        core_.state().load(readFile(dir_ / "state.txt"), day);
        core_.seen().load(readFile(dir_ / "seen.txt"));
        core_.loadCursors(readFile(dir_ / "cursors.txt"));  // before the first peer is set
        userOn_ = readFile(dir_ / "user.txt").compare(0, 1, "1") == 0;
        core_.setAppsFile([this](const std::string& l, uint32_t& off) { return appsAppend(l, off); },
                          [this](const NetCore::AppsLineFn& each) { appsScan(each); },
                          [this](uint32_t off, std::string& l) { return appsReadAt(off, l); });
        loadApps();
        core_.needHistory();  // a missing history flag is decided before any session adds posts (saved below)
        // The post counter, as the board keeps it: the next seq is saved before a post is stored, and with no
        // saved counter and a set clock it starts from the clock.
        std::string seqText = readFile(dir_ / "postseq.txt");
        core_.setPostSeq(netSeqStart((uint32_t)std::strtoul(seqText.c_str(), nullptr, 10), !seqText.empty(), unixNow()));
        core_.setSeqReserve([this](uint32_t next) { return writeFile(dir_ / "postseq.txt", std::to_string(next) + "\n"); });
        std::string as = readFile(dir_ / "appseq.txt"), vs = readFile(dir_ / "voteseq.txt");
        appSeq_ = netSeqStart((uint32_t)std::strtoul(as.c_str(), nullptr, 10), !as.empty(), unixNow());
        voteSeq_ = netSeqStart((uint32_t)std::strtoul(vs.c_str(), nullptr, 10), !vs.empty(), unixNow());
        core_.setOnEvent([](const std::string& who, const std::string& what, const std::string&) {
            say("event: %s %s", who.empty() ? "local" : who.c_str(), what.c_str());  // never the signed line
        });
        // The listing index (files.idx), the same ranged reads and writes the board does; the key is drawn from the OS.
        core_.setListingIndex(
            [this](uint32_t off, void* p, size_t n) {
                std::ifstream f(dir_ / "files.idx", std::ios::binary);
                if (!f) return false;
                f.seekg((std::streamoff)off);
                f.read(static_cast<char*>(p), (std::streamsize)n);
                return (size_t)f.gcount() == n;
            },
            [this](uint32_t off, const void* p, size_t n) {
                fs::path path = dir_ / "files.idx";
                std::fstream f(path, std::ios::binary | std::ios::in | std::ios::out);
                if (!f) f.open(path, std::ios::binary | std::ios::in | std::ios::out | std::ios::trunc);  // first use
                if (!f) return false;
                f.seekp((std::streamoff)off);
                f.write(static_cast<const char*>(p), (std::streamsize)n);
                return (bool)f.flush();
            },
            [](void* p, size_t n) {
                if (!randomBytes(static_cast<uint8_t*>(p), n)) {  // (no random source: the clock, better than nothing for a bench tool)
                    uint64_t t = (uint64_t)std::chrono::high_resolution_clock::now().time_since_epoch().count();
                    for (size_t i = 0; i < n; i++) static_cast<uint8_t*>(p)[i] = (uint8_t)(t >> ((i % 8) * 8)) ^ (uint8_t)(i * 31);
                }
            });
        indexSteps();
        {
            std::string sh = readFile(dir_ / "share.txt");
            while (!sh.empty() && (sh.back() == '\n' || sh.back() == '\r')) sh.pop_back();
            if (!sh.empty()) share_ = sh;
        }
        applyAreas();
        if (!makeCard()) return false;
        wasMember_ = core_.selfMember();
        say("board %s (%s)", core_.board().c_str(), name_.c_str());
        core_.trim(unixNow());
        save();
        return true;
    }

    NetCore& core() { return core_; }
    const std::string& board() const { return core_.board(); }

    void setFirstPeer(const std::string& board) {
        if (board.empty()) return;
        core_.setFirstPeer(board);
    }

    void save() {
        bool ok = writeFile(dir_ / "state.txt", core_.state().save()) && writeFile(dir_ / "seen.txt", core_.seen().save()) &&
                  writeFile(dir_ / "cursors.txt", core_.saveCursors());  // (postseq.txt is written as each post is made)
        if (!ok) say("warning: saving the net state failed");
    }

    bool post(const std::string& area, const std::string& subject, const std::string& body) {
        axis::bbs::Message m;
        m.from = "Sysop";
        m.to = "All";
        m.subject = subject;
        m.body = body;
        int num = 0;
        std::string id = core_.originate(area, m, &num);
        save();
        if (id.empty()) return false;
        say("posted %s in %s as #%d", id.c_str(), area.c_str(), num);
        return true;
    }

    void list(const std::string& area) {
        auto h = files_.headers(area);
        say("%s: %u posts", area.c_str(), (unsigned)h.size());
        for (const auto& m : h) {
            std::string id = netPostId(m.net);
            const char* tag = id.empty() ? "" : core_.state().killed(id) ? " [killed]" : core_.state().held(id) ? " [held]" : "";
            say("  #%d %s | %s | from %s | %s%s", m.num, m.date.c_str(), m.subject.c_str(), m.from.c_str(), id.c_str(), tag);
        }
    }

    void peers() {
        say("cards:");
        core_.state().forEachCard([this](const NetCard& c) {
            say("  %s %s %s:%d%s%s", c.board.c_str(), c.name.c_str(), c.host.c_str(), c.port, c.board == board() ? " (this node)" : "",
                core_.isLinkPeer(c.board) ? " (link peer)" : "");
        });
        std::string cur = core_.saveCursors();
        say("link peers:%s", cur.empty() ? " none" : "");
        std::istringstream in(cur);
        std::string l;
        while (std::getline(in, l)) say("  %s", l.c_str());
    }

    // user on|off: carries all four User Net areas and says net2 on the card (kept in user.txt).
    bool setUser(bool on) {
        if (!writeFile(dir_ / "user.txt", on ? "1\n" : "0\n")) {
            say("user.txt could not be written");
            return false;
        }
        bool was = userOn_;
        userOn_ = on;
        applyAreas();
        if (on && !was) core_.resetUserHistory();  // a new area carried: the User Net pull is asked for again
        if (!makeCard()) return false;
        save();
        say("User Net %s (%u areas carried)", on ? "on" : "off", (unsigned)core_.userAreas().size());
        return true;
    }

    // After a command or a turn: this node became a member, so it asks for the User Net pull again.
    void userTick() {
        bool member = core_.selfMember();
        if (member && !wasMember_) {
            core_.resetUserHistory();
            say("this node is now a User Net member");
            save();
        }
        wasMember_ = member;
    }

    void apply(const std::string& blurbText) {
        using S = NetMembers::Status;
        S st = core_.members().status(core_.board());
        const char* no = !userOn_ ? "user is off (user on first)" : st == S::Member ? "already a member" : st == S::Pending ? "already applied" :
                         st == S::Vetoed ? "vetoed" : nullptr;
        if (!no && st == S::Denied && unixNow() > kNetClockSet) {
            uint32_t d = core_.state().memberNoticeDay(core_.board());
            if (d < kNetClockDay) d = unixNow() / 86400;
            if (unixNow() / 86400 < d + kDenyWaitDays) no = "denied less than 30 days ago";
        }
        if (no) {
            say("apply refused: %s", no);
            return;
        }
        NetApp a;
        a.pub = toHex(key_.pub, 32);
        a.board = core_.board();
        a.name = field(name_, 36, false);
        a.sysop = "PC peer";
        a.location = "PC";
        a.blurb = field(blurbText, kNetMaxBlurb, true);
        a.date = unixNow();
        a.seq = appSeq_;
        if (!writeFile(dir_ / "appseq.txt", std::to_string(a.seq + 1) + "\n")) {  // saved before it is used
            say("appseq.txt could not be written");
            return;
        }
        appSeq_ = a.seq + 1;
        if (!signApp(a, key_) || !core_.takeApp(a, "")) {
            say("apply not taken");
            return;
        }
        save();
        say("applied as %s-a%u", a.board.c_str(), (unsigned)a.seq);
    }

    void vote(const std::string& board, const std::string& yn, const std::string& comment) {
        uint32_t off, appSeq = 0;
        const char* no = !core_.selfMember() ? "this node is not a member" :
                         !isBoardId(board) || (yn != "y" && yn != "n") ? "usage: vote <board id> y|n [comment]" :
                         board == core_.board() ? "not on this node's own application" :
                         !core_.members().heldApp(board, off, &appSeq) ? "no application held from that board" : nullptr;
        if (no) {
            say("vote refused: %s", no);
            return;
        }
        NetVote v;
        v.voter = core_.board();
        v.pub = toHex(key_.pub, 32);
        v.app = board;
        v.appSeq = appSeq;
        v.seq = voteSeq_;
        v.date = unixNow();
        v.yes = yn == "y";
        v.comment = field(comment, kNetMaxComment, false);
        if (!writeFile(dir_ / "voteseq.txt", std::to_string(v.seq + 1) + "\n")) {
            say("voteseq.txt could not be written");
            return;
        }
        voteSeq_ = v.seq + 1;
        if (!signVote(v, key_) || !core_.takeVote(v, "")) {
            say("vote not taken");
            return;
        }
        save();
        say("voted %s on %s", v.yes ? "yes" : "no", board.c_str());
    }

    // One line per held application: board | name | sysop | location | mac | status | yes n no n | you yes|no|-
    void apps() {
        const NetMembers& mem = core_.members();
        const NetState& st = core_.state();
        std::vector<std::string> in = mem.members(), dn = mem.denied();
        std::string line;
        bool any = false;
        for (uint32_t off : mem.offsets()) {
            NetApp a;
            NetVote v;
            uint32_t vo;
            if (!appsReadAt(off, line)) continue;
            if (line[0] != 'A') break;  // offsets() lists the applications before the votes
            if (!decode(line, a)) continue;
            const NetNotice* n = st.memberNotice(a.board);
            NetMembers::Tally t = mem.tally(a.board);
            const char* is = st.banned(a.board) ? "banned" : has(in, a.board) ? "member" : has(dn, a.board) ? "denied" :
                             n && n->kind == "veto" ? "vetoed" : "pending";
            const char* mine = mem.heldVote(core_.board(), a.board, vo) && appsReadAt(vo, line) && decode(line, v) && v.appSeq == a.seq
                                   ? (v.yes ? "yes" : "no") : "-";
            say("%s | %s | %s | %s | %s | %s | yes %u no %u | you %s", a.board.c_str(), safe(a.name).c_str(), safe(a.sysop).c_str(),
                safe(a.location).c_str(), safe(a.mac).c_str(), is, (unsigned)t.yes, (unsigned)t.no, mine);
            any = true;
        }
        if (!any) say("no applications held");
    }

    void members() {
        for (const std::string& b : core_.members().members()) {
            const NetCard* c = core_.state().card(b);
            say("%s %s%s", b.c_str(), c ? safe(c->name).c_str() : "-", core_.isRevBoard(b) ? " (Rev)" : "");
        }
    }

    // ---- Net Files

    // The listing index looks at its head or builds a piece of itself; called until it is ready, and from the loop.
    void indexSteps() {
        for (int i = 0; i < 100000 && !core_.listingIndexReady() && core_.listingIndexStep(); i++) {
        }
    }

    // share <folder>: the files in it are served as area "pc" (only the ones listed are offered).
    void share(const std::string& d) {
        std::error_code ec;
        if (d.empty()) {
            if (share_.empty()) say("nothing is shared (share <folder>)");
            else say("sharing %s as area %s", share_.string().c_str(), kShareArea);
            return;
        }
        if (!fs::is_directory(d, ec)) {
            say("share refused: %s is not a folder", d.c_str());
            return;
        }
        share_ = fs::absolute(d, ec);
        if (!writeFile(dir_ / "share.txt", share_.string() + "\n")) say("warning: share.txt could not be written");
        unsigned n = 0;
        for (const auto& e : fs::directory_iterator(share_, ec))
            if (e.is_regular_file(ec) && axis::bbs::safeFileName(e.path().filename().string())) n++;
        say("sharing %u files as area %s (list <file> to offer one)", n, kShareArea);
    }

    // list <file> [desc]: signs and stores this node's listing of a file in the shared folder.
    void listFile(const std::string& name, const std::string& desc) {
        std::error_code ec;
        if (share_.empty()) {
            say("list refused: share a folder first (share <folder>)");
            return;
        }
        fs::path p = share_ / name;
        if (!axis::bbs::safeFileName(name) || !fs::is_regular_file(p, ec)) {
            say("list refused: no file %s in the shared folder", name.c_str());
            return;
        }
        uint64_t size = fs::file_size(p, ec);
        if (ec || size > 0xFFFFFFFFull) {
            say("list refused: %s is too big", name.c_str());
            return;
        }
        std::ifstream f(p, std::ios::binary);
        uint32_t crc = 0xFFFFFFFFu;
        char buf[4096];
        uint64_t got = 0;
        while (f.read(buf, sizeof buf) || f.gcount() > 0) {
            crc = crc32Update(crc, (const uint8_t*)buf, (size_t)f.gcount());
            got += (uint64_t)f.gcount();
        }
        if (got != size) {
            say("list refused: %s could not be read", name.c_str());
            return;
        }
        NetListing l;
        l.area = kShareArea;
        l.name = name;
        l.desc = field(desc, kNetListDesc, false);
        l.uploader = "Sysop";
        l.reach = "i";
        l.size = (uint32_t)size;
        l.crc = ~crc;
        std::string id = core_.originateListing(l);
        save();
        if (id.empty()) say("list refused: this node must be a member carrying the Net Files area (user on, then a member), or a field is bad");
        else say("listed %s as %s (%u bytes)", name.c_str(), id.c_str(), (unsigned)size);
    }

    // unlist <file>: the removal (a tombstone) of this node's live listing of that name.
    void unlistFile(const std::string& name) {
        bool have = false;
        for (int skip = 0; skip >= 0;)
            skip = core_.listingsPage(skip, 50, [&](const NetListing& l) {
                if (l.board == core_.board() && l.area == kShareArea && l.name == name) have = true;
            });
        if (!have) {
            say("unlist refused: no live listing of %s here", name.c_str());
            return;
        }
        NetListing l;
        l.area = kShareArea;
        l.name = name;
        l.uploader = "Sysop";
        l.reach = "i";
        l.gone = true;
        std::string id = core_.originateListing(l);
        save();
        if (id.empty()) say("unlist refused");
        else say("unlisted %s (removal %s)", name.c_str(), id.c_str());
    }

    // files: the live listings held, newest first: id | home | area/name | bytes | description
    void files() {
        unsigned n = 0;
        for (int skip = 0; skip >= 0;)
            skip = core_.listingsPage(skip, 50, [&](const NetListing& l) {
                const NetCard* c = core_.state().card(l.board);
                std::string home = l.board == core_.board() ? std::string("this node") : c ? safe(c->name) : l.board;
                say("  %s | %s | %s/%s | %u | %s", listingId(l).c_str(), home.c_str(), safe(l.area).c_str(), safe(l.name).c_str(),
                    (unsigned)l.size, safe(l.desc).c_str());
                n++;
            });
        say("%u files listed%s", n, share_.empty() ? "" : " (a folder is shared)");
    }

    // check (FetchServer): who is asking, then the listing and the file. Nothing lasting beyond opening the file.
    ServeFile checkFetch(const NetFetch& f, ServeNote& note) {
        ServeFile r;
        note.id = f.listing;
        note.from = f.board;
        const NetCard* card = core_.state().card(f.board);
        if (!card || !verifyFetch(f, card->pub)) {
            r.no = "sig";
            return r;
        }
        note.proven = true;
        NetListing l;
        std::error_code ec;
        std::shared_ptr<std::ifstream> file;
        if (core_.state().banned(f.board) || core_.blockedHere(f.board)) r.no = "banned";
        else if (core_.members().status(f.board) != NetMembers::Status::Member) r.no = "member";
        else if (!core_.carries(kNetFilesUserArea) || !core_.selfMember() || share_.empty() || !core_.findListing(f.listing, l) || l.board != core_.board() || l.gone || l.area != kShareArea ||
                 !axis::bbs::safeFileName(l.name) || !fs::is_regular_file(share_ / l.name, ec))
            r.no = "gone";
        else if (fs::file_size(share_ / l.name, ec) != l.size || ec) r.no = "size";
        else if (!(file = std::make_shared<std::ifstream>(share_ / l.name, std::ios::binary)) || !*file) r.no = "gone";
        else {
            r.ok = true;
            r.size = l.size;
            r.read = [file](uint32_t off, uint8_t* buf, size_t n) {
                file->clear();
                file->seekg((std::streamoff)off);
                file->read(reinterpret_cast<char*>(buf), (std::streamsize)n);
                return (size_t)file->gcount() == n;
            };
            note.served = true;
        }
        return r;
    }

    // What a fetch needs from the node: the listing, its home board's card, and the way to dial it.
    struct FetchPlan {
        NetListing l;
        NetCard home;
    };
    bool planFetch(const std::string& id, FetchPlan& out) {
        if (!core_.findListing(id, out.l)) {
            say("fetch refused: no live listing %s (files lists them)", id.c_str());
            return false;
        }
        if (out.l.board == core_.board()) {
            say("fetch refused: that listing is this node's own");
            return false;
        }
        const NetCard* c = core_.state().card(out.l.board);
        if (!c || c->host.empty() || c->port <= 0 || out.l.reach.find('i') == std::string::npos) {
            say("fetch refused: that board can't be reached (no host on its card)");
            return false;
        }
        out.home = *c;
        return true;
    }

    const NetCard& card() const { return card_; }
    const KeyPair& key() const { return key_; }

private:
    static bool has(const std::vector<std::string>& v, const std::string& b) { return std::find(v.begin(), v.end(), b) != v.end(); }

    // The areas carried follow user on|off: all four, or none.
    void applyAreas() {
        std::vector<std::string> v;
        if (userOn_)
            for (const NetArea& a : userNetAreas()) v.push_back(a.id);
        core_.setUserAreas(std::move(v));
    }

    // apps.txt: the signed applications and votes, one line each, read back a line at a time (as /net/apps.txt on a board).
    bool appsAppend(const std::string& line, uint32_t& off) {
        fs::path p = dir_ / "apps.txt";
        std::error_code ec;
        uint64_t at = fs::exists(p, ec) ? (uint64_t)fs::file_size(p, ec) : 0;
        std::string pre;
        if (at > 0) {  // a torn append: end it first
            std::ifstream f(p, std::ios::binary);
            f.seekg((std::streamoff)at - 1);
            if (f.get() != '\n') {
                pre = "\n";
                at++;
            }
        }
        std::ofstream f(p, std::ios::binary | std::ios::app);
        if (!(f << pre << line << '\n') || !f.flush()) return false;
        off = (uint32_t)at;
        return true;
    }
    void appsScan(const NetCore::AppsLineFn& each) {
        std::ifstream f(dir_ / "apps.txt", std::ios::binary);
        uint32_t pos = 0;
        std::string line;
        while (nextLine(f, pos, line))
            if (!line.empty() && !each(line)) break;
    }
    bool appsReadAt(uint32_t off, std::string& line) {
        std::ifstream f(dir_ / "apps.txt", std::ios::binary);
        if (!f) return false;
        f.seekg((std::streamoff)off);
        uint32_t pos = off;
        return nextLine(f, pos, line) && !line.empty();
    }
    void loadApps() {
        NetMembers& mem = core_.members();
        std::ifstream f(dir_ / "apps.txt", std::ios::binary);
        mem.beginLoad();
        uint32_t pos = 0;
        std::string line;
        for (;;) {
            uint32_t at = pos;
            if (!nextLine(f, pos, line)) break;
            if (!line.empty()) mem.loadLine(line, at);
        }
        mem.endLoad();
    }

    // The card held in the state when nothing changed, else a new one one past any seq used before.
    bool makeCard() {
        NetCard c;
        c.pub = toHex(key_.pub, 32);
        c.board = boardId(c.pub);
        c.name = name_.substr(0, 40);
        c.host = host_.substr(0, 64);
        c.port = port_;
        c.mac = "";
        c.sysop = "PC peer";
        c.location = "PC";
        c.links = userOn_ ? kNetCardLinks : "internet";  // net2: a Phase 2 board, peers send it UWANT; net3: listings
        const NetCard* held = core_.state().card(c.board);
        if (held && held->name == c.name && held->host == c.host && held->port == c.port && held->sysop == c.sysop &&
            held->location == c.location && held->links == c.links && held->mac == c.mac) {
            card_ = *held;
            return true;
        }
        std::string savedText = readFile(dir_ / "card.txt");
        uint32_t saved = (uint32_t)std::strtoul(savedText.c_str(), nullptr, 10);
        if (savedText.empty() && unixNow() > kNetClockSet) saved = std::max(saved, unixNow());  // a lost counter: the clock
        c.seq = std::max(saved, held ? held->seq : 0u) + 1;
        if (!writeFile(dir_ / "card.txt", std::to_string(c.seq) + "\n")) {  // saved before it is used
            say("card.txt could not be written");
            return false;
        }
        if (!signCard(c, key_) || !core_.setOwnCard(c)) {
            say("this node's card could not be made (check --name and --host)");
            return false;
        }
        card_ = c;
        return true;
    }

    fs::path dir_;
    axis::bbs::FileMessageStore files_;
    KeyPair key_;
    std::string name_, host_;
    int port_;
    LogFile logFile_;
    FileOutLog log_;
    NetCore core_;
    NetCard card_;
    fs::path share_;
    bool userOn_ = false, wasMember_ = false;
    uint32_t appSeq_ = 1, voteSeq_ = 1;
};

// One live session over a raw socket.
struct Live {
    std::unique_ptr<axis::pc::TcpStream> sock;
    std::unique_ptr<NetSession> s;
    bool dialer = false;
};

// One turn: what arrived is fed, what the session says is sent, then it ticks. True while it runs.
bool pump(Peer& peer, Live& l, const std::string& firstPeerFile, bool firstUnknown) {
    std::string in = l.sock->takeAll();
    if (!in.empty()) l.s->feed(in.data(), in.size());
    std::string out = l.s->takeOut();
    if (!out.empty()) l.sock->write((const uint8_t*)out.data(), out.size());
    l.s->tick(msNow(), true);
    out = l.s->takeOut();
    if (!out.empty()) l.sock->write((const uint8_t*)out.data(), out.size());
    bool over = l.s->done() || l.s->failed();
    if (!over) {
        if (l.sock->carrier()) return true;
        // Closed: the last line and the close can land together, so what arrived with it is fed first.
        std::string last = l.sock->takeAll();
        if (!last.empty()) {
            l.s->feed(last.data(), last.size());
            l.s->tick(msNow(), true);
        }
        over = l.s->done() || l.s->failed();
    }
    out = l.s->takeOut();  // what it still has to say goes out before the close
    if (!out.empty()) l.sock->write((const uint8_t*)out.data(), out.size());
    const std::string who = l.s->peer().empty() ? std::string("?") : l.s->peer();
    if (l.s->done()) {
        say("synced with %s: %u out, %u in", who.c_str(), l.s->postsSent(), l.s->postsTaken());
        if (l.dialer && firstUnknown && !l.s->peer().empty()) {  // trust on first use: remember who answered
            writeFile(firstPeerFile, l.s->peer() + "\n");
            peer.setFirstPeer(l.s->peer());
        }
    } else {
        say("sync failed with %s (%s): %u out, %u in", who.c_str(), over ? l.s->why() : "connection closed",
            l.s->postsSent(), l.s->postsTaken());
    }
    peer.save();
    l.s.reset();
    l.sock.reset();
    return false;
}

// One inbound fetch call (AXISFILE 1): the board's FetchServer over a socket.
struct Serve {
    std::unique_ptr<axis::pc::TcpStream> sock;
    std::unique_ptr<FetchServer> s;
    std::unique_ptr<ServeNote> note;  // (the server's check points at it)
    std::string pend;                 // taken from the server, not yet taken by the socket
    FetchServeEnd end;                // once the call is over: pend goes out, then the half-close
    bool ending = false, closed = false;  // the call is over; it was over because the dialer closed
};

// Sends what the socket takes now; the rest stays in pend. Never waits (a slow caller must not hold the loop).
void flushServe(Serve& v) {
    size_t w = v.pend.empty() ? 0 : v.sock->writeSome((const uint8_t*)v.pend.data(), v.pend.size());
    v.pend.erase(0, w);
}

// One turn of a served call. True while it runs; false once it ended (and was logged). Once the server is done, the slot
// stays taken while pend goes out and, after the half-close, until the dialer closes (FetchServeEnd says how long).
bool pumpServe(Serve& v) {
    std::string in = v.sock->takeAll();
    if (!v.ending) {
        if (!in.empty()) v.s->feed(in.data(), in.size());
        in.clear();
        flushServe(v);
        for (int i = 0; i < 4 && v.pend.empty(); i++) {  // up to four pieces a turn, each taken only once the last is out
            v.pend = v.s->takeOut(kFetchChunk + 64);
            flushServe(v);
        }
        v.s->tick(msNow());
        bool end = v.s->failed() || v.s->finished();
        v.closed = !end && !v.sock->carrier();
        if (!end && !v.closed) return true;
        if (v.pend.empty()) v.pend = v.s->takeOut(256);  // a NO line still to go
        v.ending = true;
    }
    flushServe(v);
    if (v.sock->carrier()) {
        FetchServeEnd::Step st = v.end.step(msNow(), v.pend.size(), !in.empty());
        if (st == FetchServeEnd::Shut) v.sock->shutdownWrite();
        if (st != FetchServeEnd::Close) return true;
    }
    bool done = v.s->finished() && !v.s->failed() && v.note->served && v.pend.empty();
    // a whole file whose last piece never went out is not served: the dialer went (closed) or took nothing (stall)
    const char* word = v.closed || (!v.s->failed() && !v.pend.empty() && !v.sock->carrier()) ? "closed"
                       : !v.s->failed() && !v.pend.empty()                                   ? "stall"
                                                                                               : v.s->why();
    if (done) say("fetch served %s to board %s", v.note->id.c_str(), v.note->from.c_str());
    else say("fetch %s from %s %s (%s)", v.note->id.empty() ? "call" : v.note->id.c_str(), v.note->from.empty() ? "?" : v.note->from.c_str(),
             v.note->served ? "stopped" : "refused", word);
    v.s.reset();
    v.sock.reset();
    v.pend.clear();
    v.end = FetchServeEnd();
    v.ending = v.closed = false;
    return false;
}

// One outbound fetch: the board's FetchClient over a socket, written to a file as the bytes become readable.
struct Get {
    std::unique_ptr<axis::pc::TcpStream> sock;
    std::unique_ptr<FetchClient> f;
    std::ofstream out;
    fs::path path;
    std::string id, inbuf;
    uint32_t pos = 0, size = 0;
};

void endGet(Get& g, const char* fail) {
    g.out.close();
    std::error_code ec;
    fs::path part = g.path;
    part += ".part";
    if (!fail) {  // only a whole, checked file takes the name
        fs::remove(g.path, ec);
        fs::rename(part, g.path, ec);
        if (ec) fail = "rename";
    }
    if (fail) {
        fs::remove(part, ec);  // no half file left behind (an old file of that name stays)
        say("fetch %s failed (%s)", g.id.c_str(), fail);
    } else {
        say("fetched %s: %u bytes to %s (%s)", g.id.c_str(), (unsigned)g.pos, g.path.string().c_str(),
            g.f->verified() ? "crc ok" : "crc not checked");
    }
    g.f.reset();
    g.sock.reset();
}

// One turn of a fetch. True while it runs.
bool pumpGet(Get& g) {
    bool up = g.sock->carrier();
    g.inbuf += g.sock->takeAll();
    std::string o = g.f->takeOut();
    if (!o.empty()) g.sock->write((const uint8_t*)o.data(), o.size());
    while (!g.inbuf.empty()) {  // only what the window has room for; the rest waits
        size_t room = g.f->room();
        if (!room) break;
        size_t n = g.f->feed(g.inbuf.data(), std::min(room, g.inbuf.size()));
        if (!n) break;
        g.inbuf.erase(0, n);
    }
    g.f->tick(msNow());
    o = g.f->takeOut();  // the keepalive, or the GET after the HI
    if (!o.empty()) g.sock->write((const uint8_t*)o.data(), o.size());
    using St = FetchClient::St;
    if (g.f->state() == St::Failed) {
        endGet(g, g.f->why());
        return false;
    }
    if (g.f->state() == St::Streaming || g.f->state() == St::Done) {
        uint8_t buf[4096];
        while (g.pos < g.size) {  // in order, whatever has arrived whole
            size_t n = std::min<size_t>(sizeof buf, g.size - g.pos);
            if (!g.f->ready(g.pos, n)) break;
            if (!g.f->read(g.pos, buf, n) || !g.out.write((const char*)buf, (std::streamsize)n)) {
                endGet(g, "write");
                return false;
            }
            g.pos += (uint32_t)n;
        }
        if (g.pos >= g.size && g.f->state() == St::Done) {
            endGet(g, nullptr);
            return false;
        }
    }
    if (!up && g.inbuf.empty() && g.f->state() != St::Done) {  // closed before all of it came
        endGet(g, "closed");
        return false;
    }
    return true;
}

// The first bytes of a new connection (up to 11, or a line), waiting at most ms: tells a fetch call from a sync.
std::string sniff(axis::pc::TcpStream& c, int ms) {
    std::string got;
    for (int t = 0; t <= ms && got.size() < 11 && got.find('\n') == std::string::npos; t += 5) {
        got += c.takeAll();
        if (got.size() >= 11 || !c.carrier()) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    got += c.takeAll();
    return got;
}

std::mutex g_mu;
std::deque<std::string> g_cmds;

#ifdef _WIN32
// Windows cannot poll a console or pipe handle like a socket: a thread waits for the lines instead.
void readStdin() {
    std::string line;
    while (std::getline(std::cin, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::lock_guard<std::mutex> g(g_mu);
        g_cmds.push_back(line);
    }
}
#else
// Whatever stdin holds now, read without waiting (poll with no timeout), so the loop keeps answering calls
// and running a session while no line comes (stdin fed by tail -f, say). Complete lines become commands.
std::string g_in;
bool g_inEnd = false;

void pollStdin() {
    while (!g_inEnd) {
        pollfd p{};
        p.fd = 0;
        p.events = POLLIN;
        if (::poll(&p, 1, 0) <= 0 || !(p.revents & (POLLIN | POLLHUP | POLLERR))) return;
        char buf[1024];
        ssize_t n = ::read(0, buf, sizeof buf);
        if (n <= 0) {
            g_inEnd = true;  // end of input: a last line without a newline still counts
            if (!g_in.empty()) g_in += '\n';
        } else {
            g_in.append(buf, (size_t)n);
        }
        std::lock_guard<std::mutex> g(g_mu);
        for (size_t at; (at = g_in.find('\n')) != std::string::npos;) {
            std::string line = g_in.substr(0, at);
            g_in.erase(0, at + 1);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            g_cmds.push_back(line);
        }
    }
}
#endif

bool nextCommand(std::string& out) {
#ifndef _WIN32
    pollStdin();
#endif
    std::lock_guard<std::mutex> g(g_mu);
    if (g_cmds.empty()) return false;
    out = g_cmds.front();
    g_cmds.pop_front();
    return true;
}

int usage() {
    std::fprintf(stderr,
                 "usage: axis_netpeer listen <port> --dir <folder> [--seed <file>] [--name <name>] [--host <host>]\n"
                 "       axis_netpeer dial <host:port[@board]> --dir <folder> [--seed <file>] [--name <name>]\n");
    return 1;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) return usage();
    std::string mode = argv[1], target = argv[2], dirArg, seedArg, name = "AXIS netpeer", host;
    for (int i = 3; i + 1 < argc; i += 2) {
        std::string k = argv[i], v = argv[i + 1];
        if (k == "--dir") dirArg = v;
        else if (k == "--seed") seedArg = v;
        else if (k == "--name") name = v;
        else if (k == "--host") host = v;
        else return usage();
    }
    if ((argc - 3) % 2 != 0 || dirArg.empty() || (mode != "listen" && mode != "dial")) return usage();
    bool listening = mode == "listen";

    int port = 0;
    std::string dialHost, expect;
    if (listening) {
        port = std::atoi(target.c_str());
        if (port <= 0 || port > 65535) return usage();
        if (host.empty()) host = "127.0.0.1";
    } else {
        size_t at = target.find('@');
        if (at != std::string::npos) {
            expect = target.substr(at + 1);
            target.erase(at);
        }
        size_t colon = target.rfind(':');
        if (colon == std::string::npos) return usage();
        dialHost = target.substr(0, colon);
        port = std::atoi(target.c_str() + colon + 1);
        if (dialHost == "localhost") dialHost = "127.0.0.1";
        if (port <= 0 || port > 65535 || dialHost.empty()) return usage();
        if (!expect.empty() && (expect.size() != 16 || expect.find_first_not_of("0123456789abcdef") != std::string::npos)) {
            std::fprintf(stderr, "axis_netpeer: @board must be 16 lowercase hex characters\n");
            return 1;
        }
    }

    fs::path dir(dirArg);
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (!fs::is_directory(dir)) {
        std::fprintf(stderr, "axis_netpeer: cannot use folder %s\n", dirArg.c_str());
        return 1;
    }
    uint8_t seed[32];
    if (!(seedArg.empty() ? ownSeed(dir, seed) : loadSeed(seedArg, seed))) {
        std::fprintf(stderr, "axis_netpeer: no usable seed (a file of 64 hex characters)\n");
        return 1;
    }
    KeyPair key = keyFromSeed(seed);
    wipe(seed, sizeof seed);

    Peer peer(dir, key, name, listening ? host : std::string(), listening ? port : 0);
    wipe(&key, sizeof key);
    if (!peer.start()) return 1;

    std::string firstPeerFile = (dir / "firstpeer.txt").string();
    if (!listening) {
        if (expect.empty()) {
            std::string saved = readFile(firstPeerFile);
            while (!saved.empty() && (saved.back() == '\n' || saved.back() == '\r')) saved.pop_back();
            if (saved.size() == 16) expect = saved;
        }
        peer.setFirstPeer(expect);
        say("dial mode: %s:%d%s%s", dialHost.c_str(), port, expect.empty() ? " (board not known yet, trusted on first use)" : " expecting ",
            expect.c_str());
    }

    std::unique_ptr<axis::pc::TcpListener> listener;
    if (listening) {
        listener.reset(new axis::pc::TcpListener((uint16_t)port));
        say("listening on port %u", (unsigned)listener->port());
    }

#ifdef _WIN32
    std::thread(readStdin).detach();
#endif
    Live live;
    Serve serve;
    Get get;
    bool firstUnknown = false;
    for (;;) {
        std::string cmd;
        while (nextCommand(cmd)) {
            std::string rest = cmd, verb = word(rest);
            if (verb.empty()) continue;
            if (verb == "quit" || verb == "exit") {
                if (live.s) say("quitting with a session open");
                if (get.f) {
                    get.out.close();
                    std::error_code qec;
                    fs::remove(get.path.string() + ".part", qec);
                }
                peer.save();
                say("bye");
                std::fflush(stdout);
                std::_Exit(0);  // (on Windows the stdin reader thread may still be blocked in getline)
            } else if (verb == "post") {
                std::string area = areaId(word(rest)), subject = word(rest), body = rest;
                if (area.empty() || subject.empty()) {
                    say("usage: post <area> <subject> <body> (areas: support, announce, updates, bugs)");
                    continue;
                }
                std::string b;
                for (size_t i = 0; i < body.size(); i++) {
                    if (body[i] == '\\' && i + 1 < body.size() && body[i + 1] == 'n') {
                        b += '\n';
                        i++;
                    } else {
                        b += body[i];
                    }
                }
                if (!peer.post(area, subject, b)) say("post refused (Rev-only area, or a bad field)");
            } else if (verb == "list") {
                std::string first = word(rest), area = areaId(first);
                if (first.empty()) say("usage: list <area> (posts) or list <file> [desc] (Net Files)");
                else if (!area.empty() && rest.empty()) peer.list(area);
                else peer.listFile(first, rest);
            } else if (verb == "share") {
                peer.share(word(rest));
            } else if (verb == "unlist") {
                std::string name = word(rest);
                if (name.empty()) say("usage: unlist <file>");
                else peer.unlistFile(name);
            } else if (verb == "files") {
                peer.files();
            } else if (verb == "fetch") {
                std::string id = word(rest), outFile = word(rest);
                Peer::FetchPlan plan;
                if (id.empty() || outFile.empty()) {
                    say("usage: fetch <listing id> <outfile>");
                } else if (get.f) {
                    say("a fetch is already running");
                } else if (peer.planFetch(id, plan)) {
                    std::string h = plan.home.host == "localhost" ? "127.0.0.1" : plan.home.host;
                    std::ofstream o(outFile + ".part", std::ios::binary | std::ios::trunc);
                    if (!o) {
                        say("fetch refused: cannot write %s", outFile.c_str());
                        continue;
                    }
                    auto sock = axis::pc::TcpStream::dial(h.c_str(), (uint16_t)plan.home.port, false);
                    if (!sock) {
                        o.close();
                        std::error_code rec;
                        fs::remove(outFile + ".part", rec);
                        say("fetch failed: no answer from that board");
                        continue;
                    }
                    get.sock = std::move(sock);
                    get.out = std::move(o);
                    get.path = outFile;
                    get.id = id;
                    get.inbuf.clear();
                    get.pos = 0;
                    get.size = plan.l.size;
                    get.f.reset(new FetchClient(plan.l, plan.home.pub, kFetchHandle, peer.key(), peer.board(), nonceHex(), 0));
                    get.f->tick(msNow());  // its connect clock starts now
                    say("fetching %s (%u bytes) from %s", id.c_str(), (unsigned)plan.l.size, plan.l.board.c_str());
                }
            } else if (verb == "peers") {
                peer.peers();
            } else if (verb == "sync") {
                if (listening) {
                    say("listen mode: the other node dials in");
                } else if (live.s) {
                    say("a session is already running");
                } else {
                    live.sock = axis::pc::TcpStream::dial(dialHost.c_str(), (uint16_t)port, false);
                    if (!live.sock) {
                        say("no answer from %s:%d", dialHost.c_str(), port);
                        continue;
                    }
                    std::string first = readFile(firstPeerFile);
                    while (!first.empty() && (first.back() == '\n' || first.back() == '\r')) first.pop_back();
                    if (expect.empty() && first.size() == 16) expect = first;
                    firstUnknown = expect.empty();
                    live.s.reset(new NetSession(peer.core(), peer.card(), peer.key(), nonceHex(), true, expect));
                    live.dialer = true;
                    say("dialing %s:%d", dialHost.c_str(), port);
                }
            } else if (verb == "user") {
                std::string w = word(rest);
                if (w != "on" && w != "off") say("usage: user on|off");
                else if (live.s) say("a session is running (try again when it ends)");
                else peer.setUser(w == "on");
            } else if (verb == "apply") {
                std::string b;
                for (size_t i = 0; i < rest.size(); i++) {
                    if (rest[i] == '\\' && i + 1 < rest.size() && rest[i + 1] == 'n') {
                        b += '\n';
                        i++;
                    } else {
                        b += rest[i];
                    }
                }
                peer.apply(b);
            } else if (verb == "vote") {
                std::string board = word(rest), yn = word(rest);
                for (char& ch : yn) ch = (char)std::tolower((unsigned char)ch);
                peer.vote(board, yn, rest);
            } else if (verb == "apps") {
                peer.apps();
            } else if (verb == "members") {
                peer.members();
            } else if (verb == "help") {
                say("commands: post <area> <subject> <body> | peers | sync | list <area> | user on|off | apply <blurb> | "
                    "vote <board> y|n [comment] | apps | members | share <folder> | list <file> [desc] | unlist <file> | "
                    "files | fetch <id> <outfile> | quit");
            } else {
                say("unknown command: %s (try help)", verb.c_str());
            }
        }

        if (listener) {
            bool busy = live.s || serve.s;
            if (auto c = listener->accept(busy ? 0 : 20, false)) {
                std::string first = sniff(*c, 200);  // a fetch call (AXISFILE 1) or a sync (AXISNET 2)
                if (first.compare(0, 11, "AXISFILE 1 ") == 0) {
                    if (serve.s) {
                        const char no[] = "NO busy\r\n";
                        c->write((const uint8_t*)no, sizeof no - 1);
                    } else {
                        serve.note.reset(new ServeNote());
                        ServeNote* note = serve.note.get();
                        serve.sock = std::move(c);
                        serve.s.reset(new FetchServer(peer.board(), peer.key(), nonceHex(),
                                                      [&peer, note](const NetFetch& f) { return peer.checkFetch(f, *note); }));
                        serve.s->feed(first.data(), first.size());
                        say("a fetch call came in");
                    }
                } else if (live.s) {
                    const char bye[] = "BYE busy\n";
                    c->write((const uint8_t*)bye, sizeof bye - 1);
                } else {
                    live.sock = std::move(c);
                    live.s.reset(new NetSession(peer.core(), peer.card(), peer.key(), nonceHex(), false, std::string()));
                    if (!first.empty()) live.s->feed(first.data(), first.size());
                    live.dialer = false;
                    say("a board dialed in");
                }
            }
        }
        peer.userTick();
        peer.indexSteps();
        if (serve.s) pumpServe(serve);
        if (get.f) pumpGet(get);
        if (live.s) {
            pump(peer, live, firstPeerFile, firstUnknown);
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        } else if (serve.s || get.f) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        } else if (!listener) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        peer.core().trim(unixNow());  // once a day at most
    }
}
