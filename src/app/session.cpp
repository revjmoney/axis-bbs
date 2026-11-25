// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// One caller's turn (app/session.h): moved out of the board's main.cpp, where it ran over globals bound to one session at
// a time. What is the board's own stays there, reached through Services.
#include "app/session.h"

#include <cstdio>
#include <cstring>
#include <ctime>
#include <new>
#include <utility>
#include <vector>

#include "bbs/features.h"
#include "bbs/inflatelease.h"
#include "bbs/theme.h"
#include "bbs/uploadproc.h"
#include "bbs/version.h"

// Size over speed, as main.cpp (after the includes: the door headers' inline functions keep -O2).
#pragma GCC optimize("Os")

namespace axis::app {

namespace {
// "YYYY-MM-DD HH:MM", or "clock not set" (main.cpp's timeText: a LORD mail's date)
std::string clockText() {
    time_t now = time(nullptr);
    if (now < 1700000000) return "clock not set";
    char b[24];
    strftime(b, sizeof b, "%Y-%m-%d %H:%M", localtime(&now));
    return b;
}
}  // namespace

Session::Session(int slotNo, std::unique_ptr<net::TelnetStream> l, Services& s) : slot(slotNo), line(std::move(l)), s_(s) {
    mail_.s = &s_;
#if AXIS_MODEMLINK
    line->acceptLink(s_.cfg->modemLink);  // the LFD3K-SM modem's link reports (LFD3K-LINK, telnet option 90)
#endif
    line->setHold(true);  // a slow caller's screens wait in its own line, never in the loop (service drains it)
    startMs = s_.host.nowMs();
    line->start();
}

Session::~Session() { dropLord(); }  // (LORD first, while its mail box and folder are still here)

int Session::nodeNumber() const { return s_.nodeNumber ? s_.nodeNumber(slot) : slot + 1; }

void Session::sendApp(const std::string& s) {
    if (!line) return;
    if (s.size() > 4096)  // a turn's output not drawn a piece at a time: which screen (it sits in RAM whole for a slow caller)
        s_.host.event("big output " + std::to_string((unsigned)s.size()) + " B, state " + std::to_string(node ? (int)node->state() : -1) +
                      ", held " + std::to_string((unsigned)line->backlog()));
    if (!s.empty()) line->write((const uint8_t*)s.data(), s.size());
    line->flush();  // (even when empty: a screen that left in 2 KB pieces through setDirect still has its tail held here)
}

void Session::open(bool console) {
    bool ready = s_.ready ? s_.ready() : s_.cfg->setupDone;
    if (ready) {
        s_.stats.countCall();
        node.reset(new bbs::Node(nodeNumber(), s_.sysInfo, s_.nodeCfg ? s_.nodeCfg() : s_.cfg, s_.files ? &s_.files() : nullptr));
        wireNode();
        if (console) {  // the sysop, from the panel: no password asked, and the WFC is the first screen
            node->beginLocal(s_.host.nowMs());
            node->openWfc(s_.host.nowMs());
        } else node->begin(s_.host.nowMs());
        sendApp(node->takeOutput());
    } else {
        gate.reset(new bbs::SetupGate(s_.setupPin ? s_.setupPin() : std::string()));
        gate->begin();
        sendApp(gate->takeOutput());
    }
    dirty();
}

void Session::wireNode() {
    ThemeProvider* t = &s_.theme;
    BoardData*     d = &s_.data;
    Host*          h = &s_.host;
    node->setAccounts(&s_.users, s_.saveUsers, s_.rng, s_.unixNow);
    node->setWarrior(s_.warrior);
    node->onEvent = [h](const std::string& e) { h->event(e); };
    node->setSysopRun(s_.sysopRun);
    node->setWfcSnapshot(s_.wfcSnapshot);
    if (s_.dayStats) node->setDayStats(s_.dayStats);
    else node->setDayStats([st = &s_.stats] { return st->dayStats; });
    node->setChat(
        [this](const std::string& text, bool page) {  // through the main to every node (or just this board)
            if (!node) return;
            if (s_.chat) s_.chat(slot, node->handle(), text, page);
        },
        s_.who);
    node->setNodeLink([this](int kind, int to, const std::string& text) {  // notes, broadcasts, split chat keys
        if (!node) return;
        if (s_.nodeLink) s_.nodeLink(slot, kind, to, node->handle(), text);
    });
    node->setMessages(s_.messages ? &s_.messages() : nullptr);  // (none: the message base is offline)
    node->setArt([t](const std::string& name) { return t->themeArt(name); });
    node->setWfcArt([t] { return t->wfcArtShared(); });
    node->setPoster([t](const std::string& name, size_t off, size_t want, bool* exists) { return t->posterPiece(name, off, want, exists); });
    node->setMenus([t](const std::string& name) { return t->menuFile(name); },
                   [t](const std::string& name, const bbs::ArtEmit& piece) { return t->menuArtPieces(name, piece); });
    node->setMenuArt([t](const std::string& name, size_t off, size_t want, bool* exists) { return t->menuArtPiece(name, off, want, exists); });
    node->setExists([t](const std::string& name) { return t->menuExists(name); });
#if AXIS_SCRIPTS  // (scripts left out of the build: no loader, nothing runs)
    node->setScripts([t](const std::string& name) { return t->scriptFile(name); }, [t] { return t->scriptNames(); });
#endif
    bbs::theme::setArt([t](const std::string& name) { return t->themeArt(name); });  // section header screens
    bbs::theme::setArtTop([t](const std::string& name, int rows) { return t->themeArtTop(name, rows); });  // (a list screen's header)
    bbs::theme::setListTemplate([t](const std::string& name) { return t->listTemplateText(name); },  // a look's list templates
                                [h](const std::string& name) { h->event("look: " + name + " is not a valid list template, using the built-in list"); });
    bbs::theme::setThemeCfg([t](std::string& text) { return t->themeCfgText(text); },  // the look's art dialect and colours
                            [t, h](const std::string& why) { h->event("THEME.CFG for " + t->lookId() + ": " + why + ", drawing it as axis"); });
    node->setOneLiners([d] { return d->sharedOneLiners(); },  // the wall: here on the main, over the link on a follower
                       [d, h](const std::string& line) {
                           h->event("oneliner: " + line);
                           d->sharedAddOneLiner(line);
                       });
    node->setLists([d](const std::string& n) { return d->sharedListLoad(n); },
                   [d, h](const std::string& n, const std::string& line) {
                       if (n == "rumors") h->event("rumor: " + line);
                       d->sharedListAdd(n, line);
                   });
    node->setLastCallers([d] { return d->sharedLastCallers(); }, [d](const std::string& line) { d->sharedNoteCaller(line); });
    // The login art from flash, and any screen as it is drawn (2 KB at a time), straight to this session's own line
    // (none once hangUp has let it go).
    node->setDirect([this](const char* p, size_t n) {
        if (line) line->write((const uint8_t*)p, n);
    });
    node->setBackedUp([this]() { return line && line->backlog() >= net::TelnetStream::kHoldLow; });
#if AXIS_NETS && AXIS_FILES && AXIS_TRANSFERS
    if (s_.netFiles) s_.netFiles(*this);  // Net Files: this board's fetch, or on a follower the main's
#endif
}

AXIS_NOINLINE void Session::hangUp(const char* /*why*/) {
    HeapWatch scope(s_.host, "hangUp");
    doing("idle");
    viewer.reset();
    job.reset();  // a viewer or job gives the board's inflate window back as it goes
#if AXIS_NETS && AXIS_TRANSFERS
    if (s_.netRelease) s_.netRelease(node.get());  // a Net Files fetch still connecting for this caller
#endif
    zipHeap();
    dropLord();
    if (line && node && s_.lineEnding) s_.lineEnding(*this);  // wrong passwords against the address, a radio call kept
    if (line && line->backlog() && line->carrier() && s_.park) s_.park(std::move(line));  // the goodbye still going out
    line.reset();  // closes the connection
    s_.host.event("hang up" + (s_.heapText ? s_.heapText() : std::string()) + " " +
                  (node && !node->handle().empty() ? node->handle() : std::string()) + " after " +
                  std::to_string((s_.host.nowMs() - startMs) / 1000) + "s");
    if (node && !node->handle().empty() && s_.callerGone) s_.callerGone(node->handle());
    node.reset();
    gate.reset();
    wizard.reset();
    dirty();
}

void Session::dropLord() {
    lord.reset();
    lordLocks.reset();
    lordFs.reset();
}

// Give back this session's door. Safe to call twice, and it does not allocate.
void Session::releaseDoorSlot() {
    if (s_.releaseDoor) s_.releaseDoor(nodeNumber());
    viewer.reset();
    zipHeap();
    lord.reset();
    dirty();
}

void Session::abortDoor(bbs::Node::DoorEnd how) {
    using E = bbs::Node::DoorEnd;
    doorNote(how == E::OutOfMemory   ? bbs::DoorDiag::OutOfMemory
             : how == E::TimeLimit   ? bbs::DoorDiag::TimeLimit
             : how == E::LoadError   ? bbs::DoorDiag::LoadError
             : how == E::CarrierLost ? bbs::DoorDiag::Hangup
                                     : bbs::DoorDiag::Quit);
    releaseDoorSlot();
    if (!node) return;
    try {
        node->resumeAfterDoor(how, s_.host.nowMs());
        sendApp(node->takeOutput());
    } catch (...) {
        romLog("axis: door menu recover failed\n");
    }
}

std::string Session::areaName(const std::string& id) const {
    for (auto& a : s_.cfg->areas)
        if (a.id == id) return a.name;
    return id;
}

namespace {
bool lordSame(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); i++)
        if ((a[i] | 0x20) != (b[i] | 0x20)) return false;
    return true;
}
bool lordCoded(const std::string& body) {
    size_t i = 0;
    while (i < body.size()) {
        if (body[i] == '`') return true;
        size_t n = body.find('\n', i);
        if (n == std::string::npos) break;
        i = n + 1;
    }
    return false;
}
}  // namespace

