/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 Koen De Vleeschauwer */

/*
  u8g2_line.c: u8g2_DrawLine() of u8g2 (csrc/u8g2_line.c), here anti-aliased:
  the line drawing of Xiaolin Wu, in integers, as Michael Abrash gave it
  (Dr. Dobb's Journal, June 1992): a 16-bit error accumulator, one division
  per line for the slope, then additions only.

  Along the major axis, each step sets two pixels across the line: the
  upper 2 bits of the accumulator, w = 0 .. 3, the position of the line
  between them, give level 3 - w to the one and w to the other; the two
  levels add up to 3, the foreground. With 2 bits per pixel the 4 levels
  are the codes of the frame buffer. Horizontal, vertical and 45 degree
  lines and the end points are level 3.

  The pixels go through u8g2_DrawPixelLevel(): clipped to the screen and
  the page, a pixel takes the larger of its level and the new one, so
  crossing lines and lines over text keep the stronger.

  Coordinates are unsigned, as in u8g2; a value that wrapped below 0 is
  read as signed and clipped.
*/

#include "u8g2.h"

void u8g2_DrawLine(u8g2_t *u8g2, u8g2_uint_t x1, u8g2_uint_t y1, u8g2_uint_t x2, u8g2_uint_t y2)
{
  int16_t x0 = (int16_t)x1, y0 = (int16_t)y1, xe = (int16_t)x2, ye = (int16_t)y2;
  int16_t dx, dy, xdir, t;
  uint16_t acc, adj, prev;
  uint8_t w;

  if ( y0 > ye )                         /* top to bottom */
  {
    t = y0; y0 = ye; ye = t;
    t = x0; x0 = xe; xe = t;
  }
  dx = (int16_t)(xe - x0);
  dy = (int16_t)(ye - y0);
  xdir = 1;
  if ( dx < 0 )
  {
    xdir = -1;
    dx = (int16_t)-dx;
  }

  u8g2_DrawPixelLevel(u8g2, (u8g2_uint_t)x0, (u8g2_uint_t)y0, 3);

  /* horizontal, vertical, diagonal: no anti-aliasing needed */
  if ( dy == 0 || dx == 0 || dx == dy )
  {
    while ( dx > 0 || dy > 0 )
    {
      if ( dx > 0 )
      {
        x0 = (int16_t)(x0 + xdir);
        dx--;
      }
      if ( dy > 0 )
      {
        y0++;
        dy--;
      }
      u8g2_DrawPixelLevel(u8g2, (u8g2_uint_t)x0, (u8g2_uint_t)y0, 3);
    }
    return;
  }

  acc = 0;
  if ( dy > dx )
  {
    /* y major: a step down each pixel row, x moves when acc rolls over */
    adj = (uint16_t)(((uint32_t)dx << 16) / (uint32_t)dy);
    while ( --dy )
    {
      prev = acc;
      acc = (uint16_t)(acc + adj);
      if ( acc <= prev )
        x0 = (int16_t)(x0 + xdir);
      y0++;
      w = (uint8_t)(acc >> 14);
      u8g2_DrawPixelLevel(u8g2, (u8g2_uint_t)x0, (u8g2_uint_t)y0, (uint8_t)(3 - w));
      u8g2_DrawPixelLevel(u8g2, (u8g2_uint_t)(x0 + xdir), (u8g2_uint_t)y0, w);
    }
  }
  else
  {
    /* x major: a step along x each pixel, y moves when acc rolls over */
    adj = (uint16_t)(((uint32_t)dy << 16) / (uint32_t)dx);
    while ( --dx )
    {
      prev = acc;
      acc = (uint16_t)(acc + adj);
      if ( acc <= prev )
        y0++;
      x0 = (int16_t)(x0 + xdir);
      w = (uint8_t)(acc >> 14);
      u8g2_DrawPixelLevel(u8g2, (u8g2_uint_t)x0, (u8g2_uint_t)y0, (uint8_t)(3 - w));
      u8g2_DrawPixelLevel(u8g2, (u8g2_uint_t)x0, (u8g2_uint_t)(y0 + 1), w);
    }
  }
  u8g2_DrawPixelLevel(u8g2, (u8g2_uint_t)xe, (u8g2_uint_t)ye, 3);
}
