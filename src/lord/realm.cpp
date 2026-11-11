// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The rest of the realm: mail, the day log, forest scripts, the dragon, and live fights.
// Prose stays in the installed door files. This file is the engine.
#include "lord/game.h"

#include <cctype>
#include <cstdio>
#include <cstring>

#pragma GCC optimize("Os")

namespace axis {
namespace lord {
namespace {

constexpr int kDuel = 24;
constexpr int kRows = 8;

int cap16(int v) {
    if (v > 32000) return 32000;
    if (v < -32768) return -32768;
    return v;
}
int32_t capG(int64_t v) {
    if (v > 2000000000) return 2000000000;
    if (v < 0) return 0;
    return (int32_t)v;
}
std::string trimC(std::string s) {
    while (!s.empty() && (s.back() == ' ' || s.back() == '\r' || s.back() == '\t')) s.pop_back();
    size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) i++;
    return s.substr(i);
}
bool ieq(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); i++)
        if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i])) return false;
    return true;
}
int get16(const uint8_t* p) { return (int)(int16_t)(p[0] | (p[1] << 8)); }
void put16(uint8_t* p, int v) {
    v = cap16(v);
    p[0] = (uint8_t)(v & 0xff);
    p[1] = (uint8_t)((v >> 8) & 0xff);
}
int get32(const uint8_t* p) {
    return (int)(int32_t)(p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24));
}
std::string pnm(const uint8_t* p, int cap) {
    int n = p[0];
    if (n < 0) n = 0;
    if (n > cap) n = cap;
    return trimC(std::string((const char*)p + 1, (size_t)n));
}
std::string wordOf(const std::string& t) {
    std::string w;
    for (size_t i = 1; i < t.size() && t[i] != ' '; i++) w.push_back((char)std::tolower((unsigned char)t[i]));
    return w;
}
std::string restOf(const std::string& t) {
    size_t i = 1;
    while (i < t.size() && t[i] != ' ') i++;
    if (i < t.size() && t[i] == ' ') i++;
    return t.substr(i);
}
bool armOf(const std::string& t, char& letter) {
    if (t.size() < 2 || t[1] != ':') return false;
    if (!std::isalpha((unsigned char)t[0])) return false;
    letter = (char)std::toupper((unsigned char)t[0]);
    return true;
}
int tailId(const std::string& t) {
    int id = 0, any = 0;
    for (size_t i = 0; i < t.size();) {
        while (i < t.size() && t[i] == ' ') i++;
        size_t j = i;
        while (j < t.size() && t[j] != ' ') j++;
        bool dig = j > i;
        for (size_t k = i; k < j; k++)
            if (!std::isdigit((unsigned char)t[k])) dig = false;
        if (dig) {
            id = 0;
            for (size_t k = i; k < j; k++) id = id * 10 + (t[k] - '0');
            any = 1;
        }
        i = j;
    }
    return any ? id : 0;
}

}  // namespace

bool NativeLord::loadDuel(uint8_t* rec) {
    if (duelIdx_ < 0 || duelIdx_ >= kRows) return false;
    std::string b = readFile("DUEL.DAT", kRows * kDuel);
    if ((int)b.size() < (duelIdx_ + 1) * kDuel) return false;
    std::memcpy(rec, b.data() + duelIdx_ * kDuel, kDuel);
    return rec[0] != 0;
}

bool NativeLord::storeDuel(const uint8_t* rec) {
    int h = fs_.open("DUEL.DAT", 2, true, false);
    if (h < 0) return false;
    int key = fs_.fileKey(h);
    bool locked = false;
    if (locks_) {
        locked = locks_->lock(key, 0, 0x7fffffffu, owner_);
        if (!locked) {
            fs_.close(h);
            return false;
        }
    }
    uint8_t all[kRows * kDuel];
    std::memset(all, 0, sizeof all);
    int n = fs_.read(h, all, (int)sizeof all);
    if (n < 0) n = 0;
    if (duelIdx_ < 0 || duelIdx_ >= kRows) duelIdx_ = 0;
    std::memcpy(all + duelIdx_ * kDuel, rec, kDuel);
    fs_.seek(h, 0, 0);
    bool ok = fs_.write(h, all, (int)sizeof all) == (int)sizeof all;
    if (locked) locks_->unlock(key, 0, 0x7fffffffu, owner_);
    fs_.close(h);
    return ok;
}

void NativeLord::clearDuel() {
    if (duelIdx_ >= 0) {
        uint8_t z[kDuel];
        std::memset(z, 0, sizeof z);
        storeDuel(z);
    }
    duelLive_ = false;
    duelJoined_ = false;
    duelAsked_ = false;
    duelIdx_ = -1;
    duelWaits_ = 0;
}

bool NativeLord::outOfRealm(int slot) {
    char name[32];
    std::snprintf(name, sizeof name, "OUT.%d", slot);
    std::string b = readFile(name, 400);
    if (b.empty()) {
        std::snprintf(name, sizeof name, "out%d.lrd", slot);
        b = readFile(name, 400);
    }
    int line = 0;
    std::string second;
    for (size_t i = 0; i <= b.size() && line < 2; i++) {
        if (i == b.size() || b[i] == '\n') {
            line++;
            if (line == 2) second = trimC(b.substr(0, i));
        }
    }
    // second line, not the whole file: take the second newline-separated piece
    line = 0;
    size_t start = 0;
    second.clear();
    for (size_t i = 0; i <= b.size(); i++) {
        if (i == b.size() || b[i] == '\n') {
            if (line == 1) second = trimC(b.substr(start, i - start));
            line++;
            start = i + 1;
        }
    }
    if (second.empty()) return false;
    return !ieq(second, "QUIT");
}

