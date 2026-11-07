// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#pragma GCC optimize("Os")
#include "bbs/axisnet.h"

#include "bbs/theme.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>

namespace axis::bbs {

namespace {
std::string clean(const std::string& s, size_t n) {
    std::string o;
    o.reserve(n < s.size() ? n : s.size());
    for (unsigned char c : s) {
        if (o.size() >= n) break;
        if (c < 0x20 || c >= 0x7f || c == '|') o += ' ';
        else o += (char)c;
    }
    while (!o.empty() && o.back() == ' ') o.pop_back();
    return o;
}
std::string field(const std::string& line, int n) {
    size_t pos = 0;
    for (int i = 0; i < n; i++) {
        pos = line.find('|', pos);
        if (pos == std::string::npos) return {};
        ++pos;
    }
    size_t bar = line.find('|', pos);
    return line.substr(pos, bar == std::string::npos ? std::string::npos : bar - pos);
}
bool sameHost(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); i++)
        if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i])) return false;
    return true;
}
std::string ago(uint32_t seen, uint32_t now) {
    if (!seen || !now || now < seen) return {};
    uint32_t s = now - seen;
    if (s < 90) return "just now";
    if (s < 3600) return std::to_string(s / 60) + "m ago";
    if (s < 86400) return std::to_string(s / 3600) + "h ago";
    return std::to_string(s / 86400) + "d ago";
}
}  // namespace

std::string axisLine(const AxisCard& c) {
    std::string o = clean(c.name, 32) + "|" + clean(c.host, 48) + "|" + std::to_string(c.port > 0 ? c.port : 23) + "|" +
                    clean(c.location, 24) + "|" + clean(c.sysop, 24) + "|";
    o += c.online < 0 ? "" : std::to_string(c.online);
    o += "|" + (c.seen ? std::to_string(c.seen) : "") + "|" + clean(c.blurb, 24);
    return o;
}

bool axisParse(const std::string& line, AxisCard& c) {
    c = AxisCard();
    if (line.empty() || line.find('|') == std::string::npos) return false;
    c.name = field(line, 0);
    c.host = field(line, 1);
    std::string port = field(line, 2), on = field(line, 5), seen = field(line, 6);
    c.location = field(line, 3);
    c.sysop = field(line, 4);
    c.blurb = field(line, 7);
    c.port = port.empty() ? 23 : std::atoi(port.c_str());
    if (c.port < 1 || c.port > 65535) c.port = 23;
    c.online = on.empty() ? -1 : std::atoi(on.c_str());
    c.seen = seen.empty() ? 0 : (uint32_t)std::strtoul(seen.c_str(), nullptr, 10);
    return !c.host.empty();
}

void axisMerge(std::vector<std::string>& lines, const AxisCard& c, size_t cap) {
    if (c.host.empty()) return;
    std::string neu = axisLine(c);
    for (size_t i = 0; i < lines.size(); i++) {
        AxisCard had;
        if (!axisParse(lines[i], had)) continue;
        if (sameHost(had.host, c.host) && had.port == c.port) {
            lines[i] = neu;
            return;
        }
    }
    lines.push_back(neu);
    while (lines.size() > cap) lines.erase(lines.begin());
}

bool axisHostPort(const std::string& text, std::string& host, int& port) {
    host.clear();
    port = 23;
    std::string s = text;
    while (!s.empty() && s.front() == ' ') s.erase(s.begin());
    while (!s.empty() && s.back() == ' ') s.pop_back();
    size_t col = s.rfind(':');
    std::string h = col == std::string::npos ? s : s.substr(0, col);
    if (h.empty() || h.size() > 48) return false;
    if (h.find(':') != std::string::npos) return false;
    for (unsigned char c : h)
        if (!std::isalnum(c) && c != '.' && c != '-') return false;
    if (col != std::string::npos) {
        std::string ps = s.substr(col + 1);
        if (ps.empty() || ps.size() > 5) return false;
        int p = 0;
        for (char c : ps) {
            if (c < '0' || c > '9') return false;
            p = p * 10 + (c - '0');
        }
        if (p < 1 || p > 65535) return false;
        port = p;
    }
    host = h;
    return true;
}

std::string axisGreet(const AxisCard& self, const std::vector<std::string>& known, const std::vector<std::string>& notes) {
    std::string o = "AXISNET 1\nHELLO " + axisLine(self) + "\nLIST\n";
    for (size_t i = 0; i < known.size() && i < 24; i++) o += known[i] + "\n";
    for (size_t i = 0; i < notes.size() && i < 6; i++) o += "NOTE " + notes[i] + "\n";
    o += "END\n";
    return o;
}

