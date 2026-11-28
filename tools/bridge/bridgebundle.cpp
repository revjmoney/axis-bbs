// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include "bridgebundle.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <set>

#include "net/record.h"

namespace axis::bbs::bridge {

namespace {
// CP437's upper half as Unicode (the same table the PC console draws with)
const uint16_t kCp437[128] = {
    0x00C7, 0x00FC, 0x00E9, 0x00E2, 0x00E4, 0x00E0, 0x00E5, 0x00E7, 0x00EA, 0x00EB, 0x00E8, 0x00EF, 0x00EE, 0x00EC, 0x00C4, 0x00C5,
    0x00C9, 0x00E6, 0x00C6, 0x00F4, 0x00F6, 0x00F2, 0x00FB, 0x00F9, 0x00FF, 0x00D6, 0x00DC, 0x00A2, 0x00A3, 0x00A5, 0x20A7, 0x0192,
    0x00E1, 0x00ED, 0x00F3, 0x00FA, 0x00F1, 0x00D1, 0x00AA, 0x00BA, 0x00BF, 0x2310, 0x00AC, 0x00BD, 0x00BC, 0x00A1, 0x00AB, 0x00BB,
    0x2591, 0x2592, 0x2593, 0x2502, 0x2524, 0x2561, 0x2562, 0x2556, 0x2555, 0x2563, 0x2551, 0x2557, 0x255D, 0x255C, 0x255B, 0x2510,
    0x2514, 0x2534, 0x252C, 0x251C, 0x2500, 0x253C, 0x255E, 0x255F, 0x255A, 0x2554, 0x2569, 0x2566, 0x2560, 0x2550, 0x256C, 0x2567,
    0x2568, 0x2564, 0x2565, 0x2559, 0x2558, 0x2552, 0x2553, 0x256B, 0x256A, 0x2518, 0x250C, 0x2588, 0x2584, 0x258C, 0x2590, 0x2580,
    0x03B1, 0x00DF, 0x0393, 0x03C0, 0x03A3, 0x03C3, 0x00B5, 0x03C4, 0x03A6, 0x0398, 0x03A9, 0x03B4, 0x221E, 0x03C6, 0x03B5, 0x2229,
    0x2261, 0x00B1, 0x2265, 0x2264, 0x2320, 0x2321, 0x00F7, 0x2248, 0x00B0, 0x2219, 0x00B7, 0x221A, 0x207F, 0x00B2, 0x25A0, 0x00A0};

void putUtf8(std::string& out, uint32_t u) {
    if (u < 0x80) out += (char)u;
    else if (u < 0x800) {
        out += (char)(0xC0 | (u >> 6));
        out += (char)(0x80 | (u & 0x3F));
    } else if (u < 0x10000) {
        out += (char)(0xE0 | (u >> 12));
        out += (char)(0x80 | ((u >> 6) & 0x3F));
        out += (char)(0x80 | (u & 0x3F));
    } else {
        out += (char)(0xF0 | (u >> 18));
        out += (char)(0x80 | ((u >> 12) & 0x3F));
        out += (char)(0x80 | ((u >> 6) & 0x3F));
        out += (char)(0x80 | (u & 0x3F));
    }
}

// next code point of a UTF-8 string, or 0xFFFD for a bad sequence (one byte eaten)
uint32_t nextUtf8(const std::string& s, size_t& i) {
    unsigned char c = (unsigned char)s[i++];
    if (c < 0x80) return c;
    int      more = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : -1;
    uint32_t u = more == 3 ? (c & 0x07) : more == 2 ? (c & 0x0F) : (c & 0x1F);
    if (more < 0 || i + more > s.size()) return 0xFFFD;
    for (int k = 0; k < more; k++) {
        unsigned char d = (unsigned char)s[i + k];
        if ((d & 0xC0) != 0x80) return 0xFFFD;
        u = (u << 6) | (d & 0x3F);
    }
    i += more;
    return u;
}

bool idChar(char c) { return std::isalnum((unsigned char)c) || c == '.' || c == '_' || c == '-'; }

bool partOk(const std::string& s, size_t lo, size_t hi, bool lowerOnly) {
    if (s.size() < lo || s.size() > hi) return false;
    for (char c : s) {
        if (lowerOnly ? !(std::islower((unsigned char)c) || std::isdigit((unsigned char)c) || c == '-') : !idChar(c))
            return false;
    }
    return true;
}

std::string cut(const std::string& s, size_t n) { return s.size() > n ? s.substr(0, n) : s; }

// at most n characters (code points, as RadNet counts them); a cut text ends " [truncated]" like RadNet's own
std::string utf8Cut(const std::string& s, size_t n) {
    size_t chars = 0, i = 0;
    std::vector<size_t> starts;
    while (i < s.size()) {
        starts.push_back(i);
        unsigned char c = (unsigned char)s[i];
        i += c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;
        chars++;
    }
    if (chars <= n) return s;
    const std::string tag = " [truncated]";
    size_t keep = n > tag.size() ? n - tag.size() : 0;
    return s.substr(0, starts[keep]) + (n > tag.size() ? tag : std::string());
}

// an AXIS one-line header field (from, subj): CP437, no control bytes, 72 at most
std::string headerField(const std::string& utf8, const std::string& fallback) {
    std::string t = utf8ToCp437(plainText(utf8));
    for (char& c : t)
        if ((unsigned char)c < 32) c = ' ';
    size_t a = t.find_first_not_of(' '), b = t.find_last_not_of(' ');
    t = a == std::string::npos ? std::string() : t.substr(a, b - a + 1);
    return t.empty() ? fallback : cut(t, 72);
}
}  // namespace

std::string cp437ToUtf8(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (unsigned char c : s) {
        if (c == '\n' || c == '\t') out += (char)c;
        else if (c < 0x20 || c == 0x7f) out += ' ';
        else if (c < 0x80) out += (char)c;
        else putUtf8(out, kCp437[c - 0x80]);
    }
    return out;
}

std::string utf8ToCp437(const std::string& s) {
    static std::map<uint32_t, unsigned char> back;
    if (back.empty())
        for (int i = 0; i < 128; i++) back[kCp437[i]] = (unsigned char)(0x80 + i);
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size();) {
        uint32_t u = nextUtf8(s, i);
        if (u == '\n' || u == '\t') out += (char)u;
        else if (u < 0x20 || u == 0x7f) out += ' ';
        else if (u < 0x80) out += (char)u;
        else if (u == 0x2014 || u == 0x2013) out += '-';
        else if (u == 0x2018 || u == 0x2019) out += '\'';
        else if (u == 0x201C || u == 0x201D) out += '"';
        else if (u == 0x2026) out += "...";
        else {
            auto it = back.find(u);
            out += it == back.end() ? '?' : (char)it->second;
        }
    }
    return out;
}

