// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#pragma GCC optimize("Os")
#include "bbs/sysopshell.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <new>

#include "bbs/config.h"
#include "bbs/features.h"  // AXIS_BENCH (boardOnlyCommand)
#include "bbs/lightbar.h"
#include "bbs/listscreen.h"
#include "bbs/listtemplate.h"
#include "bbs/theme.h"

namespace axis::bbs {

namespace {
std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> v;
    size_t                   from = 0;
    for (;;) {
        size_t at = s.find(sep, from);
        v.push_back(s.substr(from, at == std::string::npos ? std::string::npos : at - from));
        if (at == std::string::npos) return v;
        from = at + 1;
    }
}
std::string after(const std::vector<std::string>& lines, const std::string& key) {  // "real Rev Money" -> "Rev Money"
    for (auto& l : lines)
        if (l.rfind(key + " ", 0) == 0) return l.substr(key.size() + 1);
    return "";
}
std::string fit(std::string s, size_t n) {
    if (s.size() > n) s.resize(n);
    return s;
}
constexpr size_t kTextLines = 400;  // a text page keeps at most this many lines (the rest is said to be cut)
char keyFor(size_t i) { return i < 9 ? (char)('1' + i) : (char)('a' + (i - 9)); }

// settings groups: the axis.ini keys each one shows (the rest land in "other")
struct Group {
    const char* name;
    const char* keys[12];
};
const Group kGroups[] = {
    {"BBS and sysop", {"bbs_name", "location", "sysop_handle", "sysop_real_name", "board_name", "ftn_address", "timezone", nullptr}},
    {"Network", {"telnet_port", "public_host", "sessions",
#if AXIS_MODEMLINK
                 "modem_link", "lite_below_bps",
#endif
                 nullptr}},
    {"Callers and limits", {"new_user_security", "nuv_yes", "nuv_no", "nuv_level", "daily_minutes", "idle_minutes", "ratio", "free_kb", nullptr}},
    {"Look", {"theme", "login_splash", "wfc_anim", nullptr}},
};
bool inGroups(const std::string& k) {
    for (auto& g : kGroups)
        for (int i = 0; g.keys[i]; i++)
            if (k == g.keys[i]) return true;
#if AXIS_NETS
    if (k.rfind("net_", 0) == 0) return true;  // on the Sysop Net page (net_first_peer never shown in clear)
#endif
#if !AXIS_MODEMLINK
    if (k == "modem_link" || k == "lite_below_bps") return true;  // kept as saved, shown nowhere: the modem link is not built in
#endif
    return k.rfind("area", 0) == 0 || k.rfind("msgarea", 0) == 0 || k.rfind("door", 0) == 0 || k.rfind("menu.", 0) == 0 ||
           k == "messages_on" || k == "files_on" || k == "doors_on" || k == "setup_done" || k[0] == '#';
}
bool pcUpdateRow = false;
}  // namespace

void setPcUpdateRow(bool on) { pcUpdateRow = on; }

bool boardOnlyCommand(const std::string& cmd) {
    static const char* const kMine[] = {"@CLUSTERKEY", "@HANDBACK", "@CARDMIRROR", "@RESTART", "@RERUNSETUP", "@RESETSETUP", "@SDFORMAT", "@SDCHECK", "@HEADLESS", "@RELAY",
                                        "@BEEP",       "@STATION", "@LED",       "@WIFIPICK",   "@STATS",   "@DOORSTATE",
#if AXIS_BENCH  // bench builds only (bbs/features.h); elsewhere they are unknown commands, sent on like any other
                                        "@SHOT",       "@TAP",     "@LKEY",      "@LSCREEN",    "@HEAPWIN", "@SPRITEFAIL",
#endif
#if AXIS_NETS
                                        "@NETKEY",     "@NETROTATE",  // Sysop Net: Rev's keys, at the board's serial console only
#endif
    };
    std::string word = cmd.substr(0, cmd.find(' '));
    for (char& c : word) c = (char)std::toupper((unsigned char)c);
    for (const char* m : kMine)
        if (word == m) return true;
    return false;
}

std::string followerCommand(const std::string& cmd, const std::function<std::string(const std::string&)>& run) {
    if (boardOnlyCommand(cmd)) return "axis: refused: that command runs only on the board where it is typed\n";
    return run ? run(cmd) : std::string();
}

SysopShell::SysopShell(std::string& out, Run run, int cols, bool local) : out_(out), run_(std::move(run)), cols_(cols), local_(local) {}

size_t SysopShell::page_() const {
    if (cols_ < 80) return 7;  // the board's own screen stays on today's page
    ListTemplate tpl;
    if (listScreenTemplate("SYSOP.LST", tpl)) return (size_t)tpl.rows;
    return 14;
}

// the theme's header art (80 columns) online; one bright title line on the board's 40-column screen
std::string SysopShell::header(const std::string& title) const {
    if (cols_ >= 80) return theme::drip(title);
    return std::string(" ") + theme::HOT + title + theme::DGREY + " " + std::string((size_t)std::max(0, 36 - (int)title.size()), '\xc4') + theme::TEXT + "\r\n";
}

std::vector<std::string> SysopShell::lines(const std::string& cmd) {
    std::vector<std::string> v;
    for (auto& l : split(run_ ? run_(cmd) : std::string(), '\n')) {
        std::string t = l;
        while (!t.empty() && (t.back() == '\r' || t.back() == ' ')) t.pop_back();
        if (!t.empty()) v.push_back(t);
    }
    cut_ = !v.empty() && v.back().rfind("axis: reply cut", 0) == 0;  // (sysopRun stopped at 6 KB: not the whole list)
    if (cut_) did(v.back());
    return v;
}

std::vector<std::string> SysopShell::fetch(const std::string& cmd, size_t from, size_t& next, bool& ok) {
    auto v = lines(cmd + std::to_string(from));
    next = 0;
    if (!v.empty() && v.back().size() > 5 && v.back().rfind("more ", 0) == 0 && v.back().find_first_not_of("0123456789", 5) == std::string::npos) {
        size_t n = std::strtoul(v.back().c_str() + 5, nullptr, 10);
        if (n > from) next = n;  // (one that doesn't move on ends the list)
        v.pop_back();
    }
    old_ = !from && v.empty();
    if (old_) v = lines(cmd.substr(0, cmd.size() - 1));  // an older main answers nothing for a start: its whole list
    // not answered: only the follower's "the main didn't answer" (an empty page past the first is past the end)
    ok = v.empty() || v[0].rfind("axis: the main didn't answer", 0) != 0;
    if (!ok) did("the board didn't answer, try again");
    return v;
}

const std::vector<std::string>& SysopShell::page(Layer& l, const std::string& cmd) {
    size_t at = l.step > 0 ? l.next : l.step < 0 && !l.back.empty() ? l.back.back() : l.start, next;
    bool   ok, moved = false;
    auto   v = fetch(cmd, at, next, ok);
    if (ok && at && std::none_of(v.begin(), v.end(), [](const std::string& s) { return s.rfind("axis: ", 0) != 0; })) {
        // past the end, the list got shorter: More stays on this page, a rebuild goes back one, Earlier to the top
        at = l.step > 0 ? l.start : l.step == 0 && !l.back.empty() ? l.back.back() : 0;
        if (l.step < 0) l.back.clear();
        else if (!l.step && !l.back.empty()) l.back.pop_back();
        l.step = 0;
        moved = true;
        v = fetch(cmd, at, next, ok);
    }
    if (ok) {  // (not answered: the rows before stay, and so do More and Earlier, to try again)
        if (l.step > 0) l.back.push_back(l.start);
        else if (l.step < 0 && !l.back.empty()) l.back.pop_back();
        moved = moved || l.step != 0;
        l.start = at, l.next = next, l.rows = std::move(v);
    }
    l.step = 0;
    if (!l.back.empty()) l.items.push_back({"Earlier...", "", [this] { layers_.back().step = -1; }});
    if (!ok && l.rows.empty()) l.items.push_back({"Try again", "", [] {}});  // (the pick's rebuild asks again)
    if (moved) l.at = l.items.size();  // the lightbar on the page's first row
    return l.rows;
}

