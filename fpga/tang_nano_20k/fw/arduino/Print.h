/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 Koen De Vleeschauwer */

/* Print.h: the base class of Adafruit_GFX, text output through write() */

#ifndef PRINT_H
#define PRINT_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

class Print {
public:
  virtual size_t write(uint8_t c) = 0;
  virtual size_t write(const uint8_t *buf, size_t n)
  {
    size_t i;

    for (i = 0; i < n; i++)
      write(buf[i]);
    return n;
  }
  size_t write(const char *s) { return s ? write((const uint8_t *)s, strlen(s)) : 0; }
  size_t print(const char *s) { return write(s); }
  size_t println(const char *s) { return write(s) + write((uint8_t)'\n'); }
};

#endif
