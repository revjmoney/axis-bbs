// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include "bbs/panelproto.h"

#include <cctype>

#include "bbs/password.h"

namespace axis::bbs {

namespace {
bool same(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); i++)
        if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i])) return false;
    return true;
}
}  // namespace

PanelSession::PanelSession(const BbsConfig& cfg, const UserStore& users, std::string nonce)
    : cfg_(cfg), users_(users), nonce_(std::move(nonce)) {}

bool PanelSession::lookup(const std::string& handle, std::string& salt, std::string& hash, std::string& name) const {
    if (!cfg_.sysopHash.empty() && same(handle, cfg_.sysopHandle)) {
        salt = cfg_.sysopSalt, hash = cfg_.sysopHash, name = cfg_.sysopHandle;
        return true;
    }
    for (const User& u : users_.all())
        if (same(u.handle, handle) && u.security >= kSysopSecurity) {
            salt = u.salt, hash = u.hash, name = u.handle;
            return true;
        }
    return false;
}

std::string PanelSession::line(const std::string& raw) {
    std::string in = raw;
    while (!in.empty() && (in.back() == '\r' || in.back() == '\n')) in.pop_back();
    if (closed_) return "";
    if (in.rfind("SALT ", 0) == 0) {
        std::string salt, hash, name;
        return lookup(in.substr(5), salt, hash, name) ? "SALT " + salt + "\n" : "SALT -\n";
    }
    if (in.rfind("AUTH ", 0) == 0) {
        size_t      sp = in.find(' ', 5);
        std::string handle = in.substr(5, sp == std::string::npos ? std::string::npos : sp - 5);
        std::string got = sp == std::string::npos ? "" : in.substr(sp + 1), salt, hash, name;
        if (lookup(handle, salt, hash, name) && got == sha256Hex(nonce_ + hash)) {
            authed_ = true;
            who_ = name;
            if (onEvent) onEvent("panel: " + name + " logged in");
            return "OK\n";
        }
        if (++bad_ >= 3) {
            closed_ = true;
            if (onEvent) onEvent("panel: 3 bad logins (" + handle + ")");
            return "NO\nBYE\n";
        }
        return "NO\n";
    }
    if (in.rfind("CMD ", 0) == 0) {
        if (!authed_) return "NO\n";
        std::string out = run ? run(in.substr(4)) : std::string();
        if (!out.empty() && out.back() != '\n') out += '\n';
        return out + "END\n";
    }
    if (!authed_) return "NO\n";
    return extra ? extra(in) : "?\n";
}

}  // namespace axis::bbs
