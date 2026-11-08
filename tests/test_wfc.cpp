// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include "bbs/wfc.h"

#include "platform/cyd/wfccyd_art.h"

#include "doctest.h"

using namespace axis::bbs;

TEST_CASE("wfc values fill the codes") {
    WfcSnapshot s;
    s.bbsName = "AXIS";
    s.callsToday = 12;
    s.nodes.push_back({1, "", ""});
    s.nodes.push_back({2, "Spindle Kid", "doors"});
    CHECK(expandArt("|BN |CT @NB##############@", {}, wfcValues(s)).find("AXIS 12 2 Spindle Kid") != std::string::npos);
    CHECK(expandArt("|NA", {}, wfcValues(s)) == "1 waiting");
    s.sdFreeKb = 1258291;
    s.uptimeS = 3 * 86400 + 4 * 3600 + 12 * 60;
    CHECK(expandArt("|SD |UP |MB", {}, wfcValues(s)) == "1.2G 3d 04:12 main board");
}

TEST_CASE("a node past the art's slots is left off") {
    WfcSnapshot s;
    for (int i = 1; i <= 9; i++) s.nodes.push_back({i, "h", ""});
    std::string out = expandArt("|NA|NB|NC|ND|NE|NF|NG|NH", {}, wfcValues(s));
    CHECK(out.find("9 ") == std::string::npos);
    CHECK(out.find("8 h") != std::string::npos);
}

TEST_CASE("a value longer than its field is cut to the field") {
    WfcSnapshot s;
    s.nodes.push_back({1, std::string(40, 'x'), ""});
    CHECK(expandArt("[@NA##########@]", {}, wfcValues(s)) == "[" + std::string("1 xxxxxxxxxxxx") + "]");
}

TEST_CASE("hotkeys and node lines become tap targets") {
    WfcSnapshot s;
    s.nodes.push_back({1, "Rev", "menu"});
    AnsiTerm term(53, 30);
    auto     spots = drawWfc("|XY05,03[|KS]ettings|XY01,10|NA", s, term);
    int      n = -1;
    CHECK(wfcKeyAt(spots, 2, 5, &n) == 'S');
    CHECK(n == 0);
    CHECK(wfcKeyAt(spots, 2, 6, &n) == 'S');
    CHECK(wfcKeyAt(spots, 2, 8, &n) == 'S');  // the label "ettings" is the key too
    CHECK(wfcKeyAt(spots, 2, 13, &n) == 'S');  // its last letter
    CHECK(wfcKeyAt(spots, 2, 15, &n) == 0);    // past it
    CHECK(wfcKeyAt(spots, 9, 4, &n) == 'N');
    CHECK(n == 1);
    CHECK(term.cell(2, 5).ch == 'S');
    CHECK(term.cell(2, 4).ch == '[');
    CHECK(term.cell(9, 0).ch == '1');
}

TEST_CASE("markers do not shift fields or special keys") {
    WfcSnapshot s;
    s.nodes.push_back({1, "Rev", ""});
    AnsiTerm term(40, 5);
    auto     spots = drawWfc("@NA##########@|XY01,03|KM|KV|KO|XY01,05|NB|CT", s, term);
    CHECK(term.cell(0, 0).ch == '1');
    CHECK(term.cell(0, 11).ch == ' ');
    CHECK(term.cell(0, 12).ch == ' ');
    CHECK(wfcKeyAt(spots, 2, 0, nullptr) == '#');
    CHECK(wfcKeyAt(spots, 2, 1, nullptr) == '!');  // adjacent keys: each cell is its own key
    CHECK(wfcKeyAt(spots, 2, 2, nullptr) == ' ');
    CHECK(wfcKeyAt(spots, 2, 3, nullptr) == ' ');
    CHECK(term.cell(4, 0).ch == '0');  // |NB with one node shows nothing, then |CT prints 0
}

TEST_CASE("marker bytes in free text make no hotspots") {
    WfcSnapshot s;
    std::string evil;
    for (int c = 1; c < 32; c++)
        if (c != 9 && c != 10 && c != 13 && c != 27) evil += (char)c;
    s.page = evil;
    s.nodes.push_back({1, evil, evil});
    s.lastCaller = evil;
    AnsiTerm term(80, 5);
    auto     spots = drawWfc("|PG|XY01,02|LC|XY01,03|NA|XY01,04|NC", s, term);
    CHECK(spots.size() == 1);  // only the real node line
    CHECK(spots[0].node == 1);
}

