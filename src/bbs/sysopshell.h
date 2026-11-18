// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The sysop menu inside the BBS (docs/superpowers/specs/2026-11-05-sysop-menu-design.md): layered
// lightbar screens in the board's look, the same online (* at the main menu) and on the board's own
// screen (the local console, by touch). Every change goes through an @ command (`run`), the same
// code as the serial console and the PC control panel, so a change here is a change everywhere.
//   keys: up/down or the item's number/letter to move, Enter to pick, Esc or Q back a layer,
//   N / P next and previous page. tapRow(): the local console's touch picks the item on that row.
#pragma once
#include <functional>
#include <memory>
#include <string>
#include <vector>

#ifndef AXIS_NETS
#define AXIS_NETS 1  // Sysop Net (main.cpp); -DAXIS_NETS=0 leaves its page and commands out
#endif

namespace axis::bbs {

// the sysop menu's sections: beginAt() goes straight into one (the Renegade hotkeys and the board's own screen)
enum class SysopSection { Top, Settings, Users, Menus, MsgBases, FileAreas, System, Look };

// An @ command that only means something on the board where it was typed (its cluster key, a restart,
// setup, its SD card, its relays and screen). A follower runs these itself; the main never runs one
// a follower sends it ("@CLUSTERKEY", "@RESTART force" ... ; any case).
bool boardOnlyCommand(const std::string& cmd);
// The main's answer to a follower's "C" request: a board-only command is refused, anything else is run.
std::string followerCommand(const std::string& cmd, const std::function<std::string(const std::string&)>& run);

class SysopShell {
public:
    using Run = std::function<std::string(const std::string&)>;  // "@USERS" -> its output
    // cols 40: the board's own screen in big letters. local: the sysop is at the board (its touchscreen, the
    // serial console, the PC panel's console); false over telnet, where restart, setup, format, the cluster
    // key and the station's relays are not offered.
    SysopShell(std::string& out, Run run, int cols = 80, bool local = false);
    void begin();
    void beginAt(SysopSection s);  // straight into a section; Esc from there leaves the shell
    bool key(char c);          // false once the sysop has left the top layer
    void tapRow(int row);      // a tap on that screen row (0 = the top line)
    bool open() const { return !layers_.empty(); }
    bool wantsWfc() const { return wantWfc_; }  // left by picking the waiting for caller screen (telnet)
    // Look previews: a host that can draw a look's screens turns them on. Picking a theme or waiting screen
    // skin then sets nothing: takePreview() hands the host the setting ("theme" / "wfc") and the look id
    // ("classic" for the built-in waiting screen), once. previewDone(): the host's back; a note means it set
    // the look (the picker closes and the note shows), none means it didn't (the list again).
    void setPreviews(bool on) { previews_ = on; }
    // A lone Esc: the host calls escTimeout() once nothing has followed it for a moment (an arrow key's
    // bytes arrive together). It goes back a layer; false when that left the menu.
    bool escPending() const { return esc_ == 1 && !asking_; }
    bool escTimeout();
    bool takePreview(std::string& key, std::string& look);
    void previewDone(const std::string& note);
    bool asking() const { return asking_; }  // a question is waiting for a typed answer and Enter
    // The page on top, built and drawn again when its title is `title` and no question is open (new data came in). True if so.
    bool reloadIf(const std::string& title);
    bool yesNo() const { return asking_ && yesNo_; }  // ... or for one key, Y or N
    bool secret() const { return asking_ && secret_; }  // the answer is a password
    const std::string& question() const { return question_; }

