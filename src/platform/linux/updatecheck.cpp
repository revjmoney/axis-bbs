// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include "platform/linux/updatecheck.h"

#include <cctype>

namespace axis::pcupdate {
namespace {

bool digitsOnly(std::string_view s) {
    if (s.empty()) return false;
    for (char c : s)
        if (c < '0' || c > '9') return false;
    return true;
}

std::string versionText(const Version& v) {
    std::string s;
    for (int i = 0; i < v.n; i++) {
        if (i) s.push_back('.');
        s += std::to_string(v.part[i]);
    }
    return s;
}

bool safeGithubUrl(const std::string& u) {
    const std::string p = "https://github.com/";
    if (u.size() < p.size() + 3 || u.size() > 300 || u.compare(0, p.size(), p) != 0) return false;
    for (unsigned char c : u)
        if (c <= 0x20 || c >= 0x7f) return false;
    return true;
}

// One line, no controls, capped. Release text is shown to the sysop and stored on disk, so an escape
// or a huge body must not land in the version field, the log, or the cache file.
std::string oneLine(const std::string& body, const std::string& name) {
    const std::string& src = body.empty() ? name : body;
    std::string        o;
    bool               sp = false;
    for (unsigned char c : src) {
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            sp = !o.empty();
            continue;
        }
        if (c < 0x20 || c == 0x7f) continue;
        if (sp) {
            o.push_back(' ');
            sp = false;
        }
        o.push_back((char)c);
        if (o.size() >= 180) break;
    }
    while (!o.empty() && ((unsigned char)o.back() & 0xc0) == 0x80) o.pop_back();
    return o;
}

std::string notesOf(const std::string& body, const std::string& summary) {
    if (body.empty()) return summary;
    std::string o;
    int         lines = 1;
    for (unsigned char c : body) {
        if (c == '\r') continue;
        if (c == '\n') {
            if (lines >= 16) break;
            o.push_back('\n');
            lines++;
        } else if (c == '\t' || (c >= 0x20 && c != 0x7f)) {
            o.push_back((char)c);
        }
        if (o.size() >= 800) break;
    }
    while (!o.empty() && (o.back() == ' ' || o.back() == '\n' || o.back() == '\t')) o.pop_back();
    while (!o.empty() && ((unsigned char)o.back() & 0xc0) == 0x80) o.pop_back();
    return o.empty() ? summary : o;
}

std::string escField(const std::string& s) {
    std::string o;
    o.reserve(s.size());
    for (unsigned char c : s) {
        if (c == '\\') o += "\\\\";
        else if (c == '\n') o += "\\n";
        else if (c == '\r') o += "\\r";
        else if (c < 0x20 || c == 0x7f) continue;
        else o.push_back((char)c);
    }
    return o;
}

std::string unescField(std::string_view s) {
    std::string o;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            char n = s[++i];
            if (n == 'n') o.push_back('\n');
            else if (n == 'r') o.push_back('\r');
            else o.push_back(n);
        } else if ((unsigned char)s[i] >= 0x20 && (unsigned char)s[i] != 0x7f) {
            o.push_back(s[i]);
        }
    }
    return o;
}

struct Cur {
    const char* p;
    const char* end;
    bool        bad = false;
    char        get() {
        if (p >= end) {
            bad = true;
            return 0;
        }
        return *p++;
    }
    char peek() const { return p < end ? *p : 0; }
    void skip() {
        while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
    }
};

bool parseString(Cur& c, std::string& out) {
    if (c.get() != '"') {
        c.bad = true;
        return false;
    }
    out.clear();
    while (c.p < c.end) {
        unsigned char ch = (unsigned char)c.get();
        if (ch == '"') return true;
        if (ch == '\\') {
            char e = c.get();
            if (c.bad) return false;
            char put = e;
            if (e == 'n') put = '\n';
            else if (e == 'r') put = '\r';
            else if (e == 't') put = '\t';
            else if (e == 'u') {
                for (int i = 0; i < 4; i++) c.get();
                put = '?';
            }
            if (out.size() < 8000) out.push_back(put);
            continue;
        }
        if (ch < 0x20) {
            c.bad = true;
            return false;
        }
        if (out.size() < 8000) out.push_back((char)ch);
    }
    c.bad = true;
    return false;
}

