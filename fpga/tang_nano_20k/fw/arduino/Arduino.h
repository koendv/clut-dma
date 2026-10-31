/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 Koen De Vleeschauwer */

/*
  Arduino.h: the little of the Arduino core that Adafruit_GFX.cpp needs, to
  build Adafruit GFX without Arduino (Cortex-M1 and the simulation host).
  Built with -DARDUINO=100.
*/

#ifndef ARDUINO_H
#define ARDUINO_H

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define PROGMEM
#define radians(deg) ((deg) * (float)M_PI / 180.0f)

typedef bool boolean;
typedef uint8_t byte;

static inline void optimistic_yield(uint32_t us) { (void)us; }

/* a string in flash, on the AVR; here an ordinary string */
class __FlashStringHelper;

/* only what Adafruit_GFX::getTextBounds(const String &) uses */
class String {
public:
  String(const char *s = "") : s_(s) {}
  const char *c_str() const { return s_; }
  unsigned length() const { return (unsigned)strlen(s_); }

private:
  const char *s_;
};

#include "Print.h"

#endif