bool NativeLord::offerOnline() {
    if (outOfRealm(pvpSlot_)) {
        send("  Out of the realm.\r\n");
        pvpSlot_ = -1;
        st_ = St::Pvp;
        placePrompt();
        return true;
    }
    std::string b = readFile("DUEL.DAT", kRows * kDuel);
    int freeRow = -1;
    for (int i = 0; i < kRows; i++) {
        if ((int)b.size() < (i + 1) * kDuel) {
            if (freeRow < 0) freeRow = i;
            continue;
        }
        const uint8_t* row = (const uint8_t*)b.data() + i * kDuel;
        if (row[0] == 0) {
            if (freeRow < 0) freeRow = i;
            continue;
        }
        int a = get16(row + 2), d = get16(row + 4);
        if (a == slot_ || d == slot_ || a == pvpSlot_ || d == pvpSlot_) {
            send("  A fight is already on.\r\n");
            pvpSlot_ = -1;
            st_ = St::Pvp;
            placePrompt();
            return true;
        }
    }
    if (freeRow < 0) {
        send("  A fight is already on.\r\n");
        pvpSlot_ = -1;
        st_ = St::Pvp;
        placePrompt();
        return true;
    }
    duelIdx_ = freeRow;
    uint8_t rec[kDuel];
    std::memset(rec, 0, sizeof rec);
    rec[0] = 1;
    put16(rec + 2, slot_);
    put16(rec + 4, pvpSlot_);
    put16(rec + 6, g16(72));
    put16(rec + 8, g16(119));
    put16(rec + 10, g16(117));
    put16(rec + 12, (int)mon_.hp);
    put16(rec + 14, (int)mon_.str);
    put16(rec + 16, (int)mon_.def);
    if (!storeDuel(rec)) {
        send("  The warrior file is in use.\r\n");
        duelIdx_ = -1;
        pvpSlot_ = -1;
        st_ = St::Pvp;
        placePrompt();
        return true;
    }
    duelAtk_ = true;
    duelJoined_ = false;
    duelLive_ = false;
    duelWaits_ = 0;
    st_ = St::Duel;
    send("  They are online.\r\n");
    return true;
}

void NativeLord::enterDuel(uint8_t* rec, bool iAmAtk) {
    duelAtk_ = iAmAtk;
    int myHp = get16(rec + (iAmAtk ? 6 : 12));
    int opHp = get16(rec + (iAmAtk ? 12 : 6));
    int opSlot = iAmAtk ? get16(rec + 4) : get16(rec + 2);
    s16(72, myHp);
    if (myHp <= 0) {
        raw_[154] = 5;
        duelLive_ = true;
        pvpSlot_ = opSlot;
        loadSlot(opSlot, pvpRaw_);
        finishFight(false);
        return;
    }
    if (iAmAtk && !duelJoined_) {
        duelJoined_ = true;
        if ((rec[22] & 1) == 0) {
            if (g16(107) > 0) s16(107, g16(107) - 1);
            save();
            rec[22] |= 1;
            storeDuel(rec);
        }
    }
    if (!iAmAtk) duelJoined_ = true;
    pvpSlot_ = opSlot;
    if (!loadSlot(opSlot, pvpRaw_)) {
        send("  The warrior is gone.\r\n");
        clearDuel();
        town();
        return;
    }
    mon_ = Fighter{};
    mon_.name = pnm(pvpRaw_, 20);
    if (mon_.name.empty()) mon_.name = "warrior";
    mon_.weapon = pnm(pvpRaw_ + 82, 20);
    mon_.str = get16(rec + (iAmAtk ? 14 : 8));
    mon_.def = get16(rec + (iAmAtk ? 16 : 10));
    mon_.hp = opHp;
    mon_.gold = get32(pvpRaw_ + 109);
    mon_.xp = get32(pvpRaw_ + 158);
    master_ = false;
    dragon_ = false;
    duelLive_ = true;
    st_ = St::Fight;
    send("\r\n  Player fight.\r\n  ");
    send(mon_.name);
    send("   HP ");
    num((int32_t)mon_.hp);
    send("\r\n");
    fightPrompt();
}

void NativeLord::pollDuel() {
    if (slot_ < 0 || st_ == St::Yes) return;
    std::string b = readFile("DUEL.DAT", kRows * kDuel);
    if (b.empty()) return;
    for (int i = 0; i < kRows; i++) {
        if ((int)b.size() < (i + 1) * kDuel) break;
        const uint8_t* row = (const uint8_t*)b.data() + i * kDuel;
        if (row[0] == 0) continue;
        int atk = get16(row + 2), def = get16(row + 4);
        if (atk != slot_ && def != slot_) continue;
        duelIdx_ = i;
        uint8_t rec[kDuel];
        std::memcpy(rec, row, kDuel);
        int state = rec[0];
        if (state == 1 && slot_ == def && !duelAsked_) {
            duelAsked_ = true;
            duelAtk_ = false;
            askYes(Ask::DuelY, "\r\n  A warrior challenges you. Accept? [N]\r\n");
            return;
        }
        if (state == 3 && slot_ == atk && st_ == St::Duel) {
            send("  They decline.\r\n");
            clearDuel();
            town();
            return;
        }
        if (state == 1 && slot_ == atk && st_ == St::Duel) {
            if (++duelWaits_ > 3000) {
                send("  They are not there.\r\n");
                clearDuel();
                town();
            }
            return;
        }
        if (state == 2 && slot_ == atk && st_ == St::Duel && rec[1] == 0) {
            enterDuel(rec, true);
            return;
        }
        if (state == 2 && slot_ == def && rec[1] == 1 && st_ != St::Fight) {
            enterDuel(rec, false);
            return;
        }
        return;
    }
}

void NativeLord::answerDuel(bool yes) {
    uint8_t rec[kDuel];
    if (!loadDuel(rec) || rec[0] != 1 || get16(rec + 4) != slot_) {
        send("  The challenge is gone.\r\n");
        duelAsked_ = false;
        town();
        return;
    }
    if (!yes) {
        rec[0] = 3;
        storeDuel(rec);
        duelAsked_ = false;
        send("  No.\r\n");
        town();
        return;
    }
    int max = g16(78);
    if (max < 1) max = 1;
    s16(72, max);
    save();
    rec[0] = 2;
    rec[1] = 0;
    put16(rec + 12, max);
    put16(rec + 14, g16(119));
    put16(rec + 16, g16(117));
    storeDuel(rec);
    duelAtk_ = false;
    duelAsked_ = false;
    st_ = St::Duel;
    send("  You accept.\r\n");
}

void NativeLord::handTurn() {
    uint8_t rec[kDuel];
    if (!loadDuel(rec)) {
        duelLive_ = false;
        town();
        return;
    }
    if (duelAtk_) {
        put16(rec + 6, g16(72));
        put16(rec + 12, (int)mon_.hp);
        rec[1] = 1;
    } else {
        put16(rec + 12, g16(72));
        put16(rec + 6, (int)mon_.hp);
        rec[1] = 0;
    }
    storeDuel(rec);
    duelLive_ = false;
    st_ = St::Duel;
    send("  You wait.\r\n");
}

