// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#pragma GCC optimize("Os")  // menus and screens: size over speed (the door CPU and transfers stay -O2)
#include "bbs/node.h"

#include "bbs/ansiterm.h"
#include "bbs/login_art.h"
#include "bbs/password.h"
#include "bbs/theme.h"

#include <algorithm>
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
#define CYAN theme::ORANGE  // these follow the theme's accent colour
#define AMBER theme::ORANGE
#define RED theme::ORANGE

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
    callStart_ = lastInput_;
    timeWarned_ = false;
    if (msgStore_) {  // new private mail: said under the main menu
        const User* u = users_ && !sysop_ ? users_->byId(userId_) : nullptr;
        auto        it = (u ? u->lastRead : lastRead_).find(EmailBox::kArea);
        int         n = EmailBox::countNew(*msgStore_, handle_, it == (u ? u->lastRead : lastRead_).end() ? 0 : it->second);
        if (n) notes_.push_back(std::string("   ") + theme::HOT + "*** You have " + std::to_string(n) + " new mail: [E] reads it." + theme::TEXT);
    }
    if (callersAdd_) {
        const User* u = users_ && !sysop_ ? users_->byId(userId_) : nullptr;
        callersAdd_(handle_ + "|" + std::to_string(node_) + "|" + info_().timeText + "|" + (u ? u->location : cfg_.location));
    }
    out_ += "\r\n\r\n   ";
    out_ += theme::TEXT;
    out_ += "Welcome back, ";
    out_ += theme::WHITE + handle_ + theme::TEXT + (sysop_ ? ". " + std::string(theme::ORANGE) + "Sysop access." : ".");
    menuOrLiners();
}

void Node::menuOrLiners() {
    if (state_ != State::Bulletin && art_) {  // the sysop's BULLETIN.ANS first, once per call
        std::string b = art_("BULLETIN.ANS");
        if (!b.empty()) {
            state_ = State::Bulletin;
            out_ += theme::CLS + mci(b) + "\r\n   " + theme::DGREY + "(press a key)" + theme::RESET;
            return;
        }
    }
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
    MciScope mciScope(*this);
    lastInput_ = nowMs;
    theme::use(theme::lookFrom(cfg_.theme));  // one look for the whole board: the setting decides
    if (cfg_.loginSplash) {  // Rev's RotatoCAM splash first; poll() plays it, a key skips it
        state_ = State::Splash;
        splash_.reset(new Splash(nowMs));
        out_ += splash_->step(nowMs);
        return;
    }
    showWelcome();
}

void Node::beginLocal(uint32_t nowMs) {
    MciScope mciScope(*this);
    lastInput_ = nowMs;
    theme::use(theme::lookFrom(cfg_.theme));
    handle_ = cfg_.sysopHandle.empty() ? "Sysop" : cfg_.sysopHandle;
    sysop_ = true;
    loggedIn();
}

void Node::beginLocalQuiet(uint32_t nowMs) {  // (the bulletin, oneliners and menu art cost the main RAM it hasn't got)
    lastInput_ = nowMs;
    callStart_ = nowMs;
    theme::use(theme::lookFrom(cfg_.theme));
    handle_ = cfg_.sysopHandle.empty() ? "Sysop" : cfg_.sysopHandle;
    sysop_ = true;
    state_ = State::Menu;
}

void Node::beginLocalMenu(uint32_t nowMs) {
    MciScope mciScope(*this);  // (the full login piles every screen into one string: more than the main can spare)
    beginLocalQuiet(nowMs);
    showMainMenu();
}

// While this node works, the theme's header screens get its codes (one node runs at a time; cleared after)
Node::MciScope::MciScope(Node& n) { theme::setMci([&n](const std::string& s) { return n.mci(s); }); }
Node::MciScope::~MciScope() { theme::setMci(nullptr); }

// ---- ViSiON/3-style menu files

namespace {
// ViSiON/3's RUN: functions onto AXIS's own commands (the built-in menu's keys); 0 = not on AXIS yet
char v3Command(const std::string& f) {
    static const struct { const char* name; char key; } table[] = {
        {"READMSGS", 'm'}, {"LISTMSGAR", 'm'}, {"SELECTMSGAREA", 'm'}, {"COMPOSEMSG", 'm'}, {"NEWSCAN", 's'},
        {"LISTFILES", 'f'}, {"SELECTFILEAREA", 'f'}, {"DOWNLOADFILE", 'f'}, {"UPLOADFILE", 'f'}, {"SEARCH_FILES", 'f'},
        {"FILE_NEWSCAN", 'f'}, {"CHAT", 'p'}, {"ONELINER", 'o'}, {"LISTUSERS", 'l'}, {"LASTCALLERS", 'w'},
        {"WHOISONLINE", 'w'}, {"RUMORSLIST", 'r'}, {"RANDOMRUMOR", 'r'}, {"BBSLIST", 'b'}, {"VOTE", 'v'},
        {"SHOWSTATS", 'i'}, {"SYSTEMSTATS", 'i'}, {"USERCONFIG", 'u'}, {"SENDPRIVMAIL", 'e'}, {"READPRIVMAIL", 'e'},
        {"MAINLOGOFF", 'g'}};
    for (const auto& t : table)
        if (f == t.name) return t.key;
    return 0;
}
}  // namespace

void Node::setMenus(MenuRunner::Loader load, MenuRunner::ArtPieces pieces) {
    menu_.reset();
    builtin_ = false;
    if (!load || load("MAIN.MNU").empty()) return;
    menu_.reset(new MenuRunner(
        load, [this](const char* d, size_t n) { out_.append(d, n); },
        [this](const std::string& k) { return menuValue(k); }));
    menu_->setArtPieces(std::move(pieces));
}

// a placeholder's value: "%XX" are AXIS's own codes (as theme art has always used them), the rest ViSiON/3's names
std::string Node::menuValue(const std::string& k) const {
    if (!k.empty() && k[0] == '%') {
        std::string v = mci(k);
        return v == k ? std::string() : v;
    }
    const User* u = users_ ? users_->byId(userId_) : nullptr;
    auto        num = [](long n) { return std::to_string(n); };
    if (k == "UH" || k == "ALIAS" || k == "HANDLE") return handle_;
    if (k == "NAME") return u ? u->realName : cfg_.sysopRealName;
    if (k == "LEVEL") return num(sysop_ ? kSysopSecurity : u ? u->security : 0);
    if (k == "GL") return u ? u->location : cfg_.location;
    if (k == "CALLS") return u ? num((long)u->calls) : std::string("0");
    if (k == "TL") {
        int left = minutesLeft(lastInput_);
        return left < 0 ? std::string("--") : num(left);
    }
    if (k == "NODE") return num(node_);
    if (k == "MN") return menuName_;
    if (k == "TIME" || k == "DATE") {
        std::string t = info_().timeText;  // "2026-11-05 10:42"
        size_t      sp = t.find(' ');
        if (k == "TIME") return sp == std::string::npos ? t : t.substr(sp + 1);
        return t.size() >= 10 ? t.substr(5, 2) + "/" + t.substr(8, 2) + "/" + t.substr(2, 2) : t;
    }
    if (k == "NEWUSERS") return "YES";  // AXIS always takes applications (an optional new user password guards them)
    if (k == "BN") return cfg_.bbsName;
    return "";  // |UN (a user note), |PV, areas and conferences: not on AXIS yet
}


AcsUser Node::menuWho() const {
    const User* u = users_ ? users_->byId(userId_) : nullptr;
    if (u) return acsFor(*u, sysop_);
    return AcsUser{sysop_ ? kSysopSecurity : 10, "FMD"};
}

// a menu by name; else the current menu's fallback; else MAIN. false: none of them would load
bool Node::enterMenu(const std::string& name) {
    std::string fallback = menu_->def().fallback;
    for (const std::string& n : {name, fallback, std::string("MAIN")}) {
        if (n.empty() || !menu_->enter(n, menuWho())) continue;
        menuName_ = n;
        for (int i = 0; i < 8 && menu_->key(0).kind != MenuAction::None; i++) {
        }  // auto-runs (sysop notices and such): not on AXIS yet
        return true;
    }
    return false;
}

bool Node::o2() const { return cfg_.theme == "oblivion" || cfg_.theme == "iniquity"; }  // (iniquity: the same layout, in green and grey)