void SysopShell::moreItem(Layer& l) {
    if (l.next) l.items.push_back({"More...", "", [this] { layers_.back().step = 1; }});
}

bool SysopShell::walk(const std::string& cmd, const std::function<bool(const std::string&)>& each, size_t from, size_t until) {
    for (int p = 0; p < 64 && from < until; p++) {  // (64 pages: far past any list, and never a loop)
        size_t next;
        bool   ok;
        auto   v = fetch(cmd, from, next, ok);
        if (!ok) return !(noAnswer_ = true);
        for (auto& s : v)
            if (each(s)) return true;
        if (!next) break;
        from = next;
    }
    return true;
}

void SysopShell::did(const std::string& reply) {
    note_ = reply;
    while (!note_.empty() && (note_.back() == '\n' || note_.back() == '\r')) note_.pop_back();
    if (note_.rfind("axis: ", 0) == 0) note_ = note_.substr(6);
    size_t nl = note_.find('\n');
    if (nl != std::string::npos) note_.resize(nl);
}

// A row that opens a page: title and build go to push when it is picked.
SysopShell::Item SysopShell::page(const std::string& label, const std::string& value, const std::string& title, std::function<void(Layer&)> build) {
    return {label, value, [this, title, build] { push(title, build); }};
}

void SysopShell::push(const std::string& title, std::function<void(Layer&)> build) {
    Layer l;
    l.title = title;
    l.build = std::move(build);
    layers_.push_back(std::move(l));
    refresh();
}

void SysopShell::pushText(const std::string& title, const std::string& text) {
    Layer l;
    l.title = title;
    for (size_t at = 0, nl; at < text.size(); at = nl + 1) {
        nl = std::min(text.find('\n', at), text.size());
        if (l.text.size() >= kTextLines) {
            l.text.push_back("(the rest is not shown)");
            break;
        }
        if (nl > at) l.text.push_back(text.compare(at, 6, "axis: ") == 0 ? text.substr(at + 6, nl - at - 6) : text.substr(at, nl - at));
    }
    layers_.push_back(std::move(l));
    draw();
}

void SysopShell::pop() {
    layers_.pop_back();
    note_.clear();
    if (!layers_.empty()) refresh();
}

void SysopShell::refresh() {
    Layer& l = layers_.back();
    haveSettings_ = noAnswer_ = false;  // a change may have just gone out: ask again, once, for this build
    if (l.build) {
        l.items.clear();
        try {
            l.build(l);
        } catch (const std::bad_alloc&) {  // a board low on memory: a note, not a restart
            l.items.clear();
            l.rows.clear();
            did("not enough memory for this list now");
        }
        if (noAnswer_) l.items.assign(1, Item{"(no answer from the board: Q, then try again)", "", nullptr});  // never a default shown as a value
        if (l.at >= l.items.size()) l.at = l.items.empty() ? 0 : l.items.size() - 1;
    }
    draw();
}

void SysopShell::enter(SysopSection s) {
    switch (s) {
        case SysopSection::Users: push("USERS", [this](Layer& x) { users(x); }); break;
        case SysopSection::FileAreas:
#if AXIS_FILES
            push("FILE AREAS", [this](Layer& x) { areas(x, false); });
#else
            pushText("FILE AREAS", "The file areas are not in this firmware.");
#endif
            break;
        case SysopSection::MsgBases: push("MSG BASES", [this](Layer& x) { areas(x, true); }); break;
        case SysopSection::Settings: push("SETTINGS", [this](Layer& x) { settings(x); }); break;
        case SysopSection::Menus: push("MENUS", [this](Layer& x) { menus(x); }); break;
        case SysopSection::System: push("SYSTEM", [this](Layer& x) { system(x); }); break;
        case SysopSection::Look: push("LOOK", [this](Layer& x) { look(x); }); break;
        case SysopSection::Top: begin(); break;
    }
}

void SysopShell::showPage(const std::string& title, const std::string& text) {
    layers_.clear();
    note_.clear();
    asking_ = false;
    esc_ = 0;
    pushText(title, text);
}

void SysopShell::beginAt(SysopSection s) {
    layers_.clear();
    note_.clear();
    asking_ = false;
    esc_ = 0;
    enter(s);
}

void SysopShell::begin() {
    push("SYSOP", [this](Layer& l) {
        l.items = {{"Users", "", [this] { enter(SysopSection::Users); }},
                   {"File areas", "", [this] { enter(SysopSection::FileAreas); }},  // (without them: "not in this firmware")
                   {"Message bases", "", [this] { enter(SysopSection::MsgBases); }},
                   {"Settings", "", [this] { enter(SysopSection::Settings); }},
                   {"Menus", "", [this] { enter(SysopSection::Menus); }},
                   {"System", "", [this] { enter(SysopSection::System); }},
                   page("Sections", "", "SECTIONS", [this](Layer& x) { sections(x); }),
                   {"Look", "", [this] { enter(SysopSection::Look); }}};
        if (!local_)  // over telnet the WFC is only ever picked here (at the board it is the home screen)
            l.items.push_back({"Waiting for caller screen", "", [this] {
                                   wantWfc_ = true;
                                   layers_.clear();
                               }});
        l.items.push_back({"Leave the sysop menu", "", [this] { layers_.clear(); }});
    });
}

const std::vector<std::string>& SysopShell::settingLines() {
    if (!haveSettings_ && !noAnswer_) {  // every page: a setting is looked up by key
        settings_.clear();
        haveSettings_ = walk("@SETTINGS ", [this](const std::string& s) { settings_.push_back(s); return false; });
        if (!haveSettings_) settings_.clear();
    }
    return settings_;
}

std::string SysopShell::setting(const std::string& key) {
    for (auto& line : settingLines()) {
        size_t eq = line.find('=');
        if (eq != std::string::npos && line.compare(0, eq, key) == 0 && eq == key.size()) return line.substr(eq + 1);
    }
    return "";
}

// ---- drawing: a header (the theme's art for the layer, or its built-in title), then the list
void SysopShell::draw() {
    if (layers_.empty()) return;
    Layer& l = layers_.back();
    ListTemplate        tpl;
    const ListTemplate* t = cols_ >= 80 && listScreenTemplate("SYSOP.LST", tpl) ? &tpl : nullptr;
    out_ += theme::CLS;
    bool artHead = false;
    if (t) {
        // The page's own art (MENUS.ANS on the menu page) when the look has it, else the template's head (SYSOP.ANS).
        std::string art = theme::artFor(l.title, 3);
        if (art.empty()) art = theme::artFor(t->head.empty() ? std::string("SYSOP") : t->head, 3);
        if (!art.empty()) {
            out_ += art;
            out_ += "\x1b[4;1H\x1b[J";  // the header's top rows stay; the list starts under them
            artHead = true;
        }
    }
    if (!artHead) out_ += header(l.title);
    std::string crumbs;
    for (size_t i = 0; i < layers_.size(); i++) crumbs += (i ? " > " : "") + layers_[i].title;
    if (t && !t->caption.empty()) {
        out_ += renderLine(t->caption, t->width, AcsUser{}, [&](const std::string& k) { return k == "CR" ? crumbs : std::string(); }, theme::artStyle()) +
                theme::RESET + "\r\n";
        if (t->rule)
            out_ += std::string(theme::ORANGE) + std::string((size_t)std::max(0, t->width), '\xcd') + theme::RESET + "\r\n";
    } else
        out_ += " " + std::string(theme::DGREY) + fit(crumbs, (size_t)cols_ - 4) + theme::TEXT + "\r\n";
    if (!l.text.empty() || !l.build) {  // a read-only page
        size_t from = l.at;
        for (size_t i = from; i < l.text.size() && i < from + page_() + (cols_ >= 80 ? 2 : 1); i++) out_ += "   " + std::string(theme::TEXT) + fit(l.text[i], 74) + "\r\n";
        out_ += "\r\n   " + theme::key("N", "ext  ") + theme::key("P", "rev  ") + theme::key("Q", " back") + " ";
        return;
    }
    size_t page = l.at / page_(), from = page * page_();
    const ArtStyle style = t ? theme::artStyle() : ArtStyle{};
    for (size_t i = from; i < l.items.size() && i < from + page_(); i++) {
        const Item& it = l.items[i];
        bool        on = i == l.at;
        if (t) {  // SYSOP.LST's row: KY the key, LB the label, VL the value. The bar colour is colours=.
            char        key = keyFor(i - from);
            std::string label = it.label, value = it.value;
            out_ += std::string(cols_ >= 80 ? "  " : "") + dosColour(on ? t->hi : t->reg) +
                    renderRow(*t, AcsUser{}, [key, label, value](const std::string& k) {
                        return k == "KY" ? std::string(1, key) : k == "LB" ? label : k == "VL" ? value : std::string();
                    }, style) +
                    "\x1b[0m\r\n";
            continue;
        }
        size_t      lw = cols_ >= 80 ? 30 : 21, vw = cols_ >= 80 ? 40 : (size_t)cols_ - 26;
        std::string label = fit(it.label, lw), value = fit(it.value, vw);
        out_ += cols_ >= 80 ? "  " : "";
        out_ += on ? std::string("\x1b[0;30;47m") : std::string(theme::DGREY);
        out_ += std::string(" ") + keyFor(i - from) + " ";
        out_ += on ? std::string("\x1b[0;30;47m") : std::string(theme::TEXT);
        out_ += label + std::string(lw + 1 - label.size(), ' ');
        out_ += on ? std::string("\x1b[0;30;47m") : std::string(theme::WHITE);
        out_ += value + std::string(vw - value.size(), ' ') + "\x1b[0m\r\n";
    }
    size_t pages = (l.items.size() + page_() - 1) / page_();
    out_ += "\r\n   ";
    if (!note_.empty()) out_ += std::string(theme::HOT) + fit(note_, 70) + theme::TEXT + "\r\n   ";
    if (pages > 1 && cols_ < 80) out_ += std::string(theme::DGREY) + "page " + std::to_string(page + 1) + "/" + std::to_string(pages) + " (tap: more)  ";  // the board: no N key
    else if (pages > 1) out_ += std::string(theme::DGREY) + "page " + std::to_string(page + 1) + "/" + std::to_string(pages) + "  " + theme::key("N", "/") + theme::key("P", "  ");
    if (t && !t->keys.empty())
        out_ += renderLine(t->keys, t->width, AcsUser{}, [](const std::string&) { return std::string(); }, style) + theme::RESET;
    else out_ += theme::key("Enter", " pick  ") + theme::key("Q", " back") + " ";
}