bool skipLit(Cur& c, const char* lit) {
    for (; *lit; lit++)
        if (c.get() != *lit) {
            c.bad = true;
            return false;
        }
    return true;
}

bool skipNumber(Cur& c) {
    if (c.peek() == '-') c.get();
    if (c.peek() < '0' || c.peek() > '9') {
        c.bad = true;
        return false;
    }
    while (c.p < c.end) {
        char ch = c.peek();
        if ((ch >= '0' && ch <= '9') || ch == '.' || ch == 'e' || ch == 'E' || ch == '+' || ch == '-') c.get();
        else break;
    }
    return true;
}

bool parseObject(Cur& c, int depth, Release* rel);
bool skipValue(Cur& c, int depth);

bool parseArraySkip(Cur& c, int depth) {
    c.skip();
    if (c.peek() == ']') {
        c.get();
        return true;
    }
    while (!c.bad) {
        if (!skipValue(c, depth)) return false;
        c.skip();
        char sep = c.get();
        if (sep == ']') return true;
        if (sep != ',') {
            c.bad = true;
            return false;
        }
    }
    return false;
}

bool skipValue(Cur& c, int depth) {
    if (depth > 8) {
        c.bad = true;
        return false;
    }
    c.skip();
    char ch = c.peek();
    if (ch == '"') {
        std::string tmp;
        return parseString(c, tmp);
    }
    if (ch == '{') {
        c.get();
        return parseObject(c, depth + 1, nullptr);
    }
    if (ch == '[') {
        c.get();
        return parseArraySkip(c, depth + 1);
    }
    if (ch == 't') return skipLit(c, "true");
    if (ch == 'f') return skipLit(c, "false");
    if (ch == 'n') return skipLit(c, "null");
    if (ch == '-' || (ch >= '0' && ch <= '9')) return skipNumber(c);
    c.bad = true;
    return false;
}

bool parseObject(Cur& c, int depth, Release* rel) {
    if (depth > 8) {
        c.bad = true;
        return false;
    }
    c.skip();
    if (c.peek() == '}') {
        c.get();
        return true;
    }
    while (!c.bad) {
        c.skip();
        std::string key;
        if (!parseString(c, key)) return false;
        c.skip();
        if (c.get() != ':') {
            c.bad = true;
            return false;
        }
        c.skip();
        bool take = rel && depth == 1;
        if (take && (key == "tag_name" || key == "name" || key == "body" || key == "html_url") && c.peek() == '"') {
            std::string v;
            if (!parseString(c, v)) return false;
            if (key == "tag_name") rel->tag = std::move(v);
            else if (key == "name") rel->name = std::move(v);
            else if (key == "body") rel->body = std::move(v);
            else rel->url = std::move(v);
        } else if (take && (key == "draft" || key == "prerelease") && (c.peek() == 't' || c.peek() == 'f')) {
            bool val = c.peek() == 't';
            if (!(val ? skipLit(c, "true") : skipLit(c, "false"))) return false;
            if (key == "draft") rel->draft = val;
            else rel->prerelease = val;
        } else if (!skipValue(c, depth)) {
            return false;
        }
        c.skip();
        char sep = c.get();
        if (sep == '}') return true;
        if (sep != ',') {
            c.bad = true;
            return false;
        }
    }
    return false;
}

}  // namespace

Version parseVersion(std::string_view text) {
    Version v;
    if (text.empty() || text.size() > 32) return v;
    int acc = -1;
    // Index only while i < size. The last component is stored after the loop, so a read never lands on text[size].
    for (size_t i = 0; i < text.size(); i++) {
        char c = text[i];
        if (c == '.') {
            if (acc < 0 || v.n >= 4) return Version{};
            v.part[v.n++] = acc;
            acc = -1;
        } else if (c >= '0' && c <= '9') {
            if (acc < 0) acc = 0;
            if (acc > 1000000) return Version{};
            acc = acc * 10 + (c - '0');
        } else {
            return Version{};
        }
    }
    if (acc < 0 || v.n >= 4) return Version{};  // a trailing dot, or a fifth component
    v.part[v.n++] = acc;
    v.ok = true;
    return v;
}