void NativeLord::duelStrike(bool skillBlow) {
    int atk = strikeOf(g16(119)) - (int)mon_.def;
    if (skillBlow) {
        atk *= 3;
        if (atk < 1) atk = 1;
    } else if ((int)roll(10) + 1 > 9) {
        atk *= 3;
        send("  Power move.\r\n");
    }
    if (atk < 1) {
        send("  You miss ");
        send(mon_.name);
        send(".\r\n");
        handTurn();
        return;
    }
    mon_.hp -= atk;
    send("  You hit ");
    send(mon_.name);
    send(" for ");
    num(atk);
    send(".\r\n");
    if (mon_.hp <= 0) {
        duelLive_ = true;
        finishFight(true);
        return;
    }
    handTurn();
}

bool NativeLord::soften() {
    if (pvpSlot_ >= 0 || master_ || duelLive_) return false;
    int kids = g16(210);
    if (kids > 5 && (int)roll(100) + 1 < kids - 5) {
        s16(210, kids - 1);
        save();
        send("  A child takes the blow.\r\n");
        return true;
    }
    if (g16(72) > 0 && g16(180) == 1 && roll(25) == 10) {
        s16(180, 0);
        save();
        if (dragon_) {
            send("  The horse stops the blow.\r\n");
            return true;
        }
        mon_.hp = 0;
        send("  The horse takes the hit.\r\n");
        return true;
    }
    return false;
}

bool NativeLord::spareFairy() {
    if (!fairy_) return false;
    int max = g16(78);
    if (max < 1) max = 1;
    s16(72, max);
    raw_[154] = 0;
    fairy_ = 0;
    saveFairy();
    save();
    send("  The fairy pulls you back.\r\n");
    return true;
}

void NativeLord::loadFairy() {
    fairy_ = 0;
    if (slot_ < 0) return;
    std::string b = readFile("FAIRY", 64);
    if ((int)b.size() > slot_) fairy_ = (unsigned char)b[(size_t)slot_] ? 1 : 0;
}

void NativeLord::saveFairy() {
    std::string b = readFile("FAIRY", 64);
    if ((int)b.size() <= slot_) b.resize((size_t)slot_ + 1, '\0');
    b[(size_t)slot_] = (char)(fairy_ ? 1 : 0);
    int h = fs_.open("FAIRY", 2, true, true);
    if (h < 0) return;
    fs_.write(h, b.data(), (int)b.size());
    fs_.close(h);
}

void NativeLord::logLine(const char* s) {
    if (!s || !s[0]) return;
    std::string line = std::string(s) + "\r\n---\r\n";
    int h = fs_.open("lognow.lrd", 2, true, false);
    if (h < 0) return;
    fs_.seek(h, 0, 2);
    fs_.write(h, line.data(), (int)line.size());
    fs_.close(h);
}

void NativeLord::rotateLog() {
    std::string cur = readFile("lognow.lrd", 8000);
    if (cur.empty()) return;
    int h = fs_.open("logold.lrd", 2, true, true);
    if (h >= 0) {
        fs_.write(h, cur.data(), (int)cur.size());
        fs_.close(h);
    }
    fs_.remove("lognow.lrd");
}

void NativeLord::dropLine(int slot, const std::string& line) {
    uint8_t rec[236];
    std::string to = "warrior";
    if (loadSlot(slot, rec)) {
        to = pnm(rec + 21, 50);
        if (to.empty()) to = pnm(rec, 20);
    }
    if (mailPost_) {
        mailPost_(mailCtx_, alias_.c_str(), to.c_str(), line.c_str());
        return;
    }
    char name[32];
    std::snprintf(name, sizeof name, "mail%d.lrd", slot);
    std::string body = line + "\n";
    int h = fs_.open(name, 2, true, false);
    if (h < 0) return;
    fs_.seek(h, 0, 2);
    fs_.write(h, body.data(), (int)body.size());
    fs_.close(h);
}

void NativeLord::maybeBirth() {
    if (raw_[162] != 5) return;
    int spouse = g16(221);
    if (spouse < 0) return;
    if ((int)roll(34) + 1 != 11) return;
    int kids = g16(210);
    int lays = g16(166);
    if (lays * 5 <= kids) return;
    int r = (int)roll(20) + 1;
    if (r == 20) {
        send("  The child is lost.\r\n");
        dropLine(spouse, "A child was lost.");
        return;
    }
    if (kids < 32000) s16(210, kids + 1);
    if (lays < 32000) s16(166, lays + 1);
    save();
    send(r < 10 ? "  A boy.\r\n" : "  A girl.\r\n");
    dropLine(spouse, "`K");
    logLine("A child was born.");
}

void NativeLord::applyCode(const std::string& line) {
    if (line.size() < 2 || line[0] != '`') return;
    char code = line[1];
    std::string rest = line.substr(2);
    bool neg = false;
    size_t i = 0;
    while (i < rest.size() && rest[i] == ' ') i++;
    if (i < rest.size() && rest[i] == '-') {
        neg = true;
        i++;
    }
    int amt = 0, any = 0;
    for (; i < rest.size() && std::isdigit((unsigned char)rest[i]); i++) {
        any = 1;
        amt = amt * 10 + (rest[i] - '0');
        if (amt > 2000000000) amt = 2000000000;
    }
    if (!any) amt = 1;
    if (neg) amt = -amt;
    auto add16 = [&](int off) {
        int v = cap16(g16(off) + amt);
        if (off == 72 || off == 78 || off == 117 || off == 119 || off == 121 || off == 156 || off == 105 || off == 107 || off == 210)
            if (v < 0) v = 0;
        s16(off, v);
    };
    switch (code) {
    case 'G': s32(109, capG((int64_t)g32(109) + amt)); break;
    case 'b': s32(113, capG((int64_t)g32(113) + amt)); break;
    case 'E': s32(158, capG((int64_t)g32(158) + amt)); break;
    case 'V': {
        int32_t xp = g32(158) - (amt < 0 ? -amt : amt);
        if (xp < 1) xp = 1;
        s32(158, xp);
        break;
    }
    case '{': add16(166); break;
    case '}': add16(121); break;
    case '+':
        if (any) {
            int c = neg ? -amt : amt;
            if (c < 0) c = 0;
            s16(121, cap16(c));
        }
        break;
    case 'K':
    case 'k': add16(210); break;
    case 'M': add16(119); break;
    case 'D': add16(117); break;
    case ',': add16(105); break;
    case ':': add16(107); break;
    case ';':
        if (any) s16(78, cap16(neg ? g16(78) + amt : amt));
        break;
    case 'S': bumpSkill(); break;
    case '-':
    case 'T':
    case 'Y':
    case 'U':
    case 'I':
    case 'P': send("  Reply?\r\n"); break;
    case 'o': send("  A challenge waits in town.\r\n"); break;
    default: break;
    }
}

