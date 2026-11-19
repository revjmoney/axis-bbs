// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The screens a caller sees: the theme's art (MAIN.ANS, MATRIX.ANS, the waiting screens, the Saylor poster), the menu
// files (MAIN.MNU and their art) and the scripts, from this board's storage (host.sdRoot()). A cluster follower that
// lacks them asks the main over the link (`follower` and `remote`), and keeps the small answers in caches.
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "app/host.h"
#include "bbs/artfetch.h"
#include "bbs/config.h"
#include "bbs/features.h"
#include "bbs/pipecodes.h"

namespace axis::app {

class ThemeProvider {
public:
    explicit ThemeProvider(Host& h) : host_(h), artCache_(kArtCacheCap) {}

    // The board's settings as they are now (they are replaced, not edited, so this is asked on every use).
    std::function<const axis::bbs::BbsConfig&()> config;
    // A cluster follower now: the theme lives on the main (empty: never, as on the PC).
    std::function<bool()> follower;
    // Asks the main (the follower's link); false when it did not answer. Empty on the PC.
    std::function<bool(const std::string& req, std::string& reply)> remote;

    // The follower's theme art, at most kArtCacheCap bytes in all (bbs/artfetch.h).
    static constexpr size_t kArtCacheCap = 16384;
    static constexpr size_t kThemeArtCap = 12288;
    static constexpr size_t kPosterCap = 49152;   // SAYLOR.ANS is 37 KB, SAYLORLG.ANS is 45 KB. Not cached.
    static constexpr size_t kPosterChunk = 7000;  // a cluster frame is 8 KB, so a follower pulls the poster in pieces
    static bool             posterName(const std::string& name) { return name == "SAYLOR.ANS" || name == "SAYLORLG.ANS"; }

    std::string readThemeFile(const char* folder, const std::string& name, size_t cap, size_t offset, bool* opened);
    std::string readThemeFile(const char* folder, const std::string& name) { return readThemeFile(folder, name, kThemeArtCap, 0, nullptr); }
    std::string posterPiece(const std::string& name, size_t offset, size_t want, bool* exists);
    std::string themeArtLocal(const std::string& name);
    bool        themeArtPieceAppend(const std::string& name, size_t off, size_t want, std::string& out);
    std::string themeArt(const std::string& name);
    std::string themeArtTop(const std::string& name, int rows);
    std::shared_ptr<const std::string> wfcArtShared();
    std::string menuFileLocal(const std::string& name, bool theme = true);
    std::string menuFile(const std::string& name);
    bool        menuExistsLocal(const std::string& name, bool theme = true);
    bool        menuExists(const std::string& name);
#if AXIS_SCRIPTS
    std::vector<std::string> scriptNamesLocal();
    std::string              scriptFile(const std::string& name);
    std::vector<std::string> scriptNames();
#endif
    bool        menuArtPieces(const std::string& name, const axis::bbs::ArtEmit& piece);
    std::string menuArtPiece(const std::string& name, size_t offset, size_t want, bool* exists);

    // The settings arrived from the main: the theme may have changed, so forget what came from the main.
    void forgetRemote();
    // The largest art string read from the SD (theme or menu file), for @STATUS.
    size_t artMax() const { return artMax_; }

private:
    bool        isFollower() { return follower && follower(); }
    const axis::bbs::BbsConfig& cfg() { return config(); }
    std::string themesDir() { return host_.sdRoot() + "/themes/"; }
    bool        readThemeFileAppend(const char* folder, const std::string& name, size_t cap, size_t offset, std::string& out);
    bool        posterPieceLocalAppend(const std::string& name, size_t offset, size_t want, std::string& out);
    std::string posterPieceLocal(const std::string& name, size_t offset, size_t want, bool* opened);
    std::string themeArtRemote(const std::string& name, bool* answered);
    std::string readThemeTop(const char* folder, const std::string& name, int rows);
    FILE*       openThemeScreen(const std::string& name);

    Host&                              host_;
    size_t                             artMax_ = 0;
    axis::bbs::ArtCache                artCache_;
    std::shared_ptr<const std::string> wfcAnsShared_;
    std::string                        wfcAnsKey_;
    std::map<std::string, std::string> menuCache_;
    // The follower's answers from the main ("E"), kept until the theme may change (cleared with artCache).
    std::map<std::string, bool> existsCache_;
    uint32_t                    lastEMissMs_ = 0;         // nowMs() of the last unanswered "E" (0: none)
    bool                        mainKnowsExists_ = false;  // the main has answered "E" with a "1" once: its "0" can be trusted
};

}  // namespace axis::app
