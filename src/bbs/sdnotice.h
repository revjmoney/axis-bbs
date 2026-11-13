// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The SD card notice. A board built with a feature that keeps its files on the card (AXIS_NEEDS_SD, the "needs_sd"
// features in installer/features.json) and running without a card says so on both WFCs (touch and telnet/serial) and
// in @STATUS (sd=missing). A follower whose main has a card needs none: the main serves it everything.
#pragma once
#include "bbs/features.h"

namespace axis::bbs {

constexpr const char* kSdNotice = "YOU NEED AN SD CARD FOR FULL FUNCTIONS";

// hasCard: this board has its card. cardMain: it is a follower of a main that has one.
inline bool sdNoticeShown(bool hasCard, bool cardMain, bool builtWithSdFeature = AXIS_NEEDS_SD != 0) {
    return builtWithSdFeature && !hasCard && !cardMain;
}

// With the cluster built in, the notice waits while this board does not yet know whether it follows a main:
// before the cluster starts when a cluster key is saved (it will look for one), and while it is finding its main.
// A board with no key saved runs alone, so it knows at once.
inline bool sdNoticeWaits(bool clusterStarted, bool keySaved, bool findingMain) {
    return clusterStarted ? findingMain : keySaved;
}

}  // namespace axis::bbs
