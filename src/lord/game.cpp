// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include "lord/game.h"

#include <cstring>
#include <ctime>

#pragma GCC optimize("Os")  // menus and screens: size over speed (the door CPU and transfers stay -O2)

namespace axis {
namespace lord {
namespace {

constexpr int kRec = 236;
constexpr int kMon = 239;
constexpr int O_NAME = 0;
constexpr int O_BBS = 21;
constexpr int O_HP = 72;
constexpr int O_HPMAX = 78;
constexpr int O_FIGHTS = 105;
constexpr int O_GOLD = 109;
constexpr int O_BANK = 113;
constexpr int O_DEF = 117;
constexpr int O_STR = 119;
constexpr int O_CHARM = 121;
constexpr int O_LEVEL = 127;
constexpr int O_ARM = 131;
constexpr int O_DEAD = 154;
constexpr int O_XP = 158;
constexpr int O_WEAP = 80;
constexpr int O_SEEN = 103;
constexpr int O_PFIGHTS = 107;
constexpr int O_DRAGON = 123;
constexpr int O_VIOLET = 125;
constexpr int O_ARMN = 152;
constexpr int O_INN = 155;
constexpr int O_GEM = 156;
constexpr int O_ON = 170;
constexpr int O_CLASS = 179;
constexpr int O_HORSE = 180;
constexpr int O_KIDS = 210;
constexpr int O_SKILL = 214;
constexpr int O_USES = 217;
constexpr int O_PKILL = 227;
constexpr int32_t kCap = 2000000000;

int32_t capGold(int64_t v) {
    if (v > kCap) return kCap;
    if (v < 0) return 0;
    return (int32_t)v;
}

int usesLeft(const uint8_t* raw) {
    int c = raw[O_CLASS];
    int off = c == 1 ? O_USES : c == 2 ? O_USES + 1 : c == 3 ? O_USES + 2 : 0;
    if (!off) return 0;
    int u = (int)(int8_t)raw[off];
    return u > 0 ? u : 0;
}

int i16(const uint8_t* p) { return (int16_t)(p[0] | (p[1] << 8)); }
int32_t i32(const uint8_t* p) { return (int32_t)(uint32_t)(p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24)); }
void w16(uint8_t* p, int v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
void w32(uint8_t* p, int32_t v) {
    uint32_t u = (uint32_t)v;
    p[0] = (uint8_t)u;
    p[1] = (uint8_t)(u >> 8);
    p[2] = (uint8_t)(u >> 16);
    p[3] = (uint8_t)(u >> 24);
}

std::string pstr(const uint8_t* p, int cap) {
    int n = p[0];
    if (n < 0) n = 0;
    if (n > cap) n = cap;
    return std::string((const char*)p + 1, (const char*)p + 1 + n);
}

std::string trim(std::string s) {
    while (!s.empty() && (s.back() == ' ' || s.back() == '\0')) s.pop_back();
    return s;
}

bool same(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); i++) {
        char x = a[i], y = b[i];
        if (x >= 'A' && x <= 'Z') x = (char)(x - 'A' + 'a');
        if (y >= 'A' && y <= 'Z') y = (char)(y - 'A' + 'a');
        if (x != y) return false;
    }
    return true;
}

const char* klass(uint8_t c) {
    if (c == 1) return "Death Knight";
    if (c == 2) return "Mystic";
    if (c == 3) return "Thief";
    return "warrior";
}

uint32_t fallbackRng() {
    static uint32_t s = 0xC0FFEEu;
    s = s * 1664525u + 1013904223u;
    return s;
}

}  // namespace

NativeLord::NativeLord(door::HostFs& fs, door::ByteStream& line, std::string alias, door::LockTable* locks, int owner)
    : fs_(fs), line_(line), locks_(locks), alias_(trim(std::move(alias))), owner_(owner), rng_(fallbackRng) {}

void NativeLord::send(const char* s) {
    if (!s || !*s) return;
    line_.write((const uint8_t*)s, std::strlen(s));
}

void NativeLord::send(const std::string& s) {
    if (!s.empty()) line_.write((const uint8_t*)s.data(), s.size());
}

void NativeLord::num(int32_t v) {
    char b[16];
    char* p = b + 15;
    *p = 0;
    uint32_t u = v < 0 ? (uint32_t)(-v) : (uint32_t)v;
    do {
        *--p = (char)('0' + (u % 10u));
        u /= 10u;
    } while (u);
    if (v < 0) *--p = '-';
    send(p);
}

uint32_t NativeLord::roll(uint32_t n) { return n == 0 ? 0 : rng_() % n; }

int NativeLord::strikeOf(int str) {
    int half = str / 2;
    if (half < 1) half = 1;
    return (int)roll((uint32_t)half) + half;
}

std::string NativeLord::colorize(const std::string& s) const {
    std::string o;
    o.reserve(s.size() + 8);
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] != '`') {
            o.push_back(s[i]);
            continue;
        }
        if (++i >= s.size()) break;
        char c = s[i];
        const char* esc = nullptr;
        switch (c) {
        case 'c': esc = "\x1b[2J\x1b[H"; break;
        case '1': esc = "\x1b[0;34m"; break;
        case '2': esc = "\x1b[0;32m"; break;
        case '3': esc = "\x1b[0;36m"; break;
        case '4': esc = "\x1b[0;31m"; break;
        case '5': esc = "\x1b[0;35m"; break;
        case '6': esc = "\x1b[0;33m"; break;
        case '7': esc = "\x1b[0;37m"; break;
        case '8': esc = "\x1b[1;30m"; break;
        case '9': esc = "\x1b[1;34m"; break;
        case '0': esc = "\x1b[1;32m"; break;
        case '!': esc = "\x1b[1;36m"; break;
        case '@': esc = "\x1b[1;31m"; break;
        case '#': esc = "\x1b[1;35m"; break;
        case '$': esc = "\x1b[1;33m"; break;
        case '%': esc = "\x1b[1;37m"; break;
        case '*': esc = "\x1b[0;30;41m"; break;
        default: break;
        }
        if (c == 'r' && i + 1 < s.size()) {
            char b = s[++i];
            switch (b) {
            case '0': esc = "\x1b[40m"; break;
            case '1': esc = "\x1b[44m"; break;
            case '2': esc = "\x1b[42m"; break;
            case '3': esc = "\x1b[43m"; break;
            case '4': esc = "\x1b[41m"; break;
            case '5': esc = "\x1b[45m"; break;
            case '6': esc = "\x1b[47m"; break;
            case '7': esc = "\x1b[46m"; break;
            default: break;
            }
        }
        if (esc) o += esc;
    }
    return o;
}

std::string NativeLord::readFile(const std::string& name, uint32_t maxBytes) {
    door::FileInfo fi;
    if (!fs_.stat(name, fi) || fi.size == 0 || fi.size > maxBytes) return {};
    int h = fs_.open(name, 0, false, false);
    if (h < 0) return {};
    std::string b(fi.size, '\0');
    int n = fs_.read(h, &b[0], (int)fi.size);
    fs_.close(h);
    if (n < 0) n = 0;
    b.resize((size_t)n);
    return b;
}

