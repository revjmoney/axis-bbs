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
#include "bbs/daystats.h"
#include "bbs/script.h"
#include "bbs/sysopshell.h"
#include "bbs/email.h"
#include "bbs/filebrowser.h"
#include "bbs/filelib.h"
#include "bbs/msgreader.h"
#include "bbs/newuser.h"
#include "bbs/nuv.h"
#include "bbs/splash.h"
#include "bbs/users.h"
#include "bbs/usersettings.h"
#include "bbs/version.h"
#include "bbs/menurunner.h"
#include "bbs/loginseq.h"
#include "bbs/wfc.h"
#include "net/telnet.h"

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
    enum class State { Splash, Matrix, Login, Password, ApplyAsk, NupAsk, Applying, Pending, OneLinerAsk, OneLiner, Menu, Messages, Doors, InDoor, Files, Transfer, Chat, Paging, Settings, MultiNode, Split, Email, Rumors, BbsList, Bulletin, Voting, UserEdit, PreLogon, CheckAccess, ApplyBar, SeqPause, MenuHold, AxisNet, AxisCall, Script, Sysop, Wfc, LookPreview, Poster, Nuv, Done };
    struct WhoLine {
        int         node = 0;
        std::string handle, activity;  // handle "" = waiting for a caller
        std::string link;              // "RADIO SF7 -81" for a caller on the LFD3K-SM modem, else ""
    };
    enum class DoorEnd { Normal, OutOfMemory, CarrierLost, TimeLimit, LoadError, Busy, NotHere };  // NotHere: asked for at the board's own screen (no terminal for a door)  // Busy: another caller here has the slot
    static constexpr size_t   kMaxHandle = 30;
    static constexpr uint32_t kPreLoginIdleMs = 60000;   // before login: this long with no key logs the caller off
    static constexpr uint32_t kPreLoginMaxMs = 180000;   // before login: this long in all (not while applying)

    // files: the file areas' store (nullptr: the Files menu says the areas are offline)
    // cfg: the board's settings, shared by every node (a change makes a new copy; a caller keeps the one they called into)
    Node(int nodeNumber, std::function<SysInfo()> info, std::shared_ptr<const BbsConfig> cfg, FileLibrary* files = nullptr);
    Node(int nodeNumber, std::function<SysInfo()> info, BbsConfig cfg = {}, FileLibrary* files = nullptr);  // (tests: a copy of its own)
    // accounts: without them anyone is let in by handle (the first builds, tests)
    void setAccounts(UserStore* users, std::function<void()> save, uint32_t (*rng)(), std::function<uint32_t()> unixNow);
    // true if this handle has a warrior after the call. Registration writes one. Null skips it.
    void setWarrior(std::function<bool(const std::string&)> ensure) { ensureWarrior_ = std::move(ensure); }
    bool isSysop() const { return sysop_; }
    // Kept so a caller can still hand in an @ runner. The telnet sysop shell is gone.
    // * opens the user editor. The touch sysop menu is the panel, not this session.
    void setSysopRun(std::function<std::string(const std::string&)> run) { sysopRun_ = std::move(run); }
    // The 80x25 waiting-for-caller screen (State::Wfc): the theme's WFC.ANS (through setArt) or the built-in one, filled with this
    // snapshot, redrawn every 5 s when it changed (every half second while a newer axispc release blinks in the version).
    // Keys: S U # B F L open the sysop shell at that section (Esc comes back here), E A ! are coming soon, Space goes to the
    // main menu as this sysop, Q leaves (State::Done). V, only while that blink is up, shows the release notes.
    void setWfcSnapshot(std::function<WfcSnapshot()> snap) { wfcSnap_ = std::move(snap); }
    // The cleaned WFC.ANS, read once by the owner and shared (null: the built-in art). When set, wfcDraw uses it instead of setArt.
    void setWfcArt(std::function<std::shared_ptr<const std::string>()> art) { wfcArt_ = std::move(art); }
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
    void        setArt(std::function<std::string(const std::string&)> art);  // (a lite caller gets NAME.LITE.ANS when the theme has it)
    // The Saylor poster (SAYLOR.ANS 37 KB, SAYLORLG.ANS 45 KB) a piece at a time: up to `want` bytes of the file from
    // `offset` ("" at the end; shorter than `want` is the last piece; *exists false: no such poster). With this set the
    // poster streams out one piece per poll(), so neither the file nor its ANSI ever sits in RAM whole.
    using PosterPiece = std::function<std::string(const std::string& file, size_t offset, size_t want, bool* exists)>;
    void        setPoster(PosterPiece piece) { posterPiece_ = std::move(piece); }
    // A menu screen (MAIN.ANS: the theme's, else the menu set's) the same way, a piece at a time from the card; *exists false:
    // not here (a follower), the menu loader's whole copy is drawn instead.
    void        setMenuArt(PosterPiece piece) { menuArtPiece_ = std::move(piece); }
    static constexpr size_t kPosterPiece = 2048, kPosterMax = 49152;  // (SAYLORLG.ANS is 45 KB)
    // ViSiON/3-style menu files (MAIN.MNU, MAIN.ANS...): with a MAIN.MNU they drive the main menu; without, the built-in one
    void        setMenus(MenuRunner::Loader load, MenuRunner::ArtPieces pieces = nullptr);
    // the last callers list ("handle|node|time|location" lines, oldest first) and adding this call to it
    void        setLastCallers(std::function<std::vector<std::string>()> get, std::function<void(const std::string&)> add) {
        callersGet_ = std::move(get);
        callersAdd_ = std::move(add);
    }
    // the board's day counters for System Stats (calls, posts, uploads today, all-time calls). Unset: SysInfo's calls today only.
    void        setDayStats(std::function<DayStats()> get) { dayStats_ = std::move(get); }
    // the board's shared lists ("rumors": anonymous one-liners, "bbslist": "name|address|about"), kept on the main
    void        setLists(std::function<std::vector<std::string>(const std::string&)> get,
                         std::function<void(const std::string&, const std::string&)> add) {
        listGet_ = std::move(get);
        listAdd_ = std::move(add);
    }
    // Whether a menu screen exists, asked without reading it (showPrelogon's probes). Unset: the screen is loaded to find out.
    void        setExists(std::function<bool(const std::string&)> exists) { exists_ = std::move(exists); }
    void        setDirect(std::function<void(const char*, size_t)> direct) { direct_ = std::move(direct); }
    // Whether this caller's line is backed up (a slow radio caller): while it is, the login sequence does not start its next
    // step or the menu after it (poll does once there is room), so one turn draws at most one screen.
    void        setBackedUp(std::function<bool()> backedUp) { backedUp_ = std::move(backedUp); }
