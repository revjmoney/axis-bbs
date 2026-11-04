// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#pragma GCC optimize("Os")  // menus and screens: size over speed (the door CPU and transfers stay -O2)
#include "bbs/msgreader.h"

#include <cstdio>
#include <cstdlib>

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

void MessageReader::showAreas() {
    s_ = St::Areas;
    out_ += "\x1b[0m\x1b[2J\x1b[H\r\n" + drip("MSGS") + "   " + fade("M E S S A G E   A R E A S") + "\r\n   " + DGREY + std::string(66, '\xc4') + "\r\n";
    for (size_t i = 0; i < areas_.size(); i++) {
        auto h = st_.headers(areas_[i].id);
        int  last = lastRead_.count(areas_[i].id) ? lastRead_[areas_[i].id] : 0, fresh = 0;
        for (auto& m : h) fresh += m.num > last;
        char b[160];
        std::snprintf(b, sizeof b, "   %s[%2u] %s%-34s %s%4u msgs%s", ORANGE, (unsigned)(i + 1), WHITE, areas_[i].name.c_str(), TEXT,
                      (unsigned)h.size(), fresh ? "" : "\r\n");
        out_ += b;
        if (fresh) out_ += std::string("  ") + ORANGE + std::to_string(fresh) + " new" + TEXT + "\r\n";
    }
    out_ += std::string("\r\n   ") + TEXT + "Area number (Enter for the main menu): " + WHITE;
    line_.clear();
}

void MessageReader::openArea(size_t i) {
    area_ = i;
    heads_ = st_.headers(areas_[i].id);
    showList();
}

void MessageReader::showList() {
    s_ = St::List;
    out_ += "\x1b[0m\x1b[2J\x1b[H\r\n   " + fade(areas_[area_].name) + std::string(TEXT) + "   " + std::to_string(heads_.size()) +
            " messages\r\n   " + DGREY + std::string(70, '\xc4') + "\r\n";
    if (heads_.empty()) out_ += std::string("   ") + TEXT + "(no messages yet: [P]ost the first)\r\n";
    int    last = lastRead_.count(areas_[area_].id) ? lastRead_[areas_[area_].id] : 0;
    size_t from = heads_.size() > (size_t)kPerPage ? heads_.size() - kPerPage : 0;  // the newest page
    for (size_t i = from; i < heads_.size(); i++) {
        const Message& m = heads_[i];
        char           b[200];
        std::snprintf(b, sizeof b, "   %s%c%s%4d %s%-14.14s %s%-14.14s %s%-26.26s\r\n", ORANGE, m.num > last ? '*' : ' ', DGREY, m.num, WHITE,
                      m.from.c_str(), TEXT, m.to.c_str(), WHITE, m.subject.c_str());
        out_ += b;
    }
    listPrompt();
}

void MessageReader::listPrompt() {
    out_ += "\r\n   " + theme::key("R", "ead #  ") + theme::key("N", "ext new  ") + theme::key("P", "ost  ") + theme::key("Q", "uit") + WHITE + ": ";
}

void MessageReader::show(int num) {
    Message m;
    if (!st_.read(areas_[area_].id, num, m)) {
        out_ += std::string("\r\n   ") + ORANGE + "No message #" + std::to_string(num) + " here.\r\n";
        s_ = St::List;
        listPrompt();
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
    heads_ = st_.headers(areas_[area_].id);
    for (const Message& m : heads_)
        if (m.num > last) {
            show(m.num);
            return;
        }
    out_ += std::string("\r\n   ") + TEXT + "No new messages in " + areas_[area_].name + ".\r\n";
    s_ = St::List;
    listPrompt();
}

void MessageReader::startPost(bool reply) {
    draft_ = Message();
    draft_.from = handle_;
    if (reply) {
        draft_.to = current_.from;
        draft_.subject = current_.subject.rfind("Re: ", 0) == 0 ? current_.subject : "Re: " + current_.subject;
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
    heads_ = st_.headers(areas_[area_].id);
    s_ = St::List;
    listPrompt();
}

bool MessageReader::key(char c) {
    switch (s_) {
        case St::Areas:
            if (!lineKey(c, 3)) return true;
            if (line_.empty()) return false;
            {
                int n = std::atoi(line_.c_str());
                line_.clear();
                if (n >= 1 && (size_t)n <= areas_.size()) openArea((size_t)n - 1);
                else showAreas();
            }
            return true;
        case St::List:
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
