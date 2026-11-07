// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#pragma GCC optimize("Os")  // menus and screens: size over speed (the door CPU and transfers stay -O2)
#include "bbs/menurunner.h"

#include <cctype>

namespace axis::bbs {

namespace {
std::vector<std::string> keysOf(const MenuCmd& c) {  // "V VOTE" -> V, VOTE (upper case)
    std::vector<std::string> v;
    std::string              k;
    for (char ch : c.keys + " ") {
        if (ch == ' ') {
            if (!k.empty()) v.push_back(k);
            k.clear();
        } else {
            k += (char)std::toupper((unsigned char)ch);
        }
    }
    return v;
}
bool autoKey(const std::string& k) { return k == "//" || k == "~~"; }
}  // namespace

MenuRunner::MenuRunner(Loader load, ArtEmit emit, ArtValue value) : load_(std::move(load)), emit_(std::move(emit)), value_(std::move(value)) {}

bool MenuRunner::enter(const std::string& name, const AcsUser& who) {
    MenuDef d;
    if (!parseMenu(load_(name + ".MNU"), d) || !acsAllows(d.acs, who)) return false;
    def_ = std::move(d), who_ = who, name_ = name, typed_.clear(), autos_.clear();
    bar_.reset(def_.bars.empty() ? nullptr : new Lightbar(def_.bars));
    for (size_t i = 0; i < def_.cmds.size(); i++)
        for (const std::string& k : keysOf(def_.cmds[i]))
            if ((k == "~~" || (k == "//" && !ranOnce_.count(name_ + "#" + std::to_string(i)))) && acsAllows(def_.cmds[i].acs, who_)) {
                autos_.push_back(i);
                if (k == "//") ranOnce_.insert(name_ + "#" + std::to_string(i));
            }
    redraw();
    return true;
}

void MenuRunner::redraw() {
    if (def_.clr) emit("\x1b[0m\x1b[2J\x1b[H");
    std::string carry;  // text up to the last line break goes out; the rest waits, so no code is cut in two
    auto        piece = [&](const char* d, size_t n) {
        carry.append(d, n);
        size_t cut = carry.rfind('\n');
        cut = cut != std::string::npos ? cut + 1 : carry.size() > 1024 ? carry.size() - 32 : 0;
        if (cut) {
            expandArt(carry.substr(0, cut), who_, value_, emit_);
            carry.erase(0, cut);
        }
    };
    if (!artPieces_ || !artPieces_(name_ + ".ANS", piece)) {
        std::string art = load_(name_ + ".ANS");
        if (!art.empty()) expandArt(art, who_, value_, emit_);
    }
    if (!carry.empty()) expandArt(carry, who_, value_, emit_);
    if (bar_) emit(bar_->draw());
    prompt();
}

void MenuRunner::prompt() {
    if (def_.usePrompt && !def_.prompt1.empty()) emit("\r\n" + expandArt(def_.prompt1, who_, value_));
}

MenuAction MenuRunner::run(const MenuCmd& c) {
    MenuAction a;
    activity_ = c.activity;
    const std::string& s = c.cmd;
    size_t             colon = s.find(':');
    std::string        verb = colon == std::string::npos ? s : s.substr(0, colon), rest = colon == std::string::npos ? "" : s.substr(colon + 1);
    if (verb == "GOTO") a.kind = MenuAction::Goto, a.arg = rest;
    else if (verb == "DOOR") a.kind = MenuAction::Door, a.arg = rest;
    else if (verb == "LOGOFF") a.kind = MenuAction::Logoff;
    else if (verb == "QUIT") a.kind = MenuAction::Quit;
    else if (verb == "RUN") {
        size_t sp = rest.find(' ');
        a.kind = MenuAction::Run, a.arg = rest.substr(0, sp);
        if (sp != std::string::npos) a.args = rest.substr(sp + 1);
    } else if (!verb.empty()) {  // ViSiON/3's other verbs (LOGIN, NEWUSER, CHECKACCESS, DISCONNECT): the BBS decides
        size_t sp = s.find(' ');
        a.kind = MenuAction::Run, a.arg = s.substr(0, sp);
        if (sp != std::string::npos) a.args = s.substr(sp + 1);
    }
    return a;
}

// what the caller has typed so far: a one-key command runs at once, a longer one waits for Enter
MenuAction MenuRunner::typed(bool enter) {
    bool longer = false;  // some allowed command (or /G) is longer and starts with what's typed
    for (const MenuCmd& c : def_.cmds) {
        if (!acsAllows(c.acs, who_)) continue;
        for (const std::string& k : keysOf(c)) {
            if (autoKey(k)) continue;
            if (k == typed_ && (k.size() == 1 || enter)) {
                typed_.clear();
                return run(c);
            }
            if (k.size() >= typed_.size() && k.compare(0, typed_.size(), typed_) == 0) longer = true;  // (a whole long one waits for Enter)
        }
    }
    if (typed_ == "/G") return typed_.clear(), MenuAction{MenuAction::Quit, "", ""};
    if (typed_ == "G" && !longer) return typed_.clear(), MenuAction{MenuAction::Logoff, "", ""};
    if (typed_ == "/") longer = true;
    if (enter || !longer) typed_.clear();
    return MenuAction{};
}

MenuAction MenuRunner::key(char c, uint32_t nowMs) {
    if (c == 0) {
        if (autos_.empty()) return MenuAction{};
        size_t i = autos_.front();
        autos_.erase(autos_.begin());
        return run(def_.cmds[i]);
    }
    lastKeyMs_ = nowMs;
    if (esc_) {  // a terminal key sequence with no lightbar to take it (ESC [ A, ESC [ 5 ~, ESC O A): swallowed whole
        if (esc_ == 1 && (c == '[' || c == 'O')) {
            esc_ = c == '[' ? 2 : 3;
            return MenuAction{};
        }
        if (esc_ == 2 && ((c >= '0' && c <= '9') || c == ';')) return MenuAction{};
        bool lone = esc_ == 1;
        esc_ = 0;
        if (!lone) return MenuAction{};  // the sequence's final byte
    }
    if (c == '\x1b' && (!bar_ || !typed_.empty())) {
        esc_ = 1;
        return MenuAction{};
    }
    if (bar_ && typed_.empty() && (c == '\x1b' || bar_->midKey())) {
        bar_->key(c);
        emit(bar_->takeRedraw());
        return MenuAction{};
    }
    if ((c == '\r' || c == '\n') && typed_.empty()) {
        if (!bar_) return MenuAction{};
        typed_ = bar_->item((size_t)bar_->current()).key;
        for (char& ch : typed_) ch = (char)std::toupper((unsigned char)ch);
        return typed(true);
    }
    if (c == '\r' || c == '\n') return typed(true);
    if (c == 8 || c == 127) {
        if (!typed_.empty()) typed_.pop_back(), emit("\b \b");
        return MenuAction{};
    }
    if ((unsigned char)c < 32) return MenuAction{};
    typed_ += (char)std::toupper((unsigned char)c);
    MenuAction a = typed(false);
    if (a.kind == MenuAction::None && !typed_.empty()) emit(std::string(1, c));  // a longer command: show it as typed
    return a;
}

void MenuRunner::tick(uint32_t nowMs) {
    if (bar_ && bar_->midKey() && nowMs - lastKeyMs_ > 300) bar_->keyTimeout();
}

}  // namespace axis::bbs