void Session::lordLoadSeen(LordMail* box) {
    box->loaded = true;
    box->nseen = 0;
    if (!box->fs) return;
    int h = box->fs->open("MAILGOT", 0, false, false);
    if (h < 0) return;
    int32_t nums[48];
    int n = box->fs->read(h, nums, (int)sizeof nums);
    box->fs->close(h);
    if (n < 4) return;
    int count = n / 4;
    if (count > 48) count = 48;
    box->nseen = count;
    for (int i = 0; i < count; i++) box->seen[i] = (int)nums[i];
}

void Session::lordSaveSeen(LordMail* box) {
    if (!box->fs) return;
    int32_t nums[48];
    for (int i = 0; i < box->nseen; i++) nums[i] = box->seen[i];
    int h = box->fs->open("MAILGOT", 2, true, true);
    if (h < 0) return;
    if (box->nseen > 0) box->fs->write(h, nums, box->nseen * 4);
    box->fs->close(h);
}

int Session::lordPull(void* ctx, char* body, int bodyN, char* from, int fromN) {
    auto* box = (LordMail*)ctx;
    if (!box || !box->s->messages || bodyN < 2 || fromN < 2) return 0;
    if (!box->loaded) lordLoadSeen(box);
    body[0] = from[0] = 0;
    bbs::MessageStore& msgs = box->s->messages();
    for (const auto& h : msgs.headers("email")) {
        if (!lordSame(h.to, box->me)) continue;
        bool saw = false;
        for (int i = 0; i < box->nseen; i++)
            if (box->seen[i] == h.num) saw = true;
        if (saw) continue;
        bbs::Message m;
        if (!msgs.read("email", h.num, m) || !lordCoded(m.body)) continue;
        if (box->nseen == 48) {
            std::memmove(box->seen, box->seen + 1, sizeof(int) * 47);
            box->nseen = 47;
        }
        box->seen[box->nseen++] = h.num;
        lordSaveSeen(box);
        std::snprintf(from, (size_t)fromN, "%s", m.from.c_str());
        std::snprintf(body, (size_t)bodyN, "%s", m.body.c_str());
        return h.num;
    }
    return 0;
}