std::string plainText(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); i++) {
        char c = s[i];
        if (c == '\x1b') {  // ESC [ params final, or a two-byte ESC sequence
            if (i + 1 < s.size() && s[i + 1] == '[') {
                size_t j = i + 2;
                while (j < s.size() && ((unsigned char)s[j] < 0x40 || (unsigned char)s[j] > 0x7e)) j++;
                i = j;
            } else {
                i++;
            }
            continue;
        }
        if (c == '|' && i + 2 < s.size() && std::isdigit((unsigned char)s[i + 1]) && std::isdigit((unsigned char)s[i + 2]) &&
            (s[i + 1] - '0') * 10 + (s[i + 2] - '0') <= 23) {
            i += 2;
            continue;
        }
        out += c;
    }
    return out;
}

bool originOk(const std::string& origin) {
    size_t a = origin.find(':');
    if (a == std::string::npos) return false;
    size_t b = origin.find(':', a + 1);
    if (b == std::string::npos) return false;
    return partOk(origin.substr(0, a), 1, 16, true) && partOk(origin.substr(a + 1, b - a - 1), 1, 64, false) &&
           partOk(origin.substr(b + 1), 1, 128, false);
}

std::string idPart(const std::string& s) {
    std::string out;
    for (char c : s) out += idChar(c) ? c : '_';
    return cut(out.empty() ? std::string("_") : out, 64);
}

