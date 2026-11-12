// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include "doctest.h"
#include "bbs/features.h"

// Built with no switches (AXIS_OFF empty) every selectable feature is on and the
// reserved ones are off. A build with AXIS_OFF set skips this check.
#ifndef AXIS_TEST_SWITCHED
TEST_CASE("features: defaults") {
    CHECK(AXIS_LORD == 1);
    CHECK(AXIS_SCRIPTS == 1);
    CHECK(AXIS_DOSPROMPT == 1);
    CHECK(AXIS_FILES == 1);
    CHECK(AXIS_ANSIVIEW == 1);
    CHECK(AXIS_TRANSFERS == 1);
    CHECK(AXIS_CLUSTER == 1);
    CHECK(AXIS_MODEMLINK == 1);
    CHECK(AXIS_WEB == 1);
    CHECK(AXIS_NETS == 0);
    CHECK(AXIS_GPIO == 0);
}
#endif
