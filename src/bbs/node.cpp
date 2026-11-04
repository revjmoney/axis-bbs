// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#pragma GCC optimize("Os")  // menus and screens: size over speed (the door CPU and transfers stay -O2)
#include "bbs/node.h"

#include "bbs/login_art.h"
#include "bbs/password.h"
#include "bbs/theme.h"

#include <cstring>
#include <cctype>
#include <cstdio>

namespace axis::bbs {

namespace {
// ANSI colors (CP437 terminal)
const char* const CLS = "\x1b[0m\x1b[2J\x1b[H";
const char* const RST = "\x1b[0m";
const char* const DIM = "\x1b[0;37m";
const char* const HI = "\x1b[1;37m";
const char* const CYAN = "\x1b[0;33m";
const char* const AMBER = "\x1b[0;33m";
const char* const RED = "\x1b[0;33m";

std::string uptime(uint32_t sec);

// text padded (or cut) to exactly n columns
std::string fit(std::string t, size_t n) {
    if (t.size() > n) t.resize(n);
    return t + std::string(n - t.size(), ' ');
}

// One line of the two boxes: label + value in each, 38 columns inside every box.
std::string boxLine(const char* l1, const std::string& v1, const char* l2, const std::string& v2) {
    const char* const FRAME = "\x1b[0;33m";
    const char* const LABEL = "\x1b[0;37m";
    const char* const VALUE = "\x1b[1;37m";
    std::string       s = FRAME;
    s += "\xba ";
    s += LABEL + fit(l1, 9) + VALUE + fit(v1, 28) + FRAME + "\xba\xba ";
    s += LABEL + fit(l2, 9) + VALUE + fit(v2, 28) + FRAME + "\xba";  // 80 columns: the terminal wraps by itself
    return s;
}

std::string boxEdge(bool top) {
    std::string s = "\x1b[0;33m";
    if (top) {
        s += "\xc9\xcd\xb5 " + theme::fade("THE BOARD") + " " + theme::ORANGE + "\xc6" + std::string(24, '\xcd') + "\xbb";
        s += "\xc9\xcd\xb5 " + theme::fade("THE SYSTEM") + " " + theme::ORANGE + "\xc6" + std::string(23, '\xcd') + "\xbb";
    } else {
        s += "\xc8" + std::string(38, '\xcd') + "\xbc\xc8" + std::string(38, '\xcd') + "\xbc";
    }
    return s;
}

// The caller's first screen: the drippy logo (art/login.ans), then the board and the system.
static std::string welcomeBoxes(int node, const SysInfo& s, const BbsConfig& cfg);

std::string welcome(int node, const SysInfo& s, const BbsConfig& cfg) {
    std::string w = CLS;
    w += kLoginArt;  // 80-column rows: each one wraps to the next by itself
    return w + welcomeBoxes(node, s, cfg);
}

static std::string welcomeBoxes(int node, const SysInfo& s, const BbsConfig& cfg) {
    std::string w = RST;
    std::string who = std::to_string(s.nodesTotal ? s.nodes.size() : 1) + " of " + std::to_string(s.nodesTotal) + "  ";
    for (size_t i = 0; i < s.nodes.size(); i++)
        who += "[" + std::to_string(i + 1) + "] " + ((int)i + 1 == node ? "you" : s.nodes[i].empty() ? "waiting" : s.nodes[i]) + " ";
    std::string dial = s.host + "  " + s.ip;
    w += boxEdge(true);
    w += boxLine("BBS", cfg.bbsName, "software", std::string(kAxisSoftware) + " " + kAxisVersion);
    w += boxLine("sysop", cfg.sysopHandle.empty() ? "the sysop" : cfg.sysopHandle, "license", "GPL-3.0+ by Rev. J. Money");
    w += boxLine("from", cfg.location.empty() ? "the underground" : cfg.location, "hardware", s.hardware);
    w += boxLine("dial", dial, "storage", s.storage);
    w += boxLine("nodes", who, "up", uptime(s.uptimeSec) + "  calls today " + std::to_string(s.callsToday));
    w += boxEdge(false);
    w += "  \x1b[1;37mjmscnc.com\x1b[0;37m the JMSCNC store  \x1b[0;33m\xfa  \x1b[1;37mrotatocam.com\x1b[0;37m RotatoCAM  ";
    w += "\x1b[0;33m\xfa  \x1b[0;37mFTN " + cfg.ftnAddress + "\r\n";  // 72 columns: no wrap
    return w;
}

std::string uptime(uint32_t sec) {
    char b[32];
    unsigned d = sec / 86400, h = sec / 3600 % 24, m = sec / 60 % 60;
    if (d) std::snprintf(b, sizeof b, "%ud %uh %um", d, h, m);
    else std::snprintf(b, sizeof b, "%uh %um", h, m);
    return b;
}
}  // namespace

Node::Node(int nodeNumber, std::function<SysInfo()> info, BbsConfig cfg, FileLibrary* files)
    : node_(nodeNumber), info_(std::move(info)), cfg_(std::move(cfg)), files_(files) {}

void Node::setAccounts(UserStore* users, std::function<void()> save, uint32_t (*rng)(), std::function<uint32_t()> unixNow) {
    users_ = users;
    saveUsers_ = std::move(save);
    rng_ = rng;
    unixNow_ = std::move(unixNow);
}

void Node::loggedIn() {
    state_ = State::Menu;
    out_ += "\r\n\r\n   ";
    out_ += theme::TEXT;
    out_ += "Welcome back, ";
    out_ += theme::WHITE + handle_ + theme::TEXT + (sysop_ ? ". " + std::string(theme::ORANGE) + "Sysop access." : ".");
    menuOrLiners();
}

void Node::menuOrLiners() {
    if (!linersGet_) {
        showMainMenu();
        return;
    }
    std::vector<std::string> wall = linersGet_();
    out_ += theme::CLS;
    out_ += "\r\n" + theme::drip("ONELINERS");
    out_ += "   ";
    out_ += theme::fade("ONELINERS");
    out_ += std::string("\r\n   ") + theme::DGREY + std::string(60, '\xc4') + "\r\n";
    size_t from = wall.size() > 10 ? wall.size() - 10 : 0;  // the newest ten
    if (wall.empty()) out_ += std::string("   ") + theme::TEXT + "(nothing yet: be the first)\r\n";
    for (size_t i = from; i < wall.size(); i++) {
        const std::string& l = wall[i];
        size_t             c = l.find(": ");
        out_ += "   ";
        if (c != std::string::npos)
            out_ += std::string(theme::ORANGE) + l.substr(0, c) + theme::DGREY + ": " + theme::WHITE + l.substr(c + 2) + "\r\n";
        else
            out_ += std::string(theme::WHITE) + l + "\r\n";
    }
    out_ += std::string("\r\n   ") + theme::TEXT + "Add a oneliner? " + theme::ORANGE + "[y/N]" + theme::TEXT + ": ";
    state_ = State::OneLinerAsk;
}

void Node::showPending() {
    User* u = users_ ? users_->byId(userId_) : nullptr;
    out_ += theme::CLS;
    out_ += "\r\n   ";
    out_ += theme::DGREY;
    out_ += "[";
    out_ += theme::fade("ACCOUNT STATUS:") + " " + theme::ORANGE + "PENDING VERIFICATION" + theme::DGREY + "]\r\n\r\n";
    out_ += theme::window("PENDING", {{"handle", handle_},
                                      {"status", "waiting for the sysop"},
                                      {"answers", u ? std::to_string(u->answers.size()) + " questions answered" : ""},
                                      {"next", "call back later: the sysop reads every one"}});
    out_ += "\r\n   " + std::string(theme::TEXT) +
            "Until you're validated you can't reach the files or the doors.\r\n   Lamers, narcs and leeches need not wait.\r\n";
    pendingMenu();
}

void Node::pendingMenu() {
    out_ += "\r\n   " + theme::key("S", "tatus   ") + theme::key("C", "redits   ") + theme::key("G", "oodbye");
    out_ += "\r\n   " + theme::fade("Command") + theme::ORANGE + ": " + theme::WHITE;
}

void Node::begin(uint32_t nowMs) {
    lastInput_ = nowMs;
    if (cfg_.loginSplash) {  // Rev's RotatoCAM splash first; poll() plays it, a key skips it
        state_ = State::Splash;
        splash_.reset(new Splash(nowMs));
        out_ += splash_->step(nowMs);
        return;
    }
    showWelcome();
}

void Node::beginLocal(uint32_t nowMs) {
    lastInput_ = nowMs;
    handle_ = cfg_.sysopHandle.empty() ? "Sysop" : cfg_.sysopHandle;
    sysop_ = true;
    loggedIn();
}

void Node::showWelcome() {
    state_ = State::Login;
    if (direct_) {  // the art from flash, straight out; only the boxes are built here
        if (!out_.empty()) direct_(out_.data(), out_.size());
        out_.clear();
        direct_(CLS, std::strlen(CLS));
        direct_(kLoginArt, sizeof kLoginArt - 1);
        out_ += welcomeBoxes(node_, info_(), cfg_);
    } else {
        out_ += welcome(node_, info_(), cfg_);
    }
    prompt();
}

void Node::prompt() {
    if (state_ == State::Login) {
        out_ += "   " + theme::fade("Handle") + theme::ORANGE + ": " + theme::WHITE;
    } else if (state_ == State::Password) {
        out_ += "\r\n   " + theme::fade("Password") + theme::ORANGE + ": " + theme::WHITE;
    } else if (state_ == State::Menu) {
        out_ += "\r\n   " + theme::fade("Command") + theme::ORANGE + ": " + theme::WHITE;
    } else if (state_ == State::Doors) {
        out_ += DIM;
        out_ += "\r\n   Door number (Enter for the menu): ";
        out_ += HI;
    }
}

void Node::showDoors() {
    out_ += theme::CLS;
    out_ += "\r\n" + theme::drip("DOORS");
    std::vector<std::pair<std::string, std::string>> rows;
    for (size_t i = 0; i < cfg_.doors.size() && i < 9; i++) rows.push_back({"[" + std::to_string(i + 1) + "]", cfg_.doors[i].name});
    out_ += theme::window("Doors", rows);
    state_ = State::Doors;
    prompt();
}

void Node::resumeAfterDoor(DoorEnd how, uint32_t nowMs) {
    if (state_ != State::InDoor) return;
    lastInput_ = nowMs;
    state_ = State::Menu;
    if (how == DoorEnd::NotHere) {  // the board's own screen has no terminal for a door
        out_ += "\r\n\r\n   ";
        out_ += AMBER;
        out_ += "Doors and downloads need a real terminal: call in over telnet for those.";
        showMenu();
        return;
    }
    if (how == DoorEnd::Busy) {  // one door at a time per board: they never went in
        out_ += "\r\n\r\n   ";
        out_ += AMBER;
        out_ += "The door is busy: another caller on this board is playing one. Try again in a bit,";
        out_ += "\r\n   or call back and you may land on a free node.";
        showMenu();
        return;
    }
    out_ += RST;
    out_ += "\x1b[2J\x1b[H\r\n   ";
    out_ += DIM;
    out_ += "Back from " + door().name + ".";
    const char* why = how == DoorEnd::OutOfMemory ? "The door ran out of memory and was closed; your game is saved up to its last write."
                      : how == DoorEnd::TimeLimit ? "Time is up for this call in the door."
                      : how == DoorEnd::LoadError ? "That door couldn't be started (its files may be missing)."
                                                  : nullptr;
    if (why) {
        out_ += "\r\n   ";
        out_ += AMBER;
        out_ += why;
    }
    showMenu();
}

void Node::showMainMenu() {
    state_ = State::Menu;
    out_ += theme::CLS;
    out_ += "\r\n" + theme::drip("MENU");
    out_ += theme::window("COMMANDS", {{"[F]", "file areas: browse, info, download, upload"},
                                       {"[M]", "message areas: read, post, reply"},
                                       {"[D]", "doors"},
                                       {"[T]", "teleconference with every node"},
                                       {"[W]", "who's online"},
                                       {"[P]", "page the sysop"},
                                       {"[I] [C]", "system info, credits"},
                                       {"[G]", "goodbye"}});
    prompt();
}

void Node::showMenu() {
    out_ += "\r\n\r\n";
    out_ += DIM;
    out_ += "   ";
    out_ += theme::key("F", "iles   ") + theme::key("D", "oors   ") + theme::key("I", "nfo   ") + theme::key("C", "redits   ") +
            theme::key("G", "oodbye");
    out_ += "\r\n   ";
    out_ += theme::key("M", "essages   ") + theme::key("T", "eleconference   ") + theme::key("W", "ho's online   ") + theme::key("P", "age sysop");
    out_ += "\r\n";
    prompt();
}

void Node::input(const uint8_t* p, size_t n, uint32_t nowMs) {
    lastInput_ = nowMs;
    for (size_t i = 0; i < n && state_ != State::Done; i++) key((char)p[i]);
}

void Node::key(char c) {
    if (state_ == State::Splash) {  // any key: on to the login screen (at the next poll)
        splash_.reset();
        return;
    }
    if (state_ == State::Login) {
        if (c == '\r') {
            size_t a = line_.find_first_not_of(' '), b = line_.find_last_not_of(' ');
            std::string h = a == std::string::npos ? "" : line_.substr(a, b - a + 1);
            line_.clear();
            if (h.empty()) {
                out_ += "\r\n";
                prompt();
                return;
            }
            handle_ = h;
            if (!users_) {  // no accounts: in by handle
                state_ = State::Menu;
                out_ += "\r\n\r\n";
                out_ += DIM;
                out_ += "   Welcome, ";
                out_ += HI + handle_ + DIM + ".";
                menuOrLiners();
                return;
            }
            auto same = [](const std::string& a, const std::string& b) {
                if (a.size() != b.size()) return false;
                for (size_t i = 0; i < a.size(); i++)
                    if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i])) return false;
                return true;
            };
            wantSysop_ = !cfg_.sysopHash.empty() && same(h, cfg_.sysopHandle);
            User* u = wantSysop_ ? nullptr : users_->find(h);
            if (wantSysop_ || u) {
                if (u) {
                    userId_ = u->id;
                    handle_ = u->handle;
                } else {
                    handle_ = cfg_.sysopHandle;
                }
                tries_ = 0;
                state_ = State::Password;
                prompt();
                return;
            }
            state_ = State::ApplyAsk;
            out_ += "\r\n\r\n   " + std::string(theme::TEXT) + "Nobody here goes by " + theme::WHITE + handle_ + theme::TEXT + ".";
            out_ += "\r\n   " + theme::fade("Apply for an account") + " " + theme::key("Y", "/") + theme::key("N", "") + theme::ORANGE +
                    ": " + theme::WHITE;
        } else if (c == '\b' || c == 0x7f) {
            if (!line_.empty()) {
                line_.pop_back();
                out_ += "\b \b";
            }
        } else if ((unsigned char)c >= 0x20 && (unsigned char)c < 0x7f && line_.size() < kMaxHandle) {
            line_ += c;
            out_ += c;
        }
        return;
    }
    if (state_ == State::Password) {
        if (c == '\r') {
            std::string pw = line_;
            line_.clear();
            User* u = users_ ? users_->byId(userId_) : nullptr;
            bool  ok = wantSysop_ ? checkPassword(cfg_.sysopSalt, cfg_.sysopHash, pw) : (u && checkPassword(u->salt, u->hash, pw));
            if (!ok) {
                if (++tries_ >= 3) {
                    out_ += "\r\n\r\n   " + std::string(theme::ORANGE) + "Three strikes." + theme::TEXT + " Call back when you remember it.\r\n";
                    state_ = State::Done;
                    return;
                }
                out_ += "\r\n   " + std::string(theme::ORANGE) + "Wrong password.";
                prompt();
                return;
            }
            if (wantSysop_) {
                sysop_ = true;
                loggedIn();
                return;
            }
            u->calls++;
            if (unixNow_) u->lastCall = unixNow_();
            if (saveUsers_) saveUsers_();
            if (u->status == User::Status::Denied) {
                out_ += "\r\n\r\n   " + std::string(theme::TEXT) + "Your application was " + theme::ORANGE + "denied" + theme::TEXT +
                        ". This board isn't for you.\r\n";
                state_ = State::Done;
            } else if (u->status == User::Status::Pending) {
                state_ = State::Pending;
                showPending();
            } else {
                sysop_ = u->security >= kSysopSecurity;  // promoted by the sysop
                loggedIn();
            }
        } else if (c == '\b' || c == 0x7f) {
            if (!line_.empty()) {
                line_.pop_back();
                out_ += "\b \b";
            }
        } else if ((unsigned char)c >= 0x20 && (unsigned char)c < 0x7f && line_.size() < 40) {
            line_ += c;
            out_ += '*';
        }
        return;
    }
    if (state_ == State::ApplyAsk) {
        char k = (char)(c | 0x20);
        if (k == 'y') {
            out_ += "yes";
            state_ = State::Applying;
            app_.reset(new NewUserApp(handle_, out_, cfg_, rng_));
            app_->begin();
        } else if (k == 'n') {
            out_ += "no\r\n";
            state_ = State::Login;
            prompt();
        }
        return;
    }
    if (state_ == State::Applying) {
        auto r = app_->key(c);
        if (r == NewUserApp::Outcome::Running) return;
        if (r == NewUserApp::Outcome::Submitted) {
            User u = app_->user();
            if (unixNow_) u.created = u.lastCall = unixNow_();
            u.calls = 1;
            if (users_->find(u.handle)) {  // someone took the handle while they typed
                out_ += "\r\n   " + std::string(theme::ORANGE) + "That handle was just taken. Call back with another.\r\n";
                state_ = State::Done;
                app_.reset();
                return;
            }
            userId_ = users_->add(u).id;
            if (saveUsers_) saveUsers_();
            app_.reset();
            if (u.status == User::Status::Validated) {  // a machine owner or RotatoCAM Pro user: straight in
                out_ += theme::CLS;
                out_ += "\r\n   ";
                out_ += theme::DGREY;
                out_ += "[";
                out_ += theme::fade("ACCOUNT STATUS:") + " " + theme::ORANGE + "ACCESS GRANTED" + theme::DGREY + "]\r\n\r\n";
                out_ += theme::window("WELCOME TO THE UNDERGROUND",
                                      {{"handle", handle_}, {"access", "validated"}, {"why", "you make chips fly"}});
                loggedIn();
                return;
            }
            state_ = State::Pending;
            showPending();
        } else {
            app_.reset();
            state_ = State::Login;
            out_ += "\r\n";
            prompt();
        }
        return;
    }
    if (state_ == State::Pending) {
        switch (c | 0x20) {
            case 's':
                showPending();
                break;
            case 'c':
                out_ += "\r\n\r\n   " + std::string(theme::ORANGE) + kAxisSoftware + theme::TEXT + " by " + theme::WHITE + "Rev. J. Money" +
                        theme::TEXT + "   rotatocam.com   jmscnc.com\r\n";
                pendingMenu();
                break;
            case 'g':
                out_ += "\r\n\r\n   " + std::string(theme::ORANGE) + "Later. The sysop will get to you." + theme::RESET + "\r\n";
                state_ = State::Done;
                break;
            default:
                break;
        }
        return;
    }
    if (state_ == State::Doors) {
        size_t pick = c >= '1' && c <= '9' ? (size_t)(c - '1') : cfg_.doors.size();
        if (pick < cfg_.doors.size()) {
            door_ = pick;
            state_ = State::InDoor;
            out_ += c;
            out_ += "\r\n\r\n   ";
            out_ += DIM;
            out_ += "Opening " + door().name + "...";
            out_ += RST;
            out_ += "\r\n";
        } else {
            state_ = State::Menu;
            showMenu();
        }
        return;
    }
    if (state_ == State::Transfer) return;  // the line belongs to the transfer
    if (state_ == State::Files) {
        bool stay = browser_->key(c);
        if (stay && browser_->wantsTransfer()) state_ = State::Transfer;
        if (!stay) {
            browser_.reset();
            showMainMenu();
        }
        return;
    }
    if (state_ == State::Chat || state_ == State::Paging) {
        lineKey(c);
        return;
    }
    if (state_ == State::Messages) {
        if (msgs_ && msgs_->key(c)) return;
        msgs_.reset();
        if (User* u = users_ && !sysop_ ? users_->byId(userId_) : nullptr)  // where they got to, kept with the account
            if (saveUsers_) saveUsers_();
        showMainMenu();
        return;
    }
    if (state_ == State::OneLinerAsk) {
        if ((c | 0x20) == 'y') {
            out_ += std::string("y\r\n   ") + theme::ORANGE + "> " + theme::WHITE;
            line_.clear();
            state_ = State::OneLiner;
        } else {
            showMainMenu();
        }
        return;
    }
    if (state_ == State::OneLiner) {
        if (c == '\r') {
            if (!line_.empty() && linersAdd_) linersAdd_(handle_ + ": " + line_);
            line_.clear();
            showMainMenu();
        } else if (c == 8 || c == 127) {
            if (!line_.empty()) {
                line_.pop_back();
                out_ += "\b \b";
            }
        } else if ((unsigned char)c >= 0x20 && (unsigned char)c < 0x7f && line_.size() < 60) {
            line_ += c;
            out_ += c;
        }
        return;
    }
    if (state_ != State::Menu) return;
    switch (c | 0x20) {  // either case
        case 't':
        case 'p':
            if (!say_) {
                out_ += "\r\n\r\n   ";
                out_ += AMBER;
                out_ += "Chat is offline right now.";
                showMenu();
                break;
            }
            line_.clear();
            if ((c | 0x20) == 't') {
                out_ += theme::CLS;
                out_ += "\r\n" + theme::drip("CHAT") + "   ";
            } else {
                out_ += "\r\n\r\n   ";
            }
            if ((c | 0x20) == 't') {
                state_ = State::Chat;
                out_ += theme::fade("TELECONFERENCE");
                out_ += std::string(theme::TEXT) + "  every node hears you. " + theme::ORANGE + "/Q" + theme::TEXT + " leaves.\r\n\r\n";
                out_ += std::string(theme::ORANGE) + "> " + theme::WHITE;
            } else {
                state_ = State::Paging;
                out_ += std::string(theme::TEXT) + "Message for the sysop (Enter alone to cancel):\r\n   " + theme::ORANGE + "> " + theme::WHITE;
            }
            break;
        case 'w': {
            out_ += theme::CLS;
            out_ += "\r\n" + theme::drip("WHO") + "   ";
            out_ += theme::fade("WHO'S ONLINE");
            out_ += "\r\n";
            std::vector<WhoLine> all = who_ ? who_() : std::vector<WhoLine>{{node_, handle_, "Menu"}};
            for (const WhoLine& w : all) {
                char b[160];
                std::snprintf(b, sizeof b, "   %snode %-3d %s%-24s %s%s\r\n", theme::ORANGE, w.node, theme::WHITE,
                              w.handle.empty() ? "" : w.handle.c_str(), theme::TEXT,
                              w.handle.empty() ? "waiting for a caller" : w.activity.c_str());
                out_ += b;
            }
            showMenu();
            break;
        }
        case 'f':
            if (!files_) {
                out_ += "\r\n\r\n   ";
                out_ += AMBER;
                out_ += "The file areas are offline right now (no SD card).";
                showMenu();
                break;
            }
            if (User* u = users_ && !sysop_ ? users_->byId(userId_) : nullptr; u && !u->permFiles) {
                out_ += "\r\n\r\n   ";
                out_ += AMBER;
                out_ += "The file areas are closed to your account. Page the sysop if that's wrong.";
                showMenu();
                break;
            }
            state_ = State::Files;
            browser_.reset(new FileBrowser(cfg_.areas, *files_, out_));
            browser_->setDownloadGate([this]() -> std::string {
                User* u = users_ && !sysop_ ? users_->byId(userId_) : nullptr;
                if (!u || mayDownload(*u)) return "";
                return "Downloads open up once you've uploaded something: [U]pload first. Keep the scene alive.";
            });
            browser_->begin();
            break;
        case 'm': {
            User* u = users_ && !sysop_ ? users_->byId(userId_) : nullptr;
            if (!msgStore_ || (u && !u->permMessages)) {
                out_ += "\r\n\r\n   ";
                out_ += AMBER;
                out_ += msgStore_ ? "The message areas are closed to your account. Page the sysop if that's wrong."
                                  : "The message areas are offline right now.";
                showMenu();
                break;
            }
            state_ = State::Messages;
            msgs_.reset(new MessageReader(cfg_.msgAreas, *msgStore_, out_, handle_, u ? u->lastRead : lastRead_,
                                          [this] { return info_().timeText; }));
            msgs_->begin();
            break;
        }
        case 'd':
            if (User* u = users_ && !sysop_ ? users_->byId(userId_) : nullptr; u && !u->permDoors) {
                out_ += "\r\n\r\n   ";
                out_ += AMBER;
                out_ += "The doors are closed to your account. Page the sysop if that's wrong.";
                showMenu();
                break;
            }
            showDoors();
            break;
        case 'i': {
            SysInfo s = info_();
            char b[320];
            std::snprintf(b, sizeof b,
                          "\r\n\r\n   %sSystem%s\r\n   node      %d\r\n   address   %s  (%s)\r\n"
                          "   wifi      %s, %d dBm\r\n   uptime    %s\r\n   free RAM  %u bytes\r\n"
                          "   storage   %s\r\n",
                          HI, DIM, node_, s.ip.c_str(), s.host.c_str(), s.wifi.c_str(), s.rssi,
                          uptime(s.uptimeSec).c_str(), (unsigned)s.freeHeap, s.storage.c_str());
            out_ += b;
            showMenu();
            break;
        }
        case 'c':
            out_ += "\r\n\r\n   ";
            out_ += RED;
            out_ += kAxisSoftware;
            out_ += DIM;
            out_ += " by ";
            out_ += HI;
            out_ += "Rev. J. Money";
            out_ += DIM;
            out_ += "\r\n   rotatocam.com   jmscnc.com\r\n\r\n";
            out_ += "   Copyright (C) 2026 Rev. J. Money. Free software under the GNU GPL v3\r\n";
            out_ += "   or later, with an added term: keep this credit (see the README).\r\n";
            showMenu();
            break;
        case 'g':
            out_ += "\r\n\r\n   ";
            out_ += AMBER;
            out_ += "Goodbye from the underground. Keep your tools sharp.";
            out_ += RST;
            out_ += "\r\n";
            state_ = State::Done;
            break;
        default:
            prompt();
            break;
    }
}

