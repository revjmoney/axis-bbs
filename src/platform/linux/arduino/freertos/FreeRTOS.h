// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The bits of FreeRTOS the board's code names. The Linux program runs the board's loop on one thread, so a
// critical section has nothing to guard against.
#pragma once
#include <cstdint>

typedef uint32_t     TickType_t;
typedef int          BaseType_t;
typedef unsigned int UBaseType_t;
typedef void*        TaskHandle_t;
typedef struct {
    int owner;
} portMUX_TYPE;

#define portMUX_INITIALIZER_UNLOCKED {0}
#define portENTER_CRITICAL(mux) ((void)(mux))
#define portEXIT_CRITICAL(mux) ((void)(mux))
#define portMAX_DELAY ((TickType_t)0xffffffffu)
#define portTICK_PERIOD_MS 1
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))
#define pdFALSE 0
#define pdTRUE 1
#define pdFAIL 0
#define pdPASS 1

inline BaseType_t xPortGetCoreID(void) { return 1; }  // the board's loop runs on core 1