void NativeLord::applyMail(const std::string& body, const char* from) {
    send("\r\n  Mail from ");
    send(from && from[0] ? from : "someone");
    send(".\r\n");
    size_t i = 0;
    while (i < body.size()) {
        size_t e = body.find('\n', i);
        if (e == std::string::npos) e = body.size();
        std::string line = body.substr(i, e - i);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty() && line[0] == '`') {
            if (line.size() > 1 && line[1] == '|') break;
            applyCode(line);
        } else if (!line.empty()) {
            send("  ");
            send(colorize(line));
            send("\r\n");
        }
        if (e == body.size()) break;
        i = e + 1;
    }
    save();
}

void NativeLord::takeMail() {
    if (slot_ < 0) return;
    char name[32];
    std::snprintf(name, sizeof name, "mail%d.lrd", slot_);
    std::string file = readFile(name, 4000);
    if (!file.empty()) {
        applyMail(file, "mail");
        fs_.remove(name);
    }
    if (!mailPull_) return;
    for (int n = 0; n < 8; n++) {
        char body[480];
        char from[40];
        body[0] = from[0] = 0;
        int num = mailPull_(mailCtx_, body, (int)sizeof body, from, (int)sizeof from);
        if (num <= 0) break;
        body[sizeof body - 1] = 0;
        from[sizeof from - 1] = 0;
        applyMail(body, from);
    }
}

void NativeLord::deliver(int slot, const std::string& body) {
    uint8_t rec[236];
    if (!loadSlot(slot, rec)) {
        send("  No such warrior.\r\n");
        town();
        return;
    }
    std::string to = pnm(rec + 21, 50);
    if (to.empty()) to = pnm(rec, 20);
    if (mailPost_) mailPost_(mailCtx_, alias_.c_str(), to.c_str(), body.c_str());
    else {
        char name[32];
        std::snprintf(name, sizeof name, "mail%d.lrd", slot);
        std::string out = body + "\n";
        int h = fs_.open(name, 2, true, false);
        if (h >= 0) {
            fs_.seek(h, 0, 2);
            fs_.write(h, out.data(), (int)out.size());
            fs_.close(h);
        }
    }
    send("  Sent.\r\n");
    town();
}

void NativeLord::startCompose(int slot) {
    composeTo_ = slot;
    compose_.clear();
    st_ = St::Compose;
    send(slot < 0 ? "  Announce: " : "  Message: ");
}

void NativeLord::composeKey(int c) {
    if (c == '\r' || c == '\n') {
        send("\r\n");
        std::string body = compose_;
        compose_.clear();
        if (composeTo_ < 0) {
            if (!body.empty()) logLine(body.c_str());
            send("  Announced.\r\n");
            town();
            return;
        }
        if (body.empty()) {
            send("  Okay.\r\n");
            town();
            return;
        }
        deliver(composeTo_, body);
        return;
    }
    if (c == 8 || c == 127) {
        if (!compose_.empty()) compose_.pop_back();
        return;
    }
    if (c >= 32 && c < 127 && compose_.size() < 78) {
        compose_.push_back((char)c);
        char s[2] = {(char)c, 0};
        send(s);
    }
}

void NativeLord::writeInfo() {
    char name[24];
    std::snprintf(name, sizeof name, "INFO.%d", owner_ > 0 ? owner_ : 0);
    char body[180];
    std::snprintf(body, sizeof body, "%d\r\nANSI\r\nRIP NO\r\nFAIRY %s\r\n99\r\n%s\r\n\r\n", slot_ < 0 ? 0 : slot_, fairy_ ? "YES" : "NO",
                  alias_.c_str());
    int h = fs_.open(name, 2, true, true);
    if (h < 0) return;
    fs_.write(h, body, (int)std::strlen(body));
    fs_.close(h);
}

void NativeLord::onBeginExtras() {
    loadFairy();
    takeMail();
    writeInfo();
}

bool NativeLord::ladyGet(const std::string& name, int& out) {
    std::string n = name;
    if (!n.empty() && n[0] == '&') n = n.substr(1);
    if (n.size() >= 3 && (n[0] == 'r' || n[0] == 'R') && (n[1] == 'n' || n[1] == 'N')) {
        int span = std::atoi(n.c_str() + 3);
        if (span < 1) span = 1;
        out = (int)roll((uint32_t)span);
        return true;
    }
    if (n.size() == 2 && (n[0] == 'N' || n[0] == 'n') && n[1] >= '1' && n[1] <= '4') {
        out = nreg_[n[1] - '1'];
        return true;
    }
    if (ieq(n, "Plv")) out = g16(127);
    else if (ieq(n, "Pgo")) out = g32(109);
    else if (ieq(n, "Pba")) out = g32(113);
    else if (ieq(n, "Pch")) out = g16(121);
    else if (ieq(n, "Pff")) out = g16(105);
    else if (ieq(n, "Pge")) out = g16(156);
    else if (ieq(n, "Pht")) out = g16(72);
    else if (ieq(n, "Phx")) out = g16(78);
    else if (ieq(n, "Psr")) out = g16(119);
    else if (ieq(n, "Pde")) out = g16(117);
    else if (ieq(n, "Pho")) out = g16(180) == 1;
    else if (ieq(n, "Phf")) out = fairy_;
    else if (ieq(n, "Pwe")) out = g16(80);
    else if (ieq(n, "Pcl")) out = raw_[179];
    else if (ieq(n, "Psx")) out = raw_[162] == 5;
    else if (ieq(n, "Pam")) out = amulet_;
    else if (ieq(n, "Pdd")) out = raw_[154] == 5;
    else if (ieq(n, "Psb")) out = raw_[163] != 0;
    else if (ieq(n, "rip")) out = 0;
    else return false;
    return true;
}