// ---- questions
void SysopShell::ask(const std::string& question, bool secret, std::function<void(const std::string&)> done) {
    asking_ = true, secret_ = secret, yesNo_ = false;
    question_ = question, line_.clear();
    answer_ = std::move(done);
    out_ += "\r\n   " + std::string(theme::HOT) + question + theme::ORANGE + ": " + theme::WHITE;
}

void SysopShell::confirm(const std::string& question, std::function<void()> yes) {
    asking_ = true, secret_ = false, yesNo_ = true;
    question_ = question, line_.clear();
    answer_ = [yes](const std::string& a) {
        if (a == "y") yes();
    };
    out_ += "\r\n   " + std::string(theme::HOT) + question + " " + theme::key("Y", "/") + theme::key("N", "") + theme::ORANGE + ": " + theme::WHITE;
}

bool SysopShell::key(char c) {
    if (layers_.empty()) return false;
    if (asking_) {
        if (yesNo_) {
            asking_ = false;
            answer_((c | 0x20) == 'y' ? "y" : "n");
            if (!layers_.empty()) refresh();
            return open();
        }
        if (c == '\r' || c == '\n') {
            asking_ = false;
            std::string a = line_;
            size_t      depth = layers_.size();
            if (!a.empty()) answer_(a);
            if (!layers_.empty() && layers_.size() == depth) refresh();  // (an answer that opened a layer has drawn it)
        } else if (c == 0x1b) {
            asking_ = false;
            refresh();
        } else if (c == '\b' || c == 0x7f) {
            if (!line_.empty()) line_.pop_back(), out_ += "\b \b";
        } else if ((unsigned char)c >= 0x20 && (unsigned char)c < 0x7f && line_.size() < 70) {
            line_ += c;
            out_ += secret_ ? '*' : c;
        }
        return open();
    }
    Layer& l = layers_.back();
    size_t n = l.build ? l.items.size() : l.text.size();
    if (esc_ == 1) {
        esc_ = c == '[' || c == 'O' ? 2 : 0;
        if (!esc_) pop();  // a lone ESC: back a layer
        if (!esc_) return open();
        return true;
    }
    if (esc_ == 2) {
        esc_ = 0;
        if (c == 'A' && l.at > 0) l.at--;
        else if (c == 'B' && l.at + 1 < n) l.at++;
        draw();
        return true;
    }
    if (c == 0x1b) {
        esc_ = 1;
        return true;
    }
    char k = (char)(c | 0x20);
    if (!l.build) {  // a text page: N, P, Q
        if (k == 'n' && l.at + page_() < n) l.at += page_();
        else if (k == 'p') l.at = l.at > page_() ? l.at - page_() : 0;
        else if (k == 'q') pop();
        if (!layers_.empty()) draw();
        return open();
    }
    size_t page = l.at / page_();
    if (k == 'q') {
        pop();
        return open();
    }
    if (c == '\r' || c == '\n') {
        if (l.at < n && l.items[l.at].pick) {
            note_.clear();
            size_t depth = layers_.size();
            l.items[l.at].pick();
            if (!layers_.empty() && layers_.size() == depth && !asking_) refresh();
        }
        return open();
    }
    if (k == 'n' && (page + 1) * page_() < n) l.at = (page + 1) * page_();
    else if (k == 'p' && page > 0) l.at = (page - 1) * page_();
    else {
        size_t idx = k >= '1' && k <= '9' ? (size_t)(k - '1') : k >= 'a' && k <= 'm' && k != 'n' ? (size_t)(9 + k - 'a') : 99;
        if (idx < page_() && page * page_() + idx < n) {  // jump there and pick it
            l.at = page * page_() + idx;
            return key('\r');
        }
    }
    draw();
    return true;
}

void SysopShell::tapRow(int row) {
    if (layers_.empty() || asking_) return;
    // the header is the title art or the built-in title; count its lines to find the list
    std::string head = header(layers_.back().title);
    int         top = (int)std::count(head.begin(), head.end(), '\n') + 1;
    Layer&      l = layers_.back();
    if (!l.build) {
        key('n');
        return;
    }
    int idx = row - top;
    if (idx >= (int)page_()) {  // under the list (the page line): the next page, round to the first (no N key on the board)
        size_t next = (l.at / page_() + 1) * page_();
        l.at = next < l.items.size() ? next : 0;
        note_.clear();
        draw();
        return;
    }
    if (idx >= 0 && idx < (int)page_()) {
        size_t at = (l.at / page_()) * page_() + (size_t)idx;
        if (at < l.items.size()) {
            l.at = at;
            key('\r');
        }
    }
}

// ---- users
void SysopShell::users(Layer& l) {
    l.items.push_back({"Add a user", "", [this] {
                           ask("handle|location|password (6+)", false, [this](const std::string& a) { did(run_("@ADDUSER " + a)); });
                       }});
    const auto& rows = page(l, "@USERS ");
    size_t      st = l.start;  // (a user's own row is looked for from this page first)
    for (auto& line : rows) {
        if (line.rfind("user ", 0) != 0) continue;
        auto f = split(line.substr(5), '|');
        if (f.size() < 6) continue;
        std::string h = f[1];
        l.items.push_back(page(h, f[2] + ", level " + f[3] + ", " + f[4] + " calls", "USER", [this, h, st](Layer& x) { user(x, h, st); }));
    }
    moreItem(l);
}