bool NativeLord::emitRaw(const std::string& file, uint32_t cap) {
    door::FileInfo fi;
    if (!fs_.stat(file, fi) || fi.size == 0 || fi.size > 400000u) return false;
    int h = fs_.open(file, 0, false, false);
    if (h < 0) return false;
    send("\x1b[2J\x1b[H");
    uint8_t buf[512];
    uint32_t sent = 0;
    while (sent < fi.size && sent < cap) {
        int n = fs_.read(h, buf, (int)sizeof buf);
        if (n <= 0) break;
        if (sent + (uint32_t)n > cap) n = (int)(cap - sent);
        line_.write(buf, (size_t)n);
        sent += (uint32_t)n;
    }
    fs_.close(h);
    if (sent == 0) return false;
    send("\r\n");
    return true;
}

bool NativeLord::emitMarked(const std::string& file, const std::string& name) {
    door::FileInfo fi;
    if (!fs_.stat(file, fi) || fi.size == 0 || fi.size > 400000u) return false;
    int h = fs_.open(file, 0, false, false);
    if (h < 0) return false;
    const std::string tag = "@#" + name;
    std::string carry;
    bool in = false, any = false, stop = false;
    uint32_t sent = 0;
    uint8_t buf[512];
    auto take = [&](std::string line) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!in) {
            if (line == tag) in = true;
            return;
        }
        if (line.size() >= 2 && line[0] == '@' && line[1] == '#') {
            stop = true;
            return;
        }
        std::string out = colorize(line);
        out += "\r\n";
        if (sent + out.size() > 24000u) {
            stop = true;
            return;
        }
        send(out);
        sent += (uint32_t)out.size();
        any = true;
    };
    while (!stop) {
        int n = fs_.read(h, buf, (int)sizeof buf);
        if (n <= 0) break;
        carry.append((const char*)buf, (size_t)n);
        size_t cut;
        while (!stop && (cut = carry.find('\n')) != std::string::npos) {
            take(carry.substr(0, cut));
            carry.erase(0, cut + 1);
        }
        if (carry.size() > 4000) {
            take(carry);
            carry.clear();
        }
    }
    if (in && !stop && !carry.empty()) take(carry);
    fs_.close(h);
    return any;
}

void NativeLord::scene(const std::string& name) {
    if (emitRaw(name + ".ANS", 24000)) return;
    if (emitMarked("lordtxt.lrd", name)) return;
    emitMarked("LORDTXT.DAT", name);
}

bool NativeLord::loadPlayer() {
    door::FileInfo fi;
    if (!fs_.stat("PLAYER.DAT", fi) || fi.size < (uint32_t)kRec) return false;
    int h = fs_.open("PLAYER.DAT", 0, false, false);
    if (h < 0) return false;
    int nrec = (int)(fi.size / kRec);
    for (int i = 0; i < nrec; i++) {
        if (fs_.read(h, raw_, kRec) != kRec) break;
        std::string name = trim(pstr(raw_ + O_NAME, 20));
        std::string bbs = trim(pstr(raw_ + O_BBS, 50));
        if (same(name, alias_) || same(bbs, alias_)) {
            slot_ = i;
            have_ = true;
            fs_.close(h);
            return true;
        }
    }
    fs_.close(h);
    return false;
}

void NativeLord::save() { saveOther(-1, nullptr); }

void NativeLord::saveOther(int slot, const uint8_t* rec) {
    if (slot_ < 0) return;
    int h = fs_.open("PLAYER.DAT", 2, false, false);
    if (h < 0) return;
    lockKey_ = fs_.fileKey(h);
    if (locks_) {
        locked_ = locks_->lock(lockKey_, 0, 0x7fffffffu, owner_);
        if (!locked_) {
            fs_.close(h);
            send("\r\n  The warrior file is in use.\r\n");
            return;
        }
    }
    fs_.seek(h, (int64_t)slot_ * kRec, 0);
    fs_.write(h, raw_, kRec);
    if (slot >= 0 && rec && slot != slot_) {
        fs_.seek(h, (int64_t)slot * kRec, 0);
        fs_.write(h, rec, kRec);
    }
    if (locked_ && locks_) {
        locks_->unlock(lockKey_, 0, 0x7fffffffu, owner_);
        locked_ = false;
    }
    fs_.close(h);
}

bool NativeLord::loadSlot(int slot, uint8_t* rec) {
    if (slot < 0 || !rec) return false;
    door::FileInfo fi;
    if (!fs_.stat("PLAYER.DAT", fi) || fi.size < (uint32_t)(slot + 1) * (uint32_t)kRec) return false;
    int h = fs_.open("PLAYER.DAT", 0, false, false);
    if (h < 0) return false;
    bool ok = fs_.seek(h, (int64_t)slot * kRec, 0) >= 0 && fs_.read(h, rec, kRec) == kRec;
    fs_.close(h);
    return ok;
}

int NativeLord::g16(int off) const { return i16(raw_ + off); }
int32_t NativeLord::g32(int off) const { return i32(raw_ + off); }
void NativeLord::s16(int off, int v) {
    if (v > 32767) v = 32767;
    if (v < -32768) v = -32768;
    w16(raw_ + off, v);
}
void NativeLord::s32(int off, int32_t v) { w32(raw_ + off, v); }

void NativeLord::putName(int off, int cap, const std::string& s) {
    int n = (int)s.size();
    if (n > cap) n = cap;
    if (n < 0) n = 0;
    raw_[off] = (uint8_t)n;
    for (int i = 0; i < cap; i++) raw_[off + 1 + i] = i < n ? (uint8_t)s[(size_t)i] : 0;
}

bool NativeLord::loadMonster(Fighter& out) {
    std::string b = readFile("LENEMY.DAT", 400000);
    int n = (int)(b.size() / kMon);
    if (n < 1) return false;
    int level = i16(raw_ + O_LEVEL);
    if (level < 1) level = 1;
    int idx = (level - 1) * 11 + (int)roll(11);
    if (idx >= n) idx = (int)roll((uint32_t)n);
    const uint8_t* p = (const uint8_t*)b.data() + idx * kMon;
    out.name = trim(pstr(p, 60));
    out.str = i32(p + 61);
    out.gold = i32(p + 65);
    out.weapon = trim(pstr(p + 69, 60));
    out.xp = i32(p + 130);
    out.hp = i32(p + 134);
    out.death = trim(pstr(p + 138, 100));
    out.def = 0;
    if (out.name.empty()) out.name = "something";
    return out.hp > 0;
}

bool NativeLord::gear(const char* file, int index, std::string& name, int32_t& price, int32_t& bonus) {
    if (index < 1 || index > 15) return false;
    std::string b = readFile(file, 5000);
    int off = (index - 1) * 264;
    if ((int)b.size() < off + 264) return false;
    const uint8_t* p = (const uint8_t*)b.data() + off;
    name = trim(pstr(p, 255));
    price = i32(p + 256);
    bonus = i32(p + 260);
    if (price < 0) price = 0;
    return true;
}

int NativeLord::gearNeed(bool armor, int n) {
    if (n < 3) return 0;
    std::string b = readFile("TRAINERS.DAT", 8000);
    if (b.size() < 219) return 0;
    int ret = armor ? 0 : 10;
    for (int i = 1; i < n - 1; i++) {
        int off = (i - 1) * 219;
        if ((size_t)off + 219 > b.size()) break;
        const uint8_t* p = (const uint8_t*)b.data() + off;
        ret += i16(p + (armor ? 213 : 211));
    }
    return ret < 0 ? 0 : ret;
}

