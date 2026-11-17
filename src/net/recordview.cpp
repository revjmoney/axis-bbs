// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// Reading the net record back (net/recordview.h).
#pragma GCC optimize("Os")
#include "net/recordview.h"

#include <algorithm>
#include <cctype>
#include <cstring>

#include "net/areas.h"
#include "net/record.h"

namespace axis::net {

std::string netSafe(std::string s) {
    for (char& c : s)
        if ((unsigned char)c < 0x20 || c == 0x7f) c = '?';
    return s;
}

bool recordMatches(const std::string& line, RecordKind k, const std::string& text) {
    if (k != RecordKind::All) {
        size_t end = std::min(line.find('\x1f'), line.size());  // the head: "<unix> <who> <what>"
        size_t s1 = line.find(' '), at = s1 < end ? line.find(' ', s1 + 1) : std::string::npos;
        auto   is = [&](const char* w) {
            size_t n = std::strlen(w);
            return at < end && at + 1 + n <= end && line.compare(at + 1, n, w) == 0;
        };
        RecordKind got = is("app ") || is("vote ") || is("notice admit ") || is("notice deny ") || is("notice veto ") || is("notice votes ")
                             ? RecordKind::Votes
                         : is("card ") || is("member ") ? RecordKind::Members
                                                        : RecordKind::Actions;
        if (got != k) return false;
    }
    if (text.empty()) return true;
    return std::search(line.begin(), line.end(), text.begin(), text.end(), [](char a, char b) {
               return std::tolower((unsigned char)a) == std::tolower((unsigned char)b);
           }) != line.end();
}

namespace {
struct Out {
    RecordLineFn fn;
    void*        ctx;
};

void field(const Out& o, const char* name, const std::string& value) {
    if (o.fn) o.fn(o.ctx, std::string(name) + " " + netSafe(value));
}

// A field that may hold line breaks: a line of its own per line, at most kRecordFieldLines, then how many more.
void lines(const Out& o, const char* name, const std::string& text) {
    if (!o.fn) return;
    o.fn(o.ctx, name);
    size_t at = 0;
    for (size_t shown = 0; at <= text.size() && shown < kRecordFieldLines; shown++) {
        size_t nl = std::min(text.find('\n', at), text.size());
        o.fn(o.ctx, "  " + netSafe(text.substr(at, nl - at)));
        at = nl + 1;
    }
    if (at <= text.size())
        o.fn(o.ctx, "  (" + std::to_string(1 + std::count(text.begin() + at, text.end(), '\n')) + " more lines)");
}

// Decodes the line once: its fields go to o (if o.fn), and with st the check is worked out and returned.
std::string view(const std::string& sl, const NetState* st, const Out& o) {
    if (sl.empty()) return "local action, not signed";
    std::string who = "?";
    int         ok = 0;  // 1 checks, 2 no card held
    switch (sl[0]) {
        case 'P': {
            NetPost p;
            if (!decode(sl, p)) break;
            who = p.board;
            if (st) {
                const NetCard* held = st->card(p.board);
                ok = !held ? 2 : verifyPost(p, held->pub);
            }
            field(o, "id", p.id), field(o, "area", p.area), field(o, "board", p.board), field(o, "from", p.from), field(o, "to", p.to);
            field(o, "subject", p.subject), field(o, "date", netDateText(p.date)), field(o, "path", p.path), lines(o, "body", p.body);
            break;
        }
        case 'C': {
            NetCard c;
            if (!decode(sl, c)) break;
            who = c.board, ok = st && verifyCard(c);
            field(o, "board", c.board), field(o, "name", c.name), field(o, "sysop", c.sysop), field(o, "location", c.location);
            field(o, "host", c.host), field(o, "port", std::to_string(c.port)), field(o, "mac", c.mac), field(o, "links", c.links);
            field(o, "seq", std::to_string(c.seq)), field(o, "pub", c.pub);
            break;
        }
        case 'N': {
            NetNotice n;
            if (!decode(sl, n)) break;
            who = "Rev", ok = st && verifyNotice(n);
            field(o, "notice", n.kind), field(o, "target", n.target), field(o, "value", n.value), field(o, "seq", std::to_string(n.seq));
            field(o, "key", std::to_string(n.key));
            break;
        }
        case 'A': {
            NetApp a;
            if (!decode(sl, a)) break;
            who = a.board, ok = st && verifyApp(a);
            field(o, "application", appId(a)), field(o, "name", a.name), field(o, "sysop", a.sysop), field(o, "location", a.location);
            field(o, "mac", a.mac), field(o, "date", netDateText(a.date)), lines(o, "blurb", a.blurb);
            break;
        }
        case 'V': {
            NetVote v;
            if (!decode(sl, v)) break;
            who = v.voter, ok = st && verifyVote(v);
            field(o, "voter", v.voter), field(o, "on", v.app + "-a" + std::to_string(v.appSeq)), field(o, "vote", v.yes ? "yes" : "no");
            field(o, "comment", v.comment), field(o, "seq", std::to_string(v.seq)), field(o, "date", netDateText(v.date));
            break;
        }
        case 'L': {
            NetListing l;
            if (!decode(sl, l)) break;
            who = l.board;
            if (st) {
                const NetCard* held = st->card(l.board);
                ok = !held ? 2 : verifyListing(l, held->pub);
            }
            field(o, "file", l.name), field(o, "size", std::to_string(l.size)), field(o, "area", l.area), field(o, "uploader", l.uploader);
            field(o, "reach", l.reach), field(o, "gone", l.gone ? "yes" : "no"), field(o, "id", listingId(l));
            break;
        }
    }
    return "signed by " + netSafe(who) + (ok == 2 ? ", no card held to check it" : ok ? ", checks" : ", does NOT check");
}
}  // namespace

std::string recordCheck(const std::string& signedLine, const NetState& st) { return view(signedLine, &st, Out{nullptr, nullptr}); }

void recordFields(const std::string& signedLine, RecordLineFn line, void* ctx) {
    if (!signedLine.empty()) view(signedLine, nullptr, Out{line, ctx});
}

}  // namespace axis::net
