// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// Native LORD reads a synthetic 4.07 warrior file. Scene art comes from the door folder.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "doctest.h"

#include "door/bytestream.h"
#include "lord/game.h"
#include "platform/pc/stdhostfs.h"

namespace fs = std::filesystem;

namespace {

constexpr int kRec = 236;
constexpr int kMon = 239;

std::string plain(const std::string& s) {
    std::string o;
    for (size_t i = 0; i < s.size();) {
        if ((unsigned char)s[i] == 0x1b && i + 1 < s.size() && s[i + 1] == '[') {
            i += 2;
            while (i < s.size()) {
                unsigned char c = (unsigned char)s[i++];
                if (c >= 0x40 && c <= 0x7e) break;
            }
            continue;
        }
        o.push_back(s[i++]);
    }
    return o;
}

void pstr(uint8_t* rec, int off, int cap, const char* s) {
    size_t n = std::strlen(s);
    if (n > (size_t)cap) n = (size_t)cap;
    rec[off] = (uint8_t)n;
    std::memcpy(rec + off + 1, s, n);
}

void w16(uint8_t* rec, int off, int v) {
    rec[off] = (uint8_t)(v & 0xFF);
    rec[off + 1] = (uint8_t)((v >> 8) & 0xFF);
}

void w32(uint8_t* rec, int off, int v) {
    for (int i = 0; i < 4; i++) rec[off + i] = (uint8_t)((v >> (8 * i)) & 0xFF);
}

int r16(const uint8_t* rec, int off) { return (int)(int16_t)(rec[off] | (rec[off + 1] << 8)); }
int r32(const uint8_t* rec, int off) {
    return (int)(int32_t)(rec[off] | (rec[off + 1] << 8) | (rec[off + 2] << 16) | (rec[off + 3] << 24));
}

void save(const fs::path& path, const std::string& text) {
    std::ofstream out(path, std::ios::binary);
    out << text;
}

void saveBytes(const fs::path& path, const std::vector<uint8_t>& bytes) {
    std::ofstream out(path, std::ios::binary);
    out.write((const char*)bytes.data(), (std::streamsize)bytes.size());
}

std::vector<uint8_t> load(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

std::vector<uint8_t> warriors() {
    std::vector<uint8_t> f(kRec * 2, 0);
    pstr(f.data(), 0, 20, "Rex");
    pstr(f.data(), 21, 50, "Redking");
    w16(f.data(), 72, 10);
    w16(f.data(), 78, 20);
    pstr(f.data(), 82, 20, "Stick");
    w16(f.data(), 105, 2);
    w32(f.data(), 109, 100);
    w32(f.data(), 113, 5);
    w16(f.data(), 117, 3);
    w16(f.data(), 119, 20);
    w16(f.data(), 121, 2);
    w16(f.data(), 127, 1);
    pstr(f.data(), 131, 20, "Coat");
    w32(f.data(), 158, 10);
    f[179] = 1;
    std::memset(f.data() + kRec, 0x5A, kRec);
    pstr(f.data() + kRec, 0, 20, "Keepme");
    return f;
}

std::vector<uint8_t> oneMonster() {
    std::vector<uint8_t> f(kMon, 0);
    pstr(f.data(), 0, 60, "Wolf");
    w32(f.data(), 61, 2);
    w32(f.data(), 65, 10);
    pstr(f.data(), 69, 60, "Bite");
    w32(f.data(), 130, 3);
    w32(f.data(), 134, 5);
    pstr(f.data(), 138, 100, "Down");
    return f;
}

fs::path stage(const char* name) {
    fs::path root = fs::temp_directory_path() / name;
    fs::remove_all(root);
    fs::create_directories(root);
    saveBytes(root / "PLAYER.DAT", warriors());
    return root;
}

uint32_t zeroRng() { return 0; }

int rngOnes = 0;
uint32_t oneThenZero() {
    if (rngOnes > 0) {
        rngOnes--;
        return 1;
    }
    return 0;
}

uint16_t unixDay() {
    time_t now = std::time(nullptr);
    unsigned d = now > 0 ? (unsigned)(now / 86400) : 0;
    if (d > 65535u) d = 65535u;
    return (uint16_t)d;
}

void gearRec(uint8_t* p, const char* name, int price, int bonus) {
    std::memset(p, 0, 264);
    pstr(p, 0, 255, name);
    w32(p, 256, price);
    w32(p, 260, bonus);
}

void trainerRec(uint8_t* p) {
    std::memset(p, 0, 219);
    pstr(p, 0, 30, "Master");
    pstr(p, 31, 20, "Fist");
    w16(p, 205, 1);
    w16(p, 207, 5);
    w16(p, 209, 10);
    w16(p, 211, 4);
    w16(p, 213, 3);
    w32(p, 215, 100);
}

}  // namespace

TEST_CASE("native lord uses the ansi pack then the stock screen") {
    fs::path root = stage("axis_lordnative_art");
    save(root / "lordtxt.lrd", "@#OTHER\r\nnope\r\n@#MAIN\r\n`2PACK-MARK\r\n@#NEXT\r\nx\r\n");
    save(root / "LORDTXT.DAT", "@#MAIN\r\nSTOCK-MARK\r\n@#FOREST\r\nSTOCK-FOREST\r\n");
    axis::pc::StdHostFs host(root);
    axis::door::ScriptStream line;
    axis::lord::NativeLord game(host, line, "Redking", nullptr, 1);
    game.begin();
    std::string screen = plain(line.output());
    CHECK(screen.find("PACK-MARK") != std::string::npos);
    CHECK(line.output().find("\x1b[0;32m") != std::string::npos);
    CHECK(screen.find("STOCK-MARK") == std::string::npos);
    line.clearOutput();
    line.push("f");
    CHECK(game.step());
    CHECK(plain(line.output()).find("STOCK-FOREST") != std::string::npos);
    CHECK(plain(line.output()).find("The forest is empty.") != std::string::npos);
}

TEST_CASE("native lord prefers a dropped ans screen") {
    fs::path root = stage("axis_lordnative_ans");
    save(root / "MAIN.ANS", "ANS-MARK\r\n");
    save(root / "lordtxt.lrd", "@#MAIN\r\nPACK-MARK\r\n");
    axis::pc::StdHostFs host(root);
    axis::door::ScriptStream line;
    axis::lord::NativeLord game(host, line, "Redking", nullptr, 1);
    game.begin();
    std::string screen = plain(line.output());
    CHECK(screen.find("ANS-MARK") != std::string::npos);
    CHECK(screen.find("PACK-MARK") == std::string::npos);
}

TEST_CASE("native lord heals then wins one fight") {
    fs::path root = stage("axis_lordnative_fight");
    saveBytes(root / "LENEMY.DAT", oneMonster());
    std::vector<uint8_t> before = load(root / "PLAYER.DAT");
    axis::pc::StdHostFs host(root);
    axis::door::ScriptStream line;
    axis::lord::NativeLord game(host, line, "Redking", nullptr, 1);
    game.setRng(zeroRng);
    game.begin();
    line.clearOutput();
    line.push("h");
    CHECK(game.step());
    CHECK(plain(line.output()).find("10 hit points are healed.") != std::string::npos);
    line.clearOutput();
    line.push("f");
    CHECK(game.step());
    CHECK(plain(line.output()).find("Wolf") != std::string::npos);
    CHECK(line.output().find("  (A)ttack  (R)un  (S)tats\r\n") != std::string::npos);
    CHECK(plain(line.output()).find("(K)") == std::string::npos);
    line.clearOutput();
    line.push("a");
    CHECK(game.step());
    std::string hit = plain(line.output());
    CHECK(hit.find("You hit Wolf for 10.") != std::string::npos);
    CHECK(hit.find("Down") != std::string::npos);
    CHECK(hit.find("You win.") != std::string::npos);
    CHECK(hit.find("You receive ") != std::string::npos);  // what the fight paid is said, not just added
    CHECK(hit.find(" experience.") != std::string::npos);
    line.push("q");
    CHECK_FALSE(game.step());
    std::vector<uint8_t> after = load(root / "PLAYER.DAT");
    CHECK(r16(after.data(), 72) == 20);
    CHECK(r32(after.data(), 109) == 60);
    CHECK(r32(after.data(), 158) == 13);
    CHECK(r16(after.data(), 105) == 1);
    CHECK(after[170] == 0);
    CHECK(std::memcmp(after.data() + kRec, before.data() + kRec, kRec) == 0);
}

TEST_CASE("native lord stamps today and does not wake a first sight") {
    fs::path root = stage("axis_lordnative_day0");
    axis::pc::StdHostFs host(root);
    axis::door::ScriptStream line;
    axis::lord::NativeLord game(host, line, "Redking", nullptr, 1);
    game.begin();
    CHECK(plain(line.output()).find("A new day.") == std::string::npos);
    auto file = load(root / "PLAYER.DAT");
    CHECK(r16(file.data(), 105) == 2);
    CHECK(r16(file.data(), 72) == 10);
    auto day = load(root / "NATIVEDAY");
    REQUIRE(day.size() >= 2);
    CHECK((uint16_t)(day[0] | (day[1] << 8)) == unixDay());
}

TEST_CASE("native lord wakes on a new day without touching the lord day count") {
    fs::path root = stage("axis_lordnative_day1");
    {
        axis::pc::StdHostFs host(root);
        axis::door::ScriptStream line;
        axis::lord::NativeLord game(host, line, "Redking", nullptr, 1);
        game.begin();
    }
    auto file = load(root / "PLAYER.DAT");
    std::vector<uint8_t> keep(file.begin() + kRec, file.end());
    w16(file.data(), 129, 77);
    w16(file.data(), 72, 4);
    w16(file.data(), 105, 1);
    file[154] = 5;
    w16(file.data(), 103, 5);
    w16(file.data(), 210, 2);
    w16(file.data(), 180, 1);
    w32(file.data(), 113, 100);
    file[214] = 8;
    file[179] = 1;
    saveBytes(root / "PLAYER.DAT", file);
    uint16_t yest = (uint16_t)(unixDay() - 1);
    saveBytes(root / "NATIVEDAY", {(uint8_t)(yest & 0xff), (uint8_t)((yest >> 8) & 0xff)});
    axis::pc::StdHostFs host(root);
    axis::door::ScriptStream line;
    axis::lord::NativeLord game(host, line, "Redking", nullptr, 1);
    game.begin();
    CHECK(plain(line.output()).find("A new day.") != std::string::npos);
    auto after = load(root / "PLAYER.DAT");
    CHECK(r16(after.data(), 105) == 21);
    CHECK(r16(after.data(), 72) == 20);
    CHECK(r32(after.data(), 113) == 110);
    CHECK(after[217] == 3);
    CHECK(r16(after.data(), 129) == 77);
    CHECK(after[154] == 0);
    CHECK(r16(after.data(), 103) == 0);
    CHECK(std::memcmp(after.data() + kRec, keep.data(), kRec) == 0);
}

TEST_CASE("native lord deposits an amount and withdraws all on 1") {
    fs::path root = stage("axis_lordnative_bank");
    axis::pc::StdHostFs host(root);
    axis::door::ScriptStream line;
    axis::lord::NativeLord game(host, line, "Redking", nullptr, 1);
    game.begin();
    line.clearOutput();
    line.push("yd40\rw1\r");
    CHECK(game.step());
    std::string out = plain(line.output());
    CHECK(out.find("Deposited.") != std::string::npos);
    CHECK(out.find("Withdrawn.") != std::string::npos);
    auto after = load(root / "PLAYER.DAT");
    CHECK(r32(after.data(), 109) == 105);
    CHECK(r32(after.data(), 113) == 0);
}

TEST_CASE("native lord stays in the inn when the room costs more than the gold") {
    fs::path root = stage("axis_lordnative_inn");
    axis::pc::StdHostFs host(root);
    axis::door::ScriptStream line;
    axis::lord::NativeLord game(host, line, "Redking", nullptr, 1);
    game.begin();
    line.clearOutput();
    line.push("igy");
    CHECK(game.step());
    CHECK(plain(line.output()).find("Not enough gold.") != std::string::npos);
    auto after = load(root / "PLAYER.DAT");
    CHECK(r32(after.data(), 109) == 100);
    CHECK(after[155] == 0);
}

TEST_CASE("native lord gives a charming warrior the room and leaves") {
    fs::path root = stage("axis_lordnative_innfree");
    auto file = load(root / "PLAYER.DAT");
    w16(file.data(), 121, 100);
    saveBytes(root / "PLAYER.DAT", file);
    axis::pc::StdHostFs host(root);
    axis::door::ScriptStream line;
    axis::lord::NativeLord game(host, line, "Redking", nullptr, 1);
    game.begin();
    line.clearOutput();
    line.push("igy");
    CHECK_FALSE(game.step());
    CHECK(plain(line.output()).find("The room is free.") != std::string::npos);
    auto after = load(root / "PLAYER.DAT");
    CHECK(after[155] == 5);
    CHECK(r32(after.data(), 109) == 100);
    CHECK(after[170] == 0);
}

TEST_CASE("native lord sells the held weapon then buys the next one") {
    fs::path root = stage("axis_lordnative_shop");
    std::vector<uint8_t> cat(264 * 2, 0);
    gearRec(cat.data(), "Blade", 100, 5);
    gearRec(cat.data() + 264, "Axe", 50, 9);
    saveBytes(root / "WEAPONS.DAT", cat);
    auto file = load(root / "PLAYER.DAT");
    w16(file.data(), 80, 1);
    saveBytes(root / "PLAYER.DAT", file);
    axis::pc::StdHostFs host(root);
    axis::door::ScriptStream line;
    axis::lord::NativeLord game(host, line, "Redking", nullptr, 1);
    game.setRng(zeroRng);
    game.begin();
    line.clearOutput();
    line.push("ksyb2\ryb1\ry");
    CHECK(game.step());
    std::string out = plain(line.output());
    CHECK(out.find("Sold.") != std::string::npos);
    CHECK(out.find("Bought.") != std::string::npos);
    CHECK(out.find("Already holding one.") != std::string::npos);
    auto after = load(root / "PLAYER.DAT");
    CHECK(r16(after.data(), 119) == 24);
    CHECK(r32(after.data(), 109) == 100);
    CHECK(r16(after.data(), 80) == 2);
    CHECK(after[82] == 3);
    CHECK(std::memcmp(after.data() + 83, "Axe", 3) == 0);
}

TEST_CASE("native lord refuses a master fight under the experience line") {
    fs::path root = stage("axis_lordnative_trainlow");
    std::vector<uint8_t> tr(219, 0);
    trainerRec(tr.data());
    saveBytes(root / "TRAINERS.DAT", tr);
    axis::pc::StdHostFs host(root);
    axis::door::ScriptStream line;
    axis::lord::NativeLord game(host, line, "Redking", nullptr, 1);
    game.begin();
    line.clearOutput();
    line.push("ta");
    CHECK(game.step());
    CHECK(plain(line.output()).find("Need 90 more experience.") != std::string::npos);
    auto after = load(root / "PLAYER.DAT");
    CHECK(r16(after.data(), 103) == 5);
    CHECK(r16(after.data(), 127) == 1);
}

TEST_CASE("native lord honors a master then wins the fight and levels") {
    fs::path root = stage("axis_lordnative_trainwin");
    std::vector<uint8_t> tr(219, 0);
    trainerRec(tr.data());
    saveBytes(root / "TRAINERS.DAT", tr);
    auto file = load(root / "PLAYER.DAT");
    w32(file.data(), 158, 100);
    file[217] = 2;
    w16(file.data(), 103, 0);
    saveBytes(root / "PLAYER.DAT", file);
    uint16_t today = unixDay();
    saveBytes(root / "NATIVEDAY", {(uint8_t)(today & 0xff), (uint8_t)((today >> 8) & 0xff)});
    axis::pc::StdHostFs host(root);
    axis::door::ScriptStream line;
    axis::lord::NativeLord game(host, line, "Redking", nullptr, 1);
    game.setRng(zeroRng);
    game.begin();
    CHECK(plain(line.output()).find("A new day.") == std::string::npos);
    line.clearOutput();
    line.push("tak");
    CHECK(game.step());
    CHECK(plain(line.output()).find("Honor holds.") != std::string::npos);
    auto mid = load(root / "PLAYER.DAT");
    CHECK(mid[217] == 2);
    CHECK(r16(mid.data(), 127) == 1);
    line.clearOutput();
    line.push("a");
    CHECK(game.step());
    CHECK(plain(line.output()).find("Level 2.") != std::string::npos);
    auto after = load(root / "PLAYER.DAT");
    CHECK(r16(after.data(), 127) == 2);
    CHECK(r16(after.data(), 78) == 30);
    CHECK(r16(after.data(), 72) == 30);
    CHECK(r16(after.data(), 119) == 24);
    CHECK(r16(after.data(), 117) == 6);
    CHECK(r16(after.data(), 103) == 0);
    CHECK(after[217] == 2);
    CHECK(after[214] == 1);
}

TEST_CASE("native lord wins one player fight and writes both warriors") {
    fs::path root = stage("axis_lordnative_pvp");
    auto file = load(root / "PLAYER.DAT");
    std::memset(file.data() + kRec, 0, kRec);
    pstr(file.data() + kRec, 0, 20, "Foe");
    w16(file.data() + kRec, 72, 4);
    w16(file.data() + kRec, 78, 4);
    w16(file.data() + kRec, 117, 0);
    w16(file.data() + kRec, 119, 1);
    w32(file.data() + kRec, 109, 30);
    w32(file.data() + kRec, 158, 20);
    w16(file.data() + kRec, 156, 4);
    pstr(file.data() + kRec, 82, 20, "Club");
    w16(file.data(), 107, 2);
    saveBytes(root / "PLAYER.DAT", file);
    axis::pc::StdHostFs host(root);
    axis::door::ScriptStream line;
    axis::lord::NativeLord game(host, line, "Redking", nullptr, 1);
    game.setRng(zeroRng);
    game.begin();
    line.clearOutput();
    line.push("sn2\ry");
    CHECK(game.step());
    line.clearOutput();
    line.push("a");
    CHECK(game.step());
    std::string hit = plain(line.output());
    CHECK(hit.find("You hit Foe for 10.") != std::string::npos);
    CHECK(hit.find("You win.") != std::string::npos);
    auto after = load(root / "PLAYER.DAT");
    CHECK(r32(after.data(), 109) == 130);
    CHECK(r32(after.data(), 158) == 20);
    CHECK(r16(after.data(), 156) == 2);
    CHECK(r16(after.data(), 107) == 1);
    CHECK(r16(after.data(), 227) == 1);
    CHECK(r16(after.data(), 105) == 2);
    const uint8_t* foe = after.data() + kRec;
    CHECK(r32(foe, 109) == 0);
    CHECK(foe[154] == 5);
    CHECK(r32(foe, 158) == 18);
    CHECK(r16(foe, 156) == 2);
    CHECK(r16(foe, 72) == 0);
    CHECK(foe[0] == 3);
    CHECK(std::memcmp(foe + 1, "Foe", 3) == 0);
}

TEST_CASE("native lord spends a death knight use as a triple hit") {
    fs::path root = stage("axis_lordnative_skill");
    saveBytes(root / "LENEMY.DAT", oneMonster());
    auto file = load(root / "PLAYER.DAT");
    file[217] = 1;
    saveBytes(root / "PLAYER.DAT", file);
    axis::pc::StdHostFs host(root);
    axis::door::ScriptStream line;
    axis::lord::NativeLord game(host, line, "Redking", nullptr, 1);
    game.setRng(zeroRng);
    game.begin();
    line.clearOutput();
    line.push("fk");
    CHECK(game.step());
    std::string out = plain(line.output());
    CHECK(out.find("Skill.") != std::string::npos);
    CHECK(out.find("You hit Wolf for 30.") != std::string::npos);
    CHECK(out.find("Down") != std::string::npos);
    CHECK(out.find("You win.") != std::string::npos);
    auto after = load(root / "PLAYER.DAT");
    CHECK(after[217] == 0);
    CHECK(r16(after.data(), 105) == 1);
    CHECK(r32(after.data(), 109) == 110);
    CHECK(r32(after.data(), 158) == 13);
}

TEST_CASE("native lord applies mail codes once and drops the file") {
    fs::path root = stage("axis_lordnative_mail");
    save(root / "mail0.lrd", "`G40\n`K\n");
    auto before = load(root / "PLAYER.DAT");
    axis::pc::StdHostFs host(root);
    axis::door::ScriptStream line;
    axis::lord::NativeLord game(host, line, "Redking", nullptr, 1);
    game.begin();
    CHECK(plain(line.output()).find("Mail from mail.") != std::string::npos);
    auto after = load(root / "PLAYER.DAT");
    CHECK(r32(after.data(), 109) == 140);
    CHECK(r16(after.data(), 210) == 1);
    CHECK(std::memcmp(after.data() + kRec, before.data() + kRec, kRec) == 0);
    CHECK_FALSE(fs::exists(root / "mail0.lrd"));
}

struct PullOnce {
    int n = 0;
};
int pullBank(void* ctx, char* body, int bodyN, char* from, int fromN) {
    auto* box = (PullOnce*)ctx;
    if (box->n) return 0;
    box->n = 1;
    std::snprintf(body, (size_t)bodyN, "`b7");
    std::snprintf(from, (size_t)fromN, "IGM");
    return 9;
}

TEST_CASE("native lord applies one pulled mail body") {
    fs::path root = stage("axis_lordnative_pull");
    axis::pc::StdHostFs host(root);
    axis::door::ScriptStream line;
    axis::lord::NativeLord game(host, line, "Redking", nullptr, 1);
    PullOnce box;
    game.setMailbox(&box, pullBank, nullptr);
    game.begin();
    CHECK(plain(line.output()).find("Mail from IGM.") != std::string::npos);
    auto after = load(root / "PLAYER.DAT");
    CHECK(r32(after.data(), 113) == 12);
}

TEST_CASE("native lord writes a letter into the warrior mail file") {
    fs::path root = stage("axis_lordnative_letter");
    axis::pc::StdHostFs host(root);
    axis::door::ScriptStream line;
    axis::lord::NativeLord game(host, line, "Redking", nullptr, 1);
    game.begin();
    line.clearOutput();
    line.push("w2\rHi there\r");
    CHECK(game.step());
    CHECK(plain(line.output()).find("Sent.") != std::string::npos);
    auto mail = load(root / "mail1.lrd");
    std::string body(mail.begin(), mail.end());
    CHECK(body.find("Hi there") != std::string::npos);
}

TEST_CASE("native lord runs one forest event and keeps the fight") {
    fs::path root = stage("axis_lordnative_event");
    save(root / "EVENTS.LDY", "@#GOLD\r\n@math &N1 = 7 * &Plv\r\n@set &Pgo to +&N1\r\n");
    axis::pc::StdHostFs host(root);
    axis::door::ScriptStream line;
    axis::lord::NativeLord game(host, line, "Redking", nullptr, 1);
    game.setRng(oneThenZero);
    rngOnes = 1;
    game.begin();
    line.clearOutput();
    line.push("f");
    CHECK(game.step());
    auto after = load(root / "PLAYER.DAT");
    CHECK(r32(after.data(), 109) == 107);
    CHECK(r16(after.data(), 105) == 2);
}

TEST_CASE("native lord shows the day log then returns to town") {
    fs::path root = stage("axis_lordnative_log");
    save(root / "lognow.lrd", "Raid\r\n");
    axis::pc::StdHostFs host(root);
    axis::door::ScriptStream line;
    axis::lord::NativeLord game(host, line, "Redking", nullptr, 1);
    game.begin();
    line.clearOutput();
    line.push("dc");
    CHECK(game.step());
    std::string out = plain(line.output());
    CHECK(out.find("Raid") != std::string::npos);
    CHECK(out.find("The Town Square") != std::string::npos);
}

TEST_CASE("native lord lists an other place and refuses the exe") {
    fs::path root = stage("axis_lordnative_igm");
    save(root / "3RDPARTY.LRD", "FOO.EXE\r\nA small door\r\n");
    axis::pc::StdHostFs host(root);
    axis::door::ScriptStream line;
    axis::lord::NativeLord game(host, line, "Redking", nullptr, 1);
    game.begin();
    line.clearOutput();
    line.push("o1");
    CHECK(game.step());
    std::string out = plain(line.output());
    CHECK(out.find("A small door") != std::string::npos);
    CHECK(out.find("The DOS runtime is not in this firmware, so that .EXE cannot start.") != std::string::npos);
}

TEST_CASE("native lord changes profession at skill 40") {
    fs::path root = stage("axis_lordnative_class");
    std::vector<uint8_t> tr(219, 0);
    trainerRec(tr.data());
    saveBytes(root / "TRAINERS.DAT", tr);
    auto file = load(root / "PLAYER.DAT");
    w32(file.data(), 158, 100);
    file[214] = 39;
    saveBytes(root / "PLAYER.DAT", file);
    axis::pc::StdHostFs host(root);
    axis::door::ScriptStream line;
    axis::lord::NativeLord game(host, line, "Redking", nullptr, 1);
    game.setRng(zeroRng);
    game.begin();
    line.clearOutput();
    line.push("taad");
    CHECK(game.step());
    std::string out = plain(line.output());
    CHECK(out.find("Skill 40.") != std::string::npos);
    CHECK(out.find("Class set.") != std::string::npos);
    auto after = load(root / "PLAYER.DAT");
    CHECK(after[214] == 40);
    CHECK(after[179] == 2);
    CHECK(r16(after.data(), 127) == 2);
}

TEST_CASE("native lord wins the dragon and keeps charm and skill") {
    fs::path root = stage("axis_lordnative_dragon");
    std::vector<uint8_t> mon(kMon, 0);
    pstr(mon.data(), 0, 60, "Wyrm");
    w32(mon.data(), 61, 100);
    w32(mon.data(), 134, 5);
    saveBytes(root / "dragon.bin", mon);
    auto file = load(root / "PLAYER.DAT");
    w16(file.data(), 127, 12);
    w16(file.data(), 121, 9);
    file[214] = 7;
    saveBytes(root / "PLAYER.DAT", file);
    axis::pc::StdHostFs host(root);
    axis::door::ScriptStream line;
    axis::lord::NativeLord game(host, line, "Redking", nullptr, 1);
    game.setRng(zeroRng);
    game.begin();
    line.clearOutput();
    line.push("ta");
    CHECK(game.step());
    std::string out = plain(line.output());
    CHECK(out.find("You win.") != std::string::npos);
    CHECK(out.find("The dragon falls.") != std::string::npos);
    auto after = load(root / "PLAYER.DAT");
    CHECK(r16(after.data(), 127) == 1);
    CHECK(r32(after.data(), 109) == 500);
    CHECK(r16(after.data(), 212) == 1);
    CHECK(r32(after.data(), 158) == 10);
    CHECK(r16(after.data(), 121) == 9);
    CHECK(after[214] == 7);
    CHECK(r16(after.data(), 123) == 5);
}

TEST_CASE("native lord lets the fairy pull a killing blow back") {
    fs::path root = stage("axis_lordnative_fairy");
    std::vector<uint8_t> mon = oneMonster();
    w32(mon.data(), 61, 80);
    w32(mon.data(), 134, 50);
    saveBytes(root / "LENEMY.DAT", mon);
    auto file = load(root / "PLAYER.DAT");
    w16(file.data(), 117, 0);
    w16(file.data(), 72, 10);
    w16(file.data(), 78, 20);
    saveBytes(root / "PLAYER.DAT", file);
    saveBytes(root / "FAIRY", {1});
    axis::pc::StdHostFs host(root);
    axis::door::ScriptStream line;
    axis::lord::NativeLord game(host, line, "Redking", nullptr, 1);
    game.setRng(zeroRng);
    game.begin();
    line.clearOutput();
    line.push("fa");
    CHECK(game.step());
    CHECK(plain(line.output()).find("The fairy pulls you back.") != std::string::npos);
    auto after = load(root / "PLAYER.DAT");
    CHECK(r16(after.data(), 72) == 20);
    CHECK(r16(after.data(), 105) == 2);
    CHECK(after[154] == 0);
    auto fairy = load(root / "FAIRY");
    CHECK(fairy.size() >= 1);
    CHECK(fairy[0] == 0);
}

TEST_CASE("native lord interrupts an online warrior and writes one fight") {
    fs::path root = stage("axis_lordnative_duel");
    auto file = load(root / "PLAYER.DAT");
    std::memset(file.data() + kRec, 0, kRec);
    pstr(file.data() + kRec, 0, 20, "Foe");
    pstr(file.data() + kRec, 21, 50, "Foe");
    w16(file.data() + kRec, 72, 4);
    w16(file.data() + kRec, 78, 4);
    w16(file.data() + kRec, 107, 3);
    w16(file.data() + kRec, 117, 0);
    w16(file.data() + kRec, 119, 1);
    w32(file.data() + kRec, 109, 30);
    w32(file.data() + kRec, 158, 20);
    w16(file.data() + kRec, 156, 4);
    pstr(file.data() + kRec, 82, 20, "Club");
    file[kRec + 170] = 1;
    w16(file.data(), 107, 2);
    saveBytes(root / "PLAYER.DAT", file);
    axis::pc::StdHostFs hostA(root);
    axis::pc::StdHostFs hostB(root);
    axis::door::ScriptStream lineA;
    axis::door::ScriptStream lineB;
    axis::lord::NativeLord foe(hostB, lineB, "Foe", nullptr, 2);
    axis::lord::NativeLord rex(hostA, lineA, "Redking", nullptr, 1);
    foe.setRng(zeroRng);
    rex.setRng(zeroRng);
    foe.begin();
    rex.begin();
    lineA.clearOutput();
    lineA.push("sn2\ry");
    CHECK(rex.step());
    CHECK(plain(lineA.output()).find("They are online.") != std::string::npos);
    auto mid = load(root / "PLAYER.DAT");
    CHECK(r16(mid.data(), 107) == 2);
    lineB.clearOutput();
    CHECK(foe.step());
    CHECK(plain(lineB.output()).find("Accept?") != std::string::npos);
    lineB.push("y");
    CHECK(foe.step());
    CHECK(plain(lineB.output()).find("You accept.") != std::string::npos);
    lineA.clearOutput();
    lineA.push("a");
    CHECK(rex.step());
    std::string hit = plain(lineA.output());
    CHECK(hit.find("You hit Foe for 10.") != std::string::npos);
    CHECK(hit.find("You win.") != std::string::npos);
    auto after = load(root / "PLAYER.DAT");
    CHECK(r32(after.data(), 109) == 130);
    CHECK(r32(after.data(), 158) == 20);
    CHECK(r16(after.data(), 156) == 2);
    CHECK(r16(after.data(), 107) == 1);
    CHECK(r16(after.data(), 227) == 1);
    CHECK(r16(after.data(), 105) == 2);
    const uint8_t* opp = after.data() + kRec;
    CHECK(r32(opp, 109) == 0);
    CHECK(opp[154] == 5);
    CHECK(r32(opp, 158) == 18);
    CHECK(r16(opp, 156) == 2);
    CHECK(r16(opp, 72) == 0);
    CHECK(r16(opp, 107) == 3);
}

TEST_CASE("native lord reads a copied live warrior file when one is staged") {
    fs::path src = fs::path("C:/Users/rev/AppData/Local/Temp/axis-live-PLAYER.DAT");
    if (!fs::exists(src)) return;
    auto orig = load(src);
    if (orig.size() < 472) return;
    fs::path root = fs::temp_directory_path() / "axis_lordnative_live";
    fs::remove_all(root);
    fs::create_directories(root);
    saveBytes(root / "PLAYER.DAT", orig);
    axis::pc::StdHostFs host(root);
    axis::door::ScriptStream line;
    axis::lord::NativeLord game(host, line, "Revjmoney", nullptr, 1);
    game.begin();
    CHECK(plain(line.output()).find("No warrior") == std::string::npos);
    auto after = load(root / "PLAYER.DAT");
    REQUIRE(after.size() >= orig.size());
    CHECK(r32(after.data() + kRec, 109) == 1411);
    CHECK(r16(after.data() + kRec, 72) == 20);
    CHECK(after[kRec] == 9);
    CHECK(std::memcmp(after.data() + kRec + 1, "Revjmoney", 9) == 0);
    for (size_t i = 0; i < orig.size(); i++) {
        if (i == (size_t)kRec + 170) continue;
        CHECK(after[i] == orig[i]);
    }
    line.push("q");
    CHECK_FALSE(game.step());
}

TEST_CASE("native lord writes a level 1 warrior on registration and leaves the others") {
    fs::path root = stage("axis_lordnative_new");
    auto before = load(root / "PLAYER.DAT");
    axis::pc::StdHostFs host(root);
    axis::door::LockTable locks;
    CHECK(axis::lord::ensureWarrior(host, &locks, 64, "Redking") == 0);
    CHECK(load(root / "PLAYER.DAT") == before);
    CHECK(axis::lord::ensureWarrior(host, &locks, 64, "ChipBreaker") == 1);
    auto mid = load(root / "PLAYER.DAT");
    REQUIRE(mid.size() == before.size() + 236);
    CHECK(std::memcmp(mid.data(), before.data(), before.size()) == 0);
    const uint8_t* rec = mid.data() + before.size();
    CHECK(rec[0] == 11);
    CHECK(std::memcmp(rec + 1, "ChipBreaker", 11) == 0);
    CHECK(rec[21] == 11);
    CHECK(std::memcmp(rec + 22, "ChipBreaker", 11) == 0);
    CHECK(r16(rec, 72) == 20);
    CHECK(r16(rec, 78) == 20);
    CHECK(r16(rec, 80) == 1);
    CHECK(r16(rec, 105) == 15);
    CHECK(r16(rec, 107) == 3);
    CHECK(r32(rec, 109) == 500);
    CHECK(r16(rec, 117) == 1);
    CHECK(r16(rec, 119) == 10);
    CHECK(r16(rec, 121) == 1);
    CHECK(r16(rec, 127) == 1);
    CHECK(r16(rec, 152) == 1);
    CHECK(r16(rec, 221) == -1);
    CHECK(rec[154] == 0);
    CHECK(rec[155] == 0);
    CHECK(rec[170] == 0);
    CHECK(rec[179] == 1);
    CHECK(axis::lord::ensureWarrior(host, &locks, 64, "chipbreaker") == 0);
    CHECK(load(root / "PLAYER.DAT").size() == mid.size());
    axis::door::ScriptStream line;
    axis::lord::NativeLord game(host, line, "ChipBreaker", &locks, 1);
    game.begin();
    std::string screen = plain(line.output());
    CHECK(screen.find("No warrior") == std::string::npos);
    CHECK(screen.find("could not be written") == std::string::npos);
    CHECK(screen.find("The Town Square") != std::string::npos);
    line.clearOutput();
    line.push("v");
    CHECK(game.step());
    CHECK(plain(line.output()).find("ChipBreaker") != std::string::npos);
    auto after = load(root / "PLAYER.DAT");
    CHECK(std::memcmp(after.data(), before.data(), before.size()) == 0);
}

namespace {
// LENEMY.DAT with n records, every field different per record, so the wrong record (a wrong seek) shows
std::vector<uint8_t> monsters(int n) {
    std::vector<uint8_t> f((size_t)n * kMon, 0);
    for (int i = 0; i < n; i++) {
        uint8_t* r = f.data() + (size_t)i * kMon;
        char     name[16], weapon[16], death[16];
        std::snprintf(name, sizeof name, "Mon%02d", i);
        std::snprintf(weapon, sizeof weapon, "Claw%02d", i);
        std::snprintf(death, sizeof death, "Fell%02d", i);
        pstr(r, 0, 60, name);
        w32(r, 61, 100 + i);  // str
        w32(r, 65, 300 + i);  // gold
        pstr(r, 69, 60, weapon);
        w32(r, 130, 400 + i);  // xp
        w32(r, 134, 200 + i);  // hp
        pstr(r, 138, 100, death);
    }
    return f;
}
uint32_t g_monRoll = 0;
uint32_t monRng() { return g_monRoll; }
}  // namespace

TEST_CASE("LORD picks the same monster") {
    const int n = 30;
    for (int level = 1; level <= 3; level++)
        for (uint32_t c : {0u, 7u, 10u}) {  // (c % 5 != 1: no forest event first)
            fs::path root = stage("axis_lordnative_monsters");
            saveBytes(root / "LENEMY.DAT", monsters(n));
            auto file = load(root / "PLAYER.DAT");
            w16(file.data(), 127, level);
            saveBytes(root / "PLAYER.DAT", file);
            g_monRoll = c;
            // the old code's pick, from the whole file in RAM: (level-1)*11 + roll(11), else roll(n) past the end
            int idx = (level - 1) * 11 + (int)(c % 11);
            if (idx >= n) idx = (int)(c % (uint32_t)n);
            axis::pc::StdHostFs      host(root);
            axis::door::ScriptStream line;
            axis::lord::NativeLord   game(host, line, "Redking", nullptr, 1);
            game.setRng(monRng);
            game.begin();
            line.clearOutput();
            line.push("f");
            CHECK(game.step());
            char want[48];
            std::snprintf(want, sizeof want, "Mon%02d   HP %d", idx, 200 + idx);
            CAPTURE(level);
            CAPTURE(c);
            CHECK(plain(line.output()).find(want) != std::string::npos);  // name and hp: one record, all its fields (str too) read together
        }
}
