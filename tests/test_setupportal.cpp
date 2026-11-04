// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include "doctest.h"
#include "bbs/setupportal.h"

using namespace axis::bbs;

TEST_CASE("the hotspot page lists the networks it found and asks for a password without showing it") {
    std::string page = portalPage({"FBIVAN2", "Neighbor 5G"}, "");
    CHECK(page.find("AXIS-SETUP") != std::string::npos);
    CHECK(page.find("FBIVAN2") != std::string::npos);
    CHECK(page.find("Neighbor 5G") != std::string::npos);
    CHECK(page.find("type=\"password\"") != std::string::npos);
    CHECK(page.find("action=\"/join\"") != std::string::npos);
}

TEST_CASE("network names can't break the page") {
    std::string page = portalPage({"<script>alert(1)</script>", "Tom & Jerry's \"net\""}, "couldn't join <x>");
    CHECK(page.find("<script>") == std::string::npos);
    CHECK(page.find("&lt;script&gt;") != std::string::npos);
    CHECK(page.find("Tom &amp; Jerry&#39;s &quot;net&quot;") != std::string::npos);
    CHECK(page.find("couldn&#39;t join &lt;x&gt;") != std::string::npos);
}
