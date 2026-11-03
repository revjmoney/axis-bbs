// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// Joins a WiFi network the board already remembers (NVS namespace "net", keys s0..s4 / p0..p4,
// the same format the CYD Demoscene firmware writes), then sets the clock from NTP.
#pragma once
#include <Arduino.h>

bool        wifiJoinSaved();   // true once connected; skips gadget hotspots, strongest first
bool        wifiHasTime();
const char* wifiSsid();        // network joined, "" if none