void Session::lordPost(void* ctx, const char* from, const char* to, const char* body) {
    auto* box = (LordMail*)ctx;
    if (!box || !box->s->messages) return;  // (no message base: the mail is dropped)
    bbs::Message m;
    m.from = from ? from : "";
    m.to = to ? to : "";
    m.subject = "LORD";
    m.date = clockText();
    m.body = body ? body : "";
    box->s->messages().post("email", m);
}

void Session::startDoorInner() {
    const bbs::DoorEntry d = node->door();  // (a copy: new settings may arrive while it starts)
    sendApp(node->takeOutput());
    if (AXIS_LORD && d.exe == "*LORD") {  // one game per caller. The EXE slot stays free.
        s_.host.event(node->handle() + " opened " + d.name);
        doorStartMs = s_.host.nowMs();
        dropLord();
        int              me = nodeNumber();
        door::LockTable* locks = s_.doorFiles ? s_.doorFiles(d.id, lordFs, lordLocks) : nullptr;  // here, or the main's
        if (!lordFs || !line) {  // (no door folder from the program, or the caller is already gone)
            abortDoor(bbs::Node::DoorEnd::LoadError);
            return;
        }
        if (mail_.me != node->handle()) {
            mail_.me = node->handle();
            mail_.nseen = 0;
            mail_.loaded = false;
        }
        mail_.fs = lordFs.get();
        lord.reset(new axis::lord::NativeLord(*lordFs, *line, node->handle(), locks, me));
        if (s_.rng) lord->setRng(s_.rng);  // (none: the game keeps its own)
        lord->setMailbox(&mail_, lordPull, lordPost);
        lord->begin();
        line->flush();
        return;
    }
    if (bbs::doorLeftOut(d.exe)) {  // a firmware door this build leaves out (never offered: a stale request)
        s_.host.event(node->handle() + " opened " + d.name + " (not in this firmware)");
        abortDoor(bbs::Node::DoorEnd::LoadError);
        return;
    }
    if (d.exe == "*DOSSHELL" || d.exe == "*SCRIPT") {
        // *SCRIPT runs inside the session. *DOSSHELL is not in this firmware.
        s_.host.event(node->handle() + " opened " + d.name + " (not a slot door)");
        abortDoor(bbs::Node::DoorEnd::LoadError);
        return;
    }
    doorStartMs = s_.host.nowMs();
#if AXIS_ANSIVIEW
    if (!d.exe.empty() && d.exe[0] == '*' && !s_.files) {  // (a program with no file areas: nothing to view)
        s_.host.event(node->handle() + " opened " + d.name + " (no file areas)");
        abortDoor(bbs::Node::DoorEnd::LoadError);
        return;
    }
    if (!d.exe.empty() && d.exe[0] == '*') {  // one viewer per caller. It borrows the inflate window only while a deflated piece plays.
        s_.host.event(node->handle() + " opened " + d.name);
        std::vector<std::pair<std::string, std::string>> va;  // "ansi,scene-acid,...": several areas, a picker first
        for (size_t from = 0; from <= d.args.size();) {
            size_t      comma = d.args.find(',', from);
            std::string id = d.args.substr(from, comma == std::string::npos ? std::string::npos : comma - from);
            if (!id.empty()) va.push_back({id, areaName(id)});
            if (comma == std::string::npos) break;
            from = comma + 1;
        }
        viewer.reset(new bbs::AnsiViewer(s_.files(), va.empty() ? d.args : va[0].first, va.empty() ? areaName(d.args) : va[0].second,
                                         nullptr, 0, *line));
        viewer->setAreas(va);
        Host* h = &s_.host;
        if (!node->lite()) viewer->setClock([h] { return h->nowMs(); });  // ANSI movies play at modem speed (a lite caller: unpaced)
        // The viewer limit (Rev, 2026-11-12): with its 44 KB window taken the board keeps the floors the viewer asks for (the
        // busy test's 60 KB and 32 KB while a piece plays from the window; a little less for a moment's unpack into the SD
        // view cache), or the piece is refused or stopped with a note. No cushion above them (Rev: "loosen the margin").
        viewer->setRoom([h](uint32_t minFree, uint32_t minLargest) { return h->roomFor(minFree, minLargest); });
        viewer->setCache(s_.host.hasSd() || follower());  // the cache is on the main's card (a follower reaches it over the link)
        viewer->begin();
        line->flush();
        return;
    }
#endif
    // An .EXE needs the 8086 runtime. That runtime is dosemu/ in the repo, and it is not linked here.
    s_.host.event(node->handle() + " opened " + d.name + " (no DOS runtime)");
    abortDoor(bbs::Node::DoorEnd::LoadError);
}

