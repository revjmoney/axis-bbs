// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#pragma GCC optimize("Os")  // menus and screens: size over speed (the door CPU and transfers stay -O2)
#include "bbs/newuser.h"

#include "bbs/password.h"
#include "bbs/theme.h"

namespace axis::bbs {

namespace {
bool yes(const std::map<std::string, std::string>& a, const char* k) {
    auto it = a.find(k);
    return it != a.end() && it->second == "yes";
}
bool no(const std::map<std::string, std::string>& a, const char* k) {
    auto it = a.find(k);
    return it != a.end() && it->second == "no";
}
}  // namespace

NewUserApp::NewUserApp(std::string handle, std::string& out, const BbsConfig& cfg, uint32_t (*rng)())
    : handle_(std::move(handle)), out_(out), cfg_(cfg), rng_(rng) {
    using A = std::map<std::string, std::string>;
    steps_ = {
        {"ABOUT YOU", "password", "Pick a password (6 or more characters)", Kind::Password, {}},
        {"ABOUT YOU", "password2", "Type it again", Kind::Confirm, {}},
        {"ABOUT YOU", "First name", "Your first name (only the sysop sees it)", Kind::Text, {}},
        {"ABOUT YOU", "Location", "Where are you calling from (city, state)", Kind::Text, {}},
        {"ABOUT YOU", "Email", "Email, optional (only the sysop sees it)", Kind::Optional, {}},
        {"ABOUT YOU", "Heard about the board", "How did you hear about " + cfg_.bbsName, Kind::Text, {}},
        {"THE SHOP", "Own a CNC", "Do you own a CNC machine (router, mill, lathe, laser or 3D printer)", Kind::YesNo, {}},
        {"THE SHOP", "Machine and controller", "What machine and controller", Kind::Text,
         [](const A& a) { return yes(a, "Own a CNC"); }},
        {"THE SHOP", "What they cut", "What do you cut on it", Kind::Text, [](const A& a) { return yes(a, "Own a CNC"); }},
        {"THE SHOP", "Knows someone with a CNC", "Do you know anyone with a CNC machine", Kind::YesNo,
         [](const A& a) { return no(a, "Own a CNC"); }},
        {"THE SHOP", "Who has one", "Who, and what do they run", Kind::Text, [](const A& a) { return yes(a, "Knows someone with a CNC"); }},
        {"THE SHOP", "Rotary axis", "Got a rotary (4th) axis, or want one", Kind::YesNo, {}},
        {"ROTATOCAM", "Knows RotatoCAM", "Do you know about RotatoCAM", Kind::YesNo, {}},
        {"ROTATOCAM", "RotatoCAM Pro user", "Are you a RotatoCAM Pro user", Kind::YesNo,
         [](const A& a) { return yes(a, "Knows RotatoCAM"); }},
        {"ROTATOCAM", "Been to rotatocam.com", "Have you been to rotatocam.com", Kind::YesNo, {}},
        {"ROTATOCAM", "Been to jmscnc.com", "Have you been to jmscnc.com", Kind::YesNo, {}},
        {"ROTATOCAM", "CAM software", "What CAM software do you use", Kind::Text, {}},
        {"THE SCENE", "First BBS", "What was the first BBS you ever called", Kind::Text, {}},
        {"THE SCENE", "Best ANSI group", "Best ANSI art group of all time", Kind::Text, {}},
        {"THE SCENE", "Terminal", "What terminal program are you on right now", Kind::Text, {}},
        {"THE SCENE", "Would upload", "Got anything to upload? What", Kind::Text, {}},
        {"THE SCENE", "Why let them in", "Why should we let you in", Kind::Text, {}},
        {"THE RULES", "Agreed to the rules", "Do you agree to the rules", Kind::Agree, {}},
        {"LAST STEP", "submit", "Send your application to the sysop", Kind::Submit, {}},
    };
}

void NewUserApp::begin() {
    out_ += theme::CLS;
    out_ += "\r\n";
    out_ += theme::window("NEW USER APPLICATION", {{"handle", handle_},
                                                   {"questions", "about twenty, a few minutes"},
                                                   {"who reads it", "the sysop, and only the sysop"},
                                                   {"then", "the sysop validates you"}});
    out_ += "\r\n   " + theme::fade("Real answers get you in.") + "  " + theme::TEXT +
            "This board is for people who make chips fly.\r\n";
    at_ = 0;
    ask();
}

void NewUserApp::section(const std::string& title) {
    if (title == lastSection_) return;
    lastSection_ = title;
    std::string spaced;
    for (char c : title) {
        spaced += c;
        spaced += ' ';
    }
    out_ += "\r\n\r\n  ";
    out_ += theme::ORANGE;
    out_ += "\xdc\xdc ";
    out_ += theme::fade(spaced);
    out_ += theme::DGREY;
    out_ += std::string(spaced.size() < 60 ? 60 - spaced.size() : 4, '\xc4');
    out_ += "\r\n";
    if (title == "THE RULES") {
        out_ += "\r\n";
        out_ += theme::window("THE RULES", {{"1", "Real handle, real answers. Lamers get deleted."},
                                            {"2", "Upload only what you have the right to share."},
                                            {"3", "Be cool to the other callers."},
                                            {"4", "The sysop's word is final."}});
    }
}

void NewUserApp::ask() {
    while (at_ < steps_.size() && steps_[at_].when && !steps_[at_].when(answers_)) at_++;  // follow-ups that don't apply
    if (at_ >= steps_.size()) return;
    const Step& s = steps_[at_];
    section(s.section);
    if (s.kind == Kind::Submit) {
        out_ += "\r\n";
        for (auto& a : answers_)
            if (a.first != "password" && a.first != "password2")
                out_ += std::string("   ") + theme::DGREY + "\xfa " + theme::LGREY + a.first + theme::DGREY + ": " + theme::WHITE +
                        a.second.substr(0, 50) + "\r\n";
    }
    out_ += "\r\n   ";
    out_ += theme::TEXT + s.prompt;
    if (s.kind == Kind::YesNo || s.kind == Kind::Agree || s.kind == Kind::Submit) out_ += std::string(" ") + theme::key("Y", "/") + theme::key("N", "");
    out_ += theme::ORANGE;
    out_ += ": ";
    out_ += theme::WHITE;
    line_.clear();
}

void NewUserApp::next() {
    at_++;
    ask();
}

NewUserApp::Outcome NewUserApp::key(char c) {
    if (at_ >= steps_.size()) return Outcome::Cancelled;
    const Step& s = steps_[at_];
    if (s.kind == Kind::YesNo || s.kind == Kind::Agree || s.kind == Kind::Submit) {
        char k = (char)(c | 0x20);
        if (k != 'y' && k != 'n') return Outcome::Running;
        out_ += k == 'y' ? "yes" : "no";
        if (s.kind == Kind::Agree && k == 'n') {
            out_ += "\r\n\r\n   " + theme::fade("No hard feelings.") + theme::TEXT + " Nothing was saved.\r\n";
            return Outcome::Cancelled;
        }
        if (s.kind == Kind::Submit) {
            if (k == 'n') {
                out_ += "\r\n\r\n   " + std::string(theme::TEXT) + "Application thrown away. Nothing was saved.\r\n";
                return Outcome::Cancelled;
            }
            user_ = User();
            user_.handle = handle_;
            user_.salt = makeSalt(rng_);
            user_.hash = hashPassword(user_.salt, password_);
            // Rev's rule: a machine owner (CNC, laser, 3D printer) or a RotatoCAM Pro user is in at once;
            // everyone else waits for the sysop
            bool vouched = answers_["Own a CNC"] == "yes" || answers_["RotatoCAM Pro user"] == "yes";
            user_.status = vouched ? User::Status::Validated : User::Status::Pending;
            user_.security = vouched ? (cfg_.newUserSecurity > 20 ? cfg_.newUserSecurity : 20) : cfg_.newUserSecurity;
            user_.realName = answers_["First name"];
            user_.location = answers_["Location"];
            user_.email = answers_["Email"];
            for (const Step& q : steps_) {  // in the order they were asked
                auto it = answers_.find(q.key);
                if (it != answers_.end() && q.kind != Kind::Password && q.kind != Kind::Confirm && q.kind != Kind::Submit)
                    user_.answers.emplace_back(q.key, it->second);  // the short label fits the sysop's screen
            }
            return Outcome::Submitted;
        }
        answers_[s.key] = k == 'y' ? "yes" : "no";
        next();
        return Outcome::Running;
    }
    bool secret = s.kind == Kind::Password || s.kind == Kind::Confirm;
    if (c == '\r') {
        std::string v = line_;
        size_t      a = v.find_first_not_of(' '), b = v.find_last_not_of(' ');
        v = a == std::string::npos ? "" : v.substr(a, b - a + 1);
        if (s.kind == Kind::Password && v.size() < 6) {
            out_ += "\r\n   " + std::string(theme::ORANGE) + "Six characters at least.";
            ask();
            return Outcome::Running;
        }
        if (s.kind == Kind::Confirm && v != password_) {
            out_ += "\r\n   " + std::string(theme::ORANGE) + "Those didn't match. Once more from the top.";
            at_--;  // back to the first password
            ask();
            return Outcome::Running;
        }
        if (s.kind == Kind::Text && v.empty()) {
            ask();
            return Outcome::Running;
        }
        if (s.kind == Kind::Password) password_ = v;
        else if (s.kind != Kind::Confirm) answers_[s.key] = v.empty() ? "(skipped)" : v;
        next();
        return Outcome::Running;
    }
    if (c == '\b' || c == 0x7f) {
        if (!line_.empty()) {
            line_.pop_back();
            out_ += "\b \b";
        }
    } else if ((unsigned char)c >= 0x20 && (unsigned char)c < 0x7f && line_.size() < 70) {
        line_ += c;
        out_ += secret ? '*' : c;
    }
    return Outcome::Running;
}

}  // namespace axis::bbs