int cmpVersion(const Version& a, const Version& b) {
    if (!a.ok && !b.ok) return 0;
    if (!a.ok) return -1;
    if (!b.ok) return 1;
    for (int i = 0; i < 4; i++) {
        int av = i < a.n ? a.part[i] : 0;
        int bv = i < b.n ? b.part[i] : 0;
        if (av < bv) return -1;
        if (av > bv) return 1;
    }
    return 0;
}

Tag parseTag(std::string_view tag) {
    Tag out;
    std::string_view rest;
    if (tag.size() >= 4 && tag.substr(0, 4) == "pc-v") {
        out.kind = TagKind::Pc;
        rest = tag.substr(4);
    } else if (tag.size() >= 2 && tag[0] == 'v' && tag[1] >= '0' && tag[1] <= '9') {
        out.kind = TagKind::Board;
        rest = tag.substr(1);
    } else {
        return out;
    }
    out.version = parseVersion(rest);
    if (!out.version.ok) out.kind = TagKind::Other;
    return out;
}

ReleaseList parseReleaseList(std::string_view json) {
    ReleaseList out;
    if (json.size() > 512u * 1024u) return out;
    Cur c{json.data(), json.data() + json.size(), false};
    c.skip();
    if (c.get() != '[') return out;
    c.skip();
    if (c.peek() == ']') {
        c.get();
        c.skip();
        if (c.p != c.end || c.bad) return ReleaseList{};
        out.ok = true;
        return out;
    }
    int n = 0;
    while (!c.bad && n < 120) {
        c.skip();
        if (c.get() != '{') return ReleaseList{};
        Release r;
        if (!parseObject(c, 1, &r)) return ReleaseList{};
        out.releases.push_back(std::move(r));
        n++;
        c.skip();
        if (c.peek() == ']') {
            c.get();
            c.skip();
            if (c.p != c.end || c.bad) return ReleaseList{};
            out.ok = true;
            return out;
        }
        if (c.get() != ',') return ReleaseList{};
    }
    return ReleaseList{};
}

Pick pickPcRelease(const ReleaseList& list, std::string_view current, std::string_view repo) {
    Pick    out;
    Version cur = parseVersion(current);
    Version best;
    const Release* chosen = nullptr;
    for (const auto& r : list.releases) {
        if (r.draft || r.prerelease) continue;
        Tag t = parseTag(r.tag);
        if (t.kind != TagKind::Pc) continue;
        if (!chosen || cmpVersion(t.version, best) > 0) {
            best = t.version;
            chosen = &r;
        }
    }
    if (!chosen) return out;
    out.found = true;
    out.version = versionText(best);
    out.tag = chosen->tag;
    out.summary = oneLine(chosen->body, chosen->name);
    out.notes = notesOf(chosen->body, out.summary);
    if (safeGithubUrl(chosen->url)) out.url = chosen->url;
    else if (!repo.empty() && !out.tag.empty()) out.url = "https://github.com/" + std::string(repo) + "/releases/tag/" + out.tag;
    out.newer = cur.ok && cmpVersion(best, cur) > 0;
    return out;
}

std::string writeCache(const Cache& c) {
    std::string o = "checked=" + std::to_string(c.checked < 0 ? 0 : c.checked) + "\n";
    o += "latest=" + escField(c.latest) + "\n";
    o += std::string("newer=") + (c.newer ? "1" : "0") + "\n";
    o += "url=" + escField(c.url) + "\n";
    o += "summary=" + escField(c.summary) + "\n";
    o += "notes=" + escField(c.notes) + "\n";
    return o;
}

