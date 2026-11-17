// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#pragma GCC optimize("Os")  // menus and screens: size over speed (the door CPU and transfers stay -O2)
#include "bbs/msgbase.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>

namespace axis::bbs {

namespace {
constexpr char   kSep = '\x1f', kSub = '\x1e';
constexpr size_t kHeadLine = 448;  // what a headers-only scan keeps of a line (a Net Files listing is at most 434 with "net=")

std::string oneLine(std::string s) {
    for (char& c : s)
        if (c == '\n' || c == '\r' || c == kSep || c == kSub) c = ' ';
    return s.substr(0, 72);
}
// What a scan keeps of each record: the body and the whole net line (kWhole), or headers only with the net line cut
// to "<kind><sep><first field>" (kCut), or headers only with a Net Files listing line kept whole (kListing).
enum ScanMode { kCut, kListing, kWhole };
// read the file a record at a time from byte start (a record's start, or 0); visit(header) -> keep going. at, if
// given, ends at the start of the record visit refused (so a later scan from there sees it again), or at the end.
template <typename F>
void scan(const std::string& path, ScanMode mode, F visit, uint32_t start = 0, uint32_t* at = nullptr) {
    if (at) *at = start;
    std::FILE* in = std::fopen(path.c_str(), "rb");
    if (!in) return;
    if (start && std::fseek(in, (long)start, SEEK_SET) != 0) {
        std::fclose(in);
        return;
    }
    const bool  bodies = mode == kWhole;
    Message     m;
    bool        inBody = false, have = false;
    std::string line;
    uint32_t    pos = start, lineAt = start, recAt = start;  // bytes read; where this line and this record began
    auto        done = [&]() {
        if (!have) return true;
        if (!m.body.empty() && m.body.back() == '\n') m.body.pop_back();
        return visit(m);
    };
    auto stop = [&](bool refused) {
        if (at) *at = refused ? recAt : pos;
    };
    // Headers only: a line is kept to its first kHeadLine characters (a header, the start of the net line, or enough
    // of a body line to tell a record mark), so a long net line or body line is never held whole.
    const size_t cap = bodies ? (size_t)-1 : kHeadLine;
    for (int c;;) {
        c = std::fgetc(in);
        if (c != EOF) pos++;
        if (c != EOF && c != '\n') {
            if (c != '\r' && line.size() < cap) line += (char)c;
            continue;
        }
        if (c == EOF && line.empty()) {  // the file's last newline: nothing more
            stop(!done());
            break;
        }
        if (line == "\x01MSG") {
            if (!done()) {
                stop(true);
                break;
            }
            m = Message();
            have = true;
            inBody = false;
            recAt = lineAt;
        } else if (have && !inBody) {
            if (line.empty()) inBody = true;
            else {
                size_t eq = line.find('=');
                std::string k = line.substr(0, eq), v = eq == std::string::npos ? "" : line.substr(eq + 1);
                if (k == "num") m.num = std::atoi(v.c_str());
                else if (k == "from") m.from = v;
                else if (k == "to") m.to = v;
                else if (k == "subj") m.subject = v;
                else if (k == "date") m.date = v;
                else if (k == "net") {
                    if (bodies || (mode == kListing && v.compare(0, 2, "L\x1f") == 0)) m.net = v;  // a listing: at most 430
                    else {  // headers only: keep "P<sep><id>", not the whole (up to 9 KB) line
                        size_t a = v.find(kSep), b = a == std::string::npos ? a : v.find(kSep, a + 1);
                        m.net = b == std::string::npos ? v : v.substr(0, b);
                    }
                }
            }
        } else if (have && inBody && bodies) {
            m.body += (line.size() > 1 && line[0] == ' ' && line[1] == '\x01' ? line.substr(1) : line) + "\n";
        }
        line.clear();
        lineAt = pos;
        if (c == EOF) {
            stop(!done());
            break;
        }
    }
    std::fclose(in);
}
// One post as it's stored, written a piece at a time through a small buffer: the net line and the body are never
// copied into one record string. False if a write failed.
bool writeRecord(std::FILE* f, int num, const Message& m) {
    char   buf[256];
    size_t n = 0;
    bool   ok = true;
    auto   flush = [&]() {
        if (n && std::fwrite(buf, 1, n, f) != n) ok = false;
        n = 0;
    };
    auto put = [&](char c) {
        if (n == sizeof buf) flush();
        buf[n++] = c;
    };
    auto text = [&](const std::string& s) {
        for (char c : s) put(c);
    };
    text("\x01MSG\nnum=" + std::to_string(num) + "\nfrom=" + oneLine(m.from) + "\nto=" + oneLine(m.to) + "\nsubj=" +
         oneLine(m.subject) + "\ndate=" + oneLine(m.date) + "\n");
    if (!m.net.empty()) {  // written whole: it holds 0x1F separators and may run past 72 characters
        text("net=");
        for (char c : m.net)
            if (c != '\n' && c != '\r') put(c);
        put('\n');
    }
    put('\n');
    bool start = true;  // at the start of a body line: one that starts like a record mark is escaped
    for (char c : m.body) {
        if (c == '\r') continue;
        if (c == '\n') {
            put('\n');
            start = true;
            continue;
        }
        if (start && c == '\x01') put(' ');
        start = false;
        put(c);
    }
    put('\n');
    flush();
    return ok;
}
std::vector<std::string> split(const std::string& s, char sep, size_t n) {  // the last field whole
    std::vector<std::string> v;
    size_t                   pos = 0;
    while (v.size() + 1 < n) {
        size_t e = s.find(sep, pos);
        if (e == std::string::npos) break;
        v.push_back(s.substr(pos, e - pos));
        pos = e + 1;
    }
    v.push_back(s.substr(pos));
    return v;
}
}  // namespace

std::string FileMessageStore::path(const std::string& area) const {
    std::string a;
    for (char c : area)  // an area id is a plain name: never a path
        if (std::isalnum((unsigned char)c) || c == '_' || c == '-') a += c;
    return root_ + "/" + a + ".msg";
}

std::vector<Message> FileMessageStore::headers(const std::string& area) {
    std::vector<Message> v;
    scan(path(area), kCut, [&](const Message& m) {
        v.push_back(m);
        if (v.size() > kMaxHeaders) v.erase(v.begin());
        return true;
    });
    return v;
}

bool FileMessageStore::read(const std::string& area, int num, Message& out) {
    bool found = false;
    scan(path(area), kWhole, [&](Message& m) {
        if (m.num != num) return true;
        out = std::move(m);  // the scan stops here, so the record is not needed again
        found = true;
        return false;
    });
    return found;
}

int FileMessageStore::drop(const std::string& area, const std::function<bool(const Message&)>& gone) {
    std::string tmp = path(area) + ".tmp";
    std::FILE*  out = nullptr;
    int         dropped = 0, kept = 0;
    scan(path(area), kWhole, [&](const Message& m) {
        if (gone(m)) {
            dropped++;
            return true;
        }
        if (!out) out = std::fopen(tmp.c_str(), "wb");
        if (out) writeRecord(out, m.num, m);
        kept++;
        return true;
    });
    if (out) std::fclose(out);
    if (!dropped) {
        std::remove(tmp.c_str());
        return 0;
    }
    std::remove(path(area).c_str());
    if (kept) std::rename(tmp.c_str(), path(area).c_str());
    return dropped;
}

int FileMessageStore::post(const std::string& area, const Message& m) {
    int last = 0;
    scan(path(area), kCut, [&](const Message& h) {
        if (h.num > last) last = h.num;
        return true;
    });
    std::FILE* out = std::fopen(path(area).c_str(), "ab");
    if (!out) return 0;
    bool ok = writeRecord(out, last + 1, m);
    ok = std::fclose(out) == 0 && ok;
    return ok ? last + 1 : 0;
}

void FileMessageStore::scanHeaders(const std::string& area, const std::function<bool(const Message&)>& each) {
    scan(path(area), kListing, [&](const Message& m) { return each(m); });
}

void FileMessageStore::scanHeadersFrom(const std::string& area, uint32_t& pos,
                                       const std::function<bool(const Message&)>& each) {
    scan(path(area), kListing, [&](const Message& m) { return each(m); }, pos, &pos);
}

std::string serveMessages(MessageStore& st, const std::string& req) {
    if (req.size() < 3 || req[0] != 'M') return "0";
    char op = req[1];
    auto f = split(req.substr(3), kSep, op == 'p' ? 6 : 3);
    auto a = [&](size_t i) { return i < f.size() ? f[i] : std::string(); };
    if (op == 'h') {
        std::string out = "1";
        for (const Message& m : st.headers(a(0)))
            out += std::to_string(m.num) + kSub + m.from + kSub + m.to + kSub + m.subject + kSub + m.date + "\n";
        return out;
    }
    if (op == 'r') {
        Message m;
        if (!st.read(a(0), std::atoi(a(1).c_str()), m)) return "0";
        return "1" + m.from + kSub + m.to + kSub + m.subject + kSub + m.date + kSub + m.body;
    }
    if (op == 'p') {
        Message m;
        m.from = a(1);
        m.to = a(2);
        m.subject = a(3);
        m.date = a(4);
        m.body = a(5);
        int n = st.post(a(0), m);
        return n ? "1" + std::to_string(n) : "0";
    }
    return "0";
}

std::vector<Message> RemoteMessageStore::headers(const std::string& area) {
    std::vector<Message> v;
    std::string          r;
    if (!ask_ || !ask_(std::string("Mh") + kSep + area, r) || r.empty() || r[0] != '1') return v;
    size_t pos = 1;
    while (pos < r.size()) {
        size_t e = r.find('\n', pos);
        if (e == std::string::npos) e = r.size();
        auto p = split(r.substr(pos, e - pos), kSub, 5);
        pos = e + 1;
        if (p.size() < 5) continue;
        Message m;
        m.num = std::atoi(p[0].c_str());
        m.from = p[1];
        m.to = p[2];
        m.subject = p[3];
        m.date = p[4];
        v.push_back(m);
    }
    return v;
}

bool RemoteMessageStore::read(const std::string& area, int num, Message& out) {
    std::string r;
    if (!ask_ || !ask_(std::string("Mr") + kSep + area + kSep + std::to_string(num), r) || r.empty() || r[0] != '1') return false;
    auto p = split(r.substr(1), kSub, 5);
    if (p.size() < 5) return false;
    out = Message();
    out.num = num;
    out.from = p[0];
    out.to = p[1];
    out.subject = p[2];
    out.date = p[3];
    out.body = p[4];
    return true;
}

int RemoteMessageStore::post(const std::string& area, const Message& m) {
    std::string r;
    std::string req = std::string("Mp") + kSep + area + kSep + oneLine(m.from) + kSep + oneLine(m.to) + kSep + oneLine(m.subject) + kSep +
                      oneLine(m.date) + kSep + m.body;
    if (!ask_ || !ask_(req, r) || r.empty() || r[0] != '1') return 0;
    return std::atoi(r.c_str() + 1);
}

}  // namespace axis::bbs
