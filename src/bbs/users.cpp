// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include "bbs/users.h"

#include <cctype>
#include <cstdlib>

namespace axis::bbs {

namespace {
bool sameName(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); i++)
        if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i])) return false;
    return true;
}
std::string oneLine(std::string s) {  // values live on one line of the file
    for (char& c : s)
        if (c == '\r' || c == '\n') c = ' ';
    return s;
}
User::Status statusFrom(const std::string& s) {
    return s == "validated" ? User::Status::Validated : s == "denied" ? User::Status::Denied : User::Status::Pending;
}
}  // namespace

const char* statusName(User::Status s) {
    return s == User::Status::Validated ? "validated" : s == User::Status::Denied ? "denied" : "pending";
}

void UserStore::parse(const std::string& text) {
    users_.clear();
    User*  u = nullptr;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(pos, end - pos);
        pos = end + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line == "[user]") {
            users_.emplace_back();
            u = &users_.back();
            continue;
        }
        size_t eq = line.find('=');
        if (!u || eq == std::string::npos) continue;
        std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        uint32_t    n = (uint32_t)std::strtoul(v.c_str(), nullptr, 10);
        if (k == "id") u->id = (int)n;
        else if (k == "handle") u->handle = v;
        else if (k == "salt") u->salt = v;
        else if (k == "hash") u->hash = v;
        else if (k == "real_name") u->realName = v;
        else if (k == "location") u->location = v;
        else if (k == "email") u->email = v;
        else if (k == "status") u->status = statusFrom(v);
        else if (k == "security") u->security = (int)n;
        else if (k == "calls") u->calls = n;
        else if (k == "created") u->created = n;
        else if (k == "last_call") u->lastCall = n;
        else if (k == "uploads") u->uploads = n;
        else if (k == "downloads") u->downloads = n;
        else if (k.rfind("q.", 0) == 0) {  // q.<question>=answer
            u->answers.emplace_back(k.substr(2), v);
        }
    }
}

std::string UserStore::format() const {
    std::string s = "# Axis Micro BBS Firmware users (written by the board)\n";
    for (const User& u : users_) {
        auto kv = [&](const char* k, const std::string& v) { s += std::string(k) + "=" + oneLine(v) + "\n"; };
        s += "[user]\n";
        kv("id", std::to_string(u.id));
        kv("handle", u.handle);
        kv("salt", u.salt);
        kv("hash", u.hash);
        kv("real_name", u.realName);
        kv("location", u.location);
        kv("email", u.email);
        kv("status", statusName(u.status));
        kv("security", std::to_string(u.security));
        kv("calls", std::to_string(u.calls));
        kv("created", std::to_string(u.created));
        kv("last_call", std::to_string(u.lastCall));
        kv("uploads", std::to_string(u.uploads));
        kv("downloads", std::to_string(u.downloads));
        for (auto& a : u.answers) {
            std::string q = oneLine(a.first);
            for (char& c : q)
                if (c == '=') c = '-';
            s += "q." + q + "=" + oneLine(a.second) + "\n";
        }
    }
    return s;
}

User* UserStore::find(const std::string& handle) {
    for (User& u : users_)
        if (sameName(u.handle, handle)) return &u;
    return nullptr;
}

User* UserStore::byId(int id) {
    for (User& u : users_)
        if (u.id == id) return &u;
    return nullptr;
}

User& UserStore::add(User u) {
    int next = 1;
    for (const User& o : users_)
        if (o.id >= next) next = o.id + 1;
    u.id = next;
    users_.push_back(std::move(u));
    return users_.back();
}

size_t UserStore::pending() const {
    size_t n = 0;
    for (const User& u : users_) n += u.status == User::Status::Pending;
    return n;
}

}  // namespace axis::bbs
