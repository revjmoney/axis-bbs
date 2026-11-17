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
//   quit
//
// Without --seed the node makes its own seed in <dir>/seed.txt the first time and keeps it. The seed is
// never printed. Everything else lives in <dir> too: msgs/, outlog.txt, state.txt, seen.txt,
// cursors.txt, card.txt, postseq.txt, appseq.txt, voteseq.txt, user.txt, apps.txt (the signed User Net
// applications and votes) and (dial mode) firstpeer.txt.
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

#include "bbs/msgbase.h"
#include "net/areas.h"
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
            if (line[0] != 'A') break;  // the votes come after the applications
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
        c.links = userOn_ ? "internet,net2" : "internet";  // net2: a Phase 2 board, peers send it UWANT
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

std::mutex g_mu;
std::deque<std::string> g_cmds;

void readStdin() {
    std::string line;
    while (std::getline(std::cin, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::lock_guard<std::mutex> g(g_mu);
        g_cmds.push_back(line);
    }
}

bool nextCommand(std::string& out) {
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

    std::thread(readStdin).detach();
    Live live;
    bool firstUnknown = false;
    for (;;) {
        std::string cmd;
        while (nextCommand(cmd)) {
            std::string rest = cmd, verb = word(rest);
            if (verb.empty()) continue;
            if (verb == "quit" || verb == "exit") {
                if (live.s) say("quitting with a session open");
                peer.save();
                say("bye");
                std::fflush(stdout);
                std::_Exit(0);  // the stdin reader is still blocked in getline
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
                std::string area = areaId(word(rest));
                if (area.empty()) say("usage: list <area>");
                else peer.list(area);
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
                    "vote <board> y|n [comment] | apps | members | quit");
            } else {
                say("unknown command: %s (try help)", verb.c_str());
            }
        }

        if (listener) {
            if (auto c = listener->accept(live.s ? 0 : 20, false)) {
                if (live.s) {
                    const char bye[] = "BYE busy\n";
                    c->write((const uint8_t*)bye, sizeof bye - 1);
                } else {
                    live.sock = std::move(c);
                    live.s.reset(new NetSession(peer.core(), peer.card(), peer.key(), nonceHex(), false, std::string()));
                    live.dialer = false;
                    say("a board dialed in");
                }
            }
        }
        peer.userTick();
        if (live.s) {
            pump(peer, live, firstPeerFile, firstUnknown);
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        } else if (!listener) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        peer.core().trim(unixNow());  // once a day at most
    }
}