std::string Node::mci(const std::string& s) const {
    std::string r;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '%' && i + 2 < s.size()) {
            std::string code = s.substr(i + 1, 2), v;
            bool        known = true;
            if (code == "UN") v = handle_;
            else if (code == "BN") v = cfg_.bbsName;
            else if (code == "SN") v = cfg_.sysopHandle;
            else if (code == "NN") v = std::to_string(node_);
            else if (code == "DT" || code == "TM") v = info_().timeText;
            else if (code == "VN") v = kAxisVersion;
            else known = false;
            if (known) {
                r += v;
                i += 2;
                continue;
            }
        }
        r += s[i];
    }
    return theme::pipes(r);
}

namespace {
struct O2Item {
    char        key;
    const char* label;
};
const O2Item kMatrix[] = {{'L', "Login"}, {'A', "Apply"}, {'W', "Who's Online"}, {'G', "Goodbye"}};
constexpr int kMatrixItems = 4, kMatrixRow = 11, kMatrixCol = 30;
}  // namespace

void Node::drawBarItem(int i, bool lit) {
    char b[96];
    if (barAt_[i][0]) {  // in the sysop's art, at its pulldown mark: short labels, six columns like the mark
        static const char* const shortName[] = {"login", "apply", "who", "bye"};
        std::snprintf(b, sizeof b, "\x1b[%d;%dH", barAt_[i][0], barAt_[i][1]);
        out_ += b;
        if (lit) std::snprintf(b, sizeof b, "\x1b[0;1;37;44m%-6s", shortName[i]);
        else std::snprintf(b, sizeof b, "%s%c%s%-5s", theme::KEY, shortName[i][0], theme::TEXT, shortName[i] + 1);
        out_ += b;
        out_ += theme::RESET;
        return;
    }
    std::snprintf(b, sizeof b, "\x1b[%d;%dH", kMatrixRow + i, kMatrixCol);
    out_ += b;
    if (lit) std::snprintf(b, sizeof b, "\x1b[0;1;37;44m  %c  %-16s", kMatrix[i].key, kMatrix[i].label);
    else std::snprintf(b, sizeof b, "%s[%s%c%s] %s%-16s ", theme::DGREY, theme::KEY, kMatrix[i].key, theme::DGREY, theme::TEXT, kMatrix[i].label);
    out_ += b;
    out_ += theme::RESET;
}

// The Oblivion look's front door: the board's title bar (or MATRIX.ANS from the card), then a
// lightbar over Login, Apply, Who's Online and Goodbye: arrows move it, Enter or a letter picks.
void Node::showMatrix() {
    state_ = State::Matrix;
    esc_ = 0;
    out_ += theme::CLS;
    std::string art = art_ ? art_("MATRIX.ANS") : "";
    for (auto& at : barAt_) at[0] = at[1] = 0;
    if (art.find("|01%01") != std::string::npos) {  // O/2 pulldown marks: find each on the drawn screen, then blank it
        std::unique_ptr<AnsiTerm> t(new AnsiTerm());
        t->feed(art);
        for (int r = 0; r < AnsiTerm::kRows; r++)
            for (int c = 0; c + 2 < AnsiTerm::kCols; c++) {
                char k = (char)t->cell(r, c + 2).ch;
                if (t->cell(r, c).ch == '|' && t->cell(r, c + 1).ch == '0' && k >= '1' && k <= '4') barAt_[k - '1'][0] = r + 1, barAt_[k - '1'][1] = c + 1;
            }
        for (char k = '1'; k <= '9'; k++) {
            std::string mark = std::string("|0") + k + "%0" + k;
            for (size_t at; (at = art.find(mark)) != std::string::npos;) art.replace(at, mark.size(), std::string(mark.size(), ' '));
        }
        if (!barAt_[3][0])
            for (auto& at : barAt_) at[0] = at[1] = 0;  // not all four marks: the stock layout after all
    }
    if (!art.empty()) {
        out_ += mci(art);
    } else {
        out_ += theme::titleBar(cfg_.bbsName.empty() ? "AXIS" : cfg_.bbsName);
        SysInfo s = info_();
        out_ += std::string("\r\n    ") + theme::DGREY + "node " + theme::WHITE + std::to_string(node_) + theme::DGREY + "  \xfa  " + theme::TEXT +
                kAxisSoftware + theme::DGREY + "  \xfa  " + theme::TEXT + cfg_.location + "\r\n    " + theme::DGREY + "dial " + theme::HOT +
                (cfg_.publicHost.empty() ? s.host : cfg_.publicHost) + theme::DGREY + "  \xfa  " + theme::TEXT + s.timeText + "\r\n";
    }
    for (int i = 0; i < kMatrixItems; i++) drawBarItem(i, i == bar_);
    char b[64];
    if (barAt_[0][0]) {
        out_ += "\x1b[24;1H";
        return;
    }
    std::snprintf(b, sizeof b, "\x1b[%d;%dH", kMatrixRow + kMatrixItems + 2, kMatrixCol - 6);
    out_ += b;
    out_ += std::string(theme::DGREY) + "arrows move, " + theme::HOT + "Enter" + theme::DGREY + " picks, or press the letter" + theme::RESET;
    std::snprintf(b, sizeof b, "\x1b[%d;1H", kMatrixRow + kMatrixItems + 4);
    out_ += b;
}

void Node::matrixKey(char c) {
    if (esc_ == 3) {  // after the who list: back to the matrix
        esc_ = 0;
        showMatrix();
        return;
    }
    if (esc_ == 1) {
        esc_ = c == '[' || c == 'O' ? 2 : 0;
        return;
    }
    int pick = -1;
    if (esc_ == 2) {
        esc_ = 0;
        int to = c == 'A' || c == 'D' ? bar_ - 1 : c == 'B' || c == 'C' ? bar_ + 1 : bar_;
        to = (to + kMatrixItems) % kMatrixItems;
        if (to != bar_) {
            drawBarItem(bar_, false);
            bar_ = to;
            drawBarItem(bar_, true);
            char b[24];
            std::snprintf(b, sizeof b, "\x1b[%d;1H", barAt_[0][0] ? 24 : kMatrixRow + kMatrixItems + 4);
            out_ += b;
        }
        return;
    }
    if (c == 0x1b) {
        esc_ = 1;
        return;
    }
    if (c == '\r') pick = bar_;
    for (int i = 0; i < kMatrixItems; i++)
        if ((c & ~0x20) == kMatrix[i].key) pick = i;
    if (pick < 0) return;
    bar_ = pick;
    switch (kMatrix[pick].key) {
        case 'L':
        case 'A':
            state_ = State::Login;
            if (kMatrix[pick].key == 'A')
                out_ += std::string("   ") + theme::TEXT + "Pick the handle for your application (one nobody here uses).\r\n";
            prompt();
            break;
        case 'W': {
            out_ += std::string("   ") + theme::fade("WHO'S ONLINE") + "\r\n";
            std::vector<WhoLine> all = who_ ? who_() : std::vector<WhoLine>{{node_, "", ""}};
            for (const WhoLine& w : all)
                out_ += std::string("   ") + theme::HOT + "node " + std::to_string(w.node) + "  " + theme::WHITE +
                        (w.handle.empty() ? std::string(theme::DGREY) + "waiting for a caller" : w.handle + theme::TEXT + "  " + w.activity) + "\r\n";
            out_ += std::string("   ") + theme::DGREY + "(press a key)" + theme::RESET;
            esc_ = 3;  // the next key redraws the matrix
            break;
        }
        default:
            out_ += std::string("   ") + theme::HOT + "Later." + theme::RESET + "\r\n";
            state_ = State::Done;
            break;
    }
}

void Node::o2Prompt(const char* menu) {  // ▄▀[Main Menu]▀▀[Node 1]▀ ▀ :
    std::string m;
    for (const char* p = menu; *p; p++) {
        bool cap = p == menu || p[-1] == ' ';
        m += cap ? theme::HOT : theme::ORANGE;
        m += *p;
    }
    out_ += std::string("\r\n  ") + theme::HOT + "\xdc" + theme::ORANGE + "\xdf" + theme::DGREY + "[" + m + theme::DGREY + "]" + theme::ORANGE +
            "\xdf\xdf" + theme::DGREY + "[" + theme::HOT + "N" + theme::ORANGE + "ode " + theme::WHITE + std::to_string(node_) + theme::DGREY + "]" +
            (minutesLeft(lastInput_) >= 0 ? std::string(theme::ORANGE) + "\xdf" + theme::DGREY + "[" + theme::WHITE + std::to_string(minutesLeft(lastInput_)) +
                                                theme::ORANGE + " left" + theme::DGREY + "]"
                                          : std::string()) +
            theme::ORANGE + "\xdf " + theme::DGREY + "\xdf " + theme::TEXT + ": " + theme::WHITE;
}