void Node::lineKey(char c) {
    if (c == '\r') {
        std::string text = line_;
        line_.clear();
        out_ += "\r\n";
        if (state_ == State::Paging) {
            state_ = State::Menu;
            if (!text.empty()) {
                say_(text, true);
                out_ += std::string("   ") + theme::TEXT + "The sysop has been paged on every node's screen.";
            }
            showMenu();
        } else if (text == "/q" || text == "/Q") {
            showMainMenu();
        } else {
            if (!text.empty()) say_(text, false);  // it shows, with a fresh prompt, when it comes back from the main
            else out_ += std::string(theme::ORANGE) + "> " + theme::WHITE;
        }
    } else if (c == 8 || c == 127) {
        if (!line_.empty()) {
            line_.pop_back();
            out_ += "\b \b";
        }
    } else if ((unsigned char)c >= 0x20 && (unsigned char)c < 0x7f && line_.size() < 70) {
        line_ += c;
        out_ += c;
    }
}

void Node::chatIn(int node, const std::string& handle, const std::string& text, bool page) {
    if (state_ != State::Chat || page) return;  // pages are for the sysop's screens; others wait their turn
    out_ += "\r\x1b[K";  // over the half-typed line, then put it back below
    char b[48];
    std::snprintf(b, sizeof b, "%s[%d] ", theme::DGREY, node);
    out_ += b;
    out_ += std::string(theme::ORANGE) + handle + theme::DGREY + ": " + theme::WHITE + text + "\r\n";
    out_ += std::string(theme::ORANGE) + "> " + theme::WHITE + line_;
}

