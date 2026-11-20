// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The Vision Gate and Outpost packs: load each zip and draw a menu in that look's dialect.
#include "bbs/features.h"
#if AXIS_FILES || AXIS_DOSPROMPT

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "doctest.h"
#include "bbs/ansiterm.h"
#include "bbs/artstyle.h"
#include "bbs/listtemplate.h"
#include "bbs/pipecodes.h"
#include "bbs/zipreader.h"

using namespace axis::bbs;

namespace {

const AcsUser kWho{10, ""};

std::vector<uint8_t> loadFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), {});
}

ZipReader readerOver(const std::vector<uint8_t>& z) {
    return ZipReader(
        [&z](uint32_t off, uint8_t* buf, size_t n) {
            if ((uint64_t)off + n > z.size()) return false;
            std::copy(z.begin() + off, z.begin() + off + n, buf);
            return true;
        },
        (uint32_t)z.size());
}

std::string extract(ZipReader& r, const std::string& name) {
    std::vector<uint8_t> ws(ZipReader::kWorkspace);
    std::string          out;
    for (const auto& e : r.entries())
        if (e.name == name)
            r.extract(e, ws.data(), ws.size(), [&](const uint8_t* p, size_t n) {
                out.append((const char*)p, n);
                return true;
            });
    return out;
}

std::string valueOf(const std::string& k) {
    if (k == "BN" || k == "%BN") return "Northline";
    if (k == "NODE" || k == "%NN" || k == "%ND") return "1";
    if (k == "UH" || k == "%UN") return "Spindle";
    if (k == "TL" || k == "%TL") return "60";
    if (k == "%SN") return "Rev";
    if (k == "TIME") return "12:00";
    if (k == "DATE" || k == "LCALL") return "11/20/26";
    if (k == "CALLS" || k == "CA") return "4";
    if (k == "LEVEL") return "10";
    if (k == "HN") return "Spindle";
    if (k == "LO") return "Shop";
    if (k == "TG") return "chips";
    if (k == "PG") return "page 1/1";
    if (k == "CT") return "3 msgs";
    return "";
}

std::string draw(const std::string& art, const ArtStyle& style, AnsiTerm& term) {
    std::string out = expandArt(art, kWho, valueOf, style);
    term.feed(out);
    return out;
}

std::string plain(const AnsiTerm& t) {
    std::string s;
    s.reserve((size_t)t.rows() * t.cols());
    for (int r = 0; r < t.rows(); r++)
        for (int c = 0; c < t.cols(); c++) s += (char)t.cell(r, c).ch;
    return s;
}

void windowBlank(const AnsiTerm& t) {
    for (int r = 8; r <= 20; r++)
        for (int c = 3; c <= 43; c++) CHECK(t.cell(r, c).ch == ' ');
}

void noRawCodes(const std::string& shown) {
    CHECK(shown.find("|C") == std::string::npos);
    CHECK(shown.find("|CS") == std::string::npos);
    CHECK(shown.find("|CL") == std::string::npos);
    CHECK(shown.find("|XY") == std::string::npos);
    CHECK(shown.find("%BN") == std::string::npos);
    CHECK(shown.find("%NN") == std::string::npos);
    CHECK(shown.find("%ND") == std::string::npos);
    CHECK(shown.find("%TL") == std::string::npos);
    CHECK(shown.find("^1") == std::string::npos);
    CHECK(shown.find("^0") == std::string::npos);
}

struct Pack {
    const char* file;
    const char* ans;
    Dialect     dialect;
    char        title;  // first letter of the header word, column 2
};

const Pack kPacks[] = {
    {"axis-vision-gate-ansi-2026-11-20.zip", "vision", Dialect::Vision3, 'V'},
    {"axis-outpost-rampart-ansi-2026-11-20.zip", "outpost", Dialect::Renegade, 'O'},
};

}  // namespace