void Node::showWelcome() {
    if (o2()) {
        showMatrix();
        return;
    }
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
    } else if (state_ == State::Menu && menu_ && !builtin_) {
        showNotes();
        menu_->prompt();
    } else if (state_ == State::Menu) {
        showNotes();
        if (o2()) o2Prompt("Main Menu");
        else out_ += "\r\n   " + theme::fade("Command") + theme::ORANGE + ": " + theme::WHITE;
    } else if (state_ == State::Doors) {
        out_ += DIM;
        out_ += "\r\n   Door number (Enter for the menu): ";
        out_ += HI;
    }
}

void Node::openSysopMenu(const std::string& keys, int cols) {
    MciScope mciScope(*this);
    if (!sysop_ || !sysopRun_) return;
    state_ = State::SysopMenu;
    shell_.reset(new SysopShell(out_, sysopRun_, cols));
    shell_->begin();
    for (char k : keys) shell_->key(k);
}

void Node::tapRow(int row) {
    MciScope mciScope(*this);
    if (state_ == State::SysopMenu && shell_) {
        shell_->tapRow(row);
        if (!shell_->open()) {
            shell_.reset();
            showMainMenu();
        }
    }
}

std::string Node::editedMenu() const {  // the sysop's menu items in a window
    std::vector<std::pair<std::string, std::string>> rows;
    for (const MenuItem& m : cfg_.mainMenu)
        if (m.cmd != "sysop" || sysop_) rows.push_back({"[" + m.key + "]", m.name});
    return theme::window("COMMANDS", rows);
}

std::vector<DoorEntry> Node::doorChoices() const {
    std::vector<DoorEntry> v = cfg_.doors;
    if (!sysop_) return v;
    for (const DoorEntry& d : cfg_.doors) {
        std::string exe = d.exe;
        for (char& ch : exe) ch = (char)std::toupper((unsigned char)ch);
        if (exe == "LORD.EXE") v.push_back({d.id, d.name + " Setup (sysop)", "LORDCFG.EXE", "", "local"});
    }
    return v;
}

void Node::showDoors() {
    out_ += theme::CLS;
    out_ += "\r\n" + theme::drip("DOORS");
    std::vector<std::pair<std::string, std::string>> rows;
    doorList_ = doorChoices();
    for (size_t i = 0; i < doorList_.size() && i < 35; i++)  // 1 to 9, then A, B, C ...
        rows.push_back({"[" + std::string(1, i < 9 ? (char)('1' + i) : (char)('A' + i - 9)) + "]", doorList_[i].name});
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
    if (menu_ && !builtin_) {
        if (enterMenu(menuName_)) return;
        builtin_ = true;  // the menu files are broken: the built-in menu for this call
    }
    out_ += theme::CLS;
    if (o2()) {  // MAIN.ANS from the card, or the stock look: a title bar and a boxed two-column list
        std::string art = art_ ? art_("MAIN.ANS") : "";
        o2Keys_ = !art.empty();
        if (!art.empty()) {  // one copy at a time, and the output sized once (with room for the rumor): the main has no 10 KB block
            std::string m = mci(art);
            std::string().swap(art);
            out_.reserve(out_.size() + m.size() + 256);
            out_ += m;
        } else {
            if (!cfg_.mainMenu.empty()) {  // the sysop's edited menu
                out_ += theme::titleBar("Main Menu");
                out_ += editedMenu();
                rumorLine();
                prompt();
                return;
            }
            static const O2Item items[] = {{'M', "Message Bases"}, {'F', "File Vaults"},    {'D', "Doors"},         {'T', "Teleconference"},
                                            {'W', "Who's Online"},  {'O', "Oneliners"},      {'U', "User Settings"}, {'Y', "Yell for SysOp"},
                                            {'I', "System Info"},   {'C', "Credits"},        {'G', "Goodbye"},       {'N', "MultiNode"}};  // (R rumors, B BBS list, K top ten, E email, S scan work too)
            out_ += theme::titleBar("Main Menu");
            out_ += std::string("  ") + theme::ORANGE + "\xc9" + std::string(74, '\xcd') + "\xbb\r\n";
            char b[160];
            for (int r = 0; r < 6; r++) {
                const O2Item &a = items[r * 2], &z = items[r * 2 + 1];
                std::snprintf(b, sizeof b, "  %s\xba   %s[%s%c%s] %s%-28s%s[%s%c%s] %s%-28s       %s\xba\r\n", theme::ORANGE, theme::DGREY, theme::KEY, a.key,
                              theme::DGREY, theme::TEXT, a.label, theme::DGREY, theme::KEY, z.key, theme::DGREY, theme::TEXT, z.label, theme::ORANGE);
                out_ += b;
            }
            out_ += std::string("  ") + theme::ORANGE + "\xc8" + std::string(74, '\xcd') + "\xbc" + theme::TEXT;
        }
        rumorLine();
        prompt();
        return;
    }
    if (std::string art = art_ ? art_("MAIN.ANS") : ""; !art.empty()) {  // the axis look's own main menu screen
        out_ += mci(art);
        prompt();
        return;
    }
    if (!cfg_.mainMenu.empty()) {
        out_ += "\r\n" + theme::drip("MENU") + editedMenu();
        prompt();
        return;
    }
    out_ += "\r\n" + theme::drip("MENU");
    out_ += theme::window("COMMANDS", {{"[F]", "file areas: browse, info, download, upload"},
                                       {"[M]", "message areas: read, post, reply"},
                                       {"[D]", "doors"},
                                       {"[T]", "teleconference with every node"},
                                       {"[W]", "who's online"},
                                       {"[P]", "page the sysop"},
                                       {"[U] [O]", "your account and tagline, oneliners"},
                                       {"[N]", "multinode: notes, broadcasts, split chat"},
                                       {"[L]", "last callers"},
                                       {"[E] [S]", "private email, scan for new messages"},
                                       {"[R] [B] [K] [V]", "rumors, BBS list, top ten, voting booth"},
                                       {"[I] [C]", "system info, credits"},
                                       {"[G]", "goodbye"}});
    rumorLine();
    prompt();
}

void Node::showMenu() {
    if (menu_ && !builtin_) {
        showMainMenu();
        return;
    }
    if (o2()) {  // Oblivion style: just the prompt again; ? brings the menu back
        out_ += "\r\n";
        prompt();
        return;
    }
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
    MciScope mciScope(*this);
    lastInput_ = nowMs;
    if (holdUntil_ && (int32_t)(nowMs - holdUntil_) < 0) return;  // a wrong password: slow the guessing down
    holdUntil_ = 0;
    for (size_t i = 0; i < n && state_ != State::Done; i++) key((char)p[i]);
}