void NativeLord::listGear(const char* file) {
    std::string b = readFile(file, 5000);
    int n = (int)(b.size() / 264);
    if (n > 15) n = 15;
    if (n < 1) {
        send("  Nothing for sale.\r\n");
        return;
    }
    for (int i = 0; i < n; i++) {
        const uint8_t* p = (const uint8_t*)b.data() + i * 264;
        send("  ");
        num(i + 1);
        send(". ");
        send(colorize(trim(pstr(p, 255))));
        send("  ");
        num(i32(p + 256));
        send("\r\n");
    }
}

bool NativeLord::trainerOf(int level, Fighter& out, int& hpG, int& strG, int& defG, int32_t& need) {
    if (level < 1 || level > 11) return false;
    std::string b = readFile("TRAINERS.DAT", 8000);
    int off = (level - 1) * 219;
    if ((int)b.size() < off + 219) return false;
    const uint8_t* p = (const uint8_t*)b.data() + off;
    out = Fighter{};
    out.name = trim(pstr(p, 30));
    out.weapon = trim(pstr(p + 31, 20));
    out.str = i16(p + 205);
    out.hp = i16(p + 207);
    hpG = i16(p + 209);
    strG = i16(p + 211);
    defG = i16(p + 213);
    need = i32(p + 215);
    out.def = 0;
    if (out.name.empty()) out.name = "master";
    return true;
}

int32_t NativeLord::sellPrice(int32_t price) {
    if (price < 0) price = 0;
    int level = g16(O_LEVEL);
    if (level < 1) level = 1;
    int cha = g16(O_CHARM);
    if (cha < 0) cha = 0;
    int64_t mult = (int64_t)level * cha * level;
    uint32_t span = (mult > 0 && mult < 65530) ? (uint32_t)mult : 65535u;
    int32_t p = price / 2 + (int32_t)roll(span);
    if (p > price - p / 3) p = price - price / 3;
    if (p < 0) p = 0;
    return p;
}

void NativeLord::beginAmount(Ask a, const char* prompt) {
    ask_ = a;
    amt_.clear();
    st_ = St::Amount;
    if (prompt) send(prompt);
}

void NativeLord::askYes(Ask a, const char* prompt) {
    ask_ = a;
    st_ = St::Yes;
    if (prompt) send(prompt);
}

void NativeLord::placePrompt() {
    if (st_ == St::Bank) {
        send("\r\n  Gold ");
        num(g32(O_GOLD));
        send("   bank ");
        num(g32(O_BANK));
        send("\r\n  (D)eposit  (W)ithdraw  (Q)uit\r\n");
    } else if (st_ == St::King || st_ == St::Armor) {
        bool armor = st_ == St::Armor;
        std::string w = trim(pstr(raw_ + (armor ? O_ARM : 82), 20));
        if (w.empty()) w = "none";
        send(armor ? "\r\n  Armor: " : "\r\n  Weapon: ");
        send(w);
        send(armor ? "\r\n  (B)uy (S)ell (L)ist (V)iew (Q)uit\r\n"
                   : "\r\n  (B)uy (S)ell (L)ist (V)iew (J) (Q)uit\r\n");
    } else if (st_ == St::Train) {
        Fighter tr;
        int hpG = 0, strG = 0, defG = 0;
        int32_t need = 0;
        if (trainerOf(g16(O_LEVEL), tr, hpG, strG, defG, need)) {
            send("\r\n  Master ");
            send(tr.name);
            send(".\r\n");
        } else {
            send("\r\n  No master on file.\r\n");
        }
        send("  (A)ttack (V)iew (Q)uit\r\n");
    } else if (st_ == St::Inn) {
        send("\r\n  The Inn\r\n  (G) room (V)iolet (S)eth (B)ard (Q)uit\r\n");
    } else if (st_ == St::Pvp) {
        send("\r\n  Slaughter\r\n  (N)umber (L)ist (Q)uit\r\n");
    }
}

void NativeLord::returnTo(Ask a) {
    amt_.clear();
    ask_ = Ask::None;
    if (a == Ask::Dep || a == Ask::With) st_ = St::Bank;
    else if (a == Ask::BuyW || a == Ask::SellW) st_ = St::King;
    else if (a == Ask::BuyA || a == Ask::SellA) st_ = St::Armor;
    else if (a == Ask::Room) st_ = St::Inn;
    else if (a == Ask::PvpN) st_ = St::Pvp;
    else st_ = St::Town;
    if (st_ == St::Town) town();
    else placePrompt();
}

void NativeLord::doBuy(bool armor) {
    std::string name;
    int32_t price = 0, bonus = 0;
    const char* file = armor ? "ARMOR.DAT" : "WEAPONS.DAT";
    if (!gear(file, pendingN_, name, price, bonus)) {
        send("  No such item.\r\n");
    } else {
        int need = gearNeed(armor, pendingN_);
        int held = g16(armor ? O_ARMN : O_WEAP);
        int stat = g16(armor ? O_DEF : O_STR);
        if (stat < need) send(armor ? "  Not enough defense.\r\n" : "  Not strong enough.\r\n");
        else if (held > 0) send("  Already holding one.\r\n");
        else if (g32(O_GOLD) < pendingPrice_) send("  Not enough gold.\r\n");
        else {
            s32(O_GOLD, g32(O_GOLD) - pendingPrice_);
            int64_t ns = (int64_t)stat + bonus;
            if (ns > 32000) ns = 32000;
            if (ns < 0) ns = 0;
            s16(armor ? O_DEF : O_STR, (int)ns);
            s16(armor ? O_ARMN : O_WEAP, pendingN_);
            putName(armor ? O_ARM : 82, 20, name);
            save();
            send("  Bought.\r\n");
        }
    }
    st_ = armor ? St::Armor : St::King;
    placePrompt();
}

void NativeLord::doSell(bool armor) {
    std::string name;
    int32_t price = 0, bonus = 0;
    const char* file = armor ? "ARMOR.DAT" : "WEAPONS.DAT";
    int held = g16(armor ? O_ARMN : O_WEAP);
    if (held < 1 || !gear(file, held, name, price, bonus)) {
        send("  Nothing to sell.\r\n");
    } else {
        s32(O_GOLD, capGold((int64_t)g32(O_GOLD) + pendingPrice_));
        int stat = g16(armor ? O_DEF : O_STR) - (int)bonus;
        if (armor) {
            if (stat < 0) stat = 0;
        } else if (stat < 5) stat = 5;
        s16(armor ? O_DEF : O_STR, stat);
        s16(armor ? O_ARMN : O_WEAP, 0);
        putName(armor ? O_ARM : 82, 20, "");
        save();
        send("  Sold.\r\n");
    }
    st_ = armor ? St::Armor : St::King;
    placePrompt();
}

