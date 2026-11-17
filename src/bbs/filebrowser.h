// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The caller's Files menu: pick an area, page through its files, view text files. The Node hands
// it every key while the caller is in Files and returns to its own menu when it is done.
#pragma once
#include <functional>
#include <string>
#include <vector>

#include "bbs/config.h"
#include "bbs/filelib.h"
#include "bbs/lightbar.h"
#include "bbs/listscreen.h"
#include "bbs/transferjob.h"

// The Net Files row: User Net listings from other boards, fetched from their home board as the caller downloads.
#define AXIS_NET_FILES (AXIS_NETS && AXIS_FILES && AXIS_TRANSFERS)

namespace axis::bbs {

#if AXIS_NET_FILES
// The host's view of Net Files, set by Node when nets are on and the caller may read User Net: Files. The browser
// knows nothing of sockets: it asks begin and poll, and once poll says ready it asks for a transfer with req.net set,
// which the host runs over its fetch slot. Every text in it is peer text the host has already made safe (netSafe).
struct NetFilesView {
    enum : int { kStarted = 0, kBusy, kGone, kUnreachable, kOff, kOwn };  // begin's answers (the host's NetFetchBegin)
    static constexpr int kShort = -2;  // page's answer when the board is short of memory now (nothing listed)
    // one page, newest first: files (name, size, description), their listing ids and home board names; next skip, -1
    // for none, or kShort
    std::function<int(int skip, int max, std::vector<FileEntry>& out, std::vector<std::string>& ids,
                      std::vector<std::string>& homes)>
                                                      page;
    std::function<std::string(const std::string& id)> unreachable;     // "" or the reason text
    std::function<int32_t()>                          callerRadioBps;  // -1 when the caller's link is not radio
    // starts the fetch for this caller: kStarted, or why not. kOwn: the listing is this board's, area and name set to
    // the local file (downloaded from its area like any other).
    std::function<int(const std::string& id, std::string& area, std::string& name)> begin;
    std::function<int(uint32_t& size, const char*& word)> poll;  // 0 connecting, 1 ready, -1 failed (word: why)
    std::function<void()>                                 release;  // the caller gives the fetch up
    uint32_t                                              radioLimitKb = 64;
};
#endif

#if AXIS_FILES
class FileBrowser {
    std::function<std::string(uint32_t)> gate_;
    uint32_t                             since_ = 0;
    void                                 showNewFiles();
    void                                 search(const std::string& what);  // every area: names and descriptions

public:
    static constexpr int kPerPage = 15, kViewLines = 20;
    static constexpr size_t kMaxView = 64 * 1024;

    FileBrowser(const std::vector<FileArea>& areas, FileLibrary& lib, std::string& out)
        : areas_(areas), lib_(lib), out_(out) {}
    // downloads and viewing: "" = allowed, else why not (a new caller uploads something first)
    // (size: the file's bytes, 0 for viewing)
    void setDownloadGate(std::function<std::string(uint32_t size)> gate) { gate_ = std::move(gate); }
    void setNewSince(uint32_t t) { since_ = t; }  // [N] at the area list: files newer than this (the last call)
    void setLightbars(bool on) { bars_ = on; }    // false: the numbered text prompts (no ANSI cursor lightbar)
#if AXIS_NET_FILES
    void setNetFiles(NetFilesView v) { net_ = std::move(v); }  // the area list ends with "Net Files (User Net)"
#endif
    void begin();       // show the area list
    std::string where() const;  // "area list" or the open area's name, for the sysop's activity screen
    bool key(char c);   // false once the caller leaves Files
    bool tick(uint32_t now, uint32_t lastInput);  // a lone Esc backs out; false once the caller leaves Files
    // a download or upload the caller asked for: the host runs it, then calls transferDone
    bool                   wantsTransfer() const { return st_ == St::Transfer; }
    const TransferRequest& transfer() const { return req_; }
    void transferDone(bool ok, uint32_t bytes, uint32_t ms, const std::string& note, uint32_t size);

private:
    enum class St { Areas, List, ViewName, Viewing, DownName, UpDesc, Proto, UpName, Transfer, InfoName, Search, NetWarn, NetConnect };
    void showAreas();
    void refuse(const std::string& why);  // the why in amber, then the list's prompt
    std::string status(size_t ix) const;  // the status line for files_[ix]
    std::string about(size_t i, size_t w) const;  // files_[i]'s About column, w wide
    std::string keyText() const;                  // the list's keys
    std::string title() const;
#if AXIS_NET_FILES
    bool net() const { return net_.page && area_ >= areas_.size(); }  // the Net Files list is open
    size_t netRow() const { return net_.page ? 1 : 0; }               // the area list's extra row
    void netGet(size_t i);   // gate passed: unreachable, the radio question, then the protocol
    void netStart();         // the protocol picked: the host's fetch slot
    void netPoll();          // NetConnect: connecting, ready or failed
    int32_t tripBps() const; // the slowest leg's bps for the radio question, -1 for none
    NetFilesView             net_;
    std::vector<std::string> ids_, homes_;  // the Net Files page's listing ids and home board names
    int                      skip_ = 0, next_ = -1;  // the page's skip and the next page's (-1: none)
    std::string              netId_, netHome_;  // the listing being downloaded
    uint32_t                 netSize_ = 0;
#else
    static constexpr bool   net() { return false; }
    static constexpr size_t netRow() { return 0; }
#endif
    void openArea(size_t i);
    void showPage(int at = 0);
    void selectFile(size_t index);  // Enter on the lightbar: view text, or start a download
    bool listCommand(char c);       // N/P/V/I/D/U/Q while a lightbar is up; true if the key was one of those
    void listPrompt();
    void view(const std::string& name);
    void info(const std::string& name);  // [I]: everything about a file, a ZIP's whole listing
    void showMoreText();
    void askProtocol();
    void startTransfer();
    bool lineKey(char c, size_t max);  // true when Enter ends the line (in line_)

    const std::vector<FileArea>& areas_;
    FileLibrary&                 lib_;
    std::string&                 out_;
    St                           st_ = St::Areas;
    std::string                  line_;
    size_t                       area_ = 0;
    int                          page_ = 0;
    std::vector<FileEntry>       files_;
    std::vector<std::string>     text_;  // the file being viewed, as lines
    size_t                       shown_ = 0;
    TransferRequest              req_;
    ScrollList                   list_{3, kListRow, 74, kListRows, 111, 15};  // white on the theme orange
    bool                         bars_ = true;
    bool                         usingList_ = false;   // a lightbar is on screen
    bool                         typedAreas_ = false;  // after New or Search: the old "area number" line
};
#else
// The file areas are left out of this build (features.h): filebrowser.cpp compiles to nothing, and the main menu never
// offers Files, so a Node never opens one of these.
class FileBrowser {
public:
    FileBrowser(const std::vector<FileArea>&, FileLibrary&, std::string&) {}
    void                   setDownloadGate(std::function<std::string(uint32_t size)>) {}
    void                   setNewSince(uint32_t) {}
    void                   setLightbars(bool) {}
    void                   begin() {}
    std::string            where() const { return "area list"; }
    bool                   key(char) { return false; }
    bool                   tick(uint32_t, uint32_t) { return false; }
    bool                   wantsTransfer() const { return false; }
    const TransferRequest& transfer() const { return req_; }
    void                   transferDone(bool, uint32_t, uint32_t, const std::string&, uint32_t) {}

private:
    TransferRequest req_;
};
#endif

}  // namespace axis::bbs