AxisHeard axisTake(const std::string& reply, std::vector<std::string>& lines, const std::string& ourHost) {
    AxisHeard heard;
    if (reply.compare(0, 8, "AXISNET ") != 0) return heard;
    heard.ok = true;
    size_t pos = 0;
    bool   list = false;
    while (pos < reply.size()) {
        size_t e = reply.find('\n', pos);
        if (e == std::string::npos) e = reply.size();
        std::string row = reply.substr(pos, e - pos);
        if (!row.empty() && row.back() == '\r') row.pop_back();
        pos = e + 1;
        if (row == "END") break;
        if (row == "LIST") {
            list = true;
            continue;
        }
        if (row.compare(0, 6, "HELLO ") == 0 || (list && !row.empty() && row.compare(0, 5, "NOTE ") != 0)) {
            AxisCard c;
            if (axisParse(row.compare(0, 6, "HELLO ") == 0 ? row.substr(6) : row, c) && !sameHost(c.host, ourHost)) axisMerge(lines, c);
            continue;
        }
        if (row.compare(0, 5, "NOTE ") == 0) {
            std::string n = row.substr(5);
            if (std::count(n.begin(), n.end(), '|') >= 2) heard.notes.push_back(n);
        }
    }
    return heard;
}

AxisNetMenu::AxisNetMenu(std::string& out, std::function<AxisCard()> self, const std::string& handle,
                         std::function<std::vector<std::string>(const std::string&)> get,
                         std::function<void(const std::string&, const std::string&)> add, std::function<uint32_t()> now)
    : out_(out), self_(std::move(self)), handle_(handle), get_(std::move(get)), add_(std::move(add)), now_(std::move(now)) {}

std::vector<AxisCard> AxisNetMenu::load() const {
    std::vector<AxisCard> v;
    AxisCard me = self_ ? self_() : AxisCard();
    if (me.name.empty()) me.name = "this board";
    if (me.port < 1) me.port = 23;
    v.push_back(me);
    if (!get_) return v;
    for (const std::string& line : get_("axisnet")) {
        AxisCard c;
        if (!axisParse(line, c)) continue;
        if (sameHost(c.host, me.host) && c.port == me.port) continue;
        v.push_back(c);
        if (v.size() >= 25) break;
    }
    return v;
}

void AxisNetMenu::setStatus(const std::string& line) { out_ += listScreenStatus(line); }

void AxisNetMenu::status() {
    if (cards_.empty()) return;
    int at = list_.current();
    if (at < 0 || (size_t)at >= cards_.size()) at = 0;
    const AxisCard& c = cards_[(size_t)at];
    std::string     s = (at == 0 ? "This board.  " : "");
    if (!c.sysop.empty()) s += "sysop " + c.sysop + "  ";
    if (c.online >= 0) s += std::to_string(c.online) + " on  ";
    uint32_t now = now_ ? now_() : 0;
    std::string when = ago(c.seen, now);
    if (!when.empty()) s += when + "  ";
    if (!c.location.empty() && at != 0) s += c.location + "  ";
    if (at == 0 && c.host.empty()) s += "no public hostname yet  ";
    if (!c.blurb.empty()) s += c.blurb;
    if (get_) {
        int notes = 0;
        std::string last;
        for (const std::string& n : get_("netnotes")) {
            std::string to = field(n, 2);
            if (!sameHost(to, c.name) && !sameHost(to, c.host)) continue;
            notes++;
            last = field(n, 1) + ": " + field(n, 3);
        }
        if (notes) s += "  " + std::to_string(notes) + " note" + (notes == 1 ? "" : "s");
        if (!last.empty()) s += "  " + last;
    }
    setStatus(s);
}

void AxisNetMenu::show() {
    st_ = St::List;
    line_.clear();
    cards_ = load();
    std::vector<std::string> rows;
    rows.reserve(cards_.size());
    for (size_t i = 0; i < cards_.size(); i++) {
        const AxisCard& c = cards_[i];
        std::string     addr = c.host.empty() ? std::string("no public address") : c.host + ":" + std::to_string(c.port);
        std::string     where = i == 0 ? "you are here" : c.location;
        char            b[80];
        std::snprintf(b, sizeof b, "%-22.22s %-24.24s %-26.26s", c.name.empty() ? c.host.c_str() : c.name.c_str(), addr.c_str(), where.c_str());
        rows.emplace_back(b);
    }
    int keep = list_.size() ? list_.current() : 0;
    if (keep < 0) keep = 0;
    if ((size_t)keep >= rows.size()) keep = 0;
    list_.setItems(std::move(rows), (size_t)keep);
    out_ += listScreenTop("AXIS NET", "Board                  Address                 Where");
    out_ += list_.draw();
    status();
    out_ += listScreenKeys("arrows move   Enter calls   " + theme::key("A", "dd") + "  " + theme::key("N", "ote") + "  " + theme::key("R", "efresh") +
                           "  " + theme::key("Q", "uit") + " or Esc");
}

void AxisNetMenu::begin() {
    calling_ = false;
    callHost_.clear();
    show();
}

bool AxisNetMenu::refresh() {
    if (st_ != St::List || calling_ || list_.midKey()) return false;
    show();
    return true;
}

