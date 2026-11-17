// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The Arduino and ESP-IDF surface the board's src/main.cpp uses, for the native Linux program. main.cpp is
// compiled unchanged against this; arduino_core.cpp has the bodies.
#pragma once
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "Esp.h"
#include "HardwareSerial.h"
#include "IPAddress.h"
#include "Print.h"
#include "Stream.h"
#include "WString.h"
#include "esp_attr.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// time: a monotonic clock from the program's start (millis wraps after 49 days, as on the board)
uint32_t millis();
uint32_t micros();
void     delay(uint32_t ms);
void     delayMicroseconds(uint32_t us);
void     yield();

// pins and the status light's PWM: a PC has none, so these do nothing
#define LOW 0x0
#define HIGH 0x1
#define INPUT 0x01
#define OUTPUT 0x03
#define PULLUP 0x04
#define INPUT_PULLUP 0x05
#define PULLDOWN 0x08
#define INPUT_PULLDOWN 0x09
inline void     pinMode(uint8_t, uint8_t) {}
inline void     digitalWrite(uint8_t, uint8_t) {}
inline int      digitalRead(uint8_t) { return LOW; }
inline double   ledcSetup(uint8_t, double freq, uint8_t) { return freq; }
inline void     ledcAttachPin(uint8_t, uint8_t) {}
inline void     ledcDetachPin(uint8_t) {}
inline void     ledcWrite(uint8_t, uint32_t) {}

template <typename T, typename L, typename H>
inline T constrain(T v, L lo, H hi) {
    return v < (T)lo ? (T)lo : v > (T)hi ? (T)hi : v;
}

extern "C" int esp_rom_printf(const char* fmt, ...) __attribute__((format(printf, 1, 2)));  // straight to stdout
