// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#pragma GCC optimize("Os")
#include "bbs/script.h"

#include "bbs/station.h"
#include "bbs/theme.h"

#include <cctype>

namespace axis::bbs {

namespace {
std::string lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}
std::string trim(std::string s) {
    while (!s.empty() && (s.back() == '\r' || s.back() == ' ' || s.back() == '\t')) s.pop_back();
    size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) i++;
    return s.substr(i);
}
void splitVerb(const std::string& line, std::string& verb, std::string& rest) {
    size_t i = 0;
    while (i < line.size() && line[i] != ' ' && line[i] != '\t') i++;
    verb = lower(line.substr(0, i));
    while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) i++;
    rest = line.substr(i);
}
std::string first(const std::string& s) {
    size_t i = 0;
    while (i < s.size() && s[i] != ' ' && s[i] != '\t') i++;
    return s.substr(0, i);
}
int takeInt(const std::string& s, int fallback) {
    if (s.empty()) return fallback;
    int n = 0;
    for (char ch : s) {
        if (ch < '0' || ch > '9') return fallback;
        n = n * 10 + (ch - '0');
        if (n > 100000) return fallback;
    }
    return n;
}
char fold(char c) { return c >= 'A' && c <= 'Z' ? (char)(c + 32) : c; }
}  // namespace

void ScriptProg::reset() {
    lines_.clear();
    vars_.clear();
    stack_.clear();
    ifs_.clear();
    items_.clear();
    picks_.clear();
    picked_.clear();
    askVar_.clear();
    askLine_.clear();
    pc_ = 0;
    steps_ = 0;
    pending_ = -1;
    wait_ = Wait::None;
    on_ = false;
    shown_ = false;
    skipMenu_ = false;
}

bool ScriptProg::load(const std::string& text, const World& world) {
    reset();
    world_ = world;
    std::string src = text.size() > 8000 ? text.substr(0, 8000) : text;
    for (size_t from = 0; from < src.size() && lines_.size() < 400;) {
        size_t nl = src.find('\n', from);
        std::string line = trim(src.substr(from, nl == std::string::npos ? std::string::npos : nl - from));
        from = nl == std::string::npos ? src.size() : nl + 1;
        if (line.empty() || line[0] == ';') continue;
        lines_.push_back(std::move(line));
    }
    on_ = !lines_.empty();
    return on_;
}

bool ScriptProg::offer(const std::vector<std::string>& names) {
    reset();
    picks_ = names;
    if (picks_.size() > 35) picks_.resize(35);
    on_ = !picks_.empty();
    wait_ = on_ ? Wait::Pick : Wait::None;
    return on_;
}

bool ScriptProg::live() const {
    for (const If& f : ifs_)
        if (!f.active) return false;
    return true;
}

std::string ScriptProg::value(const std::string& key) const {
    std::string k = lower(key);
    if (k == "handle") return world_.handle;
    if (k == "node") return world_.node;
    if (k == "bbs") return world_.bbs;
    if (k == "time") return world_.time;
    for (const auto& v : vars_)
        if (v.first == k) return v.second;
    return {};
}

void ScriptProg::setVar(const std::string& key, const std::string& val) {
    std::string k = lower(key);
    if (k == "handle" || k == "node" || k == "bbs" || k == "time" || k.empty()) return;
    std::string v = val.size() > 40 ? val.substr(0, 40) : val;
    for (auto& e : vars_)
        if (e.first == k) {
            e.second = v;
            return;
        }
    if (vars_.size() < 24) vars_.push_back({k, v});
}

std::string ScriptProg::expand(const std::string& in) const {
    std::string o;
    o.reserve(in.size());
    for (size_t i = 0; i < in.size(); i++) {
        if (in[i] == '@' && i + 1 < in.size() && std::isalpha((unsigned char)in[i + 1])) {
            size_t j = i + 1;
            while (j < in.size() && (std::isalnum((unsigned char)in[j]) || in[j] == '_')) j++;
            o += value(in.substr(i + 1, j - (i + 1)));
            i = j - 1;
        } else o += in[i];
    }
    return o;
}

