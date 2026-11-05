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

namespace axis::bbs {

class SysopShell {
public:
    using Run = std::function<std::string(const std::string&)>;  // "@USERS" -> its output
    SysopShell(std::string& out, Run run);
    void begin();
    bool key(char c);          // false once the sysop has left the top layer
    void tapRow(int row);      // a tap on that screen row (0 = the top line)
    bool open() const { return !layers_.empty(); }

    struct Item {
        std::string label, value;
        std::function<void()> pick;  // what Enter does
    };
    struct Layer {
        std::string        title;
        std::vector<Item>  items;
        std::function<void(Layer&)> build;  // (re)fills items: after every change the layer is rebuilt
        size_t             at = 0;          // the lightbar
        std::vector<std::string> text;      // a read-only page (the event log ...) instead of items
    };

private:
    void push(const std::string& title, std::function<void(Layer&)> build);
    void pushText(const std::string& title, const std::string& lines);
    void pop();
    void draw();
    void ask(const std::string& question, bool secret, std::function<void(const std::string&)> done);
    void confirm(const std::string& question, std::function<void()> yes);
    void did(const std::string& reply);  // the last command's answer, shown under the list
    void refresh();
    std::vector<std::string> lines(const std::string& cmd);
    // the layers
    void users(Layer& l);
    void user(Layer& l, const std::string& handle);
    void areas(Layer& l, bool msgs);
    void area(Layer& l, bool msgs, int n);
    void files(Layer& l, const std::string& area);
    void posts(Layer& l, const std::string& area);
    void settings(Layer& l);
    void settingGroup(Layer& l, int group);
    void menus(Layer& l);
    void menuItem(Layer& l, int n);
    void system(Layer& l);
    void nodes(Layer& l);

    std::string&       out_;
    Run                run_;
    std::vector<Layer> layers_;
    std::string        note_;                 // the last command's answer
    // a question being answered (ask) or a yes/no (confirm)
    bool                                   asking_ = false, secret_ = false, yesNo_ = false;
    std::string                            question_, line_;
    std::function<void(const std::string&)> answer_;
    int                                    esc_ = 0;  // 1 after ESC, 2 after ESC [
    static constexpr size_t kPage = 14;
};

}  // namespace axis::bbs