#if AXIS_NET_FILES
    // The host's Net Files: Files ends with a "Net Files (User Net)" row while the caller may read User Net: Files.
    void        setNetFiles(NetFilesView v) { netFiles_ = std::move(v); }
#endif
    // With direct set, a screen leaves in pieces of about this size as it is drawn, instead of piling up whole in the output.
    static constexpr size_t kOutSpill = 2048;
    void        begin(uint32_t nowMs);
    void        beginLocal(uint32_t nowMs);
    void        beginLocalMenu(uint32_t nowMs);  // the board's local logon: sysop, then the same login sequence as a caller
    void        beginLocalQuiet(uint32_t nowMs);  // the sysop at the board, logged in, no splash and no menu yet
    void        input(const uint8_t* p, size_t n, uint32_t nowMs);
    void        poll(uint32_t nowMs);
    std::string takeOutput();
    static uint32_t loginScans();  // how many times the login screen's field scan ran (cache misses), for tests
    static size_t outMax();  // the largest output buffer (capacity) handed out so far, for @STATUS
    bool        wantsHangup() const { return state_ == State::Done; }
    // Axis Net asked to call a remote board, or to ask the boards we know (R, or a new note).
    bool               axisTakeProbe();
    std::string        axisDialHost() const;
    int                axisDialPort() const;
    void               axisCallDone(bool reached, const char* why = nullptr);
    void               axisRedraw();
    State       state() const { return state_; }
    int         seqDepthPeak() const { return seqDepthPeak_; }  // tests: login steps nested (1 = never inside another)
    std::string tagline() const;       // the caller's tagline (signs their posts), "" if none
    std::string activityText() const;  // what the caller is doing, in a few words ("files: ANSI Packs")
    const std::string& handle() const { return handle_; }
    int                securityLevel() const;  // the sysop 255, a caller their account's, no accounts 0
    // the door the caller picked ("" unless InDoor); the host runs it, then calls resumeAfterDoor
    std::string      doorRequested() const { return state_ == State::InDoor ? door().id : ""; }
    const DoorEntry& door() const { return door_ < doorList_.size() ? doorList_[door_] : cfg_->doors[door_]; }
    // the doors this caller can pick: enabled rows, and for the sysop each enabled LORD's setup program
    // (LORDCFG, drawn straight to the screen: drop "local")
    std::vector<DoorEntry> doorChoices() const;
    // section switches and the door list, for a caller already on (the rest stays as they called in)
    // prev: the copy main replaced with next; onlySwitches: they differ only in the fields above (onlySwitches(), once per push)
    void                   applyBoard(std::shared_ptr<const BbsConfig> next, const std::shared_ptr<const BbsConfig>& prev = nullptr,
                                      bool onlySwitches = false);
    // a and b match outside the section switches, modem_link, lite_below_bps and the door list (no allocation)
    static bool            onlySwitches(const BbsConfig& a, const BbsConfig& b);
    // The modem's newest report for this call (LFD3K-LINK, net/telnet.h): who is on the radio and how fast. Display only.
    void                   setLink(const net::LinkInfo& l);
    const net::LinkInfo&   link() const { return link_; }
    bool                   viaModem() const { return link_.any; }
    std::string            linkTiers() const;  // the LoRa tiers this call has seen: "SF7", "SF7-SF9", "WIFI"; "" for none
    // The short label the screens show ("RADIO SF7", no RSSI). It follows the link only when the tier changes, and at most
    // every 5 s; linkRedrawDue() is true once each time it moved (the host redraws its screens then, not at every report).
    const std::string&     linkShown() const { return linkShown_; }
    bool                   linkRedrawDue(uint32_t nowMs);
    void                   sysopReload(const std::string& title);  // the sysop menu's page with that title, built again (new data)
    // Lite screens: the modem's last report put this caller below lite_below_bps (0 = never). The same menus and features,
    // lighter art: no login splash, NAME.LITE.ANS where the theme or menu set has one, ANSI movies unpaced. Checked at every
    // screen, so a retrain up to a fast tier gets the normal art from the next screen on.
    bool                   lite() const;  // a radio call, bps at or below lite_below_bps
    static std::string     liteName(const std::string& file);  // "MAIN.ANS" -> "MAIN.LITE.ANS"; "" for a name that has none
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
    void openListedDoor(size_t pick, char echo);  // the door the list picked (a number, a letter, or the lightbar's Enter)
    void leaveDoors();                            // back to the menu, the numbered list and the lightbar both leave this way
    void loggedIn();
    void showPending();
    void pendingMenu();
    // The look's GOODBYE.ANS (a slow radio caller gets GOODBYE.LITE.ANS). False when the look has none, so the
    // caller keeps the line it already builds.
    bool showGoodbye();
    void prompt();
    void showWelcome();
    void menuOrLiners();  // after login: the oneliner wall if there is one, then the menu
    void lineKey(char c);
    static constexpr uint32_t kSplashHoldMs = 1000;  // nothing is sent this long after connect: the modem's link report (a slow radio caller skips the splash) lands first
    bool                    splashHold_ = false;  // in the start delay, before the splash
    uint32_t                splashHoldFrom_ = 0;
    std::unique_ptr<Splash> splash_;  // the RotatoCAM splash, before the welcome  // line input for the teleconference and pages
    int                       node_;
    std::function<void(const std::string&, bool)> say_;
    std::function<std::vector<WhoLine>()>        who_;
    std::function<std::vector<std::string>()>    linersGet_;
    MessageStore*                                msgStore_ = nullptr;
    std::unique_ptr<MessageReader>               msgs_;
    std::unique_ptr<UserSettings>                settings_;
    User                                         sysopSelf_;  // [U] for the sysop: their settings as a record
    void openSysopAccount();
    void saveSysopAccount();
    std::unique_ptr<EmailBox>                    email_;
    std::map<std::string, int>                   lastRead_;  // without an account (tests, first builds)
    std::function<void(const std::string&)>      linersAdd_;
    std::function<SysInfo()>  info_;
    std::shared_ptr<const BbsConfig> cfg_;      // shared with main and the other nodes until this node changes it
    std::vector<std::shared_ptr<const BbsConfig>> retired_;  // copies swapped out, kept until the next poll (a caller may still hold a piece)
    std::shared_ptr<const BbsConfig> appCfg_;   // the copy the new user application reads (it holds a reference)
    bool                      cfgOwn_ = false;  // cfg_ is this node's own copy: written in place
    BbsConfig&                cfgMut();         // a write: this node's own copy first
    void                      retire();         // cfg_ is about to be replaced
    void                      closeFiles();     // the file browser closes: its areas go too
    void                      closeMsgs();      // the message reader closes: its areas go too
    State                     state_ = State::Login;
    std::function<void(const char*, size_t)> direct_;
    std::function<bool()>                    backedUp_;
    bool        seqWait();  // the line is backed up: the sequence's next step is left for poll (seqDue_)
    void spill();  // direct set and the output has reached kOutSpill: all of it goes out now, in order
    void flushDirect();  // out_ to direct_, emptied even if the write throws (capacity kept)
    std::function<std::string(const std::string&)> art_;
    std::function<std::shared_ptr<const std::string>()> wfcArt_;
    PosterPiece                                    posterPiece_;
    PosterPiece                                    menuArtPiece_;
    bool                                           posterHold_ = false;
    int         startPoster(const std::string& file, bool clear, bool hold);
    // The art drawer: every art screen (menu screens, theme screens, the Saylor poster) goes out through it, a piece at a time
    // from its source (the card, or the copy already read), each piece expanded as it goes (artPiece). It draws while the
    // caller's line has room (setBackedUp) and the rest waits for poll(), so a slow caller paces it and its screen never sits
    // in RAM whole. What the node writes after a screen starts (a lightbar, a prompt, a key's echo) goes out behind it.
    // With no line check: plain screens draw at once and the poster a piece per poll(), as before.
    using ArtSource = std::function<std::string(size_t offset, size_t want, bool* exists)>;
    struct Drawing {
        size_t      pos = 0;   // where in out_ its next piece goes
        ArtSource   src;
        size_t      off = 0;   // the next byte of the source
        std::string carry;     // a cut line (artPiece)
        bool        ended = false;  // Ctrl-Z was drawn to: no more of this screen (artPiece)
        bool        poster = false;  // the poster: State::Poster until it is out, then its pause and the next step
    };
    std::vector<Drawing> drawings_;
    static constexpr size_t kArtCap = 16384;  // a screen's art, at most (the poster: kPosterMax)
    void        drawArt(ArtSource src, bool poster);  // starts drawing (behind any drawing not finished yet)
    void        pumpArt(bool onePiece = false);       // as much as the line takes; onePiece: the poster with no line check
    size_t      ready() const { return drawings_.empty() ? out_.size() : drawings_.front().pos; }  // out_ that may go now
    bool        drawMenuArt(const std::string& file);  // MenuRunner::drawArt
    ArtSource   firstThen(std::string first, const std::string& file, const PosterPiece& piece);
    bool        posterStarting_ = false;  // startPoster's own first pieces: it says what comes next, not pumpArt
    std::unique_ptr<MenuRunner>                    menu_;
    MenuRunner*                                    styled();  // the menu, told the look's art dialect (asked at each draw)
    MenuRunner::Loader                             menuLoad_;
    std::function<bool(const std::string&)>        exists_;  // setExists: a menu screen's presence, without reading it
    bool                                           frontDoor_ = false;  // ViSiON/3's front door (PDMATRIX) is running
    bool                                           backToMatrix_ = false;  // a key after check access: the matrix again
    int                                            wanders_ = 0;        // matrix picks without logging in (10, then goodbye)
    int                                            loginAt_[2][2] = {};  // LOGIN.ANS |{P} and |{O}: row, column (0: none)
    std::unique_ptr<YesNoBar>                      applyBar_;
    std::vector<LoginStep>                         seq_;  // ViSiON/3's login sequence, running between login and MAIN
    size_t                                         seqAt_ = 0;
    bool                                           inSeq_ = false, seqFast_ = false, seqPauseNext_ = false;
    bool seqUnheld_ = false;  // a login step's screen is up with no pause of its own: held for a key before the next clear
    bool crLf_ = false;       // the last key was a CR: a LF or NUL right after it is the same Enter (input() drops it)
    void                                           startSequence();
    void                                           nextStep();
    void                                           seqResume();  // a step's screen is done: its pause, or the next step
    bool                                           showFrontDoor();
    void                                           frontKey(char c);
    void                                           showPrelogon();
    void                                           showLoginScreen();
    std::string                                    loadScreen(const std::string& file) const;  // menu file, else the theme. Empty when it is not there.
    void                                           paintScreen(std::string art);
    bool                                           showArt(const std::string& file);  // false when that screen is not on the card
    std::string                                    menuName_ = "MAIN";
    bool                                           builtin_ = false;  // the menu files failed: the built-in menu this call
    bool                                           enterMenu(const std::string& name);
    AcsUser                                        menuWho() const;
    void                                           menuNote(const char* text);  // a short answer under a menu file's prompt
    std::string                                    menuValue(const std::string& k) const;  // a menu placeholder's value
    std::function<void(int, int, const std::string&)> send_;
    std::function<std::vector<std::string>()> callersGet_;
    std::function<DayStats()>                 dayStats_;
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
    // new user voting (nuv.h): the applicants this caller still has to see, by handle
    std::vector<std::string>  nuvList_;
    size_t                    nuvAt_ = 0;
    int                       nuvStep_ = 0;  // 0 asked at login, 1 an applicant on screen, 2 typing the comment, 3 a result on screen
    bool                      nuvYesPick_ = false;
    NuvRules                  nuvRules() const;
    bool                      nuvMayVoteNow();  // this caller may vote, and someone is waiting
    void                      startNuv(bool ask);  // ask: the login's "vote now?" first
    void                      showNuvApplicant();
    void                      nuvKey(char c);
    void                      endNuv();
    void                      rumorLine();          // one random rumor, over the main menu
    std::string               menuRumor() const;    // a menu file's ~~ RUN:RANDOMRUMOR: one rumor just above its prompt
    void                      extrasKey(char c);    // Rumors and BbsList states
    void                      showLastCallers();
    void                      showUserList();
    void                      showWhoOnline();
    void                      showSystemInfo();
    void                      showYourStats();
    void                      showAbout();
    bool                      available_ = true;
    uint32_t                  prevCall_ = 0;
    uint32_t                  callStart_ = 0;  // when they logged in (ms); the call's time limit counts from here
    bool                      timeWarned_ = false;
    int                       minutesLeft(uint32_t nowMs) const;  // -1 = no limit  // the caller's last call before this one (unix time)
    char                      mnAsk_ = 0;          // MultiNode: the command waiting for a node number / text
    int                       mnTo_ = 0, splitWith_ = 0, splitAsk_ = 0;  // the node asked, in split chat with, asking us
    int                       splitRow_[2] = {2, 14}, splitCol_[2] = {1, 1};  // split chat cursors: their half, mine
    std::vector<FileArea>     myAreas_, myMsgAreas_;  // the areas this caller's security level opens
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
    std::string               artText(const std::string& s) const;  // theme art: mci for the axis dialect, the code expander for the others
    void                      showMatrix();
    void                      drawBarItem(int i, bool lit);
    void                      matrixKey(char c);
    void                      o2Prompt(const char* menu);
    std::string               out_, line_, handle_;
    uint32_t                  lastInput_ = 0;
    uint32_t                  beganMs_ = 0;  // when the call began (begin): the pre-login limit counts from here
    bool                      everIn_ = false;  // logged in (or a local console) at least once this call
    size_t                    door_ = 0;
    std::vector<DoorEntry>      doorList_;  // what showDoors listed
    std::unique_ptr<ScrollList> doorBar_;   // the doors lightbar when the look has DOORLB.LST; null keeps the numbered window
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
    bool                                           wfcNotes_ = false;   // the axispc release notes are up; the next key returns to the WFC
    bool                                           wfcFromShell_ = false;
    // a login step's screen is done: the next step runs once the key (or poll) that ended it has returned,
    // never nested under it (the loop task's stack is a few KB)
    // the sysop menu's look preview: the setting ("theme" / "wfc") and the look on screen
    std::string                                    previewKey_, previewLook_;
    int                                            previewEsc_ = 0;  // 1 after ESC, 2 inside a CSI
    void                                           openPreview(const std::string& key, const std::string& look);
    void                                           drawPreview();
    void                                           previewKeyIn(char c);
    void                                           previewStep(int dir);
    void                                           closePreview(const std::string& note);
    bool                                           seqDue_ = false;
    int                                            seqDepth_ = 0, seqDepthPeak_ = 0;
    void                                           drainSeq();  // telnet: the WFC was picked from the sysop menu (Q/Esc go back there)
    void                                           leaveWfc();
    void                                           leftShell();
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
#if AXIS_NET_FILES
    NetFilesView                 netFiles_;
#endif
    std::unique_ptr<AxisNetMenu> net_;
    net::LinkInfo                link_;
    std::shared_ptr<bool>        liteNow_ = std::make_shared<bool>(false);  // lite(), for the art loaders (survives a move)
    // lite names this call found missing: asked once, not at every screen (a follower asks the main for each)
    std::shared_ptr<std::vector<std::string>> liteMiss_ = std::make_shared<std::vector<std::string>>();
    std::string                  linkShown_;
    uint32_t                     linkShownMs_ = 0;
    int                          sfLo_ = 0, sfHi_ = 0;  // the fastest and slowest LoRa tier this call (0: none yet)
    bool                         sawWifi_ = false;
};

// Sent to a caller who connects while the node is in use.
std::string busyScreen();

}  // namespace axis::bbs
