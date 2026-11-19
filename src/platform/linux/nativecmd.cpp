// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The host's own @ commands, asked by main.cpp's runCommand after every board command (its AXIS_NATIVE line).
// None yet: every command is the board's.
#include <Arduino.h>

bool nativeCommand(const String& cmd, Print& out) {
    (void)cmd;
    (void)out;
    return false;
}