void SysopShell::user(Layer& l, const std::string& h, size_t from) {
    auto d = lines("@USER " + h);
    std::string flags;
    auto        mine = [&](const std::string& line) {  // its own row: from the page it was listed on, then from the top
        auto f = split(line.size() > 5 ? line.substr(5) : "", '|');
        if (line.rfind("user ", 0) == 0 && f.size() >= 6 && f[1] == h) flags = f[5];
        return !flags.empty();
    };
    if (!walk("@USERS ", mine, from) || (flags.empty() && from && !walk("@USERS ", mine, 0, from))) return;
    if (flags.empty()) {  // (deleted or renamed elsewhere)
        did("that user is gone");
        l.items.push_back({"(no such user now: Q)", "", nullptr});
        return;
    }
    auto field = [&](const char* label, const char* key, const char* field) {
        std::string f = field;
        l.items.push_back({label, after(d, key), [this, h, f, label] {
                               ask(std::string("New ") + label, false, [this, h, f](const std::string& v) { did(run_("@UEDIT " + h + "|" + f + "|" + v)); });
                           }});
    };
    field("Real name", "real", "realname");
    field("Location", "from", "location");
    field("Email", "email", "email");
    field("Tagline", "tagline", "tagline");
    field("Security level", "security", "security");
    l.items.push_back({"Validate", "", [this, h] { did(run_("@VALIDATE " + h)); }});
    l.items.push_back({"Deny", "", [this, h] { did(run_("@DENY " + h)); }});
    l.items.push_back({"Set password", "", [this, h] {
                           ask("New password for " + h + " (4+)", true, [this, h](const std::string& v) { did(run_("@USERPASS " + h + " " + v)); });
                       }});
    const char* perms[5][3] = {{"Files", "files", "F"}, {"Messages", "msgs", "M"}, {"Doors", "doors", "D"}, {"Free downloads", "freedl", "$"}, {"Twit", "", "T"}};
    for (int i = 0; i < 5; i++) {
        bool        on = flags.find(perms[i][2]) != std::string::npos;
        std::string what = perms[i][1], label = perms[i][0];
        l.items.push_back({label, on ? "on" : "off", [this, h, what, on] {
                               did(run_(what.empty() ? "@TWIT " + h + (on ? " off" : " on") : "@PERM " + h + " " + what + (on ? " off" : " on")));
                           }});
    }
    l.items.push_back({"Make sysop", "", [this, h] { did(run_("@SYSOP " + h)); }});
    l.items.push_back({"Delete this user", "", [this, h] {
                           confirm("Delete " + h + " for good?", [this, h] {
                               did(run_("@UDEL " + h));
                               pop();
                           });
                       }});
}

// ---- file areas and message bases
void SysopShell::areas(Layer& l, bool msgs) {
    auto list = lines(msgs ? "@MSGAREAS" : "@AREAS");
    int  count = (int)list.size();
    if (!cut_)  // (a cut list's count is short: adding would overwrite an area)
        l.items.push_back({msgs ? "Add a message base" : "Add a file area", "", [this, msgs, count] {
                               ask("id|name|level (id: short, no spaces)", false, [this, msgs, count](const std::string& a) {
                                   did(run_(std::string("@AREASET ") + (msgs ? "msgs" : "files") + "|" + std::to_string(count + 1) + "|" + a));
                               });
                           }});
    if (msgs)
        l.items.push_back({"Purge old posts", "", [this] {
                               ask("area|days (posts older than that go)", false, [this](const std::string& a) { did(run_("@PURGE " + a)); });
                           }});
    for (auto& line : list) {
        auto f = split(line, '|');
        if (f.size() < 4) continue;
        int n = std::atoi(f[0].c_str());
        l.items.push_back(page(f[2], f[1] + (f[3] != "0" ? ", level " + f[3] : "") + (f.size() > 4 && f[4] == "m" ? ", mirrored" : ""),
                           f[2], [this, msgs, n](Layer& x) { area(x, msgs, n); }));
    }
}

void SysopShell::area(Layer& l, bool msgs, int n) {
    auto list = lines(msgs ? "@MSGAREAS" : "@AREAS");
    if (n < 1 || n > (int)list.size()) return;
    auto        f = split(list[(size_t)n - 1], '|');
    std::string kind = msgs ? "msgs" : "files", id = f[1], name = f[2], level = f.size() > 3 ? f[3] : "0";
    std::string n_ = std::to_string(n);
    l.items.push_back({msgs ? "Posts" : "Files", "", [this, msgs, id] {
                           push(msgs ? "POSTS" : "FILES", [this, msgs, id](Layer& x) { msgs ? posts(x, id) : files(x, id); });
                       }});
    l.items.push_back({"Name", name, [this, kind, n_, id, level] {
                           ask("New name", false, [this, kind, n_, id, level](const std::string& v) {
                               did(run_("@AREASET " + kind + "|" + n_ + "|" + id + "|" + v + "|" + level));
                           });
                       }});
    l.items.push_back({"Security level", level, [this, kind, n_, id, name] {
                           ask("Lowest level that sees it (0 = everyone)", false, [this, kind, n_, id, name](const std::string& v) {
                               did(run_("@AREASET " + kind + "|" + n_ + "|" + id + "|" + name + "|" + v));
                           });
                       }});
    l.items.push_back({"Move up the list", "", [this, kind, n_] { did(run_("@AREAUP " + kind + "|" + n_)); }});
    l.items.push_back({"Remove this area", "(its files stay on the card)", [this, kind, n_, name] {
                           confirm("Remove " + name + " from the list?", [this, kind, n_] {
                               did(run_("@AREADEL " + kind + "|" + n_));
                               pop();
                           });
                       }});
}

void SysopShell::files(Layer& l, const std::string& a) {
    for (auto& line : page(l, "@FILES " + a + "|")) {
        auto f = split(line, '|');
        if (f.size() < 2) continue;
        std::string name = f[0], desc = f.size() > 2 ? f[2] : "";
        l.items.push_back({name, desc.empty() ? f[1] + " bytes" : desc, [this, a, name] {
                               push(name, [this, a, name](Layer& x) {
                                   x.items.push_back({"Description", "", [this, a, name] {
                                                          ask("Description", false, [this, a, name](const std::string& v) { did(run_("@FDESC " + a + "|" + name + "|" + v)); });
                                                      }});
                                   x.items.push_back({"Rename", "", [this, a, name] {
                                                          ask("New name", false, [this, a, name](const std::string& v) {
                                                              did(run_("@FREN " + a + "|" + name + "|" + v));
                                                              pop();
                                                          });
                                                      }});
                                   x.items.push_back({"Move to another area", "", [this, a, name] {
                                                          ask("Area id to move it to", false, [this, a, name](const std::string& v) {
                                                              did(run_("@FMV " + a + "|" + name + "|" + v));
                                                              pop();
                                                          });
                                                      }});
                                   x.items.push_back({"Delete", "", [this, a, name] {
                                                          confirm("Delete " + name + " from the card?", [this, a, name] {
                                                              did(run_("@FDEL " + a + "|" + name));
                                                              pop();
                                                          });
                                                      }});
                               });
                           }});
    }
    moreItem(l);
    if (l.items.empty()) l.items.push_back({"(no files)", "", nullptr});
}

void SysopShell::posts(Layer& l, const std::string& a) {
    auto list = page(l, "@POSTS " + a + "|");  // (paged: the newest first)
    if (old_) std::reverse(list.begin(), list.end());  // (an older main's whole list: oldest first)
    for (auto& line : list) {
        auto f = split(line, '|');
        if (f.size() < 3) continue;
        std::string num = f[0], subj = f[2];
        l.items.push_back({"#" + num + " " + subj, f[1] + (f.size() > 3 ? "  " + f[3] : ""), [this, a, num, subj] {
                               push("POST", [this, a, num, subj](Layer& x) {
                                   x.items.push_back({"Delete", subj, [this, a, num] {
                                                          confirm("Delete post #" + num + "?", [this, a, num] {
                                                              did(run_("@POSTDEL " + a + "|" + num));
                                                              pop();
                                                          });
                                                      }});
                                   x.items.push_back({"Move to another base", "", [this, a, num] {
                                                          ask("Base id to move it to", false, [this, a, num](const std::string& v) {
                                                              did(run_("@POSTMV " + a + "|" + num + "|" + v));
                                                              pop();
                                                          });
                                                      }});
                               });
                           }});
    }
    moreItem(l);
    if (l.items.empty()) l.items.push_back({"(no posts)", "", nullptr});
}

