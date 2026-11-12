/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 Koen De Vleeschauwer */

/*
  u8g2_circle.c: u8g2_DrawCircle() and u8g2_DrawDisc() of u8g2
  (csrc/u8g2_circle.c), here anti-aliased by coverage, in integers: no
  square root, no division.

  A pixel at (dx, dy) from the center, a pixel center as in u8g2, is at
  distance d = sqrt(dx^2 + dy^2) - rad from the circle (d < 0 inside). The
  coverage, as a function of d:
    circle, 1 pixel wide   1 - |d|      level 3, 2, 1 for |d| < 1/6, 1/2, 5/6
    disc                   1/2 - d      level 3, 2, 1 for d < -1/3, 0, 1/3
  rounded to the 4 levels of 2 bits per pixel. The level changes only at
  these distances, compared squared and scaled to integers:
    circle   36 (dx^2 + dy^2)  against  (6 rad -+ k)^2, k = 1, 3, 5
    disc      9 (dx^2 + dy^2)  against  (3 rad - 1)^2, (3 rad)^2, (3 rad + 1)^2
  So the level of a pixel depends on its position only, not on the order
  of drawing.

  Each pixel row dy visits the pixels of the edge only: their first and
  last dx shrink as dy grows, and follow it with a small loop. A disc
  fills its inside with spans at level 3 (u8g2_DrawBox()).

  option, as in u8g2: U8G2_DRAW_UPPER_RIGHT, U8G2_DRAW_UPPER_LEFT,
  U8G2_DRAW_LOWER_LEFT, U8G2_DRAW_LOWER_RIGHT, U8G2_DRAW_ALL. The pixels go
  through u8g2_DrawPixelLevel(): clipped, a pixel keeps the larger level,
  so where the quarters meet nothing is drawn twice over.

  Coordinates are unsigned, as in u8g2; a value that wrapped below 0 is
  read as signed and clipped.
*/

#include "u8g2.h"

/* the level of a pixel of the circle: s = 36 (dx^2 + dy^2), r6 = 6 rad */
static uint8_t u8g2_circle_level(int32_t s, int32_t r6)
{
  int32_t k, lo, hi;

  for ( k = 1; k <= 5; k += 2 )
  {
    lo = r6 - k;
    hi = r6 + k;
    if ( (lo < 0 || lo * lo < s) && s < hi * hi )
      return (uint8_t)(3 - k / 2);       /* k 1, 3, 5: level 3, 2, 1 */
  }
  return 0;
}

/* the level of a pixel of the disc: s = 9 (dx^2 + dy^2), r3 = 3 rad */
static uint8_t u8g2_disc_level(int32_t s, int32_t r3)
{
  if ( r3 > 1 && s < (r3 - 1) * (r3 - 1) )
    return 3;
  if ( s < r3 * r3 )
    return 2;
  if ( s < (r3 + 1) * (r3 + 1) )
    return 1;
  return 0;
}

/* a pixel at (dx, dy) from the center, in the quarters of option */
static void u8g2_circle_pixel(u8g2_t *u8g2, int16_t x0, int16_t y0, int16_t dx, int16_t dy,
    uint8_t level, uint8_t option)
{
  if ( option & U8G2_DRAW_UPPER_RIGHT )
    u8g2_DrawPixelLevel(u8g2, (u8g2_uint_t)(x0 + dx), (u8g2_uint_t)(y0 - dy), level);
  if ( option & U8G2_DRAW_UPPER_LEFT )
    u8g2_DrawPixelLevel(u8g2, (u8g2_uint_t)(x0 - dx), (u8g2_uint_t)(y0 - dy), level);
  if ( option & U8G2_DRAW_LOWER_RIGHT )
    u8g2_DrawPixelLevel(u8g2, (u8g2_uint_t)(x0 + dx), (u8g2_uint_t)(y0 + dy), level);
  if ( option & U8G2_DRAW_LOWER_LEFT )
    u8g2_DrawPixelLevel(u8g2, (u8g2_uint_t)(x0 - dx), (u8g2_uint_t)(y0 + dy), level);
}

