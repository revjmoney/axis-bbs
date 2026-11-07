// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#pragma GCC optimize("Os")  // menus and screens: size over speed (the door CPU and transfers stay -O2)
#include "bbs/ansiviewer.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <map>

#include "bbs/theme.h"

namespace axis::bbs {

namespace {
const char* const CLS = "\x1b[0m\x1b[2J\x1b[H";
const char* const DIM = "\x1b[0;37m";
const char* const HI = "\x1b[1;37m";
#define CYAN theme::ORANGE  // the theme's accent colour
const char* const YEL = "\x1b[1;37m";
const char* const MAG = "\x1b[1;30m";
#define AMBER theme::ORANGE

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

AnsiViewer::~AnsiViewer() { releaseInflate(); }

bool AnsiViewer::inflateReady() const {
    if (state_ && wsSize_ >= 32768) return true;
    return wsSize_ >= ZipReader::kWorkspace;
}

bool AnsiViewer::grabInflate() {
    if (inflateReady()) return true;
    if (!ws_) {
        ws_ = (uint8_t*)std::malloc(32768);
        if (!ws_) return false;
        ownWs_ = true;
        wsSize_ = 32768;
    }
    if (!state_) {
        state_ = (uint8_t*)std::malloc(ZipReader::kStateBytes);
        if (!state_) {
            if (ownWs_) {
                std::free(ws_);
                ws_ = nullptr;
                ownWs_ = false;
                wsSize_ = 0;
            }
            return false;
        }
        ownState_ = true;
    }
    return inflateReady();
}

void AnsiViewer::releaseInflate() {
    stream_.reset();
    if (ownState_) std::free(state_);
    if (ownWs_) std::free(ws_);
    if (ownState_) state_ = nullptr;
    if (ownWs_) {
        ws_ = nullptr;
        wsSize_ = 0;
    }
    ownState_ = false;
    ownWs_ = false;
}

void AnsiViewer::begin() {
    if (areas_.size() > 1) showAreas();
    else showPacks();
}

void AnsiViewer::showList(const std::string& title, const std::string& help) {
    std::string area = area_;
    for (char& c : area) c = (char)std::toupper((unsigned char)c);
    const char* banner = areas_.size() <= 1 && area == "ANSI" ? "A N S I   M O V I E S" : "A N S I   P A C K   V I E W E R";
    send(std::string(CLS) + "\r\n   " + theme::fade(banner) + DIM + "   " + title + "\r\n" + MAG + "   " +
         std::string(70, '\xc4') + "\r\n" + list_.draw() + "\x1b[23;4H" + DIM + help + HI);
}

void AnsiViewer::showAreas() {  // which file area's packs (each one small enough for the main to list)
    st_ = St::Areas;
    std::vector<std::string> v;
    for (auto& a : areas_) v.push_back(a.second);
    list_.setItems(std::move(v), areaAt_);
    showList("pick a file area", "arrows move  Enter opens  Esc or Q leaves");
}

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
    zip_.reset();
    file_.reset();
    packs_.clear();
    for (auto& f : lib_.list(area_))
        if (ext(f.name) == "ZIP") packs_.push_back(f);
    std::stable_partition(packs_.begin(), packs_.end(), [](const FileEntry& f) {
        std::string low = f.desc;
        for (char& c : low) c = (char)std::tolower((unsigned char)c);
        return low.find("movie") != std::string::npos;
    });
    std::vector<std::string> v;
    for (auto& p : packs_) {
        char        b[160];
        std::string d = fileAbout(p.desc, 48);
        std::snprintf(b, sizeof b, "%-13s %7s  %s", p.name.c_str(), fileSizeText(p.size).c_str(), d.c_str());
        v.push_back(b);
    }
    if (v.empty()) v.push_back("No packs in the area yet.");
    list_.setItems(std::move(v), pack_ < packs_.size() ? pack_ : 0);
    showList("artpacks from the " + areaName_ + " file area", "arrows move  Enter opens  number + Enter  Esc or Q back");
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
    // the name prefix at least half the pieces share, cut at a word boundary: left off in the list
    prefix_.clear();
    std::map<std::string, int> counts;
    for (size_t k : pieces_) {
        std::string n = baseName(zip_->entries()[k].name);
        for (size_t m = 4; m < n.size(); m++)
            if (n[m - 1] == '_' || n[m - 1] == '-' || n[m - 1] == ' ') counts[n.substr(0, m)]++;
    }
    for (auto& c : counts)
        if (c.second * 2 >= (int)pieces_.size() && c.second > 1 && c.first.size() > prefix_.size()) prefix_ = c.first;
    showPieces();
    return true;
}

std::string AnsiViewer::shownName(size_t i) const {
    std::string n = baseName(zip_->entries()[pieces_[i]].name);
    if (!prefix_.empty() && n.size() > prefix_.size() && n.compare(0, prefix_.size(), prefix_) == 0) n = n.substr(prefix_.size());
    return n;
}

int AnsiViewer::columns() const {
    for (size_t i = 0; i < pieces_.size(); i++)
        if (shownName(i).size() > 17) return 2;
    return 3;
}

void AnsiViewer::showPieces() {
    st_ = St::Pieces;
    std::vector<std::string> v;
    for (size_t i = 0; i < pieces_.size(); i++) v.push_back(shownName(i));
    list_.setItems(std::move(v), piece_ < pieces_.size() ? piece_ : 0);
    showList(packs_[pack_].name + ", " + std::to_string(pieces_.size()) + " pieces", "arrows move  PgUp/PgDn  Enter views  Esc or Q back");
}

void AnsiViewer::showPiece(size_t i) {
    piece_ = i;
    const ZipEntry& e = zip_->entries()[pieces_[i]];
    if (e.method == 8 && !grabInflate()) {  // stored pieces need no window; a second movie waits for one
        send(std::string("\r\n\r\n   ") + AMBER +
             "The board is short on memory for another movie right now. Try again in a minute.\r\n" + DIM);
        showPieces();
        return;
    }
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
    releaseInflate();  // the window is only held while a piece is actually playing
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
        case St::Areas:
        case St::Packs:
        case St::Pieces: {
            if ((c | 0x20) == 'q' && !list_.midKey()) return back();
            int pick = list_.key(c);
            send(list_.takeRedraw());
            if (list_.escaped()) return back();
            if (pick < 0) return true;
            if (st_ == St::Areas) {
                areaAt_ = (size_t)pick;
                area_ = areas_[(size_t)pick].first;
                areaName_ = areas_[(size_t)pick].second;
                pack_ = 0;
                showPacks();
            } else if (st_ == St::Packs) {
                if (packs_.empty() || !openPack((size_t)pick)) showPacks();
            } else {
                showPiece((size_t)pick);
            }
            return true;
        }
        case St::Playing:
            if ((c | 0x20) == 'q') {  // stop: back to the list of pieces
                releaseInflate();
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
                    page_ = (int)piece_ / perPage();
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

bool AnsiViewer::back() {
    if (st_ == St::Pieces) {
        showPacks();
        return true;
    }
    if (st_ == St::Packs && areas_.size() > 1) {
        showAreas();
        return true;
    }
    return false;  // from the top list: leave the viewer
}

bool AnsiViewer::idle() {
    if (!list_.midKey() || (st_ != St::Areas && st_ != St::Packs && st_ != St::Pieces)) return true;
    list_.keyTimeout();
    return list_.escaped() ? back() : true;
}

bool AnsiViewer::step() {
    if (!line_.carrier()) return false;
    if (line_.available() <= 0 && ++quiet_ > 60 && !idle()) return false;  // ~300 ms of quiet
    if (line_.available() > 0) quiet_ = 0;
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