void Node::key(char c) {
    if (state_ == State::Splash) {  // any key: on to the login screen (at the next poll)
        splash_.reset();
        return;
    }
    if (state_ == State::Matrix) {
        matrixKey(c);
        return;
    }
    if (state_ == State::Bulletin) {  // any key: on to the oneliners or the menu
        menuOrLiners();
        if (state_ == State::Bulletin) showMainMenu();
        return;
    }
    if (state_ == State::SysopMenu) {
        if (shell_ && shell_->key(c)) return;
        shell_.reset();
        showMainMenu();
        return;
    }
    if (state_ == State::UserEdit) {
        userEditKey(c);
        return;
    }
    if (state_ == State::Voting) {
        votingKey(c);
        return;
    }
    if (state_ == State::Rumors || state_ == State::BbsList) {
        extrasKey(c);
        return;
    }
    if (splitAsk_ && (state_ == State::Menu || state_ == State::MultiNode)) {  // answering a split chat request
        char k = (char)(c | 0x20);
        if (k != 'y' && k != 'n') return;
        int with = splitAsk_;
        splitAsk_ = 0;
        if (send_) send_(k == 'y' ? kSplitYes : kSplitNo, with, std::string());
        if (k == 'y') startSplit(with, splitHandle_);
        else state_ == State::Menu ? prompt() : multiKey(0);
        return;
    }
    if (state_ == State::Split) {
        splitKey(c);
        return;
    }
    if (state_ == State::MultiNode) {
        if (mnAsk_ == 'W' && c != 0x1b) return;  // waiting on their answer
        multiKey(c);
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
                    failedLogin_ = true;
                    state_ = State::Done;
                    return;
                }
                holdUntil_ = lastInput_ + 2000u * (uint32_t)tries_;  // 2 s, then 4 s
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
            prevCall_ = u->lastCall;  // for [N]ew files: what landed since they were last here
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
        if (k == 'y' && !cfg_.nupHash.empty()) {  // an elite board: the new user password first
            out_ += "yes\r\n\r\n   " + theme::fade("New user password") + theme::ORANGE + ": " + theme::WHITE;
            state_ = State::NupAsk;
            tries_ = 0;
            line_.clear();
        } else if (k == 'y') {
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
    if (state_ == State::NupAsk) {
        if (c == '\r') {
            bool ok = checkPassword(cfg_.nupSalt, cfg_.nupHash, line_);
            line_.clear();
            if (ok) {
                state_ = State::Applying;
                app_.reset(new NewUserApp(handle_, out_, cfg_, rng_));
                app_->begin();
            } else if (++tries_ >= 3) {
                failedLogin_ = true;
                out_ += "\r\n\r\n   " + std::string(theme::ORANGE) + "Wrong." + theme::TEXT + " This board is private.\r\n";
                if (onEvent) onEvent("new user password: 3 wrong tries from " + handle_);
                state_ = State::Done;
            } else {
                holdUntil_ = lastInput_ + 2000u * (uint32_t)tries_;
                out_ += "\r\n   " + std::string(theme::ORANGE) + "Wrong." + theme::TEXT + " New user password" + theme::ORANGE + ": " + theme::WHITE;
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
        size_t pick = c >= '1' && c <= '9' ? (size_t)(c - '1') : (c | 0x20) >= 'a' && (c | 0x20) <= 'z' ? (size_t)(9 + (c | 0x20) - 'a') : doorList_.size();
        if (pick < doorList_.size()) {
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
    if (state_ == State::Email) {
        if (email_ && email_->key(c)) return;
        email_.reset();
        if (saveUsers_ && users_ && !sysop_) saveUsers_();  // what they read, kept with the account
        showMainMenu();
        return;
    }
    if (state_ == State::Settings) {
        if (settings_ && settings_->key(c)) return;
        settings_.reset();
        showMainMenu();
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
            if (!line_.empty() && linersAdd_ && !twit()) linersAdd_(handle_ + ": " + line_);  // (a twit's goes nowhere)
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
    bool fromMenus = false;
    if (menu_ && !builtin_) {
        MenuAction a = menu_->key(c, lastInput_);
        switch (a.kind) {
            case MenuAction::None:
                if (c == '?') menu_->redraw();
                return;
            case MenuAction::Goto:
                if (!enterMenu(a.arg)) {
                    builtin_ = true;
                    showMainMenu();
                }
                return;
            case MenuAction::Quit: state_ = State::Done; return;
            case MenuAction::Logoff: c = 'g'; break;
            case MenuAction::Door: {
                int pick = -1;
                for (size_t i = 0; i < cfg_.doors.size(); i++)
                    if (cfg_.doors[i].id == a.arg) pick = (int)i;
                const User* u = users_ && !sysop_ ? users_->byId(userId_) : nullptr;
                if (pick < 0 || (u && !u->permDoors)) {
                    out_ += std::string("\r\n   ") + AMBER + (pick < 0 ? "Coming soon to AXIS." : "The doors are closed to your account.") + theme::TEXT;
                    menu_->prompt();
                    return;
                }
                door_ = pick;
                state_ = State::InDoor;
                return;
            }
            case MenuAction::Run:
                c = v3Command(a.arg);
                if (!c) {
                    out_ += std::string("\r\n   ") + AMBER + "Coming soon to AXIS." + theme::TEXT;
                    menu_->prompt();
                    return;
                }
                break;
        }
        fromMenus = true;
    }
    if (fromMenus) {
    } else if (o2() && o2Keys_ && c != '?' && c != '*') {  // (? the menu, * the sysop's editor: the same in every look)  // the sysop's O/2 MAIN.ANS: stock Oblivion/2 letters onto ours
        static const char from[] = "abcefgklmorstvwxy+=./!-", to[] = "ubtepguwmdrifvlfuuncgsk";  // a account, c chat, e email, f feedback, l list users, o open doors, r rumors, t transfers, w recent callers ...
        const char*       hit = std::strchr(from, (char)(c | 0x20));
        if (!hit || !*hit) {
            out_ += std::string("\r\n   ") + AMBER + "Not on this board yet." + theme::TEXT;
            prompt();
            return;
        }
        c = to[hit - from];
    } else if (!cfg_.mainMenu.empty() && c != '?' && c != '*') {  // the sysop's edited menu: its hotkeys only
        char run = 0;
        for (const MenuItem& m : cfg_.mainMenu)
            if (!m.key.empty() && (m.key[0] | 0x20) == (c | 0x20)) run = menuCommandKey(m.cmd);
        if (!run) {
            out_ += std::string("\r\n   ") + AMBER + "Not on this menu." + theme::TEXT;
            prompt();
            return;
        }
        c = run;
    }
    switch (c | 0x20) {  // either case
        case 't':
        case 'p':
        case 'y':  // Oblivion's Yell for SysOp
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
            myAreas_ = openTo(cfg_.areas);
            browser_.reset(new FileBrowser(myAreas_, *files_, out_));
            browser_->setDownloadGate([this](uint32_t size) -> std::string {
                User* u = users_ && !sysop_ ? users_->byId(userId_) : nullptr;
                if (!u) return "";
                if (!mayDownload(*u)) return "Downloads open up once you've uploaded something: [U]pload first. Keep the scene alive.";
                return size ? ratioBlock(*u, size, cfg_.ratio, cfg_.freeKb) : std::string();
            });
            browser_->setNewSince(prevCall_ ? prevCall_ : (unixNow_ && unixNow_() > 7 * 86400 ? unixNow_() - 7 * 86400 : 0));
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
            myMsgAreas_ = openTo(cfg_.msgAreas);
            msgs_.reset(new MessageReader(myMsgAreas_, *msgStore_, out_, handle_, u ? u->lastRead : lastRead_,
                                          [this] { return info_().timeText; }));
            msgs_->setTagline(tagline());
            msgs_->setHidden([this](const std::string& from) { return hiddenWriter(from); });
            msgs_->begin();
            break;
        }
        case 'u': {
            User* u = users_ && !sysop_ ? users_->byId(userId_) : nullptr;
            if (!u) {
                out_ += "\r\n\r\n   ";
                out_ += AMBER;
                out_ += sysop_ && users_ ? "The sysop account lives in BBS SETUP on the board." : "Accounts are off on this board.";
                showMenu();
                break;
            }
            state_ = State::Settings;
            settings_.reset(new UserSettings(*u, out_, saveUsers_, rng_));
            settings_->begin();
            break;
        }
        case 'n':
            showMultiNode();
            break;
        case 'r':
            showRumors();
            break;
        case 'b':
            showBbsList();
            break;
        case 'k':
            showTopTen();
            break;
        case 'v':
            showVotes();
            break;
        case '*':  // the sysop menu (the old user editor without a way to run commands)
            if (sysop_ && sysopRun_) {
                state_ = State::SysopMenu;
                shell_.reset(new SysopShell(out_, sysopRun_));
                shell_->begin();
            } else if (sysop_ && users_) showUsers();
            else prompt();
            break;
        case 'l':
            showLastCallers();
            break;
        case 's': {  // the new scan across every message area this caller can see
            User* u = users_ && !sysop_ ? users_->byId(userId_) : nullptr;
            if (!msgStore_ || (u && !u->permMessages)) {
                out_ += "\r\n\r\n   ";
                out_ += AMBER;
                out_ += "The message areas aren't open to you right now.";
                showMenu();
                break;
            }
            myMsgAreas_ = openTo(cfg_.msgAreas);
            if (MessageReader::countNew(myMsgAreas_, *msgStore_, u ? u->lastRead : lastRead_) == 0) {
                out_ += std::string("\r\n\r\n   ") + theme::TEXT + "No new messages anywhere.";
                showMenu();
                break;
            }
            state_ = State::Messages;
            msgs_.reset(new MessageReader(myMsgAreas_, *msgStore_, out_, handle_, u ? u->lastRead : lastRead_,
                                          [this] { return info_().timeText; }));
            msgs_->setTagline(tagline());
            msgs_->setHidden([this](const std::string& from) { return hiddenWriter(from); });
            msgs_->beginScan();
            break;
        }
        case 'e': {
            if (!msgStore_) {
                out_ += "\r\n\r\n   ";
                out_ += AMBER;
                out_ += "Email is offline right now.";
                showMenu();
                break;
            }
            User* u = users_ && !sysop_ ? users_->byId(userId_) : nullptr;
            state_ = State::Email;
            email_.reset(new EmailBox(*msgStore_, out_, handle_, (u ? u->lastRead : lastRead_)[EmailBox::kArea],
                                      [this](const std::string& name, std::string& handle) {
                                          if (!users_) return false;
                                          if (User* f = users_->find(name)) {
                                              handle = f->handle;
                                              return true;
                                          }
                                          if (!cfg_.sysopHandle.empty() && name.size() == cfg_.sysopHandle.size()) {
                                              bool same = true;
                                              for (size_t i = 0; i < name.size(); i++) same = same && (name[i] | 0x20) == (cfg_.sysopHandle[i] | 0x20);
                                              if (same) handle = cfg_.sysopHandle;
                                              return same;
                                          }
                                          return false;
                                      },
                                      [this] { return info_().timeText; }, tagline()));
            email_->begin();
            break;
        }
        case 'o':
            if (linersGet_) menuOrLiners();
            else prompt();
            break;
        case '?':
            showMainMenu();
            break;
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
            if (!text.empty() && twit()) chatIn(node_, handle_, text, false);  // a twit only ever hears themself
            else if (!text.empty()) say_(text, false);  // it shows, with a fresh prompt, when it comes back from the main
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
    MciScope mciScope(*this);
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

// ---- MultiNode (Oblivion/2's & commands): notes, broadcasts, chat availability, force, split chat

void Node::showMultiNode() {
    state_ = State::MultiNode;
    mnAsk_ = 0;
    out_ += theme::CLS;
    out_ += "\r\n" + theme::drip("NODES");
    std::vector<std::pair<std::string, std::string>> rows = {{"[M]", "message one node"},
                                                             {"[B]", "broadcast to every node"},
                                                             {"[S]", "split-screen chat with a node"},
                                                             {"[A]", std::string("available for chat: ") + (available_ ? "yes" : "no")},
                                                             {"[W]", "who's online"},
                                                             {"[Q]", "back to the main menu"}};
    if (sysop_) rows.insert(rows.begin() + 3, {"[F]", "force a node into the teleconference"});
    out_ += theme::window("MULTINODE", rows);
    multiKey(0);
}

void Node::multiKey(char c) {
    auto promptHere = [this] {
        if (o2()) o2Prompt("MultiNode");
        else out_ += "\r\n   " + theme::fade("MultiNode") + theme::ORANGE + ": " + theme::WHITE;
    };
    if (!c) {
        promptHere();
        return;
    }
    if (mnAsk_) {  // typing a node number, then (for M and B) the text
        if (c == 8 || c == 127) {
            if (!line_.empty()) {
                line_.pop_back();
                out_ += "\b \b";
            }
            return;
        }
        if (c == 0x1b) {
            mnAsk_ = 0;
            line_.clear();
            promptHere();
            return;
        }
        if (c != '\r') {
            bool digits = !mnTo_ && mnAsk_ != 'B';
            if ((digits ? (c >= '0' && c <= '9' && line_.size() < 2) : ((unsigned char)c >= 0x20 && (unsigned char)c < 0x7f && line_.size() < 70))) {
                line_ += c;
                out_ += c;
            }
            return;
        }
        std::string got = line_;
        line_.clear();
        if (mnAsk_ == 'B' || mnTo_) {  // the text is in
            if (!got.empty() && send_) send_(mnAsk_ == 'B' ? kNodeBroadcast : kNodeNote, mnAsk_ == 'B' ? 0 : mnTo_, got);
            out_ += std::string("\r\n   ") + theme::TEXT + (got.empty() ? "Nothing sent." : mnAsk_ == 'B' ? "Sent to every node." : "Sent to node " + std::to_string(mnTo_) + ".");
            mnAsk_ = 0;
            mnTo_ = 0;
            promptHere();
            return;
        }
        int                  to = std::atoi(got.c_str());
        const WhoLine*       hit = nullptr;
        std::vector<WhoLine> all = who_ ? who_() : std::vector<WhoLine>{};
        for (const WhoLine& w : all)
            if (w.node == to) hit = &w;
        const char* why = to == node_ ? "That's you." : !hit || hit->handle.empty() ? "Nobody is on that node."
                        : mnAsk_ != 'F' && hit->activity.find("(DND)") != std::string::npos ? "That caller is not available right now."
                                                                                              : nullptr;
        if (why) {
            out_ += std::string("\r\n   ") + AMBER + why + theme::TEXT;
            mnAsk_ = 0;
            promptHere();
            return;
        }
        if (mnAsk_ == 'M') {
            mnTo_ = to;
            out_ += std::string("\r\n   ") + theme::TEXT + "Message for " + theme::WHITE + hit->handle + theme::TEXT + ": " + theme::WHITE;
            return;
        }
        if (mnAsk_ == 'F') {
            if (send_) send_(kNodeForce, to, "");
            out_ += std::string("\r\n   ") + theme::TEXT + hit->handle + " is pulled into the teleconference.";
            mnAsk_ = 0;
            promptHere();
            return;
        }
        // S: ask them; their answer comes back through nodeIn
        mnAsk_ = 0;
        splitAsk_ = 0;
        mnTo_ = to;
        if (send_) send_(kSplitAsk, to, "");
        out_ += std::string("\r\n   ") + theme::TEXT + "Waiting for node " + std::to_string(to) + " to answer... (Esc gives up)";
        mnAsk_ = 'W';
        return;
    }
    switch (c | 0x20) {
        case 'm':
        case 's':
        case 'f':
            if ((c | 0x20) == 'f' && !sysop_) {
                out_ += std::string("\r\n   ") + AMBER + "Only the sysop can pull a caller into chat." + theme::TEXT;
                promptHere();
                return;
            }
            mnAsk_ = (char)(c & ~0x20);
            mnTo_ = 0;
            line_.clear();
            out_ += std::string("\r\n   ") + theme::TEXT + "Node number: " + theme::WHITE;
            break;
        case 'b':
            mnAsk_ = 'B';
            line_.clear();
            out_ += std::string("\r\n   ") + theme::TEXT + "Message for every node: " + theme::WHITE;
            break;
        case 'a':
            available_ = !available_;
            out_ += std::string("\r\n   ") + theme::TEXT +
                    (available_ ? "You're available for chat and notes again." : "You're not available: no notes, broadcasts or chat requests.");
            promptHere();
            break;
        case 'w': {
            std::vector<WhoLine> all = who_ ? who_() : std::vector<WhoLine>{{node_, handle_, "Menu"}};
            out_ += "\r\n";
            for (const WhoLine& w : all)
                out_ += std::string("\r\n   ") + theme::HOT + "node " + std::to_string(w.node) + "  " + theme::WHITE +
                        (w.handle.empty() ? std::string(theme::DGREY) + "waiting for a caller" : w.handle + theme::TEXT + "  " + w.activity);
            promptHere();
            break;
        }
        case 'q':
            showMainMenu();
            break;
        default:
            break;
    }
}

void Node::showLastCallers() {
    std::vector<std::string> all = callersGet_ ? callersGet_() : std::vector<std::string>{};
    out_ += "\r\n\r\n   " + theme::fade("LAST CALLERS") + "\r\n";
    out_ += std::string("   ") + theme::DGREY + std::string(70, '\xc4') + "\r\n";
    if (all.empty()) out_ += std::string("   ") + theme::TEXT + "(nobody yet)\r\n";
    int shown = 0;
    for (size_t i = all.size(); i-- > 0 && shown < 12; shown++) {  // newest first
        std::string f[4];
        size_t      pos = 0;
        for (int k = 0; k < 4; k++) {
            size_t bar = k < 3 ? all[i].find('|', pos) : std::string::npos;
            f[k] = all[i].substr(pos, bar == std::string::npos ? std::string::npos : bar - pos);
            if (bar == std::string::npos) break;
            pos = bar + 1;
        }
        char b[200];
        std::snprintf(b, sizeof b, "   %s%-22.22s %snode %-3s %s%-17.17s %s%.24s\r\n", theme::WHITE, f[0].c_str(), theme::DGREY, f[1].c_str(), theme::TEXT,
                      f[2].c_str(), theme::ORANGE, f[3].c_str());
        out_ += b;
    }
    showMenu();
}

// ---- Oblivion/Iniquity extras: rumors, the BBS list, top ten

void Node::rumorLine() {
    std::vector<std::string> all = listGet_ ? listGet_("rumor") : std::vector<std::string>{};
    if (all.empty()) return;
    const std::string& r = all[(lastInput_ / 1000 + all.size() * 7 + node_) % all.size()];
    out_ += std::string("\r\n  ") + theme::DGREY + "rumor: " + theme::HOT + r.substr(0, 68) + theme::TEXT;
}

void Node::showRumors() {
    state_ = State::Rumors;
    step_ = 0;
    out_ += "\r\n\r\n   " + theme::fade("RUMORS") + theme::TEXT + "  anonymous, like the old days\r\n   " + theme::key("A", "dd one   ") +
            theme::key("L", "ist   ") + theme::key("Q", "uit") + theme::ORANGE + ": " + theme::WHITE;
}

void Node::showBbsList() {
    state_ = State::BbsList;
    step_ = 0;
    std::vector<std::string> all = listGet_ ? listGet_("bbslist") : std::vector<std::string>{};
    out_ += theme::CLS;
    out_ += "\r\n" + theme::drip("BBS LIST");
    out_ += std::string("   ") + theme::DGREY + std::string(70, '\xc4') + "\r\n";
    if (all.empty()) out_ += std::string("   ") + theme::TEXT + "(none yet: add the boards you call)\r\n";
    char b[200];
    for (const std::string& l : all) {
        size_t      a = l.find('|'), z = a == std::string::npos ? a : l.find('|', a + 1);
        std::string name = l.substr(0, a), addr = a == std::string::npos ? "" : l.substr(a + 1, z == std::string::npos ? z : z - a - 1),
                    about = z == std::string::npos ? "" : l.substr(z + 1);
        std::snprintf(b, sizeof b, "   %s%-22.22s %s%-30.30s %s%.20s\r\n", theme::WHITE, name.c_str(), theme::HOT, addr.c_str(), theme::TEXT, about.c_str());
        out_ += b;
    }
    out_ += "\r\n   " + theme::key("A", "dd a board   ") + theme::key("Q", "uit") + theme::ORANGE + ": " + theme::WHITE;
}

void Node::extrasKey(char c) {
    bool rumors = state_ == State::Rumors;
    if (step_ == 0) {  // the little menu
        switch (c | 0x20) {
            case 'a':
                step_ = 1;
                line_.clear();
                entry_.clear();
                out_ += std::string("\r\n   ") + theme::TEXT + (rumors ? "Your rumor (nobody sees who wrote it):" : "Board name:") + "\r\n   " +
                        theme::ORANGE + "> " + theme::WHITE;
                break;
            case 'l':
                if (rumors) {
                    std::vector<std::string> all = listGet_ ? listGet_("rumors") : std::vector<std::string>{};
                    out_ += "\r\n";
                    size_t from = all.size() > 15 ? all.size() - 15 : 0;
                    for (size_t i = from; i < all.size(); i++) out_ += std::string("   ") + theme::HOT + "\xfa " + theme::WHITE + all[i] + "\r\n";
                    if (all.empty()) out_ += std::string("   ") + theme::TEXT + "(no rumors yet)\r\n";
                    showRumors();
                }
                break;
            case 'q': showMainMenu(); break;
            default: break;
        }
        return;
    }
    if (c == 8 || c == 127) {
        if (!line_.empty()) {
            line_.pop_back();
            out_ += "\b \b";
        }
        return;
    }
    if (c != '\r') {
        if ((unsigned char)c >= 0x20 && (unsigned char)c < 0x7f && c != '|' && line_.size() < (size_t)(rumors ? 70 : 40)) {
            line_ += c;
            out_ += c;
        }
        return;
    }
    std::string got = line_;
    line_.clear();
    if (got.empty()) {  // Enter alone: never mind
        step_ = 0;
        rumors ? showRumors() : showBbsList();
        return;
    }
    if (rumors) {
        if (listAdd_ && !twit()) listAdd_("rumors", got);
        out_ += std::string("\r\n   ") + theme::TEXT + "The rumor is out there now.";
        showRumors();
        return;
    }
    entry_ += (step_ > 1 ? "|" : "") + got;
    static const char* const asks[] = {"", "Address (host:port):", "About it (a few words):"};
    if (++step_ <= 3) {
        out_ += std::string("\r\n   ") + theme::TEXT + asks[step_ - 1] + "\r\n   " + theme::ORANGE + "> " + theme::WHITE;
        return;
    }
    if (listAdd_) listAdd_("bbslist", entry_);
    showBbsList();
}

// The voting booth: "votes" holds "question|choice|choice...", "ballots" holds "question#|handle|choice#".
namespace {
std::vector<std::string> fields(const std::string& l) {
    std::vector<std::string> f;
    for (size_t pos = 0;;) {
        size_t bar = l.find('|', pos);
        f.push_back(l.substr(pos, bar == std::string::npos ? std::string::npos : bar - pos));
        if (bar == std::string::npos) return f;
        pos = bar + 1;
    }
}
bool sameName(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); i++)
        if ((a[i] | 0x20) != (b[i] | 0x20)) return false;
    return true;
}
}  // namespace

void Node::showVotes() {
    state_ = State::Voting;
    step_ = 0;
    voteQ_ = 0;
    std::vector<std::string> qs = listGet_ ? listGet_("votes") : std::vector<std::string>{};
    out_ += "\r\n\r\n   " + theme::fade("VOTING BOOTH") + "\r\n";
    if (qs.empty()) out_ += std::string("   ") + theme::TEXT + "(no questions yet)\r\n";
    for (size_t i = 0; i < qs.size() && i < 9; i++)
        out_ += std::string("   ") + theme::KEY + std::to_string(i + 1) + theme::DGREY + ". " + theme::WHITE + fields(qs[i])[0] + "\r\n";
    out_ += "\r\n   " + theme::key("#", " look and vote   ") + theme::key("A", "sk a question   ") + theme::key("Q", "uit") + theme::ORANGE + ": " + theme::WHITE;
}

void Node::showQuestion(int q) {
    std::vector<std::string> qs = listGet_ ? listGet_("votes") : std::vector<std::string>{};
    if (q < 1 || q > (int)qs.size()) {
        showVotes();
        return;
    }
    std::vector<std::string> f = fields(qs[(size_t)q - 1]);
    std::vector<int>         count(f.size(), 0);
    int                      mine = 0, total = 0;
    for (const std::string& b : listGet_ ? listGet_("ballots") : std::vector<std::string>{}) {
        std::vector<std::string> v = fields(b);
        if (v.size() < 3 || std::atoi(v[0].c_str()) != q) continue;
        int c = std::atoi(v[2].c_str());
        if (c < 1 || c >= (int)f.size()) continue;
        count[(size_t)c]++;
        total++;
        if (sameName(v[1], handle_)) mine = c;
    }
    voteQ_ = q;
    step_ = 1;
    out_ += "\r\n\r\n   " + std::string(theme::WHITE) + f[0] + "\r\n";
    char b[160];
    for (size_t i = 1; i < f.size(); i++) {
        int bar = total ? count[i] * 30 / total : 0;
        std::snprintf(b, sizeof b, "   %s%u%s. %s%-24.24s %s%s%s %d\r\n", theme::KEY, (unsigned)i, theme::DGREY, theme::TEXT, f[i].c_str(), theme::ORANGE,
                      std::string((size_t)bar, '\xdb').c_str(), theme::DGREY, count[i]);
        out_ += b;
    }
    out_ += std::string("   ") + theme::TEXT + std::to_string(total) + (total == 1 ? " vote" : " votes");
    if (mine) out_ += std::string(", you voted ") + theme::WHITE + f[(size_t)mine] + theme::TEXT + ". (any key)";
    else out_ += std::string(". Your vote ") + theme::ORANGE + "[1-" + std::to_string(f.size() - 1) + "]" + theme::TEXT + ", Enter to skip: " + theme::WHITE;
    if (mine) step_ = 2;  // seen: any key goes back
}

void Node::votingKey(char c) {
    if (step_ == 0) {  // the list
        if (c >= '1' && c <= '9') showQuestion(c - '0');
        else if ((c | 0x20) == 'q') showMainMenu();
        else if ((c | 0x20) == 'a') {
            if (!sysop_) {
                out_ += std::string("\r\n   ") + AMBER + "Only the sysop puts questions in the booth." + theme::TEXT;
                showVotes();
                return;
            }
            step_ = 10;
            entry_.clear();
            line_.clear();
            out_ += std::string("\r\n   ") + theme::TEXT + "The question:\r\n   " + theme::ORANGE + "> " + theme::WHITE;
        }
        return;
    }
    if (step_ == 1) {  // their vote
        std::vector<std::string> f = fields((listGet_ ? listGet_("votes") : std::vector<std::string>{"?"})[(size_t)voteQ_ - 1]);
        int                      pick = c - '0';
        if (pick >= 1 && pick < (int)f.size() && listAdd_) {
            listAdd_("ballots", std::to_string(voteQ_) + "|" + handle_ + "|" + std::to_string(pick));
            out_ += std::string("\r\n   ") + theme::TEXT + "Counted.";
        }
        showVotes();
        return;
    }
    if (step_ == 2) {  // looked at a question they voted on
        showVotes();
        return;
    }
    // step 10: the question, then 11+: choices until an empty line (6 at most)
    if (c == 8 || c == 127) {
        if (!line_.empty()) {
            line_.pop_back();
            out_ += "\b \b";
        }
        return;
    }
    if (c != '\r') {
        if ((unsigned char)c >= 0x20 && (unsigned char)c < 0x7f && c != '|' && line_.size() < 50) {
            line_ += c;
            out_ += c;
        }
        return;
    }
    std::string got = line_;
    line_.clear();
    if (!got.empty()) entry_ += (step_ > 10 ? "|" : "") + got;
    if ((got.empty() && step_ >= 12) || step_ >= 16) {  // a question with two choices or more
        if (listAdd_) listAdd_("votes", entry_);
        showVotes();
        return;
    }
    if (got.empty()) {
        if (step_ == 10) showVotes();
        else out_ += std::string("\r\n   ") + theme::TEXT + "At least two choices.\r\n   " + theme::ORANGE + "> " + theme::WHITE;
        return;
    }
    step_++;
    out_ += std::string("\r\n   ") + theme::TEXT + "Choice " + std::to_string(step_ - 10) + " (Enter alone when done):\r\n   " + theme::ORANGE + "> " + theme::WHITE;
}

void Node::showTopTen() {
    if (!users_) {
        out_ += std::string("\r\n\r\n   ") + AMBER + "Top ten needs accounts on this board." + theme::TEXT;
        showMenu();
        return;
    }
    std::vector<const User*> all;
    for (const User& u : users_->all())
        if (u.status == User::Status::Validated) all.push_back(&u);
    out_ += theme::CLS;
    out_ += "\r\n" + theme::drip("TOP TEN");
    char b[160];
    auto board = [&](const char* title, uint64_t (*value)(const User&), const char* unit) {
        std::vector<const User*> v = all;
        std::sort(v.begin(), v.end(), [&](const User* a, const User* z) { return value(*a) > value(*z); });
        out_ += std::string("   ") + theme::fade(title) + "\r\n";
        for (size_t i = 0; i < v.size() && i < 10 && value(*v[i]) > 0; i++) {
            std::snprintf(b, sizeof b, "   %s%2u. %s%-24.24s %s%llu %s\r\n", theme::KEY, (unsigned)(i + 1), theme::WHITE, v[i]->handle.c_str(), theme::TEXT,
                          (unsigned long long)value(*v[i]), unit);
            out_ += b;
        }
        out_ += "\r\n";
    };
    board("MOST CALLS", [](const User& u) -> uint64_t { return u.calls; }, "calls");
    board("TOP UPLOADERS", [](const User& u) -> uint64_t { return u.uploads / 1024; }, "KB");
    showMenu();
}

void Node::showNotes() {
    for (const std::string& n : notes_) out_ += "\r\n" + n;
    notes_.clear();
}

void Node::nodeIn(int kind, int from, const std::string& handle, const std::string& text) {
    bool screenFree = state_ == State::Menu || state_ == State::MultiNode || state_ == State::Chat || state_ == State::Settings ||
                      state_ == State::Files || state_ == State::Messages || state_ == State::Doors || state_ == State::Paging;
    if (kind == kNodeNote || kind == kNodeBroadcast) {
        if (!available_ || state_ == State::Split) return;
        std::string n = std::string("   ") + theme::HOT + "*** " + (kind == kNodeBroadcast ? "to every node" : "note") + " from " + theme::WHITE +
                        handle + theme::DGREY + " (node " + std::to_string(from) + ")" + theme::HOT + ": " + theme::WHITE + text + theme::TEXT;
        if (!screenFree) {
            notes_.push_back(n);
            return;
        }
        out_ += "\r\n" + n + "\r\n";
        if (state_ == State::Chat) out_ += std::string(theme::ORANGE) + "> " + theme::WHITE + line_;
        else if (state_ == State::Menu || state_ == State::MultiNode) state_ == State::Menu ? prompt() : multiKey(0);
    } else if (kind == kNodeForce) {
        if (state_ != State::Menu && state_ != State::MultiNode && state_ != State::Chat) {
            notes_.push_back(std::string("   ") + theme::HOT + "*** the sysop wants you in the teleconference ([T])" + theme::TEXT);
            return;
        }
        if (state_ == State::Chat) return;
        line_.clear();
        state_ = State::Chat;
        out_ += theme::CLS;
        out_ += "\r\n" + theme::drip("CHAT") + "   " + theme::fade("TELECONFERENCE");
        out_ += std::string(theme::TEXT) + "  the sysop pulled you in. " + theme::ORANGE + "/Q" + theme::TEXT + " leaves.\r\n\r\n";
        out_ += std::string(theme::ORANGE) + "> " + theme::WHITE;
    } else if (kind == kSplitAsk) {
        if (!available_ || splitWith_ || splitAsk_ || !(state_ == State::Menu || state_ == State::MultiNode)) {
            if (send_) send_(kSplitNo, from, "");
            return;
        }
        splitAsk_ = from;
        splitHandle_ = handle;
        mnAsk_ = 0;
        out_ += std::string("\r\n\r\n   ") + theme::HOT + "*** " + theme::WHITE + handle + theme::DGREY + " (node " + std::to_string(from) + ")" +
                theme::TEXT + " wants a split-screen chat. Accept? " + theme::key("Y", "/") + theme::key("N", "") + theme::ORANGE + ": " + theme::WHITE;
    } else if (kind == kSplitYes && mnAsk_ == 'W' && from == mnTo_) {
        mnAsk_ = 0;
        startSplit(from, handle);
    } else if (kind == kSplitNo && mnAsk_ == 'W' && from == mnTo_) {
        mnAsk_ = 0;
        out_ += std::string("\r\n   ") + AMBER + "Node " + std::to_string(from) + " declined (or isn't free)." + theme::TEXT;
        multiKey(0);
    } else if (kind == kSplitKey && state_ == State::Split && from == splitWith_) {
        for (char k : text) splitPut(0, k);
        char b[24];
        std::snprintf(b, sizeof b, "\x1b[%d;%dH", splitRow_[1], splitCol_[1]);  // back to my cursor
        out_ += b;
    } else if (kind == kSplitEnd && state_ == State::Split && from == splitWith_) {
        splitWith_ = 0;
        showMainMenu();
    }
}

// Split-screen chat: their keystrokes in the top half (rows 2 to 12), mine in the bottom (14 to 24),
// each half wiping itself when it fills. Every key goes across as it's typed, like Oblivion/2's.
void Node::startSplit(int with, const std::string& them) {
    state_ = State::Split;
    splitWith_ = with;
    splitAsk_ = 0;
    splitRow_[0] = 2, splitCol_[0] = 1, splitRow_[1] = 14, splitCol_[1] = 1;
    out_ += theme::CLS;
    out_ += std::string("\x1b[1;1H") + theme::ORANGE + std::string(4, '\xc4') + theme::DGREY + "[ " + theme::WHITE + them + theme::DGREY + " ]" +
            theme::ORANGE + std::string(20, '\xc4') + theme::DGREY + " split screen chat " + theme::ORANGE + std::string(10, '\xc4') +
            theme::DGREY + " Esc ends " + theme::ORANGE + std::string(6, '\xc4');
    out_ += std::string("\x1b[13;1H") + theme::ORANGE + std::string(4, '\xc4') + theme::DGREY + "[ " + theme::WHITE + handle_ + theme::DGREY +
            " ]" + theme::ORANGE + std::string(60, '\xc4');
    out_ += std::string("\x1b[14;1H") + theme::WHITE;
}

void Node::splitPut(int half, char c) {
    int &row = splitRow_[half], &col = splitCol_[half], top = half ? 14 : 2, bottom = half ? 24 : 12;
    char b[24];
    auto wipe = [&] {
        for (int r = top; r <= bottom; r++) {
            std::snprintf(b, sizeof b, "\x1b[%d;1H\x1b[K", r);
            out_ += b;
        }
        row = top, col = 1;
    };
    if (c == '\r' || col > 79) {
        row++, col = 1;
        if (row > bottom) wipe();
        if (c == '\r') return;
    }
    if (c == 8 || c == 127) {
        if (col > 1) {
            col--;
            std::snprintf(b, sizeof b, "\x1b[%d;%dH ", row, col);
            out_ += b;
        }
        return;
    }
    if ((unsigned char)c < 0x20) return;
    std::snprintf(b, sizeof b, "\x1b[%d;%dH%s", row, col, half ? theme::WHITE : theme::HOT);
    out_ += b;
    out_ += c;
    col++;
}

void Node::splitKey(char c) {
    if (c == 0x1b) {
        if (send_) send_(kSplitEnd, splitWith_, "");
        splitWith_ = 0;
        showMainMenu();
        return;
    }
    if (c == '\n') return;
    splitPut(1, c);
    if (send_) send_(kSplitKey, splitWith_, std::string(1, c));
}

int Node::securityLevel() const {
    if (sysop_) return 255;
    const User* u = users_ ? users_->byId(userId_) : nullptr;
    return u ? u->security : 0;
}

std::vector<FileArea> Node::openTo(const std::vector<FileArea>& all) const {
    std::vector<FileArea> v;
    int                   level = securityLevel();
    for (const FileArea& a : all)
        if (a.minSecurity <= level) v.push_back(a);
    return v;
}

int Node::minutesLeft(uint32_t nowMs) const {
    if (sysop_ || cfg_.dailyMinutes <= 0 || !callStart_) return -1;
    int32_t left = (int32_t)((uint32_t)cfg_.dailyMinutes * 60000u - (nowMs - callStart_));
    return left > 0 ? left / 60000 : 0;
}

bool Node::twit() const {
    const User* u = users_ && !sysop_ ? users_->byId(userId_) : nullptr;
    return u && u->twit;
}

bool Node::hiddenWriter(const std::string& from) const {  // a twit's posts: hidden from everyone but them and the sysop
    if (sysop_ || !users_ || from == handle_) return false;
    const User* u = const_cast<UserStore*>(users_)->find(from);
    return u && u->twit;
}

// The sysop's user editor over telnet: the callers, then one at a time.
void Node::showUsers() {
    state_ = State::UserEdit;
    editId_ = 0;
    line_.clear();
    out_ += "\r\n\r\n   " + theme::fade("USER EDITOR") + "\r\n";
    char b[160];
    int  i = 0;
    for (const User& u : users_->all()) {
        i++;
        const char* st = u.status == User::Status::Pending ? "pending" : u.status == User::Status::Denied ? "denied" : "ok";
        std::snprintf(b, sizeof b, "   %s%3d %s%-24.24s %ssec %-3d %-8s %s%s\r\n", theme::KEY, i, theme::WHITE, u.handle.c_str(), theme::TEXT, u.security, st,
                      theme::ORANGE, u.twit ? "TWIT" : "");
        out_ += b;
    }
    out_ += std::string("\r\n   ") + theme::TEXT + "Number to edit (Enter or Q for the menu): " + theme::WHITE;
}

void Node::showUser() {
    User* u = users_->byId(editId_);
    if (!u) {
        showUsers();
        return;
    }
    const char* st = u->status == User::Status::Pending ? "pending" : u->status == User::Status::Denied ? "denied" : "validated";
    out_ += "\r\n\r\n";
    out_ += theme::window("CALLER", {{"handle", u->handle},
                                     {"status", st},
                                      {"security", std::to_string(u->security)},
                                      {"from", u->location},
                                      {"calls", std::to_string(u->calls)},
                                      {"up/down", std::to_string(u->uploads / 1024) + " / " + std::to_string(u->downloads / 1024) + " KB"},
                                      {"access", std::string(u->permFiles ? "files " : "") + (u->permMessages ? "msgs " : "") + (u->permDoors ? "doors " : "") +
                                                     (u->freeDownloads ? "free-dl" : "")},
                                      {"twit", u->twit ? "YES" : "no"}});
    out_ += "   " + theme::key("S", "ecurity  ") + theme::key("V", "alidate  ") + theme::key("D", "eny  ") + theme::key("T", "wit  ") + theme::key("F", "iles  ") +
            theme::key("M", "sgs  ") + theme::key("O", " doors  ") + theme::key("L", " free dl  ") + theme::key("Q", "uit") + theme::ORANGE + ": " + theme::WHITE;
}

void Node::userEditKey(char c) {
    if (c == 8 || c == 127) {
        if (!line_.empty()) {
            line_.pop_back();
            out_ += "\b \b";
        }
        return;
    }
    if (!editId_ || step_ == 30) {  // typing a number: which caller, or (step 30) their new security level
        if (c >= '0' && c <= '9' && line_.size() < 3) {
            line_ += c;
            out_ += c;
            return;
        }
        if (!editId_ && line_.empty() && (c == '\r' || (c | 0x20) == 'q')) {
            showMainMenu();
            return;
        }
        if (c != '\r') return;
        int n = std::atoi(line_.c_str());
        line_.clear();
        if (step_ == 30) {
            step_ = 0;
            if (User* u = users_->byId(editId_); u && n >= 0 && n <= 255) {
                u->security = n;
                if (saveUsers_) saveUsers_();
            }
            showUser();
            return;
        }
        auto& all = users_->all();
        if (n < 1 || n > (int)all.size()) {
            showUsers();
            return;
        }
        editId_ = all[(size_t)n - 1].id;
        showUser();
        return;
    }
    User* u = users_->byId(editId_);
    if (!u) {
        showUsers();
        return;
    }
    switch (c | 0x20) {
        case 's':
            step_ = 30;
            line_.clear();
            out_ += std::string("\r\n   ") + theme::TEXT + "New security level (0-255, 255 = sysop): " + theme::WHITE;
            return;
        case 'v': u->status = User::Status::Validated; if (u->security < 20) u->security = 20; break;
        case 'd': u->status = User::Status::Denied; break;
        case 't': u->twit = !u->twit; break;
        case 'f': u->permFiles = !u->permFiles; break;
        case 'm': u->permMessages = !u->permMessages; break;
        case 'o': u->permDoors = !u->permDoors; break;
        case 'l': u->freeDownloads = !u->freeDownloads; break;
        case 'q': showUsers(); return;
        default: return;
    }
    if (saveUsers_) saveUsers_();  // and on to every board
    showUser();
}

std::string Node::tagline() const {
    const User* u = users_ && !sysop_ ? users_->byId(userId_) : nullptr;
    return u ? u->tagline : "";
}

std::string Node::activityText() const {
    switch (state_) {
        case State::Splash:
        case State::Matrix:
        case State::Login:
        case State::Password: return "logging in";
        case State::ApplyAsk:
        case State::NupAsk:
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
        case State::Settings: return "account settings";
        case State::MultiNode: return "multinode menu";
        case State::Email: return "email";
        case State::Rumors: return "rumors";
        case State::BbsList: return "BBS list";
        case State::Bulletin: return "bulletin";
        case State::Voting: return "voting booth";
        case State::UserEdit: return "user editor";
        case State::SysopMenu: return "sysop menu";
        case State::Split: return "split-screen chat";
        case State::Done: return "leaving";
    }
    return "online";
}

void Node::poll(uint32_t nowMs) {
    MciScope mciScope(*this);
    if (menu_ && !builtin_ && state_ == State::Menu) menu_->tick(nowMs);  // a lone ESC on a lightbar: Esc after a pause
    if (state_ == State::Files && browser_ && !browser_->tick(nowMs, lastInput_)) {
        browser_.reset();
        showMainMenu();
    }
    if (state_ == State::Messages && msgs_ && !msgs_->tick(nowMs, lastInput_)) {
        msgs_.reset();
        if (users_ && !sysop_ && users_->byId(userId_) && saveUsers_) saveUsers_();
        showMainMenu();
    }
    if (callStart_ && !sysop_ && cfg_.dailyMinutes > 0 && state_ != State::Done && state_ != State::InDoor && state_ != State::Transfer) {
        uint32_t used = nowMs - callStart_, limit = (uint32_t)cfg_.dailyMinutes * 60000u;
        if (used >= limit) {
            out_ += std::string("\r\n\r\n   ") + AMBER + "Your time is up for this call. Call back soon." + theme::RESET + "\r\n";
            state_ = State::Done;
            return;
        }
        if (!timeWarned_ && limit - used <= 5u * 60000u) {
            timeWarned_ = true;
            out_ += std::string("\r\n   ") + theme::HOT + "*** 5 minutes left on this call." + theme::TEXT + "\r\n";
        }
    }  // the idle timer covers the application and the pending screen too
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