void NativeLord::doRoom() {
    int level = g16(O_LEVEL);
    if (level < 1) level = 1;
    int cost = 400 * level;
    if (g16(O_CHARM) > 99) {
        send("  The room is free.\r\n");
    } else if (g32(O_GOLD) < cost) {
        send("  Not enough gold.\r\n");
        st_ = St::Inn;
        placePrompt();
        return;
    } else {
        s32(O_GOLD, g32(O_GOLD) - cost);
    }
    raw_[O_INN] = 5;
    raw_[O_ON] = 0;
    save();
    send("  You take the room.\r\n");
    st_ = St::Done;
}

void NativeLord::startPreparedFight() {
    if (pvpSlot_ < 0) {
        st_ = St::Pvp;
        placePrompt();
        return;
    }
    if (g16(O_PFIGHTS) < 1) {
        pvpSlot_ = -1;
        send("  No player fights left today.\r\n");
        st_ = St::Pvp;
        placePrompt();
        return;
    }
    if (outOfRealm(pvpSlot_)) {
        send("  Out of the realm.\r\n");
        pvpSlot_ = -1;
        st_ = St::Pvp;
        placePrompt();
        return;
    }
    if (pvpRaw_[O_ON] == 1) {
        offerOnline();
        return;
    }
    s16(O_PFIGHTS, g16(O_PFIGHTS) - 1);
    save();
    master_ = false;
    st_ = St::Fight;
    send("\r\n  Player fight.\r\n  ");
    send(mon_.name);
    send("   HP ");
    num(mon_.hp);
    send("\r\n");
    fightPrompt();
}

void NativeLord::applyYes(bool yes) {
    Ask a = ask_;
    ask_ = Ask::None;
    amt_.clear();
    if (a == Ask::DuelY) {
        answerDuel(yes);
        return;
    }
    if (!yes) {
        if (a == Ask::PvpN) pvpSlot_ = -1;
        send("  No.\r\n");
        returnTo(a);
        return;
    }
    if (a == Ask::BuyW) doBuy(false);
    else if (a == Ask::BuyA) doBuy(true);
    else if (a == Ask::SellW) doSell(false);
    else if (a == Ask::SellA) doSell(true);
    else if (a == Ask::Room) doRoom();
    else if (a == Ask::PvpN) startPreparedFight();
    else returnTo(a);
}

void NativeLord::commitAmount() {
    int64_t n = 0;
    for (char c : amt_) {
        if (c < '0' || c > '9') continue;
        n = n * 10 + (c - '0');
        if (n > kCap) n = kCap;
    }
    Ask a = ask_;
    amt_.clear();
    if (a == Ask::Dep || a == Ask::With) {
        send("\r\n");
        int32_t hand = g32(O_GOLD);
        int32_t bank = g32(O_BANK);
        int64_t amt = n;
        if (a == Ask::Dep) {
            if (amt == 1) amt = hand;
            if ((int64_t)bank + amt > kCap) amt = kCap - bank;
            if (amt == 0) send("  Okay.\r\n");
            else if (amt > hand) send("  Not enough gold.\r\n");
            else if (bank >= kCap) {
                s32(O_BANK, kCap);
                send("  The bank is full.\r\n");
            } else {
                s32(O_GOLD, (int32_t)(hand - amt));
                s32(O_BANK, (int32_t)(bank + amt));
                save();
                send("  Deposited.\r\n");
            }
        } else {
            if (amt == 1) amt = bank;
            if ((int64_t)hand + amt > kCap) amt = kCap - hand;
            if (amt == 0) send(hand >= kCap ? "  Your hands are full.\r\n" : "  Okay.\r\n");
            else if (amt > bank) send("  Not enough gold.\r\n");
            else {
                s32(O_BANK, (int32_t)(bank - amt));
                s32(O_GOLD, (int32_t)(hand + amt));
                save();
                send("  Withdrawn.\r\n");
            }
        }
        st_ = St::Bank;
        ask_ = Ask::None;
        placePrompt();
        return;
    }
    if (a == Ask::BuyW || a == Ask::BuyA) {
        bool armor = a == Ask::BuyA;
        send("\r\n");
        if (n < 1 || n > 15) {
            send("  No such item.\r\n");
            st_ = armor ? St::Armor : St::King;
            ask_ = Ask::None;
            placePrompt();
            return;
        }
        std::string name;
        int32_t price = 0, bonus = 0;
        if (!gear(armor ? "ARMOR.DAT" : "WEAPONS.DAT", (int)n, name, price, bonus)) {
            send("  No such item.\r\n");
            st_ = armor ? St::Armor : St::King;
            ask_ = Ask::None;
            placePrompt();
            return;
        }
        pendingN_ = (int)n;
        pendingPrice_ = price;
        int need = gearNeed(armor, pendingN_);
        send("  ");
        send(colorize(name));
        send(" costs ");
        num(price);
        send(" gold. Need ");
        num(need);
        send(".\r\n  Buy? [N]\r\n");
        askYes(a, nullptr);
        return;
    }
    if (a == Ask::PvpN) {
        send("\r\n");
        if (n <= 0) {
            send("  Okay.\r\n");
            st_ = St::Pvp;
            ask_ = Ask::None;
            placePrompt();
            return;
        }
        int slot = (int)n - 1;
        if (slot == slot_) send("  Not you.\r\n");
        else if (!loadSlot(slot, pvpRaw_)) send("  No such warrior.\r\n");
        else if (pvpRaw_[O_DEAD] == 5 || i16(pvpRaw_ + O_HP) <= 0) send("  That warrior is dead.\r\n");
        else if (pvpRaw_[O_INN] == 5) send("  That warrior is at the inn.\r\n");
        else {
            pvpSlot_ = slot;
            mon_ = Fighter{};
            mon_.name = trim(pstr(pvpRaw_ + O_NAME, 20));
            if (mon_.name.empty()) mon_.name = "warrior";
            mon_.weapon = trim(pstr(pvpRaw_ + 82, 20));
            mon_.str = i16(pvpRaw_ + O_STR);
            mon_.def = i16(pvpRaw_ + O_DEF);
            mon_.hp = i16(pvpRaw_ + O_HP);
            mon_.gold = i32(pvpRaw_ + O_GOLD);
            mon_.xp = i32(pvpRaw_ + O_XP);
            int max = i16(pvpRaw_ + O_HPMAX);
            if (mon_.hp < max) mon_.hp = max;
            send("  Attack? [Y]\r\n");
            askYes(Ask::PvpN, nullptr);
            return;
        }
        pvpSlot_ = -1;
        st_ = St::Pvp;
        ask_ = Ask::None;
        placePrompt();
        return;
    }
    if (a == Ask::MailTo) {
        send("\r\n");
        ask_ = Ask::None;
        if (n <= 0) {
            send("  Okay.\r\n");
            town();
            return;
        }
        startCompose((int)n - 1);
        return;
    }
    ask_ = Ask::None;
    st_ = St::Town;
    town();
}