int ScriptProg::findLabel(const std::string& name) const {
    std::string want = lower(name);
    for (size_t i = 0; i < lines_.size(); i++) {
        const std::string& s = lines_[i];
        if (!s.empty() && (s[0] == '#' || s[0] == ':') && lower(s.substr(1)) == want) return (int)i;
    }
    return -1;
}

bool ScriptProg::cond(const std::string& rest) const {
    std::string var, op, rhs;
    size_t i = 0;
    while (i < rest.size() && rest[i] != ' ' && rest[i] != '\t') var += rest[i++];
    while (i < rest.size() && (rest[i] == ' ' || rest[i] == '\t')) i++;
    while (i < rest.size() && rest[i] != ' ' && rest[i] != '\t') op += rest[i++];
    while (i < rest.size() && (rest[i] == ' ' || rest[i] == '\t')) i++;
    rhs = rest.substr(i);
    if (op == "==") op = "=";
    std::string got = value(var), want = expand(rhs);
    if (op == "=") return got == want;
    if (op == "!") return got != want;
    return false;
}

void ScriptProg::doElse() {
    if (ifs_.empty()) return;
    bool parent = true;
    for (size_t i = 0; i + 1 < ifs_.size(); i++)
        if (!ifs_[i].active) parent = false;
    If& f = ifs_.back();
    if (f.hadElse) {
        f.active = false;
        return;
    }
    f.hadElse = true;
    f.active = parent && !f.took;
}

void ScriptProg::paintPick(std::string& out) const {
    out += theme::pipes("|15Scripts|07\r\n\r\n");
    for (size_t i = 0; i < picks_.size(); i++) {
        char key = i < 9 ? (char)('1' + i) : (char)('A' + i - 9);
        out += std::string("   ") + key + "  " + picks_[i] + "\r\n";
    }
    out += "   Q  leave\r\n";
}

void ScriptProg::exec(const std::string& line, std::string& out) {
    std::string verb, rest;
    splitVerb(line, verb, rest);
    if (verb.empty() || verb[0] == '#' || verb[0] == ':') return;
    if (skipMenu_) {
        if (verb == "end") skipMenu_ = false;
        return;
    }
    if (!live()) {
        if (verb == "if") ifs_.push_back({false, false, false});
        else if (verb == "else") doElse();
        else if (verb == "end") {
            if (!ifs_.empty()) ifs_.pop_back();
        } else if (verb == "menu") skipMenu_ = true;
        return;
    }
    if (verb == "say") {
        out += theme::pipes(expand(rest)) + "\r\n";
    } else if (verb == "cls") {
        out += "\x1b[0m\x1b[2J\x1b[H";
    } else if (verb == "pause") {
        wait_ = Wait::Pause;
    } else if (verb == "ask") {
        askVar_ = first(rest);
        askLine_.clear();
        std::string prompt = rest.size() > askVar_.size() ? trim(rest.substr(askVar_.size())) : std::string();
        std::string shown = theme::pipes(expand(prompt));
        out += shown;
        if (shown.empty() || shown.back() != ' ') out += ' ';
        wait_ = Wait::Ask;
    } else if (verb == "goto" || verb == "gosub") {
        int at = findLabel(first(rest));
        if (at < 0) {
            out += "\r\n   script: no label " + first(rest) + "\r\n";
            on_ = false;
            return;
        }
        if (verb == "gosub") {
            if (stack_.size() >= 8) {
                out += "\r\n   script: too many gosubs\r\n";
                on_ = false;
                return;
            }
            stack_.push_back((int)pc_);
        }
        pc_ = (size_t)at;
    } else if (verb == "return" || verb == "exit") {
        if (verb == "return" && !stack_.empty()) {
            pc_ = (size_t)stack_.back();
            stack_.pop_back();
        } else on_ = false;
    } else if (verb == "if") {
        bool take = cond(rest);
        ifs_.push_back({take, take, false});
    } else if (verb == "else") {
        doElse();
    } else if (verb == "end") {
        if (!ifs_.empty()) ifs_.pop_back();
    } else if (verb == "menu") {
        items_.clear();
        while (pc_ < lines_.size()) {
            std::string l = lines_[pc_++];
            std::string v, r;
            splitVerb(l, v, r);
            if (v == "end") break;
            std::string key = first(l);
            std::string label = first(r);
            std::string text = r.size() > label.size() ? trim(r.substr(label.size())) : std::string();
            if (key.empty() || label.empty()) continue;
            items_.push_back({key, label, text});
            out += "   " + key + "  " + theme::pipes(expand(text.empty() ? label : text)) + "\r\n";
        }
        wait_ = Wait::Menu;
    } else if (verb == "sensor") {
        std::string kind = first(rest);
        std::string var = rest.size() > kind.size() ? first(trim(rest.substr(kind.size()))) : std::string();
        std::string v = station::read(kind);
        if (v.empty()) {
            out += "\r\n   no such sensor\r\n";
            return;
        }
        if (!var.empty()) setVar(var, v);
        else out += station::line(kind) + "\r\n";
    } else if (verb == "relay") {
        std::string ns = first(rest);
        int         n = takeInt(ns, 0);
        std::string how = rest.size() > ns.size() ? lower(first(trim(rest.substr(ns.size())))) : std::string();
        if (how == "on" || how == "off") {
            if (!station::relay(n, how == "on" ? 1 : 0)) out += "\r\n   no such relay\r\n";
        } else if (how.empty()) {
            int s = station::relayState(n);
            if (s < 0) out += "\r\n   no such relay\r\n";
            else out += "relay " + std::to_string(n) + (s ? " on" : " off") + "\r\n";
        } else out += "\r\n   relay: on or off\r\n";
    } else if (verb == "beep") {
        std::string hz = first(rest);
        std::string ms = hz.empty() || rest.size() <= hz.size() ? std::string() : first(trim(rest.substr(hz.size())));
        station::beep(takeInt(hz, 880), takeInt(ms, 120));
    }
}

