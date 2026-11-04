// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#pragma GCC optimize("Os")  // menus and screens: size over speed (the door CPU and transfers stay -O2)
#include "bbs/msgbase.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>

namespace axis::bbs {

namespace {
constexpr char kSep = '\x1f', kSub = '\x1e';

std::string oneLine(std::string s) {
    for (char& c : s)
        if (c == '\n' || c == '\r' || c == kSep || c == kSub) c = ' ';
    return s.substr(0, 72);
}
// read the file a record at a time; visit(header, body-wanted?) -> keep going
template <typename F>
void scan(const std::string& path, bool bodies, F visit) {
    std::FILE* in = std::fopen(path.c_str(), "rb");
    if (!in) return;
    Message     m;
    bool        inBody = false, have = false;
    std::string line;
    auto        done = [&]() {
        if (!have) return true;
        if (!m.body.empty() && m.body.back() == '\n') m.body.pop_back();
        return visit(m);
    };
    for (int c;;) {
        c = std::fgetc(in);
        if (c != EOF && c != '\n') {
            if (c != '\r') line += (char)c;
            continue;
        }
        if (c == EOF && line.empty()) {  // the file's last newline: nothing more
            done();
            break;
        }
        if (line == "\x01MSG") {
            if (!done()) break;
            m = Message();
            have = true;
            inBody = false;
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
            }
        } else if (have && inBody && bodies) {
            m.body += (line.size() > 1 && line[0] == ' ' && line[1] == '\x01' ? line.substr(1) : line) + "\n";
        }
        line.clear();
        if (c == EOF) {
            done();
            break;
        }
    }
    std::fclose(in);
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
    scan(path(area), false, [&](const Message& m) {
        v.push_back(m);
        if (v.size() > kMaxHeaders) v.erase(v.begin());
        return true;
    });
    return v;
}

bool FileMessageStore::read(const std::string& area, int num, Message& out) {
    bool found = false;
    scan(path(area), true, [&](const Message& m) {
        if (m.num != num) return true;
        out = m;
        found = true;
        return false;
    });
    return found;
}

int FileMessageStore::post(const std::string& area, const Message& m) {
    int last = 0;
    scan(path(area), false, [&](const Message& h) {
        if (h.num > last) last = h.num;
        return true;
    });
    std::FILE* out = std::fopen(path(area).c_str(), "ab");
    if (!out) return 0;
    std::string rec = "\x01MSG\nnum=" + std::to_string(last + 1) + "\nfrom=" + oneLine(m.from) + "\nto=" + oneLine(m.to) +
                      "\nsubj=" + oneLine(m.subject) + "\ndate=" + oneLine(m.date) + "\n\n";
    std::string line;
    for (size_t i = 0; i <= m.body.size(); i++) {  // body lines; one that starts like a record mark is escaped
        if (i == m.body.size() || m.body[i] == '\n') {
            if (!line.empty() && line[0] == '\x01') line = " " + line;
            rec += line + "\n";
            line.clear();
        } else if (m.body[i] != '\r') {
            line += m.body[i];
        }
    }
    bool ok = std::fwrite(rec.data(), 1, rec.size(), out) == rec.size();
    std::fclose(out);
    return ok ? last + 1 : 0;
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
