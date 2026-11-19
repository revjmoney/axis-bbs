// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include "app/themeprovider.h"

#include <dirent.h>
#include <sys/stat.h>

#include <algorithm>
#include <cctype>
#include <cstring>

#include "bbs/script.h"
#include "bbs/theme.h"
#include "bbs/wfc.h"

// Size over speed, as main.cpp builds the code that was moved here.
#pragma GCC optimize("Os")

namespace axis::app {

namespace {
bool wfcScreen(const std::string& name) {  // a waiting screen (or its lite one): from the WFC skin's folder
    return name == "WFC.ANS" || name == "WFCCYD.ANS" || name == "WFC.LITE.ANS" || name == "WFCCYD.LITE.ANS";
}
}  // namespace

// The sysop's own screens for the theme (MATRIX.ANS, MAIN.ANS): from the card on the main.
// The Downloads screensets run 6 to 11 KB, so the main keeps 12 KB and cuts at the SAUCE record.
// A board without the card asks the main once. The cluster frame is 8 KB, so a follower can
// lose the bottom of a screen that is bigger than that. The settings arriving forget the cache.
// (readFileAppend: one piece of a file, cut at the first 0x1a: bbs/artfetch.h)
bool ThemeProvider::readThemeFileAppend(const char* folder, const std::string& name, size_t cap, size_t offset, std::string& out) {
    if (!folder || !folder[0]) return false;
    std::string path = themesDir() + folder + "/" + name;
    FILE*       f = fopen(path.c_str(), "rb");
    if (!f) return false;
    const size_t old = out.size();
    axis::bbs::readFileAppend(f, cap, offset, out);
    fclose(f);
    if (out.size() - old > artMax_) artMax_ = out.size() - old;
    return true;
}
std::string ThemeProvider::readThemeFile(const char* folder, const std::string& name, size_t cap, size_t offset, bool* opened) {
    std::string s;
    bool        ok = readThemeFileAppend(folder, name, cap, offset, s);
    if (opened) *opened = ok;
    return s;
}
// One piece of the Saylor poster, from the theme's folder (`want` bytes at most, never more than a cluster frame takes).
// It is never read whole: Node streams it a piece at a time (Node::setPoster). A short read at a later offset means
// the end of the file, not "try the other folder".
bool ThemeProvider::posterPieceLocalAppend(const std::string& name, size_t offset, size_t want, std::string& out) {
    if (!host_.hasSd() || !posterName(name) || offset >= kPosterCap) return false;
    if (want > kPosterChunk) want = kPosterChunk;
    if (want > kPosterCap - offset) want = kPosterCap - offset;
    if (readThemeFileAppend(cfg().theme.c_str(), name, want, offset, out)) return true;
    if (readThemeFileAppend(axis::bbs::theme::legacyThemeFolder(cfg().theme), name, want, offset, out)) return true;
    return readThemeFileAppend("axis", name, want, offset, out);  // a look without its own poster shows the axis one
}
std::string ThemeProvider::posterPieceLocal(const std::string& name, size_t offset, size_t want, bool* opened) {
    std::string s;
    *opened = posterPieceLocalAppend(name, offset, want, s);
    return s;
}
// A follower asks the main for the same piece ("T name offset want"); *exists false when the main has none (or did not answer).
std::string ThemeProvider::posterPiece(const std::string& name, size_t offset, size_t want, bool* exists) {
    *exists = false;
    if (!isFollower()) return posterPieceLocal(name, offset, want, exists);
    std::string reply;
    if (!remote || !remote(std::string("T\x1f") + name + "\x1f" + std::to_string(offset) + "\x1f" + std::to_string(want), reply) ||
        reply.empty() || reply[0] != '1')
        return "";
    *exists = true;
    reply.erase(0, 1);  // (in place: no second copy)
    if (reply.size() > want) reply.resize(want);
    return reply;
}
std::string ThemeProvider::themeArtLocal(const std::string& name) {
    if (!host_.hasSd() || name.find('/') != std::string::npos) return "";
    std::string look, file;
    if (axis::bbs::theme::splitLookFile(name, look, file)) {  // "crt:MAIN.ANS": that look's screen (the sysop's preview)
        std::string s = readThemeFile(look.c_str(), file);
        if (s.empty()) s = readThemeFile(axis::bbs::theme::legacyThemeFolder(look), file);
        return s;
    }
    if (posterName(name)) return "";  // the poster streams in pieces (posterPiece), never read whole
    // The waiting screens come from the WFC skin's folder (the theme's, unless the sysop picked another); the rest from the theme.
    const std::string folder = wfcScreen(name) ? axis::bbs::wfcFolder(cfg()) : cfg().theme;
    std::string       s = readThemeFile(folder.c_str(), name);
    if (s.empty()) s = readThemeFile(axis::bbs::theme::legacyThemeFolder(folder), name);
    return s;
}
// The main's answer to "T name offset": one piece of a theme file, at most kPosterChunk bytes, so the reply stays under the
// cluster's 8 KB frame (a bigger one drops the link). The piece is appended to out (the reply being built: no second copy).
// Returns whether the file is there.
bool ThemeProvider::themeArtPieceAppend(const std::string& name, size_t off, size_t want, std::string& out) {
    if (!host_.hasSd() || name.empty() || name.find('/') != std::string::npos || name.find("..") != std::string::npos) return false;
    if (posterName(name)) return posterPieceLocalAppend(name, off, want, out);
    size_t cap = kThemeArtCap;
    if (off >= cap) return false;
    if (!want || want > kPosterChunk) want = kPosterChunk;
    if (want > cap - off) want = cap - off;
    // the waiting screens come from the WFC skin's folder (the theme's, unless the sysop picked another); the rest from the theme;
    // "crt:MAIN.ANS" from that look's folder (the sysop's preview on a follower)
    std::string folder = wfcScreen(name) ? axis::bbs::wfcFolder(cfg()) : cfg().theme, file = name, look;
    if (axis::bbs::theme::splitLookFile(name, look, file)) folder = look;
    if (readThemeFileAppend(folder.c_str(), file, want, off, out)) return true;
    return readThemeFileAppend(axis::bbs::theme::legacyThemeFolder(folder), file, want, off, out);
}
// A follower pulls a theme file from the main in pieces. *answered: every request was answered (an empty file is a real "none").
std::string ThemeProvider::themeArtRemote(const std::string& name, bool* answered) {
    constexpr size_t kFollowerWant = 4096;  // a follower asks for 4 KB pieces: the main's reply (and this reply) stay small
    return axis::bbs::assembleArt(posterName(name) ? kPosterCap : kThemeArtCap, kFollowerWant, !posterName(name),
                                  [this, &name](size_t off, std::string& reply) {
                                      return remote && remote(std::string("T\x1f") + name + "\x1f" + std::to_string(off) + "\x1f" +
                                                                  std::to_string(kFollowerWant),
                                                              reply);
                                  },
                                  answered);
}
std::string ThemeProvider::themeArt(const std::string& name) {
    if (!isFollower()) return themeArtLocal(name);
    if (posterName(name)) return "";  // the poster streams in pieces (posterPiece), never fetched whole
    bool poster = posterName(name) || name.find(':') != std::string::npos;  // (a preview is seen once: never cached either)
    if (!poster) {
        if (const std::string* hit = artCache_.find(name)) return *hit;
    }
    bool        answered = false;
    std::string art = themeArtRemote(name, &answered);
    if (!poster && answered) artCache_.put(name, art);  // (the poster is one login screen, then it can go; a failed ask is asked again)
    return art;
}
// A theme file's first `rows` lines, read from the card and stopped there (the list screens keep only a header's top rows:
// MSGS.ANS read whole for them was 8 KB held, the slow call's lowest heap on board 1). Elsewhere the whole-file reader.
std::string ThemeProvider::readThemeTop(const char* folder, const std::string& name, int rows) {
    if (!folder || !folder[0]) return "";
    FILE* f = fopen((themesDir() + folder + "/" + name).c_str(), "rb");
    if (!f) return "";
    std::string s;
    axis::bbs::readTopRowsAppend(f, rows, kThemeArtCap, s);
    fclose(f);
    if (s.size() > artMax_) artMax_ = s.size();
    return s;
}
std::string ThemeProvider::themeArtTop(const std::string& name, int rows) {
    // a follower's art comes whole from the main (and its cache); previews, the poster and the waiting screens keep their own reader
    if (isFollower() || name.find(':') != std::string::npos || posterName(name) || wfcScreen(name)) return themeArt(name);
    if (!host_.hasSd() || name.find('/') != std::string::npos) return "";
    std::string s = readThemeTop(cfg().theme.c_str(), name, rows);
    if (s.empty()) s = readThemeTop(axis::bbs::theme::legacyThemeFolder(cfg().theme), name, rows);
    return s;
}
// The cleaned WFC.ANS every WFC screen (telnet, serial, local) draws: read once, shared, and read again only when something may have
// changed. wfcAnsKey says what it was read from: the WFC folder plus (here on a board with the card) the file's size and time in the
// folder and its legacy folder, so a new WFC.ANS from any writer (the sysop's @PUT, the web, a theme install) is seen on the next draw,
// as when it was read every time. On a follower the key is the folder and the settings arrival clears it (with artCache). A follower
// that got no art does not keep the key, so it asks again as before. Null (no art): the built-in screen.
std::shared_ptr<const std::string> ThemeProvider::wfcArtShared() {
    const std::string folder = axis::bbs::wfcFolder(cfg());
    std::string       key = folder;
    if (!isFollower()) {
        const char* dirs[2] = {folder.c_str(), axis::bbs::theme::legacyThemeFolder(folder)};
        for (const char* d : dirs) {
            struct stat st;
            key += '|';
            if (d && d[0] && host_.hasSd() && stat((themesDir() + d + "/WFC.ANS").c_str(), &st) == 0)
                key += std::to_string((unsigned long)st.st_size) + "." + std::to_string((unsigned long)st.st_mtime);
        }
    } else {
        key += "|F";
    }
    if (key == wfcAnsKey_) return wfcAnsShared_;
    std::string art = axis::bbs::cleanWfcArt(themeArt("WFC.ANS"));
    wfcAnsShared_ = art.empty() ? nullptr : std::make_shared<const std::string>(std::move(art));
    if (wfcAnsShared_ || !isFollower()) wfcAnsKey_ = key;
    else wfcAnsKey_.clear();
    return wfcAnsShared_;
}
// ViSiON/3-style menu files (MAIN.MNU, MAIN.ANS...): this board's own /menus first (its SD card or flash),
// then the main's over the cluster. Only the small .MNU files are cached; art is fetched each time it shows.
// A menu screen the theme draws (MAIN.ANS, PDMATRIX.ANS as MATRIX.ANS ...): the theme's file, so switching the theme
// switches the menus too. Null when the theme has none (or the name is a menu file): the menu set's own then.
FILE* ThemeProvider::openThemeScreen(const std::string& name) {
    const char* file = axis::bbs::theme::menuScreenFile(name);
    if (!file || !host_.hasSd() || cfg().theme.empty()) return nullptr;
    FILE* f = fopen((themesDir() + cfg().theme + "/" + file).c_str(), "rb");
    if (!f)
        if (const char* old = axis::bbs::theme::legacyThemeFolder(cfg().theme)) f = fopen((themesDir() + old + "/" + file).c_str(), "rb");
    return f;
}
// theme: a menu screen the theme draws comes from the theme (off for the main's "U" answer to a follower: theme
// screens run past the 8 KB cluster frame, so a follower pulls those through "T" in pieces instead)
std::string ThemeProvider::menuFileLocal(const std::string& name, bool theme) {
    std::string s;
    if (name.empty() || name.find('/') != std::string::npos || name.find("..") != std::string::npos) return s;
    size_t cap = 8192;
    FILE*  f = theme ? openThemeScreen(name) : nullptr;
    if (f) cap = kThemeArtCap;  // (theme screens run to 11 KB)
    else f = fopen((host_.sdRoot() + "/menus/" + name).c_str(), "rb");
    if (f) {
        axis::bbs::readFileAppend(f, cap, 0, s);
        fclose(f);
    }
    if (s.size() > artMax_) artMax_ = s.size();
    return s;
}
std::string ThemeProvider::menuFile(const std::string& name) {
    if (isFollower() && remote)
        if (const char* screen = axis::bbs::theme::menuScreenFile(name)) {  // the theme's screen from the main, in pieces (not cached)
            bool        answered = false;
            std::string art = themeArtRemote(screen, &answered);
            if (!art.empty()) return art;
        }
    std::string s = menuFileLocal(name);
    if (!s.empty() || !isFollower()) return s;
    bool mnu = name.size() > 4 && name.compare(name.size() - 4, 4, ".MNU") == 0;
    if (mnu)
        if (auto it = menuCache_.find(name); it != menuCache_.end()) return it->second;
    std::string reply;
    if (remote && remote(std::string("U\x1f") + name, reply) && !reply.empty() && reply[0] == '1') {
        reply.erase(0, 1);  // (in place: art is several KB, no second copy)
        s = std::move(reply);
    }
    if (mnu) menuCache_[name] = s;
    return s;
}

// Does a menu screen exist and have a body? The same lookup as menuFileLocal (theme screen first, then /menus/name) but only
// fopen + the file's size, no read: showPrelogon probes up to ten names and must not load 16 to 24 KB for each.
bool ThemeProvider::menuExistsLocal(const std::string& name, bool theme) {
    if (name.empty() || name.find('/') != std::string::npos || name.find("..") != std::string::npos) return false;
    FILE* f = theme ? openThemeScreen(name) : nullptr;
    if (!f) f = fopen((host_.sdRoot() + "/menus/" + name).c_str(), "rb");
    if (!f) return false;
    bool has = fseek(f, 0, SEEK_END) == 0 && ftell(f) > 0;
    fclose(f);
    return has;
}
bool ThemeProvider::menuExists(const std::string& name) {
    if (menuExistsLocal(name)) return true;
    if (!isFollower() || !remote) return false;
    if (auto it = existsCache_.find(name); it != existsCache_.end()) return it->second;
    if (lastEMissMs_ && host_.nowMs() - lastEMissMs_ < 10000) return !menuFile(name).empty();  // the main was silent a moment ago: no second wait
    std::string reply;
    bool        asked = remote(std::string("E\x1f") + name, reply);
    if (!asked) lastEMissMs_ = host_.nowMs() | 1;
    if (asked && reply == "1") {
        mainKnowsExists_ = true;
        return existsCache_[name] = true;
    }
    if (asked && reply == "0" && mainKnowsExists_) return existsCache_[name] = false;
    // No answer, or a "0" from a main that may be older (it answers every letter it doesn't know with "0"): read it as before
    bool has = !menuFile(name).empty();
    if (asked) existsCache_[name] = has;  // (an unanswered ask is asked again)
    return has;
}

#if AXIS_SCRIPTS
// /scripts/NAME.axs. 8000 bytes so a follower can carry the file in one 8 KB cluster frame.
static std::string scriptRead(const std::string& path) {
    std::string s;
    if (FILE* f = fopen(path.c_str(), "rb")) {
        char b[512];
        for (size_t n; s.size() < 8000 && (n = fread(b, 1, sizeof b, f)) > 0;) s.append(b, n);
        fclose(f);
    }
    return s;
}
std::vector<std::string> ThemeProvider::scriptNamesLocal() {
    std::vector<std::string> v;
    if (!host_.hasSd()) return v;
    std::string dirp = host_.sdRoot() + "/scripts";
    if (DIR* d = opendir(dirp.c_str())) {
        while (dirent* e = readdir(d)) {
            std::string n = e->d_name;
            if (n.size() <= 4) continue;
            std::string ext = n.substr(n.size() - 4);
            for (char& c : ext) c = (char)std::tolower((unsigned char)c);
            if (ext != ".axs") continue;
            std::string base = n.substr(0, n.size() - 4);
            if (axis::bbs::scriptNameOk(base)) v.push_back(base);
        }
        closedir(d);
    }
    std::sort(v.begin(), v.end());
    if (v.size() > 35) v.resize(35);
    return v;
}
std::string ThemeProvider::scriptFile(const std::string& name) {
    if (!axis::bbs::scriptNameOk(name)) return {};
    if (host_.hasSd()) return scriptRead(host_.sdRoot() + "/scripts/" + name + ".axs");
    if (!isFollower() || !remote) return {};
    std::string reply;
    if (remote(std::string("J\x1f") + name, reply) && !reply.empty() && reply[0] == '1') reply.erase(0, 1);
    else reply.clear();
    return reply;
}
std::vector<std::string> ThemeProvider::scriptNames() {
    if (host_.hasSd() || !isFollower() || !remote) return scriptNamesLocal();
    std::vector<std::string> v;
    std::string              reply;
    if (remote(std::string("J\x1f"), reply) && !reply.empty() && reply[0] == '1') {
        for (size_t pos = 1; pos < reply.size();) {
            size_t e = reply.find('\n', pos);
            if (e == std::string::npos) e = reply.size();
            std::string n = reply.substr(pos, e - pos);
            if (!n.empty() && n.back() == '\r') n.pop_back();
            if (axis::bbs::scriptNameOk(n)) v.push_back(n);
            pos = e + 1;
        }
    }
    return v;
}
#endif

std::string ThemeProvider::listTemplateText(const std::string& name) {
    if (!axis::bbs::theme::menuScreenFile(name)) return "";  // only the names the resolver knows (plain names, no folders)
    constexpr size_t kListMax = 2048;  // a longer .LST is malformed (the built-in frame is used)
    if (!isFollower()) {
        std::string t = menuFileLocal(name);
        return t.size() > kListMax ? std::string() : t;
    }
    if (auto it = listCache_.find(name); it != listCache_.end()) return it->second;
    if (!menuExists(name)) return "";  // (menuExists remembers the main's answer, so a look without one costs no second ask)
    std::string s = menuFile(name);
    if (s.size() > kListMax) s.clear();
    if (!s.empty()) listCache_[name] = s;  // (a failed ask is asked again)
    return s;
}

// a menu's art from this board's own storage a piece at a time (false when it isn't here: the whole-file path)
bool ThemeProvider::menuArtPieces(const std::string& name, const axis::bbs::ArtEmit& piece) {
    if (name.empty() || name.find('/') != std::string::npos || name.find("..") != std::string::npos) return false;
    if (isFollower() && axis::bbs::theme::menuScreenFile(name)) return false;  // a follower: the theme's screen comes from the main (menuFile)
    FILE* f = openThemeScreen(name);  // the theme's screen first
    if (!f) f = fopen((host_.sdRoot() + "/menus/" + name).c_str(), "rb");
    if (!f) return false;
    char b[512];
    for (size_t n, total = 0; total < 16384 && (n = fread(b, 1, sizeof b, f)) > 0; total += n) {
        const void* sauce = memchr(b, 0x1a, n);  // the SAUCE record: not part of the picture
        piece(b, sauce ? (size_t)((const char*)sauce - b) : n);
        if (sauce) break;
    }
    fclose(f);
    return true;
}

// One piece of a menu screen for the art drawer (Node::setMenuArt): the same file menuArtPieces reads, `want` bytes from
// `offset`, so a screen is never in RAM whole. *exists false: not here (a follower's theme screen comes whole from the main).
std::string ThemeProvider::menuArtPiece(const std::string& name, size_t offset, size_t want, bool* exists) {
    *exists = false;
    std::string s;
    if (name.empty() || name.find('/') != std::string::npos || name.find("..") != std::string::npos || offset >= 16384) return s;
    if (isFollower() && axis::bbs::theme::menuScreenFile(name)) return s;
    FILE* f = openThemeScreen(name);  // the theme's screen first
    if (!f) f = fopen((host_.sdRoot() + "/menus/" + name).c_str(), "rb");
    if (!f) return s;
    *exists = true;
    axis::bbs::readFileAppend(f, want < 16384 - offset ? want : 16384 - offset, offset, s);
    fclose(f);
    return s;
}

void ThemeProvider::forgetRemote() {
    artCache_.clear();  // the theme may have changed
    wfcAnsKey_.clear();  // (and so may the waiting screen)
    existsCache_.clear();
    listCache_.clear();
    mainKnowsExists_ = false;  // (this may be another main: an older one answers every unknown letter "0")
}

}  // namespace axis::app
