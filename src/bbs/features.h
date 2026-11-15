// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// Build switches for AXIS Micro BBS. Each AXIS_X is 1 (built in) unless the build
// sets it to 0 (tools/pio_features.py from AXIS_OFF or AXIS_PRESET; the list is
// installer/features.json). Selectable features default to 1 (AXIS_NETS agrees with
// the fallback in main.cpp); AXIS_GPIO is reserved and defaults to 0.
#pragma once

#ifndef AXIS_LORD
#define AXIS_LORD 1
#endif
#ifndef AXIS_SCRIPTS
#define AXIS_SCRIPTS 1
#endif
#ifndef AXIS_DOSPROMPT
#define AXIS_DOSPROMPT 1
#endif
#ifndef AXIS_FILES
#define AXIS_FILES 1
#endif
#ifndef AXIS_ANSIVIEW
#define AXIS_ANSIVIEW 1
#endif
#ifndef AXIS_TRANSFERS
#define AXIS_TRANSFERS 1
#endif
#ifndef AXIS_CLUSTER
#define AXIS_CLUSTER 1
#endif
#ifndef AXIS_MODEMLINK
#define AXIS_MODEMLINK 1
#endif
#ifndef AXIS_WEB
#define AXIS_WEB 1
#endif
#ifndef AXIS_NETS
#define AXIS_NETS 1
#endif
#ifndef AXIS_GPIO
#define AXIS_GPIO 0
#endif

// Dependencies (the "needs" lists in installer/features.json).
static_assert(!AXIS_ANSIVIEW || AXIS_FILES, "AXIS_ANSIVIEW needs AXIS_FILES");
static_assert(!AXIS_TRANSFERS || AXIS_FILES, "AXIS_TRANSFERS needs AXIS_FILES");

// 1 when the build has any feature that keeps its files on the SD card (the "needs_sd" ones in installer/features.json;
// tools/test_buildopts.py checks the two lists agree). Such a board without a card shows the SD card notice (sdnotice.h).
#define AXIS_NEEDS_SD (AXIS_LORD || AXIS_SCRIPTS || AXIS_FILES || AXIS_ANSIVIEW || AXIS_TRANSFERS || AXIS_NETS)