void ScriptProg::takeKey(int key, std::string& out) {
    char c = (char)key;
    if (wait_ == Wait::Pause) {
        wait_ = Wait::None;
        return;
    }
    if (wait_ == Wait::Ask) {
        if (c == '\r') {
            setVar(askVar_, askLine_);
            askLine_.clear();
            out += "\r\n";
            wait_ = Wait::None;
        } else if (c == '\b' || c == 0x7f) {
            if (!askLine_.empty()) {
                askLine_.pop_back();
                out += "\b \b";
            }
        } else if ((unsigned char)c >= 0x20 && (unsigned char)c < 0x7f && askLine_.size() < 40) {
            askLine_ += c;
            out += c;
        }
        return;
    }
    if (wait_ == Wait::Menu) {
        for (const Item& it : items_) {
            if (!it.key.empty() && fold(it.key[0]) == fold(c)) {
                int at = findLabel(it.label);
                if (at < 0) {
                    out += "\r\n   script: no label " + it.label + "\r\n";
                    on_ = false;
                } else pc_ = (size_t)at;
                wait_ = Wait::None;
                return;
            }
        }
        return;
    }
    if (wait_ == Wait::Pick) {
        if (c == 'q' || c == 'Q') {
            on_ = false;
            return;
        }
        int idx = -1;
        if (c >= '1' && c <= '9') idx = c - '1';
        else if (c >= 'a' && c <= 'z') idx = 9 + c - 'a';
        else if (c >= 'A' && c <= 'Z') idx = 9 + c - 'A';
        if (idx >= 0 && idx < (int)picks_.size()) {
            picked_ = picks_[(size_t)idx];
            on_ = false;
        }
    }
}

bool ScriptProg::step(int key, std::string& out) {
    if (!on_) return false;
    if (key >= 0) pending_ = key;
    for (int n = 0; on_ && n < 400; n++) {
        if (wait_ == Wait::Pick && !shown_) {
            paintPick(out);
            shown_ = true;
        }
        if (wait_ != Wait::None) {
            if (pending_ < 0) return true;
            int k = pending_;
            pending_ = -1;
            takeKey(k, out);
            if (wait_ != Wait::None || !on_) return on_;
            steps_ = 0;
            continue;
        }
        if (pc_ >= lines_.size()) {
            on_ = false;
            break;
        }
        if (++steps_ >= 400) {
            out += "\r\n   The script ran too long and was stopped.\r\n";
            on_ = false;
            break;
        }
        exec(lines_[pc_++], out);
    }
    return on_;
}

}  // namespace axis::bbs