AXIS_NOINLINE void Session::startDoor() {
    bool isLord = node && node->door().exe == "*LORD";
    if (node) doing("door " + node->door().name + " by " + node->handle());
    doorNote(bbs::DoorDiag::Running);
    try {
        startDoorInner();
    } catch (const std::bad_alloc&) {
        romLog("axis: ALLOC FAILED during door start (uncaught)\n");
        if (isLord) {
            dropLord();
            if (node) node->resumeAfterDoor(bbs::Node::DoorEnd::OutOfMemory, s_.host.nowMs());
        } else abortDoor(bbs::Node::DoorEnd::OutOfMemory);
    } catch (...) {
        romLog("axis: door start failed\n");
        if (isLord) {
            dropLord();
            if (node) node->resumeAfterDoor(bbs::Node::DoorEnd::LoadError, s_.host.nowMs());
        } else abortDoor(bbs::Node::DoorEnd::LoadError);
    }
    zipHeap();
}

AXIS_NOINLINE void Session::finishLord() {
    bool drop = !line || !line->carrier();
    doorNote(drop ? bbs::DoorDiag::Hangup : bbs::DoorDiag::Quit);
    dropLord();
    s_.host.event("door lord ended after " + std::to_string((s_.host.nowMs() - doorStartMs) / 1000) + "s");
    if (drop) {
        hangUp();
        return;
    }
    if (!node) return;
    node->resumeAfterDoor(bbs::Node::DoorEnd::Normal, s_.host.nowMs());
    sendApp(node->takeOutput());
}

