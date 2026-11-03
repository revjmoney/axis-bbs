// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include "bbs/filebrowser.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>

namespace axis::bbs {

namespace {
const char* const CLS = "\x1b[0m\x1b[2J\x1b[H";
const char* const DIM = "\x1b[0;37m";
const char* const HI = "\x1b[1;37m";
const char* const CYAN = "\x1b[1;36m";
const char* const YEL = "\x1b[1;33m";
const char* const MAG = "\x1b[0;35m";
const char* const GRN = "\x1b[0;32m";
const char* const AMBER = "\x1b[0;33m";

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
    out_ += CLS;
    out_ += "\r\n   ";
    out_ += YEL;
    out_ += "FILE AREAS";
    out_ += MAG;
    out_ += "  \xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\r\n";
    for (size_t i = 0; i < areas_.size(); i++) {
        size_t n = lib_.list(areas_[i].id).size();
        char   b[128];
        std::snprintf(b, sizeof b, "   %s[%2u]%s %-34s%s%4u file%s\r\n", CYAN, (unsigned)(i + 1), HI, areas_[i].name.c_str(), DIM,
                      (unsigned)n, n == 1 ? "" : "s");
        out_ += b;
    }
    out_ += DIM;
    out_ += "\r\n   Area number (Enter for the main menu): ";
    out_ += HI;
}

void FileBrowser::openArea(size_t i) {
    area_ = i;
    page_ = 0;
    files_ = lib_.list(areas_[i].id);
    st_ = St::List;
    showPage();
}

void FileBrowser::showPage() {
    int pages = ((int)files_.size() + kPerPage - 1) / kPerPage;
    if (pages == 0) pages = 1;
    if (page_ >= pages) page_ = pages - 1;
    if (page_ < 0) page_ = 0;
    out_ += CLS;
    char b[160];
    std::snprintf(b, sizeof b, "\r\n   %s%s%s   page %d of %d, %u files\r\n", YEL, areas_[area_].name.c_str(), DIM, page_ + 1, pages,
                  (unsigned)files_.size());
    out_ += b;
    out_ += MAG;
    out_ += "   \xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\r\n";
    if (files_.empty()) {
        out_ += DIM;
        out_ += "   (nothing here yet)\r\n";
    }
    for (size_t i = (size_t)page_ * kPerPage; i < files_.size() && i < (size_t)(page_ + 1) * kPerPage; i++) {
        const FileEntry& f = files_[i];
        std::string      desc = f.desc.size() > 50 ? f.desc.substr(0, 50) : f.desc;
        std::snprintf(b, sizeof b, "   %s%-13s%s%7s  %s%s\r\n", HI, f.name.substr(0, 13).c_str(), GRN, fileSizeText(f.size).c_str(), DIM,
                      desc.c_str());
        out_ += b;
    }
    listPrompt();
}

void FileBrowser::listPrompt() {
    out_ += "\r\n   ";
    for (const char* item : {"[N]", "ext", "[P]", "rev", "[V]", "iew a text file", "[Q]", "uit"}) {
        out_ += item[0] == '[' ? CYAN : DIM;
        out_ += item;
        if (item[0] != '[') out_ += "  ";
    }
    out_ += HI;
    out_ += ": ";
}

void FileBrowser::view(const std::string& name) {
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
        out_ += "   " + f->name + " isn't a text file. ZMODEM downloads are coming in the next build.\r\n";
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

bool FileBrowser::key(char c) {
    switch (st_) {
        case St::Areas:
            if (!lineKey(c, 3)) return true;
            if (line_.empty()) return false;  // Enter alone: back to the main menu
            {
                int n = std::atoi(line_.c_str());
                line_.clear();
                if (n >= 1 && (size_t)n <= areas_.size()) openArea((size_t)n - 1);
                else showAreas();
            }
            return true;
        case St::List:
            switch (c | 0x20) {
                case 'n':
                    page_++;
                    showPage();
                    break;
                case 'p':
                    page_--;
                    showPage();
                    break;
                case 'v':
                    out_ += "v\r\n   ";
                    out_ += DIM;
                    out_ += "File name: ";
                    out_ += HI;
                    line_.clear();
                    st_ = St::ViewName;
                    break;
                case 'q':
                    showAreas();
                    break;
                default:
                    break;
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
                view(name);
            }
            return true;
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