void NativeLord::ladySet(const std::string& name, int v) {
    std::string n = name;
    if (!n.empty() && n[0] == '&') n = n.substr(1);
    if (n.size() == 2 && (n[0] == 'N' || n[0] == 'n') && n[1] >= '1' && n[1] <= '4') {
        nreg_[n[1] - '1'] = v;
        return;
    }
    if (ieq(n, "Pgo")) s32(109, capG(v));
    else if (ieq(n, "Pba")) s32(113, capG(v));
    else if (ieq(n, "Pch")) s16(121, cap16(v < 0 ? 0 : v));
    else if (ieq(n, "Pff")) s16(105, cap16(v < 0 ? 0 : v));
    else if (ieq(n, "Pge")) s16(156, cap16(v < 0 ? 0 : v));
    else if (ieq(n, "Pht")) s16(72, cap16(v < 0 ? 0 : v));
    else if (ieq(n, "Phx")) s16(78, cap16(v < 1 ? 1 : v));
    else if (ieq(n, "Psr")) s16(119, cap16(v));
    else if (ieq(n, "Pde")) s16(117, cap16(v < 0 ? 0 : v));
    else if (ieq(n, "Plv")) s16(127, cap16(v < 1 ? 1 : v));
    else if (ieq(n, "Pho")) s16(180, v ? 1 : 0);
    else if (ieq(n, "Phf")) {
        fairy_ = v ? 1 : 0;
        saveFairy();
    } else if (ieq(n, "Pam")) amulet_ = v;
    else if (ieq(n, "Pdd")) raw_[154] = v ? 5 : 0;
    else if (ieq(n, "Psb")) raw_[163] = v ? 5 : 0;
    else if (ieq(n, "Pwe")) s16(80, cap16(v));
}

std::string NativeLord::expand(const std::string& s) {
    std::string o;
    for (size_t i = 0; i < s.size();) {
        if (s[i] != '&') {
            o.push_back(s[i++]);
            continue;
        }
        size_t j = i + 1;
        while (j < s.size() && std::isalnum((unsigned char)s[j])) j++;
        int v = 0;
        if (ladyGet(s.substr(i + 1, j - (i + 1)), v)) {
            char buf[16];
            std::snprintf(buf, sizeof buf, "%d", v);
            o += buf;
        } else o.append(s, i, j - i);
        i = j;
    }
    return o;
}

bool NativeLord::nextRaw(std::string& raw) {
    if (ladyPos_ >= script_.size()) return false;
    size_t e = script_.find('\n', ladyPos_);
    if (e == std::string::npos) {
        raw = script_.substr(ladyPos_);
        ladyPos_ = script_.size();
    } else {
        raw = script_.substr(ladyPos_, e - ladyPos_);
        ladyPos_ = e + 1;
    }
    if (!raw.empty() && raw.back() == '\r') raw.pop_back();
    return true;
}

bool NativeLord::loadScript(const char* file) {
    script_ = readFile(file, 24000);
    if (script_.empty()) {
        std::string alt = file;
        for (char& c : alt) c = (char)std::tolower((unsigned char)c);
        if (alt != file) script_ = readFile(alt, 24000);
    }
    ladyFile_ = script_.empty() ? "" : file;
    ladyPos_ = 0;
    ladyWait_ = false;
    ladyRp_ = 0;
    return !script_.empty();
}

bool NativeLord::ladyGoto(const std::string& name) {
    if (name.empty()) {
        ladyPos_ = 0;
        size_t pos = 0;
        while (pos < script_.size()) {
            size_t e = script_.find('\n', pos);
            if (e == std::string::npos) e = script_.size();
            std::string t = trimC(script_.substr(pos, e - pos));
            pos = e < script_.size() ? e + 1 : script_.size();
            if (t.size() >= 2 && t[0] == '@' && t[1] == '#') {
                ladyPos_ = pos;
                return true;
            }
        }
        ladyPos_ = 0;
        return true;
    }
    size_t pos = 0;
    while (pos < script_.size()) {
        size_t e = script_.find('\n', pos);
        if (e == std::string::npos) e = script_.size();
        std::string t = trimC(script_.substr(pos, e - pos));
        size_t next = e < script_.size() ? e + 1 : script_.size();
        if (t.size() >= 2 && t[0] == '@' && t[1] == '#') {
            std::string nm = trimC(t.substr(2));
            if (ieq(nm, name)) {
                ladyPos_ = next;
                return true;
            }
        }
        pos = next;
    }
    return false;
}

void NativeLord::ladyEnd() {
    if (ladyRp_ > 0) {
        ladyPos_ = ladyRet_[--ladyRp_];
        return;
    }
    script_.clear();
    script_.shrink_to_fit();
    ladyWait_ = false;
    save();
    if (raw_[154] == 5) {
        raw_[170] = 0;
        save();
        send("  You are dead.\r\n");
        st_ = St::Done;
        return;
    }
    town();
}

