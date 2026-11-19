// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#pragma GCC optimize("Os")  // menus and screens: size over speed (the door CPU and transfers stay -O2)
#include "bbs/msgreader.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>

#include "bbs/listscreen.h"
#include "bbs/theme.h"

namespace axis::bbs {

using namespace theme;

std::string MessageReader::where() const {
    if (s_ == St::Areas || area_ >= areas_.size()) return "message areas";
    if (s_ == St::Body || s_ == St::To || s_ == St::Subject) return "writing in " + areas_[area_].name;
    return "reading " + areas_[area_].name;
}

bool MessageReader::lineKey(char c, size_t max) {
    if (c == '\r') return true;
    if (c == 8 || c == 127) {
        if (!line_.empty()) {
            line_.pop_back();
            out_ += "\b \b";
        }
    } else if ((unsigned char)c >= 0x20 && (unsigned char)c < 0x7f && line_.size() < max) {
        line_ += c;
        out_ += c;
    }
    return false;
}

void MessageReader::begin() { showAreas(); }

std::vector<Message> MessageReader::headersOf(const std::string& area) {
    std::vector<Message> h = st_.headers(area);
    if (hidden_)
        for (size_t i = 0; i < h.size();)
            if (hidden_(h[i].from)) h.erase(h.begin() + (long)i);
            else i++;
    return h;
}

void MessageReader::tally(MessageStore& store, const std::string& area, int last, const std::function<bool(const std::string&)>& hidden,
                          unsigned& total, unsigned& fresh) {
    total = fresh = 0;
    store.scanHeaders(area, [&](const Message& m) {
        if (hidden && hidden(m.from)) return true;
        total++;
        fresh += m.num > last;
        return true;
    });
    // headers() keeps the newest kMaxHeaders; posts are numbered as they are added, so the new ones are the newest
    if (total > MessageStore::kMaxHeaders) total = (unsigned)MessageStore::kMaxHeaders;
    if (fresh > total) fresh = total;
}

int MessageReader::countNew(const std::vector<FileArea>& areas, MessageStore& store, const std::map<std::string, int>& lastRead) {
    int n = 0;
    for (const FileArea& a : areas) {
        auto     it = lastRead.find(a.id);
        unsigned total = 0, fresh = 0;
        tally(store, a.id, it == lastRead.end() ? 0 : it->second, nullptr, total, fresh);
        n += (int)fresh;
    }
    return n;
}

void MessageReader::beginScan() {
    scanning_ = true;
    area_ = 0;
    hasTpl_ = bars_ && listScreenTemplate("MSGS.LST", tpl_);  // the scan's L list (it opens no area)
    heads_ = headersOf(areas_.empty() ? std::string() : areas_[0].id);
    if (areas_.empty()) {
        showAreas();
        return;
    }
    nextNew();
}

void MessageReader::showAreas() {
    s_ = St::Areas;
    line_.clear();
    if (!bars_) {
        out_ += std::string("\x1b[0m\x1b[2J\x1b[H") + listScreenHead("MSGS", "MESSAGE AREAS") + "   " + ORANGE +
                std::string(70, '\xcd') + "\r\n";
        usingList_ = false;
        for (size_t i = 0; i < areas_.size(); i++) {
            unsigned total = 0, fresh = 0;
            tally(st_, areas_[i].id, lastRead_.count(areas_[i].id) ? lastRead_[areas_[i].id] : 0, hidden_, total, fresh);
            char b[160];
            std::snprintf(b, sizeof b, "   %s[%2u] %s%-34s %s%4u %s%s", ORANGE, (unsigned)(i + 1), WHITE, areas_[i].name.c_str(), TEXT,
                          total, total == 1 ? "msg " : "msgs", fresh ? "" : "\r\n");
            out_ += b;
            if (fresh) out_ += std::string("  ") + ORANGE + std::to_string(fresh) + " new" + TEXT + "\r\n";
            spill();
        }
        out_ += std::string("\r\n   ") + TEXT + "Area number (Enter for the main menu): " + WHITE;
        return;
    }
    std::vector<std::string> rows;
    rows.reserve(areas_.size());
    for (size_t i = 0; i < areas_.size(); i++) {  // counted a header at a time (none kept)
        unsigned total = 0, fresh = 0;
        tally(st_, areas_[i].id, lastRead_.count(areas_[i].id) ? lastRead_[areas_[i].id] : 0, hidden_, total, fresh);
        char b[80];
        std::snprintf(b, sizeof b, "%-32.32s %4u %s%s", areas_[i].name.c_str(), total, total == 1 ? "msg " : "msgs",
                      fresh ? (std::string("  ") + std::to_string(fresh) + " new").c_str() : "");
        rows.emplace_back(b);
    }
    list_ = ScrollList{3, kListRow, 74, kListRows, 111, 15};  // (a message list may have left its template's geometry)
    list_.setItems(std::move(rows));
    usingList_ = true;
    out_ += listScreenTop("MESSAGE AREAS", "Area                              Messages");
    spill();
    out_ += list_.draw();
    spill();
    out_ += listScreenStatus(areas_.empty() ? "" : areas_[0].name);
    out_ += listScreenKeys("arrows move   Enter opens   number+Enter   " + theme::key("Q", "uit") + " or Esc");
}

void MessageReader::openArea(size_t i) {
    area_ = i;
    heads_ = headersOf(areas_[i].id);
    hasTpl_ = bars_ && listScreenTemplate("MSGS.LST", tpl_);  // once per open area: every window and redraw uses this one
    listFrom_ = heads_.size() > (size_t)perPage() ? heads_.size() - (size_t)perPage() : 0;
    showList();
}

void MessageReader::showList() {
    s_ = St::List;
    int last = lastRead_.count(areas_[area_].id) ? lastRead_[areas_[area_].id] : 0;
    if (!bars_) {
        out_ += "\x1b[0m\x1b[2J\x1b[H\r\n   " + fade(areas_[area_].name) + std::string(TEXT) + "   " + std::to_string(heads_.size()) +
                " messages\r\n   " + DGREY + std::string(70, '\xc4') + "\r\n";
        if (heads_.empty()) out_ += std::string("   ") + TEXT + "(no messages yet: [P]ost the first)\r\n";
        usingList_ = false;
        size_t from = heads_.size() > (size_t)kPerPage ? heads_.size() - kPerPage : 0;  // the newest page
        for (size_t i = from; i < heads_.size(); i++) {
            const Message& m = heads_[i];
            char           b[200];
            std::snprintf(b, sizeof b, "   %s%c%s%4d %s%-14.14s %s%-14.14s %s%-26.26s\r\n", ORANGE, m.num > last ? '*' : ' ', DGREY, m.num, WHITE,
                          m.from.c_str(), TEXT, m.to.c_str(), WHITE, m.subject.c_str());
            out_ += b;
            spill();
        }
        listPrompt();
        return;
    }
    if (heads_.empty()) listFrom_ = 0;
    else if (listFrom_ >= heads_.size()) listFrom_ = heads_.size() - 1;
    size_t end = listFrom_ + (size_t)perPage();
    if (end > heads_.size()) end = heads_.size();
    const ListTemplate*      t = tpl();
    std::vector<std::string> rows;
    rows.reserve(end - listFrom_);
    last_ = last;
    const ArtStyle style = artStyle();  // (once: a follower's ask can wait)
    for (size_t i = listFrom_; i < end; i++) {  // one window of headers, bodies stay on disk until Read
        if (t) {  // MSGS.LST's row: NW new marker, NM number, FR from, TO to, SU subject
            rows.emplace_back(renderRow(*t, AcsUser{}, [this, i](const std::string& k) { return msgField(i, k); }, style));
            continue;
        }
        const Message& m = heads_[i];
        char           b[80];
        std::snprintf(b, sizeof b, "%c%4d %-12.12s %-12.12s %-24.24s", m.num > last ? '*' : ' ', m.num, m.from.c_str(), m.to.c_str(),
                      m.subject.c_str());
        rows.emplace_back(b);
    }
    if (rows.empty()) {
        usingList_ = false;
        listPrompt();
        return;
    }
    list_ = t ? ScrollList{t->x, t->y, t->width, t->rows, t->hi, t->reg} : ScrollList{3, kListRow, 74, kListRows, 111, 15};
    list_.setItems(std::move(rows));
    usingList_ = true;
    char cap[80];
    std::snprintf(cap, sizeof cap, "  #  From          To            Subject          %u %s", (unsigned)heads_.size(), heads_.size() == 1 ? "msg" : "msgs");
    out_ += listScreenTop(areas_[area_].name, cap, t, [this](const std::string& k) {  // CT the count ("12 msgs")
        return k == "CT" ? std::to_string(heads_.size()) + (heads_.size() == 1 ? " msg" : " msgs") : std::string();
    });
    spill();
    out_ += list_.draw();
    spill();
    size_t at = listFrom_ + (size_t)list_.current();
    out_ += listScreenStatus(statusLine(at, heads_[at].subject), t);
    out_ += listScreenKeys(theme::key("R", "ead #  ") + theme::key("N", "ext new  ") + theme::key("P", "ost  ") + theme::key("Q", "uit") +
                               "   Enter reads",
                           t);
}

// MSGS.LST's values for heads_[i]: NW '*' when new to this caller, NM the number, FR from, TO to, SU the subject
std::string MessageReader::msgField(size_t i, const std::string& k) const {
    if (i >= heads_.size()) return "";
    const Message& m = heads_[i];
    return k == "NW" ? std::string(1, m.num > last_ ? '*' : ' ') : k == "NM" ? std::to_string(m.num) : k == "FR" ? m.from
           : k == "TO" ? m.to : k == "SU" ? m.subject : std::string();
}

// the status line in the list's frame: the template's status= when it has one, else the built-in line made safe to draw
std::string MessageReader::statusLine(size_t i, const std::string& builtin) const {
    const ListTemplate* t = tpl();
    if (!t) return builtin;
    if (t->status.empty()) return listPlain(builtin);
    return renderLine(t->status, t->width, AcsUser{}, [this, i](const std::string& k) { return msgField(i, k); }, artStyle());
}

bool MessageReader::tick(uint32_t now, uint32_t lastInput) {
    if (!bars_ || !usingList_ || !list_.midKey() || now - lastInput <= 300) return true;
    list_.keyTimeout();
    if (!list_.escaped()) return true;
    if (s_ == St::Areas) return false;
    if (s_ == St::List) showAreas();
    return true;
}

void MessageReader::listPrompt() {
    out_ += "\r\n   " + theme::key("R", "ead #  ") + theme::key("N", "ext new  ") + theme::key("P", "ost  ") + theme::key("Q", "uit") + WHITE + ": ";
}

void MessageReader::show(int num) {
    usingList_ = false;
    Message m;
    if (!st_.read(areas_[area_].id, num, m)) {
        out_ += std::string("\r\n   ") + ORANGE + "No message #" + std::to_string(num) + " here.\r\n";
        if (bars_) showList();
        else {
            s_ = St::List;
            listPrompt();
        }
        return;
    }
    current_ = m;
    int& last = lastRead_[areas_[area_].id];
    if (num > last) last = num;
    out_ += "\x1b[0m\x1b[2J\x1b[H\r\n";
    out_ += window("#" + std::to_string(m.num) + " " + areas_[area_].name, {{"from", m.from}, {"to", m.to}, {"subject", m.subject}, {"date", m.date}});
    out_ += std::string("\r\n") + WHITE;
    std::string l;
    for (size_t i = 0; i <= m.body.size(); i++) {
        if (i == m.body.size() || m.body[i] == '\n') {
            out_ += "   " + l + "\r\n";
            l.clear();
        } else {
            l += m.body[i];
        }
    }
    s_ = St::Reading;
    out_ += "\r\n   " + theme::key("A", " reply  ") + theme::key("N", "ext new  ") + theme::key("L", "ist  ") + theme::key("Q", "uit") + WHITE + ": ";
}

void MessageReader::nextNew() {
    int last = lastRead_.count(areas_[area_].id) ? lastRead_[areas_[area_].id] : 0;
    heads_ = headersOf(areas_[area_].id);
    for (const Message& m : heads_)
        if (m.num > last) {
            show(m.num);
            return;
        }
    if (scanning_)  // the scan goes on to the next area that has something new
        for (size_t j = area_ + 1; j < areas_.size(); j++) {
            int  lastJ = lastRead_.count(areas_[j].id) ? lastRead_[areas_[j].id] : 0;
            auto h = headersOf(areas_[j].id);
            for (const Message& m : h)
                if (m.num > lastJ) {
                    area_ = j;
                    heads_ = h;
                    out_ += std::string("\r\n   ") + DGREY + "--- " + WHITE + areas_[j].name + DGREY + " ---" + TEXT;
                    show(m.num);
                    return;
                }
        }
    if (scanning_) {
        scanning_ = false;
        out_ += std::string("\r\n   ") + TEXT + "Scan done: you're all caught up.\r\n";
    } else {
        out_ += std::string("\r\n   ") + TEXT + "No new messages in " + areas_[area_].name + ".\r\n";
    }
    if (bars_ && area_ < areas_.size()) showList();
    else {
        s_ = St::List;
        listPrompt();
    }
}

void MessageReader::startPost(bool reply) {
    usingList_ = false;
    draft_ = Message();
    draft_.from = handle_;
    if (reply) {
        draft_.to = current_.from;
        draft_.subject = current_.subject.rfind("Re: ", 0) == 0 ? current_.subject : "Re: " + current_.subject;
        std::string who;  // the old boards' quoting: the writer's initials in front of each line
        bool        start = true;
        for (char ch : current_.from) {
            if (start && std::isalnum((unsigned char)ch) && who.size() < 3) who += (char)std::toupper((unsigned char)ch);
            start = ch == ' ' || ch == '_' || ch == '.';
        }
        int    quoted = 0;
        size_t pos = 0;
        while (pos <= current_.body.size() && quoted < 8) {
            size_t      e = current_.body.find('\n', pos);
            std::string l = current_.body.substr(pos, e == std::string::npos ? std::string::npos : e - pos);
            pos = e == std::string::npos ? current_.body.size() + 1 : e + 1;
            if (l.empty() || l.compare(0, 4, "... ") == 0) continue;  // blank lines, taglines
            draft_.body += who + "> " + l.substr(0, 70) + "\n";
            quoted++;
        }
        if (!draft_.body.empty()) draft_.body += "\n";
    }
    line_.clear();
    lines_ = 0;
    s_ = St::To;
    out_ += std::string("\r\n\r\n   ") + TEXT + "To (Enter for " + ORANGE + (draft_.to.empty() ? "All" : draft_.to) + TEXT + "): " + WHITE;
}

void MessageReader::save() {
    draft_.date = now_ ? now_() : "";
    if (!draft_.body.empty() && draft_.body.back() == '\n') draft_.body.pop_back();
    if (!tagline_.empty()) draft_.body += "\n\n... " + tagline_;
    int n = st_.post(areas_[area_].id, draft_);
    out_ += std::string("\r\n   ") + (n ? std::string(TEXT) + "Saved as #" + std::to_string(n) + "." : std::string(ORANGE) + "Couldn't save it, sorry.") + "\r\n";
    if (n) lastRead_[areas_[area_].id] = n > lastRead_[areas_[area_].id] ? n : lastRead_[areas_[area_].id];
    heads_ = headersOf(areas_[area_].id);
    listFrom_ = heads_.size() > (size_t)perPage() ? heads_.size() - (size_t)perPage() : 0;
    if (bars_) showList();
    else {
        s_ = St::List;
        listPrompt();
    }
}

bool MessageReader::key(char c) {
    switch (s_) {
        case St::Areas:
            if (!bars_ || !usingList_) {
                if (!lineKey(c, 3)) return true;
                if (line_.empty()) return false;
                int n = std::atoi(line_.c_str());
                line_.clear();
                if (n >= 1 && (size_t)n <= areas_.size()) openArea((size_t)n - 1);
                else showAreas();
                return true;
            }
            if (!list_.midKey() && (c | 0x20) == 'q') return false;
            {
                int pick = list_.key(c);
                out_ += list_.takeRedraw();
                if (list_.size() && (size_t)list_.current() < areas_.size()) out_ += listScreenStatus(areas_[(size_t)list_.current()].name);
                if (list_.escaped()) return false;
                if (pick >= 0 && (size_t)pick < areas_.size()) openArea((size_t)pick);
            }
            return true;
        case St::List:
            if (bars_ && usingList_) {
                if (!list_.midKey()) {
                    char k = (char)(c | 0x20);
                    if (k == 'r') {
                        out_ += std::string("r\r\n   ") + TEXT + "Message number: " + WHITE;
                        line_.clear();
                        usingList_ = false;
                        s_ = St::ReadNum;
                        return true;
                    }
                    if (k == 'n') {
                        nextNew();
                        return true;
                    }
                    if (k == 'p') {
                        startPost(false);
                        return true;
                    }
                    if (k == 'q') {
                        showAreas();
                        return true;
                    }
                }
                int before = list_.current();
                int shown = (int)list_.size();
                int pick = list_.key(c);
                out_ += list_.takeRedraw();
                {
                    size_t i = listFrom_ + (size_t)list_.current();
                    if (i < heads_.size()) out_ += listScreenStatus(statusLine(i, heads_[i].from + "  " + heads_[i].subject), tpl());
                }
                if (list_.escaped()) {
                    showAreas();
                    return true;
                }
                if (pick >= 0) {
                    size_t i = listFrom_ + (size_t)pick;
                    if (i < heads_.size()) show(heads_[i].num);
                    return true;
                }
                if (list_.clamped() < 0 && before == 0 && listFrom_ > 0) {
                    listFrom_ = listFrom_ > (size_t)perPage() ? listFrom_ - (size_t)perPage() : 0;
                    showList();
                } else if (list_.clamped() > 0 && shown > 0 && before == shown - 1 && listFrom_ + (size_t)shown < heads_.size()) {
                    listFrom_ += (size_t)perPage();
                    showList();
                }
                return true;
            }
            [[fallthrough]];  // the text list and a message being read share R N P Q
        case St::Reading:
            switch (c | 0x20) {
                case 'r':
                    out_ += std::string("r\r\n   ") + TEXT + "Message number: " + WHITE;
                    line_.clear();
                    s_ = St::ReadNum;
                    break;
                case 'n': nextNew(); break;
                case 'p': startPost(false); break;
                case 'a':
                    if (s_ == St::Reading) startPost(true);
                    break;
                case 'l': showList(); break;
                case 'q': showAreas(); break;
                default: break;
            }
            return true;
        case St::ReadNum:
            if (!lineKey(c, 6)) return true;
            if (line_.empty()) {
                s_ = St::List;
                listPrompt();
            } else {
                int n = std::atoi(line_.c_str());
                line_.clear();
                show(n);
            }
            return true;
        case St::To:
            if (!lineKey(c, 30)) return true;
            if (!line_.empty()) draft_.to = line_;
            if (draft_.to.empty()) draft_.to = "All";
            line_.clear();
            s_ = St::Subject;
            out_ += std::string("\r\n   ") + TEXT + "Subject" + (draft_.subject.empty() ? "" : std::string(" (Enter for ") + ORANGE + draft_.subject + TEXT + ")") +
                    ": " + WHITE;
            return true;
        case St::Subject:
            if (!lineKey(c, 60)) return true;
            if (!line_.empty()) draft_.subject = line_;
            line_.clear();
            if (draft_.subject.empty()) {
                out_ += std::string("\r\n   ") + ORANGE + "A message needs a subject.\r\n";
                s_ = St::List;
                listPrompt();
                return true;
            }
            s_ = St::Body;
            out_ += std::string("\r\n\r\n   ") + TEXT + "Write it. " + ORANGE + "/S" + TEXT + " on a line saves, " + ORANGE + "/A" + TEXT + " drops it.\r\n   " + WHITE;
            return true;
        case St::Body:
            if (!lineKey(c, 76)) return true;
            if (line_ == "/s" || line_ == "/S") {
                line_.clear();
                if (draft_.body.empty()) {
                    out_ += std::string("\r\n   ") + ORANGE + "Nothing written: not saved.\r\n";
                    s_ = St::List;
                    listPrompt();
                } else {
                    save();
                }
                return true;
            }
            if (line_ == "/a" || line_ == "/A") {
                line_.clear();
                out_ += std::string("\r\n   ") + TEXT + "Dropped.\r\n";
                s_ = St::List;
                listPrompt();
                return true;
            }
            draft_.body += line_ + "\n";
            line_.clear();
            if (++lines_ >= kMaxLines) {
                save();
                return true;
            }
            out_ += "\r\n   ";
            return true;
    }
    return true;
}

}  // namespace axis::bbs
