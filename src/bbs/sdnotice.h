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

}  // namespace axis::bbs