AXIS_NOINLINE void Session::finishViewer() {
    doorNote(line && line->carrier() ? bbs::DoorDiag::Quit : bbs::DoorDiag::Hangup);
    releaseDoorSlot();
    s_.host.event("door " + node->door().id + " ended after " + std::to_string((s_.host.nowMs() - doorStartMs) / 1000) + "s");
    if (!line || !line->carrier()) {
        hangUp();
        return;
    }
    node->resumeAfterDoor(bbs::Node::DoorEnd::Normal, s_.host.nowMs());
    sendApp(node->takeOutput());
}

// The name a "heap low in ..." line gives the node: a key or a poll, the node's state number and what its line holds.
const char* Session::scopeName(const char* what) {
    static char name[40];
    std::snprintf(name, sizeof name, "node %s s%d held %u", what, node ? (int)node->state() : -1, line ? (unsigned)line->backlog() : 0u);
    return name;
}

// keystrokes from the caller, already through the telnet filter
AXIS_NOINLINE void Session::callerInput(const std::string& app) {
    const uint8_t* p = (const uint8_t*)app.data();
    if (node) {
        std::string before = node->handle();
        {
            HeapWatch scope(s_.host, scopeName("key"));
            node->input(p, app.size(), s_.host.nowMs());
        }
        if (node->handle() != before) dirty();
        return;
    }
    if (wizard) {
        wizard->input(p, app.size());
        sendApp(wizard->takeOutput());
        if (wizard->finished()) {
            if (!s_.setupSave) {  // (a program that cannot keep settings: nothing is saved, and the caller is told so)
                sendApp("\r\n   Nothing was saved. Call again to start over.\x1b[0m\r\n");
                hangUp();
                return;
            }
            s_.setupSave(*wizard);  // the settings kept (and the event logged) by the program
            char b[220];
            std::snprintf(b, sizeof b,
                          "\r\n   \x1b[1;32mSettings saved.\x1b[0;37m Your BBS is open: call %s port %d.\r\n"
                          "   Log in as %s for the sysop menu (coming soon).\x1b[0m\r\n",
                          (s_.dialName ? s_.dialName() : std::string()).c_str(), s_.cfg->telnetPort, s_.cfg->sysopHandle.c_str());
            sendApp(b);
            hangUp();
            if (s_.setupFinished) s_.setupFinished();
        } else if (wizard->cancelled()) {
            sendApp("\r\n   Nothing was saved. Call again to start over.\x1b[0m\r\n");
            hangUp();
        }
        return;
    }
    if (gate) {
        gate->input(p, app.size());
        sendApp(gate->takeOutput());
        if (gate->passed()) {
            if (s_.setupChosen) s_.setupChosen();  // the right PIN means the sysop chose telnet
            gate.reset();
            wizard.reset(new bbs::SetupWizard(*s_.cfg, s_.rng));
            wizard->begin();
            sendApp(wizard->takeOutput());
            dirty();
        } else if (gate->rejected()) {
            hangUp();
        }
    }
}

AXIS_NOINLINE void Session::input(uint32_t /*nowMs*/) {
    if (!line) return;
    std::string app = line->takeInput();
    if (!app.empty()) callerInput(app);
}