void NativeLord::stats() {
    send("\r\n  ");
    send(trim(pstr(raw_ + O_NAME, 20)));
    send("   level ");
    num(i16(raw_ + O_LEVEL));
    send("  ");
    send(klass(raw_[O_CLASS]));
    send("\r\n  HP ");
    num(i16(raw_ + O_HP));
    send("/");
    num(i16(raw_ + O_HPMAX));
    send("   forest ");
    num(i16(raw_ + O_FIGHTS));
    send("\r\n  gold ");
    num(i32(raw_ + O_GOLD));
    send("   bank ");
    num(i32(raw_ + O_BANK));
    send("   xp ");
    num(i32(raw_ + O_XP));
    send("\r\n  ");
    send(trim(pstr(raw_ + 82, 20)));
    send("   ");
    send(trim(pstr(raw_ + O_ARM, 20)));
    send("\r\n  str ");
    num(i16(raw_ + O_STR));
    send("   def ");
    num(i16(raw_ + O_DEF));
    send("   charm ");
    num(i16(raw_ + O_CHARM));
    send("\r\n  gems ");
    num(g16(O_GEM));
    send("   player fights ");
    num(g16(O_PFIGHTS));
    send("\r\n");
}

void NativeLord::townMenu() {
    send("\r\n  The Town Square\r\n");
    send("  (F)orest (S)laughter (K)ing (A)rmor (H)ealers (V)iew\r\n");
    send("  (I)nn (T)rain (Y)bank (L)ist (P)eople (X)pert (Q)uit (?)\r\n");
    send("  (W)rite (D)aily (O)ther (M)sg (C)ouples\r\n");
}

void NativeLord::town() {
    master_ = false;
    pvpSlot_ = -1;
    ask_ = Ask::None;
    duelLive_ = false;
    dragon_ = false;
    if (needClass_) {
        needClass_ = false;
        st_ = St::Class;
        send("\r\n  Skill 40. (K) Death Knight (D) Mystic (L) Thief\r\n");
        return;
    }
    st_ = St::Town;
    if (!expert_) scene("MAIN");
    townMenu();
}

void NativeLord::heal() {
    int hp = i16(raw_ + O_HP);
    int max = i16(raw_ + O_HPMAX);
    int level = i16(raw_ + O_LEVEL);
    if (level < 1) level = 1;
    int32_t gold = i32(raw_ + O_GOLD);
    if (!expert_) scene("HEAL");
    if (hp >= max) {
        send("\r\n  You look fine.\r\n");
        return;
    }
    int need = max - hp;
    int32_t cost = (int32_t)need * 5 * level;
    if (gold >= cost) {
        w16(raw_ + O_HP, max);
        w32(raw_ + O_GOLD, gold - cost);
        save();
        send("\r\n  ");
        num(need);
        send(" hit points are healed.\r\n");
        return;
    }
    int afford = (int)(gold / 5 / level);
    if (afford > 0) {
        w16(raw_ + O_HP, hp + afford);
        w32(raw_ + O_GOLD, gold - (int32_t)afford * 5 * level);
        save();
        send("\r\n  ");
        num(afford);
        send(" hit points are healed.\r\n");
        return;
    }
    send("\r\n  Not enough gold.\r\n");
}

void NativeLord::fightPrompt() {
    send("  (A)ttack  (R)un  (S)tats");
    if (usesLeft(raw_) > 0) send("  (K)");
    send("\r\n");
}

void NativeLord::forest() {
    master_ = false;
    pvpSlot_ = -1;
    if (raw_[O_DEAD] == 5 || i16(raw_ + O_HP) <= 0) {
        send("\r\n  This warrior is dead.\r\n");
        return;
    }
    if (i16(raw_ + O_FIGHTS) <= 0) {
        send("\r\n  No forest fights left today.\r\n");
        return;
    }
    if (!expert_) scene("FOREST");
    if (roll(5) == 1 && playForestEvent()) return;
    if (!loadMonster(mon_)) {
        send("\r\n  The forest is empty.\r\n");
        return;
    }
    send("\r\n  ");
    send(mon_.name);
    send("   HP ");
    num(mon_.hp);
    send("\r\n");
    fightPrompt();
    st_ = St::Fight;
}

void NativeLord::enemyStrike() {
    int atk = strikeOf((int)mon_.str) - i16(raw_ + O_DEF);
    if (atk < 1) {
        send("  ");
        send(mon_.name);
        send(" misses you.\r\n");
        return;
    }
    if (soften()) return;
    int hp = i16(raw_ + O_HP) - atk;
    send("  ");
    send(mon_.name);
    if (!mon_.weapon.empty()) {
        send(" hits with its ");
        send(mon_.weapon);
    } else {
        send(" hits");
    }
    send(" for ");
    num(atk);
    send(".\r\n");
    if (hp < 1) {
        if (pvpSlot_ < 0 && !master_ && spareFairy()) return;
        w16(raw_ + O_HP, 0);
        raw_[O_DEAD] = 5;
    } else {
        w16(raw_ + O_HP, hp);
    }
}

void NativeLord::bumpSkill() {
    int c = raw_[O_CLASS];
    int off = c == 1 ? O_SKILL : c == 2 ? O_SKILL + 1 : c == 3 ? O_SKILL + 2 : 0;
    int use = c == 1 ? O_USES : c == 2 ? O_USES + 1 : c == 3 ? O_USES + 2 : 0;
    if (!off) return;
    int s = (int)(int8_t)raw_[off];
    if (s < 0) s = 0;
    if (s >= 40) {
        bool all = (int)(int8_t)raw_[O_SKILL] > 39 && (int)(int8_t)raw_[O_SKILL + 1] > 39 && (int)(int8_t)raw_[O_SKILL + 2] > 39;
        send(all ? "  Already mastered.\r\n" : "  Skill capped.\r\n");
        return;
    }
    s += 1;
    raw_[off] = (uint8_t)s;
    if (s == 40) needClass_ = true;
    if (use && (c == 2 || s % 4 == 0)) {
        int u = (int)(int8_t)raw_[use];
        if (u < 0) u = 0;
        if (u < 127) raw_[use] = (uint8_t)(u + 1);
    }
    send("  Skill ");
    num(s);
    send(".\r\n");
}

