// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// File copying over the USB serial port, the same line protocol as the CYD Demoscene:
//   @PUT path size  then 512 byte chunks, each acked "@K", then "@DONE <byte sum>"
//   @LS dir  ("name size" lines, "@END")   @RM path   @MKDIR path   @BAUD n  (each answers @OK/@ERR)
// Paths are on the BBS storage ("/doors/lord/LORD.EXE" lands on the SD card, or internal flash).
#pragma once
#include <Arduino.h>

bool serialFileCommand(const String& cmd);  // true if it was one of these commands