TEST_CASE("a key beats a node line on the same row") {
    WfcSnapshot s;
    s.nodes.push_back({1, "Rev", ""});
    AnsiTerm term(60, 5);
    auto     spots = drawWfc("|XY01,02|NA|XY40,02|KS", s, term);
    int      n = -1;
    CHECK(wfcKeyAt(spots, 1, 39, &n) == 'S');
    CHECK(n == 0);
    CHECK(wfcKeyAt(spots, 1, 3, &n) == 'N');
    CHECK(n == 1);
    CHECK(wfcKeyAt(spots, 1, 20, &n) == 0);  // past the node's text
}

TEST_CASE("a key's label and a node's field share a row without overlapping") {
    WfcSnapshot s;
    s.nodes.push_back({1, "Rev", "menu"});
    AnsiTerm term(45, 24);
    // the built-in layout: a key box on the left, the nodes box on the right of the same row
    auto spots = drawWfc("|XY01,08\xb3 [|KS] settings    \xb3|XY20,08\xb3 @NA##############@ \xb3", s, term);
    int  n = -1;
    CHECK(wfcKeyAt(spots, 7, 8, &n) == 'S');  // a letter of "settings", not the node
    CHECK(n == 0);
    CHECK(wfcKeyAt(spots, 7, 13, &n) == 'S');
    CHECK(wfcKeyAt(spots, 7, 25, &n) == 'N');  // inside the node's field
    CHECK(n == 1);
    CHECK(wfcKeyAt(spots, 7, 16, &n) == 0);    // between the boxes
    CHECK(wfcKeyAt(spots, 7, 19, &n) == 0);    // the box line
    CHECK(wfcKeyAt(spots, 7, 40, &n) == 0);    // past the field
}

TEST_CASE("long art is drawn in pieces with the same result") {
    WfcSnapshot s;
    s.nodes.push_back({1, "Rev", ""});
    std::string art = "|XY01,01|CT\r\n";
    for (int i = 0; i < 40; i++) art += "0123456789 0123456789 0123456789 \r\n";
    art += "|XY01,05[|KS] @NA##########@";
    AnsiTerm term(80, 30);
    auto     spots = drawWfc(art, s, term);
    CHECK(wfcKeyAt(spots, 4, 1, nullptr) == 'S');
    CHECK(term.cell(4, 4).ch == '1');
}

TEST_CASE("a control byte in the art is not a hotspot") {
    WfcSnapshot s;
    AnsiTerm    term(45, 24);
    auto        spots = drawWfc(std::string("a\x01" "b\x05" "c[|KU]"), s, term);
    CHECK(spots.size() == 1);
    CHECK(term.cell(0, 1).ch == ' ');
    CHECK(wfcKeyAt(spots, 0, 7, nullptr) == 'U');
}

TEST_CASE("the built-in CYD WFC fills 45x24 without scrolling, every key tappable") {
    WfcSnapshot s;
    s.bbsName = "AXIS UNDERGROUND";
    for (int i = 1; i <= 4; i++) s.nodes.push_back({i, i == 2 ? "Rev" : "", ""});
    AnsiTerm term(45, 24);
    auto     spots = drawWfc(kWfcCydAns, s, term);
    std::string top, keys;
    for (int c = 0; c < 45; c++) top += (char)term.cell(0, c).ch;
    CHECK(top.find("AXIS UNDERGROUND") != std::string::npos);  // row 0 is still the title: nothing scrolled
    for (const auto& h : spots)
        if (h.node == 0) keys += h.key;
    CHECK(keys == " SU#BFLEA!");
    int n = 0;
    for (const auto& h : spots) n += h.node > 0;
    CHECK(n == 4);
    int node = 0;
    for (const auto& h : spots)
        if (h.node == 2) CHECK(wfcKeyAt(spots, h.row, 30, &node) == 'N');
    CHECK(node == 2);
    for (const auto& h : spots)  // every key's label: the cell 4 past the key ("[S] settings" -> 'e') is still the key
        if (h.node == 0 && h.key != ' ') CHECK(wfcKeyAt(spots, h.row, h.col + 4, nullptr) == h.key);
    for (const auto& h : spots)
        if (h.node == 0 && h.key == ' ') CHECK(wfcKeyAt(spots, h.row, h.col + 6, nullptr) == ' ');  // "local logon"
}