// ---- settings: every axis.ini setting, in groups; passwords set but never shown
void SysopShell::settings(Layer& l) {
    for (int g = 0; g < (int)(sizeof kGroups / sizeof kGroups[0]); g++)
        l.items.push_back(page(kGroups[g].name, "", kGroups[g].name, [this, g](Layer& x) { settingGroup(x, g); }));
    l.items.push_back(page("Other settings", "", "OTHER", [this](Layer& x) { settingGroup(x, -1); }));
    l.items.push_back({"Sysop password", "(never shown)", [this] {
                           ask("New sysop password (4+)", true, [this](const std::string& v) { did(run_("@SYSOPPASS " + v)); });
                       }});
    l.items.push_back({"New user password (NUP)", "(never shown)", [this] {
                           ask("New user password (Enter alone: none)", true, [this](const std::string& v) { did(run_("@NUP " + v)); });
                       }});
    l.items.push_back({"Clear the NUP", "anyone can apply", [this] { did(run_("@NUP")); }});
#if AXIS_CLUSTER
    if (local_)  // this board's own key: set at the board, never over telnet
        l.items.push_back({"Cluster key", "(never shown; the same on every board)", [this] {
                               ask("Cluster key, 8+ (off: this board runs alone)", true, [this](const std::string& v) { did(run_("@CLUSTERKEY " + v)); });
                           }});
#endif
#if AXIS_DOSPROMPT
    l.items.push_back({"DOS prompt password", "(never shown)", [this] {
                           ask("New DOS prompt password", true, [this](const std::string& v) { did(run_("@DOSPASS " + v)); });
                       }});
#endif
    // the board-wide switches (a caller's own access is on the caller's page)
    std::string files = setting("files_on"), msgs = setting("messages_on"), doors = setting("doors_on"), tel = setting("sysop_telnet"), freekb = setting("free_kb");
    auto flip = [this](const std::string& key, bool on) { return [this, key, on] { did(run_("@SET " + key + "=" + (on ? "0" : "1"))); }; };
#if AXIS_FILES  // (a build without the file areas keeps files_on as saved and never shows it)
    l.items.push_back({"Files", files == "0" ? "off" : "on", flip("files_on", files != "0")});
#endif
    l.items.push_back({"Messages", msgs == "0" ? "off" : "on", flip("messages_on", msgs != "0")});
    l.items.push_back({"Doors", doors == "0" ? "off" : "on", flip("doors_on", doors != "0")});
    l.items.push_back({"Free downloads (KB)", freekb, [this] {
                           ask("Free download allowance in KB (0: earn it by uploading)", false, [this](const std::string& v) { did(run_("@SET free_kb=" + v)); });
                       }});
    l.items.push_back({"Telnet sysop access", tel == "0" ? "off" : "on", flip("sysop_telnet", tel != "0")});
}

void SysopShell::settingGroup(Layer& l, int g) {
    for (auto& line : settingLines()) {
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        bool        mine = false;
        if (g < 0) mine = !inGroups(k);
        else
            for (int i = 0; kGroups[g].keys[i]; i++) mine = mine || k == kGroups[g].keys[i];
        if (!mine) continue;
        if (k == "wfc") continue;  // the waiting screen skin is picked in the Look section, never here
        if (k == "theme") {
            std::string cur = theme::knownLook(v) ? theme::knownLook(v) : "axis";
            l.items.push_back({"look", cur, [this, cur] {
                                   push("LOOK", [this, cur](Layer& x) {
                                       for (int i = 0; i < theme::lookCount(); i++) {
                                           std::string id = theme::lookId(i);
                                           std::string label = std::string(theme::lookLabel(i)) + ", " + theme::lookBlurb(id.c_str());
                                           x.items.push_back({label, id == cur ? "now" : "", [this, id] {
                                                                  did(run_("@SET theme=" + id));
                                                                  pop();
                                                              }});
                                       }
                                   });
                               }});
            continue;
        }
        l.items.push_back({k, v, [this, k] {
                               ask("New " + k, false, [this, k](const std::string& nv) { did(run_("@SET " + k + "=" + nv)); });
                           }});
    }
}

// ---- menus: the main menu's items (hotkey, name, command)
void SysopShell::menus(Layer& l) {
    auto items = lines("@MENU");
    if (items.empty()) {
        l.items.push_back({"Start editing the main menu", "a copy of the built-in one", [this] { did(run_("@MENUSTART")); }});
        l.items.push_back({"(a theme's MAIN.ANS keeps its own letters)", "", nullptr});
        return;
    }
    int count = (int)items.size();
    if (!cut_)  // (a cut list's count is short: adding would overwrite an item)
        l.items.push_back({"Add an item", "", [this, count] {
                               ask("key|name|command (see the list in an item)", false, [this, count](const std::string& a) {
                                   did(run_("@MENUSET " + std::to_string(count + 1) + "|" + a));
                               });
                           }});
    for (auto& line : items) {
        auto f = split(line, '|');
        if (f.size() < 4) continue;
        int n = std::atoi(f[0].c_str());
        l.items.push_back(page("[" + f[1] + "] " + f[2], f[3], "MENU ITEM", [this, n](Layer& x) { menuItem(x, n); }));
    }
    l.items.push_back({"Back to the built-in menu", "", [this] {
                           confirm("Throw the edited menu away?", [this] { did(run_("@MENURESET")); });
                       }});
}

void SysopShell::menuItem(Layer& l, int n) {
    auto items = lines("@MENU");
    if (n < 1 || n > (int)items.size()) return;
    auto        f = split(items[(size_t)n - 1], '|');
    std::string key = f[1], name = f[2], cmd = f[3], n_ = std::to_string(n);
    l.items.push_back({"Hotkey", key, [this, n_, name, cmd] {
                           ask("New hotkey (one key)", false, [this, n_, name, cmd](const std::string& v) {
                               did(run_("@MENUSET " + n_ + "|" + v.substr(0, 1) + "|" + name + "|" + cmd));
                           });
                       }});
    l.items.push_back({"Name", name, [this, n_, key, cmd] {
                           ask("New name", false, [this, n_, key, cmd](const std::string& v) { did(run_("@MENUSET " + n_ + "|" + key + "|" + v + "|" + cmd)); });
                       }});
    l.items.push_back({"Command", cmd, [this, n_, key, name] {
                           push("COMMAND", [this, n_, key, name](Layer& x) {
                               for (auto& c : lines("@MENUCMDS")) {
                                   std::string id = split(c, '|')[0];
                                   x.items.push_back({id, "", [this, n_, key, name, id] {
                                                          did(run_("@MENUSET " + n_ + "|" + key + "|" + name + "|" + id));
                                                          pop();
                                                      }});
                               }
                           });
                       }});
    l.items.push_back({"Move up", "", [this, n_] { did(run_("@MENUUP " + n_)); }});
    l.items.push_back({"Remove", "", [this, n_, name] {
                           confirm("Remove " + name + " from the menu?", [this, n_] {
                               did(run_("@MENUDEL " + n_));
                               pop();
                           });
                       }});
}

// ---- sections: board-wide messages, files and doors, plus each door row
void SysopShell::sections(Layer& l) {
    std::string messages = "1", files = "1", doors = "1";
    struct DoorRow {
        std::string key, line, name;
        bool        on = true;
    };
    std::vector<DoorRow> doorRows;
    for (auto& line : settingLines()) {
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        if (k == "messages_on") messages = v;
        else if (k == "files_on") files = v;
        else if (k == "doors_on") doors = v;
        else if (k.size() > 4 && k.rfind("door", 0) == 0 && k[4] >= '0' && k[4] <= '9') {
            auto f = split(v, '|');
            if (f.size() < 3 || f[0].empty() || doorLeftOut(f[2])) continue;  // (a door this build leaves out: not listed)
            bool on = true;
            if (f.size() > 5 && !f[5].empty()) on = f[5] != "0";
            else if (lordGameExe(f[2])) on = false;
            std::string args = f.size() > 3 ? f[3] : "";
            std::string drop = f.size() > 4 && !f[4].empty() ? f[4] : "doorsys";
            doorRows.push_back({k, f[0] + "|" + f[1] + "|" + f[2] + "|" + args + "|" + drop, f[1].empty() ? f[2] : f[1], on});
        }
    }
    auto flip = [this](const std::string& key, bool on) {
        return [this, key, on] { did(run_("@SET " + key + "=" + (on ? "0" : "1"))); };
    };
    l.items.push_back({"Messages", messages == "0" ? "off" : "on", flip("messages_on", messages != "0")});
#if AXIS_FILES
    l.items.push_back({"Files", files == "0" ? "off" : "on", flip("files_on", files != "0")});
#endif
    l.items.push_back({"Doors", doors == "0" ? "off" : "on", flip("doors_on", doors != "0")});
    for (const DoorRow& d : doorRows)
        l.items.push_back({d.name, d.on ? "on" : "off", [this, d] {
                               did(run_("@SET " + d.key + "=" + d.line + "|" + (d.on ? "0" : "1")));
                           }});
}

