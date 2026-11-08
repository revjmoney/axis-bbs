// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// One caller's session: welcome, handle, main menu, logoff. Portable: it sees clean
// keystrokes (after telnet) and produces bytes to send; the host owns the socket.
#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "bbs/config.h"
#include "bbs/script.h"
#include "bbs/sysopshell.h"
#include "bbs/email.h"
#include "bbs/filebrowser.h"
#include "bbs/filelib.h"
#include "bbs/msgreader.h"
#include "bbs/newuser.h"
#include "bbs/splash.h"
#include "bbs/users.h"
#include "bbs/usersettings.h"
#include "bbs/version.h"
#include "bbs/menurunner.h"
#include "bbs/loginseq.h"
#include "bbs/wfc.h"

namespace axis::bbs {

struct SysInfo {
    std::string ip, host, wifi, timeText, storage;
    std::string hardware = "ESP32";
    int         rssi = 0;
    uint32_t    uptimeSec = 0, freeHeap = 0, callsToday = 0;
    int         nodesTotal = 1;
    std::vector<std::string> nodes;  // who is on each node ("" = waiting); this caller's own node too
};

class AxisNetMenu;

// Renegade sysop keys: S U # B F L open a section; E A ! are coming soon
bool renegadeKey(char key, SysopSection& section, bool& comingSoon);

class Node {
public:
    ~Node();
    Node(Node&&);
    Node& operator=(Node&&);
    enum class State { Splash, Matrix, Login, Password, ApplyAsk, NupAsk, Applying, Pending, OneLinerAsk, OneLiner, Menu, Messages, Doors, InDoor, Files, Transfer, Chat, Paging, Settings, MultiNode, Split, Email, Rumors, BbsList, Bulletin, Voting, UserEdit, PreLogon, CheckAccess, ApplyBar, SeqPause, AxisNet, AxisCall, Script, Sysop, Wfc, Done };
    struct WhoLine {
        int         node = 0;
        std::string handle, activity;  // handle "" = waiting for a caller
    };
    enum class DoorEnd { Normal, OutOfMemory, CarrierLost, TimeLimit, LoadError, Busy, NotHere };  // NotHere: asked for at the board's own screen (no terminal for a door)  // Busy: another caller here has the slot
    static constexpr size_t   kMaxHandle = 30;