Cache readCache(std::string_view text) {
    Cache c;
    if (text.size() > 16000) return c;
    bool sawChecked = false;
    bool badChecked = false;
    for (size_t at = 0; at < text.size();) {
        size_t nl = text.find('\n', at);
        if (nl == std::string_view::npos) nl = text.size();
        std::string_view line = text.substr(at, nl - at);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        at = nl < text.size() ? nl + 1 : text.size();
        if (line.empty()) continue;
        if (line.size() > 5000) return Cache{};
        size_t eq = line.find('=');
        if (eq == std::string_view::npos || eq == 0) continue;
        std::string_view key = line.substr(0, eq);
        std::string      val = unescField(line.substr(eq + 1));
        if (val.size() > 4000) return Cache{};
        if (key == "checked") {
            sawChecked = true;
            if (!digitsOnly(line.substr(eq + 1)) || line.substr(eq + 1).size() > 12) badChecked = true;
            else {
                long long n = 0;
                for (char ch : line.substr(eq + 1)) n = n * 10 + (ch - '0');
                c.checked = (int64_t)n;
            }
        } else if (key == "latest") c.latest = val;
        else if (key == "newer") c.newer = val == "1";
        else if (key == "url") c.url = val;
        else if (key == "summary") c.summary = val;
        else if (key == "notes") c.notes = val;
    }
    if (badChecked) return Cache{};  // a stamp that is not a number: the file is not one of ours
    if (!sawChecked) c.checked = 0;
    if (!parseVersion(c.latest).ok) {
        c.latest.clear();
        c.newer = false;
        c.summary.clear();
        c.notes.clear();
        c.url.clear();
    } else if (!safeGithubUrl(c.url)) {
        c.url.clear();
    }
    if (c.latest.empty()) c.newer = false;
    return c;
}

bool due(int64_t checked, int64_t now, int64_t interval) {
    if (interval < 1) interval = 1;
    if (checked <= 0 || now <= 0) return true;
    if (now < checked) return true;
    return now - checked >= interval;
}

Applied applyFetch(const Cache& prev, int64_t now, std::string_view current, std::string_view repo, FetchKind kind, int httpCode,
                   std::string_view body) {
    Applied a;
    a.cache = prev;
    if (kind == FetchKind::NoTls) {
        a.remember = false;
        return a;
    }
    a.remember = true;
    if (now > 0) a.cache.checked = now;
    if (kind != FetchKind::Ok || httpCode != 200) return a;
    ReleaseList list = parseReleaseList(body);
    if (!list.ok) return a;
    a.parsed = true;
    Pick pick = pickPcRelease(list, current, repo);
    a.cache.latest = pick.version;
    a.cache.summary = pick.summary;
    a.cache.notes = pick.notes;
    a.cache.url = pick.url;
    a.cache.newer = pick.newer;
    return a;
}

HttpMessage parseHttp(std::string_view raw) {
    HttpMessage m;
    if (raw.size() < 12 || raw.compare(0, 5, "HTTP/") != 0) return m;
    size_t sp = raw.find(' ');
    if (sp == std::string_view::npos || sp + 4 > raw.size()) return m;
    if (raw[sp + 1] < '0' || raw[sp + 1] > '9' || raw[sp + 2] < '0' || raw[sp + 2] > '9' || raw[sp + 3] < '0' || raw[sp + 3] > '9')
        return m;
    m.code = (raw[sp + 1] - '0') * 100 + (raw[sp + 2] - '0') * 10 + (raw[sp + 3] - '0');
    size_t hdr = raw.find("\r\n\r\n");
    if (hdr == std::string_view::npos) return m;
    m.headersOk = true;
    std::string_view headers = raw.substr(0, hdr);
    std::string_view rest = raw.substr(hdr + 4);
    bool             chunked = false;
    long long        length = -1;
    size_t           at = 0;
    while (at < headers.size()) {
        size_t nl = headers.find("\r\n", at);
        if (nl == std::string_view::npos) nl = headers.size();
        std::string_view line = headers.substr(at, nl - at);
        at = nl < headers.size() ? nl + 2 : headers.size();
        auto lower = [](unsigned char c) { return (char)std::tolower(c); };
        std::string key;
        size_t      colon = line.find(':');
        if (colon == std::string_view::npos) continue;
        for (size_t i = 0; i < colon; i++) key.push_back(lower((unsigned char)line[i]));
        std::string_view val = line.substr(colon + 1);
        while (!val.empty() && (val.front() == ' ' || val.front() == '\t')) val.remove_prefix(1);
        if (key == "transfer-encoding") {
            std::string v;
            for (unsigned char c : val) v.push_back(lower(c));
            if (v.find("chunked") != std::string::npos) chunked = true;
        } else if (key == "content-length" && !val.empty()) {
            length = 0;
            for (char c : val) {
                if (c < '0' || c > '9' || length > 1000000) {
                    length = -1;
                    break;
                }
                length = length * 10 + (c - '0');
            }
        }
    }
    constexpr size_t kCap = 512u * 1024u;
    if (chunked) {
        std::string body;
        size_t      i = 0;
        while (i < rest.size() && body.size() < kCap) {
            size_t lineEnd = rest.find("\r\n", i);
            if (lineEnd == std::string_view::npos) return m;
            std::string_view hex = rest.substr(i, lineEnd - i);
            size_t           semi = hex.find(';');
            if (semi != std::string_view::npos) hex = hex.substr(0, semi);
            if (hex.empty()) return m;
            size_t n = 0;
            for (char c : hex) {
                int d;
                if (c >= '0' && c <= '9') d = c - '0';
                else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
                else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
                else return m;
                if (n > kCap) return m;
                n = (n << 4) + (size_t)d;
            }
            i = lineEnd + 2;
            if (n == 0) {
                m.body = std::move(body);
                m.bodyOk = true;
                return m;
            }
            if (n > kCap || i + n + 2 > rest.size()) return m;
            body.append(rest.substr(i, n));
            i += n;
            if (rest.substr(i, 2) != "\r\n") return m;
            i += 2;
        }
        return m;
    }
    if (length >= 0) {
        if ((size_t)length > kCap || rest.size() < (size_t)length) return m;
        m.body.assign(rest.substr(0, (size_t)length));
        m.bodyOk = true;
        return m;
    }
    if (rest.size() > kCap) return m;
    m.body.assign(rest);
    m.bodyOk = true;
    return m;
}