void NativeLord::finishFight(bool won) {
    if (dragon_) {
        finishDragon(won);
        return;
    }
    if (duelLive_) clearDuel();
    if (master_) {
        if (won) {
            int max = g16(O_HPMAX) + mon_.hpG;
            if (max > 32000) max = 32000;
            if (max < 1) max = 1;
            s16(O_HPMAX, max);
            s16(O_HP, max);
            int str = g16(O_STR) + mon_.strG;
            if (str > 32000) str = 32000;
            s16(O_STR, str);
            int def = g16(O_DEF) + mon_.defG;
            if (def > 32000) def = 32000;
            if (def < 0) def = 0;
            s16(O_DEF, def);
            int lv = g16(O_LEVEL) + 1;
            if (lv > 32000) lv = 32000;
            s16(O_LEVEL, lv);
            s16(O_SEEN, 0);
            send("  Level ");
            num(lv);
            send(".\r\n");
            bumpSkill();
        } else {
            raw_[O_DEAD] = 0;
            s16(O_HP, g16(O_HPMAX));
            send("  Your master lets you live.\r\n");
        }
        master_ = false;
        save();
        town();
        return;
    }
    if (pvpSlot_ >= 0) {
        int slot = pvpSlot_;
        bool dead = raw_[O_DEAD] == 5 || g16(O_HP) <= 0;
        if (dead) {
            int32_t xp = g32(O_XP) - g32(O_XP) / 10;
            if (xp < 0) xp = 0;
            s32(O_XP, xp);
            s32(O_GOLD, 0);
            raw_[O_DEAD] = 5;
            s16(O_HP, 0);
            int32_t ox = i32(pvpRaw_ + O_XP);
            w32(pvpRaw_ + O_XP, capGold((int64_t)ox + xp / 2));
            int kills = i16(pvpRaw_ + O_PKILL);
            if (kills < 32000) w16(pvpRaw_ + O_PKILL, kills + 1);
            int hp = (int)mon_.hp;
            if (hp < 0) hp = 0;
            if (hp > 32000) hp = 32000;
            w16(pvpRaw_ + O_HP, hp);
            raw_[O_ON] = 0;
            send("  You are dead.\r\n");
            saveOther(slot, pvpRaw_);
            pvpSlot_ = -1;
            st_ = St::Done;
            return;
        }
        if (won) {
            int32_t og = i32(pvpRaw_ + O_GOLD);
            int32_t ox = i32(pvpRaw_ + O_XP);
            s32(O_GOLD, capGold((int64_t)g32(O_GOLD) + og));
            s32(O_XP, capGold((int64_t)g32(O_XP) + ox / 2));
            int gems = i16(pvpRaw_ + O_GEM);
            if (gems >= 2) {
                int take = gems / 2;
                int mine = g16(O_GEM) + take;
                if (mine > 32000) mine = 32000;
                s16(O_GEM, mine);
                w16(pvpRaw_ + O_GEM, gems - take);
                send("  You find ");
                num(take);
                send(take == 1 ? " gem.\r\n" : " gems.\r\n");
            }
            w32(pvpRaw_ + O_GOLD, 0);
            int32_t left = i32(pvpRaw_ + O_XP);
            w32(pvpRaw_ + O_XP, left - left / 10);
            pvpRaw_[O_DEAD] = 5;
            pvpRaw_[O_INN] = 0;
            w16(pvpRaw_ + O_HP, 0);
            int pk = g16(O_PKILL);
            if (pk < 32000) s16(O_PKILL, pk + 1);
            send("  You win.\r\n");
            saveOther(slot, pvpRaw_);
        } else {
            save();
        }
        pvpSlot_ = -1;
        st_ = St::Pvp;
        placePrompt();
        return;
    }
    int fights = i16(raw_ + O_FIGHTS);
    if (fights > 0) w16(raw_ + O_FIGHTS, fights - 1);
    if (won) {
        w32(raw_ + O_GOLD, capGold((int64_t)i32(raw_ + O_GOLD) + mon_.gold));
        w32(raw_ + O_XP, capGold((int64_t)i32(raw_ + O_XP) + mon_.xp));
        send("  You win.\r\n");
    }
    save();
    if (raw_[O_DEAD] == 5) {
        send("  You are dead.\r\n");
        raw_[O_ON] = 0;
        save();
        st_ = St::Done;
        return;
    }
    town();
}

void NativeLord::land(int atk) {
    if (atk < 1) {
        send("  You miss ");
        send(mon_.name);
        send(".\r\n");
        enemyStrike();
    } else {
        mon_.hp -= atk;
        send("  You hit ");
        send(mon_.name);
        send(" for ");
        num(atk);
        send(".\r\n");
        if (mon_.hp <= 0) {
            if (pvpSlot_ < 0 && !master_ && !mon_.death.empty()) {
                send("  ");
                send(mon_.death);
                send("\r\n");
            }
            if (atk > g16(O_STR)) {
                uint32_t g = roll(3);
                if (g == 0) {
                    int gem = g16(O_GEM) + 1;
                    if (gem > 32000) gem = 32000;
                    s16(O_GEM, gem);
                    send("  You find a gem.\r\n");
                } else if (g == 1 && mon_.gold > 0) {
                    if (mon_.gold > kCap / 2) mon_.gold = kCap;
                    else mon_.gold *= 2;
                    send("  You find more gold.\r\n");
                }
            }
            finishFight(true);
            return;
        }
        enemyStrike();
    }
    if (raw_[O_DEAD] == 5) finishFight(false);
    else if (mon_.hp <= 0 && st_ == St::Fight) finishFight(true);
}

void NativeLord::strike() {
    int atk = strikeOf(g16(O_STR));
    if (pvpSlot_ >= 0) atk -= (int)mon_.def;
    if ((int)roll(10) + 1 > 9) {
        atk *= 3;
        send("  Power move.\r\n");
    }
    land(atk);
}

void NativeLord::skill() {
    if (master_ || (pvpSlot_ >= 0 && g16(O_LEVEL) >= i16(pvpRaw_ + O_LEVEL))) {
        send("  Honor holds.\r\n");
        return;
    }
    int c = raw_[O_CLASS];
    int off = c == 1 ? O_USES : c == 2 ? O_USES + 1 : c == 3 ? O_USES + 2 : 0;
    if (!off || (int)(int8_t)raw_[off] < 1) {
        send("  No uses left.\r\n");
        return;
    }
    raw_[off] = (uint8_t)((int)(int8_t)raw_[off] - 1);
    int atk = strikeOf(g16(O_STR));
    if (pvpSlot_ >= 0) atk -= (int)mon_.def;
    atk *= 3;
    if (atk < 1) atk = 1;
    send("  Skill.\r\n");
    if (duelLive_) {
        duelStrike(true);
        return;
    }
    land(atk);
}

void NativeLord::wakeIfNewDay() {
    if (slot_ < 0) return;
    time_t now = time(nullptr);
    if (now < 1000000000) return;
    unsigned today = (unsigned)(now / 86400);
    if (today > 65535u) today = 65535u;
    std::string b = readFile("NATIVEDAY", 4096);
    int need = (slot_ + 1) * 2;
    bool missing = (int)b.size() < need;
    unsigned stored = 0;
    if (!missing) stored = (unsigned)(uint8_t)b[slot_ * 2] | ((unsigned)(uint8_t)b[slot_ * 2 + 1] << 8);
    auto stamp = [&]() {
        if ((int)b.size() < need) b.resize((size_t)need, '\0');
        b[slot_ * 2] = (char)(today & 0xff);
        b[slot_ * 2 + 1] = (char)((today >> 8) & 0xff);
        int h = fs_.open("NATIVEDAY", 2, true, true);
        if (h < 0) return;
        fs_.write(h, b.data(), (int)b.size());
        fs_.close(h);
    };
    if (missing || stored == 0) {
        stamp();
        return;
    }
    if (stored == today) return;
    // player.time (offset 129) is LORD's own day count. This clock lives in NATIVEDAY.
    s16(O_HP, g16(O_HPMAX));
    s16(O_SEEN, 0);
    s16(O_DRAGON, 0);
    s16(O_VIOLET, 0);
    raw_[O_DEAD] = 0;
    raw_[163] = 0;
    raw_[232] = 0;
    s32(223, 0);
    int fights = 15;
    int kids = g16(O_KIDS);
    if (kids > 0) fights += kids;
    if (fights > 32000) fights = 32000;
    if (g16(O_HORSE) == 1) {
        fights += fights / 4;
        if (fights > 32000) fights = 32000;
    }
    s16(O_FIGHTS, fights);
    s16(O_PFIGHTS, 3);
    int sk = raw_[O_CLASS];
    int sw = (int)(int8_t)raw_[O_SKILL];
    int sm = (int)(int8_t)raw_[O_SKILL + 1];
    int sth = (int)(int8_t)raw_[O_SKILL + 2];
    if (sw < 0) sw = 0;
    if (sm < 0) sm = 0;
    if (sth < 0) sth = 0;
    int lw = sw / 4;
    int lm = sm;
    int lt = sth / 4;
    if (sk == 1) lw += 1;
    if (sk == 2) lm += 1;
    if (sk == 3) lt += 1;
    if (lw > 127) lw = 127;
    if (lm > 127) lm = 127;
    if (lt > 127) lt = 127;
    raw_[O_USES] = (uint8_t)lw;
    raw_[O_USES + 1] = (uint8_t)lm;
    raw_[O_USES + 2] = (uint8_t)lt;
    rotateLog();
    maybeBirth();
    int32_t bank = g32(O_BANK);
    if (bank >= kCap) s32(O_BANK, kCap);
    else s32(O_BANK, capGold((int64_t)bank + bank / 10));
    send("\r\n  A new day.\r\n");
    stamp();
}