// The LFD3K-SM modem's newest link report for this caller, into its node (before any door, movie or transfer takes the
// turn, so a retrain during one still counts).
AXIS_NOINLINE void Session::takeLinkReport() {
#if AXIS_MODEMLINK
    net::LinkInfo li;
    if (line && node && s_.cfg->modemLink && line->takeLink(li)) {  // (at most one a second per line: TelnetStream::takeLink)
        bool first = !node->viaModem();
        node->setLink(li);
        if (s_.radio) s_.radio(*this, li, first);  // the WFC's |R? codes, and the first report's event line
    }
    if (node && node->linkRedrawDue(s_.host.nowMs())) dirty();  // the tier changed (at most every 5 s), not every report
#endif
}

// The caller's line in the cluster's node list (handle and what they are doing). Its own frame: service is on the stack
// under every key a caller types (and the screen code below it), so it stays small (2026-11-16: 1.9 KB a turn with these
// inlined, and a slow call took the board's loop stack to 284 bytes free).
AXIS_NOINLINE void Session::postStatus() {
    std::string handle = node ? node->handle() : "", activity;
    if (node) {
        using S = bbs::Node::State;
        S st = node->state();
        activity = (lord || viewer || st == S::Script) ? "Doors" : job ? "Transfer" : st == S::Files ? "Files"
                 : st == S::Applying ? "Applying" : st == S::Chat ? "Chat" : st == S::Messages ? "Messages" : st == S::Menu ? "Menu"
                 : st == S::Splash || st == S::Login || st == S::Password ? "Logging in" : st == S::Split ? "Split chat"
                 : st == S::AxisNet ? "Axis Net" : st == S::AxisCall ? "Remote board" : "Online";
        if (!node->available()) activity += " (DND)";
        if (!node->linkShown().empty()) activity += "  " + node->linkShown();  // (tier only, at most every 5 s)
    }
    if (handle + "|" + activity != lastStatus) {
        lastStatus = handle + "|" + activity;
        if (s_.status) s_.status(slot, handle, activity);
    }
}

// This caller's download or upload, one step (its own frame, as above).
AXIS_NOINLINE void Session::serviceJob() {
    if (job->needsWorkspace()) {  // a ZIP upload: the inflate window (lent to a viewer: wait up to 60 s)
        bool busy = bbs::inflate::busy();
        if (busy && (!line || !line->carrier())) {  // hung up while waiting
            hangUp();
            return;
        }
        if (busy && !job->waiting) {
            job->waiting = true;
            job->waitSince = s_.host.nowMs();
            sendApp("\r\n  Checking your upload, waiting for the ZIP checker...\r\n");
        }
        bool late = busy && s_.host.nowMs() - job->waitSince >= 60000u;
        if (!busy || late) {
            uint8_t *w = nullptr, *st = nullptr;  // late: today's short-memory path (no window)
            if (s_.drawTransfer) s_.drawTransfer(*this);
            if (late) s_.host.event("upload: ZIP window busy");
            else if (!bbs::inflate::take(job.get(), w, st)) s_.host.event("upload: short on memory to check the ZIP");
            job->beginProcessing(w, w ? bbs::inflate::kWindow : 0, st);
            zipHeap();
        }
    }
    bool running = job->step(s_.host.nowMs());
    if (line) line->flush();
    if (s_.host.nowMs() - jobDrawnMs > 500) {
        jobDrawnMs = s_.host.nowMs();
        if (s_.drawTransfer) s_.drawTransfer(*this);
    }
    if (!running) {
        const auto& r = job->request();
        std::string note = job->note();
#if AXIS_NETS && AXIS_TRANSFERS
        if (s_.netJobNote) s_.netJobNote(job.get(), note);  // a Net Files download whose fetch stopped
#endif
        bool held = r.upload && job->ok() && job->held();  // waiting for the sysop: not on the net, and not announced as a fresh file
        if (r.upload && job->ok()) {
            s_.stats.countUpload();
#if AXIS_NETS && AXIS_FILES
            if (!held && s_.netUploaded) s_.netUploaded(r.area, job->savedName(), r.desc, node->handle());
#endif
        }
        std::string verb = !r.upload ? " downloaded " : (held ? " held upload " : " uploaded ");
        s_.host.event(node->handle() + verb + (r.upload ? job->savedName() : r.name) +
                      (job->ok() ? std::string() : " (failed: " + note + ")"));
        doing("idle");
        if (line && line->carrier()) {
            node->transferDone(job->ok(), job->bytes(), s_.host.nowMs() - jobStartMs, note, s_.host.nowMs());
            job.reset();  // gives the inflate window back
#if AXIS_NETS && AXIS_TRANSFERS
            if (s_.netRelease) s_.netRelease(node.get());  // (a follower: the main's fetch ends now; here the job's file let it go)
#endif
            zipHeap();
            sendApp(node->takeOutput());
        } else {
            hangUp();
        }
        dirty();
    }
}