FetchKind classifyHttp(const HttpMessage& msg) {
    if (!msg.headersOk) return FetchKind::BadBody;
    if (msg.code == 429) return FetchKind::RateLimit;
    if (msg.code == 403) {
        std::string low;
        low.reserve(msg.body.size());
        for (unsigned char c : msg.body) low.push_back((char)std::tolower(c));
        if (low.find("rate limit") != std::string::npos) return FetchKind::RateLimit;
        return FetchKind::BadHttp;
    }
    if (msg.code != 200 || !msg.bodyOk) return FetchKind::BadHttp;
    return FetchKind::Ok;
}

std::string buildRequest(std::string_view repo, std::string_view version) {
    bool ua = !version.empty() && version.size() <= 32;
    for (char c : version)
        if (c != '.' && (c < '0' || c > '9')) ua = false;
    std::string q = "GET /repos/";
    q.append(repo);
    q += "/releases?per_page=100 HTTP/1.0\r\nHost: api.github.com\r\nUser-Agent: axispc";
    if (ua) {
        q.push_back('/');
        q.append(version);
    }
    q += "\r\nAccept: application/vnd.github+json\r\nConnection: close\r\n\r\n";
    return q;
}

std::string formatReport(const Cache& c, std::string_view current, bool enabled) {
    if (!enabled) return "Update check is off.\nSet update_check=1 in axis.conf to turn it on.\n";
    std::string o = "axispc ";
    o.append(current);
    o += "\n";
    if (c.checked <= 0 && c.latest.empty()) {
        o += "No release check yet. It runs once a day when the clock is set.\n";
        return o;
    }
    if (c.newer && !c.latest.empty()) {
        o += "A newer axispc release is out: pc-v";
        o += c.latest;
        o += "\n";
        if (!c.notes.empty()) {
            o += c.notes;
            o += "\n";
        }
        if (!c.url.empty()) {
            o += c.url;
            o += "\n";
        }
        o += "The version on the waiting screen blinks. Press V there, or tap that spot, to read this.\n";
    } else {
        o += "No newer pc release.\n";
        if (!c.latest.empty()) {
            o += "Latest pc release seen: pc-v";
            o += c.latest;
            o += "\n";
            if (!c.notes.empty()) {
                o += c.notes;
                o += "\n";
            }
            if (!c.url.empty()) {
                o += c.url;
                o += "\n";
            }
        }
    }
    o += "Checked once a day. update_check=0 in axis.conf turns this off.\n";
    return o;
}

#if !defined(__linux__)
void        start(const std::string&, bool, const std::string&, const std::string&) {}
void        poll() {}
void        stop() {}
View        view() { return {}; }
std::string report() { return {}; }
#endif

}  // namespace axis::pcupdate
