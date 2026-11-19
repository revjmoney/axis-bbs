// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// [M]essages: the areas with their counts and how many are new to this caller, an area's newest
// headers, reading by number or the next new one, posting and replying (a body ends with /S).
#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "bbs/config.h"
#include "bbs/lightbar.h"
#include "bbs/listscreen.h"
#include "bbs/msgbase.h"

namespace axis::bbs {

class MessageReader {
public:
    static constexpr int kPerPage = 15, kMaxLines = 99;
    // lastRead: the caller's newest-read number per area (kept with their account)
    MessageReader(const std::vector<FileArea>& areas, MessageStore& store, std::string& out, std::string handle,
                  std::map<std::string, int>& lastRead, std::function<std::string()> now)
        : areas_(areas), st_(store), out_(out), handle_(std::move(handle)), lastRead_(lastRead), now_(std::move(now)) {}
    void        setTagline(std::string t) { tagline_ = std::move(t); }  // signs each post
    // posts by these writers don't show (a twit's, to everyone but them and the sysop)
    void        setHidden(std::function<bool(const std::string& from)> h) { hidden_ = std::move(h); }
    void        setLightbars(bool on) { bars_ = on; }  // false: numbered text prompts
    // called as list screens are built, so a long one leaves a piece at a time (Node::spill) instead of sitting in out whole
    void        setSpill(std::function<void()> s) { spill_ = std::move(s); }
    void        begin();
    // the new scan: every area's new messages one after another ([N] moves on, across areas)
    void        beginScan();
    static int  countNew(const std::vector<FileArea>& areas, MessageStore& store, const std::map<std::string, int>& lastRead);
    bool        key(char c);  // false once the caller leaves for the main menu
    bool        tick(uint32_t now, uint32_t lastInput);  // a lone Esc backs out
    std::string where() const;
    std::string areaName() const { return s_ != St::Areas && area_ < areas_.size() ? areas_[area_].name : std::string(); }  // "" at the area list

private:
    enum class St { Areas, List, ReadNum, Reading, To, Subject, Body };
    void showAreas();
    void openArea(size_t i);
    // the message list's look (MSGS.LST), loaded when an area opens (lightbars only)
    const ListTemplate* tpl() const { return hasTpl_ ? &tpl_ : nullptr; }
    // headers a window: today 15 on a 14 row bar (the bar scrolls one), so a template's window is its rows plus that one
    int         perPage() const { return hasTpl_ ? tpl_.rows + (kPerPage - kListRows) : kPerPage; }
    std::string msgField(size_t i, const std::string& k) const;  // MSGS.LST's values for heads_[i]
    std::string statusLine(size_t i, const std::string& builtin) const;  // `builtin`, or the template's status line
    void showList();
    void listPrompt();
    void show(int num);
    void nextNew();
    void startPost(bool reply);
    void save();
    bool lineKey(char c, size_t max);  // true on Enter

    const std::vector<FileArea>& areas_;
    MessageStore&                st_;
    std::string                  tagline_;
    bool                         scanning_ = false;
    std::function<bool(const std::string&)> hidden_;
    std::vector<Message>         headersOf(const std::string& area);
    // an area's message count and how many are newer than `last`, streamed (headersOf held up to 200 headers an area just to
    // count them); as headersOf would give them: the newest MessageStore::kMaxHeaders, hidden writers left out
    static void tally(MessageStore& store, const std::string& area, int last, const std::function<bool(const std::string&)>& hidden,
                      unsigned& total, unsigned& fresh);
    std::function<void()>        spill_;
    void                         spill() { if (spill_) spill_(); }
    std::string&                 out_;
    std::string                  handle_;
    std::map<std::string, int>&  lastRead_;
    std::function<std::string()> now_;
    St                           s_ = St::Areas;
    size_t                       area_ = 0;
    std::vector<Message>         heads_;
    size_t                       listFrom_ = 0;  // first header in the lightbar window (newest page to start)
    ScrollList                   list_{3, kListRow, 74, kListRows, 111, 15};  // white on the theme orange
    ListTemplate                 tpl_;
    bool                         hasTpl_ = false;
    int                          last_ = 0;  // the open area's last read number when the list was built (the new marker)
    bool                         bars_ = true;
    bool                         usingList_ = false;
    Message                      current_, draft_;
    std::string                  line_;
    int                          lines_ = 0;
};

}  // namespace axis::bbs
