// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#pragma GCC optimize("Os")  // menus and screens: size over speed (the door CPU and transfers stay -O2)
#include "bbs/dosshell.h"

#include <cctype>
#include <cstdio>

#include "bbs/archive.h"
#include "bbs/password.h"
#include "bbs/version.h"

namespace axis::bbs {

namespace {
std::string upper(std::string s) {
    for (char& c : s) c = (char)std::toupper((unsigned char)c);
    return s;
}
std::vector<std::string> split(const std::string& s, const char* seps) {
    std::vector<std::string> v;
    std::string              w;
    for (char c : s) {
        if (std::string(seps).find(c) != std::string::npos) {
            if (!w.empty()) v.push_back(w);
            w.clear();
        } else {
            w += c;
        }
    }
    if (!w.empty()) v.push_back(w);
    return v;
}
std::string baseName(const std::string& p) {
    size_t s = p.find_last_of("\\/:");
    return s == std::string::npos ? p : p.substr(s + 1);
}
// cwd plus a DOS path, with '.' and '..' applied and clamped at the sandbox root ("" = the root,
// parts separated by '\'). False if it names another drive or a part holds ':'.
bool joinPath(const std::string& cwd, std::string p, std::string& out) {
    for (char& c : p)
        if (c == '/') c = '\\';
    if (p.size() >= 2 && p[1] == ':') {
        if (std::toupper((unsigned char)p[0]) != 'C') return false;
        p = p.substr(2);
    }
    std::vector<std::string> parts;
    if (p.empty() || p[0] != '\\') parts = split(cwd, "\\");
    for (const std::string& part : split(p, "\\")) {
        if (part == "..") {
            if (!parts.empty()) parts.pop_back();
        } else if (part != ".") {
            if (part.find(':') != std::string::npos) return false;
            parts.push_back(upper(part));
        }
    }
    out.clear();
    for (auto& s : parts) out += (out.empty() ? "" : "\\") + s;
    return true;
}
std::string inDir(const std::string& dir, const std::string& name) { return "\\" + dir + (dir.empty() ? "" : "\\") + name; }
}  // namespace

DosShell::DosShell(door::HostFs& fs, door::ByteStream& line, DosGate& gate, DosShellConfig cfg)
    : fs_(fs), line_(line), gate_(gate), cfg_(std::move(cfg)) {}

void DosShell::begin() {
    typed_.clear();
    tries_ = 0;
    uint32_t now = cfg_.clockMs ? cfg_.clockMs() : 0;
    if (cfg_.hash.empty()) {
        send("\r\nThe DOS prompt is not enabled (no password set on the board).\r\n");
        st_ = St::Done;
    } else if (gate_.lockedUntil && (int32_t)(gate_.lockedUntil - now) > 0) {
        send("\r\nThe DOS prompt is locked after too many wrong passwords. Try again later.\r\n");
        events_.push_back("DOS prompt: " + cfg_.handle + " tried it while locked");
        st_ = St::Done;
    } else {
        gate_.lockedUntil = 0;
        send("\r\nDOS prompt password: ");
        st_ = St::Password;
    }
}

bool DosShell::step() {
    if (!line_.carrier()) return false;
    while (st_ != St::Done && line_.available() > 0) {
        int c = line_.read();
        if (st_ == St::More) {  // a page is up: Q stops, any other key shows the next
            if ((c | 0x20) == 'q') {
                fs_.close(moreH_);
                moreH_ = -1;
                moreBuf_.clear();
                st_ = St::Prompt;
                send("\r\n\r\n");
                prompt();
            } else {
                send("\r\x1b[K");
                moreNext();
            }
            continue;
        }
        if (st_ == St::Prompt && esc_ == 1) {
            esc_ = c == '[' || c == 'O' ? 2 : 0;
            continue;
        }
        if (st_ == St::Prompt && esc_ == 2) {  // up / down through the history, F3 the last command
            esc_ = 0;
            if (c == 'A' && histAt_ > 0) recall(history_[--histAt_]);
            else if (c == 'B' && histAt_ + 1 < history_.size()) recall(history_[++histAt_]);
            else if (c == 'B') {
                histAt_ = history_.size();
                recall("");
            } else if (c == 'R' && !history_.empty()) recall(history_.back());
            continue;
        }
        if (st_ == St::Prompt && c == 0x1b) {
            esc_ = 1;
            continue;
        }
        if (c == '\r') {
            std::string t = typed_;
            typed_.clear();
            send("\r\n");
            if (st_ == St::Password) password(t);
            else {
                if (!t.empty() && (history_.empty() || history_.back() != t)) history_.push_back(t);
                if (history_.size() > 20) history_.erase(history_.begin());
                histAt_ = history_.size();
                command(t);
            }
        } else if (c == 8 || c == 127) {
            if (!typed_.empty()) {
                typed_.pop_back();
                send("\b \b");
            }
        } else if (c >= 32 && c < 127 && typed_.size() < 126) {
            typed_ += (char)c;
            send(st_ == St::Password ? std::string("*") : std::string(1, (char)c));
        }
    }
    return st_ != St::Done;
}

void DosShell::password(const std::string& typed) {
    if (checkPassword(cfg_.salt, cfg_.hash, typed)) {
        gate_.fails = 0;
        events_.push_back("DOS prompt: " + cfg_.handle + " logged in");
        send(std::string("\r\n") + kAxisSoftware + " DOS sandbox. Type HELP for commands, EXIT to leave.\r\n\r\n");
        st_ = St::Prompt;
        prompt();
        return;
    }
    tries_++;
    gate_.fails++;
    events_.push_back("DOS prompt: WRONG PASSWORD from " + cfg_.handle);
    if (gate_.fails >= kLockFails) {
        gate_.fails = 0;
        gate_.lockedUntil = (cfg_.clockMs ? cfg_.clockMs() : 0) + kLockMs;
        if (!gate_.lockedUntil) gate_.lockedUntil = 1;
        events_.push_back("DOS prompt: locked for 10 minutes");
        send("Access denied. The DOS prompt is now locked.\r\n");
        st_ = St::Done;
    } else if (tries_ >= kTries) {
        send("Access denied.\r\n");
        st_ = St::Done;
    } else {
        send("Wrong password.\r\nDOS prompt password: ");
    }
}

void DosShell::recall(const std::string& cmd) {
    for (size_t i = 0; i < typed_.size(); i++) send("\b \b");
    typed_ = cmd;
    send(cmd);
}

void DosShell::moreNext() {  // up to 22 lines, then "-- More --" (or the end of the file)
    int lines = 0;
    for (;;) {
        size_t nl;
        while (lines < 22 && (nl = moreBuf_.find('\n')) != std::string::npos) {
            send(moreBuf_.substr(0, nl + 1));
            moreBuf_.erase(0, nl + 1);
            lines++;
        }
        if (lines >= 22) {
            send("-- More -- (any key, Q stops)");
            return;
        }
        char buf[256];
        int  n = fs_.read(moreH_, buf, sizeof buf);
        if (n <= 0) break;
        moreBuf_.append(buf, (size_t)n);
    }
    send(moreBuf_);
    moreBuf_.clear();
    fs_.close(moreH_);
    moreH_ = -1;
    st_ = St::Prompt;
    send("\r\n\r\n");
    prompt();
}

void DosShell::tree(const std::string& dir, const std::string& lead) {
    door::FindState st;
    fs_.find(inDir(dir, "*.*"), 0x10, st);
    std::vector<std::string> dirs;
    for (auto& f : st.matches)
        if ((f.attr & 0x10) && f.name83 != "." && f.name83 != "..") dirs.push_back(f.name83);
    for (size_t i = 0; i < dirs.size(); i++) {
        bool last = i + 1 == dirs.size();
        send(lead + (last ? "\xc0\xc4\xc4\xc4" : "\xc3\xc4\xc4\xc4") + dirs[i] + "\r\n");
        tree(dir + (dir.empty() ? "" : "\\") + dirs[i], lead + (last ? "    " : "\xb3   "));
    }
}

namespace {
struct ShellFile : RandomFile {  // a sandbox file for the archive lister
    door::HostFs& fs;
    int           h;
    uint32_t      bytes;
    ShellFile(door::HostFs& f, int handle) : fs(f), h(handle) {
        int64_t end = fs.seek(h, 0, 2);
        bytes = end > 0 ? (uint32_t)end : 0;
    }
    ~ShellFile() override { fs.close(h); }
    uint32_t size() const override { return bytes; }
    bool     readAt(uint32_t off, uint8_t* buf, size_t n) override {
        if ((uint64_t)off + n > bytes || fs.seek(h, off, 0) != (int64_t)off) return false;
        return fs.read(h, buf, (int)n) == (int)n;
    }
};
}  // namespace

void DosShell::listArc(const std::string& arg) {
    std::string p;
    if (arg.empty()) {
        send("Required parameter missing\r\n");
        return;
    }
    if (!joinPath(fs_.cwd(), arg, p) || p.empty()) {
        send("Invalid path\r\n");
        return;
    }
    int h = fs_.open("\\" + p, 0, false, false);
    if (h < 0) {
        error(-h);
        return;
    }
    ShellFile   f(fs_, h);
    ArchiveInfo a = listArchive(p, f);
    if (a.kind.empty()) {
        send(upper(arg) + " is not an archive this board knows (ZIP, RAR, ARJ, TAR, GZ)\r\n");
        return;
    }
    char     row[128];
    uint64_t total = 0;
    send(" " + a.kind + " " + upper(arg) + "\r\n\r\n  Length   Packed  Method    Name\r\n  ------   ------  ------    ----\r\n");
    for (const ArchiveEntry& e : a.entries) {
        std::snprintf(row, sizeof row, "%8lu %8lu  %-8.8s  %s\r\n", (unsigned long)e.size, (unsigned long)e.packed, e.method.c_str(), e.name.c_str());
        send(row);
        total += e.size;
    }
    std::snprintf(row, sizeof row, "  ------\r\n%8llu  %u files\r\n", (unsigned long long)total, (unsigned)a.entries.size());
    send(row);
}

void DosShell::prompt() { send("C:\\" + fs_.cwd() + ">"); }

void DosShell::error(int code) {
    switch (code) {
        case 2: send("File not found\r\n"); break;
        case 3: send("Path not found\r\n"); break;
        case 5: send("Access denied\r\n"); break;
        default: send("Error " + std::to_string(code) + "\r\n"); break;
    }
}

void DosShell::command(const std::string& typed) {
    auto w = split(typed, " \t");
    if (w.empty()) {
        prompt();
        return;
    }
    std::string cmd = upper(w[0]), a = w.size() > 1 ? w[1] : "", b = w.size() > 2 ? w[2] : "";
    if (cmd.rfind("CD", 0) == 0 && cmd.size() > 2 && (cmd[2] == '\\' || cmd[2] == '.')) {  // "cd\" and "cd.."
        a = w[0].substr(2);
        cmd = "CD";
    }
    if (cmd == "EXIT") {
        st_ = St::Done;
        return;
    } else if (cmd == "HELP" || cmd == "?") {
        send("DIR [path]   CD [dir]   MD dir   RD dir   TYPE file   MORE file   COPY from to\r\n"
             "DEL file     REN old new   TREE   DATE   TIME   CLS   VER   ECHO text   EXIT\r\n"
             "PKUNZIP -V file (or LIST file): what's inside a ZIP, RAR, ARJ, TAR or GZ\r\n"
             "Up arrow: the commands before (DOSKEY), F3: the last one\r\n");
    } else if (cmd == "VER") {
        send(std::string("\r\n") + kAxisSoftware + " " + kAxisVersion + " DOS sandbox\r\n");
    } else if (cmd == "CLS") {
        send("\x1b[2J\x1b[H");
    } else if (cmd == "ECHO") {
        size_t p = typed.find_first_not_of(" \t");
        p = typed.find_first_of(" \t", p);
        send((p == std::string::npos ? std::string() : typed.substr(p + 1)) + "\r\n");
    } else if (cmd == "DATE" || cmd == "TIME") {
        std::string t = cfg_.timeText ? cfg_.timeText() : "";
        send("Current " + std::string(cmd == "DATE" ? "date and time" : "time") + " is " + (t.empty() ? "unknown (the clock isn't set)" : t) + "\r\n");
    } else if (cmd == "TREE") {
        send("Folder PATH listing\r\nC:\\" + fs_.cwd() + "\r\n");
        tree(fs_.cwd(), "");
    } else if (cmd == "PKUNZIP" || cmd == "LIST" || cmd == "ARJ" || cmd == "UNRAR") {
        std::string f = cmd == "LIST" ? a : (upper(a) == "-V" || upper(a) == "L" || upper(a) == "V") ? b : "";
        if (f.empty()) send(cmd == "LIST" ? "Required parameter missing\r\n" : "Only listing works here: " + cmd + (cmd == "PKUNZIP" ? " -V" : " L") + " file\r\n");
        else listArc(f);
    } else if (cmd == "MORE") {
        std::string p;
        if (a.empty()) send("Required parameter missing\r\n");
        else if (!joinPath(fs_.cwd(), a, p) || p.empty()) send("Invalid path\r\n");
        else if ((moreH_ = fs_.open("\\" + p, 0, false, false)) < 0) {
            error(-moreH_);
            moreH_ = -1;
        } else {
            st_ = St::More;
            moreBuf_.clear();
            moreNext();
            return;
        }
    } else if (cmd == "DIR") {
        dir(a);
    } else if (cmd == "CD" || cmd == "CHDIR") {
        cd(a);
    } else if (cmd == "TYPE") {
        type(a);
    } else if (cmd == "COPY") {
        copy(a, b);
    } else if (cmd == "MD" || cmd == "MKDIR" || cmd == "RD" || cmd == "RMDIR" || cmd == "DEL" || cmd == "ERASE") {
        std::string p;
        if (a.empty()) send("Required parameter missing\r\n");
        else if (!joinPath(fs_.cwd(), a, p) || p.empty()) send("Invalid path\r\n");
        else {
            int r = cmd == "MD" || cmd == "MKDIR" ? fs_.mkdir("\\" + p) : cmd[0] == 'R' ? fs_.rmdir("\\" + p) : fs_.remove("\\" + p);
            if (r < 0) error(-r);
        }
    } else if (cmd == "REN" || cmd == "RENAME") {
        std::string from;
        if (b.empty()) send("Required parameter missing\r\n");
        else if (!joinPath(fs_.cwd(), a, from) || from.empty() || b.find_first_of("\\/:") != std::string::npos)
            send("Invalid path\r\n");
        else {
            size_t      s = from.find_last_of('\\');
            std::string to = (s == std::string::npos ? "" : from.substr(0, s + 1)) + upper(b);
            int         r = fs_.rename("\\" + from, "\\" + to);
            if (r < 0) error(-r);
        }
    } else {
        send("Bad command or file name\r\n");
    }
    if (st_ == St::Prompt) {
        send("\r\n");
        prompt();
    }
}

void DosShell::cd(const std::string& arg) {
    if (arg.empty()) {
        send("C:\\" + fs_.cwd() + "\r\n");
        return;
    }
    std::string d;
    if (!joinPath(fs_.cwd(), arg, d) || (!d.empty() && !fs_.isDir("\\" + d))) {
        send("Invalid directory\r\n");
        return;
    }
    fs_.setCwd(d);
}

void DosShell::dir(const std::string& arg) {
    std::string d = fs_.cwd(), pattern = "*.*";
    if (!arg.empty()) {
        std::string p;
        if (!joinPath(fs_.cwd(), arg, p)) {
            send("Invalid drive specification\r\n");
            return;
        }
        if (p.empty() || fs_.isDir("\\" + p)) {
            d = p;
        } else {
            size_t s = p.find_last_of('\\');
            d = s == std::string::npos ? "" : p.substr(0, s);
            pattern = s == std::string::npos ? p : p.substr(s + 1);
        }
    }
    door::FindState st;
    fs_.find(inDir(d, pattern), 0x10, st);
    send(" Directory of C:\\" + d + "\r\n\r\n");
    uint64_t total = 0;
    int      files = 0;
    char     row[96];
    for (auto& f : st.matches) {
        std::string n = f.name83, ext;
        size_t      dot = n.find('.');
        if (dot != std::string::npos && n != "." && n != "..") {
            ext = n.substr(dot + 1);
            n = n.substr(0, dot);
        }
        if (f.attr & 0x10) {
            std::snprintf(row, sizeof row, "%-8s %-3s    <DIR>\r\n", n.c_str(), ext.c_str());
        } else {
            std::snprintf(row, sizeof row, "%-8s %-3s %12lu\r\n", n.c_str(), ext.c_str(), (unsigned long)f.size);
            total += f.size;
            files++;
        }
        send(row);
    }
    if (st.matches.empty()) send("File not found\r\n");
    uint64_t inBox = used(""), left = inBox >= cfg_.quotaBytes ? 0 : cfg_.quotaBytes - inBox;
    std::snprintf(row, sizeof row, "%9d file(s) %12llu bytes\r\n%25llu bytes free\r\n", files, (unsigned long long)total,
                  (unsigned long long)left);
    send(row);
}

void DosShell::type(const std::string& arg) {
    std::string p;
    if (arg.empty()) {
        send("Required parameter missing\r\n");
        return;
    }
    if (!joinPath(fs_.cwd(), arg, p) || p.empty()) {
        send("Invalid path\r\n");
        return;
    }
    int h = fs_.open("\\" + p, 0, false, false);
    if (h < 0) {
        error(-h);
        return;
    }
    char buf[256];
    for (int n; (n = fs_.read(h, buf, sizeof buf)) > 0;) line_.write((const uint8_t*)buf, (size_t)n);
    fs_.close(h);
    send("\r\n");
}

uint64_t DosShell::used(const std::string& dir) {
    door::FindState st;
    fs_.find(inDir(dir, "*.*"), 0x10, st);
    uint64_t total = 0;
    for (auto& f : st.matches) {
        if (f.name83 == "." || f.name83 == "..") continue;
        if (f.attr & 0x10) total += used(dir + (dir.empty() ? "" : "\\") + f.name83);
        else total += f.size;
    }
    return total;
}

void DosShell::copy(const std::string& from, const std::string& to) {
    std::string src, dst;
    if (from.empty()) {
        send("Required parameter missing\r\n");
        return;
    }
    if (!joinPath(fs_.cwd(), from, src) || src.empty() || !joinPath(fs_.cwd(), to.empty() ? "." : to, dst)) {
        send("Invalid path\r\n");
        return;
    }
    if (dst.empty() || fs_.isDir("\\" + dst)) dst += (dst.empty() ? "" : "\\") + upper(baseName(src));
    if (dst == src) {
        send("File cannot be copied onto itself\r\n");
        return;
    }
    door::FileInfo info;
    if (!fs_.stat("\\" + src, info) || (info.attr & 0x10)) {
        send("File not found\r\n");
        return;
    }
    if (used("") + info.size > cfg_.quotaBytes) {
        send("Insufficient disk space (the sandbox is full)\r\n");
        return;
    }
    int in = fs_.open("\\" + src, 0, false, false);
    if (in < 0) {
        error(-in);
        return;
    }
    int out = fs_.open("\\" + dst, 1, true, true);
    if (out < 0) {
        fs_.close(in);
        error(-out);
        return;
    }
    char buf[512];
    bool whole = true;
    for (int n; (n = fs_.read(in, buf, sizeof buf)) > 0;)
        if (fs_.write(out, buf, n) != n) whole = false;
    fs_.close(in);
    fs_.close(out);
    send(whole ? "        1 file(s) copied\r\n" : "Write error\r\n");
}

}  // namespace axis::bbs