void NativeLord::listPlayers(bool onlineOnly) {
    door::FileInfo fi;
    if (!fs_.stat("PLAYER.DAT", fi) || fi.size < (uint32_t)kRec) {
        send("  No warriors.\r\n");
        return;
    }
    int h = fs_.open("PLAYER.DAT", 0, false, false);
    if (h < 0) return;
    int nrec = (int)(fi.size / (uint32_t)kRec);
    uint8_t rec[kRec];
    int shown = 0;
    for (int i = 0; i < nrec; i++) {
        if (fs_.read(h, rec, kRec) != kRec) break;
        std::string name = trim(pstr(rec + O_NAME, 20));
        if (name.empty() || name == "X" || name == "x") continue;
        if (onlineOnly && rec[O_ON] != 1) continue;
        send("  ");
        num(i + 1);
        send(". ");
        send(name);
        send("  level ");
        num(i16(rec + O_LEVEL));
        if (rec[O_DEAD] == 5) send("  dead");
        if (rec[O_ON] == 1) send("  on");
        send("\r\n");
        if (++shown >= 40) break;
    }
    fs_.close(h);
    if (shown == 0) send("  No warriors.\r\n");
}

int ensureWarrior(door::HostFs& fs, door::LockTable* locks, int owner, const std::string& aliasIn) {
    std::string alias = trim(aliasIn);
    if (alias.size() > 50) alias.resize(50);
    if (alias.empty()) return -1;
    auto matches = [&](const uint8_t* rec) {
        return same(trim(pstr(rec + O_NAME, 20)), alias) || same(trim(pstr(rec + O_BBS, 50)), alias);
    };
    auto put = [](uint8_t* raw, int off, int cap, const std::string& s) {
        int n = (int)s.size();
        if (n > cap) n = cap;
        if (n < 0) n = 0;
        raw[off] = (uint8_t)n;
        for (int i = 0; i < cap; i++) raw[off + 1 + i] = i < n ? (uint8_t)s[(size_t)i] : 0;
    };
    constexpr int kFull = 147;
    int h = fs.open("PLAYER.DAT", 2, true, false);
    if (h < 0) return -1;
    int key = fs.fileKey(h);
    bool locked = false;
    if (locks) {
        locked = locks->lock(key, 0, 0x7fffffffu, owner);
        if (!locked) {
            fs.close(h);
            return -1;
        }
    }
    auto finish = [&](int result) {
        if (locked && locks) locks->unlock(key, 0, 0x7fffffffu, owner);
        fs.close(h);
        return result;
    };
    door::FileInfo fi;
    if (!fs.stat("PLAYER.DAT", fi)) return finish(-1);
    if (fi.size % (uint32_t)kRec != 0) return finish(-1);
    int nrec = (int)(fi.size / (uint32_t)kRec);
    if (nrec > kFull) return finish(-1);
    uint8_t rec[kRec];
    for (int i = 0; i < nrec; i++) {
        if (fs.read(h, rec, kRec) != kRec) return finish(-1);
        if (matches(rec)) return finish(0);
    }
    if (nrec == kFull) return finish(-1);
    std::memset(rec, 0, kRec);
    put(rec, O_NAME, 20, alias);
    put(rec, O_BBS, 50, alias);
    put(rec, 82, 20, "Stick");
    put(rec, O_ARM, 20, "Coat");
    w16(rec + O_HP, 20);
    w16(rec + O_HPMAX, 20);
    w16(rec + O_WEAP, 1);
    w16(rec + O_FIGHTS, 15);
    w16(rec + O_PFIGHTS, 3);
    w32(rec + O_GOLD, 500);
    w16(rec + O_DEF, 1);
    w16(rec + O_STR, 10);
    w16(rec + O_CHARM, 1);
    w16(rec + O_LEVEL, 1);
    w16(rec + O_ARMN, 1);
    w16(rec + 221, -1);  // not married
    rec[O_CLASS] = 1;
    rec[O_USES] = 1;
    if (fs.seek(h, (int64_t)nrec * kRec, 0) < 0 || fs.write(h, rec, kRec) != kRec) return finish(-1);
    return finish(1);
}

void NativeLord::begin() {
    send("\x1b[2J\x1b[H");
    int made = 0;
    if (!loadPlayer()) {
        made = ensureWarrior(fs_, locks_, owner_, alias_);
        if (made >= 0) loadPlayer();
    }
    if (!loadPlayer()) {
        send(made < 0 ? "The warrior file could not be written.\r\n" : "No warrior for this name.\r\n");
        st_ = St::Done;
        return;
    }
    if (made == 1) send("\r\n  Your warrior is in the realm.\r\n");
    wakeIfNewDay();
    onBeginExtras();
    raw_[O_ON] = 1;
    save();
    if (st_ == St::Duel || st_ == St::Lady || st_ == St::Class) return;
    if (raw_[O_INN] == 5) {
        st_ = St::Inn;
        if (!expert_) scene(raw_[162] == 5 ? "RDIF" : "RDI");
        placePrompt();
    } else {
        town();
    }
}

