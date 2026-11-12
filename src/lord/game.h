// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// Native LORD. Runs in the BBS process, not in the 8086. Warriors and monsters are the
// installed LORD 4.07 files. Scene art, in order: NAME.ANS, then lordtxt.lrd (Lord Ansi
// 1.00, the 4.07 full-screen pack), then LORDTXT.DAT (the stock screens).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "door/bytestream.h"
#include "door/hostfs.h"

namespace axis {
namespace lord {

class NativeLord {
public:
    NativeLord(door::HostFs& fs, door::ByteStream& line, std::string alias, door::LockTable* locks, int owner);
    ~NativeLord();  // closes a script still open (a caller who hung up mid-event)
    NativeLord(const NativeLord&) = delete;
    NativeLord& operator=(const NativeLord&) = delete;
    void setRng(uint32_t (*rng)()) { rng_ = rng; }
    // Pull returns a message number, or 0. Only bodies with a backtick line are pulled.
    // Post writes one BBS email (from, to, body). Both may be null in a test.
    void setMailbox(void* ctx, int (*pull)(void*, char*, int, char*, int),
                    void (*post)(void*, const char*, const char*, const char*)) {
        mailCtx_ = ctx;
        mailPull_ = pull;
        mailPost_ = post;
    }
    void begin();
    bool step();  // false once the caller is back at the BBS

private:
    enum class St { Town, Fight, Bank, King, Armor, Train, Inn, Pvp, Amount, Yes, Done, Lady, Log, Places, Compose, Class, Duel };
    enum class Ask { None, Dep, With, BuyW, BuyA, SellW, SellA, Room, PvpN, MailTo, DuelY };

    struct Fighter {
        std::string name, weapon, death;
        int32_t     hp = 0, str = 0, def = 0, gold = 0, xp = 0;
        int         hpG = 0, strG = 0, defG = 0;
    };

    void        send(const char* s);
    void        send(const std::string& s);
    void        num(int32_t v);
    void        town();
    void        scene(const std::string& name);
    bool        emitRaw(const std::string& file, uint32_t cap);
    bool        emitMarked(const std::string& file, const std::string& name);
    void        townMenu();
    void        wakeIfNewDay();
    void        stats();
    void        heal();
    void        forest();
    void        strike();
    void        skill();
    void        enemyStrike();
    void        finishFight(bool won);
    void        save();
    void        saveOther(int slot, const uint8_t* rec);
    bool        loadPlayer();
    bool        loadMonster(Fighter& out);
    bool        gear(const char* file, int index, std::string& name, int32_t& price, int32_t& bonus);
    int         gearNeed(bool armor, int n);
    void        listGear(const char* file);
    bool        trainerOf(int level, Fighter& out, int& hpG, int& strG, int& defG, int32_t& need);
    void        beginAmount(Ask a, const char* prompt);
    void        commitAmount();
    void        askYes(Ask a, const char* prompt);
    void        applyYes(bool yes);
    void        placePrompt();
    void        returnTo(Ask a);
    void        doBuy(bool armor);
    void        doSell(bool armor);
    void        doRoom();
    void        startPreparedFight();
    void        fightPrompt();
    void        bumpSkill();
    void        land(int atk);
    bool        loadSlot(int slot, uint8_t* rec);
    int         g16(int off) const;
    int32_t     g32(int off) const;
    void        s16(int off, int v);
    void        s32(int off, int32_t v);
    void        putName(int off, int cap, const std::string& s);
    int32_t     sellPrice(int32_t price);
    void        listPlayers(bool onlineOnly);
    bool        playForestEvent();
    bool        soften();
    bool        spareFairy();
    void        finishDragon(bool won);
    bool        outOfRealm(int slot);
    bool        offerOnline();
    void        pollDuel();
    void        enterDuel(uint8_t* rec, bool iAmAtk);
    void        answerDuel(bool yes);
    void        duelStrike(bool skillBlow);
    void        handTurn();
    void        clearDuel();
    bool        loadDuel(uint8_t* rec);
    bool        storeDuel(const uint8_t* rec);
    void        rotateLog();
    void        maybeBirth();
    void        onBeginExtras();
    void        loadFairy();
    void        saveFairy();
    void        takeMail();
    void        applyMail(const std::string& body, const char* from);
    void        applyCode(const std::string& line);
    void        dropLine(int slot, const std::string& line);
    void        deliver(int slot, const std::string& body);
    void        startCompose(int slot);
    void        composeKey(int c);
    void        ladyKey(int c);
    void        realmKey(char k, int c);
    void        showLog();
    void        showPlaces();
    void        listMarried();
    void        kingQuest();
    void        startDragon();
    void        violet();
    void        seth();
    void        bard();
    void        writeInfo();
    void        logLine(const char* s);
    bool        playSection(const char* file, const char* section);
    bool        loadScript(const char* file);
    bool        ladyGoto(const std::string& name);
    void        ladyRun();
    void        ladyEnd();
    void        execLine(const std::string& t);
    bool        nextRaw(std::string& raw);
    bool        scriptLine(size_t pos, std::string& line, size_t& next);  // the line at file offset pos ('\n' and on left off)
    void        scriptClose();
    bool        scriptFill(size_t pos);
    bool        scriptOpen(const std::string& path);
    std::string expand(const std::string& s);
    bool        ladyGet(const std::string& name, int& out);
    void        ladySet(const std::string& name, int v);
    std::string readFile(const std::string& name, uint32_t maxBytes);
    std::string colorize(const std::string& s) const;
    uint32_t    roll(uint32_t n);
    int         strikeOf(int str);

