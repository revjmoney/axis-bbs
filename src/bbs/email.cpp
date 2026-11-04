// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#pragma GCC optimize("Os")  // menus and screens: size over speed (the door CPU and transfers stay -O2)
#include "bbs/email.h"

#include <cstdio>
#include <cstdlib>

#include "bbs/theme.h"

namespace axis::bbs {

namespace {
bool same(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); i++)
        if ((a[i] | 0x20) != (b[i] | 0x20)) return false;
    return true;
}
}  // namespace

int EmailBox::countNew(MessageStore& store, const std::string& me, int lastRead) {
    int n = 0;
    for (const Message& m : store.headers(kArea))
        if (m.num > lastRead && same(m.to, me)) n++;
    return n;
}

void EmailBox::begin() {
    s_ = St::Box;
    show();
}

void EmailBox::show() {
    mine_.clear();
    for (const Message& m : st_.headers(kArea))
        if (same(m.to, me_)) mine_.push_back(m);
    out_ += theme::CLS;
    out_ += "\r\n" + theme::drip("EMAIL");
    out_ += std::string("   ") + theme::fade("PRIVATE MAIL FOR") + " " + theme::WHITE + me_ + "\r\n";
    out_ += std::string("   ") + theme::DGREY + std::string(70, '\xc4') + "\r\n";
    if (mine_.empty()) out_ += std::string("   ") + theme::TEXT + "(no mail)\r\n";
    char b[200];
    size_t from = mine_.size() > 15 ? mine_.size() - 15 : 0;  // the newest 15
    for (size_t i = from; i < mine_.size(); i++) {
        const Message& m = mine_[i];
        std::snprintf(b, sizeof b, "   %s%3u%s %s %s%-20.20s %s%-28.28s %s%s\r\n", theme::KEY, (unsigned)(i + 1), theme::DGREY,
                      m.num > lastRead_ ? "*" : " ", theme::WHITE, m.from.c_str(), theme::TEXT, m.subject.c_str(), theme::DGREY, m.date.c_str());
        out_ += b;
    }
    out_ += "\r\n   " + theme::key("#", " read   ") + theme::key("S", "end   ") + theme::key("Q", "uit");
    prompt();
}

void EmailBox::prompt() { out_ += "\r\n   " + theme::fade("Email") + theme::ORANGE + ": " + theme::WHITE; }

void EmailBox::read(int i) {
    if (i < 1 || i > (int)mine_.size()) {
        out_ += std::string("\r\n   ") + theme::ORANGE + "No mail by that number." + theme::TEXT;
        prompt();
        return;
    }
    Message m;
    if (!st_.read(kArea, mine_[(size_t)i - 1].num, m) || !same(m.to, me_)) {  // (only ever their own)
        out_ += std::string("\r\n   ") + theme::ORANGE + "That one couldn't be read." + theme::TEXT;
        prompt();
        return;
    }
    if (m.num > lastRead_) lastRead_ = m.num;
    out_ += "\r\n\r\n";
    out_ += theme::window("MAIL " + std::to_string(i), {{"from", m.from}, {"subject", m.subject}, {"date", m.date}});
    out_ += std::string(theme::WHITE);
    for (char ch : m.body) out_ += ch == '\n' ? std::string("\r\n   ") : std::string(1, ch);
    out_ += "\r\n";
    prompt();
}

bool EmailBox::lineKey(char c, size_t max) {
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

bool EmailBox::key(char c) {
    switch (s_) {
        case St::Box:
            if ((c | 0x20) == 'q') return false;
            if ((c | 0x20) == 's') {
                draft_ = Message();
                draft_.from = me_;
                line_.clear();
                s_ = St::To;
                out_ += std::string("\r\n\r\n   ") + theme::TEXT + "To (a user's handle): " + theme::WHITE;
            } else if (c >= '0' && c <= '9') {
                line_.assign(1, c);
                out_ += c;
                s_ = St::Num;
            }
            return true;
        case St::Num:
            if (lineKey(c, 4)) {
                int n = std::atoi(line_.c_str());
                line_.clear();
                s_ = St::Box;
                read(n);
            }
            return true;
        case St::To:
            if (lineKey(c, 30)) {
                std::string who;
                if (line_.empty() || !findUser_ || !findUser_(line_, who)) {
                    out_ += std::string("\r\n   ") + theme::ORANGE + "No user goes by that handle." + theme::TEXT;
                    s_ = St::Box;
                    prompt();
                } else {
                    draft_.to = who;
                    s_ = St::Subject;
                    out_ += std::string("\r\n   ") + theme::TEXT + "Subject: " + theme::WHITE;
                }
                line_.clear();
            }
            return true;
        case St::Subject:
            if (lineKey(c, 50)) {
                draft_.subject = line_.empty() ? "(no subject)" : line_;
                line_.clear();
                lines_ = 0;
                s_ = St::Body;
                out_ += std::string("\r\n\r\n   ") + theme::TEXT + "Write it. " + theme::ORANGE + "/S" + theme::TEXT + " on a line sends, " +
                        theme::ORANGE + "/A" + theme::TEXT + " drops it.\r\n   " + theme::WHITE;
            }
            return true;
        case St::Body:
            if (lineKey(c, 76)) {
                std::string l = line_;
                line_.clear();
                if (l == "/a" || l == "/A") {
                    out_ += std::string("\r\n   ") + theme::TEXT + "Dropped.";
                    s_ = St::Box;
                    prompt();
                } else if (l == "/s" || l == "/S" || ++lines_ >= 60) {
                    if (l != "/s" && l != "/S") draft_.body += l + "\n";
                    if (!draft_.body.empty() && draft_.body.back() == '\n') draft_.body.pop_back();
                    if (!tagline_.empty()) draft_.body += "\n\n... " + tagline_;
                    draft_.date = now_ ? now_() : "";
                    int n = st_.post(kArea, draft_);
                    out_ += std::string("\r\n   ") + (n ? std::string(theme::TEXT) + "Sent to " + draft_.to + "." : std::string(theme::ORANGE) + "Couldn't send it, sorry.");
                    s_ = St::Box;
                    prompt();
                } else {
                    draft_.body += l + "\n";
                    out_ += "\r\n   ";
                }
            }
            return true;
    }
    return true;
}

}  // namespace axis::bbs