TEST_CASE("each dialect pack loads and its main menu draws in that dialect") {
    for (const Pack& pack : kPacks) {
        INFO(pack.file);
        auto bytes = loadFile(std::string(AXIS_ART_PACKS) + "/" + pack.file);
        REQUIRE(bytes.size() > 100);
        auto zip = readerOver(bytes);
        REQUIRE(zip.open());

        ArtStyle style;
        REQUIRE(parseThemeCfg(extract(zip, "axis/THEME.CFG"), style));
        CHECK(style.dialect == pack.dialect);

        std::string main = extract(zip, "axis/MAIN.ANS");
        std::string numbered = extract(zip, std::string("ans/04_") + pack.ans + "_main.ans");
        REQUIRE(main.size() > 200);
        CHECK(main == numbered);
        if (pack.dialect == Dialect::Vision3) {
            CHECK(main.find("|CL") != std::string::npos);
            CHECK(main.find("|C7") != std::string::npos);
            CHECK(main.find("||") != std::string::npos);
        } else {
            CHECK(main.find("|CS") != std::string::npos);
            CHECK(main.find("^1") != std::string::npos);
            CHECK(main.find("|17") != std::string::npos);
            CHECK(main.find("|23") != std::string::npos);
            CHECK(main.find("%ND") != std::string::npos);
            CHECK(main.find("%TL") != std::string::npos);
        }

        AnsiTerm term;
        std::string shown = draw(main, style, term);
        noRawCodes(plain(term));
        CHECK(shown.find("Northline") != std::string::npos);
        CHECK(plain(term).find("arrows move") != std::string::npos);
        CHECK(term.cell(0, 1).ch == (uint8_t)pack.title);
        CHECK(term.cell(0, 1).attr == 31);  // white on blue: |C7, or |17|15
        windowBlank(term);
        // a literal bar from ||, not a code the dialect failed to read
        CHECK(plain(term).find("| ") != std::string::npos);

        if (pack.dialect == Dialect::Renegade) {
            CHECK(term.cell(24, 1).ch == 'r');
            CHECK(term.cell(24, 1).attr == 112);  // |23 is light grey, not a reset, then |00
            bool lamp = false;
            for (int c = 0; c < term.cols(); c++)
                if (term.cell(1, c).ch == '*' && term.cell(1, c).attr == 159) lamp = true;
            CHECK(lamp);  // |15|25, white on a blinking blue
        }

        std::string lite = extract(zip, "axis/MAIN.LITE.ANS");
        CHECK(lite.size() > 80);
        CHECK(lite.size() <= 700);
        AnsiTerm small;
        draw(lite, style, small);
        noRawCodes(plain(small));
        CHECK(plain(small).find("arrows move") != std::string::npos);
        windowBlank(small);

        std::string login = extract(zip, "axis/LOGIN.ANS");
        AnsiTerm log;
        draw(login, style, log);
        std::string logged = plain(log);
        CHECK(logged.find("|{P}") != std::string::npos);
        CHECK(logged.find("|{O}") != std::string::npos);

        ListTemplate users;
        REQUIRE(parseListTemplate(extract(zip, "axis/USERS.LST"), users));
        std::string row = renderLine(users.row, users.width, kWho, valueOf, style);
        CHECK(row.find("Spindle") != std::string::npos);
        CHECK(row.find("|C") == std::string::npos);
        CHECK(row.find('^') == std::string::npos);
        if (pack.dialect == Dialect::Vision3) CHECK(row.find(attrSgr(style.c[1])) != std::string::npos);  // |C2
        else CHECK(row.find(attrSgr(style.r[3])) != std::string::npos);                                   // ^3

        std::string byeArt = extract(zip, "axis/GOODBYE.ANS");
        AnsiTerm bye;
        draw(byeArt, style, bye);
        CHECK(plain(bye).find("Call back soon") != std::string::npos);
        bool footer = false;
        for (int r = 0; r < bye.rows(); r++) {
            std::string row;
            for (int c = 0; c < bye.cols(); c++) row += (char)bye.cell(r, c).ch;
            auto at = row.find("AXIS MICRO BBS");
            if (at == std::string::npos) continue;
            footer = true;
            for (size_t i = 0; i < std::string("AXIS MICRO BBS").size(); i++) {
                if (row[at + i] == ' ') continue;
                CHECK(bye.cell(r, (int)(at + i)).attr == 15);  // bright white on black, the look's hot footer
            }
        }
        CHECK(footer);
        std::string byeLite = extract(zip, "axis/GOODBYE.LITE.ANS");
        CHECK(byeLite.size() <= 700);
        AnsiTerm byeSmall;
        draw(byeLite, style, byeSmall);
        CHECK(plain(byeSmall).find("AXIS MICRO BBS") != std::string::npos);

        ListTemplate files;
        REQUIRE(parseListTemplate(extract(zip, "axis/FILES.LST"), files));
        std::string cap = renderLine(files.caption, files.width, kWho, valueOf, style);
        CHECK(cap.find("page 1/1") != std::string::npos);
        CHECK(cap.find("|PG") == std::string::npos);
    }
}

#endif
