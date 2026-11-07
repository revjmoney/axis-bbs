// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#pragma GCC optimize("Os")  // menus and screens: size over speed (the door CPU and transfers stay -O2)
#include "bbs/filebrowser.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>

#include "bbs/archive.h"
#include "bbs/listscreen.h"
#include "bbs/theme.h"
#include "bbs/zipreader.h"

namespace axis::bbs {

namespace {
const char* const CLS = "\x1b[0m\x1b[2J\x1b[H";
const char* const DIM = "\x1b[0;37m";
const char* const HI = "\x1b[1;37m";
#define CYAN theme::ORANGE  // the theme's accent colour
const char* const YEL = "\x1b[1;37m";
const char* const MAG = "\x1b[1;30m";
const char* const GRN = "\x1b[0;37m";
#define AMBER theme::ORANGE

bool sameName(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); i++)
        if (std::toupper((unsigned char)a[i]) != std::toupper((unsigned char)b[i])) return false;
    return true;
}
}  // namespace

void FileBrowser::begin() { showAreas(); }

bool FileBrowser::lineKey(char c, size_t max) {
    if (c == '\r') {
        out_ += "\r\n";
        return true;
    }
    if (c == '\b' || c == 0x7f) {
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

void FileBrowser::showAreas() {
    st_ = St::Areas;
    line_.clear();
    typedAreas_ = false;
    if (!bars_) {
        out_ += CLS;
        out_ += listScreenHead("FILES", "FILE AREAS");
        out_ += std::string("   ") + theme::ORANGE + std::string(70, '\xcd') + "\r\n";
        usingList_ = false;
        for (size_t i = 0; i < areas_.size(); i++) {
            size_t n = lib_.list(areas_[i].id).size();
            char   b[128];
            std::snprintf(b, sizeof b, "   %s[%2u]%s %-34s%s%4u file%s\r\n", CYAN, (unsigned)(i + 1), HI, areas_[i].name.c_str(), DIM,
                          (unsigned)n, n == 1 ? "" : "s");
            out_ += b;
        }
        out_ += DIM;
        out_ += "\r\n   " + theme::key("N", "ew files since your last call   ") + theme::key("/", " search") + DIM +
                "\r\n   Area number (Enter for the main menu): ";
        out_ += HI;
        return;
    }
    std::vector<std::string> rows;
    rows.reserve(areas_.size());
    for (size_t i = 0; i < areas_.size(); i++) {  // names and counts only: one pass, not every file's bytes
        size_t n = lib_.list(areas_[i].id).size();
        char   b[80];
        std::snprintf(b, sizeof b, "%-40.40s %4u file%s", areas_[i].name.c_str(), (unsigned)n, n == 1 ? "" : "s");
        rows.emplace_back(b);
    }
    list_.setItems(std::move(rows));
    usingList_ = true;
    out_ += listScreenTop("FILE AREAS", "Area                                      Files");
    out_ += list_.draw();
    out_ += listScreenStatus(areas_.empty() ? "" : areas_[0].name);
    out_ += listScreenKeys("arrows move   Enter opens   number+Enter   " + theme::key("N", "ew") + "  " + theme::key("/", "search") + "  " +
                           theme::key("Q", "uit") + " or Esc");
}

void FileBrowser::openArea(size_t i) {
    area_ = i;
    page_ = 0;
    files_ = lib_.list(areas_[i].id);
    st_ = St::List;
    showPage();
}

void FileBrowser::showPage(int at) {
    int pages = ((int)files_.size() + kPerPage - 1) / kPerPage;
    if (pages == 0) pages = 1;
    if (page_ >= pages) page_ = pages - 1;
    if (page_ < 0) page_ = 0;
    st_ = St::List;
    char b[160];
    if (!bars_) {
        out_ += CLS;
        std::snprintf(b, sizeof b, "\r\n   %s%s%s   page %d of %d, %u files\r\n", YEL, areas_[area_].name.c_str(), DIM, page_ + 1, pages,
                      (unsigned)files_.size());
        out_ += b;
        out_ += MAG;
        out_ += "   \xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\r\n";
    }
    size_t begin = (size_t)page_ * kPerPage;
    size_t end = begin + kPerPage;
    if (end > files_.size()) end = files_.size();
    if (!bars_) {
        usingList_ = false;
        if (files_.empty()) {
            out_ += DIM;
            out_ += "   (nothing here yet)\r\n";
        }
        for (size_t i = begin; i < end; i++) {
            const FileEntry& f = files_[i];
            std::string      desc = fileAbout(f.desc, 50);
            std::snprintf(b, sizeof b, "   %s%-13s%s%7s  %s%s\r\n", HI, f.name.substr(0, 13).c_str(), GRN, fileSizeText(f.size).c_str(), DIM,
                          desc.c_str());
            out_ += b;
        }
        listPrompt();
        return;
    }
    std::vector<std::string> rows;
    rows.reserve(end - begin);
    for (size_t i = begin; i < end; i++) {  // this page only, not the whole area
        const FileEntry& f = files_[i];
        std::string      desc = fileAbout(f.desc, 40);
        std::snprintf(b, sizeof b, "%-12.12s %7s  %s", f.name.c_str(), fileSizeText(f.size).c_str(), desc.c_str());
        rows.emplace_back(b);
    }
    if (rows.empty()) {
        usingList_ = false;
        out_ += listScreenTop(areas_[area_].name, "Name          Size   About");
        out_ += std::string("   ") + theme::TEXT + "(nothing here yet)\r\n";
        listPrompt();
        return;
    }
    if (at < 0) at = 0;
    if (at >= (int)rows.size()) at = (int)rows.size() - 1;
    list_.setItems(std::move(rows), (size_t)at);
    usingList_ = true;
    std::snprintf(b, sizeof b, "Name          Size   About                         page %d/%d", page_ + 1, pages);
    out_ += listScreenTop(areas_[area_].name, b);
    out_ += list_.draw();
    size_t ix = (size_t)page_ * kPerPage + (size_t)list_.current();
    std::string about = ix < files_.size() ? files_[ix].name + "  " + fileSizeText(files_[ix].size) + "  " + files_[ix].desc : "";
    out_ += listScreenStatus(about);
    out_ += listScreenKeys(theme::key("N", "ext  ") + theme::key("P", "rev  ") + theme::key("V", "iew  ") + theme::key("I", "nfo  ") +
                           theme::key("D", "ownload  ") + theme::key("U", "pload  ") + theme::key("Q", "uit") + "   Enter selects");
}

void FileBrowser::selectFile(size_t index) {
    if (index >= files_.size()) return;
    const FileEntry& f = files_[index];
    if (isTextFile(f.name)) {
        if (std::string why = gate_ ? gate_(0) : std::string(); !why.empty()) {
            out_ += AMBER;
            out_ += "   " + why + "\r\n";
            listPrompt();
            return;
        }
        view(f.name);
        return;
    }
    if (std::string why = gate_ ? gate_(f.size) : std::string(); !why.empty()) {
        out_ += AMBER;
        out_ += "   " + why + "\r\n";
        listPrompt();
        return;
    }
    req_ = TransferRequest();
    req_.area = areas_[area_].id;
    req_.name = f.name;
    usingList_ = false;
    askProtocol();
}

bool FileBrowser::listCommand(char c) {
    switch (c | 0x20) {
        case 'n':
            page_++;
            showPage();
            return true;
        case 'p':
            page_--;
            showPage();
            return true;
        case 'd':
            out_ += "d\r\n   ";
            out_ += DIM;
            out_ += "File name: ";
            out_ += HI;
            line_.clear();
            req_ = TransferRequest();
            usingList_ = false;
            st_ = St::DownName;
            return true;
        case 'u':
            out_ += "u\r\n   ";
            out_ += DIM;
            out_ += "Describe it in one line (Enter to cancel): ";
            out_ += HI;
            line_.clear();
            req_ = TransferRequest();
            req_.upload = true;
            usingList_ = false;
            st_ = St::UpDesc;
            return true;
        case 'v':
            out_ += "v\r\n   ";
            out_ += DIM;
            out_ += "File name: ";
            out_ += HI;
            line_.clear();
            usingList_ = false;
            st_ = St::ViewName;
            return true;
        case 'i':
            out_ += "i\r\n   ";
            out_ += DIM;
            out_ += "File name: ";
            out_ += HI;
            line_.clear();
            usingList_ = false;
            st_ = St::InfoName;
            return true;
        case 'q':
            showAreas();
            return true;
        default:
            return false;
    }
}

bool FileBrowser::tick(uint32_t now, uint32_t lastInput) {
    if (!bars_ || !usingList_ || !list_.midKey() || now - lastInput <= 300) return true;
    list_.keyTimeout();
    if (!list_.escaped()) return true;
    if (st_ == St::Areas && !typedAreas_) return false;
    if (st_ == St::List) showAreas();
    return true;
}

std::string FileBrowser::where() const {
    return st_ == St::Areas || area_ >= areas_.size() ? "area list" : areas_[area_].name;
}

void FileBrowser::search(const std::string& what) {
    auto lower = [](std::string s) {
        for (char& ch : s) ch = (char)std::tolower((unsigned char)ch);
        return s;
    };
    std::string want = lower(what);
    out_ += "\r\n\r\n   " + theme::fade("SEARCH") + DIM + "  \"" + what + "\"\r\n";
    int  count = 0;
    char b[160];
    for (const FileArea& a : areas_) {
        bool header = false;
        for (const FileEntry& f : lib_.list(a.id)) {
            if (lower(f.name).find(want) == std::string::npos && lower(f.desc).find(want) == std::string::npos) continue;
            if (!header) {
                out_ += std::string("\r\n   ") + theme::ORANGE + a.name + "\r\n";
                header = true;
            }
            if (++count > 60) continue;
            std::snprintf(b, sizeof b, "   %s%-14.14s %s%6s  %s%.52s\r\n", HI, f.name.c_str(), DIM, fileSizeText(f.size).c_str(), DIM, f.desc.c_str());
            out_ += b;
        }
    }
    out_ += std::string("\r\n   ") + theme::TEXT + std::to_string(count) + " found" + (count > 60 ? " (the first 60 listed)" : "") + "\r\n";
    out_ += DIM;
    out_ += "\r\n   Area number (Enter for the main menu): ";
}

void FileBrowser::showNewFiles() {
    out_ += "\r\n\r\n   " + theme::fade("NEW FILES SINCE YOUR LAST CALL") + "\r\n";
    int  count = 0;
    char b[160];
    for (const FileArea& a : areas_) {
        bool header = false;
        for (const FileEntry& f : lib_.list(a.id)) {
            if (f.date <= since_) continue;
            if (!header) {
                out_ += std::string("\r\n   ") + theme::ORANGE + a.name + "\r\n";
                header = true;
            }
            if (++count > 60) continue;  // a flood: the first 60, then the count
            std::snprintf(b, sizeof b, "   %s%-14.14s %s%6s  %s%.52s\r\n", HI, f.name.c_str(), DIM, fileSizeText(f.size).c_str(), DIM, f.desc.c_str());
            out_ += b;
        }
    }
    out_ += std::string("\r\n   ") + theme::TEXT + (count ? std::to_string(count) + (count == 1 ? " new file" : " new files") : std::string("Nothing new since your last call.")) + "\r\n";
    out_ += DIM;
    out_ += "\r\n   Area number (Enter for the main menu): ";
}

void FileBrowser::listPrompt() {
    out_ += "\r\n   ";
    out_ += theme::key("N", "ext  ") + theme::key("P", "rev  ") + theme::key("V", "iew  ") + theme::key("I", "nfo  ") +
            theme::key("D", "ownload  ") + theme::key("U", "pload  ") + theme::key("Q", "uit");
    out_ += theme::WHITE;
    out_ += ": ";
}

void FileBrowser::info(const std::string& name) {
    usingList_ = false;
    const FileEntry* f = nullptr;
    for (const FileEntry& e : files_)
        if (sameName(e.name, name)) f = &e;
    if (!f) {
        out_ += AMBER;
        out_ += "   No file by that name in this area.\r\n";
        st_ = St::List;
        listPrompt();
        return;
    }
    std::vector<std::pair<std::string, std::string>> rows = {
        {"file", f->name}, {"size", fileSizeText(f->size) + "  (" + std::to_string(f->size) + " bytes)"}, {"area", areas_[area_].name}};
    if (!f->desc.empty()) rows.push_back({"about", f->desc.substr(0, 60)});
    std::unique_ptr<RandomFile> file = lib_.open(areas_[area_].id, f->name);
    ArchiveInfo                 arc;
    if (file) arc = listArchive(f->name, *file);
    if (!arc.kind.empty()) rows.push_back({"archive", arc.kind + ", " + std::to_string(arc.entries.size()) + " files"});
    out_ += "\r\n";
    out_ += theme::window("FILE INFO", rows);
    if (!arc.entries.empty()) {  // the archive's own directory: every entry, nothing unpacked
        uint64_t total = 0;
        char     b[160];
        out_ += std::string("\r\n   ") + theme::DGREY + "name                     size    packed  method    crc\r\n";
        size_t shown = 0;
        for (const ArchiveEntry& e : arc.entries) {
            total += e.size;
            if (shown++ >= 40) continue;  // a long pack: the first 40, then the count
            std::string n = e.name.size() > 22 ? "~" + e.name.substr(e.name.size() - 21) : e.name;  // the end of a long path
            std::snprintf(b, sizeof b, "   %s%-22.22s %s%7lu %9lu  %-8.8s  %08lx\r\n", theme::WHITE, n.c_str(), theme::TEXT,
                          (unsigned long)e.size, (unsigned long)e.packed, e.method.c_str(), (unsigned long)e.crc);
            out_ += b;
        }
        std::snprintf(b, sizeof b, "   %s%u files, %lu bytes unpacked%s\r\n", theme::ORANGE, (unsigned)arc.entries.size(),
                      (unsigned long)total, shown > 40 ? " (first 40 listed)" : "");
        out_ += b;
        if (arc.kind != "ZIP" && arc.kind != "TAR")
            out_ += std::string("   ") + theme::DGREY + "(listed only: this board unpacks ZIP; the rest download as is)\r\n";
    }
    if (arc.kind == "ZIP") {
        RandomFile* raw = file.get();
        ZipReader   zipr([raw](uint32_t off, uint8_t* buf, size_t n) { return raw->readAt(off, buf, n); }, raw->size());
        ZipReader*  zip = zipr.open() ? &zipr : nullptr;
        for (const ZipEntry& e : zip ? zip->entries() : std::vector<ZipEntry>{}) {  // FILE_ID.DIZ, when it's stored as is (no workspace here to unpack)
            std::string n = e.name;
            for (char& ch : n) ch = (char)std::toupper((unsigned char)ch);
            if (n != "FILE_ID.DIZ" || e.method != 0 || e.usize > 2048) continue;
            std::string diz;
            uint8_t     scratch[1];
            zip->extract(e, scratch, 0, [&](const uint8_t* p, size_t k) {
                diz.append((const char*)p, k);
                return true;
            });
            out_ += std::string("\r\n   ") + theme::DGREY + "FILE_ID.DIZ" + theme::TEXT + "\r\n";
            for (char ch : diz) out_ += ch == '\n' ? std::string("\r\n") : ch == '\r' ? std::string() : std::string(1, ch);
            out_ += "\r\n";
        }
    }
    st_ = St::List;
    listPrompt();
}

void FileBrowser::view(const std::string& name) {
    usingList_ = false;
    const FileEntry* f = nullptr;
    for (const FileEntry& e : files_)
        if (sameName(e.name, name)) f = &e;
    if (!f) {
        out_ += AMBER;
        out_ += "   No file by that name in this area.\r\n";
        st_ = St::List;
        listPrompt();
        return;
    }
    if (!isTextFile(f->name)) {
        out_ += AMBER;
        out_ += "   " + f->name + " isn't a text file: [D]ownload it instead.\r\n";
        st_ = St::List;
        listPrompt();
        return;
    }
    std::string body;
    if (!lib_.read(areas_[area_].id, f->name, body, kMaxView)) {
        out_ += AMBER;
        out_ += "   That file couldn't be read.\r\n";
        st_ = St::List;
        listPrompt();
        return;
    }
    text_.clear();
    size_t pos = 0;
    while (pos < body.size()) {  // split on LF; a CR before it goes, so every line is sent with CR LF
        size_t end = body.find('\n', pos);
        if (end == std::string::npos) end = body.size();
        std::string l = body.substr(pos, end - pos);
        if (!l.empty() && l.back() == '\r') l.pop_back();
        text_.push_back(l);
        pos = end + 1;
    }
    shown_ = 0;
    st_ = St::Viewing;
    out_ += CLS;
    out_ += DIM;
    showMoreText();
}

void FileBrowser::showMoreText() {
    for (int i = 0; i < kViewLines && shown_ < text_.size(); i++) out_ += text_[shown_++] + "\r\n";
    if (shown_ < text_.size()) {
        out_ += "\x1b[0m";
        out_ += CYAN;
        out_ += "-- more (Enter, Q to stop) --";
        out_ += DIM;
        return;
    }
    out_ += "\x1b[0m";
    out_ += CYAN;
    out_ += "-- end of file (Enter) --";
    out_ += DIM;
    shown_ = text_.size() + 1;  // the next key goes back to the list
}

void FileBrowser::askProtocol() {
    st_ = St::Proto;
    out_ += "\r\n   ";
    out_ += theme::key("Z", "MODEM  ") + theme::key("X", "MODEM  ") + theme::key("K", " XMODEM-1K  ") + theme::key("Q", "uit");
    out_ += theme::WHITE;
    out_ += ": ";
}

void FileBrowser::startTransfer() {
    const char* proto = req_.proto == 'Z' ? "ZMODEM" : req_.proto == 'K' ? "XMODEM-1K" : "XMODEM";
    out_ += theme::CLS;
    out_ += "\r\n";
    if (req_.upload) {
        out_ += theme::window("UPLOAD", {{"area", areas_[area_].name}, {"file", req_.name.empty() ? "(your program names it)" : req_.name},
                                         {"about", req_.desc}, {"protocol", proto}});
        out_ += "\r\n  " + theme::fade("Start the upload in your terminal now.") + "  " + theme::key("Ctrl-X", " x5 cancels") + "\r\n";
    } else {
        uint32_t size = 0;
        for (auto& f : files_)
            if (f.name == req_.name) size = f.size;
        char est[48];
        std::snprintf(est, sizeof est, "about %u s at 24 KB/s", (unsigned)(size / 24576 + 1));
        out_ += theme::window("DOWNLOAD", {{"file", req_.name}, {"size", fileSizeText(size) + "  (" + std::to_string(size) + " bytes)"},
                                           {"protocol", proto}, {"time", est}});
        out_ += "\r\n  " + theme::fade("Start your terminal's download now.") + "  " + theme::key("Ctrl-X", " x5 cancels") + "\r\n";
    }
    out_ += theme::RESET;
    st_ = St::Transfer;
}

void FileBrowser::transferDone(bool ok, uint32_t bytes, uint32_t ms, const std::string& note, uint32_t) {
    char speed[48];
    std::snprintf(speed, sizeof speed, "%u.%u s, %u bytes/s", (unsigned)(ms / 1000), (unsigned)(ms % 1000 / 100),
                  (unsigned)(ms ? (uint64_t)bytes * 1000 / ms : 0));
    std::vector<std::pair<std::string, std::string>> rows = {{"file", req_.upload ? (note.empty() ? req_.name : req_.name) : req_.name},
                                                             {"moved", fileSizeText(bytes) + "  (" + std::to_string(bytes) + " bytes)"},
                                                             {"time", speed}};
    if (!ok) rows.push_back({"why", note.empty() ? "stopped" : note});
    out_ += "\r\n\r\n";
    out_ += theme::window(ok ? (req_.upload ? "UPLOAD COMPLETE" : "DOWNLOAD COMPLETE") : "TRANSFER FAILED", rows);
    files_ = lib_.list(areas_[area_].id);  // an upload is in the list now
    if (bars_) showPage();
    else {
        st_ = St::List;
        listPrompt();
    }
}

bool FileBrowser::key(char c) {
    switch (st_) {
        case St::Areas:
            if (typedAreas_ || !bars_ || !usingList_) {
                if (line_.empty() && (c | 0x20) == 'n') {
                    showNewFiles();
                    typedAreas_ = true;
                    usingList_ = false;
                    return true;
                }
                if (line_.empty() && c == '/') {
                    st_ = St::Search;
                    usingList_ = false;
                    out_ += std::string("\r\n   ") + DIM + "Search every area for: " + HI;
                    return true;
                }
                if (!lineKey(c, 3)) return true;
                typedAreas_ = false;
                if (line_.empty()) return false;  // Enter alone: back to the main menu
                int n = std::atoi(line_.c_str());
                line_.clear();
                if (n >= 1 && (size_t)n <= areas_.size()) openArea((size_t)n - 1);
                else showAreas();
                return true;
            }
            if (!list_.midKey() && (c | 0x20) == 'n') {
                showNewFiles();
                typedAreas_ = true;
                usingList_ = false;
                return true;
            }
            if (!list_.midKey() && c == '/') {
                st_ = St::Search;
                usingList_ = false;
                out_ += std::string("\r\n   ") + DIM + "Search every area for: " + HI;
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
        case St::Search:
            if (!lineKey(c, 30)) return true;
            {
                std::string what = line_;
                line_.clear();
                st_ = St::Areas;
                if (what.empty()) showAreas();
                else {
                    search(what);
                    typedAreas_ = true;
                    usingList_ = false;
                }
            }
            return true;
        case St::List:
            if (bars_ && usingList_) {
                if (!list_.midKey() && listCommand(c)) return true;
                int before = list_.current();
                int shown = (int)list_.size();
                int pick = list_.key(c);
                out_ += list_.takeRedraw();
                {
                    size_t ix = (size_t)page_ * kPerPage + (size_t)list_.current();
                    if (ix < files_.size())
                        out_ += listScreenStatus(files_[ix].name + "  " + fileSizeText(files_[ix].size) + "  " + files_[ix].desc);
                }
                if (list_.escaped()) {
                    showAreas();
                    return true;
                }
                if (pick >= 0) {
                    selectFile((size_t)page_ * kPerPage + (size_t)pick);
                    return true;
                }
                int pages = ((int)files_.size() + kPerPage - 1) / kPerPage;
                if (list_.clamped() > 0 && shown > 0 && before == shown - 1 && page_ + 1 < pages) {
                    page_++;
                    showPage();
                } else if (list_.clamped() < 0 && before == 0 && page_ > 0) {
                    page_--;
                    showPage(kPerPage);
                }
                return true;
            }
            switch (c | 0x20) {
                case 'n':
                    page_++;
                    showPage();
                    break;
                case 'p':
                    page_--;
                    showPage();
                    break;
                case 'd':
                    out_ += "d\r\n   ";
                    out_ += DIM;
                    out_ += "File name: ";
                    out_ += HI;
                    line_.clear();
                    req_ = TransferRequest();
                    st_ = St::DownName;
                    break;
                case 'u':
                    out_ += "u\r\n   ";
                    out_ += DIM;
                    out_ += "Describe it in one line (Enter to cancel): ";
                    out_ += HI;
                    line_.clear();
                    req_ = TransferRequest();
                    req_.upload = true;
                    st_ = St::UpDesc;
                    break;
                case 'v':
                    out_ += "v\r\n   ";
                    out_ += DIM;
                    out_ += "File name: ";
                    out_ += HI;
                    line_.clear();
                    st_ = St::ViewName;
                    break;
                case 'i':
                    out_ += "i\r\n   ";
                    out_ += DIM;
                    out_ += "File name: ";
                    out_ += HI;
                    line_.clear();
                    st_ = St::InfoName;
                    break;
                case 'q':
                    showAreas();
                    break;
                default:
                    break;
            }
            return true;
        case St::InfoName:
            if (!lineKey(c, 40)) return true;
            if (line_.empty()) {
                st_ = St::List;
                listPrompt();
            } else {
                std::string name = line_;
                line_.clear();
                info(name);
            }
            return true;
        case St::ViewName:
            if (!lineKey(c, 40)) return true;
            if (line_.empty()) {
                st_ = St::List;
                listPrompt();
            } else {
                std::string name = line_;
                line_.clear();
                if (std::string why = gate_ ? gate_(0) : std::string(); !why.empty()) {
                    out_ += AMBER;
                    out_ += "   " + why + "\r\n";
                    st_ = St::List;
                    listPrompt();
                    return true;
                }
                view(name);
            }
            return true;
        case St::DownName: {
            if (!lineKey(c, 40)) return true;
            std::string want = line_;
            line_.clear();
            const FileEntry* f = nullptr;
            for (const FileEntry& e : files_)
                if (sameName(e.name, want)) f = &e;
            if (!f) {
                if (!want.empty()) {
                    out_ += AMBER;
                    out_ += "   No file by that name in this area.\r\n";
                }
                st_ = St::List;
                listPrompt();
                return true;
            }
            if (std::string why = gate_ ? gate_(f->size) : std::string(); !why.empty()) {
                out_ += AMBER;
                out_ += "   " + why + "\r\n";
                st_ = St::List;
                listPrompt();
                return true;
            }
            req_.area = areas_[area_].id;
            req_.name = f->name;
            askProtocol();
            return true;
        }
        case St::UpDesc:
            if (!lineKey(c, 60)) return true;
            if (line_.empty()) {
                st_ = St::List;
                listPrompt();
                return true;
            }
            req_.area = areas_[area_].id;
            req_.desc = line_;
            line_.clear();
            askProtocol();
            return true;
        case St::Proto: {
            char p = (char)(c | 0x20);
            if (p == 'q' || c == '\r') {
                out_ += "\r\n";
                st_ = St::List;
                listPrompt();
            } else if (p == 'z' || p == 'x' || p == 'k') {
                out_ += (char)(p - 32);
                req_.proto = (char)(p - 32);
                if (req_.upload && req_.proto != 'Z') {  // XMODEM sends no name: ask for one
                    out_ += "\r\n   ";
                    out_ += DIM;
                    out_ += "Name to save it as: ";
                    out_ += HI;
                    line_.clear();
                    st_ = St::UpName;
                } else {
                    startTransfer();
                }
            }
            return true;
        }
        case St::UpName:
            if (!lineKey(c, 40)) return true;
            if (line_.empty()) {
                st_ = St::List;
                listPrompt();
                return true;
            }
            req_.name = line_;
            line_.clear();
            startTransfer();
            return true;
        case St::Transfer:
            return true;  // the host owns the line until transferDone
        case St::Viewing:
            if ((c | 0x20) == 'q' || shown_ > text_.size()) {
                st_ = St::List;
                showPage();
            } else if (c == '\r' || c == ' ') {
                out_ += "\r\x1b[K";
                showMoreText();
            }
            return true;
    }
    return true;
}

}  // namespace axis::bbs
