// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// Names the code that pushed the byte heap to a new low: wrap a call in a HeapScope and, if the board's
// lowest-ever free size dropped inside it, one event line says where.
#pragma once
#include <string>

class HeapScope {
public:
    explicit HeapScope(const char* name);
    ~HeapScope();

private:
    const char* name_;
    unsigned    before_;
};

// The log sink (main.cpp sets it to logEvent). Lines made on core 0 (the web task) are held and
// delivered by heapScopeFlush() from the main loop, so the event log is only touched from core 1.
void heapScopeHook(void (*log)(const std::string&));
void heapScopeFlush();