void Node::transferDone(bool ok, uint32_t bytes, uint32_t ms, const std::string& note, uint32_t nowMs) {
    if (state_ != State::Transfer) return;
    lastInput_ = nowMs;
    if (User* u = ok && users_ && !sysop_ ? users_->byId(userId_) : nullptr) {  // ratios: what they gave, what they took
        (browser_->transfer().upload ? u->uploads : u->downloads) += bytes;
        if (saveUsers_) saveUsers_();
    }
    browser_->transferDone(ok, bytes, ms, note, 0);
    state_ = State::Files;
}

std::string Node::activityText() const {
    switch (state_) {
        case State::Splash:
        case State::Login:
        case State::Password: return "logging in";
        case State::ApplyAsk:
        case State::Applying: return "new user application";
        case State::Pending: return "waiting to be validated";
        case State::OneLinerAsk:
        case State::OneLiner: return "oneliners";
        case State::Menu: return "main menu";
        case State::Messages: return msgs_ ? msgs_->where() : "message areas";
        case State::Doors: return "door menu";
        case State::InDoor: return "in " + door().name;
        case State::Files: return "files: " + (browser_ ? browser_->where() : std::string("area list"));
        case State::Transfer: return browser_ ? std::string(transfer().upload ? "uploading " : "downloading ") + transfer().name : "transfer";
        case State::Chat: return "teleconference";
        case State::Paging: return "paging the sysop";
        case State::Done: return "leaving";
    }
    return "online";
}

void Node::poll(uint32_t nowMs) {  // the idle timer covers the application and the pending screen too
    if (state_ == State::Splash) {
        if (splash_ && !splash_->done(nowMs)) {
            out_ += splash_->step(nowMs);
            return;
        }
        splash_.reset();
        lastInput_ = nowMs;
        showWelcome();
        return;
    }
    if (state_ != State::Done && state_ != State::InDoor && state_ != State::Transfer && nowMs - lastInput_ > (uint32_t)cfg_.idleMinutes * 60000u) {
        out_ += "\r\n\r\n   ";
        out_ += AMBER;
        out_ += "Logged off: idle too long.";
        out_ += RST;
        out_ += "\r\n";
        state_ = State::Done;
    }
}

std::string Node::takeOutput() {
    std::string s;
    s.swap(out_);
    return s;
}

std::string busyScreen() {
    std::string s = "\x1b[0m\r\n\x1b[0;33m   AXIS UNDERGROUND CNC\x1b[0;37m: all nodes are busy right now.\r\n";
    s += "   Try again in a few minutes.\x1b[0m\r\n";
    return s;
}

}  // namespace axis::bbs