// One turn: the caller, and their own movie or transfer when they have one.
AXIS_NOINLINE void Session::service(uint32_t nowMs) {
    takeLinkReport();
    if (s_.clustered && s_.clustered()) postStatus();
    if (line) {  // a slow caller (a LoRa radio call): what its line still holds goes out first, and the next screen piece,
                 // movie frame or LORD step waits for room, so the loop never waits on one caller's socket
        line->setHold(!job);  // (a transfer's protocol needs each block out before it waits for the answer)
        if (line->backlog() && !line->drain()) {
            hangUp();
            return;
        }
        if (!job && line->backlog() >= net::TelnetStream::kHoldLow) return;
    }
    if (job) {  // this caller's download or upload
        serviceJob();
        return;
    }
    if (lord) {  // this caller is in LORD
        bool running;
        {
            HeapWatch scope(s_.host, "door");
            running = lord->step();
        }
        if (line) line->flush();
        if (!running) finishLord();
        nap(5);
        return;
    }
    if (viewer) {  // this caller's movie or pack
        bool running = viewer->step();
        zipHeap();
        if (line) line->flush();
        if (!running) finishViewer();
        nap(5);
        return;
    }
    if (line && s_.axisPump && s_.axisPump(*this)) return;  // a call to another Axis board owns the keys
    if (line) {
        if (s_.axisDirty && s_.axisDirty() && node && node->state() == bbs::Node::State::AxisNet) node->axisRedraw();
        input(nowMs);
        if (line && node && node->axisTakeProbe() && s_.axisProbe) s_.axisProbe();
        if (line && node && node->state() == bbs::Node::State::AxisCall && !(s_.axisBusy && s_.axisBusy())) {
            sendApp(node->takeOutput());  // "Calling ..." before the connect, which can take a second
            if (s_.axisCall) s_.axisCall(*this);
            else node->axisCallDone(false, "Could not reach that board.");  // (a program with no bridge: the list again)
            sendApp(node->takeOutput());  // the list again when the call did not open
            return;
        }
        if (line && node) {
            {
                HeapWatch scope(s_.host, scopeName("poll"));
                node->poll(s_.host.nowMs());
            }
            if (node->state() == bbs::Node::State::InDoor) {
                startDoor();
                return;
            }
#if AXIS_TRANSFERS
            if (node->state() == bbs::Node::State::Transfer && !job) {
                sendApp(node->takeOutput());  // the transfer window, before the protocol starts
                jobStartMs = jobDrawnMs = s_.host.nowMs();
                bbs::UploadInfo info{s_.cfg->bbsName, s_.dialName ? s_.dialName() : std::string(), s_.cfg->location,
                                     std::string(bbs::kAxisSoftware) + " " + bbs::kAxisVersion};
                doing(std::string(node->transfer().upload ? "upload " : "download ") + node->transfer().name + " by " + node->handle());
#if AXIS_NETS
                if (node->transfer().net) {  // a Net Files download: over the fetch slot its connect left ready
                    job.reset(s_.netJob ? s_.netJob(*this) : nullptr);
                    if (!job) {  // (no memory for the job right after ready: the fetch stopped)
                        if (s_.netRelease) s_.netRelease(node.get());
                        node->transferDone(false, 0, 0, "stopped", s_.host.nowMs());
                        doing("idle");
                        sendApp(node->takeOutput());
                    }
                    return;
                }
#endif
                if (!s_.files) {  // (a program with no file areas: the transfer ends at once)
                    node->transferDone(false, 0, 0, "the file areas are offline", s_.host.nowMs());
                    doing("idle");
                    sendApp(node->takeOutput());
                    return;
                }
                job.reset(new bbs::TransferJob(s_.files(), node->transfer(), *line, info, bbs::archivePolicy(*s_.cfg)));
                return;
            }
#endif
            sendApp(node->takeOutput());
            if (node->wantsHangup()) hangUp();
        }
        if (line && !line->carrier()) hangUp();
    }
}

}  // namespace axis::app
