// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include "bbs/menufile.h"

#include "doctest.h"

using namespace axis::bbs;

TEST_CASE("a menu file reads ViSiON/3's fields, commands and lightbar items, and writes back the same") {
    const char* f = "title=Message Menu\nprompt1=|08[|15Message Menu|08] ->\nfallback=MAIN\nhotkey=0\n"
                    "cmd=R|RUN:READMSGS|*|0|Reading Messages\ncmd=/SYSOP|GOTO:ADMIN|s255|1|\n"
                    "bar=10,5,31,7,R,Read messages\n";
    MenuDef m;
    REQUIRE(parseMenu(f, m));
    CHECK(m.title == "Message Menu");
    CHECK(m.fallback == "MAIN");
    REQUIRE(m.cmds.size() == 2);
    CHECK(m.cmds[1].keys == "/SYSOP");
    CHECK(m.cmds[1].hidden);
    REQUIRE(m.bars.size() == 1);
    CHECK(m.bars[0].x == 10);
    CHECK(m.bars[0].text == "Read messages");
    MenuDef again;
    REQUIRE(parseMenu(formatMenu(m), again));
    CHECK(formatMenu(again) == formatMenu(m));
}

TEST_CASE("a menu file with no commands is refused") {
    MenuDef m;
    CHECK_FALSE(parseMenu("title=Empty\n", m));
    CHECK_FALSE(parseMenu("", m));
}