    door::HostFs&     fs_;
    door::ByteStream& line_;
    door::LockTable*  locks_;
    std::string       alias_;
    int               owner_;
    uint32_t          (*rng_)() = nullptr;
    St                st_ = St::Town;
    Ask               ask_ = Ask::None;
    std::string       amt_;
    int               pendingN_ = 0;
    int32_t           pendingPrice_ = 0;
    bool              expert_ = false;
    bool              master_ = false;
    int               pvpSlot_ = -1;
    uint8_t           pvpRaw_[236] = {};
    bool              have_ = false;
    int               slot_ = -1;
    uint8_t           raw_[236] = {};
    Fighter           mon_{};
    bool              locked_ = false;
    int               lockKey_ = 0;
    void*             mailCtx_ = nullptr;
    int               (*mailPull_)(void*, char*, int, char*, int) = nullptr;
    void              (*mailPost_)(void*, const char*, const char*, const char*) = nullptr;
    // The running .LDY script, read from its file a window at a time (EVENTS.LDY is 18 KB: held whole, it sat on top
    // of an ANSI viewer's 44 KB window at the busy test's lowest point). Positions (ladyPos_ and the rest) are file offsets.
    struct ScriptMark {  // "@#name" (a section) or "@label name": at is the offset of the line after it
        std::string name;
        size_t      at;
        bool        section;
    };
    int               scriptH_ = -1;  // open while a script runs (one pass at load found the marks)
    size_t            scriptSize_ = 0;
    std::vector<ScriptMark> scriptMarks_;
    std::string       scriptWin_;   // bytes [scriptBase_, scriptBase_ + scriptWin_.size()) of the file
    size_t            scriptBase_ = 0;
    std::string       compose_;
    std::string       ladyKeys_;
    std::string       ladyFile_;
    size_t            ladyPos_ = 0;
    size_t            ladyRet_[4] = {};
    int               ladyRp_ = 0;
    int               nreg_[4] = {};
    int               fairy_ = 0;
    int               amulet_ = 0;
    int               composeTo_ = -1;
    int               duelWaits_ = 0;
    int               duelIdx_ = -1;
    int               placeCount_ = 0;
    char              ladyAns_ = 'N';
    bool              ladyWait_ = false;
    bool              ladyLoose_ = true;
    bool              dragon_ = false;
    bool              needClass_ = false;
    bool              duelLive_ = false;
    bool              duelJoined_ = false;
    bool              duelAsked_ = false;
    bool              duelAtk_ = false;
};

// 1 if a level-1 record was appended, 0 if this handle already matches a warrior, -1 if the
// file could not be written. Existing records are not changed. The name and the BBS field
// are both the handle, so the next visit finds the same warrior.
int ensureWarrior(door::HostFs& fs, door::LockTable* locks, int owner, const std::string& alias);

}  // namespace lord
}  // namespace axis