std::string jsonQuote(const std::string& utf8) {
    std::string out = "\"";
    for (size_t i = 0; i < utf8.size();) {
        size_t   at = i;
        uint32_t u = nextUtf8(utf8, i);
        switch (u) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\t': out += "\\t"; break;
        case '\r': out += "\\r"; break;
        default:
            if (u < 0x20) {
                char b[8];
                std::snprintf(b, sizeof b, "\\u%04x", (unsigned)u);
                out += b;
            } else if (u == 0xFFFD && !(i - at == 3 && (unsigned char)utf8[at] == 0xEF)) {
                putUtf8(out, 0xFFFD);  // a bad byte in the input: never pass invalid UTF-8 on
            } else {
                out.append(utf8, at, i - at);
            }
        }
    }
    return out + "\"";
}

bool jsonFlat(const std::string& line, std::map<std::string, std::string>& out, std::map<std::string, bool>& isString) {
    out.clear();
    isString.clear();
    size_t i = 0;
    auto   ws = [&]() {
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t' || line[i] == '\r')) i++;
    };
    auto str = [&](std::string& v) -> bool {
        if (i >= line.size() || line[i] != '"') return false;
        i++;
        v.clear();
        while (i < line.size() && line[i] != '"') {
            char c = line[i++];
            if ((unsigned char)c < 0x20) return false;
            if (c != '\\') {
                v += c;
                continue;
            }
            if (i >= line.size()) return false;
            char e = line[i++];
            switch (e) {
            case '"': v += '"'; break;
            case '\\': v += '\\'; break;
            case '/': v += '/'; break;
            case 'b': v += '\b'; break;
            case 'f': v += '\f'; break;
            case 'n': v += '\n'; break;
            case 'r': v += '\r'; break;
            case 't': v += '\t'; break;
            case 'u': {
                auto hex4 = [&](uint32_t& u) {
                    if (i + 4 > line.size()) return false;
                    u = 0;
                    for (int k = 0; k < 4; k++) {
                        char h = line[i++];
                        u <<= 4;
                        if (h >= '0' && h <= '9') u |= h - '0';
                        else if (h >= 'a' && h <= 'f') u |= h - 'a' + 10;
                        else if (h >= 'A' && h <= 'F') u |= h - 'A' + 10;
                        else return false;
                    }
                    return true;
                };
                uint32_t u;
                if (!hex4(u)) return false;
                if (u >= 0xD800 && u <= 0xDBFF) {  // a surrogate pair
                    uint32_t lo;
                    if (i + 2 > line.size() || line[i] != '\\' || line[i + 1] != 'u') return false;
                    i += 2;
                    if (!hex4(lo) || lo < 0xDC00 || lo > 0xDFFF) return false;
                    u = 0x10000 + ((u - 0xD800) << 10) + (lo - 0xDC00);
                } else if (u >= 0xDC00 && u <= 0xDFFF) {
                    return false;
                }
                putUtf8(v, u);
                break;
            }
            default: return false;
            }
        }
        if (i >= line.size()) return false;
        i++;  // closing quote
        return true;
    };
    ws();
    if (i >= line.size() || line[i++] != '{') return false;
    ws();
    if (i < line.size() && line[i] == '}') {
        i++;
    } else {
        for (;;) {
            std::string k, v;
            ws();
            if (!str(k)) return false;
            ws();
            if (i >= line.size() || line[i++] != ':') return false;
            ws();
            bool quoted = i < line.size() && line[i] == '"';
            if (quoted) {
                if (!str(v)) return false;
            } else if (line.compare(i, 4, "null") == 0) {
                v = "null";
                i += 4;
            } else {
                size_t s = i;
                while (i < line.size() && (std::isdigit((unsigned char)line[i]) || line[i] == '-' || line[i] == '.' ||
                                           line[i] == 'e' || line[i] == 'E' || line[i] == '+'))
                    i++;
                if (i == s) return false;  // objects, arrays, true/false: not in a flat record
                v = line.substr(s, i - s);
            }
            if (out.count(k)) return false;  // a repeated key is ambiguous
            out[k] = v;
            isString[k] = quoted;
            ws();
            if (i < line.size() && line[i] == ',') {
                i++;
                continue;
            }
            if (i < line.size() && line[i] == '}') {
                i++;
                break;
            }
            return false;
        }
    }
    ws();
    return i == line.size();
}

