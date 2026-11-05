// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#pragma GCC optimize("Os")
#include "bbs/sysopshell.h"

#include <algorithm>
#include <cstdlib>

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
char keyFor(size_t i) { return i < 9 ? (char)('1' + i) : (char)('a' + (i - 9)); }

// settings groups: the axis.ini keys each one shows (the rest land in "other")
struct Group {
    const char* name;
    const char* keys[12];
};
const Group kGroups[] = {
    {"BBS and sysop", {"bbs_name", "location", "sysop_handle", "sysop_real_name", "board_name", "ftn_address", "timezone", nullptr}},
    {"Network", {"telnet_port", "public_host", "sessions", nullptr}},
    {"Callers and limits", {"new_user_security", "daily_minutes", "idle_minutes", "ratio", "free_kb", nullptr}},
    {"Look", {"theme", "login_splash", nullptr}},
};
bool inGroups(const std::string& k) {
    for (auto& g : kGroups)
        for (int i = 0; g.keys[i]; i++)
            if (k == g.keys[i]) return true;
    return k.rfind("area", 0) == 0 || k.rfind("msgarea", 0) == 0 || k.rfind("door", 0) == 0 || k.rfind("menu.", 0) == 0 ||
           k == "setup_done" || k[0] == '#';
}
}  // namespace

SysopShell::SysopShell(std::string& out, Run run, int cols) : out_(out), run_(std::move(run)), cols_(cols) {}

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
    return v;
}

void SysopShell::did(const std::string& reply) {
    note_ = reply;
    while (!note_.empty() && (note_.back() == '\n' || note_.back() == '\r')) note_.pop_back();
    if (note_.rfind("axis: ", 0) == 0) note_ = note_.substr(6);
    size_t nl = note_.find('\n');
    if (nl != std::string::npos) note_.resize(nl);
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
    for (auto& s : split(text, '\n'))
        if (!s.empty()) l.text.push_back(s.rfind("axis: ", 0) == 0 ? s.substr(6) : s);
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
    if (l.build) {
        l.items.clear();
        l.build(l);
        if (l.at >= l.items.size()) l.at = l.items.empty() ? 0 : l.items.size() - 1;
    }
    draw();
}

void SysopShell::begin() {
    push("SYSOP", [this](Layer& l) {
        l.items = {{"Users", "", [this] { push("USERS", [this](Layer& x) { users(x); }); }},
                   {"File areas", "", [this] { push("FILE AREAS", [this](Layer& x) { areas(x, false); }); }},
                   {"Message bases", "", [this] { push("MSG BASES", [this](Layer& x) { areas(x, true); }); }},
                   {"Settings", "", [this] { push("SETTINGS", [this](Layer& x) { settings(x); }); }},
                   {"Menus", "", [this] { push("MENUS", [this](Layer& x) { menus(x); }); }},
                   {"System", "", [this] { push("SYSTEM", [this](Layer& x) { system(x); }); }},
                   {"Leave the sysop menu", "", [this] { layers_.clear(); }}};
    });
}