void u8g2_DrawCircle(u8g2_t *u8g2, u8g2_uint_t x0, u8g2_uint_t y0, u8g2_uint_t rad, uint8_t option)
{
  int16_t xc = (int16_t)x0, yc = (int16_t)y0;
  int32_t r6 = 6 * (int32_t)rad;
  int32_t lo = r6 - 5, hi = (r6 + 5) * (r6 + 5);
  int32_t a = (int32_t)rad + 1, b = (int32_t)rad + 1, dy, dx, s;
  uint8_t level;

  for ( dy = 0; ; dy++ )
  {
    /* b: the last dx inside the outer edge, 36 (dx^2 + dy^2) < (6 rad + 5)^2 */
    while ( b >= 0 && 36 * (b * b + dy * dy) >= hi )
      b--;
    if ( b < 0 )
      break;
    /* a: the first dx outside the inner edge, 36 (dx^2 + dy^2) > (6 rad - 5)^2 */
    while ( a > 0 && (lo < 0 || 36 * ((a - 1) * (a - 1) + dy * dy) > lo * lo) )
      a--;
    for ( dx = a; dx <= b; dx++ )
    {
      s = 36 * (dx * dx + dy * dy);
      level = u8g2_circle_level(s, r6);
      if ( level )
        u8g2_circle_pixel(u8g2, xc, yc, (int16_t)dx, (int16_t)dy, level, option);
    }
  }
}

void u8g2_DrawDisc(u8g2_t *u8g2, u8g2_uint_t x0, u8g2_uint_t y0, u8g2_uint_t rad, uint8_t option)
{
  int16_t xc = (int16_t)x0, yc = (int16_t)y0;
  int32_t r3 = 3 * (int32_t)rad;
  int32_t in = (r3 - 1) * (r3 - 1), out = (r3 + 1) * (r3 + 1);
  int32_t xin = (int32_t)rad, xout = (int32_t)rad + 1, dy, dx;
  uint8_t level;
  u8g2_uint_t w;

  if ( r3 <= 1 )
    xin = -1;
  for ( dy = 0; ; dy++ )
  {
    /* xout: the last dx with some coverage, 9 (dx^2 + dy^2) < (3 rad + 1)^2 */
    while ( xout >= 0 && 9 * (xout * xout + dy * dy) >= out )
      xout--;
    if ( xout < 0 )
      break;
    /* xin: the last dx at level 3, 9 (dx^2 + dy^2) < (3 rad - 1)^2 */
    while ( xin >= 0 && 9 * (xin * xin + dy * dy) >= in )
      xin--;
    if ( xin >= 0 )
    {
      w = (u8g2_uint_t)(xin + 1);
      if ( option & U8G2_DRAW_UPPER_RIGHT )
        u8g2_DrawBox(u8g2, (u8g2_uint_t)xc, (u8g2_uint_t)(yc - dy), w, 1);
      if ( option & U8G2_DRAW_UPPER_LEFT )
        u8g2_DrawBox(u8g2, (u8g2_uint_t)(xc - xin), (u8g2_uint_t)(yc - dy), w, 1);
      if ( option & U8G2_DRAW_LOWER_RIGHT )
        u8g2_DrawBox(u8g2, (u8g2_uint_t)xc, (u8g2_uint_t)(yc + dy), w, 1);
      if ( option & U8G2_DRAW_LOWER_LEFT )
        u8g2_DrawBox(u8g2, (u8g2_uint_t)(xc - xin), (u8g2_uint_t)(yc + dy), w, 1);
    }
    for ( dx = xin + 1; dx <= xout; dx++ )
    {
      level = u8g2_disc_level(9 * (dx * dx + dy * dy), r3);
      if ( level )
        u8g2_circle_pixel(u8g2, xc, yc, (int16_t)dx, (int16_t)dy, level, option);
    }
  }
}