// ---- system
void SysopShell::system(Layer& l) {
    auto show = [this](const char* title, const char* cmd) { return [this, title, cmd] { pushText(title, run_(cmd)); }; };
    l.items.push_back({"Event log", "", show("EVENT LOG", "@LOG")});
    l.items.push_back(page("Nodes", "", "NODES", [this](Layer& x) { nodes(x); }));
    l.items.push_back({"Status", "", show("STATUS", "@STATUS")});
#if AXIS_PC
    bool updateRow = true;
#else
    bool updateRow = pcUpdateRow;  // axispc, whose core is not built with AXIS_PC
#endif
    if (updateRow) {  // the firmware leaves this out, so its System list does not move
        std::string st;
        for (auto& line : lines("@PCUPDATE state")) st += line;
        bool on = st.find("off") == std::string::npos;
        l.items.push_back({"Update check", on ? "on" : "off", [this, on] {
                               did(run_(std::string("@PCUPDATE ") + (on ? "off" : "on")));
                           }});
        l.items.push_back({"Update", "", show("UPDATE", "@PCUPDATE")});
    }
    l.items.push_back({"SD cards", "", show("CARDS", "@CARDS")});
    l.items.push_back({"Web, No-IP, Discord", "", show("WEB", "@WEB")});
    l.items.push_back({"Update No-IP now", "", show("NO-IP", "@NOIPNOW")});
    l.items.push_back({"SD card check", "", show("SD CHECK", "@SDCHECK")});
    l.items.push_back({"Doors on the card", "", show("DOORS", "@DOORLS")});
    Item doorMem{"Door memory", "the last door's memory", show("DOOR MEMORY (this board)", "@DOORSTATE")};  // (last: no key moves)
#if AXIS_MODEMLINK
    Item modemPage = page("Modem", "LFD3K-SM radio callers", "MODEM", [this](Layer& x) { modem(x); });  // (last, online and here)
#endif
#if AXIS_NETS
    Item netPage = page("Sysop Net", "board to board net", "SYSOP NET", [this](Layer& x) { sysopNet(x); });  // after Modem: no key moves
    Item userPage = page("User Net", "the member boards' net", "USER NET", [this](Layer& x) { userNet(x); });
#endif
    if (!local_) {  // the rest is for the sysop at the board: never over telnet
#if AXIS_MODEMLINK
        l.items.push_back(modemPage);
#endif
#if AXIS_NETS
        l.items.push_back(netPage);
        l.items.push_back(userPage);
#endif
        l.items.push_back(doorMem);
        return;
    }
    l.items.push_back({"Restart this board", "", [this] { confirm("Restart the board now?", [this] { did(run_("@RESTART")); }); }});
    // setup again on the board's screen; the settings stay as they are until the wizard saves (@RESETSETUP is the erase, serial only)
    l.items.push_back({"Rerun setup", "kept till saved", [this] { confirm("Run setup again? Settings stay until it saves", [this] { did(run_("@RERUNSETUP")); }); }});
    l.items.push_back({"Format SD card", "erases everything", [this] {
                           ask("Type FORMAT to erase the whole SD card", false, [this](const std::string& v) {
                               std::string w = v;  // any case: the touch keyboard has no caps lock
                               for (char& c : w) c = (char)std::toupper((unsigned char)c);
                               if (w == "FORMAT") did(run_("@SDFORMAT FORMAT"));
                               else did("not formatted");
                           });
                       }});
    l.items.push_back(page("Station", "sensors and relays", "STATION", [this](Layer& x) { station(x); }));
#if AXIS_MODEMLINK
    l.items.push_back(modemPage);
#endif
#if AXIS_NETS
    l.items.push_back(netPage);
    l.items.push_back(userPage);
#endif
    l.items.push_back(doorMem);
}

// ---- station: sensors (read only), the relays (pick to flip), a beep
void SysopShell::station(Layer& l) {
    for (auto& line : lines("@STATION")) {
        auto f = split(line, '|');
        if (f[0] == "sensor" && f.size() >= 2) l.items.push_back({f[1], "", nullptr});
        else if (f[0] == "relay" && f.size() >= 3) {
            std::string n = f[1];
            bool        on = f[2] == "1";
            l.items.push_back({"Relay " + n, on ? "on" : "off", [this, n, on] { did(run_("@RELAY " + n + (on ? " off" : " on"))); }});
        }
    }
    l.items.push_back({"Beep", "", [this] { did(run_("@BEEP")); }});
}

// ---- modem: the LORA FLOWTRON DATAMASTER 3000 SERIAL MODEM - LFD3K-SM. Radio callers now, the last 10 radio calls, the
// modems found on the LAN (mDNS) and its two settings. @MODEM lines: "now|node|handle|label|bps|tiers",
// "call|handle|tiers|seconds|bytes|retrains", "found|name|ip", "search|on" while a search runs.
bool SysopShell::reloadIf(const std::string& title) {
    if (layers_.empty() || layers_.back().title != title || !layers_.back().build || asking_) return false;
    refresh();
    return true;
}

#if AXIS_MODEMLINK
void SysopShell::modem(Layer& l) {
    std::string link = setting("modem_link"), lite = setting("lite_below_bps");
    l.items.push_back({"Take link reports from the modem", link == "0" ? "off" : "on", [this, link] {
                           did(run_(std::string("@SET modem_link=") + (link == "0" ? "1" : "0")));
                       }});
    l.items.push_back({"Lite screens at or below (bps)", lite == "0" ? "never" : lite, [this] {
                           ask("Lite screens at or below this many bps (7031 = SF9 and slower, 0 = never)", false,
                               [this](const std::string& v) { did(run_("@SET lite_below_bps=" + v)); });
                       }});
    std::vector<Item> now, calls, found;
    bool              searching = false;
    for (auto& line : lines("@MODEM")) {
        auto f = split(line, '|');
        if (f[0] == "now" && f.size() >= 6)
            now.push_back({"node " + f[1] + " " + (f[2].empty() ? "(logging in)" : f[2]), f[3] + (f[4].empty() ? "" : " " + f[4] + " bps"), nullptr});
        else if (f[0] == "call" && f.size() >= 6) {
            unsigned secs = (unsigned)std::atoi(f[3].c_str());
            std::string v = (f[2].empty() ? std::string("?") : f[2]) + " " + std::to_string(secs / 60) + "m" + std::to_string(secs % 60) + "s " +
                            std::to_string(std::atoi(f[4].c_str()) / 1024) + "KB";
            if (!f[5].empty() && f[5] != "-1") v += " " + f[5] + " retrains";
            calls.push_back({f[1].empty() ? "(no handle)" : f[1], v, nullptr});
        } else if (f[0] == "found" && f.size() >= 3)
            found.push_back({f[1], f[2], nullptr});
        else if (f[0] == "search")
            searching = true;
    }
    auto section = [&](const char* title, std::vector<Item>& v, const char* none) {
        l.items.push_back({title, "", nullptr});
        if (v.empty()) l.items.push_back({std::string("  ") + none, "", nullptr});
        for (auto& i : v) l.items.push_back({"  " + i.label, i.value, nullptr});
    };
    section("Radio callers now", now, "none");
    section("Last radio calls", calls, "none yet");
    section("Modems on the LAN", found, searching ? "looking..." : "none found");
    l.items.push_back({"Look for modems again", "", [this] { did(run_("@MODEM scan")); }});
}
#endif  // AXIS_MODEMLINK