ExportResult exportAreas(MessageStore& store, const std::string& station, const std::vector<ExportArea>& areas,
                         const std::function<std::vector<int>(const std::string& area)>& imported,
                         const std::function<void(const std::string& line)>& out) {
    ExportResult             r;
    std::vector<std::string> records;
    std::set<std::string>    boardsSent, postsSent;
    if (!partOk(station, 1, 64, false)) {
        r.error = "station must be 1 to 64 of A-Z a-z 0-9 . _ -";
        return r;
    }
    auto board = [&](const std::string& origin, const std::string& title) {
        if (!boardsSent.insert(origin).second) return;
        records.push_back("{\"author\":" + jsonQuote(station) + ",\"origin\":" + jsonQuote(origin) +
                          ",\"title\":" + jsonQuote(utf8Cut(cp437ToUtf8(plainText(title)), 100)) + ",\"type\":\"board\"}");
        r.boards++;
    };
    for (const ExportArea& a : areas) {
        std::vector<int> mine = imported ? imported(a.id) : std::vector<int>();
        std::set<int>    skip(mine.begin(), mine.end());
        std::vector<std::pair<int, uint32_t>> nums;  // (num, at) from a header scan, bodies read one at a time
        store.scanHeaders(a.id, [&](const Message& m) {
            nums.push_back({m.num, m.at});
            return true;
        });
        for (auto [num, at] : nums) {
            if (skip.count(num)) {
                r.skipped++;
                continue;
            }
            Message m;
            if (!store.readAt(a.id, at, num, m) && !store.read(a.id, num, m)) continue;
            std::string origin, boardOrigin, boardTitle = a.title.empty() ? a.id : a.title;
            net::NetPost p;
            if (!m.net.empty() && net::decode(m.net, p) && !p.id.empty() && !p.board.empty()) {
                origin = "axis:" + idPart(p.board) + ":" + idPart(p.id);
                boardOrigin = "axis:net:" + idPart(p.area.empty() ? a.id : p.area);
            } else {
                origin = "axis:" + station + ":" + idPart(a.id) + "." + std::to_string(m.num);
                boardOrigin = "axis:" + station + ":" + idPart(a.id);
            }
            if (!originOk(origin) || !originOk(boardOrigin) || !postsSent.insert(origin).second) continue;
            board(boardOrigin, boardTitle);
            std::string author = cp437ToUtf8(plainText(m.from)), title = cp437ToUtf8(plainText(m.subject));
            std::string body = cp437ToUtf8(plainText(m.body));
            body = utf8Cut(body, 4000);
            author = utf8Cut(author, 32);
            title = utf8Cut(title, 100);
            records.push_back("{\"author\":" + jsonQuote(author) + ",\"body\":" + jsonQuote(body) + ",\"board\":" +
                              jsonQuote(boardOrigin) + ",\"origin\":" + jsonQuote(origin) +
                              ",\"parent\":null,\"title\":" + jsonQuote(title) + ",\"type\":\"post\"}");
            r.posts++;
        }
    }
    out("{\"format\":\"radnet-bridge\",\"network\":\"axis\",\"records\":" + std::to_string(records.size()) +
        ",\"station\":" + jsonQuote(station) + ",\"version\":1}");
    for (const std::string& line : records) out(line);
    return r;
}

std::map<std::string, int> parseLedger(const std::string& text) {
    std::map<std::string, int> seen;
    size_t                     i = 0;
    while (i < text.size()) {
        size_t      e = text.find('\n', i);
        std::string line = text.substr(i, e == std::string::npos ? std::string::npos : e - i);
        i = e == std::string::npos ? text.size() : e + 1;
        size_t sp = line.find(' ');
        if (sp == std::string::npos) continue;
        std::string origin = line.substr(sp + 1);
        if (!origin.empty() && origin.back() == '\r') origin.pop_back();
        int num = std::atoi(line.c_str());
        if (num > 0 && originOk(origin)) seen[origin] = num;
    }
    return seen;
}