void NativeLord::execLine(const std::string& t) {
    if (t.empty() || t[0] == ';') return;
    if (t[0] != '@') {
        send(colorize(expand(t)));
        send("\r\n");
        return;
    }
    std::string w = wordOf(t);
    std::string rest = restOf(t);
    if (w == "endquest" || w == "endgame") {
        if (w == "endgame") raw_[154] = 5;
        ladyEnd();
        return;
    }
    if (w == "begin" || w == "end" || w == "endif" || w == "endcase" || w == "verreq" || w == "label" || w == "eventname" ||
        w == "author" || w == "authid" || w == "codebegin" || w == "codeend" || w == "target")
        return;
    if (w == "saveplayer") {
        save();
        return;
    }
    if (w == "cls" || w == "clrscr") {
        send("\x1b[2J\x1b[H");
        return;
    }
    if (w == "hitakey") {
        ladyWait_ = true;
        ladyKeys_.clear();
        return;
    }
    if (w == "writeln") {
        send(colorize(expand(rest)));
        send("\r\n");
        return;
    }
    if (w == "display") {
        std::string raw;
        while (nextRaw(raw)) {
            std::string u = trimC(raw);
            if (ieq(wordOf(u.empty() || u[0] != '@' ? "@" : u), "enddisplay") || ieq(u, "@enddisplay")) break;
            send(colorize(expand(raw)));
            send("\r\n");
        }
        return;
    }
    if (w == "goto") {
        size_t pos = 0;
        std::string want = trimC(rest);
        while (pos < script_.size()) {
            size_t e = script_.find('\n', pos);
            if (e == std::string::npos) e = script_.size();
            std::string line = trimC(script_.substr(pos, e - pos));
            size_t next = e < script_.size() ? e + 1 : script_.size();
            if (wordOf(line) == "label" && ieq(trimC(restOf(line)), want)) {
                ladyPos_ = next;
                return;
            }
            pos = next;
        }
        return;
    }
    if (w == "runsub") {
        std::string name, file;
        size_t sp = rest.find(' ');
        name = trimC(sp == std::string::npos ? rest : rest.substr(0, sp));
        file = sp == std::string::npos ? "" : trimC(rest.substr(sp + 1));
        if (!file.empty() && !ieq(file, "&filename") && !ieq(file, ladyFile_)) loadScript(file.c_str());
        else if (ladyRp_ < 4) ladyRet_[ladyRp_++] = ladyPos_;
        ladyGoto(name);
        return;
    }
    if (w == "choice") {
        std::string keys, prompt = rest;
        size_t sp = rest.find(' ');
        if (sp != std::string::npos) {
            keys = rest.substr(0, sp);
            prompt = rest.substr(sp + 1);
        } else keys = rest;
        ladyKeys_ = keys;
        ladyLoose_ = true;
        std::string plain;
        for (size_t i = 0; i < prompt.size(); i++) {
            if (prompt[i] == '`' && i + 1 < prompt.size()) {
                i++;
                continue;
            }
            plain.push_back(prompt[i]);
        }
        for (size_t i = 0; i + 2 < plain.size(); i++) {
            if (plain[i] == '[' && plain[i + 2] == ']') {
                char d = (char)std::toupper((unsigned char)plain[i + 1]);
                ladyLoose_ = d != 'N';
                ladyAns_ = d;
            }
        }
        ladyWait_ = true;
        send(colorize(expand(prompt)));
        send("\r\n");
        return;
    }
    if (w == "case") {
        while (st_ == St::Lady && !ladyWait_) {
            size_t mark = ladyPos_;
            std::string raw;
            if (!nextRaw(raw)) return;
            std::string u = trimC(raw);
            if (u.empty() || u[0] == ';') continue;
            char letter = 0;
            if (armOf(u, letter)) {
                bool take = letter == ladyAns_;
                while (true) {
                    size_t m2 = ladyPos_;
                    std::string raw2;
                    if (!nextRaw(raw2)) return;
                    std::string u2 = trimC(raw2);
                    char letter2 = 0;
                    if (armOf(u2, letter2) || wordOf(u2) == "endcase") {
                        ladyPos_ = m2;
                        break;
                    }
                    if (take && !u2.empty() && u2[0] != ';') execLine(u2);
                    if (ladyWait_) return;
                }
                continue;
            }
            if (wordOf(u) == "endcase") return;
            ladyPos_ = mark;
            return;
        }
        return;
    }
    if (w == "if") {
        // "&Pch < 10 1" or "&Pho is true"
        std::string tok[6];
        int nt = 0;
        for (size_t i = 0; i < rest.size() && nt < 6;) {
            while (i < rest.size() && rest[i] == ' ') i++;
            size_t j = i;
            while (j < rest.size() && rest[j] != ' ') j++;
            if (j > i) tok[nt++] = rest.substr(i, j - i);
            i = j;
        }
        int id = 0;
        if (nt > 0) {
            bool dig = true;
            for (char c : tok[nt - 1])
                if (!std::isdigit((unsigned char)c)) dig = false;
            if (dig && nt >= 3) {
                id = std::atoi(tok[nt - 1].c_str());
                nt--;
            }
        }
        bool pass = false;
        if (nt >= 3 && ieq(tok[1], "is")) {
            int v = 0;
            ladyGet(tok[0], v);
            pass = ieq(tok[2], "true") ? v != 0 : v == 0;
        } else if (nt >= 3) {
            int l = 0, r = 0;
            if (!ladyGet(tok[0], l)) l = std::atoi(tok[0].c_str());
            if (!ladyGet(tok[2], r)) r = std::atoi(tok[2].c_str());
            if (tok[1] == "<") pass = l < r;
            else if (tok[1] == ">") pass = l > r;
            else if (tok[1] == "=" || tok[1] == "==") pass = l == r;
            else if (tok[1] == "<=") pass = l <= r;
            else if (tok[1] == ">=") pass = l >= r;
        }
        if (!pass) {
            int nest = 0;
            std::string raw;
            while (nextRaw(raw)) {
                std::string u = trimC(raw);
                std::string uw = wordOf(u);
                if (uw == "if") nest++;
                else if (uw == "endif") {
                    if (nest == 0 && tailId(u) == id) break;
                    if (nest > 0) nest--;
                } else if (uw == "else" && nest == 0 && tailId(u) == id) break;
            }
        }
        return;
    }
    if (w == "else") {
        int id = tailId(t);
        int nest = 0;
        std::string raw;
        while (nextRaw(raw)) {
            std::string u = trimC(raw);
            std::string uw = wordOf(u);
            if (uw == "if") nest++;
            else if (uw == "endif") {
                if (nest == 0 && tailId(u) == id) break;
                if (nest > 0) nest--;
            }
        }
        return;
    }
    if (w == "set") {
        size_t to = rest.find(" to ");
        if (to == std::string::npos) return;
        std::string left = trimC(rest.substr(0, to));
        std::string right = trimC(rest.substr(to + 4));
        int cur = 0;
        ladyGet(left, cur);
        if (ieq(right, "true")) ladySet(left, 1);
        else if (ieq(right, "false")) ladySet(left, 0);
        else {
            bool rel = !right.empty() && (right[0] == '+' || right[0] == '-');
            int sign = 1;
            std::string e = right;
            if (rel) {
                if (right[0] == '-') sign = -1;
                e = right.substr(1);
            }
            int v = 0;
            if (!ladyGet(e, v)) v = std::atoi(e.c_str());
            ladySet(left, rel ? cur + sign * v : v);
        }
        return;
    }
    if (w == "math") {
        // &N1 = 7 * &Plv
        size_t eq = rest.find('=');
        if (eq == std::string::npos) return;
        std::string left = trimC(rest.substr(0, eq));
        std::string rhs = trimC(rest.substr(eq + 1));
        std::string a, op, b;
        size_t i = 0;
        auto tok = [&](std::string& o) {
            while (i < rhs.size() && rhs[i] == ' ') i++;
            size_t j = i;
            while (j < rhs.size() && rhs[j] != ' ') j++;
            o = rhs.substr(i, j - i);
            i = j;
        };
        tok(a);
        tok(op);
        tok(b);
        int va = 0, vb = 0;
        if (!ladyGet(a, va)) va = std::atoi(a.c_str());
        int r = va;
        if (!op.empty()) {
            if (!ladyGet(b, vb)) vb = std::atoi(b.c_str());
            if (op == "*") r = va * vb;
            else if (op == "+") r = va + vb;
            else if (op == "-") r = va - vb;
            else if (op == "/" && vb) r = va / vb;
        }
        ladySet(left, r);
    }
}

