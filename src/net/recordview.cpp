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

std::string recordCheck(const std::string& signedLine, const NetState& st) {
    if (signedLine.empty()) return "local action, not signed";
    std::string who = "?";
    int         ok = 0;  // 1 checks, 2 no card held
    NetPost     p;
    NetCard     c;
    NetNotice   n;
    NetApp      a;
    NetVote     v;
    switch (signedLine[0]) {
        case 'P':
            if (decode(signedLine, p)) {
                const NetCard* held = st.card(p.board);
                who = p.board;
                ok = !held ? 2 : verifyPost(p, held->pub);
            }
            break;
        case 'C':
            if (decode(signedLine, c)) who = c.board, ok = verifyCard(c);
            break;
        case 'N':
            if (decode(signedLine, n)) who = "Rev", ok = verifyNotice(n);
            break;
        case 'A':
            if (decode(signedLine, a)) who = a.board, ok = verifyApp(a);
            break;
        case 'V':
            if (decode(signedLine, v)) who = v.voter, ok = verifyVote(v);
            break;
    }
    return "signed by " + netSafe(who) + (ok == 2 ? ", no card held to check it" : ok ? ", checks" : ", does NOT check");
}

void recordFields(const std::string& signedLine, const std::function<void(const std::string&)>& line) {
    auto f = [&line](const char* name, const std::string& value) { line(std::string(name) + " " + netSafe(value)); };
    auto lines = [&line](const char* name, const std::string& text) {  // a field that may hold line breaks
        line(name);
        for (size_t at = 0; at <= text.size();) {
            size_t nl = std::min(text.find('\n', at), text.size());
            line("  " + netSafe(text.substr(at, nl - at)));
            at = nl + 1;
        }
    };
    if (signedLine.empty()) return;
    NetPost   p;
    NetCard   c;
    NetNotice n;
    NetApp    a;
    NetVote   v;
    switch (signedLine[0]) {
        case 'P':
            if (!decode(signedLine, p)) return;
            f("id", p.id), f("area", p.area), f("board", p.board), f("from", p.from), f("to", p.to), f("subject", p.subject);
            f("date", netDateText(p.date)), f("path", p.path), lines("body", p.body);
            break;
        case 'C':
            if (!decode(signedLine, c)) return;
            f("board", c.board), f("name", c.name), f("sysop", c.sysop), f("location", c.location), f("host", c.host);
            f("port", std::to_string(c.port)), f("mac", c.mac), f("links", c.links), f("seq", std::to_string(c.seq)), f("pub", c.pub);
            break;
        case 'N':
            if (!decode(signedLine, n)) return;
            f("notice", n.kind), f("target", n.target), f("value", n.value), f("seq", std::to_string(n.seq)), f("key", std::to_string(n.key));
            break;
        case 'A':
            if (!decode(signedLine, a)) return;
            f("application", appId(a)), f("name", a.name), f("sysop", a.sysop), f("location", a.location), f("mac", a.mac);
            f("date", netDateText(a.date)), lines("blurb", a.blurb);
            break;
        case 'V':
            if (!decode(signedLine, v)) return;
            f("voter", v.voter), f("on", v.app + "-a" + std::to_string(v.appSeq)), f("vote", v.yes ? "yes" : "no");
            f("comment", v.comment), f("seq", std::to_string(v.seq)), f("date", netDateText(v.date));
            break;
    }
}

}  // namespace axis::net
