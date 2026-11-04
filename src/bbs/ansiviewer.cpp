// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#pragma GCC optimize("Os")  // menus and screens: size over speed (the door CPU and transfers stay -O2)
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
    const ZipEntry& e = zip_->entries()[pieces_[i]];
    send(CLS);
    stream_.reset(new ZipReader::Stream(*zip_, e, ws_, wsSize_, state_));
    chunk_.assign(4096, '\0');  // the start, to tell a movie from a still piece
    chunk_.resize(stream_->read((uint8_t*)&chunk_[0], chunk_.size()));
    chunkPos_ = 0;
    int moves = 0;  // absolute cursor moves and cursor-ups: an animation redraws in place
    for (size_t k = 0; k + 2 < chunk_.size(); k++)
        if (chunk_[k] == 27 && chunk_[k + 1] == '[') {
            size_t m = k + 2;
            while (m < chunk_.size() && ((chunk_[m] >= '0' && chunk_[m] <= '9') || chunk_[m] == ';')) m++;
            if (m < chunk_.size() && (chunk_[m] == 'H' || chunk_[m] == 'f' || chunk_[m] == 'A')) moves++;
        }
    movie_ = moves >= 10;
    nonstop_ = false;
    waiting_ = false;
    lines_ = 0;
    lastPump_ = now();
    st_ = St::Playing;
    pump();
}

void AnsiViewer::pump() {
    if (st_ != St::Playing || waiting_) return;
    size_t budget = 2048;  // a still piece: a screenful a turn at most
    if (movie_ && clock_) {  // a movie: what the modem would have carried since last time
        uint32_t t = now(), elapsed = t - lastPump_;
        budget = (size_t)((uint64_t)elapsed * (baud_ / 10) / 1000);
        if (budget == 0) return;
        if (budget > 2048) budget = 2048;
        lastPump_ = t;
    }
    std::string out;
    bool        end = false;
    while (out.size() < budget) {
        if (chunkPos_ >= chunk_.size()) {  // the next part out of the ZIP
            chunk_.assign(512, '\0');
            chunk_.resize(stream_->read((uint8_t*)&chunk_[0], chunk_.size()));
            chunkPos_ = 0;
            if (chunk_.empty()) {
                end = true;
                break;
            }
        }
        char c = chunk_[chunkPos_++];
        if (c == 0x1A) {  // the end-of-file mark: the SAUCE record and padding stay hidden
            end = true;
            break;
        }
        out += c;
        if (c == '\n' && !movie_ && !nonstop_ && ++lines_ >= kPageLines) {  // a screenful: wait for the caller
            lines_ = 0;
            waiting_ = true;
            out += std::string("\x1b[0m") + DIM + "-- " + CYAN + "[Enter]" + DIM + " more  " + CYAN + "[N]" + DIM + "onstop  " + CYAN +
                   "[Q]" + DIM + "uit --";
            break;
        }
    }
    if (!out.empty()) send(out);
    if (end) finishPiece(stream_->result());
}

void AnsiViewer::finishPiece(ZipReader::Result res) {
    stream_.reset();
    chunk_.clear();
    st_ = St::Shown;
    const ZipEntry& e = zip_->entries()[pieces_[piece_]];
    size_t          i = piece_;
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
        case St::Playing:
            if ((c | 0x20) == 'q') {  // stop: back to the list of pieces
                stream_.reset();
                chunk_.clear();
                send("\x1b[0m");
                showPieces();
                return true;
            }
            if (movie_) {
                if (c == ' ') {  // pause, play
                    waiting_ = !waiting_;
                    lastPump_ = now();
                } else if (c >= '1' && c <= '4') {  // 2400, 9600, 19200, 57600 baud
                    const uint32_t speeds[4] = {2400, 9600, 19200, 57600};
                    baud_ = speeds[c - '1'];
                }
                return true;
            }
            if (waiting_ && (c == '\r' || c == ' ' || (c | 0x20) == 'n')) {  // more: the prompt goes, the art goes on
                if ((c | 0x20) == 'n') nonstop_ = true;
                waiting_ = false;
                send("\r\x1b[K");
                pump();
            }
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
    pump();  // the piece playing goes on
    return true;
}

}  // namespace axis::bbs