void NativeLord::ladyRun() {
    int guard = 0;
    while (st_ == St::Lady && !ladyWait_ && guard++ < 400) {
        std::string raw;
        if (!nextRaw(raw)) {
            ladyEnd();
            return;
        }
        execLine(trimC(raw));
    }
    if (guard >= 400) ladyEnd();
}

void NativeLord::ladyKey(int c) {
    if (!ladyWait_) return;
    ladyWait_ = false;
    if (!ladyKeys_.empty()) {
        char u = (char)std::toupper((unsigned char)c);
        if (ladyLoose_) ladyAns_ = u == 'N' ? 'N' : 'Y';
        else ladyAns_ = u == 'Y' ? 'Y' : 'N';
    }
    ladyRun();
}

bool NativeLord::playSection(const char* file, const char* section) {
    if (!loadScript(file)) return false;
    if (!ladyGoto(section ? section : "") && !ladyGoto("")) {
        script_.clear();
        return false;
    }
    st_ = St::Lady;
    ladyRun();
    return true;
}

bool NativeLord::playForestEvent() {
    if (raw_[229] == 5) {
        int lv = g16(127);
        if (lv < 1) lv = 1;
        int g = (int)roll((uint32_t)lv) + 1;
        int gems = g16(156) + g;
        if (gems > 32000) gems = 32000;
        s16(156, gems);
        raw_[229] = 0;
        save();
        send("  You find ");
        num(g);
        send(" gems.\r\n");
        return true;
    }
    if (!loadScript("EVENTS.LDY")) return false;
    std::string names[16];
    int n = 0;
    size_t pos = 0;
    while (pos < script_.size() && n < 16) {
        size_t e = script_.find('\n', pos);
        if (e == std::string::npos) e = script_.size();
        std::string t = trimC(script_.substr(pos, e - pos));
        pos = e < script_.size() ? e + 1 : script_.size();
        if (t.size() >= 2 && t[0] == '@' && t[1] == '#') {
            std::string nm = trimC(t.substr(2));
            if (g16(180) != 1 && ieq(nm, "HORSE")) continue;
            names[n++] = nm;
        }
    }
    if (n < 1) {
        script_.clear();
        return false;
    }
    int pick = n == 1 ? 0 : (int)roll((uint32_t)n);
    if (pick < 0 || pick >= n) pick = 0;
    if (!ladyGoto(names[pick])) ladyGoto("");
    st_ = St::Lady;
    ladyWait_ = false;
    ladyRun();
    return true;
}

void NativeLord::showLog() {
    send("\r\n  Today\r\n");
    std::string t = readFile("lognow.lrd", 4000);
    if (t.empty()) send("  Nothing logged today.\r\n");
    else {
        for (char c : t) send(c == '\n' ? "\r\n" : std::string(1, c));
        if (t.back() != '\n') send("\r\n");
    }
    send("  (C)ontinue (T)oday (Y)esterday\r\n");
    st_ = St::Log;
}

void NativeLord::showPlaces() {
    std::string b = readFile("3RDPARTY.LRD", 4000);
    if (b.empty()) b = readFile("3rdparty.lrd", 4000);
    if (b.empty()) b = readFile("3RDPARTY.DAT", 4000);
    placeCount_ = 0;
    send("\r\n  Other places\r\n");
    std::string pending;
    bool have = false;
    size_t start = 0;
    for (size_t i = 0; i <= b.size(); i++) {
        if (i != b.size() && b[i] != '\n') continue;
        std::string line = trimC(b.substr(start, i - start));
        start = i + 1;
        if (line.empty() || line[0] == ';') continue;
        if (!have) {
            pending = line;
            have = true;
        } else {
            placeCount_++;
            send("  ");
            num(placeCount_);
            send(". ");
            send(colorize(line));
            send("\r\n");
            have = false;
            if (placeCount_ >= 12) break;
        }
    }
    if (placeCount_ == 0) send("  No other places.\r\n");
    else send("  Number (0 = cancel)\r\n");
    st_ = St::Places;
}

void NativeLord::listMarried() {
    std::string b = readFile("PLAYER.DAT", 24000);
    int n = (int)(b.size() / 236);
    int shown = 0;
    send("\r\n");
    for (int i = 0; i < n && i < 40; i++) {
        const uint8_t* rec = (const uint8_t*)b.data() + i * 236;
        if (get16(rec + 221) < 0) continue;
        std::string name = pnm(rec, 20);
        if (name.empty()) continue;
        send("  ");
        num(i + 1);
        send(". ");
        send(name);
        send("\r\n");
        shown++;
    }
    if (!shown) send("  No marriages.\r\n");
}

void NativeLord::violet() {
    if (raw_[162] == 5) {
        send("  Not interested.\r\n");
        return;
    }
    if (g16(125) != 0) {
        send("  Already today.\r\n");
        return;
    }
    s16(125, 5);
    raw_[232] = 5;
    int c = g16(121) + 1;
    if (c > 32000) c = 32000;
    s16(121, c);
    save();
    if (!expert_) scene("VIOLET");
    send("  Violet.\r\n  Charm up.\r\n");
}

void NativeLord::seth() {
    if (raw_[162] != 5) {
        send("  Not interested.\r\n");
        return;
    }
    if (raw_[232] == 5) {
        send("  Already today.\r\n");
        return;
    }
    raw_[232] = 5;
    int c = g16(121) + 1;
    if (c > 32000) c = 32000;
    s16(121, c);
    save();
    if (!expert_) scene("SETH");
    send("  Seth.\r\n  Charm up.\r\n");
}

void NativeLord::bard() {
    if (raw_[163]) {
        send("  Already today.\r\n");
        return;
    }
    raw_[163] = 5;
    save();
    if (!expert_) scene("BARD");
    const char* sec = raw_[162] == 5 ? "FEMALESONGS" : "MALESONGS";
    if (playSection("BARDSONG.LDY", "BARDSONG")) return;
    if (playSection("EVENTS.LDY", sec)) return;
    if (playSection("EVENTS.LDY", "BARDSONG")) return;
    send("  No song on file.\r\n");
}