// ---- drawing: a header (the theme's art for the layer, or its built-in title), then the list
void SysopShell::draw() {
    if (layers_.empty()) return;
    Layer& l = layers_.back();
    out_ += theme::CLS;
    out_ += header(l.title);
    std::string crumbs;
    for (size_t i = 0; i < layers_.size(); i++) crumbs += (i ? " > " : "") + layers_[i].title;
    out_ += " " + std::string(theme::DGREY) + fit(crumbs, (size_t)cols_ - 4) + theme::TEXT + "\r\n";
    if (!l.text.empty() || !l.build) {  // a read-only page
        size_t from = l.at;
        for (size_t i = from; i < l.text.size() && i < from + page_() + (cols_ >= 80 ? 2 : 1); i++) out_ += "   " + std::string(theme::TEXT) + fit(l.text[i], 74) + "\r\n";
        out_ += "\r\n   " + theme::key("N", "ext  ") + theme::key("P", "rev  ") + theme::key("Q", " back") + " ";
        return;
    }
    size_t page = l.at / page_(), from = page * page_();
    for (size_t i = from; i < l.items.size() && i < from + page_(); i++) {
        const Item& it = l.items[i];
        bool        on = i == l.at;
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
    if (pages > 1) out_ += std::string(theme::DGREY) + "page " + std::to_string(page + 1) + "/" + std::to_string(pages) + "  " + theme::key("N", "/") + theme::key("P", "  ");
    out_ += theme::key("Enter", " pick  ") + theme::key("Q", " back") + " ";
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
            if (!a.empty()) answer_(a);
            if (!layers_.empty()) refresh();
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
    for (auto& line : lines("@USERS")) {
        if (line.rfind("user ", 0) != 0) continue;
        auto f = split(line.substr(5), '|');
        if (f.size() < 6) continue;
        std::string h = f[1];
        l.items.push_back({h, f[2] + ", level " + f[3] + ", " + f[4] + " calls", [this, h] { push("USER", [this, h](Layer& x) { user(x, h); }); }});
    }
}

void SysopShell::user(Layer& l, const std::string& h) {
    auto d = lines("@USER " + h);
    std::string flags;
    for (auto& line : lines("@USERS"))
        if (line.rfind("user ", 0) == 0) {
            auto f = split(line.substr(5), '|');
            if (f.size() >= 6 && f[1] == h) flags = f[5];
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
        l.items.push_back({f[2], f[1] + (f[3] != "0" ? ", level " + f[3] : "") + (f.size() > 4 && f[4] == "m" ? ", mirrored" : ""),
                           [this, msgs, n, f] { push(f[2], [this, msgs, n](Layer& x) { area(x, msgs, n); }); }});
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
    for (auto& line : lines("@FILES " + a)) {
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
    if (l.items.empty()) l.items.push_back({"(no files)", "", nullptr});
}

void SysopShell::posts(Layer& l, const std::string& a) {
    auto list = lines("@POSTS " + a);
    for (auto it = list.rbegin(); it != list.rend(); ++it) {  // the newest first
        auto f = split(*it, '|');
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
    if (l.items.empty()) l.items.push_back({"(no posts)", "", nullptr});
}

// ---- settings: every axis.ini setting, in groups; passwords set but never shown
void SysopShell::settings(Layer& l) {
    for (int g = 0; g < (int)(sizeof kGroups / sizeof kGroups[0]); g++)
        l.items.push_back({kGroups[g].name, "", [this, g] { push(kGroups[g].name, [this, g](Layer& x) { settingGroup(x, g); }); }});
    l.items.push_back({"Other settings", "", [this] { push("OTHER", [this](Layer& x) { settingGroup(x, -1); }); }});
    l.items.push_back({"Sysop password", "(never shown)", [this] {
                           ask("New sysop password (4+)", true, [this](const std::string& v) { did(run_("@SYSOPPASS " + v)); });
                       }});
    l.items.push_back({"New user password (NUP)", "(never shown)", [this] {
                           ask("New user password (Enter alone: none)", true, [this](const std::string& v) { did(run_("@NUP " + v)); });
                       }});
    l.items.push_back({"Clear the NUP", "anyone can apply", [this] { did(run_("@NUP")); }});
    l.items.push_back({"DOS prompt password", "(never shown)", [this] {
                           ask("New DOS prompt password", true, [this](const std::string& v) { did(run_("@DOSPASS " + v)); });
                       }});
}

void SysopShell::settingGroup(Layer& l, int g) {
    for (auto& line : lines("@SETTINGS")) {
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        bool        mine = false;
        if (g < 0) mine = !inGroups(k);
        else
            for (int i = 0; kGroups[g].keys[i]; i++) mine = mine || k == kGroups[g].keys[i];
        if (!mine) continue;
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
    l.items.push_back({"Add an item", "", [this, count] {
                           ask("key|name|command (see the list in an item)", false, [this, count](const std::string& a) {
                               did(run_("@MENUSET " + std::to_string(count + 1) + "|" + a));
                           });
                       }});
    for (auto& line : items) {
        auto f = split(line, '|');
        if (f.size() < 4) continue;
        int n = std::atoi(f[0].c_str());
        l.items.push_back({"[" + f[1] + "] " + f[2], f[3], [this, n] { push("MENU ITEM", [this, n](Layer& x) { menuItem(x, n); }); }});
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

// ---- system
void SysopShell::system(Layer& l) {
    auto show = [this](const char* title, const char* cmd) { return [this, title, cmd] { pushText(title, run_(cmd)); }; };
    l.items.push_back({"Event log", "", show("EVENT LOG", "@LOG")});
    l.items.push_back({"Nodes", "", [this] { push("NODES", [this](Layer& x) { nodes(x); }); }});
    l.items.push_back({"Status", "", show("STATUS", "@STATUS")});
    l.items.push_back({"SD cards", "", show("CARDS", "@CARDS")});
    l.items.push_back({"Web, No-IP, Discord", "", show("WEB", "@WEB")});
    l.items.push_back({"Update No-IP now", "", show("NO-IP", "@NOIPNOW")});
    l.items.push_back({"SD card check", "", show("SD CHECK", "@SDCHECK")});
    l.items.push_back({"Doors on the card", "", show("DOORS", "@DOORLS")});
    l.items.push_back({"Restart this board", "", [this] { confirm("Restart the board now?", [this] { did(run_("@RESTART")); }); }});
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