    struct Item {
        std::string label, value;
        std::function<void()> pick;  // what Enter does
    };
    struct Layer {
        std::string        title;
        std::vector<Item>  items;
        std::function<void(Layer&)> build;  // (re)fills items: after every change the layer is rebuilt
        size_t             at = 0;          // the lightbar
        // a paged list, one page at a time: the start shown and the next page's (0: the last page), the earlier
        // pages' starts, the move asked for (+1 More, -1 Earlier), and the rows shown (kept when the board doesn't answer)
        size_t                   start = 0, next = 0;
        int                      step = 0;
        std::vector<size_t>      back;
        std::vector<std::string> rows;
        std::vector<std::string> text;      // a read-only page (the event log ...) instead of items
    };

private:
    void push(const std::string& title, std::function<void(Layer&)> build);
    Item page(const std::string& label, const std::string& value, const std::string& title, std::function<void(Layer&)> build);
    void pushText(const std::string& title, const std::string& lines);
    void pop();
    void draw();
    void ask(const std::string& question, bool secret, std::function<void(const std::string&)> done);
    void confirm(const std::string& question, std::function<void()> yes);
    void did(const std::string& reply);  // the last command's answer, shown under the list
    void refresh();
    void enter(SysopSection s);
    std::string setting(const std::string& key);  // one axis.ini value ("" if unset)
    const std::vector<std::string>& settingLines();  // @SETTINGS, asked once per layer build
    std::vector<std::string> lines(const std::string& cmd);
    // a list the board sends a page at a time ("@USERS " + start; "more <next>" ends a page with more to come).
    // fetch: one page; ok false when the board didn't answer (the note says so).
    std::vector<std::string> fetch(const std::string& cmd, size_t from, size_t& next, bool& ok);
    const std::vector<std::string>& page(Layer& l, const std::string& cmd);  // the layer's page, after Earlier... if any
    void moreItem(Layer& l);  // More...: the next page in its place
    // the pages from `from` on until each() says stop; false (and the layer shows only a note) when one didn't come
    bool walk(const std::string& cmd, const std::function<bool(const std::string&)>& each, size_t from = 0, size_t until = (size_t)-1);
    // the layers
    void users(Layer& l);
    void user(Layer& l, const std::string& handle, size_t from);
    void areas(Layer& l, bool msgs);
    void area(Layer& l, bool msgs, int n);
    void files(Layer& l, const std::string& area);
    void posts(Layer& l, const std::string& area);
    void settings(Layer& l);
    void settingGroup(Layer& l, int group);
    void menus(Layer& l);
    void menuItem(Layer& l, int n);
    void system(Layer& l);
    void sections(Layer& l);
    void nodes(Layer& l);
    void look(Layer& l);
    void lookPick(Layer& l, const std::string& key, bool follow);
    void station(Layer& l);
    void modem(Layer& l);
#if AXIS_NETS
    void sysopNet(Layer& l);
    void records(Layer& l);                                                // Traffic and logs: the record browser
    void recordPage(const std::string& title, const std::string& cmd);  // a list of 20 lines, from the newest
    void recordList(Layer& l, const std::string& cmd);                 // the 20 from recPages_.back() on
    void userNet(Layer& l);
    void userVoting(Layer& l);                                                         // New Board Voting
    void userApp(Layer& l, const std::string& board);  // one applicant, and the vote
    Item netAsk(const std::string& label, const std::string& value, const std::string& cmd);  // asks, then runs cmd + answer
    Item runRow(const std::string& label, const std::string& value, std::string cmd);  // a row that runs one command
    Item netShow(const std::string& label, const std::string& cmd);                           // a text page of cmd's answer
#endif

    std::string&       out_;
    Run                run_;
    std::vector<Layer> layers_;
    std::string        note_;                 // the last command's answer
    // a question being answered (ask) or a yes/no (confirm)
    bool                                   asking_ = false, secret_ = false, yesNo_ = false;
    std::string                            question_, line_;
    std::function<void(const std::string&)> answer_;
    int                                    esc_ = 0;  // 1 after ESC, 2 after ESC [
    int                                    cols_ = 80;
    bool                                   local_ = false;
    bool                                   wantWfc_ = false;
    bool                                   previews_ = false;
    std::string                            previewKey_, previewLook_;
    std::vector<std::string>               settings_;  // @SETTINGS for the layer being built
    bool                                   haveSettings_ = false;
    bool                                   cut_ = false;  // the last lines() reply ended "axis: reply cut"
    bool                                   old_ = false;  // the last fetch() was an older main's whole list
    bool                                   noAnswer_ = false;  // a walk() failed: the layer shows a note, no values
#if AXIS_NETS
    std::vector<size_t>                    recPages_;     // the record list: the index each page starts at (Older, Newer)
    std::string                            voteComment_;  // New Board Voting: the comment typed for the next vote
#endif
    std::string                            header(const std::string& title) const;
    size_t page_() const { return cols_ >= 80 ? 14 : 7; }  // items a page: 14 online, 7 on the board's 12-line screen
};

}  // namespace axis::bbs