bool AxisNetMenu::tick(uint32_t nowMs, uint32_t lastInput) {
    if (st_ != St::List || !list_.midKey() || nowMs - lastInput <= 300) return true;
    list_.keyTimeout();
    return !list_.escaped();
}

bool AxisNetMenu::takeProbe() {
    bool w = probe_;
    probe_ = false;
    return w;
}

std::string AxisNetMenu::where() const {
    if (calling_) return "calling " + callHost_;
    if (st_ == St::Add) return "adding a board";
    if (st_ == St::Note) return "writing a note";
    return "board list";
}

bool AxisNetMenu::lineKey(char c, size_t max) {
    if (c == 8 || c == 127) {
        if (!line_.empty()) line_.pop_back(), out_ += "\b \b";
        return false;
    }
    if (c != '\r' && c != '\n') {
        if ((unsigned char)c >= 0x20 && (unsigned char)c < 0x7f && c != '|' && line_.size() < max) {
            line_ += c;
            out_ += c;
        }
        return false;
    }
    return true;
}

bool AxisNetMenu::key(char c) {
    if (calling_) return true;
    if (st_ == St::Add || st_ == St::Note) {
        if ((c | 0x20) == 'q' && line_.empty()) {
            show();
            return true;
        }
        if (!lineKey(c, st_ == St::Add ? 48 : 70)) return true;
        std::string got = line_;
        line_.clear();
        if (got.empty()) {
            show();
            return true;
        }
        if (st_ == St::Add) {
            std::string host;
            int         port = 23;
            if (!axisHostPort(got, host, port) || !add_) {
                show();
                setStatus("That address is not a host or host:port.");
                return true;
            }
            AxisCard card;
            card.name = host;
            card.host = host;
            card.port = port;
            card.blurb = "added here";
            add_("axisnet", axisLine(card));
            probe_ = true;
            show();
            setStatus("Added " + host + ". Asking it when the link is free.");
            return true;
        }
        int at = list_.current();
        if (at < 0 || (size_t)at >= cards_.size() || !add_) {
            show();
            return true;
        }
        const AxisCard& to = cards_[(size_t)at];
        std::string     who = handle_.empty() ? std::string("someone") : handle_;
        std::string     dest = to.name.empty() ? to.host : to.name;
        uint32_t        t = now_ ? now_() : 0;
        add_("netnotes", std::to_string(t) + "|" + clean(who, 30) + "|" + clean(dest, 32) + "|" + clean(got, 70));
        probe_ = true;
        show();
        setStatus("Note kept for " + dest + ".");
        return true;
    }
    if (!list_.midKey()) {
        char k = (char)(c | 0x20);
        if (k == 'q') return false;
        if (k == 'a') {
            st_ = St::Add;
            line_.clear();
            setStatus("");
            out_ += listScreenKeys("Board address, host or host:port. Enter stores it, empty Enter cancels.");
            out_ += std::string("\x1b[24;3H") + theme::WHITE;
            return true;
        }
        if (k == 'r') {
            probe_ = true;
            setStatus("Asking the boards we know.");
            return true;
        }
        if (k == 'n') {
            if (cards_.empty()) return true;
            st_ = St::Note;
            line_.clear();
            int at = list_.current();
            if (at < 0 || (size_t)at >= cards_.size()) at = 0;
            std::string dest = cards_[(size_t)at].name.empty() ? cards_[(size_t)at].host : cards_[(size_t)at].name;
            setStatus("");
            out_ += listScreenKeys("Note to " + dest + ". Enter sends it.");
            out_ += std::string("\x1b[24;3H") + theme::WHITE;
            return true;
        }
    }
    int pick = list_.key(c);
    out_ += list_.takeRedraw();
    if (list_.escaped()) return false;
    status();
    if (pick < 0 || cards_.empty()) return true;
    if (pick == 0 || (size_t)pick >= cards_.size() || cards_[(size_t)pick].host.empty()) {
        setStatus("This is your board. Move the bar to another one and Enter calls it.");
        return true;
    }
    callHost_ = cards_[(size_t)pick].host;
    callPort_ = cards_[(size_t)pick].port;
    calling_ = true;
    out_ += theme::CLS;
    out_ += "\r\n\r\n   " + theme::fade("Calling") + " " + theme::WHITE + cards_[(size_t)pick].name + theme::RESET + "\r\n";
    out_ += std::string("   ") + theme::HOT + callHost_ + ":" + std::to_string(callPort_) + theme::RESET + "\r\n\r\n";
    out_ += std::string("   ") + theme::TEXT + "You stay on that board until Ctrl-\\ or ~. on a line by itself.\r\n";
    return true;
}

void AxisNetMenu::callDone(bool ok, const char* why) {
    calling_ = false;
    callHost_.clear();
    show();
    setStatus(why ? why : ok ? "Back from that board." : "Could not reach that board.");
}

}  // namespace axis::bbs