    // files: the file areas' store (nullptr: the Files menu says the areas are offline)
    Node(int nodeNumber, std::function<SysInfo()> info, BbsConfig cfg = {}, FileLibrary* files = nullptr);
    // accounts: without them anyone is let in by handle (the first builds, tests)
    void setAccounts(UserStore* users, std::function<void()> save, uint32_t (*rng)(), std::function<uint32_t()> unixNow);
    // true if this handle has a warrior after the call. Registration writes one. Null skips it.
    void setWarrior(std::function<bool(const std::string&)> ensure) { ensureWarrior_ = std::move(ensure); }
    bool isSysop() const { return sysop_; }
    // Kept so a caller can still hand in an @ runner. The telnet sysop shell is gone.
    // * opens the user editor. The touch sysop menu is the panel, not this session.
    void setSysopRun(std::function<std::string(const std::string&)> run) { sysopRun_ = std::move(run); }
    // The 80x25 waiting-for-caller screen (State::Wfc): the theme's WFC.ANS (through setArt) or the built-in one, filled with this
    // snapshot, redrawn every 5 s when it changed. Keys: S U # B F L open the sysop shell at that section (Esc comes back here),
    // E A ! are coming soon, Space goes to the main menu as this sysop, Q leaves (State::Done).
    void setWfcSnapshot(std::function<WfcSnapshot()> snap) { wfcSnap_ = std::move(snap); }
    void openWfc(uint32_t nowMs);
    void tapRow(int) {}
    // /scripts/name.axs. An empty name lists the folder. False when that file is not on the card.
    void setScripts(std::function<std::string(const std::string&)> load, std::function<std::vector<std::string>()> list) {
        scriptLoad_ = std::move(load);
        scriptList_ = std::move(list);
    }
    bool beginScript(const std::string& name);
    bool failedLogin() const { return failedLogin_; }  // the call ended on wrong passwords (for the per-address lockout)
    void addMinutes(int m) { if (callStart_) callStart_ += (uint32_t)m * 60000u; }  // the sysop gives more time (this call only)
    std::function<void(const std::string&)> onEvent;  // for the event log ("new user password: 3 wrong tries from X")
    // Chat across the nodes: say(text, page) sends a teleconference line (or a page for the sysop);
    // who() lists every node. Without them [T] [W] [P] say the chat is offline.
    void setChat(std::function<void(const std::string&, bool)> say, std::function<std::vector<WhoLine>()> who) {
        say_ = std::move(say);
        who_ = std::move(who);
    }
    // the message base ([M]): nullptr = offline
    void setMessages(MessageStore* store) { msgStore_ = store; }
    // the oneliner wall: get() the newest lines ("handle: text"), add() one; shown after login
    void setOneLiners(std::function<std::vector<std::string>()> get, std::function<void(const std::string&)> add) {
        linersGet_ = std::move(get);
        linersAdd_ = std::move(add);
    }
    // a line said on any node (this one's own lines come back this way too, so all see one order)
    void chatIn(int node, const std::string& handle, const std::string& text, bool page);
    // Node to node (Oblivion/2's & commands): what this node sends (kind, target node or 0 for all,
    // text) and what reaches it. Kinds above chat (0) and page (1):
    static constexpr int kNodeNote = 2, kNodeBroadcast = 3, kNodeForce = 4, kSplitAsk = 5, kSplitYes = 6, kSplitNo = 7,
                         kSplitKey = 8, kSplitEnd = 9;
    void setNodeLink(std::function<void(int kind, int to, const std::string& text)> send) { send_ = std::move(send); }
    void nodeIn(int kind, int from, const std::string& handle, const std::string& text);
    bool available() const { return available_; }  // for chat and notes; off = do not disturb
    int  number() const { return node_; }
    // Big fixed screens (the login art) go straight to the caller through this, a piece at a time
    // from flash, instead of through takeOutput(): a copy of them needs more heap than a busy board has.
    // the Oblivion look's screens from the card (MATRIX.ANS, MAIN.ANS): their text, or "" for the stock screen
    void        setArt(std::function<std::string(const std::string&)> art) { art_ = std::move(art); }
    // ViSiON/3-style menu files (MAIN.MNU, MAIN.ANS...): with a MAIN.MNU they drive the main menu; without, the built-in one
    void        setMenus(MenuRunner::Loader load, MenuRunner::ArtPieces pieces = nullptr);
    // the last callers list ("handle|node|time|location" lines, oldest first) and adding this call to it
    void        setLastCallers(std::function<std::vector<std::string>()> get, std::function<void(const std::string&)> add) {
        callersGet_ = std::move(get);
        callersAdd_ = std::move(add);
    }
    // the board's shared lists ("rumors": anonymous one-liners, "bbslist": "name|address|about"), kept on the main
    void        setLists(std::function<std::vector<std::string>(const std::string&)> get,
                         std::function<void(const std::string&, const std::string&)> add) {
        listGet_ = std::move(get);
        listAdd_ = std::move(add);
    }
    void        setDirect(std::function<void(const char*, size_t)> direct) { direct_ = std::move(direct); }
    void        begin(uint32_t nowMs);
    void        beginLocal(uint32_t nowMs);
    void        beginLocalMenu(uint32_t nowMs);  // the board's local logon: sysop, then the same login sequence as a caller
    void        beginLocalQuiet(uint32_t nowMs);  // the sysop at the board, logged in, no splash and no menu yet
    void        input(const uint8_t* p, size_t n, uint32_t nowMs);
    void        poll(uint32_t nowMs);
    std::string takeOutput();
    bool        wantsHangup() const { return state_ == State::Done; }
    // Axis Net asked to call a remote board, or to ask the boards we know (R, or a new note).
    bool               axisTakeProbe();
    std::string        axisDialHost() const;
    int                axisDialPort() const;
    void               axisCallDone(bool reached, const char* why = nullptr);
    void               axisRedraw();
    State       state() const { return state_; }
    std::string tagline() const;       // the caller's tagline (signs their posts), "" if none
    std::string activityText() const;  // what the caller is doing, in a few words ("files: ANSI Packs")
    const std::string& handle() const { return handle_; }
    // the door the caller picked ("" unless InDoor); the host runs it, then calls resumeAfterDoor
    std::string      doorRequested() const { return state_ == State::InDoor ? door().id : ""; }
    const DoorEntry& door() const { return door_ < doorList_.size() ? doorList_[door_] : cfg_.doors[door_]; }
    // the doors this caller can pick: enabled rows, and for the sysop each enabled LORD's setup program
    // (LORDCFG, drawn straight to the screen: drop "local")
    std::vector<DoorEntry> doorChoices() const;
    void                   applyBoard(const BbsConfig& next);  // section switches and the door list, for a caller already on
    std::string            editedMenu() const;  // the sysop's main menu items (cfg mainMenu) in a window
    void             resumeAfterDoor(DoorEnd how, uint32_t nowMs);
    // a download or upload from the Files menu (State::Transfer): the host runs it, then reports back
    const TransferRequest& transfer() const { return browser_->transfer(); }
    void transferDone(bool ok, uint32_t bytes, uint32_t ms, const std::string& note, uint32_t nowMs);

private:
    struct MciScope {  // the theme's header art gets this node's codes while it is in scope
        explicit MciScope(Node& n);
        ~MciScope();
    };
    enum class ScriptBack { Menu, Sequence, Doors };
    bool startScript(const std::string& name, ScriptBack back);
    void pumpScript();
    void finishScript();
    void scriptKey(char c);
    bool scriptDoor();
    std::string cleanScriptName(std::string name) const;
    ScriptProg::World scriptWorld() const;
    void key(char c);
    void showMenu();
    void showMainMenu();  // the full-screen menu: the drippy MENU banner and every command
    void showDoors();
    void loggedIn();
    void showPending();
    void pendingMenu();
    void prompt();
    void showWelcome();
    void menuOrLiners();  // after login: the oneliner wall if there is one, then the menu
    void lineKey(char c);
    std::unique_ptr<Splash> splash_;  // the RotatoCAM splash, before the welcome  // line input for the teleconference and pages
    int                       node_;
    std::function<void(const std::string&, bool)> say_;
    std::function<std::vector<WhoLine>()>        who_;
    std::function<std::vector<std::string>()>    linersGet_;
    MessageStore*                                msgStore_ = nullptr;
    std::unique_ptr<MessageReader>               msgs_;
    std::unique_ptr<UserSettings>                settings_;
    std::unique_ptr<EmailBox>                    email_;
    std::map<std::string, int>                   lastRead_;  // without an account (tests, first builds)
    std::function<void(const std::string&)>      linersAdd_;
    std::function<SysInfo()>  info_;
    BbsConfig                 cfg_;
    State                     state_ = State::Login;
    std::function<void(const char*, size_t)> direct_;
    std::function<std::string(const std::string&)> art_;
    std::unique_ptr<MenuRunner>                    menu_;
    MenuRunner::Loader                             menuLoad_;
    bool                                           frontDoor_ = false;  // ViSiON/3's front door (PDMATRIX) is running
    bool                                           backToMatrix_ = false;  // a key after check access: the matrix again
    int                                            wanders_ = 0;        // matrix picks without logging in (10, then goodbye)
    int                                            loginAt_[2][2] = {};  // LOGIN.ANS |{P} and |{O}: row, column (0: none)
    std::unique_ptr<YesNoBar>                      applyBar_;
    std::vector<LoginStep>                         seq_;  // ViSiON/3's login sequence, running between login and MAIN
    size_t                                         seqAt_ = 0;
    bool                                           inSeq_ = false, seqFast_ = false, seqPauseNext_ = false;
    void                                           startSequence();
    void                                           nextStep();
    void                                           seqResume();  // a step's screen is done: its pause, or the next step
    bool                                           showFrontDoor();
    void                                           frontKey(char c);
    void                                           showPrelogon();
    void                                           showLoginScreen();
    std::string                                    loadScreen(const std::string& file) const;  // menu file, else the theme. Empty when it is not there.
    void                                           paintScreen(const std::string& art);
    bool                                           showArt(const std::string& file);  // false when that screen is not on the card
    std::string                                    menuName_ = "MAIN";
    bool                                           builtin_ = false;  // the menu files failed: the built-in menu this call
    bool                                           enterMenu(const std::string& name);
    AcsUser                                        menuWho() const;
    std::string                                    menuValue(const std::string& k) const;  // a menu placeholder's value
    std::function<void(int, int, const std::string&)> send_;
    std::function<std::vector<std::string>()> callersGet_;
    std::function<void(const std::string&)>   callersAdd_;
    std::function<std::vector<std::string>(const std::string&)>        listGet_;
    std::function<void(const std::string&, const std::string&)>       listAdd_;
    int                       step_ = 0;            // which question a multi-step entry is on
    std::string               entry_;               // the answers so far ("name|address|...")
    void                      showRumors();
    void                      showBbsList();
    void                      showAxisNet();
    void                      showTopTen();
    int                       editId_ = 0;  // the sysop's user editor: the caller open (0 = the list)
    bool                      twit() const;
    bool                      hiddenWriter(const std::string& from) const;  // this caller is flagged a twit
    void                      showUsers();
    void                      showUser();
    void                      userEditKey(char c);
    int                       voteQ_ = 0;  // the question open in the voting booth (1-based)
    void                      showVotes();
    void                      showQuestion(int q);
    void                      votingKey(char c);
    void                      rumorLine();          // one random rumor, over the main menu
    void                      extrasKey(char c);    // Rumors and BbsList states
    void                      showLastCallers();
    void                      showUserList();
    void                      showWhoOnline();
    void                      showSystemInfo();
    bool                      available_ = true;
    uint32_t                  prevCall_ = 0;
    uint32_t                  callStart_ = 0;  // when they logged in (ms); the call's time limit counts from here
    bool                      timeWarned_ = false;
    int                       minutesLeft(uint32_t nowMs) const;  // -1 = no limit  // the caller's last call before this one (unix time)
    char                      mnAsk_ = 0;          // MultiNode: the command waiting for a node number / text
    int                       mnTo_ = 0, splitWith_ = 0, splitAsk_ = 0;  // the node asked, in split chat with, asking us
    int                       splitRow_[2] = {2, 14}, splitCol_[2] = {1, 1};  // split chat cursors: their half, mine
    std::vector<FileArea>     myAreas_, myMsgAreas_;  // the areas this caller's security level opens
    int                       securityLevel() const;  // the sysop 255, a caller their account's, no accounts 0
    std::vector<FileArea>     openTo(const std::vector<FileArea>& all) const;
    std::string               splitHandle_;        // who is asking us
    std::vector<std::string>  notes_;              // notes that came while a door or transfer had the screen
    void                      showMultiNode();
    void                      multiKey(char c);
    void                      splitKey(char c);
    void                      splitPut(int half, char c);
    void                      startSplit(int with, const std::string& them);
    void                      showNotes();
    int                       barAt_[4][2] = {};  // row, column of each matrix item from MATRIX.ANS's |0N marks (0 = stock layout)
    bool                      o2Keys_ = false;   // a MAIN.ANS is up: keys follow stock Oblivion/2 letters
    int                       bar_ = 0, esc_ = 0;  // the matrix lightbar; an arrow key's ESC sequence so far
    bool                      o2() const;
    std::string               mci(const std::string& s) const;  // %UN %BN %SN %NN %DT %VN filled in, pipe colours to ANSI
    void                      showMatrix();
    void                      drawBarItem(int i, bool lit);
    void                      matrixKey(char c);
    void                      o2Prompt(const char* menu);
    std::string               out_, line_, handle_;
    uint32_t                  lastInput_ = 0;
    size_t                    door_ = 0;
    std::vector<DoorEntry>    doorList_;  // what showDoors listed
    std::function<std::string(const std::string&)> scriptLoad_;
    std::function<std::vector<std::string>()>      scriptList_;
    std::unique_ptr<ScriptProg>                    script_;
    std::unique_ptr<SysopShell>                    shell_;
    std::function<std::string(const std::string&)> sysopRun_;
    std::string                                    slash_;  // a sysop's // keys so far
    bool                                           local_ = false, slashPass_ = false, swallowLf_ = false;
    bool                                           slashKey(char c);
    std::function<WfcSnapshot()>                   wfcSnap_;
    WfcSnapshot                                    wfcLast_;
    uint32_t                                       wfcDrawn_ = 0;
    bool                                           wfcReturn_ = false;  // the shell was opened from the WFC: Esc comes back to it
    int                                            wfcEsc_ = 0;         // 1 after ESC, 2 inside a CSI (arrow keys are nothing)
    WfcSnapshot                                    wfcSnapshot();
    void                                           wfcDraw(const WfcSnapshot& s, bool clear);
    void                                           wfcKey(char c);
    void                                           openSysop(bool at, SysopSection s);
    ScriptBack                                     scriptBack_ = ScriptBack::Menu;
    uint32_t                  holdUntil_ = 0;  // after a wrong password, typing is ignored until then (2 s, then 4 s)
    bool                      failedLogin_ = false;
    UserStore*                users_ = nullptr;
    std::function<void()>     saveUsers_;
    std::function<bool(const std::string&)> ensureWarrior_;
    uint32_t (*rng_)() = nullptr;
    std::function<uint32_t()> unixNow_;
    int                       userId_ = 0, tries_ = 0;
    bool                      sysop_ = false, wantSysop_ = false;
    std::unique_ptr<NewUserApp> app_;
    FileLibrary*              files_;
    std::unique_ptr<FileBrowser> browser_;
    std::unique_ptr<AxisNetMenu> net_;
};

// Sent to a caller who connects while the node is in use.
std::string busyScreen();

}  // namespace axis::bbs