// ---- look: the caller theme and the waiting screen's skin (blank: it follows the theme)
void SysopShell::look(Layer& l) {
    const char* t = theme::knownLook(setting("theme"));
    l.items.push_back(page("Theme", t ? t : "axis", "THEME", [this](Layer& x) { lookPick(x, "theme", false); }));
    std::string ws = setting("wfc");
    const char* w = theme::isClassicWfc(ws) ? "classic" : theme::knownLook(ws);
    l.items.push_back(page("Waiting screen skin", w ? w : "follows the theme", "WFC SKIN", [this](Layer& x) { lookPick(x, "wfc", true); }));
    bool anim = setting("wfc_anim") != "0";  // the board's own panel only (against burn-in): telnet and serial WFCs stay still
    l.items.push_back({"Waiting screen animation", anim ? "on" : "off", [this, anim] { did(run_(std::string("@SET wfc_anim=") + (anim ? "0" : "1"))); }});
}

void SysopShell::lookPick(Layer& l, const std::string& key, bool follow) {
    std::string now = setting(key);
    const char* c = theme::knownLook(now);
    std::string cur = follow && theme::isClassicWfc(now) ? "classic" : c ? c : "";
    // a look: set it, or (when the host can draw it) show it first and let the host set it
    auto pick = [this, key](const std::string& id) {
        if (previews_) {
            previewKey_ = key;
            previewLook_ = id;
            return;
        }
        did(run_("@SET " + key + "=" + id));
        pop();
    };
    if (follow) {
        l.items.push_back({"Follow the theme", cur.empty() ? "now" : "", [this, key] {
                               did(run_("@SET " + key + "="));
                               pop();
                           }});
        l.items.push_back({"Classic AXIS (built in)", cur == "classic" ? "now" : "", [pick] { pick("classic"); }});
    }
    for (int i = 0; i < theme::lookCount(); i++) {
        std::string id = theme::lookId(i);
        std::string label = std::string(theme::lookLabel(i)) + ", " + theme::lookBlurb(id.c_str());
        l.items.push_back({label, id == cur ? "now" : "", [pick, id] { pick(id); }});
    }
}

#if AXIS_NETS
// ---- Sysop Net (System > Sysop Net, after Modem), in the Modem page's style: one list with section rows. Everything goes
// through the @NET commands and @SET, the same as the serial console; few lambda kinds, to keep the image small.
SysopShell::Item SysopShell::netAsk(const std::string& label, const std::string& value, const std::string& cmd) {
    return {label, value, [this, cmd] { ask(cmd, cmd.find("first_peer") != std::string::npos, [this, cmd](const std::string& v) { did(run_(cmd + v)); }); }};
}
SysopShell::Item SysopShell::runRow(const std::string& label, const std::string& value, std::string cmd) {
    return {label, value, [this, cmd] { did(run_(cmd)); }};
}
SysopShell::Item SysopShell::netShow(const std::string& label, const std::string& cmd) {
    return {label, "", [this, label, cmd] { pushText(label, run_(cmd)); }};
}

void SysopShell::sysopNet(Layer& l) {
    // kind: H a section row, V a value from @NET, S a setting (asked, then @SET), A a command asked for its
    // argument, R the same on Rev's board only, T a text page of a command's answer
    static const char* const kRows[][3] = {
        {"T", "Status", "@NET"},
        {"H", "This node", ""},
        {"V", "  Board id", "board"},
        {"V", "  MAC", "mac"},
        {"V", "  Origin", "origin"},
        {"S", "  Name", "net_name"},
        {"S", "  Contact", "net_sysop_name"},
        {"S", "  Location", "net_location"},
        {"T", "Peers", "@NET PEERS"},
        {"H", "Sync and routing", ""},
        {"S", "  Sync every (min)", "net_sync_min"},
        {"S", "  Keep posts", "net_keep_posts"},
        {"S", "  Keep days", "net_keep_days"},
        {"S", "  First peer", "net_first_peer"},
        {"T", "  Sync now", "@NET SYNC"},
        {"B", "Traffic and logs", ""},
        {"H", "Moderation", ""},
        {"T", "  Hidden and blocked here", "@NET LOCAL"},
        {"A", "  Hide a post here", "@NETHIDE "},
        {"A", "  Unhide a post", "@NETUNHIDE "},
        {"A", "  Block a board here", "@NETBLOCK "},
        {"A", "  Unblock a board", "@NETUNBLOCK "},
        {"R", "  Ban a board (net)", "@NETBAN "},
        {"R", "  Hold a post (net)", "@NETHOLD "},
        {"R", "  Kill a post (net): id YES", "@NETKILL "},
        {"R", "  Lift (net)", "@NETLIFT "},
        {"R", "  Limits (net)", "@NETLIMITS "},
    };
    auto d = lines("@NET");
    l.items.push_back(netAsk("Sysop Net (1 on, 0 off)", setting("net_sysop") == "0" ? "off" : "on", "@SET net_sysop="));
    if (after(d, "board").empty()) {  // not running here
        l.items.push_back({d.empty() ? std::string("(no answer)") : d[0], "", nullptr});
        return;
    }
    bool rev = after(d, "rev board") == "yes";
    for (const auto& r : kRows) {
        char        k = r[0][0];
        std::string arg = r[2];
        if (k == 'H') l.items.push_back({r[1], "", nullptr});
        else if (k == 'V') l.items.push_back({r[1], after(d, arg), nullptr});
        else if (k == 'T') l.items.push_back(netShow(r[1], arg));
        else if (k == 'B') l.items.push_back(page(r[1], "", "TRAFFIC AND LOGS", [this](Layer& x) { records(x); }));
        else if (k == 'A' || (k == 'R' && rev)) l.items.push_back(netAsk(r[1], "", arg));
        else if (k == 'S') {
            std::string v = setting(arg);
            if (arg == "net_first_peer") v = v.empty() ? "none" : "set (hidden)";
            l.items.push_back(netAsk(r[1], v, "@SET " + arg + "="));
        }
    }
}

// ---- Traffic and logs: the net record browser. Lists come from @NET RECORD KIND / FIND (each line "<index>
// <file>:<offset>:<time> <date> <who> <what>", newest first, then "older from <index>" if lines are left). Picking a line
// opens @NET RECORD SHOW <file>:<offset>:<time>: that very line, however the record has grown since. Older and Newer
// page the same layer (recPages_: the index each page starts at).
namespace {
std::string wrap74(const std::string& s) {  // text pages cut lines at 74 columns: a record's fields are folded instead
    std::string o;
    size_t      col = 0;
    for (char c : s) {
        if (c == '\n') col = 0;
        else if (col++ == 74) o += '\n', col = 1;
        o += c;
    }
    return o;
}
}  // namespace

void SysopShell::recordPage(const std::string& title, const std::string& cmd) {
    recPages_.assign(1, 0);
    push(title, [this, cmd](Layer& x) { recordList(x, cmd); });
}

void SysopShell::records(Layer& l) {
    static const char* const kLists[][2] = {{"All", "all"}, {"Votes", "votes"}, {"Members", "members"}, {"Actions", "actions"}};
    for (const auto& r : kLists) {
        const char *title = r[0], *kind = r[1];
        l.items.push_back({title, "", [this, title, kind] { recordPage(title, std::string("@NET RECORD KIND ") + kind + " 20 "); }});
    }
    l.items.push_back({"Search", "", [this] {
                           ask("Search the record for", false, [this](const std::string& v) { recordPage("SEARCH", "@NET RECORD FIND " + v + " 20 "); });
                       }});
    l.items.push_back(netShow("Months", "@NET RECORD MONTHS"));
}

void SysopShell::recordList(Layer& l, const std::string& cmd) {
    long older = -1;
    for (auto& line : lines(cmd + std::to_string(recPages_.empty() ? 0 : recPages_.back()))) {
        auto f = split(line, ' ');  // index, file:offset:time, YYYY-MM-DD, HH:MM, who, what
        if (line.rfind("axis: older from ", 0) == 0) older = std::strtol(line.c_str() + 17, nullptr, 10);
        else if (f.size() < 5 || !std::isdigit((unsigned char)line[0]) || f[2].size() < 6)
            l.items.push_back({line.rfind("axis: ", 0) == 0 ? line.substr(6) : line, "", nullptr});
        else {
            std::string at = f[1];
            size_t      what = f[0].size() + f[1].size() + f[2].size() + f[3].size() + f[4].size() + 5;  // "MM-DD HH:MM who", then what
            l.items.push_back({f[2].substr(5) + " " + f[3] + " " + f[4], what < line.size() ? line.substr(what) : "", [this, at] {
                                   pushText("RECORD", wrap74(run_("@NET RECORD SHOW " + at)));
                               }});
        }
    }
    if (older >= 0) l.items.push_back({"Older", "", [this, older] { recPages_.push_back((size_t)older), layers_.back().at = 0; }});
    if (recPages_.size() > 1) l.items.push_back({"Newer", "", [this] { recPages_.pop_back(), layers_.back().at = 0; }});
}

