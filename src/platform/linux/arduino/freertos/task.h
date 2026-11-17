// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#pragma once
#include "freertos/FreeRTOS.h"

void vTaskDelay(TickType_t ticks);
// A thread's stack on Linux is megabytes: report a fixed, roomy high-water mark (in words, as FreeRTOS does).
inline UBaseType_t  uxTaskGetStackHighWaterMark(TaskHandle_t) { return 16384; }
inline TaskHandle_t xTaskGetHandle(const char*) { return nullptr; }  // no ESP-IDF tasks here