bool NativeLord::step() {
    if (st_ == St::Done) return false;
    if (!line_.carrier()) {
        if (have_) {
            raw_[O_ON] = 0;
            save();
        }
        return false;
    }
    pollDuel();
    while (st_ != St::Done && line_.available() > 0) {
        int c = line_.read();
        if (c < 0) break;
        if (st_ == St::Lady) {
            ladyKey(c);
            continue;
        }
        if (st_ == St::Compose) {
            composeKey(c);
            continue;
        }
        if (st_ == St::Amount) {
            if (c == '\r' || c == '\n') commitAmount();
            else if ((c == 8 || c == 127) && !amt_.empty()) amt_.pop_back();
            else if (c >= '0' && c <= '9' && amt_.size() < 10) {
                amt_.push_back((char)c);
                char s[2] = {(char)c, 0};
                send(s);
            }
            continue;
        }
        if (c == 0x1b || c == '\r' || c == '\n') continue;
        char k = (char)(c | 0x20);
        if (st_ == St::Log || st_ == St::Places || st_ == St::Class || st_ == St::Duel) {
            realmKey(k, c);
            continue;
        }
        if (st_ == St::Yes) {
            bool loose = ask_ == Ask::Room || ask_ == Ask::PvpN;
            applyYes(loose ? k != 'n' : k == 'y');
            continue;
        }
        if (st_ == St::Fight) {
            if (k == 'a') {
                if (duelLive_) duelStrike(false);
                else strike();
            } else if (k == 's') stats();
            else if (k == 'k' || (k == 'd' && raw_[O_CLASS] == 1) || (k == 'm' && raw_[O_CLASS] == 2) ||
                     (k == 't' && raw_[O_CLASS] == 3))
                skill();
            else if (k == 'r') {
                if (duelLive_) {
                    send("  You get away.\r\n");
                    finishFight(false);
                } else if (master_) send("  You cannot run.\r\n");
                else if (roll(9) == 1) {
                    send("  ");
                    send(mon_.name);
                    send(" sees you.\r\n");
                    enemyStrike();
                    if (raw_[O_DEAD] == 5) finishFight(false);
                } else {
                    send("  You get away.\r\n");
                    finishFight(false);
                }
            }
            continue;
        }
        if (st_ == St::Bank) {
            if (k == 'q' || k == 'r') town();
            else if (k == 'v') stats();
            else if (k == '?') placePrompt();
            else if (k == 'd') beginAmount(Ask::Dep, "  Amount (1 = all, 0 = cancel): ");
            else if (k == 'w') beginAmount(Ask::With, "  Amount (1 = all, 0 = cancel): ");
            continue;
        }
        if (st_ == St::King || st_ == St::Armor) {
            bool armor = st_ == St::Armor;
            if (k == 'q' || k == 'r') town();
            else if (k == 'v') stats();
            else if (k == 'j' && !armor) kingQuest();
            else if (k == '?') placePrompt();
            else if (k == 'l') listGear(armor ? "ARMOR.DAT" : "WEAPONS.DAT");
            else if (k == 'b') beginAmount(armor ? Ask::BuyA : Ask::BuyW, "  Number (0 = cancel): ");
            else if (k == 's') {
                int held = g16(armor ? O_ARMN : O_WEAP);
                std::string name;
                int32_t price = 0, bonus = 0;
                if (held < 1 || !gear(armor ? "ARMOR.DAT" : "WEAPONS.DAT", held, name, price, bonus))
                    send("  Nothing to sell.\r\n");
                else {
                    pendingN_ = held;
                    pendingPrice_ = sellPrice(price);
                    send("\r\n  Sell for ");
                    num(pendingPrice_);
                    send(" gold? [N]\r\n");
                    askYes(armor ? Ask::SellA : Ask::SellW, nullptr);
                }
            }
            continue;
        }
        if (st_ == St::Inn) {
            if (k == 'q' || k == 'r') {
                raw_[O_INN] = 0;
                save();
                town();
            } else if (k == 'v') violet();
            else if (k == 's') seth();
            else if (k == 'b') bard();
            else if (k == '?') placePrompt();
            else if (k == 'g') {
                int level = g16(O_LEVEL);
                if (level < 1) level = 1;
                send("\r\n  Room is ");
                num(400 * level);
                send(" gold. Agree? [Y]\r\n");
                askYes(Ask::Room, nullptr);
            }
            continue;
        }
        if (st_ == St::Pvp) {
            if (k == 'q' || k == 'r') town();
            else if (k == 'v') stats();
            else if (k == '?') placePrompt();
            else if (k == 'l') listPlayers(false);
            else if (k == 'n' || k == 's') beginAmount(Ask::PvpN, "  Number (0 = cancel): ");
            continue;
        }
        if (st_ == St::Train) {
            if (k == 'q' || k == 'r') town();
            else if (k == '?') placePrompt();
            else if (k == 'v') {
                Fighter tr;
                int hpG = 0, strG = 0, defG = 0;
                int32_t need = 0;
                if (!trainerOf(g16(O_LEVEL), tr, hpG, strG, defG, need)) send("  No master on file.\r\n");
                else {
                    send("  Experience ");
                    num(g32(O_XP));
                    send(" / ");
                    num(need);
                    send(".\r\n");
                }
            } else if (k == 'a') {
                Fighter tr;
                int hpG = 0, strG = 0, defG = 0;
                int32_t need = 0;
                int lv = g16(O_LEVEL);
                if (lv >= 12 || !trainerOf(lv, tr, hpG, strG, defG, need)) send("  No master on file.\r\n");
                else if (g16(O_SEEN) != 0) send("  Already fought today.\r\n");
                else if (g32(O_XP) < need) {
                    s16(O_SEEN, 5);
                    save();
                    send("  Need ");
                    num(need - g32(O_XP));
                    send(" more experience.\r\n");
                } else {
                    s16(O_SEEN, 5);
                    save();
                    mon_ = tr;
                    mon_.def = 0;
                    mon_.hpG = hpG;
                    mon_.strG = strG;
                    mon_.defG = defG;
                    mon_.gold = 0;
                    mon_.xp = 0;
                    mon_.death.clear();
                    master_ = true;
                    pvpSlot_ = -1;
                    st_ = St::Fight;
                    send("\r\n  Master fight.\r\n  ");
                    send(mon_.name);
                    send("   HP ");
                    num(mon_.hp);
                    send("\r\n");
                    fightPrompt();
                }
            }
            continue;
        }
        if (k == 'f') forest();
        else if (k == 's') {
            if (!expert_) scene("WAR");
            st_ = St::Pvp;
            placePrompt();
        } else if (k == 'k') {
            if (!expert_) scene("ARTHUR");
            st_ = St::King;
            placePrompt();
        } else if (k == 'a') {
            if (!expert_) scene("ABDUL");
            st_ = St::Armor;
            placePrompt();
        } else if (k == 'h') heal();
        else if (k == 'v') stats();
        else if (k == 'i') {
            st_ = St::Inn;
            if (!expert_) scene(raw_[162] == 5 ? "RDIF" : "RDI");
            placePrompt();
        } else if (k == 't') {
            if (g16(O_LEVEL) >= 12) startDragon();
            else {
                Fighter tr;
                int hpG = 0, strG = 0, defG = 0;
                int32_t need = 0;
                if (!trainerOf(g16(O_LEVEL), tr, hpG, strG, defG, need)) send("\r\n  No master on file.\r\n");
                else {
                    if (!expert_) scene("TURGON");
                    st_ = St::Train;
                    placePrompt();
                }
            }
        } else if (k == 'y') {
            if (!expert_) scene("BANK");
            st_ = St::Bank;
            placePrompt();
        } else if (k == 'l') listPlayers(false);
        else if (k == 'p') listPlayers(true);
        else if (k == 'x') {
            expert_ = !expert_;
            town();
        } else if (k == 'q') {
            raw_[O_ON] = 0;
            save();
            send("\r\n  Quit.\r\n");
            st_ = St::Done;
        } else if (k == '?') town();
        else if (k == 'w') {
            listPlayers(false);
            beginAmount(Ask::MailTo, "  To number (0 = cancel): ");
        } else if (k == 'd') showLog();
        else if (k == 'o') showPlaces();
        else if (k == 'm') startCompose(-2);
        else if (k == 'c') listMarried();
        else if (k == 'r') send("  Not in this build.\r\n");
    }
    return st_ != St::Done;
}

}  // namespace lord
}  // namespace axis