ImportResult importBundle(const std::string& bundle, MessageStore& store, const std::string& area,
                          const std::map<std::string, int>& seenIn, const std::string& now,
                          const std::function<void(int num, const std::string& origin)>& remember) {
    ImportResult r;
    if (bundle.size() > kMaxBytes) {
        r.fatal = "bundle too large";
        return r;
    }
    std::vector<std::string> lines;
    for (size_t i = 0; i < bundle.size();) {
        size_t e = bundle.find('\n', i);
        std::string line = bundle.substr(i, e == std::string::npos ? std::string::npos : e - i);
        i = e == std::string::npos ? bundle.size() : e + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.size() > kMaxLine) {
            r.fatal = "bundle line too long";
            return r;
        }
        lines.push_back(line);
    }
    while (!lines.empty() && lines.back().find_first_not_of(" \t") == std::string::npos) lines.pop_back();
    if (lines.empty()) {
        r.fatal = "empty bundle";
        return r;
    }
    std::map<std::string, std::string> f;
    std::map<std::string, bool>        q;
    if (!jsonFlat(lines[0], f, q) || f["format"] != "radnet-bridge" || f["version"] != "1" || q["version"] ||
        !partOk(f["network"], 1, 16, true)) {
        r.fatal = "not a radnet-bridge v1 bundle";
        return r;
    }
    if (lines.size() - 1 > kMaxRecords) {
        r.fatal = "too many records";
        return r;
    }
    std::map<std::string, int>         seen = seenIn;
    std::map<std::string, std::string> titles;  // board origin -> title
    auto reject = [&](size_t n, const std::string& why) {
        r.rejected++;
        if (r.errors.size() < 20) r.errors.push_back("line " + std::to_string(n) + ": " + why);
    };
    for (size_t n = 1; n < lines.size(); n++) {
        if (lines[n].find_first_not_of(" \t") == std::string::npos) continue;
        if (!jsonFlat(lines[n], f, q)) {
            reject(n + 1, "not a flat JSON object");
            continue;
        }
        const std::string& type = f["type"];
        const std::string& origin = f["origin"];
        if (!q["origin"] || !originOk(origin)) {
            reject(n + 1, "invalid origin reference");
            continue;
        }
        if (type == "board") {
            titles[origin] = q["title"] ? f["title"] : std::string();
            continue;
        }
        if (type != "post") {
            reject(n + 1, "unknown record type");
            continue;
        }
        if (!q["board"] || !originOk(f["board"])) {
            reject(n + 1, "invalid board reference");
            continue;
        }
        if (origin.compare(0, 5, "axis:") == 0) {  // an AXIS post already: it lives on its board and on the nets
            r.fromAxis++;
            continue;
        }
        if (seen.count(origin)) {
            r.duplicates++;
            continue;
        }
        Message m;
        std::string title = headerField(q["title"] ? f["title"] : "", "(no subject)");
        auto        bt = titles.find(f["board"]);
        if (bt != titles.end() && !bt->second.empty()) title = headerField("[" + bt->second + "] ", "") + " " + title;
        m.subject = cut(title, 72);
        m.from = headerField(q["author"] ? f["author"] : "", "unknown");
        m.to = "All";
        m.date = now;
        std::string body = utf8ToCp437(plainText(q["body"] ? f["body"] : ""));
        body += "\n\n[via " + headerField(origin.substr(0, origin.find(':')), "?") + ", unverified]";
        m.body = body;
        int num = store.post(area, m);
        if (num <= 0) {
            reject(n + 1, "the message base did not take it");
            continue;
        }
        seen[origin] = num;
        if (remember) remember(num, origin);
        r.posts++;
    }
    return r;
}

}  // namespace axis::bbs::bridge
