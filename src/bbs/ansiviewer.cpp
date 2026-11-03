// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include "bbs/ansiviewer.h"

#include <cctype>
#include <cstdio>

#include "bbs/theme.h"

namespace axis::bbs {

namespace {
const char* const CLS = "\x1b[0m\x1b[2J\x1b[H";
const char* const DIM = "\x1b[0;37m";
const char* const HI = "\x1b[1;37m";
const char* const CYAN = "\x1b[0;33m";
const char* const YEL = "\x1b[1;37m";
const char* const MAG = "\x1b[1;30m";
const char* const AMBER = "\x1b[0;33m";

std::string ext(const std::string& n) {
    size_t dot = n.rfind('.');
    std::string e = dot == std::string::npos ? "" : n.substr(dot + 1);
    for (char& c : e) c = (char)std::toupper((unsigned char)c);
    return e;
}
std::string baseName(const std::string& path) {
    size_t s = path.find_last_of("/\\");
    return s == std::string::npos ? path : path.substr(s + 1);
}
}  // namespace

bool isAnsiPiece(const std::string& name) {
    std::string e = ext(name);
    for (const char* t : {"ANS", "ICE", "CIA", "DRK", "LGO", "ASC", "NFO", "DIZ", "TXT", "MEM"})
        if (e == t) return true;
    return false;
}

AnsiViewer::AnsiViewer(FileLibrary& lib, std::string areaId, std::string areaName, uint8_t* workspace, size_t workspaceSize,
                       door::ByteStream& line)
    : lib_(lib), area_(std::move(areaId)), areaName_(std::move(areaName)), ws_(workspace), wsSize_(workspaceSize), line_(line) {}

void AnsiViewer::begin() { showPacks(); }

bool AnsiViewer::number(char c, int& n) {
    if (c == '\r') {
        send("\r\n");
        n = typed_.empty() ? 0 : std::atoi(typed_.c_str());
        typed_.clear();
        return true;
    }
    if ((c == '\b' || c == 0x7f) && !typed_.empty()) {
        typed_.pop_back();
        send("\b \b");
    } else if (c >= '0' && c <= '9' && typed_.size() < 4) {
        typed_ += c;
        send(std::string(1, c));
    }
    return false;
}

void AnsiViewer::showPacks() {
    st_ = St::Packs;
    typed_.clear();
    zip_.reset();
    file_.reset();
    packs_.clear();
    for (auto& f : lib_.list(area_))
        if (ext(f.name) == "ZIP") packs_.push_back(f);
    std::string s = CLS;
    s += "\r\n   ";
    s += theme::fade("A N S I   P A C K   V I E W E R");
    s += DIM;
    s += "   artpacks from the " + areaName_ + " file area\r\n";
    s += MAG;
    s += "   " + std::string(70, '\xc4') + "\r\n";
    if (packs_.empty()) s += std::string(AMBER) + "   No packs in the area yet.\r\n";
    for (size_t i = 0; i < packs_.size(); i++) {
        char b[160];
        std::string d = packs_[i].desc.size() > 44 ? packs_[i].desc.substr(0, 44) : packs_[i].desc;
        std::snprintf(b, sizeof b, "   %s[%2u]%s %-13s%s%7s  %s\r\n", CYAN, (unsigned)(i + 1), HI, packs_[i].name.c_str(), DIM,
                      fileSizeText(packs_[i].size).c_str(), d.c_str());
        s += b;
    }
    s += DIM;
    s += "\r\n   Pack number (Enter or Q to leave): ";
    s += HI;
    send(s);
}

bool AnsiViewer::openPack(size_t i) {
    pack_ = i;
    file_ = lib_.open(area_, packs_[i].name);
    if (!file_) return false;
    RandomFile* f = file_.get();
    zip_.reset(new ZipReader([f](uint32_t off, uint8_t* buf, size_t n) { return f->readAt(off, buf, n); }, f->size()));
    if (!zip_->open()) return false;
    pieces_.clear();
    for (size_t k = 0; k < zip_->entries().size(); k++)
        if (isAnsiPiece(zip_->entries()[k].name)) pieces_.push_back(k);
    page_ = 0;
    showPieces();
    return true;
}

void AnsiViewer::showPieces() {
    st_ = St::Pieces;
    typed_.clear();
    int pages = ((int)pieces_.size() + kPerPage - 1) / kPerPage;
    if (pages < 1) pages = 1;
    page_ = (page_ % pages + pages) % pages;
    char b[160];
    std::string s = CLS;
    std::snprintf(b, sizeof b, "\r\n   %s%s%s   %u pieces, page %d of %d\r\n", YEL, packs_[pack_].name.c_str(), DIM,
                  (unsigned)pieces_.size(), page_ + 1, pages);
    s += b;
    s += MAG;
    s += "   " + std::string(70, '\xc4') + "\r\n";
    int first = page_ * kPerPage, rows = 18;
    for (int r = 0; r < rows; r++) {
        std::string row = "   ";
        for (int c = 0; c < 3; c++) {
            int i = first + c * rows + r;
            if (i >= (int)pieces_.size()) continue;
            std::string n = baseName(zip_->entries()[pieces_[i]].name).substr(0, 16);
            std::snprintf(b, sizeof b, "%s[%3d]%s %-17s", CYAN, i + 1, HI, n.c_str());
            row += b;
        }
        if (row.size() > 3) s += row + "\r\n";
    }
    s += DIM;
    s += "\r\n   Piece number, ";
    s += CYAN;
    s += "[N]";
    s += DIM;
    s += "ext page, ";
    s += CYAN;
    s += "[P]";
    s += DIM;
    s += "rev page, ";
    s += CYAN;
    s += "[Q]";
    s += DIM;
    s += "uit: ";
    s += HI;
    send(s);
}

void AnsiViewer::showPiece(size_t i) {
    piece_ = i;
    st_ = St::Shown;
    const ZipEntry& e = zip_->entries()[pieces_[i]];
    send(CLS);
    bool ended = false;  // past the 1Ah end-of-file mark: the SAUCE record and padding stay hidden
    auto res = zip_->extract(e, ws_, wsSize_, [&](const uint8_t* p, size_t n) {
        size_t k = 0;
        while (k < n && p[k] != 0x1A) k++;
        if (k) line_.write(p, k);
        if (k < n) ended = true;
        return !ended && line_.carrier();
    });
    std::string s = "\x1b[0m\r\n";
    if (res == ZipReader::Result::Unsupported) s += std::string(AMBER) + "   (packed with an old PKZIP method this viewer can't open)\r\n";
    else if (res != ZipReader::Result::Ok && res != ZipReader::Result::Stopped) s += std::string(AMBER) + "   (this piece is damaged)\r\n";
    char b[160];
    std::snprintf(b, sizeof b, "%s %s %s(%u of %u)  %s[Enter]%s next  %s[P]%s rev  %s[L]%s ist  %s[Q]%s uit \x1b[0m", "\x1b[0;30;47m",
                  baseName(e.name).c_str(), "\x1b[0;37m", (unsigned)(i + 1), (unsigned)pieces_.size(), CYAN, DIM, CYAN, DIM, CYAN,
                  DIM, CYAN, DIM);
    s += b;
    send(s);
}

bool AnsiViewer::key(char c) {
    int n = 0;
    switch (st_) {
        case St::Packs:
            if ((c | 0x20) == 'q') return false;
            if (!number(c, n)) return true;
            if (n == 0) return false;
            if (n > (int)packs_.size() || !openPack((size_t)n - 1)) showPacks();
            return true;
        case St::Pieces:
            if (typed_.empty() && (c | 0x20) == 'q') {
                showPacks();
                return true;
            }
            if (typed_.empty() && (c | 0x20) == 'n') {
                page_++;
                showPieces();
                return true;
            }
            if (typed_.empty() && (c | 0x20) == 'p') {
                page_--;
                showPieces();
                return true;
            }
            if (!number(c, n)) return true;
            if (n >= 1 && n <= (int)pieces_.size()) showPiece((size_t)n - 1);
            else showPieces();
            return true;
        case St::Shown:
            switch (c | 0x20) {
                case '\r' | 0x20:
                case ' ':
                case 'n':
                    if (piece_ + 1 < pieces_.size()) showPiece(piece_ + 1);
                    else showPieces();
                    break;
                case 'p':
                    showPiece(piece_ > 0 ? piece_ - 1 : 0);
                    break;
                case 'l':
                    page_ = (int)piece_ / kPerPage;
                    showPieces();
                    break;
                case 'q':
                    showPacks();
                    break;
                default:
                    break;
            }
            return true;
    }
    return true;
}

bool AnsiViewer::step() {
    if (!line_.carrier()) return false;
    while (line_.available() > 0) {
        int c = line_.read();
        if (c < 0) break;
        if (c == '\n' || c == 0) continue;
        if (!key((char)c)) return false;
        if (!line_.carrier()) return false;
    }
    return true;
}

}  // namespace axis::bbs