void NativeLord::startDragon() {
    if (g16(123) != 0) {
        send("\r\n  Already fought today.\r\n");
        return;
    }
    s16(123, 5);
    save();
    std::string b = readFile("dragon.bin", 400);
    if (b.size() < 239) b = readFile("DRAGON.DAT", 400);
    mon_ = Fighter{};
    if (b.size() >= 239) {
        const uint8_t* p = (const uint8_t*)b.data();
        mon_.name = pnm(p, 60);
        mon_.str = get32(p + 61);
        mon_.gold = get32(p + 65);
        mon_.weapon = pnm(p + 69, 60);
        mon_.xp = get32(p + 130);
        mon_.hp = get32(p + 134);
        mon_.death = pnm(p + 138, 100);
    }
    if (mon_.hp <= 0) {
        mon_.name = "Dragon";
        mon_.str = 2000;
        mon_.gold = 1000;
        mon_.xp = 1000;
        mon_.hp = 15000;
        mon_.weapon = "breath";
    }
    mon_.def = 0;
    if (!expert_) {
        if (!emitRaw("LAIRANS.ANS", 24000)) scene("LAIR");
    }
    dragon_ = true;
    master_ = false;
    pvpSlot_ = -1;
    duelLive_ = false;
    st_ = St::Fight;
    send("\r\n  ");
    send(mon_.name);
    send("   HP ");
    num((int32_t)mon_.hp);
    send("\r\n");
    fightPrompt();
}

void NativeLord::kingQuest() {
    if (g16(127) < 12) {
        send("  Not ready.\r\n");
        return;
    }
    startDragon();
}

void NativeLord::finishDragon(bool won) {
    dragon_ = false;
    duelLive_ = false;
    if (won) {
        send("  You win.\r\n  The dragon falls.\r\n");
        int king = g16(212);
        if (king < 32000) king++;
        s16(212, king);
        s16(127, 1);
        s16(78, 20);
        s16(72, 20);
        std::string wn, an;
        int32_t price = 0, bonus = 0;
        if (!gear("WEAPONS.DAT", 1, wn, price, bonus) || wn.empty()) wn = "Stick";
        if (!gear("ARMOR.DAT", 1, an, price, bonus) || an.empty()) an = "Coat";
        s16(80, 1);
        putName(82, 20, wn);
        s32(109, 500);
        s32(113, 0);
        s16(117, 1);
        s16(119, 10);
        s16(156, 10);
        putName(131, 20, an);
        s16(152, 1);
        raw_[154] = 0;
        raw_[155] = 0;
        s32(158, 10);
        int fights = 15 + (g16(210) > 0 ? g16(210) : 0);
        if (fights > 32000) fights = 32000;
        s16(105, fights);
        s16(107, 3);
        raw_[232] = 0;
        raw_[231] = 5;
        s16(123, 5);
        save();
        logLine("A warrior felled the dragon.");
        if (king >= 3) {
            send("  Your quest is over.\r\n");
            raw_[170] = 0;
            save();
            st_ = St::Done;
            return;
        }
        town();
        return;
    }
    s32(109, 0);
    s16(72, 0);
    raw_[154] = 5;
    raw_[170] = 0;
    save();
    logLine("A warrior fell to the dragon.");
    send("  You are dead.\r\n");
    st_ = St::Done;
}

void NativeLord::realmKey(char k, int c) {
    if (st_ == St::Class) {
        int cls = k == 'k' ? 1 : k == 'd' ? 2 : k == 'l' ? 3 : 0;
        if (!cls) {
            send("  Pick one of those.\r\n");
            return;
        }
        raw_[179] = (uint8_t)cls;
        save();
        send("  Class set.\r\n");
        town();
        return;
    }
    if (st_ == St::Duel) {
        if (k == 'q') {
            uint8_t rec[kDuel];
            if (loadDuel(rec) && rec[0] == 1 && get16(rec + 2) == slot_) {
                std::memset(rec, 0, sizeof rec);
                storeDuel(rec);
            }
            send("  No.\r\n");
            clearDuel();
            town();
        }
        return;
    }
    if (st_ == St::Log) {
        if (k == 't') showLog();
        else if (k == 'y') {
            std::string y = readFile("logold.lrd", 4000);
            send("\r\n  Yesterday\r\n");
            if (y.empty()) send("  Apparently nothing of importance happened yesterday.\r\n");
            else {
                for (char ch : y) send(ch == '\n' ? "\r\n" : std::string(1, ch));
                if (y.back() != '\n') send("\r\n");
            }
            send("  (C)ontinue (T)oday (Y)esterday\r\n");
        } else town();
        return;
    }
    if (st_ == St::Places) {
        if (c < '0' || c > '9') return;
        int n = c - '0';
        if (n == 0 || n > placeCount_) {
            town();
            return;
        }
        std::string b = readFile("3RDPARTY.LRD", 4000);
        if (b.empty()) b = readFile("3rdparty.lrd", 4000);
        if (b.empty()) b = readFile("3RDPARTY.DAT", 4000);
        std::string cmd, desc, pending;
        bool have = false;
        int seen = 0;
        size_t start = 0;
        for (size_t i = 0; i <= b.size(); i++) {
            if (i != b.size() && b[i] != '\n') continue;
            std::string line = trimC(b.substr(start, i - start));
            start = i + 1;
            if (line.empty() || line[0] == ';') continue;
            if (!have) {
                pending = line;
                have = true;
            } else {
                seen++;
                if (seen == n) {
                    cmd = pending;
                    desc = line;
                    break;
                }
                have = false;
            }
        }
        send("\r\n  ");
        send(colorize(desc));
        send("\r\n");
        for (char& ch : cmd)
            if (ch == '*') {
                char num[8];
                std::snprintf(num, sizeof num, "%d", owner_ > 0 ? owner_ : 1);
                // one star only: replace this character by splicing below
                (void)num;
            }
        std::string ran;
        for (size_t i = 0; i < cmd.size(); i++) {
            if (cmd[i] == '*') {
                char num[8];
                std::snprintf(num, sizeof num, "%d", owner_ > 0 ? owner_ : 1);
                ran += num;
            } else ran.push_back(cmd[i]);
        }
        bool ldy = ran.size() >= 4 && ieq(ran.substr(ran.size() - 4), ".LDY");
        if (ldy) {
            playSection(ran.c_str(), "");
            return;
        }
        send("  The DOS runtime is not in this firmware, so that .EXE cannot start.\r\n");
        town();
    }
}

}  // namespace lord
}  // namespace axis
