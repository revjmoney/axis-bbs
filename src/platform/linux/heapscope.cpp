// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// HeapScope on Linux: the heap figures are fixed (Esp.h), so no scope ever sees a new low and nothing is logged.
#include "platform/cyd/heapscope.h"

namespace {
void (*hook_)(const std::string&) = nullptr;
}

void heapScopeHook(void (*log)(const std::string&)) { hook_ = log; }
void heapScopeFlush() {}

HeapScope::HeapScope(const char* name) : name_(name), before_(0) {}
HeapScope::~HeapScope() {}