// ---- User Net (System > User Net, after Sysop Net): this board's standing, its settings, applying, New Board Voting,
// the members, and on Rev's board admit, deny, veto and the vote counts. All through @NET, @SET and the User Net commands.
void SysopShell::userNet(Layer& l) {
    auto        d = lines("@NET");
    std::string st = after(d, "user net"), areas = setting("net_user_areas");
    std::string said = d.empty() ? std::string("(no answer)") : d[0].rfind("axis: ", 0) == 0 ? d[0].substr(6) : d[0];
    l.items.push_back({"Status", st.empty() ? said : st, nullptr});
    l.items.push_back(netAsk("User Net (1 on, 0 off)", setting("net_user") == "1" ? "on" : "off", "@SET net_user="));
    l.items.push_back(page("Areas carried", areas.empty() ? "none" : areas, "AREAS CARRIED", [this](Layer& x) {
        uint8_t m = userNetMask(setting("net_user_areas"));
        for (unsigned i = 0; i < 4; i++)
            x.items.push_back(runRow(userNetList((uint8_t)(1u << i)), m >> i & 1 ? "on" : "off", "@SET net_user_areas=" + userNetList((uint8_t)(m ^ (1u << i)))));
    }));
    l.items.push_back(netAsk("Caller level", setting("net_user_level"), "@SET net_user_level="));
    {  // Net Files: pick the file areas to share, see how many are listed, set the size a radio caller is asked about
        std::string sh = setting("net_files_areas");
        l.items.push_back(page("Net Files", sh.empty() ? "none shared" : sh, "NET FILES", [this](Layer& x) {
            std::string cur = setting("net_files_areas");
            cur.erase(std::remove(cur.begin(), cur.end(), ' '), cur.end());  // a hand-edited "a, b"
            cur = "," + cur + ",";  // ",a,b,"
            for (auto& line : lines("@AREAS")) {
                auto f = split(line, '|');
                if (f.size() < 3) continue;
                std::string key = "," + f[1] + ",", next = cur;
                size_t      at = next.find(key);
                if (at == std::string::npos) next += f[1] + ",";
                else next.erase(at, key.size() - 1);
                next.erase(0, next.find_first_not_of(','));
                if (!next.empty()) next.pop_back();
                x.items.push_back(runRow(f[2], at == std::string::npos ? "not shared" : "shared", "@SET net_files_areas=" + next));
            }
            x.items.push_back(netAsk("Radio size limit (KB)", setting("net_files_radio_kb"), "@SET net_files_radio_kb="));
        }));
    }
    if (st.empty() || st == "loading") return;  // not running here, or the applications are still loading
    // With net_user off the line reads "off", the held standing in brackets: a member still votes, nobody applies.
    if (st == "member" || st == "off (member)")
        l.items.push_back(page("New Board Voting", std::to_string(lines("@NET WAITING").size()) + " waiting", "NEW BOARD VOTING",
                               [this](Layer& x) { userVoting(x); }));
    else if (st.rfind("off", 0) != 0)
        l.items.push_back(netAsk("Apply (a short blurb)", "", "@NETAPPLY "));
    l.items.push_back(netShow("Members", "@NET MEMBERS"));
    if (after(d, "rev board") != "yes") return;
    l.items.push_back(netShow("Applications", "@NET APPS"));
    l.items.push_back(netAsk("Admit a board", "", "@NETADMIT "));
    l.items.push_back(netAsk("Deny a board", "", "@NETDENY "));
    l.items.push_back(netAsk("Veto a board: id YES", "", "@NETVETO "));
    l.items.push_back(netAsk("Vote counts: yes=3,no=3", "", "@NETVOTES "));
}

// One row per applicant this board has not voted on: "<board> <yes> <no> <name>, <location>" from @NET WAITING.
void SysopShell::userVoting(Layer& l) {
    voteComment_.clear();  // a comment is for one applicant: typed again for the next
    for (auto& line : lines("@NET WAITING")) {
        auto f = split(line, ' ');
        if (f.size() < 4 || f[0].size() != 16) {
            l.items.push_back({line.rfind("axis: ", 0) == 0 ? line.substr(6) : line, "", nullptr});
            continue;
        }
        std::string board = f[0];
        l.items.push_back(page(line.substr(f[0].size() + f[1].size() + f[2].size() + 3), "yes " + f[1] + " no " + f[2], "APPLICATION",
                               [this, board](Layer& x) { userApp(x, board); }));
    }
    if (l.items.empty()) l.items.push_back({"Nobody is waiting for this board's vote", "", nullptr});
}

// The application (@NET APP <board>: its fields, the counts, each member's vote and comment), then the vote.
void SysopShell::userApp(Layer& l, const std::string& board) {
    std::string cmd = "@NET APP " + board;
    for (auto& line : lines(cmd)) {
        size_t sp = line.find(' ');
        l.items.push_back({line.substr(0, sp), sp == std::string::npos ? "" : line.substr(sp + 1), nullptr});
    }
    l.items.push_back({"Read it all", "", [this, cmd] { pushText("APPLICATION", wrap74(run_(cmd))); }});
    l.items.push_back({"Comment", voteComment_, [this] { ask("Comment for your vote", false, [this](const std::string& v) { voteComment_ = v; }); }});
    for (const char* yn : {"Y", "N"})
        l.items.push_back({*yn == 'Y' ? "Vote yes" : "Vote no", "", [this, board, yn] { did(run_("@NETVOTE " + board + " " + yn + " " + voteComment_)); }});
}
#endif

bool SysopShell::escTimeout() {
    if (esc_ != 1 || layers_.empty()) return open();
    esc_ = 0;
    pop();
    return open();
}

bool SysopShell::takePreview(std::string& key, std::string& look) {
    if (previewLook_.empty()) return false;
    key = previewKey_;
    look = previewLook_;
    previewKey_.clear();
    previewLook_.clear();
    return true;
}

void SysopShell::previewDone(const std::string& note) {
    if (layers_.empty()) return;
    if (!note.empty()) {  // the host set a look: off the picker, the change said under the list
        layers_.pop_back();
        if (layers_.empty()) return;
        did(note);
    }
    refresh();
}

void SysopShell::nodes(Layer& l) {
    for (auto& line : lines("@NODES")) {
        if (line.rfind("axis: node ", 0) != 0) continue;
        int         n = std::atoi(line.c_str() + 11);
        size_t      h = line.find("handle '"), e = h == std::string::npos ? h : line.find('\'', h + 8);
        std::string who = h == std::string::npos ? "" : line.substr(h + 8, e - h - 8);
        std::string ns = std::to_string(n);
        l.items.push_back({"node " + ns, who.empty() ? "waiting" : who, [this, ns, who] {
                               if (who.empty()) return;
                               push("NODE " + ns, [this, ns](Layer& x) {
                                   x.items.push_back({"Send a note", "", [this, ns] {
                                                          ask("Note", false, [this, ns](const std::string& v) { did(run_("@NOTE " + ns + " " + v)); });
                                                      }});
                                   x.items.push_back({"Give 15 more minutes", "", [this, ns] { did(run_("@ADDTIME " + ns + " 15")); }});
                                   x.items.push_back({"Send to the teleconference", "", [this, ns] { did(run_("@FORCETC " + ns)); }});
                                   x.items.push_back({"Kick", "", [this, ns] {
                                                          confirm("Kick node " + ns + "?", [this, ns] {
                                                              did(run_("@KICK " + ns));
                                                              pop();
                                                          });
                                                      }});
                               });
                           }});
    }
}

}  // namespace axis::bbs
